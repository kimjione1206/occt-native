#include "prime.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#ifdef _MSC_VER
    #define DO_NOT_OPTIMIZE(x) { volatile auto _v = (x); (void)_v; }
#else
    #define DO_NOT_OPTIMIZE(x) asm volatile("" : : "r,m"(x) : "memory")
#endif

namespace occt { namespace cpu {

// Modular exponentiation: (base^exp) mod mod
static uint64_t mod_pow(uint64_t base, uint64_t exp, uint64_t mod) {
    uint64_t result = 1;
    base %= mod;

    while (exp > 0) {
        if (exp & 1) {
            // Use __uint128_t for overflow-safe multiplication on GCC/Clang
#if defined(__GNUC__) || defined(__clang__)
            result = static_cast<uint64_t>(
                (static_cast<__uint128_t>(result) * base) % mod);
#else
            // MSVC fallback: use _umul128
            uint64_t high;
            uint64_t low = _umul128(result, base, &high);
            uint64_t rem;
            _udiv128(high, low, mod, &rem);
            result = rem;
#endif
        }
#if defined(__GNUC__) || defined(__clang__)
        base = static_cast<uint64_t>(
            (static_cast<__uint128_t>(base) * base) % mod);
#else
        uint64_t high;
        uint64_t low = _umul128(base, base, &high);
        uint64_t rem;
        _udiv128(high, low, mod, &rem);
        base = rem;
#endif
        exp >>= 1;
    }
    return result;
}

bool miller_rabin(uint64_t n, int rounds) {
    if (n < 2) return false;
    if (n == 2 || n == 3) return true;
    if (n % 2 == 0) return false;

    // Write n-1 as 2^r * d
    uint64_t d = n - 1;
    int r = 0;
    while ((d & 1) == 0) {
        d >>= 1;
        ++r;
    }

    // Deterministic witnesses for n < 2^64
    // Using fixed witnesses that cover all 64-bit integers
    static const uint64_t witnesses[] = {
        2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37
    };

    int num_witnesses = (rounds < 12) ? rounds : 12;

    for (int i = 0; i < num_witnesses; ++i) {
        uint64_t a = witnesses[i];
        if (a >= n) continue;

        uint64_t x = mod_pow(a, d, n);
        if (x == 1 || x == n - 1) continue;

        bool composite = true;
        for (int j = 0; j < r - 1; ++j) {
#if defined(__GNUC__) || defined(__clang__)
            x = static_cast<uint64_t>(
                (static_cast<__uint128_t>(x) * x) % n);
#else
            uint64_t high;
            uint64_t low = _umul128(x, x, &high);
            uint64_t rem;
            _udiv128(high, low, n, &rem);
            x = rem;
#endif
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        if (composite) return false;
    }
    return true;
}

bool lucas_lehmer(int p) {
    if (p == 2) return true;
    if (p < 2) return false;

    // For Lucas-Lehmer, we need arbitrary precision for large p.
    // For stress testing, we use a simplified version with 128-bit
    // arithmetic, which works for p up to about 60.

    // M_p = 2^p - 1
    // We work modulo M_p using __uint128_t
#if defined(__GNUC__) || defined(__clang__)
    if (p > 60) {
        // For very large p, fall back to basic computation
        // that still stresses the CPU without needing big integers
        __uint128_t mp = (static_cast<__uint128_t>(1) << p) - 1;
        __uint128_t s = 4;

        for (int i = 0; i < p - 2; ++i) {
            s = (s * s - 2) % mp;
        }
        return s == 0;
    }

    uint64_t mp = (1ULL << p) - 1;
    uint64_t s = 4;

    for (int i = 0; i < p - 2; ++i) {
        // s = (s*s - 2) mod mp
        __uint128_t ss = static_cast<__uint128_t>(s) * s;
        ss -= 2;
        s = static_cast<uint64_t>(ss % mp);
    }
    return s == 0;
#else
    // MSVC simplified version
    if (p > 30) return false; // Can't handle without big integers
    uint64_t mp = (1ULL << p) - 1;
    uint64_t s = 4;
    for (int i = 0; i < p - 2; ++i) {
        uint64_t high;
        uint64_t low = _umul128(s, s, &high);
        uint64_t rem;
        if (low < 2) {
            // Handle underflow
            low -= 2;
            if (high > 0) --high;
        } else {
            low -= 2;
        }
        _udiv128(high, low, mp, &rem);
        s = rem;
    }
    return s == 0;
#endif
}

uint64_t stress_prime(uint64_t duration_ns) {
    uint64_t ops = 0;
    uint64_t iterations = 0;
    uint64_t candidate = 1000000007ULL; // Start from a known prime region

    auto start = std::chrono::high_resolution_clock::now();

    for (;;) {
        // Test primality with Miller-Rabin (20 rounds = heavy computation)
        bool is_prime = miller_rabin(candidate, 20);
        DO_NOT_OPTIMIZE(is_prime);

        // Each Miller-Rabin round involves modular exponentiation
        // Approximate ops: 20 rounds * ~64 squarings * 2 muls = ~2560 ops
        ops += 2560;
        candidate += 2; // Only test odd numbers
        ++iterations;

        // Periodically run Lucas-Lehmer for additional ALU stress
        if ((iterations & 0xFF) == 0) {
            // Test Mersenne primes for various exponents
            for (int p = 3; p <= 31; p += 2) {
                bool is_mersenne = lucas_lehmer(p);
                DO_NOT_OPTIMIZE(is_mersenne);
                ops += static_cast<uint64_t>(p) * p; // Approximate work
            }
        }

        // Check elapsed time every 32 iterations using iteration counter
        // (not ops, which may drift due to variable Lucas-Lehmer additions)
        if ((iterations & 0x1F) == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            uint64_t elapsed = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
            if (elapsed >= duration_ns) break;
        }
    }

    return ops;
}

}} // namespace occt::cpu
