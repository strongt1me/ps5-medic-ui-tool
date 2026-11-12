// SPDX-License-Identifier: GPL-3.0-or-later
// Kleine Hilfen zu Befunden ohne Dateizugriff und ohne Ausnahmen (auch von der nativen App gebraucht).
#include "medic/rules.hpp"
#include "medic/history.hpp"
#include "medic/selftest.hpp"

#include <algorithm>

namespace medic {

const char* to_string(Severity s) {
    switch (s) {
        case Severity::Critical: return "critical";
        case Severity::Warning: return "warning";
        case Severity::Info: return "info";
    }
    return "info";
}

Confidence default_confidence(const std::string& verified) {
    Confidence c;
    if (verified == "doc") {
        c.level = "high";
        c.basis = "doc";
    } else if (verified == "community") {
        c.level = "medium";
        c.basis = "community";
    } else {
        c.level = "low";
        c.basis = "unknown";
    }
    return c;
}

Confidence effective_confidence(const Finding& f) {
    Confidence c = f.confidence;
    const Confidence d = default_confidence(f.verified);
    if (c.level.empty()) c.level = d.level;
    if (c.basis.empty()) c.basis = d.basis;
    return c;
}

const char* to_string(TestStatus s) {
    switch (s) {
        case TestStatus::NotRun: return "not_run";
        case TestStatus::Passed: return "passed";
        case TestStatus::Warn: return "warn";
        case TestStatus::Failed: return "failed";
        case TestStatus::Error: return "error";
        case TestStatus::Cancelled: return "cancelled";
        case TestStatus::Skipped: return "skipped";
    }
    return "not_run";
}

std::string urgency_of(const Finding& f) {
    int level = f.severity == Severity::Critical ? 2 : (f.severity == Severity::Warning ? 1 : 0);
    if (f.status == "recurring" && f.seen_runs >= 3 && effective_confidence(f).level != "low") level = std::min(2, level + 1);
    return level == 2 ? "high" : (level == 1 ? "medium" : "low");
}

}  // namespace medic
