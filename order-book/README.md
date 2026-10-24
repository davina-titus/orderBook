# Limit Order Book & Matching Engine

A single-instrument limit order book written in modern C++17, with
price-time priority matching, the order types used on real exchanges, a
command-line replay tool, unit tests, and a latency benchmark.

```
LIMIT #6 B 250 @ 100.50
  TRADE     200 @ 100.50   buy #6  sell #2  (aggressor: BUY)
  TRADE      50 @ 100.50   buy #6  sell #3  (aggressor: BUY)
  +------------- BOOK -------------
  | ASK    101.00       300   1 order
  | ASK    100.50        50   1 order
  |   ----- spread 0.50 -----
  | BID    100.00       250   1 order
  | BID     99.75       400   1 order
  +--------------------------------
```

## Features

- **Order types:** limit, market, immediate-or-cancel (IOC)
- **Order management:** cancel, and reduce-quantity that keeps queue position
- **Price-time priority:** best price fills first; at the same price, the
  earliest order fills first (FIFO)
- **Exchange-style fills:** trades execute at the resting order's price, so
  an aggressive order gets any price improvement; partial fills and
  multi-level sweeps are supported
- **Integer prices:** prices are stored in ticks ($0.01) instead of
  floating point, so `100.10 + 0.20` never turns into `100.30000000000001`
- **Market data:** best bid/ask, spread, and top-N depth snapshots
- **Validation:** rejects duplicate live ids, non-positive quantities and
  prices, and malformed price strings

## Performance

1,000,000 operations of synthetic order flow (60% limit, 30% cancel,
10% market/IOC around a drifting mid price), Release build:

| Metric            | Result          |
|-------------------|-----------------|
| Throughput        | ~3.3M ops/sec   |
| Latency p50       | ~170 ns         |
| Latency p99       | ~900 ns         |
| Latency p99.9     | ~4.5 µs         |

Measured with `g++ 13 -O2` on a shared cloud Linux VM. Numbers vary by
machine; run `./build/bench` to reproduce on yours.

## Design

```
            bids (std::map, highest first)        asks (std::map, lowest first)
            ┌──────────┬──────────────────┐       ┌──────────┬──────────────────┐
  begin() → │ 100.00   │ [#5] → ...       │       │ 100.50   │ [#2] → [#3]      │ ← begin()
            │  99.75   │ [#4]             │       │ 101.00   │ [#1]             │
            └──────────┴──────────────────┘       └──────────┴──────────────────┘
                              ▲  FIFO std::list per price level  ▲
                              │                                  │
                  unordered_map<OrderId, iterator>  →  O(1) cancel / reduce
```

| Operation           | Complexity                                         |
|---------------------|----------------------------------------------------|
| Add resting order   | O(log L), where L = price levels on that side      |
| Cancel / reduce     | O(1) (plus O(log L) if a level empties)            |
| Each fill           | O(1) (plus O(log L) per level fully consumed)      |
| Best bid / ask      | O(1)                                               |

**Why these structures?**

- `std::map` keeps price levels sorted, so the best price is always
  `begin()`. Bids use `std::greater` so both sides read best-first.
- `std::list` per level gives FIFO time priority and, unlike a vector,
  iterators stay valid when other orders are removed.
- The id → iterator index means a cancel jumps straight to the order
  instead of scanning the book. Cancels are the most common message on
  real exchanges, so this matters.

**Possible next steps:** a flat array of price levels indexed by tick for
O(1) level lookup, an object pool to avoid per-order heap allocation, and
multiple instruments behind a symbol router.

## Build and run

Requires CMake 3.16+ and a C++17 compiler (g++, clang++ or MSVC).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

./build/tests                          # 17 unit tests
./build/replay examples/orders.txt     # replay a sample trading session
./build/bench                          # 1M-op throughput/latency benchmark
```

### Replay commands

Write your own session file, one command per line (`#` starts a comment):

```
LIMIT  <id> <B|S> <price> <qty>
MARKET <id> <B|S> <qty>
IOC    <id> <B|S> <price> <qty>
CANCEL <id>
REDUCE <id> <newQty>
PRINT  [levels]
```

## Project layout

```
include/orderbook/   Types.hpp, OrderBook.hpp   public API
src/                 OrderBook.cpp, Types.cpp   matching engine
apps/replay.cpp      command-line replay tool
bench/bench.cpp      throughput and latency benchmark
tests/               unit tests (no external dependencies)
examples/            sample order sessions
```

## Testing

The 17 unit tests cover resting orders, full and partial fills, price
priority, FIFO time priority, multi-level sweeps, market and IOC
behaviour, cancels at the front and middle of a queue, reduce keeping
priority, input validation, and price parsing. The suite and benchmark
also run clean under AddressSanitizer and UndefinedBehaviorSanitizer.

GitHub Actions builds the project and runs the tests on every push.

---

Built by [Davina Titus](https://github.com/davina-titus).
