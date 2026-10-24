#include "orderbook/Types.hpp"

#include <cctype>
#include <stdexcept>

namespace ob {

Price parsePrice(const std::string& text) {
    if (text.empty()) throw std::invalid_argument("empty price");

    Price whole = 0;
    Price cents = 0;
    int decimals = 0;
    bool seenDot = false;
    bool seenDigit = false;

    for (char c : text) {
        if (c == '.') {
            if (seenDot) throw std::invalid_argument("bad price: " + text);
            seenDot = true;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            seenDigit = true;
            if (seenDot) {
                if (++decimals > 2) {
                    throw std::invalid_argument("price has more than 2 decimals: " + text);
                }
                cents = cents * 10 + (c - '0');
            } else {
                whole = whole * 10 + (c - '0');
            }
        } else {
            throw std::invalid_argument("bad price: " + text);
        }
    }
    if (!seenDigit) throw std::invalid_argument("bad price: " + text);
    if (decimals == 1) cents *= 10;  // "101.5" means 101.50
    return whole * 100 + cents;
}

std::string formatPrice(Price ticks) {
    const bool negative = ticks < 0;
    if (negative) ticks = -ticks;
    std::string cents = std::to_string(ticks % 100);
    if (cents.size() == 1) cents = "0" + cents;
    return (negative ? "-" : "") + std::to_string(ticks / 100) + "." + cents;
}

} // namespace ob
