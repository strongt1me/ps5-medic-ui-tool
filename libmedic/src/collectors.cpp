// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/collectors.hpp"
#include "medic/image_reader.hpp"
#include "medic/history.hpp"
#include "medic/selftest.hpp"

#include "medic/rules.hpp"
#include "medic/sqlite_check.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>

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

// Namen klein geschrieben und ohne ".elf": geladene Payloads erscheinen auf der Konsole als "kstuff.elf",
// "shadowmountplus.elf" (erster Konsolenlauf), die Regeln vergleichen den Namen ohne Endung.
std::string normalize_proc_name(const std::string& n) {
    std::string name = lower(n);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".elf") == 0) name.resize(name.size() - 4);
    return name;
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

struct IniLine {
    int line = 0;  // 1-basiert
    std::string section, key, value;
};

struct Config {
    json values = json::object();          // [section] key=value -> values[section][key]; ohne Sektion: values[key]
    std::vector<std::string> scanpaths;    // "scanpath=" (nur v1) und "[scan] path="
    bool v2 = false;                       // Sektion [config] vorhanden: Format v2 (1.7beta12)
    std::vector<IniLine> lines;            // alle Schluessel-Zeilen in Dateireihenfolge (fuer die Pruefungen unten)
    std::vector<int> unreadable_lines;     // Zeilen, die weder Kommentar, Sektion noch "schluessel = wert" sind
};

std::string safe_key(std::string k) {
    std::replace(k.begin(), k.end(), '.', '_');  // Punkte wuerden Fakt-Pfade zerschneiden
    return k;
}

// Wert hinter dem '=' so lesen wie ShadowMount+ (src/sm_config_file.c): '#' und ';' leiten einen Kommentar ein (v1 und v2);
// ab Konfigurationsformat v2 (Sektion [config], 1.7beta12) kann ein Wert in Anfuehrungszeichen stehen, dann zaehlen '#' und ';'
// darin nicht, und ein Backslash maskiert das folgende Zeichen (Backslash, Anfuehrungszeichen).
std::string decode_ini_value(const std::string& raw, bool v2) {
    std::string v = trim(raw);
    if (v2 && !v.empty() && v[0] == '"') {
        std::string out;
        for (std::size_t i = 1; i < v.size(); ++i) {
            if (v[i] == '\\' && i + 1 < v.size()) {
                out += v[++i];
                continue;
            }
            if (v[i] == '"') break;
            out += v[i];
        }
        return out;
    }
    if (const auto c = v.find_first_of("#;"); c != std::string::npos) v.erase(c);
    return trim(v);
}

Config parse_ini(const std::string& text) {
    Config c;
    std::string section;
    bool v2 = false;  // wie SM+: eine Sektion [config] macht die Datei zu Format v2
    for (const auto& l : split_lines(text)) {
        const std::string t = lower(trim(l));
        if (t.rfind("[config]", 0) == 0) v2 = true;
    }
    c.v2 = v2;
    int line_no = 0;
    for (auto line : split_lines(text)) {
        ++line_no;
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            auto end = line.find(']');
            section = end == std::string::npos ? std::string() : safe_key(lower(trim(line.substr(1, end - 1))));
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) {
            c.unreadable_lines.push_back(line_no);
            continue;
        }
        std::string key = safe_key(lower(trim(line.substr(0, eq))));
        std::string val = decode_ini_value(line.substr(eq + 1), v2);
        c.lines.push_back({line_no, section, key, val});
        // Eine v2-Datei kennt "scanpath=" nicht mehr (SM+ ignoriert die Zeile, Antwort aus dem Quelltext 1.7beta12).
        if ((key == "scanpath" && !v2) || (section == "scan" && key == "path")) {
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

// Wert aus Sektion ([section] key) oder aelterem flachem Schluessel. In einer v2-Datei gilt der flache Schluessel nicht:
// SM+ 1.7beta12 liest nur bekannte Paare (Sektion, Schluessel) und ignoriert den Rest ("[CFG] unknown setting").
const json* cfg_value(const Config& c, const char* section, const char* key, const char* legacy_flat) {
    if (c.values.contains(section) && c.values[section].is_object() && c.values[section].contains(key))
        return &c.values[section][key];
    if (!c.v2 && legacy_flat && c.values.contains(legacy_flat) && c.values[legacy_flat].is_string()) return &c.values[legacy_flat];
    return nullptr;
}

// ---- Pruefungen einer v2-Datei nach den Regeln von ShadowMount+ 1.7beta12 -------------------------------------------
// (sm_config_mount.c: ein einziger ungueltiger Wert verwirft die ganze Konfiguration; beim Start beendet sich der Payload,
//  beim Reload bleibt der alte Stand. Unter v1/beta11 wurden ungueltige Werte nur uebersprungen.)

struct V1Map {
    const char* v1;       // flacher v1-Schluessel
    const char* section;  // v2-Sektion; nullptr = keine verifizierte Entsprechung
    const char* key;
    bool add;             // mehrere Zeilen (scanpath -> [scan] path)
};

// Entsprechungen, die SM+ bei der Umwandlung der Test-Konsole selbst geschrieben hat (config-v1-backup/config.ini gegen die
// neue config.ini, 9. Nov. 2026), dazu scanpath -> [scan] path laut Dokumentation. Alle anderen v1-Namen sind bekannt, aber
// ohne verifizierte Entsprechung: der Befund nennt sie, der Fix fasst sie nicht an.
const V1Map kV1Keys[] = {
    {"scan_depth", "scan", "depth", false},
    {"scanpath", "scan", "path", true},
    {"api_bind_address", "api", "bind_address", false},
    {"fan_target_temperature", "fan", "target_temperature", false},
    {"auto_remove_missing_games", "library", "remove_missing", false},
    {"auto_remove_missing_delay_seconds", "library", "remove_delay_seconds", false},
    {"update_emulators", "emulators", "update_enabled", false},
    {"fakelib_default_mode", "fakelib", "default_mode", false},
    {"recursive_scan", nullptr, nullptr, false},
    {"scan_interval_seconds", nullptr, nullptr, false},
    {"stability_wait_seconds", nullptr, nullptr, false},
    {"api_enabled", nullptr, nullptr, false},
    {"api_port", nullptr, nullptr, false},
    {"mount_read_only", nullptr, nullptr, false},
    {"force_mount", nullptr, nullptr, false},
    {"persistent_image_mounts", nullptr, nullptr, false},
    {"auto_remove_games_with_dlc", nullptr, nullptr, false},
    {"backport_fakelib", nullptr, nullptr, false},
    {"fakelib_mode", nullptr, nullptr, false},
    {"fakelib_exclude", nullptr, nullptr, false},
    {"emulators_path", nullptr, nullptr, false},
    {"image_ro", nullptr, nullptr, false},
    {"image_rw", nullptr, nullptr, false},
    {"image_sector", nullptr, nullptr, false},
};

const V1Map* find_v1(const std::string& key) {
    for (const auto& m : kV1Keys)
        if (key == m.v1) return &m;
    return nullptr;
}

bool is_flag_value(const std::string& v) {
    const std::string s = lower(v);
    return s == "1" || s == "0" || s == "true" || s == "false" || s == "yes" || s == "no" || s == "on" || s == "off";
}

bool is_int_in(const std::string& v, long lo, long hi) {
    if (v.empty() || v.size() > 9 || !std::all_of(v.begin(), v.end(), [](unsigned char ch) { return std::isdigit(ch); })) return false;
    const long n = std::stol(v);
    return n >= lo && n <= hi;
}

// Liefert eine kurze Beschreibung ("[scan] depth = 3"), wenn SM+ die Datei wegen dieser Zeile ablehnen wuerde.
std::string invalid_v2_value(const IniLine& l) {
    struct Check {
        const char* section;
        const char* key;
        int kind;  // 0 = Wahrheitswert, 1 = Zahl lo..hi, 2 = "system" oder 50..91
        long lo, hi;
    };
    static const Check kChecks[] = {
        {"general", "debug", 0, 0, 0},          {"general", "quiet_mode", 0, 0, 0},
        {"emulators", "update_enabled", 0, 0, 0}, {"emulators", "auto_update_ampr", 0, 0, 0},
        {"library", "remove_missing", 0, 0, 0},  {"library", "remove_delay_seconds", 1, 1, 86400},
        {"scan", "depth", 1, 1, 2},              {"scan", "interval_seconds", 1, 1, 3600},
        {"scan", "stability_wait_seconds", 1, 0, 3600}, {"fan", "target_temperature", 2, 50, 91},
    };
    for (const auto& c : kChecks) {
        if (l.section != c.section || l.key != c.key) continue;
        const bool ok = c.kind == 0 ? is_flag_value(l.value) : c.kind == 1 ? is_int_in(l.value, c.lo, c.hi) : (lower(l.value) == "system" || is_int_in(l.value, c.lo, c.hi));
        if (!ok) return "[" + l.section + "] " + l.key + " = " + l.value;
    }
    return {};
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

std::string app_title_name(const json& param, const std::string& fallback);

// Anzeigename aus der param.json eines Quellordners (leer, wenn nicht lesbar oder ohne Namen).
std::string title_name_from_param(const IFileSystem& fs, const std::string& game_dir) {
    std::string text;
    if (!fs.read_file(join_path(game_dir, "sce_sys/param.json"), text)) return {};
    auto j = json::parse(text, nullptr, false);
    return j.is_object() ? app_title_name(j, std::string()) : std::string();
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
// Zusaetzlich die Laenge des Dateisystems (VolumeLength, Offset 72, in Sektoren; Microsoft exFAT-Spezifikation, Abschnitt Main Boot Sector):
// volume_bytes = VolumeLength << BytesPerSectorShift, 0 = Feld leer oder unplausibel (dann keine Laengenpruefung).
struct ExfatInfo {
    std::uint64_t cluster = 0, volume_bytes = 0;
};

ExfatInfo exfat_info(const IFileSystem& fs, const std::string& path) {
    ExfatInfo r;
    std::string b;
    if (!fs.read_range(path, 0, 512, b) || b.size() < 110) return r;
    if (b.compare(3, 8, "EXFAT   ") != 0) return r;
    unsigned bps = static_cast<unsigned char>(b[108]), spc = static_cast<unsigned char>(b[109]);
    if (bps < 9 || bps > 12 || spc > 25 || bps + spc > 25) return r;  // ausserhalb der exFAT-Spezifikation
    r.cluster = std::uint64_t{1} << (bps + spc);
    std::uint64_t vol = 0;
    for (int i = 7; i >= 0; --i) vol = (vol << 8) | static_cast<unsigned char>(b[72 + static_cast<std::size_t>(i)]);
    if (vol >= 1 && vol <= (std::uint64_t{1} << 40)) r.volume_bytes = vol << bps;
    return r;
}

struct Entry {
    std::string path, name, ext, titleid, root;  // root = Scanpfad, unter dem der Eintrag gefunden wurde
    std::string title_name;                      // Name laut param.json des Spielordners (Images: leer)
    bool is_image = false;
    bool active = false;  // diese Quelle nutzt SM+ gerade (Link-Datei unter /user/app/<TITLE_ID>/)
    std::uint64_t size = 0;
    int level = 0;  // 1 = direkt im Scanpfad
};

// Sammelt Eintraege je Pfad (kleinste Ebene gewinnt, weil sich Scanpfade ueberlappen: /mnt/usb0 und /mnt/usb0/homebrew).
// Wie der Scanner von SM+ (sm_scan_tree.c): Namen mit '.' am Anfang werden auf jeder Ebene uebersprungen, "_..." nicht.
// Die Quarantaene der Fix-Engine heisst deshalb ".duplicates" (ein "_duplicates" wuerde bei Tiefe 2 mitgescannt).
void scan_dir(const IFileSystem& fs, const std::string& root, const std::string& dir, int level, std::map<std::string, Entry>& out) {
    for (const auto& e : fs.list_dir(dir)) {
        if (!e.name.empty() && e.name[0] == '.') continue;
        // "backports" ist laut SM+-README ein Overlay-Ordner (<scanpath>/backports/<TITLE_ID>/) und kein Spiel.
        // "sce_sys" ist nie eine Quelle: in Ordnern ohne param.json (z. B. PS4-Dumps) enthaelt es .dat-Dateien (npbind.dat),
        // die sonst als Images in grosser Tiefe gezaehlt wuerden (erster Konsolenlauf: falscher B10-Befund).
        if (e.is_dir && (e.name == "backports" || e.name == "sce_sys")) continue;
        const std::string path = join_path(dir, e.name);
        Entry en;
        en.root = root;
        en.path = path;
        en.name = e.name;
        en.level = level;
        if (e.is_dir) {
            if (fs.exists(join_path(path, "sce_sys/param.json"))) {  // Spielordner: nicht weiter hinein
                en.titleid = title_id_from_param(fs, path);
                en.title_name = title_name_from_param(fs, path);
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
    } else if (fs.exists(path)) {
        return;  // vorhanden, aber nicht lesbar: der Fakt fehlt ("nicht geprueft"), statt als leeres Log "keine Fehler" zu behaupten
    }
    f.set(fact, std::move(arr));
}

// "source not stable yet: <Name> (<Pfad>)" (oder nur "<Pfad>"): je Quelle zaehlen. Jeder normale Kopiervorgang erzeugt ein bis
// zwei solche Zeilen, bevor ShadowMount+ die Quelle installiert (echtes Log: zwei Zeilen, dann "Installing:"). Nur dieselbe
// Quelle mehrfach hintereinander deutet auf ein Problem hin; Zeilen verschiedener Quellen zaehlen nicht zusammen.
void collect_unstable_sources(Facts& f) {
    static const std::string kMarker = "source not stable yet:";
    std::vector<std::string> keys;
    std::vector<int> counts;
    if (const json* lines = f.get("smp.log.lines"); lines && lines->is_array())
        for (const auto& l : *lines) {
            if (!l.is_string()) continue;
            const std::string line = l.get<std::string>();
            const auto pos = line.find(kMarker);
            if (pos == std::string::npos) continue;
            const std::string rest = trim(line.substr(pos + kMarker.size()));
            std::string key = rest;
            if (!rest.empty() && rest.back() == ')') {
                const auto open = rest.rfind(" (/");
                if (open != std::string::npos) key = rest.substr(open + 2, rest.size() - open - 3);
            }
            const auto it = std::find(keys.begin(), keys.end(), key);
            if (it == keys.end()) {
                keys.push_back(key);
                counts.push_back(1);
            } else {
                ++counts[static_cast<std::size_t>(it - keys.begin())];
            }
        }
    json out = json::array();
    for (std::size_t i = 0; i < keys.size(); ++i) out.push_back({{"path", keys[i]}, {"count", counts[i]}});
    f.set("smp.unstable_sources", std::move(out));
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
    for (auto& n : p.procs().running()) arr.push_back(normalize_proc_name(n));
    f.set("proc.running", std::move(arr));
}

// net.port_9021 ("open"/"closed", fehlt wenn unbekannt), jb.seconds_after_boot, jb.first_process.
// "Jailbreak-Start" = der frueheste Prozess der Jailbreak-Kette (Loader, etaHEN, kstuff, ShadowMount+); die Startzeit
// kommt aus der Prozessliste, der Konsolenstart aus der Plattform. Ohne beides fehlt der Fakt (Regel feuert nicht).
void collect_system(const Platform& p, Facts& f) {
    switch (p.sys().local_port_state(9021)) {
        case PortState::Open: f.set("net.port_9021", "open"); break;
        case PortState::Closed: f.set("net.port_9021", "closed"); break;
        case PortState::Unknown: break;
    }

    // sys.temp.cpu_c / sys.temp.soc_c: Momentaufnahme in Grad Celsius; fehlt, wenn nicht lesbar.
    const Thermal th = p.sys().thermal();
    if (th.cpu_c >= 0) f.set("sys.temp.cpu_c", th.cpu_c);
    if (th.soc_c >= 0) f.set("sys.temp.soc_c", th.soc_c);

    const std::int64_t boot = p.sys().boot_epoch();
    if (boot <= 0) return;
    static const std::set<std::string> kChain = {"elfldr", "etahen", "kstuff", "shadowmountplus"};
    std::int64_t first = 0;
    std::string first_name;
    for (const auto& i : p.procs().running_info()) {
        if (i.start_epoch <= 0) continue;
        const std::string name = normalize_proc_name(i.name);
        if (!kChain.count(name)) continue;
        if (first == 0 || i.start_epoch < first) {
            first = i.start_epoch;
            first_name = name;
        }
    }
    if (first == 0) return;
    f.set("jb.seconds_after_boot", std::max<std::int64_t>(0, first - boot));
    f.set("jb.first_process", first_name);
}

namespace {

constexpr std::size_t kMaxPluginBytes = 8u << 20;

// Das Plugin schreibt beim Start "ps5-theme 1.23 starting" (im Log mit Zeitstempel davor, im Programm fest eingebaut,
// teils als "[ps5-theme] 1.23 starting"). Version im Format Haupt.Neben mit zwei Nebenstellen. Leer = nichts gefunden.
std::string find_ps5theme_version(const std::string& text, bool want_last) {
    static const std::string kTag = "ps5-theme", kStarting = " starting";
    auto digit = [&](std::size_t i) { return i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); };
    std::string found;
    for (std::size_t pos = text.find(kTag); pos != std::string::npos; pos = text.find(kTag, pos + kTag.size())) {
        std::size_t i = pos + kTag.size();
        if (i < text.size() && text[i] == ']') ++i;
        if (i >= text.size() || text[i] != ' ') continue;
        while (i < text.size() && text[i] == ' ') ++i;
        const std::size_t major = i;
        while (digit(i)) ++i;
        if (i == major || i - major > 3 || i >= text.size() || text[i] != '.') continue;
        const std::size_t minor = ++i;
        while (digit(i)) ++i;
        if (i - minor != 2 || text.compare(i, kStarting.size(), kStarting) != 0) continue;
        found = text.substr(major, i - major);
        if (!want_last) break;
    }
    return found;
}

constexpr std::int64_t kOversizedReserveMiB = 4096;      // downloadDataSize (MiB) ab hier verdaechtig
constexpr std::uint64_t kSmallContentBytes = 1ull << 30; // darunter gilt der Inhalt als "klein"
constexpr std::size_t kMaxContentEntries = 20000;

// Prueft, ob der Inhalt unter `dir` kleiner als `limit` ist. false = gross oder nicht sicher bestimmbar
// (zu viele Eintraege, zu tief): dann wird nichts gemeldet.
bool content_below(const IFileSystem& fs, const std::string& dir, std::uint64_t limit, std::uint64_t& sum, std::size_t& entries, int depth) {
    if (depth > 8) return false;
    for (const auto& e : fs.list_dir(dir)) {
        if (++entries > kMaxContentEntries) return false;
        if (e.is_dir) {
            if (!content_below(fs, join_path(dir, e.name), limit, sum, entries, depth + 1)) return false;
        } else {
            sum += e.size;
            if (sum >= limit) return false;
        }
    }
    return true;
}

std::string app_title_name(const json& param, const std::string& fallback) {
    if (!param.contains("localizedParameters") || !param["localizedParameters"].is_object()) return fallback;
    const json& lp = param["localizedParameters"];
    std::vector<std::string> langs;
    if (lp.contains("defaultLanguage") && lp["defaultLanguage"].is_string()) langs.push_back(lp["defaultLanguage"].get<std::string>());
    langs.push_back("en-US");
    for (const auto& l : langs)
        if (lp.contains(l) && lp[l].is_object() && lp[l].contains("titleName") && lp[l]["titleName"].is_string())
            return lp[l]["titleName"].get<std::string>();
    return fallback;
}

}  // namespace

// ps5theme.version / ps5theme.source: laufende Version laut Log ("log"), sonst die Version der Plugin-Datei ("plugin").
void collect_plugins(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    std::string version, source, text;
    for (const char* log : {"/data/ps5-theme/logs/log.txt", "/data/ps5-theme/logs/log.1.txt"}) {
        if (!fs.read_file(log, text)) continue;
        if (text.size() > kMaxLogBytes) text.erase(0, text.size() - kMaxLogBytes);
        version = find_ps5theme_version(text, /*want_last=*/true);
        if (!version.empty()) {
            source = "log";
            break;
        }
    }
    if (version.empty() && fs.read_range("/data/etaHEN/plugins/ps5-theme.plugin", 0, kMaxPluginBytes, text)) {
        version = find_ps5theme_version(text, /*want_last=*/false);
        source = "plugin";
    }
    if (version.empty()) return;
    f.set("ps5theme.version", version);
    f.set("ps5theme.source", source);
}

// apps.oversized[]: installierte Apps, die laut param.json (downloadDataSize in MiB) mehrere GiB reservieren, obwohl
// ihre Dateien zusammen unter 1 GiB bleiben. Gepruft wird nur dann, wenn die Reservierung gross genug ist.
void collect_apps(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    json arr = json::array();
    for (const auto& app : fs.list_dir("/user/app")) {
        if (!app.is_dir) continue;
        const std::string dir = join_path("/user/app", app.name);
        std::string text;
        if (!fs.read_file(join_path(dir, "sce_sys/param.json"), text)) continue;
        const json param = json::parse(text, nullptr, false);
        if (!param.is_object() || !param.contains("downloadDataSize")) continue;
        const json& v = param["downloadDataSize"];
        std::int64_t mib = 0;
        if (v.is_number_integer()) mib = v.get<std::int64_t>();
        else if (v.is_string()) mib = std::strtoll(v.get<std::string>().c_str(), nullptr, 10);
        if (mib < kOversizedReserveMiB) continue;
        std::uint64_t sum = 0;
        std::size_t entries = 0;
        if (!content_below(fs, dir, kSmallContentBytes, sum, entries, 0)) continue;
        arr.push_back({{"titleid", app.name}, {"name", app_title_name(param, app.name)}, {"reserved_mib", mib}, {"content_bytes", sum}});
    }
    f.set("apps.oversized", std::move(arr));
}

namespace {

// requiredSystemSoftwareVersion bzw. sdkVersion der param.json: "0x1360000000000000" ist Firmware 13.60 (die ersten beiden Bytes,
// jedes als zwei Dezimalziffern in Hex; an echten param.json der Test-Konsole abgelesen: 0x0960... = 9.60, 0x0403... = 4.03).
// Leer, wenn das Feld fehlt, 0 ist (Homebrew) oder nicht in dieses Format passt.
std::string decode_param_fw(const json& param, const char* key) {
    if (!param.contains(key) || !param[key].is_string()) return {};
    const std::string s = param[key].get<std::string>();
    if (s.size() != 18 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return {};
    for (std::size_t i = 2; i < s.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return {};
    const std::uint64_t n = std::strtoull(s.c_str() + 2, nullptr, 16);
    int parts[2] = {0, 0};
    for (int k = 0; k < 2; ++k) {
        const unsigned byte = static_cast<unsigned>((n >> (56 - 8 * k)) & 0xff);
        if ((byte >> 4) > 9 || (byte & 0xf) > 9) return {};
        parts[k] = static_cast<int>((byte >> 4) * 10 + (byte & 0xf));
    }
    if (parts[0] == 0) return {};
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d.%02d", parts[0], parts[1]);
    return buf;
}

// SDK-Version aus den Prozessparametern einer (fake-signierten) SELF-eboot.bin, wie PS5 Cooling & System Center sie liest
// (an drei Spielen der Test-Konsole gemessen): Kopf 4F 15 3D 1D oder 54 14 F5 EE, ab 0x18 die Zahl der Eintraege n (je 32 Byte ab 0x20),
// danach der ELF-Kopf mit den Programmkoepfen. PT_SCE_PROCPARAM (0x61000001) liegt in einem LOAD-Segment; die Datei-Position steht im
// Eintrag dieses Segments. Der Block beginnt mit seiner Groesse und "ORBI"; das u32 bei +0x14 ist die SDK-Version
// (0x04000033 = 4.00, 0x10000040 = 10.00). Zwei kleine Lesezugriffe, nie die ganze Datei (manche sind ueber 200 MB). 0 = nicht lesbar.
std::uint32_t rd_le(const std::string& b, std::size_t off, int n) {
    std::uint64_t v = 0;
    for (int i = n - 1; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(b[off + static_cast<std::size_t>(i)]);
    return static_cast<std::uint32_t>(v);
}
std::uint64_t rd_le64(const std::string& b, std::size_t off) {
    return static_cast<std::uint64_t>(rd_le(b, off, 4)) | (static_cast<std::uint64_t>(rd_le(b, off + 4, 4)) << 32);
}

// Liest Bytebereiche der eboot.bin: aus einer Datei im Dateisystem oder aus einem exFAT-Image.
using RangeReader = std::function<bool(std::uint64_t, std::size_t, std::string&)>;

std::uint32_t eboot_sdk_raw(const RangeReader& read) {
    std::string h;
    if (!read(0, 4096, h) || h.size() < 0x100) return 0;
    if (h.compare(0, 4, "\x4f\x15\x3d\x1d") != 0 && h.compare(0, 4, "\x54\x14\xf5\xee") != 0) return 0;
    const std::uint32_t cnt = rd_le(h, 0x18, 2);
    const std::size_t elf = 0x20 + static_cast<std::size_t>(cnt) * 0x20;
    if (cnt == 0 || cnt > 64 || elf + 64 > h.size() || h.compare(elf, 4, "\x7f" "ELF") != 0) return 0;
    const std::uint64_t phoff = rd_le64(h, elf + 32);
    const std::uint32_t phent = rd_le(h, elf + 54, 2), phnum = rd_le(h, elf + 56, 2);
    if (phent != 56 || phnum == 0 || phnum > 64 || phoff > h.size() || elf + phoff + static_cast<std::uint64_t>(phnum) * 56 > h.size()) return 0;
    const std::size_t ph = elf + static_cast<std::size_t>(phoff);
    std::uint64_t pp = UINT64_MAX;
    for (std::uint32_t i = 0; i < phnum; ++i)
        if (rd_le(h, ph + i * 56, 4) == 0x61000001u) {
            pp = rd_le64(h, ph + i * 56 + 8);
            break;
        }
    if (pp == UINT64_MAX) return 0;
    for (std::uint32_t i = 0; i < phnum; ++i) {
        const std::size_t p = ph + i * 56;
        const std::uint64_t off = rd_le64(h, p + 8), fsz = rd_le64(h, p + 32);
        if (rd_le(h, p, 4) != 1 || pp < off || pp - off + 0x18 > fsz) continue;
        for (std::uint32_t e = 0; e < cnt; ++e) {
            const std::size_t en = 0x20 + e * 0x20;
            const std::uint64_t props = rd_le64(h, en);
            if (((props >> 20) & 0xFFFF) != i || (props & 0x10000)) continue;
            if (props & 0xA) return 0;  // verschluesselt oder komprimiert
            const std::uint64_t eoff = rd_le64(h, en + 8), efsz = rd_le64(h, en + 16), rel = pp - off;
            std::string b;
            if (rel + 0x18 <= efsz && read(eoff + rel, 0x18, b) && b.size() == 0x18 && b.compare(8, 4, "ORBI") == 0) return rd_le(b, 0x14, 4);
            return 0;
        }
        return 0;
    }
    return 0;
}

std::string lower_ext_of(const std::string& path) {
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// "0x10000040" -> "10.00": die oberen zwei Bytes, je zwei Dezimalziffern in Hex (wie bei sdkVersion der param.json). Leer, wenn es nicht passt.
std::string sdk_raw_to_fw(std::uint32_t v) {
    if (v == 0) return {};
    int parts[2];
    for (int k = 0; k < 2; ++k) {
        const unsigned byte = (v >> (24 - 8 * k)) & 0xff;
        if ((byte >> 4) > 9 || (byte & 0xf) > 9) return {};
        parts[k] = static_cast<int>((byte >> 4) * 10 + (byte & 0xf));
    }
    if (parts[0] == 0) return {};
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d.%02d", parts[0], parts[1]);
    return buf;
}

}  // namespace

// games.fw[]: PS5-Titel (PPSA...) unter /user/app mit der Firmware, die ihre param.json verlangt (SM+ legt dort je gemountetem Spiel
// einen Ordner mit sce_sys/param.json an). games.need_backport[]: davon die Titel, die eine neuere Firmware verlangen, als die Konsole hat,
// und fuer die es weder einen Backport-Ordner <Scanpfad>/backports/<TITLE_ID>/ gibt noch einen im Spiel eingebauten Backport (SDK-Version der
// eboot.bin nicht ueber der Firmware, games.fw[].backport_inline). Nur lesen.
void collect_game_firmware(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    // Ein nicht lesbarer Rohwert ("0x00000000" u. ae., siehe format_ps5_fw) ist keine Version: dann gilt die Firmware als unbekannt und kein
    // Spiel wird fuer "braucht Backport" gehalten (Audit 12. Nov. 2026)
    std::string fw = p.sys().firmware();
    if (fw.compare(0, 2, "0x") == 0) fw.clear();
    std::vector<std::string> roots;
    if (const json* r = f.get("smp.scan_roots"); r && r->is_array())
        for (const auto& e : *r)
            if (e.is_string()) roots.push_back(e.get<std::string>());
    for (const auto& d : default_scan_paths()) roots.push_back(d);

    // Ordner je Titel aus ShadowMount+ (smp.titles): dort liegt die eboot.bin; Images sind nicht lesbar (dann bleibt es unbekannt).
    std::map<std::string, std::vector<std::string>> folders;
    if (const json* t = f.get("smp.titles"); t && t->is_array())
        for (const auto& e : *t)
            if (e.is_object() && e.contains("titleid") && e["titleid"].is_string() && e.contains("path") && e["path"].is_string())
                folders[e["titleid"].get<std::string>()].push_back(e["path"].get<std::string>());

    static const std::regex id_re("^PPSA[0-9]{5}$");
    json games = json::array(), need = json::array();
    for (const auto& app : fs.list_dir("/user/app")) {
        if (!app.is_dir || !std::regex_match(app.name, id_re)) continue;
        std::string text;
        if (!fs.read_file(join_path(join_path("/user/app", app.name), "sce_sys/param.json"), text)) continue;
        const json param = json::parse(text, nullptr, false);
        if (!param.is_object()) continue;
        const std::string required = decode_param_fw(param, "requiredSystemSoftwareVersion");
        if (required.empty()) continue;
        bool backport = false;
        for (const auto& r : roots)
            if (fs.is_dir(join_path(join_path(r, "backports"), app.name))) {
                backport = true;
                break;
            }
        // Backport im Spiel selbst (Marc, 12. Nov. 2026: zwei Spiele verlangen 13.60 und laufen auf 12.00): Ein Backport-Werkzeug setzt die
        // SDK-Version in den Prozessparametern der eboot.bin herunter, param.json behaelt die alte Angabe. Liegt die SDK-Version der eboot.bin
        // nicht ueber der Firmware der Konsole, obwohl param.json mehr verlangt, ist das Spiel angepasst. Nur wenn die eboot.bin lesbar ist.
        std::string eboot_sdk;
        if (const auto it = folders.find(app.name); it != folders.end())
            for (const auto& src : it->second) {
                std::uint32_t raw = 0;
                if (fs.is_dir(src)) {
                    const std::string eboot = join_path(src, "eboot.bin");
                    raw = eboot_sdk_raw([&](std::uint64_t off, std::size_t n, std::string& out) { return fs.read_range(eboot, off, n, out); });
                } else if (const std::string ext = lower_ext_of(src); ext == ".exfat" || ext == ".ffpkg" || ext == ".ffpfs" || ext == ".ffpfsc") {
                    // Image (exFAT, UFS2, PFS, PFSC): ohne Einhaengen aus dem Image lesen (libmedic/src/imgread.c)
                    if (const auto img = ImageReader::open(fs, src))
                        raw = eboot_sdk_raw([&](std::uint64_t off, std::size_t n, std::string& out) { return img->read_file("/eboot.bin", off, n, out); });
                }
                eboot_sdk = sdk_raw_to_fw(raw);
                if (!eboot_sdk.empty()) break;
            }
        bool backport_inline = false;
        if (!backport && !fw.empty() && !eboot_sdk.empty() && compare_versions(required, fw) > 0 && compare_versions(eboot_sdk, fw) <= 0) {
            backport = true;
            backport_inline = true;
        }
        games.push_back({{"titleid", app.name}, {"name", app_title_name(param, app.name)}, {"required_fw", required}, {"sdk_fw", decode_param_fw(param, "sdkVersion")},
                         {"eboot_sdk", eboot_sdk}, {"backport", backport}, {"backport_inline", backport_inline}});
        if (!fw.empty() && !backport && compare_versions(required, fw) > 0)
            need.push_back({{"titleid", app.name}, {"required_fw", required}, {"desc", app.name + ": " + required}});
    }
    f.set("games.fw", std::move(games));
    f.set("games.need_backport", std::move(need));
}

namespace {
// "TITLE" aus einer param.sfo (PS4-Titel): Kopf "\0PSF", Schluesseltabelle ab Offset 8, Datentabelle ab 12, Anzahl bei 16, Eintraege zu 16 Bytes ab 20.
std::string sfo_title(const std::string& d) {
    auto u16 = [&](std::size_t o) { return static_cast<std::uint32_t>(static_cast<unsigned char>(d[o])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(d[o + 1])) << 8); };
    auto u32 = [&](std::size_t o) { return u16(o) | (u16(o + 2) << 16); };
    if (d.size() < 20 || d.compare(0, 4, std::string("\0PSF", 4)) != 0) return {};
    const std::size_t kt = u32(8), dt = u32(12), n = std::min<std::size_t>(u32(16), 256);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t e = 20 + 16 * i;
        if (e + 16 > d.size()) break;
        const std::size_t k = kt + u16(e);
        if (k >= d.size() || d.compare(k, 6, std::string("TITLE\0", 6)) != 0) continue;
        const std::size_t off = dt + u32(e + 12), len = std::min<std::size_t>(u32(e + 4), 128);
        if (off >= d.size()) return {};
        std::string t = d.substr(off, std::min(len, d.size() - off));
        t.resize(std::strlen(t.c_str()));
        return t;
    }
    return {};
}
}  // namespace

// titles.names: Anzeigenamen je Titel-Kennung aus den Metadaten der Konsole, auch fuer nicht eingehaengte Titel: /user/appmeta/<ID>/param.json
// (PS5, und Homebrew mit param.json) und /system_data/priv/appmeta/<ID>/param.sfo (PS4). Nur lesen, nur Namen (D17).
void collect_title_names(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    static const std::regex id_re("^(PPSA|CUSA)[0-9]{5}$");
    json names = json::object();
    std::size_t n = 0;
    for (const auto& e : fs.list_dir("/user/appmeta")) {
        if (!e.is_dir || !std::regex_match(e.name, id_re) || ++n > 600) continue;
        std::string name, text;
        if (fs.read_file(join_path(join_path("/user/appmeta", e.name), "param.json"), text) && text.size() <= (256u << 10)) {
            const json j = json::parse(text, nullptr, false);
            if (j.is_object()) name = app_title_name(j, std::string());
        }
        if (name.empty() && e.name.compare(0, 4, "CUSA") == 0 &&
            fs.read_file(join_path(join_path("/system_data/priv/appmeta", e.name), "param.sfo"), text) && text.size() <= (64u << 10))
            name = sfo_title(text);
        if (!name.empty()) names[e.name] = name;
    }
    f.set("titles.names", std::move(names));
}

void collect_storage(const Platform& p, Facts& f) {
    json arr = json::array();
    for (auto& s : p.sys().storage()) {
        double pct = s.total_bytes ? 100.0 * static_cast<double>(s.free_bytes) / static_cast<double>(s.total_bytes) : 0.0;
        arr.push_back({{"mount", s.mount}, {"total", s.total_bytes}, {"free", s.free_bytes}, {"free_pct", pct},
                       {"fstype", s.fstype}, {"readonly", s.read_only}});
    }
    f.set("storage.mounts", std::move(arr));
    for (auto& s : p.sys().storage())  // einfacher Pfad fuer die Messwerte im Verlauf
        if (s.mount == "/data" && s.total_bytes) f.set("storage.data_free_pct", 100.0 * static_cast<double>(s.free_bytes) / static_cast<double>(s.total_bytes));
}

// kstuff.version, kstuff.origin, smp.version, etahen.version ... (Quelle: ISystemInfo::components)
void collect_components(const Platform& p, Facts& f) {
    for (auto& [name, props] : p.sys().components())
        for (auto& [k, v] : props) f.set(safe_key(name) + "." + safe_key(k), v);
}

namespace {

// ShadowMount+ meldet beim Start "[API] HTTP/JSON ready: http://0.0.0.0:10101/api/v1 ..." (echtes Log, 1.7beta11). Daraus:
// smp.api.bind, smp.api.port und -- nur wenn die Plattform den Port pruefen kann -- smp.api.state ("open"/"closed").
// Ohne diese Zeile (API aus, aeltere Version, Start noch nicht abgeschlossen) fehlen die Fakten und keine Regel feuert.
// Geprueft wird 127.0.0.1: das passt zu 0.0.0.0 und 127.0.0.1; ist der Listener an eine bestimmte LAN-Adresse gebunden,
// sagt ein Loopback-Versuch nichts aus, dann bleibt der Zustand unbekannt. Die Bereit-Zeile steht nach einem laengeren
// Lauf nur noch am Anfang der Datei, deshalb wie bei der Version: erst die gelesenen Zeilen, dann die ersten 16 KiB.
void collect_smp_api(const Platform& p, Facts& f) {
    static const std::regex re(R"(\[API\] HTTP/JSON ready: http://([0-9A-Za-z.\-]+):([0-9]{1,5})/)");
    auto latest_in = [&](const std::vector<std::string>& lines, std::smatch& m) {
        for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            if (std::regex_search(*it, m, re)) return true;
        return false;
    };
    std::vector<std::string> tail;
    if (const json* lines = f.get("smp.log.lines"); lines && lines->is_array())
        for (const auto& l : *lines)
            if (l.is_string()) tail.push_back(l.get<std::string>());
    // Die Treffergruppen werden sofort in eigene Strings kopiert: ein smatch zeigt in die durchsuchte Zeile, und die Zeilen der zweiten Suche
    // (head_lines) leben nur in diesem Block (Sanitizer-Fund, 12. Nov. 2026: Use-after-free).
    std::string bind, port_text;
    {
        std::smatch m;
        if (latest_in(tail, m)) {
            bind = m[1].str();
            port_text = m[2].str();
        } else {
            std::string head;
            if (!p.fs().read_range(join_path(kSmpDir, "debug.log"), 0, 16384, head)) return;
            const auto head_lines = split_lines(head);
            if (!latest_in(head_lines, m)) return;
            bind = m[1].str();
            port_text = m[2].str();
        }
    }
    const long port = std::strtol(port_text.c_str(), nullptr, 10);
    if (port < 1 || port > 65535) return;
    f.set("smp.api.bind", bind);
    f.set("smp.api.port", std::to_string(port));
    if (bind != "0.0.0.0" && bind != "127.0.0.1") return;
    switch (p.sys().local_port_state(static_cast<int>(port))) {
        case PortState::Open: f.set("smp.api.state", "open"); break;
        case PortState::Closed: f.set("smp.api.state", "closed"); break;
        case PortState::Unknown: break;
    }
}

}  // namespace

void collect_shadowmount(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    const std::string cfg_path = join_path(kSmpDir, "config.ini");

    std::string cfg_text;
    const bool cfg_read = fs.read_file(cfg_path, cfg_text);
    // Vorhanden, aber nicht lesbar (Lesefehler): nicht als "config.ini fehlt, Standardwerte" melden, sondern nichts ueber die Datei behaupten
    // (die uebrigen Fakten von ShadowMount+ werden trotzdem gesammelt)
    if (cfg_read || !fs.exists(cfg_path)) f.set("smp.config_present", cfg_read);
    Config cfg = parse_ini(cfg_text);
    f.set("smp.config", cfg.values);
    f.set("smp.scanpaths_custom", cfg.scanpaths);
    f.set("smp.config_v2", cfg.v2);
    // Nach der Umwandlung legt SM+ config-v1-backup/ready.ini an. Steht dann wieder eine v1-Datei in config.ini, verweigert
    // SM+ den Start (sm_config_file.c: Abbruch, damit die umgewandelten Dateien nicht von einer alten ueberschrieben werden).
    f.set("smp.v1_backup_ready", fs.exists(join_path(kSmpDir, "config-v1-backup/ready.ini")));

    // Zeilen, die SM+ 1.7beta12 in einer v2-Datei ignoriert (flache v1-Namen) oder wegen derer es die Datei ablehnt.
    json v1keys = json::array(), problems = json::array();
    if (cfg.v2) {
        for (const auto& l : cfg.lines) {
            if (const V1Map* m = find_v1(l.key)) {
                // "[auto_update_ampr]" u. a. heissen in v1 und v2 gleich und stehen nicht in der Liste; hier nur echte v1-Namen.
                v1keys.push_back({{"key", m->v1}, {"value", l.value}, {"line", l.line}, {"section", l.section},
                                  {"target", m->section ? "[" + std::string(m->section) + "] " + m->key : std::string()},
                                  {"tsection", m->section ? m->section : ""}, {"tkey", m->key ? m->key : ""}, {"add", m->add}});
            } else if (const std::string bad = invalid_v2_value(l); !bad.empty()) {
                problems.push_back({{"line", l.line}, {"text", "Zeile " + std::to_string(l.line) + ": " + bad}});
            }
        }
        for (int n : cfg.unreadable_lines) problems.push_back({{"line", n}, {"text", "Zeile " + std::to_string(n) + ": weder Kommentar noch Sektion noch Schluessel=Wert"}});
        if (const json* ver = cfg.values.contains("config") && cfg.values["config"].is_object() && cfg.values["config"].contains("version") ? &cfg.values["config"]["version"] : nullptr;
            ver && ver->is_string() && ver->get<std::string>() != "2")
            problems.push_back({{"line", 0}, {"text", "[config] version = " + ver->get<std::string>() + " (erwartet: 2)"}});
    }
    f.set("smp.v1_keys", std::move(v1keys));
    f.set("smp.config_problems", std::move(problems));

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
    f.set("smp.default_scan_roots", default_scan_paths());

    // Eigene Scanpfade auf einem Laufwerk, das nicht eingehaengt ist. Ohne Laufwerk sind /mnt/usbN und /mnt/extN auf der PS5
    // nur kleine tmpfs-Platzhalter (getfsstat auf der Test-Konsole, 9. Nov. 2026); fehlt die Speicherliste ganz, ist der
    // Zustand unbekannt und nichts wird gemeldet.
    {
        json missing = json::array();
        const auto storage = p.sys().storage();
        if (!storage.empty()) {
            for (const auto& root : cfg.scanpaths) {
                std::string drive;
                for (const char* prefix : {"/mnt/usb", "/mnt/ext"}) {
                    const std::string pre = prefix;
                    if (root.compare(0, pre.size(), pre) != 0) continue;
                    std::size_t i = pre.size();
                    while (i < root.size() && std::isdigit(static_cast<unsigned char>(root[i]))) ++i;
                    if (i > pre.size() && (i == root.size() || root[i] == '/')) drive = root.substr(0, i);
                }
                if (drive.empty()) continue;
                // Nur der ausdrueckliche Platzhalter zaehlt: eine Speicherliste ohne den Eintrag (aeltere Snapshots, unvollstaendige
                // Angaben) sagt nichts darueber, ob das Laufwerk steckt.
                const auto it = std::find_if(storage.begin(), storage.end(), [&](const StorageInfo& s) { return s.mount == drive; });
                if (it != storage.end() && it->fstype == "tmpfs") missing.push_back({{"path", root}, {"drive", drive}});
            }
        }
        f.set("smp.missing_drive_paths", std::move(missing));
    }
    // SM+ legt je installiertem Titel /user/app/<TITLE_ID>/mount_img.lnk (Pfad des Images) bzw. mount.lnk (Pfad des Ordners; bei
    // Images der Einhaengepunkt unter /mnt/shadowmnt) an (echtes Log und echte Dateien der Test-Konsole, 10. Nov. 2026). Damit bekommt
    // ein Image ohne TITLE_ID im Namen (z. B. Spiel.exfat) seine TITLE_ID, und ein Ordner desselben Titels daneben wird als Duplikat
    // erkannt: SM+ nutzt nur eine Quelle und meldet bei jedem Start "Duplikat <TITLE_ID> ignoriert". "active" = die von SM+ genutzte.
    std::map<std::string, std::string> active_source;  // Quellpfad -> TITLE_ID
    json links = json::array();
    {
        static const std::regex id_re("^PPSA[0-9]{5}$");
        for (const auto& app : fs.list_dir("/user/app")) {
            if (!app.is_dir || !std::regex_match(app.name, id_re)) continue;
            for (const char* kind : {"image", "folder"}) {
                std::string text;
                if (!fs.read_file(join_path(join_path("/user/app", app.name), std::string(kind) == "image" ? "mount_img.lnk" : "mount.lnk"), text)) continue;
                const auto nl = text.find_first_of("\r\n");
                if (nl != std::string::npos) text.erase(nl);
                text = trim(text);
                while (!text.empty() && text.back() == '\0') text.pop_back();
                if (text.size() < 2 || text.size() > 512 || text[0] != '/') continue;
                if (std::string(kind) == "folder" && starts_with_dir(text, "/mnt/shadowmnt")) continue;  // Einhaengepunkt eines Images, keine Quelle
                active_source[text] = app.name;
                links.push_back({{"titleid", app.name}, {"kind", kind}, {"path", text}});
            }
        }
    }
    f.set("smp.mount_links", std::move(links));

    auto entries = scan_roots(fs, roots);
    for (auto& [path, e] : entries) {
        const auto it = active_source.find(path);
        if (it == active_source.end()) continue;
        if (e.titleid.empty() && e.is_image) e.titleid = it->second;
        if (e.titleid == it->second) e.active = true;
    }

    json images = json::array(), deep = json::array();
    std::map<std::string, int> count;
    for (auto& [path, e] : entries)
        if (e.level <= depth && !e.titleid.empty()) ++count[e.titleid];

    json titles = json::array();
    for (auto& [path, e] : entries) {
        if (e.is_image) {
            json img = {{"name", e.name}, {"ext", e.ext}, {"path", e.path}, {"size", e.size}, {"level", e.level}};
            if (e.ext == ".exfat") {
                const ExfatInfo xi = exfat_info(fs, e.path);
                if (xi.cluster) img["cluster_size"] = xi.cluster;
                if (xi.volume_bytes) {
                    img["volume_bytes"] = xi.volume_bytes;
                    img["truncated"] = e.size < xi.volume_bytes ? 1 : 0;  // Datei kleiner als das beschriebene Dateisystem: unvollstaendige Kopie
                }
            }
            if (e.ext != ".dat") img["tiny"] = e.size < 512 ? 1 : 0;  // kleiner als ein Kopf: leer oder abgebrochen
            images.push_back(std::move(img));
        }
        if (e.level > depth) {  // von SM+ bei dieser Tiefe nicht gescannt
            deep.push_back({{"path", e.path}, {"level", e.level}, {"titleid", e.titleid}});
            continue;
        }
        if (!e.titleid.empty()) {
            json t = {{"titleid", e.titleid}, {"path", e.path}, {"root", e.root}, {"level", e.level}, {"dup_count", count[e.titleid]}, {"active", e.active}};
            if (!e.title_name.empty()) t["name"] = e.title_name;
            titles.push_back(std::move(t));
        }
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

    // Hat SM+ die Konfiguration abgelehnt, steht das im Log: "[CFG] invalid <was> at line N: <Schluessel>=<Wert>" bzw.
    // "[CFG] cannot read <Datei> near line N". Die Zeilennummer N zaehlt in der geglaetteten Liste, nicht in der Datei.
    if (const json* lines = f.get("smp.log.lines"); lines && lines->is_array()) {
        static const std::regex re(R"(\[CFG\] (invalid .*|cannot read .*))");
        json log_problems = json::array();
        for (const auto& l : *lines) {
            if (!l.is_string()) continue;
            std::smatch m;
            const std::string line = l.get<std::string>();
            if (std::regex_search(line, m, re)) log_problems.push_back({{"line", 0}, {"text", "Log: [CFG] " + trim(m[1].str())}});
        }
        if (!log_problems.empty()) {
            json all = f.get("smp.config_problems") ? *f.get("smp.config_problems") : json::array();
            for (auto& p2 : log_problems) all.push_back(std::move(p2));
            f.set("smp.config_problems", std::move(all));
        }
    }
    collect_unstable_sources(f);

    // ShadowMount+ schreibt beim Start "ShadowMount+ v1.7beta11 ... FW: 12.00" ins debug.log (erster Konsolenlauf).
    // Dient als Versionsquelle, solange die Plattform keine Komponenten-Version liefert; die juengste Startzeile gilt.
    // Auf einer laenger laufenden Konsole ist das Log laenger als die gelesenen letzten Zeilen, die Startzeile steht dann
    // nur noch am Anfang der Datei (am 8. Nov. so auf der Test-Konsole): deshalb zusaetzlich die ersten 16 KiB pruefen.
    if (!f.get("smp.version")) {
        static const std::regex re(R"(ShadowMount\+ v([0-9][0-9A-Za-z.\-]*))");
        auto latest_in = [&](const std::vector<std::string>& lines) {
            for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
                std::smatch m;
                if (!std::regex_search(*it, m, re)) continue;
                std::string v = m[1].str();
                while (!v.empty() && (v.back() == '.' || v.back() == '-')) v.pop_back();
                if (!v.empty()) return v;
            }
            return std::string();
        };
        std::vector<std::string> tail;
        if (const json* lines = f.get("smp.log.lines"); lines && lines->is_array())
            for (const auto& l : *lines)
                if (l.is_string()) tail.push_back(l.get<std::string>());
        std::string v = latest_in(tail);
        if (v.empty()) {
            std::string head;
            if (fs.read_range(join_path(kSmpDir, "debug.log"), 0, 16384, head)) v = latest_in(split_lines(head));
        }
        if (!v.empty()) f.set("smp.version", v);
    }
    collect_smp_api(p, f);
    collect_mounts(fs, f);
}

// Spielstaende und Konto: Die Metadaten jedes Spielstands (/user/home/<uid>/savedata_prospero_meta/user/<TITLE_ID>/[sce_bu_]<Name>.sfo,
// Format PSF) tragen ACCOUNT_ID, die Konto-ID, mit der er erstellt wurde. Der "Spielstandspeicher" sce_sdmemory legt das System bei jedem
// Spielstart neu an, seine ACCOUNT_ID ist daher die des jetzt aktiven Kontos. Beobachtung auf der Test-Konsole (10. Nov. 2026): Alle
// Spielstaende bis zum 3. Nov. tragen eine andere Konto-ID als die Speicher seit dem 7. Nov.; drei verschiedene Titel mit der alten ID
// zeigten im Spiel "Die gespeicherten Daten koennen nicht geladen werden, weil sie beschaedigt sind" (Eintrag "Beschaedigte Daten"),
// obwohl ihre Abbilder strukturell gesund sind. Ein Block-Vergleich von Abbild und Sicherung taugt dafuer nicht: auch die Abbilder
// von Spielstaenden, die nichts melden, weichen dort massiv ab. Die Liste in den Einstellungen zeigt alte Spielstaende trotzdem normal.
// Ohne eindeutige aktuelle Konto-ID (kein Speicher oder zwei verschiedene) wird nichts gemeldet.
static bool parse_psf(const std::string& d, std::map<std::string, std::string>& out) {
    auto u16 = [&](std::size_t o) { return static_cast<std::uint32_t>(static_cast<unsigned char>(d[o])) | static_cast<std::uint32_t>(static_cast<unsigned char>(d[o + 1])) << 8; };
    auto u32 = [&](std::size_t o) { return u16(o) | u16(o + 2) << 16; };
    if (d.size() < 20 || d.compare(0, 4, std::string("\0PSF", 4)) != 0) return false;
    const std::uint32_t key_table = u32(8), data_table = u32(12), count = u32(16);
    if (count > 64 || 20 + static_cast<std::uint64_t>(count) * 16 > d.size()) return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t e = 20 + static_cast<std::size_t>(i) * 16;
        const std::uint32_t key_off = u16(e), fmt = u16(e + 2), len = u32(e + 4), data_off = u32(e + 12);
        const std::uint64_t kpos = static_cast<std::uint64_t>(key_table) + key_off, dpos = static_cast<std::uint64_t>(data_table) + data_off;
        if (kpos >= d.size() || dpos + len > d.size() || len > 4096) continue;
        const auto kend = d.find('\0', static_cast<std::size_t>(kpos));
        if (kend == std::string::npos) continue;
        const std::string key = d.substr(static_cast<std::size_t>(kpos), kend - static_cast<std::size_t>(kpos));
        std::string raw = d.substr(static_cast<std::size_t>(dpos), len);
        if (fmt == 0x0204) {
            while (!raw.empty() && raw.back() == '\0') raw.pop_back();
            out[key] = raw;
        } else {
            static const char* hex = "0123456789abcdef";
            std::string h;
            for (unsigned char c : raw) h += {hex[c >> 4], hex[c & 15]};
            out[key] = h;
        }
    }
    return true;
}

void collect_savedata(const Platform& p, Facts& f) {
    static const std::regex id_re("^PPSA[0-9]{5}$"), account_re("^[0-9a-f]{16}$");
    const IFileSystem& fs = p.fs();
    json foreign = json::array();
    std::string first_current;
    for (const auto& user : fs.list_dir("/user/home")) {
        if (!user.is_dir) continue;
        const std::string meta = join_path(join_path("/user/home", user.name), "savedata_prospero_meta/user");
        struct Save {
            std::string titleid, name, title, account, path;
            bool memory;
        };
        std::vector<Save> saves;
        std::set<std::string> seen;
        for (const auto& title : fs.list_dir(meta)) {
            if (!title.is_dir || !std::regex_match(title.name, id_re)) continue;
            for (const auto& file : fs.list_dir(join_path(meta, title.name))) {
                if (file.is_dir || file.size > 65536 || file.name.size() < 5 || file.name.compare(file.name.size() - 4, 4, ".sfo") != 0) continue;
                std::string data;
                std::map<std::string, std::string> psf;
                const std::string path = join_path(join_path(meta, title.name), file.name);
                if (!fs.read_file(path, data) || !parse_psf(data, psf)) continue;
                const std::string account = psf.count("ACCOUNT_ID") ? psf["ACCOUNT_ID"] : std::string();
                if (!std::regex_match(account, account_re) || account == "0000000000000000") continue;
                std::string name = psf.count("SAVEDATA_DIRECTORY") ? psf["SAVEDATA_DIRECTORY"] : file.name.substr(0, file.name.size() - 4);
                if (name.empty() || !seen.insert(title.name + "/" + name).second) continue;
                saves.push_back({title.name, name, psf.count("MAINTITLE") ? psf["MAINTITLE"] : std::string(), account, path, name == "sce_sdmemory"});
            }
        }
        std::set<std::string> current;
        for (const auto& s : saves)
            if (s.memory) current.insert(s.account);
        if (current.size() != 1) continue;
        const std::string& now = *current.begin();
        if (first_current.empty()) first_current = now;
        for (const auto& s : saves) {
            if (s.memory || s.account == now) continue;
            foreign.push_back({{"uid", user.name}, {"titleid", s.titleid}, {"name", s.name}, {"title", s.title}, {"account", s.account},
                               {"current", now}, {"path", s.path}, {"label", s.titleid + " " + s.name + (s.title.empty() ? std::string() : " (" + s.title + ")")}});
        }
    }
    if (!first_current.empty()) f.set("savedata.current_account", first_current);
    f.set("savedata.foreign", std::move(foreign));
}

// Startverlauf und Fehlerverlauf der Systemsoftware (beides auf der Test-Konsole am 10. Nov. 2026 gelesen, nur lesend).
// /system_data/priv/vshlog/vshlog.0.txt: eine Zeile je Ereignis, z. B. "Mon, 12 Oct 2026 16:52:51 +0000 Boot up (build#..., MAIN_SOC)",
// "... Shutdown for reboot", "... Shutdown". Jeder normale Neustart steht als "Shutdown for reboot" vor dem naechsten "Boot up" (40 von 49).
// Folgt ein "Boot up" direkt auf ein anderes "Boot up", ging die Sitzung ohne protokolliertes Herunterfahren zu Ende; das passt zu
// Absturz oder Stromverlust (abgeleitet, nicht bestaetigt). Fenster: 14 Tage vor dem letzten Eintrag, damit die Pruefung am PC gegen
// einen Snapshot dasselbe ergibt wie auf der Konsole.
// /system_data/priv/error/history/*.json: je Fehlermeldung eine JSON-Datei (Ring, 100 Stueck): shortError, titleId, tick (Mikrosekunden
// seit Jahr 1). Die Regeln werten Paare aus Code und Titel aus.
static std::int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + doe - 719468;
}

static std::string iso_from_epoch(std::int64_t t) {
    std::int64_t days = t / 86400;
    std::int64_t rem = t % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int doe = static_cast<int>(days - era * 146097);
    const int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yoe + static_cast<int>(era) * 400;
    const int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int mp = (5 * doy + 2) / 153;
    const int d = doy - (153 * mp + 2) / 5 + 1;
    const int m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, m, d, static_cast<int>(rem / 3600), static_cast<int>(rem % 3600 / 60), static_cast<int>(rem % 60));
    return buf;
}

// "Mon, 12 Oct 2026 16:52:51 +0200 Boot up (...)" -> Unix-Sekunden (UTC) und Rest der Zeile.
static bool parse_vsh_line(const std::string& line, std::int64_t& epoch, std::string& msg) {
    static const std::regex re(R"(^\w{3}, (\d{1,2}) (\w{3}) (\d{4}) (\d{2}):(\d{2}):(\d{2}) ([+-])(\d{2})(\d{2}) (.*)$)");
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::smatch m;
    if (!std::regex_match(line, m, re)) return false;
    int mon = 0;
    for (int i = 0; i < 12; ++i)
        if (m[2].str() == months[i]) mon = i + 1;
    if (!mon) return false;
    const std::int64_t local = days_from_civil(std::stoi(m[3]), mon, std::stoi(m[1])) * 86400 + std::stoi(m[4]) * 3600 + std::stoi(m[5]) * 60 + std::stoi(m[6]);
    const int off = (std::stoi(m[8]) * 3600 + std::stoi(m[9]) * 60) * (m[7].str() == "-" ? -1 : 1);
    epoch = local - off;
    msg = m[10].str();
    return true;
}

void collect_system_logs(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    {  // Startverlauf
        std::string text;
        if (fs.read_file("/system_data/priv/vshlog/vshlog.0.txt", text) && text.size() <= (1u << 20)) {
            struct Ev {
                std::int64_t t;
                bool boot;
                std::string cause;
            };
            std::vector<Ev> evs;
            for (auto line : split_lines(text)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                std::int64_t t = 0;
                std::string msg;
                if (!parse_vsh_line(line, t, msg)) continue;
                Ev e{t, msg.compare(0, 7, "Boot up") == 0, std::string()};
                if (e.boot) {
                    static const std::regex cause_re(R"(, (\w+)\)\s*$)");
                    std::smatch m;
                    if (std::regex_search(msg, m, cause_re)) e.cause = m[1].str();
                }
                evs.push_back(std::move(e));
            }
            if (!evs.empty()) {
                std::int64_t last = evs[0].t;
                for (const auto& e : evs) last = std::max(last, e.t);
                json unclean = json::array();
                int boots = 0, n14 = 0;
                for (std::size_t i = 0; i < evs.size(); ++i) {
                    if (!evs[i].boot) continue;
                    ++boots;
                    if (i == 0 || !evs[i - 1].boot) continue;
                    unclean.push_back({{"ts", iso_from_epoch(evs[i].t)}, {"epoch", evs[i].t}, {"cause", evs[i].cause}});
                    if (evs[i].t >= last - 14 * 86400) ++n14;
                }
                f.set("boot.log_last", iso_from_epoch(last));
                f.set("boot.log_last_epoch", last);
                f.set("boot.boots_total", boots);
                f.set("boot.unclean_total", static_cast<int>(unclean.size()));
                f.set("boot.unclean_14d", n14);
                f.set("boot.unclean", std::move(unclean));
            }
        }
    }
    if (fs.is_dir("/system_data/priv/error/history")) {  // Fehlerverlauf (fehlt der Ordner, fehlt der Fakt: nicht "0 Fehler")
        const std::string dir = "/system_data/priv/error/history";
        struct Agg {
            int count = 0;
            std::int64_t last = 0;
        };
        std::map<std::pair<std::string, std::string>, Agg> pairs;
        int total = 0, seen = 0;
        static const std::regex code_re("^[A-Za-z0-9]{1,4}-[0-9]{3,6}(-[0-9]{1,2})?$|^0x[0-9A-Fa-f]{8}$"), title_re("^[A-Z]{4}[0-9]{5}$|^NPXS[0-9]{5}$");
        for (const auto& e : fs.list_dir(dir)) {
            if (e.is_dir || e.size > 4096 || e.name.size() < 6 || e.name.compare(e.name.size() - 5, 5, ".json") != 0) continue;
            if (++seen > 400) break;
            std::string t;
            if (!fs.read_file(join_path(dir, e.name), t)) continue;
            if (t.compare(0, 3, "\xEF\xBB\xBF") == 0) t.erase(0, 3);
            const json j = json::parse(t, nullptr, false);
            if (!j.is_object() || !j.contains("shortError") || !j["shortError"].is_string()) continue;
            const std::string code = j["shortError"].get<std::string>();
            if (!std::regex_match(code, code_re)) continue;
            std::string title = j.contains("titleId") && j["titleId"].is_string() ? j["titleId"].get<std::string>() : std::string();
            if (!title.empty() && !std::regex_match(title, title_re)) title.clear();
            std::int64_t when = 0;
            if (j.contains("tick") && j["tick"].is_number_integer()) when = j["tick"].get<std::int64_t>() / 1000000 - 62135596800LL;
            Agg& a = pairs[{code, title}];
            ++a.count;
            a.last = std::max(a.last, when);
            ++total;
        }
        json arr = json::array();
        for (const auto& [key, a] : pairs) {
            const std::string day = a.last > 0 ? iso_from_epoch(a.last).substr(0, 10) : std::string();
            arr.push_back({{"code", key.first}, {"titleid", key.second}, {"count", a.count}, {"last", day}, {"last_epoch", a.last},
                           {"label", key.first + " - " + (key.second.empty() ? std::string("System") : key.second) + " - " + std::to_string(a.count) + "x" + (day.empty() ? std::string() : ", zuletzt " + day)}});
        }
        f.set("errors.total", total);
        f.set("errors.pairs", std::move(arr));
    }
}

// Datenbanken der Systemsoftware (SQLite), Stufe 1: nur Kopf, Groesse und Journal, ohne die Datenbank zu oeffnen. Pfade und Zustand
// auf der Test-Konsole am 10. Nov. 2026 per FTP gelesen: 15 Dateien, alle mit gueltigem Kopf, und die Seitenzahl im Kopf ergibt genau die
// Dateigroesse. SQLite-Dateiformat (sqlite.org/fileformat2.html): Kopf 100 Bytes; Seitengroesse bei 16 (1 = 65536); Nutzlast-Anteile bei
// 21..23 sind immer 64/32/32; Aenderungszaehler bei 24, Seitenzahl bei 28 und "Version-valid-for" bei 92 -- die Seitenzahl im Kopf gilt nur,
// wenn Aenderungszaehler und Version-valid-for gleich sind. Ein Journal ("<db>-journal") mit der Kennung d9 d5 05 f9 20 a1 63 d7 am Anfang
// ist eine unfertige Transaktion; SQLite rollt sie beim naechsten Oeffnen zurueck.
void collect_databases(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    std::vector<std::string> dirs = {"/system_data/priv/mms", "/system_data/priv/license", "/system_data/priv/pbtc", "/system_data/priv/cache/httpcache/nobackup",
                                     "/system_data/priv/system_logger2/nobackup/database"};
    for (const char* root : {"/system_data/savedata_prospero", "/system_data/savedata"})
        for (const auto& u : fs.list_dir(root))
            if (u.is_dir) dirs.push_back(join_path(join_path(root, u.name), "db/user"));
    auto be16 = [](const std::string& s, std::size_t o) { return (static_cast<unsigned>(static_cast<unsigned char>(s[o])) << 8) | static_cast<unsigned char>(s[o + 1]); };
    auto be32 = [&](const std::string& s, std::size_t o) { return (be16(s, o) << 16) | be16(s, o + 2); };
    json files = json::array();
    int count = 0;
    for (const auto& dir : dirs) {
        std::map<std::string, std::uint64_t> sizes;
        for (const auto& e : fs.list_dir(dir))
            if (!e.is_dir) sizes[e.name] = e.size;
        for (const auto& [name, size] : sizes) {
            if (name.size() < 4 || name.compare(name.size() - 3, 3, ".db") != 0) continue;
            if (++count > 64) break;
            const std::string path = join_path(dir, name);
            json d = {{"path", path}, {"name", name}, {"size", size}, {"readable", 0}, {"header_bad", 0}, {"truncated", 0}, {"unaligned", 0},
                      {"page_size", 0}, {"header_pages", 0}, {"journal_hot", 0}, {"journal_size", -1}, {"wal_size", -1}, {"integrity", -1}, {"integrity_note", ""}, {"integrity_skip", ""}};
            std::string head;
            if (fs.read_range(path, 0, 100, head) && head.size() == 100) {
                d["readable"] = 1;
                unsigned page = be16(head, 16);
                if (page == 1) page = 65536;
                const bool page_ok = page >= 512 && page <= 65536 && (page & (page - 1)) == 0;
                const bool magic_ok = head.compare(0, 16, std::string("SQLite format 3\0", 16)) == 0;
                const bool fractions_ok = static_cast<unsigned char>(head[21]) == 64 && static_cast<unsigned char>(head[22]) == 32 && static_cast<unsigned char>(head[23]) == 32;
                d["header_bad"] = magic_ok && page_ok && fractions_ok ? 0 : 1;
                if (!d["header_bad"].get<int>()) {
                    const std::uint64_t pages = be32(head, 28);
                    d["page_size"] = page;
                    d["header_pages"] = pages;
                    d["unaligned"] = size % page != 0 ? 1 : 0;
                    // Die Seitenzahl im Kopf gilt nur, wenn Aenderungszaehler und "Version-valid-for" uebereinstimmen.
                    if (be32(head, 24) == be32(head, 92) && pages > 0 && size < pages * page) d["truncated"] = 1;
                }
            }
            if (const auto j = sizes.find(name + "-journal"); j != sizes.end()) {
                d["journal_size"] = j->second;
                std::string jh;
                if (j->second >= 8 && fs.read_range(path + "-journal", 0, 8, jh) && jh.size() == 8 && jh == std::string("\xd9\xd5\x05\xf9\x20\xa1\x63\xd7", 8)) d["journal_hot"] = 1;
            }
            if (const auto w = sizes.find(name + "-wal"); w != sizes.end()) d["wal_size"] = w->second;
            // Stufe 2: integrity_check auf einer Kopie im Arbeitsspeicher (SQLite ohne Dateizugriff). Nicht geprueft wird bei auffaelligem Kopf, offenem
            // Rollback-Journal, Dateien ueber 32 MiB und wenn sich Datei oder WAL beim zweimaligen Lesen unterscheiden (die Konsole schreibt gerade).
            // Ein WAL mit Eintraegen wird in die Kopie eingerechnet, wie SQLite es beim Oeffnen tut. Der Grund steht in "integrity_skip"; "nicht
            // getestet" ist ein eigener Zustand und heisst nie "gesund".
            constexpr std::uint64_t kMaxIntegrityBytes = 32u << 20;
            const std::int64_t wal_size = d["wal_size"].get<std::int64_t>();
            std::string skip;
            if (d["readable"] != 1) skip = "unlesbar";
            else if (d["header_bad"] == 1 || d["truncated"] == 1 || d["unaligned"] == 1) skip = "kopf";
            else if (d["journal_hot"] == 1) skip = "journal";
            else if (size < 512) skip = "klein";
            else if (size > kMaxIntegrityBytes) skip = "gross";
            else if (wal_size > static_cast<std::int64_t>(kMaxIntegrityBytes)) skip = "wal_gross";
            if (skip.empty()) {
                std::string first, second, wal1, wal2;
                const bool wal_used = wal_size > 32;
                if (!fs.read_file(path, first) || !fs.read_file(path, second) || (wal_used && (!fs.read_file(path + "-wal", wal1) || !fs.read_file(path + "-wal", wal2)))) skip = "unlesbar";
                else if (first != second || wal1 != wal2 || first.size() != size) skip = "aendert_sich";
                else {
                    const SqliteCheck c = sqlite_integrity_check(first, wal1);
                    if (!c.ran) skip = "nicht_startbar";
                    else {
                        d["integrity"] = c.ok ? 1 : 0;
                        if (!c.ok) d["integrity_note"] = c.note;
                    }
                }
            }
            d["integrity_skip"] = skip;
            files.push_back(std::move(d));
        }
    }
    int integrity_tested = 0;
    for (const auto& d : files)
        if (d["integrity"] != -1) ++integrity_tested;
    f.set("db.integrity_tested", integrity_tested);
    f.set("db.count", static_cast<int>(files.size()));
    f.set("db.files", std::move(files));
}

// Spielordner: Dateinamen, die sich nur in Gross-/Kleinschreibung von dem unterscheiden, was das Spiel laedt. UFS (interne SSD, UFS2-Images) und
// nullfs darueber unterscheiden Gross- und Kleinschreibung, exFAT und FAT nicht. Belegt (ShadowMount+ #159 und #173, Eigentuemer: "Rename it" bzw.
// "use exfat or rename files", mehrere Nutzer bestaetigen): Unity-Spiele laden Media/Modules/Il2CppUserAssemblies.prx; liegt dort
// Il2cppUserAssemblies.prx, stuerzt das Spiel gleich nach dem Start ab (SIGSEGV in der eboot.bin, Adresse 0x0). Nur dieser eine Name ist bekannt;
// was ein Spiel sonst laedt, laesst sich von aussen nicht wissen. case_sensitive: 1 = Dateisystem unterscheidet (Befund), 0 = exFAT/FAT (harmlos).
void collect_game_files(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    const auto storage = p.sys().storage();
    auto fstype_of = [&](const std::string& path) {
        std::string best_mount, best_type;
        for (const auto& s : storage)
            if (path.compare(0, s.mount.size(), s.mount) == 0 && (path.size() == s.mount.size() || path[s.mount.size()] == '/' || s.mount == "/") && s.mount.size() >= best_mount.size()) {
                best_mount = s.mount;
                best_type = s.fstype;
            }
        return best_type;
    };
    json mismatches = json::array();
    const json* titles = f.get("smp.titles");
    std::set<std::string> done;
    if (titles && titles->is_array())
        for (const auto& t : *titles) {
            if (!t.is_object() || !t.contains("path") || !t["path"].is_string() || !t.contains("titleid") || !t["titleid"].is_string()) continue;
            const std::string path = t["path"].get<std::string>();
            if (!done.insert(path).second || !fs.is_dir(path)) continue;
            const std::string dir = join_path(path, "Media/Modules");
            for (const auto& e : fs.list_dir(dir)) {
                if (e.is_dir || lower(e.name) != "il2cppuserassemblies.prx" || e.name == "Il2CppUserAssemblies.prx") continue;
                const std::string type = fstype_of(dir);
                const bool insensitive = type == "exfatfs" || type == "msdosfs";
                mismatches.push_back({{"titleid", t["titleid"]}, {"path", join_path(dir, e.name)}, {"expected", "Il2CppUserAssemblies.prx"}, {"actual", e.name},
                                      {"fstype", type}, {"case_sensitive", insensitive ? 0 : 1}});
            }
        }
    f.set("games.case_mismatch", std::move(mismatches));

    // eboot.bin: nur offensichtlich Unbrauchbares. Gueltig sind ELF (7F 45 4C 46) und SELF (4F 15 3D 1D, 54 14 F5 EE); verschluesselte Teile werden
    // nie gelesen. Gemeldet wird nur eine Datei unter 64 Byte oder eine reine Textdatei (z. B. eine gespeicherte Fehlerseite statt der Datei);
    // jedes andere unbekannte Format gilt als "nicht pruefbar" und ist kein Befund.
    json bad = json::array();
    done.clear();
    if (titles && titles->is_array())
        for (const auto& t : *titles) {
            if (!t.is_object() || !t.contains("path") || !t["path"].is_string() || !t.contains("titleid") || !t["titleid"].is_string()) continue;
            const std::string path = t["path"].get<std::string>();
            if (!done.insert(path).second || !fs.is_dir(path)) continue;
            std::uint64_t size = 0;
            bool found = false;
            for (const auto& e : fs.list_dir(path))
                if (!e.is_dir && e.name == "eboot.bin") {
                    size = e.size;
                    found = true;
                }
            if (!found) continue;
            std::string head;
            fs.read_range(join_path(path, "eboot.bin"), 0, 64, head);
            std::string reason;
            if (size < 64) {
                reason = "too_small";
            } else if (head.size() >= 16 && head.compare(0, 4, "\x7f" "ELF") != 0 && head.compare(0, 4, "\x4f\x15\x3d\x1d") != 0 && head.compare(0, 4, "\x54\x14\xf5\xee") != 0) {
                const bool text = std::all_of(head.begin(), head.begin() + 16, [](unsigned char c) { return (c >= 0x20 && c < 0x7f) || c == '\n' || c == '\r' || c == '\t'; });
                if (text) reason = "text";
            }
            if (!reason.empty()) bad.push_back({{"titleid", t["titleid"]}, {"path", join_path(path, "eboot.bin")}, {"size", size}, {"reason", reason}});
        }
    f.set("games.eboot_problems", std::move(bad));
}

// Vergleich mit den eigenen frueheren Messungen (Verlauf der Check-ups, history.hpp): Abweichung der jetzigen Temperatur vom Median der
// bis zu kMetricWindow frueheren Pruefungen, erst ab fuenf frueheren Werten. Die Ids sind die der Messwerte in rules.json.
// sys.temp.cpu_delta, sys.temp.soc_delta (Grad Celsius), baseline.temp_samples
void collect_baseline(const Platform& p, Facts& f) {
    const auto history = load_history(p.fs());
    std::size_t max_samples = 0;
    for (const auto& [metric, fact, delta] : {std::tuple{"cpu_temp", "sys.temp.cpu_c", "sys.temp.cpu_delta"}, {"soc_temp", "sys.temp.soc_c", "sys.temp.soc_delta"}}) {
        const json* now = f.get(fact);
        if (!now || !now->is_number()) continue;
        std::vector<double> past;
        for (auto it = history.rbegin(); it != history.rend() && past.size() < kMetricWindow; ++it)
            if (const auto m = it->metrics.find(metric); m != it->metrics.end()) past.push_back(m->second);
        if (past.size() < 5) continue;
        std::sort(past.begin(), past.end());
        const std::size_t n = past.size();
        const double median = n % 2 ? past[n / 2] : (past[n / 2 - 1] + past[n / 2]) / 2.0;
        f.set(delta, now->get<double>() - median);
        max_samples = std::max(max_samples, n);
    }
    if (max_samples) f.set("baseline.temp_samples", static_cast<long>(max_samples));
}

// Lüfterregler (die eigene App des Entwicklers, PS5 Cooling Center): Sie regelt den Lüfter und haelt die Konsole kuehl; dann sagt ein Waermetest
// wenig ueber die Standardkuehlung. Nur Vorhandensein, Modus und Zieltemperatur aus ihrer config.json (reine Zahlen und Modus).
// fan.controller.name, fan.controller.mode, fan.controller.target_c
void collect_fan_controller(const Platform& p, Facts& f) {
    std::string text;
    if (!p.fs().read_file("/data/PS5-Cooling-Center/config.json", text)) return;
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return;
    f.set("fan.controller.name", std::string("PS5 Cooling Center"));
    if (j.contains("mode") && j["mode"].is_string()) f.set("fan.controller.mode", j["mode"].get<std::string>());
    if (j.contains("target_temp_c") && j["target_temp_c"].is_number()) f.set("fan.controller.target_c", j["target_temp_c"].get<double>());
}

// Kernel-Log (klogsrv, Port 3232; Freigabe von Marc, D10). Das Lesen VERBRAUCHT, was sich seit dem letzten Leser angesammelt hat; ein Check-up
// sieht also den Rueckstau seit dem letzten Lesen (nach dem Start: alles seit dem Start, soweit der Puffer reicht), spaeter nur Neues. "Nichts im
// Kernel-Log" heisst deshalb nie "in Ordnung". Gelesen wird hoechstens 256 KiB in 1,5 s, nichts wird gesendet. Es bleiben nur Zeilen mit
// Fehlerwoertern oder von kstuff bzw. dem Kernel (Warnung/Fehler), ohne Prioritaetsvorsatz, auf 220 Zeichen gekuerzt, ohne Doppelte, hoechstens 120;
// Ausgaben von Payloads ("[payload.elf]", darunter Medic selbst) werden uebersprungen, sonst laese Medic seine eigene Ausgabe.
// klog.readable (1 = Verbindung kam zustande), klog.total_lines, klog.lines[]
void collect_klog(const Platform& p, Facts& f) {
    std::string raw;
    if (!p.sys().read_local_stream(3232, 256u << 10, 1500, raw)) return;
    f.set("klog.readable", 1);
    static const std::regex interesting(R"(error|fail|panic|fatal|crash|assert|\[kstuff\.elf\]|\[KERNEL\] (WARNING|ERROR))", std::regex::icase | std::regex::ECMAScript);
    json lines = json::array();
    std::set<std::string> seen;
    long total = 0;
    for (std::string line : split_lines(raw)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        ++total;
        if (line.size() > 4 && line[0] == '<') {  // Prioritaetsvorsatz "<118>"
            const auto gt = line.find('>');
            if (gt != std::string::npos && gt <= 5) line.erase(0, gt + 1);
        }
        if (line.find("[payload.elf]") != std::string::npos) continue;
        if (!std::regex_search(line, interesting)) continue;
        if (line.size() > 220) line.resize(220);
        if (lines.size() < 120 && seen.insert(line).second) lines.push_back(line);
    }
    f.set("klog.total_lines", total);
    f.set("klog.new_lines", lines);  // nur der Strom dieses Lesens; persist_klog haengt sie an die Datei
    // Zeilen aus fruehren Check-ups desselben Konsolenstarts (Datei von persist_klog) dazunehmen: die neuen kommen zuletzt
    json all = json::array();
    std::set<std::string> have;
    const std::int64_t boot = p.sys().boot_epoch();
    std::string stored;
    if (p.fs().read_file(kKlogBootPath, stored)) {
        const auto old_lines = split_lines(stored);
        if (!old_lines.empty() && old_lines[0] == "# boot " + std::to_string(boot) && boot != 0)
            for (std::size_t i = 1; i < old_lines.size() && all.size() < 200; ++i)
                if (!old_lines[i].empty() && have.insert(old_lines[i]).second) all.push_back(old_lines[i]);
    }
    f.set("klog.persisted_lines", static_cast<long>(all.size()));
    for (const auto& l : lines)
        if (all.size() < 200 && have.insert(l.get<std::string>()).second) all.push_back(l);
    f.set("klog.lines", std::move(all));
}

bool persist_klog(Platform& p, const Facts& facts) {
    IFileSystemRW* rw = p.fs_rw();
    const json* fresh = facts.get("klog.new_lines");
    if (!rw || !fresh || !fresh->is_array() || fresh->empty()) return false;
    const std::int64_t boot = p.sys().boot_epoch();
    if (boot == 0) return false;  // ohne bekannte Startzeit laesst sich nicht sagen, zu welchem Start die Zeilen gehoeren
    const std::string header = "# boot " + std::to_string(boot);
    std::string text;
    std::vector<std::string> have;
    if (p.fs().read_file(kKlogBootPath, text)) {
        const auto lines = split_lines(text);
        if (!lines.empty() && lines[0] == header) {
            have.assign(lines.begin() + 1, lines.end());
        } else if (!lines.empty() && lines[0].rfind("# boot ", 0) == 0) {
            // anderer Konsolenstart: die alte Datei beiseite legen (nie loeschen); gelingt das nicht, wird nichts ueberschrieben
            const std::string old_epoch = lines[0].substr(7);
            if (old_epoch.empty() || old_epoch.find_first_not_of("0123456789") != std::string::npos) return false;
            if (!rw->rename(kKlogBootPath, "/data/ps5medic/klog_boot_" + old_epoch + ".txt")) return false;
        } else {
            return false;  // fremde Datei gleichen Namens: nicht anfassen
        }
    }
    std::set<std::string> seen(have.begin(), have.end());
    std::string out = header + "\n";
    for (const auto& l : have) out += l + "\n";
    bool added = false;
    for (const auto& l : *fresh) {
        if (!l.is_string()) continue;
        const std::string line = l.get<std::string>();
        if (out.size() + line.size() + 1 > kKlogBootMaxBytes) break;
        if (seen.insert(line).second) {
            out += line + "\n";
            added = true;
        }
    }
    return (added || !p.fs().exists(kKlogBootPath)) && rw->write_file(kKlogBootPath, out);
}

// Titelabgleich (Ausnahme von D17, Freigabe von Marc am 11. Nov. 2026): liest aus /system_data/priv/mms/app.db NUR die Spalten titleId und metaDataPath
// der Tabelle tbl_contentinfo (nie Namen, Spielzeiten, Konten oder Kaeufe) auf einer Kopie im Speicher und vergleicht mit den Ordnern /user/app und
// /system_ex/app. Nicht gelesen wird bei offenem Journal, Dateien ueber 32 MiB, wenn sich Datei oder WAL beim zweimaligen Lesen unterscheiden oder die
// Tabelle fehlt (dann bleibt appdb.registered ungesetzt: nicht geprueft).
// appdb.registered, appdb.entries[].{titleid,path}, appdb.orphans[].{titleid,path,dir}, appdb.unregistered[].{titleid,path}
void collect_app_registry(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    const std::string path = "/system_data/priv/mms/app.db";
    if (!fs.exists(path)) return;
    std::string first, second, wal1, wal2, jh;
    std::uint64_t size = 0, wal_size = 0;
    for (const auto& e : fs.list_dir("/system_data/priv/mms")) {
        if (e.name == "app.db") size = e.size;
        if (e.name == "app.db-wal") wal_size = e.size;
    }
    if (size < 512 || size > (32u << 20) || wal_size > (32u << 20)) return;
    if (fs.read_range(path + "-journal", 0, 8, jh) && jh == std::string("\xd9\xd5\x05\xf9\x20\xa1\x63\xd7", 8)) return;  // offenes Journal
    const bool wal_used = wal_size > 32;
    if (!fs.read_file(path, first) || !fs.read_file(path, second) || (wal_used && (!fs.read_file(path + "-wal", wal1) || !fs.read_file(path + "-wal", wal2)))) return;
    if (first != second || wal1 != wal2 || first.size() != size) return;
    const SqliteRows rows = sqlite_select_text(first, wal1, "SELECT titleId, metaDataPath FROM tbl_contentinfo", 2, 2000);
    if (!rows.ran) return;
    json entries = json::array(), orphans = json::array(), unregistered = json::array();
    std::set<std::string> known;  // Ordnernamen und Titel-IDs der Eintraege
    static const std::regex under_app(R"(^(/user/app|/system_ex/app)/([^/]+)/)");
    for (const auto& r : rows.rows) {
        if (entries.size() < 500) entries.push_back({{"titleid", r[0]}, {"path", r[1]}});
        known.insert(r[0]);
        std::smatch m;
        if (std::regex_search(r[1], m, under_app)) {
            known.insert(m[2].str());
            const std::string dir = m[1].str() + "/" + m[2].str();
            if (!fs.is_dir(dir) && orphans.size() < 100) orphans.push_back({{"titleid", r[0]}, {"path", r[1]}, {"dir", dir}});
        }
    }
    static const std::regex game_id(R"(^(PPSA|CUSA)[0-9]{5}$)");
    for (const auto& e : fs.list_dir("/user/app"))
        if (e.is_dir && std::regex_match(e.name, game_id) && !known.count(e.name) && unregistered.size() < 100) unregistered.push_back({{"titleid", e.name}, {"path", "/user/app/" + e.name}});
    f.set("appdb.registered", static_cast<long>(rows.rows.size()));
    f.set("appdb.entries", std::move(entries));
    f.set("appdb.orphans", std::move(orphans));
    f.set("appdb.unregistered", std::move(unregistered));
}

Facts collect_all(const Platform& p, const std::function<void(int, int)>& progress) {
    Facts f;
    json failed = json::array();
    constexpr int kSteps = 23;  // 22 Collectors + Gateway (nur fuer die Fortschrittsanzeige; Abweichungen schaden nicht)
    int done = 0;
    // Jeder Collector einzeln: ein Fehler (z. B. beschaedigtes JSON in einer von Hand bearbeiteten Datei) beendet nicht den ganzen Check-up,
    // sondern wird als collector.failed vermerkt; die uebrigen Fakten bleiben gueltig (Audit 12. Nov. 2026)
    auto run = [&](const char* name, auto&& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            failed.push_back({{"name", name}, {"error", e.what()}});
        } catch (...) {
            failed.push_back({{"name", name}, {"error", "unbekannt"}});
        }
        if (progress) progress(std::min(++done, kSteps), kSteps);
    };
#define MEDIC_RUN(fn) run(#fn, [&] { fn(p, f); })
    MEDIC_RUN(collect_firmware);
    MEDIC_RUN(collect_components);
    MEDIC_RUN(collect_processes);
    MEDIC_RUN(collect_system);
    MEDIC_RUN(collect_storage);
    MEDIC_RUN(collect_shadowmount);
    MEDIC_RUN(collect_plugins);
    MEDIC_RUN(collect_apps);
    MEDIC_RUN(collect_game_firmware);
    MEDIC_RUN(collect_title_names);
    MEDIC_RUN(collect_autoload);
    MEDIC_RUN(collect_payload_files);
    MEDIC_RUN(collect_helpers);
    MEDIC_RUN(collect_savedata);
    MEDIC_RUN(collect_system_logs);
    MEDIC_RUN(collect_databases);
    MEDIC_RUN(collect_game_files);
    MEDIC_RUN(collect_baseline);
    MEDIC_RUN(collect_selftest);
    MEDIC_RUN(collect_fan_controller);
    MEDIC_RUN(collect_klog);
    MEDIC_RUN(collect_app_registry);
#undef MEDIC_RUN
    run("net.gateway", [&] {
        if (const std::string gw = p.sys().default_gateway(); !gw.empty()) f.set("net.gateway", gw);  // fuer den lokalen Netztest des Selbsttests
    });
    f.set("collector.failed", std::move(failed));
    return f;
}

}  // namespace medic
