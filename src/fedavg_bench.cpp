#include "fedavg_fixture.h"
#include "resource_usage.h"
#include "timer.h"

#ifdef UTILITY_BENCH_WITH_OPENFHE
#include "openfhe.h"
#include "ciphertext-ser.h"
#include "cryptocontext-ser.h"
#include "key/key-ser.h"
#include "scheme/ckksrns/ckksrns-ser.h"

#ifdef _OPENMP
#include <omp.h>
#endif
#endif

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct CliArgs {
    std::filesystem::path fixture_dir = "data/generated_fedavg/mini_mlp_75_c4";
    std::filesystem::path results_path = "results/fedavg_results.csv";
    std::string backend = "plain";
    std::size_t ckks_ring_dim = 0;
    std::size_t ckks_batch_size = 0;
    std::size_t ckks_depth = 1;
    std::size_t ckks_scaling_mod_size = 50;
    std::size_t ckks_first_mod_size = 60;
    std::vector<std::size_t> threads = {1};
    std::size_t repeat_count = 1;
};

struct FedAvgResult {
    std::string fixture;
    std::string backend;
    std::size_t clients = 0;
    std::size_t parameters = 0;
    std::size_t chunks = 0;
    std::size_t slots = 0;
    std::size_t threads = 1;
    double setup_time_ms = 0.0;
    double flatten_time_ms = 0.0;
    double plain_aggregate_time_ms = 0.0;
    double encode_time_ms = 0.0;
    double encrypt_time_ms = 0.0;
    double serialize_time_ms = 0.0;
    double deserialize_time_ms = 0.0;
    double he_merge_time_ms = 0.0;
    double decrypt_time_ms = 0.0;
    double decode_time_ms = 0.0;
    double unflatten_time_ms = 0.0;
    double total_he_time_ms = 0.0;
    double mae_vs_expected = 0.0;
    double max_abs_error_vs_expected = 0.0;
    std::size_t plain_payload_bytes = 0;
    std::size_t serialized_ciphertext_bytes = 0;
    double serialized_ciphertext_kb = 0.0;
    std::size_t peak_rss_kb = 0;
    std::size_t requested_ring_dimension = 0;
    std::size_t actual_ring_dimension = 0;
    std::size_t multiplicative_depth = 0;
    std::size_t scaling_mod_size = 0;
    std::size_t first_mod_size = 0;
    std::string notes;
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--fixture data/generated_fedavg/mini_mlp_75_c4] "
        << "[--backend plain|openfhe_ckks|all] [--threads 1 4 8] "
        << "[--repeat 3] "
        << "[--ckks-ring-dim 0] [--ckks-batch-size 0] "
        << "[--ckks-depth 1] [--ckks-scale-bits 50] [--ckks-first-mod-bits 60] "
        << "[--results results/fedavg_results.csv]\n";
}

CliArgs parse_args(int argc, char** argv) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--help" || flag == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
        auto need_value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(name + " requires a value");
            }
            return argv[++i];
        };
        if (flag == "--fixture") {
            args.fixture_dir = need_value(flag);
        } else if (flag == "--backend") {
            args.backend = need_value(flag);
        } else if (flag == "--threads") {
            args.threads.clear();
            while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                args.threads.push_back(static_cast<std::size_t>(std::stoull(argv[++i])));
            }
            if (args.threads.empty()) {
                throw std::runtime_error("--threads requires at least one value");
            }
        } else if (flag == "--repeat") {
            args.repeat_count = static_cast<std::size_t>(std::stoull(need_value(flag)));
        } else if (flag == "--ckks-ring-dim") {
            args.ckks_ring_dim = static_cast<std::size_t>(std::stoull(need_value(flag)));
        } else if (flag == "--ckks-batch-size") {
            args.ckks_batch_size = static_cast<std::size_t>(std::stoull(need_value(flag)));
        } else if (flag == "--ckks-depth") {
            args.ckks_depth = static_cast<std::size_t>(std::stoull(need_value(flag)));
        } else if (flag == "--ckks-scale-bits") {
            args.ckks_scaling_mod_size = static_cast<std::size_t>(std::stoull(need_value(flag)));
        } else if (flag == "--ckks-first-mod-bits") {
            args.ckks_first_mod_size = static_cast<std::size_t>(std::stoull(need_value(flag)));
        } else if (flag == "--results") {
            args.results_path = need_value(flag);
        } else {
            throw std::runtime_error("unknown argument: " + flag);
        }
    }
    if (args.backend != "plain" && args.backend != "openfhe_ckks" && args.backend != "all") {
        throw std::runtime_error("--backend must be plain, openfhe_ckks, or all");
    }
    if (std::any_of(args.threads.begin(), args.threads.end(), [](std::size_t threads) {
            return threads == 0;
        })) {
        throw std::runtime_error("--threads values must be positive");
    }
    if (args.repeat_count == 0) {
        throw std::runtime_error("--repeat must be positive");
    }
    return args;
}

double divide_or_zero(double numerator, double denominator) {
    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

std::size_t ceil_div(std::size_t numerator, std::size_t denominator) {
    return (numerator + denominator - 1) / denominator;
}

std::pair<double, double> error_stats(
    const std::vector<double>& actual,
    const std::vector<double>& expected) {
    if (actual.size() != expected.size()) {
        throw std::runtime_error("FedAvg result size does not match expected result size");
    }
    double abs_sum = 0.0;
    double max_abs = 0.0;
    for (std::size_t idx = 0; idx < actual.size(); ++idx) {
        const double err = std::abs(actual[idx] - expected[idx]);
        abs_sum += err;
        max_abs = std::max(max_abs, err);
    }
    return {divide_or_zero(abs_sum, static_cast<double>(actual.size())), max_abs};
}

bool file_exists_and_has_content(const std::filesystem::path& path) {
    return std::filesystem::exists(path) && std::filesystem::file_size(path) > 0;
}

void append_result_csv(const std::filesystem::path& path, const FedAvgResult& result) {
    std::filesystem::create_directories(path.parent_path());
    const bool write_header = !file_exists_and_has_content(path);
    std::ofstream output(path, std::ios::app);
    if (!output) {
        throw std::runtime_error("failed to open FedAvg result CSV: " + path.string());
    }
    if (write_header) {
        output
            << "fixture,backend,clients,parameters,chunks,slots,threads,"
            << "setup_time_ms,flatten_time_ms,plain_aggregate_time_ms,"
            << "encode_time_ms,encrypt_time_ms,"
            << "serialize_time_ms,deserialize_time_ms,he_merge_time_ms,decrypt_time_ms,"
            << "decode_time_ms,unflatten_time_ms,total_he_time_ms,"
            << "mae_vs_expected,max_abs_error_vs_expected,plain_payload_bytes,"
            << "serialized_ciphertext_bytes,serialized_ciphertext_kb,peak_rss_kb,"
            << "requested_ring_dimension,actual_ring_dimension,multiplicative_depth,"
            << "scaling_mod_size,first_mod_size,notes\n";
    }
    const std::size_t peak_rss_kb =
        result.peak_rss_kb == 0 ? current_peak_rss_kb() : result.peak_rss_kb;
    const double serialized_ciphertext_kb =
        result.serialized_ciphertext_kb == 0.0
            ? bytes_to_kb(result.serialized_ciphertext_bytes)
            : result.serialized_ciphertext_kb;
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << result.fixture << ','
           << result.backend << ','
           << result.clients << ','
           << result.parameters << ','
           << result.chunks << ','
           << result.slots << ','
           << result.threads << ','
           << result.setup_time_ms << ','
           << result.flatten_time_ms << ','
           << result.plain_aggregate_time_ms << ','
           << result.encode_time_ms << ','
           << result.encrypt_time_ms << ','
           << result.serialize_time_ms << ','
           << result.deserialize_time_ms << ','
           << result.he_merge_time_ms << ','
           << result.decrypt_time_ms << ','
           << result.decode_time_ms << ','
           << result.unflatten_time_ms << ','
           << result.total_he_time_ms << ','
           << result.mae_vs_expected << ','
           << result.max_abs_error_vs_expected << ','
           << result.plain_payload_bytes << ','
           << result.serialized_ciphertext_bytes << ','
           << serialized_ciphertext_kb << ','
           << peak_rss_kb << ','
           << result.requested_ring_dimension << ','
           << result.actual_ring_dimension << ','
           << result.multiplicative_depth << ','
           << result.scaling_mod_size << ','
           << result.first_mod_size << ','
           << result.notes << '\n';
}

FedAvgResult run_plain_fedavg(
    const FedAvgFixture& fixture,
    const std::string& fixture_name) {
    FedAvgResult result;
    result.fixture = fixture_name;
    result.backend = "plain_flat_json";
    result.clients = fixture.clients.size();
    result.parameters = fixture.param_count;
    result.chunks = 0;
    result.slots = 0;
    result.plain_payload_bytes =
        fixture.clients.size() * fixture.param_count * sizeof(double);
    result.peak_rss_kb = current_peak_rss_kb();

    const Timer flatten_timer;
    std::vector<std::vector<double>> copied_flats;
    copied_flats.reserve(fixture.clients.size());
    for (const auto& client : fixture.clients) {
        copied_flats.push_back(client.parameters_flat);
    }
    result.flatten_time_ms = flatten_timer.elapsed_ms();

    const Timer aggregate_timer;
    const auto plain = plain_fedavg_flat(fixture);
    result.plain_aggregate_time_ms = aggregate_timer.elapsed_ms();

    const Timer unflatten_timer;
    const auto tensors = unflatten_by_layout(plain, fixture.layout);
    result.unflatten_time_ms = unflatten_timer.elapsed_ms();
    (void)tensors;

    const auto [mae, max_abs] = error_stats(plain, fixture.expected_global_flat);
    result.mae_vs_expected = mae;
    result.max_abs_error_vs_expected = max_abs;
    result.notes = "json_fixture;plain_flat_fedavg;expected_global_json_check";
    return result;
}

std::string repeat_note(std::size_t repeat_index, std::size_t repeat_count) {
    return "repeat_index=" + std::to_string(repeat_index) +
           ";repeat_count=" + std::to_string(repeat_count);
}

double average_field(const std::vector<FedAvgResult>& results, double FedAvgResult::*field) {
    double sum = 0.0;
    for (const auto& result : results) {
        sum += result.*field;
    }
    return divide_or_zero(sum, static_cast<double>(results.size()));
}

FedAvgResult summarize_fedavg_results(
    const std::vector<FedAvgResult>& results,
    const std::string& backend) {
    if (results.empty()) {
        throw std::runtime_error("cannot summarize empty FedAvg result list");
    }

    FedAvgResult summary = results.front();
    summary.backend = backend + "_summary_avg";
    summary.setup_time_ms = average_field(results, &FedAvgResult::setup_time_ms);
    summary.flatten_time_ms = average_field(results, &FedAvgResult::flatten_time_ms);
    summary.plain_aggregate_time_ms =
        average_field(results, &FedAvgResult::plain_aggregate_time_ms);
    summary.encode_time_ms = average_field(results, &FedAvgResult::encode_time_ms);
    summary.encrypt_time_ms = average_field(results, &FedAvgResult::encrypt_time_ms);
    summary.serialize_time_ms = average_field(results, &FedAvgResult::serialize_time_ms);
    summary.deserialize_time_ms = average_field(results, &FedAvgResult::deserialize_time_ms);
    summary.he_merge_time_ms = average_field(results, &FedAvgResult::he_merge_time_ms);
    summary.decrypt_time_ms = average_field(results, &FedAvgResult::decrypt_time_ms);
    summary.decode_time_ms = average_field(results, &FedAvgResult::decode_time_ms);
    summary.unflatten_time_ms = average_field(results, &FedAvgResult::unflatten_time_ms);
    summary.total_he_time_ms = average_field(results, &FedAvgResult::total_he_time_ms);
    summary.mae_vs_expected = average_field(results, &FedAvgResult::mae_vs_expected);
    summary.max_abs_error_vs_expected =
        average_field(results, &FedAvgResult::max_abs_error_vs_expected);
    summary.serialized_ciphertext_kb =
        average_field(results, &FedAvgResult::serialized_ciphertext_kb);
    summary.notes = "repeat_summary;repeat_count=" + std::to_string(results.size()) +
        ";real_calculation_ms=he_merge_time_ms" +
        ";overall_lifecycle_ms=total_he_time_ms;setup_keygen_excluded_from_total";
    return summary;
}

#ifdef UTILITY_BENCH_WITH_OPENFHE
std::size_t configure_threads(std::size_t requested_threads) {
#ifdef _OPENMP
    omp_set_num_threads(static_cast<int>(requested_threads));
    return static_cast<std::size_t>(std::max(1, omp_get_max_threads()));
#else
    return requested_threads;
#endif
}

std::string serialize_ciphertext_to_string(
    const lbcrypto::Ciphertext<lbcrypto::DCRTPoly>& ciphertext) {
    std::ostringstream stream;
    lbcrypto::Serial::Serialize(ciphertext, stream, lbcrypto::SerType::BINARY);
    return stream.str();
}

lbcrypto::Ciphertext<lbcrypto::DCRTPoly> deserialize_ciphertext_from_string(
    const std::string& serialized) {
    lbcrypto::Ciphertext<lbcrypto::DCRTPoly> ciphertext;
    std::istringstream stream(serialized);
    lbcrypto::Serial::Deserialize(ciphertext, stream, lbcrypto::SerType::BINARY);
    return ciphertext;
}

FedAvgResult run_openfhe_fedavg(
    const FedAvgFixture& fixture,
    const std::string& fixture_name,
    const CliArgs& args) {
    using lbcrypto::ADVANCEDSHE;
    using lbcrypto::CCParams;
    using lbcrypto::Ciphertext;
    using lbcrypto::CryptoContext;
    using lbcrypto::CryptoContextCKKSRNS;
    using lbcrypto::DCRTPoly;
    using lbcrypto::GenCryptoContext;
    using lbcrypto::HEStd_128_classic;
    using lbcrypto::KEYSWITCH;
    using lbcrypto::LEVELEDSHE;
    using lbcrypto::PKE;
    using lbcrypto::Plaintext;

    FedAvgResult result;
    result.fixture = fixture_name;
    result.backend = "openfhe_ckks_fedavg";
    result.clients = fixture.clients.size();
    result.parameters = fixture.param_count;
    result.threads = configure_threads(args.threads.front());
    result.requested_ring_dimension = args.ckks_ring_dim;
    result.multiplicative_depth = args.ckks_depth;
    result.scaling_mod_size = args.ckks_scaling_mod_size;
    result.first_mod_size = args.ckks_first_mod_size;

    const Timer setup_timer;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(static_cast<uint32_t>(args.ckks_depth));
    parameters.SetScalingModSize(static_cast<uint32_t>(args.ckks_scaling_mod_size));
    parameters.SetFirstModSize(static_cast<uint32_t>(args.ckks_first_mod_size));
    parameters.SetSecurityLevel(HEStd_128_classic);
    if (args.ckks_ring_dim != 0) {
        parameters.SetRingDim(static_cast<uint32_t>(args.ckks_ring_dim));
    }
    if (args.ckks_batch_size != 0) {
        parameters.SetBatchSize(static_cast<uint32_t>(args.ckks_batch_size));
    }

    CryptoContext<DCRTPoly> cc = GenCryptoContext(parameters);
    cc->Enable(PKE);
    cc->Enable(KEYSWITCH);
    cc->Enable(LEVELEDSHE);
    cc->Enable(ADVANCEDSHE);

    const auto keys = cc->KeyGen();
    result.setup_time_ms = setup_timer.elapsed_ms();
    result.actual_ring_dimension = cc->GetRingDimension();
    result.slots = args.ckks_batch_size == 0
        ? result.actual_ring_dimension / 2
        : args.ckks_batch_size;
    result.chunks = ceil_div(fixture.param_count, result.slots);
    result.plain_payload_bytes =
        fixture.clients.size() * fixture.param_count * sizeof(double);

    std::vector<double> client_alphas;
    client_alphas.reserve(fixture.clients.size());
    for (const auto& client : fixture.clients) {
        client_alphas.push_back(
            static_cast<double>(client.num_examples) /
            static_cast<double>(fixture.total_examples));
    }

    std::vector<double> decoded_flat;
    decoded_flat.reserve(result.chunks * result.slots);
    for (std::size_t chunk_index = 0; chunk_index < result.chunks; ++chunk_index) {
        const std::size_t offset = chunk_index * result.slots;
        const std::size_t used = std::min(result.slots, fixture.param_count - offset);
        bool has_chunk = false;
        Ciphertext<DCRTPoly> global_chunk;

        for (std::size_t client_index = 0; client_index < fixture.clients.size(); ++client_index) {
            std::vector<double> chunk(result.slots, 0.0);
            const auto& client_flat = fixture.clients[client_index].parameters_flat;
            for (std::size_t i = 0; i < used; ++i) {
                chunk[i] = client_flat[offset + i];
            }

            const Timer encode_timer;
            Plaintext input_plaintext = cc->MakeCKKSPackedPlaintext(chunk);
            result.encode_time_ms += encode_timer.elapsed_ms();

            const Timer encrypt_timer;
            auto encrypted_chunk = cc->Encrypt(keys.publicKey, input_plaintext);
            result.encrypt_time_ms += encrypt_timer.elapsed_ms();

            const Timer serialize_timer;
            const std::string serialized = serialize_ciphertext_to_string(encrypted_chunk);
            result.serialize_time_ms += serialize_timer.elapsed_ms();
            result.serialized_ciphertext_bytes += serialized.size();

            const Timer deserialize_timer;
            auto deserialized_chunk = deserialize_ciphertext_from_string(serialized);
            result.deserialize_time_ms += deserialize_timer.elapsed_ms();

            const Timer merge_timer;
            auto weighted = cc->EvalMult(deserialized_chunk, client_alphas[client_index]);
            if (has_chunk) {
                global_chunk = cc->EvalAdd(global_chunk, weighted);
            } else {
                global_chunk = weighted;
                has_chunk = true;
            }
            result.he_merge_time_ms += merge_timer.elapsed_ms();
        }

        Plaintext output_plaintext;
        const Timer decrypt_timer;
        cc->Decrypt(keys.secretKey, global_chunk, &output_plaintext);
        result.decrypt_time_ms += decrypt_timer.elapsed_ms();

        const Timer decode_timer;
        output_plaintext->SetLength(result.slots);
        const auto values = output_plaintext->GetRealPackedValue();
        decoded_flat.insert(decoded_flat.end(), values.begin(), values.end());
        result.decode_time_ms += decode_timer.elapsed_ms();
    }
    decoded_flat.resize(fixture.param_count);

    const Timer unflatten_timer;
    const auto tensors = unflatten_by_layout(decoded_flat, fixture.layout);
    result.unflatten_time_ms = unflatten_timer.elapsed_ms();
    (void)tensors;

    const auto [mae, max_abs] = error_stats(decoded_flat, fixture.expected_global_flat);
    result.mae_vs_expected = mae;
    result.max_abs_error_vs_expected = max_abs;
    result.total_he_time_ms =
        result.encode_time_ms + result.encrypt_time_ms + result.serialize_time_ms +
        result.deserialize_time_ms + result.he_merge_time_ms + result.decrypt_time_ms +
        result.decode_time_ms;
    result.serialized_ciphertext_kb = bytes_to_kb(result.serialized_ciphertext_bytes);
    result.peak_rss_kb = current_peak_rss_kb();
    result.notes =
#ifdef _OPENMP
        "fedavg_json_fixture;server_weighted;chunk_pipeline;ciphertext_serialized_in_memory;public_num_examples;he_total_excludes_json_load_and_unflatten;omp_set_num_threads";
#else
        "fedavg_json_fixture;server_weighted;chunk_pipeline;ciphertext_serialized_in_memory;public_num_examples;he_total_excludes_json_load_and_unflatten;openmp_not_seen_by_runner";
#endif
    return result;
}
#endif

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliArgs args = parse_args(argc, argv);
        const auto fixture = load_fedavg_fixture_dir(args.fixture_dir);
        const std::string fixture_name = args.fixture_dir.filename().string();

        if (args.backend == "plain" || args.backend == "all") {
            std::vector<FedAvgResult> plain_results;
            plain_results.reserve(args.repeat_count);
            for (std::size_t repeat_index = 1;
                 repeat_index <= args.repeat_count;
                 ++repeat_index) {
                auto plain = run_plain_fedavg(fixture, fixture_name);
                plain.notes += ";" + repeat_note(repeat_index, args.repeat_count);
                plain_results.push_back(plain);
                append_result_csv(args.results_path, plain_results.back());
                std::cout << "Ran " << plain.backend
                          << " repeat=" << repeat_index << '/' << args.repeat_count
                          << " fixture=" << plain.fixture
                          << " params=" << plain.parameters
                          << " plain_calc=" << plain.plain_aggregate_time_ms
                          << " ms mae=" << plain.mae_vs_expected
                          << " max_abs=" << plain.max_abs_error_vs_expected << '\n';
            }
            const auto plain_summary =
                summarize_fedavg_results(plain_results, "plain_flat_json");
            append_result_csv(args.results_path, plain_summary);
            std::cout << "Summary " << plain_summary.backend
                      << " repeats=" << args.repeat_count
                      << " avg_plain_calc=" << plain_summary.plain_aggregate_time_ms
                      << " ms\n";
        }

        if (args.backend == "openfhe_ckks" || args.backend == "all") {
#ifdef UTILITY_BENCH_WITH_OPENFHE
            for (const std::size_t requested_threads : args.threads) {
                std::vector<FedAvgResult> he_results;
                he_results.reserve(args.repeat_count);
                for (std::size_t repeat_index = 1;
                     repeat_index <= args.repeat_count;
                     ++repeat_index) {
                    CliArgs thread_args = args;
                    thread_args.threads = {requested_threads};
                    auto he = run_openfhe_fedavg(fixture, fixture_name, thread_args);
                    he.notes += ";" + repeat_note(repeat_index, args.repeat_count);
                    he_results.push_back(he);
                    append_result_csv(args.results_path, he_results.back());
                    std::cout << "Ran " << he.backend
                              << " repeat=" << repeat_index << '/' << args.repeat_count
                              << " fixture=" << he.fixture
                              << " params=" << he.parameters
                              << " chunks=" << he.chunks
                              << " threads=" << he.threads
                              << " he_total=" << he.total_he_time_ms
                              << " he_calc=" << he.he_merge_time_ms
                              << " mae=" << he.mae_vs_expected
                              << " max_abs=" << he.max_abs_error_vs_expected << '\n';
                }
                const auto he_summary =
                    summarize_fedavg_results(he_results, he_results.front().backend);
                append_result_csv(args.results_path, he_summary);
                std::cout << "Summary " << he_summary.backend
                          << " threads=" << he_summary.threads
                          << " repeats=" << args.repeat_count
                          << " avg_total=" << he_summary.total_he_time_ms
                          << " ms avg_calc=" << he_summary.he_merge_time_ms
                          << " ms avg_mae=" << he_summary.mae_vs_expected << '\n';
            }
#else
            throw std::runtime_error(
                "fedavg_bench was built without UTILITY_BENCH_WITH_OPENFHE=ON");
#endif
        }

        std::cout << "Wrote results: " << args.results_path << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
