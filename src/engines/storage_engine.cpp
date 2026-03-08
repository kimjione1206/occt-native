#include "storage_engine.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <io.h>
#else
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#if defined(__linux__)
    #include <linux/fs.h>
#endif

namespace occt {

static constexpr size_t BLOCK_SIZE_4K  = 4096;
static constexpr size_t BLOCK_SIZE_1M  = 1024 * 1024;

// ─── xoshiro256** (same fast PRNG for offset generation) ─────────────────────

static inline uint64_t rotl64(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

struct FastRng {
    uint64_t s[4];

    void seed(uint64_t v) {
        for (int i = 0; i < 4; ++i) {
            v += 0x9e3779b97f4a7c15ULL;
            uint64_t z = v;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            s[i] = z ^ (z >> 31);
        }
    }

    uint64_t next() {
        const uint64_t result = rotl64(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
        s[2] ^= t; s[3] = rotl64(s[3], 45);
        return result;
    }
};

// ─── Constructor / Destructor ────────────────────────────────────────────────

StorageEngine::StorageEngine() = default;

StorageEngine::~StorageEngine() {
    stop();
}

// ─── Public API ──────────────────────────────────────────────────────────────

void StorageEngine::start(StorageMode mode, const std::string& path,
                          uint64_t file_size_mb, int queue_depth) {
    if (running_.load()) return;

    queue_depth = std::max(queue_depth, 1);
    uint64_t file_size_bytes = file_size_mb * 1024ULL * 1024ULL;

    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_ = StorageMetrics{};
    }

    stop_requested_.store(false);
    running_.store(true);

    worker_ = std::thread(&StorageEngine::run, this, mode, path,
                          file_size_bytes, queue_depth);
}

void StorageEngine::stop() {
    stop_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    running_.store(false);

    // Clean up test file
    if (!test_file_path_.empty()) {
#if defined(_WIN32)
        DeleteFileA(test_file_path_.c_str());
#else
        unlink(test_file_path_.c_str());
#endif
        test_file_path_.clear();
    }
}

bool StorageEngine::is_running() const {
    return running_.load();
}

StorageMetrics StorageEngine::get_metrics() const {
    std::lock_guard<std::mutex> lk(metrics_mutex_);
    return metrics_;
}

void StorageEngine::set_metrics_callback(MetricsCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    metrics_cb_ = std::move(cb);
}

// ─── Platform-specific helpers ───────────────────────────────────────────────

int StorageEngine::open_direct(const std::string& path, bool read_only) {
#if defined(_WIN32)
    DWORD access = read_only ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    DWORD creation = read_only ? OPEN_EXISTING : CREATE_ALWAYS;
    DWORD flags = FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;

    HANDLE h = CreateFileA(path.c_str(), access, 0, nullptr,
                           creation, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        // Direct I/O may fail on USB drives or network shares.
        // Retry without FILE_FLAG_NO_BUFFERING for compatibility.
        std::cerr << "[Storage] Warning: Direct I/O failed (error " << err
                  << "), retrying with buffered I/O. "
                  << "USB drives may not support unbuffered access." << std::endl;
        flags = FILE_FLAG_WRITE_THROUGH; // Keep write-through but drop no-buffering
        h = CreateFileA(path.c_str(), access, 0, nullptr,
                        creation, flags, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            // Last resort: try fully buffered mode
            std::cerr << "[Storage] Warning: Write-through also failed, "
                      << "using fully buffered mode" << std::endl;
            h = CreateFileA(path.c_str(), access, 0, nullptr,
                            creation, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return -1;
            std::cerr << "[Storage] Active mode: fully buffered I/O" << std::endl;
        } else {
            std::cerr << "[Storage] Active mode: buffered I/O with write-through" << std::endl;
        }
    } else {
        std::cerr << "[Storage] Active mode: direct I/O (unbuffered)" << std::endl;
    }
    return static_cast<int>(reinterpret_cast<intptr_t>(h));
#else
    int flags_val = read_only ? O_RDONLY : (O_RDWR | O_CREAT | O_TRUNC);
#if defined(__linux__)
    flags_val |= O_DIRECT;
#endif
    int fd = open(path.c_str(), flags_val, 0644);

#if defined(__linux__)
    if (fd < 0 && (flags_val & O_DIRECT)) {
        // O_DIRECT may fail on some filesystems (tmpfs, USB drives, etc.)
        std::cerr << "[Storage] Warning: O_DIRECT not supported, "
                  << "falling back to buffered I/O" << std::endl;
        flags_val &= ~O_DIRECT;
        fd = open(path.c_str(), flags_val, 0644);
        if (fd >= 0) {
            std::cerr << "[Storage] Active mode: buffered I/O" << std::endl;
        }
    } else if (fd >= 0) {
        std::cerr << "[Storage] Active mode: direct I/O (O_DIRECT)" << std::endl;
    }
#endif

    if (fd < 0) return -1;

#if defined(__APPLE__)
    // macOS: F_NOCACHE disables buffer cache (similar to O_DIRECT)
    if (fcntl(fd, F_NOCACHE, 1) == 0) {
        std::cerr << "[Storage] Active mode: direct I/O (F_NOCACHE)" << std::endl;
    } else {
        std::cerr << "[Storage] Active mode: buffered I/O (F_NOCACHE failed)" << std::endl;
    }
#endif
    return fd;
#endif
}

void StorageEngine::close_file(int fd) {
    if (fd < 0) return;
#if defined(_WIN32)
    CloseHandle(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)));
#else
    close(fd);
#endif
}

uint8_t* StorageEngine::alloc_aligned(size_t size) {
#if defined(_WIN32)
    return static_cast<uint8_t*>(_aligned_malloc(size, 4096));
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, 4096, size) != 0) return nullptr;
    return static_cast<uint8_t*>(ptr);
#endif
}

void StorageEngine::free_aligned(uint8_t* ptr) {
    if (!ptr) return;
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// ─── Worker thread ───────────────────────────────────────────────────────────

void StorageEngine::run(StorageMode mode, const std::string& path,
                        uint64_t file_size_bytes, int queue_depth) {
    // Build test file path
    std::string dir = path;
    if (dir.empty()) dir = ".";
    if (dir.back() != '/' && dir.back() != '\\') dir += '/';
    test_file_path_ = dir + "occt_storage_test.bin";

    // Allocate I/O buffer (1 MB for sequential, 4K for random)
    size_t buf_size = BLOCK_SIZE_1M;
    uint8_t* io_buf = alloc_aligned(buf_size);
    if (!io_buf) {
        running_.store(false);
        return;
    }

    // Fill buffer with pattern
    std::memset(io_buf, 0xA5, buf_size);

    bool needs_existing_file = (mode == StorageMode::SEQ_READ ||
                                mode == StorageMode::RAND_READ ||
                                mode == StorageMode::MIXED);

    // For read modes, create the file first
    if (needs_existing_file) {
        int wfd = open_direct(test_file_path_, false);
        if (wfd < 0) {
            free_aligned(io_buf);
            running_.store(false);
            return;
        }

        uint64_t written = 0;
        while (written < file_size_bytes && !stop_requested_.load()) {
            size_t to_write = std::min(static_cast<uint64_t>(buf_size),
                                       file_size_bytes - written);
#if defined(_WIN32)
            DWORD bytes_written = 0;
            WriteFile(reinterpret_cast<HANDLE>(static_cast<intptr_t>(wfd)),
                      io_buf, static_cast<DWORD>(to_write), &bytes_written, nullptr);
            written += bytes_written;
#else
            ssize_t ret = write(wfd, io_buf, to_write);
            if (ret <= 0) break;
            written += static_cast<uint64_t>(ret);
#endif
        }
        close_file(wfd);
    }

    // Open file for the actual test
    bool ro = (mode == StorageMode::SEQ_READ || mode == StorageMode::RAND_READ);
    int fd = open_direct(test_file_path_, ro);
    if (fd < 0) {
        free_aligned(io_buf);
        running_.store(false);
        return;
    }

    switch (mode) {
        case StorageMode::SEQ_WRITE:
            seq_write(fd, io_buf, buf_size, file_size_bytes, queue_depth);
            break;
        case StorageMode::SEQ_READ:
            seq_read(fd, io_buf, buf_size, file_size_bytes, queue_depth);
            break;
        case StorageMode::RAND_WRITE:
            rand_write(fd, io_buf, file_size_bytes, queue_depth);
            break;
        case StorageMode::RAND_READ:
            rand_read(fd, io_buf, file_size_bytes, queue_depth);
            break;
        case StorageMode::MIXED:
            mixed_io(fd, io_buf, file_size_bytes, queue_depth);
            break;
    }

    close_file(fd);
    free_aligned(io_buf);
    running_.store(false);
}

// ─── Sequential Write ────────────────────────────────────────────────────────

void StorageEngine::seq_write(int fd, uint8_t* buf, size_t buf_size,
                              uint64_t file_size, int /*queue_depth*/) {
    auto start = std::chrono::steady_clock::now();
    uint64_t total_written = 0;

    while (total_written < file_size && !stop_requested_.load()) {
        size_t to_write = std::min(static_cast<uint64_t>(buf_size),
                                   file_size - total_written);

#if defined(_WIN32)
        DWORD bytes_written = 0;
        WriteFile(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)),
                  buf, static_cast<DWORD>(to_write), &bytes_written, nullptr);
        total_written += bytes_written;
#else
        ssize_t ret = write(fd, buf, to_write);
        if (ret <= 0) break;
        total_written += static_cast<uint64_t>(ret);
#endif

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
            metrics_.write_mbs = elapsed > 0 ?
                (static_cast<double>(total_written) / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.progress_pct = (static_cast<double>(total_written) / file_size) * 100.0;
        }
    }
}

// ─── Sequential Read ─────────────────────────────────────────────────────────

void StorageEngine::seq_read(int fd, uint8_t* buf, size_t buf_size,
                             uint64_t file_size, int /*queue_depth*/) {
    auto start = std::chrono::steady_clock::now();
    uint64_t total_read = 0;

    while (total_read < file_size && !stop_requested_.load()) {
        size_t to_read = std::min(static_cast<uint64_t>(buf_size),
                                  file_size - total_read);

#if defined(_WIN32)
        DWORD bytes_read = 0;
        ReadFile(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)),
                 buf, static_cast<DWORD>(to_read), &bytes_read, nullptr);
        if (bytes_read == 0) break;
        total_read += bytes_read;
#else
        ssize_t ret = read(fd, buf, to_read);
        if (ret <= 0) break;
        total_read += static_cast<uint64_t>(ret);
#endif

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
            metrics_.read_mbs = elapsed > 0 ?
                (static_cast<double>(total_read) / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.progress_pct = (static_cast<double>(total_read) / file_size) * 100.0;
        }
    }
}

// ─── Random 4K Write ─────────────────────────────────────────────────────────

void StorageEngine::rand_write(int fd, uint8_t* buf, uint64_t file_size,
                               int queue_depth) {
    auto start = std::chrono::steady_clock::now();
    const uint64_t max_blocks = file_size / BLOCK_SIZE_4K;
    if (max_blocks == 0) return;

    FastRng rng;
    rng.seed(std::chrono::steady_clock::now().time_since_epoch().count());

    uint64_t ops = 0;
    const uint64_t target_ops = max_blocks; // one pass over all blocks

    std::vector<std::thread> workers;
    std::atomic<uint64_t> shared_ops{0};

    auto worker_fn = [&](int /*id*/) {
        FastRng local_rng;
        local_rng.seed(std::chrono::steady_clock::now().time_since_epoch().count()
                       + std::hash<std::thread::id>{}(std::this_thread::get_id()));

        while (!stop_requested_.load()) {
            uint64_t current = shared_ops.fetch_add(1);
            if (current >= target_ops) break;

            uint64_t block = local_rng.next() % max_blocks;
            uint64_t offset = block * BLOCK_SIZE_4K;

#if defined(_WIN32)
            OVERLAPPED ov{};
            ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD written = 0;
            WriteFile(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)),
                      buf, BLOCK_SIZE_4K, &written, &ov);
#else
            pwrite(fd, buf, BLOCK_SIZE_4K, static_cast<off_t>(offset));
#endif
        }
    };

    for (int i = 0; i < queue_depth; ++i) {
        workers.emplace_back(worker_fn, i);
    }

    // Monitor progress
    while (!stop_requested_.load()) {
        ops = shared_ops.load();
        if (ops >= target_ops) break;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
            metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
            metrics_.write_mbs = metrics_.iops * BLOCK_SIZE_4K / (1024.0 * 1024.0);
            metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
            metrics_.progress_pct = (static_cast<double>(ops) / target_ops) * 100.0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    // Final metrics
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start).count();
    ops = shared_ops.load();
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.elapsed_secs = elapsed;
        metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
        metrics_.write_mbs = metrics_.iops * BLOCK_SIZE_4K / (1024.0 * 1024.0);
        metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
        metrics_.progress_pct = 100.0;
    }
}

// ─── Random 4K Read ──────────────────────────────────────────────────────────

void StorageEngine::rand_read(int fd, uint8_t* buf, uint64_t file_size,
                              int queue_depth) {
    auto start = std::chrono::steady_clock::now();
    const uint64_t max_blocks = file_size / BLOCK_SIZE_4K;
    if (max_blocks == 0) return;

    uint64_t target_ops = max_blocks;
    std::atomic<uint64_t> shared_ops{0};
    std::vector<std::thread> workers;

    // Per-thread aligned read buffer
    auto worker_fn = [&](int /*id*/) {
        uint8_t* local_buf = alloc_aligned(BLOCK_SIZE_4K);
        if (!local_buf) return;

        FastRng local_rng;
        local_rng.seed(std::chrono::steady_clock::now().time_since_epoch().count()
                       + std::hash<std::thread::id>{}(std::this_thread::get_id()));

        while (!stop_requested_.load()) {
            uint64_t current = shared_ops.fetch_add(1);
            if (current >= target_ops) break;

            uint64_t block = local_rng.next() % max_blocks;
            uint64_t offset = block * BLOCK_SIZE_4K;

#if defined(_WIN32)
            OVERLAPPED ov{};
            ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD bytes_read = 0;
            ReadFile(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)),
                     local_buf, BLOCK_SIZE_4K, &bytes_read, &ov);
#else
            pread(fd, local_buf, BLOCK_SIZE_4K, static_cast<off_t>(offset));
#endif
        }

        free_aligned(local_buf);
    };

    for (int i = 0; i < queue_depth; ++i) {
        workers.emplace_back(worker_fn, i);
    }

    while (!stop_requested_.load()) {
        uint64_t ops = shared_ops.load();
        if (ops >= target_ops) break;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
            metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
            metrics_.read_mbs = metrics_.iops * BLOCK_SIZE_4K / (1024.0 * 1024.0);
            metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
            metrics_.progress_pct = (static_cast<double>(ops) / target_ops) * 100.0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start).count();
    uint64_t ops = shared_ops.load();
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.elapsed_secs = elapsed;
        metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
        metrics_.read_mbs = metrics_.iops * BLOCK_SIZE_4K / (1024.0 * 1024.0);
        metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
        metrics_.progress_pct = 100.0;
    }
}

// ─── Mixed I/O (70% read / 30% write) ───────────────────────────────────────

void StorageEngine::mixed_io(int fd, uint8_t* buf, uint64_t file_size,
                             int queue_depth) {
    auto start = std::chrono::steady_clock::now();
    const uint64_t max_blocks = file_size / BLOCK_SIZE_4K;
    if (max_blocks == 0) return;

    uint64_t target_ops = max_blocks;
    std::atomic<uint64_t> shared_ops{0};
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> total_writes{0};
    std::vector<std::thread> workers;

    auto worker_fn = [&](int /*id*/) {
        uint8_t* local_buf = alloc_aligned(BLOCK_SIZE_4K);
        if (!local_buf) return;
        std::memset(local_buf, 0xCD, BLOCK_SIZE_4K);

        FastRng local_rng;
        local_rng.seed(std::chrono::steady_clock::now().time_since_epoch().count()
                       + std::hash<std::thread::id>{}(std::this_thread::get_id()));

        while (!stop_requested_.load()) {
            uint64_t current = shared_ops.fetch_add(1);
            if (current >= target_ops) break;

            uint64_t block = local_rng.next() % max_blocks;
            uint64_t offset = block * BLOCK_SIZE_4K;
            bool do_read = (local_rng.next() % 100) < 70; // 70% reads

#if defined(_WIN32)
            OVERLAPPED ov{};
            ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD bytes = 0;
            if (do_read) {
                ReadFile(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)),
                         local_buf, BLOCK_SIZE_4K, &bytes, &ov);
                total_reads.fetch_add(1);
            } else {
                WriteFile(reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd)),
                          local_buf, BLOCK_SIZE_4K, &bytes, &ov);
                total_writes.fetch_add(1);
            }
#else
            if (do_read) {
                pread(fd, local_buf, BLOCK_SIZE_4K, static_cast<off_t>(offset));
                total_reads.fetch_add(1);
            } else {
                pwrite(fd, local_buf, BLOCK_SIZE_4K, static_cast<off_t>(offset));
                total_writes.fetch_add(1);
            }
#endif
        }

        free_aligned(local_buf);
    };

    for (int i = 0; i < queue_depth; ++i) {
        workers.emplace_back(worker_fn, i);
    }

    while (!stop_requested_.load()) {
        uint64_t ops = shared_ops.load();
        if (ops >= target_ops) break;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        double iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
            metrics_.iops = iops;
            uint64_t r = total_reads.load();
            uint64_t w = total_writes.load();
            metrics_.read_mbs = elapsed > 0 ?
                (static_cast<double>(r) * BLOCK_SIZE_4K / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.write_mbs = elapsed > 0 ?
                (static_cast<double>(w) * BLOCK_SIZE_4K / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
            metrics_.progress_pct = (static_cast<double>(ops) / target_ops) * 100.0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start).count();
    uint64_t ops = shared_ops.load();
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.elapsed_secs = elapsed;
        metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
        metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
        metrics_.progress_pct = 100.0;
    }
}

} // namespace occt
