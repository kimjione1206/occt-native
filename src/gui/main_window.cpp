#include "main_window.h"
#include "panels/dashboard_panel.h"
#include "panels/cpu_panel.h"
#include "panels/gpu_panel.h"
#include "panels/ram_panel.h"
#include "panels/storage_panel.h"
#include "panels/monitor_panel.h"
#include "panels/results_panel.h"
#include "panels/psu_panel.h"
#include "panels/benchmark_panel.h"
#include "panels/sysinfo_panel.h"
#include "panels/schedule_panel.h"
#include "panels/certificate_panel.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMessageBox>
#include <QFileDialog>

namespace occt { namespace gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("OCCT Native Stress Test");
    setMinimumSize(1200, 800);
    resize(1400, 900);

    setupUi();

    // Status bar timer
    statusTimer_ = new QTimer(this);
    connect(statusTimer_, &QTimer::timeout, this, &MainWindow::updateStatusBar);
    statusTimer_->start(1000);

    // Default panel
    setActiveTab("dashboard");
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    createMenuBar();

    centralContainer_ = new QWidget(this);
    setCentralWidget(centralContainer_);

    auto* mainLayout = new QVBoxLayout(centralContainer_);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    createHeaderBar();
    mainLayout->addWidget(headerBar_);

    auto* bodyLayout = new QHBoxLayout();
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    createSidebar();
    createContentArea();
    createPanels();

    bodyLayout->addWidget(sidebar_);
    bodyLayout->addWidget(contentStack_, 1);
    mainLayout->addLayout(bodyLayout, 1);

    createStatusBarWidgets();
}

void MainWindow::createHeaderBar()
{
    headerBar_ = new QFrame(centralContainer_);
    headerBar_->setObjectName("headerBar");
    headerBar_->setFixedHeight(50);
    headerBar_->setStyleSheet(
        "QFrame#headerBar { background-color: #C0392B; border: none; }"
        "QFrame#headerBar QLabel { color: white; background: transparent; }"
    );

    auto* layout = new QHBoxLayout(headerBar_);
    layout->setContentsMargins(20, 0, 20, 0);

    // App icon placeholder (unicode gear)
    auto* iconLabel = new QLabel(QString::fromUtf8("\xe2\x9a\x99"), headerBar_);
    QFont iconFont = iconLabel->font();
    iconFont.setPixelSize(24);
    iconLabel->setFont(iconFont);
    layout->addWidget(iconLabel);

    auto* titleLabel = new QLabel("OCCT Native Stress Test", headerBar_);
    QFont titleFont = titleLabel->font();
    titleFont.setPixelSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    layout->addStretch();

    auto* versionLabel = new QLabel("v1.0.0", headerBar_);
    versionLabel->setStyleSheet("color: rgba(255,255,255,0.7);");
    layout->addWidget(versionLabel);
}

void MainWindow::createSidebar()
{
    sidebar_ = new QFrame(centralContainer_);
    sidebar_->setObjectName("sidebar");
    sidebar_->setFixedWidth(220);
    sidebar_->setStyleSheet(
        "QFrame#sidebar { background-color: #161B22; border-right: 1px solid #30363D; }"
    );

    sidebarLayout_ = new QVBoxLayout(sidebar_);
    sidebarLayout_->setContentsMargins(0, 10, 0, 10);
    sidebarLayout_->setSpacing(2);

    // Group 1: Main stress tests
    addNavButton("dashboard", "Dashboard",    "\xf0\x9f\x93\x8a");
    addNavButton("cpu",       "CPU Test",     "\xf0\x9f\x94\xa5");
    addNavButton("gpu",       "GPU Test",     "\xf0\x9f\x8e\xae");
    addNavButton("ram",       "RAM Test",     "\xf0\x9f\x92\xbe");
    addNavButton("storage",   "Storage Test", "\xf0\x9f\x92\xbf");
    addNavButton("psu",       "PSU Test",     "\xf0\x9f\x94\x8c");

    addSeparator();

    // Group 2: Scheduling & benchmarks
    addNavButton("schedule",    "Schedule",     "\xf0\x9f\x93\x85");
    addNavButton("benchmark",   "Benchmark",    "\xf0\x9f\x93\x90");
    addNavButton("certificate", "Certificate",  "\xf0\x9f\x8f\x86");

    addSeparator();

    // Group 3: Monitoring & results
    addNavButton("monitor",   "Monitoring",   "\xf0\x9f\x93\x89");
    addNavButton("sysinfo",   "System Info",  "\xf0\x9f\x96\xa5");
    addNavButton("results",   "Results",      "\xf0\x9f\x93\x8b");

    sidebarLayout_->addStretch();
}

void MainWindow::addNavButton(const QString& key, const QString& text, const QString& iconChar)
{
    auto* btn = new QPushButton(QString::fromUtf8(iconChar.toUtf8()) + "  " + text, sidebar_);
    btn->setObjectName("nav_" + key);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(44);
    btn->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #8B949E;"
        "  border: none;"
        "  text-align: left;"
        "  padding: 0 20px;"
        "  font-size: 14px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #21262D;"
        "  color: #C9D1D9;"
        "}"
    );

    sidebarLayout_->addWidget(btn);
    navButtons_[key] = btn;

    connect(btn, &QPushButton::clicked, this, [this, key]() {
        onNavButtonClicked(key);
    });
}

void MainWindow::addSeparator()
{
    sidebarLayout_->addSpacing(10);
    auto* sep = new QFrame(sidebar_);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background-color: #30363D; max-height: 1px;");
    sidebarLayout_->addWidget(sep);
    sidebarLayout_->addSpacing(10);
}

void MainWindow::createContentArea()
{
    contentStack_ = new QStackedWidget(centralContainer_);
    contentStack_->setStyleSheet("QStackedWidget { background-color: #0D1117; }");
}

void MainWindow::createPanels()
{
    auto* dashboardPanel = new DashboardPanel(contentStack_);
    auto* cpuPanel       = new CpuPanel(contentStack_);
    auto* gpuPanel       = new GpuPanel(contentStack_);
    auto* ramPanel       = new RamPanel(contentStack_);
    auto* storagePanel   = new StoragePanel(contentStack_);
    auto* psuPanel       = new PsuPanel(contentStack_);
    auto* benchmarkPanel = new BenchmarkPanel(contentStack_);
    auto* monitorPanel   = new MonitorPanel(contentStack_);
    auto* sysinfoPanel   = new SysInfoPanel(contentStack_);
    resultsPanel_        = new ResultsPanel(contentStack_);
    auto* resultsPanel   = resultsPanel_;
    auto* schedulePanel  = new SchedulePanel(contentStack_);
    auto* certPanel      = new CertificatePanel(contentStack_);

    panels_["dashboard"]   = dashboardPanel;
    panels_["cpu"]         = cpuPanel;
    panels_["gpu"]         = gpuPanel;
    panels_["ram"]         = ramPanel;
    panels_["storage"]     = storagePanel;
    panels_["psu"]         = psuPanel;
    panels_["benchmark"]   = benchmarkPanel;
    panels_["monitor"]     = monitorPanel;
    panels_["sysinfo"]     = sysinfoPanel;
    panels_["results"]     = resultsPanel;
    panels_["schedule"]    = schedulePanel;
    panels_["certificate"] = certPanel;

    for (auto it = panels_.begin(); it != panels_.end(); ++it) {
        contentStack_->addWidget(it.value());
    }

    // Connect Dashboard quick-start buttons to navigate to the correct panels
    connect(dashboardPanel, &DashboardPanel::startCpuTest, this, [this]() {
        setActiveTab("cpu");
    });
    connect(dashboardPanel, &DashboardPanel::startGpuTest, this, [this]() {
        setActiveTab("gpu");
    });
    connect(dashboardPanel, &DashboardPanel::startFullTest, this, [this]() {
        setActiveTab("cpu");
    });
}

void MainWindow::createStatusBarWidgets()
{
    auto* sb = statusBar();
    sb->setStyleSheet(
        "QStatusBar { background-color: #161B22; color: #8B949E; border-top: 1px solid #30363D; }"
        "QStatusBar::item { border: none; }"
    );

    statusLabel_ = new QLabel("Ready", sb);
    statusLabel_->setStyleSheet("color: #8B949E; padding: 2px 8px;");
    sb->addWidget(statusLabel_, 1);

    auto* timeLabel = new QLabel(sb);
    timeLabel->setStyleSheet("color: #8B949E; padding: 2px 8px;");
    sb->addPermanentWidget(timeLabel);

    connect(statusTimer_, &QTimer::timeout, this, [timeLabel]() {
        timeLabel->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    });
}

void MainWindow::onNavButtonClicked(const QString& panelKey)
{
    setActiveTab(panelKey);
}

void MainWindow::setActiveTab(const QString& key)
{
    if (!panels_.contains(key)) return;

    currentPanel_ = key;
    contentStack_->setCurrentWidget(panels_[key]);

    // Update button styles
    for (auto it = navButtons_.begin(); it != navButtons_.end(); ++it) {
        QPushButton* btn = it.value();
        bool active = (it.key() == key);

        QString style;
        if (active) {
            style =
                "QPushButton {"
                "  background-color: #1F2937;"
                "  color: #F0F6FC;"
                "  border: none;"
                "  border-left: 3px solid #C0392B;"
                "  text-align: left;"
                "  padding: 0 17px;"
                "  font-size: 14px;"
                "  font-weight: bold;"
                "}"
                "QPushButton:hover {"
                "  background-color: #1F2937;"
                "  color: #F0F6FC;"
                "}";
        } else {
            style =
                "QPushButton {"
                "  background-color: transparent;"
                "  color: #8B949E;"
                "  border: none;"
                "  text-align: left;"
                "  padding: 0 20px;"
                "  font-size: 14px;"
                "}"
                "QPushButton:hover {"
                "  background-color: #21262D;"
                "  color: #C9D1D9;"
                "}";
        }
        btn->setStyleSheet(style);
    }

    statusLabel_->setText("Panel: " + key);
}

void MainWindow::updateStatusBar()
{
    // Could be extended with real system info
}

void MainWindow::createMenuBar()
{
    auto* mb = menuBar();
    mb->setStyleSheet(
        "QMenuBar { background-color: #161B22; color: #C9D1D9; border-bottom: 1px solid #30363D; }"
        "QMenuBar::item { padding: 6px 12px; }"
        "QMenuBar::item:selected { background-color: #21262D; }"
        "QMenu { background-color: #161B22; color: #C9D1D9; border: 1px solid #30363D; }"
        "QMenu::item { padding: 6px 24px; }"
        "QMenu::item:selected { background-color: #1F2937; }"
        "QMenu::separator { height: 1px; background: #30363D; margin: 4px 8px; }"
    );

    // File menu
    auto* fileMenu = mb->addMenu("&File");
    auto* exportAction = fileMenu->addAction("Export Report...");
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExportReport);
    fileMenu->addSeparator();
    auto* exitAction = fileMenu->addAction("Exit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &MainWindow::onExit);

    // Test menu
    auto* testMenu = mb->addMenu("&Test");
    auto* startScheduleAction = testMenu->addAction("Start Schedule");
    connect(startScheduleAction, &QAction::triggered, this, [this]() {
        setActiveTab("schedule");
    });
    auto* stopAllAction = testMenu->addAction("Stop All Tests");
    connect(stopAllAction, &QAction::triggered, this, [this]() {
        statusLabel_->setText("All tests stopped");
    });

    // Help menu
    auto* helpMenu = mb->addMenu("&Help");
    auto* aboutAction = helpMenu->addAction("About");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::onExportReport()
{
    setActiveTab("results");
    if (resultsPanel_) {
        resultsPanel_->onExportHtmlClicked();
    }
}

void MainWindow::onExit()
{
    QApplication::quit();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, "About OCCT Native Stress Test",
        "<h2>OCCT Native Stress Test</h2>"
        "<p>Version 1.0.0</p>"
        "<p>A professional hardware stress testing tool for CPU, GPU, RAM, "
        "Storage, and PSU stability verification.</p>"
        "<p>Features:</p>"
        "<ul>"
        "<li>CPU: AVX2/AVX-512/SSE/Linpack/Prime stress tests</li>"
        "<li>GPU: OpenCL/Vulkan compute stress</li>"
        "<li>RAM: March C/Walking Ones/Random pattern tests</li>"
        "<li>Storage: Sequential/Random I/O tests</li>"
        "<li>PSU: Combined CPU+GPU load patterns</li>"
        "<li>Real-time hardware monitoring</li>"
        "<li>Test scheduling and automation</li>"
        "<li>Stability certificate generation</li>"
        "<li>Report export: HTML, PNG, CSV, JSON</li>"
        "</ul>"
        "<p>Built with Qt6 and C++17.</p>"
    );
}

}} // namespace occt::gui
