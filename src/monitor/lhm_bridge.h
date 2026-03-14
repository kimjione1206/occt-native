#pragma once

#include "sensor_manager.h"

#include <QObject>
#include <QString>
#include <QVector>

#ifdef _WIN32
#include <QThread>
#include <QTimer>
#endif

#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace occt {

/// Bridge to LibreHardwareMonitor on Windows.
///
/// Runs on a dedicated QThread with its own event loop so that QProcess
/// and QTimer work correctly (avoiding the std::thread + QProcess problem).
///
/// Polls the helper periodically and caches the results thread-safely.
/// Other threads retrieve cached data via get_cached_readings().
///
/// On non-Windows platforms this is a no-op stub.
class LhmBridge : public QObject {
    Q_OBJECT

public:
    explicit LhmBridge(QObject* parent = nullptr);
    ~LhmBridge() override;

    /// Try to locate the LHM helper.  Returns true if the helper was found.
    bool initialize();

    /// Returns true if the bridge is providing data.
    bool is_available() const;

    /// Get cached readings (thread-safe).  Returns true if valid cached data exists.
    bool get_cached_readings(std::vector<SensorReading>& out) const;

public slots:
    /// Start the QTimer-based polling loop (called when QThread starts).
    void start_polling();

    /// Stop polling and clean up (call via QMetaObject::invokeMethod).
    void stop_polling();

private:
#ifdef _WIN32
    void poll_once();
    void handle_failure();
    void log(const std::string& msg);

    QString helper_path_;
    QTimer* poll_timer_ = nullptr;

    // Thread-safe cached readings
    mutable std::mutex data_mutex_;
    std::vector<SensorReading> cached_readings_;
    std::chrono::steady_clock::time_point last_success_time_;
    bool has_valid_data_ = false;

    // Failure tracking and exponential backoff
    int fail_count_ = 0;
    int disable_count_ = 0;
    std::chrono::steady_clock::time_point next_retry_time_;

    // Log file
    QString log_file_;
#endif

    bool available_ = false;
};

} // namespace occt
