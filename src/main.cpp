#include "csv_loader.h"
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

struct CliArgs {
    std::string data_path = "data/generated/tiny_1k/transactions.csv";
    std::filesystem::path results_path = "results/benchmark_results.csv";
    std::vector<std::string> benches = {"all_agg"};
    std::vector<std::size_t> thread_counts = {1, 4, 8};
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
        << "[--threads 1 4 8] "
        << "[--results results/benchmark_results.csv] "
        << "[--save-outputs] [--output-dir results/outputs/tiny_1k]\n\n"
        << "Runs aggregation baselines used as comparison targets for later "
        << "OpenFHE CKKS EvalSum/rotation benchmarks.\n\n"
        << "Available aggregation benchmarks:\n"
        << "  sum_amount\n\n"
        << "Default thread counts: 1 4 8.\n";
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
    result.notes = "compute_only_no_io;std_thread_baseline";
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

        for (const std::size_t thread_count : args.thread_counts) {
            for (const std::string& bench_name : selected_benches) {
                const BenchmarkDefinition& benchmark = available_benchmarks.at(bench_name);
                const BenchmarkResult result = benchmark.run(data, thread_count);
                append_result_csv(args.results_path, result);

                if (args.save_outputs) {
                    benchmark.save_output(data, thread_count, args.output_dir);
                }

                std::cout << "Ran " << result.backend << ':' << result.operation
                          << " threads=" << result.threads
                          << " in " << result.plain_time_ms << " ms\n";
            }
        }

        if (args.save_outputs) {
            std::cout << "Wrote operation outputs: " << args.output_dir << '\n';
        }
        std::cout << "Wrote results: " << args.results_path << '\n';
        std::cout << "Plain benchmarks complete.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
