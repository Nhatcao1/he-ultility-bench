#pragma once

#ifdef UTILITY_BENCH_WITH_OPENFHE

#include "ckks_sum_amount.h"
#include "tiny_query_loader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

inline std::size_t next_power_of_two(std::size_t value) {
    std::size_t result = 1;
    while (result < value) {
        result <<= 1U;
    }
    return result;
}

inline BenchmarkResult openfhe_ckks_tiny_onehot_query(
    const TinyCryptoQueryData& data,
    const std::string& operation,
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

    if (operation != "tiny_lookup_onehot_risk_weight" &&
        operation != "tiny_join_onehot_amount_risk") {
        throw std::runtime_error("unknown tiny OpenFHE one-hot operation: " + operation);
    }
    if (data.size() == 0 || data.key_domain() == 0) {
        throw std::runtime_error("tiny query data is empty");
    }

    const bool include_amount = operation == "tiny_join_onehot_amount_risk";
    const std::size_t openfhe_threads = configure_openfhe_threads(thread_count);
    const std::size_t slots = nonzero_or_default(config.batch_size, next_power_of_two(data.size()));
    if (!is_power_of_two(slots)) {
        throw std::runtime_error("tiny one-hot query requires --ckks-batch-size to be 0 or a power of two");
    }
    if (slots < data.size()) {
        throw std::runtime_error("--ckks-batch-size must be at least the tiny query row count");
    }

    const std::size_t depth = std::max<std::size_t>(config.multiplicative_depth, include_amount ? 2 : 1);

    const Timer setup_timer;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(static_cast<uint32_t>(depth));
    parameters.SetScalingModSize(static_cast<uint32_t>(config.scaling_mod_size));
    parameters.SetFirstModSize(static_cast<uint32_t>(config.first_mod_size));
    parameters.SetSecurityLevel(HEStd_128_classic);
    parameters.SetBatchSize(static_cast<uint32_t>(slots));
    if (config.requested_ring_dimension != 0) {
        parameters.SetRingDim(static_cast<uint32_t>(config.requested_ring_dimension));
    }

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);

    const auto keys = cc->KeyGen();
    cc->EvalMultKeyGen(keys.secretKey);
    const double setup_time_ms = setup_timer.elapsed_ms();

    const std::size_t actual_ring_dimension = cc->GetRingDimension();
    const std::size_t key_domain = data.key_domain();

    double encode_time_ms = 0.0;
    double encrypt_time_ms = 0.0;
    double he_eval_time_ms = 0.0;
    double decrypt_time_ms = 0.0;
    double decode_time_ms = 0.0;

    std::vector<double> amount_packed(slots, 0.0);
    for (std::size_t row = 0; row < data.size(); ++row) {
        amount_packed[row] = data.amount[row];
    }

    Ciphertext<DCRTPoly> amount_ciphertext;
    if (include_amount) {
        const Timer encode_timer;
        Plaintext amount_plaintext = cc->MakeCKKSPackedPlaintext(amount_packed);
        encode_time_ms += encode_timer.elapsed_ms();

        const Timer encrypt_timer;
        amount_ciphertext = cc->Encrypt(keys.publicKey, amount_plaintext);
        encrypt_time_ms += encrypt_timer.elapsed_ms();
    }

    bool has_result = false;
    Ciphertext<DCRTPoly> result_ciphertext;

    for (std::size_t key = 0; key < key_domain; ++key) {
        std::vector<double> mask_packed(slots, 0.0);
        std::vector<double> risk_packed(slots, data.customer_risk_weight_by_key[key]);
        for (std::size_t row = 0; row < data.size(); ++row) {
            mask_packed[row] = data.customer_onehot_by_key[key][row];
        }

        const Timer encode_timer;
        Plaintext mask_plaintext = cc->MakeCKKSPackedPlaintext(mask_packed);
        Plaintext risk_plaintext = cc->MakeCKKSPackedPlaintext(risk_packed);
        encode_time_ms += encode_timer.elapsed_ms();

        const Timer encrypt_timer;
        auto mask_ciphertext = cc->Encrypt(keys.publicKey, mask_plaintext);
        auto risk_ciphertext = cc->Encrypt(keys.publicKey, risk_plaintext);
        encrypt_time_ms += encrypt_timer.elapsed_ms();

        const Timer eval_timer;
        auto term = cc->EvalMult(mask_ciphertext, risk_ciphertext);
        if (include_amount) {
            term = cc->EvalMult(term, amount_ciphertext);
        }
        if (has_result) {
            result_ciphertext = cc->EvalAdd(result_ciphertext, term);
        } else {
            result_ciphertext = term;
            has_result = true;
        }
        he_eval_time_ms += eval_timer.elapsed_ms();
    }

    Plaintext decrypted;
    const Timer decrypt_timer;
    cc->Decrypt(keys.secretKey, result_ciphertext, &decrypted);
    decrypt_time_ms += decrypt_timer.elapsed_ms();

    const Timer decode_timer;
    decrypted->SetLength(data.size());
    const auto values = decrypted->GetRealPackedValue();
    double result_value = 0.0;
    for (std::size_t row = 0; row < data.size() && row < values.size(); ++row) {
        result_value += values[row];
    }
    decode_time_ms += decode_timer.elapsed_ms();

    const double total_he_time_ms =
        encode_time_ms + encrypt_time_ms + he_eval_time_ms + decrypt_time_ms + decode_time_ms;
    const double absolute_error = std::abs(result_value - baseline_value);
    const double relative_error = divide_or_zero(absolute_error, std::abs(baseline_value));

    BenchmarkResult result;
    result.operation = operation;
    result.backend = "openfhe_ckks_tiny_onehot";
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
    result.ciphertext_count = key_domain * 2 + (include_amount ? 1 : 0);
    result.slots_per_ciphertext = slots;
    result.used_slots_last_ciphertext = data.size();
    result.padding_slots_last_ciphertext = slots - data.size();
    result.slot_utilization = divide_or_zero(static_cast<double>(data.size()), static_cast<double>(slots));
    result.requested_ring_dimension = config.requested_ring_dimension;
    result.actual_ring_dimension = actual_ring_dimension;
    result.security_bits = 128;
    result.multiplicative_depth = depth;
    result.scaling_mod_size = config.scaling_mod_size;
    result.first_mod_size = config.first_mod_size;
    result.rotation_count_reported = 0;
    result.notes =
#ifdef _OPENMP
        include_amount
            ? "tiny_crypto_query;onehot_lookup;encrypted_mask;encrypted_risk_weight;encrypted_amount;ct_ct_mult_depth2;omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total"
            : "tiny_crypto_query;onehot_lookup;encrypted_mask;encrypted_risk_weight;ct_ct_mult_depth1;omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total";
#else
        include_amount
            ? "tiny_crypto_query;onehot_lookup;encrypted_mask;encrypted_risk_weight;encrypted_amount;ct_ct_mult_depth2;openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total"
            : "tiny_crypto_query;onehot_lookup;encrypted_mask;encrypted_risk_weight;ct_ct_mult_depth1;openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total";
#endif

    return result;
}

#endif  // UTILITY_BENCH_WITH_OPENFHE
