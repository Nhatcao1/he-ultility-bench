#include "csv_loader.h"
#include "plain_ops.h"
#include "timer.h"

#ifdef UTILITY_BENCH_WITH_OPENFHE
#include "openfhe.h"

#ifdef _OPENMP
#include <omp.h>
#endif
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

enum class BackendMode {
    PlainCpp,
    OpenFheCkks,
    All,
};

enum class PolyVariant {
    PowerTreeBootstrap,
    BlockBootstrap,
    All,
};

struct CliArgs {
    std::filesystem::path data_path = "data/generated/medium_100k/transactions.csv";
    std::filesystem::path results_path =
        "results/optimized_poly/poly_score_opt.csv";
    BackendMode backend = BackendMode::All;
    PolyVariant variant = PolyVariant::All;
    std::vector<std::size_t> thread_counts = {1};
    std::size_t repeat_count = 3;
    std::size_t max_rows = 0;
    std::size_t ckks_ring_dim = 0;
    std::size_t ckks_batch_size = 0;
    std::size_t ckks_depth = 9;
    std::size_t ckks_scaling_mod_size = 59;
    std::size_t ckks_first_mod_size = 60;
    std::size_t bootstrap_levels_after = 10;
    std::vector<uint32_t> bootstrap_level_budget = {4, 4};
};

struct PolyResult {
    std::string operation;
    std::string backend;
    std::size_t rows = 0;
    std::size_t threads = 1;
    std::size_t polynomial_degree = 9;
    bool bootstrap_enabled = true;
    double plain_time_ms = 0.0;
    double setup_time_ms = 0.0;
    double bootstrap_setup_time_ms = 0.0;
    double bootstrap_keygen_time_ms = 0.0;
    double encode_time_ms = 0.0;
    double encrypt_time_ms = 0.0;
    double he_eval_time_ms = 0.0;
    double bootstrap_time_ms = 0.0;
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
    std::size_t security_bits = 128;
    std::size_t multiplicative_depth = 0;
    std::size_t scaling_mod_size = 0;
    std::size_t first_mod_size = 0;
    std::size_t bootstrap_levels_after = 0;
    std::string notes;
};

void print_usage(const char* program) {
    std::cerr
        << "Usage: " << program << " [--data transactions.csv] "
        << "[--variant power_tree_bootstrap|block_bootstrap|all] "
        << "[--backend plain_cpp|openfhe_ckks|all] [--threads 1] "
        << "[--repeat 3] [--max-rows 1000] [--ckks-ring-dim 0] "
        << "[--ckks-batch-size 0] [--ckks-depth 9] [--ckks-scale-bits 59] "
        << "[--ckks-first-mod-bits 60] [--bootstrap-levels-after 10] "
        << "[--bootstrap-level-budget 4 4] "
        << "[--results results/optimized_poly/poly_score_opt.csv]\n";
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

PolyVariant parse_variant(const std::string& value) {
    if (value == "power_tree_bootstrap") {
        return PolyVariant::PowerTreeBootstrap;
    }
    if (value == "block_bootstrap") {
        return PolyVariant::BlockBootstrap;
    }
    if (value == "all") {
        return PolyVariant::All;
    }
    throw std::runtime_error("unknown variant: " + value);
}

std::string variant_name(PolyVariant variant) {
    switch (variant) {
        case PolyVariant::PowerTreeBootstrap:
            return "poly_score_degree9_power_tree_bootstrap";
        case PolyVariant::BlockBootstrap:
            return "poly_score_degree9_block_bootstrap";
        case PolyVariant::All:
            return "all";
    }
    throw std::runtime_error("unknown polynomial variant enum");
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

        if (flag == "--data") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--data requires a path");
            }
            args.data_path = argv[++i];
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
        } else if (flag == "--variant") {
            if (i + 1 >= argc) {
                throw std::runtime_error("--variant requires a value");
            }
            args.variant = parse_variant(argv[++i]);
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
        } else if (flag == "--repeat") {
            args.repeat_count = parse_size_arg(i, argc, argv, flag);
            if (args.repeat_count == 0) {
                throw std::runtime_error("--repeat must be positive");
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
            if (args.ckks_scaling_mod_size == 0) {
                throw std::runtime_error("--ckks-scale-bits must be positive");
            }
        } else if (flag == "--ckks-first-mod-bits") {
            args.ckks_first_mod_size = parse_size_arg(i, argc, argv, flag);
            if (args.ckks_first_mod_size == 0) {
                throw std::runtime_error("--ckks-first-mod-bits must be positive");
            }
        } else if (flag == "--bootstrap-levels-after") {
            args.bootstrap_levels_after = parse_size_arg(i, argc, argv, flag);
        } else if (flag == "--bootstrap-level-budget") {
            args.bootstrap_level_budget.clear();
            while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                args.bootstrap_level_budget.push_back(static_cast<uint32_t>(
                    std::stoul(argv[++i])));
            }
            if (args.bootstrap_level_budget.size() != 2) {
                throw std::runtime_error("--bootstrap-level-budget requires exactly two values");
            }
        } else {
            throw std::runtime_error("unknown argument: " + flag);
        }
    }

    return args;
}

std::vector<PolyVariant> expand_variants(PolyVariant requested) {
    if (requested == PolyVariant::All) {
        return {PolyVariant::PowerTreeBootstrap, PolyVariant::BlockBootstrap};
    }
    return {requested};
}

double divide_or_zero(double numerator, double denominator) {
    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

std::size_t ceil_div(std::size_t numerator, std::size_t denominator) {
    return (numerator + denominator - 1) / denominator;
}

double coefficient_for_odd_power(std::size_t power) {
    switch (power) {
        case 1:
            return 0.197;
        case 3:
            return -0.004;
        case 5:
            return 0.00008;
        case 7:
            return -0.000001;
        case 9:
            return 0.00000001;
        default:
            throw std::runtime_error("unsupported polynomial power");
    }
}

double normalized_linear_score(const Transactions& data, std::size_t row) {
    const double amount = data.amount[row] / 10000.0;
    const double x1 = data.x1[row] / 10000.0;
    const double x2 = data.x2[row] / 10000.0;
    const double x3 = data.x3[row] / 10000.0;
    return 0.50 * amount + 0.30 * x1 + 0.20 * x2 - 0.10 * x3 - 0.25;
}

double polynomial_activation_degree9(double z) {
    const double z2 = z * z;
    double odd_power = z;
    double result = 0.5 + coefficient_for_odd_power(1) * odd_power;
    for (std::size_t power = 3; power <= 9; power += 2) {
        odd_power *= z2;
        result += coefficient_for_odd_power(power) * odd_power;
    }
    return result;
}

PolyResult run_plain_poly(
    const Transactions& data,
    std::size_t requested_threads,
    PolyVariant variant) {
    const std::size_t threads = effective_thread_count(requested_threads, data.size());
    const Timer timer;
    const double value = parallel_sum(data.size(), threads, [&](std::size_t row) {
        return polynomial_activation_degree9(normalized_linear_score(data, row));
    });
    const double plain_time_ms = timer.elapsed_ms();

    PolyResult result;
    result.operation = variant_name(variant);
    result.backend = "plain_cpp";
    result.rows = data.size();
    result.threads = threads;
    result.plain_time_ms = plain_time_ms;
    result.result_value = value;
    result.baseline_value = value;
    result.notes = "compute_only_no_io;degree9_plain_reference";
    return result;
}

std::string repeat_note(std::size_t repeat_index, std::size_t repeat_count) {
    return "repeat_index=" + std::to_string(repeat_index) +
           ";repeat_count=" + std::to_string(repeat_count);
}

double average_field(const std::vector<PolyResult>& results, double PolyResult::*field) {
    double sum = 0.0;
    for (const auto& result : results) {
        sum += result.*field;
    }
    return divide_or_zero(sum, static_cast<double>(results.size()));
}

PolyResult summarize_poly_results(
    const std::vector<PolyResult>& results,
    const std::string& operation,
    const std::string& backend) {
    if (results.empty()) {
        throw std::runtime_error("cannot summarize empty polynomial result list");
    }

    PolyResult summary = results.front();
    summary.operation = operation + "_summary_avg";
    summary.backend = backend + "_summary";
    summary.plain_time_ms = average_field(results, &PolyResult::plain_time_ms);
    summary.setup_time_ms = average_field(results, &PolyResult::setup_time_ms);
    summary.bootstrap_setup_time_ms =
        average_field(results, &PolyResult::bootstrap_setup_time_ms);
    summary.bootstrap_keygen_time_ms =
        average_field(results, &PolyResult::bootstrap_keygen_time_ms);
    summary.encode_time_ms = average_field(results, &PolyResult::encode_time_ms);
    summary.encrypt_time_ms = average_field(results, &PolyResult::encrypt_time_ms);
    summary.he_eval_time_ms = average_field(results, &PolyResult::he_eval_time_ms);
    summary.bootstrap_time_ms = average_field(results, &PolyResult::bootstrap_time_ms);
    summary.decrypt_time_ms = average_field(results, &PolyResult::decrypt_time_ms);
    summary.decode_time_ms = average_field(results, &PolyResult::decode_time_ms);
    summary.total_he_time_ms = average_field(results, &PolyResult::total_he_time_ms);
    summary.operation_slowdown = average_field(results, &PolyResult::operation_slowdown);
    summary.end_to_end_slowdown = average_field(results, &PolyResult::end_to_end_slowdown);
    summary.result_value = average_field(results, &PolyResult::result_value);
    summary.baseline_value = average_field(results, &PolyResult::baseline_value);
    summary.absolute_error = average_field(results, &PolyResult::absolute_error);
    summary.relative_error = average_field(results, &PolyResult::relative_error);
    summary.slot_utilization = average_field(results, &PolyResult::slot_utilization);
    summary.notes = "repeat_summary;repeat_count=" + std::to_string(results.size()) +
        ";real_calculation_ms=he_eval_time_ms_plus_bootstrap_time_ms" +
        ";overall_lifecycle_ms=total_he_time_ms;setup_keygen_excluded_from_total";
    return summary;
}

bool file_exists_and_has_content(const std::filesystem::path& path) {
    return std::filesystem::exists(path) && std::filesystem::file_size(path) > 0;
}

void append_poly_result_csv(const std::filesystem::path& path, const PolyResult& result) {
    std::filesystem::create_directories(path.parent_path());
    const bool write_header = !file_exists_and_has_content(path);
    std::ofstream output(path, std::ios::app);
    if (!output) {
        throw std::runtime_error("failed to open polynomial result CSV: " + path.string());
    }

    if (write_header) {
        output
            << "operation,backend,rows,threads,polynomial_degree,bootstrap_enabled,"
            << "plain_time_ms,setup_time_ms,bootstrap_setup_time_ms,bootstrap_keygen_time_ms,"
            << "encode_time_ms,encrypt_time_ms,he_eval_time_ms,bootstrap_time_ms,"
            << "decrypt_time_ms,decode_time_ms,total_he_time_ms,operation_slowdown,"
            << "end_to_end_slowdown,result_value,baseline_value,absolute_error,relative_error,"
            << "ciphertext_count,slots_per_ciphertext,used_slots_last_ciphertext,"
            << "padding_slots_last_ciphertext,slot_utilization,requested_ring_dimension,"
            << "actual_ring_dimension,security_bits,multiplicative_depth,scaling_mod_size,"
            << "first_mod_size,bootstrap_levels_after,notes\n";
    }

    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << result.operation << ','
           << result.backend << ','
           << result.rows << ','
           << result.threads << ','
           << result.polynomial_degree << ','
           << (result.bootstrap_enabled ? 1 : 0) << ','
           << result.plain_time_ms << ','
           << result.setup_time_ms << ','
           << result.bootstrap_setup_time_ms << ','
           << result.bootstrap_keygen_time_ms << ','
           << result.encode_time_ms << ','
           << result.encrypt_time_ms << ','
           << result.he_eval_time_ms << ','
           << result.bootstrap_time_ms << ','
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
           << result.bootstrap_levels_after << ','
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

template <typename CryptoContextT, typename CiphertextT, typename PlaintextT>
CiphertextT eval_power_tree_degree9(
    const CryptoContextT& cc,
    const CiphertextT& z,
    PlaintextT constant_half) {
    const double c1 = coefficient_for_odd_power(1);
    const double c3 = coefficient_for_odd_power(3);
    const double c5 = coefficient_for_odd_power(5);
    const double c7 = coefficient_for_odd_power(7);
    const double c9 = coefficient_for_odd_power(9);

    auto z2 = cc->EvalMult(z, z);
    auto z3 = cc->EvalMult(z2, z);
    auto z4 = cc->EvalMult(z2, z2);
    auto z5 = cc->EvalMult(z4, z);
    auto z6 = cc->EvalMult(z4, z2);
    auto z7 = cc->EvalMult(z6, z);
    auto z8 = cc->EvalMult(z4, z4);
    auto z9 = cc->EvalMult(z8, z);

    auto result = cc->EvalAdd(cc->EvalMult(z, c1), constant_half);
    result = cc->EvalAdd(result, cc->EvalMult(z3, c3));
    result = cc->EvalAdd(result, cc->EvalMult(z5, c5));
    result = cc->EvalAdd(result, cc->EvalMult(z7, c7));
    result = cc->EvalAdd(result, cc->EvalMult(z9, c9));
    return result;
}

template <typename CryptoContextT, typename CiphertextT, typename PlaintextT>
CiphertextT eval_block_degree9(
    const CryptoContextT& cc,
    const CiphertextT& z,
    PlaintextT constant_half,
    PlaintextT constant_c5) {
    const double c1 = coefficient_for_odd_power(1);
    const double c3 = coefficient_for_odd_power(3);
    const double c7 = coefficient_for_odd_power(7);
    const double c9 = coefficient_for_odd_power(9);

    auto z2 = cc->EvalMult(z, z);
    auto z3 = cc->EvalMult(z2, z);
    auto z4 = cc->EvalMult(z2, z2);
    auto z5 = cc->EvalMult(z4, z);

    auto low_block = cc->EvalAdd(cc->EvalMult(z, c1), constant_half);
    low_block = cc->EvalAdd(low_block, cc->EvalMult(z3, c3));

    auto high_block = cc->EvalAdd(cc->EvalMult(z2, c7), constant_c5);
    high_block = cc->EvalAdd(high_block, cc->EvalMult(z4, c9));

    auto high_terms = cc->EvalMult(z5, high_block);
    return cc->EvalAdd(low_block, high_terms);
}

PolyResult run_openfhe_poly(
    const Transactions& data,
    PolyVariant variant,
    std::size_t requested_threads,
    const PolyResult& baseline,
    const CliArgs& args) {
    using lbcrypto::ADVANCEDSHE;
    using lbcrypto::CCParams;
    using lbcrypto::Ciphertext;
    using lbcrypto::CryptoContext;
    using lbcrypto::CryptoContextCKKSRNS;
    using lbcrypto::DCRTPoly;
    using lbcrypto::FHE;
    using lbcrypto::FHECKKSRNS;
    using lbcrypto::FIXEDAUTO;
    using lbcrypto::GenCryptoContext;
    using lbcrypto::HEStd_128_classic;
    using lbcrypto::KEYSWITCH;
    using lbcrypto::LEVELEDSHE;
    using lbcrypto::PKE;
    using lbcrypto::Plaintext;
    using lbcrypto::UNIFORM_TERNARY;

    const std::size_t openfhe_threads = configure_openfhe_threads(requested_threads);
    const auto secret_key_dist = UNIFORM_TERNARY;
    const std::vector<uint32_t> level_budget = args.bootstrap_level_budget;
    const std::size_t configured_depth = std::max<std::size_t>(
        args.ckks_depth,
        args.bootstrap_levels_after +
            FHECKKSRNS::GetBootstrapDepth(level_budget, secret_key_dist));

    const Timer setup_timer;
    CCParams<CryptoContextCKKSRNS> parameters;
    parameters.SetMultiplicativeDepth(static_cast<uint32_t>(configured_depth));
    parameters.SetScalingModSize(static_cast<uint32_t>(args.ckks_scaling_mod_size));
    parameters.SetFirstModSize(static_cast<uint32_t>(args.ckks_first_mod_size));
    parameters.SetSecurityLevel(HEStd_128_classic);
    parameters.SetSecretKeyDist(secret_key_dist);
    parameters.SetScalingTechnique(FIXEDAUTO);
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
    cc->Enable(FHE);

    const std::size_t actual_ring_dimension = cc->GetRingDimension();
    const std::size_t slots_per_ciphertext =
        args.ckks_batch_size == 0 ? actual_ring_dimension / 2 : args.ckks_batch_size;
    if (slots_per_ciphertext == 0) {
        throw std::runtime_error("OpenFHE CKKS context reported zero slots");
    }

    const Timer bootstrap_setup_timer;
    cc->EvalBootstrapSetup(
        level_budget,
        {0, 0},
        static_cast<uint32_t>(slots_per_ciphertext));
    const double bootstrap_setup_time_ms = bootstrap_setup_timer.elapsed_ms();

    const auto keys = cc->KeyGen();
    cc->EvalMultKeyGen(keys.secretKey);
    cc->EvalSumKeyGen(keys.secretKey);

    const Timer bootstrap_keygen_timer;
    cc->EvalBootstrapKeyGen(
        keys.secretKey,
        static_cast<uint32_t>(slots_per_ciphertext));
    const double bootstrap_keygen_time_ms = bootstrap_keygen_timer.elapsed_ms();
    const double setup_time_ms = setup_timer.elapsed_ms();

    const std::size_t ciphertext_count = ceil_div(data.size(), slots_per_ciphertext);
    const std::size_t used_slots_last_ciphertext =
        data.size() - ((ciphertext_count - 1) * slots_per_ciphertext);
    const std::size_t padding_slots_last_ciphertext =
        slots_per_ciphertext - used_slots_last_ciphertext;
    const double slot_utilization = divide_or_zero(
        static_cast<double>(data.size()),
        static_cast<double>(ciphertext_count * slots_per_ciphertext));

    double encode_time_ms = 0.0;
    double encrypt_time_ms = 0.0;
    double he_eval_time_ms = 0.0;
    double bootstrap_time_ms = 0.0;
    bool has_total = false;
    Ciphertext<DCRTPoly> total_ciphertext;

    for (std::size_t offset = 0; offset < data.size(); offset += slots_per_ciphertext) {
        const std::size_t used_slots = std::min(slots_per_ciphertext, data.size() - offset);
        std::vector<double> amount(slots_per_ciphertext, 0.0);
        std::vector<double> x1(slots_per_ciphertext, 0.0);
        std::vector<double> x2(slots_per_ciphertext, 0.0);
        std::vector<double> x3(slots_per_ciphertext, 0.0);
        std::vector<double> linear_bias(slots_per_ciphertext, -0.25);
        std::vector<double> half(slots_per_ciphertext, 0.5);
        std::vector<double> coeff5(
            slots_per_ciphertext,
            coefficient_for_odd_power(5));

        for (std::size_t i = 0; i < used_slots; ++i) {
            amount[i] = data.amount[offset + i] / 10000.0;
            x1[i] = data.x1[offset + i] / 10000.0;
            x2[i] = data.x2[offset + i] / 10000.0;
            x3[i] = data.x3[offset + i] / 10000.0;
        }

        const Timer encode_timer;
        Plaintext amount_plain = cc->MakeCKKSPackedPlaintext(amount);
        Plaintext x1_plain = cc->MakeCKKSPackedPlaintext(x1);
        Plaintext x2_plain = cc->MakeCKKSPackedPlaintext(x2);
        Plaintext x3_plain = cc->MakeCKKSPackedPlaintext(x3);
        Plaintext linear_bias_plain = cc->MakeCKKSPackedPlaintext(linear_bias);
        Plaintext half_plain = cc->MakeCKKSPackedPlaintext(half);
        Plaintext coeff5_plain = cc->MakeCKKSPackedPlaintext(coeff5);
        encode_time_ms += encode_timer.elapsed_ms();

        const Timer encrypt_timer;
        auto amount_cipher = cc->Encrypt(keys.publicKey, amount_plain);
        auto x1_cipher = cc->Encrypt(keys.publicKey, x1_plain);
        auto x2_cipher = cc->Encrypt(keys.publicKey, x2_plain);
        auto x3_cipher = cc->Encrypt(keys.publicKey, x3_plain);
        encrypt_time_ms += encrypt_timer.elapsed_ms();

        const Timer linear_eval_timer;
        auto z = cc->EvalMult(amount_cipher, 0.50);
        z = cc->EvalAdd(z, cc->EvalMult(x1_cipher, 0.30));
        z = cc->EvalAdd(z, cc->EvalMult(x2_cipher, 0.20));
        z = cc->EvalAdd(z, cc->EvalMult(x3_cipher, -0.10));
        z = cc->EvalAdd(z, linear_bias_plain);
        he_eval_time_ms += linear_eval_timer.elapsed_ms();

        const Timer bootstrap_timer;
        z = cc->EvalBootstrap(z);
        bootstrap_time_ms += bootstrap_timer.elapsed_ms();

        const Timer poly_eval_timer;
        Ciphertext<DCRTPoly> poly;
        if (variant == PolyVariant::PowerTreeBootstrap) {
            poly = eval_power_tree_degree9(cc, z, half_plain);
        } else {
            poly = eval_block_degree9(cc, z, half_plain, coeff5_plain);
        }
        auto chunk_sum = cc->EvalSum(poly, static_cast<uint32_t>(used_slots));
        if (has_total) {
            total_ciphertext = cc->EvalAdd(total_ciphertext, chunk_sum);
        } else {
            total_ciphertext = chunk_sum;
            has_total = true;
        }
        he_eval_time_ms += poly_eval_timer.elapsed_ms();
    }

    Plaintext decrypted;
    const Timer decrypt_timer;
    cc->Decrypt(keys.secretKey, total_ciphertext, &decrypted);
    const double decrypt_time_ms = decrypt_timer.elapsed_ms();

    const Timer decode_timer;
    decrypted->SetLength(1);
    const auto decoded_values = decrypted->GetRealPackedValue();
    if (decoded_values.empty()) {
        throw std::runtime_error("OpenFHE CKKS polynomial decrypt produced no values");
    }
    const double result_value = decoded_values[0];
    const double decode_time_ms = decode_timer.elapsed_ms();

    const double total_he_time_ms =
        encode_time_ms + encrypt_time_ms + he_eval_time_ms + bootstrap_time_ms +
        decrypt_time_ms + decode_time_ms;
    const double absolute_error = std::abs(result_value - baseline.baseline_value);
    const double relative_error = divide_or_zero(absolute_error, std::abs(baseline.baseline_value));

    PolyResult result;
    result.operation = variant_name(variant);
    result.backend = "openfhe_ckks_opt";
    result.rows = data.size();
    result.threads = openfhe_threads;
    result.plain_time_ms = baseline.plain_time_ms;
    result.setup_time_ms = setup_time_ms;
    result.bootstrap_setup_time_ms = bootstrap_setup_time_ms;
    result.bootstrap_keygen_time_ms = bootstrap_keygen_time_ms;
    result.encode_time_ms = encode_time_ms;
    result.encrypt_time_ms = encrypt_time_ms;
    result.he_eval_time_ms = he_eval_time_ms;
    result.bootstrap_time_ms = bootstrap_time_ms;
    result.decrypt_time_ms = decrypt_time_ms;
    result.decode_time_ms = decode_time_ms;
    result.total_he_time_ms = total_he_time_ms;
    result.operation_slowdown =
        divide_or_zero(he_eval_time_ms + bootstrap_time_ms, baseline.plain_time_ms);
    result.end_to_end_slowdown = divide_or_zero(total_he_time_ms, baseline.plain_time_ms);
    result.result_value = result_value;
    result.baseline_value = baseline.baseline_value;
    result.absolute_error = absolute_error;
    result.relative_error = relative_error;
    result.ciphertext_count = ciphertext_count;
    result.slots_per_ciphertext = slots_per_ciphertext;
    result.used_slots_last_ciphertext = used_slots_last_ciphertext;
    result.padding_slots_last_ciphertext = padding_slots_last_ciphertext;
    result.slot_utilization = slot_utilization;
    result.requested_ring_dimension = args.ckks_ring_dim;
    result.actual_ring_dimension = actual_ring_dimension;
    result.security_bits = 128;
    result.multiplicative_depth = configured_depth;
    result.scaling_mod_size = args.ckks_scaling_mod_size;
    result.first_mod_size = args.ckks_first_mod_size;
    result.bootstrap_levels_after = args.bootstrap_levels_after;
    result.notes =
#ifdef _OPENMP
        "src_optimized;poly_score;degree9;bootstrap_after_linear_score;setup_recorded_separately;omp_set_num_threads;";
#else
        "src_optimized;poly_score;degree9;bootstrap_after_linear_score;setup_recorded_separately;openmp_not_seen_by_runner;";
#endif
    result.notes += variant == PolyVariant::PowerTreeBootstrap
        ? "power_tree_powers"
        : "block_paterson_stockmeyer_style";

    cc->ClearStaticMapsAndVectors();
    return result;
}

#endif

int run(const CliArgs& args) {
    std::cout << "Loading transactions: " << args.data_path << '\n';
    const Timer load_timer;
    const Transactions data = load_transactions_csv(args.data_path.string(), args.max_rows);
    const double load_ms = load_timer.elapsed_ms();
    std::cout << "Loaded " << data.size() << " rows in " << load_ms << " ms\n";
    if (args.max_rows != 0) {
        std::cout << "Applied --max-rows " << args.max_rows << '\n';
    }

    const std::vector<PolyVariant> variants = expand_variants(args.variant);
    for (const PolyVariant variant : variants) {
        std::vector<PolyResult> plain_results;
        plain_results.reserve(args.repeat_count);
        for (std::size_t repeat_index = 1; repeat_index <= args.repeat_count; ++repeat_index) {
            auto plain = run_plain_poly(data, 1, variant);
            plain.notes += ";" + repeat_note(repeat_index, args.repeat_count);
            plain_results.push_back(plain);
        }
        const PolyResult baseline =
            summarize_poly_results(plain_results, variant_name(variant), "plain_cpp");

        if (wants_plain(args.backend)) {
            for (const auto& plain : plain_results) {
                append_poly_result_csv(args.results_path, plain);
                std::cout << "Ran " << plain.backend << ':' << plain.operation
                          << " threads=" << plain.threads
                          << " in " << plain.plain_time_ms << " ms"
                          << " value=" << plain.result_value << '\n';
            }
            append_poly_result_csv(args.results_path, baseline);
            std::cout << "Summary " << baseline.backend << ':' << baseline.operation
                      << " repeats=" << args.repeat_count
                      << " avg_plain=" << baseline.plain_time_ms << " ms\n";
        }

        if (wants_openfhe(args.backend)) {
#ifdef UTILITY_BENCH_WITH_OPENFHE
            for (const std::size_t thread_count : args.thread_counts) {
                std::vector<PolyResult> he_results;
                he_results.reserve(args.repeat_count);
                for (std::size_t repeat_index = 1;
                     repeat_index <= args.repeat_count;
                     ++repeat_index) {
                    auto result = run_openfhe_poly(
                        data,
                        variant,
                        thread_count,
                        baseline,
                        args);
                    result.notes += ";" + repeat_note(repeat_index, args.repeat_count);
                    he_results.push_back(result);
                    append_poly_result_csv(args.results_path, he_results.back());
                    std::cout << "Ran " << result.backend << ':' << result.operation
                              << " repeat=" << repeat_index << '/' << args.repeat_count
                              << " threads=" << result.threads
                              << " he_total=" << result.total_he_time_ms << " ms"
                              << " he_calc=" << (result.he_eval_time_ms + result.bootstrap_time_ms)
                              << " ms bootstrap=" << result.bootstrap_time_ms
                              << " ms abs_error=" << result.absolute_error
                              << " rel_error=" << result.relative_error << '\n';
                }
                const PolyResult he_summary = summarize_poly_results(
                    he_results,
                    he_results.front().operation,
                    he_results.front().backend);
                append_poly_result_csv(args.results_path, he_summary);
                std::cout << "Summary " << he_summary.backend << ':'
                          << he_summary.operation
                          << " threads=" << he_summary.threads
                          << " repeats=" << args.repeat_count
                          << " avg_total=" << he_summary.total_he_time_ms
                          << " ms avg_calc="
                          << (he_summary.he_eval_time_ms + he_summary.bootstrap_time_ms)
                          << " ms avg_bootstrap=" << he_summary.bootstrap_time_ms
                          << " ms avg_rel_error=" << he_summary.relative_error << '\n';
            }
#else
            throw std::runtime_error(
                "OpenFHE CKKS backend requested but poly_score_opt_bench was built without "
                "UTILITY_BENCH_WITH_OPENFHE=ON");
#endif
        }
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
