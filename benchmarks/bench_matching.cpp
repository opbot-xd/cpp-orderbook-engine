#include "matching/matching_engine.h"
#include <benchmark/benchmark.h>
#include <random>
#include <vector>

using namespace matching;

// Generate a realistic order mix for benchmarking
static std::vector<IncomingOrder> generate_orders(int n, uint32_t seed = 12345) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int64_t> price_dist(9950, 10050);
    std::uniform_int_distribution<int64_t> qty_dist(1, 100);
    std::uniform_real_distribution<double> market_prob(0.0, 1.0);

    std::vector<IncomingOrder> orders;
    orders.reserve(n);

    for (int i = 0; i < n; ++i) {
        IncomingOrder o;
        o.id = static_cast<OrderID>(i + 1);
        o.side = static_cast<Side>(side_dist(rng));

        bool is_market = market_prob(rng) < 0.2;
        o.type = is_market ? OrderType::Market : OrderType::Limit;
        o.price = is_market ? 0 : price_dist(rng);
        o.quantity = qty_dist(rng);
        o.timestamp = o.id;

        orders.push_back(o);
    }
    return orders;
}

// Benchmark: process a batch of mixed orders (no cancellation)
static void BM_MatchingEngine_Throughput(benchmark::State& state) {
    const int batch_size = static_cast<int>(state.range(0));
    auto orders = generate_orders(batch_size);

    for (auto _ : state) {
        MatchingEngine engine(20000, 1000000);
        for (const auto& order : orders) {
            engine.process_order(order);
        }
    }
    state.SetItemsProcessed(state.iterations() * batch_size);
}
BENCHMARK(BM_MatchingEngine_Throughput)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000);

// Benchmark: single order processing (latency measurement)
static void BM_MatchingEngine_SingleOrder(benchmark::State& state) {
    MatchingEngine engine(20000, 1000000);

    // Warm up the book with some resting orders
    for (int i = 0; i < 1000; ++i) {
        IncomingOrder o;
        o.id = static_cast<OrderID>(i + 1);
        o.side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        o.type = OrderType::Limit;
        o.price = (o.side == Side::Buy) ? 10000 : 10005;
        o.quantity = 100;
        o.timestamp = o.id;
        engine.process_order(o);
    }

    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> price_dist(9998, 10007);
    std::uniform_int_distribution<int64_t> qty_dist(1, 10);

    for (auto _ : state) {
        IncomingOrder o;
        o.id = 1000000 + state.iterations();
        o.side = Side::Buy;
        o.type = OrderType::Limit;
        o.price = price_dist(rng);
        o.quantity = qty_dist(rng);
        o.timestamp = o.id;

        auto event = engine.process_order(o);
        benchmark::DoNotOptimize(&event);
    }
}
BENCHMARK(BM_MatchingEngine_SingleOrder);

// Benchmark: cancel orders
static void BM_MatchingEngine_Cancel(benchmark::State& state) {
    const int n = 10000;
    MatchingEngine engine(20000, 1000000);

    // Pre-populate
    for (int i = 0; i < n; ++i) {
        IncomingOrder o;
        o.id = static_cast<OrderID>(i + 1);
        o.side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        o.type = OrderType::Limit;
        o.price = (o.side == Side::Buy) ? 10000 : 10005;
        o.quantity = 10;
        o.timestamp = o.id;
        engine.process_order(o);
    }

    for (auto _ : state) {
        // Cancel half of them
        for (int i = 0; i < n; i += 2) {
            engine.cancel_order(static_cast<OrderID>(i + 1));
        }
    }
    state.SetItemsProcessed(state.iterations() * (n / 2));
}
BENCHMARK(BM_MatchingEngine_Cancel);
