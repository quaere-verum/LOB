#include "orderbook.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <chrono>

void print_trades(std::vector<Trade>& trades) {
    for (auto trade : trades) {
        std::cout << 
            "Taker Order ID: " << trade.taker_order_id << "\n" <<
            "Maker Order ID: " << trade.maker_order_id << "\n" <<
            "Price: " << trade.price << "\n" <<
            "Quantity: " << trade.quantity << "\n" <<
            "===============" << "\n";
    }
    
}

void performance_test() {
    OrderBook orderbook;

    constexpr size_t NUM_ORDERS = 1'000'000;
    std::mt19937_64 rng(5);

    std::uniform_int_distribution<size_t> price_dist(PRICE_MIN, PRICE_MAX);
    std::uniform_int_distribution<size_t> qty_dist(1, 10);
    std::bernoulli_distribution side_dist(0.5);

    std::vector<size_t> prices, quantities;
    std::vector<bool> is_buys;

    for (size_t i = 0; i < NUM_ORDERS; ++i) {
        prices.push_back(price_dist(rng));
        quantities.push_back(qty_dist(rng));
        is_buys.push_back(side_dist(rng));
    }

    std::vector<Trade> all_trades;
    all_trades.reserve(NUM_ORDERS);

    std::vector<Trade> trades;
    trades.reserve(16);

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < NUM_ORDERS; ++i) {

        orderbook.submit_order(prices[i], quantities[i], i, is_buys[i], trades);
        all_trades.insert(all_trades.end(), trades.begin(), trades.end());
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    std::cout << "Processed " << NUM_ORDERS << " orders in " 
              << elapsed.count() << " seconds.\n";
}

void order_test() {
    OrderBook orderbook;

    std::vector<Trade> all_trades;

    std::vector<Trade> trades;
    trades.reserve(16);

    orderbook.submit_order(900, 20, 0, true, trades);
    all_trades.insert(all_trades.end(), trades.begin(), trades.end());

    orderbook.submit_order(901, 10, 1, true, trades);
    all_trades.insert(all_trades.end(), trades.begin(), trades.end());

    orderbook.submit_order(900, 15, 2, false, trades);
    all_trades.insert(all_trades.end(), trades.begin(), trades.end());

    orderbook.submit_order(902, 10, 3, true, trades);
    all_trades.insert(all_trades.end(), trades.begin(), trades.end());
    
    orderbook.submit_order(902, 5, 4, false, trades);
    all_trades.insert(all_trades.end(), trades.begin(), trades.end());

    orderbook.print_book();
    print_trades(all_trades);
}

int main() {
    performance_test();
    order_test();
    return 0;
}
