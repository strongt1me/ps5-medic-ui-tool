// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/rules.hpp"
#include "medic/jsondump.hpp"
#include "medic/text_policy.hpp"

#include <algorithm>
#include <cctype>
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
    {  // mehr als ein [] im Fakt-Pfad loest die Auswertung nie auf: die Regel feuerte still nie
        const std::string fact = c["fact"].get<std::string>();
        const auto first = fact.find("[]");
        if (first != std::string::npos && fact.find("[]", first + 2) != std::string::npos) fail(where, "'fact' darf hoechstens ein [] enthalten");
    }
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

const std::set<std::string> kConfLevels = {"high", "medium", "low"};
const std::set<std::string> kConfBases = {"observed", "doc", "community", "derived", "unknown"};

// Optionales Feld "confidence": {"level"?, "basis"?, "reason": {"de","en"}}. Fehlende level/basis kommen aus `verified`;
// eine Angabe braucht immer einen Grund. Widerspruechliche Angaben werden abgelehnt: ohne Beleg nie "hoch", und die
// Grundlage darf nicht staerker sein als `verified` (todo: kein doc/community; community: kein doc).
void parse_confidence(const json& j, Rule& r, const std::string& where) {
    r.confidence = default_confidence(r.verified);
    if (!j.contains("confidence")) return;
    const json& c = j["confidence"];
    const std::string w = where + ".confidence";
    if (!c.is_object()) fail(w, "muss ein Objekt sein");
    for (auto it = c.begin(); it != c.end(); ++it)
        if (it.key() != "level" && it.key() != "basis" && it.key() != "reason") fail(w, "unbekannter Schluessel '" + it.key() + "'");
    if (c.contains("level")) {
        r.confidence.level = need_string(c, "level", w);
        if (!kConfLevels.count(r.confidence.level)) fail(w, "level muss high|medium|low sein");
    }
    if (c.contains("basis")) {
        r.confidence.basis = need_string(c, "basis", w);
        if (!kConfBases.count(r.confidence.basis)) fail(w, "basis muss observed|doc|community|derived|unknown sein");
    }
    if (!c.contains("reason") || !c["reason"].is_object()) fail(w, "'reason' fehlt (eine Angabe braucht einen Grund, de und en)");
    r.confidence.reason_de = need_string(c["reason"], "de", w + ".reason");
    r.confidence.reason_en = need_string(c["reason"], "en", w + ".reason");
    if (r.confidence.reason_de.empty() || r.confidence.reason_en.empty()) fail(w, "der Grund darf nicht leer sein");

    const std::string& basis = r.confidence.basis;
    if (r.verified == "todo" && (basis == "doc" || basis == "community")) fail(w, "bei verified=todo ist doc/community als Grundlage ein Widerspruch");
    if (r.verified == "community" && basis == "doc") fail(w, "bei verified=community ist doc als Grundlage ein Widerspruch");
    if (r.verified == "todo" && r.confidence.level == "high") fail(w, "ohne Beleg (verified=todo) nie level=high");
    if (r.confidence.level == "high" && basis != "doc" && basis != "observed") fail(w, "level=high braucht basis doc oder observed");
}

// Version in applies_fw: Zahlen mit Punkten ("13.60"), optional eine Vorabversion direkt dahinter ("1.7beta12").
bool valid_fw_version(const std::string& v) {
    std::size_t i = 0;
    for (;;) {
        std::size_t j = i;
        while (j < v.size() && std::isdigit(static_cast<unsigned char>(v[j]))) ++j;
        if (j == i) return false;
        i = j;
        if (i < v.size() && v[i] == '.') {
            ++i;
            continue;
        }
        break;
    }
    while (i < v.size() && std::isalpha(static_cast<unsigned char>(v[i]))) ++i;
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) ++i;
    return i == v.size();
}

// applies_fw: Teile mit Leerraum getrennt, je "<Operator><Version>" mit Operator < <= > >= = (oder ohne); der Operator darf auch durch Leerraum von der Version
// getrennt stehen. Ein Tippfehler (">=13,60", "13.60-14.10", "=>13.60") schaltete die Regel sonst still ab oder auf "immer": lieber die Datei ablehnen.
void validate_fw_range(const std::string& range, const std::string& where) {
    std::stringstream ss(range);
    std::string tok;
    int parts = 0;
    while (ss >> tok) {
        std::size_t i = 0;
        while (i < tok.size() && (tok[i] == '<' || tok[i] == '>' || tok[i] == '=')) ++i;
        const std::string op = tok.substr(0, i);
        if (!(op.empty() || op == "<" || op == ">" || op == "=" || op == "<=" || op == ">=")) fail(where, "'applies_fw': ungueltiger Operator '" + op + "'");
        std::string ver = tok.substr(i);
        if (ver.empty() && !(ss >> ver)) fail(where, "'applies_fw': hinter '" + op + "' fehlt die Version");
        if (!valid_fw_version(ver)) fail(where, "'applies_fw': ungueltige Version '" + ver + "'");
        ++parts;
    }
    if (parts == 0) fail(where, "'applies_fw' darf nicht leer sein");
}

Rule parse_rule(const json& j, std::size_t index) {
    std::string where = "rules[" + std::to_string(index) + "]";
    if (!j.is_object()) fail(where, "Regel muss ein Objekt sein");
    Rule r;
    r.id = need_string(j, "id", where);
    where = "rule '" + r.id + "'";
    // ^[a-z0-9_.]+$ ohne std::regex (jede Konstruktion kostete auf der Konsole rund 10 ms; 76 Regeln = Sekunden Ladezeit)
    if (r.id.empty() || !std::all_of(r.id.begin(), r.id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; })) fail(where, "ungueltige ID (erlaubt: a-z 0-9 _ .)");

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
    parse_confidence(j, r, where);

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
    if (j.contains("applies_fw")) {
        r.applies_fw = need_string(j, "applies_fw", where);
        validate_fw_range(r.applies_fw, where);
    }
    if (j.contains("confirm_runs")) {
        if (!j["confirm_runs"].is_number_integer() || j["confirm_runs"].get<long>() < 1 || j["confirm_runs"].get<long>() > 10) fail(where, "'confirm_runs' muss eine Ganzzahl von 1 bis 10 sein");
        r.confirm_runs = static_cast<int>(j["confirm_runs"].get<long>());
    }
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
    return dump_safe(v);
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

// to_string(Severity), default_confidence und effective_confidence stehen in finding.cpp (ohne Ausnahmen, auch in der nativen App)

namespace {

// Zahlenteile plus Vorabversion: "1.7beta10" = {1, 7} mit Tag beta/10. Eine Vorabversion ist kleiner als die fertige
// ("1.7beta12" < "1.7"); test < alpha < beta < rc, danach zaehlt die Nummer ("beta9" < "beta10"). Andere Suffixe
// ("1.13-fpkg-dr-test8") bleiben wie bisher ohne Wirkung.
struct VersionKey {
    std::vector<long> nums;
    int tag_rank = 100;  // 100 = keine Vorabversion
    long tag_num = 0;
};

VersionKey parse_version(const std::string& s) {
    VersionKey k;
    static const std::pair<const char*, int> kTags[] = {{"test", 0}, {"alpha", 1}, {"beta", 2}, {"rc", 3}};
    for (auto& seg : split(s, '.')) {
        char* end = nullptr;
        k.nums.push_back(std::strtol(seg.c_str(), &end, 10));
        std::string rest = end ? std::string(end) : std::string();
        std::transform(rest.begin(), rest.end(), rest.begin(), [](unsigned char c) { return std::tolower(c); });
        for (const auto& [name, rank] : kTags) {
            const std::string tag = name;
            if (rest.compare(0, tag.size(), tag) != 0) continue;
            const std::string num = rest.substr(tag.size());
            if (!num.empty() && !std::all_of(num.begin(), num.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
            k.tag_rank = rank;
            k.tag_num = num.empty() ? 0 : std::strtol(num.c_str(), nullptr, 10);
        }
    }
    return k;
}

}  // namespace

int compare_versions(const std::string& a, const std::string& b) {
    VersionKey ka = parse_version(a), kb = parse_version(b);
    const std::size_t n = std::max(ka.nums.size(), kb.nums.size());
    ka.nums.resize(n, 0);
    kb.nums.resize(n, 0);
    for (std::size_t i = 0; i < n; ++i)
        if (ka.nums[i] != kb.nums[i]) return ka.nums[i] < kb.nums[i] ? -1 : 1;
    if (ka.tag_rank != kb.tag_rank) return ka.tag_rank < kb.tag_rank ? -1 : 1;
    if (ka.tag_num != kb.tag_num) return ka.tag_num < kb.tag_num ? -1 : 1;
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
        std::string ver = tok.substr(i);
        if (ver.empty()) ss >> ver;  // ">= 13.60": Operator und Version durch Leerraum getrennt
        int c = compare_versions(fw, ver);
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
    require_no_system_update_advice(j, "rules");  // nie ein Systemupdate nennen (docs/RULES.md, Textregel)
    RuleSet rs;
    rs.db_version = need_string(j, "db_version", "Datei");
    if (j.contains("requires_level")) {
        if (!j["requires_level"].is_number_integer() || j["requires_level"].get<long>() < 1) fail("Datei", "'requires_level' muss eine Ganzzahl >= 1 sein");
        if (j["requires_level"].get<long>() > kRuleFormatLevel) fail("Datei", "braucht eine neuere PS5-Medic-Version (Formatstufe " + std::to_string(j["requires_level"].get<long>()) + ")");
        rs.requires_level = static_cast<int>(j["requires_level"].get<long>());
    }
    if (!j.contains("rules") || !j["rules"].is_array()) fail("Datei", "'rules' fehlt oder ist kein Array");
    std::set<std::string> seen;
    for (std::size_t i = 0; i < j["rules"].size(); ++i) {
        Rule r = parse_rule(j["rules"][i], i);
        if (!seen.insert(r.id).second) fail("rule '" + r.id + "'", "doppelte ID");
        rs.rules.push_back(std::move(r));
    }
    if (j.contains("coverage")) {
        if (!j["coverage"].is_array()) fail("Datei", "'coverage' muss ein Array sein");
        std::set<std::string> cov_seen;
        for (std::size_t i = 0; i < j["coverage"].size(); ++i) {
            const json& c = j["coverage"][i];
            std::string where = "coverage[" + std::to_string(i) + "]";
            if (!c.is_object()) fail(where, "Eintrag muss ein Objekt sein");
            CoverageItem item;
            item.id = need_string(c, "id", where);
            where = "coverage '" + item.id + "'";
            if (!std::regex_match(item.id, std::regex("^[a-z0-9_.]+$"))) fail(where, "ungueltige ID (erlaubt: a-z 0-9 _ .)");
            if (!cov_seen.insert(item.id).second) fail(where, "doppelte ID");
            if (!c.contains("text") || !c["text"].is_object()) fail(where, "'text' fehlt oder ist kein Objekt");
            item.text_de = need_string(c["text"], "de", where + ".text");
            item.text_en = need_string(c["text"], "en", where + ".text");
            if (!c.contains("tested_when")) fail(where, "'tested_when' fehlt");
            validate_condition(c["tested_when"], where + ".tested_when");
            item.tested_when = c["tested_when"];
            rs.coverage.push_back(std::move(item));
        }
    }
    if (j.contains("metrics")) {
        if (!j["metrics"].is_array()) fail("Datei", "'metrics' muss ein Array sein");
        std::set<std::string> seen_m;
        for (std::size_t i = 0; i < j["metrics"].size(); ++i) {
            const json& m = j["metrics"][i];
            std::string where = "metrics[" + std::to_string(i) + "]";
            if (!m.is_object()) fail(where, "Eintrag muss ein Objekt sein");
            MetricItem item;
            item.id = need_string(m, "id", where);
            where = "metric '" + item.id + "'";
            if (!std::regex_match(item.id, std::regex("^[a-z0-9_.]+$"))) fail(where, "ungueltige ID (erlaubt: a-z 0-9 _ .)");
            if (!seen_m.insert(item.id).second) fail(where, "doppelte ID");
            item.fact = need_string(m, "fact", where);
            if (item.fact.empty() || item.fact.find("[]") != std::string::npos) fail(where, "'fact' muss ein einfacher Faktpfad ohne [] sein");
            if (m.contains("unit")) item.unit = need_string(m, "unit", where);
            if (!m.contains("text") || !m["text"].is_object()) fail(where, "'text' fehlt oder ist kein Objekt");
            item.text_de = need_string(m["text"], "de", where + ".text");
            item.text_en = need_string(m["text"], "en", where + ".text");
            rs.metrics.push_back(std::move(item));
        }
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

std::string peek_db_version(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("kann '" + path + "' nicht oeffnen");
    std::ostringstream ss;
    ss << in.rdbuf();
    const json j = json::parse(ss.str(), nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) throw std::runtime_error("kein gueltiges JSON");
    const auto it = j.find("db_version");
    if (it == j.end() || !it->is_string()) throw std::runtime_error("Feld 'db_version' fehlt");
    return it->get<std::string>();
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
        f.confidence = r.confidence;
        f.confirm_runs = r.confirm_runs;
        out.push_back(std::move(f));
    }

    std::sort(out.begin(), out.end(), [](const Finding& a, const Finding& b) {
        if (a.severity != b.severity) return a.severity < b.severity;  // Critical < Warning < Info
        return a.rule_id < b.rule_id;
    });
    return out;
}

std::vector<std::pair<std::string, double>> evaluate_metrics(const RuleSet& rs, const Facts& facts) {
    std::vector<std::pair<std::string, double>> out;
    for (const MetricItem& m : rs.metrics) {
        const json* v = facts.get(m.fact);
        if (v && v->is_number()) out.emplace_back(m.id, v->get<double>());
    }
    return out;
}

std::vector<CoverageResult> evaluate_coverage(const RuleSet& rs, const Facts& facts) {
    std::vector<CoverageResult> out;
    for (const CoverageItem& c : rs.coverage) out.push_back({c.id, c.text_de, c.text_en, eval(c.tested_when, facts, Binding{})});
    return out;
}

}  // namespace medic
