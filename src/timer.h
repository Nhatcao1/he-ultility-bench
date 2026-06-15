#pragma once

#include <chrono>

// Small timing helper used by every benchmark stage.
// It reports wall-clock milliseconds because the first goal is practical
// end-to-end comparison against OpenFHE, not CPU-cycle microbenchmarking.
class Timer {
public:
    using Clock = std::chrono::steady_clock;

    Timer() : start_(Clock::now()) {}

    double elapsed_ms() const {
        const auto end = Clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

private:
    Clock::time_point start_;
};

