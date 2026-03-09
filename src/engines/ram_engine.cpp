#include "ram_engine.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#elif defined(__linux__)
    #include <sys/mman.h>
    #include <sys/sysinfo.h>
    #include <unistd.h>
#elif defined(__APPLE__)
    #include <mach/mach.h>
    #include <sys/mman.h>
    #include <sys/sysctl.h>
    #include <unistd.h>
#endif

// AVX2 streaming stores (when available at compile time)
#if defined(__AVX2__) || defined(__AVX__)
    #include <immintrin.h>
    #define OCCT_USE_AVX2_STREAM 1
#elif defined(_MSC_VER) && defined(OCCT_HAS_AVX2)
    #include <immintrin.h>
    #define OCCT_USE_AVX2_STREAM 1
#endif

namespace occt {

// ─── xoshiro256** PRNG (fast, high-quality) ──────────────────────────────────

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

struct Xoshiro256 {
    uint64_t s[4];

    void seed(uint64_t seed_val) {
        // SplitMix64 to fill state
        for (int i = 0; i < 4; ++i) {
            seed_val += 0x9e3779b97f4a7c15ULL;
            uint64_t z = seed_val;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            s[i] = z ^ (z >> 31);
        }
    }

    uint64_t next() {
        const uint64_t result = rotl(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }
};

// ─── Constructor / Destructor ────────────────────────────────────────────────

RamEngine::RamEngine() = default;

RamEngine::~RamEngine() {
    stop();
}

// ─── Public API ──────────────────────────────────────────────────────────────

void RamEngine::start(RamPattern pattern, double memory_pct, int passes) {
    std::lock_guard<std::mutex> guard(start_stop_mutex_);

    if (running_.load()) return;

    memory_pct = std::clamp(memory_pct, 0.01, 0.95);
    passes = std::max(passes, 1);

    size_t total_ram = get_total_physical_ram();
    size_t test_size = static_cast<size_t>(static_cast<double>(total_ram) * memory_pct);

    // Align to page boundary (4 KB)
    test_size &= ~static_cast<size_t>(4095);
    if (test_size < 4096) test_size = 4096;

    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_ = RamMetrics{};
        metrics_.memory_used_mb = static_cast<double>(test_size) / (1024.0 * 1024.0);
    }

    stop_requested_.store(false);
    running_.store(true);

    worker_ = std::thread(&RamEngine::run, this, pattern, test_size, passes);
}

void RamEngine::stop() {
    std::lock_guard<std::mutex> guard(start_stop_mutex_);

    stop_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    running_.store(false);
}

bool RamEngine::is_running() const {
    return running_.load();
}

RamMetrics RamEngine::get_metrics() const {
    std::lock_guard<std::mutex> lk(metrics_mutex_);
    return metrics_;
}

void RamEngine::set_metrics_callback(MetricsCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    metrics_cb_ = std::move(cb);
}

// ─── Platform: get total physical RAM ────────────────────────────────────────

size_t RamEngine::get_total_physical_ram() const {
#if defined(_WIN32)
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    GlobalMemoryStatusEx(&mem);
    return static_cast<size_t>(mem.ullTotalPhys);
#elif defined(__linux__)
    struct sysinfo si{};
    sysinfo(&si);
    return static_cast<size_t>(si.totalram) * si.mem_unit;
#elif defined(__APPLE__)
    int64_t memsize = 0;
    size_t len = sizeof(memsize);
    sysctlbyname("hw.memsize", &memsize, &len, nullptr, 0);
    return static_cast<size_t>(memsize);
#else
    return 1ULL << 30; // fallback: 1 GB
#endif
}

// ─── Worker thread ───────────────────────────────────────────────────────────

void RamEngine::run(RamPattern pattern, size_t total_bytes, int passes) {
    // Allocate and lock memory
    uint8_t* buffer = nullptr;
    locked_pages_ = false;

#if defined(_WIN32)
    buffer = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, total_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!buffer) {
        std::cerr << "[RAM] Warning: VirtualAlloc failed (error "
                  << GetLastError() << "), cannot run test" << std::endl;
        running_.store(false);
        return;
    }
    // Attempt to lock pages into physical RAM
    if (!VirtualLock(buffer, total_bytes)) {
        DWORD err = GetLastError();
        if (err == ERROR_WORKING_SET_QUOTA) {
            // Try increasing the working set size first, then retry
            SIZE_T min_ws = 0, max_ws = 0;
            if (GetProcessWorkingSetSize(GetCurrentProcess(), &min_ws, &max_ws)) {
                SIZE_T new_min = min_ws + total_bytes;
                SIZE_T new_max = max_ws + total_bytes;
                if (SetProcessWorkingSetSize(GetCurrentProcess(), new_min, new_max)) {
                    if (VirtualLock(buffer, total_bytes)) {
                        locked_pages_ = true;
                    }
                }
            }
        }
        if (!locked_pages_) {
            std::cerr << "[RAM] Warning: VirtualLock failed (error " << err
                      << "), continuing with unlocked memory. "
                      << "Results may be less accurate due to paging." << std::endl;
        }
    } else {
        locked_pages_ = true;
    }
#else
    buffer = static_cast<uint8_t*>(
        mmap(nullptr, total_bytes, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANON, -1, 0));
    if (buffer == MAP_FAILED) {
        std::cerr << "[RAM] Warning: mmap failed, cannot run test" << std::endl;
        running_.store(false);
        return;
    }
    // Best-effort: lock pages (may fail without CAP_IPC_LOCK)
    if (mlock(buffer, total_bytes) == 0) {
        locked_pages_ = true;
    } else {
        std::cerr << "[RAM] Warning: mlock failed, continuing with unlocked memory. "
                  << "Results may be less accurate due to paging." << std::endl;
    }
#endif

    // Record lock status in metrics
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.pages_locked = locked_pages_;
    }

    test_start_time_ = std::chrono::steady_clock::now();
    auto start_time = test_start_time_;

    for (int pass = 0; pass < passes && !stop_requested_.load(); ++pass) {
        // Update progress based on pass
        double base_pct = (static_cast<double>(pass) / passes) * 100.0;
        update_progress(base_pct);

        switch (pattern) {
            case RamPattern::MARCH_C_MINUS:
                march_c_minus(buffer, total_bytes);
                break;
            case RamPattern::WALKING_ONES:
                walking_ones(buffer, total_bytes);
                break;
            case RamPattern::WALKING_ZEROS:
                walking_zeros(buffer, total_bytes);
                break;
            case RamPattern::CHECKERBOARD:
                checkerboard(buffer, total_bytes);
                break;
            case RamPattern::RANDOM:
                random_pattern(buffer, total_bytes);
                break;
            case RamPattern::BANDWIDTH:
                bandwidth_test(buffer, total_bytes);
                break;
        }

        // Update elapsed time
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
        }
    }

    update_progress(100.0);

    // Free memory
#if defined(_WIN32)
    if (locked_pages_) {
        VirtualUnlock(buffer, total_bytes);
    }
    VirtualFree(buffer, 0, MEM_RELEASE);
#else
    if (locked_pages_) {
        munlock(buffer, total_bytes);
    }
    munmap(buffer, total_bytes);
#endif

    running_.store(false);
}

// ─── March C- Algorithm ──────────────────────────────────────────────────────
// 6 phases: W0, R0W1(up), R1W0(up), R0W1(down), R1W0(down), R0
void RamEngine::march_c_minus(uint8_t* buf, size_t size) {
    const size_t count = size / sizeof(uint64_t);
    auto* p = reinterpret_cast<uint64_t*>(buf);

    auto start = std::chrono::steady_clock::now();
    size_t bytes_processed = 0;

    // Phase 1: Write 0 everywhere
    for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
        p[i] = 0ULL;
    }
    bytes_processed += size;

    // Phase 2: Read 0 / Write 1 (ascending)
    for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
        if (p[i] != 0ULL) report_error(i * 8, 0ULL, p[i]);
        p[i] = ~0ULL;
    }
    bytes_processed += size * 2; // read + write

    // Phase 3: Read 1 / Write 0 (ascending)
    for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
        if (p[i] != ~0ULL) report_error(i * 8, ~0ULL, p[i]);
        p[i] = 0ULL;
    }
    bytes_processed += size * 2;

    // Phase 4: Read 0 / Write 1 (descending)
    for (size_t idx = count; idx > 0 && !stop_requested_.load(); --idx) {
        size_t i = idx - 1;
        if (p[i] != 0ULL) report_error(i * 8, 0ULL, p[i]);
        p[i] = ~0ULL;
    }
    bytes_processed += size * 2;

    // Phase 5: Read 1 / Write 0 (descending)
    for (size_t idx = count; idx > 0 && !stop_requested_.load(); --idx) {
        size_t i = idx - 1;
        if (p[i] != ~0ULL) report_error(i * 8, ~0ULL, p[i]);
        p[i] = 0ULL;
    }
    bytes_processed += size * 2;

    // Phase 6: Read 0 (verify)
    for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
        if (p[i] != 0ULL) report_error(i * 8, 0ULL, p[i]);
    }
    bytes_processed += size;

    auto elapsed = std::chrono::steady_clock::now() - start;
    double secs = std::chrono::duration<double>(elapsed).count();
    if (secs > 0.0) {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.bandwidth_mbs = (static_cast<double>(bytes_processed) / (1024.0 * 1024.0)) / secs;
    }
}

// ─── Walking Ones ────────────────────────────────────────────────────────────
void RamEngine::walking_ones(uint8_t* buf, size_t size) {
    const size_t count = size / sizeof(uint64_t);
    auto* p = reinterpret_cast<uint64_t*>(buf);

    auto start = std::chrono::steady_clock::now();
    size_t bytes_processed = 0;

    for (int bit = 0; bit < 64 && !stop_requested_.load(); ++bit) {
        uint64_t pattern = 1ULL << bit;

        // Write pattern
        for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
            p[i] = pattern;
        }
        bytes_processed += size;

        // Verify pattern
        for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
            if (p[i] != pattern) report_error(i * 8, pattern, p[i]);
        }
        bytes_processed += size;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    double secs = std::chrono::duration<double>(elapsed).count();
    if (secs > 0.0) {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.bandwidth_mbs = (static_cast<double>(bytes_processed) / (1024.0 * 1024.0)) / secs;
    }
}

// ─── Walking Zeros ───────────────────────────────────────────────────────────
void RamEngine::walking_zeros(uint8_t* buf, size_t size) {
    const size_t count = size / sizeof(uint64_t);
    auto* p = reinterpret_cast<uint64_t*>(buf);

    auto start = std::chrono::steady_clock::now();
    size_t bytes_processed = 0;

    for (int bit = 0; bit < 64 && !stop_requested_.load(); ++bit) {
        uint64_t pattern = ~(1ULL << bit);

        for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
            p[i] = pattern;
        }
        bytes_processed += size;

        for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
            if (p[i] != pattern) report_error(i * 8, pattern, p[i]);
        }
        bytes_processed += size;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    double secs = std::chrono::duration<double>(elapsed).count();
    if (secs > 0.0) {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.bandwidth_mbs = (static_cast<double>(bytes_processed) / (1024.0 * 1024.0)) / secs;
    }
}

// ─── Checkerboard ────────────────────────────────────────────────────────────
void RamEngine::checkerboard(uint8_t* buf, size_t size) {
    const size_t count = size / sizeof(uint64_t);
    auto* p = reinterpret_cast<uint64_t*>(buf);

    auto start = std::chrono::steady_clock::now();
    size_t bytes_processed = 0;

    // 0xAAAAAAAAAAAAAAAA pattern
    const uint64_t pat_a = 0xAAAAAAAAAAAAAAAAULL;
    const uint64_t pat_b = 0x5555555555555555ULL;

    // Pass 1: write pattern A, verify, write pattern B, verify
    for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
        p[i] = pat_a;
    }
    bytes_processed += size;

    for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
        if (p[i] != pat_a) report_error(i * 8, pat_a, p[i]);
    }
    bytes_processed += size;

    for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
        p[i] = pat_b;
    }
    bytes_processed += size;

    for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
        if (p[i] != pat_b) report_error(i * 8, pat_b, p[i]);
    }
    bytes_processed += size;

    auto elapsed = std::chrono::steady_clock::now() - start;
    double secs = std::chrono::duration<double>(elapsed).count();
    if (secs > 0.0) {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.bandwidth_mbs = (static_cast<double>(bytes_processed) / (1024.0 * 1024.0)) / secs;
    }
}

// ─── Random Pattern ──────────────────────────────────────────────────────────
void RamEngine::random_pattern(uint8_t* buf, size_t size) {
    const size_t count = size / sizeof(uint64_t);
    auto* p = reinterpret_cast<uint64_t*>(buf);

    Xoshiro256 rng;
    rng.seed(std::chrono::steady_clock::now().time_since_epoch().count());

    auto start = std::chrono::steady_clock::now();
    size_t bytes_processed = 0;

    // Generate and write random values, storing seed state to re-generate for verify
    // Strategy: write random, then re-seed and verify
    uint64_t seed_save = std::chrono::steady_clock::now().time_since_epoch().count();
    Xoshiro256 rng_write;
    rng_write.seed(seed_save);

    for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
        p[i] = rng_write.next();
    }
    bytes_processed += size;

    // Verify with same sequence
    Xoshiro256 rng_verify;
    rng_verify.seed(seed_save);

    for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
        uint64_t expected = rng_verify.next();
        if (p[i] != expected) report_error(i * 8, expected, p[i]);
    }
    bytes_processed += size;

    auto elapsed = std::chrono::steady_clock::now() - start;
    double secs = std::chrono::duration<double>(elapsed).count();
    if (secs > 0.0) {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.bandwidth_mbs = (static_cast<double>(bytes_processed) / (1024.0 * 1024.0)) / secs;
    }
}

// ─── Bandwidth Test ──────────────────────────────────────────────────────────
void RamEngine::bandwidth_test(uint8_t* buf, size_t size) {
    auto start = std::chrono::steady_clock::now();
    size_t bytes_processed = 0;

#if defined(OCCT_USE_AVX2_STREAM)
    // Use non-temporal (streaming) stores for maximum bandwidth
    const size_t avx_count = size / 32; // 256-bit = 32 bytes
    auto* dst = reinterpret_cast<__m256i*>(buf);

    // Ensure alignment (mmap/VirtualAlloc typically returns page-aligned)
    if ((reinterpret_cast<uintptr_t>(buf) & 31) == 0) {
        __m256i val = _mm256_set1_epi64x(static_cast<long long>(0xDEADBEEFCAFEBABEULL));

        // Write pass (streaming store)
        for (size_t i = 0; i < avx_count && !stop_requested_.load(); ++i) {
            _mm256_stream_si256(&dst[i], val);
        }
        _mm_sfence();
        bytes_processed += avx_count * 32;

        // Read pass (regular load; there's no streaming load on x86)
        volatile uint64_t sink = 0;
        auto* src = reinterpret_cast<const __m256i*>(buf);
        for (size_t i = 0; i < avx_count && !stop_requested_.load(); ++i) {
            __m256i data = _mm256_load_si256(&src[i]);
            // Prevent optimization
            sink += static_cast<uint64_t>(_mm256_extract_epi64(data, 0));
        }
        bytes_processed += avx_count * 32;
        (void)sink;
    } else
#endif
    {
        // Fallback: 64-bit streaming
        const size_t count = size / sizeof(uint64_t);
        auto* p = reinterpret_cast<uint64_t*>(buf);

        // Write pass
        for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
            p[i] = 0xDEADBEEFCAFEBABEULL;
        }
        bytes_processed += size;

        // Read pass
        volatile uint64_t sink = 0;
        for (size_t i = 0; i < count && !stop_requested_.load(); ++i) {
            sink += p[i];
        }
        bytes_processed += size;
        (void)sink;
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    double secs = std::chrono::duration<double>(elapsed).count();
    if (secs > 0.0) {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.bandwidth_mbs = (static_cast<double>(bytes_processed) / (1024.0 * 1024.0)) / secs;
    }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

void RamEngine::report_error(uint64_t address, uint64_t expected, uint64_t actual) {
    auto now = std::chrono::steady_clock::now();
    double timestamp = std::chrono::duration<double>(now - test_start_time_).count();

    std::lock_guard<std::mutex> lk(metrics_mutex_);
    metrics_.errors_found++;

    if (metrics_.error_log.size() < 1000) {
        metrics_.error_log.push_back({address, expected, actual, timestamp});
    }

    std::cerr << "[RAM] Error at offset 0x" << std::hex << address
              << ": expected 0x" << expected
              << ", actual 0x" << actual
              << std::dec << " (t=" << timestamp << "s)" << std::endl;

    if (stop_on_error()) {
        stop_requested_.store(true);
    }
}

void RamEngine::update_progress(double pct) {
    RamMetrics snapshot;
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.progress_pct = pct;
        snapshot = metrics_;
    }

    MetricsCallback cb_copy;
    {
        std::lock_guard<std::mutex> lk(cb_mutex_);
        cb_copy = metrics_cb_;
    }
    if (cb_copy) {
        cb_copy(snapshot);
    }
}

} // namespace occt
