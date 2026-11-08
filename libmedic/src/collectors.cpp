// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/collectors.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
        // "sce_sys" ist nie eine Quelle: in Ordnern ohne param.json (z. B. PS4-Dumps) enthaelt es .dat-Dateien (npbind.dat),
        // die sonst als Images in grosser Tiefe gezaehlt wuerden (erster Konsolenlauf: falscher B10-Befund).
        if (e.is_dir && (e.name == "_duplicates" || e.name == "_to_delete" || e.name == "backports" || e.name == "sce_sys")) continue;
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
    std::smatch m;
    if (!latest_in(tail, m)) {
        std::string head;
        if (!p.fs().read_range(join_path(kSmpDir, "debug.log"), 0, 16384, head)) return;
        const auto head_lines = split_lines(head);
        if (!latest_in(head_lines, m)) return;
    }
    const std::string bind = m[1].str();
    const long port = std::strtol(m[2].str().c_str(), nullptr, 10);
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
    f.set("smp.default_scan_roots", default_scan_paths());
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

Facts collect_all(const Platform& p) {
    Facts f;
    collect_firmware(p, f);
    collect_components(p, f);
    collect_processes(p, f);
    collect_system(p, f);
    collect_storage(p, f);
    collect_shadowmount(p, f);
    collect_plugins(p, f);
    collect_apps(p, f);
    return f;
}

}  // namespace medic
