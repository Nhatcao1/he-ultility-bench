#include "csv_loader.h"
#include "plain_ops.h"
#include "result_writer.h"
#include "timer.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct CliArgs {
    std::string data_path = "data/generated/tiny_1k/transactions.csv";
    std::filesystem::path results_path = "results/benchmark_results.csv";
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--data transactions.csv] "
        << "[--results results/benchmark_results.csv]\n\n"
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

        const BenchmarkResult vector_add = run_plain_operation(
            data,
            "vector_add_x1_x2",
            plaintext_vector_add_checksum);
        append_result_csv(args.results_path, vector_add);

        const BenchmarkResult linear_score = run_plain_operation(
            data,
            "linear_score",
            plaintext_linear_score_checksum);
        append_result_csv(args.results_path, linear_score);

        const BenchmarkResult masked_sum = run_plain_operation(
            data,
            "masked_sum_amount_channel_5",
            plaintext_masked_sum_channel_5);
        append_result_csv(args.results_path, masked_sum);

        std::cout << "Wrote results: " << args.results_path << '\n';
        std::cout << "Plain benchmarks complete.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}

