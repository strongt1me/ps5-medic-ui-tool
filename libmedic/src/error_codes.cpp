// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/error_codes.hpp"
#include "medic/text_policy.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace medic {
namespace {

// Feste Muster von Hand statt als std::regex: jede std::regex-Konstruktion kostete auf der Konsole rund 10 ms, bei 40 Codes mit je drei Mustern
// 1,4 s Ladezeit (Messung 12. Nov. 2026). Gleiche Bedeutung wie die bisherigen Ausdruecke.
bool digits(const std::string& s, std::size_t from, std::size_t n) {
    if (from + n > s.size()) return false;
    for (std::size_t i = from; i < from + n; ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}
// ^([A-Z]{2}-[0-9]{6}-[0-9]|0x[0-9A-F]{8})$
bool valid_code_id(const std::string& s) {
    auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    if (s.size() == 11 && upper(s[0]) && upper(s[1]) && s[2] == '-' && digits(s, 3, 6) && s[9] == '-' && digits(s, 10, 1)) return true;
    if (s.size() == 10 && s[0] == '0' && s[1] == 'x') {
        for (std::size_t i = 2; i < 10; ++i)
            if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'F'))) return false;
        return true;
    }
    return false;
}
// ^[0-9]{4}-(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])$
bool valid_date(const std::string& s) {
    if (s.size() != 10 || !digits(s, 0, 4) || s[4] != '-' || !digits(s, 5, 2) || s[7] != '-' || !digits(s, 8, 2)) return false;
    const int month = (s[5] - '0') * 10 + (s[6] - '0'), day = (s[8] - '0') * 10 + (s[9] - '0');
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}
// ^[0-9a-f]{64}$
bool valid_sha256(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

using json = nlohmann::json;

[[noreturn]] void fail(const std::string& where, const std::string& msg) {
    throw std::runtime_error("error_codes: " + where + ": " + msg);
}

std::string need_string(const json& obj, const char* key, const std::string& where) {
    if (!obj.contains(key) || !obj[key].is_string() || obj[key].get<std::string>().empty())
        fail(where, std::string("'") + key + "' fehlt oder ist kein nicht-leerer String");
    return obj[key].get<std::string>();
}

// {"de": "...", "en": "..."}
void need_text_pair(const json& obj, const char* key, const std::string& where, std::string& de, std::string& en) {
    if (!obj.contains(key) || !obj[key].is_object()) fail(where, std::string("'") + key + "' fehlt oder ist kein Objekt");
    const json& o = obj[key];
    const std::string w = where + "." + key;
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.key() != "de" && it.key() != "en") fail(w, "unbekannter Schluessel '" + it.key() + "'");
    de = need_string(o, "de", w);
    en = need_string(o, "en", w);
}

// {"de": ["..."], "en": ["..."]}: beide Sprachen gleich lang, jeder Eintrag ein nicht-leerer String
void need_list_pair(const json& obj, const char* key, const std::string& where, std::vector<std::string>& de, std::vector<std::string>& en) {
    if (!obj.contains(key) || !obj[key].is_object()) fail(where, std::string("'") + key + "' fehlt oder ist kein Objekt");
    const json& o = obj[key];
    const std::string w = where + "." + key;
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.key() != "de" && it.key() != "en") fail(w, "unbekannter Schluessel '" + it.key() + "'");
    for (const char* lang : {"de", "en"}) {
        if (!o.contains(lang) || !o[lang].is_array()) fail(w, std::string("'") + lang + "' fehlt oder ist kein Array");
        auto& out = std::string(lang) == "de" ? de : en;
        for (std::size_t i = 0; i < o[lang].size(); ++i) {
            const json& e = o[lang][i];
            if (!e.is_string() || e.get<std::string>().empty()) fail(w, std::string(lang) + "[" + std::to_string(i) + "] ist kein nicht-leerer String");
            out.push_back(e.get<std::string>());
        }
    }
    if (de.size() != en.size()) fail(w, "Deutsch und Englisch haben nicht gleich viele Eintraege");
}

void need_https(const std::string& url, const std::string& where) {
    if (url.rfind("https://", 0) != 0 || url.size() < 12 || url.find(' ') != std::string::npos)
        fail(where, "Quelle muss eine https-Adresse ohne Leerzeichen sein");
}

const std::set<std::string> kEntryKeys = {"id", "title", "text", "steps", "notes", "context", "source", "more_sources", "source_sha256", "checked", "level"};
const std::set<std::string> kContextKinds = {"fw", "storage", "drives", "smp", "need_backport"};

ErrorCode parse_code(const json& j, std::size_t index) {
    std::string where = "codes[" + std::to_string(index) + "]";
    if (!j.is_object()) fail(where, "Eintrag muss ein Objekt sein");
    ErrorCode c;
    c.id = need_string(j, "id", where);
    if (!valid_code_id(c.id))
        fail(where, "ungueltige ID '" + c.id + "' (erwartet z. B. CE-108262-9 oder 0x809F8032)");
    where = "code '" + c.id + "'";
    for (auto it = j.begin(); it != j.end(); ++it)
        if (!kEntryKeys.count(it.key())) fail(where, "unbekannter Schluessel '" + it.key() + "'");

    need_text_pair(j, "title", where, c.title_de, c.title_en);
    need_text_pair(j, "text", where, c.text_de, c.text_en);
    need_list_pair(j, "steps", where, c.steps_de, c.steps_en);
    if (j.contains("notes")) need_list_pair(j, "notes", where, c.notes_de, c.notes_en);

    if (j.contains("context")) {
        if (!j["context"].is_array()) fail(where, "'context' muss ein Array sein");
        for (std::size_t i = 0; i < j["context"].size(); ++i) {
            const json& e = j["context"][i];
            if (!e.is_string() || !kContextKinds.count(e.get<std::string>())) fail(where, "context[" + std::to_string(i) + "] ist keine bekannte Angabe (fw|storage|drives|smp|need_backport)");
            c.context.push_back(e.get<std::string>());
        }
    }

    c.source = need_string(j, "source", where);
    need_https(c.source, where + ".source");
    if (j.contains("more_sources")) {
        if (!j["more_sources"].is_array()) fail(where, "'more_sources' muss ein Array sein");
        for (std::size_t i = 0; i < j["more_sources"].size(); ++i) {
            const json& e = j["more_sources"][i];
            if (!e.is_string()) fail(where, "more_sources[" + std::to_string(i) + "] ist kein String");
            need_https(e.get<std::string>(), where + ".more_sources[" + std::to_string(i) + "]");
            c.more_sources.push_back(e.get<std::string>());
        }
    }

    c.checked = need_string(j, "checked", where);
    if (!valid_date(c.checked)) fail(where, "checked muss ein Datum YYYY-MM-DD sein");
    if (j.contains("source_sha256")) {
        c.source_sha256 = need_string(j, "source_sha256", where);
        if (!valid_sha256(c.source_sha256)) fail(where, "source_sha256 muss 64 Hex-Ziffern (klein) sein");
    }

    c.level = need_string(j, "level", where);
    if (c.level != "doc" && c.level != "community") fail(where, "level muss doc|community sein");
    // Eine Sony-Seite (doc) nennt immer Schritte; ohne belegte Schritte ist der Eintrag nur Community-Wissen.
    if (c.level == "doc" && c.steps_de.empty()) fail(where, "level doc braucht mindestens einen Schritt");
    return c;
}

}  // namespace

const ErrorCode* ErrorCodeSet::find(const std::string& id) const {
    for (const ErrorCode& c : codes)
        if (c.id == id) return &c;
    return nullptr;
}

ErrorCodeSet load_error_codes_from_string(const std::string& text) {
    json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) fail("Datei", "kein gueltiges JSON");
    if (!j.is_object()) fail("Datei", "Wurzel muss ein Objekt sein");
    if (!j.contains("schema_version") || j["schema_version"] != 1) fail("Datei", "schema_version muss 1 sein");
    require_no_system_update_advice(j, "error_codes");  // nie ein Systemupdate nennen (docs/RULES.md, Textregel)
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& k = it.key();
        if (k != "schema_version" && k != "db_version" && k != "codes" && (k.empty() || k[0] != '_')) fail("Datei", "unbekannter Schluessel '" + k + "'");
    }
    ErrorCodeSet set;
    set.db_version = need_string(j, "db_version", "Datei");
    if (!j.contains("codes") || !j["codes"].is_array()) fail("Datei", "'codes' fehlt oder ist kein Array");
    std::set<std::string> seen;
    for (std::size_t i = 0; i < j["codes"].size(); ++i) {
        ErrorCode c = parse_code(j["codes"][i], i);
        if (!seen.insert(c.id).second) fail("code '" + c.id + "'", "doppelte ID");
        set.codes.push_back(std::move(c));
    }
    return set;
}

ErrorCodeSet load_error_codes_from_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("error_codes: kann '" + path + "' nicht oeffnen");
    std::ostringstream ss;
    ss << in.rdbuf();
    return load_error_codes_from_string(ss.str());
}

}  // namespace medic
