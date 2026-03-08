#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "base_engine.h"

namespace occt {

enum class StorageMode {
    SEQ_WRITE,   // Sequential write
    SEQ_READ,    // Sequential read
    RAND_WRITE,  // Random 4K write
    RAND_READ,   // Random 4K read
    MIXED        // Mixed 70% read / 30% write
};

struct StorageMetrics {
    double write_mbs   = 0.0;   // MB/s write throughput
    double read_mbs    = 0.0;   // MB/s read throughput
    double iops        = 0.0;   // I/O operations per second
    double latency_us  = 0.0;   // Average latency in microseconds
    double elapsed_secs = 0.0;
    double progress_pct = 0.0;  // 0 ~ 100
};

class StorageEngine : public IEngine {
public:
    StorageEngine();
    ~StorageEngine() override;

    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;

    /// Start storage I/O stress test.
    /// @param mode          I/O pattern.
    /// @param path          Directory/file path for test file.
    /// @param file_size_mb  Total test file size in MB.
    /// @param queue_depth   Number of concurrent I/O threads.
    void start(StorageMode mode, const std::string& path,
               uint64_t file_size_mb = 2048, int queue_depth = 4);

    void stop() override;
    bool is_running() const override;
    std::string name() const override { return "Storage"; }

    StorageMetrics get_metrics() const;

    using MetricsCallback = std::function<void(const StorageMetrics&)>;
    void set_metrics_callback(MetricsCallback cb);

private:
    void run(StorageMode mode, const std::string& path,
             uint64_t file_size_bytes, int queue_depth);

    void seq_write(int fd, uint8_t* buf, size_t buf_size,
                   uint64_t file_size, int queue_depth);
    void seq_read(int fd, uint8_t* buf, size_t buf_size,
                  uint64_t file_size, int queue_depth);
    void rand_write(int fd, uint8_t* buf, uint64_t file_size, int queue_depth);
    void rand_read(int fd, uint8_t* buf, uint64_t file_size, int queue_depth);
    void mixed_io(int fd, uint8_t* buf, uint64_t file_size, int queue_depth);

    // Platform-specific helpers
    int open_direct(const std::string& path, bool read_only);
    void close_file(int fd);
    uint8_t* alloc_aligned(size_t size);
    void free_aligned(uint8_t* ptr);

    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    mutable std::mutex metrics_mutex_;
    StorageMetrics metrics_;

    MetricsCallback metrics_cb_;
    std::mutex cb_mutex_;

    std::string test_file_path_;
};

} // namespace occt
