#pragma once

#include <cstdint>
#include <string>

namespace ob {

// Prices are stored as integer ticks (1 tick = $0.01) to avoid
// floating-point rounding errors when comparing and matching prices.
using Price   = std::int64_t;
using Qty     = std::int64_t;
using OrderId = std::uint64_t;
using SeqNo   = std::uint64_t;

enum class Side { Buy, Sell };

struct Order {
    OrderId id;
    Side    side;
    Price   price;   // ticks
    Qty     qty;     // remaining quantity
    SeqNo   seq;     // arrival sequence number, used for time priority
};

struct Trade {
    OrderId buyId;
    OrderId sellId;
    Price   price;   // always the resting order's price
    Qty     qty;
    OrderId aggressorId;
    Side    aggressorSide;
};

// One price level in a depth snapshot.
struct LevelView {
    Price price;
    Qty   totalQty;
    std::size_t orderCount;
};

inline const char* toString(Side s) { return s == Side::Buy ? "BUY" : "SELL"; }

// Converts a decimal price string such as "101.25" into ticks (10125).
// Throws std::invalid_argument on malformed input or more than 2 decimals.
Price parsePrice(const std::string& text);

// Formats ticks back into a decimal string, e.g. 10125 -> "101.25".
std::string formatPrice(Price ticks);

} // namespace ob
