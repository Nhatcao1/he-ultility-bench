#pragma once

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string>

// One row in results/benchmark_results.csv.
// Plain rows fill the baseline and accuracy columns. OpenFHE rows additionally
// fill setup, CKKS timing, and packing metadata so the SIMD shape is visible.
struct BenchmarkResult {
    std::string operation;
    std::string backend;
    std::size_t rows = 0;
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
    double result_value = 0.0;
    double baseline_value = 0.0;
    double absolute_error = 0.0;
    double relative_error = 0.0;
    std::size_t ciphertext_count = 0;
    std::size_t slots_per_ciphertext = 0;
    std::size_t used_slots_last_ciphertext = 0;
    std::size_t padding_slots_last_ciphertext = 0;
    double slot_utilization = 0.0;
    std::size_t requested_ring_dimension = 0;
    std::size_t actual_ring_dimension = 0;
    std::size_t security_bits = 0;
    std::size_t multiplicative_depth = 0;
    std::size_t scaling_mod_size = 0;
    std::size_t first_mod_size = 0;
    std::size_t rotation_count_estimate = 0;
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
            << "operation,backend,rows,threads,plain_time_ms,setup_time_ms,encode_time_ms,"
            << "encrypt_time_ms,he_eval_time_ms,decrypt_time_ms,decode_time_ms,"
            << "total_he_time_ms,operation_slowdown,end_to_end_slowdown,"
            << "result_value,baseline_value,absolute_error,relative_error,"
            << "ciphertext_count,slots_per_ciphertext,used_slots_last_ciphertext,"
            << "padding_slots_last_ciphertext,slot_utilization,"
            << "requested_ring_dimension,actual_ring_dimension,security_bits,multiplicative_depth,"
            << "scaling_mod_size,first_mod_size,rotation_count_estimate,notes\n";
    }

    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << result.operation << ','
           << result.backend << ','
           << result.rows << ','
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
           << result.result_value << ','
           << result.baseline_value << ','
           << result.absolute_error << ','
           << result.relative_error << ','
           << result.ciphertext_count << ','
           << result.slots_per_ciphertext << ','
           << result.used_slots_last_ciphertext << ','
           << result.padding_slots_last_ciphertext << ','
           << result.slot_utilization << ','
           << result.requested_ring_dimension << ','
           << result.actual_ring_dimension << ','
           << result.security_bits << ','
           << result.multiplicative_depth << ','
           << result.scaling_mod_size << ','
           << result.first_mod_size << ','
           << result.rotation_count_estimate << ','
           << result.notes << '\n';
}
