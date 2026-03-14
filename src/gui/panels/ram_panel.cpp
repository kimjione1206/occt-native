#include "ram_panel.h"
#include "panel_styles.h"
#include "../widgets/realtime_chart.h"
#include "../../engines/ram_engine.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace occt { namespace gui {

RamPanel::RamPanel(QWidget* parent)
    : QWidget(parent)
    , engine_(std::make_unique<RamEngine>())
{
    setupUi();

    monitorTimer_ = new QTimer(this);
    connect(monitorTimer_, &QTimer::timeout, this, &RamPanel::updateMonitoring);
}

RamPanel::~RamPanel()
{
    if (engine_ && engine_->is_running()) {
        engine_->stop();
    }
}

IEngine* RamPanel::engine() const
{
    return engine_.get();
}

void RamPanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(createSettingsSection());
    mainLayout->addWidget(createMonitoringSection(), 1);
}

QFrame* RamPanel::createSettingsSection()
{
    auto* frame = new QFrame();
    frame->setFixedWidth(320);
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("RAM Test", frame);
    title->setStyleSheet(styles::kPanelTitle);
    layout->addWidget(title);

    auto* subtitle = new QLabel("Memory integrity and bandwidth tests", frame);
    subtitle->setStyleSheet(styles::kPanelSubtitle);
    layout->addWidget(subtitle);

    layout->addSpacing(10);

    // Pattern selection
    auto* patternLabel = new QLabel("Test Pattern", frame);
    patternLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(patternLabel);

    patternCombo_ = new QComboBox(frame);
    patternCombo_->setAccessibleDescription("ram_pattern_combo");
    patternCombo_->addItems({
        "March C-",
        "Walking Ones",
        "Walking Zeros",
        "Checkerboard",
        "Random",
        "Bandwidth"
    });
    layout->addWidget(patternCombo_);

    // Memory allocation slider
    auto* memLabel = new QLabel("Memory Allocation", frame);
    memLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(memLabel);

    auto* memRow = new QHBoxLayout();
    memSlider_ = new QSlider(Qt::Horizontal, frame);
    memSlider_->setAccessibleDescription("ram_mem_slider");
    memSlider_->setRange(10, 90);
    memSlider_->setValue(50);
    connect(memSlider_, &QSlider::valueChanged, this, &RamPanel::onMemSliderChanged);

    memValueLabel_ = new QLabel("50%", frame);
    memValueLabel_->setAccessibleDescription("ram_mem_value");
    memValueLabel_->setFixedWidth(40);
    memValueLabel_->setAlignment(Qt::AlignCenter);
    memValueLabel_->setStyleSheet(styles::kStatusIdle);

    memRow->addWidget(memSlider_, 1);
    memRow->addWidget(memValueLabel_);
    layout->addLayout(memRow);

    auto* memWarning = new QLabel("Warning: High values may cause system instability", frame);
    memWarning->setStyleSheet("color: #E74C3C; font-size: 10px; border: none; background: transparent;");
    memWarning->setWordWrap(true);
    layout->addWidget(memWarning);

    // Passes
    auto* passesLabel = new QLabel("Number of Passes", frame);
    passesLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(passesLabel);

    passesSpinBox_ = new QSpinBox(frame);
    passesSpinBox_->setAccessibleDescription("ram_passes_spin");
    passesSpinBox_->setRange(1, 100);
    passesSpinBox_->setValue(3);
    layout->addWidget(passesSpinBox_);

    layout->addSpacing(20);

    startStopBtn_ = new QPushButton("Start Test", frame);
    startStopBtn_->setAccessibleDescription("ram_start_stop_btn");
    startStopBtn_->setCursor(Qt::PointingHandCursor);
    startStopBtn_->setFixedHeight(48);
    startStopBtn_->setStyleSheet(
        styles::kStartButton
    );
    connect(startStopBtn_, &QPushButton::clicked, this, &RamPanel::onStartStopClicked);
    layout->addWidget(startStopBtn_);

    layout->addStretch();

    return frame;
}

QFrame* RamPanel::createMonitoringSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("RAM Test Monitoring", frame);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    // Metrics row
    auto* metricsLayout = new QHBoxLayout();
    metricsLayout->setSpacing(16);

    auto createMetric = [frame](const QString& label, const QString& val) -> QLabel* {
        auto* card = new QFrame(frame);
        card->setStyleSheet(styles::kCardFrame);
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(12, 8, 12, 8);
        auto* lbl = new QLabel(label, card);
        lbl->setStyleSheet(styles::kSmallInfo);
        auto* v = new QLabel(val, card);
        v->setStyleSheet(styles::kPanelTitle);
        cl->addWidget(lbl);
        cl->addWidget(v);
        return v;
    };

    bandwidthLabel_ = createMetric("Bandwidth", "-- GB/s");
    bandwidthLabel_->setAccessibleDescription("ram_bandwidth_value");
    metricsLayout->addWidget(bandwidthLabel_->parentWidget());
    errorsLabel_ = createMetric("Errors Found", "0");
    errorsLabel_->setAccessibleDescription("ram_errors_value");
    metricsLayout->addWidget(errorsLabel_->parentWidget());
    progressLabel_ = createMetric("Pass", "0 / 0");
    progressLabel_->setAccessibleDescription("ram_progress_label");
    metricsLayout->addWidget(progressLabel_->parentWidget());

    layout->addLayout(metricsLayout);

    // Test progress
    auto* progressTitle = new QLabel("Test Progress", frame);
    progressTitle->setStyleSheet(styles::kPanelSubtitle);
    layout->addWidget(progressTitle);

    testProgress_ = new QProgressBar(frame);
    testProgress_->setAccessibleDescription("ram_progress_bar");
    testProgress_->setRange(0, 100);
    testProgress_->setValue(0);
    testProgress_->setTextVisible(true);
    testProgress_->setFixedHeight(24);
    layout->addWidget(testProgress_);

    // Bandwidth chart
    bandwidthChart_ = new RealtimeChart(frame);
    bandwidthChart_->setTitle("Memory Bandwidth");
    bandwidthChart_->setUnit("GB/s");
    bandwidthChart_->setLineColor(QColor(142, 68, 173));
    bandwidthChart_->setMinimumHeight(200);
    layout->addWidget(bandwidthChart_, 1);

    return frame;
}

void RamPanel::onStartStopClicked()
{
    isRunning_ = !isRunning_;

    if (isRunning_) {
        startStopBtn_->setText("Stop Test");
        startStopBtn_->setStyleSheet(
            styles::kStopButton
        );

        // Map combo index to RamPattern
        int idx = patternCombo_->currentIndex();
        RamPattern pattern;
        switch (idx) {
            case 0: pattern = RamPattern::MARCH_C_MINUS; break;
            case 1: pattern = RamPattern::WALKING_ONES; break;
            case 2: pattern = RamPattern::WALKING_ZEROS; break;
            case 3: pattern = RamPattern::CHECKERBOARD; break;
            case 4: pattern = RamPattern::RANDOM; break;
            case 5: pattern = RamPattern::BANDWIDTH; break;
            default: pattern = RamPattern::MARCH_C_MINUS; break;
        }

        double memory_pct = memSlider_->value() / 100.0;
        int passes = passesSpinBox_->value();

        engine_->start(pattern, memory_pct, passes);
        monitorTimer_->start(500);

        emit testStartRequested(patternCombo_->currentText(), memSlider_->value(), passes);
    } else {
        startStopBtn_->setText("Start Test");
        startStopBtn_->setStyleSheet(styles::kStartButton);

        engine_->stop();
        monitorTimer_->stop();
        emit testStopRequested();
    }
}

void RamPanel::onMemSliderChanged(int value)
{
    memValueLabel_->setText(QString::number(value) + "%");
}

void RamPanel::updateMonitoring()
{
    if (!engine_ || !engine_->is_running()) {
        // Engine stopped on its own (all passes completed)
        if (isRunning_) {
            isRunning_ = false;
            startStopBtn_->setText("Start Test");
            startStopBtn_->setStyleSheet(styles::kStartButton);
            monitorTimer_->stop();
        }
        return;
    }

    auto m = engine_->get_metrics();

    // Update bandwidth (convert MB/s to GB/s for display)
    double bw_gbs = m.bandwidth_mbs / 1000.0;
    bandwidthLabel_->setText(QString::number(bw_gbs, 'f', 2) + " GB/s");
    bandwidthChart_->addPoint(bw_gbs);

    // Update errors
    errorsLabel_->setText(QString::number(m.errors_found));

    // Update progress
    int passes = passesSpinBox_->value();
    int currentPass = static_cast<int>(m.progress_pct / 100.0 * passes) + 1;
    if (currentPass > passes) currentPass = passes;
    progressLabel_->setText(QString("%1 / %2").arg(currentPass).arg(passes));
    testProgress_->setValue(static_cast<int>(m.progress_pct));
}

}} // namespace occt::gui
