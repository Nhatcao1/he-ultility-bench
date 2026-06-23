#include "csv_loader.h"
#include "plain_ops.h"
#include "resource_usage.h"
#include "result_writer.h"
#include "timer.h"

#ifdef UTILITY_BENCH_WITH_OPENFHE
#include "binfhecontext.h"

#ifdef _OPENMP
#include <omp.h>
#endif
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kProductDomain = 20;

// Arbitrary, non-linear fixed-point-ish product risk codes.
// Interpret 11 as 1.1, 7 as 0.7, etc. The benchmark itself stays integer.
constexpr std::array<std::uint32_t, kProductDomain> kProductRiskCode = {
    11, 7, 15, 4, 9, 13, 6, 8, 12, 3,
    5, 10, 2, 14, 18, 16, 1, 19, 17, 20,
};

enum class BackendMode {
    PlainCpp,
    BinFheLut,
    All,
};

struct CliArgs {
    std::string data_path = "data/generated/medium_100k/transactions.csv";
    std::filesystem::path results_path = "results/original/binfhe_product_lut.csv";
    BackendMode backend = BackendMode::All;
    std::vector<std::size_t> thread_counts = {1};
    std::size_t repeat_count = 3;
    std::size_t max_rows = 1000;
    std::uint32_t binfhe_ring_dim = 8192;
    std::uint32_t binfhe_logq = 12;
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--data transactions.csv] "
        << "[--backend plain_cpp|binfhe_lut|all] "
        << "[--threads 1] [--repeat 3] [--max-rows 1000] "
        << "[--binfhe-ring-dim 8192] [--binfhe-logq 12] "
        << "[--results results/original/binfhe_product_lut.csv]\n";
}

BackendMode parse_backend_mode(const std::string& value) {
    if (value == "plain_cpp") {
        return BackendMode::PlainCpp;
    }
    if (value == "binfhe_lut") {
        return BackendMode::BinFheLut;
    }
    if (value == "all") {
        return BackendMode::All;
    }
    throw std::runtime_error("unknown backend: " + value);
}

bool wants_plain_cpp(BackendMode backend) {
    return backend == BackendMode::PlainCpp || backend == BackendMode::All;
}

bool wants_binfhe_lut(BackendMode backend) {
    return backend == BackendMode::BinFheLut || backend == BackendMode::All;
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
        } else if (flag == "--binfhe-ring-dim") {
            args.binfhe_ring_dim = static_cast<std::uint32_t>(
                parse_size_arg(i, argc, argv, flag));
            if (args.binfhe_ring_dim == 0) {
                throw std::runtime_error("--binfhe-ring-dim must be positive");
            }
        } else if (flag == "--binfhe-logq") {
            args.binfhe_logq = static_cast<std::uint32_t>(parse_size_arg(i, argc, argv, flag));
            if (args.binfhe_logq == 0) {
                throw std::runtime_error("--binfhe-logq must be positive");
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

std::string repeat_note(std::size_t repeat_index, std::size_t repeat_count) {
    return "repeat_index=" + std::to_string(repeat_index) +
           ";repeat_count=" + std::to_string(repeat_count);
}

std::uint32_t product_risk_code(std::size_t product_id) {
    if (product_id >= kProductDomain) {
        throw std::runtime_error("product_id outside product LUT domain: " +
                                 std::to_string(product_id));
    }
    return kProductRiskCode[product_id];
}

void validate_product_domain(const Transactions& data) {
    for (const std::size_t product_id : data.product_id) {
        (void)product_risk_code(product_id);
    }
}

double plaintext_product_lut_checksum(
    const Transactions& data,
    std::size_t thread_count) {
    return parallel_sum(data.size(), thread_count, [&](std::size_t row) {
        return static_cast<double>(product_risk_code(data.product_id[row]));
    });
}

#ifdef UTILITY_BENCH_WITH_OPENFHE

lbcrypto::NativeInteger product_risk_lut_function(
    lbcrypto::NativeInteger input,
    lbcrypto::NativeInteger plaintext_modulus) {
    const std::uint64_t product_id = input.ConvertToInt();
    const std::uint64_t modulus = plaintext_modulus.ConvertToInt();
    if (product_id < kProductDomain) {
        return lbcrypto::NativeInteger(kProductRiskCode[product_id] % modulus);
    }
    return lbcrypto::NativeInteger(0);
}

std::size_t configure_openfhe_threads(std::size_t requested_threads) {
#ifdef _OPENMP
    omp_set_num_threads(static_cast<int>(requested_threads));
    return static_cast<std::size_t>(std::max(1, omp_get_max_threads()));
#else
    return requested_threads;
#endif
}

#endif

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
        throw std::runtime_error("cannot summarize empty BinFHE LUT result list");
    }

    BenchmarkResult summary = results.front();
    summary.operation = operation + "_summary_avg";
    summary.backend = backend + "_summary";
    summary.plain_time_ms = average_field(results, &BenchmarkResult::plain_time_ms);
    summary.setup_time_ms = average_field(results, &BenchmarkResult::setup_time_ms);
    summary.encrypt_time_ms = average_field(results, &BenchmarkResult::encrypt_time_ms);
    summary.he_eval_time_ms = average_field(results, &BenchmarkResult::he_eval_time_ms);
    summary.decrypt_time_ms = average_field(results, &BenchmarkResult::decrypt_time_ms);
    summary.total_he_time_ms = average_field(results, &BenchmarkResult::total_he_time_ms);
    summary.operation_slowdown = average_field(results, &BenchmarkResult::operation_slowdown);
    summary.end_to_end_slowdown = average_field(results, &BenchmarkResult::end_to_end_slowdown);
    summary.result_value = average_field(results, &BenchmarkResult::result_value);
    summary.baseline_value = average_field(results, &BenchmarkResult::baseline_value);
    summary.absolute_error = average_field(results, &BenchmarkResult::absolute_error);
    summary.relative_error = average_field(results, &BenchmarkResult::relative_error);
    summary.ciphertext_payload_kb =
        average_field(results, &BenchmarkResult::ciphertext_payload_kb);
    summary.notes = "repeat_summary;repeat_count=" + std::to_string(results.size()) +
        ";real_calculation_ms=he_eval_time_ms" +
        ";overall_lifecycle_ms=total_he_time_ms;setup_keygen_excluded_from_total";
    return summary;
}

BenchmarkResult run_plain_product_lut(
    const Transactions& data,
    std::size_t thread_count) {
    const std::size_t actual_threads = effective_thread_count(thread_count, data.size());
    const Timer timer;
    const double value = plaintext_product_lut_checksum(data, actual_threads);
    const double plain_time_ms = timer.elapsed_ms();

    BenchmarkResult result;
    result.operation = "binfhe_lut_product_risk_code";
    result.backend = "plain_cpp";
    result.rows = data.size();
    result.threads = actual_threads;
    result.plain_time_ms = plain_time_ms;
    result.result_value = value;
    result.baseline_value = value;
    result.peak_rss_kb = current_peak_rss_kb();
    result.notes = "product_id_to_risk_code_plain_table_lookup";
    return result;
}

#ifdef UTILITY_BENCH_WITH_OPENFHE

BenchmarkResult run_binfhe_product_lut(
    const Transactions& data,
    std::size_t thread_count,
    double baseline_value,
    double plain_time_ms,
    std::uint32_t ring_dim,
    std::uint32_t logq) {
    using lbcrypto::BinFHEContext;
    using lbcrypto::GINX;
    using lbcrypto::LARGE_DIM;
    using lbcrypto::LWEPlaintext;
    using lbcrypto::LWECiphertext;
    using lbcrypto::STD128;

    if (data.size() == 0) {
        throw std::runtime_error("BinFHE product LUT requires at least one row");
    }
    validate_product_domain(data);

    const std::size_t openfhe_threads = configure_openfhe_threads(thread_count);

    const Timer setup_timer;
    auto cc = BinFHEContext();
    cc.GenerateBinFHEContext(STD128, true, logq, ring_dim, GINX, false);
    auto secret_key = cc.KeyGen();
    cc.BTKeyGen(secret_key);
    const std::uint64_t plaintext_modulus = cc.GetMaxPlaintextSpace().ConvertToInt();
    const std::uint64_t max_input = kProductDomain - 1;
    const std::uint64_t max_output =
        *std::max_element(kProductRiskCode.begin(), kProductRiskCode.end());
    const std::uint64_t required_plaintext_modulus =
        std::max(max_input, max_output) + 1;
    if (plaintext_modulus < required_plaintext_modulus) {
        throw std::runtime_error(
            "BinFHE plaintext modulus is too small for product risk LUT: p=" +
            std::to_string(plaintext_modulus) +
            ", required>=" + std::to_string(required_plaintext_modulus) +
            ". Increase --binfhe-ring-dim, e.g. --binfhe-ring-dim 8192");
    }
    auto lut = cc.GenerateLUTviaFunction(
        product_risk_lut_function,
        lbcrypto::NativeInteger(plaintext_modulus));
    const double setup_time_ms = setup_timer.elapsed_ms();

    const auto lwe_params = cc.GetParams()->GetLWEParams();
    const std::size_t lwe_dimension = lwe_params->Getn();
    const std::size_t actual_ring_dimension = lwe_params->GetN();
    const std::size_t lwe_modulus_bits = lwe_params->Getq().GetMSB();

    std::vector<LWECiphertext> encrypted_products;
    encrypted_products.reserve(data.size());
    double encrypt_time_ms = 0.0;
    for (const std::size_t product_id : data.product_id) {
        const Timer encrypt_timer;
        encrypted_products.push_back(cc.Encrypt(
            secret_key,
            static_cast<LWEPlaintext>(product_id),
            LARGE_DIM,
            plaintext_modulus));
        encrypt_time_ms += encrypt_timer.elapsed_ms();
    }

    std::vector<LWECiphertext> encrypted_risk_codes;
    encrypted_risk_codes.reserve(data.size());
    double he_eval_time_ms = 0.0;
    for (auto& encrypted_product : encrypted_products) {
        const Timer eval_timer;
        encrypted_risk_codes.push_back(cc.EvalFunc(encrypted_product, lut));
        he_eval_time_ms += eval_timer.elapsed_ms();
    }

    double decrypt_time_ms = 0.0;
    double result_checksum = 0.0;
    for (auto& encrypted_code : encrypted_risk_codes) {
        LWEPlaintext decrypted = 0;
        const Timer decrypt_timer;
        cc.Decrypt(secret_key, encrypted_code, &decrypted, plaintext_modulus);
        decrypt_time_ms += decrypt_timer.elapsed_ms();
        result_checksum += static_cast<double>(decrypted);
    }

    const double total_he_time_ms = encrypt_time_ms + he_eval_time_ms + decrypt_time_ms;
    const double absolute_error = std::abs(result_checksum - baseline_value);
    const double relative_error = divide_or_zero(absolute_error, std::abs(baseline_value));
    const std::size_t ciphertext_count = data.size() * 2;
    const std::size_t ciphertext_payload_bytes = estimate_lwe_ciphertext_payload_bytes(
        ciphertext_count,
        lwe_dimension,
        lwe_modulus_bits);

    BenchmarkResult result;
    result.operation = "binfhe_lut_product_risk_code";
    result.backend = "binfhe_lut";
    result.rows = data.size();
    result.threads = openfhe_threads;
    result.plain_time_ms = plain_time_ms;
    result.setup_time_ms = setup_time_ms;
    result.encrypt_time_ms = encrypt_time_ms;
    result.he_eval_time_ms = he_eval_time_ms;
    result.decrypt_time_ms = decrypt_time_ms;
    result.total_he_time_ms = total_he_time_ms;
    result.operation_slowdown = divide_or_zero(he_eval_time_ms, plain_time_ms);
    result.end_to_end_slowdown = divide_or_zero(total_he_time_ms, plain_time_ms);
    result.result_value = result_checksum;
    result.baseline_value = baseline_value;
    result.absolute_error = absolute_error;
    result.relative_error = relative_error;
    result.ciphertext_count = ciphertext_count;
    result.slots_per_ciphertext = 1;
    result.used_slots_last_ciphertext = 1;
    result.padding_slots_last_ciphertext = 0;
    result.slot_utilization = 1.0;
    result.requested_ring_dimension = ring_dim;
    result.actual_ring_dimension = actual_ring_dimension;
    result.security_bits = 128;
    result.scaling_mod_size = lwe_modulus_bits;
    result.first_mod_size = static_cast<std::size_t>(logq);
    result.peak_rss_kb = current_peak_rss_kb();
    result.ciphertext_payload_bytes = ciphertext_payload_bytes;
    result.ciphertext_payload_kb = bytes_to_kb(ciphertext_payload_bytes);
    result.notes =
#ifdef _OPENMP
        "product_id_scalar_lut;BinFHE_EvalFunc;programmable_bootstrap;ciphertext_payload_estimate_lwe;omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total;plaintext_modulus=" +
        std::to_string(plaintext_modulus);
#else
        "product_id_scalar_lut;BinFHE_EvalFunc;programmable_bootstrap;ciphertext_payload_estimate_lwe;openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total;plaintext_modulus=" +
        std::to_string(plaintext_modulus);
#endif

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
        validate_product_domain(data);

        std::vector<BenchmarkResult> plain_results;
        plain_results.reserve(args.repeat_count);
        for (std::size_t repeat_index = 1; repeat_index <= args.repeat_count; ++repeat_index) {
            auto plain = run_plain_product_lut(data, 1);
            plain.notes += ";" + repeat_note(repeat_index, args.repeat_count);
            plain_results.push_back(plain);
        }
        const BenchmarkResult plain_summary = summarize_results(
            plain_results,
            "binfhe_lut_product_risk_code",
            "plain_cpp");

        if (wants_plain_cpp(args.backend)) {
            for (std::size_t repeat_index = 0;
                 repeat_index < plain_results.size();
                 ++repeat_index) {
                append_result_csv(args.results_path, plain_results[repeat_index]);
                std::cout << "Ran plain_cpp:binfhe_lut_product_risk_code"
                          << " repeat=" << (repeat_index + 1) << '/' << args.repeat_count
                          << " in " << plain_results[repeat_index].plain_time_ms
                          << " ms value=" << plain_results[repeat_index].result_value << '\n';
            }
            append_result_csv(args.results_path, plain_summary);
            std::cout << "Summary plain_cpp:binfhe_lut_product_risk_code"
                      << " repeats=" << args.repeat_count
                      << " avg_plain=" << plain_summary.plain_time_ms << " ms\n";
        }

        if (wants_binfhe_lut(args.backend)) {
#ifdef UTILITY_BENCH_WITH_OPENFHE
            for (const std::size_t thread_count : args.thread_counts) {
                std::vector<BenchmarkResult> he_results;
                he_results.reserve(args.repeat_count);
                for (std::size_t repeat_index = 1;
                     repeat_index <= args.repeat_count;
                     ++repeat_index) {
                    auto result = run_binfhe_product_lut(
                        data,
                        thread_count,
                        plain_summary.baseline_value,
                        plain_summary.plain_time_ms,
                        args.binfhe_ring_dim,
                        args.binfhe_logq);
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
                "binfhe_lut_bench was built without OpenFHE; rebuild with "
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
