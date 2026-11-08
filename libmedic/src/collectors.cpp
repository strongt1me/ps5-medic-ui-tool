#include "medic/collectors.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace medic {
namespace {

using json = nlohmann::json;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string join_path(const std::string& dir, const std::string& name) {
    return (!dir.empty() && dir.back() == '/') ? dir + name : dir + "/" + name;
}

std::string extension_of(const std::string& name) {
    auto pos = name.rfind('.');
    return pos == std::string::npos || pos == 0 ? std::string() : lower(name.substr(pos));
}

bool starts_with_dir(const std::string& path, const std::string& dir) {
    return path == dir || (path.size() > dir.size() && path.compare(0, dir.size(), dir) == 0 && path[dir.size()] == '/');
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(std::move(line));
    }
    return out;
}

// ---- ShadowMount+ -------------------------------------------------------

constexpr const char* kSmpDir = "/data/shadowmount";

// Standard-Scanpfade laut SM+ README (Reihenfolge: spezifischere Pfade zuerst).
// Eigene Pfade (scanpath / [scan] path) ersetzen diese Liste komplett.
std::vector<std::string> default_scan_paths() {
    std::vector<std::string> v = {"/data/homebrew", "/data/etaHEN/games"};
    std::vector<std::string> mounts = {"/mnt/ext0", "/mnt/ext1"};
    for (int i = 0; i < 8; ++i) mounts.push_back("/mnt/usb" + std::to_string(i));
    for (auto& m : mounts) {
        v.push_back(m + "/homebrew");
        v.push_back(m + "/etaHEN/games");
    }
    for (auto& m : mounts) v.push_back(m);
    return v;
}

const std::set<std::string> kImageExts = {".ffpfs", ".ffpfsc", ".exfat", ".dat"};

constexpr int kMaxScanLevel = 4;       // wie tief wir zur Faktensammlung suchen (Ebene 1 = direkt im Scanpfad)
constexpr std::size_t kMaxLogLines = 2000;
constexpr std::size_t kMaxLogBytes = 4u << 20;

struct Config {
    json values = json::object();          // [section] key=value -> values[section][key]; ohne Sektion: values[key]
    std::vector<std::string> scanpaths;    // "scanpath=" und "[scan] path="
};

std::string safe_key(std::string k) {
    std::replace(k.begin(), k.end(), '.', '_');  // Punkte wuerden Fakt-Pfade zerschneiden
    return k;
}

Config parse_ini(const std::string& text) {
    Config c;
    std::string section;
    for (auto line : split_lines(text)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            auto end = line.find(']');
            section = end == std::string::npos ? std::string() : safe_key(lower(trim(line.substr(1, end - 1))));
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = safe_key(lower(trim(line.substr(0, eq))));
        std::string val = trim(line.substr(eq + 1));
        if (key == "scanpath" || (section == "scan" && key == "path")) {
            if (!val.empty()) c.scanpaths.push_back(val);
            continue;
        }
        if (section.empty()) c.values[key] = val;
        else {
            if (!c.values.contains(section) || !c.values[section].is_object()) c.values[section] = json::object();
            c.values[section][key] = val;
        }
    }
    return c;
}

// Wert aus Sektion ([section] key) oder aelterem flachem Schluessel.
const json* cfg_value(const Config& c, const char* section, const char* key, const char* legacy_flat) {
    if (c.values.contains(section) && c.values[section].is_object() && c.values[section].contains(key))
        return &c.values[section][key];
    if (legacy_flat && c.values.contains(legacy_flat) && c.values[legacy_flat].is_string()) return &c.values[legacy_flat];
    return nullptr;
}

// "1/true/yes/on" -> 1, "0/false/no/off" -> 0, sonst Default.
int parse_flag(const json* v, int def) {
    if (!v || !v->is_string()) return def;
    std::string s = lower(v->get<std::string>());
    if (s == "1" || s == "true" || s == "yes" || s == "on") return 1;
    if (s == "0" || s == "false" || s == "no" || s == "off") return 0;
    return def;
}

int parse_int(const json* v, int def) {
    if (!v || !v->is_string()) return def;
    const std::string s = v->get<std::string>();
    if (s.empty() || !std::all_of(s.begin(), s.end(), [](unsigned char ch) { return std::isdigit(ch); })) return def;
    return std::min(std::stoi(s.substr(0, 6)), 99);
}

std::string title_id_from_name(const std::string& name) {
    static const std::regex re("PPSA[0-9]{5}");
    std::smatch m;
    return std::regex_search(name, m, re) ? m.str() : std::string();
}

std::string title_id_from_param(const IFileSystem& fs, const std::string& game_dir) {
    std::string text;
    if (!fs.read_file(join_path(game_dir, "sce_sys/param.json"), text)) return {};
    auto j = json::parse(text, nullptr, false);
    if (!j.is_object()) return {};
    for (const char* key : {"titleId", "title_id", "TITLE_ID"})
        if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return {};
}

// Cluster-Groesse aus dem exFAT-Boot-Sektor (Offset 3: "EXFAT   ", 108: BytesPerSectorShift, 109: SectorsPerClusterShift).
// 0 = kein exFAT-Header erkennbar.
std::uint64_t exfat_cluster_size(const IFileSystem& fs, const std::string& path) {
    std::string b;
    if (!fs.read_range(path, 0, 512, b) || b.size() < 110) return 0;
    if (b.compare(3, 8, "EXFAT   ") != 0) return 0;
    unsigned bps = static_cast<unsigned char>(b[108]), spc = static_cast<unsigned char>(b[109]);
    if (bps < 9 || bps > 12 || spc > 25 || bps + spc > 25) return 0;  // ausserhalb der exFAT-Spezifikation
    return std::uint64_t{1} << (bps + spc);
}

struct Entry {
    std::string path, name, ext, titleid, root;  // root = Scanpfad, unter dem der Eintrag gefunden wurde
    bool is_image = false;
    std::uint64_t size = 0;
    int level = 0;  // 1 = direkt im Scanpfad
};

// Sammelt Eintraege je Pfad (kleinste Ebene gewinnt, weil sich Scanpfade ueberlappen: /mnt/usb0 und /mnt/usb0/homebrew).
// Eigene Quarantaene-Ordner der Fix-Engine (_duplicates, _to_delete) werden nicht als Quelle gezaehlt.
void scan_dir(const IFileSystem& fs, const std::string& root, const std::string& dir, int level, std::map<std::string, Entry>& out) {
    for (const auto& e : fs.list_dir(dir)) {
        // "backports" ist laut SM+-README ein Overlay-Ordner (<scanpath>/backports/<TITLE_ID>/) und kein Spiel.
        if (e.is_dir && (e.name == "_duplicates" || e.name == "_to_delete" || e.name == "backports")) continue;
        const std::string path = join_path(dir, e.name);
        Entry en;
        en.root = root;
        en.path = path;
        en.name = e.name;
        en.level = level;
        if (e.is_dir) {
            if (fs.exists(join_path(path, "sce_sys/param.json"))) {  // Spielordner: nicht weiter hinein
                en.titleid = title_id_from_param(fs, path);
                if (en.titleid.empty()) en.titleid = title_id_from_name(e.name);
            } else {
                if (level < kMaxScanLevel) scan_dir(fs, root, path, level + 1, out);
                continue;
            }
        } else {
            en.ext = extension_of(e.name);
            if (!kImageExts.count(en.ext)) continue;
            en.is_image = true;
            en.size = e.size;
            en.titleid = title_id_from_name(e.name);
        }
        auto it = out.find(path);
        if (it == out.end() || it->second.level > level) out[path] = std::move(en);
    }
}

std::map<std::string, Entry> scan_roots(const IFileSystem& fs, const std::vector<std::string>& roots) {
    std::map<std::string, Entry> out;
    for (auto& r : roots)
        if (fs.is_dir(r)) scan_dir(fs, r, r, 1, out);
    return out;
}

void collect_log_lines(const IFileSystem& fs, const std::string& path, Facts& f, const std::string& fact) {
    std::string text;
    json arr = json::array();
    if (fs.read_file(path, text)) {
        if (text.size() > kMaxLogBytes) text.erase(0, text.size() - kMaxLogBytes);
        auto lines = split_lines(text);
        std::size_t from = lines.size() > kMaxLogLines ? lines.size() - kMaxLogLines : 0;
        for (std::size_t i = from; i < lines.size(); ++i)
            if (!trim(lines[i]).empty()) arr.push_back(lines[i]);
    }
    f.set(fact, std::move(arr));
}

// Gemountete Images unter /mnt/shadowmnt (Layout vorlaeufig, auf der Konsole in Phase 2 zu verifizieren):
//   /mnt/shadowmnt/<name>/            gemountetes Spiel-Image
//   /mnt/shadowmnt/pfsc/<name>/       gemounteter .ffpfsc-Container (soll ein inneres Image enthalten)
void collect_mounts(const IFileSystem& fs, Facts& f) {
    json mounts = json::array();
    const std::string base = "/mnt/shadowmnt";
    for (const auto& e : fs.list_dir(base)) {
        if (!e.is_dir) continue;
        const std::string path = join_path(base, e.name);
        if (e.name == "pfsc") {
            for (const auto& c : fs.list_dir(path)) {
                if (!c.is_dir) continue;
                const std::string cp = join_path(path, c.name);
                bool direct = fs.exists(join_path(cp, "sce_sys")) || fs.exists(join_path(cp, "eboot.bin"));
                bool nested = false;
                for (const auto& x : fs.list_dir(cp))
                    if (!x.is_dir && (kImageExts.count(extension_of(x.name)))) nested = true;
                mounts.push_back({{"kind", "pfsc"}, {"name", c.name}, {"path", cp}, {"direct_files", direct}, {"nested_image", nested}});
            }
            continue;
        }
        bool root_param = fs.exists(join_path(path, "sce_sys/param.json"));
        std::string extra;
        if (!root_param)
            for (const auto& x : fs.list_dir(path))
                if (x.is_dir && fs.exists(join_path(join_path(path, x.name), "sce_sys/param.json"))) {
                    extra = x.name;
                    break;
                }
        mounts.push_back({{"kind", "image"}, {"name", e.name}, {"path", path}, {"root_param", root_param},
                          {"extra_folder", !extra.empty()}, {"extra_folder_name", extra}});
    }
    f.set("smp.mounts", std::move(mounts));
}

}  // namespace

void collect_firmware(const Platform& p, Facts& f) {
    f.set("fw.version", p.sys().firmware());
    f.set("fw.model", p.sys().model());
}

void collect_processes(const Platform& p, Facts& f) {
    json arr = json::array();
    for (auto& n : p.procs().running()) arr.push_back(lower(n));
    f.set("proc.running", std::move(arr));
}

void collect_storage(const Platform& p, Facts& f) {
    json arr = json::array();
    for (auto& s : p.sys().storage()) {
        double pct = s.total_bytes ? 100.0 * static_cast<double>(s.free_bytes) / static_cast<double>(s.total_bytes) : 0.0;
        arr.push_back({{"mount", s.mount}, {"total", s.total_bytes}, {"free", s.free_bytes}, {"free_pct", pct}});
    }
    f.set("storage.mounts", std::move(arr));
}

// kstuff.version, kstuff.origin, smp.version, etahen.version ... (Quelle: ISystemInfo::components)
void collect_components(const Platform& p, Facts& f) {
    for (auto& [name, props] : p.sys().components())
        for (auto& [k, v] : props) f.set(safe_key(name) + "." + safe_key(k), v);
}

void collect_shadowmount(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    const std::string cfg_path = join_path(kSmpDir, "config.ini");

    std::string cfg_text;
    f.set("smp.config_present", fs.read_file(cfg_path, cfg_text));
    Config cfg = parse_ini(cfg_text);
    f.set("smp.config", cfg.values);
    f.set("smp.scanpaths_custom", cfg.scanpaths);

    // Wirksame Einstellungen (Defaults laut SM+ README: scan depth 1, debug an).
    // fakelib: README kennt [fakelib] enabled; aelterer Schluessel backport_fakelib. Default "an" laut RULES.md (B5).
    // recursive_scan=1 erzwingt laut README scan_depth=2.
    int depth = parse_int(cfg_value(cfg, "scan", "depth", "scan_depth"), 1);
    if (parse_flag(cfg_value(cfg, "scan", "recursive", "recursive_scan"), 0) == 1) depth = std::max(depth, 2);
    f.set("smp.scan_depth", depth);
    f.set("smp.debug", parse_flag(cfg_value(cfg, "general", "debug", "debug"), 1));
    f.set("smp.fakelib_enabled", parse_flag(cfg_value(cfg, "fakelib", "enabled", "backport_fakelib"), 1));

    const bool custom = !cfg.scanpaths.empty();
    const std::vector<std::string> roots = custom ? cfg.scanpaths : default_scan_paths();
    f.set("smp.scan_roots", roots);
    auto entries = scan_roots(fs, roots);

    json images = json::array(), deep = json::array();
    std::map<std::string, int> count;
    for (auto& [path, e] : entries)
        if (e.level <= depth && !e.titleid.empty()) ++count[e.titleid];

    json titles = json::array();
    for (auto& [path, e] : entries) {
        if (e.is_image) {
            json img = {{"name", e.name}, {"ext", e.ext}, {"path", e.path}, {"size", e.size}, {"level", e.level}};
            if (e.ext == ".exfat")
                if (auto cs = exfat_cluster_size(fs, e.path)) img["cluster_size"] = cs;
            images.push_back(std::move(img));
        }
        if (e.level > depth) {  // von SM+ bei dieser Tiefe nicht gescannt
            deep.push_back({{"path", e.path}, {"level", e.level}, {"titleid", e.titleid}});
            continue;
        }
        if (!e.titleid.empty()) titles.push_back({{"titleid", e.titleid}, {"path", e.path}, {"root", e.root}, {"level", e.level}, {"dup_count", count[e.titleid]}});
    }
    f.set("smp.images", std::move(images));
    f.set("smp.titles", std::move(titles));
    f.set("smp.deep_sources", std::move(deep));

    // B9: Spiele in Standardpfaden, die wegen eigener Scanpfade nicht mehr gescannt werden.
    json hidden = json::array();
    if (custom)
        for (auto& [path, e] : scan_roots(fs, default_scan_paths())) {
            if (e.level > depth || e.titleid.empty()) continue;
            bool covered = std::any_of(cfg.scanpaths.begin(), cfg.scanpaths.end(), [&](auto& r) { return starts_with_dir(path, r); });
            if (!covered) hidden.push_back({{"path", e.path}, {"root", e.root}, {"titleid", e.titleid}});
        }
    f.set("smp.hidden_sources", std::move(hidden));

    collect_log_lines(fs, join_path(kSmpDir, "debug.log"), f, "smp.log.lines");
    collect_log_lines(fs, join_path(kSmpDir, "autotune.ini"), f, "smp.autotune.lines");
    collect_mounts(fs, f);
}

Facts collect_all(const Platform& p) {
    Facts f;
    collect_firmware(p, f);
    collect_components(p, f);
    collect_processes(p, f);
    collect_storage(p, f);
    collect_shadowmount(p, f);
    return f;
}

}  // namespace medic
