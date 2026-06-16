#include "fedavg_fixture.h"
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
    std::size_t threads = 1;
};

struct FedAvgResult {
    std::string fixture;
    std::string backend;
    std::size_t clients = 0;
    std::size_t parameters = 0;
    std::size_t chunks = 0;
    std::size_t slots = 0;
    std::size_t threads = 1;
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
        << "[--backend plain|openfhe_ckks|all] [--threads 1] "
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
            args.threads = static_cast<std::size_t>(std::stoull(need_value(flag)));
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
    if (args.threads == 0) {
        throw std::runtime_error("--threads must be positive");
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
            << "flatten_time_ms,plain_aggregate_time_ms,encode_time_ms,encrypt_time_ms,"
            << "serialize_time_ms,deserialize_time_ms,he_merge_time_ms,decrypt_time_ms,"
            << "decode_time_ms,unflatten_time_ms,total_he_time_ms,"
            << "mae_vs_expected,max_abs_error_vs_expected,plain_payload_bytes,"
            << "serialized_ciphertext_bytes,requested_ring_dimension,actual_ring_dimension,"
            << "multiplicative_depth,scaling_mod_size,first_mod_size,notes\n";
    }
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << result.fixture << ','
           << result.backend << ','
           << result.clients << ','
           << result.parameters << ','
           << result.chunks << ','
           << result.slots << ','
           << result.threads << ','
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
    result.threads = configure_threads(args.threads);
    result.requested_ring_dimension = args.ckks_ring_dim;
    result.multiplicative_depth = args.ckks_depth;
    result.scaling_mod_size = args.ckks_scaling_mod_size;
    result.first_mod_size = args.ckks_first_mod_size;

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
    result.actual_ring_dimension = cc->GetRingDimension();
    result.slots = args.ckks_batch_size == 0
        ? result.actual_ring_dimension / 2
        : args.ckks_batch_size;
    result.chunks = ceil_div(fixture.param_count, result.slots);
    result.plain_payload_bytes =
        fixture.clients.size() * fixture.param_count * sizeof(double);

    std::vector<std::vector<std::string>> serialized_by_client;
    serialized_by_client.reserve(fixture.clients.size());

    for (const auto& client : fixture.clients) {
        std::vector<std::string> serialized_chunks;
        serialized_chunks.reserve(result.chunks);

        for (std::size_t offset = 0; offset < fixture.param_count; offset += result.slots) {
            const std::size_t used = std::min(result.slots, fixture.param_count - offset);
            std::vector<double> chunk(result.slots, 0.0);
            for (std::size_t i = 0; i < used; ++i) {
                chunk[i] = client.parameters_flat[offset + i];
            }

            const Timer encode_timer;
            Plaintext plaintext = cc->MakeCKKSPackedPlaintext(chunk);
            result.encode_time_ms += encode_timer.elapsed_ms();

            const Timer encrypt_timer;
            auto ciphertext = cc->Encrypt(keys.publicKey, plaintext);
            result.encrypt_time_ms += encrypt_timer.elapsed_ms();

            const Timer serialize_timer;
            serialized_chunks.push_back(serialize_ciphertext_to_string(ciphertext));
            result.serialize_time_ms += serialize_timer.elapsed_ms();
            result.serialized_ciphertext_bytes += serialized_chunks.back().size();
        }

        serialized_by_client.push_back(std::move(serialized_chunks));
    }

    std::vector<std::vector<Ciphertext<DCRTPoly>>> ciphertext_by_client;
    ciphertext_by_client.reserve(serialized_by_client.size());
    for (const auto& serialized_chunks : serialized_by_client) {
        std::vector<Ciphertext<DCRTPoly>> chunks;
        chunks.reserve(serialized_chunks.size());
        for (const auto& serialized : serialized_chunks) {
            const Timer deserialize_timer;
            chunks.push_back(deserialize_ciphertext_from_string(serialized));
            result.deserialize_time_ms += deserialize_timer.elapsed_ms();
        }
        ciphertext_by_client.push_back(std::move(chunks));
    }

    std::vector<Ciphertext<DCRTPoly>> global_chunks;
    global_chunks.reserve(result.chunks);
    const Timer merge_timer;
    for (std::size_t chunk_index = 0; chunk_index < result.chunks; ++chunk_index) {
        bool has_chunk = false;
        Ciphertext<DCRTPoly> global_chunk;
        for (std::size_t client_index = 0; client_index < fixture.clients.size(); ++client_index) {
            const double alpha =
                static_cast<double>(fixture.clients[client_index].num_examples) /
                static_cast<double>(fixture.total_examples);
            auto weighted = cc->EvalMult(ciphertext_by_client[client_index][chunk_index], alpha);
            if (has_chunk) {
                global_chunk = cc->EvalAdd(global_chunk, weighted);
            } else {
                global_chunk = weighted;
                has_chunk = true;
            }
        }
        global_chunks.push_back(global_chunk);
    }
    result.he_merge_time_ms = merge_timer.elapsed_ms();

    std::vector<double> decoded_flat;
    decoded_flat.reserve(result.chunks * result.slots);
    for (const auto& chunk : global_chunks) {
        Plaintext plaintext;
        const Timer decrypt_timer;
        cc->Decrypt(keys.secretKey, chunk, &plaintext);
        result.decrypt_time_ms += decrypt_timer.elapsed_ms();

        const Timer decode_timer;
        plaintext->SetLength(result.slots);
        const auto values = plaintext->GetRealPackedValue();
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
        result.decode_time_ms + result.unflatten_time_ms;
    result.notes =
#ifdef _OPENMP
        "fedavg_json_fixture;server_weighted;ciphertext_serialized_in_memory;public_num_examples;omp_set_num_threads";
#else
        "fedavg_json_fixture;server_weighted;ciphertext_serialized_in_memory;public_num_examples;openmp_not_seen_by_runner";
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
            const auto plain = run_plain_fedavg(fixture, fixture_name);
            append_result_csv(args.results_path, plain);
            std::cout << "Ran " << plain.backend
                      << " fixture=" << plain.fixture
                      << " params=" << plain.parameters
                      << " mae=" << plain.mae_vs_expected
                      << " max_abs=" << plain.max_abs_error_vs_expected << '\n';
        }

        if (args.backend == "openfhe_ckks" || args.backend == "all") {
#ifdef UTILITY_BENCH_WITH_OPENFHE
            const auto he = run_openfhe_fedavg(fixture, fixture_name, args);
            append_result_csv(args.results_path, he);
            std::cout << "Ran " << he.backend
                      << " fixture=" << he.fixture
                      << " params=" << he.parameters
                      << " chunks=" << he.chunks
                      << " he_total=" << he.total_he_time_ms
                      << " mae=" << he.mae_vs_expected
                      << " max_abs=" << he.max_abs_error_vs_expected << '\n';
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
