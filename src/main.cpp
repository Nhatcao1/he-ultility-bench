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
#include <utility>
#include <vector>

namespace {

struct CliArgs {
    std::string data_path = "data/generated/tiny_1k/transactions.csv";
    std::filesystem::path results_path = "results/benchmark_results.csv";
    std::string scheme = "plain";
    std::vector<std::string> benches = {"all_plain"};
    bool save_outputs = false;
    std::filesystem::path output_dir = "results/outputs/tiny_1k";
};

struct BenchmarkDefinition {
    std::string name;
    bool writes_vector_output = false;
    std::function<BenchmarkResult(const Transactions&)> run;
    std::function<void(const Transactions&, const std::filesystem::path&)> save_output;
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--data transactions.csv] "
        << "[--scheme plain] [--bench all_plain|benchmark_name] "
        << "[--results results/benchmark_results.csv] "
        << "[--save-outputs] [--output-dir results/outputs/tiny_1k]\n\n"
        << "Runs selected baselines used as comparison targets for later "
        << "OpenFHE benchmarks.\n\n"
        << "Available plaintext benchmarks:\n"
        << "  vector_add_x1_x2\n"
        << "  vector_mul_x1_x2\n"
        << "  sum_x1\n"
        << "  linear_score\n"
        << "  masked_sum_amount_channel_5\n"
        << "  masked_count_channel_5\n"
        << "  masked_avg_amount_channel_5\n";
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

        if (flag == "--scheme") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--scheme requires a value");
            }
            args.scheme = argv[++i];
            continue;
        }

        if (flag == "--bench") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--bench requires a benchmark name");
            }
            const std::string bench = argv[++i];
            if (args.benches.size() == 1 && args.benches[0] == "all_plain") {
                args.benches.clear();
            }
            args.benches.push_back(bench);
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
    double (*operation_fn)(const Transactions&)) {
    // Timing starts after CSV loading. This keeps compute baselines separate
    // from I/O so the HE slowdown numbers are easier to explain.
    const Timer timer;
    const double value = operation_fn(data);
    const double elapsed_ms = timer.elapsed_ms();

    BenchmarkResult result;
    result.operation = operation;
    result.scheme = "plain_cpp";
    result.rows = data.size();
    result.plain_time_ms = elapsed_ms;
    result.result_value = value;
    result.notes = "compute_only_no_io";
    return result;
}

struct VectorOperationResult {
    BenchmarkResult benchmark;
    std::vector<double> values;
};

VectorOperationResult run_plain_vector_operation(
    const Transactions& data,
    const std::string& operation,
    std::vector<double> (*operation_fn)(const Transactions&)) {
    // Timing starts after CSV loading and stops before optional output writes.
    // This mirrors the future OpenFHE split between compute and serialization.
    const Timer timer;
    std::vector<double> values = operation_fn(data);
    const double elapsed_ms = timer.elapsed_ms();

    BenchmarkResult benchmark;
    benchmark.operation = operation;
    benchmark.scheme = "plain_cpp";
    benchmark.rows = data.size();
    benchmark.plain_time_ms = elapsed_ms;
    benchmark.result_value = checksum(values);
    benchmark.notes = "compute_only_no_io";

    return {benchmark, std::move(values)};
}

BenchmarkResult run_plain_scalar_benchmark(
    const Transactions& data,
    const std::string& operation,
    double (*operation_fn)(const Transactions&)) {
    return run_plain_operation(data, operation, operation_fn);
}

BenchmarkDefinition make_vector_benchmark(
    const std::string& name,
    std::vector<double> (*operation_fn)(const Transactions&)) {
    return BenchmarkDefinition{
        name,
        true,
        [name, operation_fn](const Transactions& data) {
            return run_plain_vector_operation(data, name, operation_fn).benchmark;
        },
        [name, operation_fn](const Transactions& data, const std::filesystem::path& output_dir) {
            const std::vector<double> values = operation_fn(data);
            write_vector_output_csv(
                output_dir / ("plain_" + name + ".csv"),
                data.row_id,
                values);
        }};
}

BenchmarkDefinition make_scalar_benchmark(
    const std::string& name,
    double (*operation_fn)(const Transactions&)) {
    return BenchmarkDefinition{
        name,
        false,
        [name, operation_fn](const Transactions& data) {
            return run_plain_scalar_benchmark(data, name, operation_fn);
        },
        [name, operation_fn](const Transactions& data, const std::filesystem::path& output_dir) {
            const double value = operation_fn(data);
            write_scalar_output_txt(
                output_dir / ("plain_" + name + ".txt"),
                value);
        }};
}

std::map<std::string, BenchmarkDefinition> plain_benchmarks() {
    std::map<std::string, BenchmarkDefinition> benchmarks;

    for (const auto& benchmark : {
             make_vector_benchmark("vector_add_x1_x2", plaintext_vector_add_values),
             make_vector_benchmark("vector_mul_x1_x2", plaintext_vector_mul_values),
             make_vector_benchmark("linear_score", plaintext_linear_score_values),
             make_scalar_benchmark("sum_x1", plaintext_sum_x1),
             make_scalar_benchmark("masked_sum_amount_channel_5", plaintext_masked_sum_channel_5),
             make_scalar_benchmark("masked_count_channel_5", plaintext_masked_count_channel_5),
             make_scalar_benchmark("masked_avg_amount_channel_5", plaintext_masked_avg_amount_channel_5),
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
        if (name == "all_plain") {
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
            message << " all_plain";
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
        if (args.scheme != "plain") {
            throw std::runtime_error(
                "only --scheme plain is implemented in this build");
        }

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
            const BenchmarkResult result = benchmark.run(data);
            append_result_csv(args.results_path, result);

            if (args.save_outputs) {
                benchmark.save_output(data, args.output_dir);
            }

            std::cout << "Ran " << result.scheme << ':' << result.operation
                      << " in " << result.plain_time_ms << " ms\n";
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
