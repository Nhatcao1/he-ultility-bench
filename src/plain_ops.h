#pragma once

#include "csv_loader.h"

#include <cstddef>
#include <numeric>
#include <vector>

inline double checksum(const std::vector<double>& values) {
    double checksum = 0.0;
    for (const double value : values) {
        checksum += value;
    }
    return checksum;
}

// Plaintext vector addition baseline:
// this is the direct comparison target for OpenFHE EvalAdd.
inline std::vector<double> plaintext_vector_add_values(const Transactions& data) {
    std::vector<double> values;
    values.reserve(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        values.push_back(data.x1[i] + data.x2[i]);
    }
    return values;
}

// Plaintext linear score baseline:
// this mirrors the synthetic score formula used by the Python generator,
// without random noise. OpenFHE CKKS inference will compare against this shape.
inline std::vector<double> plaintext_linear_score_values(const Transactions& data) {
    std::vector<double> values;
    values.reserve(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        values.push_back(
            0.30 * data.x1[i] +
            0.20 * data.x2[i] +
            0.10 * data.x3[i] +
            static_cast<double>(data.i1[i]) / 50'000.0 +
            static_cast<double>(data.i2[i]) / 50'000.0);
    }
    return values;
}

// Query-like baseline:
// SQL form is SELECT SUM(amount) WHERE channel_id = 5.
// The HE version will use amount * mask_channel_5 before reduction.
inline double plaintext_masked_sum_channel_5(const Transactions& data) {
    double sum = 0.0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        sum += data.amount[i] * static_cast<double>(data.mask_channel_5[i]);
    }
    return sum;
}
