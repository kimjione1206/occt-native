#pragma once

#include "sensor_manager.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <chrono>
#include <functional>
#include <memory>

namespace occt {

/// Bridge to LibreHardwareMonitor on Windows.
///
/// Strategy: launch an external "lhm-sensor-reader" helper process that
/// outputs JSON on stdout each polling cycle.  Falls back to WMI if the
/// helper is not found.
///
/// On non-Windows platforms this is a no-op stub.
class LhmBridge : public QObject {
    Q_OBJECT

public:
    explicit LhmBridge(QObject* parent = nullptr);
    ~LhmBridge() override;

    /// Try to locate and start the LHM helper.  Returns true if the bridge
    /// is active (helper found or COM interop succeeded).
    bool initialize();

    /// Returns true if the bridge is providing data.
    bool is_available() const;

    /// Perform one poll cycle.  On success the readings are appended to @p out.
    void poll(std::vector<SensorReading>& out);

private:
#ifdef _WIN32
    struct Impl;
    std::unique_ptr<Impl> impl_;
#endif
    bool available_ = false;
    int fail_count_ = 0;
    int disable_count_ = 0;

    // Cache: reuse last data between poll intervals
    std::vector<SensorReading> cached_readings_;
    std::chrono::steady_clock::time_point last_poll_time_;
    static constexpr int POLL_INTERVAL_MS = 3000;  // LHM 호출 간격 (3초)
};

} // namespace occt
