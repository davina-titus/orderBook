// Benchmark: replays a synthetic order flow and reports throughput and
// per-operation latency percentiles.
//
// The flow is generated up front (fixed seed, so runs are reproducible)
// and only the order book calls are timed.
//   ~60% limit orders clustered around a drifting mid price
//   ~30% cancels of a random live order
//   ~10% marketable orders (market or IOC) that take liquidity
//
// Usage: ./bench [numOps]   (default 1,000,000)

#include "orderbook/OrderBook.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace {

enum class OpType { Limit, Cancel, Market, IOC };

struct Op {
    OpType type;
    ob::OrderId id;
    ob::Side side;
    ob::Price price;
    ob::Qty qty;
};

std::vector<Op> generate(std::size_t n, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::uniform_int_distribution<int> offset(1, 50);   // ticks away from mid
    std::uniform_int_distribution<int> lot(1, 10);
    std::normal_distribution<double> drift(0.0, 0.5);

    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<ob::OrderId> live;  // ids that may still be resting
    double mid = 10000.0;           // $100.00
    ob::OrderId nextId = 1;

    for (std::size_t i = 0; i < n; ++i) {
        mid = std::max(100.0, mid + drift(rng));
        const double r = u(rng);
        const ob::Side side = u(rng) < 0.5 ? ob::Side::Buy : ob::Side::Sell;
        const ob::Qty qty = lot(rng) * 100;

        if (r < 0.60 || live.empty()) {
            const ob::Price m = static_cast<ob::Price>(mid);
            const ob::Price px = side == ob::Side::Buy ? m - offset(rng) : m + offset(rng);
            ops.push_back({OpType::Limit, nextId, side, px, qty});
            live.push_back(nextId++);
        } else if (r < 0.90) {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const std::size_t k = pick(rng);
            ops.push_back({OpType::Cancel, live[k], side, 0, 0});
            live[k] = live.back();
            live.pop_back();
        } else if (r < 0.95) {
            ops.push_back({OpType::Market, nextId++, side, 0, qty});
        } else {
            const ob::Price m = static_cast<ob::Price>(mid);
            const ob::Price px = side == ob::Side::Buy ? m + 5 : m - 5;
            ops.push_back({OpType::IOC, nextId++, side, px, qty});
        }
    }
    return ops;
}

double percentile(std::vector<long long>& v, double p) {
    const std::size_t k = static_cast<std::size_t>(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return static_cast<double>(v[k]);
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    auto ops = generate(n, 42);

    ob::OrderBook book;
    std::vector<long long> latency;
    latency.reserve(n);
    std::size_t trades = 0, cancelsHit = 0;

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    for (const Op& op : ops) {
        const auto t0 = clock::now();
        switch (op.type) {
            case OpType::Limit:  trades += book.addLimit(op.id, op.side, op.price, op.qty).size(); break;
            case OpType::Market: trades += book.addMarket(op.id, op.side, op.qty).size(); break;
            case OpType::IOC:    trades += book.addIOC(op.id, op.side, op.price, op.qty).size(); break;
            case OpType::Cancel: cancelsHit += book.cancel(op.id); break;
        }
        latency.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - t0).count());
    }
    const double secs = std::chrono::duration<double>(clock::now() - start).count();

    std::printf("operations     %zu\n", n);
    std::printf("trades         %zu\n", trades);
    std::printf("cancels hit    %zu\n", cancelsHit);
    std::printf("resting orders %zu\n", book.orderCount());
    std::printf("throughput     %.2f M ops/sec\n", n / secs / 1e6);
    std::printf("latency p50    %.0f ns\n", percentile(latency, 0.50));
    std::printf("latency p99    %.0f ns\n", percentile(latency, 0.99));
    std::printf("latency p99.9  %.0f ns\n", percentile(latency, 0.999));
    return 0;
}
