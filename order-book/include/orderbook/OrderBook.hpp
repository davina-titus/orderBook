#pragma once

#include "orderbook/Types.hpp"

#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ob {

// A single-instrument limit order book with price-time priority matching.
//
// Data structures
//   - Each side is a std::map from price to a Level, sorted so that
//     begin() is always the best price (highest bid, lowest ask).
//   - Each Level holds a FIFO std::list of resting orders.
//   - An unordered_map from order id to its list iterator gives O(1)
//     cancels without searching the book.
//
// Complexity (L = number of price levels on a side)
//   add (resting)     O(log L)
//   cancel            O(1) amortised, plus O(log L) if the level empties
//   match             O(1) per fill, plus O(log L) per level consumed
//   best bid / ask    O(1)
class OrderBook {
public:
    // Limit order: matches against the opposite side up to `price`,
    // then rests any remaining quantity on the book.
    std::vector<Trade> addLimit(OrderId id, Side side, Price price, Qty qty);

    // Market order: matches at any price; unfilled quantity is discarded.
    std::vector<Trade> addMarket(OrderId id, Side side, Qty qty);

    // Immediate-or-cancel: matches up to `price`; remainder is discarded.
    std::vector<Trade> addIOC(OrderId id, Side side, Price price, Qty qty);

    // Removes a resting order. Returns false if the id is not on the book.
    bool cancel(OrderId id);

    // Reduces a resting order's quantity in place, keeping its time
    // priority. newQty must be > 0 and < current quantity. Returns false
    // if the id is not on the book or the new quantity is not a reduction.
    bool reduce(OrderId id, Qty newQty);

    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    std::optional<Price> spread() const;

    // Top `levels` price levels on each side, best first.
    std::vector<LevelView> bidDepth(std::size_t levels) const;
    std::vector<LevelView> askDepth(std::size_t levels) const;

    std::size_t orderCount() const { return index_.size(); }
    bool contains(OrderId id) const { return index_.count(id) != 0; }

private:
    struct Level {
        std::list<Order> orders;
        Qty totalQty = 0;
    };

    using BidMap = std::map<Price, Level, std::greater<Price>>;
    using AskMap = std::map<Price, Level, std::less<Price>>;

    struct Locator {
        Side side;
        Price price;
        std::list<Order>::iterator it;
    };

    void validateNew(OrderId id, Qty qty) const;

    // Matches `incoming` against `book` while prices cross. When
    // `limit` is empty the order is a market order and crosses at any price.
    template <typename Book>
    void match(Order& incoming, Book& book, std::optional<Price> limit,
               std::vector<Trade>& trades);

    void rest(const Order& order);

    template <typename Book>
    static std::vector<LevelView> depth(const Book& book, std::size_t levels);

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, Locator> index_;
    SeqNo nextSeq_ = 0;
};

} // namespace ob
