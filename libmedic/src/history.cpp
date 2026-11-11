// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/history.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace medic {
namespace {

using json = nlohmann::json;

bool valid_ts(const std::string& ts) {
    return !ts.empty() && ts.size() <= 40 && std::all_of(ts.begin(), ts.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        if (!line.empty()) out.push_back(line);
    return out;
}

bool parse_run(const std::string& line, HistoryRun& run) {
    const json j = json::parse(line, nullptr, false);
    if (!j.is_object() || !j.contains("ts") || !j["ts"].is_string() || !j.contains("findings") || !j["findings"].is_array()) return false;
    run = HistoryRun{};
    run.ts = j["ts"].get<std::string>();
    if (j.contains("rules") && j["rules"].is_string()) run.rules_version = j["rules"].get<std::string>();
    if (j.contains("fw") && j["fw"].is_string()) run.fw = j["fw"].get<std::string>();
    for (const auto& f : j["findings"])
        if (f.is_object() && f.contains("id") && f["id"].is_string())
            run.findings.emplace_back(f["id"].get<std::string>(), f.contains("sev") && f["sev"].is_string() ? f["sev"].get<std::string>() : std::string());
    return true;
}

bool has_rule(const HistoryRun& r, const std::string& id) {
    return std::any_of(r.findings.begin(), r.findings.end(), [&](const auto& f) { return f.first == id; });
}

}  // namespace

std::vector<HistoryRun> load_history(const IFileSystem& fs) {
    std::vector<HistoryRun> out;
    std::string text;
    if (!fs.read_file(kHistoryPath, text)) return out;
    for (const auto& line : lines_of(text)) {
        HistoryRun r;
        if (parse_run(line, r)) out.push_back(std::move(r));
    }
    if (out.size() > kHistoryMaxRuns) out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(kHistoryMaxRuns));
    return out;
}

HistoryRun make_history_run(const std::string& ts, const std::string& rules_version, const std::string& fw, const std::vector<Finding>& findings) {
    HistoryRun r;
    r.ts = ts;
    r.rules_version = rules_version;
    r.fw = fw;
    for (const auto& f : findings) r.findings.emplace_back(f.rule_id, to_string(f.severity));
    return r;
}

std::vector<std::string> annotate_with_history(std::vector<Finding>& findings, const std::vector<HistoryRun>& history, const std::string& now_ts) {
    std::vector<std::string> resolved;
    if (history.empty()) return resolved;
    const HistoryRun& last = history.back();
    for (auto& f : findings) {
        if (!has_rule(last, f.rule_id)) {
            f.status = "new";
            f.first_seen = now_ts;
            f.seen_runs = 1;
            continue;
        }
        std::size_t streak = 0;  // Pruefungen in Folge vor dieser, in denen die Regel gemeldet wurde
        while (streak < history.size() && has_rule(history[history.size() - 1 - streak], f.rule_id)) ++streak;
        f.status = "recurring";
        f.first_seen = history[history.size() - streak].ts;
        f.seen_runs = static_cast<int>(streak) + 1;
    }
    std::set<std::string> now;
    for (const auto& f : findings) now.insert(f.rule_id);
    for (const auto& [id, sev] : last.findings)
        if (!now.count(id)) resolved.push_back(id);
    std::sort(resolved.begin(), resolved.end());
    resolved.erase(std::unique(resolved.begin(), resolved.end()), resolved.end());
    return resolved;
}

bool append_history(Platform& p, const HistoryRun& run) {
    IFileSystemRW* rw = p.fs_rw();
    if (!rw || !valid_ts(run.ts)) return false;
    json rec = {{"ts", run.ts}, {"rules", run.rules_version}, {"fw", run.fw}, {"findings", json::array()}};
    for (const auto& [id, sev] : run.findings) rec["findings"].push_back({{"id", id}, {"sev", sev}});

    std::string text;
    p.fs().read_file(kHistoryPath, text);
    if (text.size() > kHistoryMaxBytes) {
        // Beiseite legen statt kuerzen: die alte Datei bleibt vollstaendig, die neue beginnt mit den juengsten Pruefungen.
        const std::string aside = std::string("/data/ps5medic/history_") + run.ts + ".jsonl";
        if (!rw->rename(kHistoryPath, aside)) return false;
        const auto lines = lines_of(text);
        const std::size_t from = lines.size() > kHistoryKeepRuns ? lines.size() - kHistoryKeepRuns : 0;
        text.clear();
        for (std::size_t i = from; i < lines.size(); ++i) text += lines[i] + "\n";
    }
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += rec.dump() + "\n";
    return rw->write_file(kHistoryPath, text);
}

std::string urgency_of(const Finding& f) {
    int level = f.severity == Severity::Critical ? 2 : (f.severity == Severity::Warning ? 1 : 0);
    if (f.status == "recurring" && f.seen_runs >= 3 && effective_confidence(f).level != "low") level = std::min(2, level + 1);
    return level == 2 ? "high" : (level == 1 ? "medium" : "low");
}

}  // namespace medic
