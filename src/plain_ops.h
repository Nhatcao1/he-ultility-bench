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

// Plaintext element-wise multiplication baseline:
// this is the direct comparison target for OpenFHE EvalMult.
inline std::vector<double> plaintext_vector_mul_values(const Transactions& data) {
    std::vector<double> values;
    values.reserve(data.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        values.push_back(data.x1[i] * data.x2[i]);
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

// Reduction baseline for a single packed vector.
// Later CKKS/BFV tests will implement this with rotations and additions.
inline double plaintext_sum_x1(const Transactions& data) {
    return checksum(data.x1);
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

// Query-like count baseline:
// SQL form is SELECT COUNT(*) WHERE channel_id = 5.
inline double plaintext_masked_count_channel_5(const Transactions& data) {
    double count = 0.0;
    for (const int mask : data.mask_channel_5) {
        count += static_cast<double>(mask);
    }
    return count;
}

// Query-like average baseline:
// SQL form is SELECT AVG(amount) WHERE channel_id = 5.
inline double plaintext_masked_avg_amount_channel_5(const Transactions& data) {
    const double sum = plaintext_masked_sum_channel_5(data);
    const double count = plaintext_masked_count_channel_5(data);
    return count == 0.0 ? 0.0 : sum / count;
}
