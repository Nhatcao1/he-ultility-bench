#pragma once

#include "csv_loader.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

// Tiny fixture for encrypted lookup/join feasibility tests. Unlike the normal
// transactions.csv loader, this one expects the files produced by
// scripts/generate_tiny_crypto_query_data.py.
struct TinyCryptoQueryData {
    std::vector<std::size_t> tx_id;
    std::vector<std::size_t> customer_id;
    std::vector<double> amount;
    std::vector<std::size_t> channel_id;
    std::vector<double> x1;
    std::vector<double> x2;
    std::vector<std::vector<double>> customer_onehot_by_key;
    std::vector<double> customer_risk_weight_by_key;
    std::vector<double> expected_risk_weight_by_row;
    std::vector<double> expected_amount_times_risk_by_row;

    std::size_t size() const { return amount.size(); }
    std::size_t key_domain() const { return customer_risk_weight_by_key.size(); }
};

inline double tiny_checksum(const std::vector<double>& values) {
    double total = 0.0;
    for (const double value : values) {
        total += value;
    }
    return total;
}

inline std::vector<std::vector<double>> make_onehot_columns(
    std::size_t rows,
    std::size_t key_domain) {
    return std::vector<std::vector<double>>(key_domain, std::vector<double>(rows, 0.0));
}

inline void load_tiny_transactions_onehot_csv(
    const std::filesystem::path& path,
    TinyCryptoQueryData& data) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open tiny transactions CSV: " + path.string());
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("tiny transactions CSV is empty: " + path.string());
    }

    const auto header = split_csv_line(line);
    if (header.size() < 7) {
        throw std::runtime_error("tiny transactions_onehot.csv needs one-hot columns");
    }
    const std::size_t key_domain = header.size() - 6;

    std::vector<std::vector<double>> onehot_rows;
    std::size_t row_number = 1;

    while (std::getline(input, line)) {
        ++row_number;
        if (line.empty()) {
            continue;
        }

        const auto fields = split_csv_line(line);
        if (fields.size() != header.size()) {
            throw std::runtime_error(
                "expected " + std::to_string(header.size()) + " columns in " +
                path.string() + " at row " + std::to_string(row_number));
        }

        data.tx_id.push_back(static_cast<std::size_t>(std::stoull(fields[0])));
        data.customer_id.push_back(static_cast<std::size_t>(std::stoull(fields[1])));
        data.amount.push_back(std::stod(fields[2]));
        data.channel_id.push_back(static_cast<std::size_t>(std::stoull(fields[3])));
        data.x1.push_back(std::stod(fields[4]));
        data.x2.push_back(std::stod(fields[5]));

        std::vector<double> onehot_row;
        onehot_row.reserve(key_domain);
        for (std::size_t key = 0; key < key_domain; ++key) {
            onehot_row.push_back(std::stod(fields[6 + key]));
        }
        onehot_rows.push_back(onehot_row);
    }

    if (data.size() == 0) {
        throw std::runtime_error("tiny transactions CSV has no data rows: " + path.string());
    }

    data.customer_onehot_by_key = make_onehot_columns(data.size(), key_domain);
    for (std::size_t row = 0; row < onehot_rows.size(); ++row) {
        for (std::size_t key = 0; key < key_domain; ++key) {
            data.customer_onehot_by_key[key][row] = onehot_rows[row][key];
        }
    }
}

inline void load_tiny_customers_csv(
    const std::filesystem::path& path,
    TinyCryptoQueryData& data) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open tiny customers CSV: " + path.string());
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("tiny customers CSV is empty: " + path.string());
    }

    std::size_t row_number = 1;
    while (std::getline(input, line)) {
        ++row_number;
        if (line.empty()) {
            continue;
        }

        const auto fields = split_csv_line(line);
        if (fields.size() != 3) {
            throw std::runtime_error(
                "expected 3 columns in " + path.string() + " at row " +
                std::to_string(row_number));
        }

        const std::size_t customer_id = static_cast<std::size_t>(std::stoull(fields[0]));
        if (data.customer_risk_weight_by_key.size() <= customer_id) {
            data.customer_risk_weight_by_key.resize(customer_id + 1, 0.0);
        }
        data.customer_risk_weight_by_key[customer_id] = std::stod(fields[1]);
    }

    if (data.customer_risk_weight_by_key.empty()) {
        throw std::runtime_error("tiny customers CSV has no data rows: " + path.string());
    }
}

inline void load_tiny_expected_lookup_csv(
    const std::filesystem::path& path,
    TinyCryptoQueryData& data) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open tiny expected lookup CSV: " + path.string());
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("tiny expected lookup CSV is empty: " + path.string());
    }

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split_csv_line(line);
        if (fields.size() != 3) {
            throw std::runtime_error("expected 3 columns in " + path.string());
        }
        data.expected_risk_weight_by_row.push_back(std::stod(fields[2]));
    }
}

inline void load_tiny_expected_join_csv(
    const std::filesystem::path& path,
    TinyCryptoQueryData& data) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open tiny expected join CSV: " + path.string());
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::runtime_error("tiny expected join CSV is empty: " + path.string());
    }

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split_csv_line(line);
        if (fields.size() != 5) {
            throw std::runtime_error("expected 5 columns in " + path.string());
        }
        data.expected_amount_times_risk_by_row.push_back(std::stod(fields[4]));
    }
}

inline TinyCryptoQueryData load_tiny_crypto_query_dir(
    const std::filesystem::path& dir) {
    TinyCryptoQueryData data;

    load_tiny_transactions_onehot_csv(dir / "transactions_onehot.csv", data);
    load_tiny_customers_csv(dir / "customers.csv", data);
    load_tiny_expected_lookup_csv(dir / "expected_lookup.csv", data);
    load_tiny_expected_join_csv(dir / "expected_join.csv", data);

    if (data.customer_onehot_by_key.size() != data.customer_risk_weight_by_key.size()) {
        throw std::runtime_error("tiny one-hot key domain does not match customers.csv");
    }
    if (data.expected_risk_weight_by_row.size() != data.size()) {
        throw std::runtime_error("tiny expected_lookup.csv row count does not match transactions");
    }
    if (data.expected_amount_times_risk_by_row.size() != data.size()) {
        throw std::runtime_error("tiny expected_join.csv row count does not match transactions");
    }

    return data;
}

inline double plaintext_tiny_lookup_onehot_risk_weight(
    const TinyCryptoQueryData& data) {
    return tiny_checksum(data.expected_risk_weight_by_row);
}

inline double plaintext_tiny_join_onehot_amount_risk(
    const TinyCryptoQueryData& data) {
    return tiny_checksum(data.expected_amount_times_risk_by_row);
}
