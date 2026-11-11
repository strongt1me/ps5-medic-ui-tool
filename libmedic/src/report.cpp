// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/report.hpp"

#include "medic/history.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>
#include <set>
#include <sstream>

namespace medic {
namespace {

using json = nlohmann::json;

// ---- Anonymisierung (Zeichen-Scan statt std::regex: schnell und ohne Rekursionsgrenzen auf langen Logzeilen) ----

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_hex(char c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool is_word(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
char low(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool before_ok(const std::string& s, std::size_t i) { return i == 0 || !is_word(s[i - 1]); }
bool after_ok(const std::string& s, std::size_t i) { return i >= s.size() || !is_word(s[i]); }

// "/user/home/<8 Hex>" -> "/user/home/[ID]"
std::string mask_user_home(const std::string& s) {
    static const std::string kPrefix = "/user/home/";
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (s.compare(i, kPrefix.size(), kPrefix) == 0) {
            out += kPrefix;
            i += kPrefix.size();
            std::size_t n = 0;
            while (i + n < s.size() && is_hex(s[i + n])) ++n;
            if (n == 8 && after_ok(s, i + n)) {
                out += "[ID]";
                i += n;
            }
            continue;
        }
        out += s[i++];
    }
    return out;
}

// "serial: X", "account id = X", "user_id: X" ... -> Wert ersetzen. Laengere Schluessel zuerst ("serial number" vor "serial").
std::string mask_key_values(const std::string& s) {
    struct Key {
        const char* name;
        const char* placeholder;
    };
    static const Key kKeys[] = {{"serial number", "[SERIAL]"}, {"serial_number", "[SERIAL]"}, {"serialnumber", "[SERIAL]"},
                                {"seriennummer", "[SERIAL]"},  {"serial no", "[SERIAL]"},     {"serial", "[SERIAL]"},
                                {"account id", "[ID]"},        {"account_id", "[ID]"},        {"accountid", "[ID]"},
                                {"psn id", "[ID]"},            {"user id", "[ID]"},           {"user_id", "[ID]"},
                                {"userid", "[ID]"}};
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) lower += low(c);

    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        bool replaced = false;
        if (before_ok(s, i)) {
            for (const Key& k : kKeys) {
                const std::size_t len = std::char_traits<char>::length(k.name);
                if (lower.compare(i, len, k.name) != 0) continue;
                std::size_t j = i + len;
                while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
                if (j >= s.size() || (s[j] != ':' && s[j] != '=')) continue;
                ++j;
                while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
                std::size_t v = j;
                while (v < s.size() && !std::isspace(static_cast<unsigned char>(s[v])) && s[v] != ',' && s[v] != ';' && s[v] != '"' && s[v] != '\'') ++v;
                if (v == j) continue;
                out.append(s, i, j - i);
                out += k.placeholder;
                i = v;
                replaced = true;
                break;
            }
        }
        if (!replaced) out += s[i++];
    }
    return out;
}

// 6 Gruppen zu je 2 Hex-Ziffern, einheitlich durch ':' oder '-' getrennt.
std::string mask_mac(const std::string& s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (before_ok(s, i) && i + 17 <= s.size() && is_hex(s[i])) {
            const char sep = s[i + 2];
            bool ok = sep == ':' || sep == '-';
            for (int g = 0; ok && g < 6; ++g) {
                const std::size_t p = i + static_cast<std::size_t>(g) * 3;
                ok = is_hex(s[p]) && is_hex(s[p + 1]) && (g == 5 || s[p + 2] == sep);
            }
            if (ok && after_ok(s, i + 17)) {
                out += "[MAC]";
                i += 17;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

// Laufende Hex-/Doppelpunkt-Folge mit "::" oder genau 7 Doppelpunkten (acht Gruppen). Zeitangaben wie "19:04:13" bleiben.
std::string mask_ipv6(const std::string& s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const bool start = (is_hex(s[i]) || s[i] == ':') && (i == 0 || (!is_word(s[i - 1]) && s[i - 1] != ':'));
        if (start) {
            std::size_t j = i;
            while (j < s.size() && (is_hex(s[j]) || s[j] == ':')) ++j;
            const std::string run = s.substr(i, j - i);
            const std::size_t colons = static_cast<std::size_t>(std::count(run.begin(), run.end(), ':'));
            const bool double_colon = run.find("::") != std::string::npos;
            bool groups_ok = true;
            std::size_t g = 0;
            for (char c : run) {
                if (c == ':') g = 0;
                else if (++g > 4) groups_ok = false;
            }
            const bool has_hex = std::any_of(run.begin(), run.end(), is_hex);
            if (after_ok(s, j) && groups_ok && has_hex && ((double_colon && colons >= 2) || colons == 7)) {
                out += "[IPV6]";
                i = j;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

// a.b.c.d mit Oktetten 0..255; "v1.2.3.4" (Buchstabe davor) und Folgen mit mehr als vier Teilen bleiben unberuehrt.
std::string mask_ipv4(const std::string& s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (is_digit(s[i]) && before_ok(s, i) && (i == 0 || s[i - 1] != '.')) {
            std::size_t p = i;
            int octets = 0;
            bool ok = true;
            while (ok && octets < 4) {
                std::size_t d = p;
                while (d < s.size() && is_digit(s[d]) && d - p < 3) ++d;
                if (d == p || std::stoi(s.substr(p, d - p)) > 255) {
                    ok = false;
                    break;
                }
                ++octets;
                p = d;
                if (octets < 4) {
                    if (p < s.size() && s[p] == '.') ++p;
                    else ok = false;
                }
            }
            const bool trailing_digit = p < s.size() && is_digit(s[p]);
            const bool more_parts = p + 1 < s.size() && s[p] == '.' && is_digit(s[p + 1]);
            if (ok && octets == 4 && !trailing_digit && !more_parts && after_ok(s, p)) {
                out += "[IP]";
                i = p;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

// Genau 16 Hex-Ziffern (PSN-Konto-ID), auch mit "0x"-Vorsatz. Ohne Vorsatz muss mindestens ein Buchstabe a-f dabei sein,
// damit 16-stellige Dezimalzahlen (z. B. Zeitstempel in Mikrosekunden) nicht als Kennung gelten.
std::string mask_account_hex(const std::string& s) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const bool prefixed = s[i] == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X');
        const std::size_t start = prefixed ? i + 2 : i;
        if (before_ok(s, i) && start < s.size() && is_hex(s[start])) {
            std::size_t n = 0;
            while (start + n < s.size() && is_hex(s[start + n])) ++n;
            bool has_letter = false;
            for (std::size_t k = 0; k < n; ++k) has_letter = has_letter || !is_digit(s[start + k]);
            if (n == 16 && (prefixed || has_letter) && after_ok(s, start + n)) {
                out += "[ACCOUNT-ID]";
                i = start + n;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

// ---- Bausteine ---------------------------------------------------------------

std::string label(Severity s, bool de) {
    switch (s) {
        case Severity::Critical: return de ? "KRITISCH" : "CRITICAL";
        case Severity::Warning: return de ? "WARNUNG" : "WARNING";
        case Severity::Info: return de ? "HINWEIS" : "NOTICE";
    }
    return "";
}

std::string verified_text(const std::string& v, bool de) {
    if (v == "doc") return de ? "Dokumentation" : "documentation";
    if (v == "community") return de ? "Community-Meldung" : "community report";
    return de ? "noch nicht bestätigt" : "not verified yet";
}

std::string level_text(const std::string& l, bool de) {
    if (l == "high") return de ? "hoch" : "high";
    if (l == "low") return de ? "niedrig" : "low";
    return de ? "mittel" : "medium";
}

std::string basis_text(const std::string& b, bool de) {
    if (b == "observed") return de ? "an der Konsole beobachtet" : "observed on the console";
    if (b == "doc") return de ? "Dokumentation" : "documentation";
    if (b == "community") return de ? "Community-Meldung" : "community report";
    if (b == "derived") return de ? "abgeleitet" : "derived";
    return de ? "nicht bestätigt" : "not confirmed";
}

std::string state_text(const std::string& s, bool de) {
    if (s == "applied") return de ? "angewendet" : "applied";
    if (s == "undone") return de ? "rückgängig gemacht" : "undone";
    if (s == "aborted") return de ? "abgebrochen" : "aborted";
    return de ? "unvollständig" : "incomplete";
}

std::string time_text(const std::string& ts) {
    // YYYYMMDDTHHMMSSZ -> "YYYY-MM-DD HH:MM UTC"
    if (ts.size() != 16 || ts[8] != 'T' || ts[15] != 'Z') return ts;
    return ts.substr(0, 4) + "-" + ts.substr(4, 2) + "-" + ts.substr(6, 2) + " " + ts.substr(9, 2) + ":" + ts.substr(11, 2) + " UTC";
}

std::string str_fact(const Facts& f, const char* key) {
    const json* v = f.get(key);
    return v && v->is_string() ? v->get<std::string>() : std::string();
}

std::string clip(const std::string& s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;  // nicht mitten in ein UTF-8-Zeichen schneiden
    return s.substr(0, cut) + "...";
}

std::string one_line(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    return s;
}

bool proc_running(const Facts& f, const std::string& name) {
    const json* v = f.get("proc.running");
    if (!v || !v->is_array()) return false;
    return std::any_of(v->begin(), v->end(), [&](const json& e) { return e.is_string() && e.get<std::string>() == name; });
}

// "kstuff 1.10 (drakmor)", "laeuft, Version unbekannt" oder leer, wenn weder Version noch Prozess da sind.
std::string component_line(const Facts& f, const char* proc, const char* version_key, const char* origin_key, bool de) {
    const std::string v = str_fact(f, version_key), origin = origin_key ? str_fact(f, origin_key) : std::string();
    if (!v.empty()) return v + (origin.empty() ? "" : " (" + origin + ")");
    if (proc && proc_running(f, proc)) return de ? "läuft, Version unbekannt" : "running, version unknown";
    return {};
}

std::string size_text(double bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f GB", bytes / 1e9);
    return buf;
}

std::vector<std::string> known_payloads(const Facts& f) {
    static const std::set<std::string> kKnown = {"elfldr", "etahen", "kstuff", "shadowmountplus", "backpork", "pldmgr",
                                                 "ftpsrv", "klogsrv", "shsrv", "nanodns", "websrv", "ps5tm", "fs_cleaner"};
    std::vector<std::string> out;
    if (const json* v = f.get("proc.running"); v && v->is_array())
        for (const auto& e : *v)
            if (e.is_string() && kKnown.count(e.get<std::string>()) && std::find(out.begin(), out.end(), e.get<std::string>()) == out.end())
                out.push_back(e.get<std::string>());
    return out;
}

std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) out += (i ? sep : "") + v[i];
    return out;
}

// "storage.mounts[].free_pct" -> "free_pct", "proc.running" -> "running"; leer, wenn die Regel keinen Fakt nennt.
std::string evidence_label(const std::string& path) {
    const auto pos = path.rfind('.');
    std::string seg = pos == std::string::npos ? path : path.substr(pos + 1);
    seg.erase(std::remove_if(seg.begin(), seg.end(), [](char c) { return c == '[' || c == ']'; }), seg.end());
    return seg;
}

bool looks_like_failure(const std::string& line) {
    static const std::regex words(R"(\b(failed|failure|panic|fatal|cannot|can't|unable|denied|corrupt(ed)?|timed out|aborted|exception|invalid)\b)",
                                  std::regex::icase);
    static const std::regex code(R"(\berror\s*[:=]\s*(-?[1-9][0-9]*|0[xX][0-9a-fA-F]*[1-9a-fA-F]))", std::regex::icase);
    static const std::regex upper(R"(\bERROR\b)");
    return std::regex_search(line, words) || std::regex_search(line, code) || std::regex_search(line, upper);
}

// Ohne Uhrzeit am Anfang, Zahlen und Kennungen (>= 6 Hex-Zeichen mit mindestens einer Ziffer) als '#': so bilden Zeilen, die
// sich nur darin unterscheiden, eine Gruppe.
std::string normalize_log_line(const std::string& line) {
    std::size_t i = 0;
    if (!line.empty() && line[0] == '[') {
        const auto e = line.find(']');
        if (e != std::string::npos && e <= 10) i = e + 1;
    }
    while (i < line.size() && line[i] == ' ') ++i;
    std::string out;
    while (i < line.size()) {
        if (std::isalnum(static_cast<unsigned char>(line[i]))) {
            std::size_t j = i;
            while (j < line.size() && std::isalnum(static_cast<unsigned char>(line[j]))) ++j;
            const std::string tok = line.substr(i, j - i);
            const bool hex = std::all_of(tok.begin(), tok.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
            const bool digit = std::any_of(tok.begin(), tok.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
            const bool prefixed_hex = tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X') &&
                                      std::all_of(tok.begin() + 2, tok.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
            if (prefixed_hex || (tok.size() >= 6 && hex && digit)) out += '#';
            else
                for (char c : tok) out += std::isdigit(static_cast<unsigned char>(c)) ? '#' : c;
            i = j;
        } else {
            out += line[i++];
        }
    }
    std::string collapsed;
    for (char c : out)
        if (!(c == '#' && !collapsed.empty() && collapsed.back() == '#')) collapsed += c;
    return collapsed;
}

std::string md_code(std::string s) {
    std::replace(s.begin(), s.end(), '`', '\'');
    return "`" + one_line(clip(s, 240)) + "`";
}

}  // namespace

std::string anonymize(const std::string& text) {
    // Reihenfolge: erst Schluessel/Wert und Pfade, dann Adressen, zuletzt lose 16-Hex-Kennungen.
    return mask_account_hex(mask_ipv4(mask_ipv6(mask_mac(mask_user_home(mask_key_values(text))))));
}

std::vector<UnknownLine> unknown_error_lines(const Facts& facts, const std::vector<Finding>& findings, std::size_t max_groups) {
    const json* lines = facts.get("smp.log.lines");
    if (!lines || !lines->is_array()) return {};

    std::set<std::string> explained;  // Zeilen, die schon als Evidenz eines Befunds im Bericht stehen
    for (const auto& f : findings)
        if (f.evidence_path == "smp.log.lines") explained.insert(f.evidence.begin(), f.evidence.end());

    std::vector<UnknownLine> groups;
    std::vector<std::string> keys;
    for (const auto& l : *lines) {
        if (!l.is_string()) continue;
        const std::string line = l.get<std::string>();
        if (explained.count(line) || !looks_like_failure(line)) continue;
        const std::string key = normalize_log_line(line);
        const auto it = std::find(keys.begin(), keys.end(), key);
        if (it == keys.end()) {
            keys.push_back(key);
            groups.push_back({line, 1});
        } else {
            ++groups[static_cast<std::size_t>(it - keys.begin())].count;
        }
    }
    std::stable_sort(groups.begin(), groups.end(), [](const UnknownLine& a, const UnknownLine& b) { return a.count > b.count; });
    if (groups.size() > max_groups) groups.resize(max_groups);
    return groups;
}

ReportInput make_report_input(const Platform& p, const Facts& facts, const RuleSet& rules, std::vector<Finding> findings,
                              const std::string& timestamp) {
    ReportInput in;
    in.created_utc = timestamp;
    in.rules_version = rules.db_version;
    in.rule_count = rules.rules.size();
    in.findings = std::move(findings);
    in.coverage = evaluate_coverage(rules, facts);
    in.facts = facts;
    in.history = history(p);
    in.unknown_lines = unknown_error_lines(facts, in.findings);
    return in;
}

std::string report_path_for(const std::string& timestamp) {
    if (timestamp.empty() || timestamp.size() > 40) return {};
    for (char c : timestamp)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return {};
    return std::string(kReportDir) + "/report_" + timestamp + ".md";
}

std::string build_report(const ReportInput& in, ReportLang lang) {
    const bool de = lang == ReportLang::De;
    const Facts& f = in.facts;
    std::ostringstream o;

    o << (de ? "# PS5 Medic – Bericht\n\n" : "# PS5 Medic – Report\n\n");
    o << "- " << (de ? "Erstellt: " : "Created: ") << time_text(in.created_utc) << "\n";
    o << "- PS5 Medic " << kMedicVersion << ", " << (de ? "Regelbasis " : "rules ") << in.rules_version << " (" << in.rule_count << ")\n\n";

    o << (de ? "## Umgebung\n\n" : "## Environment\n\n");
    const std::string fw = str_fact(f, "fw.version"), model = str_fact(f, "fw.model");
    o << "- " << (de ? "Firmware: " : "Firmware: ") << (fw.empty() ? (de ? "unbekannt" : "unknown") : fw) << "\n";
    if (!model.empty()) o << "- " << (de ? "Modell: " : "Model: ") << model << "\n";
    if (auto l = component_line(f, "kstuff", "kstuff.version", "kstuff.origin", de); !l.empty()) o << "- kstuff: " << l << "\n";
    if (auto l = component_line(f, "shadowmountplus", "smp.version", nullptr, de); !l.empty()) o << "- ShadowMount+: " << l << "\n";
    if (auto l = component_line(f, "etahen", "etahen.version", nullptr, de); !l.empty()) o << "- etaHEN: " << l << "\n";
    if (auto v = str_fact(f, "ps5theme.version"); !v.empty()) o << "- ps5-theme: " << v << "\n";
    if (auto p = known_payloads(f); !p.empty()) o << "- " << (de ? "Laufende Payloads: " : "Running payloads: ") << join(p, ", ") << "\n";
    if (const json* m = f.get("storage.mounts"); m && m->is_array())
        for (const auto& s : *m) {
            // Leere Mountpunkte (/mnt/usb1 ohne Laufwerk, /mnt/ext0) melden auf der Konsole die Groesse eines winzigen
            // Dateisystems (auf der Test-Konsole "0,0 GB, 80 %"): nur Laufwerke ab 1 GB zaehlen.
            if (!s.is_object() || !s.contains("total") || !s["total"].is_number() || s["total"].get<double>() < 1e9) continue;
            const double total = s["total"].get<double>(), free_b = s.contains("free") && s["free"].is_number() ? s["free"].get<double>() : 0;
            char pct[16];
            std::snprintf(pct, sizeof pct, "%.0f", 100.0 * free_b / total);
            o << "- " << (de ? "Speicher " : "Storage ") << s.value("mount", std::string("?")) << ": " << size_text(free_b) << (de ? " frei von " : " free of ")
              << size_text(total) << " (" << pct << " %)\n";
        }
    o << "\n";

    if (!in.error_code.empty()) {
        o << (de ? "## Gemeldeter Fehlercode\n\n" : "## Reported error code\n\n");
        o << "- " << md_code(in.error_code) << ": " << (de ? in.error_title_de : in.error_title_en) << "\n\n";
    }

    o << (de ? "## Befunde (" : "## Findings (") << in.findings.size() << ")\n\n";
    if (in.findings.empty()) o << (de ? "Keine Befunde.\n\n" : "No findings.\n\n");
    for (const auto& x : in.findings) {
        o << "### [" << label(x.severity, de) << "] " << x.rule_id << "\n\n" << (de ? x.text_de : x.text_en) << "\n\n";
        const std::string label_text = evidence_label(x.evidence_path);
        for (const auto& e : x.evidence)
            o << "- " << (de ? "Evidenz" : "Evidence") << (label_text.empty() ? "" : " (" + label_text + ")") << ": " << md_code(e) << "\n";
        if (!x.fix.empty()) o << "- " << (de ? "Reparatur: " : "Repair: ") << md_code(x.fix) << "\n";
        o << "- " << (de ? "Quelle: " : "Source: ") << x.source << " (" << verified_text(x.verified, de) << ")\n";
        {
            const Confidence conf = effective_confidence(x);
            const std::string& reason = de ? conf.reason_de : conf.reason_en;
            o << "- " << (de ? "Verlässlichkeit: " : "Confidence: ") << level_text(conf.level, de) << " (" << basis_text(conf.basis, de) << ")";
            if (!reason.empty()) o << (de ? ". Grund: " : ". Reason: ") << reason;
            o << "\n";
        }
        {
            const std::string urgency = urgency_of(x);
            o << "- " << (de ? "Dringlichkeit: " : "Urgency: ") << (urgency == "high" ? (de ? "hoch" : "high") : (urgency == "medium" ? (de ? "mittel" : "medium") : (de ? "niedrig" : "low")));
            if (x.status == "new") o << (de ? ". Verlauf: neu seit der letzten Prüfung" : ". History: new since the last check");
            else if (x.status == "recurring") {
                const std::string day = x.first_seen.size() >= 8 ? x.first_seen.substr(0, 4) + "-" + x.first_seen.substr(4, 2) + "-" + x.first_seen.substr(6, 2) : x.first_seen;
                o << (de ? ". Verlauf: seit " : ". History: since ") << day << " (" << x.seen_runs << (de ? " Prüfungen in Folge)" : " checks in a row)");
            }
            if (is_pending(x)) o << (de ? ". Vorläufig: wird erst nach " : ". Provisional: confirmed only after ") << x.confirm_runs << (de ? " Prüfungen in Folge bestätigt" : " checks in a row");
            o << "\n";
        }
        o << "\n";
    }
    if (!in.metrics.empty()) {
        o << (de ? "## Messwerte\n\n" : "## Measurements\n\n");
        auto num = [](double v) {
            char b[32];
            std::snprintf(b, sizeof b, "%.1f", v);
            std::string s = b;
            if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
            return s;
        };
        for (const auto& m : in.metrics) {
            const std::string unit = m.unit.empty() ? "" : (m.unit == "%" ? " %" : " " + m.unit);
            o << "- " << (de ? m.text_de : m.text_en) << ": " << num(m.now) << unit;
            std::string extra;
            if (m.has_prev) extra += (de ? "zuletzt " : "last ") + num(m.prev);
            if (m.has_typical && m.samples >= 3) extra += (extra.empty() ? "" : ", ") + std::string(de ? "üblich ≈ " : "usual ≈ ") + num(m.typical) + (de ? " (aus " : " (from ") + std::to_string(m.samples) + (de ? " Prüfungen)" : " checks)");
            if (!extra.empty()) o << " (" << extra << ")";
            o << "\n";
        }
        o << "\n";
    }
    if (!in.resolved.empty()) {
        o << (de ? "## Seit der letzten Prüfung behoben (" : "## Resolved since the last check (") << in.resolved.size() << ")\n\n";
        for (const auto& id : in.resolved) o << "- " << id << "\n";
        o << "\n";
    }

    {
        std::vector<const CoverageResult*> untested;
        for (const auto& c : in.coverage)
            if (!c.tested) untested.push_back(&c);
        if (!untested.empty()) {
            o << (de ? "## Nicht geprüft (" : "## Not checked (") << untested.size() << ")\n\n";
            o << (de ? "Für diese Bereiche lagen keine lesbaren Daten vor. „Keine Befunde“ heißt dort nicht „in Ordnung“, sondern „nicht geprüft“.\n\n"
                     : "No readable data was available for these areas. “No findings” there does not mean “fine”, it means “not checked”.\n\n");
            for (const auto* c : untested) o << "- " << (de ? c->text_de : c->text_en) << "\n";
            o << "\n";
        }
    }

    if (!in.unknown_lines.empty()) {
        o << (de ? "## Unbekannte Fehlerzeilen (" : "## Unknown error lines (") << in.unknown_lines.size() << ")\n\n";
        o << (de ? "Diese Zeilen im ShadowMount+-Log sehen nach Fehlern aus, passen aber zu keiner Regel. Wenn etwas nicht funktioniert, "
                   "gehören sie in ein Issue des Projekts; daraus können neue Regeln entstehen.\n\n"
                 : "These lines in the ShadowMount+ log look like errors but match no rule. If something does not work, they belong in an "
                   "issue of the project; new rules can come from them.\n\n");
        for (const auto& u : in.unknown_lines) o << "- " << u.count << "× " << md_code(u.example) << "\n";
        o << "\n";
    }

    if (!in.history.empty()) {
        o << (de ? "## Reparaturen dieser Konsole\n\n" : "## Repairs on this console\n\n");
        for (const auto& t : in.history) o << "- " << t.txn << ": " << t.fix << " (" << t.rule << ") – " << state_text(t.state, de) << "\n";
        o << "\n";
    }

    o << (de ? "---\nAnonymisiert: IP- und MAC-Adressen, Benutzer- und Konto-IDs sowie Seriennummern wurden entfernt. "
               "Datei- und Ordnernamen sind enthalten – vor dem Teilen prüfen.\n"
             : "---\nAnonymized: IP and MAC addresses, user and account IDs and serial numbers were removed. "
               "File and folder names are included – check before sharing.\n");
    return anonymize(o.str());
}

std::string build_report_compact(const ReportInput& in, ReportLang lang, std::size_t max_bytes, bool* truncated) {
    const bool de = lang == ReportLang::De;
    const Facts& f = in.facts;
    if (truncated) *truncated = false;

    auto line = [](const std::string& s) { return anonymize(one_line(s)) + "\n"; };
    std::string head = line(std::string("PS5 Medic ") + kMedicVersion + " " + time_text(in.created_utc));
    const std::string fw = str_fact(f, "fw.version"), model = str_fact(f, "fw.model");
    head += line("FW " + (fw.empty() ? std::string("?") : fw) + (model.empty() ? "" : " " + model));
    std::string versions;
    for (auto& [name, v] : std::vector<std::pair<std::string, std::string>>{{"kstuff", str_fact(f, "kstuff.version")},
                                                                           {"SM+", str_fact(f, "smp.version")},
                                                                           {"ps5-theme", str_fact(f, "ps5theme.version")}})
        if (!v.empty()) versions += (versions.empty() ? "" : ", ") + name + " " + v;
    if (!versions.empty()) head += line(versions);
    if (!in.error_code.empty()) head += line((de ? "Fehlercode " : "Error code ") + in.error_code);
    head += line(in.findings.empty() ? (de ? "Keine Befunde" : "No findings")
                                     : std::to_string(in.findings.size()) + (de ? " Befund(e):" : " finding(s):"));

    {
        std::size_t untested = 0;
        for (const auto& c : in.coverage) untested += c.tested ? 0 : 1;
        if (untested) head += line(std::to_string(untested) + (de ? " Bereich(e) nicht geprüft" : " area(s) not checked"));
    }
    if (!in.unknown_lines.empty())
        head += line(std::to_string(in.unknown_lines.size()) + (de ? " unbekannte Fehlerzeile(n) im Bericht" : " unknown error line(s) in the report"));

    std::string foot;
    if (!in.saved_path.empty()) foot = line((de ? "Bericht: " : "Report: ") + in.saved_path);

    std::string body;
    std::size_t shown = 0;
    for (const auto& x : in.findings) {
        std::string block = line("[" + label(x.severity, de) + "] " + x.rule_id);
        std::size_t n = 0;
        const std::string label_text = evidence_label(x.evidence_path);
        for (const auto& e : x.evidence) {
            if (n++ >= 3) break;
            block += line(" > " + (label_text.empty() ? "" : label_text + ": ") + clip(e, 90));
        }
        const std::string more = line(de ? "... weitere Befunde im Bericht" : "... more findings in the report");
        const bool last = shown + 1 == in.findings.size();
        if (head.size() + body.size() + block.size() + foot.size() + (last ? 0 : more.size()) > max_bytes) {
            body += more;
            if (truncated) *truncated = true;
            break;
        }
        body += block;
        ++shown;
    }
    std::string out = head + body + foot;
    if (out.size() > max_bytes) {  // Kopfzeilen allein zu lang (sehr lange Pfade): hart kuerzen
        out.resize(max_bytes);
        if (truncated) *truncated = true;
    }
    return out;
}

}  // namespace medic
