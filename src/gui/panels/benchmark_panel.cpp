#include "benchmark_panel.h"
#include "../../engines/benchmark/cache_benchmark.h"
#include "../../engines/benchmark/memory_benchmark.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QTimer>
#include <QThread>
#include <QtConcurrent/QtConcurrent>

namespace occt { namespace gui {

BenchmarkPanel::BenchmarkPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void BenchmarkPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    mainLayout->addWidget(createControlSection());

    // Scrollable results area
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    auto* resultsWidget = new QWidget(scroll);
    auto* resultsLayout = new QVBoxLayout(resultsWidget);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    resultsLayout->setSpacing(16);

    resultsLayout->addWidget(createLatencySection());
    resultsLayout->addWidget(createBandwidthSection());
    resultsLayout->addWidget(createMemorySection());
    resultsLayout->addStretch();

    scroll->setWidget(resultsWidget);
    mainLayout->addWidget(scroll, 1);
}

QFrame* BenchmarkPanel::createControlSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QHBoxLayout(frame);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(16);

    auto* titleLayout = new QVBoxLayout();
    auto* title = new QLabel("Cache & Memory Benchmark", frame);
    title->setStyleSheet("color: #F0F6FC; font-size: 18px; font-weight: bold; border: none; background: transparent;");
    titleLayout->addWidget(title);

    statusLabel_ = new QLabel("Ready to run benchmark", frame);
    statusLabel_->setStyleSheet("color: #8B949E; font-size: 12px; border: none; background: transparent;");
    titleLayout->addWidget(statusLabel_);

    layout->addLayout(titleLayout, 1);

    runBtn_ = new QPushButton("Run Benchmark", frame);
    runBtn_->setCursor(Qt::PointingHandCursor);
    runBtn_->setFixedSize(180, 44);
    runBtn_->setStyleSheet(
        "QPushButton { background-color: #8E44AD; color: white; border: none; "
        "border-radius: 6px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #9B59B6; }"
    );
    connect(runBtn_, &QPushButton::clicked, this, &BenchmarkPanel::onRunClicked);
    layout->addWidget(runBtn_);

    return frame;
}

QFrame* BenchmarkPanel::createLatencySection()
{
    latencyFrame_ = new QFrame();
    latencyFrame_->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QVBoxLayout(latencyFrame_);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel("Cache Latency", latencyFrame_);
    title->setStyleSheet("color: #F0F6FC; font-size: 16px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(title);

    auto createLatRow = [this, layout](const QString& label, QLabel*& valLabel, QProgressBar*& bar) {
        auto* row = new QFrame(latencyFrame_);
        row->setStyleSheet("QFrame { border: none; background: transparent; }");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(10);

        auto* lbl = new QLabel(label, row);
        lbl->setFixedWidth(50);
        lbl->setStyleSheet("color: #C9D1D9; font-weight: bold; font-size: 13px; border: none; background: transparent;");
        rl->addWidget(lbl);

        bar = new QProgressBar(row);
        bar->setRange(0, 1000);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setFixedHeight(24);
        bar->setStyleSheet(
            "QProgressBar { background-color: #0D1117; border: 1px solid #30363D; border-radius: 4px; }"
            "QProgressBar::chunk { background-color: #3498DB; border-radius: 3px; }"
        );
        rl->addWidget(bar, 1);

        valLabel = new QLabel("--", row);
        valLabel->setFixedWidth(80);
        valLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valLabel->setStyleSheet("color: #F0F6FC; font-size: 13px; font-weight: bold; border: none; background: transparent;");
        rl->addWidget(valLabel);

        layout->addWidget(row);
    };

    createLatRow("L1", l1LatLabel_, l1LatBar_);
    createLatRow("L2", l2LatLabel_, l2LatBar_);
    createLatRow("L3", l3LatLabel_, l3LatBar_);
    createLatRow("DRAM", dramLatLabel_, dramLatBar_);

    return latencyFrame_;
}

QFrame* BenchmarkPanel::createBandwidthSection()
{
    bwFrame_ = new QFrame();
    bwFrame_->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QVBoxLayout(bwFrame_);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel("Cache Bandwidth (Read)", bwFrame_);
    title->setStyleSheet("color: #F0F6FC; font-size: 16px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(title);

    auto createBwRow = [this, layout](const QString& label, QLabel*& valLabel, QProgressBar*& bar) {
        auto* row = new QFrame(bwFrame_);
        row->setStyleSheet("QFrame { border: none; background: transparent; }");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(10);

        auto* lbl = new QLabel(label, row);
        lbl->setFixedWidth(50);
        lbl->setStyleSheet("color: #C9D1D9; font-weight: bold; font-size: 13px; border: none; background: transparent;");
        rl->addWidget(lbl);

        bar = new QProgressBar(row);
        bar->setRange(0, 1000);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setFixedHeight(24);
        bar->setStyleSheet(
            "QProgressBar { background-color: #0D1117; border: 1px solid #30363D; border-radius: 4px; }"
            "QProgressBar::chunk { background-color: #2ECC71; border-radius: 3px; }"
        );
        rl->addWidget(bar, 1);

        valLabel = new QLabel("--", row);
        valLabel->setFixedWidth(100);
        valLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valLabel->setStyleSheet("color: #F0F6FC; font-size: 13px; font-weight: bold; border: none; background: transparent;");
        rl->addWidget(valLabel);

        layout->addWidget(row);
    };

    createBwRow("L1", l1BwLabel_, l1BwBar_);
    createBwRow("L2", l2BwLabel_, l2BwBar_);
    createBwRow("L3", l3BwLabel_, l3BwBar_);
    createBwRow("DRAM", dramBwLabel_, dramBwBar_);

    return bwFrame_;
}

QFrame* BenchmarkPanel::createMemorySection()
{
    memFrame_ = new QFrame();
    memFrame_->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QVBoxLayout(memFrame_);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel("Memory Benchmark", memFrame_);
    title->setStyleSheet("color: #F0F6FC; font-size: 16px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(title);

    auto createResultRow = [this, layout](const QString& label) -> QLabel* {
        auto* row = new QFrame(memFrame_);
        row->setStyleSheet("QFrame { background-color: #0D1117; border: 1px solid #30363D; border-radius: 6px; }");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(12, 8, 12, 8);

        auto* lbl = new QLabel(label, row);
        lbl->setStyleSheet("color: #8B949E; font-size: 12px; border: none; background: transparent;");
        rl->addWidget(lbl);
        rl->addStretch();

        auto* val = new QLabel("--", row);
        val->setStyleSheet("color: #F0F6FC; font-size: 16px; font-weight: bold; border: none; background: transparent;");
        rl->addWidget(val);

        layout->addWidget(row);
        return val;
    };

    memReadLabel_ = createResultRow("Read Bandwidth");
    memWriteLabel_ = createResultRow("Write Bandwidth");
    memCopyLabel_ = createResultRow("Copy Bandwidth");
    memLatencyLabel_ = createResultRow("Random Latency");
    memChannelsLabel_ = createResultRow("Channels (est.)");

    return memFrame_;
}

void BenchmarkPanel::clearResults()
{
    l1LatLabel_->setText("--");
    l2LatLabel_->setText("--");
    l3LatLabel_->setText("--");
    dramLatLabel_->setText("--");
    l1LatBar_->setValue(0);
    l2LatBar_->setValue(0);
    l3LatBar_->setValue(0);
    dramLatBar_->setValue(0);

    l1BwLabel_->setText("--");
    l2BwLabel_->setText("--");
    l3BwLabel_->setText("--");
    dramBwLabel_->setText("--");
    l1BwBar_->setValue(0);
    l2BwBar_->setValue(0);
    l3BwBar_->setValue(0);
    dramBwBar_->setValue(0);

    memReadLabel_->setText("--");
    memWriteLabel_->setText("--");
    memCopyLabel_->setText("--");
    memLatencyLabel_->setText("--");
    memChannelsLabel_->setText("--");
}

void BenchmarkPanel::onRunClicked()
{
    if (isRunning_) return;

    isRunning_ = true;
    runBtn_->setEnabled(false);
    runBtn_->setText("Running...");
    runBtn_->setStyleSheet(
        "QPushButton { background-color: #5D3F6A; color: #999; border: none; "
        "border-radius: 6px; font-size: 14px; font-weight: bold; }"
    );

    statusLabel_->setText("Benchmark in progress...");
    clearResults();

    emit benchmarkStartRequested();

    // Run benchmarks in a background thread to keep UI responsive
    auto future = QtConcurrent::run([this]() {
        CacheBenchmark cacheBench;
        auto cacheRes = cacheBench.run();

        MemoryBenchmark memBench;
        auto memRes = memBench.run();

        // Post results back to the UI thread
        QMetaObject::invokeMethod(this, [this, cacheRes, memRes]() {
            onBenchmarkFinished(cacheRes, memRes);
        }, Qt::QueuedConnection);
    });
}

void BenchmarkPanel::onBenchmarkFinished(const CacheLatencyResult& c,
                                          const MemoryBenchmarkResult& m)
{
    // Cache latency results (bars scaled: max ~100 ns for L3/DRAM)
    auto setLat = [](QLabel* label, QProgressBar* bar, double ns) {
        label->setText(QString::number(ns, 'f', 1) + " ns");
        bar->setValue(static_cast<int>(qMin(ns * 10.0, 1000.0))); // scale: 100 ns = full
    };
    setLat(l1LatLabel_,   l1LatBar_,   c.l1_latency_ns);
    setLat(l2LatLabel_,   l2LatBar_,   c.l2_latency_ns);
    setLat(l3LatLabel_,   l3LatBar_,   c.l3_latency_ns);
    setLat(dramLatLabel_, dramLatBar_, c.dram_latency_ns);

    // Cache bandwidth results (bars scaled: max ~100 GB/s)
    auto setBw = [](QLabel* label, QProgressBar* bar, double gbs) {
        label->setText(QString::number(gbs, 'f', 1) + " GB/s");
        bar->setValue(static_cast<int>(qMin(gbs * 10.0, 1000.0))); // scale: 100 GB/s = full
    };
    setBw(l1BwLabel_,   l1BwBar_,   c.l1_bw_gbs);
    setBw(l2BwLabel_,   l2BwBar_,   c.l2_bw_gbs);
    setBw(l3BwLabel_,   l3BwBar_,   c.l3_bw_gbs);
    setBw(dramBwLabel_, dramBwBar_, c.dram_bw_gbs);

    // Memory benchmark results
    memReadLabel_->setText(QString::number(m.read_bw_gbs, 'f', 1) + " GB/s");
    memWriteLabel_->setText(QString::number(m.write_bw_gbs, 'f', 1) + " GB/s");
    memCopyLabel_->setText(QString::number(m.copy_bw_gbs, 'f', 1) + " GB/s");
    memLatencyLabel_->setText(QString::number(m.latency_ns, 'f', 1) + " ns");
    memChannelsLabel_->setText(QString::number(m.channels_detected));

    // Re-enable the button
    isRunning_ = false;
    runBtn_->setEnabled(true);
    runBtn_->setText("Run Benchmark");
    runBtn_->setStyleSheet(
        "QPushButton { background-color: #8E44AD; color: white; border: none; "
        "border-radius: 6px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background-color: #9B59B6; }"
    );
    statusLabel_->setText("Benchmark complete");
}

}} // namespace occt::gui
