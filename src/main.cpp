#include "csv_loader.h"
#include "output_writer.h"
#include "plain_ops.h"
#include "result_writer.h"
#include "timer.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

struct CliArgs {
    std::string data_path = "data/generated/tiny_1k/transactions.csv";
    std::filesystem::path results_path = "results/benchmark_results.csv";
    bool save_outputs = false;
    std::filesystem::path output_dir = "results/outputs/tiny_1k";
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--data transactions.csv] "
        << "[--results results/benchmark_results.csv] "
        << "[--save-outputs] [--output-dir results/outputs/tiny_1k]\n\n"
        << "Runs plaintext baselines used as comparison targets for later "
        << "OpenFHE benchmarks.\n";
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

        const VectorOperationResult vector_add = run_plain_vector_operation(
            data,
            "vector_add_x1_x2",
            plaintext_vector_add_values);
        append_result_csv(args.results_path, vector_add.benchmark);

        const VectorOperationResult linear_score = run_plain_vector_operation(
            data,
            "linear_score",
            plaintext_linear_score_values);
        append_result_csv(args.results_path, linear_score.benchmark);

        const BenchmarkResult masked_sum = run_plain_operation(
            data,
            "masked_sum_amount_channel_5",
            plaintext_masked_sum_channel_5);
        append_result_csv(args.results_path, masked_sum);

        if (args.save_outputs) {
            write_vector_output_csv(
                args.output_dir / "plain_vector_add_x1_x2.csv",
                data.row_id,
                vector_add.values);
            write_vector_output_csv(
                args.output_dir / "plain_linear_score.csv",
                data.row_id,
                linear_score.values);
            write_scalar_output_txt(
                args.output_dir / "plain_masked_sum_amount_channel_5.txt",
                masked_sum.result_value);
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
