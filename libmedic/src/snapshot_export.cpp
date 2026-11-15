// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/snapshot_export.hpp"
#include "medic/jsondump.hpp"

#include "medic/exfat.hpp"
#include "medic/report.hpp"
#include "medic/sqlite_check.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <map>
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
bool is_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

// MAC-Adressen (xx:xx:xx:xx:xx:xx) und IPv4-Adressen im Text unkenntlich machen (D12). Loopback und 0.x bleiben, weil sie nichts verraten.
std::string mask_addresses(const std::string& line) {
    std::string out;
    std::size_t i = 0;
    const std::size_t n = line.size();
    auto word_before = [&](std::size_t at) { return at > 0 && (std::isalnum(static_cast<unsigned char>(line[at - 1])) || line[at - 1] == '.'); };
    while (i < n) {
        // MAC: 6 Paare, getrennt durch ':' oder '-'
        if (i + 17 <= n && is_hex(line[i]) && !word_before(i)) {
            bool mac = true;
            for (std::size_t k = 0; k < 17 && mac; ++k)
                mac = (k % 3 == 2) ? (line[i + k] == ':' || line[i + k] == '-') : is_hex(line[i + k]);
            if (mac && (i + 17 == n || !std::isalnum(static_cast<unsigned char>(line[i + 17])))) {
                out += "**:**:**:**:**:**";
                i += 17;
                continue;
            }
        }
        // IPv4: vier Zahlen 0..255 ohne fuehrende Null, durch Punkte getrennt
        if (is_digit(line[i]) && !word_before(i)) {
            std::size_t j = i;
            int parts = 0, first = -1;
            bool ok = true;
            while (parts < 4 && ok) {
                std::size_t d = j;
                int v = 0;
                while (d < n && is_digit(line[d]) && d - j < 3) v = v * 10 + (line[d++] - '0');
                if (d == j || v > 255 || (line[j] == '0' && d - j > 1)) { ok = false; break; }
                if (parts == 0) first = v;
                ++parts;
                j = d;
                if (parts < 4) {
                    if (j < n && line[j] == '.') ++j;
                    else ok = false;
                }
            }
            const bool dot_digit = j + 1 < n && line[j] == '.' && is_digit(line[j + 1]);  // "1.2.3.4.5": mehr Teile, keine Adresse
            if (ok && parts == 4 && (j == n || (!std::isalnum(static_cast<unsigned char>(line[j])) && !dot_digit)) && first != 127 && first != 0) {
                out += "x.x.x.x";
                i = j;
                continue;
            }
        }
        out += line[i++];
    }
    return redact_ipv6(out);
}

std::string redact_secrets(const std::string& text) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos) {  // auch Kommentarzeilen ("# password = altes"): ein auskommentiertes Passwort ist oft ein echtes
            const std::string key = lower_str(line.substr(0, eq));
            for (const char* secret : {"password", "passwd", "secret", "token", "pass", "pwd", "username", "login", "credential", "auth", "account", "serial", "email", "psnid"})
                if (key.find(secret) != std::string::npos) {
                    const bool cr = !line.empty() && line.back() == '\r';
                    line = line.substr(0, eq + 1) + " ***" + (cr ? "\r" : "");
                    break;
                }
        }
        // Zugangsdaten in Adressen (smb://benutzer:passwort@host/...) auch ausserhalb von "Schluessel = Wert" schwaerzen (Logs)
        for (std::size_t at = line.find("://"); at != std::string::npos; at = line.find("://", at + 3)) {
            const std::size_t start = at + 3;
            const std::size_t end = line.find_first_of("/ \t\r", start);
            const std::size_t stop = end == std::string::npos ? line.size() : end;
            const std::size_t amp = line.rfind('@', stop);
            if (amp != std::string::npos && amp >= start && amp < stop) line.replace(start, amp - start, "***");
        }
        out += mask_addresses(redact_credentials(line));
        if (nl != std::string::npos) out += '\n';
        pos = nl == std::string::npos ? text.size() : nl + 1;
    }
    return out;
}

// param.json eines Spielordners: Titelname(n), Konto- und Seriennummern-Felder maskieren; die Pruefung braucht titleId und die Firmware-Felder.
// Nicht lesbares JSON wird gar nicht kopiert (leer ist sicherer als roh).
bool mask_param_json(const std::string& text, std::string& out) {
    json j = json::parse(text, nullptr, false);
    if (!j.is_object()) return false;
    std::function<void(json&)> walk = [&](json& v) {
        if (v.is_object()) {
            for (auto it = v.begin(); it != v.end(); ++it) {
                const std::string key = lower_str(it.key());
                if (it.value().is_string() && (key.find("titlename") != std::string::npos || key.find("account") != std::string::npos || key.find("serial") != std::string::npos ||
                                               key.find("email") != std::string::npos || key.find("psnid") != std::string::npos))
                    it.value() = "***";
                else
                    walk(it.value());
            }
        } else if (v.is_array()) {
            for (auto& e : v) walk(e);
        }
    };
    walk(j);
    out = dump_safe(j);
    return true;
}

// Ersatz fuer eine exportierte ELF-Datei, der beim Pruefen am PC dasselbe Ergebnis liefert wie das Original auf der Konsole:
// "ok" -> minimaler gueltiger Kopf (keine Programm-Header); sonst die ersten Bytes des Originals (falsche Kennung, zu klein,
// oder Programm-Header jenseits des Dateiendes).
// Ersatz-eboot.bin mit derselben SDK-Version in den Prozessparametern (Aufbau wie ihn collectors.cpp liest: SELF-Kopf, zwei Eintraege,
// ELF-Kopf mit PT_LOAD und PT_SCE_PROCPARAM, Block "ORBI"). Enthaelt sonst nichts. Leer, wenn die Version nicht passt.
std::string stub_eboot_with_sdk(const std::string& fw) {
    int major = 0, minor = 0;
    if (std::sscanf(fw.c_str(), "%d.%d", &major, &minor) != 2 || major < 1 || major > 99 || minor < 0 || minor > 99) return {};
    std::string b(0x1200, '\0');
    auto put = [&b](std::size_t off, std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) b[off + static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xff);
    };
    b.replace(0, 4, "\x4f\x15\x3d\x1d");
    put(0x18, 2, 2);
    put(0x20, 0x10000, 8); put(0x28, 0x1100, 8); put(0x30, 0x20, 8); put(0x38, 0x20, 8);   // Pruefblock
    put(0x40, 0, 8);       put(0x48, 0x1000, 8); put(0x50, 0x100, 8); put(0x58, 0x100, 8);  // Daten des Segments 0
    const std::size_t elf = 0x60, ph = elf + 64;
    b.replace(elf, 4, "\x7f" "ELF");
    put(elf + 32, 64, 8);
    put(elf + 54, 56, 2);
    put(elf + 56, 2, 2);
    put(ph, 1, 4);               put(ph + 8, 0x1000, 8);       put(ph + 32, 0x100, 8);    // PT_LOAD
    put(ph + 56, 0x61000001u, 4); put(ph + 56 + 8, 0x1010, 8);  put(ph + 56 + 32, 0x40, 8);  // PT_SCE_PROCPARAM
    put(0x1010, 0x40, 8);
    b.replace(0x1010 + 8, 4, "ORBI");
    const std::uint32_t sdk = (static_cast<std::uint32_t>((major / 10) << 4 | (major % 10)) << 24) | (static_cast<std::uint32_t>((minor / 10) << 4 | (minor % 10)) << 16) | 0x40u;
    put(0x1010 + 0x14, sdk, 4);
    return b;
}

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

// Kleine PSF-Datei (param.sfo-Format) mit den Feldern, die der Collector liest. ACCOUNT_ID als 16 Hexzeichen.
std::string psf_bytes(const std::string& account_hex, const std::string& titleid, const std::string& dir_name, const std::string& main_title) {
    struct Field {
        const char* key;
        bool binary;
        std::string value;
    };
    std::vector<Field> fields = {{"ACCOUNT_ID", true, account_hex}, {"MAINTITLE", false, main_title}, {"SAVEDATA_DIRECTORY", false, dir_name}, {"TITLE_ID", false, titleid}};
    auto le = [](std::string& out, std::uint32_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) out += static_cast<char>((v >> (8 * i)) & 0xff);
    };
    std::string keys, data, entries;
    for (const auto& f : fields) {
        std::string value;
        if (f.binary) {
            for (std::size_t i = 0; i + 1 < f.value.size(); i += 2) value += static_cast<char>(std::stoi(f.value.substr(i, 2), nullptr, 16));
        } else {
            value = f.value + '\0';
        }
        le(entries, static_cast<std::uint32_t>(keys.size()), 2);
        le(entries, f.binary ? 0x0004 : 0x0204, 2);
        le(entries, static_cast<std::uint32_t>(value.size()), 4);
        le(entries, static_cast<std::uint32_t>(value.size()), 4);
        le(entries, static_cast<std::uint32_t>(data.size()), 4);
        keys += std::string(f.key) + '\0';
        data += value;
    }
    while (keys.size() % 4 != 0) keys += '\0';
    const std::uint32_t key_table = 20 + static_cast<std::uint32_t>(entries.size());
    std::string out("\0PSF", 4);
    le(out, 0x101, 4);
    le(out, key_table, 4);
    le(out, key_table + static_cast<std::uint32_t>(keys.size()), 4);
    le(out, static_cast<std::uint32_t>(fields.size()), 4);
    return out + entries + keys + data;
}

// Zeile des Startverlaufs im Format der Konsole (UTC), z. B. "Mon, 12 Oct 2026 16:52:51 +0000 Shutdown".
std::string vsh_line(std::int64_t epoch, const std::string& msg) {
    const std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tmv{};
    gmtime_r(&t, &tmv);
    char buf[64];
    std::strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S +0000", &tmv);
    return std::string(buf) + " " + msg + "\n";
}

// Ersatz fuer eine SQLite-Datei mit genau den Merkmalen, die der Collector liest: Seitengroesse, Seitenzahl im Kopf (Aenderungszaehler gleich
// "Version-valid-for") und Dateigroesse. Der Inhalt ist leer; es wird nie ein echter Inhalt exportiert.
std::string sqlite_stub(unsigned page, std::uint64_t header_pages, std::uint64_t size) {
    std::string s(static_cast<std::size_t>(std::max<std::uint64_t>(size, 100)), '\0');
    s.replace(0, 16, std::string("SQLite format 3\0", 16));
    auto put32 = [&](std::size_t o, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) s[o + static_cast<std::size_t>(i)] = static_cast<char>((v >> (24 - 8 * i)) & 0xff);
    };
    const unsigned enc = page == 65536 ? 1 : page;
    s[16] = static_cast<char>(enc >> 8);
    s[17] = static_cast<char>(enc & 0xff);
    s[18] = 1;
    s[19] = 1;
    s[21] = 64;
    s[22] = 32;
    s[23] = 32;
    put32(24, 1);
    put32(28, static_cast<std::uint32_t>(header_pages));
    put32(92, 1);
    return s;
}

class Exporter {
public:
    Exporter(const Platform& p, fs::path dest) : p_(p), fs_(p.fs()), dest_(std::move(dest)) {}

    bool write(const std::string& vpath, const std::string& content) {
        fs::path target = dest_ / "fs" / vpath.substr(1);
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (out) {
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            out.close();  // Fehler beim Zurueckschreiben (Datentraeger voll, Stick gezogen) zeigen sich erst hier
        }
        if (!out) {
            ++write_failures;
            return false;
        }
        ++entries;
        return true;
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
                    std::string masked;
                    if (fs_.read_file(param, text) && text.size() <= (64u << 10) && mask_param_json(text, masked)) write(param, masked);
                    else mkdir(join_path(path, "sce_sys"));
                } else if (level < kMaxLevel) {
                    mirror(path, level + 1);
                } else {
                    mkdir(path);
                }
            } else if (stub_images.count(path)) {
                // schon als winziges Image mit Ersatz-eboot.bin geschrieben (siehe oben)
            } else if (lower_ext(e.name) == ".exfat") {
                std::string head;
                fs_.read_range(path, 0, kHeaderBytes, head);
                // Die Datei selbst wird nicht kopiert (Groesse!): Vollstaendige Images bekommen VolumeLength = 1 Sektor, damit der Ersatzkopf
                // (nur der Kopf) nicht als abgeschnitten gilt; abgeschnittene behalten die echte Laenge und bleiben es.
                if (head.size() >= 110 && head.compare(3, 8, "EXFAT   ") == 0) {
                    const unsigned bps = static_cast<unsigned char>(head[108]);
                    std::uint64_t vol = 0;
                    for (int i = 7; i >= 0; --i) vol = (vol << 8) | static_cast<unsigned char>(head[72 + static_cast<std::size_t>(i)]);
                    if (bps >= 9 && bps <= 12 && vol >= 1 && e.size >= (vol << bps))
                        for (int i = 0; i < 8; ++i) head[72 + static_cast<std::size_t>(i)] = i == 0 ? 1 : 0;
                }
                write(path, head.size() > e.size ? head.substr(0, static_cast<std::size_t>(e.size)) : head);
            } else if (lower_ext(e.name) == ".ffpfs" || lower_ext(e.name) == ".ffpfsc" || lower_ext(e.name) == ".ffpkg") {
                write(path, std::string(static_cast<std::size_t>(std::min<std::uint64_t>(e.size, 512)), '\0'));  // nur die Groessenklasse (leer oder nicht) bleibt erhalten
            } else {
                write(path, std::string());
            }
        }
    }

    std::set<std::string> stub_images;  // Images, die schon als Ersatz-Image im Export liegen
    const Platform& p_;
    const IFileSystem& fs_;
    fs::path dest_;
    std::size_t entries = 0;
    std::size_t write_failures = 0;  // Dateien, die nicht (vollstaendig) geschrieben werden konnten
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
    if (const auto* l = facts.get("klog.lines"); l && l->is_array()) {  // der gefilterte Kernel-Log (nur die Zeilen der Fakten), damit der Kern am PC gegen Fixtures prueft
        std::ofstream k(dest / "klog.txt", std::ios::binary);
        for (const auto& line : *l)
            if (line.is_string()) k << mask_addresses(line.get<std::string>()) << "\n";
        k.close();  // Schreibfehler (Datentraeger voll) zeigen sich erst hier, nicht schon im Destruktor
        if (!k) ++ex.write_failures;
    }
    // Modi der Programmdateien ohne Ausfuehrungsrecht (games.noexec[]): ohne sie liefe der Befund am PC nicht (modes.json, siehe platform.hpp)
    if (const auto* ne = facts.get("games.noexec"); ne && ne->is_array()) {
        json modes = json::object();
        for (const auto& g : *ne) {
            if (!g.is_object() || !g.contains("files") || !g["files"].is_array() || !g.contains("modes") || !g["modes"].is_array()) continue;
            for (std::size_t i = 0; i < g["files"].size() && i < g["modes"].size(); ++i)
                if (g["files"][i].is_string() && g["modes"][i].is_number_integer()) {
                    char buf[16];
                    std::snprintf(buf, sizeof buf, "%04o", static_cast<unsigned>(g["modes"][i].get<long>()));
                    modes[g["files"][i].get<std::string>()] = buf;
                }
        }
        if (!modes.empty()) {
            std::ofstream m(dest / "modes.json", std::ios::binary);
            m << dump_safe(modes, 2) << "\n";
            m.close();
            if (!m) ++ex.write_failures;
        }
    }
    {
        std::ofstream a(dest / "system.json", std::ios::binary), b(dest / "procs.json", std::ios::binary);
        a << dump_safe(sys, 2) << "\n";
        b << dump_safe(procs, 2) << "\n";
        a.close();
        b.close();
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
    ex.copy_text("/data/shadowmount/debug.log", /*tail=*/true, /*redact=*/true);
    ex.copy_text("/data/ps5medic/history.jsonl", /*tail=*/true);  // Verlauf der Check-ups (nur Regel-IDs und Zahlen): Grundlage der Vergleiche mit frueheren Messungen
    for (const char* f : {"/data/ps5medic/selftest/last.json", "/data/ps5medic/selftest/running.json"}) ex.copy_text(f, /*tail=*/false);  // Selbsttest: Ergebnis und Absturz-Marker (nur Zahlen und IDs)
    // Marke der Konfigurationsumwandlung von SM+ (nur das Vorhandensein zaehlt; die Sicherung selbst kann Zugangsdaten enthalten)
    if (const auto* v = facts.get("smp.v1_backup_ready"); v && v->is_boolean() && v->get<bool>()) ex.write("/data/shadowmount/config-v1-backup/ready.ini", "");
    // Gleicht config.ini der Sicherung von SM+ (dann setzt SM+ die Umwandlung selbst fort), bekommt die Sicherung im Schnappschuss denselben Inhalt;
    // sonst eine Datei, die sich unterscheidet (ihr Inhalt ist hier nicht von Belang und kann Zugangsdaten enthalten)
    if (const auto* v = facts.get("smp.v1_backup_ready"); v && v->is_boolean() && v->get<bool>()) {
        const auto* same = facts.get("smp.v1_matches_backup");
        std::string cfg;
        if (same && same->is_boolean() && same->get<bool>() && p.fs().read_file("/data/shadowmount/config.ini", cfg)) ex.write("/data/shadowmount/config-v1-backup/config.ini", redact_secrets(cfg));
        else ex.write("/data/shadowmount/config-v1-backup/config.ini", "# andere Sicherung\n");
    }

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
            ex.write("/user/app/" + e["titleid"].get<std::string>() + "/sce_sys/param.json", dump_safe(param));
        }
        // Eingebauter Backport (games.fw[].eboot_sdk): Ersatz-eboot.bin mit derselben SDK-Version im Ordner der Quelle (smp.titles)
        const auto* t = facts.get("smp.titles");
        const auto* im = facts.get("smp.images");
        std::set<std::string> image_paths;
        if (im && im->is_array())
            for (const auto& i : *im)
                if (i.is_object() && i.contains("path") && i["path"].is_string()) image_paths.insert(i["path"].get<std::string>());
        if (t && t->is_array())
            for (const auto& e : *g) {
                if (!e.is_object() || !e.contains("titleid") || !e["titleid"].is_string() || !e.contains("eboot_sdk") || !e["eboot_sdk"].is_string()) continue;
                const std::string stub = stub_eboot_with_sdk(e["eboot_sdk"].get<std::string>());
                if (stub.empty()) continue;
                for (const auto& x : *t)
                    if (x.is_object() && x.contains("titleid") && x["titleid"] == e["titleid"] && x.contains("path") && x["path"].is_string()) {
                        const std::string src = x["path"].get<std::string>();
                        if (!image_paths.count(src)) {
                            ex.write(join_path(src, "eboot.bin"), stub);
                        } else {
                            // Image: ein winziges exFAT-Image mit derselben Cluster-Einteilung, das nur die Ersatz-eboot.bin enthaelt (nur vollstaendige
                            // .exfat-Images; der Kopf-Export in mirror() wuerde es sonst durch den blossen Kopf ersetzen)
                            // .exfat: Cluster-Einteilung des Originals; andere Formate (.ffpkg, .ffpfs, .ffpfsc): feste Einteilung. Der Leser erkennt das
                            // Format am Inhalt, nicht am Namen: das Ersatz-Image ist immer exFAT.
                            std::string raw;
                            unsigned bps = 9, spc = 3;
                            bool ok = true;
                            if (lower_ext(src) == ".exfat") {
                                ok = p.fs().read_range(src, 0, 512, raw) && raw.size() >= 112 && raw.compare(3, 8, "EXFAT   ") == 0;
                                if (ok) {
                                    bps = static_cast<unsigned char>(raw[108]);
                                    spc = static_cast<unsigned char>(raw[109]);
                                }
                            }
                            const std::string ext = lower_ext(src);
                            if (ok && (ext == ".exfat" || ext == ".ffpkg" || ext == ".ffpfs" || ext == ".ffpfsc")) {
                                const std::string image = build_exfat_stub(bps, spc, "eboot.bin", stub);
                                if (!image.empty()) {
                                    ex.write(src, image);
                                    ex.stub_images.insert(src);
                                }
                            }
                        }
                        break;
                    }
            }
    }

    // Link-Dateien von ShadowMount+ (Pfad des Images bzw. Ordners je Titel): damit entsteht der Duplikat-Befund (Ordner und Image
    // desselben Titels) auch am PC aus dem Export.
    if (const auto* l = facts.get("smp.mount_links"); l && l->is_array())
        for (const auto& e : *l) {
            if (!e.is_object() || !e.contains("titleid") || !e["titleid"].is_string() || !e.contains("kind") || !e["kind"].is_string() || !e.contains("path") || !e["path"].is_string()) continue;
            const std::string kind = e["kind"].get<std::string>();
            if (kind != "image" && kind != "folder") continue;
            ex.write("/user/app/" + e["titleid"].get<std::string>() + "/" + (kind == "image" ? "mount_img.lnk" : "mount.lnk"), e["path"].get<std::string>());
        }

    // Spielstaende mit fremder Konto-ID (Befund savedata.foreign_account): nur die kleinen Metadaten (PSF), mit Platzhalter statt der
    // echten Konto-IDs (Konto-IDs gehoeren nicht in einen Export); das Ergebnis am PC bleibt dasselbe, weil nur "gleich/verschieden" zaehlt.
    if (const auto* l = facts.get("savedata.foreign"); l && l->is_array()) {
        std::map<std::string, std::string> placeholder;  // echte ID -> Platzhalter
        std::set<std::string> memory_written;
        for (const auto& e : *l) {
            auto str = [&](const char* k) { return e.is_object() && e.contains(k) && e[k].is_string() ? e[k].get<std::string>() : std::string(); };
            const std::string path = str("path"), titleid = str("titleid"), name = str("name"), uid = str("uid");
            if (path.empty() || titleid.empty() || name.empty() || uid.empty() || !safe_name(uid) || !safe_name(titleid)) continue;
            auto it = placeholder.emplace(str("account"), std::string()).first;
            if (it->second.empty()) {
                const std::size_t n = placeholder.size() + 1;  // 2, 3, ...: die 1 gehoert dem aktuellen Konto
                if (n > 255) continue;
                char buf[24];
                std::snprintf(buf, sizeof buf, "%02X00000000000000", static_cast<unsigned>(n));  // 0200..., 0300...: immer genau 16 Zeichen
                it->second = buf;
            }
            ex.write(path, psf_bytes(it->second, titleid, name, "Gespeicherte Daten"));  // der echte Spielstand-Titel bleibt aussen vor
            const std::string dir = "/user/home/" + uid + "/savedata_prospero_meta/user/" + titleid;
            if (memory_written.insert(uid).second) ex.write(dir + "/sce_bu_sce_sdmemory.sfo", psf_bytes("0100000000000000", titleid, "sce_sdmemory", "Gespeicherte Daten"));
        }
    }

    // Startverlauf und Fehlerverlauf: nur das, was die Befunde brauchen (Zeitpunkte der Neustarts ohne Herunterfahren, wiederholte Fehler je Titel).
    if (const auto* u = facts.get("boot.unclean"); u && u->is_array()) {
        std::string log;
        std::int64_t last_epoch = 0;
        for (const auto& e : *u) {
            if (!e.is_object() || !e.contains("epoch") || !e["epoch"].is_number_integer()) continue;
            const std::int64_t t = e["epoch"].get<std::int64_t>();
            const std::string cause = e.contains("cause") && e["cause"].is_string() ? e["cause"].get<std::string>() : std::string("MAIN_SOC");
            log += vsh_line(t - 7200, "Boot up (build#1, MAIN_SOC)");
            log += vsh_line(t, "Boot up (build#1, " + cause + ")");
            log += vsh_line(t + 60, "Shutdown");
            last_epoch = std::max(last_epoch, t + 60);
        }
        if (const auto* le = facts.get("boot.log_last_epoch"); le && le->is_number_integer()) {
            const std::int64_t t = le->get<std::int64_t>();
            if (t > last_epoch) log += vsh_line(t, "Shutdown");
        }
        if (!log.empty()) ex.write("/system_data/priv/vshlog/vshlog.0.txt", log);
    }
    if (const auto* l = facts.get("errors.pairs"); l && l->is_array()) {
        int n = 0;
        for (const auto& e : *l) {
            if (!e.is_object() || !e.contains("code") || !e["code"].is_string() || !e.contains("count") || !e["count"].is_number_integer()) continue;
            const std::string title = e.contains("titleid") && e["titleid"].is_string() ? e["titleid"].get<std::string>() : std::string();
            const std::int64_t when = e.contains("last_epoch") && e["last_epoch"].is_number_integer() ? e["last_epoch"].get<std::int64_t>() : 0;
            const int count = std::min<int>(e["count"].get<int>(), 100);
            for (int i = 0; i < count && n < 400; ++i, ++n) {
                char name[32];
                std::snprintf(name, sizeof name, "%08X.json", n);
                json j = {{"shortError", e["code"]}, {"titleId", title}, {"tick", (when + 62135596800LL) * 1000000LL}};
                ex.write(std::string("/system_data/priv/error/history/") + name, dump_safe(j));
            }
        }
    }

    // Datenbanken mit Befund (Kopf kaputt, abgeschnitten, unfertiges Journal): nur Ersatzdateien mit denselben Merkmalen, nie der Inhalt.
    if (const auto* l = facts.get("db.files"); l && l->is_array()) {
        for (const auto& e : *l) {
            auto num = [&](const char* k) { return e.is_object() && e.contains(k) && e[k].is_number_integer() ? e[k].get<std::int64_t>() : std::int64_t(0); };
            if (!e.is_object() || !e.contains("path") || !e["path"].is_string()) continue;
            const std::string path = e["path"].get<std::string>();
            const bool bad = num("header_bad") != 0, trunc = num("truncated") != 0, unaligned = num("unaligned") != 0, hot = num("journal_hot") != 0;
            if (num("integrity") == 0 && num("readable") != 0) {  // Inhalt beschaedigt: nur eine synthetische beschaedigte Datenbank, nie der echte Inhalt
                const std::string sample = sqlite_broken_sample();
                if (!sample.empty()) ex.write(path, sample);
                continue;
            }
            if (!(bad || trunc || unaligned || hot) || num("readable") == 0) continue;
            const std::uint64_t size = static_cast<std::uint64_t>(num("size"));
            if (size > (8u << 20)) {
                ex.warnings.push_back("Datenbank zu gross fuer den Ersatz, uebersprungen: " + path);
                continue;
            }
            if (bad) {
                ex.write(path, std::string(100, '\0'));
                continue;
            }
            const unsigned page = static_cast<unsigned>(num("page_size"));
            ex.write(path, sqlite_stub(page, static_cast<std::uint64_t>(num("header_pages")), size));
            if (hot) ex.write(path + "-journal", std::string("\xd9\xd5\x05\xf9\x20\xa1\x63\xd7", 8) + std::string(504, '\0'));
        }
    }

    // Dateien mit abweichender Gross-/Kleinschreibung (Befund game.il2cpp_case): nur die leere Datei unter ihrem echten Namen
    if (const auto* l = facts.get("games.case_mismatch"); l && l->is_array())
        for (const auto& e : *l)
            if (e.is_object() && e.contains("path") && e["path"].is_string()) ex.write(e["path"].get<std::string>(), std::string());

    // Titelregister (app.db): eine echte kleine Datenbank mit denselben titleId/metaDataPath-Zeilen (ueberschreibt den Ersatzkopf von oben) und die Ordner,
    // die es auf der Konsole gibt (leere Ordner), damit der Abgleich am PC dasselbe Ergebnis hat.
    if (const auto* en = facts.get("appdb.entries"); en && en->is_array()) {
        std::vector<std::pair<std::string, std::string>> rows;
        for (const auto& e : *en)
            if (e.is_object() && e.contains("titleid") && e["titleid"].is_string() && e.contains("path") && e["path"].is_string()) {
                rows.emplace_back(e["titleid"].get<std::string>(), e["path"].get<std::string>());
                static const std::regex under_app(R"(^(/user/app|/system_ex/app)/([^/]+)/)");
                std::smatch m;
                const std::string path = e["path"].get<std::string>();
                if (std::regex_search(path, m, under_app) && p.fs().is_dir(m[1].str() + "/" + m[2].str())) ex.mkdir(m[1].str() + "/" + m[2].str());
            }
        if (const auto* un = facts.get("appdb.unregistered"); un && un->is_array())
            for (const auto& e : *un)
                if (e.is_object() && e.contains("path") && e["path"].is_string()) ex.mkdir(e["path"].get<std::string>());
        const std::string db = sqlite_make_registry(rows);
        if (!db.empty()) ex.write("/system_data/priv/mms/app.db", db);
    }

    // eboot.bin-Probleme (Befund game.eboot_invalid): Ersatzdatei mit demselben Befund
    if (const auto* l = facts.get("games.eboot_problems"); l && l->is_array())
        for (const auto& e : *l)
            if (e.is_object() && e.contains("path") && e["path"].is_string() && e.contains("reason") && e["reason"].is_string())
                ex.write(e["path"].get<std::string>(), e["reason"].get<std::string>() == "text" ? std::string(64, 'x') : std::string());

    // Scanpfade (wirksame und Standard, damit auch "versteckte" Spiele sichtbar bleiben) und gemountete Images
    std::vector<std::string> roots = strings_of(facts, "smp.scan_roots");
    for (auto& d : strings_of(facts, "smp.default_scan_roots")) roots.push_back(d);
    roots.push_back("/mnt/shadowmnt");
    roots.push_back("/data/pldmgr/payloads");  // nur die Namen der Payload-Ordner (fuer die Autoload-Pruefung)
    for (const auto& root : roots)
        if (p.fs().is_dir(root)) ex.mirror(root, 1);
    ex.copy_text("/data/pldmgr/payloads/.layout", /*tail=*/false);  // nach dem Spiegeln: dieses schreibt sonst eine leere Datei darueber

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
    {
        std::ofstream m(dest / "export.json", std::ios::binary);
        m << dump_safe(meta, 2) << "\n";
        m.close();
        if (!m) ++ex.write_failures;
    }

    r.entries = ex.entries;
    r.warnings = ex.warnings;
    r.ok = ex.write_failures == 0;
    if (!r.ok) r.error = std::to_string(ex.write_failures) + " Dateien konnten nicht geschrieben werden (Datentraeger voll oder Stick entfernt?); der Export ist unvollstaendig";
    return r;
}

}  // namespace medic
