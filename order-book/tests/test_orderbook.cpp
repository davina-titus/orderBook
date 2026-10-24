// Unit tests for the order book. Uses a tiny built-in test harness so the
// project has no external dependencies.

#include "orderbook/OrderBook.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ob;

namespace {

int failures = 0;
std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> tests;
    return tests;
}

struct Register {
    Register(const std::string& name, std::function<void()> fn) {
        registry().emplace_back(name, std::move(fn));
    }
};

#define TEST(name) \
    void name();   \
    Register reg_##name(#name, name); \
    void name()

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++failures;                                                      \
            std::cerr << "    FAIL " << __FILE__ << ":" << __LINE__ << "  " \
                      << #cond << "\n";                                      \
        }                                                                    \
    } while (0)

#define CHECK_THROWS(expr)                                    \
    do {                                                      \
        bool threw = false;                                   \
        try { expr; } catch (const std::exception&) { threw = true; } \
        CHECK(threw);                                         \
    } while (0)

} // namespace

TEST(non_crossing_orders_rest_on_book) {
    OrderBook b;
    CHECK(b.addLimit(1, Side::Buy, 10000, 10).empty());
    CHECK(b.addLimit(2, Side::Sell, 10100, 5).empty());
    CHECK(b.bestBid() == 10000);
    CHECK(b.bestAsk() == 10100);
    CHECK(b.spread() == 100);
    CHECK(b.orderCount() == 2);
}

TEST(crossing_limit_fully_fills) {
    OrderBook b;
    b.addLimit(1, Side::Sell, 10000, 10);
    auto t = b.addLimit(2, Side::Buy, 10000, 10);
    CHECK(t.size() == 1);
    CHECK(t[0].qty == 10);
    CHECK(t[0].buyId == 2 && t[0].sellId == 1);
    CHECK(b.orderCount() == 0);
    CHECK(!b.bestBid() && !b.bestAsk());
}

TEST(trade_executes_at_resting_price) {
    OrderBook b;
    b.addLimit(1, Side::Sell, 10000, 10);
    auto t = b.addLimit(2, Side::Buy, 10500, 10);  // willing to pay more
    CHECK(t.size() == 1);
    CHECK(t[0].price == 10000);  // price improvement goes to the aggressor
}

TEST(partial_fill_rests_remainder) {
    OrderBook b;
    b.addLimit(1, Side::Sell, 10000, 4);
    auto t = b.addLimit(2, Side::Buy, 10000, 10);
    CHECK(t.size() == 1 && t[0].qty == 4);
    CHECK(b.bestBid() == 10000);
    CHECK(b.bidDepth(1)[0].totalQty == 6);
    CHECK(!b.bestAsk());
}

TEST(price_priority_best_price_first) {
    OrderBook b;
    b.addLimit(1, Side::Sell, 10200, 5);
    b.addLimit(2, Side::Sell, 10000, 5);
    b.addLimit(3, Side::Sell, 10100, 5);
    auto t = b.addMarket(4, Side::Buy, 5);
    CHECK(t.size() == 1 && t[0].sellId == 2 && t[0].price == 10000);
}

TEST(time_priority_fifo_within_level) {
    OrderBook b;
    b.addLimit(1, Side::Buy, 10000, 5);
    b.addLimit(2, Side::Buy, 10000, 5);
    b.addLimit(3, Side::Buy, 10000, 5);
    auto t = b.addLimit(4, Side::Sell, 10000, 7);
    CHECK(t.size() == 2);
    CHECK(t[0].buyId == 1 && t[0].qty == 5);
    CHECK(t[1].buyId == 2 && t[1].qty == 2);
    CHECK(b.bidDepth(1)[0].totalQty == 8);
    CHECK(b.bidDepth(1)[0].orderCount == 2);
}

TEST(aggressive_order_walks_multiple_levels) {
    OrderBook b;
    b.addLimit(1, Side::Sell, 10000, 3);
    b.addLimit(2, Side::Sell, 10100, 3);
    b.addLimit(3, Side::Sell, 10200, 3);
    auto t = b.addLimit(4, Side::Buy, 10100, 10);
    CHECK(t.size() == 2);
    CHECK(t[0].price == 10000 && t[1].price == 10100);
    CHECK(b.bestBid() == 10100);              // 4 left over, rests at limit
    CHECK(b.bidDepth(1)[0].totalQty == 4);
    CHECK(b.bestAsk() == 10200);
}

TEST(market_order_never_rests) {
    OrderBook b;
    b.addLimit(1, Side::Sell, 10000, 3);
    auto t = b.addMarket(2, Side::Buy, 10);
    CHECK(t.size() == 1 && t[0].qty == 3);
    CHECK(b.orderCount() == 0);
    CHECK(!b.contains(2));
}

TEST(market_order_on_empty_book_does_nothing) {
    OrderBook b;
    CHECK(b.addMarket(1, Side::Sell, 10).empty());
    CHECK(b.orderCount() == 0);
}

TEST(ioc_remainder_is_cancelled) {
    OrderBook b;
    b.addLimit(1, Side::Sell, 10000, 3);
    b.addLimit(2, Side::Sell, 10100, 3);
    auto t = b.addIOC(3, Side::Buy, 10000, 10);
    CHECK(t.size() == 1 && t[0].qty == 3);
    CHECK(!b.contains(3));
    CHECK(!b.bestBid());
    CHECK(b.bestAsk() == 10100);
}

TEST(cancel_removes_order_and_empty_level) {
    OrderBook b;
    b.addLimit(1, Side::Buy, 10000, 5);
    b.addLimit(2, Side::Buy, 9900, 5);
    CHECK(b.cancel(1));
    CHECK(b.bestBid() == 9900);
    CHECK(!b.cancel(1));      // already gone
    CHECK(!b.cancel(999));    // never existed
}

TEST(cancel_middle_of_queue_keeps_others) {
    OrderBook b;
    b.addLimit(1, Side::Sell, 10000, 1);
    b.addLimit(2, Side::Sell, 10000, 2);
    b.addLimit(3, Side::Sell, 10000, 3);
    CHECK(b.cancel(2));
    auto lvl = b.askDepth(1)[0];
    CHECK(lvl.totalQty == 4 && lvl.orderCount == 2);
    auto t = b.addMarket(4, Side::Buy, 4);
    CHECK(t.size() == 2 && t[0].sellId == 1 && t[1].sellId == 3);
}

TEST(reduce_keeps_time_priority) {
    OrderBook b;
    b.addLimit(1, Side::Buy, 10000, 10);
    b.addLimit(2, Side::Buy, 10000, 10);
    CHECK(b.reduce(1, 4));
    CHECK(b.bidDepth(1)[0].totalQty == 14);
    auto t = b.addMarket(3, Side::Sell, 4);
    CHECK(t.size() == 1 && t[0].buyId == 1);  // still first in line
    CHECK(!b.reduce(2, 20));  // increases are rejected
    CHECK(!b.reduce(2, 0));
}

TEST(filled_ids_can_be_reused_but_live_ids_cannot) {
    OrderBook b;
    b.addLimit(1, Side::Buy, 10000, 5);
    CHECK_THROWS(b.addLimit(1, Side::Sell, 20000, 5));
    b.addMarket(2, Side::Sell, 5);  // fills order 1
    CHECK(!b.contains(1));
    CHECK(b.addLimit(1, Side::Buy, 10000, 5).empty());
}

TEST(rejects_invalid_quantity_and_price) {
    OrderBook b;
    CHECK_THROWS(b.addLimit(1, Side::Buy, 10000, 0));
    CHECK_THROWS(b.addLimit(2, Side::Buy, 10000, -5));
    CHECK_THROWS(b.addLimit(3, Side::Buy, 0, 5));
    CHECK_THROWS(b.addIOC(4, Side::Buy, -1, 5));
    CHECK(b.orderCount() == 0);
}

TEST(depth_snapshot_is_sorted_best_first) {
    OrderBook b;
    b.addLimit(1, Side::Buy, 9900, 1);
    b.addLimit(2, Side::Buy, 10000, 2);
    b.addLimit(3, Side::Buy, 9800, 3);
    b.addLimit(4, Side::Sell, 10200, 4);
    b.addLimit(5, Side::Sell, 10100, 5);
    auto bids = b.bidDepth(10);
    auto asks = b.askDepth(1);
    CHECK(bids.size() == 3);
    CHECK(bids[0].price == 10000 && bids[1].price == 9900 && bids[2].price == 9800);
    CHECK(asks.size() == 1 && asks[0].price == 10100);
}

TEST(price_parsing_and_formatting) {
    CHECK(parsePrice("101.25") == 10125);
    CHECK(parsePrice("101.5") == 10150);
    CHECK(parsePrice("101") == 10100);
    CHECK(parsePrice("0.07") == 7);
    CHECK_THROWS(parsePrice("1.234"));
    CHECK_THROWS(parsePrice("abc"));
    CHECK_THROWS(parsePrice("1.2.3"));
    CHECK(formatPrice(10125) == "101.25");
    CHECK(formatPrice(7) == "0.07");
    CHECK(formatPrice(10100) == "101.00");
}

int main() {
    int failedTests = 0;
    for (auto& [name, fn] : registry()) {
        const int before = failures;
        try {
            fn();
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "    FAIL unexpected exception: " << e.what() << "\n";
        }
        const bool ok = failures == before;
        if (!ok) ++failedTests;
        std::cout << (ok ? "  PASS  " : "  FAIL  ") << name << "\n";
    }
    std::cout << "\n" << registry().size() - failedTests << "/" << registry().size()
              << " tests passed\n";
    return failedTests == 0 ? 0 : 1;
}
