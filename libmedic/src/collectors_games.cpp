// SPDX-License-Identifier: GPL-3.0-or-later
// Collectors fuer Spiele (Auswahl vom 14. Nov. 2026, ampr-emu-ng, kraken-mount, Dump Forge): Laufzeit-Bibliothek und Packs im Spiel (Ordner oder Image),
// unvollstaendige Spielordner in den Scanpfaden, Meldungen von Kraken Mount. Nur lesen; aus Images wird nur der Anfang weniger Dateien gelesen.
#include "medic/collectors.hpp"
#include "medic/compat.hpp"
#include "medic/image_reader.hpp"
#include "medic/readtail.hpp"

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
    li.debug = b.find("debugpack_format") != std::string::npos;
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
                       {"save_risk", save_errors && li.kind == "ng" && (li.version == "0.3.5" || li.version == "0.4.0")},
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

// kraken.sparse_extent[]: path (der Paketdatei, wenn die Zeile sie nennt), line. Kraken Mount lehnt ein .pkg mit einer Luecke im Layout ab und schreibt die Meldung
// nur in /data/krakenmount/kraken.log (keine Kachel, kein Hinweis); die Meldung nennt als Abhilfe das Neu-Packen mit "--kraken stored".
// Nur Log lesen (Marc, 14. Nov. 2026: Kraken nur ueber kraken.log und config.toml).
void collect_kraken(const Platform& p, Facts& f) {
    std::string text;
    if (!read_tail(p.fs(), "/data/krakenmount/kraken.log", 1u << 20, text)) return;
    f.set("kraken.log_present", true);
    json out = json::array();
    static const std::regex path_re("(/[^\\s\"']+\\.pkg)");
    std::set<std::string> seen;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("the layout has a sparse extent") == std::string::npos) continue;
        cap_line(line);
        std::smatch m;
        const std::string path = std::regex_search(line, m, path_re) ? m[1].str() : std::string();
        if (!path.empty() && !p.fs().exists(path)) continue;  // das Paket wurde seither neu gebaut oder entfernt: die alte Logzeile gilt nicht mehr
        if (!seen.insert(path.empty() ? line : path).second || out.size() >= 20) continue;
        out.push_back({{"path", path}, {"line", line}});
    }
    f.set("kraken.sparse_extent", std::move(out));
}

}  // namespace medic
