#pragma once

#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal in-memory representation of transactions.csv.
// The first plaintext benchmarks only need the numeric columns that map
// directly to later HE experiments, so we avoid carrying unused metadata.
struct Transactions {
    std::vector<std::size_t> row_id;
    std::vector<double> x1;
    std::vector<double> x2;
    std::vector<double> x3;
    std::vector<int> i1;
    std::vector<int> i2;
    std::vector<double> amount;
    std::vector<int> mask_channel_5;
    std::vector<int> label;

    std::size_t size() const { return row_id.size(); }
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

        // Column positions follow scripts/generate_benchmark_data.py.
        data.row_id.push_back(static_cast<std::size_t>(std::stoull(fields[0])));
        data.x1.push_back(std::stod(fields[5]));
        data.x2.push_back(std::stod(fields[6]));
        data.x3.push_back(std::stod(fields[7]));
        data.i1.push_back(std::stoi(fields[8]));
        data.i2.push_back(std::stoi(fields[9]));
        data.amount.push_back(std::stod(fields[10]));
        data.mask_channel_5.push_back(std::stoi(fields[11]));
        data.label.push_back(std::stoi(fields[14]));
    }

    if (data.size() == 0) {
        throw std::runtime_error("transactions CSV has no data rows: " + path);
    }

    return data;
}
