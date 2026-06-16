#pragma once

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

struct FedAvgLayoutEntry {
    std::string name;
    std::size_t start = 0;
    std::size_t end = 0;
};

struct FedAvgClient {
    std::string client_id;
    std::size_t num_examples = 0;
    std::vector<double> parameters_flat;
};

struct FedAvgFixture {
    std::vector<FedAvgLayoutEntry> layout;
    std::vector<FedAvgClient> clients;
    std::vector<double> expected_global_flat;
    std::size_t param_count = 0;
    std::size_t total_examples = 0;
};

inline std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open JSON fixture file: " + path.string());
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

inline std::size_t find_required(
    const std::string& text,
    const std::string& pattern,
    std::size_t start = 0) {
    const std::size_t pos = text.find(pattern, start);
    if (pos == std::string::npos) {
        throw std::runtime_error("missing JSON pattern: " + pattern);
    }
    return pos;
}

inline std::size_t find_matching_bracket(
    const std::string& text,
    std::size_t open_pos) {
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (std::size_t pos = open_pos; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = in_string;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                return pos;
            }
        }
    }

    throw std::runtime_error("unterminated JSON array");
}

inline std::vector<double> parse_number_array(
    const std::string& text,
    std::size_t open_pos) {
    const std::size_t close_pos = find_matching_bracket(text, open_pos);
    std::vector<double> values;
    std::size_t pos = open_pos + 1;

    while (pos < close_pos) {
        while (pos < close_pos &&
               (std::isspace(static_cast<unsigned char>(text[pos])) ||
                text[pos] == ',')) {
            ++pos;
        }
        if (pos >= close_pos) {
            break;
        }

        std::size_t parsed = 0;
        values.push_back(std::stod(text.substr(pos, close_pos - pos), &parsed));
        pos += parsed;
    }

    return values;
}

inline std::size_t parse_size_after_key(
    const std::string& text,
    const std::string& key,
    std::size_t start) {
    const std::size_t key_pos = find_required(text, key, start);
    const std::size_t colon_pos = find_required(text, ":", key_pos);
    std::size_t pos = colon_pos + 1;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    std::size_t parsed = 0;
    const auto value = static_cast<std::size_t>(std::stoull(text.substr(pos), &parsed));
    (void)parsed;
    return value;
}

inline std::string parse_string_after_key(
    const std::string& text,
    const std::string& key,
    std::size_t start) {
    const std::size_t key_pos = find_required(text, key, start);
    const std::size_t colon_pos = find_required(text, ":", key_pos);
    const std::size_t quote_pos = find_required(text, "\"", colon_pos + 1);
    const std::size_t end_quote_pos = find_required(text, "\"", quote_pos + 1);
    return text.substr(quote_pos + 1, end_quote_pos - quote_pos - 1);
}

inline std::vector<FedAvgLayoutEntry> load_fedavg_layout(
    const std::filesystem::path& path) {
    const std::string text = read_text_file(path);
    std::vector<FedAvgLayoutEntry> layout;
    std::size_t pos = 0;

    while (true) {
        const std::size_t name_pos = text.find("\"name\"", pos);
        if (name_pos == std::string::npos) {
            break;
        }

        FedAvgLayoutEntry entry;
        entry.name = parse_string_after_key(text, "\"name\"", name_pos);
        entry.start = parse_size_after_key(text, "\"start\"", name_pos);
        entry.end = parse_size_after_key(text, "\"end\"", name_pos);
        if (entry.end <= entry.start) {
            throw std::runtime_error("invalid FedAvg layout entry in " + path.string());
        }
        layout.push_back(entry);
        pos = name_pos + 1;
    }

    if (layout.empty()) {
        throw std::runtime_error("FedAvg layout has no entries: " + path.string());
    }
    return layout;
}

inline std::vector<FedAvgClient> load_fedavg_clients(
    const std::filesystem::path& path) {
    const std::string text = read_text_file(path);
    std::vector<FedAvgClient> clients;
    std::size_t pos = 0;

    while (true) {
        const std::size_t id_pos = text.find("\"client_id\"", pos);
        if (id_pos == std::string::npos) {
            break;
        }

        FedAvgClient client;
        client.client_id = parse_string_after_key(text, "\"client_id\"", id_pos);
        client.num_examples = parse_size_after_key(text, "\"num_examples\"", id_pos);
        const std::size_t flat_key_pos = find_required(text, "\"parameters_flat\"", id_pos);
        const std::size_t open_pos = find_required(text, "[", flat_key_pos);
        client.parameters_flat = parse_number_array(text, open_pos);
        clients.push_back(client);
        pos = open_pos + 1;
    }

    if (clients.empty()) {
        throw std::runtime_error("FedAvg clients fixture has no clients: " + path.string());
    }
    return clients;
}

inline std::vector<double> load_expected_global_flat(
    const std::filesystem::path& path) {
    const std::string text = read_text_file(path);
    const std::size_t key_pos = find_required(text, "\"expected_global_flat\"");
    const std::size_t open_pos = find_required(text, "[", key_pos);
    const auto values = parse_number_array(text, open_pos);
    if (values.empty()) {
        throw std::runtime_error("expected_global_flat is empty: " + path.string());
    }
    return values;
}

inline std::vector<std::vector<double>> unflatten_by_layout(
    const std::vector<double>& flat,
    const std::vector<FedAvgLayoutEntry>& layout) {
    std::vector<std::vector<double>> tensors;
    tensors.reserve(layout.size());
    for (const auto& entry : layout) {
        if (entry.end > flat.size()) {
            throw std::runtime_error("FedAvg layout slice exceeds flat vector length");
        }
        tensors.emplace_back(flat.begin() + static_cast<std::ptrdiff_t>(entry.start),
                             flat.begin() + static_cast<std::ptrdiff_t>(entry.end));
    }
    return tensors;
}

inline FedAvgFixture load_fedavg_fixture_dir(const std::filesystem::path& dir) {
    FedAvgFixture fixture;
    fixture.layout = load_fedavg_layout(dir / "layout.json");
    fixture.clients = load_fedavg_clients(dir / "clients.json");
    fixture.expected_global_flat = load_expected_global_flat(dir / "expected_global.json");
    fixture.param_count = fixture.expected_global_flat.size();

    for (const auto& client : fixture.clients) {
        if (client.parameters_flat.size() != fixture.param_count) {
            throw std::runtime_error(
                "FedAvg client " + client.client_id + " has wrong parameter count");
        }
        fixture.total_examples += client.num_examples;
    }
    if (fixture.total_examples == 0) {
        throw std::runtime_error("FedAvg fixture total_examples is zero");
    }
    if (fixture.layout.back().end != fixture.param_count) {
        throw std::runtime_error("FedAvg layout param count does not match expected_global_flat");
    }

    return fixture;
}

inline std::vector<double> plain_fedavg_flat(const FedAvgFixture& fixture) {
    std::vector<double> global(fixture.param_count, 0.0);
    for (const auto& client : fixture.clients) {
        const double alpha =
            static_cast<double>(client.num_examples) /
            static_cast<double>(fixture.total_examples);
        for (std::size_t idx = 0; idx < fixture.param_count; ++idx) {
            global[idx] += alpha * client.parameters_flat[idx];
        }
    }
    return global;
}
