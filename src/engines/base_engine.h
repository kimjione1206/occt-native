#pragma once

#include <string>

namespace occt {

/// Abstract base interface for all stress-test engines.
/// Allows SafetyGuardian to uniformly stop any registered engine.
class IEngine {
public:
    virtual ~IEngine() = default;

    /// Immediately stop the stress test.
    virtual void stop() = 0;

    /// Returns true if the engine is currently running a test.
    virtual bool is_running() const = 0;

    /// Human-readable engine name (e.g. "RAM", "Storage", "CPU").
    virtual std::string name() const = 0;
};

} // namespace occt
