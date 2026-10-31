#pragma once
#include "pool.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace parallel_stats {
// Welford updates with Chan-style pairwise merging. Public fields are read-only by convention.
struct Moments {
    std::uint64_t count = 0;
    long double mean = 0, m2 = 0;
    void add(double value) {
        if (!std::isfinite(value)) throw std::invalid_argument("observations must be finite");
        if (count == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("count overflow");
        const auto next = count + 1;
        const long double delta = static_cast<long double>(value) - mean;
        const auto new_mean = mean + delta / static_cast<long double>(next);
        const auto new_m2 = m2 + delta * (static_cast<long double>(value) - new_mean);
        if (!std::isfinite(new_mean) || !std::isfinite(new_m2)) throw std::overflow_error("moment overflow");
        count = next; mean = new_mean; m2 = new_m2;
    }
    void merge(const Moments& other) {
        if (other.count == 0) return;
        if (count == 0) { *this = other; return; }
        if (other.count > std::numeric_limits<std::uint64_t>::max() - count)
            throw std::overflow_error("count overflow");
        const auto total = count + other.count;
        const auto delta = other.mean - mean;
        const auto fraction = static_cast<long double>(other.count) / static_cast<long double>(total);
        const auto new_mean = mean + delta * fraction;
        const auto new_m2 = m2 + other.m2 + delta * delta * static_cast<long double>(count) * fraction;
        if (!std::isfinite(new_mean) || !std::isfinite(new_m2)) throw std::overflow_error("moment overflow");
        count = total; mean = new_mean; m2 = new_m2;
    }
    long double variance(bool sample = false) const {
        if (count <= static_cast<std::uint64_t>(sample)) throw std::domain_error("insufficient observations");
        return std::max(0.0L, m2 / static_cast<long double>(count - static_cast<std::uint64_t>(sample)));
    }
};
inline Moments summarize(std::span<const double> data) {
    Moments result;
    for (const auto value : data) result.add(value);
    return result;
}
inline Moments serial_chunks(std::span<const double> data, std::size_t chunk_size) {
    if (chunk_size == 0) throw std::invalid_argument("chunk_size must be positive");
    Moments result;
    for (std::size_t begin = 0; begin < data.size();) {
        const auto n = std::min(chunk_size, data.size() - begin);
        result.merge(summarize(data.subspan(begin, n)));
        begin += n;
    }
    return result;
}
inline Moments parallel_summarize(std::span<const double> data, std::size_t workers = 4,
                                  std::size_t chunk_size = 16384, std::size_t capacity = 8) {
    if (chunk_size == 0) throw std::invalid_argument("chunk_size must be positive");
    BoundedPool pool(workers, capacity);
    Moments result;
    std::deque<std::future<Moments>> in_flight;
    // Futures as well as queued tasks are bounded; merges are in input order.
    const auto limit = workers + capacity;
    auto consume = [&] { result.merge(in_flight.front().get()); in_flight.pop_front(); };
    for (std::size_t begin = 0; begin < data.size();) {
        const auto n = std::min(chunk_size, data.size() - begin);
        const auto chunk = data.subspan(begin, n);
        in_flight.push_back(pool.submit([chunk] { return summarize(chunk); }));
        if (in_flight.size() >= limit) consume();
        begin += n;
    }
    while (!in_flight.empty()) consume();
    return result;
}
}
