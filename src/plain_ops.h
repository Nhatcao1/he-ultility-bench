#pragma once

#include "csv_loader.h"

#include <algorithm>
#include <cstddef>
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

template <typename Worker>
inline void parallel_for_chunks(std::size_t rows, std::size_t thread_count, Worker worker) {
    const std::size_t threads = effective_thread_count(thread_count, rows);
    if (threads == 1) {
        worker(0, rows);
        return;
    }

    std::vector<std::thread> workers;
    workers.reserve(threads);
    const std::size_t base_chunk = rows / threads;
    const std::size_t remainder = rows % threads;
    std::size_t start = 0;

    for (std::size_t thread_index = 0; thread_index < threads; ++thread_index) {
        const std::size_t extra = thread_index < remainder ? 1 : 0;
        const std::size_t end = start + base_chunk + extra;
        workers.emplace_back([start, end, &worker]() {
            worker(start, end);
        });
        start = end;
    }

    for (std::thread& thread : workers) {
        thread.join();
    }
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

// Plaintext vector addition baseline:
// this is the direct comparison target for OpenFHE EvalAdd.
inline std::vector<double> plaintext_vector_add_values(
    const Transactions& data,
    std::size_t thread_count) {
    std::vector<double> values(data.size());
    parallel_for_chunks(data.size(), thread_count, [&](std::size_t start, std::size_t end) {
        for (std::size_t i = start; i < end; ++i) {
            values[i] = data.x1[i] + data.x2[i];
        }
    });
    return values;
}

// Plaintext element-wise multiplication baseline:
// this is the direct comparison target for OpenFHE EvalMult.
inline std::vector<double> plaintext_vector_mul_values(
    const Transactions& data,
    std::size_t thread_count) {
    std::vector<double> values(data.size());
    parallel_for_chunks(data.size(), thread_count, [&](std::size_t start, std::size_t end) {
        for (std::size_t i = start; i < end; ++i) {
            values[i] = data.x1[i] * data.x2[i];
        }
    });
    return values;
}

// Plaintext linear score baseline:
// this mirrors the synthetic score formula used by the Python generator,
// without random noise. OpenFHE CKKS inference will compare against this shape.
inline std::vector<double> plaintext_linear_score_values(
    const Transactions& data,
    std::size_t thread_count) {
    std::vector<double> values(data.size());
    parallel_for_chunks(data.size(), thread_count, [&](std::size_t start, std::size_t end) {
        for (std::size_t i = start; i < end; ++i) {
            values[i] =
                0.30 * data.x1[i] +
                0.20 * data.x2[i] +
                0.10 * data.x3[i] +
                static_cast<double>(data.i1[i]) / 50'000.0 +
                static_cast<double>(data.i2[i]) / 50'000.0;
        }
    });
    return values;
}

// Reduction baseline for a single packed vector.
// Later CKKS/BFV tests will implement this with rotations and additions.
inline double plaintext_sum_x1(const Transactions& data, std::size_t thread_count) {
    return parallel_sum(data.size(), thread_count, [&](std::size_t i) {
        return data.x1[i];
    });
}

// Query-like baseline:
// SQL form is SELECT SUM(amount) WHERE channel_id = 5.
// The HE version will use amount * mask_channel_5 before reduction.
inline double plaintext_masked_sum_channel_5(
    const Transactions& data,
    std::size_t thread_count) {
    return parallel_sum(data.size(), thread_count, [&](std::size_t i) {
        return data.amount[i] * static_cast<double>(data.mask_channel_5[i]);
    });
}

// Query-like count baseline:
// SQL form is SELECT COUNT(*) WHERE channel_id = 5.
inline double plaintext_masked_count_channel_5(
    const Transactions& data,
    std::size_t thread_count) {
    return parallel_sum(data.size(), thread_count, [&](std::size_t i) {
        return static_cast<double>(data.mask_channel_5[i]);
    });
}

// Query-like average baseline:
// SQL form is SELECT AVG(amount) WHERE channel_id = 5.
inline double plaintext_masked_avg_amount_channel_5(
    const Transactions& data,
    std::size_t thread_count) {
    const double sum = plaintext_masked_sum_channel_5(data, thread_count);
    const double count = plaintext_masked_count_channel_5(data, thread_count);
    return count == 0.0 ? 0.0 : sum / count;
}
