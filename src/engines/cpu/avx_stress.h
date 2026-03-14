#pragma once

#include <cstdint>

namespace occt { namespace cpu {

// Runtime ISA detection
bool has_sse42();
bool has_avx2();
bool has_avx512f();
bool has_fma();

// Stress functions - run FMA loop for specified duration (nanoseconds)
// Returns: number of FMA operations executed
uint64_t stress_sse(uint64_t duration_ns);
uint64_t stress_avx2(uint64_t duration_ns);
uint64_t stress_avx512(uint64_t duration_ns);

// Verification result from stress_and_verify functions
struct VerifyResult {
    uint64_t ops = 0;          // Number of operations executed
    bool passed = true;        // All results matched expected
    int lane_errors = 0;       // Number of SIMD lanes with errors
    double expected[8] = {};   // Expected values per lane (up to 8 for AVX-512)
    double actual[8] = {};     // Actual values per lane
    int lane_count = 0;        // How many lanes were checked
};

// Pure AVX 256-bit stress without FMA (uses mul+add separately)
uint64_t stress_avx_nofma(uint64_t duration_ns);
VerifyResult stress_and_verify_avx_nofma(uint64_t duration_ns);

// Stress + verify functions - run deterministic FMA chain and verify results
// These run a fixed number of iterations, verify, then repeat until duration_ns
VerifyResult stress_and_verify_sse(uint64_t duration_ns);
VerifyResult stress_and_verify_avx2(uint64_t duration_ns);
VerifyResult stress_and_verify_avx512(uint64_t duration_ns);
VerifyResult stress_and_verify_neon(uint64_t duration_ns);

}} // namespace occt::cpu
