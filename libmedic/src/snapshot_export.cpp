#include "medic/snapshot_export.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace medic {
namespace {

using json = nlohmann::json;

constexpr std::size_t kMaxEntriesPerDir = 5000;
constexpr std::size_t kMaxEntriesTotal = 50000;
constexpr std::uint64_t kMaxTextBytes = 1u << 20;  // 1 MiB; Logs: nur das Ende
constexpr std::size_t kHeaderBytes = 512;
constexpr int kMaxLevel = 4;  // wie der Collector (kMaxScanLevel)

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
std::string lower_ext(const std::string& name) {
    auto pos = name.rfind('.');
    if (pos == std::string::npos || pos == 0) return {};
    std::string e = name.substr(pos);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return std::tolower(c); });
    return e;
}
bool safe_name(const std::string& n) {
    return !n.empty() && n != "." && n != ".." && n.find('/') == std::string::npos && n.find(char(92)) == std::string::npos;
}

class Exporter {
public:
    Exporter(const Platform& p, fs::path dest) : p_(p), fs_(p.fs()), dest_(std::move(dest)) {}

    bool write(const std::string& vpath, const std::string& content) {
        fs::path target = dest_ / "fs" / vpath.substr(1);
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        ++entries;
        return static_cast<bool>(out);
    }
    void mkdir(const std::string& vpath) {
        std::error_code ec;
        fs::create_directories(dest_ / "fs" / vpath.substr(1), ec);
        ++entries;
    }

    // Kleine Textdatei kopieren; bei Logs nur das Ende.
    void copy_text(const std::string& vpath, bool tail) {
        if (!fs_.exists(vpath) || fs_.is_dir(vpath)) return;
        std::uint64_t size = 0;
        for (const auto& e : fs_.list_dir(dir_of(vpath)))
            if (e.name == base_of(vpath)) size = e.size;
        std::string data;
        if (size > kMaxTextBytes) {
            if (!tail) {
                warnings.push_back("Datei zu gross, uebersprungen: " + vpath);
                return;
            }
            if (!fs_.read_range(vpath, size - kMaxTextBytes, kMaxTextBytes, data)) return;
            warnings.push_back("Nur das Ende exportiert: " + vpath);
        } else if (!fs_.read_file(vpath, data)) {
            return;
        }
        write(vpath, data);
    }

    // Spiegelt einen Verzeichnisbaum: Namen als leere Dateien, Spielordner nur mit sce_sys/param.json.
    void mirror(const std::string& vdir, int level) {
        if (visited_.count(vdir) || total_ >= kMaxEntriesTotal) return;
        visited_.insert(vdir);
        auto entries_here = fs_.list_dir(vdir);
        if (entries_here.size() > kMaxEntriesPerDir) {
            warnings.push_back("Verzeichnis abgeschnitten (" + std::to_string(entries_here.size()) + " Eintraege): " + vdir);
            entries_here.resize(kMaxEntriesPerDir);
        }
        mkdir(vdir);
        for (const auto& e : entries_here) {
            if (!safe_name(e.name) || total_ >= kMaxEntriesTotal) continue;
            const std::string path = join_path(vdir, e.name);
            ++total_;
            if (e.is_dir) {
                const std::string param = join_path(path, "sce_sys/param.json");
                if (fs_.exists(param)) {  // Spielordner: nur param.json
                    std::string text;
                    if (fs_.read_file(param, text) && text.size() <= (64u << 10)) write(param, text);
                    else mkdir(join_path(path, "sce_sys"));
                } else if (level < kMaxLevel) {
                    mirror(path, level + 1);
                } else {
                    mkdir(path);
                }
            } else if (lower_ext(e.name) == ".exfat") {
                std::string head;
                fs_.read_range(path, 0, kHeaderBytes, head);
                write(path, head);
            } else {
                write(path, std::string());
            }
        }
    }

    const Platform& p_;
    const IFileSystem& fs_;
    fs::path dest_;
    std::size_t entries = 0;
    std::vector<std::string> warnings;

private:
    std::set<std::string> visited_;
    std::size_t total_ = 0;
};

std::vector<std::string> strings_of(const Facts& f, const char* path) {
    std::vector<std::string> v;
    if (const auto* a = f.get(path); a && a->is_array())
        for (const auto& s : *a)
            if (s.is_string()) v.push_back(s.get<std::string>());
    return v;
}

}  // namespace

ExportResult export_snapshot(const Platform& p, const Facts& facts, const std::string& dest_dir, const std::string& timestamp) {
    ExportResult r;
    fs::path dest(dest_dir);
    std::error_code ec;
    if (fs::exists(dest, ec) && !fs::is_empty(dest, ec)) {
        r.error = "Zielordner existiert und ist nicht leer: " + dest_dir;
        return r;
    }
    fs::create_directories(dest / "fs", ec);
    if (ec) {
        r.error = "Zielordner kann nicht angelegt werden: " + dest_dir;
        return r;
    }

    Exporter ex(p, dest);

    // system.json und procs.json
    json sys = {{"fw", p.sys().firmware()}, {"model", p.sys().model()}, {"storage", json::array()}, {"components", json::object()}};
    for (const auto& s : p.sys().storage()) sys["storage"].push_back({{"mount", s.mount}, {"total", s.total_bytes}, {"free", s.free_bytes}});
    for (const auto& [name, props] : p.sys().components())
        for (const auto& [k, v] : props) sys["components"][name][k] = v;
    json procs = json::array();
    for (const auto& n : p.procs().running()) procs.push_back(n);
    {
        std::ofstream a(dest / "system.json", std::ios::binary), b(dest / "procs.json", std::ios::binary);
        a << sys.dump(2) << "\n";
        b << procs.dump(2) << "\n";
        if (!a || !b) {
            r.error = "system.json/procs.json konnten nicht geschrieben werden";
            return r;
        }
    }

    // Konfigurations- und Logdateien
    for (const char* f : {"/data/shadowmount/config.ini", "/data/shadowmount/autotune.ini", "/data/shadowmount/profiles.ini",
                          "/data/shadowmount/manual.lst", "/data/ps5_autoloader/autoload.txt", "/data/pldmgr/autoload.txt"})
        ex.copy_text(f, /*tail=*/false);
    ex.copy_text("/data/shadowmount/debug.log", /*tail=*/true);

    // Scanpfade (wirksame und Standard, damit auch "versteckte" Spiele sichtbar bleiben) und gemountete Images
    std::vector<std::string> roots = strings_of(facts, "smp.scan_roots");
    for (auto& d : strings_of(facts, "smp.default_scan_roots")) roots.push_back(d);
    roots.push_back("/mnt/shadowmnt");
    for (const auto& root : roots)
        if (p.fs().is_dir(root)) ex.mirror(root, 1);

    json meta = {{"tool", "ps5-medic snapshot export"}, {"created", timestamp}, {"entries", ex.entries}, {"warnings", ex.warnings},
                 {"note", "Enthaelt Datei- und Ordnernamen. Vor dem Teilen pruefen."}};
    std::ofstream m(dest / "export.json", std::ios::binary);
    m << meta.dump(2) << "\n";

    r.ok = true;
    r.entries = ex.entries;
    r.warnings = ex.warnings;
    return r;
}

}  // namespace medic
