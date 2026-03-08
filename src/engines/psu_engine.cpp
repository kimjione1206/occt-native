#include "psu_engine.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace occt {

PsuEngine::PsuEngine() = default;

PsuEngine::~PsuEngine() {
    stop();
}

void PsuEngine::start(PsuLoadPattern pattern, int duration_secs) {
    if (running_.load()) return;

    // Initialize GPU backend
    if (!gpu_.is_opencl_available()) {
        gpu_.initialize();
    }

    stop_requested_.store(false);
    running_.store(true);
    start_time_ = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        current_metrics_ = PsuMetrics{};
    }

    // Start metrics polling thread
    metrics_thread_ = std::thread(&PsuEngine::metrics_poller_func, this);

    // Start controller thread that manages load patterns
    controller_thread_ = std::thread(&PsuEngine::controller_thread_func, this, pattern, duration_secs);
}

void PsuEngine::stop() {
    stop_requested_.store(true);

    stop_cpu_load();
    stop_gpu_load();

    if (controller_thread_.joinable()) {
        controller_thread_.join();
    }
    if (metrics_thread_.joinable()) {
        metrics_thread_.join();
    }

    running_.store(false);
}

bool PsuEngine::is_running() const {
    return running_.load();
}

PsuMetrics PsuEngine::get_metrics() const {
    std::lock_guard<std::mutex> lk(metrics_mutex_);
    return current_metrics_;
}

void PsuEngine::set_metrics_callback(MetricsCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    metrics_cb_ = std::move(cb);
}

// ─── Load control helpers ────────────────────────────────────────────────────

void PsuEngine::start_cpu_load() {
    if (!cpu_.is_running()) {
        // Use Linpack mode for maximum power draw
        cpu_.start(CpuStressMode::LINPACK, 0, 0);
    }
}

void PsuEngine::stop_cpu_load() {
    if (cpu_.is_running()) {
        cpu_.stop();
    }
}

void PsuEngine::start_gpu_load() {
    if (!gpu_.is_running()) {
        gpu_.start(GpuStressMode::MATRIX_MUL, 0);
    }
}

void PsuEngine::stop_gpu_load() {
    if (gpu_.is_running()) {
        gpu_.stop();
    }
}

// ─── Controller thread ──────────────────────────────────────────────────────

void PsuEngine::controller_thread_func(PsuLoadPattern pattern, int duration_secs) {
    auto deadline = (duration_secs > 0)
        ? start_time_ + std::chrono::seconds(duration_secs)
        : (std::chrono::steady_clock::time_point::max)();

    auto has_time = [&]() {
        return !stop_requested_.load() &&
               std::chrono::steady_clock::now() < deadline;
    };

    switch (pattern) {
        case PsuLoadPattern::STEADY: {
            // Maximum sustained load: CPU Linpack + GPU max simultaneously
            start_cpu_load();
            start_gpu_load();

            while (has_time()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Detect sudden stop (PSU protection mode)
                if (!cpu_.is_running() && !gpu_.is_running()) {
                    std::cerr << "[PSU] Warning: Both CPU and GPU stopped unexpectedly. "
                              << "PSU may have triggered protection mode." << std::endl;
                    break;
                }
            }
            break;
        }

        case PsuLoadPattern::SPIKE: {
            // 5s idle -> 5s max load, repeating
            while (has_time()) {
                // Idle phase (5 seconds)
                stop_cpu_load();
                stop_gpu_load();
                for (int i = 0; i < 50 && has_time(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                if (!has_time()) break;

                // Max load phase (5 seconds)
                start_cpu_load();
                start_gpu_load();
                for (int i = 0; i < 50 && has_time(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));

                    // Detect sudden stop during spike
                    if (!cpu_.is_running() && !gpu_.is_running()) {
                        std::cerr << "[PSU] Warning: Load dropped during spike. "
                                  << "PSU protection may have triggered." << std::endl;
                    }
                }
            }
            break;
        }

        case PsuLoadPattern::RAMP: {
            // Gradual ramp: start CPU only, then add GPU
            // Phase 1: CPU only with increasing threads (0-33%)
            int total_cores = static_cast<int>(std::thread::hardware_concurrency());
            if (total_cores < 1) total_cores = 4;

            // Ramp CPU threads from 1 to max
            for (int t = 1; t <= total_cores && has_time(); ++t) {
                stop_cpu_load();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                cpu_.start(CpuStressMode::LINPACK, t, 0);

                // Hold each level for 2 seconds
                for (int i = 0; i < 20 && has_time(); ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }

            // Phase 2: CPU max + GPU (33-100%)
            if (has_time()) {
                start_gpu_load();

                while (has_time()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            break;
        }
    }

    // Clean shutdown
    stop_cpu_load();
    stop_gpu_load();
    running_.store(false);
}

// ─── Metrics polling ─────────────────────────────────────────────────────────

void PsuEngine::metrics_poller_func() {
    while (running_.load() && !stop_requested_.load()) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time_).count();

        // Gather metrics from sub-engines
        CpuMetrics cpu_m = cpu_.get_metrics();
        GpuMetrics gpu_m = gpu_.get_metrics();

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            current_metrics_.elapsed_secs = elapsed;
            current_metrics_.cpu_running = cpu_.is_running();
            current_metrics_.gpu_running = gpu_.is_running();
            current_metrics_.cpu_power_watts = cpu_m.power_watts;
            current_metrics_.gpu_power_watts = gpu_m.power_watts;
            current_metrics_.total_power_watts = cpu_m.power_watts + gpu_m.power_watts;
            // Error counting is cumulative
            current_metrics_.errors_cpu = cpu_m.error_count;
            current_metrics_.errors_gpu = static_cast<int>(gpu_m.vram_errors);
        }

        if (stop_on_error() && (cpu_m.error_count > 0 || gpu_m.vram_errors > 0)) {
            stop_requested_.store(true);
        }

        // Invoke callback: copy metrics under lock, release, then call callback
        PsuMetrics metrics_snapshot;
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_snapshot = current_metrics_;
        }
        {
            std::lock_guard<std::mutex> lk(cb_mutex_);
            if (metrics_cb_) {
                metrics_cb_(metrics_snapshot);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace occt
