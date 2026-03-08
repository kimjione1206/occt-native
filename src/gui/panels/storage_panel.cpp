#include "storage_panel.h"
#include "../widgets/realtime_chart.h"
#include "../../engines/storage_engine.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStandardPaths>
#include <QDir>

namespace occt { namespace gui {

StoragePanel::StoragePanel(QWidget* parent)
    : QWidget(parent)
    , engine_(std::make_unique<StorageEngine>())
{
    setupUi();

    monitorTimer_ = new QTimer(this);
    connect(monitorTimer_, &QTimer::timeout, this, &StoragePanel::updateMonitoring);
}

StoragePanel::~StoragePanel()
{
    if (engine_ && engine_->is_running()) {
        engine_->stop();
    }
}

void StoragePanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(createSettingsSection());
    mainLayout->addWidget(createMonitoringSection(), 1);
}

QFrame* StoragePanel::createSettingsSection()
{
    auto* frame = new QFrame();
    frame->setFixedWidth(320);
    frame->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("Storage Test", frame);
    title->setStyleSheet("color: #F0F6FC; font-size: 18px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(title);

    auto* subtitle = new QLabel("Disk I/O performance testing", frame);
    subtitle->setStyleSheet("color: #8B949E; font-size: 12px; border: none; background: transparent;");
    layout->addWidget(subtitle);

    layout->addSpacing(10);

    // Mode
    auto* modeLabel = new QLabel("Test Mode", frame);
    modeLabel->setStyleSheet("color: #C9D1D9; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(modeLabel);

    modeCombo_ = new QComboBox(frame);
    modeCombo_->addItems({
        "Sequential Read",
        "Sequential Write",
        "Random Read",
        "Random Write",
        "Mixed Read/Write"
    });
    layout->addWidget(modeCombo_);

    // Block size
    auto* blockLabel = new QLabel("Block Size", frame);
    blockLabel->setStyleSheet("color: #C9D1D9; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(blockLabel);

    blockSizeCombo_ = new QComboBox(frame);
    blockSizeCombo_->addItems({"4 KB", "8 KB", "64 KB", "128 KB", "1 MB", "4 MB"});
    blockSizeCombo_->setCurrentIndex(0);
    layout->addWidget(blockSizeCombo_);

    // Direct I/O
    directIOCheck_ = new QCheckBox("Direct I/O (bypass cache)", frame);
    directIOCheck_->setChecked(true);
    layout->addWidget(directIOCheck_);

    // Queue Depth
    auto* qdLabel = new QLabel("Queue Depth", frame);
    qdLabel->setStyleSheet("color: #C9D1D9; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(qdLabel);

    queueDepthSpin_ = new QSpinBox(frame);
    queueDepthSpin_->setRange(1, 256);
    queueDepthSpin_->setValue(32);
    layout->addWidget(queueDepthSpin_);

    layout->addSpacing(20);

    startStopBtn_ = new QPushButton("Start Test", frame);
    startStopBtn_->setCursor(Qt::PointingHandCursor);
    startStopBtn_->setFixedHeight(48);
    startStopBtn_->setStyleSheet(
        "QPushButton { background-color: #27AE60; color: white; border: none; "
        "border-radius: 6px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #2ECC71; }"
    );
    connect(startStopBtn_, &QPushButton::clicked, this, &StoragePanel::onStartStopClicked);
    layout->addWidget(startStopBtn_);

    layout->addStretch();

    return frame;
}

QFrame* StoragePanel::createMonitoringSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("Storage Monitoring", frame);
    title->setStyleSheet("color: #F0F6FC; font-size: 16px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(title);

    // Metrics row
    auto* metricsLayout = new QHBoxLayout();
    metricsLayout->setSpacing(16);

    auto createMetric = [frame](const QString& label, const QString& val) -> QLabel* {
        auto* card = new QFrame(frame);
        card->setStyleSheet("QFrame { background-color: #0D1117; border: 1px solid #30363D; border-radius: 6px; }");
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(12, 8, 12, 8);
        auto* lbl = new QLabel(label, card);
        lbl->setStyleSheet("color: #8B949E; font-size: 11px; border: none; background: transparent;");
        auto* v = new QLabel(val, card);
        v->setStyleSheet("color: #F0F6FC; font-size: 18px; font-weight: bold; border: none; background: transparent;");
        cl->addWidget(lbl);
        cl->addWidget(v);
        return v;
    };

    iopsLabel_ = createMetric("IOPS", "0");
    metricsLayout->addWidget(iopsLabel_->parentWidget());
    throughputLabel_ = createMetric("Throughput", "0 MB/s");
    metricsLayout->addWidget(throughputLabel_->parentWidget());
    latencyLabel_ = createMetric("Avg Latency", "-- ms");
    metricsLayout->addWidget(latencyLabel_->parentWidget());

    layout->addLayout(metricsLayout);

    // IOPS chart
    iopsChart_ = new RealtimeChart(frame);
    iopsChart_->setTitle("IOPS Over Time");
    iopsChart_->setUnit("IOPS");
    iopsChart_->setLineColor(QColor(230, 126, 34));
    iopsChart_->setMinimumHeight(180);
    layout->addWidget(iopsChart_, 1);

    // Throughput chart
    throughputChart_ = new RealtimeChart(frame);
    throughputChart_->setTitle("Throughput Over Time");
    throughputChart_->setUnit("MB/s");
    throughputChart_->setLineColor(QColor(46, 204, 113));
    throughputChart_->setMinimumHeight(180);
    layout->addWidget(throughputChart_, 1);

    return frame;
}

void StoragePanel::onStartStopClicked()
{
    isRunning_ = !isRunning_;

    if (isRunning_) {
        startStopBtn_->setText("Stop Test");
        startStopBtn_->setStyleSheet(
            "QPushButton { background-color: #C0392B; color: white; border: none; "
            "border-radius: 6px; font-size: 16px; font-weight: bold; }"
            "QPushButton:hover { background-color: #E74C3C; }"
        );

        // Map combo index to StorageMode
        int modeIdx = modeCombo_->currentIndex();
        StorageMode mode;
        switch (modeIdx) {
            case 0: mode = StorageMode::SEQ_READ; break;
            case 1: mode = StorageMode::SEQ_WRITE; break;
            case 2: mode = StorageMode::RAND_READ; break;
            case 3: mode = StorageMode::RAND_WRITE; break;
            case 4: mode = StorageMode::MIXED; break;
            default: mode = StorageMode::SEQ_READ; break;
        }

        int queueDepth = queueDepthSpin_->value();

        // Use temp directory for test file (engine expects a directory path)
        QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        std::string testPath = tempPath.toStdString();

        engine_->start(mode, testPath, 256, queueDepth);
        monitorTimer_->start(500);

        emit testStartRequested(modeCombo_->currentText(), directIOCheck_->isChecked(), queueDepthSpin_->value());
    } else {
        startStopBtn_->setText("Start Test");
        startStopBtn_->setStyleSheet(
            "QPushButton { background-color: #27AE60; color: white; border: none; "
            "border-radius: 6px; font-size: 16px; font-weight: bold; }"
            "QPushButton:hover { background-color: #2ECC71; }"
        );

        engine_->stop();
        monitorTimer_->stop();
        emit testStopRequested();
    }
}

void StoragePanel::updateMonitoring()
{
    if (!engine_ || !engine_->is_running()) {
        // Engine stopped on its own (test completed)
        if (isRunning_) {
            isRunning_ = false;
            startStopBtn_->setText("Start Test");
            startStopBtn_->setStyleSheet(
                "QPushButton { background-color: #27AE60; color: white; border: none; "
                "border-radius: 6px; font-size: 16px; font-weight: bold; }"
                "QPushButton:hover { background-color: #2ECC71; }"
            );
            monitorTimer_->stop();
        }
        return;
    }

    auto m = engine_->get_metrics();

    // Update IOPS
    iopsLabel_->setText(QString::number(m.iops, 'f', 0));
    iopsChart_->addPoint(m.iops);

    // Update throughput (use whichever is higher: read or write)
    double throughput = std::max(m.read_mbs, m.write_mbs);
    if (m.read_mbs > 0 && m.write_mbs > 0)
        throughput = m.read_mbs + m.write_mbs;
    throughputLabel_->setText(QString::number(throughput, 'f', 1) + " MB/s");
    throughputChart_->addPoint(throughput);

    // Update latency (convert from microseconds to milliseconds)
    double latency_ms = m.latency_us / 1000.0;
    latencyLabel_->setText(QString::number(latency_ms, 'f', 2) + " ms");
}

}} // namespace occt::gui
