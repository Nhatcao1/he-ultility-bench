#include "csv_loader.h"
#include "plain_ops.h"
#include "result_writer.h"
#include "timer.h"

#ifdef UTILITY_BENCH_WITH_OPENFHE
#include "openfhe.h"

#ifdef _OPENMP
#include <omp.h>
#endif
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

enum class BackendMode {
    PlainCpp,
    OpenFheCkks,
    All,
};

struct CliArgs {
    std::string data_path = "data/generated/medium_100k/transactions.csv";
    std::filesystem::path results_path =
        "results/optimized_lookup/channel_lookup_vector.csv";
    BackendMode backend = BackendMode::All;
    std::vector<std::size_t> thread_counts = {1};
    std::size_t repeat_count = 3;
    std::size_t max_rows = 0;
    std::size_t channel_domain = 10;
    std::size_t ckks_ring_dim = 0;
    std::size_t ckks_batch_size = 0;
    std::size_t ckks_depth = 2;
    std::size_t ckks_scale_bits = 50;
    std::size_t ckks_first_mod_bits = 60;
};

struct CkksLookupConfig {
    std::size_t requested_ring_dimension = 0;
    std::size_t batch_size = 0;
    std::size_t multiplicative_depth = 2;
    std::size_t scaling_mod_size = 50;
    std::size_t first_mod_size = 60;
    std::size_t channel_domain = 10;
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--data transactions.csv] "
        << "[--backend plain_cpp|openfhe_ckks|all] "
        << "[--threads 1] [--repeat 3] [--max-rows 100000] "
        << "[--channel-domain 10] [--ckks-ring-dim 0] [--ckks-batch-size 0] "
        << "[--ckks-depth 2] [--ckks-scale-bits 50] [--ckks-first-mod-bits 60] "
        << "[--results results/optimized_lookup/channel_lookup_vector.csv]\n";
}

BackendMode parse_backend_mode(const std::string& value) {
    if (value == "plain_cpp") {
        return BackendMode::PlainCpp;
    }
    if (value == "openfhe_ckks") {
        return BackendMode::OpenFheCkks;
    }
    if (value == "all") {
        return BackendMode::All;
    }
    throw std::runtime_error("unknown backend: " + value);
}

bool wants_plain_cpp(BackendMode backend) {
    return backend == BackendMode::PlainCpp || backend == BackendMode::All;
}

bool wants_openfhe_ckks(BackendMode backend) {
    return backend == BackendMode::OpenFheCkks || backend == BackendMode::All;
}

std::size_t parse_size_arg(int& i, int argc, char** argv, const std::string& flag) {
    if (i + 1 >= argc) {
        throw std::runtime_error(flag + " requires a value");
    }
    return static_cast<std::size_t>(std::stoull(argv[++i]));
}

CliArgs parse_args(int argc, char** argv) {
    CliArgs args;

    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];

        if (flag == "--help" || flag == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (flag == "--data") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--data requires a path");
            }
            args.data_path = argv[++i];
        } else if (flag == "--results") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--results requires a path");
            }
            args.results_path = argv[++i];
        } else if (flag == "--backend") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--backend requires a value");
            }
            args.backend = parse_backend_mode(argv[++i]);
        } else if (flag == "--threads" || flag == "--thread-counts") {
            args.thread_counts.clear();
            while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                const auto parsed = static_cast<std::size_t>(std::stoull(argv[++i]));
                if (parsed == 0) {
                    throw std::runtime_error("--threads values must be positive");
                }
                args.thread_counts.push_back(parsed);
            }
            if (args.thread_counts.empty()) {
                throw std::runtime_error("--threads requires at least one value");
            }
        } else if (flag == "--repeat") {
            args.repeat_count = parse_size_arg(i, argc, argv, flag);
            if (args.repeat_count == 0) {
                throw std::runtime_error("--repeat must be positive");
            }
        } else if (flag == "--max-rows") {
            args.max_rows = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--channel-domain") {
            args.channel_domain = parse_size_arg(i, argc, argv, flag);
            if (args.channel_domain == 0) {
                throw std::runtime_error("--channel-domain must be positive");
            }
        } else if (flag == "--ckks-ring-dim") {
            args.ckks_ring_dim = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--ckks-batch-size") {
            args.ckks_batch_size = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--ckks-depth") {
            args.ckks_depth = parse_size_arg(i, argc, argv, flag);
            if (args.ckks_depth == 0) {
                throw std::runtime_error("--ckks-depth must be positive");
            }
        } else if (flag == "--ckks-scale-bits") {
            args.ckks_scale_bits = parse_size_arg(i, argc, argv, flag);
            if (args.ckks_scale_bits == 0) {
                throw std::runtime_error("--ckks-scale-bits must be positive");
            }
        } else if (flag == "--ckks-first-mod-bits") {
            args.ckks_first_mod_bits = parse_size_arg(i, argc, argv, flag);
            if (args.ckks_first_mod_bits == 0) {
                throw std::runtime_error("--ckks-first-mod-bits must be positive");
            }
        } else {
            throw std::runtime_error("unknown argument: " + flag);
        }
    }

    return args;
}

double divide_or_zero(double numerator, double denominator) {
    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

std::size_t ceil_div(std::size_t numerator, std::size_t denominator) {
    return (numerator + denominator - 1) / denominator;
}

std::string repeat_note(std::size_t repeat_index, std::size_t repeat_count) {
    return "repeat_index=" + std::to_string(repeat_index) +
           ";repeat_count=" + std::to_string(repeat_count);
}

double channel_lookup_value(std::size_t channel_id) {
    return 0.50 + 0.125 * static_cast<double>(channel_id);
}

void validate_channel_domain(const Transactions& data, std::size_t channel_domain) {
    for (const std::size_t channel_id : data.channel_id) {
        if (channel_id >= channel_domain) {
            throw std::runtime_error(
                "channel_id " + std::to_string(channel_id) +
                " is outside --channel-domain " + std::to_string(channel_domain));
        }
    }
}

double plaintext_channel_lookup_checksum(
    const Transactions& data,
    std::size_t thread_count) {
    return parallel_sum(data.size(), thread_count, [&](std::size_t row) {
        return channel_lookup_value(data.channel_id[row]);
    });
}

double average_field(
    const std::vector<BenchmarkResult>& results,
    double BenchmarkResult::*field) {
    double sum = 0.0;
    for (const auto& result : results) {
        sum += result.*field;
    }
    return divide_or_zero(sum, static_cast<double>(results.size()));
}

BenchmarkResult summarize_results(
    const std::vector<BenchmarkResult>& results,
    const std::string& operation,
    const std::string& backend) {
    if (results.empty()) {
        throw std::runtime_error("cannot summarize empty benchmark result list");
    }

    BenchmarkResult summary = results.front();
    summary.operation = operation + "_summary_avg";
    summary.backend = backend + "_summary";
    summary.plain_time_ms = average_field(results, &BenchmarkResult::plain_time_ms);
    summary.setup_time_ms = average_field(results, &BenchmarkResult::setup_time_ms);
    summary.encode_time_ms = average_field(results, &BenchmarkResult::encode_time_ms);
    summary.encrypt_time_ms = average_field(results, &BenchmarkResult::encrypt_time_ms);
    summary.he_eval_time_ms = average_field(results, &BenchmarkResult::he_eval_time_ms);
    summary.decrypt_time_ms = average_field(results, &BenchmarkResult::decrypt_time_ms);
    summary.decode_time_ms = average_field(results, &BenchmarkResult::decode_time_ms);
    summary.total_he_time_ms = average_field(results, &BenchmarkResult::total_he_time_ms);
    summary.operation_slowdown = average_field(results, &BenchmarkResult::operation_slowdown);
    summary.end_to_end_slowdown = average_field(results, &BenchmarkResult::end_to_end_slowdown);
    summary.result_value = average_field(results, &BenchmarkResult::result_value);
    summary.baseline_value = average_field(results, &BenchmarkResult::baseline_value);
    summary.absolute_error = average_field(results, &BenchmarkResult::absolute_error);
    summary.relative_error = average_field(results, &BenchmarkResult::relative_error);
    summary.slot_utilization = average_field(results, &BenchmarkResult::slot_utilization);
    summary.notes = "repeat_summary;repeat_count=" + std::to_string(results.size()) +
        ";real_calculation_ms=he_eval_time_ms" +
        ";overall_lifecycle_ms=total_he_time_ms;setup_keygen_excluded_from_total";
    return summary;
}

BenchmarkResult run_plain_lookup(
    const Transactions& data,
    std::size_t thread_count) {
    const std::size_t actual_threads = effective_thread_count(thread_count, data.size());
    const Timer timer;
    const double value = plaintext_channel_lookup_checksum(
        data, actual_threads);
    const double plain_time_ms = timer.elapsed_ms();

    BenchmarkResult result;
    result.operation = "lookup_channel_weight_vector";
    result.backend = "plain_cpp";
    result.rows = data.size();
    result.threads = actual_threads;
    result.plain_time_ms = plain_time_ms;
    result.result_value = value;
    result.baseline_value = value;
    result.notes = "lookup_vector_reference_plain;checksum_of_lookup_output";
    return result;
}

#ifdef UTILITY_BENCH_WITH_OPENFHE

std::size_t configure_openfhe_threads(std::size_t requested_threads) {
#ifdef _OPENMP
    omp_set_num_threads(static_cast<int>(requested_threads));
    return static_cast<std::size_t>(std::max(1, omp_get_max_threads()));
#else
    return requested_threads;
#endif
}

BenchmarkResult run_openfhe_lookup(
    const Transactions& data,
    std::size_t thread_count,
    double baseline_value,
    double plain_time_ms,
    const CkksLookupConfig& config) {
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

    if (data.size() == 0) {
        throw std::runtime_error("lookup benchmark requires at least one row");
    }
    validate_channel_domain(data, config.channel_domain);

    const std::size_t openfhe_threads = configure_openfhe_threads(thread_count);
    const std::size_t requested_depth = std::max<std::size_t>(config.multiplicative_depth, 2);

    const Timer setup_timer;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(static_cast<uint32_t>(requested_depth));
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
    cc->EvalMultKeyGen(keys.secretKey);
    const double setup_time_ms = setup_timer.elapsed_ms();

    const std::size_t actual_ring_dimension = cc->GetRingDimension();
    const std::size_t slots_per_ciphertext =
        config.batch_size == 0 ? actual_ring_dimension / 2 : config.batch_size;
    if (slots_per_ciphertext == 0) {
        throw std::runtime_error("OpenFHE CKKS context reported zero slots");
    }

    const std::size_t row_chunk_count = ceil_div(data.size(), slots_per_ciphertext);
    const std::size_t used_slots_last_ciphertext =
        data.size() - ((row_chunk_count - 1) * slots_per_ciphertext);
    const std::size_t padding_slots_last_ciphertext =
        slots_per_ciphertext - used_slots_last_ciphertext;
    const double slot_utilization = divide_or_zero(
        static_cast<double>(data.size()),
        static_cast<double>(row_chunk_count * slots_per_ciphertext));

    double encode_time_ms = 0.0;
    double encrypt_time_ms = 0.0;
    double he_eval_time_ms = 0.0;
    double decrypt_time_ms = 0.0;
    double decode_time_ms = 0.0;
    double result_checksum = 0.0;

    std::vector<Ciphertext<DCRTPoly>> encrypted_lookup_values(config.channel_domain);
    for (std::size_t key = 0; key < config.channel_domain; ++key) {
        std::vector<double> value_slots(slots_per_ciphertext, channel_lookup_value(key));

        const Timer encode_timer;
        Plaintext value_plaintext = cc->MakeCKKSPackedPlaintext(
            value_slots, 1, 0, nullptr, static_cast<uint32_t>(slots_per_ciphertext));
        encode_time_ms += encode_timer.elapsed_ms();

        const Timer encrypt_timer;
        encrypted_lookup_values[key] = cc->Encrypt(keys.publicKey, value_plaintext);
        encrypt_time_ms += encrypt_timer.elapsed_ms();
    }

    for (std::size_t chunk = 0; chunk < row_chunk_count; ++chunk) {
        const std::size_t offset = chunk * slots_per_ciphertext;
        const std::size_t used_slots = std::min(slots_per_ciphertext, data.size() - offset);

        bool has_chunk_result = false;
        Ciphertext<DCRTPoly> chunk_result;

        for (std::size_t key = 0; key < config.channel_domain; ++key) {
            std::vector<double> mask_slots(slots_per_ciphertext, 0.0);
            for (std::size_t i = 0; i < used_slots; ++i) {
                mask_slots[i] = data.channel_id[offset + i] == key ? 1.0 : 0.0;
            }

            const Timer encode_timer;
            Plaintext mask_plaintext = cc->MakeCKKSPackedPlaintext(
                mask_slots, 1, 0, nullptr, static_cast<uint32_t>(slots_per_ciphertext));
            encode_time_ms += encode_timer.elapsed_ms();

            const Timer encrypt_timer;
            auto mask_ciphertext = cc->Encrypt(keys.publicKey, mask_plaintext);
            encrypt_time_ms += encrypt_timer.elapsed_ms();

            const Timer eval_timer;
            auto term = cc->EvalMult(mask_ciphertext, encrypted_lookup_values[key]);
            if (has_chunk_result) {
                chunk_result = cc->EvalAdd(chunk_result, term);
            } else {
                chunk_result = term;
                has_chunk_result = true;
            }
            he_eval_time_ms += eval_timer.elapsed_ms();
        }

        Plaintext decrypted;
        const Timer decrypt_timer;
        cc->Decrypt(keys.secretKey, chunk_result, &decrypted);
        decrypt_time_ms += decrypt_timer.elapsed_ms();

        const Timer decode_timer;
        decrypted->SetLength(used_slots);
        const auto values = decrypted->GetRealPackedValue();
        if (values.size() < used_slots) {
            throw std::runtime_error("lookup decrypt produced fewer slots than expected");
        }
        for (std::size_t i = 0; i < used_slots; ++i) {
            result_checksum += values[i];
        }
        decode_time_ms += decode_timer.elapsed_ms();
    }

    const double total_he_time_ms =
        encode_time_ms + encrypt_time_ms + he_eval_time_ms + decrypt_time_ms + decode_time_ms;
    const double absolute_error = std::abs(result_checksum - baseline_value);
    const double relative_error = divide_or_zero(absolute_error, std::abs(baseline_value));

    BenchmarkResult result;
    result.operation = "lookup_channel_weight_vector";
    result.backend = "openfhe_ckks_lookup";
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
    result.result_value = result_checksum;
    result.baseline_value = baseline_value;
    result.absolute_error = absolute_error;
    result.relative_error = relative_error;
    result.ciphertext_count = config.channel_domain + (row_chunk_count * config.channel_domain);
    result.slots_per_ciphertext = slots_per_ciphertext;
    result.used_slots_last_ciphertext = used_slots_last_ciphertext;
    result.padding_slots_last_ciphertext = padding_slots_last_ciphertext;
    result.slot_utilization = slot_utilization;
    result.requested_ring_dimension = config.requested_ring_dimension;
    result.actual_ring_dimension = actual_ring_dimension;
    result.security_bits = 128;
    result.multiplicative_depth = requested_depth;
    result.scaling_mod_size = config.scaling_mod_size;
    result.first_mod_size = config.first_mod_size;
    result.rotation_count_reported = 0;
    result.notes =
#ifdef _OPENMP
        "src_optimized;lookup_only;channel_domain_encrypted_onehot_masks;encrypted_lookup_values_preencrypted_once;no_amount;no_weighted_sum;omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total";
#else
        "src_optimized;lookup_only;channel_domain_encrypted_onehot_masks;encrypted_lookup_values_preencrypted_once;no_amount;no_weighted_sum;openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total";
#endif

    cc->ClearStaticMapsAndVectors();
    return result;
}

#endif

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliArgs args = parse_args(argc, argv);

        std::cout << "Loading transactions: " << args.data_path << '\n';
        const Timer load_timer;
        const Transactions data = load_transactions_csv(args.data_path, args.max_rows);
        std::cout << "Loaded " << data.size() << " rows in "
                  << load_timer.elapsed_ms() << " ms\n";
        if (args.max_rows != 0) {
            std::cout << "Applied --max-rows " << args.max_rows << '\n';
        }
        validate_channel_domain(data, args.channel_domain);

        std::vector<BenchmarkResult> plain_results;
        plain_results.reserve(args.repeat_count);
        for (std::size_t repeat_index = 1; repeat_index <= args.repeat_count; ++repeat_index) {
            auto plain = run_plain_lookup(data, 1);
            plain.notes += ";" + repeat_note(repeat_index, args.repeat_count);
            plain_results.push_back(plain);
        }
        const BenchmarkResult plain_summary = summarize_results(
            plain_results,
            "lookup_channel_weight_vector",
            "plain_cpp");

        if (wants_plain_cpp(args.backend)) {
            for (std::size_t repeat_index = 0;
                 repeat_index < plain_results.size();
                 ++repeat_index) {
                append_result_csv(args.results_path, plain_results[repeat_index]);
                std::cout << "Ran plain_cpp:lookup_channel_weight_vector"
                          << " repeat=" << (repeat_index + 1) << '/' << args.repeat_count
                          << " in " << plain_results[repeat_index].plain_time_ms
                          << " ms value=" << plain_results[repeat_index].result_value << '\n';
            }
            append_result_csv(args.results_path, plain_summary);
            std::cout << "Summary plain_cpp:lookup_channel_weight_vector"
                      << " repeats=" << args.repeat_count
                      << " avg_plain=" << plain_summary.plain_time_ms << " ms\n";
        }

        if (wants_openfhe_ckks(args.backend)) {
#ifdef UTILITY_BENCH_WITH_OPENFHE
            CkksLookupConfig config;
            config.requested_ring_dimension = args.ckks_ring_dim;
            config.batch_size = args.ckks_batch_size;
            config.multiplicative_depth = args.ckks_depth;
            config.scaling_mod_size = args.ckks_scale_bits;
            config.first_mod_size = args.ckks_first_mod_bits;
            config.channel_domain = args.channel_domain;

            for (const std::size_t thread_count : args.thread_counts) {
                std::vector<BenchmarkResult> he_results;
                he_results.reserve(args.repeat_count);
                for (std::size_t repeat_index = 1;
                     repeat_index <= args.repeat_count;
                     ++repeat_index) {
                    auto result = run_openfhe_lookup(
                        data,
                        thread_count,
                        plain_summary.baseline_value,
                        plain_summary.plain_time_ms,
                        config);
                    result.notes += ";" + repeat_note(repeat_index, args.repeat_count);
                    he_results.push_back(result);
                    append_result_csv(args.results_path, he_results.back());
                    std::cout << "Ran " << result.backend << ':' << result.operation
                              << " repeat=" << repeat_index << '/' << args.repeat_count
                              << " threads=" << result.threads
                              << " total=" << result.total_he_time_ms
                              << " ms eval=" << result.he_eval_time_ms
                              << " ms abs_error=" << result.absolute_error
                              << " rel_error=" << result.relative_error << '\n';
                }
                const BenchmarkResult he_summary = summarize_results(
                    he_results,
                    he_results.front().operation,
                    he_results.front().backend);
                append_result_csv(args.results_path, he_summary);
                std::cout << "Summary " << he_summary.backend << ':'
                          << he_summary.operation
                          << " threads=" << he_summary.threads
                          << " repeats=" << args.repeat_count
                          << " avg_total=" << he_summary.total_he_time_ms
                          << " ms avg_eval=" << he_summary.he_eval_time_ms
                          << " ms avg_rel_error=" << he_summary.relative_error << '\n';
            }
#else
            throw std::runtime_error(
                "lookup_opt_bench was built without OpenFHE; rebuild with "
                "-DUTILITY_BENCH_WITH_OPENFHE=ON");
#endif
        }

        std::cout << "Wrote results: " << args.results_path << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
