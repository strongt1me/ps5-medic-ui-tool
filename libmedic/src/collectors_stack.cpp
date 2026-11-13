// SPDX-License-Identifier: GPL-3.0-or-later
// Collectors fuer den Jailbreak-Stack ausserhalb von ShadowMount+: Autoload-Listen (Payload Manager, WebKit-Autoloader),
// ELF-Dateien (abgebrochene Uebertragung), nanoDNS-Log, etaHEN. Nur lesen.
#include "medic/collectors.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
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

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) out.push_back(std::move(line));
    return out;
}

constexpr std::size_t kMaxLogLines = 20000;

// ---- Autoload-Listen -----------------------------------------------------------------------------------------------

constexpr const char* kPldmgrDir = "/data/pldmgr";
constexpr const char* kPldmgrList = "/data/pldmgr/autoload.txt";
constexpr const char* kLoaderDir = "/data/ps5_autoloader";
constexpr const char* kLoaderList = "/data/ps5_autoloader/autoload.txt";

// Payload Manager (src/autoload.c, config.c; Quelltext 0.5.1 und 0.6.x): "!<ms>" = Wartezeit, jede andere Zeile ein Eintrag;
// "#" ist dort KEIN Kommentar. Die Liste muss strikt sein, sonst startet die ganze Sequenz nicht.
constexpr std::size_t kPldmgrMaxBytes = 4095;
constexpr std::size_t kPldmgrMaxLine = 511;
constexpr long kPldmgrMaxDelayMs = 3600000;

// "/a/b/../c" -> "/a/c" (nur Text; Eintraege wie "../etaHEN/etaHEN.elf" zeigen aus dem Autoloader-Ordner hinaus, die Dateisicht lehnt ".." ab).
std::string resolve_dots(const std::string& path) {
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i <= path.size()) {
        std::size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        const std::string seg = path.substr(i, j - i);
        if (seg == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!seg.empty() && seg != ".") {
            parts.push_back(seg);
        }
        i = j + 1;
    }
    std::string out;
    for (const auto& s : parts) out += "/" + s;
    return out.empty() ? "/" : out;
}

void add_problem(json& arr, const char* kind, const std::string& file, const std::string& detail) {
    arr.push_back({{"kind", kind}, {"file", file}, {"detail", detail}});
}

bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// Gemeinsame Pruefungen fuer beide Listen: doppelte Eintraege, "elfldr" ohne Wartezeit davor dem naechsten Eintrag.
// entries = (Eintrag, Zeile); `after_elfldr` = Eintrag direkt hinter einem Loader ohne "!<ms>" dazwischen.
void check_sequence(json& problems, const std::string& file, const std::vector<std::string>& lines) {
    std::set<std::string> seen;
    bool prev_was_elfldr = false;
    for (const auto& l : lines) {
        if (l.empty()) continue;
        if (l[0] == '!') {
            prev_was_elfldr = false;
            continue;
        }
        if (!seen.insert(l).second) add_problem(problems, "duplicate", file, l);
        if (prev_was_elfldr) add_problem(problems, "elfldr_no_delay", file, l);
        prev_was_elfldr = lower(l).find("elfldr") != std::string::npos;
    }
}

void collect_pldmgr_autoload(const IFileSystem& fs, Facts& f, json& problems) {
    std::string cfg;
    if (fs.read_file(join_path(kPldmgrDir, "pldmgr_config.txt"), cfg)) {
        for (const auto& l : split_lines(cfg)) {
            const std::string t = trim(l);
            if (t.rfind("AUTOLOAD_ENABLED=", 0) == 0 && all_digits(t.substr(17)) && t.size() <= 19) f.set("pldmgr.autoload_enabled", std::atoi(t.c_str() + 17));
        }
    }
    std::string text;
    if (!fs.read_file(kPldmgrList, text)) return;

    if (text.size() > kPldmgrMaxBytes) add_problem(problems, "syntax", kPldmgrList, "Datei groesser als 4095 Byte");
    if (text.find('\0') != std::string::npos) add_problem(problems, "syntax", kPldmgrList, "enthaelt ein NUL-Zeichen");

    std::vector<std::string> lines;
    for (const auto& raw : split_lines(text)) {
        const std::string l = trim(raw);
        if (l.empty()) continue;
        if (l.size() > kPldmgrMaxLine) add_problem(problems, "syntax", kPldmgrList, "Zeile laenger als 511 Zeichen");
        else if (l[0] == '!' && (!all_digits(l.substr(1)) || l.size() > 9 || std::atol(l.c_str() + 1) > kPldmgrMaxDelayMs))
            add_problem(problems, "syntax", kPldmgrList, "Wartezeit ungueltig: " + l);
        else if (l[0] != '!' && l.find(',') != std::string::npos)
            add_problem(problems, "syntax", kPldmgrList, "Komma in einer Zeile: " + l);
        lines.push_back(l);
    }
    int entries = 0;
    for (const auto& l : lines)
        if (l[0] != '!') ++entries;
    f.set("pldmgr.autoload_entries", entries);
    check_sequence(problems, kPldmgrList, lines);

    // Payload Manager 0.6: ein Eintrag "local/<name>" bzw. "repo/<name>" ist ein Ordner unter payloads/ mit einer .elf darin.
    // Das Layout gilt, wenn payloads/.layout existiert; in aelteren Versionen standen dort nur Dateinamen.
    if (fs.exists(join_path(kPldmgrDir, "payloads/.layout"))) {
        for (const auto& l : lines) {
            if (l[0] == '!' || (l.rfind("local/", 0) != 0 && l.rfind("repo/", 0) != 0)) continue;
            const std::string dir = join_path(join_path(kPldmgrDir, "payloads"), l);
            bool has_elf = false;
            for (const auto& e : fs.list_dir(dir))
                if (!e.is_dir && lower(e.name).size() > 4 && lower(e.name).compare(lower(e.name).size() - 4, 4, ".elf") == 0) has_elf = true;
            if (!has_elf) add_problem(problems, "missing_slot", kPldmgrList, l);
        }
    }
}

// WebKit-Autoloader (itsPLK/ps5-unified-autoloader, src/main.c): "#" leitet einen Kommentar ein (auch hinter Text), "@sync" ist
// eine Anweisung, "!<ms>" eine Wartezeit; sonst ein Dateiname relativ zum Autoloader-Ordner oder ein absoluter Pfad.
// Namen sind case-sensitiv.
void collect_loader_autoload(const IFileSystem& fs, json& problems) {
    std::string text;
    if (!fs.read_file(kLoaderList, text)) return;
    std::vector<std::string> lines;
    for (const auto& raw : split_lines(text)) {
        std::string l = raw;
        if (const auto c = l.find('#'); c != std::string::npos) l.erase(c);
        l = trim(l);
        if (l.empty() || l[0] == '@') continue;
        lines.push_back(l);
    }
    check_sequence(problems, kLoaderList, lines);
    const auto names = fs.list_dir(kLoaderDir);
    for (const auto& l : lines) {
        if (l[0] == '!') continue;
        const std::string path = resolve_dots(l[0] == '/' ? l : join_path(kLoaderDir, l));
        if (fs.exists(path)) continue;
        bool case_only = false;
        if (l[0] != '/')
            for (const auto& e : names)
                if (!e.is_dir && lower(e.name) == lower(l)) case_only = true;
        add_problem(problems, case_only ? "entry_case" : "missing_file", kLoaderList, l);
    }
}

// ---- ELF-Dateien ---------------------------------------------------------------------------------------------------

constexpr std::size_t kMaxElfChecks = 400;

std::uint64_t le(const std::string& b, std::size_t off, int bytes) {
    std::uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(b[off + static_cast<std::size_t>(i)]);
    return v;
}

// "ok", "too_small", "not_elf" (falsche Kennung, z. B. eine HTML-Fehlerseite), "truncated" (Programm-Header reichen ueber das
// Dateiende: die Pruefung des Loaders elfldr_sanity_check) oder "unreadable".
std::string elf_state(const IFileSystem& fs, const std::string& path, std::uint64_t size) {
    std::string h;
    if (size == 0 || !fs.read_range(path, 0, 64, h)) return size == 0 ? "too_small" : "unreadable";
    if (size < 64 || h.size() < 64) return "too_small";
    if (std::memcmp(h.data(), "\x7f" "ELF", 4) != 0 || h[4] != 2) return "not_elf";
    const std::uint64_t phoff = le(h, 0x20, 8), phentsize = le(h, 0x36, 2), phnum = le(h, 0x38, 2);
    if (phnum == 0) return "ok";
    if (phentsize < 56 || phnum > 128 || phoff > size || phoff + phnum * phentsize > size) return "truncated";
    std::string ph;
    if (!fs.read_range(path, phoff, static_cast<std::size_t>(phnum * phentsize), ph)) return "unreadable";  // Lesefehler ist kein Befund "abgebrochene Uebertragung"
    if (ph.size() < phnum * phentsize) return "truncated";
    std::uint64_t end = 0;
    for (std::uint64_t i = 0; i < phnum; ++i) {
        const std::size_t base = static_cast<std::size_t>(i * phentsize);
        end = std::max(end, le(ph, base + 8, 8) + le(ph, base + 32, 8));
    }
    return end > size ? "truncated" : "ok";
}

bool is_elf_name(const std::string& n) {
    const std::string l = lower(n);
    return l.size() > 4 && l.compare(l.size() - 4, 4, ".elf") == 0;
}

void find_elfs(const IFileSystem& fs, const std::string& dir, int depth, json& out) {
    if (out.size() >= kMaxElfChecks) return;
    for (const auto& e : fs.list_dir(dir)) {
        if (out.size() >= kMaxElfChecks) return;
        const std::string path = join_path(dir, e.name);
        if (e.is_dir) {
            if (depth > 0 && e.name[0] != '.') find_elfs(fs, path, depth - 1, out);
        } else if (is_elf_name(e.name)) {
            out.push_back({{"path", path}, {"state", elf_state(fs, path, e.size)}, {"size", e.size}});
        }
    }
}

void collect_elfs(const IFileSystem& fs, Facts& f) {
    json files = json::array();
    find_elfs(fs, "/data/pldmgr/payloads", 3, files);
    find_elfs(fs, kLoaderDir, 0, files);
    find_elfs(fs, "/data/etaHEN", 1, files);
    find_elfs(fs, "/data/homebrew", 1, files);
    for (int i = 0; i < 8; ++i) find_elfs(fs, "/mnt/usb" + std::to_string(i), 0, files);
    json broken = json::array();
    for (const auto& e : files)
        if (e["state"] != "ok") broken.push_back({{"path", e["path"]}, {"reason", e["state"]}});
    f.set("payload.elf_files", std::move(files));
    f.set("payload.broken_elfs", std::move(broken));
}

// ---- nanoDNS und etaHEN ----------------------------------------------------------------------------------------------

// Das nanoDNS-Log enthaelt jede DNS-Abfrage samt Domain und Client-IP. Hier werden nur Fehlerzeilen GEZAEHLT; weder die Zeilen
// noch Domains gelangen in Fakten, Befunde oder den Export (Formate aus nanoDNS main.c, v0.4).
void collect_nanodns(const IFileSystem& fs, Facts& f) {
    std::string text;
    if (!fs.read_file("/data/nanodns/nanodns.log", text)) return;
    static const std::regex up(R"(\[nanodns\] all upstreams failed for )");
    static const std::regex bind(R"(\[nanodns\] bind\(.*\) failed)");
    static const std::regex warn(R"(\[nanodns\] warning: )");
    int upstream = 0, bind_failed = 0, warnings = 0;
    const auto lines = split_lines(text);
    for (std::size_t i = lines.size() > kMaxLogLines ? lines.size() - kMaxLogLines : 0; i < lines.size(); ++i) {
        if (std::regex_search(lines[i], up)) ++upstream;
        else if (std::regex_search(lines[i], bind)) ++bind_failed;
        else if (std::regex_search(lines[i], warn)) ++warnings;
    }
    f.set("nanodns.log.upstream_failed", upstream);
    f.set("nanodns.log.bind_failed", bind_failed);
    f.set("nanodns.log.config_warnings", warnings);
}

// etaHEN (Source Code/daemon): config.ini mit "Game_rest_kill=1" beendet beim Ruhemodus das offene Spiel; der Daemon schreibt
// "main etaHEN has crashed" ins etaHEN.log (bzw. legt etaHEN_crash.log an) und loescht beides beim naechsten Start.
void collect_etahen(const IFileSystem& fs, Facts& f) {
    std::string cfg;
    if (fs.read_file("/data/etaHEN/config.ini", cfg)) {
        for (const auto& raw : split_lines(cfg)) {
            const std::string l = trim(raw);
            if (l.empty() || l[0] == '#' || l[0] == ';' || l[0] == '[') continue;
            const auto eq = l.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = lower(trim(l.substr(0, eq))), val = trim(l.substr(eq + 1));
            if (key == "game_rest_kill") f.set("etahen.game_rest_kill", val == "1" ? 1 : 0);
            if (key == "util_rest_kill") f.set("etahen.util_rest_kill", val == "1" ? 1 : 0);
        }
    }
    std::string log;
    const bool crashed = fs.exists("/data/etaHEN/etaHEN_crash.log") ||
                         (fs.read_file("/data/etaHEN/etaHEN.log", log) && log.find("main etaHEN has crashed") != std::string::npos);
    if (crashed) f.set("etahen.crashed", true);
}

}  // namespace

// Layout des Payload Managers (ab 0.6: Datei payloads/.layout mit "2") und lose ELF-Dateien direkt in payloads/. Der Entwickler
// (itsPLK/ps5-payload-manager #135): lose abgelegte Payloads liefen frueher nur zufaellig ueber einen losen rekursiven Scan.
// pldmgr.layout, pldmgr.loose_elfs[].{name,path}
void collect_pldmgr_layout(const IFileSystem& fs, Facts& f) {
    const std::string dir = "/data/pldmgr/payloads";
    std::string layout;
    if (fs.read_file(dir + "/.layout", layout) && layout.size() <= 16) f.set("pldmgr.layout", trim(layout));
    json loose = json::array();
    for (const auto& e : fs.list_dir(dir))
        if (!e.is_dir && is_elf_name(e.name) && loose.size() < 50) loose.push_back({{"name", e.name}, {"path", join_path(dir, e.name)}});
    f.set("pldmgr.loose_elfs", std::move(loose));
}

// autoload.problems[].{kind,file,detail}, pldmgr.autoload_enabled, pldmgr.autoload_entries
void collect_autoload(const Platform& p, Facts& f) {
    json problems = json::array();
    collect_pldmgr_autoload(p.fs(), f, problems);
    collect_pldmgr_layout(p.fs(), f);
    collect_loader_autoload(p.fs(), problems);
    f.set("autoload.problems", std::move(problems));
}

// payload.elf_files[].{path,state,size}, payload.broken_elfs[].{path,reason}
void collect_payload_files(const Platform& p, Facts& f) { collect_elfs(p.fs(), f); }

// nanodns.log.*, etahen.*
void collect_helpers(const Platform& p, Facts& f) {
    collect_nanodns(p.fs(), f);
    collect_etahen(p.fs(), f);
}

}  // namespace medic
