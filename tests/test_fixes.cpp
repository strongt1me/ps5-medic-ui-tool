// SPDX-License-Identifier: GPL-3.0-or-later
// Tests der Fix-Engine: SHA-256, INI-Editor, Whitelist, apply -> undo = identischer Dateibaum, Rollback, Journal.
#include "check.hpp"
#include "medic/collectors.hpp"
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
    // neue Sektion am Ende
    CHECK(ini_set_value("a=1\n", {"scan", "depth", ""}, "2") == "a=1\n\n[scan]\ndepth=2\n", "ini: neue Sektion in Datei ohne Sektionen");
    CHECK(ini_set_value("[general]\ndebug=0\n", {"scan", "depth", ""}, "2") == "[general]\ndebug=0\n\n[scan]\ndepth=2\n", "ini: neue Sektion am Ende");
    // ohne Sektionen bleibt es beim alten flachen Schluessel
    CHECK(ini_set_value("debug=1\n", {"scan", "depth", "scan_depth"}, "2") == "debug=1\nscan_depth=2\n", "ini: legacy flach bei Datei ohne Sektionen");
    // vorhandener flacher Legacy-Schluessel gewinnt
    CHECK(ini_set_value("[scan]\npath=/a\n", {"scan", "depth", "scan_depth"}, "2") == "[scan]\npath=/a\ndepth=2\n", "ini: Sektion bei Datei mit Sektionen");
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
                  c.plan.changes[0].to == "/mnt/usb0/_duplicates/PPSA01234.ffpfs",
              "duplicate: Standard behaelt die flachere/erste Quelle");
        Ctx k = make_ctx(tmp.dir, "smp.duplicate_titleid", FixOptions{"/mnt/usb0/PPSA01234.ffpfs"});
        CHECK(k.plan.changes.size() == 1 && k.plan.changes[0].path == "/data/homebrew/GameA", "duplicate: keep waehlt die andere Quelle");
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
}

// ---- Snapshot-Export -----------------------------------------------------

json comparable_facts(const Facts& f) {
    json out = json::object();
    for (const char* k : {"smp.titles", "smp.deep_sources", "smp.hidden_sources", "smp.mounts", "smp.images", "smp.scan_roots", "smp.fakelib_enabled",
                          "smp.debug", "smp.scan_depth", "proc.running", "fw.version", "kstuff.version", "kstuff.origin", "smp.version",
                          "net.port_9021", "jb.seconds_after_boot", "jb.first_process", "ps5theme.version", "storage.mounts"}) {
        const json* v = f.get(k);
        if (!v) continue;
        out[k] = *v;
        if (std::string(k) == "smp.images")
            for (auto& img : out[k]) img.erase("size");  // Export legt Images als leere Dateien an
    }
    return out;
}

// Nur live auswertbare Regeln: storage.oversized_reservation vergleicht Reservierung und Dateigroesse, der Export
// enthaelt aber nur leere Dateien und laesst /user/app weg (siehe snapshot_export.cpp).
constexpr const char* kLiveOnlyRule = "storage.oversized_reservation";

std::string findings_summary(const std::vector<Finding>& fs_) {
    std::string s;
    for (auto& f : fs_) {
        if (f.rule_id == kLiveOnlyRule) continue;
        s += f.rule_id + "|" + f.fix + "|";
        for (auto& e : f.evidence) s += e + ";";
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
        std::ofstream(tmp.dir / "fs/data/homebrew/GameA/eboot.bin") << "GEHEIM";
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

}  // namespace

void fix_tests() {
    export_tests();
    sha_tests();
    fw_tests();
    ini_tests();
    policy_tests();
    const FixCase cases[] = {
        {"b2_pos", "smp.parentheses_in_pfs_name", true}, {"b3_pos", "smp.autotoggle_crash_1360", true},
        {"b5_autoload", "smp.backpork_conflict", false}, {"b5_autoload_pldmgr", "smp.backpork_conflict", false},
        {"b3_pos_default", "smp.autotoggle_crash_1360", true},{"b8_pos", "smp.duplicate_titleid", true},
        {"b9_pos", "smp.custom_scanpath_hides_defaults", true}, {"b10_pos", "smp.scan_depth", true},
        {"b13_pos", "smp.debug_off", true},
    };
    int i = 0;
    for (auto& fc : cases) roundtrip(fc, i++);
    safety_tests();
    rollback_test();
    plan_content_tests();
}
