#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

inline std::size_t bytes_to_kb_rounded(std::size_t bytes) {
    return (bytes + 1023) / 1024;
}

inline double bytes_to_kb(std::size_t bytes) {
    return static_cast<double>(bytes) / 1024.0;
}

inline std::size_t current_peak_rss_kb() {
#if defined(__unix__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0;
    }
#if defined(__APPLE__)
    return static_cast<std::size_t>(usage.ru_maxrss) / 1024;
#else
    return static_cast<std::size_t>(usage.ru_maxrss);
#endif
#else
    return 0;
#endif
}

inline std::size_t ceil_div_size(std::size_t numerator, std::size_t denominator) {
    return denominator == 0 ? 0 : (numerator + denominator - 1) / denominator;
}

inline std::size_t estimate_ckks_ciphertext_payload_bytes(
    std::size_t ciphertext_count,
    std::size_t ring_dimension,
    std::size_t multiplicative_depth,
    std::size_t scaling_mod_size,
    std::size_t first_mod_size) {
    if (ciphertext_count == 0 || ring_dimension == 0) {
        return 0;
    }

    const std::size_t active_depth = std::max<std::size_t>(multiplicative_depth, 1);
    const std::size_t total_modulus_bits =
        first_mod_size + (active_depth * scaling_mod_size);
    const std::size_t towers = std::max<std::size_t>(ceil_div_size(total_modulus_bits, 60), 1);

    // CKKS ciphertexts normally carry two DCRT polynomial elements. This is a
    // compact estimate, not serialization, so timing stays clean.
    constexpr std::size_t kCiphertextElements = 2;
    constexpr std::size_t kNativeLimbBytes = sizeof(std::uint64_t);
    return ciphertext_count * kCiphertextElements * ring_dimension * towers * kNativeLimbBytes;
}

inline std::size_t estimate_lwe_ciphertext_payload_bytes(
    std::size_t ciphertext_count,
    std::size_t lwe_dimension,
    std::size_t modulus_bits) {
    if (ciphertext_count == 0 || lwe_dimension == 0) {
        return 0;
    }

    const std::size_t limbs = std::max<std::size_t>(ceil_div_size(modulus_bits, 64), 1);
    constexpr std::size_t kNativeLimbBytes = sizeof(std::uint64_t);
    return ciphertext_count * (lwe_dimension + 1) * limbs * kNativeLimbBytes;
}
