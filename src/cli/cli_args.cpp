#include "cli_args.h"

#include <QTextStream>
#include <cstdio>
#include <cstring>

namespace occt {

CliOptions parse_args(int argc, char** argv)
{
    CliOptions opts;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (std::strcmp(arg, "--cli") == 0) {
            opts.cli_mode = true;
        } else if (std::strcmp(arg, "--test") == 0 && i + 1 < argc) {
            opts.test = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--mode") == 0 && i + 1 < argc) {
            opts.mode = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--threads") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "auto") == 0) {
                opts.threads = 0;
            } else {
                opts.threads = std::atoi(argv[i]);
            }
        } else if (std::strcmp(arg, "--duration") == 0 && i + 1 < argc) {
            opts.duration = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--schedule") == 0 && i + 1 < argc) {
            opts.schedule_file = QString::fromUtf8(argv[++i]);
            opts.test = "schedule";
        } else if (std::strcmp(arg, "--report") == 0 && i + 1 < argc) {
            opts.report_format = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--output") == 0 && i + 1 < argc) {
            opts.output_path = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--monitor-only") == 0) {
            opts.monitor_only = true;
        } else if (std::strcmp(arg, "--memory") == 0 && i + 1 < argc) {
            opts.memory_percent = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--csv") == 0 && i + 1 < argc) {
            opts.report_format = "csv";
            opts.output_path = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--load-pattern") == 0 && i + 1 < argc) {
            opts.load_pattern = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--passes") == 0 && i + 1 < argc) {
            opts.passes = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--file-size") == 0 && i + 1 < argc) {
            opts.file_size_mb = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--queue-depth") == 0 && i + 1 < argc) {
            opts.queue_depth = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--storage-path") == 0 && i + 1 < argc) {
            opts.storage_path = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--gpu-index") == 0 && i + 1 < argc) {
            opts.gpu_index = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--shader-complexity") == 0 && i + 1 < argc) {
            opts.shader_complexity = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--adaptive-mode") == 0 && i + 1 < argc) {
            opts.adaptive_mode = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--use-all-gpus") == 0) {
            opts.use_all_gpus = true;
        } else if (std::strcmp(arg, "--stop-on-error") == 0) {
            opts.stop_on_error = true;
        } else if (std::strcmp(arg, "--cert-tier") == 0 && i + 1 < argc) {
            opts.cert_tier = QString::fromUtf8(argv[++i]);
            opts.test = "certificate";
        } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            opts.show_help = true;
        } else if (std::strcmp(arg, "--version") == 0 || std::strcmp(arg, "-v") == 0) {
            opts.show_version = true;
        }
    }

    return opts;
}

void print_usage()
{
    std::fprintf(stdout,
        "OCCT Native Stress Test - CLI Mode\n"
        "\n"
        "Usage:\n"
        "  occt_native --cli --test <type> [options]\n"
        "  occt_native --cli --schedule <file.json> [options]\n"
        "  occt_native --cli --cert-tier <tier> [options]\n"
        "  occt_native --cli --monitor-only [options]\n"
        "\n"
        "Test types: cpu, gpu, ram, storage, psu, benchmark, certificate\n"
        "\n"
        "General options:\n"
        "  --cli                  Run in CLI mode (no GUI)\n"
        "  --test <type>          Test type to run\n"
        "  --mode <mode>          Test mode (see modes below)\n"
        "  --duration <secs>      Test duration in seconds\n"
        "  --report <format>      Report format: html, png, csv, json\n"
        "  --output <path>        Output file/directory path\n"
        "  --monitor-only         Only monitor sensors, no stress test\n"
        "  --csv <file>           Shorthand for --report csv --output <file>\n"
        "  --help, -h             Show this help message\n"
        "  --version, -v          Show version\n"
        "\n"
        "CPU options:\n"
        "  --threads <N|auto>     Number of threads (0 or auto = all cores)\n"
        "  --load-pattern <p>     Load pattern: steady (default), variable\n"
        "  Modes: avx2, avx512, sse, linpack, prime, all\n"
        "\n"
        "RAM options:\n"
        "  --memory <percent>     Memory percentage (default: 90)\n"
        "  --passes <N>           Number of test passes (default: 1)\n"
        "  Modes: march_c (default), walking, walking_zeros, checkerboard, random, bandwidth\n"
        "\n"
        "Storage options:\n"
        "  --file-size <MB>       Test file size in MB (default: 256)\n"
        "  --queue-depth <N>      I/O queue depth (default: 4)\n"
        "  --storage-path <path>  Test directory (default: system temp)\n"
        "  Modes: write (default), read, random_write, random_read, mixed\n"
        "\n"
        "GPU options:\n"
        "  --gpu-index <N>        GPU index (default: auto)\n"
        "  --shader-complexity <N> Vulkan shader complexity 1-5 (default: 1)\n"
        "  --adaptive-mode <m>    Adaptive mode: variable (default), switch\n"
        "  Modes: matrix_mul, fp64/matrix_fp64, fma, trig, vram, mixed, vulkan_3d, vulkan_adaptive\n"
        "\n"
        "PSU options:\n"
        "  --use-all-gpus         Use all available GPUs (default: first only)\n"
        "  Modes: steady (default), spike, ramp\n"
        "\n"
        "Benchmark options:\n"
        "  Modes: cache, memory, all (default)\n"
        "\n"
        "Schedule options:\n"
        "  --schedule <file>      Run a JSON schedule file\n"
        "  --stop-on-error        Stop schedule on first error\n"
        "\n"
        "Certificate options:\n"
        "  --cert-tier <tier>     Run certification: bronze, silver, gold, platinum\n"
        "\n"
        "Exit codes:\n"
        "  0 = PASS (all tests passed)\n"
        "  1 = FAIL (errors detected during test)\n"
        "  2 = ERROR (execution failure)\n"
        "\n"
        "Examples:\n"
        "  occt_native --cli --test cpu --mode avx2 --duration 3600 --threads auto\n"
        "  occt_native --cli --test cpu --mode all --load-pattern variable --duration 1800\n"
        "  occt_native --cli --test ram --memory 90 --passes 3 --duration 600\n"
        "  occt_native --cli --test storage --file-size 1024 --queue-depth 8 --storage-path /tmp\n"
        "  occt_native --cli --test gpu --mode vulkan_3d --gpu-index 0 --shader-complexity 3\n"
        "  occt_native --cli --test psu --mode spike --use-all-gpus --duration 600\n"
        "  occt_native --cli --test benchmark --mode all\n"
        "  occt_native --cli --schedule schedule.json --stop-on-error --report html --output ./results/\n"
        "  occt_native --cli --cert-tier gold --output ./cert/\n"
        "  occt_native --cli --monitor-only --csv sensors.csv --duration 60\n"
    );
}

} // namespace occt
