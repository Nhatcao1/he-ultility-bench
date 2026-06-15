#pragma once

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

// Optional operation outputs are for correctness inspection, not timing.
// Call these after compute timers have stopped so disk I/O never pollutes the
// plaintext-vs-HE performance comparison.
inline void write_vector_output_csv(
    const std::filesystem::path& path,
    const std::vector<std::size_t>& row_ids,
    const std::vector<double>& values) {
    if (row_ids.size() != values.size()) {
        throw std::runtime_error("row_ids and values have different sizes");
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to open vector output: " + path.string());
    }

    output << "row_id,value\n";
    output << std::fixed << std::setprecision(6);
    for (std::size_t i = 0; i < values.size(); ++i) {
        output << row_ids[i] << ',' << values[i] << '\n';
    }
}

inline void write_scalar_output_txt(
    const std::filesystem::path& path,
    double value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to open scalar output: " + path.string());
    }

    output << std::fixed << std::setprecision(6) << value << '\n';
}

