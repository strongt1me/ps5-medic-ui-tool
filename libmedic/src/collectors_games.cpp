// SPDX-License-Identifier: GPL-3.0-or-later
// Collectors fuer Spiele (Auswahl vom 14. Nov. 2026, ampr-emu-ng, kraken-mount, Dump Forge): Laufzeit-Bibliothek und Packs im Spiel (Ordner oder Image),
// unvollstaendige Spielordner in den Scanpfaden, Meldungen von Kraken Mount. Nur lesen; aus Images wird nur der Anfang weniger Dateien gelesen.
#include "medic/collectors.hpp"
#include "medic/compat.hpp"
#include "medic/image_reader.hpp"
#include "medic/readtail.hpp"
#include "medic/rules.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
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

std::string join_path(const std::string& dir, const std::string& name) {
    return (!dir.empty() && dir.back() == '/') ? dir + name : dir + "/" + name;
}

std::uint32_t le32(const std::string& s, std::size_t off) {
    if (off + 4 > s.size()) return 0;
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[off])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 2])) << 16) | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[off + 3])) << 24);
}

std::uint64_t le64(const std::string& s, std::size_t off) { return static_cast<std::uint64_t>(le32(s, off)) | (static_cast<std::uint64_t>(le32(s, off + 4)) << 32); }

// ---- Eine Quelle (Spielordner oder Image) ----------------------------------------------------------------------------------------------------------

struct Item {
    std::string name;
    bool is_dir = false;
    std::uint64_t size = 0;
    bool size_known = false;  // Images nennen keine Dateigroessen
};

class Tree {
public:
    virtual ~Tree() = default;
    virtual bool list(const std::string& dir, std::vector<Item>& out) const = 0;
    // Hoechstens len Bytes ab offset; false, wenn die Datei fehlt oder nichts zu lesen ist
    virtual bool read_at(const std::string& path, std::uint64_t offset, std::size_t len, std::string& out) const = 0;
};

class FolderTree final : public Tree {
public:
    FolderTree(const IFileSystem& fs, std::string root) : fs_(fs), root_(std::move(root)) {}
    bool list(const std::string& dir, std::vector<Item>& out) const override {
        const std::string p = dir.empty() || dir == "/" ? root_ : join_path(root_, dir);
        if (!fs_.is_dir(p)) return false;
        out.clear();
        for (const auto& e : fs_.list_dir(p)) out.push_back({e.name, e.is_dir, e.size, true});
        return true;
    }
    bool read_at(const std::string& path, std::uint64_t offset, std::size_t len, std::string& out) const override { return fs_.read_range(join_path(root_, path), offset, len, out); }

private:
    const IFileSystem& fs_;
    std::string root_;
};

class ImageTree final : public Tree {
public:
    explicit ImageTree(std::unique_ptr<ImageReader> r) : r_(std::move(r)) {}
    bool list(const std::string& dir, std::vector<Item>& out) const override {
        std::vector<ImageReader::Entry> es;
        if (!r_->list_dir(dir.empty() ? "/" : (dir[0] == '/' ? dir : "/" + dir), es, 65536)) return false;
        out.clear();
        for (const auto& e : es) out.push_back({e.name, e.is_dir, 0, false});
        return true;
    }
    bool read_at(const std::string& path, std::uint64_t offset, std::size_t len, std::string& out) const override {
        return r_->read_some(path[0] == '/' ? path : "/" + path, offset, len, out);
    }

private:
    std::unique_ptr<ImageReader> r_;
};

const Item* find_item(const std::vector<Item>& items, const std::string& name, bool want_dir) {
    const std::string l = lower(name);
    for (const auto& it : items)
        if (it.is_dir == want_dir && lower(it.name) == l) return &it;
    return nullptr;
}

// ---- Laufzeit-Bibliothek (libSceAmpr.sprx) -----------------------------------------------------------------------------------------------------------
// An den echten Dateien gelesen (14. Nov. 2026): ampr-emu-ng traegt den Text "ampr-emu-ng X.Y.Z" (0.3.4 und 0.4.0 geprueft), der Debug-Build zusaetzlich
// "debugpack_format"; die Laufzeit von Drakmor (ampr_emu 0.4.2.1) "X.Y.Z (c) Drakmor" und "[AMPR_EMU]" und kennt das zstd-Magic AMPRZPK1 nicht; Sonys
// Bibliothek (eine ELF-Datei) hat keinen dieser Texte. Die Dateien sind 0,4 bis 0,9 MiB gross.

struct LibInfo {
    std::string kind = "other";  // "ng", "drakmor" oder "other" (Sony-Original oder unbekannt)
    std::string version;
    bool debug = false;
    bool zstd = false;  // kennt das zstd-Magic AMPRZPK1
    bool elf = false;   // beginnt als ELF-Datei (Sonys Original); eine SELF-Datei ohne bekannte Texte ist "unbekannt", nicht "Sony"
};

bool version_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+' || c == '_'; }

LibInfo classify_lib(const std::string& b) {
    LibInfo li;
    li.elf = b.compare(0, 4, "\x7f" "ELF") == 0;
    li.zstd = b.find("AMPRZPK1") != std::string::npos;
    // "debugpack_format" ist eine zufaellige Verkettung im Programm und kann wegfallen; ampr_emu_tune.txt und /fsfail stehen nur im Debug-Build (0.5.1 gelesen)
    li.debug = b.find("debugpack_format") != std::string::npos || b.find("ampr_emu_tune.txt") != std::string::npos || b.find("/fsfail") != std::string::npos;
    const std::string ng = "ampr-emu-ng ";
    for (std::size_t pos = b.find(ng); pos != std::string::npos; pos = b.find(ng, pos + 1)) {
        const std::size_t v = pos + ng.size();
        if (v >= b.size() || !std::isdigit(static_cast<unsigned char>(b[v]))) continue;  // "ampr-emu-ng debugpack_format ..."
        std::size_t e = v;
        while (e < b.size() && version_char(b[e])) ++e;
        li.kind = "ng";
        li.version = b.substr(v, e - v);
        return li;
    }
    const std::size_t d = b.find(" (c) Drakmor");
    if (d != std::string::npos) {
        std::size_t s = d;
        while (s > 0 && (std::isdigit(static_cast<unsigned char>(b[s - 1])) || b[s - 1] == '.')) --s;
        li.kind = "drakmor";
        li.version = b.substr(s, d - s);
        return li;
    }
    if (b.find("[AMPR_EMU]") != std::string::npos) li.kind = "drakmor";
    return li;
}

constexpr std::size_t kMaxLibBytes = 2u << 20;  // die Bibliotheken sind kleiner; mehr wird nicht gelesen

// ng-Fassung vor 0.3.4 (S03): Frueher behobene Fehler (Stack-Ueberlauf bis 0.2.0, Dateinummern/Kernel-AIO bis 0.3.2, zu wenige Threads und zu kleiner Pool bis 0.3.3;
// ampr-emu-ng Tabelle F2). Eine Beta-Endung (0.3.4-beta1) zaehlt wie ihre Zahl.
bool ng_older_than_034(const std::string& v) {
    int part[3] = {0, 0, 0};
    std::size_t i = 0;
    for (int k = 0; k < 3; ++k) {
        if (i >= v.size() || !std::isdigit(static_cast<unsigned char>(v[i]))) return false;
        long n = 0;
        while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) n = std::min(n * 10 + (v[i++] - '0'), 100000L);
        part[k] = static_cast<int>(n);
        if (k < 2) {
            if (i >= v.size() || v[i] != '.') return false;  // "0.3" ohne dritte Zahl: unbekannt
            ++i;
        }
    }
    if (part[0] != 0) return false;
    return part[1] < 3 || (part[1] == 3 && part[2] < 4);
}

// ---- ampr_emu.index (AMPRIDX3) -----------------------------------------------------------------------------------------------------------------------
// Aufbau laut Dump Forge (vendor/ps5upload-fpkg/src/ampr_index.rs, an Spider-Man 2s ausgelieferter Index geprueft): Kopf 0x30 Byte (Magic, u32 3, u32 Satzgroesse 24, u64 Saetze @0x10,
// u64 Pfadbytes @0x18, ...), danach je Satz {u32 Pfad-Offset, u32 Pfad-Laenge, u64 Groesse, i64 mtime}, danach die Pfade ("/app0/...", je mit NUL), danach die Hash-Tabelle.
constexpr std::size_t kIdxHeader = 0x30, kIdxRecord = 24;
constexpr std::uint64_t kBigIndexRecords = 200000;       // S21: ab etwa so vielen Dateien schaltet ng 0.3.5/0.4.0 die gepackten Dateien ab (Issue 53/61, Schaetzung ungeprueft)
constexpr std::uint64_t kMaxIndexBytes = 24u << 20;       // S22: groessere Indizes werden nicht ganz gelesen
constexpr std::uint64_t kMaxIndexRecordsTotal = 60000;   // S22: Arbeitsgrenze je Check-up (Sätze), danach bleibt der Rest ungeprueft
constexpr std::size_t kMaxIndexDirs = 3000;               // S22: so viele Ordner werden je Quelle gelistet

struct IndexBudget {
    std::chrono::steady_clock::time_point deadline;
    std::uint64_t records_left = kMaxIndexRecordsTotal;
};

struct IndexHead {
    bool ok = false;
    std::uint64_t records = 0, path_bytes = 0;
};

IndexHead read_index_head(const Tree& tree, const Item& idx) {
    IndexHead h;
    std::string head;
    if (!tree.read_at(idx.name, 0, kIdxHeader, head) || head.size() < kIdxHeader || head.compare(0, 8, "AMPRIDX3") != 0 || le32(head, 8) != 3 || le32(head, 12) != kIdxRecord) return h;
    h.records = le64(head, 0x10);
    h.path_bytes = le64(head, 0x18);
    if (h.records > (1ull << 32) || h.path_bytes > (1ull << 40)) return h;
    // Die Dateigroesse muss die Saetze und Pfade fassen: sonst ist der Kopf unbrauchbar (nichts behaupten)
    if (idx.size_known && idx.size < kIdxHeader + h.records * kIdxRecord + h.path_bytes) return h;
    h.ok = true;
    return h;
}

// Die Dateien, die der Index nennt, gegen den Baum pruefen (nur lose Spiele). Ausgenommen sind Dateien, die ein Backport oder ShadowMount+ ersetzen darf (eboot.bin, sce_sys,
// sce_module, fakelib). Fehlende Dateien sind ein harter Befund; geaenderte Groessen werden nur gezaehlt (ein Index darf legitim abweichen). Mit Arbeits- und Zeitgrenze.
void check_index_files(const Tree& tree, const Item& idx, const IndexHead& h, const std::string& titleid, const std::string& path, const char* kind, IndexBudget& budget, json& out) {
    if (h.records == 0 || h.records > budget.records_left || kIdxHeader + h.records * kIdxRecord + h.path_bytes > kMaxIndexBytes) return;
    const std::size_t need = static_cast<std::size_t>(h.records * kIdxRecord + h.path_bytes);
    std::string data;
    if (!tree.read_at(idx.name, kIdxHeader, need, data) || data.size() != need) return;
    budget.records_left -= h.records;
    std::map<std::string, std::vector<Item>> dirs;
    std::set<std::string> unlistable;
    std::uint64_t checked = 0, missing = 0, changed = 0;
    json examples = json::array();
    bool partial = false;
    for (std::uint64_t i = 0; i < h.records; ++i) {
        if ((i & 255) == 0 && std::chrono::steady_clock::now() > budget.deadline) {
            partial = true;
            break;
        }
        const std::size_t r = static_cast<std::size_t>(i * kIdxRecord);
        const std::uint64_t off = le32(data, r), len = le32(data, r + 4), size = le64(data, r + 8);
        const std::uint64_t base = h.records * kIdxRecord;
        if (base + off + len >= data.size() || data[static_cast<std::size_t>(base + off + len)] != '\0') return;  // verdorbener Index: nichts behaupten
        std::string p = data.substr(static_cast<std::size_t>(base + off), static_cast<std::size_t>(len));
        for (auto& c : p)
            if (c == '\\') c = '/';
        if (p.size() < 6 || lower(p.substr(0, 6)) != "/app0/") continue;
        p.erase(0, 6);
        const std::string lp = lower(p);
        if (lp == "eboot.bin" || lp.rfind("sce_sys/", 0) == 0 || lp.rfind("sce_module/", 0) == 0 || lp.rfind("fakelib", 0) == 0) continue;
        const auto slash = p.rfind('/');
        const std::string dir = slash == std::string::npos ? "" : p.substr(0, slash), name = slash == std::string::npos ? p : p.substr(slash + 1);
        auto it = dirs.find(dir);
        if (it == dirs.end()) {
            if (dirs.size() >= kMaxIndexDirs) {
                partial = true;
                break;
            }
            std::vector<Item> items;
            const bool ok = tree.list(dir, items);
            if (!ok) unlistable.insert(dir);
            it = dirs.emplace(dir, std::move(items)).first;
        }
        ++checked;
        const Item* found = nullptr;
        for (const auto& e : it->second)
            if (!e.is_dir && e.name == name) found = &e;
        if (!found) {
            for (const auto& e : it->second)
                if (!e.is_dir && lower(e.name) == lower(name)) found = &e;  // nur andere Schreibweise: Sache von game.il2cpp_case, hier nicht "fehlt"
            if (found) continue;
            ++missing;
            if (examples.size() < 3) examples.push_back(p);
        } else if (found->size_known && found->size != size) {
            ++changed;
        }
    }
    if (checked == 0) return;
    out.push_back({{"titleid", titleid}, {"path", path}, {"kind", kind}, {"records", h.records}, {"checked", checked}, {"missing", missing}, {"changed", changed},
                   {"partial", partial}, {"examples", std::move(examples)},
                   {"desc", path + ": " + std::to_string(missing) + "/" + std::to_string(checked)}});
}

// Eine Quelle auswerten: games.runtime[], games.pack_problems[] und games.index_problems[]
void inspect_source(const Tree& tree, const std::string& titleid, const std::string& path, const char* kind, bool emu_updates, bool save_errors, bool any_error, IndexBudget& budget,
                    json& runtime, json& problems, json& index_problems) {
    std::vector<Item> root;
    if (!tree.list("", root)) return;

    // Packs: Name und Magic. zstd: ampr_zstd.index (AMPRZPK1), LZ4: ampr_assets.index (AMPRPAK4); ampr_emu.index (AMPRIDX3) kennt alle Dateien
    std::string pack;
    std::string zstd_head;
    std::uint64_t zstd_index_size = 0;
    bool zstd_size_known = false;
    if (const Item* it = find_item(root, "ampr_zstd.index", false)) {
        std::string head;
        if (tree.read_at(it->name, 0, 128, head) && head.compare(0, 8, "AMPRZPK1") == 0) {
            pack = "zstd";
            zstd_head = head;
            zstd_index_size = it->size;
            zstd_size_known = it->size_known;
        }
    }
    if (pack.empty())
        if (const Item* it = find_item(root, "ampr_assets.index", false)) {
            std::string head;
            if (tree.read_at(it->name, 0, 8, head) && head.compare(0, 4, "AMPR") == 0) pack = "lz4";
        }
    const Item* emu_item = find_item(root, "ampr_emu.index", false);
    const bool emu_index = emu_item != nullptr;
    const IndexHead emu_head = emu_item ? read_index_head(tree, *emu_item) : IndexHead{};
    // S22: nur lose Spiele (ohne Pack): bei Packs liegen gepackte Dateien absichtlich nicht im Ordner. Auch Spiele ohne libSceAmpr im fakelib (kein Eintrag in games.runtime).
    if (pack.empty() && emu_item && emu_head.ok) check_index_files(tree, *emu_item, emu_head, titleid, path, kind, budget, index_problems);

    // Bibliothek: fakelib2 hat bei ShadowMount+ Vorrang vor fakelib
    std::string lib_in, lib_name;
    LibInfo li;
    bool have_lib = false;
    for (const char* dir : {"fakelib2", "fakelib"}) {
        std::vector<Item> items;
        const Item* d = find_item(root, dir, true);
        if (!d || !tree.list(d->name, items)) continue;
        const Item* lib = find_item(items, "libSceAmpr.sprx", false);
        if (!lib) continue;
        std::string bytes;
        have_lib = true;
        lib_in = dir;
        lib_name = lib->name;
        if (tree.read_at(join_path(d->name, lib->name), 0, kMaxLibBytes, bytes)) li = classify_lib(bytes);
        else li.kind = "unreadable";
        break;
    }

    if (pack.empty() && !have_lib) return;
    std::string problem;
    if (!pack.empty()) {
        if (!have_lib) {
            if (!emu_updates) problem = "no_runtime";  // mit Emulator-Updates legt ShadowMount+ die Bibliothek beim Einhaengen selbst ein
        } else if (pack == "zstd" && li.kind == "drakmor") {
            problem = "no_zstd_support";
        } else if (li.kind == "other" && li.elf) {
            problem = "foreign_runtime";  // Sonys Original (eine ELF-Datei ohne Marken) liest keine Packs; eine unbekannte SELF-Datei beurteilt Medic nicht
        }
        // "unreadable": nichts behaupten
    }
    runtime.push_back({{"titleid", titleid}, {"path", path}, {"kind", kind}, {"pack", pack}, {"has_emu_index", emu_index}, {"lib_in", lib_in},
                       {"lib_kind", have_lib ? li.kind : "missing"}, {"lib_version", li.version}, {"lib_debug", li.debug}, {"problem", problem},
                       // Dump Forge schreibt zu jedem gepackten Spiel eine ampr_emu.index (alle Dateien, lose und gepackt: docs/LZ4.md): fehlt sie, ist etwas schiefgegangen
                       // Nur bei ampr-emu-ng (oder ganz ohne Bibliothek) gesichert; bei Drakmors Laufzeit mit LZ4-Packs ist ein Fehlen vermutlich normal
                       {"index_missing", !pack.empty() && !emu_index && (!have_lib || li.kind == "ng")},
                       // ampr-emu-ng 0.3.5 und 0.4.0 (Release): Speichern/Laden/Beenden haengt oder meldet CE-108255-1 und CE-107112-2 (Issues 60, 63, 64 im Projekt);
                       // laut Projekt erst in einer Beta behoben, deren Versionstext "0.4.0-beta..." lautet. 0.4.0 ist die ausgelieferte Release-Fassung: gemeldet
                       // wird nur mit Beleg, also wenn die Konsole einen dieser Fehler fuer genau diesen Titel verzeichnet hat (save_errors)
                       // Abgleich 28. Nov. 2026: auch 0.4.1 bis 0.5.0 hatten Speicherfehler (0.5.0 behob zurueckportierte FW-4.xx-Spiele, 0.5.1 den pkg-Modus);
                       // gemeldet wird weiter nur mit Beleg (save_errors); Beta-Fassungen ("0.4.0-beta...") nicht: die Beta zu 0.4.0 enthielt die Korrektur
                       {"save_risk", save_errors && li.kind == "ng" && !li.version.empty() && li.version.find('-') == std::string::npos && compare_versions(li.version, "0.3.5") >= 0 && compare_versions(li.version, "0.5.1") < 0},
                       // S03: eine ng-Fassung vor 0.3.4 (alte, spaeter behobene Fehler), nur gemeldet, wenn die Konsole fuer genau diesen Titel einen Fehler verzeichnet hat
                       {"old_version", li.kind == "ng" && ng_older_than_034(li.version)},
                       {"old_risk", any_error && li.kind == "ng" && ng_older_than_034(li.version)},
                       // S21: Zahl der Dateien laut ampr_emu.index (0 = Kopf unlesbar oder unstimmig) und die Gefahr bei 0.3.5/0.4.0 (Release), ab ca. 200000 Dateien
                       {"emu_records", emu_head.ok ? emu_head.records : 0},
                       {"index_risk", emu_head.ok && emu_head.records >= kBigIndexRecords && li.kind == "ng" && (li.version == "0.3.5" || li.version == "0.4.0")}});

    // zstd-Pack vollstaendig? (Format: docs/zstd.md von Dump Forge: Kopf 128 Byte, volume_count @20, volumes_off @72, strings_off @80, strings_len @88;
    // eine Volume-Datei je Eintrag, ampr_zstd-NNN.pak, die Groesse steht im Volume-Record) -- die Dateigroessen kennt nur ein Ordner
    if (pack == "zstd") {
        auto add = [&](const char* what, const std::string& detail, int count) {
            problems.push_back({{"titleid", titleid}, {"path", path}, {"kind", kind}, {"problem", what}, {"detail", detail}, {"count", count}});
        };
        if (zstd_head.size() < 128) {
            add("index_truncated", "ampr_zstd.index ist kuerzer als ihr Kopf", 1);
        } else if (le32(zstd_head, 8) == 1 && le32(zstd_head, 12) == 128) {  // nur die bekannte Fassung des Formats beurteilen; eine kuenftige Version nicht
            const std::uint32_t volumes = le32(zstd_head, 20);
            const std::uint64_t volumes_off = le64(zstd_head, 72), strings_off = le64(zstd_head, 80), strings_len = le64(zstd_head, 88);
            if (volumes == 0 || volumes > 4096) {
                add("index_header", "Anzahl der Volumes im Kopf der ampr_zstd.index ungueltig", 1);
            } else {
                if (zstd_size_known && strings_off + strings_len != zstd_index_size) add("index_truncated", "ampr_zstd.index endet nicht, wo ihr Kopf es sagt", 1);
                std::map<std::string, const Item*> have;  // kleingeschriebener Name -> Eintrag, einmal gebaut (je Volume ein Suchen im Ordner waere quadratisch)
                for (const auto& it : root)
                    if (!it.is_dir) have[lower(it.name)] = &it;
                std::string records;
                const bool rec_ok = volumes_off >= 128 && volumes_off < (1ull << 40) && tree.read_at("ampr_zstd.index", volumes_off, static_cast<std::size_t>(volumes) * 8, records) &&
                                    records.size() == static_cast<std::size_t>(volumes) * 8;
                int missing = 0, wrong = 0;
                std::string first_missing, first_wrong;
                for (std::uint32_t i = 0; i < volumes; ++i) {
                    char name[40];
                    std::snprintf(name, sizeof name, i < 1000 ? "ampr_zstd-%03u.pak" : "ampr_zstd-%04u.pak", static_cast<unsigned>(i));
                    const auto it = have.find(name);  // die Namen sind schon klein geschrieben
                    if (it == have.end()) {
                        if (missing++ == 0) first_missing = name;
                    } else if (rec_ok && it->second->size_known && it->second->size != le64(records, static_cast<std::size_t>(i) * 8)) {
                        if (wrong++ == 0) first_wrong = name;
                    }
                }
                if (missing) add("volume_missing", first_missing, missing);
                if (wrong) add("volume_size", first_wrong, wrong);
            }
        }
    }
}

}  // namespace

// games.runtime[]: titleid, path, kind (folder|image), pack ("zstd"|"lz4"|""), has_emu_index, lib_in (fakelib|fakelib2), lib_kind (ng|drakmor|other|missing|unreadable),
// lib_version, lib_debug, problem ("", no_runtime, no_zstd_support, foreign_runtime), index_missing, save_risk. Nur Quellen mit Pack oder mit libSceAmpr.sprx im fakelib.
// games.pack_problems[]: titleid, path, kind, problem (index_header, index_truncated, volume_missing, volume_size), detail, count.
// games.runtime[] hat ausserdem old_version/old_risk (S03: ng vor 0.3.4, mit Fehler der Konsole fuer den Titel), emu_records und index_risk (S21: ab 200000 Dateien bei ng 0.3.5/0.4.0).
// games.index_problems[]: titleid, path, kind, records, checked, missing, changed, partial, examples[], desc (S22: lose Spiele; Dateien laut ampr_emu.index fehlen im Baum).
void collect_game_runtime(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    json runtime = json::array(), problems = json::array(), index_problems = json::array();
    std::set<std::string> image_paths;
    if (const json* imgs = f.get("smp.images"); imgs && imgs->is_array())
        for (const auto& i : *imgs)
            if (i.is_object() && i.contains("path") && i["path"].is_string()) image_paths.insert(i["path"].get<std::string>());
    std::map<std::string, std::uint64_t> image_sizes;  // die Groesse aus dem Sammler von ShadowMount+: ImageReader::open muss dafuer nicht den ganzen Ordner listen
    if (const json* imgs = f.get("smp.images"); imgs && imgs->is_array())
        for (const auto& i : *imgs)
            if (i.is_object() && i.contains("path") && i["path"].is_string() && i.contains("size") && i["size"].is_number_unsigned()) image_sizes[i["path"].get<std::string>()] = i["size"].get<std::uint64_t>();
    // Emulator-Updates von ShadowMount+ an: dann legt es die Bibliothek beim Einhaengen selbst ein (eine fehlende gilt nicht als Befund)
    auto flag_on = [&](const char* path) {
        const json* v = f.get(path);
        if (!v || !v->is_string()) return false;
        const std::string s = lower(v->get<std::string>());
        return s == "1" || s == "true" || s == "yes" || s == "on";
    };
    const bool emu_updates = flag_on("smp.config.emulators.update_enabled") || flag_on("smp.config.emulators.auto_update_ampr") || flag_on("smp.config.update_emulators");
    // Titel, fuer die die Konsole einen Speicher-Fehler verzeichnet hat (Beleg fuer game.ng_save_risk)
    std::set<std::string> save_error_titles, error_titles;  // error_titles: irgendein Fehlercode fuer den Titel (Beleg fuer game.runtime_old_version)
    if (const json* ep = f.get("errors.pairs"); ep && ep->is_array())
        for (const auto& e : *ep)
            if (e.is_object() && e.contains("code") && e["code"].is_string() && e.contains("titleid") && e["titleid"].is_string()) {
                error_titles.insert(e["titleid"].get<std::string>());
                if (e["code"] == "CE-108255-1" || e["code"] == "CE-107112-2") save_error_titles.insert(e["titleid"].get<std::string>());
            }
    const json* titles = f.get("smp.titles");
    std::set<std::string> done;
    int attempts = 0, skipped = 0;
    constexpr int kMaxSources = 60;  // eine Quelle kostet bei einem Image das Oeffnen und einige Lesezugriffe; Fehlversuche zaehlen mit
    // Zeitbudget (USB-Platten und PFSC-Images koennen langsam sein): danach bleibt der Rest ungeprueft und steht in games.runtime_skipped
    const auto started = std::chrono::steady_clock::now();
    constexpr auto kBudget = std::chrono::seconds(8);
    IndexBudget index_budget;
    index_budget.deadline = started + kBudget;
    if (titles && titles->is_array())
        for (const auto& t : *titles) {
            if (!t.is_object() || !t.contains("path") || !t["path"].is_string() || !t.contains("titleid") || !t["titleid"].is_string()) continue;
            const std::string path = t["path"].get<std::string>(), id = t["titleid"].get<std::string>();
            if (!done.insert(path).second) continue;
            const bool is_image = image_paths.count(path) != 0;
            if (!is_image && !fs.is_dir(path)) continue;
            if (attempts >= kMaxSources || std::chrono::steady_clock::now() - started > kBudget) {
                ++skipped;
                continue;
            }
            ++attempts;
            if (is_image) {
                const auto sz = image_sizes.find(path);
                auto r = sz != image_sizes.end() ? ImageReader::open(fs, path, sz->second) : ImageReader::open(fs, path);
                if (!r) continue;
                inspect_source(ImageTree(std::move(r)), id, path, "image", emu_updates, save_error_titles.count(id) != 0, error_titles.count(id) != 0, index_budget, runtime, problems, index_problems);
            } else {
                inspect_source(FolderTree(fs, path), id, path, "folder", emu_updates, save_error_titles.count(id) != 0, error_titles.count(id) != 0, index_budget, runtime, problems, index_problems);
            }
        }
    f.set("games.runtime", std::move(runtime));
    f.set("games.pack_problems", std::move(problems));
    f.set("games.index_problems", std::move(index_problems));
    f.set("games.runtime_skipped", skipped);  // so viele Quellen blieben ungeprueft (Grenze kMaxSources): "nicht gefunden" sagt dann nichts
}

// kstuff.file, kstuff.file_version: die kstuff-Datei, die der Payload Manager laedt (Eintrag mit "kstuff" in /data/pldmgr/autoload.txt, Datei im Ordner
// /data/pldmgr/payloads/<Eintrag>/). Die Version steht nur im Dateinamen ("kstuff1.13-fpkg-dr-test8-rest-fast.elf") und ist keine gepruefte Angabe; es sind
// Forks mit eigener Zaehlung dabei. Deshalb nur Fakten zur Information (last_facts.json), keine Regel stuetzt sich darauf (kstuff.version bleibt unbekannt).
void collect_kstuff_file(const Platform& p, Facts& f) {
    std::string list;
    if (!p.fs().read_file("/data/pldmgr/autoload.txt", list)) return;
    std::istringstream in(list);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '!' || lower(line).find("kstuff") == std::string::npos || line.find("..") != std::string::npos) continue;
        for (const auto& e : p.fs().list_dir("/data/pldmgr/payloads/" + line)) {
            const std::string l = lower(e.name);
            if (e.is_dir || l.size() < 5 || l.compare(l.size() - 4, 4, ".elf") != 0) continue;
            f.set("kstuff.file", e.name);
            static const std::regex ver(R"(([0-9]+\.[0-9]+(?:\.[0-9]+)?))");
            std::smatch m;
            if (std::regex_search(e.name, m, ver)) f.set("kstuff.file_version", m[1].str());
            return;
        }
    }
}

// games.compat[]: titleid, status ("ok", "fail", "mixed"), format -- alle Titel dieser Konsole (smp.titles, games.fw), die in der eingebetteten Kompatibilitaetsliste stehen.
// games.compat_bad[]: davon die mit "fail" oder "mixed", dazu desc (Titel-ID, Ergebnis, Format) als Evidenz. Nur eingebaute Daten, kein Netz.
void collect_compat(const Platform&, Facts& f) {
    std::set<std::string> ids;
    for (const char* key : {"smp.titles", "games.fw"})
        if (const json* a = f.get(key); a && a->is_array())
            for (const auto& e : *a)
                if (e.is_object() && e.contains("titleid") && e["titleid"].is_string()) ids.insert(e["titleid"].get<std::string>());
    json all = json::array(), bad = json::array();
    for (const auto& id : ids) {
        const CompatEntry* c = find_compat(id);
        if (!c) continue;
        json row = {{"titleid", id}, {"status", c->status}, {"format", c->format}};
        all.push_back(row);
        if (std::strcmp(c->status, "ok") != 0) {
            row["desc"] = id + ": " + (std::strcmp(c->status, "fail") == 0 ? "fail" : "mixed") + (c->format[0] ? std::string(" (") + c->format + ")" : std::string());
            bad.push_back(std::move(row));
        }
    }
    if (!all.empty()) f.set("games.compat", std::move(all));
    if (!bad.empty()) f.set("games.compat_bad", std::move(bad));
}

// games.incomplete[]: path, name, missing[] ("eboot.bin", "sce_sys/param.json"), param ("ok", "missing", "bom", "invalid", "no_titleid"). Ein Ordner direkt in einem
// Scanpfad, der wie ein Spiel aussieht (sce_sys und dazu eboot.bin oder sce_module), dem aber eboot.bin oder eine lesbare param.json fehlt: ShadowMount+ und
// Kraken Mount ueberspringen ihn ohne jede Logzeile. Punkt-Ordner (.medic_broken, .duplicates) und Ordner ohne diese Marken zaehlen nicht.
void collect_game_folders(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    json out = json::array();
    std::set<std::string> seen;
    const json* roots = f.get("smp.scan_roots");
    if (!roots || !roots->is_array()) {
        f.set("games.incomplete", std::move(out));
        return;
    }
    static const std::regex id_re("^[A-Z]{4}[0-9]{5}$");
    for (const auto& r : *roots) {
        if (!r.is_string()) continue;
        const std::string root = r.get<std::string>();
        if (root.empty() || !seen.insert(root).second || !fs.is_dir(root)) continue;
        for (const auto& e : fs.list_dir(root)) {
            if (!e.is_dir || e.name.empty() || e.name[0] == '.' || out.size() >= 100) continue;
            const std::string dir = join_path(root, e.name);
            const auto kids = fs.list_dir(dir);
            bool sce_sys = false, sce_module = false, eboot = false;
            for (const auto& k : kids) {
                const std::string l = lower(k.name);
                if (k.is_dir && l == "sce_sys") sce_sys = true;
                else if (k.is_dir && l == "sce_module") sce_module = true;
                else if (!k.is_dir && l == "eboot.bin") eboot = true;
            }
            // Nur ein Ordner mit sce_sys (Marke eines Titels) zaehlt: ein Ordner mit eboot.bin allein kann ein Payload-Ordner sein. Ohne sce_module und ohne eboot.bin
            // (nur sce_sys, z. B. ein Ordner, der nur eine param.json hat) bleibt er unbeachtet.
            if (!sce_sys || (!eboot && !sce_module)) continue;
            std::string param = "missing";
            if (sce_sys) {
                bool ps4 = false;  // ein PS4-Titel hat sce_sys/param.sfo statt param.json
                for (const auto& k : fs.list_dir(join_path(dir, "sce_sys")))
                    if (!k.is_dir && lower(k.name) == "param.sfo") ps4 = true;
                if (ps4) continue;
                std::string text;
                for (const auto& k : fs.list_dir(join_path(dir, "sce_sys")))
                    if (!k.is_dir && lower(k.name) == "param.json") {
                        if (!fs.read_range(join_path(join_path(dir, "sce_sys"), k.name), 0, 262144, text) || text.empty()) {
                            param = "invalid";
                            break;
                        }
                        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0 || text.compare(0, 2, "\xFF\xFE") == 0 || text.compare(0, 2, "\xFE\xFF") == 0) {
                            param = "bom";  // ShadowMount+ liest die Datei nicht, die Konsole sagt auch dann nichts
                            break;
                        }
                        const json j = json::parse(text, nullptr, false);
                        if (!j.is_object()) param = "invalid";
                        else if (!j.contains("titleId") || !j["titleId"].is_string() || !std::regex_match(j["titleId"].get<std::string>(), id_re)) param = "no_titleid";
                        else param = "ok";
                        break;
                    }
            }
            json missing = json::array();
            if (!eboot) missing.push_back("eboot.bin");
            if (param == "missing") missing.push_back("sce_sys/param.json");
            if (missing.empty() && param == "ok") continue;
            out.push_back({{"path", dir}, {"name", e.name}, {"missing", std::move(missing)}, {"param", param}});
        }
    }
    f.set("games.incomplete", std::move(out));
}

// Aufbau der Spielordner und Reste in den Scanpfaden (Auswahl vom 16. Nov. 2026: S12, S32, S33). Nur Namen, kein Dateiinhalt.
// games.partial_folders[]: path, name, root, paks (Zahl der ampr_zstd-NNN.pak), tmp (versteckte .ampr_zstd-*.tmp-Handles von Dump Forge). Ein Ordner direkt in
//   einem Scanpfad mit zstd-Volumes, aber ohne ampr_zstd.index: Dump Forge schreibt den Index zuletzt, ein Abbruch laesst also genau diesen Zustand zurueck (S32).
//   Ein Ordner, der gerade noch geschrieben wird, sieht genauso aus: Vorschau und Bestaetigung des Fixes sagen es.
// games.undo_journals[]: path, name, kind (forge_undo, orig_part, undo), root. Neben einem Image liegt das Undo-Journal von Dump Forge (<Image>.forge-undo, *.orig.part)
//   oder des Converters (<Image>.undo): das Image wurde halb veraendert, das Werkzeug stellt es beim naechsten Oeffnen zurueck (S33). Medic fasst diese Dateien nie an.
// games.fakelib_layout[]: titleid, path, problem (both, empty_shadow, extra_ampr), detail, desc. S12: ShadowMount+ haengt je Spiel genau einen Ordner ein (fakelib2 vor fakelib,
//   auch ein leerer gewinnt); eine zweite gefuellte Fassung, ein leeres fakelib2 oder eine weitere libSceAmpr*.sprx (Debug-Fassung, Sicherung) wirkt nicht wie gedacht.
// games.with_fakelib[]: titleid, path -- Spiele (Ordner) mit mindestens einer Datei in fakelib oder fakelib2 (Grundlage von smp.fakelib_off_with_runtime, S13)
void collect_game_layout(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    json partial = json::array(), journals = json::array(), layout = json::array(), with_fakelib = json::array();
    std::set<std::string> seen;
    auto ends = [](const std::string& l, const char* s) {
        const std::size_t n = std::char_traits<char>::length(s);
        return l.size() > n && l.compare(l.size() - n, n, s) == 0;
    };
    if (const json* roots = f.get("smp.scan_roots"); roots && roots->is_array())
        for (const auto& r : *roots) {
            if (!r.is_string()) continue;
            const std::string root = r.get<std::string>();
            if (root.empty() || !seen.insert(root).second || !fs.is_dir(root)) continue;
            for (const auto& e : fs.list_dir(root)) {
                const std::string l = lower(e.name);
                if (!e.is_dir) {
                    const char* kind = ends(l, ".forge-undo") ? "forge_undo" : ends(l, ".orig.part") ? "orig_part" : nullptr;
                    if (!kind && ends(l, ".undo"))
                        for (const char* ext : {".exfat.undo", ".ffpfs.undo", ".ffpfsc.undo", ".ffpkg.undo", ".img.undo"})
                            if (ends(l, ext)) kind = "undo";
                    if (kind && journals.size() < 100) journals.push_back({{"path", join_path(root, e.name)}, {"name", e.name}, {"kind", kind}, {"root", root}});
                    continue;
                }
                if (e.name.empty() || e.name[0] == '.' || partial.size() >= 50) continue;
                int paks = 0, tmp = 0;
                bool index = false;
                for (const auto& k : fs.list_dir(join_path(root, e.name))) {
                    if (k.is_dir) continue;
                    const std::string kl = lower(k.name);
                    if (kl == "ampr_zstd.index") index = true;
                    else if (kl.rfind("ampr_zstd-", 0) == 0 && ends(kl, ".pak")) ++paks;
                    else if (kl.rfind(".ampr_zstd-", 0) == 0 && ends(kl, ".tmp")) ++tmp;
                }
                if (!index && (paks > 0 || tmp > 0)) partial.push_back({{"path", join_path(root, e.name)}, {"name", e.name}, {"root", root}, {"paks", paks}, {"tmp", tmp}});
            }
        }

    // fakelib und fakelib2 je Spielordner (smp.titles); Images bleiben aussen vor (ihr Aufbau steht in games.runtime)
    std::set<std::string> done;
    int checked = 0;
    const json* titles = f.get("smp.titles");
    if (titles && titles->is_array())
        for (const auto& t : *titles) {
            if (!t.is_object() || !t.contains("path") || !t["path"].is_string() || !t.contains("titleid") || !t["titleid"].is_string()) continue;
            const std::string path = t["path"].get<std::string>(), id = t["titleid"].get<std::string>();
            if (!done.insert(path).second || !fs.is_dir(path)) continue;
            if (++checked > 200) break;
            std::vector<DirEntry> root_items = fs.list_dir(path);
            std::vector<DirEntry> one, two;
            bool has_one = false, has_two = false;
            for (const auto& it : root_items) {
                if (!it.is_dir) continue;
                const std::string il = lower(it.name);
                if (il == "fakelib") {
                    has_one = true;
                    one = fs.list_dir(join_path(path, it.name));
                } else if (il == "fakelib2") {
                    has_two = true;
                    two = fs.list_dir(join_path(path, it.name));
                }
            }
            if (!has_one && !has_two) continue;
            if (!one.empty() || !two.empty()) with_fakelib.push_back({{"titleid", id}, {"path", path}});
            auto add = [&](const char* problem, const std::string& detail) {
                if (layout.size() < 100) layout.push_back({{"titleid", id}, {"path", path}, {"problem", problem}, {"detail", detail}, {"desc", path + ": " + problem}});
            };
            if (!one.empty() && !two.empty()) add("both", "fakelib, fakelib2");
            else if (has_two && two.empty() && !one.empty()) add("empty_shadow", "fakelib2");
            std::string extras;
            for (const auto* dir : {&one, &two})
                for (const auto& it : *dir) {
                    const std::string il = lower(it.name);
                    if (!it.is_dir && il.rfind("libsceampr", 0) == 0 && it.name != "libSceAmpr.sprx") extras += (extras.empty() ? "" : ", ") + it.name;
                }
            if (!extras.empty()) add("extra_ampr", extras);
        }
    f.set("games.partial_folders", std::move(partial));
    f.set("games.undo_journals", std::move(journals));
    f.set("games.fakelib_layout", std::move(layout));
    f.set("games.with_fakelib", std::move(with_fakelib));
}

// games.unsigned_elf[], games.flex_small[], games.playgo_missing[], games.trophy_missing[] (S07, S08, S14, S24; nur Spielordner, keine Images). Alle vier sind Hinweise mit
// hohem Fehlalarmrisiko und werden deshalb nur gemeldet, wenn dazu die Spur stimmt: S07, S08 und S14 nur, wenn die Konsole fuer den Titel einen Fehlercode verzeichnet hat
// (S08 zusaetzlich mit ng-Laufzeit ab 0.3.4); S24 nur, wenn sce_sys/trophy2 Dateien hat, aber eine der drei Trophaeen-Dateien fehlt. Nur Namen und Kopfbytes, nie Inhalte.
void collect_game_hints(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    std::set<std::string> error_titles, ng_titles, image_paths;
    if (const json* ep = f.get("errors.pairs"); ep && ep->is_array())
        for (const auto& e : *ep)
            if (e.is_object() && e.contains("titleid") && e["titleid"].is_string()) error_titles.insert(e["titleid"].get<std::string>());
    if (const json* rt = f.get("games.runtime"); rt && rt->is_array())
        for (const auto& r : *rt)
            if (r.is_object() && r.value("lib_kind", std::string()) == "ng" && r.contains("titleid") && r["titleid"].is_string() && !r.value("old_version", false))
                ng_titles.insert(r["titleid"].get<std::string>());
    if (const json* imgs = f.get("smp.images"); imgs && imgs->is_array())
        for (const auto& i : *imgs)
            if (i.is_object() && i.contains("path") && i["path"].is_string()) image_paths.insert(i["path"].get<std::string>());
    bool playgo_emu = false;
    for (const auto& e : fs.list_dir("/data/shadowmount/emus"))
        if (lower(e.name).find("playgo") != std::string::npos) playgo_emu = true;

    json unsigned_elf = json::array(), flex = json::array(), playgo = json::array(), trophy = json::array();
    const json* titles = f.get("smp.titles");
    std::set<std::string> done;
    int checked = 0;
    const auto started = std::chrono::steady_clock::now();
    if (titles && titles->is_array())
        for (const auto& t : *titles) {
            if (!t.is_object() || !t.contains("path") || !t["path"].is_string() || !t.contains("titleid") || !t["titleid"].is_string()) continue;
            const std::string path = t["path"].get<std::string>(), id = t["titleid"].get<std::string>();
            if (image_paths.count(path) || !fs.is_dir(path) || !done.insert(path).second) continue;
            if (checked >= 60 || std::chrono::steady_clock::now() - started > std::chrono::seconds(6)) break;
            ++checked;
            FolderTree tree(fs, path);
            const bool has_error = error_titles.count(id) != 0;
            std::vector<Item> root, sce, tmp;
            tree.list("", root);

            // S07: Programmdateien als reine ELF (e_type 0xFE10/0xFE18) statt signiert
            if (has_error) {
                json files = json::array();
                const auto probe = [&](const std::string& rel) {
                    std::string head;
                    if (files.size() >= 3 || !tree.read_at(rel, 0, 0x12, head) || head.size() < 0x12 || head.compare(0, 4, "\x7f" "ELF") != 0) return;
                    const unsigned type = static_cast<unsigned char>(head[0x10]) | (static_cast<unsigned>(static_cast<unsigned char>(head[0x11])) << 8);
                    if (type == 0xFE10 || type == 0xFE18) files.push_back(rel);
                };
                if (find_item(root, "eboot.bin", false)) probe("eboot.bin");
                for (const char* d : {"sce_module", "prx", "fakelib"})
                    if (const Item* di = find_item(root, d, true)) {
                        tree.list(di->name, tmp);
                        int n = 0;
                        for (const auto& it : tmp)
                            if (!it.is_dir && ++n <= 40) probe(std::string(di->name) + "/" + it.name);
                    }
                if (!files.empty()) unsigned_elf.push_back({{"titleid", id}, {"path", path}, {"files", files}, {"desc", id + ": " + files[0].get<std::string>()}});
            }

            const Item* sd = find_item(root, "sce_sys", true);
            if (!sd) continue;
            tree.list(sd->name, sce);

            // S08: kernel.flexibleMemorySize in der param.json klein, mit ng-Laufzeit und Fehlercode
            if (has_error && ng_titles.count(id)) {
                std::string text;
                if (find_item(sce, "param.json", false) && tree.read_at("sce_sys/param.json", 0, 64u << 10, text)) {
                    const json j = json::parse(text, nullptr, false);
                    if (j.is_object() && j.contains("kernel") && j["kernel"].is_object() && j["kernel"].contains("flexibleMemorySize") && j["kernel"]["flexibleMemorySize"].is_number_unsigned()) {
                        const std::uint64_t v = j["kernel"]["flexibleMemorySize"].get<std::uint64_t>();
                        if (v < 429916160ull) flex.push_back({{"titleid", id}, {"path", path}, {"value", v}, {"desc", id + ": flexibleMemorySize " + std::to_string(v)}});
                    }
                }
            }

            // S14: PlayGo-Dateien im Spiel, aber kein libScePlayGo.sprx im fakelib und keine Emulator-Datei dafuer
            if (has_error && !playgo_emu && (find_item(sce, "playgo-scenario.json", false) || find_item(sce, "playgo-chunk.dat", false))) {
                bool stub = false;
                for (const char* d : {"fakelib", "fakelib2"})
                    if (const Item* fd = find_item(root, d, true)) {
                        tree.list(fd->name, tmp);
                        if (find_item(tmp, "libScePlayGo.sprx", false)) stub = true;
                    }
                if (!stub) playgo.push_back({{"titleid", id}, {"path", path}, {"desc", id + ": [!] sce_sys/playgo-*, fakelib/libScePlayGo.sprx"}});
            }

            // S24: Trophaeen-Ordner mit Dateien, aber eine der drei Dateien fehlt
            if (const Item* td = find_item(sce, "trophy2", true)) {
                std::vector<Item> tro;
                tree.list("sce_sys/" + td->name, tro);
                if (!tro.empty()) {
                    json missing = json::array();
                    if (!find_item(sce, "nptitle.dat", false)) missing.push_back("sce_sys/nptitle.dat");
                    if (!find_item(tro, "npbind.dat", false)) missing.push_back("sce_sys/trophy2/npbind.dat");
                    bool uds_ok = false;
                    if (const Item* ud = find_item(sce, "uds", true)) {
                        tree.list("sce_sys/" + ud->name, tmp);
                        uds_ok = find_item(tmp, "npbind.dat", false) != nullptr;
                    }
                    if (!uds_ok) missing.push_back("sce_sys/uds/npbind.dat");
                    if (!missing.empty()) trophy.push_back({{"titleid", id}, {"path", path}, {"missing", missing}, {"desc", id + ": " + missing[0].get<std::string>()}});
                }
            }
        }
    f.set("games.unsigned_elf", std::move(unsigned_elf));
    f.set("games.flex_small", std::move(flex));
    f.set("games.playgo_missing", std::move(playgo));
    f.set("games.trophy_missing", std::move(trophy));
}

// games.two_mounters[].{titleid,desc}: dieselbe Title-ID bei Kraken Mount und ShadowMount+ (Handbuch PS5 Cooling Center, an der Konsole am 14. Nov. 2026:
// Kraken haengt die Einhaengung von ShadowMount+ aus, "not ours, or stale; unmounting it", Starts scheitern mit "Device busy"). Quellen: die Logzeile von Kraken,
// Title-IDs in den Ordnern Kraken-Games (intern, USB, M.2) gegen smp.titles, und Quellen von ShadowMount+, die selbst in einem Ordner Kraken-Games liegen
// (scan depth 2 auf dem Hauptordner eines Laufwerks erfasst ihn mit). Nur Namen und param.json, keine Inhalte.
void collect_two_mounters(const Platform& p, Facts& f, const std::map<std::string, std::string>& from_log) {
    static const std::regex tid_re("((?:PPSA|CUSA)[0-9]{5})");
    auto tid_of_dir = [&](const std::string& dir) -> std::string {
        std::string text;
        if (!p.fs().read_file(dir + "/sce_sys/param.json", text) || text.size() > (1u << 20)) return {};
        const json j = json::parse(text, nullptr, false);
        if (!j.is_object()) return {};
        for (const char* key : {"titleId", "title_id", "TITLE_ID"})
            if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
        return {};
    };
    std::vector<std::string> roots = {"/data/Kraken-Games"};
    for (int i = 0; i < 8; ++i) roots.push_back("/mnt/usb" + std::to_string(i) + "/Kraken-Games");
    for (int i = 0; i < 3; ++i) roots.push_back("/mnt/ext" + std::to_string(i) + "/Kraken-Games");
    std::map<std::string, std::string> kraken;  // Title-ID -> Pfad im Ordner Kraken-Games
    for (const auto& r : roots) {
        if (!p.fs().is_dir(r)) continue;
        for (const auto& e : p.fs().list_dir(r)) {  // direkt darin oder in einem Unterordner (Handbuch)
            const std::string path = r + "/" + e.name;
            std::smatch m;
            if (!e.is_dir) {
                if (std::regex_search(e.name, m, tid_re)) kraken.emplace(m[1].str(), path);
                continue;
            }
            if (std::string t = tid_of_dir(path); !t.empty()) {
                kraken.emplace(t, path);
                continue;
            }
            for (const auto& c : p.fs().list_dir(path)) {
                const std::string sub = path + "/" + c.name;
                if (c.is_dir) {
                    if (std::string t = tid_of_dir(sub); !t.empty()) kraken.emplace(t, sub);
                } else if (std::regex_search(c.name, m, tid_re)) {
                    kraken.emplace(m[1].str(), sub);
                }
            }
        }
    }
    std::map<std::string, std::string> out = from_log;
    if (const json* t = f.get("smp.titles"); t && t->is_array())
        for (const auto& e : *t) {
            if (!e.is_object() || !e.contains("titleid") || !e["titleid"].is_string() || !e.contains("path") || !e["path"].is_string()) continue;
            const std::string tid = e["titleid"].get<std::string>(), path = e["path"].get<std::string>();
            if (path.find("/Kraken-Games/") != std::string::npos) out[tid] = tid + ": ShadowMount+ -> " + path;
            else if (auto k = kraken.find(tid); k != kraken.end()) out[tid] = tid + ": Kraken Mount " + k->second + " + ShadowMount+ " + path;
        }
    json arr = json::array();
    for (const auto& [tid, desc] : out) {
        if (arr.size() >= 20) break;
        arr.push_back({{"titleid", tid}, {"desc", desc}});
    }
    f.set("games.two_mounters", std::move(arr));
}

// kraken.sparse_extent[], kraken.sysex_readonly[], kraken.pkg_other[], kraken.pkg_truncated[], kraken.cache_block[], kraken.ampr_foreign[]: je path (der Paketdatei,
// wenn die Zeile sie nennt) und line. Kraken Mount schreibt seine Ablehnungen nur in /data/krakenmount/kraken.log (keine Kachel, kein Hinweis). Die Meldung zum
// Layout mit Luecke nennt als Abhilfe das Neu-Packen mit "--kraken stored"; sie zaehlt nicht zu pkg_other, obwohl sie ebenfalls "unsupported package" sagt.
// Nur Log lesen (Marc, 14. Nov. 2026: Kraken nur ueber kraken.log und config.toml).
void collect_kraken(const Platform& p, Facts& f) {
    // kraken.crashes[].{titleid,at,process,status,desc}: letzter Absturz je Titel aus /data/krakenmount/crashes.json (seit 0.3.2; {"<TID>": {at, process, folder, status}}).
    // Nur Abstuerze der letzten 14 Tage, wenn die Uhr bekannt ist (die Datei behaelt jeden Titel fuer immer).
    if (std::string cj; p.fs().read_file("/data/krakenmount/crashes.json", cj) && cj.size() < (1u << 20)) {
        const json m = json::parse(cj, nullptr, false);
        json out = json::array();
        const std::int64_t now = p.sys().now_epoch();
        static const std::regex tid_ok("^(PPSA|CUSA)[0-9]{5}$");
        if (m.is_object())
            for (const auto& [tid, c] : m.items()) {
                if (!c.is_object() || !std::regex_match(tid, tid_ok) || out.size() >= 20) continue;
                const std::int64_t at = c.value("at", std::int64_t{0});
                if (now > 0 && at > 0 && at < now - 14 * 86400) continue;
                const std::string status = c.value("status", std::string()), process = c.value("process", std::string());
                out.push_back({{"titleid", tid}, {"at", at}, {"process", process}, {"status", status}, {"desc", tid + ": " + (status.empty() ? std::string("?") : status)}});
            }
        f.set("kraken.crashes", std::move(out));
    }
    std::string text;
    std::map<std::string, std::string> foreign_unmount;  // Title-ID -> Beleg aus kraken.log ("not ours, or stale; unmounting it")
    if (!read_tail(p.fs(), "/data/krakenmount/kraken.log", 1u << 20, text)) {
        collect_two_mounters(p, f, foreign_unmount);
        return;
    }
    f.set("kraken.log_present", true);
    // seit 0.4.2 haengt Kraken auch Images ein (.exfat, .ffpkg, .ffpfs, .ffpfsc)
    static const std::regex path_re("(/[^\\s\"']+\\.(?:pkg|exfat|ffpkg|ffpfs|ffpfsc))");
    static const std::regex tid_re("((?:PPSA|CUSA)[0-9]{5})");
    struct Kind {
        explicit Kind(const char* name) : fact(name) {}
        const char* fact;
        json out = json::array();
        std::set<std::string> seen;
    };
    Kind sparse("kraken.sparse_extent"), sysex("kraken.sysex_readonly"), other("kraken.pkg_other"), trunc("kraken.pkg_truncated"), cache("kraken.cache_block"),
        foreign("kraken.ampr_foreign"), kstuff("kraken.kstuff_missing"), launch("kraken.launch_failed");
    bool waiting = false;  // letzter Stand im Log: "waiting before start-up: shadowmount" bis "shadowmount: ready"
    const auto has = [](const std::string& l, const char* s) { return l.find(s) != std::string::npos; };
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (has(line, "not ours, or stale")) {
            static const std::regex tid_only("((?:PPSA|CUSA)[0-9]{5})");
            std::smatch t;
            if (std::regex_search(line, t, tid_only)) foreign_unmount.emplace(t[1].str(), t[1].str() + ": kraken.log \"not ours, or stale; unmounting it\"");
        }
        if (has(line, "waiting before start-up: shadowmount")) waiting = true;
        else if (has(line, "shadowmount: ready")) waiting = false;
        Kind* k = nullptr;
        if (has(line, "the layout has a sparse extent")) k = &sparse;
        else if (has(line, "/system_ex") && has(line, "Read-only filesystem")) k = &sysex;
        else if (has(line, "(truncated?)")) k = &trunc;
        else if (has(line, "cache build:") && has(line, "short compressed block")) k = &cache;
        else if (has(line, "is not a bundled ampr-emu-ng")) k = &foreign;
        else if (has(line, "did not start") && has(line, "kstuff is not running")) k = &kstuff;
        else if (has(line, "overlay mount FAILED") || has(line, "sandbox /app0 rw mount FAILED") || has(line, "backport redirects FAILED")) k = &launch;
        else if (has(line, "unsupported package") || has(line, "not a PS5 package")) k = &other;
        if (!k) continue;
        cap_line(line);
        std::smatch m;
        const std::string path = std::regex_search(line, m, path_re) ? m[1].str() : std::string();
        if (!path.empty() && !p.fs().exists(path)) continue;  // das Paket wurde seither neu gebaut oder entfernt: die alte Logzeile gilt nicht mehr
        // seit 0.3.1 beginnt jede Zeile mit der Uhrzeit: ohne Pfad zaehlt je Titel (bzw. je Text ab der Meldung) nur ein Eintrag
        std::smatch t;
        const std::string tid = std::regex_search(line, t, tid_re) ? t[1].str() : std::string();
        const std::string key = !path.empty() ? path : (!tid.empty() ? tid : line.substr(std::min(line.size(), line.find_first_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ/"))));
        if (!k->seen.insert(key).second || k->out.size() >= 20) continue;
        json e = {{"path", path}, {"line", line}};
        if (!tid.empty()) e["titleid"] = tid;
        k->out.push_back(std::move(e));
    }
    for (Kind* k : {&sparse, &sysex, &other, &trunc, &cache, &foreign, &kstuff, &launch}) f.set(k->fact, std::move(k->out));
    // Kraken wartet, solange ShadowMount+ laeuft (Quelltext 0.4.2, main.rs): nur melden, wenn das Log zuletzt "wartet" sagt UND ShadowMount+ wirklich laeuft
    bool smp = false;
    if (const json* pr = f.get("proc.running"); pr && pr->is_array())  // collect_processes laeuft vorher (Namen klein, ohne ".elf")
        for (const auto& n : *pr)
            if (n.is_string() && n.get<std::string>() == "shadowmountplus") smp = true;
    f.set("kraken.waiting_for_smp", waiting && smp);
    collect_two_mounters(p, f, foreign_unmount);
}

// Kraken Mount, lokale API (Marc, 28. Nov. 2026: lesend erlaubt; nur GET 127.0.0.1:8097 /api/state und /api/ampr, Quelltext 0.4.2 web.rs). Laeuft Kraken nicht,
// fehlen alle Fakten. kraken.api.version, kraken.api.games_error[].{titleid,error,desc} (Status "error"), kraken.api.games_duplicate[].{titleid,path,desc}
// (Status "duplicate": dieselbe Title-ID mehrfach), kraken.api.cache_low_space und kraken.api.cache_free (Byte), kraken.api.ampr_kind
// (bundled|downloaded|hand|missing|error) und kraken.api.ampr_version. Titelnamen werden bewusst nicht uebernommen (nur Title-IDs).
void collect_kraken_api(const Platform& p, Facts& f) {
    std::string body;
    if (!p.sys().local_api_get(8097, "/api/state", body)) return;
    const json s = json::parse(body, nullptr, false);
    if (!s.is_object()) return;
    f.set("kraken.api.present", true);
    if (s.contains("version") && s["version"].is_string()) f.set("kraken.api.version", s["version"].get<std::string>());
    static const std::regex tid_ok("^(PPSA|CUSA)[0-9]{5}$");
    json err = json::array(), dup = json::array();
    if (s.contains("games") && s["games"].is_array())
        for (const auto& g : s["games"]) {
            if (!g.is_object()) continue;
            const std::string tid = g.value("title_id", std::string()), status = g.value("status", std::string());
            if (!std::regex_match(tid, tid_ok)) continue;
            if (status == "error" && err.size() < 50) {
                std::string e = g.contains("error") && g["error"].is_string() ? g["error"].get<std::string>() : std::string();
                if (e.size() > 200) e.resize(200);
                err.push_back({{"titleid", tid}, {"error", e}, {"desc", tid + ": " + (e.empty() ? std::string("?") : e)}});
            } else if (status == "duplicate" && dup.size() < 50) {
                const std::string path = g.contains("path") && g["path"].is_string() ? g["path"].get<std::string>() : std::string();
                dup.push_back({{"titleid", tid}, {"path", path}, {"desc", tid + ": " + path}});
            }
        }
    f.set("kraken.api.games_error", std::move(err));
    f.set("kraken.api.games_duplicate", std::move(dup));
    if (s.contains("cache") && s["cache"].is_object()) {
        const json& c = s["cache"];
        if (c.contains("low_space") && c["low_space"].is_boolean()) f.set("kraken.api.cache_low_space", c["low_space"].get<bool>());
        if (c.contains("free") && c["free"].is_number()) f.set("kraken.api.cache_free", c["free"].get<double>());
    }
    std::string ab;
    if (p.sys().local_api_get(8097, "/api/ampr", ab)) {
        const json a = json::parse(ab, nullptr, false);
        if (a.is_object()) {
            if (a.contains("kind") && a["kind"].is_string()) f.set("kraken.api.ampr_kind", a["kind"].get<std::string>());
            if (a.contains("version") && a["version"].is_string()) f.set("kraken.api.ampr_version", a["version"].get<std::string>());
        }
    }
}

// ---- Abgleich mit Kraken Mount 0.4.2, ampr-emu-ng 0.5.1 und Dump Forge 0.6.0 (28. Nov. 2026, nur Quelltext und Releases gelesen) -------------------------------

// ampr.trace_problems[].{kind,line}: die "Black Box" der Debug-Laufzeit (/data/ampr_emu_trace.txt, docs/debug-build.md des Projekts). Sie wird bei jedem Spielstart
// neu geschrieben, gilt also fuer den letzten Start. Gemeldet werden nur Zeilen, die einen Fehler belegen: Index abgelehnt ("idx Invalid|Memory|Unreadable"),
// Ersatzaufbau oder Verbindung der Packs gescheitert ("idx walk fail", "idx join fail"), /app0 nicht beschreibbar ("rw off errno="), .pkg-Erkennung gescheitert
// ("pkg badimage|open|notregular|nomanifest|size"). "idx Missing" fehlt absichtlich: ohne Packs hat ein Spiel oft gar keinen Index.
// ampr.stale_cache: /data/ampr_cache (mmap-Plattencache bis ampr-emu-ng 0.4.1) ist nicht leer, und kein Spiel nutzt ng ab 0.5.0 (das raeumt ihn beim Start selbst auf).
void collect_ampr_trace(const Platform& p, Facts& f) {
    std::string text;
    if (read_tail(p.fs(), "/data/ampr_emu_trace.txt", 1u << 20, text)) {
        json out = json::array();
        std::set<std::string> seen;
        static const std::pair<const char*, const char*> kMarks[] = {
            {"idx Invalid", "index_invalid"},   {"idx Memory", "index_memory"},  {"idx Unreadable", "index_unreadable"}, {"idx walk fail", "index_walk_fail"},
            {"idx join fail", "pack_join_fail"}, {"rw off errno=", "app0_readonly"}, {"pkg badimage", "pkg_discovery"},     {"pkg open ", "pkg_discovery"},
            {"pkg notregular", "pkg_discovery"}, {"pkg nomanifest", "pkg_discovery"}, {"pkg size ", "pkg_discovery"}};
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line) && out.size() < 20)
            for (const auto& [mark, kind] : kMarks) {
                if (line.find(mark) == std::string::npos) continue;
                cap_line(line);
                if (seen.insert(kind).second) out.push_back({{"kind", kind}, {"line", line}});
                break;
            }
        f.set("ampr.trace_present", true);
        f.set("ampr.trace_problems", std::move(out));
    }
    bool cache = false;
    for (const auto& e : p.fs().list_dir("/data/ampr_cache")) {
        (void)e;
        cache = true;
        break;
    }
    if (!cache) return;
    bool new_runtime = false, any_ng = false;
    if (const json* rt = f.get("games.runtime"); rt && rt->is_array())
        for (const auto& r : *rt) {
            if (!r.is_object() || r.value("lib_kind", std::string()) != "ng") continue;
            any_ng = true;
            const std::string v = r.value("lib_version", std::string());
            if (!v.empty() && compare_versions(v, "0.5.0") >= 0) new_runtime = true;
        }
    if (any_ng) f.set("ampr.stale_cache", !new_runtime);
}

// forge.version: Version der installierten Dump-Forge-Payload. Sie kopiert sich nach /data/dumpforge/ps5-dump-forge.elf (aeltere Fassungen nach /data/ps5-dump-forge/)
// und traegt die Marke "ps5-dump-forge-version:X.Y.Z" (Quelltext 0.6.0, lib.rs und self_copy.rs). Ab welcher Fassung die Marke da ist, ist offen: fehlt sie, fehlt der Fakt.
void collect_dumpforge(const Platform& p, Facts& f) {
    static const std::string kMark = "ps5-dump-forge-version:";
    for (const char* path : {"/data/dumpforge/ps5-dump-forge.elf", "/data/ps5-dump-forge/ps5-dump-forge.elf"}) {
        std::string b;
        if (!p.fs().read_file(path, b)) continue;
        const std::size_t pos = b.find(kMark);
        if (pos == std::string::npos) continue;
        std::size_t e = pos + kMark.size();
        while (e < b.size() && e - pos - kMark.size() < 24 && (std::isdigit(static_cast<unsigned char>(b[e])) || b[e] == '.')) ++e;
        const std::string v = b.substr(pos + kMark.size(), e - pos - kMark.size());
        if (v.empty() || !std::isdigit(static_cast<unsigned char>(v[0]))) continue;
        f.set("forge.version", v);
        f.set("forge.path", std::string(path));
        f.set("forge.before_056", compare_versions(v, "0.5.6") < 0);
        return;
    }
}

// games.p5z[].{path,name,truncated,desc}: .p5z-Dateien (Format von MaxPack, docs/p5z-format.md: Kopf "MAXPKP5Z", Fuss "P5ZFOOT1" in den letzten 32 Byte, der Fuss wird
// zuletzt geschrieben) direkt in einem Scanpfad oder eine Ebene darunter. ShadowMount+, Kraken Mount 0.4.2 und ampr-emu-ng 0.5.1 lesen das Format nicht: Es ist ein
// Behaelter zum Aufbewahren, vor dem Spielen wird er ausgepackt. truncated: Kopf da, Fuss fehlt (Uebertragung abgebrochen).
void collect_p5z(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    const json* roots = f.get("smp.scan_roots");
    if (!roots || !roots->is_array()) return;
    json out = json::array();
    auto ends_p5z = [](const std::string& n) {
        if (n.size() < 5) return false;
        std::string t = n.substr(n.size() - 4);
        for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return t == ".p5z";
    };
    auto check = [&](const std::string& dir, const DirEntry& e) {
        if (e.is_dir || !ends_p5z(e.name) || out.size() >= 50) return;
        const std::string path = join_path(dir, e.name);
        std::string head, foot;
        if (!fs.read_range(path, 0, 8, head) || head != "MAXPKP5Z") return;  // keine MaxPack-Datei: nichts behaupten
        const bool truncated = e.size < 96 || !fs.read_range(path, e.size - 32, 8, foot) || foot != "P5ZFOOT1";
        out.push_back({{"path", path}, {"name", e.name}, {"truncated", truncated}, {"desc", e.name + (truncated ? " (unvollständig)" : "")}});
    };
    for (const auto& r : *roots) {
        if (!r.is_string()) continue;
        const std::string root = r.get<std::string>();
        for (const auto& e : fs.list_dir(root)) {
            if (e.is_dir && !e.name.empty() && e.name[0] != '.') {
                for (const auto& s : fs.list_dir(join_path(root, e.name))) check(join_path(root, e.name), s);
            } else {
                check(root, e);
            }
        }
    }
    f.set("games.p5z", std::move(out));
}

// Verwaiste Inhalte (23. Nov. 2026; Vorbild: orphan-scan in ps5-tools von CreamyG31337, MIT, dort ueber FTP): Medic liest nur Ordner- und Dateinamen, nie Inhalte (D17).
//  - Zusatzinhalte und Updates (/user/addcont/<ID>, /user/patch/<ID>), deren Spiel fehlt,
//  - Lizenzen (/user/license/*.rif; die Titel-ID steht im Dateinamen, die Datei wird nicht geoeffnet), deren Spiel fehlt,
//  - unerledigte Download- und Installationsauftraege (/user/bgft/task/<Auftrag>, je ein Ordner).
// "Spiel da" heisst: installiert (/user/app und /system_ex/app, dazu Erweiterungsspeicher /mnt/ext0 bis ext2 und /mnt/m2) ODER eine Quelle ist bekannt (smp.titles, Kraken-Cache):
// Ein Spiel auf einem getrennten USB-Laufwerk ist nicht verwaist, und Kraken Mount behaelt Zusatzinhalte absichtlich. Ist /user/app leer oder nicht lesbar, fehlen alle
// Fakten (kein Beweis). An den Test-Konsolen am 23. Nov. 2026 gezaehlt: je 0 Zusatzinhalte ohne Spiel und 0 Auftraege, 2 bzw. 3 Lizenzen ohne installiertes Spiel.
// content.installed_ids[] und content.source_ids[] (nur fuer den Snapshot-Export), content.dlc_without_game[].{titleid,kind,path,desc}, content.license_without_game[].{titleid,file,desc},
// content.download_tasks[].{name,desc}
void collect_content_leftovers(const Platform& p, Facts& f) {
    const IFileSystem& fs = p.fs();
    static const std::regex game_id(R"(^(PPSA|CUSA)[0-9]{5}$)");
    static const std::regex id_in_name(R"((PPSA|CUSA)[0-9]{5})");
    static const char* const kRoots[] = {"", "/mnt/ext0", "/mnt/ext1", "/mnt/ext2", "/mnt/m2"};

    std::set<std::string> installed;
    auto add_ids = [&](const std::string& dir) {
        for (const auto& e : fs.list_dir(dir))
            if (e.is_dir && std::regex_match(e.name, game_id)) installed.insert(e.name);
    };
    for (const char* root : kRoots) add_ids(std::string(root) + "/user/app");
    add_ids("/system_ex/app");
    if (installed.empty()) return;

    std::set<std::string> known = installed;  // installiert oder als Quelle bekannt
    if (const json* t = f.get("smp.titles"); t && t->is_array())
        for (const auto& e : *t)
            if (e.is_object() && e.contains("titleid") && e["titleid"].is_string()) known.insert(e["titleid"].get<std::string>());
    for (const auto& e : fs.list_dir("/data/krakenmount/caches"))
        if (e.is_dir && std::regex_match(e.name, game_id)) known.insert(e.name);

    json ids = json::array(), sources = json::array();
    for (const auto& id : installed)
        if (ids.size() < 500) ids.push_back(id);
    for (const auto& id : known)
        if (!installed.count(id) && sources.size() < 500) sources.push_back(id);
    f.set("content.installed_ids", std::move(ids));
    f.set("content.source_ids", std::move(sources));

    json dlc = json::array();
    for (const char* kind : {"addcont", "patch"})
        for (const char* root : kRoots) {
            const std::string dir = std::string(root) + "/user/" + kind;
            for (const auto& e : fs.list_dir(dir))
                if (e.is_dir && std::regex_match(e.name, game_id) && !known.count(e.name) && dlc.size() < 100)
                    dlc.push_back({{"titleid", e.name}, {"kind", kind}, {"path", dir + "/" + e.name}, {"desc", e.name + " (" + kind + ")"}});
        }
    f.set("content.dlc_without_game", std::move(dlc));

    json lic = json::array();
    std::set<std::string> lic_seen;
    for (const char* root : kRoots)
        for (const auto& e : fs.list_dir(std::string(root) + "/user/license")) {
            const std::string l = lower(e.name);
            if (e.is_dir || l.size() < 5 || l.compare(l.size() - 4, 4, ".rif") != 0) continue;
            std::smatch m;
            if (!std::regex_search(e.name, m, id_in_name)) continue;
            const std::string tid = m[0].str();
            if (!known.count(tid) && lic_seen.insert(tid).second && lic.size() < 100) lic.push_back({{"titleid", tid}, {"file", e.name}, {"desc", tid}});
        }
    f.set("content.license_without_game", std::move(lic));

    json tasks = json::array();
    for (const auto& e : fs.list_dir("/user/bgft/task"))
        if (e.is_dir && tasks.size() < 20) tasks.push_back({{"name", e.name}, {"desc", e.name}});
    f.set("content.download_tasks", std::move(tasks));
}

}  // namespace medic
