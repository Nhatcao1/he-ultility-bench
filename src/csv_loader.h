#pragma once

#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal in-memory representation of transactions.csv for aggregation tests.
// We intentionally load only the `amount` column because CSV loading is outside
// benchmark timing, and the first HE target is SELECT SUM(amount).
struct Transactions {
    std::vector<double> amount;

    std::size_t size() const { return amount.size(); }
};

inline std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;

    // The generated benchmark files do not contain quoted commas. Keeping
    // this parser intentionally simple makes the C++ baseline easy to audit.
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }

    return fields;
}

inline Transactions load_transactions_csv(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open transactions CSV: " + path);
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("transactions CSV is empty: " + path);
    }

    Transactions data;
    std::size_t row_number = 1;

    while (std::getline(input, line)) {
        ++row_number;
        if (line.empty()) {
            continue;
        }

        const auto fields = split_csv_line(line);
        if (fields.size() != 15) {
            throw std::runtime_error(
                "expected 15 columns in " + path + " at row " +
                std::to_string(row_number) + ", got " +
                std::to_string(fields.size()));
        }

        // Column 10 is `amount` in scripts/generate_benchmark_data.py.
        data.amount.push_back(std::stod(fields[10]));
    }

    if (data.size() == 0) {
        throw std::runtime_error("transactions CSV has no data rows: " + path);
    }

    return data;
}
