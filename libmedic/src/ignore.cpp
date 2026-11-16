// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/ignore.hpp"

#include "medic/jsondump.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace medic {

namespace {

using json = nlohmann::json;

int rank(Severity s) { return s == Severity::Critical ? 2 : (s == Severity::Warning ? 1 : 0); }

Severity severity_of(const std::string& s) {
    if (s == "critical") return Severity::Critical;
    if (s == "warning") return Severity::Warning;
    return Severity::Info;
}

}  // namespace

bool can_hide(Severity s) { return s != Severity::Critical; }

std::vector<IgnoreEntry> load_ignored(const IFileSystem& fs) {
    std::vector<IgnoreEntry> out;
    std::string text;
    if (!fs.read_file(kIgnorePath, text) || text.size() > (1u << 20)) return out;
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("entries") || !j["entries"].is_array()) return out;
    for (const auto& e : j["entries"]) {
        if (out.size() >= kIgnoreMaxEntries) break;
        if (!e.is_object() || !e.contains("rule_id") || !e["rule_id"].is_string() || e["rule_id"].get<std::string>().empty()) continue;
        IgnoreEntry x;
        x.rule_id = e["rule_id"].get<std::string>();
        x.severity = severity_of(e.contains("severity") && e["severity"].is_string() ? e["severity"].get<std::string>() : std::string());
        x.ts = e.contains("ts") && e["ts"].is_string() ? e["ts"].get<std::string>() : std::string();
        x.active = !(e.contains("active") && e["active"].is_boolean() && !e["active"].get<bool>());
        out.push_back(std::move(x));
    }
    return out;
}

bool save_ignored(Platform& p, const std::vector<IgnoreEntry>& entries) {
    IFileSystemRW* rw = p.fs_rw();
    if (!rw) return false;
    json arr = json::array();
    for (const auto& e : entries) arr.push_back({{"rule_id", e.rule_id}, {"severity", to_string(e.severity)}, {"ts", e.ts}, {"active", e.active}});
    return rw->write_file(kIgnorePath, dump_safe(json{{"version", 1}, {"entries", arr}}, 1) + "\n");
}

bool is_hidden(const std::string& rule_id, Severity now, const std::vector<IgnoreEntry>& entries) {
    if (!can_hide(now)) return false;
    for (const auto& e : entries)
        if (e.active && e.rule_id == rule_id) return rank(now) <= rank(e.severity);
    return false;
}

std::vector<IgnoreEntry> set_hidden(std::vector<IgnoreEntry> entries, const std::string& rule_id, Severity sev, const std::string& ts, bool hide) {
    if (hide && !can_hide(sev)) return entries;
    for (auto& e : entries)
        if (e.rule_id == rule_id) {
            e.active = hide;
            e.ts = ts;
            if (hide) e.severity = sev;
            return entries;
        }
    if (hide) entries.push_back({rule_id, sev, ts, true});
    return entries;
}

}  // namespace medic
