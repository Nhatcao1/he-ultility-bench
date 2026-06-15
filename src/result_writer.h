#pragma once

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>

// One row in results/benchmark_results.csv.
// Later OpenFHE benchmarks will fill the HE timing columns; plaintext rows
// intentionally set them to zero so the schema stays stable from day one.
struct BenchmarkResult {
    std::string operation;
    std::string scheme;
    std::size_t rows = 0;
    double plain_time_ms = 0.0;
    double encode_time_ms = 0.0;
    double encrypt_time_ms = 0.0;
    double he_eval_time_ms = 0.0;
    double decrypt_time_ms = 0.0;
    double decode_time_ms = 0.0;
    double total_he_time_ms = 0.0;
    double operation_slowdown = 0.0;
    double end_to_end_slowdown = 0.0;
    double result_value = 0.0;
    std::string notes;
};

inline bool file_exists_and_has_content(const std::filesystem::path& path) {
    return std::filesystem::exists(path) && std::filesystem::file_size(path) > 0;
}

inline void append_result_csv(
    const std::filesystem::path& path,
    const BenchmarkResult& result) {
    std::filesystem::create_directories(path.parent_path());
    const bool write_header = !file_exists_and_has_content(path);

    std::ofstream output(path, std::ios::app);
    if (!output) {
        throw std::runtime_error("failed to open result CSV: " + path.string());
    }

    if (write_header) {
        output
            << "operation,scheme,rows,plain_time_ms,encode_time_ms,"
            << "encrypt_time_ms,he_eval_time_ms,decrypt_time_ms,decode_time_ms,"
            << "total_he_time_ms,operation_slowdown,end_to_end_slowdown,"
            << "result_value,notes\n";
    }

    output << std::fixed << std::setprecision(6)
           << result.operation << ','
           << result.scheme << ','
           << result.rows << ','
           << result.plain_time_ms << ','
           << result.encode_time_ms << ','
           << result.encrypt_time_ms << ','
           << result.he_eval_time_ms << ','
           << result.decrypt_time_ms << ','
           << result.decode_time_ms << ','
           << result.total_he_time_ms << ','
           << result.operation_slowdown << ','
           << result.end_to_end_slowdown << ','
           << result.result_value << ','
           << result.notes << '\n';
}

