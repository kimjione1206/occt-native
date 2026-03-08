#pragma once

#include <string>
#include <vector>

namespace occt { namespace utils {

struct CpuInfo {
    std::string brand;
    int physical_cores = 0;
    int logical_cores = 0;
    int l1_cache_kb = 0;
    int l2_cache_kb = 0;
    int l3_cache_kb = 0;
    bool has_sse42 = false;
    bool has_avx = false;
    bool has_avx2 = false;
    bool has_avx512f = false;
    bool has_fma = false;
};

CpuInfo detect_cpu();

}} // namespace occt::utils
