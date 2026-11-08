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

// ---- ShadowMount+ -------------------------------------------------------

constexpr const char* kSmpConfig = "/data/shadowmount/config.ini";

// Standard-Scanpfade laut docs/ARCHITECTURE.md. Eigene scanpath=-Zeilen ersetzen sie komplett.
const std::vector<std::string> kDefaultScanPaths = {
    "/data/homebrew", "/data/etaHEN/games",
    "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3", "/mnt/usb4", "/mnt/usb5", "/mnt/usb6", "/mnt/usb7",
    "/mnt/ext0", "/mnt/ext1"};

const std::set<std::string> kImageExts = {".ffpfs", ".ffpfsc", ".exfat", ".dat"};

constexpr int kMaxScanDepth = 3;  // Ebenen unterhalb eines Scanpfads, nur zur Faktensammlung

struct Config {
    std::map<std::string, std::string> values;  // letzter Wert gewinnt
    std::vector<std::string> scanpaths;         // alle scanpath=-Zeilen in Reihenfolge
};

Config parse_ini(const std::string& text) {
    Config c;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        if (key == "scanpath") {
            if (!val.empty()) c.scanpaths.push_back(val);
        } else {
            c.values[key] = val;
        }
    }
    return c;
}

// PPSA12345 in einem Namen finden.
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

struct Source {
    std::string path, titleid;
};

void scan_dir(const IFileSystem& fs, const std::string& dir, int depth, json& images, std::vector<Source>& sources) {
    for (const auto& e : fs.list_dir(dir)) {
        const std::string path = join_path(dir, e.name);
        if (e.is_dir) {
            // Spielordner: enthaelt sce_sys/param.json. Dann nicht weiter hinein.
            if (fs.exists(join_path(path, "sce_sys/param.json"))) {
                std::string id = title_id_from_param(fs, path);
                if (id.empty()) id = title_id_from_name(e.name);
                sources.push_back({path, id});
            } else if (depth < kMaxScanDepth) {
                scan_dir(fs, path, depth + 1, images, sources);
            }
        } else {
            std::string ext = extension_of(e.name);
            if (!kImageExts.count(ext)) continue;
            images.push_back({{"name", e.name}, {"ext", ext}, {"path", path}, {"size", e.size}});
            sources.push_back({path, title_id_from_name(e.name)});
        }
    }
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

void collect_shadowmount(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();

    std::string cfg_text;
    const bool has_cfg = fs.read_file(kSmpConfig, cfg_text);
    f.set("smp.config_present", has_cfg);
    Config cfg = parse_ini(cfg_text);
    json cfg_json = json::object();
    for (auto& [k, v] : cfg.values) cfg_json[k] = v;
    f.set("smp.config", std::move(cfg_json));
    f.set("smp.scanpaths_custom", cfg.scanpaths);

    const std::vector<std::string>& roots = cfg.scanpaths.empty() ? kDefaultScanPaths : cfg.scanpaths;
    json images = json::array();
    std::vector<Source> sources;
    for (auto& root : roots)
        if (fs.is_dir(root)) scan_dir(fs, root, 0, images, sources);
    f.set("smp.images", std::move(images));

    // Quellen mit TITLE_ID, inkl. Anzahl gleicher IDs (fuer B8).
    std::map<std::string, int> count;
    for (auto& s : sources)
        if (!s.titleid.empty()) ++count[s.titleid];
    json titles = json::array();
    for (auto& s : sources) {
        if (s.titleid.empty()) continue;
        titles.push_back({{"titleid", s.titleid}, {"path", s.path}, {"dup_count", count[s.titleid]}});
    }
    f.set("smp.titles", std::move(titles));
}

Facts collect_all(const Platform& p) {
    Facts f;
    collect_firmware(p, f);
    collect_processes(p, f);
    collect_storage(p, f);
    collect_shadowmount(p, f);
    return f;
}

}  // namespace medic
