#include "lhm_bridge.h"

#ifdef _WIN32
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <iostream>
#endif

namespace occt {

// ─── Windows implementation ─────────────────────────────────────────────────

#ifdef _WIN32

struct LhmBridge::Impl {
    QString helper_path;

    bool find_helper() {
        // Look for lhm-sensor-reader.exe next to the application
        QStringList search_paths = {
            QCoreApplication::applicationDirPath(),
            QCoreApplication::applicationDirPath() + "/tools",
            QDir::currentPath()
        };

        for (const auto& dir : search_paths) {
            QString candidate = dir + "/lhm-sensor-reader.exe";
            if (QFileInfo::exists(candidate)) {
                helper_path = candidate;
                return true;
            }
        }
        return false;
    }

    bool poll_helper(std::vector<SensorReading>& out) {
        if (helper_path.isEmpty()) return false;

        QProcess proc;
        proc.setProgram(helper_path);
        proc.setArguments({"--json", "--once"});
        proc.start();

        if (!proc.waitForFinished(5000)) {
            proc.kill();
            return false;
        }

        if (proc.exitCode() != 0) return false;

        QByteArray data = proc.readAllStandardOutput();
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) return false;

        if (!doc.isObject()) return false;
        QJsonObject root = doc.object();

        // Expected JSON format:
        // { "hardware": [ { "name": "...", "type": "CPU",
        //     "sensors": [ { "name": "...", "value": 45.0, "unit": "C" }, ... ] }, ... ] }
        QJsonArray hardware = root["hardware"].toArray();
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

                out.push_back(std::move(reading));
            }
        }
        return !hardware.isEmpty();
    }
};

LhmBridge::LhmBridge(QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>()) {}

LhmBridge::~LhmBridge() = default;

bool LhmBridge::initialize() {
    available_ = impl_->find_helper();
    if (available_) {
        std::cout << "[LHM] Found helper at: "
                  << impl_->helper_path.toStdString() << std::endl;
    } else {
        std::cout << "[LHM] Helper not found, will use WMI fallback" << std::endl;
    }
    return available_;
}

bool LhmBridge::is_available() const { return available_; }

void LhmBridge::poll(std::vector<SensorReading>& out) {
    if (!available_) return;
    if (!impl_->poll_helper(out)) {
        // Helper failed – mark unavailable so caller falls back to WMI
        available_ = false;
        std::cerr << "[LHM] Helper process failed, disabling bridge" << std::endl;
    }
}

#else
// ─── Non-Windows stub ───────────────────────────────────────────────────────

LhmBridge::LhmBridge(QObject* parent) : QObject(parent) {}
LhmBridge::~LhmBridge() = default;
bool LhmBridge::initialize() { return false; }
bool LhmBridge::is_available() const { return false; }
void LhmBridge::poll(std::vector<SensorReading>& /*out*/) {}

#endif

} // namespace occt
