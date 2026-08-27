# Low-Latency Limit Order Book & Matching Engine

A high-performance, deterministic limit order book (LOB) matching engine implemented in modern C++20. Designed for ultra-low latency trading systems, the architecture focuses on mechanical sympathy: cache-line alignment, zero heap allocations on the critical execution path, O(1) price-level operations, and lock-free thread boundaries.

---

## Architectural Highlights

- **Zero-Allocation Critical Path:** Orders are allocated and managed in a contiguous, pre-allocated memory pool via an intrusive freelist, eliminating all dynamic `malloc`/`new` syscalls during matching.
- **O(1) Price-Time Priority Ladder:** Direct array indexing discretizes prices into ticks, enabling constant-time access to price levels. Price levels are maintained as intrusive doubly-linked lists to preserve FIFO time priority with O(1) insertions and removals.
- **O(1) Top-of-Book Discovery:** A custom 64-bit word dynamic bitset tracks occupied price levels and identifies best-bid and best-ask levels in constant time using hardware bit-scanning intrinsics (`__builtin_clzll` and `__builtin_ctzll`).
- **Lock-Free SPSC Ring Buffer:** Thread-safe order ingress using an acquire-release synchronized Single-Producer Single-Consumer queue, with cache-line padding (`alignas(64)`) to eliminate false sharing.
- **Differential Verification:** Differential fuzz testing against an unoptimized reference implementation (`std::map` / `std::list`) verifying identical trade executions, execution reports, and invariant preservation across randomized order flows and cancel storms.

---

## System Architecture

```
[ Ingress Network / Client ]
             |
             v
   +-------------------+
   |   Order Gateway   |  Validation & Sequence Numbering
   +-------------------+
             |
             v  (Lock-Free SPSC Queue: acquire/release, cache-line padded)
   +-------------------------------------------------------------+
   |                     Matching Engine Core                     |
   |                                                             |
   |   +-----------------------+     +-----------------------+   |
   |   |   Bids Price Ladder   |     |   Asks Price Ladder   |   |
   |   | (Array + DynamicBitset)     | (Array + DynamicBitset)   |   |
   |   +-----------------------+     +-----------------------+   |
   |               |                             |               |
   |               v                             v               |
   |   +-----------------------------------------------------+   |
   |   |       Intrusive PriceLevelList (FIFO Queues)        |   |
   |   +-----------------------------------------------------+   |
   |   |           Pre-allocated Order Memory Pool           |   |
   |   +-----------------------------------------------------+   |
   +-------------------------------------------------------------+
             |
             v
   +--------------------+
   | Market Data Handler|  Trade Notifications & Top-of-Book Feeds
   +--------------------+
```

---

## Core Components

### 1. Memory Pool (`OrderPool`)
- Pre-allocates a contiguous vector of `Order` structs at initialization.
- Uses an intrusive freelist indexed by 32-bit slot identifiers.
- Allocation and deallocation are strictly $O(1)$ operations requiring only index reassignment, completely bypassing OS heap allocation.

### 2. Array-Indexed Price Ladder (`PriceLadder`)
- Maps price directly to an array index: $\text{index} = (\text{price} - \text{base\_price}) / \text{tick\_size}$.
- Occupancy is mirrored in a `DynamicBitset`.
- Best bid is located via Count Leading Zeros (`CLZ`) on the highest non-zero 64-bit word.
- Best ask is located via Count Trailing Zeros (`CTZ`) on the lowest non-zero 64-bit word.

### 3. Intrusive Doubly-Linked List (`PriceLevelList`)
- `next` and `prev` pointers are embedded directly inside the `Order` struct.
- Allows orders to be removed from the middle of a price level in $O(1)$ during cancellations without scanning or reallocating list nodes.

### 4. Lock-Free Queue (`SPSCQueue`)
- Power-of-2 circular ring buffer using atomic head and tail pointers.
- Producer and consumer indices reside on separate 64-byte cache lines to prevent core-to-core cache bouncing (false sharing).
- Relaxed and acquire-release memory semantics guarantee sequential consistency for order delivery without mutex contention.

---

## Benchmark & Latency Measurements

Performance metrics collected using Google Benchmark on Linux (x86_64, Release `-O3 -march=native`):

### Order Matching Latency (HDR Histogram)

| Percentile | Latency |
| :--- | :--- |
| **p50 (Median)** | **139.7 ns** |
| **p90** | **270.5 ns** |
| **p99** | **448.8 ns** |
| **p99.9** | **1,011.9 ns** |
| **Mean** | **170.4 ns** |

### Throughput & Operation Benchmarks

| Benchmark Target | Metric | Rate |
| :--- | :--- | :--- |
| **Matching Engine Batch Processing** | Throughput | **1.47M orders/sec** |
| **Order Cancellation** | Throughput | **380.8M ops/sec** |
| **Memory Pool Alloc / Dealloc** | Latency | **0.25 ns / op** (3.96T ops/sec) |
| **SPSC Queue Push (Single-Thread)** | Throughput | **1.92G items/sec** |
| **SPSC Queue Pop (Single-Thread)** | Throughput | **882.9M items/sec** |
| **SPSC Queue Concurrent (2 Cores)** | Throughput | **232.6M items/sec** |

---

## Testing & Quality Assurance

The test suite includes 65 unit and integration tests covering all functional and edge-case behaviors:

1. **Unit Tests:** Component isolation tests for memory pools, bitset primitives, price ladder indexing, SPSC queues, matching engine core, order gateway validation, and market data reporting.
2. **Differential Fuzzing:** Simultaneous execution of thousands of pseudo-random orders (Limit, Market, IOC, FOK) and cancel storms against an independent reference engine, asserting exact trade and execution report equivalence.
3. **Invariant Checks:** Continuous validation of price ladder monotonicity, non-crossed book state, and bitset synchronicity after every state transition.

---

## Building and Running

### Requirements
- C++20 compliant compiler (`g++` >= 11 or `clang++` >= 13)
- CMake 3.20+ (optional, or use provided build script)
- Linux / POSIX environment

### Quick Build & Test (via build.sh)
The build script automatically sets up test harnesses and compiles optimized binaries:
```bash
./build.sh
```

Run test suite:
```bash
./build/matching_tests
```

Run latency and throughput benchmarks:
```bash
./build/matching_bench
```

### CMake Build
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run test suite
./matching_tests

# Run benchmarks
./matching_bench
```

### Sanitizer Targets (ASAN / TSAN)
```bash
cd build
make matching_tests_asan && ./matching_tests_asan
make matching_tests_tsan && ./matching_tests_tsan
```

---

## Repository Structure

```
.
├── include/
│   └── matching/
│       ├── types.h              # Core types (OrderID, Price, Quantity, Side)
│       ├── events.h             # IncomingOrder, MatchEvent structures
│       ├── order.h              # Order struct, PriceLevelList, OrderPool
│       ├── bitset.h             # DynamicBitset with CLZ/CTZ intrinsics
│       ├── price_ladder.h       # Array-indexed price ladder
│       ├── spsc_queue.h         # Lock-free SPSC circular queue
│       ├── matching_engine.h    # High-performance matching engine
│       ├── reference_engine.h   # Reference implementation for verification
│       ├── order_gateway.h      # Order validation and sequencing
│       └── market_data.h        # Top-of-book and trade event handler
├── src/
│   ├── order_pool.cpp
│   ├── price_ladder.cpp
│   ├── matching_engine.cpp
│   ├── reference_engine.cpp
│   ├── spsc_queue.cpp
│   ├── order_gateway.cpp
│   └── market_data.cpp
├── tests/
│   ├── test_order_pool.cpp
│   ├── test_bitset.cpp
│   ├── test_spsc_queue.cpp
│   ├── test_matching_engine.cpp
│   ├── test_reference_engine.cpp
│   ├── test_differential.cpp
│   ├── test_differential_debug.cpp
│   ├── test_order_gateway.cpp
│   └── test_market_data.cpp
├── benchmarks/
│   ├── bench_matching.cpp
│   ├── bench_spsc.cpp
│   ├── bench_order_pool.cpp
│   └── bench_latency_histogram.cpp
├── CMakeLists.txt
├── build.sh
└── README.md
```
