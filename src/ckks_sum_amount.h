#pragma once

#ifdef UTILITY_BENCH_WITH_OPENFHE

#include "csv_loader.h"
#include "plain_ops.h"
#include "result_writer.h"
#include "timer.h"

#include "openfhe.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Conservative CKKS defaults for the first aggregation benchmark.
// Addition-only workloads do not consume multiplicative depth, but OpenFHE
// still needs a CKKS context. We keep depth small and record the parameters
// in the result CSV so later production-like sweeps can vary them explicitly.
struct CkksSumConfig {
    std::size_t requested_ring_dimension = 0;  // 0 lets OpenFHE choose from security/depth.
    std::size_t batch_size = 0;                // 0 means use all CKKS slots.
    std::size_t multiplicative_depth = 1;
    std::size_t scaling_mod_size = 50;
    std::size_t first_mod_size = 60;
};

inline std::size_t ceil_div(std::size_t numerator, std::size_t denominator) {
    return (numerator + denominator - 1) / denominator;
}

inline std::size_t ceil_log2(std::size_t value) {
    if (value <= 1) {
        return 0;
    }

    std::size_t power = 1;
    std::size_t result = 0;
    while (power < value) {
        power <<= 1U;
        ++result;
    }
    return result;
}

inline double divide_or_zero(double numerator, double denominator) {
    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

inline std::size_t configure_openfhe_threads(std::size_t requested_threads) {
#ifdef _OPENMP
    omp_set_num_threads(static_cast<int>(requested_threads));
    return static_cast<std::size_t>(std::max(1, omp_get_max_threads()));
#else
    return requested_threads;
#endif
}

inline BenchmarkResult openfhe_ckks_sum_amount(
    const Transactions& data,
    std::size_t thread_count,
    const CkksSumConfig& config = CkksSumConfig{}) {
    using lbcrypto::ADVANCEDSHE;
    using lbcrypto::CCParams;
    using lbcrypto::Ciphertext;
    using lbcrypto::CryptoContext;
    using lbcrypto::CryptoContextCKKSRNS;
    using lbcrypto::DCRTPoly;
    using lbcrypto::GenCryptoContext;
    using lbcrypto::HEStd_128_classic;
    using lbcrypto::KEYSWITCH;
    using lbcrypto::LEVELEDSHE;
    using lbcrypto::PKE;
    using lbcrypto::Plaintext;

    const std::size_t openfhe_threads = configure_openfhe_threads(thread_count);

    const std::size_t plain_threads = effective_thread_count(thread_count, data.size());
    const Timer plain_timer;
    const double baseline_value = plaintext_sum_amount(data, plain_threads);
    const double plain_time_ms = plain_timer.elapsed_ms();

    const Timer setup_timer;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(static_cast<uint32_t>(config.multiplicative_depth));
    parameters.SetScalingModSize(static_cast<uint32_t>(config.scaling_mod_size));
    parameters.SetFirstModSize(static_cast<uint32_t>(config.first_mod_size));
    parameters.SetSecurityLevel(HEStd_128_classic);

    if (config.requested_ring_dimension != 0) {
        parameters.SetRingDim(static_cast<uint32_t>(config.requested_ring_dimension));
    }
    if (config.batch_size != 0) {
        parameters.SetBatchSize(static_cast<uint32_t>(config.batch_size));
    }

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);

    const auto keys = cc->KeyGen();
    cc->EvalSumKeyGen(keys.secretKey);
    const double setup_time_ms = setup_timer.elapsed_ms();

    const std::size_t actual_ring_dimension = cc->GetRingDimension();
    const std::size_t slots_per_ciphertext =
        config.batch_size == 0 ? actual_ring_dimension / 2 : config.batch_size;
    if (slots_per_ciphertext == 0) {
        throw std::runtime_error("OpenFHE CKKS context reported zero slots");
    }

    const std::size_t ciphertext_count = ceil_div(data.size(), slots_per_ciphertext);
    const std::size_t used_slots_last_ciphertext =
        data.size() - ((ciphertext_count - 1) * slots_per_ciphertext);
    const std::size_t padding_slots_last_ciphertext =
        slots_per_ciphertext - used_slots_last_ciphertext;
    const double slot_utilization = divide_or_zero(
        static_cast<double>(data.size()),
        static_cast<double>(ciphertext_count * slots_per_ciphertext));

    double encode_time_ms = 0.0;
    double encrypt_time_ms = 0.0;
    double he_eval_time_ms = 0.0;
    std::size_t rotation_count_estimate = 0;
    bool has_total = false;
    Ciphertext<DCRTPoly> total_ciphertext;

    for (std::size_t offset = 0; offset < data.size(); offset += slots_per_ciphertext) {
        const std::size_t used_slots = std::min(slots_per_ciphertext, data.size() - offset);
        std::vector<double> packed_values;
        packed_values.reserve(used_slots);
        for (std::size_t i = 0; i < used_slots; ++i) {
            packed_values.push_back(data.amount[offset + i]);
        }

        const Timer encode_timer;
        Plaintext plaintext = cc->MakeCKKSPackedPlaintext(packed_values);
        encode_time_ms += encode_timer.elapsed_ms();

        const Timer encrypt_timer;
        auto ciphertext = cc->Encrypt(keys.publicKey, plaintext);
        encrypt_time_ms += encrypt_timer.elapsed_ms();

        const Timer eval_timer;
        auto chunk_sum = cc->EvalSum(ciphertext, static_cast<uint32_t>(used_slots));
        rotation_count_estimate += ceil_log2(used_slots);
        if (has_total) {
            total_ciphertext = cc->EvalAdd(total_ciphertext, chunk_sum);
        } else {
            total_ciphertext = chunk_sum;
            has_total = true;
        }
        he_eval_time_ms += eval_timer.elapsed_ms();
    }

    Plaintext decrypted;
    const Timer decrypt_timer;
    cc->Decrypt(keys.secretKey, total_ciphertext, &decrypted);
    const double decrypt_time_ms = decrypt_timer.elapsed_ms();

    const Timer decode_timer;
    decrypted->SetLength(1);
    const auto decoded_values = decrypted->GetCKKSPackedValue();
    if (decoded_values.empty()) {
        throw std::runtime_error("OpenFHE CKKS decrypt produced no packed values");
    }
    const double result_value = decoded_values[0].real();
    const double decode_time_ms = decode_timer.elapsed_ms();

    const double total_he_time_ms =
        encode_time_ms + encrypt_time_ms + he_eval_time_ms + decrypt_time_ms + decode_time_ms;
    const double absolute_error = std::abs(result_value - baseline_value);
    const double relative_error = divide_or_zero(absolute_error, std::abs(baseline_value));

    BenchmarkResult result;
    result.operation = "sum_amount";
    result.backend = "openfhe_ckks";
    result.rows = data.size();
    result.threads = openfhe_threads;
    result.plain_time_ms = plain_time_ms;
    result.setup_time_ms = setup_time_ms;
    result.encode_time_ms = encode_time_ms;
    result.encrypt_time_ms = encrypt_time_ms;
    result.he_eval_time_ms = he_eval_time_ms;
    result.decrypt_time_ms = decrypt_time_ms;
    result.decode_time_ms = decode_time_ms;
    result.total_he_time_ms = total_he_time_ms;
    result.operation_slowdown = divide_or_zero(he_eval_time_ms, plain_time_ms);
    result.end_to_end_slowdown = divide_or_zero(total_he_time_ms, plain_time_ms);
    result.result_value = result_value;
    result.baseline_value = baseline_value;
    result.absolute_error = absolute_error;
    result.relative_error = relative_error;
    result.ciphertext_count = ciphertext_count;
    result.slots_per_ciphertext = slots_per_ciphertext;
    result.used_slots_last_ciphertext = used_slots_last_ciphertext;
    result.padding_slots_last_ciphertext = padding_slots_last_ciphertext;
    result.slot_utilization = slot_utilization;
    result.requested_ring_dimension = config.requested_ring_dimension;
    result.actual_ring_dimension = actual_ring_dimension;
    result.security_bits = 128;
    result.multiplicative_depth = config.multiplicative_depth;
    result.scaling_mod_size = config.scaling_mod_size;
    result.first_mod_size = config.first_mod_size;
    result.rotation_count_estimate = rotation_count_estimate;
    result.notes =
#ifdef _OPENMP
        "compute_only_no_io;omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total";
#else
        "compute_only_no_io;openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total";
#endif
    return result;
}

#endif  // UTILITY_BENCH_WITH_OPENFHE
