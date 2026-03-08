#pragma once

#include <cstdint>

namespace occt { namespace cpu {

// Run prime-finding stress for specified duration (nanoseconds)
// Returns total operations executed
uint64_t stress_prime(uint64_t duration_ns);

// Miller-Rabin primality test
bool miller_rabin(uint64_t n, int rounds = 20);

// Lucas-Lehmer primality test for Mersenne numbers (2^p - 1)
bool lucas_lehmer(int p);

}} // namespace occt::cpu
