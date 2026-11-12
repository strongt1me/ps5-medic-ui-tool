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
    if (j.contains("metrics") && j["metrics"].is_object())
        for (auto it = j["metrics"].begin(); it != j["metrics"].end(); ++it)
            if (it.value().is_number()) run.metrics[it.key()] = it.value().get<double>();
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

HistoryRun make_history_run(const std::string& ts, const std::string& rules_version, const std::string& fw, const std::vector<Finding>& findings,
                            const std::vector<std::pair<std::string, double>>& metrics) {
    HistoryRun r;
    r.ts = ts;
    r.rules_version = rules_version;
    r.fw = fw;
    for (const auto& f : findings) r.findings.emplace_back(f.rule_id, to_string(f.severity));
    for (const auto& [id, v] : metrics) r.metrics[id] = v;
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
    json rec = {{"ts", run.ts}, {"rules", run.rules_version}, {"fw", run.fw}, {"findings", json::array()}, {"metrics", json::object()}};
    for (const auto& [id, v] : run.metrics) rec["metrics"][id] = v;
    for (const auto& [id, sev] : run.findings) rec["findings"].push_back({{"id", id}, {"sev", sev}});

    std::string text;
    // Eine vorhandene, aber nicht lesbare Verlaufsdatei nicht durch eine einzelne Zeile ersetzen
    if (!p.fs().read_file(kHistoryPath, text) && p.fs().exists(kHistoryPath)) return false;
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

std::vector<MetricChange> metric_changes(const RuleSet& rs, const std::vector<std::pair<std::string, double>>& now, const std::vector<HistoryRun>& history) {
    std::vector<MetricChange> out;
    for (const auto& [id, value] : now) {
        const auto def = std::find_if(rs.metrics.begin(), rs.metrics.end(), [&](const MetricItem& m) { return m.id == id; });
        if (def == rs.metrics.end()) continue;
        MetricChange c;
        c.id = id;
        c.unit = def->unit;
        c.text_de = def->text_de;
        c.text_en = def->text_en;
        c.now = value;
        std::vector<double> past;
        for (auto it = history.rbegin(); it != history.rend() && past.size() < kMetricWindow; ++it) {
            const auto m = it->metrics.find(id);
            if (m == it->metrics.end()) continue;
            if (past.empty()) {
                c.has_prev = true;
                c.prev = m->second;
            }
            past.push_back(m->second);
        }
        if (!past.empty()) {
            std::sort(past.begin(), past.end());
            const std::size_t n = past.size();
            c.has_typical = true;
            c.typical = n % 2 ? past[n / 2] : (past[n / 2 - 1] + past[n / 2]) / 2.0;
            c.samples = n;
        }
        out.push_back(std::move(c));
    }
    return out;
}

// urgency_of steht in finding.cpp (ohne Dateizugriff, auch in der nativen App)

}  // namespace medic
