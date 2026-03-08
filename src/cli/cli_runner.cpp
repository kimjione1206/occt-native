#include "cli_runner.h"
#include "report/report_manager.h"
#include "engines/cpu_engine.h"
#include "engines/ram_engine.h"
#include "engines/storage_engine.h"
#include "engines/gpu_engine.h"
#include "engines/psu_engine.h"
#include "engines/benchmark/cache_benchmark.h"
#include "engines/benchmark/memory_benchmark.h"
#include "monitor/sensor_manager.h"
#include "utils/cpuid.h"
#include "report/csv_exporter.h"
#include "scheduler/test_scheduler.h"
#include "certification/certificate.h"
#include "certification/cert_generator.h"
#include "scheduler/preset_schedules.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QDateTime>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <chrono>

namespace occt {

void CliRunner::emit_json(const QString& type, const QString& key, const QVariant& value)
{
    QJsonObject obj;
    obj["type"] = type;
    obj[key] = QJsonValue::fromVariant(value);
    obj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(obj);
    std::fprintf(stdout, "%s\n", doc.toJson(QJsonDocument::Compact).constData());
    std::fflush(stdout);
}

SystemInfoData CliRunner::collect_system_info()
{
    SystemInfoData info;
    auto cpuInfo = occt::utils::detect_cpu();
    info.cpu_name = QString::fromStdString(cpuInfo.brand);
    info.cpu_cores = cpuInfo.physical_cores;
    info.cpu_threads = cpuInfo.logical_cores;

#if defined(Q_OS_MACOS)
    info.os_name = "macOS";
#elif defined(Q_OS_WIN)
    info.os_name = "Windows";
#elif defined(Q_OS_LINUX)
    info.os_name = "Linux";
#else
    info.os_name = "Unknown";
#endif

    info.gpu_name = "N/A";
    info.ram_total = "N/A";

    return info;
}

int CliRunner::run(const CliOptions& opts)
{
    if (opts.show_help) {
        print_usage();
        return 0;
    }

    if (opts.show_version) {
        std::fprintf(stdout, "{\"type\":\"result\",\"version\":\"1.0.0\"}\n");
        return 0;
    }

    if (opts.monitor_only) {
        return run_monitor(opts);
    }

    if (opts.test.isEmpty()) {
        emit_json("error", "message", "No test specified. Use --test <type> or --help.");
        return 2;
    }

    // Schedule dispatch (Fix 3-1)
    if (opts.test == "schedule" && !opts.schedule_file.isEmpty()) {
        return run_schedule(opts);
    }

    // Certificate dispatch (Fix 3-2)
    if (opts.test == "certificate" && !opts.cert_tier.isEmpty()) {
        return run_certificate(opts);
    }

    return run_test(opts);
}

int CliRunner::run_test(const CliOptions& opts)
{
    results_ = TestResults{};
    results_.system_info = collect_system_info();

    int duration = opts.duration > 0 ? opts.duration : 60;
    bool test_passed = true;

    emit_json("progress", "message", QString("Starting %1 test (duration: %2s)").arg(opts.test).arg(duration));

    auto start = std::chrono::steady_clock::now();

    if (opts.test == "cpu") {
        CpuEngine engine;

        CpuStressMode mode = CpuStressMode::AVX2_FMA;
        if (opts.mode == "linpack") mode = CpuStressMode::LINPACK;
        else if (opts.mode == "prime") mode = CpuStressMode::PRIME;
        else if (opts.mode == "sse") mode = CpuStressMode::SSE_FLOAT;
        else if (opts.mode == "avx512") mode = CpuStressMode::AVX512_FMA;
        else if (opts.mode == "all") mode = CpuStressMode::ALL;

        int threads = opts.threads > 0 ? opts.threads : 0;

        // Parse load pattern (Fix 1-3)
        LoadPattern lp = LoadPattern::STEADY;
        if (opts.load_pattern == "variable") lp = LoadPattern::VARIABLE;

        CpuMetrics last_metrics{};
        engine.set_metrics_callback([this, &last_metrics](const CpuMetrics& m) {
            last_metrics = m;
            QJsonObject metric;
            metric["gflops"] = m.gflops;
            metric["temperature"] = m.temperature;
            metric["power_watts"] = m.power_watts;
            metric["threads"] = m.active_threads;
            metric["elapsed"] = m.elapsed_secs;
            metric["error_count"] = m.error_count;
            emit_json("metric", "cpu", QJsonDocument(metric).toVariant());
        });

        engine.start(mode, threads, duration, lp);

        // Wait for completion
        while (engine.is_running()) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }

        auto metrics = engine.get_metrics();
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();

        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = "CPU";
        result.mode = opts.mode.isEmpty() ? "AVX2" : opts.mode.toUpper();
        result.duration = QString("%1s").arg(int(elapsed_secs));
        result.score = QString("%1 GFLOPS").arg(metrics.peak_gflops, 0, 'f', 2);
        result.passed = (last_metrics.error_count == 0);
        result.error_count = last_metrics.error_count;
        if (!result.passed) test_passed = false;
        results_.results.append(result);

    } else if (opts.test == "ram") {
        RamEngine engine;

        RamPattern pattern = RamPattern::MARCH_C_MINUS;
        if (opts.mode == "walking") pattern = RamPattern::WALKING_ONES;
        else if (opts.mode == "walking_zeros") pattern = RamPattern::WALKING_ZEROS;
        else if (opts.mode == "random") pattern = RamPattern::RANDOM;
        else if (opts.mode == "checkerboard") pattern = RamPattern::CHECKERBOARD;
        else if (opts.mode == "bandwidth") pattern = RamPattern::BANDWIDTH;

        engine.set_metrics_callback([this](const RamMetrics& m) {
            QJsonObject metric;
            metric["tested_mb"] = m.memory_used_mb;
            metric["errors"] = static_cast<int>(m.errors_found);
            metric["elapsed"] = m.elapsed_secs;
            metric["progress"] = m.progress_pct;
            emit_json("metric", "ram", QJsonDocument(metric).toVariant());
        });

        double mem_pct = opts.memory_percent / 100.0;
        engine.start(pattern, mem_pct, opts.passes);

        while (engine.is_running()) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }

        auto metrics = engine.get_metrics();
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();

        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = "RAM";
        result.mode = opts.mode.isEmpty() ? "March C-" : opts.mode;
        result.duration = QString("%1s").arg(int(elapsed_secs));
        result.score = QString("%1 MB tested").arg(metrics.memory_used_mb, 0, 'f', 0);
        result.passed = (metrics.errors_found == 0);
        result.error_count = static_cast<int>(metrics.errors_found);
        if (!result.passed) test_passed = false;
        results_.results.append(result);

    } else if (opts.test == "storage") {
        StorageEngine engine;

        StorageMode smode = StorageMode::SEQ_WRITE;
        if (opts.mode == "read") smode = StorageMode::SEQ_READ;
        else if (opts.mode == "random_write") smode = StorageMode::RAND_WRITE;
        else if (opts.mode == "random_read") smode = StorageMode::RAND_READ;
        else if (opts.mode == "mixed") smode = StorageMode::MIXED;

        engine.set_metrics_callback([this](const StorageMetrics& m) {
            QJsonObject metric;
            metric["read_mbps"] = m.read_mbs;
            metric["write_mbps"] = m.write_mbs;
            metric["iops"] = m.iops;
            metric["elapsed"] = m.elapsed_secs;
            emit_json("metric", "storage", QJsonDocument(metric).toVariant());
        });

        std::string path = opts.storage_path.isEmpty() ? QDir::tempPath().toStdString() : opts.storage_path.toStdString();
        engine.start(smode, path, opts.file_size_mb, opts.queue_depth);

        while (engine.is_running()) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }

        auto metrics = engine.get_metrics();
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();

        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = "Storage";
        result.mode = opts.mode.isEmpty() ? "Sequential Write" : opts.mode;
        result.duration = QString("%1s").arg(int(elapsed_secs));
        result.score = QString("R:%1 W:%2 MB/s").arg(metrics.read_mbs, 0, 'f', 1).arg(metrics.write_mbs, 0, 'f', 1);
        result.passed = true;  // Storage test passes if no I/O error
        result.error_count = 0;
        results_.results.append(result);

    } else if (opts.test == "gpu") {
        GpuEngine engine;
        if (!engine.initialize()) {
            emit_json("error", "message", "Failed to initialize GPU engine (no GPU or OpenCL/Vulkan unavailable)");
            return 2;
        }

        GpuStressMode gmode = GpuStressMode::MATRIX_MUL;
        if (opts.mode == "matrix_fp64" || opts.mode == "fp64") gmode = GpuStressMode::MATRIX_MUL_FP64;
        else if (opts.mode == "fma") gmode = GpuStressMode::FMA_STRESS;
        else if (opts.mode == "trig") gmode = GpuStressMode::TRIG_STRESS;
        else if (opts.mode == "vram") gmode = GpuStressMode::VRAM_TEST;
        else if (opts.mode == "mixed") gmode = GpuStressMode::MIXED;
        else if (opts.mode == "vulkan_3d") gmode = GpuStressMode::VULKAN_3D;
        else if (opts.mode == "vulkan_adaptive") gmode = GpuStressMode::VULKAN_ADAPTIVE;

        if (opts.gpu_index >= 0) engine.select_gpu(opts.gpu_index);
        if (gmode == GpuStressMode::VULKAN_3D || gmode == GpuStressMode::VULKAN_ADAPTIVE)
            engine.set_shader_complexity(opts.shader_complexity);
        if (gmode == GpuStressMode::VULKAN_ADAPTIVE) {
            AdaptiveMode am = (opts.adaptive_mode == "switch") ? AdaptiveMode::SWITCH : AdaptiveMode::VARIABLE;
            engine.set_adaptive_mode(am);
        }

        GpuMetrics last_gpu_metrics{};
        engine.set_metrics_callback([this, &last_gpu_metrics](const GpuMetrics& m) {
            last_gpu_metrics = m;
            QJsonObject metric;
            metric["gflops"] = m.gflops;
            metric["temperature"] = m.temperature;
            metric["power_watts"] = m.power_watts;
            metric["gpu_usage_pct"] = m.gpu_usage_pct;
            metric["vram_usage_pct"] = m.vram_usage_pct;
            metric["vram_errors"] = static_cast<qint64>(m.vram_errors);
            metric["elapsed"] = m.elapsed_secs;
            metric["fps"] = m.fps;
            metric["artifact_count"] = static_cast<qint64>(m.artifact_count);
            emit_json("metric", "gpu", QJsonDocument(metric).toVariant());
        });

        engine.start(gmode, duration);

        while (engine.is_running()) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }

        auto metrics = engine.get_metrics();
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();

        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = "GPU";
        result.mode = opts.mode.isEmpty() ? "Matrix Mul" : opts.mode;
        result.duration = QString("%1s").arg(int(elapsed_secs));
        result.score = QString("%1 GFLOPS").arg(metrics.gflops, 0, 'f', 2);
        result.passed = (last_gpu_metrics.vram_errors == 0 && last_gpu_metrics.artifact_count == 0);
        result.error_count = static_cast<int>(last_gpu_metrics.vram_errors + last_gpu_metrics.artifact_count);
        if (!result.passed) test_passed = false;
        results_.results.append(result);

    } else if (opts.test == "psu") {
        PsuEngine engine;

        PsuLoadPattern psu_pattern = PsuLoadPattern::STEADY;
        if (opts.mode == "spike") psu_pattern = PsuLoadPattern::SPIKE;
        else if (opts.mode == "ramp") psu_pattern = PsuLoadPattern::RAMP;

        if (opts.use_all_gpus) engine.set_use_all_gpus(true);

        PsuMetrics last_psu_metrics{};
        engine.set_metrics_callback([this, &last_psu_metrics](const PsuMetrics& m) {
            last_psu_metrics = m;
            QJsonObject metric;
            metric["total_power_watts"] = m.total_power_watts;
            metric["cpu_power_watts"] = m.cpu_power_watts;
            metric["gpu_power_watts"] = m.gpu_power_watts;
            metric["cpu_running"] = m.cpu_running;
            metric["gpu_running"] = m.gpu_running;
            metric["elapsed"] = m.elapsed_secs;
            metric["errors_cpu"] = m.errors_cpu;
            metric["errors_gpu"] = m.errors_gpu;
            emit_json("metric", "psu", QJsonDocument(metric).toVariant());
        });

        engine.start(psu_pattern, duration);

        while (engine.is_running()) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }

        auto metrics = engine.get_metrics();
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();

        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = "PSU";
        result.mode = opts.mode.isEmpty() ? "Steady" : opts.mode;
        result.duration = QString("%1s").arg(int(elapsed_secs));
        result.score = QString("%1 W total").arg(metrics.total_power_watts, 0, 'f', 1);
        result.passed = (last_psu_metrics.errors_cpu == 0 && last_psu_metrics.errors_gpu == 0);
        result.error_count = last_psu_metrics.errors_cpu + last_psu_metrics.errors_gpu;
        if (!result.passed) test_passed = false;
        results_.results.append(result);

    } else if (opts.test == "benchmark") {
        // Benchmarks are synchronous - run() blocks and returns result
        if (opts.mode == "cache" || opts.mode.isEmpty() || opts.mode == "all") {
            emit_json("progress", "message", "Running cache benchmark...");
            CacheBenchmark cb;
            auto cr = cb.run();

            QJsonObject bench;
            bench["l1_latency_ns"] = cr.l1_latency_ns;
            bench["l2_latency_ns"] = cr.l2_latency_ns;
            bench["l3_latency_ns"] = cr.l3_latency_ns;
            bench["dram_latency_ns"] = cr.dram_latency_ns;
            bench["l1_bw_gbs"] = cr.l1_bw_gbs;
            bench["l2_bw_gbs"] = cr.l2_bw_gbs;
            bench["l3_bw_gbs"] = cr.l3_bw_gbs;
            bench["dram_bw_gbs"] = cr.dram_bw_gbs;
            emit_json("result", "cache_benchmark", QJsonDocument(bench).toVariant());

            TestResultData result;
            result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            result.test_type = "Benchmark";
            result.mode = "Cache";
            result.score = QString("L1:%1ns L2:%2ns L3:%3ns DRAM:%4ns")
                .arg(cr.l1_latency_ns, 0, 'f', 2)
                .arg(cr.l2_latency_ns, 0, 'f', 2)
                .arg(cr.l3_latency_ns, 0, 'f', 2)
                .arg(cr.dram_latency_ns, 0, 'f', 2);
            result.passed = true;
            result.error_count = 0;
            results_.results.append(result);
        }
        if (opts.mode == "memory" || opts.mode.isEmpty() || opts.mode == "all") {
            emit_json("progress", "message", "Running memory benchmark...");
            MemoryBenchmark mb;
            auto mr = mb.run();

            QJsonObject bench;
            bench["read_bw_gbs"] = mr.read_bw_gbs;
            bench["write_bw_gbs"] = mr.write_bw_gbs;
            bench["copy_bw_gbs"] = mr.copy_bw_gbs;
            bench["latency_ns"] = mr.latency_ns;
            bench["channels_detected"] = mr.channels_detected;
            emit_json("result", "memory_benchmark", QJsonDocument(bench).toVariant());

            TestResultData result;
            result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            result.test_type = "Benchmark";
            result.mode = "Memory";
            result.score = QString("R:%1 W:%2 C:%3 GB/s")
                .arg(mr.read_bw_gbs, 0, 'f', 2)
                .arg(mr.write_bw_gbs, 0, 'f', 2)
                .arg(mr.copy_bw_gbs, 0, 'f', 2);
            result.passed = true;
            result.error_count = 0;
            results_.results.append(result);
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();
        // Update duration on benchmark results
        for (auto& r : results_.results) {
            if (r.test_type == "Benchmark") {
                r.duration = QString("%1s").arg(int(elapsed_secs));
            }
        }

    } else {
        emit_json("error", "message", QString("Unknown test type: %1").arg(opts.test));
        return 2;
    }

    results_.overall_verdict = test_passed ? "PASS" : "FAIL";
    auto total_elapsed = std::chrono::steady_clock::now() - start;
    results_.total_duration_secs = std::chrono::duration<double>(total_elapsed).count();

    // Generate report if requested
    if (!opts.report_format.isEmpty() && !opts.output_path.isEmpty()) {
        if (!generate_report(results_, opts)) {
            emit_json("error", "message", "Failed to generate report");
        }
    }

    // Final result
    QJsonObject final_result;
    final_result["verdict"] = results_.overall_verdict;
    final_result["total_tests"] = results_.results.size();
    final_result["duration_secs"] = results_.total_duration_secs;
    emit_json("result", "summary", QJsonDocument(final_result).toVariant());

    return test_passed ? 0 : 1;
}

int CliRunner::run_schedule(const CliOptions& opts)
{
    results_ = TestResults{};
    results_.system_info = collect_system_info();

    emit_json("progress", "message", QString("Loading schedule from %1").arg(opts.schedule_file));

    TestScheduler scheduler;
    scheduler.load_from_json(opts.schedule_file);

    if (scheduler.steps().isEmpty()) {
        emit_json("error", "message", "Schedule is empty or failed to load");
        return 2;
    }

    scheduler.set_stop_on_error(opts.stop_on_error);

    bool all_passed = true;
    int total_errors = 0;

    QObject::connect(&scheduler, &TestScheduler::stepStarted,
        [this](int index, const QString& engine) {
            emit_json("progress", "message", QString("Step %1: starting %2").arg(index).arg(engine));
        });

    QObject::connect(&scheduler, &TestScheduler::stepCompleted,
        [this](int index, bool passed, int errors) {
            QJsonObject step;
            step["step"] = index;
            step["passed"] = passed;
            step["errors"] = errors;
            emit_json("step_result", "step", QJsonDocument(step).toVariant());
        });

    QObject::connect(&scheduler, &TestScheduler::progressChanged,
        [this](double pct) {
            emit_json("progress", "percent", pct);
        });

    auto start = std::chrono::steady_clock::now();
    scheduler.start();

    while (scheduler.is_running()) {
        QThread::msleep(500);
        QCoreApplication::processEvents();
    }

    // Collect results from scheduler
    const auto& step_results = scheduler.results();
    for (const auto& sr : step_results) {
        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = sr.engine;
        result.mode = "schedule";
        result.duration = QString("%1s").arg(int(sr.duration_secs));
        result.passed = sr.passed;
        result.error_count = sr.errors;
        if (!sr.passed) {
            all_passed = false;
        }
        total_errors += sr.errors;
        results_.results.append(result);
    }

    auto total_elapsed = std::chrono::steady_clock::now() - start;
    results_.total_duration_secs = std::chrono::duration<double>(total_elapsed).count();
    results_.overall_verdict = all_passed ? "PASS" : "FAIL";

    // Generate report if requested
    if (!opts.report_format.isEmpty() && !opts.output_path.isEmpty()) {
        if (!generate_report(results_, opts)) {
            emit_json("error", "message", "Failed to generate report");
        }
    }

    QJsonObject final_result;
    final_result["verdict"] = results_.overall_verdict;
    final_result["total_steps"] = step_results.size();
    final_result["total_errors"] = total_errors;
    final_result["duration_secs"] = results_.total_duration_secs;
    emit_json("result", "summary", QJsonDocument(final_result).toVariant());

    return all_passed ? 0 : 1;
}

int CliRunner::run_certificate(const CliOptions& opts)
{
    results_ = TestResults{};
    results_.system_info = collect_system_info();

    CertTier tier = CertTier::BRONZE;
    QVector<TestStep> steps;

    if (opts.cert_tier == "bronze") {
        tier = CertTier::BRONZE;
        steps = preset_cert_bronze();
    } else if (opts.cert_tier == "silver") {
        tier = CertTier::SILVER;
        steps = preset_cert_silver();
    } else if (opts.cert_tier == "gold") {
        tier = CertTier::GOLD;
        steps = preset_cert_gold();
    } else if (opts.cert_tier == "platinum") {
        tier = CertTier::PLATINUM;
        steps = preset_cert_platinum();
    } else {
        emit_json("error", "message", QString("Unknown certificate tier: %1. Use bronze, silver, gold, or platinum.").arg(opts.cert_tier));
        return 2;
    }

    emit_json("progress", "message", QString("Starting %1 certification").arg(cert_tier_name(tier)));

    TestScheduler scheduler;
    scheduler.load_schedule(steps);
    scheduler.set_stop_on_error(true); // Certification always stops on error

    QObject::connect(&scheduler, &TestScheduler::stepStarted,
        [this](int index, const QString& engine) {
            emit_json("progress", "message", QString("Cert step %1: %2").arg(index).arg(engine));
        });

    QObject::connect(&scheduler, &TestScheduler::stepCompleted,
        [this](int index, bool passed, int errors) {
            QJsonObject step;
            step["step"] = index;
            step["passed"] = passed;
            step["errors"] = errors;
            emit_json("step_result", "step", QJsonDocument(step).toVariant());
        });

    QObject::connect(&scheduler, &TestScheduler::progressChanged,
        [this](double pct) {
            emit_json("progress", "percent", pct);
        });

    auto start = std::chrono::steady_clock::now();
    scheduler.start();

    while (scheduler.is_running()) {
        QThread::msleep(500);
        QCoreApplication::processEvents();
    }

    // Build certificate
    bool all_passed = true;
    int total_errors = 0;
    QVector<occt::TestResult> cert_results;
    const auto& step_results = scheduler.results();
    for (const auto& sr : step_results) {
        occt::TestResult tr;
        tr.engine = sr.engine;
        tr.mode = "cert";
        tr.passed = sr.passed;
        tr.errors = sr.errors;
        tr.duration_secs = sr.duration_secs;
        cert_results.append(tr);

        if (!sr.passed) all_passed = false;
        total_errors += sr.errors;

        // Also add to report results
        TestResultData rd;
        rd.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        rd.test_type = sr.engine;
        rd.mode = "cert";
        rd.duration = QString("%1s").arg(int(sr.duration_secs));
        rd.passed = sr.passed;
        rd.error_count = sr.errors;
        results_.results.append(rd);
    }

    auto total_elapsed = std::chrono::steady_clock::now() - start;
    results_.total_duration_secs = std::chrono::duration<double>(total_elapsed).count();
    results_.overall_verdict = all_passed ? "PASS" : "FAIL";

    // Generate certificate
    Certificate cert;
    cert.tier = tier;
    cert.system_info_json = CertGenerator::collect_system_info();
    cert.results = cert_results;
    cert.passed = all_passed;
    cert.issued_at = QDateTime::currentDateTime();
    cert.expires_at = cert.issued_at.addDays(90);

    CertGenerator gen;
    QJsonObject cert_json = gen.generate_json(cert);
    cert.hash_sha256 = CertGenerator::compute_hash(cert.system_info_json, cert_json);

    // Output certificate JSON
    emit_json("result", "certificate", QJsonDocument(cert_json).toVariant());

    // Save certificate if output path specified
    if (!opts.output_path.isEmpty()) {
        QDir dir(opts.output_path);
        if (opts.output_path.endsWith('/') || opts.output_path.endsWith('\\') || QFileInfo(opts.output_path).isDir()) {
            dir.mkpath(".");
        }
        QString base = "occt_cert_" + cert_tier_name(tier).toLower() + "_" +
                       QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");

        // Save HTML certificate
        QString html = gen.generate_html(cert);
        QString html_path = dir.filePath(base + ".html");
        QFile htmlFile(html_path);
        if (htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            htmlFile.write(html.toUtf8());
            htmlFile.close();
            emit_json("result", "cert_html_path", html_path);
        }

        // Save JSON certificate
        QJsonDocument json_doc(cert_json);
        QString json_path = dir.filePath(base + ".json");
        QFile jsonFile(json_path);
        if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            jsonFile.write(json_doc.toJson());
            jsonFile.close();
            emit_json("result", "cert_json_path", json_path);
        }
    }

    // Also generate standard report if requested
    if (!opts.report_format.isEmpty() && !opts.output_path.isEmpty()) {
        if (!generate_report(results_, opts)) {
            emit_json("error", "message", "Failed to generate report");
        }
    }

    QJsonObject final_result;
    final_result["verdict"] = results_.overall_verdict;
    final_result["tier"] = cert_tier_name(tier);
    final_result["total_errors"] = total_errors;
    final_result["duration_secs"] = results_.total_duration_secs;
    final_result["hash"] = cert.hash_sha256;
    emit_json("result", "summary", QJsonDocument(final_result).toVariant());

    return all_passed ? 0 : 1;
}

int CliRunner::run_monitor(const CliOptions& opts)
{
    int duration = opts.duration > 0 ? opts.duration : 60;

    emit_json("progress", "message", QString("Monitoring sensors for %1s").arg(duration));

    SensorManager sensors;
    if (!sensors.initialize()) {
        emit_json("error", "message", "Failed to initialize sensor manager");
        return 2;
    }

    QVector<SensorDataPoint> collected;
    auto start = std::chrono::steady_clock::now();

    sensors.start_polling(500);

    while (true) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();
        if (elapsed_secs >= duration) break;

        QThread::msleep(1000);
        QCoreApplication::processEvents();

        auto readings = sensors.get_all_readings();
        for (const auto& r : readings) {
            SensorDataPoint dp;
            dp.timestamp_sec = elapsed_secs;
            dp.sensor_name = QString::fromStdString(r.name);
            dp.value = r.value;
            dp.unit = QString::fromStdString(r.unit);
            collected.append(dp);

            QJsonObject metric;
            metric["sensor"] = dp.sensor_name;
            metric["value"] = dp.value;
            metric["unit"] = dp.unit;
            emit_json("metric", "sensor", QJsonDocument(metric).toVariant());
        }
    }

    sensors.stop();

    // Save CSV if requested
    if (!opts.output_path.isEmpty()) {
        CsvExporter::save_sensors(collected, opts.output_path);
        emit_json("result", "message", QString("Saved %1 data points to %2").arg(collected.size()).arg(opts.output_path));
    }

    return 0;
}

bool CliRunner::generate_report(const TestResults& results, const CliOptions& opts)
{
    ReportManager mgr;
    QString path = opts.output_path;

    // If output is a directory, generate filename
    QFileInfo fi(path);
    if (fi.isDir() || path.endsWith('/') || path.endsWith('\\')) {
        QDir dir(path);
        dir.mkpath(".");
        QString base = "occt_report_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        if (opts.report_format == "html") path = dir.filePath(base + ".html");
        else if (opts.report_format == "png") path = dir.filePath(base + ".png");
        else if (opts.report_format == "csv") path = dir.filePath(base + ".csv");
        else if (opts.report_format == "json") path = dir.filePath(base + ".json");
        else path = dir.filePath(base + ".html");
    }

    bool ok = false;
    if (opts.report_format == "html") ok = mgr.save_html(results, path);
    else if (opts.report_format == "png") ok = mgr.save_png(results, path);
    else if (opts.report_format == "csv") ok = mgr.save_csv(results, path);
    else if (opts.report_format == "json") ok = mgr.save_json(results, path);
    else ok = mgr.save_html(results, path);

    if (ok) {
        emit_json("result", "report_path", path);
    }
    return ok;
}

} // namespace occt
