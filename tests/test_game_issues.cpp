// SPDX-License-Identifier: GPL-3.0-or-later
// Tests zu den Spiel-Fehlerbildern, die Marc am 14. Nov. 2026 aus der Auswahl (ampr-emu-ng, kraken-mount, Dump Forge, Converter) gewaehlt hat:
// Programmdateien ohne Ausfuehrungsrecht (CE-107750-0), Reste abgebrochener Packvorgaenge, zu lange Image-Namen und weitere.
#include "check.hpp"
#include "medic/collectors.hpp"
#include "medic/compat.hpp"
#include "medic/exfat.hpp"
#include "medic/fixes.hpp"
#include "medic/history.hpp"
#include "medic/games.hpp"
#include "medic/image_reader.hpp"
#include "medic/posix_fs.hpp"
#include "medic/rules.hpp"
#include "testutil.hpp"

#include <nlohmann/json.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace medic;
using nlohmann::json;
using testutil::Sandbox;
using testutil::spit;

namespace {

namespace stdfs = std::filesystem;

// Plattform mit Dateimodi (wie die Konsole) und Schreibzugriff im Sandbox-Ordner; Prozesse und System kommen vom Snapshot-Ersatz.
struct ModePlatform final : Platform {
    std::unique_ptr<Platform> base;
    PosixFs fs_;
    PosixRw rw_;
    ModePlatform(std::unique_ptr<Platform> b, const stdfs::path& root) : base(std::move(b)), fs_(root, /*modes=*/true), rw_(root) {}
    const IFileSystem& fs() const override { return fs_; }
    const IProcessList& procs() const override { return base->procs(); }
    const ISystemInfo& sys() const override { return base->sys(); }
    IFileSystemRW* fs_rw() override { return &rw_; }
};

void set_mode(const stdfs::path& p, unsigned mode) { stdfs::permissions(p, static_cast<stdfs::perms>(mode), stdfs::perm_options::replace); }
unsigned mode_of(const stdfs::path& p) { return static_cast<unsigned>(stdfs::status(p).permissions()) & 07777u; }

const Finding* find_rule(const std::vector<Finding>& fs, const std::string& id) {
    for (const auto& f : fs)
        if (f.rule_id == id) return &f;
    return nullptr;
}

// CE-107750-0: eboot.bin und Module ohne Ausfuehrungsrecht
void chmod_tests() {
    Sandbox sb;
    const std::string game = "/data/homebrew/PPSA00001-Test";
    spit(sb.real(game + "/eboot.bin"), "ELF-Platzhalter");
    spit(sb.real(game + "/sce_module/libfoo.prx"), "x");
    spit(sb.real(game + "/sce_module/ok.sprx"), "x");
    spit(sb.real(game + "/Media/Modules/Unity.prx"), "x");
    spit(sb.real(game + "/Media/data.bin"), "daten");
    set_mode(sb.real(game + "/eboot.bin"), 0666);
    set_mode(sb.real(game + "/sce_module/libfoo.prx"), 0666);
    set_mode(sb.real(game + "/sce_module/ok.sprx"), 0755);
    set_mode(sb.real(game + "/Media/Modules/Unity.prx"), 0644);
    set_mode(sb.real(game + "/Media/data.bin"), 0666);  // eine Datendatei braucht kein Ausfuehrungsrecht: nie melden oder aendern

    ModePlatform p(make_snapshot_platform(sb.dir.string(), true), sb.dir / "fs");
    Facts f;
    f.set("smp.titles", json::array({{{"titleid", "PPSA00001"}, {"path", game}, {"root", "/data/homebrew"}}}));
    f.set("smp.scan_roots", json::array({"/data/homebrew"}));
    collect_game_files(p, f);
    const json* ne = f.get("games.noexec");
    CHECK(ne && ne->is_array() && ne->size() == 1, "Rechte: ein Spiel mit Programmdateien ohne Ausfuehrungsrecht");
    CHECK(ne && !ne->empty() && (*ne)[0]["count"] == 3 && (*ne)[0]["files"].size() == 3, "Rechte: eboot.bin, libfoo.prx und Unity.prx gezaehlt");
    std::string listed;
    if (ne && !ne->empty())
        for (const auto& x : (*ne)[0]["files"]) listed += x.get<std::string>() + "\n";
    CHECK(listed.find("ok.sprx") == std::string::npos && listed.find("data.bin") == std::string::npos, "Rechte: ausfuehrbare Datei und Datendatei nicht gemeldet");

    // Wo die Plattform keine Modi kennt (PC-Fixture), gibt es keinen Fakt-Eintrag (sonst meldete jedes Fixture aus dem Git-Checkout)
    {
        auto plain = make_snapshot_platform(sb.dir.string(), false);
        Facts g;
        g.set("smp.titles", json::array({{{"titleid", "PPSA00001"}, {"path", game}}}));
        collect_game_files(*plain, g);
        const json* gn = g.get("games.noexec");
        CHECK(gn && gn->is_array() && gn->empty(), "Rechte: ohne Modus-Unterstuetzung kein Befund");
    }

    // Regel und Fix
    const RuleSet rs = load_rules_from_file(testutil::kShipped);
    const auto findings = evaluate(rs, f);
    const Finding* fd = find_rule(findings, "game.no_exec_permission");
    CHECK(fd && fd->fix == "chmod_program_files", "Rechte: Regel feuert mit dem Fix chmod_program_files");
    if (!fd) return;
    FixPlan plan = plan_fix(*fd, f, p.fs());
    CHECK(plan.changes.size() == 3, "Rechte: Plan mit drei Aenderungen");
    bool kinds_ok = !plan.changes.empty();
    for (const auto& c : plan.changes) kinds_ok = kinds_ok && c.kind == Change::Kind::Chmod && c.mode == 0777 && (c.base_mode == 0666 || c.base_mode == 0644);
    CHECK(kinds_ok, "Rechte: jede Aenderung setzt 0777 und kennt den alten Modus");

    // Ausserhalb der Whitelist (kein Scanpfad): nichts geplant
    {
        Facts nofacts = f;
        nofacts.set("smp.scan_roots", json::array());
        const FixPlan none = plan_fix(*fd, nofacts, p.fs());
        CHECK(none.changes.empty() && !none.warnings.empty(), "Rechte: ausserhalb der erlaubten Ordner wird nichts geplant");
    }

    // Aendert jemand die Rechte nach der Vorschau, lehnt apply_plan ab
    {
        set_mode(sb.real(game + "/eboot.bin"), 0755);
        const ApplyResult r = apply_plan(plan, p, policy_from_facts(f), {"20261114T120000Z", true});
        CHECK(!r.ok && r.error.find("seit der Vorschau") != std::string::npos, "Rechte: geaenderte Rechte seit der Vorschau werden abgelehnt: " + r.error);
        CHECK(mode_of(sb.real(game + "/sce_module/libfoo.prx")) == 0666, "Rechte: bei Ablehnung bleibt alles unveraendert");
        set_mode(sb.real(game + "/eboot.bin"), 0666);
    }

    // Anwenden
    const ApplyResult ar = apply_plan(plan, p, policy_from_facts(f), {"20261114T120100Z", true});
    CHECK(ar.ok, "Rechte: Anwenden ok (" + ar.error + ")");
    CHECK(mode_of(sb.real(game + "/eboot.bin")) == 0777 && mode_of(sb.real(game + "/sce_module/libfoo.prx")) == 0777 && mode_of(sb.real(game + "/Media/Modules/Unity.prx")) == 0777,
          "Rechte: Programmdateien stehen auf 0777");
    CHECK(mode_of(sb.real(game + "/sce_module/ok.sprx")) == 0755 && mode_of(sb.real(game + "/Media/data.bin")) == 0666, "Rechte: andere Dateien bleiben unberuehrt");
    CHECK(testutil::slurp(sb.real(game + "/eboot.bin")) == "ELF-Platzhalter", "Rechte: der Inhalt bleibt unberuehrt");

    // Rechte jetzt in Ordnung: ein zweiter Check-up meldet nichts mehr
    {
        Facts again;
        again.set("smp.titles", json::array({{{"titleid", "PPSA00001"}, {"path", game}}}));
        collect_game_files(p, again);
        const json* an = again.get("games.noexec");
        CHECK(an && an->empty(), "Rechte: nach der Reparatur kein Befund mehr");
    }

    // Rueckgaengig
    const UndoResult ur = undo_txn(p, policy_from_facts(f), ar.txn, "20261114T120200Z");
    CHECK(ur.ok, "Rechte: Rueckgaengig ok (" + ur.error + ")");
    CHECK(mode_of(sb.real(game + "/eboot.bin")) == 0666 && mode_of(sb.real(game + "/sce_module/libfoo.prx")) == 0666 && mode_of(sb.real(game + "/Media/Modules/Unity.prx")) == 0644,
          "Rechte: Rueckgaengig stellt die alten Rechte her");
    bool undone = false;
    for (const auto& t : history(p))
        if (t.txn == ar.txn && t.state == "undone") undone = true;
    CHECK(undone, "Rechte: der Verlauf zeigt die Transaktion als rueckgaengig gemacht");

    // Wurde eine Datei nach dem Fix erneut veraendert, fasst Rueckgaengig nichts an
    const ApplyResult ar2 = apply_plan(plan_fix(*fd, f, p.fs()), p, policy_from_facts(f), {"20261114T120300Z", true});
    CHECK(ar2.ok, "Rechte: zweites Anwenden ok (" + ar2.error + ")");
    set_mode(sb.real(game + "/sce_module/libfoo.prx"), 0700);  // jemand anderes hat die Rechte inzwischen gesetzt
    const UndoResult ur2 = undo_txn(p, policy_from_facts(f), ar2.txn, "20261114T120400Z");
    CHECK(!ur2.ok && mode_of(sb.real(game + "/eboot.bin")) == 0777, "Rechte: nach fremder Aenderung bricht Rueckgaengig ab und fasst nichts an: " + ur2.error);
}

bool valid_utf8(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        for (std::size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

// Reste abgebrochener Kopier- und Packvorgaenge in den Scanpfaden (S31)
void leftover_tests() {
    Sandbox sb;
    const std::string root = "/data/homebrew";
    spit(sb.real(root + "/Muster.exfat.7-4242.part"), "rest eines abgebrochenen packvorgangs");
    spit(sb.real(root + "/.Muster.forge-123-4.tmp"), "tmp");
    spit(sb.real(root + "/Muster.teil"), "teil");
    spit(sb.real(root + "/.forge-probe-99.part"), "probe");
    spit(sb.real(root + "/Muster.exfat"), std::string(1024, 'A'));        // das fertige Image: nie anfassen
    spit(sb.real(root + "/Muster.exfat.complete"), "ok");                 // Markierung, kein Rest
    spit(sb.real(root + "/Muster.filepart"), "x");                        // endet nicht auf ".part"
    spit(sb.real(root + "/Spiel/Daten.part"), "tief");                    // nur direkt im Scanpfad wird gesucht
    spit(sb.real(root + "/.medic_broken/Alt.part"), "schon beiseite");    // der Ordner von Medic selbst wird nicht erneut gemeldet

    auto p = make_snapshot_platform(sb.dir.string(), true);
    Facts f;
    f.set("smp.scan_roots", json::array({root}));
    collect_leftovers(*p, f);
    const json* lf = f.get("leftovers.files");
    CHECK(lf && lf->is_array() && lf->size() == 4, "Reste: vier Reste gefunden, nicht das fertige Image, nicht die Markierung");
    std::string names;
    if (lf)
        for (const auto& x : *lf) names += x["name"].get<std::string>() + "\n";
    CHECK(names.find("Muster.exfat\n") == std::string::npos && names.find("complete") == std::string::npos && names.find("filepart") == std::string::npos &&
              names.find("Daten.part") == std::string::npos && names.find("Alt.part") == std::string::npos,
          "Reste: Image, Markierung, .filepart, tiefe Dateien und der Medic-Ordner bleiben aussen vor");

    const RuleSet rs = load_rules_from_file(testutil::kShipped);
    const auto findings = evaluate(rs, f);
    const Finding* fd = find_rule(findings, "game.leftover_parts");
    CHECK(fd && fd->fix == "quarantine_leftovers", "Reste: Regel feuert mit dem Fix quarantine_leftovers");
    if (!fd) return;
    FixPlan plan = plan_fix(*fd, f, p->fs());
    CHECK(plan.changes.size() == 4, "Reste: Plan mit vier Aenderungen");
    bool same_dir = !plan.changes.empty();
    for (const auto& c : plan.changes) same_dir = same_dir && c.kind == Change::Kind::Rename && c.to.rfind(root + "/.medic_broken/", 0) == 0 && c.check_size;
    CHECK(same_dir, "Reste: alle gehen in .medic_broken im selben Ordner, mit Groessenpruefung");

    // Waechst eine Datei noch (ein Job laeuft), lehnt apply_plan ab
    {
        spit(sb.real(root + "/Muster.teil"), "teil, jetzt laenger");
        PosixRw rw(sb.dir / "fs");
        struct W final : Platform {
            Platform* base;
            PosixRw* rw_;
            W(Platform* b, PosixRw* r) : base(b), rw_(r) {}
            const IFileSystem& fs() const override { return base->fs(); }
            const IProcessList& procs() const override { return base->procs(); }
            const ISystemInfo& sys() const override { return base->sys(); }
            IFileSystemRW* fs_rw() override { return rw_; }
        } wp(p.get(), &rw);
        const ApplyResult r = apply_plan(plan, wp, policy_from_facts(f), {"20261114T130000Z", true});
        CHECK(!r.ok && r.error.find("seit der Vorschau") != std::string::npos, "Reste: eine noch wachsende Datei wird nicht beiseitegelegt: " + r.error);
        CHECK(stdfs::exists(sb.real(root + "/Muster.exfat.7-4242.part")), "Reste: bei Ablehnung bleibt alles liegen");

        plan = plan_fix(*fd, f, p->fs());  // neuer Plan mit der neuen Groesse
        const ApplyResult ok = apply_plan(plan, wp, policy_from_facts(f), {"20261114T130100Z", true});
        CHECK(ok.ok, "Reste: Anwenden ok (" + ok.error + ")");
        CHECK(!stdfs::exists(sb.real(root + "/Muster.teil")) && testutil::slurp(sb.real(root + "/.medic_broken/Muster.teil")) == "teil, jetzt laenger", "Reste: Datei liegt unveraendert in .medic_broken");
        CHECK(stdfs::exists(sb.real(root + "/Muster.exfat")) && stdfs::exists(sb.real(root + "/Spiel/Daten.part")), "Reste: andere Dateien bleiben unberuehrt");
        Facts again;
        again.set("smp.scan_roots", json::array({root}));
        collect_leftovers(wp, again);
        CHECK(again.get("leftovers.files") && again.get("leftovers.files")->empty(), "Reste: danach meldet der Check-up nichts mehr");
        const UndoResult u = undo_txn(wp, policy_from_facts(f), ok.txn, "20261114T130200Z");
        CHECK(u.ok && testutil::slurp(sb.real(root + "/Muster.teil")) == "teil, jetzt laenger" && !stdfs::exists(sb.real(root + "/.medic_broken/Muster.teil")), "Reste: Rueckgaengig holt die Dateien zurueck (" + u.error + ")");
    }
}

// Bausteine fuer Laufzeit-Bibliotheken und Pack-Indizes (Aufbau: docs/zstd.md von Dump Forge; Marken an echten Dateien geprueft, siehe collectors_games.cpp)
std::string ng_lib(const std::string& version, bool debug = false) {
    return std::string("\x4f\x15\x3d\x1d", 4) + std::string(60, '\0') + (debug ? "ampr-emu-ng debugpack_format\0" : "") + "ampr-emu-ng " + version + std::string("\0AMPRZPK1\0", 10) + std::string(100, '\0');
}
std::string drakmor_lib() { return std::string("\x4f\x15\x3d\x1d", 4) + std::string(60, '\0') + std::string("[AMPR_EMU]\0", 11) + "0.4.2.1 (c) Drakmor" + std::string(100, '\0'); }
std::string retail_lib() { return std::string("\x7f" "ELF", 4) + std::string(200, '\x01'); }

void put32(std::string& s, std::size_t off, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) s[off + static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFF);
}
void put64(std::string& s, std::size_t off, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) s[off + static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFF);
}
// Gueltiger Index ohne Dateien: Kopf (128 Byte) und je Volume ein Record mit der Groesse 64
std::string zstd_index(std::uint32_t volumes) {
    std::string h(128, '\0');
    h.replace(0, 8, "AMPRZPK1");
    put32(h, 8, 1);
    put32(h, 12, 128);
    h[16] = 18;
    put32(h, 20, volumes);
    put64(h, 56, 128);
    put64(h, 64, 128);
    put64(h, 72, 128);
    put64(h, 80, 128 + 8ull * volumes);
    std::string rec;
    for (std::uint32_t i = 0; i < volumes; ++i) {
        std::string r(8, '\0');
        put64(r, 0, 64);
        rec += r;
    }
    return h + rec;
}

const json* entry_of(const json* arr, const std::string& key, const std::string& value) {
    if (!arr || !arr->is_array()) return nullptr;
    for (const auto& e : *arr)
        if (e.is_object() && e.contains(key) && e[key] == value) return &e;
    return nullptr;
}

// Laufzeit-Bibliothek und Packs im Spiel (S01, S02, S18, S19, S23)
void runtime_tests() {
    Sandbox sb;
    const std::string root = "/data/homebrew";
    auto game = [&](const std::string& dir, const std::string& lib_dir, const std::string& lib, bool pack, std::uint32_t volumes = 1, bool index = true) {
        spit(sb.real(root + "/" + dir + "/eboot.bin"), "ELF");
        if (!lib.empty()) spit(sb.real(root + "/" + dir + "/" + lib_dir + "/libSceAmpr.sprx"), lib);
        if (pack) {
            spit(sb.real(root + "/" + dir + "/ampr_zstd.index"), zstd_index(volumes));
            for (std::uint32_t i = 0; i < volumes; ++i) {
                char name[32];
                std::snprintf(name, sizeof name, "ampr_zstd-%03u.pak", static_cast<unsigned>(i));
                std::string v(64, '\0');
                v.replace(0, 8, "AMPRZDT1");
                spit(sb.real(root + "/" + dir + "/" + name), v);
            }
        }
        if (index) spit(sb.real(root + "/" + dir + "/ampr_emu.index"), "AMPRIDX3");
    };
    game("PPSA00011-NG", "fakelib", ng_lib("0.5.0"), true);                    // in Ordnung
    game("PPSA00012-Drakmor", "fakelib", drakmor_lib(), true);                  // Drakmor liest kein zstd
    game("PPSA00013-Retail", "fakelib", retail_lib(), true);                    // Sonys Bibliothek liest keine Packs
    game("PPSA00014-Keine", "fakelib", "", true);                               // Pack ohne Bibliothek
    game("PPSA00015-Zwei", "fakelib", ng_lib("0.5.0"), true);                   // fakelib2 hat Vorrang ...
    spit(sb.real(root + "/PPSA00015-Zwei/fakelib2/libSceAmpr.sprx"), drakmor_lib());  // ... und enthaelt die falsche
    game("PPSA00016-Debug", "fakelib", ng_lib("0.4.0", true), true);            // Debug-Build, Version trotzdem gelesen; 0.4.0 = Speicherproblem
    game("PPSA00017-Beta", "fakelib", ng_lib("0.4.0-beta.1a2b3c4"), true);      // Beta: kein Speicherproblem
    game("PPSA00018-OhnePack", "fakelib", ng_lib("0.4.0"), false);              // Bibliothek ohne Pack: Eintrag, aber kein Problem
    game("PPSA00019-OhneIndex", "fakelib", ng_lib("0.5.0"), true, 1, false);    // Pack ohne ampr_emu.index
    spit(sb.real(root + "/PPSA00020-Plain/eboot.bin"), "ELF");                  // weder Pack noch Bibliothek: kein Eintrag

    auto p = make_snapshot_platform(sb.dir.string(), true);
    Facts f;
    json titles = json::array();
    for (const char* d : {"PPSA00011-NG", "PPSA00012-Drakmor", "PPSA00013-Retail", "PPSA00014-Keine", "PPSA00015-Zwei", "PPSA00016-Debug", "PPSA00017-Beta", "PPSA00018-OhnePack",
                          "PPSA00019-OhneIndex", "PPSA00020-Plain"})
        titles.push_back({{"titleid", std::string(d).substr(0, 9)}, {"path", root + "/" + d}});
    f.set("smp.titles", titles);
    f.set("smp.images", json::array());
    // Die Konsole hat fuer PPSA00016 einen Speicher-Fehler verzeichnet (Beleg fuer save_risk) und fuer die Beta PPSA00017 ebenfalls; PPSA00018 (0.4.0) keinen
    f.set("errors.pairs", json::array({{{"code", "CE-108255-1"}, {"titleid", "PPSA00016"}}, {{"code", "CE-107112-2"}, {"titleid", "PPSA00017"}}}));
    collect_game_runtime(*p, f);
    const json* rt = f.get("games.runtime");
    auto by = [&](const char* id) { return entry_of(rt, "titleid", id); };
    CHECK(rt && rt->size() == 9 && !by("PPSA00020"), "Laufzeit: neun Eintraege, das Spiel ohne Pack und Bibliothek fehlt");
    CHECK(by("PPSA00011") && (*by("PPSA00011"))["lib_kind"] == "ng" && (*by("PPSA00011"))["lib_version"] == "0.5.0" && (*by("PPSA00011"))["problem"] == "" && (*by("PPSA00011"))["pack"] == "zstd",
          "Laufzeit: ng 0.5.0 mit zstd-Pack ist in Ordnung");
    CHECK(by("PPSA00012") && (*by("PPSA00012"))["lib_kind"] == "drakmor" && (*by("PPSA00012"))["lib_version"] == "0.4.2.1" && (*by("PPSA00012"))["problem"] == "no_zstd_support", "Laufzeit: Drakmor 0.4.2.1 erkannt, liest kein zstd");
    CHECK(by("PPSA00013") && (*by("PPSA00013"))["lib_kind"] == "other" && (*by("PPSA00013"))["problem"] == "foreign_runtime", "Laufzeit: Sonys Bibliothek gilt als fremd");
    CHECK(by("PPSA00014") && (*by("PPSA00014"))["lib_kind"] == "missing" && (*by("PPSA00014"))["problem"] == "no_runtime", "Laufzeit: fehlende Bibliothek bei einem Pack");
    CHECK(by("PPSA00015") && (*by("PPSA00015"))["lib_in"] == "fakelib2" && (*by("PPSA00015"))["problem"] == "no_zstd_support", "Laufzeit: fakelib2 hat Vorrang vor fakelib");
    CHECK(by("PPSA00016") && (*by("PPSA00016"))["lib_debug"] == true && (*by("PPSA00016"))["lib_version"] == "0.4.0" && (*by("PPSA00016"))["save_risk"] == true, "Laufzeit: Debug-Build erkannt, Version 0.4.0 gelesen, Speicherrisiko");
    CHECK(by("PPSA00017") && (*by("PPSA00017"))["lib_version"] == "0.4.0-beta.1a2b3c4" && (*by("PPSA00017"))["save_risk"] == false, "Laufzeit: eine Beta-Fassung hat kein Speicherrisiko");
    CHECK(by("PPSA00018") && (*by("PPSA00018"))["pack"] == "" && (*by("PPSA00018"))["problem"] == "" && (*by("PPSA00018"))["index_missing"] == false && (*by("PPSA00018"))["save_risk"] == false,
          "Laufzeit: Bibliothek ohne Pack ist kein Problem, ng 0.4.0 ohne verzeichneten Fehler kein Speicherrisiko");
    CHECK(by("PPSA00019") && (*by("PPSA00019"))["index_missing"] == true && (*by("PPSA00011"))["index_missing"] == false, "Laufzeit: fehlende ampr_emu.index nur bei einem Pack gemeldet");
    const json* pp = f.get("games.pack_problems");
    CHECK(pp && pp->is_array() && pp->empty(), "Laufzeit: vollstaendige Packs ergeben keine Pack-Probleme");

    // Pack-Probleme: fehlendes Volume, falsche Groesse, abgeschnittener Index, ungueltiger Kopf
    spit(sb.real(root + "/PPSA00021-Fehlt/eboot.bin"), "ELF");
    spit(sb.real(root + "/PPSA00021-Fehlt/ampr_zstd.index"), zstd_index(3));
    spit(sb.real(root + "/PPSA00021-Fehlt/ampr_zstd-000.pak"), std::string(64, '\0'));
    spit(sb.real(root + "/PPSA00022-Groesse/eboot.bin"), "ELF");
    spit(sb.real(root + "/PPSA00022-Groesse/ampr_zstd.index"), zstd_index(1));
    spit(sb.real(root + "/PPSA00022-Groesse/ampr_zstd-000.pak"), std::string(70, '\0'));  // laut Index 64
    spit(sb.real(root + "/PPSA00023-Kurz/eboot.bin"), "ELF");
    spit(sb.real(root + "/PPSA00023-Kurz/ampr_zstd.index"), zstd_index(1) + "xxxxx");     // Index laenger als sein Kopf sagt
    spit(sb.real(root + "/PPSA00023-Kurz/ampr_zstd-000.pak"), std::string(64, '\0'));
    spit(sb.real(root + "/PPSA00024-Kopf/eboot.bin"), "ELF");
    spit(sb.real(root + "/PPSA00024-Kopf/ampr_zstd.index"), zstd_index(0));                // null Volumes: inkonsistenter Kopf
    spit(sb.real(root + "/PPSA00025-Kurz2/eboot.bin"), "ELF");
    spit(sb.real(root + "/PPSA00025-Kurz2/ampr_zstd.index"), std::string("AMPRZPK1") + std::string(20, '\0'));  // unter 128 Byte
    std::string future = zstd_index(1);
    put32(future, 8, 2);                                                                   // eine kuenftige Version des Formats: nicht beurteilen
    spit(sb.real(root + "/PPSA00026-Neu/eboot.bin"), "ELF");
    spit(sb.real(root + "/PPSA00026-Neu/ampr_zstd.index"), future);
    spit(sb.real(root + "/PPSA00027-Gemischt/eboot.bin"), "ELF");
    spit(sb.real(root + "/PPSA00027-Gemischt/ampr_zstd.index"), zstd_index(3));            // 000 fehlt, 001 hat die falsche Groesse, 002 stimmt
    spit(sb.real(root + "/PPSA00027-Gemischt/ampr_zstd-001.pak"), std::string(70, '\0'));
    spit(sb.real(root + "/PPSA00027-Gemischt/ampr_zstd-002.pak"), std::string(64, '\0'));
    Facts g;
    json t2 = json::array();
    for (const char* d : {"PPSA00021-Fehlt", "PPSA00022-Groesse", "PPSA00023-Kurz", "PPSA00024-Kopf", "PPSA00025-Kurz2", "PPSA00026-Neu", "PPSA00027-Gemischt"})
        t2.push_back({{"titleid", std::string(d).substr(0, 9)}, {"path", root + "/" + d}});
    g.set("smp.titles", t2);
    g.set("smp.images", json::array());
    collect_game_runtime(*p, g);
    const json* gp = g.get("games.pack_problems");
    auto prob = [&](const char* id) { return entry_of(gp, "titleid", id); };
    CHECK(prob("PPSA00021") && (*prob("PPSA00021"))["problem"] == "volume_missing" && (*prob("PPSA00021"))["count"] == 2 && (*prob("PPSA00021"))["detail"] == "ampr_zstd-001.pak", "Pack: zwei fehlende Volumes gemeldet");
    CHECK(prob("PPSA00022") && (*prob("PPSA00022"))["problem"] == "volume_size", "Pack: Volume mit falscher Groesse gemeldet");
    CHECK(prob("PPSA00023") && (*prob("PPSA00023"))["problem"] == "index_truncated", "Pack: Index laenger als sein Kopf sagt gemeldet");
    CHECK(prob("PPSA00024") && (*prob("PPSA00024"))["problem"] == "index_header", "Pack: null Volumes im Kopf gemeldet");
    CHECK(prob("PPSA00025") && (*prob("PPSA00025"))["problem"] == "index_truncated", "Pack: Index unter 128 Byte gemeldet");
    CHECK(!prob("PPSA00026"), "Pack: eine unbekannte Version des Formats wird nicht beurteilt");
    // gemischter Fall: die Detailangabe gehoert zum jeweiligen Problem (000 fehlt, 001 hat die falsche Groesse)
    int mixed = 0;
    if (gp)
        for (const auto& e : *gp)
            if (e["titleid"] == "PPSA00027") {
                ++mixed;
                if (e["problem"] == "volume_missing") CHECK(e["detail"] == "ampr_zstd-000.pak" && e["count"] == 1, "Pack: fehlendes Volume nennt die fehlende Datei");
                if (e["problem"] == "volume_size") CHECK(e["detail"] == "ampr_zstd-001.pak" && e["count"] == 1, "Pack: falsche Groesse nennt die betroffene Datei");
            }
    CHECK(mixed == 2, "Pack: gemischter Fall ergibt zwei Probleme");
    CHECK(gp && gp->size() == 7, "Pack: genau sieben Probleme (" + std::to_string(gp ? gp->size() : 0) + ")");

    // Image: dieselbe Auswertung ueber den Image-Leser (ein exFAT-Ersatz-Image mit einer einzigen Datei im Wurzelordner)
    {
        Sandbox sb2;
        const std::string idx = zstd_index(2);
        const std::string img = build_exfat_stub(9, 3, "ampr_zstd.index", idx);
        CHECK(!img.empty(), "Image: Ersatz-Image mit ampr_zstd.index gebaut");
        spit(sb2.real("/data/homebrew/PPSA00031.exfat"), img);
        auto p2 = make_snapshot_platform(sb2.dir.string());
        const auto reader = ImageReader::open(p2->fs(), "/data/homebrew/PPSA00031.exfat");
        std::vector<ImageReader::Entry> es;
        CHECK(reader && reader->list_dir("/", es) && es.size() == 1 && es[0].name == "ampr_zstd.index" && !es[0].is_dir, "Image: Wurzelordner aufgelistet");
        std::string part;
        CHECK(reader && reader->read_some("/ampr_zstd.index", 0, 1 << 20, part) && part == idx, "Image: read_some liefert die ganze Datei, auch wenn weniger da ist als verlangt");
        CHECK(reader && reader->read_some("/ampr_zstd.index", 8, 4, part) && part == idx.substr(8, 4) && !reader->read_some("/gibt-es-nicht", 0, 4, part), "Image: Teilbereich, fehlende Datei");
        CHECK(reader && !reader->list_dir("/gibt-es-nicht", es), "Image: fehlender Ordner");
        Facts h;
        h.set("smp.titles", json::array({{{"titleid", "PPSA00031"}, {"path", "/data/homebrew/PPSA00031.exfat"}}}));
        h.set("smp.images", json::array({{{"path", "/data/homebrew/PPSA00031.exfat"}}}));
        collect_game_runtime(*p2, h);
        const json* hr = h.get("games.runtime");
        CHECK(hr && hr->size() == 1 && (*hr)[0]["kind"] == "image" && (*hr)[0]["pack"] == "zstd" && (*hr)[0]["problem"] == "no_runtime" && (*hr)[0]["index_missing"] == true,
              "Image: zstd-Pack im Image erkannt, Bibliothek und ampr_emu.index fehlen");
        const json* hp = h.get("games.pack_problems");
        CHECK(hp && hp->size() == 1 && (*hp)[0]["problem"] == "volume_missing" && (*hp)[0]["count"] == 2, "Image: fehlende Volumes im Image gemeldet");
    }

    // SM+ ersetzt die Laufzeit: die Regel und ihr Fix (zwei Schluessel in einer Datei, Backup, Rueckgaengig)
    {
        spit(sb.real("/data/shadowmount/config.ini"), "[config]\nversion = 2\n\n[emulators]\nupdate_enabled = 1\nauto_update_ampr = 1\n");
        spit(sb.dir / "procs.json", R"(["shadowmountplus"])");  // ShadowMount+ laeuft: sonst sammelt der Sammler keine Spiele
        spit(sb.real(root + "/PPSA00011-NG/sce_sys/param.json"), R"({"titleId":"PPSA00011","requiredSystemSoftwareVersion":"0x0900000000000000","sdkVersion":"0x0900000000000000"})");  // ein vollstaendiges Spiel
        Facts c = collect_all(*make_snapshot_platform(sb.dir.string()));
        const RuleSet rs = load_rules_from_file(testutil::kShipped);
        const auto findings = evaluate(rs, c);
        const Finding* fd = find_rule(findings, "smp.emulator_update_replaces_runtime");
        CHECK(fd && fd->fix == "smp_set_key", "Updates: Regel feuert mit dem Fix smp_set_key");
        if (fd) {
            auto pp2 = make_snapshot_platform(sb.dir.string(), true);
            FixPlan plan = plan_fix(*fd, c, pp2->fs());
            CHECK(plan.changes.size() == 1 && plan.changes[0].kind == Change::Kind::WriteFile && plan.changes[0].content.find("update_enabled=0") != std::string::npos &&
                      plan.changes[0].content.find("auto_update_ampr=0") != std::string::npos,
                  "Updates: ein Plan setzt update_enabled und auto_update_ampr auf 0");
            const ApplyResult ar = apply_plan(plan, *pp2, policy_from_facts(c), {"20261115T100000Z", true});
            CHECK(ar.ok, "Updates: Anwenden ok (" + ar.error + ")");
            const std::string now = testutil::slurp(sb.real("/data/shadowmount/config.ini"));
            CHECK(now.find("update_enabled=0") != std::string::npos && now.find("auto_update_ampr=0") != std::string::npos, "Updates: beide Schluessel stehen auf 0");
            const UndoResult ur = undo_txn(*pp2, policy_from_facts(c), ar.txn, "20261115T100100Z");
            CHECK(ur.ok && testutil::slurp(sb.real("/data/shadowmount/config.ini")).find("update_enabled = 1") != std::string::npos, "Updates: Rueckgaengig stellt die Datei wieder her (" + ur.error + ")");
        }
    }
}

// Unvollstaendige Spielordner (S06) und Meldungen von Kraken Mount (S25)
void folder_and_kraken_tests() {
    Sandbox sb;
    const std::string root = "/data/homebrew";
    const std::string param = R"({"titleId":"PPSA00041","requiredSystemSoftwareVersion":"0x0900000000000000"})";
    spit(sb.real(root + "/Gut/eboot.bin"), "ELF");
    spit(sb.real(root + "/Gut/sce_sys/param.json"), param);
    spit(sb.real(root + "/OhneEboot/sce_sys/param.json"), param);
    spit(sb.real(root + "/OhneEboot/sce_module/libx.prx"), "x");
    spit(sb.real(root + "/OhneParam/eboot.bin"), "ELF");
    spit(sb.real(root + "/OhneParam/sce_sys/icon0.png"), "png");
    spit(sb.real(root + "/Bom/eboot.bin"), "ELF");
    spit(sb.real(root + "/Bom/sce_sys/param.json"), "\xEF\xBB\xBF" + param);
    spit(sb.real(root + "/KaputtJson/eboot.bin"), "ELF");
    spit(sb.real(root + "/KaputtJson/sce_sys/param.json"), "{\"titleId\": ");
    spit(sb.real(root + "/OhneTitel/eboot.bin"), "ELF");
    spit(sb.real(root + "/OhneTitel/sce_sys/param.json"), R"({"contentId":"x"})");
    spit(sb.real(root + "/NurEboot/eboot.bin"), "ELF");                       // kein sce_sys: kann ein Payload-Ordner sein
    spit(sb.real(root + "/NurParam/sce_sys/param.json"), param);              // nur eine param.json: kein Spiel
    spit(sb.real(root + "/.medic_broken/Alt/sce_sys/param.json"), "kaputt");  // Punkt-Ordner zaehlen nie
    auto p = make_snapshot_platform(sb.dir.string());
    Facts f;
    f.set("smp.scan_roots", json::array({root}));
    collect_game_folders(*p, f);
    const json* inc = f.get("games.incomplete");
    auto by = [&](const char* name) { return entry_of(inc, "name", name); };
    CHECK(inc && inc->size() == 5, "Ordner: fuenf unvollstaendige Ordner (" + std::to_string(inc ? inc->size() : 0) + ")");
    CHECK(by("OhneEboot") && (*by("OhneEboot"))["missing"][0] == "eboot.bin" && (*by("OhneEboot"))["param"] == "ok", "Ordner: eboot.bin fehlt");
    CHECK(by("OhneParam") && (*by("OhneParam"))["missing"][0] == "sce_sys/param.json" && (*by("OhneParam"))["param"] == "missing", "Ordner: param.json fehlt");
    CHECK(by("Bom") && (*by("Bom"))["param"] == "bom" && by("KaputtJson") && (*by("KaputtJson"))["param"] == "invalid" && by("OhneTitel") && (*by("OhneTitel"))["param"] == "no_titleid",
          "Ordner: Byte-Order-Marke, kaputtes JSON, fehlende titleId");
    CHECK(!by("Gut") && !by("NurEboot") && !by("NurParam") && !by(".medic_broken"), "Ordner: vollstaendiger Ordner, Payload-Ordner, reine param.json und Punkt-Ordner bleiben aussen vor");

    // Kraken Mount: Meldungen aus kraken.log
    spit(sb.real("/data/krakenmount/kraken.log"),
         "[10:00:00] info: tile PPSA00041: up to date\n"
         "[10:00:01] error: /data/homebrew/A.pkg: unsupported package: the layout has a sparse extent at mount 0x4000; repack with --kraken stored\n"
         "[10:00:02] error: /data/homebrew/A.pkg: unsupported package: the layout has a sparse extent at mount 0x4000; repack with --kraken stored\n"
         "[10:00:03] error: /mnt/usb0/homebrew/B.pkg: unsupported package: the layout has a sparse extent at mount 0x8000; repack with --kraken stored\n"
         "[10:00:04] warn: dlsym sceAppInstUtilAppInstallTitleDir: Invalid argument\n");
    spit(sb.real("/data/homebrew/A.pkg"), "x");  // die Pakete gibt es noch (ein entferntes wird nicht mehr gemeldet)
    spit(sb.real("/mnt/usb0/homebrew/B.pkg"), "x");
    Facts k;
    collect_kraken(*p, k);
    const json* ks = k.get("kraken.sparse_extent");
    CHECK(k.get("kraken.log_present") && ks && ks->size() == 2 && (*ks)[0]["path"] == "/data/homebrew/A.pkg" && (*ks)[1]["path"] == "/mnt/usb0/homebrew/B.pkg", "Kraken: zwei Pakete, je einmal, harmlose Zeilen ignoriert");
    Facts none;
    Sandbox sb3;  // lebt bis zum Ende der Pruefung (ein Temporaeres waere vor dem Aufruf schon weg)
    auto empty = make_snapshot_platform(sb3.dir.string());
    collect_kraken(*empty, none);
    CHECK(none.get("kraken.log_present") == nullptr && none.get("kraken.sparse_extent") == nullptr, "Kraken: ohne Log keine Fakten");
}

// Verschieben nur auf demselben Dateisystem (Marc, 14. Nov. 2026)
void rename_device_tests() {
    Sandbox sb;
    spit(sb.real("/data/homebrew/a.txt"), "inhalt");
    PosixRw same(sb.dir / "fs");
    CHECK(same.rename("/data/homebrew/a.txt", "/data/homebrew/.medic_broken/a.txt") && stdfs::exists(sb.real("/data/homebrew/.medic_broken/a.txt")), "Verschieben: im selben Dateisystem geht es");
    // Ueber Dateisysteme hinweg: Temp-Ordner (meist ext4) nach /dev/shm (tmpfs); wo beide dasselbe Dateisystem sind, entfaellt die Pruefung
    struct stat s1, s2;
    if (::stat(sb.dir.c_str(), &s1) == 0 && ::stat("/dev/shm", &s2) == 0 && s1.st_dev != s2.st_dev) {
        const stdfs::path from = sb.dir / "fs" / "data" / "x.txt";
        spit(from, "inhalt");
        PosixRw root("/");
        const std::string to = "/dev/shm/medic-device-test-" + std::to_string(::getpid()) + ".txt";
        CHECK(!root.rename(from.string(), to) && stdfs::exists(from) && !stdfs::exists(to), "Verschieben: ueber Dateisysteme hinweg wird abgelehnt, die Quelle bleibt");
        std::error_code ec;
        stdfs::remove(to, ec);
    } else {
        std::fprintf(stderr, "Hinweis: Verschieben ueber Dateisysteme nicht getestet (/dev/shm fehlt oder liegt auf demselben Geraet)\n");
    }
}

// Backport-Ordner mit Namenszusatz (S11): ShadowMount+ ab beta6 und Kraken Mount nehmen backports/<TITLE_ID>*
void backport_name_tests() {
    Sandbox sb;
    spit(sb.dir / "system.json", R"({"model":"CFI-TEST","fw":"12.00"})");
    const std::string param = R"({"titleId":"PPSA00051","requiredSystemSoftwareVersion":"0x1360000000000000","sdkVersion":"0x1360000000000000"})";
    for (const char* id : {"PPSA00051", "PPSA00052", "PPSA00053"}) spit(sb.real(std::string("/user/app/") + id + "/sce_sys/param.json"), param);
    stdfs::create_directories(sb.real("/data/homebrew/backports/PPSA00051-Muster"));  // mit Namenszusatz
    stdfs::create_directories(sb.real("/data/homebrew/backports/PPSA00052"));         // genauer Name (bisher schon erkannt)
    stdfs::create_directories(sb.real("/data/homebrew/backports/PPSA00054-Anderes"));  // anderes Spiel: zaehlt nicht fuer PPSA00053
    auto p = make_snapshot_platform(sb.dir.string());
    Facts f;
    collect_game_firmware(*p, f);
    const json* need = f.get("games.need_backport");
    std::string listed;
    if (need && need->is_array())
        for (const auto& n : *need) listed += n["titleid"].get<std::string>() + " ";
    CHECK(listed == "PPSA00053 ", "Backport: Ordner mit Namenszusatz und mit genauem Namen gelten, ein anderer Titel nicht (" + listed + ")");
}

// Weitere Absicherungen aus der Pruefung vom 15. Nov. 2026
void review_followup_tests() {
    {  // chmod folgt keinem Symlink
        Sandbox sb;
        spit(sb.real("/data/homebrew/Spiel/ziel.bin"), "x");
        set_mode(sb.real("/data/homebrew/Spiel/ziel.bin"), 0666);
        std::error_code ec;
        stdfs::create_symlink(sb.real("/data/homebrew/Spiel/ziel.bin"), sb.real("/data/homebrew/Spiel/link.bin"), ec);
        PosixRw rw(sb.dir / "fs");
        if (!ec) CHECK(!rw.set_mode("/data/homebrew/Spiel/link.bin", 0777) && mode_of(sb.real("/data/homebrew/Spiel/ziel.bin")) == 0666, "Rechte: ein Symlink wird nicht angefasst");
        CHECK(rw.set_mode("/data/homebrew/Spiel/ziel.bin", 0777) && mode_of(sb.real("/data/homebrew/Spiel/ziel.bin")) == 0777, "Rechte: eine normale Datei schon");
        CHECK(!rw.set_mode("/data/homebrew/Spiel/ziel.bin", 010777), "Rechte: Modus ausserhalb von 07777 wird abgelehnt");
    }
    {  // Rueckgaengig, wenn eine Datei inzwischen fehlt: der Rest wird trotzdem zurueckgesetzt
        Sandbox sb;
        const std::string game = "/data/homebrew/PPSA00061-Test";
        spit(sb.real(game + "/eboot.bin"), "x");
        spit(sb.real(game + "/sce_module/a.prx"), "x");
        set_mode(sb.real(game + "/eboot.bin"), 0666);
        set_mode(sb.real(game + "/sce_module/a.prx"), 0666);
        ModePlatform p(make_snapshot_platform(sb.dir.string(), true), sb.dir / "fs");
        Facts f;
        f.set("smp.titles", json::array({{{"titleid", "PPSA00061"}, {"path", game}}}));
        f.set("smp.scan_roots", json::array({"/data/homebrew"}));
        collect_game_files(p, f);
        const RuleSet rs = load_rules_from_file(testutil::kShipped);
        const auto findings = evaluate(rs, f);
        const Finding* fd = find_rule(findings, "game.no_exec_permission");
        CHECK(fd != nullptr, "Rechte: Regel feuert (Rueckgaengig-Test)");
        if (fd) {
            const ApplyResult ar = apply_plan(plan_fix(*fd, f, p.fs()), p, policy_from_facts(f), {"20261115T110000Z", true});
            CHECK(ar.ok, "Rechte: Anwenden ok (" + ar.error + ")");
            stdfs::remove(sb.real(game + "/sce_module/a.prx"));  // jemand hat die Datei inzwischen geloescht
            const UndoResult ur = undo_txn(p, policy_from_facts(f), ar.txn, "20261115T110100Z");
            CHECK(ur.ok && mode_of(sb.real(game + "/eboot.bin")) == 0666, "Rechte: fehlt eine Datei, setzt Rueckgaengig die uebrigen trotzdem zurueck (" + ur.error + ")");
        }
    }
    {  // exFAT/FAT: kein Befund, wo es keine Dateimodi gibt
        Sandbox sb;
        const std::string game = "/mnt/usb0/homebrew/PPSA00062-Test";
        spit(sb.real(game + "/eboot.bin"), "x");
        set_mode(sb.real(game + "/eboot.bin"), 0666);
        spit(sb.dir / "system.json", R"({"model":"CFI-TEST","fw":"12.00","storage":[{"mount":"/mnt/usb0","total":1,"free":1,"fstype":"exfatfs"}]})");
        ModePlatform p(make_snapshot_platform(sb.dir.string(), true), sb.dir / "fs");
        Facts f;
        f.set("smp.titles", json::array({{{"titleid", "PPSA00062"}, {"path", game}}}));
        collect_game_files(p, f);
        CHECK(f.get("games.noexec") && f.get("games.noexec")->empty(), "Rechte: auf exFAT kein Befund");
    }
    {  // Reste: ein Ordner mit Spielmarken ist kein Rest; ein ".part"-Ordner ohne sie schon
        Sandbox sb;
        const std::string root = "/data/homebrew";
        spit(sb.real(root + "/Spiel.neu/eboot.bin"), "ELF");
        spit(sb.real(root + "/Spiel.neu/sce_sys/param.json"), "{}");
        spit(sb.real(root + "/Upload.part/datei.bin"), "x");
        spit(sb.real(root + "/Rest.unvollstaendig"), "x");
        auto p = make_snapshot_platform(sb.dir.string());
        Facts f;
        f.set("smp.scan_roots", json::array({root}));
        collect_leftovers(*p, f);
        std::string names;
        if (const json* lf = f.get("leftovers.files"))
            for (const auto& x : *lf) names += x["name"].get<std::string>() + "\n";
        CHECK(names.find("Spiel.neu") == std::string::npos && names.find("Upload.part") != std::string::npos && names.find("Rest.unvollstaendig") != std::string::npos,
              "Reste: Ordner mit sce_sys/eboot.bin nie, sonstige Ordner und .unvollstaendig ja");
    }
    {  // Kraken: ein Paket, das es nicht mehr gibt, wird nicht mehr gemeldet
        Sandbox sb;
        spit(sb.real("/data/krakenmount/kraken.log"),
             "[1] error: /data/homebrew/Weg.pkg: unsupported package: the layout has a sparse extent at mount 0x4000; repack with --kraken stored\n"
             "[2] error: /data/homebrew/Da.pkg: unsupported package: the layout has a sparse extent at mount 0x4000; repack with --kraken stored\n");
        spit(sb.real("/data/homebrew/Da.pkg"), "x");
        auto p = make_snapshot_platform(sb.dir.string());
        Facts k;
        collect_kraken(*p, k);
        const json* ks = k.get("kraken.sparse_extent");
        CHECK(ks && ks->size() == 1 && (*ks)[0]["path"] == "/data/homebrew/Da.pkg", "Kraken: nur das noch vorhandene Paket");
    }
    {  // PS4-Titel (param.sfo) gelten nicht als unvollstaendiges PS5-Spiel
        Sandbox sb;
        const std::string root = "/data/homebrew";
        spit(sb.real(root + "/CUSA00001/eboot.bin"), "ELF");
        spit(sb.real(root + "/CUSA00001/sce_sys/param.sfo"), "sfo");
        spit(sb.real(root + "/CUSA00001/sce_module/libx.prx"), "x");
        auto p = make_snapshot_platform(sb.dir.string());
        Facts f;
        f.set("smp.scan_roots", json::array({root}));
        collect_game_folders(*p, f);
        CHECK(f.get("games.incomplete") && f.get("games.incomplete")->empty(), "Ordner: ein PS4-Titel mit param.sfo ist kein unvollstaendiges PS5-Spiel");
    }
    {  // Emulator-Updates an: eine fehlende Bibliothek gilt nicht als Befund (ShadowMount+ legt sie ein); Sonys ELF und eine unbekannte SELF-Datei unterscheiden sich
        Sandbox sb;
        const std::string root = "/data/homebrew";
        auto mk = [&](const char* dir, const std::string& lib) {
            spit(sb.real(root + "/" + dir + "/eboot.bin"), "ELF");
            if (!lib.empty()) spit(sb.real(root + "/" + dir + "/fakelib/libSceAmpr.sprx"), lib);
            spit(sb.real(root + "/" + dir + "/ampr_zstd.index"), zstd_index(1));
            spit(sb.real(root + "/" + dir + "/ampr_zstd-000.pak"), std::string(64, '\0'));
            spit(sb.real(root + "/" + dir + "/ampr_emu.index"), "AMPRIDX3");
        };
        mk("PPSA00071-Ohne", "");
        mk("PPSA00072-Unbekannt", std::string("\x4f\x15\x3d\x1d", 4) + std::string(200, '\1'));  // SELF ohne bekannte Texte
        auto p = make_snapshot_platform(sb.dir.string());
        auto titles = json::array({{{"titleid", "PPSA00071"}, {"path", root + "/PPSA00071-Ohne"}}, {{"titleid", "PPSA00072"}, {"path", root + "/PPSA00072-Unbekannt"}}});
        Facts off;
        off.set("smp.titles", titles);
        collect_game_runtime(*p, off);
        const json* r1 = off.get("games.runtime");
        CHECK(entry_of(r1, "titleid", "PPSA00071") && (*entry_of(r1, "titleid", "PPSA00071"))["problem"] == "no_runtime", "Laufzeit: ohne Updates gilt eine fehlende Bibliothek als Befund");
        CHECK(entry_of(r1, "titleid", "PPSA00072") && (*entry_of(r1, "titleid", "PPSA00072"))["problem"] == "", "Laufzeit: eine unbekannte SELF-Datei wird nicht als fremd gemeldet");
        Facts on;
        on.set("smp.titles", titles);
        on.set("smp.config", json{{"emulators", json{{"update_enabled", "1"}}}});
        collect_game_runtime(*p, on);
        CHECK(entry_of(on.get("games.runtime"), "titleid", "PPSA00071") && (*entry_of(on.get("games.runtime"), "titleid", "PPSA00071"))["problem"] == "", "Laufzeit: mit Emulator-Updates gilt eine fehlende Bibliothek nicht als Befund");
    }
    {  // Verschieben ueber Dateisysteme: kein leerer Zielordner bleibt zurueck
        Sandbox sb;
        spit(sb.real("/data/x/a.txt"), "x");
        struct stat s1, s2;
        if (::stat(sb.dir.c_str(), &s1) == 0 && ::stat("/dev/shm", &s2) == 0 && s1.st_dev != s2.st_dev) {
            PosixRw root("/");
            const std::string dir = "/dev/shm/medic-device-" + std::to_string(::getpid());
            CHECK(!root.rename((sb.dir / "fs" / "data" / "x" / "a.txt").string(), dir + "/neu/a.txt") && !stdfs::exists(dir), "Verschieben: bei Ablehnung entsteht kein Zielordner");
        }
    }
}

// nanoDNS: die letzte Fehlerzeile je Art steht im Befund, Domains nie
void nanodns_tests() {
    Sandbox sb;
    spit(sb.real("/data/nanodns/nanodns.log"),
         "[nanodns] query from=127.0.0.1:1 id=0x1 qname=geheim.example.com qtype=A(1) qclass=1\n"
         "[nanodns] all upstreams failed for geheim.example.com\n"
         "[nanodns] all upstreams failed for noch.geheimer.example.org\n"
         "[nanodns] warning: ignoring invalid line 7 in nanodns.ini\n");
    auto p = make_snapshot_platform(sb.dir.string());
    Facts f;
    collect_helpers(*p, f);
    const json* pr = f.get("nanodns.log.problems");
    CHECK(pr && pr->size() == 2 && (*pr)[0]["kind"] == "upstream" && (*pr)[0]["count"] == 2 && (*pr)[0]["line"] == "[nanodns] all upstreams failed", "nanoDNS: Upstream-Fehler gezaehlt, Zeile ohne Domain");
    CHECK(pr && (*pr)[1]["kind"] == "warning" && (*pr)[1]["line"] == "[nanodns] warning: ignoring invalid line 7 in nanodns.ini", "nanoDNS: Warnung mit ihrer Zeile");
    CHECK(pr && pr->dump().find("geheim") == std::string::npos, "nanoDNS: keine Domain in den Fakten");
}

// kstuff-Datei aus dem Payload Manager (nur Information) und die 200-Titel-Grenze der Spiele-Ansicht
void misc_tests() {
    {
        Sandbox sb;
        spit(sb.real("/data/pldmgr/autoload.txt"), "local/kstuff1\n!3000\nlocal/shadowmountplus\n");
        spit(sb.real("/data/pldmgr/payloads/local/kstuff1/kstuff1.13-fpkg-dr-test8-rest-fast.elf"), "elf");
        spit(sb.real("/data/pldmgr/payloads/local/kstuff1/meta.json"), "{}");
        auto p = make_snapshot_platform(sb.dir.string());
        Facts f;
        collect_kstuff_file(*p, f);
        CHECK(f.get("kstuff.file") && *f.get("kstuff.file") == "kstuff1.13-fpkg-dr-test8-rest-fast.elf" && f.get("kstuff.file_version") && *f.get("kstuff.file_version") == "1.13",
              "kstuff: Datei und Version aus dem Dateinamen");
        CHECK(f.get("kstuff.version") == nullptr, "kstuff: die gepruefte Version bleibt unbekannt (keine Regel stuetzt sich auf den Dateinamen)");
    }
    {  // 250 Titel, einer mit kritischem Befund ganz hinten: er faellt nicht heraus
        Facts f;
        json titles = json::array();
        for (int i = 0; i < 250; ++i) {
            char id[16];
            std::snprintf(id, sizeof id, "PPSA%05d", i);
            titles.push_back({{"titleid", id}, {"path", std::string("/data/homebrew/") + id}});
        }
        f.set("smp.titles", titles);
        f.set("games.eboot_problems", json::array({{{"titleid", "PPSA00249"}, {"path", "/x"}, {"reason", "text"}}}));
        const auto infos = collect_game_infos(f, {});
        bool found = false;
        for (const auto& g : infos)
            if (g.titleid == "PPSA00249" && g.eboot_bad) found = true;
        CHECK(found, "Spiele-Ansicht: ein Titel mit Befund kommt auch jenseits der 200er-Grenze hinein");
    }
    {  // Kompatibilitaetsliste: Tabelle sortiert, Suche, Fakten und Spiel-Eintrag
        for (unsigned i = 1; i < kCompatCount; ++i) CHECK(std::string(kCompatTable[i - 1].titleid) < kCompatTable[i].titleid, "Kompatibilitaet: Tabelle sortiert");
        CHECK(find_compat("PPSA17942") && std::string(find_compat("PPSA17942")->status) == "fail", "Kompatibilitaet: bekannter Titel mit fail");
        CHECK(find_compat("PPSA11386") && std::string(find_compat("PPSA11386")->status) == "ok", "Kompatibilitaet: bekannter Titel mit ok");
        CHECK(find_compat("PPSA00001") == nullptr && find_compat("") == nullptr, "Kompatibilitaet: unbekannter Titel nicht in der Liste");
        Facts f;
        f.set("smp.titles", json::array({{{"titleid", "PPSA17942"}, {"path", "/data/homebrew/a"}}, {{"titleid", "PPSA11386"}, {"path", "/data/homebrew/b"}}, {{"titleid", "PPSA00001"}, {"path", "/data/homebrew/c"}}}));
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string());
        collect_compat(*p, f);
        CHECK(f.get("games.compat") && f.get("games.compat")->size() == 2 && f.get("games.compat_bad") && f.get("games.compat_bad")->size() == 1, "Kompatibilitaet: zwei Treffer, einer davon schlecht");
        CHECK((*f.get("games.compat_bad"))[0]["desc"].get<std::string>().find("PPSA17942: fail") == 0, "Kompatibilitaet: Evidenz nennt Titel und Ergebnis");
        bool seen = false;
        for (const auto& g : collect_game_infos(f, {}))
            if (g.titleid == "PPSA17942" && g.compat == "fail") seen = true;
        CHECK(seen, "Kompatibilitaet: Spiel-Eintrag traegt das Ergebnis");
    }
}

}  // namespace

void game_issue_tests() {
    chmod_tests();
    misc_tests();
    nanodns_tests();
    leftover_tests();
    rename_device_tests();
    backport_name_tests();
    runtime_tests();
    folder_and_kraken_tests();
    review_followup_tests();
}
