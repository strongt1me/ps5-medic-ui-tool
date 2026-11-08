// Plan-Phase der Fix-Engine: erzeugt konkrete Aenderungen aus Befund + Fakten. Veraendert nichts.
#include "medic/fixes.hpp"
#include "medic/ini.hpp"

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
    std::set<std::string> targets;
    for (const auto& img : array_fact(facts, "smp.images")) {
        const std::string ext = str_of(img, "ext"), name = str_of(img, "name"), path = str_of(img, "path");
        if ((ext != ".ffpfs" && ext != ".ffpfsc") || name.find_first_of("()") == std::string::npos) continue;
        std::string new_name = strip_parentheses(name);
        if (new_name.empty() || new_name == ext) {
            plan.warnings.push_back("Uebersprungen (Name waere leer): " + path);
            continue;
        }
        const std::string to = join_path(dir_of(path), new_name);
        if (fs.exists(to) || !targets.insert(to).second) {
            plan.warnings.push_back("Uebersprungen (Ziel existiert bereits): " + to);
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
        if (agg == "max" && best >= 0) values.push_back(std::to_string(best));
    }
    if (values.empty() || (!add_mode && values.size() != 1)) {
        plan.warnings.push_back("smp_set_key: kein eindeutiger Wert aus den Fakten ableitbar");
        return;
    }

    std::string old_text;
    const bool existed = fs.read_file(file, old_text);
    std::string text = old_text;
    for (const auto& v : values) text = add_mode ? ini_add_value(text, t, v) : ini_set_value(text, t, v);
    if (existed && text == old_text) {
        plan.warnings.push_back("Nichts zu tun: " + file + " enthaelt den Wert bereits");
        return;
    }
    Change c;
    c.kind = Change::Kind::WriteFile;
    c.path = file;
    c.content = text;
    const std::string shown = (t.section.empty() ? "" : "[" + t.section + "] ") + t.key + "=" + values.front() + (values.size() > 1 ? " ..." : "");
    c.note_de = "config.ini aendern: " + shown + " (vorher Backup)";
    c.note_en = "Change config.ini: " + shown + " (backup first)";
    plan.changes.push_back(std::move(c));
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
        plan.warnings.push_back("smp_disable_conflicting_backpork: ungueltiges Muster");
        return;
    }
    plan.hint_de = "BackPork laeuft bis zum naechsten Neustart weiter. Danach ist es nicht mehr geladen.";
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
                if (!is_delay && std::regex_search(line, re)) {
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
            c.note_de = "BackPork-Eintrag aus der Autoload-Liste entfernen (Original im Backup)";
            c.note_en = "Remove the BackPork entry from the autoload list (original kept in the backup)";
            plan.changes.push_back(std::move(c));
        }
    }
    if (plan.changes.empty())
        plan.warnings.push_back(any_file ? "Kein aktiver BackPork-Eintrag in den Autoload-Listen gefunden"
                                         : "Keine Autoload-Liste gefunden (Pfad auf der Konsole noch zu verifizieren)");
}

// ---- move_duplicate_titleid ---------------------------------------------

void plan_move_duplicates(FixPlan& plan, const Facts& facts, const IFileSystem& fs, const FixOptions& opt) {
    struct Src {
        std::string path, root;
        long level;
    };
    std::map<std::string, std::vector<Src>> groups;
    for (const auto& t : array_fact(facts, "smp.titles"))
        if (t.value("dup_count", 0) > 1)
            groups[str_of(t, "titleid")].push_back({str_of(t, "path"), str_of(t, "root"), t.value("level", 0L)});

    std::set<std::string> targets;
    for (auto& [id, srcs] : groups) {
        std::sort(srcs.begin(), srcs.end(), [](const Src& a, const Src& b) {
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
            const std::string to = join_path(join_path(s.root.empty() ? dir_of(s.path) : s.root, "_duplicates"), base_of(s.path));
            if (fs.exists(to) || !targets.insert(to).second) {
                plan.warnings.push_back("Uebersprungen (Ziel existiert bereits): " + to);
                continue;
            }
            Change c;
            c.kind = Change::Kind::Rename;
            c.path = s.path;
            c.to = to;
            c.note_de = id + ": Doppelte Quelle verschieben (behalten wird " + srcs[keep].path + ")";
            c.note_en = id + ": move duplicate source (keeping " + srcs[keep].path + ")";
            plan.changes.push_back(std::move(c));
        }
    }
    plan.hint_de = "Bei scan_depth >= 2 kann ShadowMount+ den Ordner _duplicates mitscannen; ihn dann von Hand aus dem Scanpfad nehmen.";
    plan.hint_en = "With scan_depth >= 2 ShadowMount+ may scan the _duplicates folder; move it out of the scan path by hand then.";
}

}  // namespace

FixPlan plan_fix(const Finding& finding, const Facts& facts, const IFileSystem& fs, const FixOptions& opt) {
    FixPlan plan;
    plan.fix_id = finding.fix;
    plan.rule_id = finding.rule_id;
    if (finding.fix.empty()) {
        plan.warnings.push_back("Dieser Befund hat keinen automatischen Fix");
    } else if (finding.fix == "rename_image_parentheses") {
        plan_rename_parentheses(plan, facts, fs);
    } else if (finding.fix == "smp_set_key") {
        plan_set_key(plan, finding, facts, fs);
    } else if (finding.fix == "smp_disable_conflicting_backpork") {
        plan_disable_backpork(plan, finding, fs);
    } else if (finding.fix == "move_duplicate_titleid") {
        plan_move_duplicates(plan, facts, fs, opt);
    } else if (finding.fix == "fix_image_nesting_hint") {
        plan.hint_de =
            "Images werden nicht veraendert. Spieldateien muessen direkt im Image-Root liegen (sce_sys/param.json im Root, kein Zusatzordner); "
            "ein .ffpfsc ist nur ein Container fuer ein inneres Image (.exfat oder pfs_image.dat). Image neu bauen.";
        plan.hint_en =
            "Images are not modified. Game files must sit directly in the image root (sce_sys/param.json at the root, no extra folder); "
            "a .ffpfsc is only a container for an inner image (.exfat or pfs_image.dat). Rebuild the image.";
    } else {
        plan.warnings.push_back("Unbekannter Fix: " + finding.fix);
    }
    return plan;
}

}  // namespace medic
