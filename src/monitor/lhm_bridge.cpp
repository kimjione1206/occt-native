#include "lhm_bridge.h"

#ifdef _WIN32
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#endif

namespace occt {

// ─── Windows implementation ─────────────────────────────────────────────────

#ifdef _WIN32

LhmBridge::LhmBridge(QObject* parent)
    : QObject(parent) {}

LhmBridge::~LhmBridge() {
    stop_polling();
}

void LhmBridge::log(const std::string& msg) {
    if (log_file_.isEmpty()) {
        QString logDir = QCoreApplication::applicationDirPath() + "/logs";
        QDir().mkpath(logDir);
        log_file_ = logDir + "/lhm_bridge.log";
    }
    QFile f(log_file_);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")
           << " " << QString::fromStdString(msg) << "\n";
    }
}

bool LhmBridge::initialize() {
    // Look for lhm-sensor-reader.exe next to the application
    QStringList search_paths = {
        QCoreApplication::applicationDirPath(),
        QCoreApplication::applicationDirPath() + "/tools",
        QCoreApplication::applicationDirPath() + "/../tools",
        QDir::currentPath()
    };

    for (const auto& dir : search_paths) {
        QString candidate = dir + "/lhm-sensor-reader.exe";
        if (QFileInfo::exists(candidate)) {
            helper_path_ = candidate;
            available_ = true;
            log("[LHM] Found helper at: " + helper_path_.toStdString());
            return true;
        }
    }

    available_ = false;
    log("[LHM] Helper not found, using WMI fallback");
    return false;
}

bool LhmBridge::is_available() const {
    return available_;
}

bool LhmBridge::get_cached_readings(std::vector<SensorReading>& out) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (!has_valid_data_) return false;

    // Consider data stale after 5 seconds
    auto age = std::chrono::steady_clock::now() - last_success_time_;
    if (age > std::chrono::seconds(5)) return false;

    out = cached_readings_;
    return true;
}

void LhmBridge::start_polling() {
    if (!available_ || helper_path_.isEmpty()) return;

    poll_timer_ = new QTimer(this);
    connect(poll_timer_, &QTimer::timeout, this, &LhmBridge::poll_once);
    poll_timer_->start(2000);  // 2-second interval (temperature doesn't change fast)

    // Do one immediate poll
    poll_once();
}

void LhmBridge::stop_polling() {
    if (poll_timer_) {
        poll_timer_->stop();
        delete poll_timer_;
        poll_timer_ = nullptr;
    }
}

void LhmBridge::handle_failure() {
    fail_count_++;
    log("[LHM] Poll failed (attempt " + std::to_string(fail_count_) + "/5)");

    if (fail_count_ >= 5) {
        available_ = false;
        disable_count_++;
        // Exponential backoff: 60s, 120s, 240s... max 300s
        int backoff_secs = std::min(60 * (1 << (disable_count_ - 1)), 300);
        next_retry_time_ = std::chrono::steady_clock::now()
                         + std::chrono::seconds(backoff_secs);
        fail_count_ = 0;
        log("[LHM] Disabled after 5 failures, retry in "
            + std::to_string(backoff_secs) + "s");
    }
}

void LhmBridge::poll_once() {
    // Check backoff: if disabled, see if it's time to retry
    if (!available_) {
        if (std::chrono::steady_clock::now() >= next_retry_time_) {
            available_ = true;
            log("[LHM] Re-enabling after backoff (attempt "
                + std::to_string(disable_count_) + ")");
        } else {
            return;  // Still in backoff
        }
    }

    QProcess proc;
    proc.setProgram(helper_path_);
    proc.setArguments({"--json", "--once"});
    proc.start();

    // First run (no valid data yet): 30s timeout; subsequent: 10s
    int timeout = (!has_valid_data_ && fail_count_ == 0) ? 30000 : 10000;
    if (!proc.waitForFinished(timeout)) {
        log("[LHM] Helper timed out after " + std::to_string(timeout / 1000) + "s");
        proc.kill();
        proc.waitForFinished(3000);
        handle_failure();
        return;
    }

    QByteArray data = proc.readAllStandardOutput();
    QByteArray errData = proc.readAllStandardError();
    int exitCode = proc.exitCode();

    if (exitCode != 0) {
        log("[LHM] Helper exit code: " + std::to_string(exitCode));
        if (!errData.isEmpty()) {
            log("[LHM] Helper stderr: " + errData.toStdString());
        }
        log("[LHM] Helper stdout (first 500 chars): " + data.left(500).toStdString());
        handle_failure();
        return;
    }

    if (data.isEmpty()) {
        log("[LHM] Helper returned empty output");
        handle_failure();
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        log("[LHM] JSON parse error: " + err.errorString().toStdString()
            + " at offset " + std::to_string(err.offset));
        handle_failure();
        return;
    }

    if (!doc.isObject()) {
        handle_failure();
        return;
    }

    QJsonObject root = doc.object();

    // Expected JSON format:
    // { "hardware": [ { "name": "...", "type": "CPU",
    //     "sensors": [ { "name": "...", "value": 45.0, "unit": "C" }, ... ] }, ... ] }
    QJsonArray hardware = root["hardware"].toArray();

    std::vector<SensorReading> new_readings;
    for (const auto& hw_val : hardware) {
        QJsonObject hw = hw_val.toObject();
        QString type = hw["type"].toString();
        QJsonArray sensors = hw["sensors"].toArray();

        for (const auto& s_val : sensors) {
            QJsonObject s = s_val.toObject();
            SensorReading reading;
            reading.name     = s["name"].toString().toStdString();
            reading.category = type.toStdString();
            reading.value    = s["value"].toDouble();
            reading.unit     = s["unit"].toString().toStdString();

            if (s.contains("min")) reading.min_value = s["min"].toDouble();
            else                   reading.min_value = reading.value;
            if (s.contains("max")) reading.max_value = s["max"].toDouble();
            else                   reading.max_value = reading.value;

            new_readings.push_back(std::move(reading));
        }
    }

    if (hardware.isEmpty()) {
        handle_failure();
        return;
    }

    // Success: store cached readings thread-safely
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        cached_readings_ = std::move(new_readings);
        has_valid_data_ = true;
        last_success_time_ = std::chrono::steady_clock::now();
    }

    fail_count_ = 0;
    available_ = true;
}

#else
// ─── Non-Windows stub ───────────────────────────────────────────────────────

LhmBridge::LhmBridge(QObject* parent) : QObject(parent) {}
LhmBridge::~LhmBridge() = default;
bool LhmBridge::initialize() { return false; }
bool LhmBridge::is_available() const { return false; }
bool LhmBridge::get_cached_readings(std::vector<SensorReading>& /*out*/) const { return false; }
void LhmBridge::start_polling() {}
void LhmBridge::stop_polling() {}

#endif

} // namespace occt
