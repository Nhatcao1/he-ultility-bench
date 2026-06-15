#pragma once

#include "csv_loader.h"

#include <cstddef>
#include <numeric>
#include <vector>

// Plaintext vector addition baseline:
// this is the direct comparison target for OpenFHE EvalAdd.
inline double plaintext_vector_add_checksum(const Transactions& data) {
    double checksum = 0.0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        checksum += data.x1[i] + data.x2[i];
    }
    return checksum;
}

// Plaintext linear score baseline:
// this mirrors the synthetic score formula used by the Python generator,
// without random noise. OpenFHE CKKS inference will compare against this shape.
inline double plaintext_linear_score_checksum(const Transactions& data) {
    double checksum = 0.0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        checksum +=
            0.30 * data.x1[i] +
            0.20 * data.x2[i] +
            0.10 * data.x3[i] +
            static_cast<double>(data.i1[i]) / 50'000.0 +
            static_cast<double>(data.i2[i]) / 50'000.0;
    }
    return checksum;
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

