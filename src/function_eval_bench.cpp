#include "timer.h"

#ifdef UTILITY_BENCH_WITH_OPENFHE
#include "openfhe.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

const std::vector<double> kTinyTrigInput{
    -0.75,
    -0.50,
    -0.25,
    0.00,
    0.25,
    0.50,
    0.75,
};

enum class BackendMode {
    PlainCpp,
    OpenFheCkks,
    All,
};

struct CliArgs {
    std::vector<std::string> benches = {"all_trig_tiny"};
    std::vector<std::size_t> degrees = {15, 30, 45};
    std::vector<std::size_t> thread_counts = {1, 4, 8};
    BackendMode backend = BackendMode::PlainCpp;
    std::filesystem::path results_path = "results/function_eval_trig_tiny.csv";
    std::size_t repeat_count = 1;
    double lower_bound = -0.75;
    double upper_bound = 0.75;
    std::size_t ckks_ring_dim = 0;
    std::size_t ckks_batch_size = 0;
    std::size_t ckks_depth = 0;  // 0 means choose from degree.
    std::size_t ckks_scaling_mod_size = 50;
    std::size_t ckks_first_mod_size = 60;
};

struct FunctionSpec {
    std::string operation;
    std::string function_name;
    double (*plain_fn)(double);
};

struct FunctionEvalResult {
    std::string operation;
    std::string backend;
    std::string function_name;
    std::size_t input_count = 0;
    std::size_t degree = 0;
    double lower_bound = 0.0;
    double upper_bound = 0.0;
    std::size_t threads = 1;
    double plain_time_ms = 0.0;
    double setup_time_ms = 0.0;
    double encode_time_ms = 0.0;
    double encrypt_time_ms = 0.0;
    double he_eval_time_ms = 0.0;
    double decrypt_time_ms = 0.0;
    double decode_time_ms = 0.0;
    double total_he_time_ms = 0.0;
    double operation_slowdown = 0.0;
    double end_to_end_slowdown = 0.0;
    double mean_absolute_error = 0.0;
    double max_absolute_error = 0.0;
    std::size_t requested_ring_dimension = 0;
    std::size_t actual_ring_dimension = 0;
    std::size_t security_bits = 0;
    std::size_t multiplicative_depth = 0;
    std::size_t scaling_mod_size = 0;
    std::size_t first_mod_size = 0;
    std::string baseline_values;
    std::string result_values;
    std::string notes;
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--bench sin_tiny|cos_tiny|tan_tiny|all_trig_tiny] "
        << "[--degrees 15 30 45] [--backend plain_cpp|openfhe_ckks|all] "
        << "[--threads 1 4 8] [--repeat 3] "
        << "[--lower-bound -0.75] [--upper-bound 0.75] "
        << "[--ckks-ring-dim 0] [--ckks-batch-size 0] [--ckks-depth 0] "
        << "[--ckks-scale-bits 50] [--ckks-first-mod-bits 60] "
        << "[--results results/function_eval_trig_tiny.csv]\n";
}

BackendMode parse_backend(const std::string& value) {
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

bool wants_plain(BackendMode backend) {
    return backend == BackendMode::PlainCpp || backend == BackendMode::All;
}

bool wants_openfhe(BackendMode backend) {
    return backend == BackendMode::OpenFheCkks || backend == BackendMode::All;
}

std::size_t parse_size_arg(int& i, int argc, char** argv, const std::string& flag) {
    if (i + 1 >= argc) {
        throw std::runtime_error(flag + " requires a value");
    }
    return static_cast<std::size_t>(std::stoull(argv[++i]));
}

double parse_double_arg(int& i, int argc, char** argv, const std::string& flag) {
    if (i + 1 >= argc) {
        throw std::runtime_error(flag + " requires a value");
    }
    return std::stod(argv[++i]);
}

CliArgs parse_args(int argc, char** argv) {
    CliArgs args;

    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--help" || flag == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (flag == "--bench") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--bench requires a value");
            }
            if (args.benches.size() == 1 && args.benches[0] == "all_trig_tiny") {
                args.benches.clear();
            }
            args.benches.push_back(argv[++i]);
        } else if (flag == "--degrees") {
            args.degrees.clear();
            while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                const auto degree = static_cast<std::size_t>(std::stoull(argv[++i]));
                if (degree == 0) {
                    throw std::runtime_error("--degrees values must be positive");
                }
                args.degrees.push_back(degree);
            }
            if (args.degrees.empty()) {
                throw std::runtime_error("--degrees requires at least one value");
            }
        } else if (flag == "--backend") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--backend requires a value");
            }
            args.backend = parse_backend(argv[++i]);
        } else if (flag == "--threads") {
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
        } else if (flag == "--results") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--results requires a path");
            }
            args.results_path = argv[++i];
        } else if (flag == "--lower-bound") {
            args.lower_bound = parse_double_arg(i, argc, argv, flag);
        } else if (flag == "--upper-bound") {
            args.upper_bound = parse_double_arg(i, argc, argv, flag);
        } else if (flag == "--ckks-ring-dim") {
            args.ckks_ring_dim = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--ckks-batch-size") {
            args.ckks_batch_size = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--ckks-depth") {
            args.ckks_depth = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--ckks-scale-bits") {
            args.ckks_scaling_mod_size = parse_size_arg(i, argc, argv, flag);
            if (args.ckks_scaling_mod_size == 0) {
                throw std::runtime_error("--ckks-scale-bits must be positive");
            }
        } else if (flag == "--ckks-first-mod-bits") {
            args.ckks_first_mod_size = parse_size_arg(i, argc, argv, flag);
            if (args.ckks_first_mod_size == 0) {
                throw std::runtime_error("--ckks-first-mod-bits must be positive");
            }
        } else {
            throw std::runtime_error("unknown argument: " + flag);
        }
    }

    if (!(args.lower_bound < args.upper_bound)) {
        throw std::runtime_error("--lower-bound must be less than --upper-bound");
    }
    const auto [min_input, max_input] = std::minmax_element(
        kTinyTrigInput.begin(),
        kTinyTrigInput.end());
    if (args.lower_bound > *min_input || args.upper_bound < *max_input) {
        throw std::runtime_error(
            "Chebyshev bounds must cover the fixed tiny input vector [-0.75, 0.75]");
    }
    return args;
}

double divide_or_zero(double numerator, double denominator) {
    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

std::string format_values(const std::vector<double>& values) {
    std::ostringstream output;
    output << '[' << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            output << ';';
        }
        output << values[i];
    }
    output << ']';
    return output.str();
}

bool file_exists_and_has_content(const std::filesystem::path& path) {
    return std::filesystem::exists(path) && std::filesystem::file_size(path) > 0;
}

std::vector<FunctionSpec> available_functions() {
    return {
        {"sin_tiny", "sin", static_cast<double (*)(double)>(std::sin)},
        {"cos_tiny", "cos", static_cast<double (*)(double)>(std::cos)},
        {"tan_tiny", "tan", static_cast<double (*)(double)>(std::tan)},
    };
}

std::vector<FunctionSpec> expand_benches(const std::vector<std::string>& requested) {
    const auto available = available_functions();
    std::vector<FunctionSpec> expanded;

    for (const std::string& name : requested) {
        if (name == "all_trig_tiny") {
            expanded.insert(expanded.end(), available.begin(), available.end());
            continue;
        }

        const auto it = std::find_if(
            available.begin(),
            available.end(),
            [&](const FunctionSpec& spec) { return spec.operation == name; });
        if (it == available.end()) {
            throw std::runtime_error(
                "unknown benchmark: " + name +
                ". Available: sin_tiny cos_tiny tan_tiny all_trig_tiny");
        }
        expanded.push_back(*it);
    }

    return expanded;
}

std::size_t depth_for_degree(std::size_t degree) {
    if (degree <= 5) {
        return 4;
    }
    if (degree <= 13) {
        return 5;
    }
    if (degree <= 27) {
        return 6;
    }
    if (degree <= 59) {
        return 7;
    }
    if (degree <= 119) {
        return 8;
    }
    if (degree <= 247) {
        return 9;
    }
    return 10;
}

FunctionEvalResult run_plain_function(
    const FunctionSpec& spec,
    std::size_t degree,
    const CliArgs& args) {
    const Timer timer;
    std::vector<double> baseline;
    baseline.reserve(kTinyTrigInput.size());
    for (double value : kTinyTrigInput) {
        baseline.push_back(spec.plain_fn(value));
    }
    const double elapsed_ms = timer.elapsed_ms();

    FunctionEvalResult result;
    result.operation = spec.operation;
    result.backend = "plain_cpp";
    result.function_name = spec.function_name;
    result.input_count = kTinyTrigInput.size();
    result.degree = degree;
    result.lower_bound = args.lower_bound;
    result.upper_bound = args.upper_bound;
    result.plain_time_ms = elapsed_ms;
    result.baseline_values = format_values(baseline);
    result.result_values = result.baseline_values;
    result.notes = "tiny_fixed_vector;plain_std_math";
    return result;
}

std::string repeat_note(std::size_t repeat_index, std::size_t repeat_count) {
    return "repeat_index=" + std::to_string(repeat_index) +
           ";repeat_count=" + std::to_string(repeat_count);
}

double average_field(
    const std::vector<FunctionEvalResult>& results,
    double FunctionEvalResult::*field) {
    double sum = 0.0;
    for (const auto& result : results) {
        sum += result.*field;
    }
    return divide_or_zero(sum, static_cast<double>(results.size()));
}

FunctionEvalResult summarize_function_results(
    const std::vector<FunctionEvalResult>& results,
    const std::string& backend) {
    if (results.empty()) {
        throw std::runtime_error("cannot summarize empty function-eval result list");
    }

    FunctionEvalResult summary = results.front();
    summary.operation = results.front().operation + "_summary_avg";
    summary.backend = backend + "_summary";
    summary.plain_time_ms = average_field(results, &FunctionEvalResult::plain_time_ms);
    summary.setup_time_ms = average_field(results, &FunctionEvalResult::setup_time_ms);
    summary.encode_time_ms = average_field(results, &FunctionEvalResult::encode_time_ms);
    summary.encrypt_time_ms = average_field(results, &FunctionEvalResult::encrypt_time_ms);
    summary.he_eval_time_ms = average_field(results, &FunctionEvalResult::he_eval_time_ms);
    summary.decrypt_time_ms = average_field(results, &FunctionEvalResult::decrypt_time_ms);
    summary.decode_time_ms = average_field(results, &FunctionEvalResult::decode_time_ms);
    summary.total_he_time_ms = average_field(results, &FunctionEvalResult::total_he_time_ms);
    summary.operation_slowdown =
        average_field(results, &FunctionEvalResult::operation_slowdown);
    summary.end_to_end_slowdown =
        average_field(results, &FunctionEvalResult::end_to_end_slowdown);
    summary.mean_absolute_error =
        average_field(results, &FunctionEvalResult::mean_absolute_error);
    summary.max_absolute_error =
        average_field(results, &FunctionEvalResult::max_absolute_error);
    summary.notes = "repeat_summary;repeat_count=" + std::to_string(results.size()) +
        ";real_calculation_ms=he_eval_time_ms" +
        ";overall_lifecycle_ms=total_he_time_ms;setup_keygen_excluded_from_total";
    return summary;
}

void append_result_csv(const std::filesystem::path& path, const FunctionEvalResult& result) {
    std::filesystem::create_directories(path.parent_path());
    const bool write_header = !file_exists_and_has_content(path);

    std::ofstream output(path, std::ios::app);
    if (!output) {
        throw std::runtime_error("failed to open function-eval result CSV: " + path.string());
    }

    if (write_header) {
        output
            << "operation,backend,function_name,input_count,degree,lower_bound,upper_bound,"
            << "threads,plain_time_ms,setup_time_ms,encode_time_ms,encrypt_time_ms,"
            << "he_eval_time_ms,decrypt_time_ms,decode_time_ms,total_he_time_ms,"
            << "operation_slowdown,end_to_end_slowdown,mean_absolute_error,"
            << "max_absolute_error,requested_ring_dimension,actual_ring_dimension,"
            << "security_bits,multiplicative_depth,scaling_mod_size,first_mod_size,"
            << "baseline_values,result_values,notes\n";
    }

    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << result.operation << ','
           << result.backend << ','
           << result.function_name << ','
           << result.input_count << ','
           << result.degree << ','
           << result.lower_bound << ','
           << result.upper_bound << ','
           << result.threads << ','
           << result.plain_time_ms << ','
           << result.setup_time_ms << ','
           << result.encode_time_ms << ','
           << result.encrypt_time_ms << ','
           << result.he_eval_time_ms << ','
           << result.decrypt_time_ms << ','
           << result.decode_time_ms << ','
           << result.total_he_time_ms << ','
           << result.operation_slowdown << ','
           << result.end_to_end_slowdown << ','
           << result.mean_absolute_error << ','
           << result.max_absolute_error << ','
           << result.requested_ring_dimension << ','
           << result.actual_ring_dimension << ','
           << result.security_bits << ','
           << result.multiplicative_depth << ','
           << result.scaling_mod_size << ','
           << result.first_mod_size << ','
           << result.baseline_values << ','
           << result.result_values << ','
           << result.notes << '\n';
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

FunctionEvalResult run_openfhe_function(
    const FunctionSpec& spec,
    std::size_t degree,
    std::size_t requested_threads,
    const FunctionEvalResult& baseline,
    const CliArgs& args) {
    using lbcrypto::ADVANCEDSHE;
    using lbcrypto::CCParams;
    using lbcrypto::CryptoContext;
    using lbcrypto::CryptoContextCKKSRNS;
    using lbcrypto::DCRTPoly;
    using lbcrypto::GenCryptoContext;
    using lbcrypto::HEStd_128_classic;
    using lbcrypto::KEYSWITCH;
    using lbcrypto::LEVELEDSHE;
    using lbcrypto::PKE;
    using lbcrypto::Plaintext;

    const std::size_t openfhe_threads = configure_openfhe_threads(requested_threads);
    const std::size_t depth =
        args.ckks_depth == 0 ? depth_for_degree(degree) : args.ckks_depth;

    const Timer setup_timer;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(static_cast<uint32_t>(depth));
    parameters.SetScalingModSize(static_cast<uint32_t>(args.ckks_scaling_mod_size));
    parameters.SetFirstModSize(static_cast<uint32_t>(args.ckks_first_mod_size));
    parameters.SetSecurityLevel(HEStd_128_classic);
    if (args.ckks_ring_dim != 0) {
        parameters.SetRingDim(static_cast<uint32_t>(args.ckks_ring_dim));
    }
    if (args.ckks_batch_size != 0) {
        parameters.SetBatchSize(static_cast<uint32_t>(args.ckks_batch_size));
    }

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);

    const auto keys = cc->KeyGen();
    cc->EvalMultKeyGen(keys.secretKey);
    const double setup_time_ms = setup_timer.elapsed_ms();

    const Timer encode_timer;
    Plaintext plaintext = cc->MakeCKKSPackedPlaintext(
        kTinyTrigInput, 1, 0, nullptr, static_cast<uint32_t>(kTinyTrigInput.size()));
    const double encode_time_ms = encode_timer.elapsed_ms();

    const Timer encrypt_timer;
    auto ciphertext = cc->Encrypt(keys.publicKey, plaintext);
    const double encrypt_time_ms = encrypt_timer.elapsed_ms();

    const Timer eval_timer;
    auto result_ciphertext = ciphertext;
    if (spec.operation == "sin_tiny") {
        result_ciphertext = cc->EvalSin(
            ciphertext,
            args.lower_bound,
            args.upper_bound,
            static_cast<uint32_t>(degree));
    } else if (spec.operation == "cos_tiny") {
        result_ciphertext = cc->EvalCos(
            ciphertext,
            args.lower_bound,
            args.upper_bound,
            static_cast<uint32_t>(degree));
    } else if (spec.operation == "tan_tiny") {
        result_ciphertext = cc->EvalChebyshevFunction(
            [](double x) -> double { return std::tan(x); },
            ciphertext,
            args.lower_bound,
            args.upper_bound,
            static_cast<uint32_t>(degree));
    } else {
        throw std::runtime_error("unsupported OpenFHE function: " + spec.operation);
    }
    const double he_eval_time_ms = eval_timer.elapsed_ms();

    Plaintext decrypted;
    const Timer decrypt_timer;
    cc->Decrypt(keys.secretKey, result_ciphertext, &decrypted);
    const double decrypt_time_ms = decrypt_timer.elapsed_ms();

    const Timer decode_timer;
    decrypted->SetLength(kTinyTrigInput.size());
    const auto decoded_values = decrypted->GetRealPackedValue();
    if (decoded_values.size() < kTinyTrigInput.size()) {
        throw std::runtime_error("function-eval decoded fewer slots than expected");
    }
    std::vector<double> result_values;
    result_values.reserve(kTinyTrigInput.size());
    for (std::size_t i = 0; i < kTinyTrigInput.size(); ++i) {
        result_values.push_back(decoded_values[i]);
    }
    const double decode_time_ms = decode_timer.elapsed_ms();

    double abs_error_sum = 0.0;
    double max_abs_error = 0.0;
    for (std::size_t i = 0; i < kTinyTrigInput.size(); ++i) {
        const double expected = spec.plain_fn(kTinyTrigInput[i]);
        const double abs_error = std::abs(result_values[i] - expected);
        abs_error_sum += abs_error;
        max_abs_error = std::max(max_abs_error, abs_error);
    }

    FunctionEvalResult result;
    result.operation = spec.operation;
    result.backend = "openfhe_ckks";
    result.function_name = spec.function_name;
    result.input_count = kTinyTrigInput.size();
    result.degree = degree;
    result.lower_bound = args.lower_bound;
    result.upper_bound = args.upper_bound;
    result.threads = openfhe_threads;
    result.plain_time_ms = baseline.plain_time_ms;
    result.setup_time_ms = setup_time_ms;
    result.encode_time_ms = encode_time_ms;
    result.encrypt_time_ms = encrypt_time_ms;
    result.he_eval_time_ms = he_eval_time_ms;
    result.decrypt_time_ms = decrypt_time_ms;
    result.decode_time_ms = decode_time_ms;
    result.total_he_time_ms =
        encode_time_ms + encrypt_time_ms + he_eval_time_ms + decrypt_time_ms + decode_time_ms;
    result.operation_slowdown = divide_or_zero(he_eval_time_ms, baseline.plain_time_ms);
    result.end_to_end_slowdown = divide_or_zero(result.total_he_time_ms, baseline.plain_time_ms);
    result.mean_absolute_error = abs_error_sum / static_cast<double>(kTinyTrigInput.size());
    result.max_absolute_error = max_abs_error;
    result.requested_ring_dimension = args.ckks_ring_dim;
    result.actual_ring_dimension = cc->GetRingDimension();
    result.security_bits = 128;
    result.multiplicative_depth = depth;
    result.scaling_mod_size = args.ckks_scaling_mod_size;
    result.first_mod_size = args.ckks_first_mod_size;
    result.baseline_values = baseline.baseline_values;
    result.result_values = format_values(result_values);
    result.notes =
#ifdef _OPENMP
        "tiny_fixed_vector;chebyshev_function_eval;degree_sweep;eval_mult_keygen;omp_set_num_threads";
#else
        "tiny_fixed_vector;chebyshev_function_eval;degree_sweep;eval_mult_keygen;openmp_not_seen_by_runner";
#endif
    cc->ClearStaticMapsAndVectors();
    return result;
}
#endif

int run(const CliArgs& args) {
    const auto functions = expand_benches(args.benches);

    std::cout << "Input vector:";
    for (double value : kTinyTrigInput) {
        std::cout << ' ' << value;
    }
    std::cout << '\n';

    for (const FunctionSpec& spec : functions) {
        for (const std::size_t degree : args.degrees) {
            std::vector<FunctionEvalResult> plain_results;
            plain_results.reserve(args.repeat_count);
            for (std::size_t repeat_index = 1;
                 repeat_index <= args.repeat_count;
                 ++repeat_index) {
                auto plain = run_plain_function(spec, degree, args);
                plain.notes += ";" + repeat_note(repeat_index, args.repeat_count);
                plain_results.push_back(plain);
            }
            const FunctionEvalResult baseline =
                summarize_function_results(plain_results, "plain_cpp");
            if (wants_plain(args.backend)) {
                for (const auto& plain : plain_results) {
                    append_result_csv(args.results_path, plain);
                    std::cout << "Ran plain_cpp:" << spec.operation
                              << " degree=" << degree
                              << " in " << plain.plain_time_ms << " ms\n";
                }
                append_result_csv(args.results_path, baseline);
                std::cout << "Summary " << baseline.backend << ':'
                          << baseline.operation
                          << " degree=" << degree
                          << " repeats=" << args.repeat_count
                          << " avg_plain=" << baseline.plain_time_ms << " ms\n";
            }

            if (wants_openfhe(args.backend)) {
#ifdef UTILITY_BENCH_WITH_OPENFHE
                for (const std::size_t thread_count : args.thread_counts) {
                    std::vector<FunctionEvalResult> he_results;
                    he_results.reserve(args.repeat_count);
                    for (std::size_t repeat_index = 1;
                         repeat_index <= args.repeat_count;
                         ++repeat_index) {
                        auto result =
                            run_openfhe_function(spec, degree, thread_count, baseline, args);
                        result.notes += ";" + repeat_note(repeat_index, args.repeat_count);
                        he_results.push_back(result);
                        append_result_csv(args.results_path, he_results.back());
                        std::cout << "Ran " << result.backend << ':' << result.operation
                                  << " repeat=" << repeat_index << '/' << args.repeat_count
                                  << " degree=" << result.degree
                                  << " threads=" << result.threads
                                  << " he_total=" << result.total_he_time_ms << " ms"
                                  << " he_calc=" << result.he_eval_time_ms << " ms"
                                  << " mae=" << result.mean_absolute_error
                                  << " max_error=" << result.max_absolute_error << '\n';
                    }
                    const FunctionEvalResult he_summary = summarize_function_results(
                        he_results,
                        he_results.front().backend);
                    append_result_csv(args.results_path, he_summary);
                    std::cout << "Summary " << he_summary.backend << ':'
                              << he_summary.operation
                              << " degree=" << he_summary.degree
                              << " threads=" << he_summary.threads
                              << " repeats=" << args.repeat_count
                              << " avg_total=" << he_summary.total_he_time_ms
                              << " ms avg_calc=" << he_summary.he_eval_time_ms
                              << " ms avg_mae=" << he_summary.mean_absolute_error << '\n';
                }
#else
                throw std::runtime_error(
                    "OpenFHE CKKS backend requested but function_eval_bench was built "
                    "without UTILITY_BENCH_WITH_OPENFHE=ON");
#endif
            }
        }
    }

    std::cout << "Wrote results: " << args.results_path << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
