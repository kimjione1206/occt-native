#include "sysinfo_panel.h"
#include "panel_styles.h"
#include "../../monitor/system_info.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QGridLayout>

namespace occt { namespace gui {

using Row = QPair<QString,QString>;

SysInfoPanel::SysInfoPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

QFrame* SysInfoPanel::createSection(const QString& title,
                                     const QVector<QPair<QString,QString>>& rows)
{
    auto* frame = new QFrame(this);
    frame->setStyleSheet(
        styles::kSectionFrame);

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    auto* titleLabel = new QLabel(title, frame);
    titleLabel->setStyleSheet(
        "color: #F0F6FC; font-size: 15px; font-weight: bold; border: none; background: transparent;");
    layout->addWidget(titleLabel);

    auto* sep = new QFrame(frame);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background-color: #30363D; max-height: 1px; border: none;");
    layout->addWidget(sep);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(6);

    int row = 0;
    for (const auto& kv : rows) {
        auto* keyLabel = new QLabel(kv.first, frame);
        keyLabel->setStyleSheet("color: #8B949E; font-size: 13px; border: none; background: transparent;");

        auto* valLabel = new QLabel(kv.second, frame);
        valLabel->setStyleSheet("color: #C9D1D9; font-size: 13px; border: none; background: transparent;");
        valLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        valLabel->setAccessibleName(
            "sysinfo_" + title.toLower().replace(" ", "_") + "_" + kv.first.toLower().replace(" ", "_"));

        grid->addWidget(keyLabel, row, 0);
        grid->addWidget(valLabel, row, 1);
        ++row;
    }
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    return frame;
}

void SysInfoPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(16);

    auto* title = new QLabel("System Information", this);
    title->setStyleSheet("color: #F0F6FC; font-size: 20px; font-weight: bold; background: transparent;");
    mainLayout->addWidget(title);

    auto* subtitle = new QLabel("Detailed hardware and software configuration", this);
    subtitle->setStyleSheet("color: #8B949E; font-size: 13px; background: transparent;");
    mainLayout->addWidget(subtitle);

    // Scroll area for the sections
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical { background: #0D1117; width: 8px; }"
        "QScrollBar::handle:vertical { background: #30363D; border-radius: 4px; }"
    );

    auto* scrollContent = new QWidget(scrollArea);
    auto* contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setSpacing(16);
    contentLayout->setContentsMargins(0, 0, 8, 0);

    // Collect system info
    auto info = occt::collect_system_info();

    // CPU section
    QVector<Row> cpuRows;
    cpuRows << Row(QStringLiteral("Model"), info.cpu.model.isEmpty() ? QStringLiteral("Unknown") : info.cpu.model);
    cpuRows << Row(QStringLiteral("Physical Cores"), QString::number(info.cpu.physical_cores));
    cpuRows << Row(QStringLiteral("Logical Cores"), QString::number(info.cpu.logical_cores));
    if (info.cpu.base_clock_mhz > 0)
        cpuRows << Row(QStringLiteral("Base Clock"), QString::number(info.cpu.base_clock_mhz) + " MHz");
    if (info.cpu.boost_clock_mhz > 0)
        cpuRows << Row(QStringLiteral("Boost Clock"), QString::number(info.cpu.boost_clock_mhz) + " MHz");
    if (info.cpu.l1_cache_kb > 0)
        cpuRows << Row(QStringLiteral("L1 Cache"), QString::number(info.cpu.l1_cache_kb) + " KB");
    if (info.cpu.l2_cache_kb > 0)
        cpuRows << Row(QStringLiteral("L2 Cache"), QString::number(info.cpu.l2_cache_kb) + " KB");
    if (info.cpu.l3_cache_kb > 0)
        cpuRows << Row(QStringLiteral("L3 Cache"), QString::number(info.cpu.l3_cache_kb) + " KB");
    if (!info.cpu.microarchitecture.isEmpty())
        cpuRows << Row(QStringLiteral("Microarchitecture"), info.cpu.microarchitecture);
    contentLayout->addWidget(createSection("CPU", cpuRows));

    // GPU section
    for (int i = 0; i < info.gpus.size(); ++i) {
        const auto& gpu = info.gpus[i];
        QString sec_title = info.gpus.size() > 1 ? QString("GPU %1").arg(i) : QStringLiteral("GPU");
        QVector<Row> gpuRows;
        gpuRows << Row(QStringLiteral("Model"), gpu.model.isEmpty() ? QStringLiteral("Unknown") : gpu.model);
        if (gpu.vram_mb > 0)
            gpuRows << Row(QStringLiteral("VRAM"), QString::number(gpu.vram_mb) + " MB");
        if (!gpu.driver_version.isEmpty())
            gpuRows << Row(QStringLiteral("Driver"), gpu.driver_version);
        gpuRows << Row(QStringLiteral("OpenCL"), gpu.has_opencl ? QStringLiteral("Yes") : QStringLiteral("No"));
        gpuRows << Row(QStringLiteral("Vulkan"), gpu.has_vulkan ? QStringLiteral("Yes") : QStringLiteral("No"));
        contentLayout->addWidget(createSection(sec_title, gpuRows));
    }
    if (info.gpus.isEmpty()) {
        QVector<Row> gpuRows;
        gpuRows << Row(QStringLiteral("Status"), QStringLiteral("No GPU detected via system API"));
        contentLayout->addWidget(createSection("GPU", gpuRows));
    }

    // RAM section
    QVector<Row> ramRows;
    ramRows << Row(QStringLiteral("Total"),
                   QString::number(info.ram.total_mb) + " MB (" +
                   QString::number(info.ram.total_mb / 1024) + " GB)");
    if (info.ram.speed_mhz > 0)
        ramRows << Row(QStringLiteral("Speed"), QString::number(info.ram.speed_mhz) + " MHz");
    if (!info.ram.timing.isEmpty())
        ramRows << Row(QStringLiteral("Timing"), info.ram.timing);
    if (info.ram.slot_count > 0)
        ramRows << Row(QStringLiteral("Slots"), QString::number(info.ram.slot_count));
    contentLayout->addWidget(createSection("RAM", ramRows));

    // Storage section
    for (int i = 0; i < info.storage.size(); ++i) {
        const auto& disk = info.storage[i];
        QVector<Row> diskRows;
        diskRows << Row(QStringLiteral("Model"), disk.model.isEmpty() ? QStringLiteral("Unknown") : disk.model);
        if (disk.capacity_gb > 0)
            diskRows << Row(QStringLiteral("Capacity"), QString::number(disk.capacity_gb) + " GB");
        diskRows << Row(QStringLiteral("Interface"),
                        disk.interface_type.isEmpty() ? QStringLiteral("Unknown") : disk.interface_type);
        contentLayout->addWidget(createSection(
            info.storage.size() > 1 ? QString("Storage %1").arg(i) : QStringLiteral("Storage"), diskRows));
    }
    if (info.storage.isEmpty()) {
        QVector<Row> diskRows;
        diskRows << Row(QStringLiteral("Status"), QStringLiteral("Storage detection requires platform-specific APIs"));
        contentLayout->addWidget(createSection("Storage", diskRows));
    }

    // OS section
    QVector<Row> osRows;
    osRows << Row(QStringLiteral("Name"), info.os.name);
    if (!info.os.version.isEmpty())
        osRows << Row(QStringLiteral("Version"), info.os.version);
    if (!info.os.build.isEmpty())
        osRows << Row(QStringLiteral("Build"), info.os.build);
    osRows << Row(QStringLiteral("Architecture"), info.os.architecture);
    contentLayout->addWidget(createSection("Operating System", osRows));

    contentLayout->addStretch();
    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);
}

}} // namespace occt::gui
