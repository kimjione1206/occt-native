#pragma once

#include "cli_args.h"
#include "report/png_report.h" // TestResults

#include <QCoreApplication>

namespace occt {

class CliRunner {
public:
    /// Run the CLI with the given options.
    /// Returns exit code: 0=PASS, 1=FAIL, 2=ERROR.
    int run(const CliOptions& opts);

private:
    /// Output JSON progress line to stdout.
    void emit_json(const QString& type, const QString& key, const QVariant& value);

    /// Run a single stress test.
    int run_test(const CliOptions& opts);

    /// Run a scheduled test sequence.
    int run_schedule(const CliOptions& opts);

    /// Run a certification tier.
    int run_certificate(const CliOptions& opts);

    /// Run monitor-only mode.
    int run_monitor(const CliOptions& opts);

    /// Generate report after test completes.
    bool generate_report(const TestResults& results, const CliOptions& opts);

    /// Collect current system info.
    SystemInfoData collect_system_info();

    TestResults results_;
};

} // namespace occt
