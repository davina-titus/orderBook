#include "orderbook/OrderBook.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace ob {

void OrderBook::validateNew(OrderId id, Qty qty) const {
    if (qty <= 0) {
        throw std::invalid_argument("order " + std::to_string(id) +
                                    ": quantity must be positive");
    }
    if (index_.count(id) != 0) {
        throw std::invalid_argument("order " + std::to_string(id) +
                                    ": duplicate order id");
    }
}

template <typename Book>
void OrderBook::match(Order& incoming, Book& book, std::optional<Price> limit,
                      std::vector<Trade>& trades) {
    const bool isBuy = incoming.side == Side::Buy;

    while (incoming.qty > 0 && !book.empty()) {
        auto levelIt = book.begin();
        const Price levelPrice = levelIt->first;

        // Stop once the best opposite price no longer crosses our limit.
        if (limit) {
            const bool crosses = isBuy ? levelPrice <= *limit
                                       : levelPrice >= *limit;
            if (!crosses) break;
        }

        Level& level = levelIt->second;
        while (incoming.qty > 0 && !level.orders.empty()) {
            Order& resting = level.orders.front();
            const Qty fill = std::min(incoming.qty, resting.qty);

            trades.push_back(Trade{
                isBuy ? incoming.id : resting.id,
                isBuy ? resting.id : incoming.id,
                levelPrice,
                fill,
                incoming.id,
                incoming.side,
            });

            incoming.qty -= fill;
            resting.qty -= fill;
            level.totalQty -= fill;

            if (resting.qty == 0) {
                index_.erase(resting.id);
                level.orders.pop_front();
            }
        }

        if (level.orders.empty()) {
            book.erase(levelIt);
        }
    }
}

void OrderBook::rest(const Order& order) {
    auto insert = [&](auto& book) {
        Level& level = book[order.price];
        level.orders.push_back(order);
        level.totalQty += order.qty;
        index_.emplace(order.id,
                       Locator{order.side, order.price, std::prev(level.orders.end())});
    };
    if (order.side == Side::Buy) insert(bids_);
    else insert(asks_);
}

std::vector<Trade> OrderBook::addLimit(OrderId id, Side side, Price price, Qty qty) {
    validateNew(id, qty);
    if (price <= 0) {
        throw std::invalid_argument("order " + std::to_string(id) +
                                    ": price must be positive");
    }

    Order order{id, side, price, qty, nextSeq_++};
    std::vector<Trade> trades;
    if (side == Side::Buy) match(order, asks_, price, trades);
    else match(order, bids_, price, trades);

    if (order.qty > 0) rest(order);
    return trades;
}

std::vector<Trade> OrderBook::addMarket(OrderId id, Side side, Qty qty) {
    validateNew(id, qty);
    Order order{id, side, 0, qty, nextSeq_++};
    std::vector<Trade> trades;
    if (side == Side::Buy) match(order, asks_, std::nullopt, trades);
    else match(order, bids_, std::nullopt, trades);
    return trades;  // any unfilled quantity is dropped
}

std::vector<Trade> OrderBook::addIOC(OrderId id, Side side, Price price, Qty qty) {
    validateNew(id, qty);
    if (price <= 0) {
        throw std::invalid_argument("order " + std::to_string(id) +
                                    ": price must be positive");
    }
    Order order{id, side, price, qty, nextSeq_++};
    std::vector<Trade> trades;
    if (side == Side::Buy) match(order, asks_, price, trades);
    else match(order, bids_, price, trades);
    return trades;  // remainder is cancelled, never rests
}

bool OrderBook::cancel(OrderId id) {
    auto found = index_.find(id);
    if (found == index_.end()) return false;

    const Locator loc = found->second;
    auto remove = [&](auto& book) {
        auto levelIt = book.find(loc.price);
        Level& level = levelIt->second;
        level.totalQty -= loc.it->qty;
        level.orders.erase(loc.it);
        if (level.orders.empty()) book.erase(levelIt);
    };
    if (loc.side == Side::Buy) remove(bids_);
    else remove(asks_);

    index_.erase(found);
    return true;
}

bool OrderBook::reduce(OrderId id, Qty newQty) {
    auto found = index_.find(id);
    if (found == index_.end()) return false;

    Order& order = *found->second.it;
    if (newQty <= 0 || newQty >= order.qty) return false;

    const Qty delta = order.qty - newQty;
    order.qty = newQty;
    if (found->second.side == Side::Buy) bids_.at(order.price).totalQty -= delta;
    else asks_.at(order.price).totalQty -= delta;
    return true;
}

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::optional<Price> OrderBook::spread() const {
    auto bid = bestBid();
    auto ask = bestAsk();
    if (!bid || !ask) return std::nullopt;
    return *ask - *bid;
}

template <typename Book>
std::vector<LevelView> OrderBook::depth(const Book& book, std::size_t levels) {
    std::vector<LevelView> out;
    out.reserve(std::min(levels, book.size()));
    for (const auto& [price, level] : book) {
        if (out.size() == levels) break;
        out.push_back(LevelView{price, level.totalQty, level.orders.size()});
    }
    return out;
}

std::vector<LevelView> OrderBook::bidDepth(std::size_t levels) const {
    return depth(bids_, levels);
}

std::vector<LevelView> OrderBook::askDepth(std::size_t levels) const {
    return depth(asks_, levels);
}

} // namespace ob
