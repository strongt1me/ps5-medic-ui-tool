// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/report.hpp"

#include "medic/checks.hpp"
#include "medic/history.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
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

bool after_ok(const std::string& s, std::size_t i) { return i >= s.size() || !is_word(s[i]); }
// Kennungen (MAC, Konto-ID, IP): ein Unterstrich daneben ("savedata_<ID>", "_<MAC>_") ist eine Grenze, kein Teil des Worts.
bool id_before_ok(const std::string& s, std::size_t i) { return i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1])); }
bool id_after_ok(const std::string& s, std::size_t i) { return i >= s.size() || !std::isalnum(static_cast<unsigned char>(s[i])); }

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

// Zugangsdaten in Logzeilen: "smb://benutzer:passwort@host", "password=x", "user: bob", {"token": "x"}. Nur der Wert wird ersetzt; Schluessel und der Rest der
// Zeile bleiben lesbar. (Frueher wurden nur Seriennummern und Kennungen geschwaerzt: eine Zeile wie "smb connect failed user=bob password=hunter2" ging in
// den Bericht und den QR-Code.)
std::string mask_credentials(const std::string& s) {
    // Starke Namen zaehlen auch mitten im Wort ("mypassword", "userPassword") und mit ':' oder '='. Schwache ("user", "login", "pass", "username") nur am Wortanfang und
    // nur mit '=' oder als JSON-Feld ("user": ...): "Tests: pass: 12" oder "Error: user: not found" sind keine Zugangsdaten.
    struct Name {
        const char* text;
        bool strong;
    };
    static const Name kNames[] = {{"password", true}, {"passwd", true}, {"passphrase", true}, {"secret", true}, {"token", true}, {"api_key", true}, {"apikey", true},
                                  {"credential", true}, {"authorization", true}, {"psk", true}, {"secret_key", true}, {"private_key", true}, {"access_key", true}, {"username", false}, {"user", false}, {"login", false}, {"pass", false}};
    // 1) Benutzerdaten in Adressen: scheme://user:pass@host
    std::string t = s;
    for (std::size_t at = t.find("://"); at != std::string::npos; at = t.find("://", at + 3)) {
        const std::size_t start = at + 3;
        const std::size_t end = t.find_first_of("/?# \t\r\n\"'", start);
        const std::size_t stop = end == std::string::npos ? t.size() : end;
        const std::size_t amp = start < stop ? t.substr(start, stop - start).rfind('@') : std::string::npos;  // nur im Host-Teil suchen, nicht bis zum Textanfang
        if (amp != std::string::npos && t.compare(start, amp, "***") != 0) t.replace(start, amp, "[CREDENTIALS]");  // "***": schon geschwaerzt (Export)
    }
    // 2) Schluessel = Wert (auch hinter '_' wie smb_user, auch JSON)
    std::string lower;
    lower.reserve(t.size());
    for (char c : t) lower += low(c);
    std::string out;
    std::size_t i = 0;
    while (i < t.size()) {
        bool done = false;
        {
            const bool word_start = i == 0 || !std::isalnum(static_cast<unsigned char>(t[i - 1]));
            for (const Name& nm : kNames) {
                if (!word_start && !nm.strong) continue;
                const char* name = nm.text;
                const std::size_t len = std::char_traits<char>::length(name);
                if (lower.compare(i, len, name) != 0) continue;
                std::size_t j = i + len;
                const bool quoted = j < t.size() && (t[j] == '"' || t[j] == '\'');  // JSON-/Python-Feld
                if (quoted) ++j;
                const std::size_t gap_start = j;
                while (j < t.size() && (t[j] == ' ' || t[j] == '\t')) ++j;
                const bool spaced = j > gap_start;
                if (j >= t.size() || (t[j] != ':' && t[j] != '=')) continue;
                const bool ini_style = spaced && t[j] == '=' && nm.strong;
                if (t[j] == ':' && !nm.strong && !quoted) continue;
                ++j;
                while (j < t.size() && (t[j] == ' ' || t[j] == '\t')) ++j;
                out.append(t, i, j - i);  // Schluessel samt Trenner
                if (j < t.size() && (t[j] == '"' || t[j] == '\'')) {
                    const char q = t[j];
                    std::size_t close = j + 1;  // ein mit Backslash maskiertes Anfuehrungszeichen beendet den Wert nicht
                    while (close < t.size() && t[close] != q) close += t[close] == '\\' ? 2 : 1;
                    if (close > t.size()) close = std::string::npos;
                    out += q;
                    out += "[SECRET]";
                    j = close == std::string::npos ? t.size() : close;  // das schliessende Anfuehrungszeichen bleibt
                } else if (t.compare(j, 3, "***") == 0) {  // schon geschwaerzt (Export): unveraendert lassen
                    out += "***";
                    j += 3;
                } else if (ini_style) {  // "password = mein langer satz": der Wert kann Leerzeichen enthalten, also bis zum Zeilenende
                    out += "[SECRET]";
                    while (j < t.size() && t[j] != '\r' && t[j] != '\n') ++j;
                } else {
                    out += "[SECRET]";
                    const std::size_t vstart = j;
                    while (j < t.size() && std::strchr(" \t\r\n&,;)\"'", t[j]) == nullptr) ++j;
                    const std::string word = lower.substr(vstart, j - vstart);
                    if ((word == "bearer" || word == "basic") && j < t.size() && t[j] == ' ') {  // "token: Bearer abc": das eigentliche Geheimnis folgt dem Schema-Wort
                        ++j;
                        while (j < t.size() && std::strchr(" \t\r\n&,;)\"'", t[j]) == nullptr) ++j;
                    }
                }
                i = j;
                done = true;
                break;
            }
        }
        if (!done) out += t[i++];
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
                                {"userid", "[ID]"},            {"online_id", "[ID]"},         {"onlineid", "[ID]"},
                                {"npid", "[ID]"},              {"psnid", "[ID]"}};
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) lower += low(c);

    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        bool replaced = false;
        if (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1]))) {  // auch hinter '_' (console_serial, default_user_id)
            for (const Key& k : kKeys) {
                const std::size_t len = std::char_traits<char>::length(k.name);
                if (lower.compare(i, len, k.name) != 0) continue;
                std::size_t j = i + len;
                if (j < s.size() && s[j] == '"') ++j;  // JSON: "serial_number":
                while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
                if (j >= s.size() || (s[j] != ':' && s[j] != '=')) continue;
                ++j;
                while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) ++j;
                if (j < s.size() && s[j] == '"') ++j;  // JSON: Wert in Anfuehrungszeichen
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
        if (id_before_ok(s, i) && i + 17 <= s.size() && is_hex(s[i])) {
            const char sep = s[i + 2];
            bool ok = sep == ':' || sep == '-';
            for (int g = 0; ok && g < 6; ++g) {
                const std::size_t p = i + static_cast<std::size_t>(g) * 3;
                ok = is_hex(s[p]) && is_hex(s[p + 1]) && (g == 5 || s[p + 2] == sep);
            }
            if (ok && id_after_ok(s, i + 17)) {
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
        // Beginn einer Folge: nach einem Nicht-Wortzeichen oder Unterstrich, oder direkt hinter "<Beschriftung>:" (Beschriftung mit einem Nicht-Hex-Buchstaben:
        // "inet6:", "addr:", "ip:"). Mitten in einer gescheiterten Folge ("12:34:56") faengt keine neue an.
        bool label_before = false;
        if (i >= 2 && s[i - 1] == ':' && is_word(s[i - 2])) {
            std::size_t b = i - 1;
            while (b > 0 && is_word(s[b - 1])) --b;
            for (std::size_t k = b; k + 1 < i; ++k)
                if (std::isalpha(static_cast<unsigned char>(s[k])) && !is_hex(s[k])) label_before = true;
        }
        // Hinter einer Beschriftung beginnt die Folge nur mit einer Hex-Ziffer: "Class::Fade::Dead" (C++-Name) ist keine Adresse, "addr:fe80::1" schon
        const bool start = (is_hex(s[i]) || s[i] == ':') && (i == 0 || (!is_word(s[i - 1]) && s[i - 1] != ':') || (label_before && is_hex(s[i])));
        if (start) {
            std::size_t j = i;
            while (j < s.size() && (is_hex(s[j]) || s[j] == ':')) ++j;
            // "::ffff:192.168.1.5": die vier Dezimal-Oktette am Ende gehoeren zur Adresse
            std::size_t run_end = j;
            if (j < s.size() && s[j] == '.' && j > i && is_digit(s[j - 1])) {
                std::size_t k = j;
                int dots = 0;
                while (k < s.size() && (is_digit(s[k]) || s[k] == '.')) dots += s[k++] == '.';
                while (k > j && s[k - 1] == '.') {  // ein Satzpunkt hinter der Adresse gehoert nicht dazu
                    --k;
                    --dots;
                }
                if (dots == 3 && k > j && is_digit(s[k - 1])) run_end = k;
            }
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
            if (after_ok(s, run_end) && groups_ok && has_hex && ((double_colon && colons >= 2) || colons == 7)) {  // "std::bad_alloc und a::b_c" bleibt: '_' gehoert zum Namen
                out += "[IPV6]";
                i = run_end;
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
        if (is_digit(s[i]) && id_before_ok(s, i) && (i == 0 || s[i - 1] != '.')) {
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
            if (ok && octets == 4 && !trailing_digit && !more_parts && id_after_ok(s, p)) {
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
        if (id_before_ok(s, i) && start < s.size() && is_hex(s[start])) {
            std::size_t n = 0;
            while (start + n < s.size() && is_hex(s[start + n])) ++n;
            bool has_letter = false;
            for (std::size_t k = 0; k < n; ++k) has_letter = has_letter || !is_digit(s[start + k]);
            if (n == 16 && (prefixed || has_letter) && id_after_ok(s, start + n)) {
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

// Wahl des Textes je Berichtssprache (de, en, fr, it, es)
struct Lx {
    ReportLang lang;
    const char* operator()(const char* de, const char* en, const char* fr, const char* it, const char* es) const {
        switch (lang) {
            case ReportLang::De: return de;
            case ReportLang::En: return en;
            case ReportLang::Fr: return fr;
            case ReportLang::It: return it;
            case ReportLang::Es: return es;
        }
        return en;
    }
};

// Text aus den Daten (de/en stehen dort selbst), fr/it/es aus der Uebersetzung der Oberflaeche (in.l10n), sonst Englisch.
std::string pick_text(const ReportInput& in, ReportLang lang, const std::string& de, const std::string& en, const char* section, const std::string& id, const char* field) {
    if (lang == ReportLang::De) return de;
    if (lang == ReportLang::En || !in.l10n) return en;
    const std::string* t = in.l10n(lang, section, id, field);
    return t ? *t : en;
}

std::string label(Severity s, const Lx& T) {
    switch (s) {
        case Severity::Critical: return T("KRITISCH", "CRITICAL", "CRITIQUE", "CRITICO", "CRÍTICO");
        case Severity::Warning: return T("WARNUNG", "WARNING", "ATTENTION", "AVVISO", "AVISO");
        case Severity::Info: return T("HINWEIS", "NOTICE", "NOTE", "NOTA", "NOTA");
    }
    return "";
}

std::string verified_text(const std::string& v, const Lx& T) {
    if (v == "doc") return T("Dokumentation", "documentation", "documentation", "documentazione", "documentación");
    if (v == "community") return T("Community-Meldung", "community report", "rapport de la communauté", "segnalazione della community", "informe de la comunidad");
    return T("noch nicht bestätigt", "not verified yet", "pas encore vérifié", "non ancora verificato", "aún sin verificar");
}

std::string level_text(const std::string& l, const Lx& T) {
    if (l == "high") return T("hoch", "high", "élevée", "alta", "alta");
    if (l == "low") return T("niedrig", "low", "faible", "bassa", "baja");
    return T("mittel", "medium", "moyenne", "media", "media");
}

std::string basis_text(const std::string& b, const Lx& T) {
    if (b == "observed") return T("an der Konsole beobachtet", "observed on the console", "observé sur la console", "osservato sulla console", "observado en la consola");
    if (b == "doc") return T("Dokumentation", "documentation", "documentation", "documentazione", "documentación");
    if (b == "community") return T("Community-Meldung", "community report", "rapport de la communauté", "segnalazione della community", "informe de la comunidad");
    if (b == "derived") return T("abgeleitet", "derived", "déduit", "dedotto", "deducido");
    return T("nicht bestätigt", "not confirmed", "non confirmé", "non confermato", "no confirmado");
}

std::string state_text(const std::string& s, const Lx& T) {
    if (s == "applied") return T("angewendet", "applied", "appliquée", "applicata", "aplicada");
    if (s == "undone") return T("rückgängig gemacht", "undone", "annulée", "annullata", "deshecha");
    if (s == "aborted") return T("abgebrochen", "aborted", "interrompue", "interrotta", "abortada");
    return T("unvollständig", "incomplete", "incomplète", "incompleta", "incompleta");
}

// Tag aus "YYYYMMDD..." je Sprache: Deutsch TT.MM.JJJJ, Franzoesisch/Italienisch/Spanisch TT/MM/JJJJ, Englisch JJJJ-MM-TT.
std::string day_text(const std::string& ts, ReportLang lang) {
    if (ts.size() < 8) return ts;
    const std::string y = ts.substr(0, 4), m = ts.substr(4, 2), d = ts.substr(6, 2);
    switch (lang) {
        case ReportLang::De: return d + "." + m + "." + y;
        case ReportLang::En: break;
        case ReportLang::Fr:
        case ReportLang::It:
        case ReportLang::Es: return d + "/" + m + "/" + y;
    }
    return y + "-" + m + "-" + d;
}

// YYYYMMDDTHHMMSSZ -> "TT.MM.JJJJ HH:MM UTC" (je Sprache wie day_text)
std::string time_text(const std::string& ts, ReportLang lang) {
    if (ts.size() != 16 || ts[8] != 'T' || ts[15] != 'Z') return ts;
    return day_text(ts, lang) + " " + ts.substr(9, 2) + ":" + ts.substr(11, 2) + " UTC";
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
std::string component_line(const Facts& f, const char* proc, const char* version_key, const char* origin_key, const Lx& T) {
    const std::string v = str_fact(f, version_key), origin = origin_key ? str_fact(f, origin_key) : std::string();
    if (!v.empty()) return v + (origin.empty() ? "" : " (" + origin + ")");
    if (proc && proc_running(f, proc)) return T("läuft, Version unbekannt", "running, version unknown", "actif, version inconnue", "in esecuzione, versione sconosciuta", "en ejecución, versión desconocida");
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
    s = anonymize(s);  // vor dem Kuerzen: eine am Schnitt halbierte Kennung passt sonst auf kein Muster mehr
    std::replace(s.begin(), s.end(), '`', '\'');
    return "`" + one_line(clip(s, 240)) + "`";
}

}  // namespace

std::string redact_credentials(const std::string& text) { return mask_credentials(text); }

std::string redact_ipv6(const std::string& text) { return mask_ipv6(text); }

std::string anonymize(const std::string& text) {
    // Reihenfolge: erst Schluessel/Wert und Pfade, dann Adressen, zuletzt lose 16-Hex-Kennungen.
    return mask_account_hex(mask_ipv4(mask_ipv6(mask_mac(mask_user_home(mask_key_values(mask_credentials(text)))))));
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
    in.selftests = load_last_selftest(p.fs(), &in.selftest_finished);
    in.unknown_lines = unknown_error_lines(facts, in.findings);
    return in;
}

std::string report_path_for(const std::string& timestamp) {
    if (timestamp.empty() || timestamp.size() > 40) return {};
    for (char c : timestamp)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return {};
    return std::string(kReportDir) + "/report_" + timestamp + ".md";
}

const char* report_lang_code(ReportLang l) {
    switch (l) {
        case ReportLang::De: return "de";
        case ReportLang::En: return "en";
        case ReportLang::Fr: return "fr";
        case ReportLang::It: return "it";
        case ReportLang::Es: return "es";
    }
    return "en";
}

ReportLang report_lang_from_code(const std::string& code) {
    if (code == "de") return ReportLang::De;
    if (code == "fr") return ReportLang::Fr;
    if (code == "it") return ReportLang::It;
    if (code == "es") return ReportLang::Es;
    return ReportLang::En;
}

std::string build_report(const ReportInput& in, ReportLang lang) {
    const Lx T{lang};
    const auto pick = [&](const std::string& de_text, const std::string& en_text, const char* section, const std::string& id, const char* field) {
        return pick_text(in, lang, de_text, en_text, section, id, field);
    };
    const Facts& f = in.facts;
    std::ostringstream o;

    o << (T("# PS5 Medic – Bericht\n\n", "# PS5 Medic – Report\n\n", "# PS5 Medic – Rapport\n\n", "# PS5 Medic – Rapporto\n\n", "# PS5 Medic – Informe\n\n"));
    o << "- " << (T("Erstellt: ", "Created: ", "Créé : ", "Creato: ", "Creado: ")) << time_text(in.created_utc, lang) << "\n";
    o << "- PS5 Medic " << kMedicVersion << ", " << (T("Regelbasis ", "rules ", "règles ", "regole ", "reglas ")) << in.rules_version << " (" << in.rule_count << ")\n\n";

    o << (T("## Umgebung\n\n", "## Environment\n\n", "## Environnement\n\n", "## Ambiente\n\n", "## Entorno\n\n"));
    const std::string fw = str_fact(f, "fw.version"), model = str_fact(f, "fw.model");
    o << "- " << (T("Firmware: ", "Firmware: ", "Firmware : ", "Firmware: ", "Firmware: ")) << (fw.empty() ? (T("unbekannt", "unknown", "inconnu", "sconosciuto", "desconocido")) : fw) << "\n";
    if (!model.empty()) o << "- " << (T("Modell: ", "Model: ", "Modèle : ", "Modello: ", "Modelo: ")) << model << "\n";
    if (auto l = component_line(f, "kstuff", "kstuff.version", "kstuff.origin", T); !l.empty()) o << "- kstuff: " << l << "\n";
    if (auto l = component_line(f, "shadowmountplus", "smp.version", nullptr, T); !l.empty()) o << "- ShadowMount+: " << l << "\n";
    if (auto l = component_line(f, "etahen", "etahen.version", nullptr, T); !l.empty()) o << "- etaHEN: " << l << "\n";
    if (auto v = str_fact(f, "ps5theme.version"); !v.empty()) o << "- ps5-theme: " << v << "\n";
    if (auto p = known_payloads(f); !p.empty()) o << "- " << (T("Laufende Payloads: ", "Running payloads: ", "Payloads actifs : ", "Payload in esecuzione: ", "Payloads en ejecución: ")) << join(p, ", ") << "\n";
    if (const json* m = f.get("storage.mounts"); m && m->is_array())
        for (const auto& s : *m) {
            // Leere Mountpunkte (/mnt/usb1 ohne Laufwerk, /mnt/ext0) melden auf der Konsole die Groesse eines winzigen
            // Dateisystems (auf der Test-Konsole "0,0 GB, 80 %"): nur Laufwerke ab 1 GB zaehlen.
            if (!s.is_object() || !s.contains("total") || !s["total"].is_number() || s["total"].get<double>() < 1e9) continue;
            const double total = s["total"].get<double>(), free_b = s.contains("free") && s["free"].is_number() ? s["free"].get<double>() : 0;
            char pct[16];
            std::snprintf(pct, sizeof pct, "%.0f", 100.0 * free_b / total);
            o << "- " << (T("Speicher ", "Storage ", "Stockage ", "Archiviazione ", "Almacenamiento ")) << s.value("mount", std::string("?")) << ": " << size_text(free_b) << (T(" frei von ", " free of ", " libres sur ", " liberi su ", " libres de "))
              << size_text(total) << " (" << pct << " %)\n";
        }
    o << "\n";

    if (!in.error_code.empty()) {
        o << (T("## Gemeldeter Fehlercode\n\n", "## Reported error code\n\n", "## Code d'erreur indiqué\n\n", "## Codice di errore indicato\n\n", "## Código de error indicado\n\n"));
        o << "- " << md_code(in.error_code) << ": " << pick(in.error_title_de, in.error_title_en, "error_codes", in.error_code, "title") << "\n\n";
    }

    o << (T("## Befunde (", "## Findings (", "## Résultats (", "## Rilevamenti (", "## Hallazgos (")) << in.findings.size() << ")\n\n";
    if (in.findings.empty()) o << (T("Keine Befunde.\n\n", "No findings.\n\n", "Aucun résultat.\n\n", "Nessun rilevamento.\n\n", "Sin hallazgos.\n\n"));
    for (const auto& x : in.findings) {
        o << "### [" << label(x.severity, T) << "] " << x.rule_id << "\n\n" << pick(x.text_de, x.text_en, "rules", x.rule_id, "text") << "\n\n";
        const std::string label_text = evidence_label(x.evidence_path);
        for (const auto& e : x.evidence)
            o << "- " << (T("Evidenz", "Evidence", "Preuve", "Prova", "Evidencia")) << (label_text.empty() ? "" : " (" + label_text + ")") << ": " << md_code(e) << "\n";
        if (!x.fix.empty()) o << "- " << (T("Reparatur: ", "Repair: ", "Réparation : ", "Riparazione: ", "Reparación: ")) << md_code(x.fix) << "\n";
        o << "- " << (T("Quelle: ", "Source: ", "Source : ", "Fonte: ", "Fuente: ")) << x.source << " (" << verified_text(x.verified, T) << ")\n";
        {
            const Confidence conf = effective_confidence(x);
            const std::string reason = pick(conf.reason_de, conf.reason_en, "rules", x.rule_id, "confidence_reason");
            o << "- " << (T("Verlässlichkeit: ", "Confidence: ", "Confiance : ", "Affidabilità: ", "Confianza: ")) << level_text(conf.level, T) << " (" << basis_text(conf.basis, T) << ")";
            if (!reason.empty()) o << (T(". Grund: ", ". Reason: ", ". Raison : ", ". Motivo: ", ". Motivo: ")) << reason;
            o << "\n";
        }
        {
            const std::string urgency = urgency_of(x);
            o << "- " << (T("Dringlichkeit: ", "Urgency: ", "Urgence : ", "Urgenza: ", "Urgencia: ")) << (urgency == "high" ? (T("hoch", "high", "élevée", "alta", "alta")) : (urgency == "medium" ? (T("mittel", "medium", "moyenne", "media", "media")) : (T("niedrig", "low", "faible", "bassa", "baja"))));
            if (x.status == "new") o << (T(". Verlauf: neu seit der letzten Prüfung", ". History: new since the last check", ". Historique : nouveau depuis le dernier diagnostic", ". Cronologia: nuovo dall'ultimo check-up", ". Historial: nuevo desde el último chequeo"));
            else if (x.status == "recurring") {
                const std::string day = day_text(x.first_seen, lang);
                o << (T(". Verlauf: seit ", ". History: since ", ". Historique : depuis le ", ". Cronologia: dal ", ". Historial: desde el ")) << day << " (" << x.seen_runs << (T(" Prüfungen in Folge)", " checks in a row)", " diagnostics de suite)", " check-up di fila)", " chequeos seguidos)"));
            }
            if (is_pending(x)) o << (T(". Vorläufig: wird erst nach ", ". Provisional: confirmed only after ", ". Provisoire : confirmé seulement après ", ". Provvisorio: confermato solo dopo ", ". Provisional: se confirma solo tras ")) << x.confirm_runs << (T(" Prüfungen in Folge bestätigt", " checks in a row", " diagnostics de suite", " check-up di fila", " chequeos seguidos"));
            o << "\n";
        }
        o << "\n";
    }
    if (!in.metrics.empty()) {
        o << (T("## Messwerte\n\n", "## Measurements\n\n", "## Mesures\n\n", "## Misurazioni\n\n", "## Mediciones\n\n"));
        auto num = [](double v) {
            char b[32];
            std::snprintf(b, sizeof b, "%.1f", v);
            std::string s = b;
            if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
            return s;
        };
        for (const auto& m : in.metrics) {
            const std::string unit = m.unit.empty() ? "" : (m.unit == "%" ? " %" : " " + m.unit);
            o << "- " << pick(m.text_de, m.text_en, "metrics", m.id, "text") << ": " << num(m.now) << unit;
            std::string extra;
            if (m.has_prev) extra += (T("zuletzt ", "last ", "dernier ", "ultimo ", "último ")) + num(m.prev);
            if (m.has_typical && m.samples >= 3) extra += (extra.empty() ? "" : ", ") + std::string(T("üblich ≈ ", "usual ≈ ", "habituel ≈ ", "abituale ≈ ", "habitual ≈ ")) + num(m.typical) + (T(" (aus ", " (from ", " (sur ", " (da ", " (de ")) + std::to_string(m.samples) + (T(" Prüfungen)", " checks)", " diagnostics)", " check-up)", " chequeos)"));
            if (!extra.empty()) o << " (" << extra << ")";
            o << "\n";
        }
        o << "\n";
    }
    if (!in.selftests.empty()) {
        o << (T("## Selbsttest\n\n", "## Self-test\n\n", "## Autotest\n\n", "## Autotest\n\n", "## Autotest\n\n"));
        o << (T("Nur lesende Tests. „Bestanden“ heißt „nicht aufgefallen“, nie „gesund“; „übersprungen“ und „nicht gelaufen“ heißen nicht bestanden.\n\n", "Read-only tests. “Passed” means “nothing stood out”, never “healthy”; “skipped” and “not run” do not mean passed.\n\n", "Tests en lecture seule. « Réussi » signifie « rien de remarqué », jamais « sain » ; « ignoré » et « non exécuté » ne signifient pas réussi.\n\n", "Test di sola lettura. «Superato» significa «nulla di anomalo», mai «sano»; «saltato» e «non eseguito» non significano superato.\n\n", "Pruebas de solo lectura. «Superado» significa «nada llamó la atención», nunca «sano»; «omitido» y «no ejecutado» no significan superado.\n\n"));
        if (!in.selftest_finished.empty()) o << (T("Letzter Lauf: ", "Last run: ", "Dernière exécution : ", "Ultima esecuzione: ", "Última ejecución: ")) << time_text(in.selftest_finished, lang) << "\n\n";
        auto status_name = [&](TestStatus st) -> std::string {
            switch (st) {
                case TestStatus::Passed: return T("bestanden", "passed", "réussi", "superato", "superado");
                case TestStatus::Warn: return T("Warnung", "warning", "avertissement", "avviso", "aviso");
                case TestStatus::Failed: return T("fehlgeschlagen", "failed", "échec", "fallito", "fallido");
                case TestStatus::Error: return T("Testfehler", "test error", "erreur de test", "errore del test", "error de prueba");
                case TestStatus::Cancelled: return T("abgebrochen", "cancelled", "annulé", "annullato", "cancelado");
                case TestStatus::Skipped: return T("übersprungen", "skipped", "ignoré", "saltato", "omitido");
                case TestStatus::NotRun: break;
            }
            return T("nicht gelaufen", "not run", "non exécuté", "non eseguito", "no ejecutado");
        };
        for (const auto& t : in.selftests) {
            const TestDef* def = find_test(t.id);
            o << "- " << (def ? pick(def->name_de, def->name_en, "selftests", def->id, "name") : t.id) << ": " << status_name(t.status);
            if (!t.reason.empty()) o << " (" << t.reason << ")";
            for (const auto& m : t.measurements) {
                char b[32];
                std::snprintf(b, sizeof b, "%.1f", m.value);
                std::string v = b;
                if (v.size() > 2 && v.compare(v.size() - 2, 2, ".0") == 0) v.resize(v.size() - 2);
                o << ", " << m.name << " " << v << (m.unit.empty() ? "" : " " + m.unit);
            }
            o << "\n";
            if (def && (t.status == TestStatus::Passed || t.status == TestStatus::Warn)) o << "  " << (T("Beweist nicht: ", "Does not prove: ", "Ne prouve pas : ", "Non dimostra: ", "No demuestra: ")) << pick(def->proves_not_de, def->proves_not_en, "selftests", def->id, "proves_not") << "\n";
        }
        o << "\n";
    }
    if (!in.resolved.empty()) {
        o << (T("## Seit der letzten Prüfung behoben (", "## Resolved since the last check (", "## Résolus depuis le dernier diagnostic (", "## Risolti dall'ultimo check-up (", "## Resueltos desde el último chequeo (")) << in.resolved.size() << ")\n\n";
        const auto links = repairs_between(in.history, in.resolved, in.prev_ts, in.created_utc);
        for (const auto& id : in.resolved) {
            o << "- " << id;
            for (const auto& l : links)
                if (l.rule_id == id) o << (T(" – durch Reparatur: ", " – by repair: ", " – par réparation : ", " – con una riparazione: ", " – mediante reparación: ")) << l.fix << " (" << l.ts << ")";
            o << "\n";
        }
        o << "\n";
    }

    {
        std::vector<const CoverageResult*> untested;
        for (const auto& c : in.coverage)
            if (!c.tested) untested.push_back(&c);
        if (!untested.empty()) {
            o << (T("## Nicht geprüft (", "## Not checked (", "## Non vérifié (", "## Non controllato (", "## Sin comprobar (")) << untested.size() << ")\n\n";
            o << (T("Für diese Bereiche lagen keine lesbaren Daten vor. „Keine Befunde“ heißt dort nicht „in Ordnung“, sondern „nicht geprüft“.\n\n", "No readable data was available for these areas. “No findings” there does not mean “fine”, it means “not checked”.\n\n", "Aucune donnée lisible n'était disponible pour ces domaines. « Aucun résultat » ne signifie pas « en ordre » mais « non vérifié ».\n\n", "Per queste aree non c'erano dati leggibili. «Nessun rilevamento» qui non significa «a posto», ma «non controllato».\n\n", "No había datos legibles para estas áreas. «Sin hallazgos» no significa «todo bien», sino «sin comprobar».\n\n"));
            for (const auto* c : untested) o << "- " << pick(c->text_de, c->text_en, "coverage", c->id, "text") << "\n";
            o << "\n";
        }
    }

    if (!in.unknown_lines.empty()) {
        o << (T("## Unbekannte Fehlerzeilen (", "## Unknown error lines (", "## Lignes d'erreur inconnues (", "## Righe di errore sconosciute (", "## Líneas de error desconocidas (")) << in.unknown_lines.size() << ")\n\n";
        o << (T("Diese Zeilen im ShadowMount+-Log sehen nach Fehlern aus, passen aber zu keiner Regel. Wenn etwas nicht funktioniert, "
                   "gehören sie in ein Issue des Projekts; daraus können neue Regeln entstehen.\n\n", "These lines in the ShadowMount+ log look like errors but match no rule. If something does not work, they belong in an "
                   "issue of the project; new rules can come from them.\n\n", "Ces lignes du journal de ShadowMount+ ressemblent à des erreurs mais ne correspondent à aucune règle. Si quelque chose ne fonctionne pas, elles sont à joindre à un ticket du projet ; de nouvelles règles peuvent en naître.\n\n", "Queste righe del registro di ShadowMount+ sembrano errori ma non corrispondono ad alcuna regola. Se qualcosa non funziona, vanno allegate a una issue del progetto; da esse possono nascere nuove regole.\n\n", "Estas líneas del registro de ShadowMount+ parecen errores pero no coinciden con ninguna regla. Si algo no funciona, deben ir en una incidencia del proyecto; de ellas pueden salir reglas nuevas.\n\n"));
        for (const auto& u : in.unknown_lines) o << "- " << u.count << "× " << md_code(u.example) << "\n";
        o << "\n";
    }

    if (!in.history.empty()) {
        o << (T("## Reparaturen dieser Konsole\n\n", "## Repairs on this console\n\n", "## Réparations sur cette console\n\n", "## Riparazioni su questa console\n\n", "## Reparaciones en esta consola\n\n"));
        for (const auto& t : in.history) o << "- " << t.txn << ": " << t.fix << " (" << t.rule << ") – " << state_text(t.state, T) << "\n";
        o << "\n";
    }

    o << (T("---\nAnonymisiert: IP- und MAC-Adressen, Benutzer- und Konto-IDs sowie Seriennummern wurden entfernt. "
               "Datei- und Ordnernamen sind enthalten – vor dem Teilen prüfen.\n", "---\nAnonymized: IP and MAC addresses, user and account IDs and serial numbers were removed. "
               "File and folder names are included – check before sharing.\n", "---\nAnonymisé : les adresses IP et MAC, les identifiants d'utilisateur et de compte ainsi que les numéros de série ont été supprimés. Les noms de fichiers et de dossiers sont inclus – à vérifier avant de partager.\n", "---\nAnonimizzato: indirizzi IP e MAC, ID utente e account e numeri di serie sono stati rimossi. I nomi di file e cartelle sono inclusi – controlla prima di condividere.\n", "---\nAnonimizado: se han eliminado las direcciones IP y MAC, los ID de usuario y de cuenta y los números de serie. Se incluyen nombres de archivos y carpetas – revísalos antes de compartir.\n"));
    return anonymize(o.str());
}

std::string build_report_compact(const ReportInput& in, ReportLang lang, std::size_t max_bytes, bool* truncated) {
    const Lx T{lang};
    const Facts& f = in.facts;
    if (truncated) *truncated = false;

    auto line = [](const std::string& s) { return anonymize(one_line(s)) + "\n"; };
    std::string head = line(std::string("PS5 Medic ") + kMedicVersion + " " + time_text(in.created_utc, lang));
    const std::string fw = str_fact(f, "fw.version"), model = str_fact(f, "fw.model");
    head += line("FW " + (fw.empty() ? std::string("?") : fw) + (model.empty() ? "" : " " + model));
    std::string versions;
    for (auto& [name, v] : std::vector<std::pair<std::string, std::string>>{{"kstuff", str_fact(f, "kstuff.version")},
                                                                           {"SM+", str_fact(f, "smp.version")},
                                                                           {"ps5-theme", str_fact(f, "ps5theme.version")}})
        if (!v.empty()) versions += (versions.empty() ? "" : ", ") + name + " " + v;
    if (!versions.empty()) head += line(versions);
    if (!in.error_code.empty()) head += line((T("Fehlercode ", "Error code ", "Code d'erreur ", "Codice di errore ", "Código de error ")) + in.error_code);
    head += line(in.findings.empty() ? (T("Keine Befunde", "No findings", "Aucun résultat", "Nessun rilevamento", "Sin hallazgos"))
                                     : std::to_string(in.findings.size()) + (T(" Befund(e):", " finding(s):", " résultat(s) :", " rilevamento/i:", " hallazgo(s):")));

    {
        std::size_t untested = 0;
        for (const auto& c : in.coverage) untested += c.tested ? 0 : 1;
        if (untested) head += line(std::to_string(untested) + (T(" Bereich(e) nicht geprüft", " area(s) not checked", " domaine(s) non vérifié(s)", " area/e non controllata/e", " área(s) sin comprobar")));
    }
    if (!in.unknown_lines.empty())
        head += line(std::to_string(in.unknown_lines.size()) + (T(" unbekannte Fehlerzeile(n) im Bericht", " unknown error line(s) in the report", " ligne(s) d'erreur inconnue(s) dans le rapport", " riga/righe di errore sconosciuta/e nel rapporto", " línea(s) de error desconocida(s) en el informe")));

    std::string foot;
    if (!in.saved_path.empty()) foot = line((T("Bericht: ", "Report: ", "Rapport : ", "Rapporto: ", "Informe: ")) + in.saved_path);

    std::string body;
    std::size_t shown = 0;
    for (const auto& x : in.findings) {
        std::string block = line("[" + label(x.severity, T) + "] " + x.rule_id);
        std::size_t n = 0;
        const std::string label_text = evidence_label(x.evidence_path);
        for (const auto& e : x.evidence) {
            if (n++ >= 3) break;
            block += line(" > " + (label_text.empty() ? "" : label_text + ": ") + clip(anonymize(e), 90));
        }
        const std::string more = line(T("... weitere Befunde im Bericht", "... more findings in the report", "... autres résultats dans le rapport", "... altri rilevamenti nel rapporto", "... más hallazgos en el informe"));
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
        std::size_t cut = max_bytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;  // nicht mitten in ein UTF-8-Zeichen schneiden
        out.resize(cut);
        if (truncated) *truncated = true;
    }
    return out;
}

}  // namespace medic
