#include "psu_panel.h"
#include "../widgets/realtime_chart.h"
#include "../../engines/psu_engine.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace occt { namespace gui {

PsuPanel::PsuPanel(QWidget* parent)
    : QWidget(parent)
    , engine_(std::make_unique<PsuEngine>())
{
    setupUi();

    monitorTimer_ = new QTimer(this);
    connect(monitorTimer_, &QTimer::timeout, this, &PsuPanel::updateMonitoring);
}

PsuPanel::~PsuPanel()
{
    if (engine_ && engine_->is_running()) {
        engine_->stop();
    }
}

void PsuPanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(createSettingsSection());
    mainLayout->addWidget(createMonitoringSection(), 1);
}

QFrame* PsuPanel::createSettingsSection()
{
    auto* frame = new QFrame();
    frame->setFixedWidth(320);
    frame->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("PSU Stress Test", frame);
    title->setStyleSheet("color: #F0F6FC; font-size: 18px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(title);

    auto* subtitle = new QLabel("Combined CPU + GPU load to stress PSU", frame);
    subtitle->setStyleSheet("color: #8B949E; font-size: 12px; border: none; background: transparent;");
    layout->addWidget(subtitle);

    layout->addSpacing(10);

    // Load pattern
    auto* patternLabel = new QLabel("Load Pattern", frame);
    patternLabel->setStyleSheet("color: #C9D1D9; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(patternLabel);

    patternCombo_ = new QComboBox(frame);
    patternCombo_->addItems({
        "Steady (Max Load)",
        "Spike (5s cycles)",
        "Ramp (0% -> 100%)"
    });
    layout->addWidget(patternCombo_);

    // Duration
    auto* durationLabel = new QLabel("Duration", frame);
    durationLabel->setStyleSheet("color: #C9D1D9; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(durationLabel);

    durationCombo_ = new QComboBox(frame);
    durationCombo_->addItem("1 minute", 60);
    durationCombo_->addItem("5 minutes", 300);
    durationCombo_->addItem("10 minutes", 600);
    durationCombo_->addItem("30 minutes", 1800);
    durationCombo_->addItem("1 hour", 3600);
    durationCombo_->addItem("Unlimited", 0);
    durationCombo_->setCurrentIndex(1);
    layout->addWidget(durationCombo_);

    layout->addSpacing(10);

    // Use all GPUs checkbox
    useAllGpusCheck_ = new QCheckBox("Use All GPUs", frame);
    useAllGpusCheck_->setStyleSheet("color: #C9D1D9; border: none; background: transparent;");
    layout->addWidget(useAllGpusCheck_);

    layout->addSpacing(20);

    startStopBtn_ = new QPushButton("Start Test", frame);
    startStopBtn_->setCursor(Qt::PointingHandCursor);
    startStopBtn_->setFixedHeight(48);
    startStopBtn_->setStyleSheet(
        "QPushButton { background-color: #E67E22; color: white; border: none; "
        "border-radius: 6px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #F39C12; }"
    );
    connect(startStopBtn_, &QPushButton::clicked, this, &PsuPanel::onStartStopClicked);
    layout->addWidget(startStopBtn_);

    layout->addStretch();

    return frame;
}

QFrame* PsuPanel::createMonitoringSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("Power Monitoring", frame);
    title->setStyleSheet("color: #F0F6FC; font-size: 16px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(title);

    // Metrics cards row
    auto* metricsLayout = new QHBoxLayout();
    metricsLayout->setSpacing(12);

    auto createMetricCard = [frame](const QString& label, const QString& val, const QString& color) -> QLabel* {
        auto* card = new QFrame(frame);
        card->setStyleSheet("QFrame { background-color: #0D1117; border: 1px solid #30363D; border-radius: 6px; }");
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(12, 10, 12, 10);
        cl->setSpacing(4);
        auto* lbl = new QLabel(label, card);
        lbl->setStyleSheet("color: #8B949E; font-size: 11px; border: none; background: transparent;");
        lbl->setAlignment(Qt::AlignCenter);
        auto* v = new QLabel(val, card);
        v->setStyleSheet(QString("color: %1; font-size: 20px; font-weight: bold; border: none; background: transparent;").arg(color));
        v->setAlignment(Qt::AlignCenter);
        cl->addWidget(lbl);
        cl->addWidget(v);
        return v;
    };

    totalPowerLabel_ = createMetricCard("Total Power", "0.0 W", "#E74C3C");
    metricsLayout->addWidget(totalPowerLabel_->parentWidget());
    cpuPowerLabel_ = createMetricCard("CPU Power", "0.0 W", "#3498DB");
    metricsLayout->addWidget(cpuPowerLabel_->parentWidget());
    gpuPowerLabel_ = createMetricCard("GPU Power", "0.0 W", "#2ECC71");
    metricsLayout->addWidget(gpuPowerLabel_->parentWidget());
    cpuErrorsLabel_ = createMetricCard("CPU Errors", "0", "#F39C12");
    metricsLayout->addWidget(cpuErrorsLabel_->parentWidget());
    gpuErrorsLabel_ = createMetricCard("GPU Errors", "0", "#F39C12");
    metricsLayout->addWidget(gpuErrorsLabel_->parentWidget());

    layout->addLayout(metricsLayout);

    // Status row
    auto* statusLayout = new QHBoxLayout();
    statusLayout->setSpacing(16);

    auto createStatusLabel = [frame](const QString& label) -> QLabel* {
        auto* lbl = new QLabel(label, frame);
        lbl->setStyleSheet("color: #8B949E; font-size: 12px; border: none; background: transparent;");
        return lbl;
    };

    cpuStatusLabel_ = createStatusLabel("CPU: Idle");
    statusLayout->addWidget(cpuStatusLabel_);
    gpuStatusLabel_ = createStatusLabel("GPU: Idle");
    statusLayout->addWidget(gpuStatusLabel_);
    elapsedLabel_ = createStatusLabel("Elapsed: 0s");
    statusLayout->addWidget(elapsedLabel_);
    statusLayout->addStretch();

    layout->addLayout(statusLayout);

    // Power chart
    powerChart_ = new RealtimeChart(frame);
    powerChart_->setTitle("Power Consumption Over Time");
    powerChart_->setUnit("Watts");
    powerChart_->setLineColor(QColor(231, 76, 60));
    powerChart_->setMinimumHeight(300);
    layout->addWidget(powerChart_, 1);

    return frame;
}

void PsuPanel::onStartStopClicked()
{
    isRunning_ = !isRunning_;

    if (isRunning_) {
        startStopBtn_->setText("Stop Test");
        startStopBtn_->setStyleSheet(
            "QPushButton { background-color: #C0392B; color: white; border: none; "
            "border-radius: 6px; font-size: 16px; font-weight: bold; }"
            "QPushButton:hover { background-color: #E74C3C; }"
        );

        // Map combo index to PsuLoadPattern
        int patIdx = patternCombo_->currentIndex();
        PsuLoadPattern pattern;
        switch (patIdx) {
            case 0: pattern = PsuLoadPattern::STEADY; break;
            case 1: pattern = PsuLoadPattern::SPIKE;  break;
            case 2: pattern = PsuLoadPattern::RAMP;   break;
            default: pattern = PsuLoadPattern::STEADY; break;
        }

        int durationSec = durationCombo_->currentData().toInt();
        bool useAllGpus = useAllGpusCheck_->isChecked();

        engine_->set_use_all_gpus(useAllGpus);
        engine_->start(pattern, durationSec);

        monitorTimer_->start(500);

        emit testStartRequested(patternCombo_->currentText(), durationSec, useAllGpus);
    } else {
        startStopBtn_->setText("Start Test");
        startStopBtn_->setStyleSheet(
            "QPushButton { background-color: #E67E22; color: white; border: none; "
            "border-radius: 6px; font-size: 16px; font-weight: bold; }"
            "QPushButton:hover { background-color: #F39C12; }"
        );

        engine_->stop();
        monitorTimer_->stop();

        emit testStopRequested();
    }
}

void PsuPanel::updateMonitoring()
{
    if (!engine_ || !engine_->is_running()) {
        // Engine stopped on its own (duration reached)
        if (isRunning_) {
            isRunning_ = false;
            startStopBtn_->setText("Start Test");
            startStopBtn_->setStyleSheet(
                "QPushButton { background-color: #E67E22; color: white; border: none; "
                "border-radius: 6px; font-size: 16px; font-weight: bold; }"
                "QPushButton:hover { background-color: #F39C12; }"
            );
            monitorTimer_->stop();
        }
        return;
    }

    auto m = engine_->get_metrics();

    // Update power labels
    totalPowerLabel_->setText(QString::number(m.total_power_watts, 'f', 1) + " W");
    cpuPowerLabel_->setText(QString::number(m.cpu_power_watts, 'f', 1) + " W");
    gpuPowerLabel_->setText(QString::number(m.gpu_power_watts, 'f', 1) + " W");

    // Update error labels
    cpuErrorsLabel_->setText(QString::number(m.errors_cpu));
    gpuErrorsLabel_->setText(QString::number(m.errors_gpu));

    // Update status labels
    cpuStatusLabel_->setText(m.cpu_running ? "CPU: Running" : "CPU: Idle");
    gpuStatusLabel_->setText(m.gpu_running ? "GPU: Running" : "GPU: Idle");

    int elapsed = static_cast<int>(m.elapsed_secs);
    int mins = elapsed / 60;
    int secs = elapsed % 60;
    if (mins > 0)
        elapsedLabel_->setText(QString("Elapsed: %1m %2s").arg(mins).arg(secs));
    else
        elapsedLabel_->setText(QString("Elapsed: %1s").arg(secs));

    // Update chart with total power
    powerChart_->addPoint(m.total_power_watts);
}

}} // namespace occt::gui
