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
#include <utility>
#include <vector>

namespace {

enum class BackendMode {
    PlainCpp,
    OpenFheCkks,
    All,
};

enum class AddVariant {
    LinearAdd,
    TreeAdd,
    Both,
};

struct CliArgs {
    std::string data_path = "data/generated/medium_100k/transactions.csv";
    std::filesystem::path results_path = "results/optimized_add/sum_amount_opt.csv";
    BackendMode backend = BackendMode::All;
    AddVariant variant = AddVariant::Both;
    std::vector<std::size_t> thread_counts = {1};
    std::size_t repeat_count = 3;
    std::size_t max_rows = 0;
    std::size_t ckks_ring_dim = 8192;
    std::size_t ckks_batch_size = 0;
    std::size_t ckks_depth = 1;
    std::size_t ckks_scale_bits = 30;
    std::size_t ckks_first_mod_bits = 40;
};

struct CkksOptConfig {
    std::size_t requested_ring_dimension = 8192;
    std::size_t batch_size = 0;
    std::size_t multiplicative_depth = 1;
    std::size_t scaling_mod_size = 30;
    std::size_t first_mod_size = 40;
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--data transactions.csv] "
        << "[--backend plain_cpp|openfhe_ckks|all] "
        << "[--variant linear_add|tree_add|both] "
        << "[--threads 1] [--repeat 3] [--max-rows 100000] "
        << "[--ckks-ring-dim 8192] [--ckks-batch-size 0] "
        << "[--ckks-depth 1] [--ckks-scale-bits 30] [--ckks-first-mod-bits 40] "
        << "[--results results/optimized_add/sum_amount_opt.csv]\n";
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

AddVariant parse_add_variant(const std::string& value) {
    if (value == "linear_add") {
        return AddVariant::LinearAdd;
    }
    if (value == "tree_add") {
        return AddVariant::TreeAdd;
    }
    if (value == "both") {
        return AddVariant::Both;
    }
    throw std::runtime_error("unknown variant: " + value);
}

bool wants_plain_cpp(BackendMode backend) {
    return backend == BackendMode::PlainCpp || backend == BackendMode::All;
}

bool wants_openfhe_ckks(BackendMode backend) {
    return backend == BackendMode::OpenFheCkks || backend == BackendMode::All;
}

bool wants_variant(AddVariant requested, AddVariant candidate) {
    return requested == AddVariant::Both || requested == candidate;
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
            continue;
        }

        if (flag == "--results") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--results requires a path");
            }
            args.results_path = argv[++i];
            continue;
        }

        if (flag == "--backend") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--backend requires a value");
            }
            args.backend = parse_backend_mode(argv[++i]);
            continue;
        }

        if (flag == "--variant") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--variant requires a value");
            }
            args.variant = parse_add_variant(argv[++i]);
            continue;
        }

        if (flag == "--threads" || flag == "--thread-counts") {
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
            continue;
        }

        if (flag == "--repeat") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--repeat requires a value");
            }
            args.repeat_count = static_cast<std::size_t>(std::stoull(argv[++i]));
            if (args.repeat_count == 0) {
                throw std::runtime_error("--repeat must be positive");
            }
            continue;
        }

        if (flag == "--max-rows") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--max-rows requires a value");
            }
            args.max_rows = static_cast<std::size_t>(std::stoull(argv[++i]));
            continue;
        }

        if (flag == "--ckks-ring-dim") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--ckks-ring-dim requires a value");
            }
            args.ckks_ring_dim = static_cast<std::size_t>(std::stoull(argv[++i]));
            continue;
        }

        if (flag == "--ckks-batch-size") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--ckks-batch-size requires a value");
            }
            args.ckks_batch_size = static_cast<std::size_t>(std::stoull(argv[++i]));
            continue;
        }

        if (flag == "--ckks-depth") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--ckks-depth requires a value");
            }
            args.ckks_depth = static_cast<std::size_t>(std::stoull(argv[++i]));
            if (args.ckks_depth == 0) {
                throw std::runtime_error("--ckks-depth must be positive");
            }
            continue;
        }

        if (flag == "--ckks-scale-bits") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--ckks-scale-bits requires a value");
            }
            args.ckks_scale_bits = static_cast<std::size_t>(std::stoull(argv[++i]));
            if (args.ckks_scale_bits == 0) {
                throw std::runtime_error("--ckks-scale-bits must be positive");
            }
            continue;
        }

        if (flag == "--ckks-first-mod-bits") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--ckks-first-mod-bits requires a value");
            }
            args.ckks_first_mod_bits = static_cast<std::size_t>(std::stoull(argv[++i]));
            if (args.ckks_first_mod_bits == 0) {
                throw std::runtime_error("--ckks-first-mod-bits must be positive");
            }
            continue;
        }

        throw std::runtime_error("unknown argument: " + flag);
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

    double min_total = results.front().total_he_time_ms;
    double max_total = results.front().total_he_time_ms;
    double min_eval = results.front().he_eval_time_ms;
    double max_eval = results.front().he_eval_time_ms;
    double min_plain = results.front().plain_time_ms;
    double max_plain = results.front().plain_time_ms;
    for (const auto& result : results) {
        min_total = std::min(min_total, result.total_he_time_ms);
        max_total = std::max(max_total, result.total_he_time_ms);
        min_eval = std::min(min_eval, result.he_eval_time_ms);
        max_eval = std::max(max_eval, result.he_eval_time_ms);
        min_plain = std::min(min_plain, result.plain_time_ms);
        max_plain = std::max(max_plain, result.plain_time_ms);
    }

    std::ostringstream notes;
    notes << "repeat_summary;repeat_count=" << results.size()
          << ";min_plain_ms=" << min_plain
          << ";max_plain_ms=" << max_plain
          << ";min_eval_ms=" << min_eval
          << ";max_eval_ms=" << max_eval
          << ";min_total_ms=" << min_total
          << ";max_total_ms=" << max_total;
    summary.notes = notes.str();
    return summary;
}

BenchmarkResult run_plain_sum_amount(const Transactions& data, std::size_t thread_count) {
    const std::size_t actual_threads = effective_thread_count(thread_count, data.size());
    const Timer timer;
    const double value = plaintext_sum_amount(data, actual_threads);
    const double plain_time_ms = timer.elapsed_ms();

    BenchmarkResult result;
    result.operation = "sum_amount";
    result.backend = "plain_cpp";
    result.rows = data.size();
    result.threads = actual_threads;
    result.plain_time_ms = plain_time_ms;
    result.result_value = value;
    result.baseline_value = value;
    result.notes = "optimized_track_reference_plain;compute_only_no_io";
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

std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> tree_reduce_add(
    const lbcrypto::CryptoContext<lbcrypto::DCRTPoly>& cc,
    std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> values) {
    if (values.empty()) {
        return values;
    }

    while (values.size() > 1) {
        std::vector<lbcrypto::Ciphertext<lbcrypto::DCRTPoly>> next;
        next.reserve((values.size() + 1) / 2);
        for (std::size_t i = 0; i < values.size(); i += 2) {
            if (i + 1 < values.size()) {
                next.push_back(cc->EvalAdd(values[i], values[i + 1]));
            } else {
                next.push_back(values[i]);
            }
        }
        values = std::move(next);
    }

    return values;
}

BenchmarkResult run_openfhe_sum_amount_variant(
    const Transactions& data,
    std::size_t thread_count,
    double baseline_value,
    double plain_time_ms,
    const CkksOptConfig& config,
    AddVariant variant) {
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
    bool has_total = false;
    Ciphertext<DCRTPoly> total_ciphertext;
    std::vector<Ciphertext<DCRTPoly>> chunk_sums;
    if (variant == AddVariant::TreeAdd) {
        chunk_sums.reserve(ciphertext_count);
    }

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
        if (variant == AddVariant::TreeAdd) {
            chunk_sums.push_back(chunk_sum);
        } else if (has_total) {
            total_ciphertext = cc->EvalAdd(total_ciphertext, chunk_sum);
        } else {
            total_ciphertext = chunk_sum;
            has_total = true;
        }
        he_eval_time_ms += eval_timer.elapsed_ms();
    }

    if (variant == AddVariant::TreeAdd) {
        const Timer eval_timer;
        auto reduced = tree_reduce_add(cc, std::move(chunk_sums));
        if (reduced.empty()) {
            throw std::runtime_error("tree_add produced no chunk sums");
        }
        total_ciphertext = reduced[0];
        has_total = true;
        he_eval_time_ms += eval_timer.elapsed_ms();
    }
    if (!has_total) {
        throw std::runtime_error("no ciphertext total was produced");
    }

    Plaintext decrypted;
    const Timer decrypt_timer;
    cc->Decrypt(keys.secretKey, total_ciphertext, &decrypted);
    const double decrypt_time_ms = decrypt_timer.elapsed_ms();

    const Timer decode_timer;
    decrypted->SetLength(1);
    const auto decoded_values = decrypted->GetCKKSPackedValue();
    if (decoded_values.empty()) {
        throw std::runtime_error("OpenFHE CKKS decrypt produced no values");
    }
    const double result_value = decoded_values[0].real();
    const double decode_time_ms = decode_timer.elapsed_ms();

    const double total_he_time_ms =
        encode_time_ms + encrypt_time_ms + he_eval_time_ms + decrypt_time_ms + decode_time_ms;
    const double absolute_error = std::abs(result_value - baseline_value);
    const double relative_error = divide_or_zero(absolute_error, std::abs(baseline_value));

    const std::string variant_name =
        variant == AddVariant::TreeAdd ? "tree_add" : "linear_add";

    BenchmarkResult result;
    result.operation = "sum_amount_opt_" + variant_name;
    result.backend = "openfhe_ckks_opt";
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
    result.notes = "src_optimized;sum_amount_only;variant=" + variant_name +
#ifdef _OPENMP
        ";omp_set_num_threads;setup_recorded_separately;encrypt_decrypt_in_total";
#else
        ";openmp_not_seen_by_runner;setup_recorded_separately;encrypt_decrypt_in_total";
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

        const BenchmarkResult baseline = run_plain_sum_amount(data, 1);
        std::vector<BenchmarkResult> plain_results;
        plain_results.reserve(args.repeat_count);
        plain_results.push_back(baseline);
        for (std::size_t repeat_index = 2; repeat_index <= args.repeat_count; ++repeat_index) {
            plain_results.push_back(run_plain_sum_amount(data, 1));
        }
        for (std::size_t repeat_index = 0; repeat_index < plain_results.size(); ++repeat_index) {
            plain_results[repeat_index].notes += ";" +
                repeat_note(repeat_index + 1, args.repeat_count);
        }
        const BenchmarkResult plain_summary =
            summarize_results(plain_results, "sum_amount", "plain_cpp");

        if (wants_plain_cpp(args.backend)) {
            for (std::size_t repeat_index = 0; repeat_index < plain_results.size(); ++repeat_index) {
                const auto& plain_result = plain_results[repeat_index];
                append_result_csv(args.results_path, plain_result);
                std::cout << "Ran plain_cpp:sum_amount repeat="
                          << (repeat_index + 1) << '/' << args.repeat_count
                          << " threads=1 in "
                          << plain_result.plain_time_ms << " ms value="
                          << plain_result.result_value << '\n';
            }
            append_result_csv(args.results_path, plain_summary);
            std::cout << "Summary plain_cpp:sum_amount repeats="
                      << args.repeat_count << " avg_plain="
                      << plain_summary.plain_time_ms << " ms\n";
        }

        if (wants_openfhe_ckks(args.backend)) {
#ifdef UTILITY_BENCH_WITH_OPENFHE
            CkksOptConfig config;
            config.requested_ring_dimension = args.ckks_ring_dim;
            config.batch_size = args.ckks_batch_size;
            config.multiplicative_depth = args.ckks_depth;
            config.scaling_mod_size = args.ckks_scale_bits;
            config.first_mod_size = args.ckks_first_mod_bits;

            for (const std::size_t thread_count : args.thread_counts) {
                for (const AddVariant variant : {AddVariant::LinearAdd, AddVariant::TreeAdd}) {
                    if (!wants_variant(args.variant, variant)) {
                        continue;
                    }
                    std::vector<BenchmarkResult> he_results;
                    he_results.reserve(args.repeat_count);
                    for (std::size_t repeat_index = 1;
                         repeat_index <= args.repeat_count;
                         ++repeat_index) {
                        auto result = run_openfhe_sum_amount_variant(
                            data,
                            thread_count,
                            plain_summary.baseline_value,
                            plain_summary.plain_time_ms,
                            config,
                            variant);
                        result.notes += ";" + repeat_note(repeat_index, args.repeat_count);
                        he_results.push_back(result);
                        append_result_csv(args.results_path, he_results.back());
                        std::cout << "Ran " << result.backend << ':' << result.operation
                                  << " repeat=" << repeat_index << '/'
                                  << args.repeat_count
                                  << " threads=" << result.threads
                                  << " total=" << result.total_he_time_ms
                                  << " ms eval=" << result.he_eval_time_ms
                                  << " ms abs_error=" << result.absolute_error
                                  << " rel_error=" << result.relative_error << '\n';
                    }
                    const BenchmarkResult summary = summarize_results(
                        he_results,
                        he_results.front().operation,
                        he_results.front().backend);
                    append_result_csv(args.results_path, summary);
                    std::cout << "Summary " << summary.backend << ':'
                              << summary.operation
                              << " threads=" << summary.threads
                              << " repeats=" << args.repeat_count
                              << " avg_total=" << summary.total_he_time_ms
                              << " ms avg_eval=" << summary.he_eval_time_ms
                              << " ms avg_rel_error=" << summary.relative_error
                              << '\n';
                }
            }
#else
            throw std::runtime_error(
                "sum_amount_opt_bench was built without OpenFHE; rebuild with "
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
