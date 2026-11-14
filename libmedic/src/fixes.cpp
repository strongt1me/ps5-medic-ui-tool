// SPDX-License-Identifier: GPL-3.0-or-later
// Plan-Phase der Fix-Engine: erzeugt konkrete Aenderungen aus Befund + Fakten. Veraendert nichts.
#include "medic/casename.hpp"
#include "medic/collectors.hpp"
#include "medic/fixes.hpp"
#include "medic/ini.hpp"
#include "medic/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>

namespace medic {
namespace {

using json = nlohmann::json;

constexpr const char* kDefaultConfig = "/data/shadowmount/config.ini";

std::string join_path(const std::string& dir, const std::string& name) {
    return (!dir.empty() && dir.back() == '/') ? dir + name : dir + "/" + name;
}
std::string dir_of(const std::string& p) {
    auto pos = p.rfind('/');
    return pos == std::string::npos || pos == 0 ? "/" : p.substr(0, pos);
}
std::string base_of(const std::string& p) {
    auto pos = p.rfind('/');
    return pos == std::string::npos ? p : p.substr(pos + 1);
}

// Freier Zielname: der gewuenschte, sonst mit Zaehler (".2", ".3" ...). Ohne das scheiterte jede weitere Verschiebung in einen Quarantaeneordner,
// sobald dort schon eine Datei gleichen Namens liegt (der Fix liesse sich nie wieder ausfuehren). Leer, wenn alle Namen belegt sind.
std::string free_target(const IFileSystem& fs, const std::set<std::string>& taken, const std::string& to) {
    if (!fs.exists(to) && !taken.count(to)) return to;
    for (int n = 2; n < 100; ++n) {
        const std::string c = to + "." + std::to_string(n);
        if (!fs.exists(c) && !taken.count(c)) return c;
    }
    return {};
}

json array_fact(const Facts& f, const std::string& path) {  // Kopie: kleine Arrays, vermeidet Dangling-Referenzen
    const json* v = f.get(path);
    return v && v->is_array() ? *v : json::array();
}

std::string str_of(const json& j, const char* key) {
    return j.is_object() && j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

// ---- rename_image_parentheses -------------------------------------------

std::string strip_parentheses(const std::string& name) {
    auto dot = name.rfind('.');
    std::string base = dot == std::string::npos ? name : name.substr(0, dot);
    std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
    std::string out;
    for (char c : base) {
        if (c == '(' || c == ')') continue;
        if (c == ' ' && !out.empty() && out.back() == ' ') continue;  // doppelte Leerzeichen zusammenfassen
        out += c;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out + ext;
}

void plan_rename_parentheses(FixPlan& plan, const Facts& facts, const IFileSystem& fs) {
    const PathPolicy policy = policy_from_facts(facts);
    std::set<std::string> targets;
    for (const auto& img : array_fact(facts, "smp.images")) {
        const std::string ext = str_of(img, "ext"), name = str_of(img, "name"), path = str_of(img, "path");
        if ((ext != ".ffpfs" && ext != ".ffpfsc") || name.find_first_of("()") == std::string::npos) continue;
        if (std::string why; !policy.allowed(path, &why)) {  // wie bei den Quarantaene-Fixes: ein Eintrag ausserhalb der Whitelist laesst nicht den ganzen Plan scheitern
            plan.warnings.push_back("Übersprungen (außerhalb der erlaubten Ordner): " + path);
            continue;
        }
        std::string new_name = strip_parentheses(name);
        if (new_name.empty() || new_name == ext) {
            plan.warnings.push_back("Übersprungen (Name wäre leer): " + path);
            continue;
        }
        const std::string to = join_path(dir_of(path), new_name);
        if (fs.exists(to) || !targets.insert(to).second) {
            plan.warnings.push_back("Übersprungen (Ziel existiert bereits): " + to);
            continue;
        }
        Change c;
        c.kind = Change::Kind::Rename;
        c.path = path;
        c.to = to;
        c.note_de = "Klammern aus dem Dateinamen entfernen";
        c.note_en = "Remove parentheses from the file name";
        plan.changes.push_back(std::move(c));
    }
}

// ---- rename_case_prx ----------------------------------------------------
// Benennt Dateien, deren Name sich nur in Gross-/Kleinschreibung vom erwarteten unterscheidet, auf den erwarteten Namen um (games.case_mismatch[]).

void plan_rename_case(FixPlan& plan, const Facts& facts, const IFileSystem& fs) {
    const PathPolicy policy = policy_from_facts(facts);
    std::set<std::string> targets;
    for (const auto& m : array_fact(facts, "games.case_mismatch")) {
        // Auch auf exFAT/FAT (case_sensitive 0): dort aendert das Umbenennen nur den Namen im Ordner; der Befund ist ein Hinweis auf spaeter
        const std::string path = str_of(m, "path"), expected = str_of(m, "expected");
        if (path.empty() || expected.empty() || expected.find('/') != std::string::npos) continue;
        if (std::string why; !policy.allowed(path, &why)) {
            plan.warnings.push_back("Übersprungen (außerhalb der erlaubten Ordner): " + path);
            continue;
        }
        const std::string to = join_path(dir_of(path), expected);
        const bool exists_to = is_case_only_rename(path, to) ? dir_has_name(fs, to) : fs.exists(to);  // exFAT: exists() findet auch dieselbe Datei
        if (exists_to || !targets.insert(to).second) {
            plan.warnings.push_back("Übersprungen (Ziel existiert bereits): " + to);
            continue;
        }
        Change c;
        c.kind = Change::Kind::Rename;
        c.path = path;
        c.to = to;
        c.note_de = "Dateinamen auf die erwartete Groß-/Kleinschreibung bringen (" + base_of(path) + " -> " + expected + ")";
        c.note_en = "Rename the file to the expected letter case (" + base_of(path) + " -> " + expected + ")";
        plan.changes.push_back(std::move(c));
    }
    plan.hint_de = "Nur der Name wird geändert (gleicher Ordner); Rückgängig stellt den alten Namen wieder her.";
    plan.hint_en = "Only the name changes (same folder); undo restores the old name.";
}

// ---- smp_set_key --------------------------------------------------------

void plan_set_key(FixPlan& plan, const Finding& f, const Facts& facts, const IFileSystem& fs) {
    const json& p = f.fix_params;
    IniTarget t{str_of(p, "section"), str_of(p, "key"), str_of(p, "legacy_key")};
    if (t.key.empty()) {
        plan.warnings.push_back("smp_set_key: Parameter 'key' fehlt");
        return;
    }
    const std::string file = p.contains("file") && p["file"].is_string() ? p["file"].get<std::string>() : kDefaultConfig;
    const bool add_mode = str_of(p, "mode") == "add";

    // clamp_max: hoechster Wert, den SM+ in der v2-Datei annimmt (scan depth 1..2); ein groesserer Wert liesse SM+ die ganze Datei verwerfen
    const long clamp_max = p.contains("clamp_max") && p["clamp_max"].is_number_integer() ? p["clamp_max"].get<long>() : -1;
    long clamped_from = -1;
    std::vector<std::string> values;
    if (p.contains("value") && p["value"].is_string()) {
        values.push_back(p["value"].get<std::string>());
    } else if (p.contains("from") && p["from"].is_object()) {
        const json& from = p["from"];
        const std::string agg = str_of(from, "agg"), field = str_of(from, "field");
        long best = -1;
        std::set<std::string> seen;
        for (const auto& el : array_fact(facts, str_of(from, "array"))) {
            if (!el.is_object() || !el.contains(field)) continue;
            if (agg == "max" && el[field].is_number()) best = std::max(best, el[field].get<long>());
            if (agg == "distinct" && el[field].is_string() && seen.insert(el[field].get<std::string>()).second)
                values.push_back(el[field].get<std::string>());
        }
        if (agg == "max" && best >= 0) {
            if (clamp_max >= 0 && best > clamp_max) {
                clamped_from = best;
                best = clamp_max;
            }
            values.push_back(std::to_string(best));
        }
    }
    if (clamped_from >= 0) {
        plan.hint_de = "ShadowMount+ nimmt höchstens den Wert " + std::to_string(clamp_max) + "; Spiele auf Ebene " + std::to_string(clamped_from) +
                       " bleiben auch danach unauffindbar. Lege sie flacher an.";
        plan.hint_en = "ShadowMount+ accepts at most the value " + std::to_string(clamp_max) + "; games on level " + std::to_string(clamped_from) +
                       " are still not found afterwards. Move them closer to the scan path.";
    }
    if (values.empty() || (!add_mode && values.size() != 1)) {
        plan.warnings.push_back("smp_set_key: kein eindeutiger Wert aus den Fakten ableitbar");
        return;
    }

    std::string old_text;
    const bool existed = fs.read_file(file, old_text);
    if (!existed && fs.exists(file)) {  // vorhanden, aber nicht lesbar: nicht als "neu" behandeln
        plan.warnings.push_back("Datei nicht lesbar: " + file);
        return;
    }
    std::string text = old_text;
    for (const auto& v : values) text = add_mode ? ini_add_value(text, t, v) : ini_set_value(text, t, v);
    if (existed && text == old_text) {
        plan.warnings.push_back("Nichts zu tun: " + file + " enthält den Wert bereits");
        return;
    }
    Change c;
    c.kind = Change::Kind::WriteFile;
    c.path = file;
    c.content = text;
    c.check_base = true;
    c.base_existed = existed;
    c.base_sha = existed ? sha256_hex(old_text) : std::string();
    const std::string shown = (t.section.empty() ? "" : "[" + t.section + "] ") + t.key + "=" + values.front() + (values.size() > 1 ? " ..." : "");
    c.note_de = base_of(file) + " ändern: " + shown + " (vorher Backup)";
    c.note_en = "Change " + base_of(file) + ": " + shown + " (backup first)";
    plan.changes.push_back(std::move(c));
}

// ---- smp_translate_v1_keys ----------------------------------------------
// ShadowMount+ 1.7beta12 liest in einer v2-Datei nur bekannte Paare (Sektion, Schluessel); flache v1-Zeilen wie "scan_depth=2"
// (z. B. von einem anderen Werkzeug angehaengt) werden ignoriert. Fuer Namen mit verifizierter Entsprechung wird die Zeile
// entfernt und der Wert in die v2-Sektion geschrieben; alle anderen Zeilen bleiben unveraendert.

std::vector<std::string> split_keep_eol(const std::string& text) {
    std::vector<std::string> v;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::size_t end = nl == std::string::npos ? text.size() : nl + 1;
        v.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return v;
}

void plan_translate_v1(FixPlan& plan, const Facts& facts, const IFileSystem& fs) {
    std::string text;
    if (!fs.read_file(kDefaultConfig, text)) {
        plan.warnings.push_back("config.ini nicht lesbar");
        return;
    }
    struct Item {
        int line;
        std::string section, key, value;
        bool add;
    };
    std::vector<Item> items;
    int unmapped = 0, invalid = 0, stale = 0;
    const std::vector<std::string> cur_lines = split_keep_eol(text);
    for (const auto& k : array_fact(facts, "smp.v1_keys")) {
        if (!k.is_object() || !k.contains("line") || !k["line"].is_number_integer()) continue;
        if (str_of(k, "tkey").empty()) {
            ++unmapped;
            continue;
        }
        const int line_no = k["line"].get<int>();
        // Die Zeilennummer stammt aus dem letzten Check-up; hat jemand die Datei seither geaendert (Web-Interface, FTP), wuerde sonst die falsche Zeile entfernt
        bool same = false;
        if (line_no >= 1 && static_cast<std::size_t>(line_no) <= cur_lines.size()) {
            const std::string& l = cur_lines[static_cast<std::size_t>(line_no) - 1];
            const auto eq = l.find('=');
            if (eq != std::string::npos) {
                std::string key = l.substr(0, eq);
                while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();
                while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) key.erase(key.begin());
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return std::tolower(ch); });
                same = key == str_of(k, "key");
            }
        }
        if (!same) {
            ++stale;
            continue;
        }
        // Ein ungueltiger Wert in der v2-Sektion laesst SM+ die ganze Datei verwerfen; die flache Zeile wird in v2 dagegen nur ignoriert
        if (!smp_v2_value_valid(str_of(k, "tsection"), str_of(k, "tkey"), str_of(k, "value"))) {
            ++invalid;
            plan.warnings.push_back("Übersprungen (ungültiger Wert für v2): " + str_of(k, "key") + "=" + str_of(k, "value"));
            continue;
        }
        items.push_back({line_no, str_of(k, "tsection"), str_of(k, "tkey"), str_of(k, "value"), k.value("add", false)});
    }
    if (stale > 0) plan.warnings.push_back(std::to_string(stale) + " Zeile(n) haben sich seit dem Check-up geändert: bitte neu prüfen");
    if (items.empty()) {
        plan.warnings.push_back("Keine flache v1-Zeile mit bekannter v2-Entsprechung gefunden");
        return;
    }
    std::set<int> drop;
    for (const auto& it : items) drop.insert(it.line);
    std::string out;
    int n = 0;
    for (const auto& l : split_keep_eol(text)) {
        if (!drop.count(++n)) out += l;
    }
    for (const auto& it : items) {
        const IniTarget t{it.section, it.key, ""};
        out = it.add ? ini_add_value(out, t, it.value) : ini_set_value(out, t, it.value);
    }
    if (out == text) {
        plan.warnings.push_back("Nichts zu tun: config.ini enthält die Werte bereits");
        return;
    }
    Change c;
    c.kind = Change::Kind::WriteFile;
    c.path = kDefaultConfig;
    c.content = out;
    c.check_base = true;
    c.base_existed = true;
    c.base_sha = sha256_hex(text);
    c.note_de = "config.ini: " + std::to_string(items.size()) + " flache v1-Zeile(n) in die v2-Sektionen übertragen (vorher Backup)";
    c.note_en = "config.ini: move " + std::to_string(items.size()) + " flat v1 line(s) into their v2 sections (backup first)";
    plan.changes.push_back(std::move(c));
    if (unmapped + invalid > 0) {
        plan.hint_de = std::to_string(unmapped + invalid) + " weitere v1-Zeile(n) haben keine v2-Entsprechung oder einen ungültigen Wert und bleiben; setzbar im Web-Interface von ShadowMount+.";
        plan.hint_en = std::to_string(unmapped + invalid) + " more v1 line(s) have no v2 equivalent or an invalid value and stay; set them in the ShadowMount+ web interface.";
    }
}

// ---- autoload_insert_delay ----------------------------------------------
// Ein neu gestarteter Loader (elfldr) lauscht erst nach einigen Sekunden auf 9021; folgt der naechste Eintrag ohne Wartezeit,
// scheitert er ("Loader connection failed"). Die README des WebKit-Autoloaders verlangt "!4000" hinter elfldr.

void plan_autoload_delay(FixPlan& plan, const Facts& facts, const IFileSystem& fs) {
    std::set<std::string> files;
    for (const auto& p : array_fact(facts, "autoload.problems"))
        if (str_of(p, "kind") == "elfldr_no_delay") files.insert(str_of(p, "file"));
    for (const auto& file : files) {
        std::string text;
        if (file.empty() || !fs.read_file(file, text)) continue;
        // Nur der WebKit-Autoloader kennt "#"-Kommentare; beim Payload Manager waere "#..." ein Dateiname.
        const bool comments = file.find("ps5_autoloader") != std::string::npos;
        std::string out;
        bool prev_elfldr = false;
        int inserted = 0;
        for (const auto& raw : split_keep_eol(text)) {
            std::string l = raw;
            if (comments)
                if (const auto c = l.find('#'); c != std::string::npos) l.erase(c);
            while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.pop_back();
            while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.erase(l.begin());
            if (l.empty() || (comments && l[0] == '@')) {  // '@sync' kennt nur der WebKit-Autoloader
                out += raw;
                continue;
            }
            if (l[0] == '!') {
                prev_elfldr = false;
            } else {
                if (prev_elfldr) {
                    out += std::string("!4000") + (raw.size() > 1 && raw[raw.size() - 2] == '\r' ? "\r\n" : "\n");
                    ++inserted;
                }
                std::string low = l;
                std::transform(low.begin(), low.end(), low.begin(), [](unsigned char ch) { return std::tolower(ch); });
                prev_elfldr = low.find("elfldr") != std::string::npos;
            }
            out += raw;
        }
        if (inserted == 0) continue;
        if (!comments && out.size() > 4095) {  // Grenze des Payload Managers (collectors_stack.cpp): eine groessere Liste startet er gar nicht; der WebKit-Autoloader kennt sie nicht
            plan.warnings.push_back("Übersprungen (die Liste würde mit den Wartezeiten über 4095 Byte groß): " + file);
            continue;
        }
        Change c;
        c.kind = Change::Kind::WriteFile;
        c.path = file;
        c.content = out;
        c.check_base = true;
        c.base_existed = true;
        c.base_sha = sha256_hex(text);
        c.note_de = "Autoload-Liste: Wartezeit !4000 hinter elfldr einfügen (Original im Backup)";
        c.note_en = "Autoload list: insert the delay !4000 after elfldr (original kept in the backup)";
        plan.changes.push_back(std::move(c));
    }
    if (plan.changes.empty() && plan.warnings.empty()) plan.warnings.push_back("Keine Stelle gefunden, an der eine Wartezeit fehlt");
}

// ---- smp_disable_conflicting_backpork -----------------------------------

void plan_disable_backpork(FixPlan& plan, const Finding& f, const IFileSystem& fs) {
    const json& p = f.fix_params;
    std::string pattern = str_of(p, "match");
    if (pattern.empty()) pattern = "backpork";
    std::regex re;
    try {
        re = std::regex(pattern, std::regex::icase);
    } catch (const std::regex_error&) {
        plan.warnings.push_back("smp_disable_conflicting_backpork: ungültiges Muster");
        return;
    }
    plan.hint_de = "BackPork läuft bis zum nächsten Neustart weiter. Danach ist es nicht mehr geladen.";
    plan.hint_en = "BackPork keeps running until the next restart. Afterwards it is no longer loaded.";
    bool any_file = false;
    if (p.contains("files") && p["files"].is_array()) {
        for (const auto& fj : p["files"]) {
            if (!fj.is_string()) continue;
            std::string text;
            if (!fs.read_file(fj.get<std::string>(), text)) continue;
            any_file = true;
            std::string out;
            bool changed = false;
            std::size_t pos = 0;
            while (pos < text.size()) {
                std::size_t nl = text.find('\n', pos);
                std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                // Format der Autoload-Listen (ps5_autoloader, Payload Manager): "!<ms>" = Wartezeit, sonst ein Dateiname.
                // Kommentarzeilen gibt es dort nicht ("#..." waere ein Dateiname), daher wird der Eintrag entfernt;
                // das Original liegt im Backup und kommt mit "undo" zurueck.
                const bool is_delay = !line.empty() && line[0] == '!';
                const std::size_t line_end = nl == std::string::npos ? text.size() : nl + 1;
                // Nur der WebKit-Autoloader kennt "#"-Kommentare; eine schon auskommentierte Zeile ist nicht aktiv und bleibt stehen
                bool is_comment = false;
                std::string code = line;  // was vor einem '#' steht ("kstuff.elf # nicht backpork"): nur das ist der Eintrag
                if (fj.get<std::string>().find("ps5_autoloader") != std::string::npos) {
                    const auto first = line.find_first_not_of(" \t");
                    is_comment = first != std::string::npos && line[first] == '#';
                    if (const auto hash = code.find('#'); hash != std::string::npos) code.erase(hash);
                }
                if (!is_delay && !is_comment && std::regex_search(code, re)) {
                    changed = true;
                } else {
                    out += text.substr(pos, line_end - pos);
                }
                pos = line_end;
            }
            if (!changed) continue;
            Change c;
            c.kind = Change::Kind::WriteFile;
            c.path = fj.get<std::string>();
            c.content = out;
            c.check_base = true;
            c.base_existed = true;
            c.base_sha = sha256_hex(text);
            c.note_de = "BackPork-Eintrag aus der Autoload-Liste entfernen (Original im Backup)";
            c.note_en = "Remove the BackPork entry from the autoload list (original kept in the backup)";
            plan.changes.push_back(std::move(c));
        }
    }
    if (plan.changes.empty())
        plan.warnings.push_back(any_file ? "Kein aktiver BackPork-Eintrag in den Autoload-Listen gefunden"
                                         : "Keine Autoload-Liste gefunden (Pfad auf der Konsole noch zu verifizieren)");
}

// ---- Autoload-Listen: doppelte und fehlende Eintraege ---------------------------------------------------------------------
// Beide Listen (WebKit-Autoloader und Payload Manager) werden zeilenweise bearbeitet; was nicht betroffen ist, bleibt Byte fuer Byte
// erhalten (Zeilenenden, Kommentare, Wartezeiten). Das Original liegt im Backup, Rueckgaengig stellt es her.

// Eintrag einer Zeile (ohne Kommentar bei der WebKit-Liste; beim Payload Manager ist "#" KEIN Kommentar und "@" ein Eintrag). Leer bei Anweisungen und Wartezeiten.
std::string autoload_entry_of(const std::string& raw, bool comments) {
    std::string l = raw;
    if (comments)
        if (const auto c = l.find('#'); c != std::string::npos) l.erase(c);
    while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.pop_back();
    while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.erase(l.begin());
    // '@sync' kennt nur der WebKit-Autoloader; beim Payload Manager ist '@...' ein Eintrag
    if (l.empty() || l[0] == '!' || (comments && l[0] == '@')) return {};
    return l;
}

bool loader_list_has_comments(const std::string& file) { return file.find("ps5_autoloader") != std::string::npos; }

void push_list_write(FixPlan& plan, const std::string& file, const std::string& old_text, std::string content, const std::string& de, const std::string& en) {
    Change c;
    c.kind = Change::Kind::WriteFile;
    c.path = file;
    c.content = std::move(content);
    c.check_base = true;
    c.base_existed = true;
    c.base_sha = sha256_hex(old_text);
    c.note_de = de;
    c.note_en = en;
    plan.changes.push_back(std::move(c));
}

// autoload_remove_duplicates: behalten wird die erste Zeile eines Eintrags, spaetere Zeilen mit demselben Eintrag entfallen.
void plan_autoload_duplicates(FixPlan& plan, const Facts& facts, const IFileSystem& fs) {
    std::set<std::string> files;
    for (const auto& p : array_fact(facts, "autoload.problems"))
        if (str_of(p, "kind") == "duplicate") files.insert(str_of(p, "file"));
    for (const auto& file : files) {
        std::string text;
        if (file.empty() || !fs.read_file(file, text)) continue;
        const bool comments = loader_list_has_comments(file);
        std::set<std::string> seen;
        std::string out;
        int removed = 0;
        for (const auto& raw : split_keep_eol(text)) {
            const std::string entry = autoload_entry_of(raw, comments);
            if (!entry.empty() && !seen.insert(entry).second) {
                ++removed;
                continue;
            }
            out += raw;
        }
        if (removed == 0) continue;
        push_list_write(plan, file, text, out, "Autoload-Liste: " + std::to_string(removed) + " doppelte Zeile(n) entfernen (Original im Backup)",
                        "Autoload list: remove " + std::to_string(removed) + " duplicate line(s) (original kept in the backup)");
    }
    if (plan.changes.empty()) plan.warnings.push_back("Keine doppelte Zeile gefunden");
    plan.hint_de = "Je Eintrag bleibt die erste Zeile; Wartezeiten und alle anderen Zeilen bleiben unverändert.";
    plan.hint_en = "The first line of each entry stays; delays and all other lines stay unchanged.";
}

// autoload_fix_missing: Eintrag ohne Datei. Unterscheidet sich der Name nur in der Schreibweise, wird er auf den vorhandenen Dateinamen
// gebracht (Namen sind case-sensitiv); sonst wird die Zeile ausgeschaltet: in der WebKit-Liste mit vorangestelltem "# ", beim Payload Manager
// (dort gibt es keine Kommentare) entfaellt sie. Dateien werden nie angefasst.
void plan_autoload_missing(FixPlan& plan, const Facts& facts, const IFileSystem& fs) {
    std::map<std::string, std::map<std::string, std::string>> per_file;  // Datei -> Eintrag -> Art
    for (const auto& p : array_fact(facts, "autoload.problems")) {
        const std::string kind = str_of(p, "kind");
        if (kind == "missing_slot" || kind == "missing_file" || kind == "entry_case") per_file[str_of(p, "file")][str_of(p, "detail")] = kind;
    }
    for (const auto& [file, entries] : per_file) {
        std::string text;
        if (file.empty() || !fs.read_file(file, text)) continue;
        const bool comments = loader_list_has_comments(file);
        const std::string dir = dir_of(file);
        std::string out;
        int fixed = 0, off = 0;
        for (const auto& raw : split_keep_eol(text)) {
            const std::string entry = autoload_entry_of(raw, comments);
            const auto it = entries.find(entry);
            if (entry.empty() || it == entries.end()) {
                out += raw;
                continue;
            }
            if (it->second == "entry_case") {
                std::string actual;
                for (const auto& e : fs.list_dir(dir))
                    if (!e.is_dir && e.name.size() == entry.size() && std::equal(e.name.begin(), e.name.end(), entry.begin(), [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
                        actual = e.name;
                if (!actual.empty() && actual != entry) {
                    out += raw.substr(0, raw.find(entry)) + actual + raw.substr(raw.find(entry) + entry.size());
                    ++fixed;
                    continue;
                }
                out += raw;
                continue;
            }
            ++off;
            if (comments) out += "# " + raw;  // der ganze Eintrag bleibt lesbar und laesst sich wieder einschalten
        }
        if (fixed + off == 0) continue;
        std::string de = "Autoload-Liste: ", en = "Autoload list: ";
        if (fixed > 0) {
            de += std::to_string(fixed) + " Name(n) auf die vorhandene Schreibweise bringen";
            en += "correct the spelling of " + std::to_string(fixed) + " name(s)";
        }
        if (off > 0) {
            de += std::string(fixed > 0 ? ", " : "") + std::to_string(off) + " Eintrag/Eintraege ohne Datei " + (comments ? "auskommentieren" : "entfernen");
            en += std::string(fixed > 0 ? ", " : "") + (comments ? "comment out " : "remove ") + std::to_string(off) + " entry/entries without a file";
        }
        push_list_write(plan, file, text, out, de + " (Original im Backup)", en + " (original kept in the backup)");
    }
    if (plan.changes.empty()) plan.warnings.push_back("Kein Eintrag gefunden, der sich sicher korrigieren lässt");
    plan.hint_de = "Payload-Dateien werden nicht angefasst. Fehlt ein Payload wirklich, muss es von Hand wieder installiert werden.";
    plan.hint_en = "Payload files are not touched. If a payload is really missing, it has to be installed again by hand.";
}

// ---- quarantine_broken_elf ----------------------------------------------------------------------------------------------
// Eine unbrauchbare ELF-Datei (zu klein, keine ELF-Kennung, abgeschnitten) wird nicht geloescht, sondern in den versteckten Ordner
// .medic_broken neben sie verschoben. Versteckt, damit sie weder Loader noch Payload Manager noch die naechste Pruefung wieder findet.
// "unreadable" (Lesefehler) ist kein Beweis fuer eine kaputte Datei und bleibt unberuehrt. Nur Pfade der Whitelist.

void plan_quarantine_elf(FixPlan& plan, const Facts& facts, const IFileSystem& fs) {
    const PathPolicy policy = policy_from_facts(facts);
    std::set<std::string> targets;
    for (const auto& b : array_fact(facts, "payload.broken_elfs")) {
        const std::string path = str_of(b, "path"), reason = str_of(b, "reason");
        if (path.empty() || reason == "unreadable") continue;
        std::string why;
        if (!policy.allowed(path, &why)) {
            plan.warnings.push_back("Übersprungen (außerhalb der erlaubten Ordner): " + path);
            continue;
        }
        const std::string to = free_target(fs, targets, join_path(join_path(dir_of(path), ".medic_broken"), base_of(path)));
        if (to.empty()) {
            plan.warnings.push_back("Übersprungen (kein freier Zielname): " + path);
            continue;
        }
        targets.insert(to);
        Change c;
        c.kind = Change::Kind::Rename;
        c.path = path;
        c.to = to;
        c.note_de = "Defekte ELF-Datei (" + reason + ") in den Ordner .medic_broken verschieben: " + base_of(path);
        c.note_en = "Move the broken ELF file (" + reason + ") into the folder .medic_broken: " + base_of(path);
        plan.changes.push_back(std::move(c));
    }
    if (plan.changes.empty()) plan.warnings.push_back("Keine defekte ELF-Datei in den erlaubten Ordnern gefunden");
    plan.hint_de = "Die Datei wird nicht gelöscht, sondern nach .medic_broken daneben verschoben (Rückgängig holt sie zurück). Danach das Payload neu übertragen (FTP im Binärmodus).";
    plan.hint_en = "The file is not deleted but moved to .medic_broken next to it (undo brings it back). Then transfer the payload again (FTP in binary mode).";
}

// ---- quarantine_empty_image ---------------------------------------------------------------------------------------------
// Ein Image (.exfat, .ffpfs, .ffpfsc) unter 512 Byte kann kein Dateisystem enthalten und sich nicht einhaengen lassen. Es wird nicht geloescht,
// sondern in den versteckten Ordner .medic_broken daneben verschoben (ShadowMount+ ueberspringt Namen mit Punkt am Anfang, wie bei den Doppelten).
// Eine Datei, die gerade noch uebertragen wird, ist auch klein: die Vorschau sagt es, die Bestaetigung bleibt beim Nutzer.

void plan_quarantine_image(FixPlan& plan, const Facts& facts, const IFileSystem& fs) {
    const PathPolicy policy = policy_from_facts(facts);
    std::set<std::string> targets;
    for (const auto& img : array_fact(facts, "smp.images")) {
        if (img.value("tiny", 0) != 1) continue;
        const std::string path = str_of(img, "path");
        if (path.empty()) continue;
        std::string why;
        if (!policy.allowed(path, &why)) {
            plan.warnings.push_back("Übersprungen (außerhalb der erlaubten Ordner): " + path);
            continue;
        }
        const std::string to = free_target(fs, targets, join_path(join_path(dir_of(path), ".medic_broken"), base_of(path)));
        if (to.empty()) {
            plan.warnings.push_back("Übersprungen (kein freier Zielname): " + path);
            continue;
        }
        targets.insert(to);
        Change c;
        c.kind = Change::Kind::Rename;
        c.path = path;
        c.to = to;
        c.note_de = "Leeres oder abgebrochenes Image in den Ordner .medic_broken verschieben: " + base_of(path);
        c.note_en = "Move the empty or interrupted image into the folder .medic_broken: " + base_of(path);
        plan.changes.push_back(std::move(c));
    }
    if (plan.changes.empty()) plan.warnings.push_back("Kein leeres Image in den erlaubten Ordnern gefunden");
    plan.hint_de = "Die Datei wird nicht gelöscht, sondern nach .medic_broken daneben verschoben (Rückgängig holt sie zurück). Läuft gerade eine Übertragung, erst beenden lassen.";
    plan.hint_en = "The file is not deleted but moved to .medic_broken next to it (undo brings it back). If a transfer is running, let it finish first.";
}

// ---- pldmgr_enable_autoload ---------------------------------------------------------------------------------------------------
void plan_pldmgr_enable(FixPlan& plan, const Finding& finding, const Facts& facts, const IFileSystem& fs) {
    Finding f = finding;
    f.fix_params = {{"file", "/data/pldmgr/pldmgr_config.txt"}, {"section", ""}, {"key", "AUTOLOAD_ENABLED"}, {"value", "1"}};
    plan_set_key(plan, f, facts, fs);
    plan.hint_de = "Nur die Zeile AUTOLOAD_ENABLED in der Einstellungsdatei des Payload Managers wird auf 1 gesetzt; Rückgängig stellt die Datei wieder her.";
    plan.hint_en = "Only the line AUTOLOAD_ENABLED in the settings file of the Payload Manager is set to 1; undo restores the file.";
}

// ---- move_duplicate_titleid ---------------------------------------------

void plan_move_duplicates(FixPlan& plan, const Facts& facts, const IFileSystem& fs, const FixOptions& opt) {
    struct Src {
        std::string path, root;
        long level;
        bool active;  // die Quelle, die ShadowMount+ gerade nutzt (Link-Datei unter /user/app/<TITLE_ID>/)
    };
    std::map<std::string, std::vector<Src>> groups;
    for (const auto& t : array_fact(facts, "smp.titles"))
        if (t.value("dup_count", 0) > 1)
            groups[str_of(t, "titleid")].push_back({str_of(t, "path"), str_of(t, "root"), t.value("level", 0L), t.value("active", false)});

    const PathPolicy policy = policy_from_facts(facts);
    std::set<std::string> targets;
    for (auto& [id, srcs] : groups) {
        // Behalten wird standardmaessig die Quelle, die SM+ schon nutzt (sonst wuerde der Fix das laufende Spiel wegnehmen), danach die flachste.
        std::sort(srcs.begin(), srcs.end(), [](const Src& a, const Src& b) {
            if (a.active != b.active) return a.active;
            return a.level != b.level ? a.level < b.level : a.path < b.path;
        });
        std::size_t keep = 0;
        if (!opt.keep.empty()) {
            auto it = std::find_if(srcs.begin(), srcs.end(), [&](const Src& s) { return s.path == opt.keep; });
            if (it != srcs.end()) keep = static_cast<std::size_t>(it - srcs.begin());
        }
        for (std::size_t i = 0; i < srcs.size(); ++i) {
            if (i == keep) continue;
            const Src& s = srcs[i];
            if (std::string why; !policy.allowed(s.path, &why)) {
                plan.warnings.push_back("Übersprungen (außerhalb der erlaubten Ordner): " + s.path);
                continue;
            }
            const std::string to = free_target(fs, targets, join_path(join_path(s.root.empty() ? dir_of(s.path) : s.root, ".duplicates"), base_of(s.path)));
            if (to.empty()) {
                plan.warnings.push_back("Übersprungen (kein freier Zielname): " + s.path);
                continue;
            }
            targets.insert(to);
            Change c;
            c.kind = Change::Kind::Rename;
            c.path = s.path;
            c.to = to;
            c.note_de = id + ": Doppelte Quelle verschieben (behalten wird " + srcs[keep].path + ")";
            c.note_en = id + ": move duplicate source (keeping " + srcs[keep].path + ")";
            plan.changes.push_back(std::move(c));
        }
    }
    // ShadowMount+ ueberspringt Ordner mit '.' am Anfang (sm_scan_tree.c), ".duplicates" bleibt also auch bei scan_depth 2 ungescannt;
    // ein "_duplicates" wuerde dort mitgescannt und die Doppelten kaemen zurueck.
    plan.hint_de = "Die Doppelten kommen nach .duplicates im Scanpfad (ShadowMount+ scannt Ordner mit Punkt nicht); Rückgängig holt sie zurück.";
    plan.hint_en = "The duplicates go to .duplicates in the scan path (ShadowMount+ skips folders starting with a dot); undo moves them back.";
}

}  // namespace

bool file_entry_size(const IFileSystem& fs, const std::string& path, std::uint64_t& size) {
    const auto slash = path.rfind('/');
    if (slash == std::string::npos || slash + 1 >= path.size()) return false;
    const std::string dir = slash == 0 ? "/" : path.substr(0, slash), name = path.substr(slash + 1);
    for (const auto& e : fs.list_dir(dir))
        if (e.name == name) {
            if (e.is_dir) return false;
            size = e.size;
            return true;
        }
    return false;
}

FixPlan plan_fix(const Finding& finding, const Facts& facts, const IFileSystem& fs, const FixOptions& opt) {
    FixPlan plan;
    plan.fix_id = finding.fix;
    plan.rule_id = finding.rule_id;
    if (finding.fix.empty()) {
        plan.warnings.push_back("Dieser Befund hat keinen automatischen Fix");
    } else if (finding.fix == "rename_image_parentheses") {
        plan_rename_parentheses(plan, facts, fs);
    } else if (finding.fix == "rename_case_prx") {
        plan_rename_case(plan, facts, fs);
    } else if (finding.fix == "smp_set_key") {
        plan_set_key(plan, finding, facts, fs);
    } else if (finding.fix == "smp_translate_v1_keys") {
        plan_translate_v1(plan, facts, fs);
    } else if (finding.fix == "autoload_insert_delay") {
        plan_autoload_delay(plan, facts, fs);
    } else if (finding.fix == "autoload_remove_duplicates") {
        plan_autoload_duplicates(plan, facts, fs);
    } else if (finding.fix == "autoload_fix_missing") {
        plan_autoload_missing(plan, facts, fs);
    } else if (finding.fix == "quarantine_broken_elf") {
        plan_quarantine_elf(plan, facts, fs);
    } else if (finding.fix == "quarantine_empty_image") {
        plan_quarantine_image(plan, facts, fs);
    } else if (finding.fix == "pldmgr_enable_autoload") {
        plan_pldmgr_enable(plan, finding, facts, fs);
    } else if (finding.fix == "smp_disable_conflicting_backpork") {
        plan_disable_backpork(plan, finding, fs);
    } else if (finding.fix == "move_duplicate_titleid") {
        plan_move_duplicates(plan, facts, fs, opt);
    } else if (finding.fix == "fix_image_nesting_hint") {
        plan.hint_de = "Images werden nicht verändert. Die Spieldateien müssen direkt im Image-Root liegen (sce_sys/param.json im Root, kein Zusatzordner): Image neu bauen.";
        plan.hint_en = "Images are not modified. Game files must sit directly in the image root (sce_sys/param.json at the root, no extra folder): rebuild the image.";
    } else {
        plan.warnings.push_back("Unbekannter Fix: " + finding.fix);
    }
    for (Change& c : plan.changes)  // Dateien, die beiseitegelegt werden: Groesse merken (siehe Change::check_size)
        if (c.kind == Change::Kind::Rename && file_entry_size(fs, c.path, c.base_size)) c.check_size = true;
    return plan;
}

}  // namespace medic
