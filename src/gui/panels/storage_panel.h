#pragma once

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QFrame>
#include <memory>

namespace occt {
class StorageEngine;
}

namespace occt { namespace gui {

class RealtimeChart;

class StoragePanel : public QWidget {
    Q_OBJECT

public:
    explicit StoragePanel(QWidget* parent = nullptr);
    ~StoragePanel() override;

signals:
    void testStartRequested(const QString& mode, bool directIO, int queueDepth);
    void testStopRequested();

private slots:
    void onStartStopClicked();
    void updateMonitoring();

private:
    void setupUi();
    QFrame* createSettingsSection();
    QFrame* createMonitoringSection();

    // Storage Engine
    std::unique_ptr<StorageEngine> engine_;

    // Settings
    QComboBox* modeCombo_ = nullptr;
    QCheckBox* directIOCheck_ = nullptr;
    QSpinBox* queueDepthSpin_ = nullptr;
    QComboBox* blockSizeCombo_ = nullptr;
    QPushButton* startStopBtn_ = nullptr;

    // Monitoring
    RealtimeChart* iopsChart_ = nullptr;
    RealtimeChart* throughputChart_ = nullptr;
    QLabel* iopsLabel_ = nullptr;
    QLabel* throughputLabel_ = nullptr;
    QLabel* latencyLabel_ = nullptr;

    bool isRunning_ = false;
    QTimer* monitorTimer_ = nullptr;
};

}} // namespace occt::gui
