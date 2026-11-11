// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/checks.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <set>

namespace medic {

namespace {

using json = nlohmann::json;

bool valid_id(const std::string& id) {
    return !id.empty() && id.size() <= 48 && std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

Severity severity_from(const std::string& s) {
    if (s == "critical") return Severity::Critical;
    if (s == "warning") return Severity::Warning;
    return Severity::Info;
}

std::string str_of(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }

json to_json(const StoredCheck& c) {
    json f = json::array();
    for (const auto& x : c.findings) {
        f.push_back({{"rule_id", x.rule_id}, {"severity", to_string(x.severity)}, {"text_de", x.text_de}, {"text_en", x.text_en}, {"evidence", x.evidence}, {"evidence_path", x.evidence_path},
                     {"fix", x.fix}, {"fix_params", x.fix_params}, {"source", x.source}, {"verified", x.verified},
                     {"confidence", {{"level", x.confidence.level}, {"basis", x.confidence.basis}, {"reason_de", x.confidence.reason_de}, {"reason_en", x.confidence.reason_en}}},
                     {"status", x.status}, {"first_seen", x.first_seen}, {"seen_runs", x.seen_runs}, {"confirm_runs", x.confirm_runs}});
    }
    json cov = json::array();
    for (const auto& x : c.coverage) cov.push_back({{"id", x.id}, {"text_de", x.text_de}, {"text_en", x.text_en}, {"tested", x.tested}});
    json m = json::array();
    for (const auto& x : c.metrics)
        m.push_back({{"id", x.id}, {"unit", x.unit}, {"text_de", x.text_de}, {"text_en", x.text_en}, {"now", x.now}, {"has_prev", x.has_prev}, {"prev", x.prev}, {"has_typical", x.has_typical}, {"typical", x.typical}, {"samples", x.samples}});
    return {{"version", 1}, {"ts", c.ts}, {"fw", c.fw}, {"model", c.model}, {"rules_version", c.rules_version}, {"findings", f}, {"coverage", cov}, {"resolved", c.resolved}, {"metrics", m}};
}

bool from_json(const json& j, StoredCheck& c) {
    if (!j.is_object() || !j.contains("findings") || !j["findings"].is_array()) return false;
    c = StoredCheck{};
    c.ts = str_of(j, "ts");
    c.fw = str_of(j, "fw");
    c.model = str_of(j, "model");
    c.rules_version = str_of(j, "rules_version");
    for (const auto& x : j["findings"]) {
        if (!x.is_object()) continue;
        Finding f;
        f.rule_id = str_of(x, "rule_id");
        f.severity = severity_from(str_of(x, "severity"));
        f.text_de = str_of(x, "text_de");
        f.text_en = str_of(x, "text_en");
        if (x.contains("evidence") && x["evidence"].is_array())
            for (const auto& e : x["evidence"])
                if (e.is_string()) f.evidence.push_back(e.get<std::string>());
        f.evidence_path = str_of(x, "evidence_path");
        f.fix = str_of(x, "fix");
        if (x.contains("fix_params") && x["fix_params"].is_object()) f.fix_params = x["fix_params"];
        f.source = str_of(x, "source");
        f.verified = str_of(x, "verified");
        if (x.contains("confidence") && x["confidence"].is_object()) {
            const auto& cf = x["confidence"];
            f.confidence.level = str_of(cf, "level");
            f.confidence.basis = str_of(cf, "basis");
            f.confidence.reason_de = str_of(cf, "reason_de");
            f.confidence.reason_en = str_of(cf, "reason_en");
        }
        f.status = str_of(x, "status");
        f.first_seen = str_of(x, "first_seen");
        f.seen_runs = x.value("seen_runs", 0);
        f.confirm_runs = x.value("confirm_runs", 1);
        if (!f.rule_id.empty()) c.findings.push_back(std::move(f));
    }
    if (j.contains("coverage") && j["coverage"].is_array())
        for (const auto& x : j["coverage"])
            if (x.is_object()) c.coverage.push_back({str_of(x, "id"), str_of(x, "text_de"), str_of(x, "text_en"), x.value("tested", false)});
    if (j.contains("resolved") && j["resolved"].is_array())
        for (const auto& x : j["resolved"])
            if (x.is_string()) c.resolved.push_back(x.get<std::string>());
    if (j.contains("metrics") && j["metrics"].is_array())
        for (const auto& x : j["metrics"]) {
            if (!x.is_object()) continue;
            MetricChange m;
            m.id = str_of(x, "id");
            m.unit = str_of(x, "unit");
            m.text_de = str_of(x, "text_de");
            m.text_en = str_of(x, "text_en");
            m.now = x.value("now", 0.0);
            m.has_prev = x.value("has_prev", false);
            m.prev = x.value("prev", 0.0);
            m.has_typical = x.value("has_typical", false);
            m.typical = x.value("typical", 0.0);
            m.samples = x.value("samples", std::size_t{0});
            c.metrics.push_back(std::move(m));
        }
    return true;
}

}  // namespace

bool save_check(Platform& p, const StoredCheck& c, std::string* id) {
    IFileSystemRW* rw = p.fs_rw();
    if (!rw || !valid_id(c.ts)) return false;
    std::string name = c.ts;
    for (int n = 2; p.fs().exists(std::string(kChecksDir) + "/" + name + ".json"); ++n) {  // nie ueberschreiben
        if (n > 20) return false;
        name = c.ts + "_" + std::to_string(n);
    }
    if (!rw->write_file(std::string(kChecksDir) + "/" + name + ".json", to_json(c).dump(1) + "\n")) return false;
    if (id) *id = name;
    return true;
}

bool load_check(const IFileSystem& fs, const std::string& id, StoredCheck& out) {
    if (!valid_id(id)) return false;
    std::string text;
    if (!fs.read_file(std::string(kChecksDir) + "/" + id + ".json", text) || text.size() > kCheckMaxBytes) return false;
    const json j = json::parse(text, nullptr, false);
    if (!from_json(j, out)) return false;
    out.id = id;
    if (out.ts.empty()) out.ts = id.substr(0, id.find('_'));
    return true;
}

std::vector<CheckSummary> list_checks(const IFileSystem& fs) {
    std::vector<std::string> ids;
    for (const auto& e : fs.list_dir(kChecksDir)) {
        if (e.is_dir || e.name.size() <= 5 || e.name.compare(e.name.size() - 5, 5, ".json") != 0) continue;
        const std::string id = e.name.substr(0, e.name.size() - 5);
        if (valid_id(id)) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end(), std::greater<std::string>());  // Zeit im Namen: neueste zuerst
    std::vector<CheckSummary> out;
    for (const auto& id : ids) {
        if (out.size() >= kChecksListMax) break;
        StoredCheck c;
        if (!load_check(fs, id, c)) continue;
        CheckSummary s;
        s.id = id;
        s.ts = c.ts;
        s.fw = c.fw;
        s.model = c.model;
        s.rules_version = c.rules_version;
        s.total = static_cast<int>(c.findings.size());
        for (const auto& f : c.findings) {
            if (f.severity == Severity::Critical) ++s.critical;
            else if (f.severity == Severity::Warning) ++s.warning;
            else ++s.info;
        }
        for (const auto& cv : c.coverage) s.untested += cv.tested ? 0 : 1;
        out.push_back(std::move(s));
    }
    return out;
}

CheckDiff compare_checks(const StoredCheck& newer, const StoredCheck& older) {
    CheckDiff d;
    std::set<std::string> in_older, in_newer;
    for (const auto& f : older.findings) in_older.insert(f.rule_id);
    for (const auto& f : newer.findings) in_newer.insert(f.rule_id);
    for (const auto& f : newer.findings) (in_older.count(f.rule_id) ? d.unchanged : d.added).push_back(f);
    for (const auto& f : older.findings)
        if (!in_newer.count(f.rule_id)) d.resolved.push_back(f);
    return d;
}

}  // namespace medic
