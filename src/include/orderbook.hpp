#include <cstdint>
#include <cassert>
#include <algorithm>
#include <vector>
#include <iostream>
#include <iomanip>

static constexpr size_t MAX_ORDERS = 1'000;
static constexpr size_t PRICE_MIN = 800;
static constexpr size_t PRICE_MAX = 1200;
static constexpr size_t TICK_SIZE = 1;
static constexpr size_t NUM_LEVELS = (PRICE_MAX - PRICE_MIN) / TICK_SIZE + 1;

struct Order {
    size_t order_id_;
    size_t price_;
    size_t quantity_;
    Order* next_; // Orders will be stored in a linked list, one linked list per price level
};

struct Trade {
    size_t taker_order_id;
    size_t maker_order_id;
    size_t price;
    size_t quantity;
};


// All
struct OrderPool {
    Order pool_[MAX_ORDERS]; // Pre-allocate the order pool
    Order* next_free_; // Next available order pointer that can be used

    OrderPool() {
        for (size_t i = 0; i < MAX_ORDERS - 1; ++i) {
            pool_[i].next_ = &pool_[i + 1]; 
        }
        pool_[MAX_ORDERS - 1].next_ = nullptr;
        next_free_ = &pool_[0];
    }

    Order* allocate() noexcept {
        if (!next_free_) return nullptr;
        Order* order = next_free_;
        next_free_ = next_free_->next_;
        order->next_ = nullptr;
        return order;
    }

    void deallocate(Order* order) noexcept {
        order->next_ = next_free_;
        next_free_ = order;
    }

};

struct PriceLevel {
    size_t price_;
    size_t total_quantity_; // Total liquidity at this price level
    Order* first_; // First order that came in at this price level
    Order* last_; // Last order that came in at this price level
};

struct OrderBookSide {
    PriceLevel levels_[NUM_LEVELS]; // Pre-allocate memory for price levels
    OrderPool pool_;
    bool is_bid_; // Bid or ask side, determines which direction to sort for best price
    size_t best_price_index_; // Index directly to the best available price for the given side (bid/ask)

    OrderBookSide(bool is_bid) : is_bid_(is_bid) {
        best_price_index_ = NUM_LEVELS; // NUM_LEVELS means no available best price (empty order book)
        for (size_t i = 0; i < NUM_LEVELS; ++i) {
            levels_[i].price_ = PRICE_MIN + i * TICK_SIZE;
            levels_[i].total_quantity_ = 0;
            levels_[i].first_ = nullptr;
            levels_[i].last_ = nullptr;
        }
    }

    inline size_t price_to_index(size_t price) const noexcept {
        assert(price >= PRICE_MIN && price <= PRICE_MAX);
        return static_cast<size_t>((price - PRICE_MIN) / TICK_SIZE);
    }


    Order* add_order(size_t price, size_t quantity, size_t id) noexcept {
        size_t idx = price_to_index(price);
        assert(idx < NUM_LEVELS);

        Order* order = pool_.allocate();
        if (!order) return nullptr; // Cannot place order because no memory is available

        order->order_id_ = id;
        order->price_ = price;
        order->quantity_ = quantity;
        order->next_ = nullptr;

        PriceLevel& level = levels_[idx];
        if (!level.first_) {
            level.first_ = order;
            level.last_ = order;
        } else {
            level.last_->next_ = order;
            level.last_ = order;
        }
        level.total_quantity_ += quantity;
        is_bid_ ? update_best_bid_after_order(idx) : update_best_ask_after_order(idx); // Update the index for the best price level
        return order;
    }

    void update_best_bid_after_order(size_t price_idx) {
        if ((best_price_index_ == NUM_LEVELS) || (price_idx > best_price_index_)) {
            best_price_index_ = price_idx;
            return;
        }
    }

    void update_best_ask_after_order(size_t price_idx) {
        if ((best_price_index_ == NUM_LEVELS) || (price_idx < best_price_index_)) {
            best_price_index_ = price_idx;
            return;
        }
    }

    void update_best_bid_after_empty(size_t old_idx) noexcept {
        for (size_t i = old_idx; i-- > 0; ) {
            if (levels_[i].total_quantity_ > 0) {
                best_price_index_ = i;
                return;
            }
        }
        best_price_index_ = NUM_LEVELS;
    }

    void update_best_ask_after_empty(size_t old_idx) noexcept {
        for (size_t i = old_idx + 1; i < NUM_LEVELS; ++i) {
            if (levels_[i].total_quantity_ > 0) {
                best_price_index_ = i;
                return;
            }
        }
        best_price_index_ = NUM_LEVELS;
    }

    size_t match_buy(
        size_t incoming_price, size_t incoming_quantity, size_t incoming_id, std::vector<Trade>& trades
    ) noexcept {
        while (incoming_quantity > 0) {
            if (best_price_index_ == NUM_LEVELS) {
                break; // best_price_index_ == NUM_LEVELS means empty order book
            }
            PriceLevel* level = &levels_[best_price_index_];            

            if (!(level->price_ <= incoming_price)){
                break;
            }

            // match orders in FIFO order
            while (incoming_quantity > 0 && level->first_) {
                Order* maker = level->first_;
                size_t trade_quantity = std::min(maker->quantity_, incoming_quantity);

                trades.push_back(Trade{incoming_id, maker->order_id_, maker->price_, trade_quantity});

                maker->quantity_ -= trade_quantity;
                incoming_quantity -= trade_quantity;
                level->total_quantity_ -= trade_quantity;

                if (maker->quantity_ == 0) {
                    // remove maker from level
                    level->first_ = maker->next_;
                    if (!level->first_) {
                        level->last_ = nullptr;
                        update_best_ask_after_empty(best_price_index_); // Price level has been depleted, update best price level
                    }
                    pool_.deallocate(maker);
                }
            }
        }

        return incoming_quantity;
    }

    size_t match_sell(
        size_t incoming_price, size_t incoming_quantity, size_t incoming_id, std::vector<Trade>& trades
    ) noexcept {
        while (incoming_quantity > 0) {
            if (best_price_index_ == NUM_LEVELS) {
                break;
            }
            
            PriceLevel* level = &levels_[best_price_index_];
    
            if (!(level->price_ >= incoming_price)){
                break;
            }

            while (incoming_quantity > 0 && level->first_) {
                Order* maker = level->first_;
                size_t trade_quantity = std::min(maker->quantity_, incoming_quantity);

                trades.push_back(Trade{incoming_id, maker->order_id_, maker->price_, trade_quantity});

                maker->quantity_ -= trade_quantity;
                incoming_quantity -= trade_quantity;
                level->total_quantity_ -= trade_quantity;

                if (maker->quantity_ == 0) {
                    level->first_ = maker->next_;
                    if (!level->first_) {
                        level->last_ = nullptr;
                        update_best_bid_after_empty(best_price_index_);
                    }
                    pool_.deallocate(maker);
                }
            }
        }

        return incoming_quantity;
    }

    void print_side(const char* name) const {
        std::cout << "=== " << name << " ===\n";
        for (size_t i = 0; i < NUM_LEVELS; ++i) {
            const PriceLevel& level = levels_[i];
            if (level.total_quantity_ == 0) continue;

            std::cout << "Price " << level.price_ << " -> ";
            Order* cur = level.first_;
            while (cur) {
                std::cout << "[id=" << cur->order_id_ 
                          << ", qty=" << cur->quantity_ << "] ";
                cur = cur->next_;
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }
};

struct OrderBook {
    OrderBookSide bids;
    OrderBookSide asks;

    OrderBook() : bids(true), asks(false) {}

    void submit_order(
        size_t price, 
        size_t quantity, 
        size_t id, 
        bool is_bid,
        std::vector<Trade>& trades
    ) {
        trades.clear();
        if (quantity == 0) return;
        size_t remaining = quantity;

        if (is_bid) {
            size_t remaining = asks.match_buy(price, quantity, id, trades);
            if (remaining > 0) {
                bids.add_order(price, remaining, id);
            }
        } else {
            size_t remaining = bids.match_sell(price, quantity, id, trades);
            if (remaining > 0) {
                asks.add_order(price, remaining, id);
            }
        }
        return;
    }

    void print_book() const {
        bids.print_side("BIDS");
        asks.print_side("ASKS");
    }
};