#pragma once

#ifdef UTILITY_BENCH_WITH_OPENFHE

#include "csv_loader.h"
#include "plain_ops.h"
#include "result_writer.h"
#include "timer.h"

#include "binfhecontext.h"
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

inline double divide_or_zero(double numerator, double denominator) {
    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

inline std::size_t nonzero_or_default(std::size_t value, std::size_t fallback) {
    return value == 0 ? fallback : value;
}

inline bool is_power_of_two(std::size_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

inline std::size_t ceil_log2_nonzero(std::size_t value) {
    if (value == 0) {
        return 0;
    }

    std::size_t rotations = 0;
    --value;
    while (value > 0) {
        value >>= 1;
        ++rotations;
    }
    return rotations;
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
    const std::string& operation,
    const std::vector<double>& values,
    const std::vector<double>* plaintext_multiplier,
    const std::vector<double>* encrypted_multiplier,
    std::size_t thread_count,
    double baseline_value,
    double plain_time_ms,
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

    if (values.size() != data.size()) {
        throw std::runtime_error("CKKS aggregate input vector size does not match row count");
    }
    if (plaintext_multiplier != nullptr && plaintext_multiplier->size() != data.size()) {
        throw std::runtime_error("CKKS plaintext multiplier vector size does not match row count");
    }
    if (encrypted_multiplier != nullptr && encrypted_multiplier->size() != data.size()) {
        throw std::runtime_error("CKKS encrypted multiplier vector size does not match row count");
    }
    if (plaintext_multiplier != nullptr && encrypted_multiplier != nullptr) {
        throw std::runtime_error("CKKS aggregate cannot use plaintext and encrypted multipliers together");
    }

    const std::size_t openfhe_threads = configure_openfhe_threads(thread_count);

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
    if (encrypted_multiplier != nullptr) {
        cc->EvalMultKeyGen(keys.secretKey);
    }
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
    bool has_total = false;
    Ciphertext<DCRTPoly> total_ciphertext;

    for (std::size_t offset = 0; offset < data.size(); offset += slots_per_ciphertext) {
        const std::size_t used_slots = std::min(slots_per_ciphertext, data.size() - offset);
        std::vector<double> packed_values;
        packed_values.reserve(used_slots);
        for (std::size_t i = 0; i < used_slots; ++i) {
            packed_values.push_back(values[offset + i]);
        }

        const Timer encode_timer;
        Plaintext plaintext = cc->MakeCKKSPackedPlaintext(packed_values);
        Plaintext multiplier_plaintext;
        if (plaintext_multiplier != nullptr) {
            std::vector<double> packed_multiplier;
            packed_multiplier.reserve(used_slots);
            for (std::size_t i = 0; i < used_slots; ++i) {
                packed_multiplier.push_back((*plaintext_multiplier)[offset + i]);
            }
            multiplier_plaintext = cc->MakeCKKSPackedPlaintext(packed_multiplier);
        }
        Plaintext encrypted_multiplier_plaintext;
        if (encrypted_multiplier != nullptr) {
            std::vector<double> packed_multiplier;
            packed_multiplier.reserve(used_slots);
            for (std::size_t i = 0; i < used_slots; ++i) {
                packed_multiplier.push_back((*encrypted_multiplier)[offset + i]);
            }
            encrypted_multiplier_plaintext = cc->MakeCKKSPackedPlaintext(packed_multiplier);
        }
        encode_time_ms += encode_timer.elapsed_ms();

        const Timer encrypt_timer;
        auto ciphertext = cc->Encrypt(keys.publicKey, plaintext);
        Ciphertext<DCRTPoly> encrypted_multiplier_ciphertext;
        if (encrypted_multiplier != nullptr) {
            encrypted_multiplier_ciphertext = cc->Encrypt(keys.publicKey, encrypted_multiplier_plaintext);
        }
        encrypt_time_ms += encrypt_timer.elapsed_ms();

        const Timer eval_timer;
        auto eval_input = ciphertext;
        if (plaintext_multiplier != nullptr) {
            eval_input = cc->EvalMult(ciphertext, multiplier_plaintext);
        }
        if (encrypted_multiplier != nullptr) {
            eval_input = cc->EvalMult(ciphertext, encrypted_multiplier_ciphertext);
        }
        auto chunk_sum = cc->EvalSum(eval_input, static_cast<uint32_t>(used_slots));
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
    result.operation = operation;
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
    result.rotation_count_reported = 0;
    result.notes =
#ifdef _OPENMP
        encrypted_multiplier != nullptr
            ? "compute_only_no_io;plaintext_lookup_weight_preexpanded;ciphertext_ciphertext_mult;amount_and_risk_weight_encrypted;plain_time_reused_from_same_run_baseline;omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total"
            : plaintext_multiplier == nullptr
            ? "compute_only_no_io;plain_time_reused_from_same_run_baseline;omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total"
            : "compute_only_no_io;plaintext_lookup_weight_preexpanded;ciphertext_plaintext_mult;plain_time_reused_from_same_run_baseline;omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total";
#else
        encrypted_multiplier != nullptr
            ? "compute_only_no_io;plaintext_lookup_weight_preexpanded;ciphertext_ciphertext_mult;amount_and_risk_weight_encrypted;plain_time_reused_from_same_run_baseline;openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total"
            : plaintext_multiplier == nullptr
            ? "compute_only_no_io;plain_time_reused_from_same_run_baseline;openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total"
            : "compute_only_no_io;plaintext_lookup_weight_preexpanded;ciphertext_plaintext_mult;plain_time_reused_from_same_run_baseline;openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total";
#endif
    return result;
}

inline BenchmarkResult openfhe_ckks_sum_amount(
    const Transactions& data,
    std::size_t thread_count,
    double baseline_value,
    double plain_time_ms,
    const CkksSumConfig& config = CkksSumConfig{}) {
    return openfhe_ckks_sum_amount(
        data,
        "sum_amount",
        data.amount,
        nullptr,
        nullptr,
        thread_count,
        baseline_value,
        plain_time_ms,
        config);
}

inline BenchmarkResult openfhe_ckks_weighted_sum_amount_risk(
    const Transactions& data,
    std::size_t thread_count,
    double baseline_value,
    double plain_time_ms,
    const CkksSumConfig& config = CkksSumConfig{}) {
    if (data.risk_weight_by_row.size() != data.size()) {
        throw std::runtime_error(
            "risk_weight_by_row is missing; load customers.csv before weighted CKKS benchmarks");
    }

    return openfhe_ckks_sum_amount(
        data,
        "weighted_sum_amount_risk",
        data.amount,
        &data.risk_weight_by_row,
        nullptr,
        thread_count,
        baseline_value,
        plain_time_ms,
        config);
}

inline BenchmarkResult openfhe_ckks_weighted_sum_amount_risk_encrypted(
    const Transactions& data,
    std::size_t thread_count,
    double baseline_value,
    double plain_time_ms,
    const CkksSumConfig& config = CkksSumConfig{}) {
    if (data.risk_weight_by_row.size() != data.size()) {
        throw std::runtime_error(
            "risk_weight_by_row is missing; load customers.csv before encrypted weighted CKKS benchmarks");
    }

    CkksSumConfig encrypted_config = config;
    encrypted_config.multiplicative_depth =
        std::max<std::size_t>(encrypted_config.multiplicative_depth, 2);

    return openfhe_ckks_sum_amount(
        data,
        "weighted_sum_amount_risk_encrypted",
        data.amount,
        nullptr,
        &data.risk_weight_by_row,
        thread_count,
        baseline_value,
        plain_time_ms,
        encrypted_config);
}

inline BenchmarkResult openfhe_ckks_rolling_avg_amount(
    const Transactions& data,
    std::size_t thread_count,
    double baseline_value,
    double plain_time_ms,
    std::size_t window_size,
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

    if (window_size == 0) {
        throw std::runtime_error("rolling average window size must be positive");
    }
    if (data.size() < window_size) {
        throw std::runtime_error("rolling average requires at least window_size rows");
    }

    CkksSumConfig rolling_config = config;
    rolling_config.multiplicative_depth =
        std::max<std::size_t>(rolling_config.multiplicative_depth, 1);

    const std::size_t openfhe_threads = configure_openfhe_threads(thread_count);

    const Timer setup_timer;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(static_cast<uint32_t>(rolling_config.multiplicative_depth));
    parameters.SetScalingModSize(static_cast<uint32_t>(rolling_config.scaling_mod_size));
    parameters.SetFirstModSize(static_cast<uint32_t>(rolling_config.first_mod_size));
    parameters.SetSecurityLevel(HEStd_128_classic);

    if (rolling_config.requested_ring_dimension != 0) {
        parameters.SetRingDim(static_cast<uint32_t>(rolling_config.requested_ring_dimension));
    }
    if (rolling_config.batch_size != 0) {
        parameters.SetBatchSize(static_cast<uint32_t>(rolling_config.batch_size));
    }

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);

    const auto keys = cc->KeyGen();
    cc->EvalSumKeyGen(keys.secretKey);

    std::vector<int32_t> rotation_indices;
    rotation_indices.reserve(window_size - 1);
    for (std::size_t offset = 1; offset < window_size; ++offset) {
        rotation_indices.push_back(static_cast<int32_t>(offset));
    }
    cc->EvalAtIndexKeyGen(keys.secretKey, rotation_indices);
    const double setup_time_ms = setup_timer.elapsed_ms();

    const std::size_t actual_ring_dimension = cc->GetRingDimension();
    const std::size_t slots_per_ciphertext =
        rolling_config.batch_size == 0 ? actual_ring_dimension / 2 : rolling_config.batch_size;
    if (slots_per_ciphertext <= window_size - 1) {
        throw std::runtime_error(
            "rolling average requires more CKKS slots than window_size - 1");
    }

    const std::size_t output_rows = data.size() - window_size + 1;
    const std::size_t output_slots_per_ciphertext =
        slots_per_ciphertext - (window_size - 1);
    const std::size_t ciphertext_count = ceil_div(output_rows, output_slots_per_ciphertext);
    std::size_t used_slots_last_ciphertext = 0;
    std::size_t total_input_slots_packed = 0;
    std::size_t rotation_count_estimate = 0;

    double encode_time_ms = 0.0;
    double encrypt_time_ms = 0.0;
    double he_eval_time_ms = 0.0;
    bool has_total = false;
    Ciphertext<DCRTPoly> total_ciphertext;

    for (std::size_t output_offset = 0;
         output_offset < output_rows;
         output_offset += output_slots_per_ciphertext) {
        const std::size_t valid_outputs =
            std::min(output_slots_per_ciphertext, output_rows - output_offset);
        const std::size_t input_slots = valid_outputs + window_size - 1;
        total_input_slots_packed += input_slots;
        used_slots_last_ciphertext = input_slots;
        rotation_count_estimate += (window_size - 1) + ceil_log2_nonzero(valid_outputs);

        std::vector<double> packed_amount(slots_per_ciphertext, 0.0);
        std::vector<double> average_mask(slots_per_ciphertext, 0.0);
        for (std::size_t i = 0; i < input_slots; ++i) {
            packed_amount[i] = data.amount[output_offset + i];
        }
        for (std::size_t i = 0; i < valid_outputs; ++i) {
            average_mask[i] = 1.0 / static_cast<double>(window_size);
        }

        const Timer encode_timer;
        Plaintext amount_plaintext = cc->MakeCKKSPackedPlaintext(packed_amount);
        Plaintext average_mask_plaintext = cc->MakeCKKSPackedPlaintext(average_mask);
        encode_time_ms += encode_timer.elapsed_ms();

        const Timer encrypt_timer;
        auto amount_ciphertext = cc->Encrypt(keys.publicKey, amount_plaintext);
        encrypt_time_ms += encrypt_timer.elapsed_ms();

        const Timer eval_timer;
        auto rolling_sum = amount_ciphertext;
        for (std::size_t offset = 1; offset < window_size; ++offset) {
            rolling_sum = cc->EvalAdd(
                rolling_sum,
                cc->EvalAtIndex(amount_ciphertext, static_cast<int32_t>(offset)));
        }
        auto rolling_average = cc->EvalMult(rolling_sum, average_mask_plaintext);
        auto chunk_sum = cc->EvalSum(rolling_average, static_cast<uint32_t>(valid_outputs));
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
        throw std::runtime_error("OpenFHE CKKS rolling average decrypt produced no values");
    }
    const double result_value = decoded_values[0].real();
    const double decode_time_ms = decode_timer.elapsed_ms();

    const double total_he_time_ms =
        encode_time_ms + encrypt_time_ms + he_eval_time_ms + decrypt_time_ms + decode_time_ms;
    const double absolute_error = std::abs(result_value - baseline_value);
    const double relative_error = divide_or_zero(absolute_error, std::abs(baseline_value));
    const std::size_t padding_slots_last_ciphertext =
        slots_per_ciphertext - used_slots_last_ciphertext;
    const double slot_utilization = divide_or_zero(
        static_cast<double>(total_input_slots_packed),
        static_cast<double>(ciphertext_count * slots_per_ciphertext));

    BenchmarkResult result;
    result.operation = "rolling_avg_amount_w" + std::to_string(window_size);
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
    result.requested_ring_dimension = rolling_config.requested_ring_dimension;
    result.actual_ring_dimension = actual_ring_dimension;
    result.security_bits = 128;
    result.multiplicative_depth = rolling_config.multiplicative_depth;
    result.scaling_mod_size = rolling_config.scaling_mod_size;
    result.first_mod_size = rolling_config.first_mod_size;
    result.rotation_count_reported = rotation_count_estimate;
    result.notes =
#ifdef _OPENMP
        "compute_only_no_io;rolling_avg_forward_window;amount_encrypted;overlap_packing_for_chunk_boundaries;eval_at_index_rotations;plaintext_average_mask;no_bootstrap;plain_time_reused_from_same_run_baseline;omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total";
#else
        "compute_only_no_io;rolling_avg_forward_window;amount_encrypted;overlap_packing_for_chunk_boundaries;eval_at_index_rotations;plaintext_average_mask;no_bootstrap;plain_time_reused_from_same_run_baseline;openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total";
#endif
    return result;
}

inline BenchmarkResult openfhe_ckks_compare_amount_gt_5000(
    const Transactions& data,
    std::size_t thread_count,
    double baseline_value,
    double plain_time_ms,
    const CkksSumConfig& config = CkksSumConfig{}) {
    using lbcrypto::ADVANCEDSHE;
    using lbcrypto::CCParams;
    using lbcrypto::CryptoContext;
    using lbcrypto::CryptoContextCKKSRNS;
    using lbcrypto::DCRTPoly;
    using lbcrypto::FLEXIBLEAUTO;
    using lbcrypto::GenCryptoContext;
    using lbcrypto::HEStd_128_classic;
    using lbcrypto::HYBRID;
    using lbcrypto::KEYSWITCH;
    using lbcrypto::LEVELEDSHE;
    using lbcrypto::PKE;
    using lbcrypto::Plaintext;
    using lbcrypto::SCHEMESWITCH;
    using lbcrypto::SchSwchParams;
    using lbcrypto::STD128;
    using lbcrypto::UNIFORM_TERNARY;

    const std::size_t openfhe_threads = configure_openfhe_threads(thread_count);
    const std::size_t comparison_slots = nonzero_or_default(config.batch_size, 16);
    if (!is_power_of_two(comparison_slots)) {
        throw std::runtime_error(
            "compare_amount_gt_5000 requires --ckks-batch-size to be 0 or a power of two");
    }

    const std::size_t comparison_depth = std::max<std::size_t>(config.multiplicative_depth, 17);
    const uint32_t log_q_lwe = 25;
    const double threshold = 5000.0;
    const double scale_sign_fhew = 1.0;

    const Timer setup_timer;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(static_cast<uint32_t>(comparison_depth));
    parameters.SetScalingModSize(static_cast<uint32_t>(config.scaling_mod_size));
    parameters.SetFirstModSize(static_cast<uint32_t>(config.first_mod_size));
    parameters.SetScalingTechnique(FLEXIBLEAUTO);
    parameters.SetSecurityLevel(HEStd_128_classic);
    parameters.SetBatchSize(static_cast<uint32_t>(comparison_slots));
    parameters.SetSecretKeyDist(UNIFORM_TERNARY);
    parameters.SetKeySwitchTechnique(HYBRID);
    parameters.SetNumLargeDigits(3);
    if (config.requested_ring_dimension != 0) {
        parameters.SetRingDim(static_cast<uint32_t>(config.requested_ring_dimension));
    }

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);
    cc->Enable(SCHEMESWITCH);

    const auto keys = cc->KeyGen();

    SchSwchParams switch_params;
    switch_params.SetSecurityLevelCKKS(HEStd_128_classic);
    switch_params.SetSecurityLevelFHEW(STD128);
    switch_params.SetCtxtModSizeFHEWLargePrec(log_q_lwe);
    switch_params.SetNumSlotsCKKS(static_cast<uint32_t>(comparison_slots));
    switch_params.SetNumValues(static_cast<uint32_t>(comparison_slots));

    auto private_key_fhew = cc->EvalSchemeSwitchingSetup(switch_params);
    auto cc_lwe = cc->GetBinCCForSchemeSwitch();
    cc_lwe->BTKeyGen(private_key_fhew);
    cc->EvalSchemeSwitchingKeyGen(keys, private_key_fhew);

    const uint32_t modulus_lwe = 1U << log_q_lwe;
    const uint32_t beta = cc_lwe->GetBeta().ConvertToInt();
    const uint32_t p_lwe = modulus_lwe / (2 * beta);
    cc->EvalCompareSwitchPrecompute(p_lwe, scale_sign_fhew);
    const double setup_time_ms = setup_timer.elapsed_ms();

    const std::size_t actual_ring_dimension = cc->GetRingDimension();
    const std::size_t ciphertext_count = ceil_div(data.size(), comparison_slots);
    const std::size_t used_slots_last_ciphertext =
        data.size() - ((ciphertext_count - 1) * comparison_slots);
    const std::size_t padding_slots_last_ciphertext =
        comparison_slots - used_slots_last_ciphertext;
    const double slot_utilization = divide_or_zero(
        static_cast<double>(data.size()),
        static_cast<double>(ciphertext_count * comparison_slots));

    double encode_time_ms = 0.0;
    double encrypt_time_ms = 0.0;
    double he_eval_time_ms = 0.0;
    double decrypt_time_ms = 0.0;
    double decode_time_ms = 0.0;
    double result_value = 0.0;

    const std::vector<double> packed_threshold(comparison_slots, threshold);
    const Timer threshold_encode_timer;
    Plaintext threshold_plaintext = cc->MakeCKKSPackedPlaintext(
        packed_threshold, 1, 0, nullptr, static_cast<uint32_t>(comparison_slots));
    encode_time_ms += threshold_encode_timer.elapsed_ms();

    const Timer threshold_encrypt_timer;
    auto threshold_ciphertext = cc->Encrypt(keys.publicKey, threshold_plaintext);
    encrypt_time_ms += threshold_encrypt_timer.elapsed_ms();

    for (std::size_t offset = 0; offset < data.size(); offset += comparison_slots) {
        const std::size_t used_slots = std::min(comparison_slots, data.size() - offset);
        std::vector<double> packed_amount;
        packed_amount.reserve(comparison_slots);
        for (std::size_t i = 0; i < used_slots; ++i) {
            packed_amount.push_back(data.amount[offset + i]);
        }
        for (std::size_t i = used_slots; i < comparison_slots; ++i) {
            packed_amount.push_back(0.0);
        }

        const Timer encode_timer;
        Plaintext amount_plaintext = cc->MakeCKKSPackedPlaintext(
            packed_amount, 1, 0, nullptr, static_cast<uint32_t>(comparison_slots));
        encode_time_ms += encode_timer.elapsed_ms();

        const Timer encrypt_timer;
        auto amount_ciphertext = cc->Encrypt(keys.publicKey, amount_plaintext);
        encrypt_time_ms += encrypt_timer.elapsed_ms();

        const Timer eval_timer;
        auto comparison_mask = cc->EvalCompareSchemeSwitching(
            threshold_ciphertext,
            amount_ciphertext,
            static_cast<uint32_t>(comparison_slots),
            static_cast<uint32_t>(comparison_slots),
            p_lwe,
            scale_sign_fhew);
        he_eval_time_ms += eval_timer.elapsed_ms();

        Plaintext comparison_plaintext;
        const Timer decrypt_timer;
        cc->Decrypt(keys.secretKey, comparison_mask, &comparison_plaintext);
        decrypt_time_ms += decrypt_timer.elapsed_ms();

        const Timer decode_timer;
        comparison_plaintext->SetLength(used_slots);
        const auto comparison_values = comparison_plaintext->GetRealPackedValue();
        for (std::size_t i = 0; i < used_slots && i < comparison_values.size(); ++i) {
            result_value += comparison_values[i];
        }
        decode_time_ms += decode_timer.elapsed_ms();
    }

    const double total_he_time_ms =
        encode_time_ms + encrypt_time_ms + he_eval_time_ms + decrypt_time_ms + decode_time_ms;
    const double absolute_error = std::abs(result_value - baseline_value);
    const double relative_error = divide_or_zero(absolute_error, std::abs(baseline_value));

    BenchmarkResult result;
    result.operation = "compare_amount_gt_5000";
    result.backend = "openfhe_ckks_scheme_switch";
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
    result.slots_per_ciphertext = comparison_slots;
    result.used_slots_last_ciphertext = used_slots_last_ciphertext;
    result.padding_slots_last_ciphertext = padding_slots_last_ciphertext;
    result.slot_utilization = slot_utilization;
    result.requested_ring_dimension = config.requested_ring_dimension;
    result.actual_ring_dimension = actual_ring_dimension;
    result.security_bits = 128;
    result.multiplicative_depth = comparison_depth;
    result.scaling_mod_size = config.scaling_mod_size;
    result.first_mod_size = config.first_mod_size;
    result.rotation_count_reported = 0;
    result.notes =
#ifdef _OPENMP
        "compute_only_no_io;encrypted_comparison_only_amount_gt_5000;ckks_fhew_scheme_switching;encrypted_threshold_reused;no_evalsum;no_mask_apply;no_selected_amount;comparison_slots_default_16;if_batch_size_unset;omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total";
#else
        "compute_only_no_io;encrypted_comparison_only_amount_gt_5000;ckks_fhew_scheme_switching;encrypted_threshold_reused;no_evalsum;no_mask_apply;no_selected_amount;comparison_slots_default_16;if_batch_size_unset;openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total";
#endif
    cc->ClearStaticMapsAndVectors();
    return result;
}

#endif  // UTILITY_BENCH_WITH_OPENFHE
