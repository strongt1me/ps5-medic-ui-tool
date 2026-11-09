// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/error_codes.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace medic {

namespace {

using json = nlohmann::ordered_json;  // Reihenfolge der Datei bleibt erhalten (Familien-Reiter in der UI)

[[noreturn]] void fail(const std::string& msg) { throw std::runtime_error("error_codes: " + msg); }

// Nur Buchstaben und Ziffern, gross geschrieben: "ce-108255-1" -> "CE1082551".
std::string squash(const std::string& s) {
    std::string out;
    for (unsigned char ch : s)
        if (std::isalnum(ch)) out += static_cast<char>(std::toupper(ch));
    return out;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alpha(char c) { return c >= 'A' && c <= 'Z'; }
bool is_hex(char c) { return is_digit(c) || (c >= 'A' && c <= 'F'); }

std::string req_string(const json& j, const char* key, const std::string& where) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string() || it->get<std::string>().empty()) fail(where + ": '" + key + "' fehlt oder ist leer");
    return it->get<std::string>();
}

// {"de": "...", "en": "..."} mit nicht leeren Texten
void req_text(const json& j, const char* key, const std::string& where, std::string& de, std::string& en) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_object()) fail(where + ": '" + key + "' fehlt (erwartet {de, en})");
    de = req_string(*it, "de", where + "." + key);
    en = req_string(*it, "en", where + "." + key);
}

std::vector<std::string> string_list(const json& j, const std::string& where) {
    if (!j.is_array()) fail(where + " muss eine Liste sein");
    std::vector<std::string> v;
    for (const auto& e : j) {
        if (!e.is_string() || e.get<std::string>().empty()) fail(where + " enthaelt einen leeren oder keinen Text");
        v.push_back(e.get<std::string>());
    }
    return v;
}

}  // namespace

const ErrorKind* ErrorDb::kind(const std::string& id) const {
    for (const auto& k : kinds)
        if (k.id == id) return &k;
    return nullptr;
}

const ErrorPrefix* ErrorDb::prefix(const std::string& id) const {
    for (const auto& p : prefixes)
        if (p.id == id) return &p;
    return nullptr;
}

std::string normalize_code(const std::string& input) {
    const std::string s = squash(input);
    if (s.size() < 3 || !is_alpha(s[0])) return {};
    const std::string head = s.substr(0, 2), body = s.substr(2);
    if (is_digit(head[1])) {  // E2-8223B400: acht Hex-Zeichen
        if (body.size() != 8 || !std::all_of(body.begin(), body.end(), is_hex)) return {};
        return head + "-" + body;
    }
    if (!is_alpha(head[1]) || body.size() != 7 || !std::all_of(body.begin(), body.end(), is_digit)) return {};
    return head + "-" + body.substr(0, 6) + "-" + body.substr(6);
}

std::string code_prefix(const std::string& input) {
    const std::string s = squash(input);
    if (s.size() < 2 || !is_alpha(s[0])) return {};
    return s.substr(0, 2);
}

const ErrorCode* find_code(const ErrorDb& db, const std::string& input) {
    const std::string c = normalize_code(input);
    if (c.empty()) return nullptr;
    for (const auto& e : db.codes)
        if (e.code == c) return &e;
    return nullptr;
}

std::vector<const ErrorCode*> search_codes(const ErrorDb& db, const std::string& input) {
    std::vector<const ErrorCode*> out;
    const std::string q = squash(input);
    if (q.size() < 3) return out;
    for (const auto& e : db.codes)
        if (squash(e.code).find(q) != std::string::npos) out.push_back(&e);
    return out;
}

std::string support_url(const ErrorDb& db, const std::string& code) {
    std::string url = db.url_template;
    const std::string token = "{code_lower}";
    const std::size_t pos = url.find(token);
    if (pos == std::string::npos) return {};
    std::string lower = code;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    url.replace(pos, token.size(), lower);
    return url;
}

std::vector<const ErrorCode*> codes_with_prefix(const ErrorDb& db, const std::string& prefix) {
    std::vector<const ErrorCode*> out;
    for (const auto& e : db.codes)
        if (code_prefix(e.code) == prefix) out.push_back(&e);
    return out;
}

ErrorDb load_error_db_from_string(const std::string& json_text) {
    json j;
    try {
        j = json::parse(json_text);
    } catch (const std::exception& e) {
        fail(std::string("kein gueltiges JSON: ") + e.what());
    }
    if (!j.is_object()) fail("Wurzel muss ein Objekt sein");

    ErrorDb db;
    db.db_version = req_string(j, "db_version", "Wurzel");
    db.source = req_string(j, "source", "Wurzel");
    db.url_template = j.value("url_template", std::string());
    if (!db.url_template.empty() && db.url_template.find("{code_lower}") == std::string::npos) fail("url_template braucht {code_lower}");

    if (!j.contains("prefixes") || !j["prefixes"].is_object() || j["prefixes"].empty()) fail("'prefixes' fehlt");
    for (const auto& [id, v] : j["prefixes"].items()) {
        if (id.size() != 2 || squash(id) != id) fail("Familie '" + id + "': zwei Zeichen, Grossbuchstaben und Ziffern");
        ErrorPrefix p;
        p.id = id;
        p.text_de = req_string(v, "de", "prefixes." + id);
        p.text_en = req_string(v, "en", "prefixes." + id);
        db.prefixes.push_back(p);
    }

    if (!j.contains("kinds") || !j["kinds"].is_object() || j["kinds"].empty()) fail("'kinds' fehlt");
    for (const auto& [id, v] : j["kinds"].items()) {
        const std::string where = "kinds." + id;
        ErrorKind k;
        k.id = id;
        req_text(v, "title", where, k.title_de, k.title_en);
        if (!v.contains("steps") || !v["steps"].is_object()) fail(where + ": 'steps' fehlt");
        k.steps_de = string_list(v["steps"].value("de", json::array()), where + ".steps.de");
        k.steps_en = string_list(v["steps"].value("en", json::array()), where + ".steps.en");
        if (k.steps_de.size() != k.steps_en.size()) fail(where + ": steps.de und steps.en haben verschieden viele Schritte");
        if (v.contains("homebrew")) {
            k.homebrew_de = v["homebrew"].value("de", std::string());
            k.homebrew_en = v["homebrew"].value("en", std::string());
            if (k.homebrew_de.empty() || k.homebrew_en.empty()) fail(where + ": homebrew braucht Deutsch und Englisch");
        }
        k.check = v.value("check", false);
        if (v.contains("related_rules")) k.related_rules = string_list(v["related_rules"], where + ".related_rules");
        db.kinds.push_back(std::move(k));
    }

    if (!j.contains("codes") || !j["codes"].is_array() || j["codes"].empty()) fail("'codes' fehlt");
    std::set<std::string> seen;
    for (const auto& v : j["codes"]) {
        ErrorCode c;
        c.code = req_string(v, "code", "codes");
        const std::string where = "codes[" + c.code + "]";
        if (normalize_code(c.code) != c.code) fail(where + ": nicht in kanonischer Form (z. B. CE-108255-1, Grossbuchstaben)");
        if (!seen.insert(c.code).second) fail(where + ": doppelt");
        c.kind = req_string(v, "kind", where);
        if (!db.kind(c.kind)) fail(where + ": unbekannte Art '" + c.kind + "'");
        if (!db.prefix(code_prefix(c.code))) fail(where + ": Familie '" + code_prefix(c.code) + "' fehlt in 'prefixes'");
        req_text(v, "text", where, c.text_de, c.text_en);
        db.codes.push_back(std::move(c));
    }
    return db;
}

ErrorDb load_error_db_from_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("kann '" + path + "' nicht oeffnen");
    std::ostringstream ss;
    ss << in.rdbuf();
    return load_error_db_from_string(ss.str());
}

}  // namespace medic
