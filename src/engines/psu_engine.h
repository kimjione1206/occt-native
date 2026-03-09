#pragma once

#include "base_engine.h"
#include "cpu_engine.h"
#include "gpu_engine.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace occt {

enum class PsuLoadPattern {
    STEADY,  // CPU Linpack + GPU max load simultaneously
    SPIKE,   // 5s idle -> 5s max load, repeating
    RAMP     // 0% -> 100% gradual increase
};

struct PsuMetrics {
    double total_power_watts = 0.0;
    double cpu_power_watts = 0.0;
    double gpu_power_watts = 0.0;
    bool cpu_running = false;
    bool gpu_running = false;
    double elapsed_secs = 0.0;
    int errors_cpu = 0;
    int errors_gpu = 0;
};

class PsuEngine : public IEngine {
public:
    PsuEngine();
    ~PsuEngine() override;

    PsuEngine(const PsuEngine&) = delete;
    PsuEngine& operator=(const PsuEngine&) = delete;

    /// Start PSU stress test.
    /// @param pattern     Load pattern to apply.
    /// @param duration_secs  0 = run until stop() is called.
    void start(PsuLoadPattern pattern, int duration_secs = 0);

    void stop() override;
    bool is_running() const override;
    std::string name() const override { return "PSU"; }

    PsuMetrics get_metrics() const;

    using MetricsCallback = std::function<void(const PsuMetrics&)>;
    void set_metrics_callback(MetricsCallback cb);

    /// Whether to use all available GPUs (default: false, uses first GPU only).
    void set_use_all_gpus(bool use_all) { use_all_gpus_ = use_all; }

private:
    void controller_thread_func(PsuLoadPattern pattern, int duration_secs);
    void metrics_poller_func();
    void start_cpu_load();
    void stop_cpu_load();
    void start_gpu_load();
    void stop_gpu_load();

    CpuEngine cpu_;
    GpuEngine gpu_;

    std::thread controller_thread_;
    std::thread metrics_thread_;
    std::mutex start_stop_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::chrono::steady_clock::time_point start_time_;

    mutable std::mutex metrics_mutex_;
    PsuMetrics current_metrics_;

    MetricsCallback metrics_cb_;
    std::mutex cb_mutex_;

    bool use_all_gpus_ = false;
};

} // namespace occt
