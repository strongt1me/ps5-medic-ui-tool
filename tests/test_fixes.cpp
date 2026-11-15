// SPDX-License-Identifier: GPL-3.0-or-later
// Tests der Fix-Engine: SHA-256, INI-Editor, Whitelist, apply -> undo = identischer Dateibaum, Rollback, Journal.
#include "check.hpp"
#include "medic/collectors.hpp"
#include "medic/casename.hpp"
#include "medic/fixes.hpp"
#include "medic/ini.hpp"
#include "medic/platform.hpp"
#include "medic/rules.hpp"
#include "medic/fwver.hpp"
#include "medic/sha256.hpp"
#include "medic/snapshot_export.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;
using namespace medic;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Dateibaum (relativer Pfad -> SHA-256 bzw. "<dir>") ohne /data/ps5medic (dort liegen Journal und Backups bewusst weiter).
std::map<std::string, std::string> tree(const fs::path& root) {
    std::map<std::string, std::string> t;
    for (auto& e : fs::recursive_directory_iterator(root)) {
        std::string rel = fs::relative(e.path(), root).generic_string();
        if (rel.rfind("fs/data/ps5medic", 0) == 0) continue;
        t[rel] = e.is_directory() ? "<dir>" : sha256_hex(slurp(e.path()));
    }
    return t;
}

struct TempCopy {
    fs::path dir;
    explicit TempCopy(const std::string& fixture) {
        static int n = 0;
        dir = fs::temp_directory_path() / ("medic-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++n));
        fs::remove_all(dir);
        fs::copy(fs::path(MEDIC_SOURCE_DIR) / "tests" / "fixtures" / fixture, dir, fs::copy_options::recursive);
    }
    ~TempCopy() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

const RuleSet& rules() {
    static RuleSet rs = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    return rs;
}

bool fires(const fs::path& dir, const std::string& rule_id) {
    auto p = make_snapshot_platform(dir.string());
    for (auto& f : evaluate(rules(), collect_all(*p)))
        if (f.rule_id == rule_id) return true;
    return false;
}

struct Ctx {
    std::unique_ptr<Platform> p;
    Facts facts;
    Finding finding;
    FixPlan plan;
};

Ctx make_ctx(const fs::path& dir, const std::string& rule_id, const FixOptions& opt = {}) {
    Ctx c;
    c.p = make_snapshot_platform(dir.string(), /*writable=*/true);
    c.facts = collect_all(*c.p);
    for (auto& f : evaluate(rules(), c.facts))
        if (f.rule_id == rule_id) c.finding = f;
    c.plan = plan_fix(c.finding, c.facts, c.p->fs(), opt);
    return c;
}

ApplyOptions confirmed(const std::string& ts) {
    ApplyOptions o;
    o.timestamp = ts;
    o.confirmed = true;
    return o;
}

// ---- Unit-Tests ---------------------------------------------------------

void sha_tests() {
    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 leer");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc");
    CHECK(sha256_hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 1 Mio. a");
    CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256 Zwei-Block-Vektor");
}

void fw_tests() {
    CHECK(format_ps5_fw(0x04030000) == "4.03", "FW 4.03");
    CHECK(format_ps5_fw(0x13600000) == "13.60", "FW 13.60");
    CHECK(format_ps5_fw(0x09000000) == "9.00", "FW 9.00");
    CHECK(format_ps5_fw(0x10010000) == "10.01", "FW 10.01");
    CHECK(format_ps5_fw(0x04030123) == "4.03", "FW ignoriert untere 16 Bit");
    CHECK(format_ps5_fw(0x0a000000) == "0x0a000000" && format_ps5_fw(0) == "0x00000000", "unplausibler Rohwert bleibt sichtbar");
    // Snapshot-Plattform und FW-Regel: Version kommt als Text an
}

void ini_tests() {
    // ersetzen, Kommentare und Reihenfolge bleiben
    CHECK(ini_set_value("# c\ndebug=1\nx=2\n", {"", "debug", ""}, "0") == "# c\ndebug=0\nx=2\n", "ini: Wert ersetzen");
    CHECK(ini_set_value("debug=0\n", {"", "debug", ""}, "0") == "debug=0\n", "ini: schon gesetzt = unveraendert");
    // flach einfuegen vor der ersten Sektion
    CHECK(ini_set_value("a=1\n[x]\nb=2\n", {"", "k", ""}, "v") == "a=1\nk=v\n[x]\nb=2\n", "ini: flach vor erster Sektion");
    // in vorhandener Sektion einfuegen
    CHECK(ini_set_value("[general]\nquiet=1\n\n[scan]\ndepth=1\n", {"general", "debug", ""}, "1") ==
              "[general]\nquiet=1\ndebug=1\n\n[scan]\ndepth=1\n",
          "ini: in vorhandener Sektion");
    // Konfigurationsformat v2 (1.7beta12): auskommentierte Standardwerte bleiben stehen, die Zeile kommt ans Ende der Sektion
    {
        const std::string v2 = "[config]\nversion = 2\n\n[general]\n# debug = on\n\n[scan]\n# depth = 1\n# path = /data/homebrew\n\n[fakelib]\n# enabled = on\n";
        CHECK(ini_set_value(v2, {"scan", "depth", "scan_depth"}, "2") ==
                  "[config]\nversion = 2\n\n[general]\n# debug = on\n\n[scan]\n# depth = 1\n# path = /data/homebrew\ndepth=2\n\n[fakelib]\n# enabled = on\n",
              "ini v2: depth wird in [scan] ergaenzt, Kommentare und Reihenfolge bleiben");
        CHECK(ini_set_value(v2, {"general", "debug", ""}, "1") ==
                  "[config]\nversion = 2\n\n[general]\n# debug = on\ndebug=1\n\n[scan]\n# depth = 1\n# path = /data/homebrew\n\n[fakelib]\n# enabled = on\n",
              "ini v2: debug wird in [general] ergaenzt");
        const std::string with_value = "[config]\nversion = 2\n[scan]\ndepth = 1 # Standard\n";
        CHECK(ini_set_value(with_value, {"scan", "depth", "scan_depth"}, "2") == "[config]\nversion = 2\n[scan]\ndepth=2\n", "ini v2: vorhandene Zeile wird ersetzt");
    }
    // neue Sektion am Ende
    CHECK(ini_set_value("a=1\n", {"scan", "depth", ""}, "2") == "a=1\n\n[scan]\ndepth=2\n", "ini: neue Sektion in Datei ohne Sektionen");
    CHECK(ini_set_value("[general]\ndebug=0\n", {"scan", "depth", ""}, "2") == "[general]\ndebug=0\n\n[scan]\ndepth=2\n", "ini: neue Sektion am Ende");
    // ohne Sektionen bleibt es beim alten flachen Schluessel
    CHECK(ini_set_value("debug=1\n", {"scan", "depth", "scan_depth"}, "2") == "debug=1\nscan_depth=2\n", "ini: legacy flach bei Datei ohne Sektionen");
    // vorhandener flacher Legacy-Schluessel gewinnt
    CHECK(ini_set_value("[scan]\npath=/a\n", {"scan", "depth", "scan_depth"}, "2") == "[scan]\npath=/a\ndepth=2\n", "ini: Sektion bei Datei mit Sektionen");
    // v2: ein Wert in Anfuehrungszeichen gilt als derselbe Pfad; Sonderzeichen werden beim Schreiben maskiert (Befund Review 13. Nov. 2026)
    {
        const std::string v2q = "[config]\nversion = 2\n[scan]\npath=\"/mnt/usb0/games\"\n";
        CHECK(ini_add_value(v2q, {"scan", "path", "scanpath"}, "/mnt/usb0/games") == v2q, "ini v2: Pfad in Anfuehrungszeichen wird nicht doppelt ergaenzt");
        CHECK(ini_set_value(v2q, {"scan", "path", "scanpath"}, "/mnt/usb0/games") == v2q, "ini v2: gleicher Wert in Anfuehrungszeichen bleibt unveraendert");
        CHECK(ini_add_value("[config]\nversion = 2\n[scan]\n", {"scan", "path", "scanpath"}, "/mnt/usb0/a;b") == "[config]\nversion = 2\n[scan]\npath=\"/mnt/usb0/a;b\"\n",
              "ini v2: Wert mit ';' wird in Anfuehrungszeichen geschrieben (sonst schneidet ShadowMount+ ab)");
        CHECK(ini_add_value("[scan]\n", {"scan", "path", "scanpath"}, "/mnt/usb0/a;b") == "[scan]\npath=/mnt/usb0/a;b\n", "ini v1: ohne Format v2 bleibt es beim rohen Wert");
    }
    // CRLF bleibt
    CHECK(ini_set_value("a=1\r\ndebug=1\r\n", {"", "debug", ""}, "0") == "a=1\r\ndebug=0\r\n", "ini: CRLF erhalten");
    // fehlendes Zeilenende am Dateiende
    CHECK(ini_set_value("a=1", {"", "b", ""}, "2") == "a=1\nb=2\n", "ini: ohne Endzeilenumbruch");
    CHECK(ini_set_value("", {"", "b", ""}, "2") == "b=2\n", "ini: leere Datei");
    // add
    CHECK(ini_add_value("scanpath=/a\nscanpath=/b\nx=1\n", {"scan", "path", "scanpath"}, "/c") == "scanpath=/a\nscanpath=/b\nscanpath=/c\nx=1\n",
          "ini: add hinter letzter gleichnamiger Zeile");
    CHECK(ini_add_value("scanpath=/a\n", {"scan", "path", "scanpath"}, "/a") == "scanpath=/a\n", "ini: add ist idempotent");
    CHECK(ini_add_value("[scan]\npath=/a\n", {"scan", "path", "scanpath"}, "/b") == "[scan]\npath=/a\npath=/b\n", "ini: add in Sektion");
}

void policy_tests() {
    PathPolicy pol({"/mnt/usb0", "/data", "/", "/system_ex/x", "relativ", "/data/../etc"});
    CHECK(pol.allowed("/data/shadowmount/config.ini"), "Whitelist: config.ini");
    CHECK(!pol.allowed("/data/shadowmount"), "Whitelist: Wurzelordner selbst nicht");
    CHECK(pol.allowed("/data/ps5medic/journal.jsonl"), "Whitelist: ps5medic");
    CHECK(pol.allowed("/data/pldmgr/autoload.txt") && pol.allowed("/data/ps5_autoloader/autoload.txt"), "Whitelist: Autoload-Listen");
    CHECK(pol.allowed("/mnt/usb0/Game/x.ffpfs"), "Whitelist: Scanpfad");
    CHECK(!pol.allowed("/mnt/usb1/Game"), "Whitelist: anderer USB");
    CHECK(!pol.allowed("/data/other/x"), "Whitelist: zu breiter Scanpfad /data wird ignoriert");
    CHECK(!pol.allowed("/system/x"), "gesperrt: /system");
    CHECK(!pol.allowed("/system_ex/x/y"), "gesperrt: /system_ex");
    CHECK(!pol.allowed("/data/shadowmount/../../system/x"), "'..' abgelehnt");
    CHECK(!pol.allowed("data/shadowmount/x"), "relativ abgelehnt");
    CHECK(!pol.allowed("/data/shadowmountX/x"), "Praefix nur an Ordnergrenze");
    CHECK(!pol.allowed("/etc/passwd"), "ausserhalb");
}

// ---- apply -> undo ------------------------------------------------------

struct FixCase {
    const char* fixture;
    const char* rule;
    bool clears;  // Regel feuert nach dem Fix nicht mehr (B5: Prozess laeuft bis zum Neustart weiter)
};

void roundtrip(const FixCase& fc, int idx) {
    const std::string name = std::string(fc.fixture) + "/" + fc.rule;
    TempCopy tmp(fc.fixture);
    const auto before = tree(tmp.dir);
    CHECK(fires(tmp.dir, fc.rule), name + ": Regel feuert vorher");

    Ctx c = make_ctx(tmp.dir, fc.rule);
    CHECK(!c.plan.empty(), name + ": Plan hat Aenderungen");
    CHECK(tree(tmp.dir) == before, name + ": plan veraendert nichts");

    PathPolicy pol = policy_from_facts(c.facts);
    const std::string ts = "20261108T1200" + std::to_string(10 + idx) + "Z";
    ApplyResult ar = apply_plan(c.plan, *c.p, pol, confirmed(ts));
    CHECK(ar.ok, name + ": apply ok (" + ar.error + ")");
    if (!ar.ok) return;
    CHECK(tree(tmp.dir) != before, name + ": apply aendert den Dateibaum");
    if (fc.clears) CHECK(!fires(tmp.dir, fc.rule), name + ": Regel feuert nach dem Fix nicht mehr");

    // Journal: jede Operation mit SHA-256 vorher/nachher, abgeschlossene Transaktion
    std::string jr = slurp(tmp.dir / "fs/data/ps5medic/journal.jsonl");
    CHECK(!jr.empty(), name + ": Journal geschrieben");
    int ops = 0, commits = 0;
    std::istringstream in(jr);
    for (std::string line; std::getline(in, line);) {
        json j = json::parse(line, nullptr, false);
        CHECK(j.is_object(), name + ": Journalzeile ist JSON");
        if (j.value("type", "") == "op") {
            ++ops;
            CHECK(j.value("sha_after", "").size() == 64 || j.value("kind", "") == "dir", name + ": sha_after (64 Hex) vorhanden");
            if (j.value("op", "") == "write" && !j.value("backup", "").empty()) {
                std::string backup = slurp(tmp.dir / "fs" / j.value("backup", "").substr(1));
                CHECK(sha256_hex(backup) == j.value("sha_before", ""), name + ": Backup entspricht sha_before");
            }
        }
        if (j.value("type", "") == "commit") ++commits;
    }
    CHECK(ops == static_cast<int>(c.plan.changes.size()) && commits == 1, name + ": Journal enthaelt alle Operationen und einen Commit");
    auto h = history(*c.p);
    CHECK(h.size() == 1 && h[0].state == "applied", name + ": history = applied");

    UndoResult ur = undo_txn(*c.p, pol, ar.txn, ts + "u");
    CHECK(ur.ok, name + ": undo ok (" + ur.error + ")");
    CHECK(tree(tmp.dir) == before, name + ": nach undo ist der Dateibaum identisch");
    CHECK(fires(tmp.dir, fc.rule), name + ": Regel feuert nach undo wieder");
    h = history(*c.p);
    CHECK(h.size() == 1 && h[0].state == "undone", name + ": history = undone");
    CHECK(!undo_txn(*c.p, pol, ar.txn, ts + "v").ok, name + ": zweites undo wird abgelehnt");
}

void safety_tests() {
    // Nicht bestaetigt -> nichts passiert
    {
        TempCopy tmp("b3_pos");
        auto before = tree(tmp.dir);
        Ctx c = make_ctx(tmp.dir, "smp.autotoggle_crash_1360");
        ApplyOptions o;
        o.timestamp = "20261108T130000Z";
        CHECK(!apply_plan(c.plan, *c.p, policy_from_facts(c.facts), o).ok, "ohne Bestaetigung: apply abgelehnt");
        CHECK(tree(tmp.dir) == before && !fs::exists(tmp.dir / "fs/data/ps5medic"), "ohne Bestaetigung: nichts geschrieben");
        // Nur-Lese-Platform
        auto ro = make_snapshot_platform(tmp.dir.string());
        CHECK(!apply_plan(c.plan, *ro, policy_from_facts(c.facts), confirmed("20261108T130001Z")).ok, "read-only Platform: apply abgelehnt");
    }
    // Pfad ausserhalb der Whitelist / System / '..' -> alles abgelehnt, nichts veraendert
    {
        TempCopy tmp("b3_pos");
        auto before = tree(tmp.dir);
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        PathPolicy pol;
        for (const char* bad : {"/system/etc/x.cfg", "/system_ex/x", "/data/shadowmount/../../etc/x", "/etc/hosts", "/data/other/x"}) {
            FixPlan plan;
            plan.fix_id = "test";
            Change ch;
            ch.kind = Change::Kind::WriteFile;
            ch.path = bad;
            ch.content = "x";
            plan.changes.push_back(ch);
            CHECK(!apply_plan(plan, *p, pol, confirmed("20261108T130002Z")).ok, std::string("Whitelist lehnt ab: ") + bad);
        }
        // Rename mit Ziel ausserhalb
        FixPlan plan;
        Change ch;
        ch.kind = Change::Kind::Rename;
        ch.path = "/data/shadowmount/config.ini";
        ch.to = "/system/config.ini";
        plan.changes.push_back(ch);
        CHECK(!apply_plan(plan, *p, pol, confirmed("20261108T130003Z")).ok, "Whitelist prueft auch das Rename-Ziel");
        CHECK(tree(tmp.dir) == before, "abgelehnte Plaene veraendern nichts");
    }
    // Zielkollision / fehlende Quelle
    {
        TempCopy tmp("b2_pos");
        auto before = tree(tmp.dir);
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        PathPolicy pol({"/data/homebrew"});
        FixPlan plan;
        Change ch;
        ch.kind = Change::Kind::Rename;
        ch.path = "/data/homebrew/Cool Game (USA).ffpfs";
        ch.to = "/data/homebrew/Clean Game.ffpfs";  // existiert
        plan.changes.push_back(ch);
        CHECK(!apply_plan(plan, *p, pol, confirmed("20261108T130004Z")).ok, "Ziel existiert: abgelehnt");
        plan.changes[0].path = "/data/homebrew/gibt-es-nicht.ffpfs";
        plan.changes[0].to = "/data/homebrew/neu.ffpfs";
        CHECK(!apply_plan(plan, *p, pol, confirmed("20261108T130005Z")).ok, "Quelle fehlt: abgelehnt");
        CHECK(tree(tmp.dir) == before, "Kollisionen veraendern nichts");
    }
    // Undo verweigert, wenn die Datei nach dem Fix geaendert wurde (fremde Aenderung nicht ueberschreiben)
    {
        TempCopy tmp("b3_pos");
        Ctx c = make_ctx(tmp.dir, "smp.autotoggle_crash_1360");
        PathPolicy pol = policy_from_facts(c.facts);
        ApplyResult ar = apply_plan(c.plan, *c.p, pol, confirmed("20261108T130006Z"));
        CHECK(ar.ok, "Undo-Schutz: apply ok");
        c.p->fs_rw()->write_file("/data/shadowmount/config.ini", "vom_nutzer_geaendert=1\n");
        auto mid = tree(tmp.dir);
        CHECK(!undo_txn(*c.p, pol, ar.txn, "20261108T130007Z").ok, "Undo-Schutz: geaenderte Datei -> undo abgelehnt");
        CHECK(tree(tmp.dir) == mid, "Undo-Schutz: nichts ueberschrieben");
    }
    // Hinweis-Fix: keine Aenderungen, nur Text
    {
        TempCopy tmp("b6_pos");
        Ctx c = make_ctx(tmp.dir, "smp.image_extra_folder");
        CHECK(c.plan.fix_id == "fix_image_nesting_hint" && c.plan.empty() && !c.plan.hint_de.empty(), "fix_image_nesting_hint: nur Hinweis");
        CHECK(!apply_plan(c.plan, *c.p, policy_from_facts(c.facts), confirmed("20261108T130008Z")).ok, "Hinweis-Fix: apply tut nichts");
    }
    // Befund ohne Fix
    {
        TempCopy tmp("b1_pos");
        Ctx c = make_ctx(tmp.dir, "smp.kstuff_min_version");
        CHECK(c.plan.empty() && !c.plan.warnings.empty(), "Befund ohne Fix: leerer Plan mit Warnung");
    }
}

// Platform-Huelle, die beim n-ten Umbenennen scheitert -> Rollback muss alles zuruecknehmen.
struct FailingRw final : IFileSystemRW {
    IFileSystemRW& in;
    int fail_rename_at;
    int renames = 0;
    FailingRw(IFileSystemRW& i, int n) : in(i), fail_rename_at(n) {}
    bool write_file(const std::string& p, const std::string& c) override { return in.write_file(p, c); }
    bool rename(const std::string& a, const std::string& b) override { return ++renames == fail_rename_at ? false : in.rename(a, b); }
    bool make_dirs(const std::string& p) override { return in.make_dirs(p); }
    bool remove_empty_dir(const std::string& p) override { return in.remove_empty_dir(p); }
};
struct FailingPlatform final : Platform {
    std::unique_ptr<Platform> inner;
    FailingRw rw;
    FailingPlatform(std::unique_ptr<Platform> i, int n) : inner(std::move(i)), rw(*inner->fs_rw(), n) {}
    const IFileSystem& fs() const override { return inner->fs(); }
    const IProcessList& procs() const override { return inner->procs(); }
    const ISystemInfo& sys() const override { return inner->sys(); }
    IFileSystemRW* fs_rw() override { return &rw; }
};

void rollback_test() {
    TempCopy tmp("b2_pos");
    auto before = tree(tmp.dir);
    FailingPlatform fp(make_snapshot_platform(tmp.dir.string(), true), /*fail_rename_at=*/3);  // 1. Rename ok, 2. Rename ok, 3. scheitert
    FixPlan plan;
    plan.fix_id = "test";
    plan.rule_id = "test";
    for (auto [a, b] : {std::pair{"Clean Game.ffpfs", "A/neu1.ffpfs"}, {"Cool Game (USA).ffpfs", "B/neu2.ffpfs"}, {"Other (Demo).exfat", "neu3.exfat"}}) {
        Change ch;
        ch.kind = Change::Kind::Rename;
        ch.path = std::string("/data/homebrew/") + a;
        ch.to = std::string("/data/homebrew/") + b;
        plan.changes.push_back(ch);
    }
    ApplyResult ar = apply_plan(plan, fp, PathPolicy({"/data/homebrew"}), confirmed("20261108T140000Z"));
    CHECK(!ar.ok, "Rollback: apply meldet Fehler");
    CHECK(tree(tmp.dir) == before, "Rollback: Dateibaum nach Fehler identisch (inkl. angelegter Ordner A/ und B/)");
    auto h = history(fp);
    CHECK(h.size() == 1 && h[0].state == "aborted", "Rollback: history = aborted");
    CHECK(slurp(tmp.dir / "fs/data/ps5medic/journal.jsonl").find("\"type\":\"abort\"") != std::string::npos, "Rollback: abort im Journal");
}

void plan_content_tests() {
    {
        TempCopy tmp("b2_pos");
        Ctx c = make_ctx(tmp.dir, "smp.parentheses_in_pfs_name");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].to == "/data/homebrew/Cool Game USA.ffpfs", "rename: Klammern entfernt");
    }
    {
        TempCopy tmp("b8_pos");
        Ctx c = make_ctx(tmp.dir, "smp.duplicate_titleid");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].path == "/mnt/usb0/PPSA01234.ffpfs" &&
                  c.plan.changes[0].to == "/mnt/usb0/.duplicates/PPSA01234.ffpfs",
              "duplicate: Standard behaelt die flachere/erste Quelle");
        Ctx k = make_ctx(tmp.dir, "smp.duplicate_titleid", FixOptions{"/mnt/usb0/PPSA01234.ffpfs"});
        CHECK(k.plan.changes.size() == 1 && k.plan.changes[0].path == "/data/homebrew/GameA", "duplicate: keep waehlt die andere Quelle");
    }
    {  // Ordner und Image desselben Titels: die Link-Datei von SM+ (/user/app/<TITLE_ID>/mount_img.lnk) ordnet dem Image (ohne ID im Namen) den Titel zu;
       // behalten wird die Quelle, die SM+ nutzt (das Image), der Ordner geht weg. Nach Pfadreihenfolge allein bliebe der Ordner (P < T).
        TempCopy tmp("d1_pos");
        Ctx c = make_ctx(tmp.dir, "smp.duplicate_titleid");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].path == "/mnt/usb0/homebrew/PPSA77001" && c.plan.changes[0].to == "/mnt/usb0/homebrew/.duplicates/PPSA77001",
              "duplicate: Ordner und Image desselben Titels, die von SM+ genutzte Quelle bleibt");
        auto p = make_snapshot_platform(tmp.dir.string(), false);
        const Facts f = collect_all(*p);
        int active = 0, with_id = 0;
        for (const auto& t : *f.get("smp.titles")) {
            if (t["titleid"] == "PPSA77001") ++with_id;
            if (t["titleid"] == "PPSA77001" && t["active"] == true) ++active;
        }
        CHECK(with_id == 2 && active == 1, "duplicate: beide Quellen haben die TITLE_ID, genau eine ist aktiv");
    }
    {
        TempCopy tmp("b10_pos");
        Ctx c = make_ctx(tmp.dir, "smp.scan_depth");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].content == "debug=1\nscan_depth=2\n", "scan_depth: Wert aus Fakten (max. Ebene)");
    }
    {
        TempCopy tmp("b5_pos");
        Ctx c = make_ctx(tmp.dir, "smp.backpork_conflict");
        CHECK(c.plan.empty() && !c.plan.warnings.empty(), "backpork: ohne Autoload-Datei keine Aenderung, nur Warnung");
    }
    {
        TempCopy tmp("b5_autoload");
        Ctx c = make_ctx(tmp.dir, "smp.backpork_conflict");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].content == "!1000\nshadowmountplus.elf\n!2000\n!3000\nkstuff.elf\n",
              "backpork: nur die BackPork-Zeile wird entfernt, Wartezeiten und andere Payloads bleiben");
    }
    {
        TempCopy tmp("b5_autoload_pldmgr");
        Ctx c = make_ctx(tmp.dir, "smp.backpork_conflict");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].path == "/data/pldmgr/autoload.txt" && c.plan.changes[0].content == "!3000\n",
              "backpork: auch die Autoload-Liste des Payload Managers wird gefunden");
    }
    {  // Marcs Entscheidungen 13. Nov. 2026: Autoload-Listen, defekte ELF, Schreibweise auf exFAT
        TempCopy tmp("al3_pos_duplicate");
        Ctx c = make_ctx(tmp.dir, "autoload.entry_duplicate");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].content == "local/a\n!1000\n", "autoload_remove_duplicates: erste Zeile bleibt, die doppelte entfaellt, Wartezeit bleibt");
    }
    {
        TempCopy tmp("al2_pos_loader_missing");
        Ctx c = make_ctx(tmp.dir, "autoload.entry_missing");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].content == "# gibt-es-nicht.elf\n", "autoload_fix_missing: WebKit-Liste: Eintrag ohne Datei wird auskommentiert");
    }
    {
        TempCopy tmp("al2_pos_missing_slot");
        Ctx c = make_ctx(tmp.dir, "autoload.entry_missing");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].content.empty(), "autoload_fix_missing: Payload Manager: Zeile ohne Ordner entfaellt (dort gibt es keine Kommentare)");
    }
    {
        TempCopy tmp("al5_pos_disabled");
        Ctx c = make_ctx(tmp.dir, "pldmgr.autoload_disabled");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].path == "/data/pldmgr/pldmgr_config.txt" && c.plan.changes[0].content == "AUTOLOAD_ENABLED=1\nAUTOLOAD_DELAY=5\n",
              "pldmgr_enable_autoload: nur AUTOLOAD_ENABLED wird auf 1 gesetzt");
    }
    {
        TempCopy tmp("elf_pos_pldmgr");
        Ctx c = make_ctx(tmp.dir, "payload.elf_invalid");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].to == "/data/pldmgr/payloads/local/bad/.medic_broken/bad.elf", "quarantine_broken_elf: Datei geht in .medic_broken daneben");
    }
    {
        TempCopy tmp("elf_pos_tiny");  // /mnt/usb0/x.elf: ein erlaubter USB-Ordner
        Ctx c = make_ctx(tmp.dir, "payload.elf_invalid");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].to == "/mnt/usb0/.medic_broken/x.elf", "quarantine_broken_elf: auch auf einem USB-Laufwerk, nur in den Ordner daneben");
    }
}

// ---- Snapshot-Export -----------------------------------------------------

json comparable_facts(const Facts& f) {
    json out = json::object();
    for (const char* k : {"smp.titles", "smp.deep_sources", "smp.hidden_sources", "smp.mounts", "smp.images", "smp.scan_roots", "smp.fakelib_enabled",
                          "smp.debug", "smp.scan_depth", "proc.running", "fw.version", "kstuff.version", "kstuff.origin", "smp.version",
                          "net.port_9021", "jb.seconds_after_boot", "jb.first_process", "ps5theme.version", "storage.mounts", "games.fw", "games.need_backport"}) {
        const json* v = f.get(k);
        if (!v) continue;
        out[k] = *v;
        if (std::string(k) == "smp.images")
            for (auto& img : out[k]) {
                img.erase("size");  // Export legt Images als leere Dateien (bzw. nur den Kopf) an
                img.erase("volume_bytes");
            }
    }
    return out;
}

// Nur live auswertbare Regeln: storage.oversized_reservation vergleicht Reservierung und Dateigroesse, der Export
// enthaelt aber nur leere Dateien und laesst /user/app weg (siehe snapshot_export.cpp).
constexpr const char* kLiveOnlyRule = "storage.oversized_reservation";

// Ebenso nur live auswertbar: Regeln, die den INHALT von Dateien brauchen (Version und Marken der Laufzeit-Bibliothek, Kopf und Volumes der Pack-Indizes,
// Meldungen in kraken.log); der Export legt Spielordner nur als Namen an.
bool live_only(const std::string& rule) {
    static const std::set<std::string> rules = {"storage.oversized_reservation", "game.pack_runtime_mismatch", "smp.emulator_update_replaces_runtime", "game.pack_no_emu_index",
                                                "game.ng_save_risk", "game.pack_incomplete", "kraken.pkg_sparse_extent", "game.incomplete_folder"};  // incomplete_folder: braucht sce_module, das der Export nicht anlegt
    return rules.count(rule) != 0;
}

std::string findings_summary(const std::vector<Finding>& fs_) {
    std::string s;
    for (auto& f : fs_) {
        if (live_only(f.rule_id)) continue;
        s += f.rule_id + "|" + f.fix + "|";
        for (auto e : f.evidence) {
            // Der Export ersetzt den Titel eines Spielstands ("<ID> <Name> (<Titel>)") durch einen Platzhalter: er gehoert nicht in einen Export (Datenschutz)
            if (f.rule_id == "savedata.foreign_account") {
                const auto paren = e.find(" (");
                if (paren != std::string::npos) e.erase(paren);
            }
            s += e + ";";
        }
        s += "\n";
    }
    return s;
}

void export_tests() {
    int n = 0;
    for (auto& e : fs::directory_iterator(fs::path(MEDIC_SOURCE_DIR) / "tests" / "fixtures")) {
        if (!e.is_directory()) continue;
        const std::string name = e.path().filename().string();
        auto p1 = make_snapshot_platform(e.path().string());
        Facts f1 = collect_all(*p1);
        fs::path out = fs::temp_directory_path() / ("medic-export-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++n));
        ExportResult r = export_snapshot(*p1, f1, out.string(), "20261108T150000Z");
        CHECK(r.ok, "Export " + name + " (" + r.error + ")");
        if (!r.ok) continue;
        auto p2 = make_snapshot_platform(out.string());
        Facts f2 = collect_all(*p2);
        CHECK(comparable_facts(f1) == comparable_facts(f2), "Export " + name + ": Fakten nach Round-Trip gleich");
        CHECK(findings_summary(evaluate(rules(), f1)) == findings_summary(evaluate(rules(), f2)), "Export " + name + ": Befunde nach Round-Trip gleich");
        for (auto& finding : evaluate(rules(), f2))
            CHECK(finding.rule_id != kLiveOnlyRule, "Export " + name + ": aus dem Export entsteht kein Befund " + kLiveOnlyRule);
        std::error_code ec;
        fs::remove_all(out, ec);
    }
    // Nicht-leeres Ziel wird abgelehnt, nichts ueberschrieben
    {
        TempCopy tmp("b2_pos");
        auto p = make_snapshot_platform(tmp.dir.string());
        Facts f = collect_all(*p);
        auto before = tree(tmp.dir);
        CHECK(!export_snapshot(*p, f, tmp.dir.string(), "20261108T150001Z").ok, "Export: nicht-leeres Ziel abgelehnt");
        CHECK(tree(tmp.dir) == before, "Export: vorhandener Ordner unveraendert");
    }
    // Spielinhalte werden nicht exportiert: nur sce_sys/param.json
    {
        TempCopy tmp("b8_pos");
        std::ofstream(tmp.dir / "fs/data/homebrew/GameA/eboot.bin") << std::string("" "ELF") << "GEHEIM" << std::string(64, 'x');  // gueltiger Anfang: wird weder gemeldet noch exportiert
        auto p = make_snapshot_platform(tmp.dir.string());
        Facts f = collect_all(*p);
        fs::path out = tmp.dir.parent_path() / (tmp.dir.filename().string() + "-out");
        CHECK(export_snapshot(*p, f, out.string(), "20261108T150002Z").ok, "Export: ok");
        CHECK(fs::exists(out / "fs/data/homebrew/GameA/sce_sys/param.json") && !fs::exists(out / "fs/data/homebrew/GameA/eboot.bin"),
              "Export: Spielordner nur mit param.json");
        std::error_code ec;
        fs::remove_all(out, ec);
    }
}

// ---- Absturz, Wiederherstellung, wiederholbares Undo (Befunde der Sicherheitspruefung vom 9. Nov. 2026) ------------

std::string journal_text(const fs::path& dir) { return slurp(dir / "fs/data/ps5medic/journal.jsonl"); }

// Journal auf bestimmte Eintragsarten kuerzen: so sieht es nach einem Absturz aus (Intent geschrieben, nichts verbucht).
void keep_journal_types(Platform& p, const fs::path& dir, const std::set<std::string>& types) {
    std::istringstream in(journal_text(dir));
    std::string out;
    for (std::string line; std::getline(in, line);) {
        json j = json::parse(line, nullptr, false);
        if (j.is_object() && types.count(j.value("type", ""))) out += line + "\n";
    }
    p.fs_rw()->write_file(kJournalPath, out);
}

// Wie FailingRw, scheitert aber bei mehreren Umbenennungen (hier: bei der Aenderung und beim Rollback).
struct FailSetRw final : IFileSystemRW {
    IFileSystemRW& in;
    std::set<int> fail_at;
    int renames = 0;
    FailSetRw(IFileSystemRW& i, std::set<int> f) : in(i), fail_at(std::move(f)) {}
    bool write_file(const std::string& p, const std::string& c) override { return in.write_file(p, c); }
    bool rename(const std::string& a, const std::string& b) override { return fail_at.count(++renames) ? false : in.rename(a, b); }
    bool make_dirs(const std::string& p) override { return in.make_dirs(p); }
    bool remove_empty_dir(const std::string& p) override { return in.remove_empty_dir(p); }
};
struct FailSetPlatform final : Platform {
    std::unique_ptr<Platform> inner;
    FailSetRw rw;
    FailSetPlatform(std::unique_ptr<Platform> i, std::set<int> f) : inner(std::move(i)), rw(*inner->fs_rw(), std::move(f)) {}
    const IFileSystem& fs() const override { return inner->fs(); }
    const IProcessList& procs() const override { return inner->procs(); }
    const ISystemInfo& sys() const override { return inner->sys(); }
    IFileSystemRW* fs_rw() override { return &rw; }
};

FixPlan three_renames() {
    FixPlan plan;
    plan.fix_id = "test";
    plan.rule_id = "test";
    for (auto [a, b] : {std::pair{"Clean Game.ffpfs", "A/neu1.ffpfs"}, {"Cool Game (USA).ffpfs", "B/neu2.ffpfs"}, {"Other (Demo).exfat", "neu3.exfat"}}) {
        Change ch;
        ch.kind = Change::Kind::Rename;
        ch.path = std::string("/data/homebrew/") + a;
        ch.to = std::string("/data/homebrew/") + b;
        plan.changes.push_back(ch);
    }
    return plan;
}

void recovery_tests() {
    const std::string rule = "smp.autotoggle_crash_1360";

    // A) Absturz nach Intent und Umbenennen, nichts verbucht: Undo holt die Datei zurueck. Die Wurzeln stehen im Journal,
    //    die heutige Policy kennt keine Scanpfade (der Scanpfad koennte inzwischen aus der Config verschwunden sein).
    {
        TempCopy tmp("b2_pos");
        const auto before = tree(tmp.dir);
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        const std::string from = "/data/homebrew/Cool Game (USA).ffpfs", to = "/data/homebrew/X/neu.ffpfs";
        json intent = {{"type", "intent"}, {"txn", "T1"}, {"seq", 0}, {"ts", "20261108T150000Z"}, {"op", "rename"}, {"path", from}, {"to", to},
                       {"created_dirs", json::array({"/data/homebrew/X"})}, {"sha_before", ""}, {"roots", json::array({"/data/homebrew"})}};
        CHECK(p->fs_rw()->append_file(kJournalPath, intent.dump() + "\n"), "Absturz A: Intent angehaengt");
        CHECK(p->fs_rw()->rename(from, to), "Absturz A: Umbenennen ist passiert");
        auto h = history(*p);
        CHECK(h.size() == 1 && h[0].state == "incomplete", "Absturz A: history = incomplete");
        UndoResult ur = undo_txn(*p, PathPolicy(), "T1", "20261108T150001Z");
        CHECK(ur.ok, "Absturz A: undo holt die Datei zurueck (" + ur.error + ")");
        CHECK(tree(tmp.dir) == before, "Absturz A: Dateibaum identisch (angelegter Ordner X wieder weg)");
        h = history(*p);
        CHECK(h.size() == 1 && h[0].state == "undone", "Absturz A: history = undone");
        CHECK(!undo_txn(*p, PathPolicy(), "T1", "20261108T150002Z").ok, "Absturz A: zweites Undo abgelehnt");
    }
    // B) Absturz nach dem Schreiben, vor dem Verbuchen (nur Intents im Journal): Original kommt aus dem Backup zurueck.
    {
        TempCopy tmp("b3_pos");
        const auto before = tree(tmp.dir);
        Ctx c = make_ctx(tmp.dir, rule);
        PathPolicy pol = policy_from_facts(c.facts);
        ApplyResult ar = apply_plan(c.plan, *c.p, pol, confirmed("20261108T150100Z"));
        CHECK(ar.ok, "Absturz B: apply ok");
        keep_journal_types(*c.p, tmp.dir, {"intent"});
        auto h = history(*c.p);
        CHECK(h.size() == 1 && h[0].state == "incomplete", "Absturz B: history = incomplete");
        UndoResult ur = undo_txn(*c.p, pol, ar.txn, "20261108T150101Z");
        CHECK(ur.ok, "Absturz B: undo ok (" + ur.error + ")");
        CHECK(tree(tmp.dir) == before, "Absturz B: config.ini wieder im Ausgangszustand");
        CHECK(history(*c.p)[0].state == "undone", "Absturz B: history = undone");
    }
    // C) Absturz vor dem Schreiben (Intent da, Datei noch das Original): Undo ist ein sauberes Nichtstun.
    {
        TempCopy tmp("b3_pos");
        const std::string original = slurp(tmp.dir / "fs/data/shadowmount/config.ini");
        const auto before = tree(tmp.dir);
        Ctx c = make_ctx(tmp.dir, rule);
        PathPolicy pol = policy_from_facts(c.facts);
        ApplyResult ar = apply_plan(c.plan, *c.p, pol, confirmed("20261108T150200Z"));
        CHECK(ar.ok, "Absturz C: apply ok");
        keep_journal_types(*c.p, tmp.dir, {"intent"});
        c.p->fs_rw()->write_file("/data/shadowmount/config.ini", original);
        UndoResult ur = undo_txn(*c.p, pol, ar.txn, "20261108T150201Z");
        CHECK(ur.ok && tree(tmp.dir) == before, "Absturz C: undo ok, nichts veraendert (" + ur.error + ")");
    }
    // D) Ein Undo, das nur teilweise lief, laesst sich wiederholen (frueher: "Datei fehlt am Zielort").
    {
        TempCopy tmp("b2_pos");
        const auto before = tree(tmp.dir);
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        const PathPolicy pol({"/data/homebrew"});
        ApplyResult ar = apply_plan(three_renames(), *p, pol, confirmed("20261108T150300Z"));
        CHECK(ar.ok, "Undo wiederholen: apply ok");
        CHECK(p->fs_rw()->rename("/data/homebrew/B/neu2.ffpfs", "/data/homebrew/Cool Game (USA).ffpfs"), "Undo wiederholen: eine Datei war schon zurueck");
        UndoResult ur = undo_txn(*p, pol, ar.txn, "20261108T150301Z");
        CHECK(ur.ok, "Undo wiederholen: Rest wird zurueckgedreht (" + ur.error + ")");
        CHECK(tree(tmp.dir) == before, "Undo wiederholen: Dateibaum identisch, auch der leere Ordner B ist weg");
    }
    // E) Rollback scheitert selbst (Stick abgezogen): history = incomplete, ein spaeteres Undo holt alles zurueck.
    {
        TempCopy tmp("b2_pos");
        const auto before = tree(tmp.dir);
        FailSetPlatform fp(make_snapshot_platform(tmp.dir.string(), true), {3, 4});  // 3. Aenderung scheitert, dann auch der Rollback von Nr. 2
        ApplyResult ar = apply_plan(three_renames(), fp, PathPolicy({"/data/homebrew"}), confirmed("20261108T150400Z"));
        CHECK(!ar.ok && ar.error.find("Rollback unvollständig") != std::string::npos, "Rollback-Fehler: apply meldet unvollstaendigen Rollback");
        auto h = history(fp);
        CHECK(h.size() == 1 && h[0].state == "incomplete", "Rollback-Fehler: history = incomplete (nicht aborted)");
        CHECK(tree(tmp.dir) != before, "Rollback-Fehler: eine Datei liegt noch am neuen Ort");
        auto p2 = make_snapshot_platform(tmp.dir.string(), true);
        UndoResult ur = undo_txn(*p2, PathPolicy({"/data/homebrew"}), ar.txn, "20261108T150401Z");
        CHECK(ur.ok, "Rollback-Fehler: undo holt den Rest zurueck (" + ur.error + ")");
        CHECK(tree(tmp.dir) == before, "Rollback-Fehler: Dateibaum identisch");
    }
    // F) Abgeschnittene Journalzeile (Strom weg mitten im Schreiben): Verlauf, Undo und weitere Reparaturen funktionieren.
    {
        TempCopy tmp("b3_pos");
        Ctx c = make_ctx(tmp.dir, rule);
        PathPolicy pol = policy_from_facts(c.facts);
        ApplyResult ar = apply_plan(c.plan, *c.p, pol, confirmed("20261108T150500Z"));
        CHECK(ar.ok, "Journal-Rest: apply ok");
        CHECK(c.p->fs_rw()->append_file(kJournalPath, "{\"type\":\"op\",\"tx"), "Journal-Rest: Bruchstueck ohne Zeilenende");
        auto h = history(*c.p);
        CHECK(h.size() == 1 && h[0].state == "applied", "Journal-Rest: Verlauf bleibt lesbar");
        UndoResult ur = undo_txn(*c.p, pol, ar.txn, "20261108T150501Z");
        CHECK(ur.ok, "Journal-Rest: undo geht (" + ur.error + ")");
        Ctx c2 = make_ctx(tmp.dir, rule);
        ApplyResult a2 = apply_plan(c2.plan, *c2.p, policy_from_facts(c2.facts), confirmed("20261108T150502Z"));
        CHECK(a2.ok, "Journal-Rest: danach laesst sich weiter reparieren (" + a2.error + ")");
        CHECK(history(*c2.p).size() == 2, "Journal-Rest: beide Transaktionen im Verlauf");
    }
    // G) Fehlt eine Operation im Journal (unlesbare Zeile), verweigert Undo -- sonst bliebe ein Teil unbemerkt.
    {
        TempCopy tmp("b2_pos");
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        const PathPolicy pol({"/data/homebrew"});
        ApplyResult ar = apply_plan(three_renames(), *p, pol, confirmed("20261108T150600Z"));
        CHECK(ar.ok, "Luecke im Journal: apply ok");
        std::istringstream in(journal_text(tmp.dir));
        std::string out;
        for (std::string line; std::getline(in, line);) {
            json j = json::parse(line, nullptr, false);
            const std::string type = j.is_object() ? j.value("type", "") : std::string();
            // Intent und Op der zweiten Operation gehen verloren (der Intent allein reichte fuer ein Undo noch aus).
            if ((type == "op" || type == "intent") && j.value("seq", -1) == 1 && j.value("txn", "") == ar.txn) out += "{\"type\":\"op\",\"seq\"\n";
            else out += line + "\n";
        }
        p->fs_rw()->write_file(kJournalPath, out);
        const auto mid = tree(tmp.dir);
        UndoResult ur = undo_txn(*p, pol, ar.txn, "20261108T150601Z");
        CHECK(!ur.ok && ur.error.find("Journal unvollständig") != std::string::npos, "Luecke im Journal: undo abgelehnt (" + ur.error + ")");
        CHECK(tree(tmp.dir) == mid, "Luecke im Journal: nichts veraendert");
    }
    // H) Die Datei hat sich seit der Vorschau geaendert: apply lehnt ab, nichts wird ueberschrieben, kein Journal.
    {
        TempCopy tmp("b3_pos");
        Ctx c = make_ctx(tmp.dir, rule);
        c.p->fs_rw()->write_file("/data/shadowmount/config.ini", slurp(tmp.dir / "fs/data/shadowmount/config.ini") + "von_aussen=1\n");
        const auto mid = tree(tmp.dir);
        ApplyResult ar = apply_plan(c.plan, *c.p, policy_from_facts(c.facts), confirmed("20261108T150700Z"));
        CHECK(!ar.ok && ar.error.find("seit der Vorschau") != std::string::npos, "veralteter Plan: abgelehnt (" + ar.error + ")");
        CHECK(tree(tmp.dir) == mid && !fs::exists(tmp.dir / "fs/data/ps5medic"), "veralteter Plan: nichts geschrieben, kein Journal");
        Ctx fresh = make_ctx(tmp.dir, rule);  // neue Vorschau -> geht wieder
        CHECK(apply_plan(fresh.plan, *fresh.p, policy_from_facts(fresh.facts), confirmed("20261108T150701Z")).ok, "veralteter Plan: neue Vorschau wird angewendet");
    }
    // I) Whitelist-Wurzeln aus Scanpfaden: nur Datenbereiche.
    {
        PathPolicy pol({"/user/app/x", "/system_data/foo", "/mnt/sandbox/a/b", "/preinst/y", "/mnt/usbX/y", "/mnt/usb0/Games", "/mnt/ext1", "/data/games/x", "/mnt/usb0/Games"});
        CHECK(!pol.allowed("/user/app/x/a") && !pol.allowed("/system_data/foo/a") && !pol.allowed("/mnt/sandbox/a/b/c") && !pol.allowed("/preinst/y/a"),
              "Whitelist: Scanpfade auf Systembereichen werden nicht beschreibbar");
        CHECK(!pol.allowed("/mnt/usbX/y/a"), "Whitelist: /mnt/usb<Buchstabe> ist kein Laufwerk");
        CHECK(pol.allowed("/mnt/usb0/Games/a") && pol.allowed("/mnt/ext1/a") && pol.allowed("/data/games/x/a"), "Whitelist: USB, ext und /data/... bleiben erlaubt");
        CHECK(pol.extra_roots().size() == 3, "Whitelist: genau die drei gueltigen Wurzeln, ohne Doppelte");
    }
    // J) Ein vorhandener Backup-Ordner wird nie wiederverwendet (auch wenn das Journal fehlt).
    {
        TempCopy tmp("b3_pos");
        Ctx c = make_ctx(tmp.dir, rule);
        c.p->fs_rw()->write_file(std::string(kBackupRoot) + "/20261108T150800Z/alt.txt", "altes Backup");
        ApplyResult ar = apply_plan(c.plan, *c.p, policy_from_facts(c.facts), confirmed("20261108T150800Z"));
        CHECK(ar.ok && ar.txn == "20261108T150800Z-2", "Backup-Ordner: vorhandener wird uebersprungen (" + ar.txn + ")");
        CHECK(slurp(tmp.dir / "fs/data/ps5medic/backups/20261108T150800Z/alt.txt") == "altes Backup", "Backup-Ordner: altes Backup unveraendert");
    }
    // K) Falsche Feldtypen im Journal werfen keine Ausnahme.
    {
        TempCopy tmp("b3_pos");
        Ctx c = make_ctx(tmp.dir, rule);
        c.p->fs_rw()->write_file(kJournalPath,
                                 "{\"type\":\"op\",\"txn\":5,\"seq\":\"x\",\"path\":7}\n{\"type\":\"commit\",\"txn\":[1]}\n{\"type\":\"intent\",\"txn\":\"T\",\"seq\":\"z\"}\n");
        bool threw = false;
        try {
            history(*c.p);
            undo_txn(*c.p, policy_from_facts(c.facts), "T", "20261108T150900Z");
        } catch (...) {
            threw = true;
        }
        CHECK(!threw, "kaputtes Journal: keine Ausnahme bei Verlauf/Undo");
        CHECK(!undo_txn(*c.p, policy_from_facts(c.facts), "T", "20261108T150901Z").ok, "kaputtes Journal: Undo ohne lesbare Operationen abgelehnt");
        CHECK(apply_plan(c.plan, *c.p, policy_from_facts(c.facts), confirmed("20261108T150902Z")).ok, "kaputtes Journal: neue Reparatur geht trotzdem");
    }
    // L) Symlinks: eine Temp-Datei-Falle schreibt nicht ans Ziel, ein Symlink wird nicht umbenannt.
    {
        TempCopy tmp("b3_pos");
        const fs::path victim = tmp.dir / "victim.txt";
        std::ofstream(victim) << "geheim";
        std::error_code ec;
        fs::create_symlink(victim, tmp.dir / "fs/data/shadowmount/config.ini.medic-tmp", ec);
        if (!ec) {
            Ctx c = make_ctx(tmp.dir, rule);
            ApplyResult ar = apply_plan(c.plan, *c.p, policy_from_facts(c.facts), confirmed("20261108T151000Z"));
            CHECK(ar.ok, "Symlink-Falle: apply ok (" + ar.error + ")");
            CHECK(slurp(victim) == "geheim", "Symlink-Falle: Ziel des Symlinks unveraendert");
        }
        TempCopy tmp2("b2_pos");
        const fs::path target = tmp2.dir / "ziel.bin";
        std::ofstream(target) << "x";
        fs::create_symlink(target, tmp2.dir / "fs/data/homebrew/Link.ffpfs", ec);
        if (!ec) {
            auto p = make_snapshot_platform(tmp2.dir.string(), true);
            FixPlan plan;
            Change ch;
            ch.kind = Change::Kind::Rename;
            ch.path = "/data/homebrew/Link.ffpfs";
            ch.to = "/data/homebrew/Neu.ffpfs";
            plan.changes.push_back(ch);
            const auto mid = tree(tmp2.dir);
            CHECK(!apply_plan(plan, *p, PathPolicy({"/data/homebrew"}), confirmed("20261108T151001Z")).ok, "Symlink: Umbenennen abgelehnt");
            CHECK(tree(tmp2.dir) == mid, "Symlink: nichts veraendert");
        }
    }
}

// ---- v1-Zeilen in v2-Dateien, Autoload-Fixes, Zugangsdaten im Export (Recherche vom 9. Nov. 2026) -------------------------

void stack_tests() {
    // SM+ 1.7beta12 ignoriert flache v1-Zeilen in einer v2-Datei: der Collector darf sie nicht mitzaehlen.
    {
        TempCopy tmp("smp2_pos_blackbox");
        auto p = make_snapshot_platform(tmp.dir.string());
        const Facts f = collect_all(*p);
        const json* depth = f.get("smp.scan_depth");
        CHECK(depth && depth->get<int>() == 1, "v2-Datei: flaches scan_depth=2 gilt nicht (Tiefe bleibt 1)");
        const json* keys = f.get("smp.v1_keys");
        CHECK(keys && keys->size() == 1 && (*keys)[0]["key"] == "scan_depth" && (*keys)[0]["target"] == "[scan] depth", "v2-Datei: flache Zeile samt Ziel erkannt");
    }
    {  // Plan: Zeile entfernen, Wert in die Sektion schreiben, Kommentar und Rest bleiben
        TempCopy tmp("smp2_pos_blackbox");
        Ctx c = make_ctx(tmp.dir, "smp.v1_keys_in_v2");
        CHECK(c.plan.changes.size() == 1, "v1-Fix: ein Plan");
        if (!c.plan.changes.empty()) {
            const std::string& t = c.plan.changes[0].content;
            CHECK(t.find("scan_depth") == std::string::npos && t.find("depth=2") != std::string::npos && t.find("# Added by another tool") != std::string::npos,
                  "v1-Fix: flache Zeile weg, [scan] depth=2 gesetzt, Kommentar bleibt");
        }
    }
    {  // Zeilen ohne verifizierte Entsprechung bleiben stehen, der Plan sagt es
        TempCopy tmp("smp2_pos_unmapped");
        Ctx c = make_ctx(tmp.dir, "smp.v1_keys_in_v2");
        CHECK(c.plan.empty() && !c.plan.warnings.empty(), "v1-Fix: nur nicht abbildbare Zeile -> kein Plan, Hinweis");
    }
    // Autoload: Wartezeit wird an der richtigen Stelle eingefuegt, Kommentare und Zeilenenden bleiben
    {
        TempCopy tmp("al1_pos_loader");
        Ctx c = make_ctx(tmp.dir, "autoload.elfldr_without_delay");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].content == "# Reihenfolge\nelfldr.elf\n!4000\nkstuff.elf # danach\n", "Autoload-Fix: !4000 zwischen elfldr und kstuff");
    }
    {
        TempCopy tmp("al1_pos_pldmgr");
        Ctx c = make_ctx(tmp.dir, "autoload.elfldr_without_delay");
        CHECK(c.plan.changes.size() == 1 && c.plan.changes[0].content == "local/elfldr\n!4000\nlocal/kstuff\n", "Autoload-Fix (Payload Manager): !4000 eingefuegt");
    }
    // Namen in der Autoload-Liste sind case-sensitiv (das Dateisystem der Fixtures auf NTFS ist es nicht, daher in /tmp aufgebaut)
    {
        TempCopy tmp("al2_pos_loader_missing");
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        std::string elf(64, '\0');
        elf.replace(0, 4, "\x7f" "ELF");
        elf[4] = 2;
        elf[0x36] = 56;
        p->fs_rw()->write_file("/data/ps5_autoloader/kstuff.elf", elf);
        p->fs_rw()->write_file("/data/ps5_autoloader/autoload.txt", "KSTUFF.elf\n");
        Facts f = collect_all(*p);
        const json* probs = f.get("autoload.problems");
        CHECK(probs && probs->size() == 1 && (*probs)[0]["kind"] == "entry_case", "Autoload: Eintrag nur in Gross-/Kleinschreibung falsch -> entry_case");
    }
    // Zugangsdaten werden beim Export geschwaerzt (SM+ speichert SMB-Passwoerter im Klartext)
    {
        TempCopy tmp("smp2_neg_same_names");
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        p->fs_rw()->write_file("/data/shadowmount/config.ini", "[config]\nversion = 2\n\n[smb:nas]\nhost = 192.0.2.1\npassword = geheim123\n# password = kommentar\nPasswd=auch\n");
        p->fs_rw()->write_file("/data/shadowmount/profiles.ini", "[config]\nversion = 2\n[smb:x]\nSecret = s3\n");
        const fs::path dest = tmp.dir / "export";
        ExportResult er = export_snapshot(*p, collect_all(*p), dest.string(), "20261108T160000Z");
        CHECK(er.ok, "Export mit Zugangsdaten: ok (" + er.error + ")");
        const std::string cfg = slurp(dest / "fs/data/shadowmount/config.ini"), prof = slurp(dest / "fs/data/shadowmount/profiles.ini");
        CHECK(cfg.find("geheim123") == std::string::npos && cfg.find("auch") == std::string::npos && prof.find("s3") == std::string::npos, "Export: Passwoerter geschwaerzt");
        CHECK(cfg.find("password = ***") != std::string::npos || cfg.find("password=***") != std::string::npos || cfg.find("password= ***") != std::string::npos, "Export: Schluessel bleibt, Wert ist ***");
        CHECK(cfg.find("host = x.x.x.x") != std::string::npos && cfg.find("192.0.2.1") == std::string::npos && cfg.find("kommentar") == std::string::npos && cfg.find("# password = ***") != std::string::npos, "Export: IP-Adresse maskiert, auch ein auskommentiertes Passwort wird geschwaerzt");
    }
    // Ein Symlink als Zwischenordner fuehrt Schreiben nicht aus den erlaubten Ordnern hinaus (ab der dritten Pfadstufe)
    {
        TempCopy tmp("smp2_neg_same_names");
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        fs::create_directories(tmp.dir / "fs/data/ps5medic/real");
        fs::create_directories(tmp.dir / "fs/user/app/OUTSIDE");
        std::error_code lec;
        fs::create_directory_symlink(tmp.dir / "fs/user/app/OUTSIDE", tmp.dir / "fs/data/ps5medic/link", lec);
        if (!lec) {
            CHECK(!p->fs_rw()->write_file("/data/ps5medic/link/x.txt", "x") && !fs::exists(tmp.dir / "fs/user/app/OUTSIDE/x.txt"), "Symlink als Zwischenordner: write_file verweigert");
            CHECK(!p->fs_rw()->append_file("/data/ps5medic/link/y.txt", "y") && !fs::exists(tmp.dir / "fs/user/app/OUTSIDE/y.txt"), "Symlink als Zwischenordner: append_file verweigert");
            CHECK(!p->fs_rw()->make_dirs("/data/ps5medic/link/neu"), "Symlink als Zwischenordner: make_dirs verweigert");
            CHECK(p->fs_rw()->write_file("/data/ps5medic/real/ok.txt", "ok"), "normaler Unterordner: write_file geht weiter");
        } else {
            CHECK(true, "Symlinks hier nicht verfuegbar: uebersprungen");
        }
    }
    // Weitere Zugangsdaten: Benutzername, Kurzformen, Adressen mit Benutzer:Passwort im Log
    {
        TempCopy tmp("smp2_neg_same_names");
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        p->fs_rw()->write_file("/data/shadowmount/config.ini", "[smb:nas]\nusername = alice\npwd = p1\nLogin=l1\n");
        p->fs_rw()->write_file("/data/shadowmount/debug.log", "mount smb://bob:topsecret@192.0.2.5/share ok\nzweite Zeile ohne Adresse\nhttp://example.invalid/pfad@x\n");
        const fs::path dest = tmp.dir / "export";
        ExportResult er = export_snapshot(*p, collect_all(*p), dest.string(), "20261108T160000Z");
        CHECK(er.ok, "Export Zugangsdaten 2: ok (" + er.error + ")");
        const std::string cfg = slurp(dest / "fs/data/shadowmount/config.ini"), log = slurp(dest / "fs/data/shadowmount/debug.log");
        CHECK(cfg.find("alice") == std::string::npos && cfg.find("p1") == std::string::npos && cfg.find("l1") == std::string::npos, "Export: Benutzername, pwd und login geschwaerzt");
        CHECK(log.find("bob") == std::string::npos && log.find("topsecret") == std::string::npos && log.find("smb://***@x.x.x.x/share ok") != std::string::npos, "Export: smb-Adresse im Log ohne Benutzer, Passwort und IP");
        CHECK(log.find("zweite Zeile ohne Adresse") != std::string::npos && log.find("http://example.invalid/pfad@x") != std::string::npos, "Export: Zeilen ohne Zugangsdaten bleiben");
    }
    // MAC, IPv4 und Konto-Felder im Export maskiert; Loopback, Versionsnummern und Firmware-Strings bleiben
    {
        TempCopy tmp("smp2_neg_same_names");
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        p->fs_rw()->write_file("/data/shadowmount/config.ini", "[config]\nversion = 2\nmac = aa:bb:cc:dd:ee:01\nAccountId = 1234567890\nserial_no = S9\n");
        p->fs_rw()->write_file("/data/shadowmount/debug.log", "link 00-11-22-33-44-55 up, ip 10.1.2.3 gw 192.168.1.1\nlokal 127.0.0.1 und 0.0.0.0 bleiben\nSM+ 1.7.0 fw 12.00.00.00 ver 1.2.3.4x\n");
        const fs::path dest = tmp.dir / "export";
        ExportResult er = export_snapshot(*p, collect_all(*p), dest.string(), "20261108T160000Z");
        CHECK(er.ok, "Export Adressen: ok (" + er.error + ")");
        const std::string cfg = slurp(dest / "fs/data/shadowmount/config.ini"), log = slurp(dest / "fs/data/shadowmount/debug.log");
        CHECK(cfg.find("1234567890") == std::string::npos && cfg.find("S9") == std::string::npos && cfg.find("aa:bb") == std::string::npos, "Export: Konto-ID, Seriennummer und MAC im Config geschwaerzt");
        CHECK(log.find("00-11-22") == std::string::npos && log.find("10.1.2.3") == std::string::npos && log.find("192.168.1.1") == std::string::npos && log.find("**:**:**:**:**:** up, ip x.x.x.x gw x.x.x.x") != std::string::npos,
              "Export: MAC und IPv4 im Log maskiert");
        CHECK(log.find("127.0.0.1 und 0.0.0.0 bleiben") != std::string::npos && log.find("SM+ 1.7.0 fw 12.00.00.00 ver 1.2.3.4x") != std::string::npos, "Export: Loopback, Versionen und Firmware-Strings bleiben");
    }
    // param.json eines Spielordners: Titelname und Konto-Felder maskiert, titleId und Firmware-Felder bleiben; kaputtes JSON wird nicht roh kopiert
    {
        TempCopy tmp("smp2_neg_same_names");
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        CHECK(p->fs_rw()->make_dirs("/data/homebrew/PPSA99001/sce_sys") && p->fs_rw()->make_dirs("/data/homebrew/PPSA99002/sce_sys"), "param-Test: Ordner angelegt");
        CHECK(p->fs_rw()->write_file("/data/homebrew/PPSA99001/sce_sys/param.json",
                                     R"({"titleId":"PPSA99001","requiredSystemSoftwareVersion":"0x0900000000000000","localizedParameters":{"defaultLanguage":"en-US","en-US":{"titleName":"Geheimes Spiel"}},"accountId":"abc"})"),
              "param-Test: param.json geschrieben");
        CHECK(p->fs_rw()->write_file("/data/homebrew/PPSA99002/sce_sys/param.json", "{kaputt Geheimtitel"), "param-Test: kaputte param.json geschrieben");
        const fs::path dest = tmp.dir / "export";
        ExportResult er = export_snapshot(*p, collect_all(*p), dest.string(), "20261108T160000Z");
        CHECK(er.ok, "Export param.json: ok (" + er.error + ")");
        const std::string good = slurp(dest / "fs/data/homebrew/PPSA99001/sce_sys/param.json"), bad = slurp(dest / "fs/data/homebrew/PPSA99002/sce_sys/param.json");
        CHECK(good.find("Geheimes Spiel") == std::string::npos && good.find("abc") == std::string::npos && good.find("PPSA99001") != std::string::npos && good.find("0x0900000000000000") != std::string::npos, "Export: param.json ohne Titelname und Konto, mit titleId und Firmware");
        CHECK(bad.find("Geheimtitel") == std::string::npos, "Export: kaputte param.json wird nicht roh kopiert");
    }
    // Quarantaene der Duplikate: ".duplicates" wird von SM+ nicht gescannt (Punkt-Namen), "_duplicates" schon
    {
        TempCopy tmp("b8_pos");
        auto p = make_snapshot_platform(tmp.dir.string(), true);
        CHECK(p->fs_rw()->make_dirs("/mnt/usb0/.duplicates") && p->fs_rw()->write_file("/mnt/usb0/.duplicates/PPSA77777.ffpfs", ""), "Scanner: Test-Ordner angelegt");
        const Facts f = collect_all(*p);
        bool found = false;
        for (const auto& t : *f.get("smp.titles"))
            if (t["titleid"] == "PPSA77777") found = true;
        CHECK(!found, "Scanner: Inhalt von .duplicates wird nicht als Quelle gezaehlt");
    }
}

}  // namespace

// ---- Dateisystem ohne Unterscheidung der Schreibweise (exFAT/FAT), nachgebildet ---------------------------------------------------------------
// Auf der Test-Konsole (USB-Laufwerk, exFAT) genuegt rename("casetest.txt", "CaseTest.txt"): der Ordnereintrag bekommt die neue Schreibweise
// (per FTP geprueft, 13. Nov. 2026). Der PC-Test laeuft auf einem Dateisystem, das unterscheidet; diese Huelle findet Namen ohne Beachtung der
// Schreibweise und benennt wie exFAT: ein Ziel, das dieselbe Datei ist, ist erlaubt, jedes andere vorhandene Ziel nicht.
class FoldFs final : public IFileSystem {
public:
    explicit FoldFs(const IFileSystem& in) : in_(in) {}
    // Wahrer Pfad im inneren Dateisystem oder leer.
    std::string resolve(const std::string& path) const {
        std::string cur = "/";
        std::size_t i = 1;
        while (i <= path.size()) {
            const std::size_t j = path.find('/', i);
            const std::string comp = path.substr(i, j == std::string::npos ? std::string::npos : j - i);
            if (!comp.empty()) {
                std::string found;
                for (const auto& e : in_.list_dir(cur))
                    if (casename_fold(e.name) == casename_fold(comp)) found = e.name;
                if (found.empty()) return {};
                cur = (cur == "/" ? std::string("/") : cur + "/") + found;
            }
            if (j == std::string::npos) break;
            i = j + 1;
        }
        return cur;
    }
    bool exists(const std::string& p) const override { return !resolve(p).empty(); }
    bool is_dir(const std::string& p) const override { const auto r = resolve(p); return !r.empty() && in_.is_dir(r); }
    std::vector<DirEntry> list_dir(const std::string& p) const override { const auto r = resolve(p); return r.empty() ? std::vector<DirEntry>{} : in_.list_dir(r); }
    bool read_file(const std::string& p, std::string& out) const override { const auto r = resolve(p); return !r.empty() && in_.read_file(r, out); }
    bool read_range(const std::string& p, std::uint64_t off, std::size_t len, std::string& out) const override { const auto r = resolve(p); return !r.empty() && in_.read_range(r, off, len, out); }

private:
    const IFileSystem& in_;
};

class FoldRw final : public IFileSystemRW {
public:
    FoldRw(IFileSystemRW& in, const FoldFs& fold) : in_(in), fold_(fold) {}
    bool write_file(const std::string& p, const std::string& c) override { return in_.write_file(real(p), c); }
    bool rename(const std::string& from, const std::string& to) override {
        const std::string a = fold_.resolve(from);
        if (a.empty()) return false;
        const std::string b = fold_.resolve(to);
        if (!b.empty() && b != a) return false;  // ein anderes Ziel existiert schon
        const std::string dir = casename_dir(real(to));
        return in_.rename(a, (dir == "/" ? std::string("/") : dir + "/") + casename_base(to));
    }
    bool make_dirs(const std::string& p) override { return in_.make_dirs(real(p)); }
    bool remove_empty_dir(const std::string& p) override { return in_.remove_empty_dir(real(p)); }
    bool append_file(const std::string& p, const std::string& c) override { return in_.append_file(real(p), c); }

private:
    // Vorhandene Teile des Pfads in ihrer wahren Schreibweise, der Rest wie angegeben.
    std::string real(const std::string& p) const {
        std::string head = p, tail;
        while (!head.empty() && head != "/" && fold_.resolve(head).empty()) {
            tail = "/" + casename_base(head) + tail;
            head = casename_dir(head);
        }
        return (head.empty() || head == "/" ? std::string() : fold_.resolve(head)) + tail;
    }
    IFileSystemRW& in_;
    const FoldFs& fold_;
};

struct FoldPlatform final : Platform {
    explicit FoldPlatform(std::unique_ptr<Platform> inner) : in(std::move(inner)), fold(in->fs()), rw(*in->fs_rw(), fold) {}
    const IFileSystem& fs() const override { return fold; }
    const IProcessList& procs() const override { return in->procs(); }
    const ISystemInfo& sys() const override { return in->sys(); }
    IFileSystemRW* fs_rw() override { return &rw; }
    std::unique_ptr<Platform> in;
    FoldFs fold;
    FoldRw rw;
};

void case_fold_tests() {
    TempCopy tmp("gc1_neg_exfat");
    FoldPlatform fp(make_snapshot_platform(tmp.dir.string(), /*writable=*/true));
    const std::string old_path = "/mnt/usb0/homebrew/PPSA77502/Media/Modules/Il2cppUserAssemblies.prx";
    const std::string new_path = "/mnt/usb0/homebrew/PPSA77502/Media/Modules/Il2CppUserAssemblies.prx";
    CHECK(fp.fs().exists(old_path) && fp.fs().exists(new_path), "exFAT-Huelle: exists() findet beide Schreibweisen (wie auf der Konsole)");
    CHECK(!dir_has_name(fp.fs(), new_path) && dir_has_name(fp.fs(), old_path), "exFAT-Huelle: der Ordnereintrag hat nur die alte Schreibweise");
    CHECK(is_case_only_rename(old_path, new_path) && !is_case_only_rename(old_path, old_path) && !is_case_only_rename(old_path, "/mnt/usb0/homebrew/PPSA77502/Media/Modules/x.prx"),
          "is_case_only_rename: nur gleicher Ordner und gleicher Name bis auf die Schreibweise");

    Facts facts = collect_all(fp);
    Finding finding;
    bool found = false;
    for (auto& f : evaluate(rules(), facts))
        if (f.rule_id == "game.il2cpp_case_latent") {
            finding = f;
            found = true;
        }
    CHECK(found && finding.fix == "rename_case_prx", "game.il2cpp_case_latent hat jetzt den Fix rename_case_prx");
    FixPlan plan = plan_fix(finding, facts, fp.fs(), {});
    CHECK(plan.changes.size() == 1 && plan.changes[0].path == old_path && plan.changes[0].to == new_path, "exFAT: Plan benennt nur die Schreibweise um (Ziel gilt nicht als vorhanden)");

    const PathPolicy pol = policy_from_facts(facts);
    ApplyResult ar = apply_plan(plan, fp, pol, confirmed("20261113T150000Z"));
    CHECK(ar.ok, "exFAT: apply ok (" + ar.error + ")");
    CHECK(dir_has_name(fp.fs(), new_path) && !dir_has_name(fp.fs(), old_path), "exFAT: der Ordnereintrag hat danach die neue Schreibweise");
    UndoResult ur = undo_txn(fp, pol, ar.txn, "20261113T150100Z");
    CHECK(ur.ok, "exFAT: undo ok (" + ur.error + ")");
    CHECK(dir_has_name(fp.fs(), old_path) && !dir_has_name(fp.fs(), new_path), "exFAT: nach undo steht die alte Schreibweise wieder im Ordner");

    // Ein anderer Eintrag mit dem Zielnamen (auf einem unterscheidenden Dateisystem) bleibt tabu
    TempCopy tmp2("gc1_pos");
    auto p2 = make_snapshot_platform(tmp2.dir.string(), /*writable=*/true);
    Facts f2 = collect_all(*p2);
    for (auto& f : evaluate(rules(), f2))
        if (f.rule_id == "game.il2cpp_case") {
            FixPlan pl = plan_fix(f, f2, p2->fs(), {});
            CHECK(pl.changes.size() == 1, "gc1_pos: Plan unveraendert (unterscheidendes Dateisystem)");
            const std::string dst = pl.changes[0].to;
            p2->fs_rw()->write_file(dst, "andere Datei");  // das Ziel liegt schon als eigene Datei dort
            ApplyResult bad = apply_plan(pl, *p2, policy_from_facts(f2), confirmed("20261113T150200Z"));
            CHECK(!bad.ok, "Umbenennen auf ein vorhandenes, anderes Ziel wird weiter abgelehnt");
        }
}

void fix_tests() {
    export_tests();
    sha_tests();
    fw_tests();
    ini_tests();
    policy_tests();
    const FixCase cases[] = {
        {"b2_pos", "smp.parentheses_in_pfs_name", true}, {"b3_pos", "smp.autotoggle_crash_1360", true},
        {"b5_autoload", "smp.backpork_conflict", false}, {"b5_autoload_pldmgr", "smp.backpork_conflict", false},
        {"b3_pos_default", "smp.autotoggle_crash_1360", true},{"b8_pos", "smp.duplicate_titleid", true}, {"d1_pos", "smp.duplicate_titleid", true}, {"gc1_pos", "game.il2cpp_case", true},
        {"b9_pos", "smp.custom_scanpath_hides_defaults", true}, {"b10_pos", "smp.scan_depth", true},
        {"b13_pos", "smp.debug_off", true},
        {"smp2_pos_blackbox", "smp.v1_keys_in_v2", true}, {"al1_pos_pldmgr", "autoload.elfldr_without_delay", true},
        {"al1_pos_loader", "autoload.elfldr_without_delay", true},
        // 13. Nov. 2026 (Marcs Entscheidungen): neue Reparaturen
        {"al3_pos_duplicate", "autoload.entry_duplicate", true}, {"al2_pos_loader_missing", "autoload.entry_missing", true},
        {"al2_pos_missing_slot", "autoload.entry_missing", true},
        {"al5_pos_disabled", "pldmgr.autoload_disabled", true}, {"elf_pos_pldmgr", "payload.elf_invalid", true},
        {"gc1_neg_exfat", "game.il2cpp_case_latent", true}, {"i2_pos_empty", "smp.image_empty", true},
    };
    int i = 0;
    for (auto& fc : cases) roundtrip(fc, i++);
    safety_tests();
    case_fold_tests();
    rollback_test();
    plan_content_tests();
    recovery_tests();
    stack_tests();
}
