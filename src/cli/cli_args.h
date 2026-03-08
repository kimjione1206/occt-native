#pragma once

#include <QString>

namespace occt {

struct CliOptions {
    bool cli_mode = false;
    QString test;             // "cpu", "gpu", "ram", "storage", "psu", "schedule", "benchmark", "certificate"
    QString mode;             // "avx2", "linpack", "prime", etc.
    int threads = 0;          // 0 = auto-detect
    int duration = 0;         // seconds, 0 = default
    QString schedule_file;    // JSON schedule file path
    QString report_format;    // "html", "png", "csv", "json"
    QString output_path;      // report output path
    bool monitor_only = false;
    int memory_percent = 90;  // RAM test: percentage of memory to use
    bool show_help = false;
    bool show_version = false;

    // CPU options (Fix 1-3)
    QString load_pattern;     // "steady" or "variable"

    // RAM options (Fix 1-5)
    int passes = 1;           // Number of RAM test passes

    // Storage options (Fix 1-6)
    int file_size_mb = 256;   // Storage test file size in MB
    int queue_depth = 4;      // Storage I/O queue depth
    QString storage_path;     // Storage test path (empty = temp dir)

    // GPU options (Fix 2-1)
    int gpu_index = -1;       // GPU index (-1 = default)
    int shader_complexity = 1; // Vulkan shader complexity (1-5)
    QString adaptive_mode;    // "variable" or "switch"

    // PSU options (Fix 2-2)
    bool use_all_gpus = false;

    // Schedule options (Fix 3-1)
    bool stop_on_error = false;

    // Certificate options (Fix 3-2)
    QString cert_tier;        // "bronze", "silver", "gold", "platinum"
};

/// Parse command-line arguments into CliOptions.
CliOptions parse_args(int argc, char** argv);

/// Print usage help to stdout.
void print_usage();

} // namespace occt
