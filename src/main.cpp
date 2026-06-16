#include "csv_loader.h"
#ifdef UTILITY_BENCH_WITH_OPENFHE
#include "ckks_sum_amount.h"
#endif
#include "output_writer.h"
#include "plain_ops.h"
#include "result_writer.h"
#include "timer.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <set>
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
    std::string data_path = "data/generated/tiny_1k/transactions.csv";
    std::filesystem::path results_path = "results/benchmark_results.csv";
    std::vector<std::string> benches = {"all_agg"};
    std::vector<std::size_t> thread_counts = {1, 4, 8};
    BackendMode backend = BackendMode::PlainCpp;
    std::size_t ckks_ring_dim = 0;
    std::size_t ckks_batch_size = 0;
    std::size_t ckks_depth = 1;
    std::size_t ckks_scaling_mod_size = 50;
    std::size_t ckks_first_mod_size = 60;
    bool save_outputs = false;
    std::filesystem::path output_dir = "results/outputs/tiny_1k";
};

struct BenchmarkDefinition {
    std::string name;
    std::function<BenchmarkResult(const Transactions&, std::size_t)> run;
    std::function<void(const Transactions&, std::size_t, const std::filesystem::path&)> save_output;
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--data transactions.csv] "
        << "[--bench all_agg|benchmark_name] "
        << "[--backend plain_cpp|openfhe_ckks|all] "
        << "[--threads 1 4 8] "
        << "[--ckks-ring-dim 8192] [--ckks-batch-size 4096] "
        << "[--ckks-depth 1] [--ckks-scale-bits 50] [--ckks-first-mod-bits 60] "
        << "[--results results/benchmark_results.csv] "
        << "[--save-outputs] [--output-dir results/outputs/tiny_1k]\n\n"
        << "Runs aggregation benchmarks for plaintext C++ and optionally "
        << "OpenFHE CKKS EvalSum/rotation.\n\n"
        << "Available aggregation benchmarks:\n"
        << "  sum_amount\n\n"
        << "Default backend: plain_cpp.\n"
        << "Default OpenFHE thread counts: 1 4 8.\n";
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
    throw std::runtime_error(
        "unknown backend: " + value + ". Available: plain_cpp openfhe_ckks all");
}

bool wants_plain_cpp(BackendMode backend) {
    return backend == BackendMode::PlainCpp || backend == BackendMode::All;
}

bool wants_openfhe_ckks(BackendMode backend) {
    return backend == BackendMode::OpenFheCkks || backend == BackendMode::All;
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

        if (flag == "--bench") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--bench requires a benchmark name");
            }
            const std::string bench = argv[++i];
            if (args.benches.size() == 1 && args.benches[0] == "all_agg") {
                args.benches.clear();
            }
            args.benches.push_back(bench);
            continue;
        }

        if (flag == "--backend") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--backend requires plain_cpp, openfhe_ckks, or all");
            }
            args.backend = parse_backend_mode(argv[++i]);
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
                throw std::runtime_error("--threads requires at least one positive value");
            }
            continue;
        }

        if (flag == "--ckks-ring-dim") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--ckks-ring-dim requires a positive integer or 0");
            }
            args.ckks_ring_dim = static_cast<std::size_t>(std::stoull(argv[++i]));
            continue;
        }

        if (flag == "--ckks-batch-size") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--ckks-batch-size requires a positive integer or 0");
            }
            args.ckks_batch_size = static_cast<std::size_t>(std::stoull(argv[++i]));
            continue;
        }

        if (flag == "--ckks-depth") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--ckks-depth requires a positive integer");
            }
            args.ckks_depth = static_cast<std::size_t>(std::stoull(argv[++i]));
            if (args.ckks_depth == 0) {
                throw std::runtime_error("--ckks-depth must be positive");
            }
            continue;
        }

        if (flag == "--ckks-scale-bits") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--ckks-scale-bits requires a positive integer");
            }
            args.ckks_scaling_mod_size = static_cast<std::size_t>(std::stoull(argv[++i]));
            if (args.ckks_scaling_mod_size == 0) {
                throw std::runtime_error("--ckks-scale-bits must be positive");
            }
            continue;
        }

        if (flag == "--ckks-first-mod-bits") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--ckks-first-mod-bits requires a positive integer");
            }
            args.ckks_first_mod_size = static_cast<std::size_t>(std::stoull(argv[++i]));
            if (args.ckks_first_mod_size == 0) {
                throw std::runtime_error("--ckks-first-mod-bits must be positive");
            }
            continue;
        }

        if (flag == "--save-outputs") {
            args.save_outputs = true;
            continue;
        }

        if (flag == "--output-dir") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--output-dir requires a path");
            }
            args.output_dir = argv[++i];
            continue;
        }

        throw std::runtime_error("unknown argument: " + flag);
    }

    return args;
}

BenchmarkResult run_plain_operation(
    const Transactions& data,
    const std::string& operation,
    std::size_t thread_count,
    double (*operation_fn)(const Transactions&, std::size_t)) {
    // Timing starts after CSV loading. This keeps compute baselines separate
    // from I/O so the HE slowdown numbers are easier to explain.
    const std::size_t actual_threads = effective_thread_count(thread_count, data.size());
    const Timer timer;
    const double value = operation_fn(data, actual_threads);
    const double elapsed_ms = timer.elapsed_ms();

    BenchmarkResult result;
    result.operation = operation;
    result.backend = "plain_cpp";
    result.rows = data.size();
    result.threads = actual_threads;
    result.plain_time_ms = elapsed_ms;
    result.result_value = value;
    result.baseline_value = value;
    result.notes = "compute_only_no_io;std_single_thread_baseline";
    return result;
}

BenchmarkResult run_plain_scalar_benchmark(
    const Transactions& data,
    const std::string& operation,
    std::size_t thread_count,
    double (*operation_fn)(const Transactions&, std::size_t)) {
    return run_plain_operation(data, operation, thread_count, operation_fn);
}

BenchmarkDefinition make_scalar_benchmark(
    const std::string& name,
    double (*operation_fn)(const Transactions&, std::size_t)) {
    return BenchmarkDefinition{
        name,
        [name, operation_fn](const Transactions& data, std::size_t thread_count) {
            return run_plain_scalar_benchmark(data, name, thread_count, operation_fn);
        },
        [name, operation_fn](
            const Transactions& data,
            std::size_t thread_count,
            const std::filesystem::path& output_dir) {
            const double value = operation_fn(data, thread_count);
            write_scalar_output_txt(
                output_dir / ("plain_" + name + "_threads_" + std::to_string(thread_count) + ".txt"),
                value);
        }};
}

std::map<std::string, BenchmarkDefinition> plain_benchmarks() {
    std::map<std::string, BenchmarkDefinition> benchmarks;

    for (const auto& benchmark : {
             make_scalar_benchmark("sum_amount", plaintext_sum_amount),
         }) {
        benchmarks.emplace(benchmark.name, benchmark);
    }

    return benchmarks;
}

std::vector<std::string> expand_benchmarks(
    const std::vector<std::string>& requested,
    const std::map<std::string, BenchmarkDefinition>& available) {
    std::vector<std::string> expanded;
    std::set<std::string> seen;

    for (const std::string& name : requested) {
        if (name == "all_agg") {
            for (const auto& entry : available) {
                if (seen.insert(entry.first).second) {
                    expanded.push_back(entry.first);
                }
            }
            continue;
        }

        if (available.find(name) == available.end()) {
            std::ostringstream message;
            message << "unknown benchmark: " << name << ". Available:";
            for (const auto& entry : available) {
                message << ' ' << entry.first;
            }
            message << " all_agg";
            throw std::runtime_error(message.str());
        }

        if (seen.insert(name).second) {
            expanded.push_back(name);
        }
    }

    return expanded;
}

BenchmarkResult run_openfhe_ckks_benchmark(
    const Transactions& data,
    const std::string& operation,
    std::size_t thread_count,
    const BenchmarkResult& baseline,
    const CliArgs& args) {
    if (operation != "sum_amount") {
        throw std::runtime_error("OpenFHE CKKS backend currently supports only sum_amount");
    }

#ifdef UTILITY_BENCH_WITH_OPENFHE
    CkksSumConfig config;
    config.requested_ring_dimension = args.ckks_ring_dim;
    config.batch_size = args.ckks_batch_size;
    config.multiplicative_depth = args.ckks_depth;
    config.scaling_mod_size = args.ckks_scaling_mod_size;
    config.first_mod_size = args.ckks_first_mod_size;
    return openfhe_ckks_sum_amount(
        data,
        thread_count,
        baseline.baseline_value,
        baseline.plain_time_ms,
        config);
#else
    (void)data;
    (void)thread_count;
    (void)baseline;
    (void)args;
    throw std::runtime_error(
        "OpenFHE CKKS backend was requested but this binary was built without "
        "UTILITY_BENCH_WITH_OPENFHE=ON");
#endif
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliArgs args = parse_args(argc, argv);

        std::cout << "Loading transactions: " << args.data_path << '\n';
        const Timer load_timer;
        const Transactions data = load_transactions_csv(args.data_path);
        const double load_ms = load_timer.elapsed_ms();
        std::cout << "Loaded " << data.size() << " rows in "
                  << load_ms << " ms\n";

        const auto available_benchmarks = plain_benchmarks();
        const auto selected_benches = expand_benchmarks(
            args.benches,
            available_benchmarks);

        for (const std::string& bench_name : selected_benches) {
            const BenchmarkDefinition& benchmark = available_benchmarks.at(bench_name);
            const BenchmarkResult baseline = benchmark.run(data, 1);

            if (wants_plain_cpp(args.backend)) {
                append_result_csv(args.results_path, baseline);

                if (args.save_outputs) {
                    benchmark.save_output(data, baseline.threads, args.output_dir);
                }

                std::cout << "Ran " << baseline.backend << ':' << baseline.operation
                          << " threads=" << baseline.threads
                          << " in " << baseline.plain_time_ms << " ms"
                          << " value=" << baseline.result_value << '\n';
            }

            for (const std::size_t thread_count : args.thread_counts) {
                if (wants_openfhe_ckks(args.backend)) {
                    const BenchmarkResult result =
                        run_openfhe_ckks_benchmark(data, bench_name, thread_count, baseline, args);
                    append_result_csv(args.results_path, result);

                    if (args.save_outputs) {
                        write_scalar_output_txt(
                            args.output_dir /
                                ("openfhe_ckks_" + result.operation + "_threads_" +
                                 std::to_string(result.threads) + ".txt"),
                            result.result_value);
                    }

                    std::cout << "Ran " << result.backend << ':' << result.operation
                              << " threads=" << result.threads
                              << " he_total=" << result.total_he_time_ms << " ms"
                              << " plain=" << result.plain_time_ms << " ms"
                              << " abs_error=" << result.absolute_error
                              << " rel_error=" << result.relative_error << '\n';
                }
            }
        }

        if (args.save_outputs) {
            std::cout << "Wrote operation outputs: " << args.output_dir << '\n';
        }
        std::cout << "Wrote results: " << args.results_path << '\n';
        std::cout << "Aggregation benchmarks complete.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
