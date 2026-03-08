#include "preset_schedules.h"

namespace occt {

QVector<TestStep> preset_quick_check()
{
    return {
        { "cpu", {{"mode", "avx2"}}, 180, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.50}}, 120, false }
    };
}

QVector<TestStep> preset_standard()
{
    return {
        { "cpu", {{"mode", "avx2"}}, 600, false },
        { "gpu", {{"mode", "matrix"}}, 600, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.70}}, 600, false }
    };
}

QVector<TestStep> preset_extreme()
{
    return {
        { "cpu", {{"mode", "avx2"}}, 1200, true },  // parallel with RAM
        { "ram", {{"mode", "random"}, {"memory_pct", 0.80}}, 1200, false },
        { "gpu", {{"mode", "mixed"}}, 1200, false },
        { "storage", {{"mode", "mixed"}}, 1200, false }
    };
}

QVector<TestStep> preset_oc_validation()
{
    return {
        { "cpu", {{"mode", "linpack"}}, 3600, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.80}}, 3600, false }
    };
}

// --- Certification tier schedules ---

QVector<TestStep> preset_cert_bronze()
{
    return {
        { "cpu", {{"mode", "sse"}}, 1800, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.70}}, 1800, false }
    };
}

QVector<TestStep> preset_cert_silver()
{
    return {
        { "cpu", {{"mode", "avx2"}}, 3600, false },
        { "gpu", {{"mode", "matrix"}}, 3600, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.70}}, 3600, false }
    };
}

QVector<TestStep> preset_cert_gold()
{
    return {
        { "cpu", {{"mode", "linpack"}}, 7200, false },
        { "gpu", {{"mode", "vram"}}, 3600, false },
        { "ram", {{"mode", "random"}, {"memory_pct", 0.80}}, 7200, false },
        { "storage", {{"mode", "fill_verify"}}, 3600, false }
    };
}

QVector<TestStep> preset_cert_platinum()
{
    return {
        { "cpu", {{"mode", "avx2"}}, 18000, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.85}}, 18000, false },
        { "gpu", {{"mode", "mixed"}}, 7200, false },
        { "storage", {{"mode", "verify_seq"}}, 7200, false }
    };
}

} // namespace occt
