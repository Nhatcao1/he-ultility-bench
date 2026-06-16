#pragma once

#include "csv_loader.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

inline double checksum(const std::vector<double>& values) {
    double checksum = 0.0;
    for (const double value : values) {
        checksum += value;
    }
    return checksum;
}

inline std::size_t effective_thread_count(std::size_t requested, std::size_t rows) {
    if (rows == 0) {
        return 1;
    }
    return std::max<std::size_t>(1, std::min(requested, rows));
}

template <typename ValueFn>
inline double parallel_sum(
    std::size_t rows,
    std::size_t thread_count,
    ValueFn value_fn) {
    const std::size_t threads = effective_thread_count(thread_count, rows);
    std::vector<double> partials(threads, 0.0);

    if (threads == 1) {
        double sum = 0.0;
        for (std::size_t i = 0; i < rows; ++i) {
            sum += value_fn(i);
        }
        return sum;
    }

    const std::size_t base_chunk = rows / threads;
    const std::size_t remainder = rows % threads;
    std::vector<std::thread> workers;
    workers.reserve(threads);
    std::size_t start = 0;

    for (std::size_t thread_index = 0; thread_index < threads; ++thread_index) {
        const std::size_t extra = thread_index < remainder ? 1 : 0;
        const std::size_t end = start + base_chunk + extra;
        workers.emplace_back([start, end, thread_index, &partials, &value_fn]() {
            double sum = 0.0;
            for (std::size_t i = start; i < end; ++i) {
                sum += value_fn(i);
            }
            partials[thread_index] = sum;
        });
        start = end;
    }

    for (std::thread& thread : workers) {
        thread.join();
    }

    return checksum(partials);
}

// Plain C++ baseline for:
//   SELECT SUM(amount) FROM transactions;
// CSV loading is timed separately by main.cpp; this function is compute only.
inline double plaintext_sum_amount(const Transactions& data, std::size_t thread_count) {
    return parallel_sum(data.size(), thread_count, [&](std::size_t i) {
        return data.amount[i];
    });
}

// Plain C++ baseline for:
//   SELECT SUM(amount * risk_weight)
// after customer_id -> risk_weight lookup has been expanded into a vector.
inline double plaintext_weighted_sum_amount_risk(
    const Transactions& data,
    std::size_t thread_count) {
    if (data.risk_weight_by_row.size() != data.size()) {
        throw std::runtime_error(
            "risk_weight_by_row is missing; load customers.csv before weighted benchmarks");
    }

    return parallel_sum(data.size(), thread_count, [&](std::size_t i) {
        return data.amount[i] * data.risk_weight_by_row[i];
    });
}

// Plain C++ baseline for:
//   SELECT amount FROM transactions WHERE amount > 5000;
// The benchmark schema stores scalar correctness values, so this returns the
// checksum/SUM of the selected output vector.
inline double plaintext_select_amount_gt_5000(
    const Transactions& data,
    std::size_t thread_count) {
    return parallel_sum(data.size(), thread_count, [&](std::size_t i) {
        return data.amount[i] > 5000.0 ? data.amount[i] : 0.0;
    });
}
