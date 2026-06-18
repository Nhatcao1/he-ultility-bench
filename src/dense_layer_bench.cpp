#include "csv_loader.h"
#include "timer.h"

#ifdef UTILITY_BENCH_WITH_OPENFHE
#include "openfhe.h"
#ifdef _OPENMP
#include <omp.h>
#endif
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kInputDim = 4;
constexpr std::size_t kOutputDim = 8;

enum class BackendMode {
    PlainCpp,
    OpenFheCkks,
    All,
};

struct CliArgs {
    std::filesystem::path fixture_dir = "data/generated_dense/dense4x8_100k";
    std::filesystem::path results_path = "results/dense_layer_results.csv";
    BackendMode backend = BackendMode::PlainCpp;
    std::vector<std::size_t> thread_counts = {1, 4, 8};
    std::size_t max_rows = 0;
    std::size_t ckks_ring_dim = 0;
    std::size_t ckks_batch_size = 0;
    std::size_t ckks_depth = 2;
    std::size_t ckks_scaling_mod_size = 50;
    std::size_t ckks_first_mod_size = 60;
};

struct DenseFixture {
    std::array<std::vector<double>, kInputDim> features;
    std::array<std::array<double, kOutputDim>, kInputDim> weights{};
    std::array<double, kOutputDim> bias{};

    std::size_t rows() const {
        return features[0].size();
    }
};

struct DenseResult {
    std::string operation = "dense4x8_vector";
    std::string backend;
    std::size_t rows = 0;
    std::size_t input_dim = kInputDim;
    std::size_t output_dim = kOutputDim;
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
    double result_mean = 0.0;
    double baseline_mean = 0.0;
    double mean_absolute_error = 0.0;
    double max_absolute_error = 0.0;
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
    std::string notes;
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--fixture data/generated_dense/dense4x8_100k] "
        << "[--backend plain_cpp|openfhe_ckks|all] [--threads 1 4 8] "
        << "[--max-rows 10000] [--ckks-ring-dim 0] [--ckks-batch-size 0] "
        << "[--ckks-depth 2] [--ckks-scale-bits 50] [--ckks-first-mod-bits 60] "
        << "[--results results/dense_layer_results.csv]\n";
}

BackendMode parse_backend(const std::string& value) {
    if (value == "plain_cpp") {
        return BackendMode::PlainCpp;
    }
    if (value == "openfhe_ckks") {
        return BackendMode::OpenFheCkks;
    }
    if (value == "all") {
        return BackendMode::All;
    }
    throw std::runtime_error("unknown backend: " + value);
}

bool wants_plain(BackendMode backend) {
    return backend == BackendMode::PlainCpp || backend == BackendMode::All;
}

bool wants_openfhe(BackendMode backend) {
    return backend == BackendMode::OpenFheCkks || backend == BackendMode::All;
}

std::size_t parse_size_arg(int& i, int argc, char** argv, const std::string& flag) {
    if (i + 1 >= argc) {
        throw std::runtime_error(flag + " requires a value");
    }
    return static_cast<std::size_t>(std::stoull(argv[++i]));
}

CliArgs parse_args(int argc, char** argv) {
    CliArgs args;

    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--help" || flag == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (flag == "--fixture") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--fixture requires a path");
            }
            args.fixture_dir = argv[++i];
        } else if (flag == "--results") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--results requires a path");
            }
            args.results_path = argv[++i];
        } else if (flag == "--backend") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--backend requires a value");
            }
            args.backend = parse_backend(argv[++i]);
        } else if (flag == "--threads") {
            args.thread_counts.clear();
            while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                const auto parsed = static_cast<std::size_t>(std::stoull(argv[++i]));
                if (parsed == 0) {
                    throw std::runtime_error("--threads values must be positive");
                }
                args.thread_counts.push_back(parsed);
            }
            if (args.thread_counts.empty()) {
                throw std::runtime_error("--threads requires at least one value");
            }
        } else if (flag == "--max-rows") {
            args.max_rows = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--ckks-ring-dim") {
            args.ckks_ring_dim = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--ckks-batch-size") {
            args.ckks_batch_size = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--ckks-depth") {
            args.ckks_depth = parse_size_arg(i, argc, argv, flag);
            if (args.ckks_depth == 0) {
                throw std::runtime_error("--ckks-depth must be positive");
            }
        } else if (flag == "--ckks-scale-bits") {
            args.ckks_scaling_mod_size = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--ckks-first-mod-bits") {
            args.ckks_first_mod_size = parse_size_arg(i, argc, argv, flag);
        } else {
            throw std::runtime_error("unknown argument: " + flag);
        }
    }

    return args;
}

double divide_or_zero(double numerator, double denominator) {
    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

std::size_t ceil_div(std::size_t numerator, std::size_t denominator) {
    return (numerator + denominator - 1) / denominator;
}

bool file_exists_and_has_content(const std::filesystem::path& path) {
    return std::filesystem::exists(path) && std::filesystem::file_size(path) > 0;
}

DenseFixture load_dense_fixture(const std::filesystem::path& fixture_dir, std::size_t max_rows) {
    DenseFixture fixture;

    {
        const auto path = fixture_dir / "features.csv";
        std::ifstream input(path);
        if (!input) {
            throw std::runtime_error("failed to open dense features CSV: " + path.string());
        }

        std::string line;
        if (!std::getline(input, line)) {
            throw std::runtime_error("features CSV is empty: " + path.string());
        }

        std::size_t row_number = 1;
        while (std::getline(input, line)) {
            ++row_number;
            if (line.empty()) {
                continue;
            }
            const auto fields = split_csv_line(line);
            if (fields.size() != kInputDim + 1) {
                throw std::runtime_error(
                    "expected 5 feature columns at row " + std::to_string(row_number));
            }
            for (std::size_t input_index = 0; input_index < kInputDim; ++input_index) {
                fixture.features[input_index].push_back(std::stod(fields[input_index + 1]));
            }
            if (max_rows != 0 && fixture.rows() >= max_rows) {
                break;
            }
        }
    }

    if (fixture.rows() == 0) {
        throw std::runtime_error("dense features fixture has no rows");
    }

    {
        const auto path = fixture_dir / "weights.csv";
        std::ifstream input(path);
        if (!input) {
            throw std::runtime_error("failed to open dense weights CSV: " + path.string());
        }

        std::string line;
        if (!std::getline(input, line)) {
            throw std::runtime_error("weights CSV is empty: " + path.string());
        }

        std::size_t loaded_rows = 0;
        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }
            const auto fields = split_csv_line(line);
            if (fields.size() != kOutputDim + 1) {
                throw std::runtime_error("expected 9 weight columns in " + path.string());
            }
            const auto input_index = static_cast<std::size_t>(std::stoull(fields[0]));
            if (input_index >= kInputDim) {
                throw std::runtime_error("dense weight input_index out of range");
            }
            for (std::size_t output_index = 0; output_index < kOutputDim; ++output_index) {
                fixture.weights[input_index][output_index] = std::stod(fields[output_index + 1]);
            }
            ++loaded_rows;
        }
        if (loaded_rows != kInputDim) {
            throw std::runtime_error("weights CSV must contain exactly 4 input rows");
        }
    }

    {
        const auto path = fixture_dir / "bias.csv";
        std::ifstream input(path);
        if (!input) {
            throw std::runtime_error("failed to open dense bias CSV: " + path.string());
        }

        std::string line;
        if (!std::getline(input, line)) {
            throw std::runtime_error("bias CSV is empty: " + path.string());
        }

        std::size_t loaded_rows = 0;
        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }
            const auto fields = split_csv_line(line);
            if (fields.size() != 2) {
                throw std::runtime_error("expected 2 bias columns in " + path.string());
            }
            const auto output_index = static_cast<std::size_t>(std::stoull(fields[0]));
            if (output_index >= kOutputDim) {
                throw std::runtime_error("dense bias output_index out of range");
            }
            fixture.bias[output_index] = std::stod(fields[1]);
            ++loaded_rows;
        }
        if (loaded_rows != kOutputDim) {
            throw std::runtime_error("bias CSV must contain exactly 8 output rows");
        }
    }

    return fixture;
}

double dense_output_at(
    const DenseFixture& fixture,
    std::size_t row,
    std::size_t output_index) {
    double value = fixture.bias[output_index];
    for (std::size_t input_index = 0; input_index < kInputDim; ++input_index) {
        value += fixture.features[input_index][row] *
                 fixture.weights[input_index][output_index];
    }
    return value;
}

DenseResult run_plain_dense(const DenseFixture& fixture) {
    const Timer timer;
    double sum = 0.0;
    for (std::size_t row = 0; row < fixture.rows(); ++row) {
        for (std::size_t output_index = 0; output_index < kOutputDim; ++output_index) {
            sum += dense_output_at(fixture, row, output_index);
        }
    }
    const double elapsed_ms = timer.elapsed_ms();

    DenseResult result;
    result.backend = "plain_cpp";
    result.rows = fixture.rows();
    result.plain_time_ms = elapsed_ms;
    result.result_mean = sum / static_cast<double>(fixture.rows() * kOutputDim);
    result.baseline_mean = result.result_mean;
    result.notes = "dense4x8;plain_cpp;X_times_W_plus_b;single_thread_baseline";
    return result;
}

void append_dense_result_csv(const std::filesystem::path& path, const DenseResult& result) {
    std::filesystem::create_directories(path.parent_path());
    const bool write_header = !file_exists_and_has_content(path);

    std::ofstream output(path, std::ios::app);
    if (!output) {
        throw std::runtime_error("failed to open dense result CSV: " + path.string());
    }

    if (write_header) {
        output
            << "operation,backend,rows,input_dim,output_dim,threads,plain_time_ms,"
            << "setup_time_ms,encode_time_ms,encrypt_time_ms,he_eval_time_ms,"
            << "decrypt_time_ms,decode_time_ms,total_he_time_ms,operation_slowdown,"
            << "end_to_end_slowdown,result_mean,baseline_mean,mean_absolute_error,"
            << "max_absolute_error,ciphertext_count,slots_per_ciphertext,"
            << "used_slots_last_ciphertext,padding_slots_last_ciphertext,slot_utilization,"
            << "requested_ring_dimension,actual_ring_dimension,security_bits,"
            << "multiplicative_depth,scaling_mod_size,first_mod_size,notes\n";
    }

    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << result.operation << ','
           << result.backend << ','
           << result.rows << ','
           << result.input_dim << ','
           << result.output_dim << ','
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
           << result.result_mean << ','
           << result.baseline_mean << ','
           << result.mean_absolute_error << ','
           << result.max_absolute_error << ','
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
           << result.notes << '\n';
}

#ifdef UTILITY_BENCH_WITH_OPENFHE
std::size_t configure_openfhe_threads(std::size_t requested_threads) {
#ifdef _OPENMP
    omp_set_num_threads(static_cast<int>(requested_threads));
    return static_cast<std::size_t>(std::max(1, omp_get_max_threads()));
#else
    return requested_threads;
#endif
}

DenseResult run_openfhe_dense(
    const DenseFixture& fixture,
    std::size_t requested_threads,
    const DenseResult& baseline,
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

    const std::size_t openfhe_threads = configure_openfhe_threads(requested_threads);
    const std::size_t depth = std::max<std::size_t>(args.ckks_depth, 2);

    const Timer setup_timer;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(static_cast<uint32_t>(depth));
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
    const double setup_time_ms = setup_timer.elapsed_ms();

    const std::size_t actual_ring_dimension = cc->GetRingDimension();
    const std::size_t slots =
        args.ckks_batch_size == 0 ? actual_ring_dimension / 2 : args.ckks_batch_size;
    if (slots == 0) {
        throw std::runtime_error("OpenFHE CKKS context reported zero slots");
    }

    const std::size_t chunks = ceil_div(fixture.rows(), slots);
    const std::size_t used_slots_last =
        fixture.rows() - ((chunks - 1) * slots);
    const std::size_t padding_slots_last = slots - used_slots_last;
    const double slot_utilization = divide_or_zero(
        static_cast<double>(fixture.rows()),
        static_cast<double>(chunks * slots));

    DenseResult result;
    result.backend = "openfhe_ckks";
    result.rows = fixture.rows();
    result.threads = openfhe_threads;
    result.plain_time_ms = baseline.plain_time_ms;
    result.baseline_mean = baseline.baseline_mean;
    result.ciphertext_count = chunks * kInputDim;
    result.slots_per_ciphertext = slots;
    result.used_slots_last_ciphertext = used_slots_last;
    result.padding_slots_last_ciphertext = padding_slots_last;
    result.slot_utilization = slot_utilization;
    result.requested_ring_dimension = args.ckks_ring_dim;
    result.actual_ring_dimension = actual_ring_dimension;
    result.security_bits = 128;
    result.multiplicative_depth = depth;
    result.scaling_mod_size = args.ckks_scaling_mod_size;
    result.first_mod_size = args.ckks_first_mod_size;
    result.setup_time_ms = setup_time_ms;

    double decoded_sum = 0.0;
    double abs_error_sum = 0.0;
    double max_abs_error = 0.0;

    std::array<std::vector<double>, kOutputDim> bias_vectors;
    std::array<Plaintext, kOutputDim> bias_plaintexts;
    for (std::size_t output_index = 0; output_index < kOutputDim; ++output_index) {
        bias_vectors[output_index].assign(slots, fixture.bias[output_index]);
    }
    {
        const Timer bias_encode_timer;
        for (std::size_t output_index = 0; output_index < kOutputDim; ++output_index) {
            bias_plaintexts[output_index] = cc->MakeCKKSPackedPlaintext(
                bias_vectors[output_index], 1, 0, nullptr, static_cast<uint32_t>(slots));
        }
        result.encode_time_ms += bias_encode_timer.elapsed_ms();
    }

    for (std::size_t offset = 0; offset < fixture.rows(); offset += slots) {
        const std::size_t used = std::min(slots, fixture.rows() - offset);
        std::array<std::vector<double>, kInputDim> packed_features;
        for (std::size_t input_index = 0; input_index < kInputDim; ++input_index) {
            packed_features[input_index].assign(slots, 0.0);
            for (std::size_t i = 0; i < used; ++i) {
                packed_features[input_index][i] = fixture.features[input_index][offset + i];
            }
        }

        std::array<Plaintext, kInputDim> feature_plaintexts;
        const Timer encode_timer;
        for (std::size_t input_index = 0; input_index < kInputDim; ++input_index) {
            feature_plaintexts[input_index] = cc->MakeCKKSPackedPlaintext(
                packed_features[input_index], 1, 0, nullptr, static_cast<uint32_t>(slots));
        }
        result.encode_time_ms += encode_timer.elapsed_ms();

        std::array<Ciphertext<DCRTPoly>, kInputDim> feature_ciphertexts;
        const Timer encrypt_timer;
        for (std::size_t input_index = 0; input_index < kInputDim; ++input_index) {
            feature_ciphertexts[input_index] =
                cc->Encrypt(keys.publicKey, feature_plaintexts[input_index]);
        }
        result.encrypt_time_ms += encrypt_timer.elapsed_ms();

        for (std::size_t output_index = 0; output_index < kOutputDim; ++output_index) {
            const Timer eval_timer;
            auto output_ciphertext = cc->EvalMult(
                feature_ciphertexts[0],
                fixture.weights[0][output_index]);
            for (std::size_t input_index = 1; input_index < kInputDim; ++input_index) {
                output_ciphertext = cc->EvalAdd(
                    output_ciphertext,
                    cc->EvalMult(
                        feature_ciphertexts[input_index],
                        fixture.weights[input_index][output_index]));
            }
            output_ciphertext = cc->EvalAdd(output_ciphertext, bias_plaintexts[output_index]);
            result.he_eval_time_ms += eval_timer.elapsed_ms();

            Plaintext output_plaintext;
            const Timer decrypt_timer;
            cc->Decrypt(keys.secretKey, output_ciphertext, &output_plaintext);
            result.decrypt_time_ms += decrypt_timer.elapsed_ms();

            const Timer decode_timer;
            output_plaintext->SetLength(used);
            const auto decoded_values = output_plaintext->GetRealPackedValue();
            if (decoded_values.size() < used) {
                throw std::runtime_error("dense layer decoded fewer slots than expected");
            }
            for (std::size_t i = 0; i < used; ++i) {
                const double decoded = decoded_values[i];
                const double expected = dense_output_at(fixture, offset + i, output_index);
                const double abs_error = std::abs(decoded - expected);
                decoded_sum += decoded;
                abs_error_sum += abs_error;
                max_abs_error = std::max(max_abs_error, abs_error);
            }
            result.decode_time_ms += decode_timer.elapsed_ms();
        }
    }

    const double output_count = static_cast<double>(fixture.rows() * kOutputDim);
    result.result_mean = decoded_sum / output_count;
    result.mean_absolute_error = abs_error_sum / output_count;
    result.max_absolute_error = max_abs_error;
    result.total_he_time_ms =
        result.encode_time_ms + result.encrypt_time_ms + result.he_eval_time_ms +
        result.decrypt_time_ms + result.decode_time_ms;
    result.operation_slowdown = divide_or_zero(result.he_eval_time_ms, baseline.plain_time_ms);
    result.end_to_end_slowdown = divide_or_zero(result.total_he_time_ms, baseline.plain_time_ms);
    result.notes =
#ifdef _OPENMP
        "dense4x8;schema_a_vector_decode;encrypted_features;plaintext_weights;plaintext_bias;no_evalsum;no_bootstrap;omp_set_num_threads";
#else
        "dense4x8;schema_a_vector_decode;encrypted_features;plaintext_weights;plaintext_bias;no_evalsum;no_bootstrap;openmp_not_seen_by_runner";
#endif
    cc->ClearStaticMapsAndVectors();
    return result;
}
#endif

int run(const CliArgs& args) {
    std::cout << "Loading dense fixture: " << args.fixture_dir << '\n';
    const Timer load_timer;
    const DenseFixture fixture = load_dense_fixture(args.fixture_dir, args.max_rows);
    const double load_ms = load_timer.elapsed_ms();
    std::cout << "Loaded " << fixture.rows() << " rows in " << load_ms << " ms\n";
    if (args.max_rows != 0) {
        std::cout << "Applied --max-rows " << args.max_rows << '\n';
    }

    const DenseResult baseline = run_plain_dense(fixture);
    if (wants_plain(args.backend)) {
        append_dense_result_csv(args.results_path, baseline);
        std::cout << "Ran plain_cpp:dense4x8_vector"
                  << " in " << baseline.plain_time_ms << " ms"
                  << " mean=" << baseline.result_mean << '\n';
    }

    if (wants_openfhe(args.backend)) {
#ifdef UTILITY_BENCH_WITH_OPENFHE
        for (const std::size_t thread_count : args.thread_counts) {
            const DenseResult result = run_openfhe_dense(fixture, thread_count, baseline, args);
            append_dense_result_csv(args.results_path, result);
            std::cout << "Ran " << result.backend << ":dense4x8_vector"
                      << " threads=" << result.threads
                      << " he_total=" << result.total_he_time_ms << " ms"
                      << " mae=" << result.mean_absolute_error
                      << " max_error=" << result.max_absolute_error << '\n';
        }
#else
        throw std::runtime_error(
            "OpenFHE CKKS backend requested but dense_layer_bench was built without "
            "UTILITY_BENCH_WITH_OPENFHE=ON");
#endif
    }

    std::cout << "Wrote results: " << args.results_path << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
