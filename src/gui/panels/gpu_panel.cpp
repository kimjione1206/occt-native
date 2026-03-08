#include "gpu_panel.h"
#include "../widgets/realtime_chart.h"
#include "../widgets/circular_gauge.h"
#include "../../engines/gpu_engine.h"

#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>

namespace occt { namespace gui {

GpuPanel::GpuPanel(QWidget* parent)
    : QWidget(parent)
    , engine_(std::make_unique<GpuEngine>())
{
    bool gpuOk = engine_->initialize();
    setupUi();
    if (!gpuOk) {
        startStopBtn_->setEnabled(false);
        startStopBtn_->setText("GPU Not Available");
        startStopBtn_->setStyleSheet("background-color: #555; color: #999;");
    }

    monitorTimer_ = new QTimer(this);
    connect(monitorTimer_, &QTimer::timeout, this, &GpuPanel::updateMonitoring);
}

GpuPanel::~GpuPanel()
{
    if (engine_ && engine_->is_running()) {
        engine_->stop();
    }
}

void GpuPanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(createSettingsSection());
    mainLayout->addWidget(createMonitoringSection(), 1);
}

QFrame* GpuPanel::createSettingsSection()
{
    auto* frame = new QFrame();
    frame->setFixedWidth(320);
    frame->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("GPU Stress Test", frame);
    title->setStyleSheet("color: #F0F6FC; font-size: 18px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(title);

    auto* subtitle = new QLabel("Configure GPU compute & 3D stress tests", frame);
    subtitle->setStyleSheet("color: #8B949E; font-size: 12px; border: none; background: transparent;");
    layout->addWidget(subtitle);

    layout->addSpacing(10);

    // GPU selection
    auto* gpuLabel = new QLabel("GPU Device", frame);
    gpuLabel->setStyleSheet("color: #C9D1D9; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(gpuLabel);

    gpuSelectCombo_ = new QComboBox(frame);
    gpuSelectCombo_->addItem("Auto-detect (default GPU)");
    layout->addWidget(gpuSelectCombo_);

    // Backend selection
    auto* backendLabel = new QLabel("Backend", frame);
    backendLabel->setStyleSheet("color: #C9D1D9; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(backendLabel);

    backendCombo_ = new QComboBox(frame);
    backendCombo_->addItems({"OpenCL", "Vulkan"});
    layout->addWidget(backendCombo_);

    // Mode
    auto* modeLabel = new QLabel("Test Mode", frame);
    modeLabel->setStyleSheet("color: #C9D1D9; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(modeLabel);

    modeCombo_ = new QComboBox(frame);
    modeCombo_->addItems({
        "Matrix FP32",
        "Matrix FP64",
        "FMA Stress",
        "Trigonometric",
        "VRAM Test",
        "Mixed",
        "Vulkan 3D",
        "Vulkan Adaptive"
    });
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GpuPanel::onModeChanged);
    layout->addWidget(modeCombo_);

    // Vulkan-specific settings container
    vulkanSettingsWidget_ = new QWidget(frame);
    auto* vulkanLayout = new QVBoxLayout(vulkanSettingsWidget_);
    vulkanLayout->setContentsMargins(0, 0, 0, 0);
    vulkanLayout->setSpacing(8);

    // Shader complexity slider
    auto* shaderLabel = new QLabel("Shader Complexity", vulkanSettingsWidget_);
    shaderLabel->setStyleSheet("color: #C9D1D9; font-weight: bold; border: none; background: transparent;");
    vulkanLayout->addWidget(shaderLabel);

    auto* sliderLayout = new QHBoxLayout();
    shaderComplexitySlider_ = new QSlider(Qt::Horizontal, vulkanSettingsWidget_);
    shaderComplexitySlider_->setRange(1, 5);
    shaderComplexitySlider_->setValue(1);
    shaderComplexitySlider_->setTickPosition(QSlider::TicksBelow);
    shaderComplexitySlider_->setTickInterval(1);

    shaderComplexityLabel_ = new QLabel("Level 1", vulkanSettingsWidget_);
    shaderComplexityLabel_->setStyleSheet("color: #58A6FF; font-weight: bold; border: none; background: transparent;");
    shaderComplexityLabel_->setFixedWidth(60);

    connect(shaderComplexitySlider_, &QSlider::valueChanged, this, [this](int val) {
        shaderComplexityLabel_->setText(QString("Level %1").arg(val));
    });

    sliderLayout->addWidget(shaderComplexitySlider_);
    sliderLayout->addWidget(shaderComplexityLabel_);
    vulkanLayout->addLayout(sliderLayout);

    // Adaptive mode
    auto* adaptiveLabel = new QLabel("Adaptive Mode", vulkanSettingsWidget_);
    adaptiveLabel->setStyleSheet("color: #C9D1D9; font-weight: bold; border: none; background: transparent;");
    vulkanLayout->addWidget(adaptiveLabel);

    adaptiveModeCombo_ = new QComboBox(vulkanSettingsWidget_);
    adaptiveModeCombo_->addItems({"Variable (+5%/20s)", "Switch (20%/80%)"});
    vulkanLayout->addWidget(adaptiveModeCombo_);

    // Multi-GPU checkbox
    multiGpuCheck_ = new QCheckBox("Multi-GPU (all detected GPUs)", vulkanSettingsWidget_);
    multiGpuCheck_->setStyleSheet("color: #C9D1D9; border: none; background: transparent;");
    vulkanLayout->addWidget(multiGpuCheck_);

    vulkanSettingsWidget_->setVisible(false);
    layout->addWidget(vulkanSettingsWidget_);

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

    layout->addSpacing(20);

    startStopBtn_ = new QPushButton("Start Test", frame);
    startStopBtn_->setCursor(Qt::PointingHandCursor);
    startStopBtn_->setFixedHeight(48);
    startStopBtn_->setStyleSheet(
        "QPushButton { background-color: #27AE60; color: white; border: none; "
        "border-radius: 6px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #2ECC71; }"
    );
    connect(startStopBtn_, &QPushButton::clicked, this, &GpuPanel::onStartStopClicked);
    layout->addWidget(startStopBtn_);

    layout->addStretch();

    return frame;
}

QFrame* GpuPanel::createMonitoringSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("GPU Monitoring", frame);
    title->setStyleSheet("color: #F0F6FC; font-size: 16px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(title);

    // Top metrics row
    auto* metricsLayout = new QHBoxLayout();
    metricsLayout->setSpacing(16);

    // GPU usage gauge
    gpuUsageGauge_ = new CircularGauge(frame);
    gpuUsageGauge_->setLabel("GPU Usage");
    gpuUsageGauge_->setFixedSize(140, 140);
    metricsLayout->addWidget(gpuUsageGauge_);

    // Metrics cards
    auto* metricsGrid = new QVBoxLayout();
    metricsGrid->setSpacing(8);

    auto createMetricCard = [frame](const QString& label, const QString& val) -> QLabel* {
        auto* card = new QFrame(frame);
        card->setStyleSheet("QFrame { background-color: #0D1117; border: 1px solid #30363D; border-radius: 6px; }");
        auto* cl = new QHBoxLayout(card);
        cl->setContentsMargins(12, 8, 12, 8);
        auto* lbl = new QLabel(label, card);
        lbl->setStyleSheet("color: #8B949E; font-size: 12px; border: none; background: transparent;");
        auto* v = new QLabel(val, card);
        v->setStyleSheet("color: #F0F6FC; font-size: 16px; font-weight: bold; border: none; background: transparent;");
        v->setAlignment(Qt::AlignRight);
        cl->addWidget(lbl);
        cl->addStretch();
        cl->addWidget(v);
        return v;
    };

    gflopsLabel_ = createMetricCard("GFLOPS", "0.00");
    metricsGrid->addWidget(gflopsLabel_->parentWidget());
    tempLabel_ = createMetricCard("Temperature", "-- C");
    metricsGrid->addWidget(tempLabel_->parentWidget());
    vramLabel_ = createMetricCard("VRAM Used", "-- / -- MB");
    metricsGrid->addWidget(vramLabel_->parentWidget());
    fpsLabel_ = createMetricCard("FPS", "--");
    metricsGrid->addWidget(fpsLabel_->parentWidget());
    artifactLabel_ = createMetricCard("Artifacts", "0");
    metricsGrid->addWidget(artifactLabel_->parentWidget());

    metricsLayout->addLayout(metricsGrid, 1);
    layout->addLayout(metricsLayout);

    // VRAM progress bar
    auto* vramTitle = new QLabel("VRAM Usage", frame);
    vramTitle->setStyleSheet("color: #8B949E; font-size: 12px; border: none; background: transparent;");
    layout->addWidget(vramTitle);

    vramBar_ = new QProgressBar(frame);
    vramBar_->setRange(0, 100);
    vramBar_->setValue(0);
    vramBar_->setTextVisible(true);
    vramBar_->setFixedHeight(20);
    layout->addWidget(vramBar_);

    // GFLOPS chart
    gflopsChart_ = new RealtimeChart(frame);
    gflopsChart_->setTitle("GPU Performance Over Time");
    gflopsChart_->setUnit("GFLOPS");
    gflopsChart_->setLineColor(QColor(41, 128, 185));
    gflopsChart_->setMinimumHeight(200);
    layout->addWidget(gflopsChart_, 1);

    return frame;
}

void GpuPanel::onModeChanged(int index)
{
    // Show Vulkan settings when Vulkan modes are selected (indices 6 and 7)
    bool isVulkanMode = (index >= 6);
    vulkanSettingsWidget_->setVisible(isVulkanMode);
}

void GpuPanel::onStartStopClicked()
{
    isRunning_ = !isRunning_;

    if (isRunning_) {
        startStopBtn_->setText("Stop Test");
        startStopBtn_->setStyleSheet(
            "QPushButton { background-color: #C0392B; color: white; border: none; "
            "border-radius: 6px; font-size: 16px; font-weight: bold; }"
            "QPushButton:hover { background-color: #E74C3C; }"
        );

        // Map combo index to GpuStressMode
        int modeIdx = modeCombo_->currentIndex();
        GpuStressMode mode;
        switch (modeIdx) {
            case 0: mode = GpuStressMode::MATRIX_MUL; break;
            case 1: mode = GpuStressMode::MATRIX_MUL_FP64; break;
            case 2: mode = GpuStressMode::FMA_STRESS; break;
            case 3: mode = GpuStressMode::TRIG_STRESS; break;
            case 4: mode = GpuStressMode::VRAM_TEST; break;
            case 5: mode = GpuStressMode::MIXED; break;
            case 6: mode = GpuStressMode::VULKAN_3D; break;
            case 7: mode = GpuStressMode::VULKAN_ADAPTIVE; break;
            default: mode = GpuStressMode::MATRIX_MUL; break;
        }

        // Configure Vulkan-specific settings if applicable
        if (mode == GpuStressMode::VULKAN_3D || mode == GpuStressMode::VULKAN_ADAPTIVE) {
            engine_->set_shader_complexity(shaderComplexitySlider_->value());
            if (mode == GpuStressMode::VULKAN_ADAPTIVE) {
                AdaptiveMode am = (adaptiveModeCombo_->currentIndex() == 1)
                    ? AdaptiveMode::SWITCH : AdaptiveMode::VARIABLE;
                engine_->set_adaptive_mode(am);
            }
        }

        int durationSec = durationCombo_->currentData().toInt();

        // Start engine
        if (!engine_->start(mode, durationSec)) {
            QMessageBox::warning(this, "GPU Test Error",
                QString::fromStdString(engine_->last_error()));
            isRunning_ = false;
            startStopBtn_->setText("Start Test");
            startStopBtn_->setStyleSheet(
                "QPushButton { background-color: #27AE60; color: white; border: none; "
                "border-radius: 6px; font-size: 16px; font-weight: bold; }"
                "QPushButton:hover { background-color: #2ECC71; }"
            );
            return;
        }

        // Start monitoring timer
        monitorTimer_->start(500);

        emit testStartRequested(gpuSelectCombo_->currentText(), modeCombo_->currentText(), durationSec);
    } else {
        startStopBtn_->setText("Start Test");
        startStopBtn_->setStyleSheet(
            "QPushButton { background-color: #27AE60; color: white; border: none; "
            "border-radius: 6px; font-size: 16px; font-weight: bold; }"
            "QPushButton:hover { background-color: #2ECC71; }"
        );

        // Stop engine
        engine_->stop();
        monitorTimer_->stop();
        emit testStopRequested();
    }
}

void GpuPanel::updateMonitoring()
{
    if (!engine_ || !engine_->is_running()) {
        // Engine stopped on its own (duration reached)
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

    // Update GFLOPS
    gflopsLabel_->setText(QString::number(m.gflops, 'f', 2));
    gflopsChart_->addPoint(m.gflops);

    // Update GPU usage gauge
    gpuUsageGauge_->setValue(m.gpu_usage_pct);

    // Update temperature
    if (m.temperature > 0)
        tempLabel_->setText(QString::number(m.temperature, 'f', 1) + " C");

    // Update VRAM
    vramLabel_->setText(QString::number(m.vram_usage_pct, 'f', 1) + "%");
    vramBar_->setValue(static_cast<int>(m.vram_usage_pct));

    // Update FPS
    if (m.fps > 0)
        fpsLabel_->setText(QString::number(m.fps, 'f', 1));

    // Update artifact count
    artifactLabel_->setText(QString::number(m.artifact_count));
}

}} // namespace occt::gui
