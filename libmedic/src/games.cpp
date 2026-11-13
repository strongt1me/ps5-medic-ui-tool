// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/games.hpp"

#include <algorithm>
#include <cstring>
#include <map>
#include <regex>

namespace medic {

namespace {

using json = nlohmann::json;

std::string str_of(const json& j, const char* k) { return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }

const json* arr_of(const Facts& f, const char* path) {
    const json* a = f.get(path);
    return a && a->is_array() ? a : nullptr;
}

int rank_of(Severity s) { return s == Severity::Critical ? 0 : (s == Severity::Warning ? 1 : 2); }

bool contains_word(const std::string& hay, const std::string& needle) { return !needle.empty() && hay.find(needle) != std::string::npos; }

}  // namespace

std::vector<GameInfo> collect_game_infos(const Facts& facts, const std::vector<Finding>& findings) {
    std::map<std::string, GameInfo> games;
    auto at = [&](const std::string& id) -> GameInfo* {
        if (id.empty()) return nullptr;
        auto it = games.find(id);
        if (it == games.end()) {
            if (games.size() >= kMaxGames) return nullptr;
            it = games.emplace(id, GameInfo{}).first;
            it->second.titleid = id;
        }
        return &it->second;
    };

    const bool have_fw = facts.get("games.fw") != nullptr;
    const bool have_db = facts.get("appdb.entries") != nullptr;
    const std::string console_fw = facts.get("fw.version") && facts.get("fw.version")->is_string() ? facts.get("fw.version")->get<std::string>() : std::string();

    if (const json* a = arr_of(facts, "games.fw"))
        for (const auto& e : *a) {
            GameInfo* g = at(str_of(e, "titleid"));
            if (!g) continue;
            g->installed = 1;
            g->name = str_of(e, "name") != g->titleid ? str_of(e, "name") : std::string();
            g->required_fw = str_of(e, "required_fw");
            g->sdk_fw = str_of(e, "sdk_fw");
            if (!console_fw.empty() && e.contains("backport") && e["backport"].is_boolean()) g->backport = e["backport"].get<bool>() ? 1 : 0;
            if (e.contains("backport_inline") && e["backport_inline"].is_boolean()) g->backport_inline = e["backport_inline"].get<bool>();
        }
    if (const json* a = arr_of(facts, "games.need_backport"))
        for (const auto& e : *a)
            if (GameInfo* g = at(str_of(e, "titleid"))) g->needs_backport = true;
    if (const json* a = arr_of(facts, "apps.oversized"))
        for (const auto& e : *a)
            if (GameInfo* g = at(str_of(e, "titleid"))) {
                g->oversized = true;
                if (g->name.empty() && str_of(e, "name") != g->titleid) g->name = str_of(e, "name");
            }
    if (const json* a = arr_of(facts, "smp.titles"))
        for (const auto& e : *a) {
            GameInfo* g = at(str_of(e, "titleid"));
            if (!g) continue;
            if (g->name.empty() && !str_of(e, "name").empty() && str_of(e, "name") != g->titleid) g->name = str_of(e, "name");  // Namen aus dem Quellordner, wenn der Titel nicht eingehaengt ist
            GameSource s;
            s.path = str_of(e, "path");
            s.root = str_of(e, "root");
            s.kind = "folder";  // Images werden unten ueber smp.images erkannt
            s.active = e.value("active", false);
            s.dup_count = e.value("dup_count", 1);
            g->sources.push_back(std::move(s));
        }
    // Ein Image erkennt man am Eintrag in smp.images (Pfad gleich); das ist genauer als die Endung.
    if (const json* imgs = arr_of(facts, "smp.images"))
        for (auto& [id, g] : games)
            for (auto& s : g.sources) {
                bool is_img = false;
                for (const auto& im : *imgs)
                    if (str_of(im, "path") == s.path) is_img = true;
                s.kind = is_img ? "image" : "folder";
            }
    if (have_db) {
        if (const json* a = arr_of(facts, "appdb.entries"))
            for (const auto& e : *a)
                if (GameInfo* g = at(str_of(e, "titleid"))) g->registered = 1;
        if (const json* a = arr_of(facts, "appdb.orphans"))
            for (const auto& e : *a)
                if (GameInfo* g = at(str_of(e, "titleid"))) {
                    g->registered = 1;
                    g->orphan = true;
                }
        if (const json* a = arr_of(facts, "appdb.unregistered"))
            for (const auto& e : *a)
                if (GameInfo* g = at(str_of(e, "titleid"))) {
                    g->installed = 1;
                    g->registered = 0;
                }
        // Titel mit Ordner unter /user/app (games.fw), aber ohne Eintrag: nur sagen, wenn die Datenbank gelesen wurde
        if (have_fw)
            for (auto& [id, g] : games)
                if (g.installed == 1 && g.registered < 0) g.registered = 0;
    }
    if (const json* a = arr_of(facts, "games.eboot_problems"))
        for (const auto& e : *a)
            if (GameInfo* g = at(str_of(e, "titleid"))) g->eboot_bad = true;
    if (const json* a = arr_of(facts, "games.case_mismatch"))
        for (const auto& e : *a)
            if (GameInfo* g = at(str_of(e, "titleid"))) g->case_mismatch = true;
    if (const json* a = arr_of(facts, "errors.pairs"))
        for (const auto& e : *a) {
            GameInfo* g = at(str_of(e, "titleid"));
            if (!g) continue;
            GameError ge;
            ge.code = str_of(e, "code");
            ge.count = e.value("count", 0);
            ge.last = str_of(e, "last");
            g->errors.push_back(std::move(ge));
        }
    if (const json* a = arr_of(facts, "savedata.foreign"))
        for (const auto& e : *a)
            if (GameInfo* g = at(str_of(e, "titleid"))) ++g->foreign_saves;

    // Befunde, die einen Titel nennen (Titel-ID in der Evidenz oder Pfad einer seiner Quellen)
    static const std::regex id_re("PPSA[0-9]{5}");
    for (const auto& f : findings)
        for (const auto& ev : f.evidence)
            for (auto it = std::sregex_iterator(ev.begin(), ev.end(), id_re); it != std::sregex_iterator(); ++it) at(it->str());
    for (std::size_t i = 0; i < findings.size(); ++i) {
        const Finding& f = findings[i];
        for (auto& [id, g] : games) {
            bool hit = false;
            for (const auto& ev : f.evidence) {
                if (contains_word(ev, id)) hit = true;
                for (const auto& s : g.sources)
                    if (contains_word(ev, s.path)) hit = true;
                if (hit) break;
            }
            if (!hit) continue;
            if (g.findings.empty() || rank_of(f.severity) < rank_of(g.worst)) g.worst = f.severity;
            g.findings.push_back(i);
        }
    }

    // SM+-Log: letzte Zeilen, die die TITLE_ID nennen
    if (const json* lines = arr_of(facts, "smp.log.lines"))
        for (auto& [id, g] : games) {
            std::vector<std::string> hits;
            for (const auto& l : *lines)
                if (l.is_string() && l.get<std::string>().find(id) != std::string::npos) hits.push_back(l.get<std::string>());
            if (hits.size() > kGameLogLines) hits.erase(hits.begin(), hits.end() - static_cast<long>(kGameLogLines));
            g.log_lines = std::move(hits);
        }

    // Namen aus den Metadaten der Konsole (titles.names: /user/appmeta, PS4-param.sfo) fuer alle Titel, die noch keinen haben
    if (const json* names = facts.get("titles.names"); names && names->is_object())
        for (auto& [id, g] : games)
            if (g.name.empty() && names->contains(id) && (*names)[id].is_string()) g.name = (*names)[id].get<std::string>();

    // ™ und ® stehen nicht in der eingebauten Schrift (es erschiene ein "?"): aus den Namen entfernen
    for (auto& [id, g] : games)
        for (const char* mark : {"\xE2\x84\xA2", "\xC2\xAE"})
            for (std::size_t p; (p = g.name.find(mark)) != std::string::npos;) g.name.erase(p, std::strlen(mark));
    for (auto& [id, g] : games)
        while (!g.name.empty() && g.name.back() == ' ') g.name.pop_back();

    std::vector<GameInfo> out;
    out.reserve(games.size());
    for (auto& [id, g] : games) out.push_back(std::move(g));
    std::stable_sort(out.begin(), out.end(), [](const GameInfo& a, const GameInfo& b) {
        const int ra = a.findings.empty() ? 3 : rank_of(a.worst), rb = b.findings.empty() ? 3 : rank_of(b.worst);
        if (ra != rb) return ra < rb;
        return a.titleid < b.titleid;
    });
    return out;
}

}  // namespace medic
