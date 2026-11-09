// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/snapshot_export.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
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

std::string lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Schluessel mit Zugangsdaten (SM+ speichert SMB-Passwoerter im Klartext in config.ini, profiles.ini, config-v1-backup/) werden
// vor dem Export geschwaerzt: ein Snapshot wird zum Teilen gedacht.
std::string redact_secrets(const std::string& text) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        const std::size_t eq = line.find('=');
        const std::size_t first = line.find_first_not_of(" \t");
        const bool comment = first != std::string::npos && (line[first] == '#' || line[first] == ';');
        if (eq != std::string::npos && !comment) {
            const std::string key = lower_str(line.substr(0, eq));
            for (const char* secret : {"password", "passwd", "secret", "token"})
                if (key.find(secret) != std::string::npos) {
                    const bool cr = !line.empty() && line.back() == '\r';
                    line = line.substr(0, eq + 1) + " ***" + (cr ? "\r" : "");
                    break;
                }
        }
        out += line;
        if (nl != std::string::npos) out += '\n';
        pos = nl == std::string::npos ? text.size() : nl + 1;
    }
    return out;
}

// Ersatz fuer eine exportierte ELF-Datei, der beim Pruefen am PC dasselbe Ergebnis liefert wie das Original auf der Konsole:
// "ok" -> minimaler gueltiger Kopf (keine Programm-Header); sonst die ersten Bytes des Originals (falsche Kennung, zu klein,
// oder Programm-Header jenseits des Dateiendes).
std::string elf_stub(const std::string& state, const std::string& real_head) {
    if (state != "ok") return real_head;
    std::string h(64, '\0');
    h.replace(0, 4, "\x7f" "ELF");
    h[4] = 2;  // ELFCLASS64
    h[5] = 1;  // little endian
    h[6] = 1;
    h[0x36] = 56;  // e_phentsize, e_phnum = 0
    return h;
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

    // Kleine Textdatei kopieren; bei Logs nur das Ende. redact: Zugangsdaten schwaerzen.
    void copy_text(const std::string& vpath, bool tail, bool redact = false) {
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
        write(vpath, redact ? redact_secrets(data) : data);
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
    for (const auto& s : p.sys().storage()) {
        json e = {{"mount", s.mount}, {"total", s.total_bytes}, {"free", s.free_bytes}};
        if (!s.fstype.empty()) e["fstype"] = s.fstype;
        if (s.read_only) e["ro"] = true;
        sys["storage"].push_back(std::move(e));
    }
    if (const auto t = p.sys().thermal(); t.cpu_c >= 0 || t.soc_c >= 0) {
        if (t.cpu_c >= 0) sys["temp"]["cpu"] = t.cpu_c;
        if (t.soc_c >= 0) sys["temp"]["soc"] = t.soc_c;
    }
    for (const auto& [name, props] : p.sys().components())
        for (const auto& [k, v] : props) sys["components"][name][k] = v;
    if (const auto boot = p.sys().boot_epoch(); boot > 0) sys["boot"] = boot;
    if (const auto* v = facts.get("net.port_9021"); v && v->is_string()) sys["ports"]["9021"] = v->get<std::string>() == "open";
    {  // Port der ShadowMount+-API samt Zustand, damit der Snapshot am PC dasselbe Ergebnis liefert
        const auto* port = facts.get("smp.api.port");
        const auto* state = facts.get("smp.api.state");
        if (port && port->is_string() && state && state->is_string()) sys["ports"][port->get<std::string>()] = state->get<std::string>() == "open";
    }
    json procs = json::array();
    for (const auto& i : p.procs().running_info()) {
        if (i.start_epoch > 0) procs.push_back({{"name", i.name}, {"start", i.start_epoch}});
        else procs.push_back(i.name);
    }
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
    for (const char* f : {"/data/shadowmount/config.ini", "/data/shadowmount/autotune.ini", "/data/shadowmount/profiles.ini"})
        ex.copy_text(f, /*tail=*/false, /*redact=*/true);
    for (const char* f : {"/data/shadowmount/manual.lst", "/data/ps5_autoloader/autoload.txt", "/data/pldmgr/autoload.txt", "/data/pldmgr/pldmgr_config.txt"})
        ex.copy_text(f, /*tail=*/false);
    ex.copy_text("/data/shadowmount/debug.log", /*tail=*/true);
    // Marke der Konfigurationsumwandlung von SM+ (nur das Vorhandensein zaehlt; die Sicherung selbst kann Zugangsdaten enthalten)
    if (const auto* v = facts.get("smp.v1_backup_ready"); v && v->is_boolean() && v->get<bool>()) ex.write("/data/shadowmount/config-v1-backup/ready.ini", "");

    // nanoDNS: nur die gezaehlten Fehlerzeilen, ohne Domains und Adressen (das echte Log enthaelt jede Abfrage).
    if (const auto* up = facts.get("nanodns.log.upstream_failed"); up && up->is_number_integer()) {
        std::string log = "NanoDNS (Export: nur Fehlerzeilen, ohne Domains)\n";
        auto n_of = [&](const char* k) { const auto* v = facts.get(k); return v && v->is_number_integer() ? std::min<long>(v->get<long>(), 1000) : 0L; };
        for (long i = 0; i < n_of("nanodns.log.upstream_failed"); ++i) log += "[nanodns] all upstreams failed for x\n";
        for (long i = 0; i < n_of("nanodns.log.bind_failed"); ++i) log += "[nanodns] bind(0.0.0.0:53) failed: x\n";
        for (long i = 0; i < n_of("nanodns.log.config_warnings"); ++i) log += "[nanodns] warning: x\n";
        ex.write("/data/nanodns/nanodns.log", log);
    }
    // etaHEN: nur die ausgewerteten Schalter und der Absturzvermerk.
    {
        std::string cfg;
        for (auto [fact, key] : {std::pair{"etahen.game_rest_kill", "Game_rest_kill"}, {"etahen.util_rest_kill", "Util_rest_kill"}})
            if (const auto* v = facts.get(fact); v && v->is_number_integer()) cfg += std::string(key) + "=" + std::to_string(v->get<int>()) + "\n";
        if (!cfg.empty()) ex.write("/data/etaHEN/config.ini", cfg);
        if (const auto* c = facts.get("etahen.crashed"); c && c->is_boolean() && c->get<bool>()) ex.write("/data/etaHEN/etaHEN.log", "main etaHEN has crashed\n");
    }

    // ps5-theme: nur die erkannte Version als Startzeile (das echte Log kann Pfade eigener Bilder enthalten).
    // /user/app wird nur verkleinert exportiert (siehe unten): der Befund storage.oversized_reservation vergleicht Reservierung und
    // Dateigroesse, und der Export enthaelt nur leere Dateien. Aus einem Export darf deshalb kein solcher Befund entstehen.
    if (const auto* v = facts.get("ps5theme.version"); v && v->is_string())
        ex.write("/data/ps5-theme/logs/log.txt", "ps5-theme " + v->get<std::string>() + " starting\n");

    // Spiele unter /user/app: nur Titel-ID und verlangte Firmware (ohne Namen, ohne downloadDataSize), fuer den Befund game.needs_backport.
    if (const auto* g = facts.get("games.fw"); g && g->is_array()) {
        auto encode = [](const std::string& fw) {  // "13.60" -> "0x1360000000000000"
            int major = 0, minor = 0;
            if (std::sscanf(fw.c_str(), "%d.%d", &major, &minor) != 2 || major < 1 || major > 99 || minor < 0 || minor > 99) return std::string();
            char buf[32];
            std::snprintf(buf, sizeof buf, "0x%02d%02d000000000000", major, minor);
            return std::string(buf);
        };
        for (const auto& e : *g) {
            if (!e.is_object() || !e.contains("titleid") || !e["titleid"].is_string() || !e.contains("required_fw") || !e["required_fw"].is_string()) continue;
            const std::string required = encode(e["required_fw"].get<std::string>());
            if (required.empty()) continue;
            json param = {{"titleId", e["titleid"]}, {"requiredSystemSoftwareVersion", required}};
            if (e.contains("sdk_fw") && e["sdk_fw"].is_string())
                if (const std::string sdk = encode(e["sdk_fw"].get<std::string>()); !sdk.empty()) param["sdkVersion"] = sdk;
            ex.write("/user/app/" + e["titleid"].get<std::string>() + "/sce_sys/param.json", param.dump());
        }
    }

    // Scanpfade (wirksame und Standard, damit auch "versteckte" Spiele sichtbar bleiben) und gemountete Images
    std::vector<std::string> roots = strings_of(facts, "smp.scan_roots");
    for (auto& d : strings_of(facts, "smp.default_scan_roots")) roots.push_back(d);
    roots.push_back("/mnt/shadowmnt");
    roots.push_back("/data/pldmgr/payloads");  // nur die Namen der Payload-Ordner (fuer die Autoload-Pruefung)
    for (const auto& root : roots)
        if (p.fs().is_dir(root)) ex.mirror(root, 1);

    // ELF-Dateien: Ersatzdateien mit demselben Pruefergebnis (siehe elf_stub), nicht die Payloads selbst.
    if (const auto* files = facts.get("payload.elf_files"); files && files->is_array()) {
        for (const auto& e : *files) {
            if (!e.is_object() || !e.contains("path") || !e["path"].is_string() || !e.contains("state") || !e["state"].is_string()) continue;
            const std::string path = e["path"].get<std::string>();
            std::string head;
            p.fs().read_range(path, 0, 64, head);
            ex.write(path, elf_stub(e["state"].get<std::string>(), head));
        }
    }

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
