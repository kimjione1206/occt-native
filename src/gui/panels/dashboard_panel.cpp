#include "dashboard_panel.h"
#include "../widgets/circular_gauge.h"
#include "../../monitor/system_info.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QSysInfo>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#endif

#ifdef Q_OS_LINUX
#include <fstream>
#include <string>
#endif

namespace occt { namespace gui {

DashboardPanel::DashboardPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    updateTimer_ = new QTimer(this);
    connect(updateTimer_, &QTimer::timeout, this, &DashboardPanel::updateGauges);
    updateTimer_->start(1000);
}

void DashboardPanel::setupUi()
{
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* container = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // Title
    auto* titleLabel = new QLabel("Dashboard", container);
    QFont titleFont = titleLabel->font();
    titleFont.setPixelSize(24);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet("color: #F0F6FC; background: transparent;");
    mainLayout->addWidget(titleLabel);

    auto* subtitleLabel = new QLabel("System overview and quick start", container);
    subtitleLabel->setStyleSheet("color: #8B949E; background: transparent; font-size: 14px;");
    mainLayout->addWidget(subtitleLabel);
    mainLayout->addSpacing(10);

    // System info cards row
    auto* cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(16);

    cpuInfoLabel_ = new QLabel("Detecting...");
    gpuInfoLabel_ = new QLabel("Detecting...");
    ramInfoLabel_ = new QLabel("Detecting...");
    osInfoLabel_  = new QLabel("Detecting...");

    auto sysInfo = occt::collect_system_info();

    QString cpuModel = sysInfo.cpu.model.isEmpty()
        ? QSysInfo::currentCpuArchitecture()
        : sysInfo.cpu.model;
    QString cpuDetail = sysInfo.cpu.physical_cores > 0
        ? QString("%1C/%2T").arg(sysInfo.cpu.physical_cores).arg(sysInfo.cpu.logical_cores)
        : QStringLiteral("Architecture");

    QString gpuModel = QStringLiteral("No GPU detected");
    QString gpuDetail = QStringLiteral("Graphics");
    if (!sysInfo.gpus.isEmpty()) {
        gpuModel = sysInfo.gpus[0].model.isEmpty()
            ? QStringLiteral("Unknown GPU")
            : sysInfo.gpus[0].model;
        gpuDetail = sysInfo.gpus[0].vram_mb > 0
            ? QString("%1 MB VRAM").arg(sysInfo.gpus[0].vram_mb)
            : QStringLiteral("Graphics");
    }

    QString ramValue = sysInfo.ram.total_mb > 0
        ? QString("%1 GB").arg(sysInfo.ram.total_mb / 1024)
        : QStringLiteral("System Memory");
    QString ramDetail = sysInfo.ram.speed_mhz > 0
        ? QString("%1 MHz").arg(sysInfo.ram.speed_mhz)
        : QStringLiteral("Memory");

    cardsLayout->addWidget(createInfoCard("CPU", cpuModel, cpuDetail));
    cardsLayout->addWidget(createInfoCard("GPU", gpuModel, gpuDetail));
    cardsLayout->addWidget(createInfoCard("RAM", ramValue, ramDetail));
    cardsLayout->addWidget(createInfoCard("OS",
        QSysInfo::prettyProductName(),
        QSysInfo::kernelType() + " " + QSysInfo::kernelVersion()));

    mainLayout->addLayout(cardsLayout);

    // Gauges row
    auto* gaugesFrame = new QFrame(container);
    gaugesFrame->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }");
    auto* gaugesLayout = new QHBoxLayout(gaugesFrame);
    gaugesLayout->setContentsMargins(24, 20, 24, 20);
    gaugesLayout->setSpacing(40);

    auto* gaugesTitle = new QLabel("Real-time Usage", gaugesFrame);
    gaugesTitle->setStyleSheet("color: #C9D1D9; font-size: 16px; font-weight: bold; border: none; background: transparent;");

    cpuGauge_ = new CircularGauge(gaugesFrame);
    cpuGauge_->setLabel("CPU");
    cpuGauge_->setValue(0);

    ramGauge_ = new CircularGauge(gaugesFrame);
    ramGauge_->setLabel("RAM");
    ramGauge_->setValue(0);

    gpuGauge_ = new CircularGauge(gaugesFrame);
    gpuGauge_->setLabel("GPU");
    gpuGauge_->setValue(0);

    auto* gaugesInnerLayout = new QVBoxLayout();
    gaugesInnerLayout->addWidget(gaugesTitle);
    auto* gaugesRow = new QHBoxLayout();
    gaugesRow->addStretch();
    gaugesRow->addWidget(cpuGauge_);
    gaugesRow->addWidget(ramGauge_);
    gaugesRow->addWidget(gpuGauge_);
    gaugesRow->addStretch();
    gaugesInnerLayout->addLayout(gaugesRow);
    gaugesLayout->addLayout(gaugesInnerLayout);

    mainLayout->addWidget(gaugesFrame);

    // Quick Start section
    mainLayout->addWidget(createQuickStartSection());

    mainLayout->addStretch();

    scrollArea->setWidget(container);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

QFrame* DashboardPanel::createInfoCard(const QString& title, const QString& value, const QString& detail)
{
    auto* card = new QFrame();
    card->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; padding: 16px; }"
    );
    card->setMinimumHeight(100);

    auto* layout = new QVBoxLayout(card);
    layout->setSpacing(6);

    auto* titleLbl = new QLabel(title, card);
    titleLbl->setStyleSheet("color: #8B949E; font-size: 12px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(titleLbl);

    auto* valueLbl = new QLabel(value, card);
    valueLbl->setStyleSheet("color: #F0F6FC; font-size: 15px; font-weight: bold; border: none; background: transparent;");
    valueLbl->setWordWrap(true);
    layout->addWidget(valueLbl);

    auto* detailLbl = new QLabel(detail, card);
    detailLbl->setStyleSheet("color: #8B949E; font-size: 11px; border: none; background: transparent;");
    layout->addWidget(detailLbl);

    layout->addStretch();
    return card;
}

QFrame* DashboardPanel::createQuickStartSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }"
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    auto* title = new QLabel("Quick Start", frame);
    title->setStyleSheet("color: #C9D1D9; font-size: 16px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(title);

    auto* buttonsLayout = new QHBoxLayout();
    buttonsLayout->setSpacing(12);

    auto createBtn = [this, frame](const QString& text, const QString& color) -> QPushButton* {
        auto* btn = new QPushButton(text, frame);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(48);
        btn->setMinimumWidth(160);
        btn->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: white; border: none; "
            "border-radius: 6px; font-size: 14px; font-weight: bold; }"
            "QPushButton:hover { background-color: %2; }"
        ).arg(color, color == "#C0392B" ? "#E74C3C" : color == "#27AE60" ? "#2ECC71" : "#3498DB"));
        return btn;
    };

    auto* cpuBtn = createBtn("CPU Test", "#C0392B");
    auto* gpuBtn = createBtn("GPU Test", "#2980B9");
    auto* fullBtn = createBtn("Full Test", "#27AE60");

    connect(cpuBtn, &QPushButton::clicked, this, &DashboardPanel::startCpuTest);
    connect(gpuBtn, &QPushButton::clicked, this, &DashboardPanel::startGpuTest);
    connect(fullBtn, &QPushButton::clicked, this, &DashboardPanel::startFullTest);

    buttonsLayout->addWidget(cpuBtn);
    buttonsLayout->addWidget(gpuBtn);
    buttonsLayout->addWidget(fullBtn);
    buttonsLayout->addStretch();

    layout->addLayout(buttonsLayout);

    return frame;
}

void DashboardPanel::updateGauges()
{
#ifdef Q_OS_MACOS
    // CPU usage via mach host_statistics
    {
        host_cpu_load_info_data_t cpuInfo;
        mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
        if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                            reinterpret_cast<host_info_t>(&cpuInfo), &count) == KERN_SUCCESS) {
            uint64_t idle  = cpuInfo.cpu_ticks[CPU_STATE_IDLE];
            uint64_t total = cpuInfo.cpu_ticks[CPU_STATE_USER]
                           + cpuInfo.cpu_ticks[CPU_STATE_SYSTEM]
                           + cpuInfo.cpu_ticks[CPU_STATE_IDLE]
                           + cpuInfo.cpu_ticks[CPU_STATE_NICE];

            if (prevTotalTicks_ > 0) {
                uint64_t dTotal = total - prevTotalTicks_;
                uint64_t dIdle  = idle  - prevIdleTicks_;
                double cpuPct = (dTotal > 0) ? (100.0 * (dTotal - dIdle) / dTotal) : 0.0;
                cpuGauge_->setValue(cpuPct);
            }
            prevIdleTicks_  = idle;
            prevTotalTicks_ = total;
        }
    }

    // RAM usage via mach vm_statistics64
    {
        vm_statistics64_data_t vmStat;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              reinterpret_cast<host_info64_t>(&vmStat), &count) == KERN_SUCCESS) {
            uint64_t pageSize = vm_kernel_page_size;
            uint64_t used = (vmStat.active_count + vmStat.wire_count) * pageSize;
            uint64_t total = (vmStat.active_count + vmStat.inactive_count
                            + vmStat.wire_count + vmStat.free_count) * pageSize;
            double ramPct = (total > 0) ? (100.0 * used / total) : 0.0;
            ramGauge_->setValue(ramPct);
        }
    }
#endif

#ifdef Q_OS_LINUX
    // CPU usage from /proc/stat
    {
        std::ifstream stat("/proc/stat");
        std::string cpu;
        uint64_t user, nice, sys, idle, iowait, irq, softirq, steal;
        if (stat >> cpu >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal) {
            uint64_t idleAll = idle + iowait;
            uint64_t total = user + nice + sys + idle + iowait + irq + softirq + steal;
            if (prevTotalTicks_ > 0) {
                uint64_t dTotal = total - prevTotalTicks_;
                uint64_t dIdle  = idleAll - prevIdleTicks_;
                double cpuPct = (dTotal > 0) ? (100.0 * (dTotal - dIdle) / dTotal) : 0.0;
                cpuGauge_->setValue(cpuPct);
            }
            prevIdleTicks_  = idleAll;
            prevTotalTicks_ = total;
        }
    }

    // RAM usage from /proc/meminfo
    {
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        uint64_t memTotal = 0, memAvailable = 0;
        while (std::getline(meminfo, line)) {
            if (line.rfind("MemTotal:", 0) == 0)
                sscanf(line.c_str(), "MemTotal: %llu kB", &memTotal);
            else if (line.rfind("MemAvailable:", 0) == 0)
                sscanf(line.c_str(), "MemAvailable: %llu kB", &memAvailable);
        }
        if (memTotal > 0) {
            double ramPct = 100.0 * (memTotal - memAvailable) / memTotal;
            ramGauge_->setValue(ramPct);
        }
    }
#endif

    // GPU gauge: not directly available without vendor-specific APIs;
    // leave at 0 unless SensorManager provides it in the future.
}

}} // namespace occt::gui
