#include "certificate_panel.h"
#include "panel_styles.h"

#include "scheduler/test_scheduler.h"
#include "scheduler/preset_schedules.h"
#include "certification/certificate.h"
#include "certification/cert_generator.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QJsonDocument>

namespace occt { namespace gui {

CertificatePanel::CertificatePanel(QWidget* parent)
    : QWidget(parent)
{
    scheduler_ = new TestScheduler(this);
    connect(scheduler_, &TestScheduler::stepStarted, this, &CertificatePanel::onStepStarted);
    connect(scheduler_, &TestScheduler::stepCompleted, this, &CertificatePanel::onStepCompleted);
    connect(scheduler_, &TestScheduler::scheduleCompleted, this, &CertificatePanel::onScheduleCompleted);
    connect(scheduler_, &TestScheduler::progressChanged, this, &CertificatePanel::onProgressChanged);

    setupUi();
    updateTierInfo();
}

void CertificatePanel::setupUi()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(24, 24, 24, 24);
    mainLayout->setSpacing(20);

    // Left column: tier selection + progress
    auto* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(16);
    leftLayout->addWidget(createTierSection());
    leftLayout->addWidget(createProgressSection(), 1);

    auto* leftWidget = new QWidget();
    leftWidget->setFixedWidth(380);
    leftWidget->setLayout(leftLayout);
    mainLayout->addWidget(leftWidget);

    // Right column: preview
    mainLayout->addWidget(createPreviewSection(), 1);
}

QFrame* CertificatePanel::createTierSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* title = new QLabel("Stability Certificate", frame);
    title->setStyleSheet(styles::kPanelTitle);
    layout->addWidget(title);

    auto* subtitle = new QLabel("Run a certified stability test to prove system reliability", frame);
    subtitle->setStyleSheet(styles::kPanelSubtitle);
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    layout->addSpacing(8);

    auto* tierLabel = new QLabel("Select Tier", frame);
    tierLabel->setStyleSheet(styles::kSettingsLabel);
    layout->addWidget(tierLabel);

    auto createTierBtn = [frame](const QString& text, const QString& color) -> QPushButton* {
        auto* btn = new QPushButton(text, frame);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(42);
        btn->setCheckable(true);
        btn->setStyleSheet(
            QString(
                "QPushButton { background-color: #0D1117; color: %1; border: 2px solid #30363D; "
                "border-radius: 6px; font-size: 14px; font-weight: bold; }"
                "QPushButton:hover { border-color: %1; }"
                "QPushButton:checked { border-color: %1; background-color: #1F2937; }"
            ).arg(color)
        );
        return btn;
    };

    bronzeBtn_   = createTierBtn("Bronze  (~1 hour)", "#CD7F32");
    silverBtn_   = createTierBtn("Silver  (~3 hours)", "#C0C0C0");
    goldBtn_     = createTierBtn("Gold  (~6 hours)", "#FFD700");
    platinumBtn_ = createTierBtn("Platinum  (~12 hours)", "#E5E4E2");

    bronzeBtn_->setChecked(true);

    layout->addWidget(bronzeBtn_);
    layout->addWidget(silverBtn_);
    layout->addWidget(goldBtn_);
    layout->addWidget(platinumBtn_);

    connect(bronzeBtn_, &QPushButton::clicked, this, [this]() { onTierSelected(0); });
    connect(silverBtn_, &QPushButton::clicked, this, [this]() { onTierSelected(1); });
    connect(goldBtn_, &QPushButton::clicked, this, [this]() { onTierSelected(2); });
    connect(platinumBtn_, &QPushButton::clicked, this, [this]() { onTierSelected(3); });

    tierInfoLabel_ = new QLabel("", frame);
    tierInfoLabel_->setStyleSheet(styles::kSmallInfo);
    tierInfoLabel_->setWordWrap(true);
    layout->addWidget(tierInfoLabel_);

    return frame;
}

QFrame* CertificatePanel::createProgressSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* title = new QLabel("Certification Progress", frame);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    certProgress_ = new QProgressBar(frame);
    certProgress_->setObjectName("cert_progress");
    certProgress_->setRange(0, 100);
    certProgress_->setValue(0);
    certProgress_->setTextVisible(true);
    certProgress_->setStyleSheet(
        "QProgressBar { background-color: #0D1117; border: 1px solid #30363D; border-radius: 6px; "
        "height: 24px; text-align: center; color: #F0F6FC; font-weight: bold; }"
        "QProgressBar::chunk { background-color: #C0392B; border-radius: 5px; }"
    );
    layout->addWidget(certProgress_);

    currentStepLabel_ = new QLabel("Not started", frame);
    currentStepLabel_->setObjectName("cert_current_step");
    currentStepLabel_->setStyleSheet("color: #8B949E; font-size: 13px; border: none; background: transparent;");
    layout->addWidget(currentStepLabel_);

    statusLabel_ = new QLabel("Select a tier and click Start Certification", frame);
    statusLabel_->setObjectName("cert_status");
    statusLabel_->setStyleSheet(
        "color: #C9D1D9; font-size: 12px; border: none; background-color: #0D1117; "
        "padding: 12px; border-radius: 6px;"
    );
    statusLabel_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_, 1);

    startBtn_ = new QPushButton("Start Certification", frame);
    startBtn_->setCursor(Qt::PointingHandCursor);
    startBtn_->setFixedHeight(48);
    startBtn_->setStyleSheet(
        styles::kStartButton
    );
    connect(startBtn_, &QPushButton::clicked, this, &CertificatePanel::onStartCertification);
    layout->addWidget(startBtn_);

    return frame;
}

QFrame* CertificatePanel::createPreviewSection()
{
    auto* frame = new QFrame();
    frame->setStyleSheet(
        styles::kSectionFrame
    );

    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* title = new QLabel("Certificate Preview", frame);
    title->setStyleSheet(styles::kSectionTitle);
    layout->addWidget(title);

    previewBrowser_ = new QTextBrowser(frame);
    previewBrowser_->setStyleSheet(
        "QTextBrowser { background-color: #0D1117; border: 1px solid #30363D; border-radius: 6px; "
        "color: #C9D1D9; padding: 12px; }"
    );
    previewBrowser_->setHtml(
        "<div style='text-align:center; padding:60px; color:#484F58;'>"
        "<h2>No Certificate</h2>"
        "<p>Complete a certification test to see the certificate here.</p></div>"
    );
    layout->addWidget(previewBrowser_, 1);

    // Save buttons (hidden until cert complete)
    saveFrame_ = new QFrame(frame);
    saveFrame_->setStyleSheet("border: none; background: transparent;");
    saveFrame_->setVisible(false);
    auto* saveLayout = new QHBoxLayout(saveFrame_);
    saveLayout->setContentsMargins(0, 0, 0, 0);
    saveLayout->setSpacing(10);

    saveHtmlBtn_ = new QPushButton("Save HTML", saveFrame_);
    savePngBtn_ = new QPushButton("Save PNG", saveFrame_);

    auto styleBtn = [](QPushButton* btn) {
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(36);
        btn->setStyleSheet(
            "QPushButton { background-color: #21262D; color: #C9D1D9; border: 1px solid #30363D; "
            "border-radius: 4px; padding: 0 16px; font-size: 13px; }"
            "QPushButton:hover { background-color: #30363D; }"
        );
    };
    styleBtn(saveHtmlBtn_);
    styleBtn(savePngBtn_);

    saveLayout->addWidget(saveHtmlBtn_);
    saveLayout->addWidget(savePngBtn_);
    saveLayout->addStretch();
    layout->addWidget(saveFrame_);

    connect(saveHtmlBtn_, &QPushButton::clicked, this, &CertificatePanel::onSaveHtml);
    connect(savePngBtn_, &QPushButton::clicked, this, &CertificatePanel::onSavePng);

    return frame;
}

void CertificatePanel::onTierSelected(int tier)
{
    if (isRunning_) return;
    selectedTier_ = tier;

    bronzeBtn_->setChecked(tier == 0);
    silverBtn_->setChecked(tier == 1);
    goldBtn_->setChecked(tier == 2);
    platinumBtn_->setChecked(tier == 3);

    updateTierInfo();
}

void CertificatePanel::updateTierInfo()
{
    struct TierInfo { QString name; QString tests; QString duration; };
    static const TierInfo infos[] = {
        {"Bronze", "CPU SSE 30min + RAM 30min", "~1 hour"},
        {"Silver", "CPU AVX2 60min + GPU 60min + RAM 60min", "~3 hours"},
        {"Gold", "CPU Linpack 120min + GPU VRAM 60min + RAM 120min + Storage 60min", "~6 hours"},
        {"Platinum", "CPU AVX2 300min + RAM 300min + GPU 120min", "~12 hours"}
    };

    const auto& info = infos[selectedTier_];
    tierInfoLabel_->setText(
        QString("Duration: %1\nTests: %2").arg(info.duration, info.tests));
}

void CertificatePanel::onStartCertification()
{
    if (isRunning_) {
        scheduler_->stop();
        isRunning_ = false;
        startBtn_->setText("Start Certification");
        startBtn_->setStyleSheet(
            styles::kStartButton
        );
        currentStepLabel_->setText("Certification stopped");
        return;
    }

    QVector<TestStep> steps;
    switch (selectedTier_) {
        case 0: steps = preset_cert_bronze(); break;
        case 1: steps = preset_cert_silver(); break;
        case 2: steps = preset_cert_gold(); break;
        case 3: steps = preset_cert_platinum(); break;
    }

    scheduler_->load_schedule(steps);
    scheduler_->set_stop_on_error(true);
    isRunning_ = true;
    certProgress_->setValue(0);
    saveFrame_->setVisible(false);
    statusLabel_->setText("Starting certification...\n");

    startBtn_->setText("Stop Certification");
    startBtn_->setStyleSheet(
        styles::kStopButton
    );

    scheduler_->start();
}

void CertificatePanel::onStepStarted(int index, const QString& engine)
{
    currentStepLabel_->setText(
        QString("Step %1/%2: %3")
            .arg(index + 1)
            .arg(scheduler_->steps().size())
            .arg(engine.toUpper()));

    statusLabel_->setText(statusLabel_->text() +
        QString("Step %1: %2 started\n").arg(index + 1).arg(engine.toUpper()));
}

void CertificatePanel::onStepCompleted(int index, bool passed, int errors)
{
    QString result = passed ? "PASS" : "FAIL";
    statusLabel_->setText(statusLabel_->text() +
        QString("Step %1: %2 (errors: %3)\n").arg(index + 1).arg(result).arg(errors));
}

void CertificatePanel::onScheduleCompleted(bool all_passed, int total_errors)
{
    isRunning_ = false;
    certProgress_->setValue(100);

    startBtn_->setText("Start Certification");
    startBtn_->setStyleSheet(
        styles::kStartButton
    );

    // Build certificate
    CertTier tier = static_cast<CertTier>(selectedTier_);
    Certificate cert;
    cert.tier = tier;
    cert.passed = all_passed;
    cert.issued_at = QDateTime::currentDateTime();
    cert.expires_at = cert.issued_at.addDays(90);
    cert.system_info_json = CertGenerator::collect_system_info();

    // Convert scheduler results to cert results
    const auto& schedResults = scheduler_->results();
    for (const auto& sr : schedResults) {
        TestResult tr;
        tr.engine = sr.engine;
        tr.passed = sr.passed;
        tr.errors = sr.errors;
        tr.duration_secs = sr.duration_secs;

        // Find mode from step settings
        if (sr.index >= 0 && sr.index < scheduler_->steps().size()) {
            tr.mode = scheduler_->steps()[sr.index].settings.value("mode", "default").toString();
        }
        cert.results.append(tr);
    }

    // Compute hash
    CertGenerator gen;
    QJsonObject resultsJson = gen.generate_json(cert);
    cert.hash_sha256 = CertGenerator::compute_hash(cert.system_info_json, resultsJson);

    // Generate HTML and show preview
    lastHtml_ = gen.generate_html(cert);
    previewBrowser_->setHtml(lastHtml_);
    saveFrame_->setVisible(true);

    QString summary = all_passed ? "PASSED" : "FAILED";
    currentStepLabel_->setText(
        QString("Certification complete: %1 (%2 tier)")
            .arg(summary, cert_tier_name(tier)));

    statusLabel_->setText(statusLabel_->text() +
        QString("\n--- Certification Complete ---\nTier: %1\nResult: %2\nTotal errors: %3\nHash: %4\n")
            .arg(cert_tier_name(tier), summary)
            .arg(total_errors)
            .arg(cert.hash_sha256));
}

void CertificatePanel::onProgressChanged(double pct)
{
    certProgress_->setValue(static_cast<int>(pct));
}

void CertificatePanel::onSaveHtml()
{
    if (lastHtml_.isEmpty()) return;
    QString path = QFileDialog::getSaveFileName(this, "Save Certificate HTML", "", "HTML (*.html)");
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (file.write(lastHtml_.toUtf8()) == -1) {
                qWarning() << "Failed to write certificate HTML to" << path
                           << ":" << file.errorString();
            }
        } else {
            qWarning() << "Failed to open file for writing:" << path
                       << ":" << file.errorString();
        }
    }
}

void CertificatePanel::onSavePng()
{
    // Rebuild certificate from last run for PNG generation
    CertTier tier = static_cast<CertTier>(selectedTier_);
    Certificate cert;
    cert.tier = tier;
    cert.issued_at = QDateTime::currentDateTime();
    cert.expires_at = cert.issued_at.addDays(90);
    cert.system_info_json = CertGenerator::collect_system_info();

    const auto& schedResults = scheduler_->results();
    cert.passed = true;
    for (const auto& sr : schedResults) {
        TestResult tr;
        tr.engine = sr.engine;
        tr.passed = sr.passed;
        tr.errors = sr.errors;
        tr.duration_secs = sr.duration_secs;
        if (sr.index >= 0 && sr.index < scheduler_->steps().size()) {
            tr.mode = scheduler_->steps()[sr.index].settings.value("mode", "default").toString();
        }
        cert.results.append(tr);
        if (!sr.passed) cert.passed = false;
    }

    CertGenerator gen;
    QJsonObject resultsJson = gen.generate_json(cert);
    cert.hash_sha256 = CertGenerator::compute_hash(cert.system_info_json, resultsJson);

    QString path = QFileDialog::getSaveFileName(this, "Save Certificate PNG", "", "PNG (*.png)");
    if (!path.isEmpty()) {
        QImage img = gen.generate_image(cert);
        img.save(path, "PNG");
    }
}

}} // namespace occt::gui
