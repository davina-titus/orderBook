// replay: feeds a text file of order commands through the order book and
// prints every trade, plus book snapshots on request.
//
// Usage:  ./replay examples/orders.txt
//         ./replay < orders.txt
//
// Commands (one per line, '#' starts a comment):
//   LIMIT  <id> <B|S> <price> <qty>
//   MARKET <id> <B|S> <qty>
//   IOC    <id> <B|S> <price> <qty>
//   CANCEL <id>
//   REDUCE <id> <newQty>
//   PRINT  [levels]

#include "orderbook/OrderBook.hpp"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

ob::Side parseSide(const std::string& s) {
    if (s == "B" || s == "BUY") return ob::Side::Buy;
    if (s == "S" || s == "SELL") return ob::Side::Sell;
    throw std::invalid_argument("side must be B or S, got: " + s);
}

void printTrades(const std::vector<ob::Trade>& trades) {
    for (const auto& t : trades) {
        std::cout << "  TRADE  " << std::setw(6) << t.qty << " @ "
                  << ob::formatPrice(t.price) << "   buy #" << t.buyId
                  << "  sell #" << t.sellId << "  (aggressor: "
                  << ob::toString(t.aggressorSide) << ")\n";
    }
}

void printBook(const ob::OrderBook& book, std::size_t levels) {
    auto asks = book.askDepth(levels);
    auto bids = book.bidDepth(levels);

    auto row = [](const char* side, const ob::LevelView& lvl) {
        std::cout << "  | " << side << std::setw(10) << ob::formatPrice(lvl.price)
                  << std::setw(10) << lvl.totalQty << "   " << lvl.orderCount
                  << (lvl.orderCount == 1 ? " order\n" : " orders\n");
    };

    std::cout << "  +------------- BOOK -------------\n";
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) row("ASK", *it);
    if (auto s = book.spread()) {
        std::cout << "  |   ----- spread " << ob::formatPrice(*s) << " -----\n";
    } else {
        std::cout << "  |   ----- one side empty -----\n";
    }
    for (const auto& lvl : bids) row("BID", lvl);
    std::cout << "  +--------------------------------\n";
}

} // namespace

int main(int argc, char** argv) {
    std::ifstream file;
    if (argc > 1) {
        file.open(argv[1]);
        if (!file) {
            std::cerr << "could not open " << argv[1] << "\n";
            return 1;
        }
    }
    std::istream& in = argc > 1 ? static_cast<std::istream&>(file) : std::cin;

    ob::OrderBook book;
    std::string line;
    int lineNo = 0;
    int errors = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ss(line);
        std::string cmd;
        if (!(ss >> cmd)) continue;

        try {
            if (cmd == "LIMIT" || cmd == "IOC") {
                ob::OrderId id; std::string side, price; ob::Qty qty;
                if (!(ss >> id >> side >> price >> qty)) throw std::invalid_argument("usage: " + cmd + " <id> <B|S> <price> <qty>");
                std::cout << cmd << " #" << id << " " << side << " " << qty << " @ " << price << "\n";
                auto trades = cmd == "LIMIT"
                    ? book.addLimit(id, parseSide(side), ob::parsePrice(price), qty)
                    : book.addIOC(id, parseSide(side), ob::parsePrice(price), qty);
                printTrades(trades);
            } else if (cmd == "MARKET") {
                ob::OrderId id; std::string side; ob::Qty qty;
                if (!(ss >> id >> side >> qty)) throw std::invalid_argument("usage: MARKET <id> <B|S> <qty>");
                std::cout << "MARKET #" << id << " " << side << " " << qty << "\n";
                printTrades(book.addMarket(id, parseSide(side), qty));
            } else if (cmd == "CANCEL") {
                ob::OrderId id;
                if (!(ss >> id)) throw std::invalid_argument("usage: CANCEL <id>");
                std::cout << "CANCEL #" << id << (book.cancel(id) ? "" : "  (not found)") << "\n";
            } else if (cmd == "REDUCE") {
                ob::OrderId id; ob::Qty qty;
                if (!(ss >> id >> qty)) throw std::invalid_argument("usage: REDUCE <id> <newQty>");
                std::cout << "REDUCE #" << id << " -> " << qty
                          << (book.reduce(id, qty) ? "" : "  (rejected)") << "\n";
            } else if (cmd == "PRINT") {
                std::size_t levels = 5;
                ss >> levels;
                printBook(book, levels);
            } else {
                throw std::invalid_argument("unknown command: " + cmd);
            }
        } catch (const std::exception& e) {
            ++errors;
            std::cerr << "line " << lineNo << ": " << e.what() << "\n";
        }
    }
    return errors == 0 ? 0 : 2;
}
