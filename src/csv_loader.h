#pragma once

#include <cstddef>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal in-memory representation of transactions.csv for aggregation tests.
// CSV loading is outside benchmark timing; benchmark functions decide which
// columns actually participate in the timed computation.
struct Transactions {
    std::vector<std::size_t> customer_id;
    std::vector<double> x1;
    std::vector<double> x2;
    std::vector<double> x3;
    std::vector<double> amount;
    std::vector<double> risk_weight_by_row;

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

inline Transactions load_transactions_csv(
    const std::string& path,
    std::size_t max_rows = 0) {
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

        // Columns follow TRANSACTION_HEADER in scripts/generate_benchmark_data.py.
        data.customer_id.push_back(static_cast<std::size_t>(std::stoull(fields[1])));
        data.x1.push_back(std::stod(fields[5]));
        data.x2.push_back(std::stod(fields[6]));
        data.x3.push_back(std::stod(fields[7]));
        data.amount.push_back(std::stod(fields[10]));

        if (max_rows != 0 && data.size() >= max_rows) {
            break;
        }
    }

    if (data.size() == 0) {
        throw std::runtime_error("transactions CSV has no data rows: " + path);
    }

    return data;
}

inline std::vector<double> load_customer_risk_weights_csv(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open customers CSV: " + path);
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("customers CSV is empty: " + path);
    }

    std::vector<double> risk_weights;
    std::size_t row_number = 1;

    while (std::getline(input, line)) {
        ++row_number;
        if (line.empty()) {
            continue;
        }

        const auto fields = split_csv_line(line);
        if (fields.size() != 4) {
            throw std::runtime_error(
                "expected 4 columns in " + path + " at row " +
                std::to_string(row_number) + ", got " +
                std::to_string(fields.size()));
        }

        const std::size_t customer_id = static_cast<std::size_t>(std::stoull(fields[0]));
        if (customer_id == std::numeric_limits<std::size_t>::max()) {
            throw std::runtime_error("customer_id is too large in " + path);
        }
        if (risk_weights.size() <= customer_id) {
            risk_weights.resize(customer_id + 1, 0.0);
        }
        risk_weights[customer_id] = std::stod(fields[1]);
    }

    if (risk_weights.empty()) {
        throw std::runtime_error("customers CSV has no data rows: " + path);
    }

    return risk_weights;
}

inline void attach_customer_risk_weights(
    Transactions& data,
    const std::vector<double>& customer_risk_weights) {
    data.risk_weight_by_row.clear();
    data.risk_weight_by_row.reserve(data.size());

    for (std::size_t row = 0; row < data.size(); ++row) {
        const std::size_t customer_id = data.customer_id[row];
        if (customer_id >= customer_risk_weights.size()) {
            throw std::runtime_error(
                "transaction references missing customer_id: " +
                std::to_string(customer_id));
        }
        data.risk_weight_by_row.push_back(customer_risk_weights[customer_id]);
    }
}
