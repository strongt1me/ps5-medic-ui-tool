// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/rules.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace medic {
namespace {

using json = nlohmann::json;

[[noreturn]] void fail(const std::string& where, const std::string& msg) {
    throw std::runtime_error("rules: " + where + ": " + msg);
}

// ---- Validierung (entspricht knowledge/rules.schema.json) ---------------

const std::set<std::string> kLeafKeys = {"fact", "eq", "in", "lt", "gt", "matches", "contains", "exists", "count_matches", "default"};

void validate_condition(const json& c, const std::string& where) {
    if (!c.is_object()) fail(where, "Bedingung muss ein Objekt sein");
    if (c.contains("all") || c.contains("any")) {
        const char* key = c.contains("all") ? "all" : "any";
        if (c.size() != 1) fail(where, std::string("'") + key + "' darf keine Geschwister-Schluessel haben");
        if (!c[key].is_array() || c[key].empty()) fail(where, std::string("'") + key + "' braucht ein nicht-leeres Array");
        for (std::size_t i = 0; i < c[key].size(); ++i)
            validate_condition(c[key][i], where + "." + key + "[" + std::to_string(i) + "]");
        return;
    }
    if (c.contains("not")) {
        if (c.size() != 1) fail(where, "'not' darf keine Geschwister-Schluessel haben");
        validate_condition(c["not"], where + ".not");
        return;
    }
    if (!c.contains("fact") || !c["fact"].is_string()) fail(where, "Blatt braucht 'fact' (String)");
    for (auto it = c.begin(); it != c.end(); ++it)
        if (!kLeafKeys.count(it.key())) fail(where, "unbekannter Schluessel '" + it.key() + "'");
    int ops = 0;
    for (const char* k : {"eq", "in", "lt", "gt", "matches", "contains", "exists", "count_matches"}) ops += c.contains(k) ? 1 : 0;
    if (ops != 1) fail(where, "genau ein Operator erwartet (eq, in, lt, gt, matches, contains, exists, count_matches)");
    if (c.contains("count_matches")) {
        const json& cm = c["count_matches"];
        if (!cm.is_object() || !cm.contains("regex") || !cm["regex"].is_string() || !cm.contains("gte") ||
            !cm["gte"].is_number_integer() || cm["gte"].get<long>() < 1 || cm.size() != 2)
            fail(where, "'count_matches' braucht {\"regex\": String, \"gte\": Ganzzahl >= 1}");
        try {
            std::regex re(cm["regex"].get<std::string>());
        } catch (const std::regex_error& e) {
            fail(where, std::string("ungueltiger regulaerer Ausdruck: ") + e.what());
        }
        if (c["fact"].get<std::string>().find("[]") != std::string::npos)
            fail(where, "'count_matches' arbeitet auf einem ganzen Array, 'fact' darf kein [] enthalten");
    }
    if (c.contains("in") && !c["in"].is_array()) fail(where, "'in' braucht ein Array");
    for (const char* k : {"lt", "gt", "matches", "contains"})
        if (c.contains(k) && !c[k].is_string()) fail(where, std::string("'") + k + "' braucht einen String");
    if (c.contains("exists") && !c["exists"].is_boolean()) fail(where, "'exists' braucht true/false");
    if (c.contains("matches")) {
        try {
            std::regex re(c["matches"].get<std::string>());
        } catch (const std::regex_error& e) {
            fail(where, std::string("ungueltiger regulaerer Ausdruck: ") + e.what());
        }
    }
}

std::string need_string(const json& obj, const char* key, const std::string& where) {
    if (!obj.contains(key) || !obj[key].is_string()) fail(where, std::string("'") + key + "' fehlt oder ist kein String");
    return obj[key].get<std::string>();
}

Rule parse_rule(const json& j, std::size_t index) {
    std::string where = "rules[" + std::to_string(index) + "]";
    if (!j.is_object()) fail(where, "Regel muss ein Objekt sein");
    Rule r;
    r.id = need_string(j, "id", where);
    where = "rule '" + r.id + "'";
    if (!std::regex_match(r.id, std::regex("^[a-z0-9_.]+$"))) fail(where, "ungueltige ID (erlaubt: a-z 0-9 _ .)");

    std::string sev = need_string(j, "severity", where);
    if (sev == "critical") r.severity = Severity::Critical;
    else if (sev == "warning") r.severity = Severity::Warning;
    else if (sev == "info") r.severity = Severity::Info;
    else fail(where, "severity muss critical|warning|info sein");

    if (!j.contains("when")) fail(where, "'when' fehlt");
    validate_condition(j["when"], where + ".when");
    r.when = j["when"];

    if (!j.contains("text") || !j["text"].is_object()) fail(where, "'text' fehlt oder ist kein Objekt");
    r.text_de = need_string(j["text"], "de", where + ".text");
    r.text_en = need_string(j["text"], "en", where + ".text");

    r.source = need_string(j, "source", where);
    r.verified = need_string(j, "verified", where);
    if (r.verified != "doc" && r.verified != "community" && r.verified != "todo")
        fail(where, "verified muss doc|community|todo sein");

    if (j.contains("evidence")) r.evidence_path = need_string(j, "evidence", where);
    if (j.contains("evidence_filter")) {
        r.evidence_filter = need_string(j, "evidence_filter", where);
        try {
            std::regex re(r.evidence_filter);
        } catch (const std::regex_error& e) {
            fail(where, std::string("evidence_filter: ungueltiger regulaerer Ausdruck: ") + e.what());
        }
    }
    if (j.contains("fix") && !j["fix"].is_null()) r.fix = need_string(j, "fix", where);
    if (j.contains("fix_params")) {
        if (!j["fix_params"].is_object()) fail(where, "'fix_params' muss ein Objekt sein");
        r.fix_params = j["fix_params"];
    }
    if (j.contains("applies_fw")) r.applies_fw = need_string(j, "applies_fw", where);
    return r;
}

// ---- Hilfen --------------------------------------------------------------

std::string to_s(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_float()) {
        std::ostringstream os;
        os << v.get<double>();
        return os.str();
    }
    return v.dump();
}

bool loose_equal(const json& a, const json& b) {
    if (a.type() == b.type()) return a == b;
    return to_s(a) == to_s(b);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string seg;
    while (std::getline(ss, seg, sep)) out.push_back(seg);
    return out;
}

// ---- Auswertung ----------------------------------------------------------

using Binding = std::vector<std::pair<std::string, std::size_t>>;  // Array-Pfad -> Index

std::optional<json> resolve(const Facts& facts, const std::string& path, const Binding& b) {
    auto pos = path.find("[]");
    if (pos == std::string::npos) {
        const json* v = facts.get(path);
        return v ? std::optional<json>(*v) : std::nullopt;
    }
    std::string prefix = path.substr(0, pos);
    std::string rest = path.substr(pos + 2);  // "" oder ".feld"
    auto it = std::find_if(b.begin(), b.end(), [&](auto& p) { return p.first == prefix; });
    if (it == b.end()) return std::nullopt;
    const json* arr = facts.get(prefix);
    if (!arr || !arr->is_array() || it->second >= arr->size()) return std::nullopt;
    const json* cur = &(*arr)[it->second];
    if (rest.empty()) return *cur;
    if (rest[0] != '.') return std::nullopt;
    for (const auto& seg : split(rest.substr(1), '.')) {
        if (!cur->is_object()) return std::nullopt;
        auto f = cur->find(seg);
        if (f == cur->end()) return std::nullopt;
        cur = &*f;
    }
    return *cur;
}

bool eval_leaf(const json& c, const Facts& facts, const Binding& b) {
    auto v = resolve(facts, c["fact"].get<std::string>(), b);
    if (!v && c.contains("default")) v = c["default"];

    if (c.contains("exists")) return v.has_value() == c["exists"].get<bool>();
    if (!v) return false;

    if (c.contains("count_matches")) {
        if (!v->is_array()) return false;
        std::regex re(c["count_matches"]["regex"].get<std::string>());
        long n = 0;
        for (auto& e : *v)
            if (std::regex_search(to_s(e), re)) ++n;
        return n >= c["count_matches"]["gte"].get<long>();
    }
    if (c.contains("eq")) return loose_equal(*v, c["eq"]);
    if (c.contains("in")) {
        for (auto& e : c["in"])
            if (loose_equal(*v, e)) return true;
        return false;
    }
    if (c.contains("lt")) return compare_versions(to_s(*v), c["lt"].get<std::string>()) < 0;
    if (c.contains("gt")) return compare_versions(to_s(*v), c["gt"].get<std::string>()) > 0;
    if (c.contains("matches")) {
        std::regex re(c["matches"].get<std::string>());
        return std::regex_search(to_s(*v), re);
    }
    if (c.contains("contains")) {
        const std::string needle = c["contains"].get<std::string>();
        if (v->is_array()) {
            for (auto& e : *v)
                if (to_s(e) == needle) return true;
            return false;
        }
        return to_s(*v).find(needle) != std::string::npos;
    }
    return false;
}

bool eval(const json& c, const Facts& facts, const Binding& b) {
    if (c.contains("all")) {
        for (auto& s : c["all"])
            if (!eval(s, facts, b)) return false;
        return true;
    }
    if (c.contains("any")) {
        for (auto& s : c["any"])
            if (eval(s, facts, b)) return true;
        return false;
    }
    if (c.contains("not")) return !eval(c["not"], facts, b);
    return eval_leaf(c, facts, b);
}

void collect_prefixes(const json& c, std::set<std::string>& out) {
    for (const char* k : {"all", "any"})
        if (c.contains(k))
            for (auto& s : c[k]) collect_prefixes(s, out);
    if (c.contains("not")) collect_prefixes(c["not"], out);
    if (c.contains("fact")) {
        const std::string f = c["fact"].get<std::string>();
        auto pos = f.find("[]");
        if (pos != std::string::npos) out.insert(f.substr(0, pos));
    }
}

constexpr std::size_t kMaxEvidence = 20;

void add_evidence(std::vector<std::string>& ev, const json& v, const std::regex* filter) {
    if (v.is_array()) {
        for (auto& e : v) add_evidence(ev, e, filter);
        return;
    }
    std::string s = to_s(v);
    if (filter && !std::regex_search(s, *filter)) return;
    if (ev.size() >= kMaxEvidence) return;
    if (std::find(ev.begin(), ev.end(), s) == ev.end()) ev.push_back(std::move(s));
}

constexpr std::size_t kMaxBindings = 100000;

}  // namespace

const char* to_string(Severity s) {
    switch (s) {
        case Severity::Critical: return "critical";
        case Severity::Warning: return "warning";
        case Severity::Info: return "info";
    }
    return "info";
}

int compare_versions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& s) {
        std::vector<long> v;
        for (auto& seg : split(s, '.')) v.push_back(std::strtol(seg.c_str(), nullptr, 10));  // "7beta" -> 7
        return v;
    };
    auto va = parts(a), vb = parts(b);
    const std::size_t n = std::max(va.size(), vb.size());
    va.resize(n, 0);
    vb.resize(n, 0);
    for (std::size_t i = 0; i < n; ++i)
        if (va[i] != vb[i]) return va[i] < vb[i] ? -1 : 1;
    return 0;
}

bool fw_in_range(const std::string& fw, const std::string& range) {
    if (fw.empty() || range.empty()) return true;  // unbekannte FW: Regel nicht unterdruecken
    std::stringstream ss(range);
    std::string tok;
    while (ss >> tok) {
        std::string op;
        std::size_t i = 0;
        while (i < tok.size() && (tok[i] == '<' || tok[i] == '>' || tok[i] == '=')) op += tok[i++];
        if (op.empty()) op = "=";
        int c = compare_versions(fw, tok.substr(i));
        bool ok = (op == ">=" && c >= 0) || (op == "<=" && c <= 0) || (op == ">" && c > 0) || (op == "<" && c < 0) ||
                  (op == "=" && c == 0);
        if (!ok) return false;
    }
    return true;
}

RuleSet load_rules_from_string(const std::string& text) {
    json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) fail("Datei", "kein gueltiges JSON");
    if (!j.is_object()) fail("Datei", "Wurzel muss ein Objekt sein");
    if (!j.contains("schema_version") || j["schema_version"] != 1) fail("Datei", "schema_version muss 1 sein");
    RuleSet rs;
    rs.db_version = need_string(j, "db_version", "Datei");
    if (!j.contains("rules") || !j["rules"].is_array()) fail("Datei", "'rules' fehlt oder ist kein Array");
    std::set<std::string> seen;
    for (std::size_t i = 0; i < j["rules"].size(); ++i) {
        Rule r = parse_rule(j["rules"][i], i);
        if (!seen.insert(r.id).second) fail("rule '" + r.id + "'", "doppelte ID");
        rs.rules.push_back(std::move(r));
    }
    return rs;
}

RuleSet load_rules_from_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("rules: kann '" + path + "' nicht oeffnen");
    std::ostringstream ss;
    ss << in.rdbuf();
    return load_rules_from_string(ss.str());
}

std::vector<Finding> evaluate(const RuleSet& rs, const Facts& facts) {
    const std::string fw = facts.get("fw.version") && facts.get("fw.version")->is_string()
                               ? facts.get("fw.version")->get<std::string>()
                               : std::string();
    std::vector<Finding> out;

    for (const Rule& r : rs.rules) {
        if (!fw_in_range(fw, r.applies_fw)) continue;

        std::set<std::string> prefix_set;
        collect_prefixes(r.when, prefix_set);
        std::vector<std::string> prefixes(prefix_set.begin(), prefix_set.end());
        std::vector<std::size_t> sizes;
        std::size_t total = 1;
        bool empty_array = false;
        for (auto& p : prefixes) {
            const json* arr = facts.get(p);
            std::size_t n = arr && arr->is_array() ? arr->size() : 0;
            if (n == 0) empty_array = true;
            sizes.push_back(n);
            total = (n && total > kMaxBindings / n) ? kMaxBindings + 1 : total * std::max<std::size_t>(n, 1);
        }
        if (empty_array || total > kMaxBindings) continue;

        Finding f;
        std::optional<std::regex> ev_filter;
        if (!r.evidence_filter.empty()) ev_filter.emplace(r.evidence_filter);
        bool hit = false;
        std::vector<std::size_t> idx(prefixes.size(), 0);
        for (;;) {
            Binding b;
            for (std::size_t i = 0; i < prefixes.size(); ++i) b.emplace_back(prefixes[i], idx[i]);
            if (eval(r.when, facts, b)) {
                hit = true;
                if (!r.evidence_path.empty())
                    if (auto ev = resolve(facts, r.evidence_path, b)) add_evidence(f.evidence, *ev, ev_filter ? &*ev_filter : nullptr);
            }
            // Zaehler weiterschalten
            std::size_t k = 0;
            while (k < idx.size() && ++idx[k] >= sizes[k]) idx[k++] = 0;
            if (k == idx.size()) break;
        }
        if (!hit) continue;

        f.rule_id = r.id;
        f.evidence_path = r.evidence_path;
        f.severity = r.severity;
        f.text_de = r.text_de;
        f.text_en = r.text_en;
        f.fix = r.fix;
        f.fix_params = r.fix_params;
        f.source = r.source;
        f.verified = r.verified;
        out.push_back(std::move(f));
    }

    std::sort(out.begin(), out.end(), [](const Finding& a, const Finding& b) {
        if (a.severity != b.severity) return a.severity < b.severity;  // Critical < Warning < Info
        return a.rule_id < b.rule_id;
    });
    return out;
}

}  // namespace medic
