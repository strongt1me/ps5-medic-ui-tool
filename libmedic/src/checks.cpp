// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/checks.hpp"
#include "medic/jsondump.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <map>
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
    json setup = json::object();
    for (const auto& kv : c.setup) setup[kv.first] = kv.second;
    return {{"version", 1}, {"ts", c.ts}, {"fw", c.fw}, {"model", c.model}, {"rules_version", c.rules_version}, {"findings", f}, {"coverage", cov}, {"resolved", c.resolved}, {"metrics", m}, {"setup", setup}};
}

namespace {
// Zahlen und Wahrheitswerte ohne Ausnahme bei falschem Typ (json::value() wirft dann type_error): eine von Hand veraenderte Datei darf nicht die ganze Liste verwerfen
double num_of(const json& x, const char* k, double d) { return x.is_object() && x.contains(k) && x[k].is_number() ? x[k].get<double>() : d; }
int int_of(const json& x, const char* k, int d) { return static_cast<int>(std::clamp(num_of(x, k, d), 0.0, 1e9)); }
bool bool_of(const json& x, const char* k, bool d) { return x.is_object() && x.contains(k) && x[k].is_boolean() ? x[k].get<bool>() : d; }
}  // namespace

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
        f.seen_runs = int_of(x, "seen_runs", 0);
        f.confirm_runs = int_of(x, "confirm_runs", 1);
        if (!f.rule_id.empty()) c.findings.push_back(std::move(f));
    }
    if (j.contains("coverage") && j["coverage"].is_array())
        for (const auto& x : j["coverage"])
            if (x.is_object()) c.coverage.push_back({str_of(x, "id"), str_of(x, "text_de"), str_of(x, "text_en"), bool_of(x, "tested", false)});
    if (j.contains("resolved") && j["resolved"].is_array())
        for (const auto& x : j["resolved"])
            if (x.is_string()) c.resolved.push_back(x.get<std::string>());
    if (j.contains("setup") && j["setup"].is_object())
        for (auto it = j["setup"].begin(); it != j["setup"].end(); ++it)
            if (it.value().is_string()) c.setup.emplace_back(it.key(), it.value().get<std::string>());
    if (j.contains("metrics") && j["metrics"].is_array())
        for (const auto& x : j["metrics"]) {
            if (!x.is_object()) continue;
            MetricChange m;
            m.id = str_of(x, "id");
            m.unit = str_of(x, "unit");
            m.text_de = str_of(x, "text_de");
            m.text_en = str_of(x, "text_en");
            m.now = num_of(x, "now", 0.0);
            m.has_prev = bool_of(x, "has_prev", false);
            m.prev = num_of(x, "prev", 0.0);
            m.has_typical = bool_of(x, "has_typical", false);
            m.typical = num_of(x, "typical", 0.0);
            m.samples = static_cast<std::size_t>(int_of(x, "samples", 0));
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
    if (!rw->write_file(std::string(kChecksDir) + "/" + name + ".json", dump_safe(to_json(c), 1) + "\n")) return false;
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
    std::map<std::string, Severity> sev_older;
    std::set<std::string> in_newer;
    for (const auto& f : older.findings) sev_older.emplace(f.rule_id, f.severity);
    for (const auto& f : newer.findings) in_newer.insert(f.rule_id);
    for (const auto& f : newer.findings) {
        const auto it = sev_older.find(f.rule_id);
        if (it == sev_older.end()) d.added.push_back(f);
        else if (it->second != f.severity) d.severity_changed.push_back({f, it->second});
        else d.unchanged.push_back(f);
    }
    for (const auto& f : older.findings)
        if (!in_newer.count(f.rule_id)) d.resolved.push_back(f);

    // Einrichtung: Firmware, Modell und Regelstand stehen in jedem Check-up, der Rest nur in neueren Dateien.
    std::map<std::string, std::string> a, b;
    std::vector<std::string> order;
    const auto put = [&](std::map<std::string, std::string>& m, const std::string& k, const std::string& v) {
        if (m.emplace(k, v).second && std::find(order.begin(), order.end(), k) == order.end()) order.push_back(k);
    };
    for (const auto& kv : std::vector<std::pair<std::string, std::string>>{{"fw", older.fw}, {"model", older.model}, {"rules_version", older.rules_version}}) put(a, kv.first, kv.second);
    for (const auto& kv : std::vector<std::pair<std::string, std::string>>{{"fw", newer.fw}, {"model", newer.model}, {"rules_version", newer.rules_version}}) put(b, kv.first, kv.second);
    for (const auto& kv : older.setup) put(a, kv.first, kv.second);
    for (const auto& kv : newer.setup) put(b, kv.first, kv.second);
    for (const auto& k : order) {
        const bool base = k == "fw" || k == "model" || k == "rules_version";
        if (!base && older.setup.empty()) continue;  // alte Datei ohne Einrichtungsdaten: nichts zum Vergleichen
        const std::string from = a.count(k) ? a[k] : std::string(), to = b.count(k) ? b[k] : std::string();
        if (from != to) d.setup_changed.push_back({k, from, to});
    }
    return d;
}

}  // namespace medic
