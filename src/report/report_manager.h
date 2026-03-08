#pragma once

#include "png_report.h" // TestResults
#include <QString>

namespace occt {

class ReportManager {
public:
    /// Save an 800x600 PNG summary image.
    bool save_png(const TestResults& results, const QString& path);

    /// Save a self-contained HTML report.
    bool save_html(const TestResults& results, const QString& path);

    /// Save sensor time-series data as CSV.
    bool save_csv(const TestResults& results, const QString& path);

    /// Save full test results as JSON.
    bool save_json(const TestResults& results, const QString& path);
};

} // namespace occt
