// SPDX-License-Identifier: GPL-3.0-or-later
// Tests der UI: Canvas, Schrift/Umbruch, Texte, PNG, Bildschirmablauf (mit Fake-Dienst) und eine Ende-zu-Ende-Reparatur
// ueber LocalService gegen eine Fixture-Kopie.
#include "check.hpp"
#include "medic/error_codes.hpp"
#include "medic/history.hpp"
#include "medic_ui/app.hpp"
#include "medic_ui/font.hpp"
#include "medic_ui/icons.hpp"
#include "medic/http.hpp"
#include "medic/sha256.hpp"
#include "medic/signature.hpp"
#include "medic_ui/pad_nav.hpp"
#include "medic_ui/png.hpp"
#include "medic_ui/shot_hook.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using namespace medic;
using namespace medic::ui;
using json = nlohmann::json;

namespace {

struct FakeService : IMedicService {
    CheckResult result;
    FixPlan plan;
    ApplyResult apply_result{true, "T1", ""};
    std::vector<TxnInfo> hist;
    UndoResult undo_result{true, ""};
    int checks = 0, plans = 0, applies = 0, undos = 0, reports = 0;
    int selftest_begins = 0, selftest_runs = 0, selftest_ends = 0;
    std::vector<TestResult> recorded;
    void selftest_record(const TestResult& r) override { recorded.push_back(r); }
    bool last_cancelled = false;
    std::vector<TestResult> last_results;
    bool selftest_begin(const std::vector<std::string>&) override {
        ++selftest_begins;
        return true;
    }
    TestResult selftest_run(const std::string& id) override {
        ++selftest_runs;
        TestResult r;
        r.id = id;
        r.status = TestStatus::Passed;
        return r;
    }
    void selftest_end(const std::vector<TestResult>& r, bool cancelled) override {
        ++selftest_ends;
        last_results = r;
        last_cancelled = cancelled;
    }
    std::string last_undo;
    ReportResult report_result{true, "/data/ps5medic/reports/report_T.md", "", "PS5 Medic 0.4.0 test\n3 Befund(e):\n[KRITISCH] a.crit\n [HINWEIS] c.info\n", false};
    ReportLang last_report_lang = ReportLang::De;

    RuleUpdateOffer offer;
    RuleUpdateResult install_result{true, "", "2026.12.01", "/data/ps5medic/knowledge/backups/rules_T.json"};
    RuleUpdateResult rollback_result{true, "", "2026.11.08", "/data/ps5medic/knowledge/backups/rules_rolledback_T.json"};
    int update_checks = 0, installs = 0, rollbacks = 0;
    std::string url;                     // Download-Adresse aus der Konfiguration
    RuleDownloadResult download_result = [] { RuleDownloadResult r; r.ok = true; return r; }();
    int downloads = 0;
    ErrorCodeSet errors;  // Fehlercode-Nachschlagewerk (leer: keine Datei geladen)
    int error_loads = 0;
    // Update der Fehlercode-Datei, Vermerk fuer den Bericht
    RuleUpdateOffer codes_offer;
    RuleUpdateResult codes_install_result{true, "", "2026.12.10", ""};
    RuleUpdateResult codes_rollback_result{true, "", "2026.11.10", ""};
    int codes_checks = 0, codes_installs = 0, codes_rollbacks = 0;
    ErrorCodeSet errors_after_codes_install;  // gilt nach einer erfolgreichen Installation (leer: bleibt)
    std::string reported;
    int reported_calls = 0;
    RuleUpdateOffer check_codes_update() override {
        ++codes_checks;
        return codes_offer;
    }
    RuleUpdateResult install_codes_update() override {
        ++codes_installs;
        if (codes_install_result.ok && !errors_after_codes_install.codes.empty()) errors = errors_after_codes_install;
        return codes_install_result;
    }
    RuleUpdateResult rollback_codes_update() override {
        ++codes_rollbacks;
        return codes_rollback_result;
    }
    void set_reported_error(const std::string& id) override {
        reported = id;
        ++reported_calls;
    }
    const ErrorCodeSet& error_codes() override {
        ++error_loads;
        return errors;
    }
    std::string rule_update_url() override { return url; }
    RuleDownloadResult download_rule_update() override {
        ++downloads;
        return download_result;
    }
    RuleUpdateOffer check_rule_update() override {
        ++update_checks;
        return offer;
    }
    RuleUpdateResult install_rule_update() override {
        ++installs;
        return install_result;
    }
    RuleUpdateResult rollback_rule_update() override {
        ++rollbacks;
        return rollback_result;
    }

    ReportResult make_report(ReportLang lang) override {
        ++reports;
        last_report_lang = lang;
        return report_result;
    }

    CheckResult run_check() override {
        ++checks;
        return result;
    }
    FixPlan plan_fix(const Finding&) override {
        ++plans;
        return plan;
    }
    ApplyResult apply_fix(const FixPlan&) override {
        ++applies;
        return apply_result;
    }
    std::string read_text(const std::string&) override { return "a=1\nb=2\n"; }
    std::vector<TxnInfo> history() override { return hist; }
    UndoResult undo(const std::string& t) override {
        ++undos;
        last_undo = t;
        return undo_result;
    }
};

Finding mk_finding(const std::string& id, Severity s, bool fix) {
    Finding f;
    f.rule_id = id;
    f.severity = s;
    f.text_de = "Deutscher Text für " + id + " mit Umlauten äöüß und einem sehr langen Satz, der mehrere Zeilen füllen soll, damit der Umbruch greift.";
    f.text_en = "English text for " + id + " with a very long sentence that should fill several lines so that wrapping applies.";
    f.evidence = {"/data/a", "/mnt/usb0/homebrew/Ein Ordner/Spiel (5).ffpfsc"};
    if (fix) f.fix = "smp_set_key";
    f.source = "https://example.org/issue/1";
    f.verified = "doc";
    return f;
}

FixPlan mk_plan() {
    FixPlan p;
    p.fix_id = "smp_set_key";
    p.rule_id = "x";
    Change c;
    c.kind = Change::Kind::WriteFile;
    c.path = "/data/shadowmount/config.ini";
    c.content = "a=1\nb=3\nc=4\n";
    c.note_de = "Setzt b auf 3.";
    c.note_en = "Sets b to 3.";
    p.changes.push_back(c);
    return p;
}

std::vector<TxnInfo> mk_history() {
    TxnInfo a;
    a.txn = "20261108T100000Z-1";
    a.ts = "20261108T100000Z";
    a.fix = "smp_set_key";
    a.rule = "smp.debug_off";
    a.state = "applied";
    a.ops = 1;
    TxnInfo b = a;
    b.txn = "20261107T090000Z-1";
    b.ts = "20261107T090000Z";
    b.state = "undone";
    return {b, a};  // chronologisch; die UI zeigt neueste zuerst
}

void press(App& a, std::initializer_list<Button> bs) {
    for (auto b : bs) a.handle(b);
}

// Prueft den Bildschirm "Pruefe..." sichtbar, fuehrt dann den Check aus.
void run_check_via_ui(App& a, Canvas& cv) {
    a.handle(Button::Cross);
    CHECK(a.screen() == App::Screen::Running, "Start: Kreuz startet den Check-up");
    a.update();
    CHECK(a.screen() == App::Screen::Running, "Check laeuft erst, nachdem 'Pruefe...' gezeichnet wurde");
    a.render(cv);
    a.update();
}

int distinct_colors(const Canvas& c) {
    std::set<Color> s;
    for (int y = 0; y < c.height(); y += 3)
        for (int x = 0; x < c.width(); x += 3) s.insert(c.at(x, y));
    return static_cast<int>(s.size());
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string no_cr_trim(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

std::set<std::string> tree(const fs::path& root) {
    std::set<std::string> t;
    for (auto& e : fs::recursive_directory_iterator(root)) {
        std::string rel = fs::relative(e.path(), root).generic_string();
        if (rel.rfind("fs/data/ps5medic", 0) == 0) continue;
        t.insert(rel);
    }
    return t;
}

struct TempCopy {
    fs::path dir;
    explicit TempCopy(const std::string& fixture) {
        static int n = 0;
        dir = fs::temp_directory_path() / ("medic-ui-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++n));
        fs::remove_all(dir);
        fs::copy(fs::path(MEDIC_SOURCE_DIR) / "tests" / "fixtures" / fixture, dir, fs::copy_options::recursive);
    }
    ~TempCopy() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void canvas_tests() {
    Canvas c(40, 30);
    c.clear(rgb(10, 20, 30));
    CHECK(c.at(0, 0) == rgb(10, 20, 30), "Canvas: clear");
    c.fill_rect(-5, -5, 10, 10, rgb(255, 0, 0));  // teilweise ausserhalb
    CHECK(c.at(0, 0) == rgb(255, 0, 0) && c.at(5, 5) == rgb(10, 20, 30), "Canvas: fill_rect wird geclippt");
    CHECK(c.at(-1, 0) == 0 && c.at(0, 99) == 0, "Canvas: at ausserhalb liefert 0");
    c.put(100, 100, rgb(1, 2, 3));  // darf nicht abstuerzen
    const Color half = blend(rgb(0, 0, 0), 0x80FFFFFFu);
    CHECK(((half >> 16) & 0xFF) >= 127 && ((half >> 16) & 0xFF) <= 129, "blend: halbes Alpha mischt zur Haelfte");
    CHECK(blend(rgb(5, 5, 5), rgb(200, 200, 200)) == rgb(200, 200, 200), "blend: deckend ersetzt");
    c.fill_round_rect(2, 2, 30, 20, 8, rgb(0, 255, 0));
    CHECK(c.at(15, 12) == rgb(0, 255, 0), "Canvas: Rundrechteck fuellt die Mitte");
    CHECK(c.at(2, 2) != rgb(0, 255, 0), "Canvas: Rundrechteck rundet die Ecke");
    Canvas g(60, 60);
    g.clear(rgb(0, 0, 0));
    for (auto gl : {PadGlyph::Cross, PadGlyph::Circle, PadGlyph::Square, PadGlyph::Triangle, PadGlyph::UpDown, PadGlyph::LeftRight, PadGlyph::Shoulders}) {
        Canvas t(60, 60);
        t.clear(rgb(0, 0, 0));
        t.draw_pad_glyph(gl, 30, 30, 44, rgb(255, 255, 255));
        CHECK(distinct_colors(t) > 2, "Canvas: Pad-Symbol zeichnet Pixel");
    }
}

void font_tests() {
    const Font& f = Font::get(Font::Id::Body);
    CHECK(f.width("Hello") > 0 && f.width("Hello") == f.width("H") + f.width("ello"), "Font: Breite ist die Summe der Zeichen");
    CHECK(f.width("中") == f.width("?"), "Font: unbekanntes Zeichen erscheint als '?'");
    CHECK(f.line_height() > f.ascent() && f.ascent() > 10, "Font: Zeilenmasse plausibel");
    for (auto id : {Font::Id::Small, Font::Id::Body, Font::Id::Title}) {
        Canvas c(300, 100);
        c.clear(rgb(0, 0, 0));
        Font::get(id).draw(c, 4, 4, "Äöü ß – „x“ …", rgb(255, 255, 255));
        CHECK(distinct_colors(c) > 4, "Font: Umlaute und Sonderzeichen werden gezeichnet");
    }
    Canvas c(300, 60);
    c.clear(rgb(0, 0, 0));
    f.draw(c, 0, 0, "Hello World Hello World", rgb(255, 255, 255), 100);
    bool right_empty = true;
    for (int y = 0; y < 60; ++y)
        for (int x = 101; x < 300; ++x)
            if (c.at(x, y) != rgb(0, 0, 0)) right_empty = false;
    CHECK(right_empty, "Font: max_width schneidet rechts ab");

    std::size_t i = 0;
    CHECK(next_codepoint("\xC3\xA4", i) == 0xE4 && i == 2, "UTF-8: ae-Umlaut");
    i = 0;
    CHECK(next_codepoint("\xE2\x80\xA6", i) == 0x2026 && i == 3, "UTF-8: Auslassungspunkte");
    i = 0;
    CHECK(next_codepoint("\xE2\x80", i) == 0xFFFD, "UTF-8: abgeschnittene Folge ist ungueltig");
    i = 0;
    CHECK(next_codepoint("\x80", i) == 0xFFFD, "UTF-8: Folgebyte allein ist ungueltig");

    // Umbruch
    const std::string text = "Dieselbe TITLE_ID liegt in mehreren Quellen vor und ShadowMount+ erwartet pro Titel nur eine Quelle";
    auto lines = wrap_text(f, text, 400);
    CHECK(lines.size() > 1, "wrap: langer Text bricht um");
    bool fits = true;
    std::string joined;
    for (auto& l : lines) {
        if (f.width(l) > 400) fits = false;
        joined += (joined.empty() ? "" : " ") + l;
    }
    CHECK(fits, "wrap: keine Zeile ist breiter als erlaubt");
    CHECK(joined == text, "wrap: der Text bleibt vollstaendig erhalten");

    const std::string path = "/mnt/usb0/homebrew/EinSehrLangerOrdnerNameOhneLeerzeichen/UndNochEinUnterordnerMitLangemNamen/spiel.ffpfsc";
    auto pl = wrap_text(Font::get(Font::Id::Small), path, 220);
    std::string pj;
    bool pfits = true;
    for (auto& l : pl) {
        pj += l;
        if (Font::get(Font::Id::Small).width(l) > 220) pfits = false;
    }
    CHECK(pl.size() > 2 && pfits && pj == path, "wrap: Pfade ohne Leerzeichen werden zeichenweise getrennt");

    auto cut = wrap_text(f, text, 300, 2);
    CHECK(cut.size() == 2 && cut.back().find("…") != std::string::npos && f.width(cut.back()) <= 300, "wrap: max_lines kuerzt mit Auslassungspunkten");
    CHECK(wrap_text(f, "", 300).size() == 1, "wrap: leerer Text ergibt eine leere Zeile");
    CHECK(wrap_text(f, "a\n\nb", 300).size() == 3, "wrap: Zeilenenden und Leerzeilen bleiben");
    const std::string e = elide(f, "/mnt/usb0/homebrew/Rennspiel (5).ffpfsc", 150);
    CHECK(f.width(e) <= 150 && e.find("…") != std::string::npos, "elide: passt in die Breite");
    CHECK(elide(f, "kurz", 150) == "kurz", "elide: kurzer Text bleibt unveraendert");
}

void i18n_tests() {
    const auto& de = Strings::get(Lang::De);
    const auto& en = Strings::get(Lang::En);
    CHECK(de.keys() == en.keys(), "i18n: Deutsch und Englisch haben dieselben Schluessel");
    std::regex token("\\{[a-z_]+\\}");
    for (auto& k : de.keys()) {
        std::set<std::string> a, b;
        const std::string sd = de(k), se = en(k);
        for (std::sregex_iterator it(sd.begin(), sd.end(), token), end; it != end; ++it) a.insert(it->str());
        for (std::sregex_iterator it(se.begin(), se.end(), token), end; it != end; ++it) b.insert(it->str());
        CHECK(a == b, "i18n: gleiche Platzhalter in " + k);
        CHECK(!sd.empty() && !se.empty(), "i18n: kein leerer Text in " + k);
    }
    CHECK(de.fmt("findings.summary", {{"n", "7"}}).find("7") != std::string::npos, "i18n: Platzhalter werden ersetzt");
    CHECK(de("gibt.es.nicht") == "!gibt.es.nicht!" && !Strings::missing().empty(), "i18n: fehlender Schluessel ist sichtbar und wird vermerkt");
    Strings::missing().clear();

    const fs::path dir = fs::path(MEDIC_SOURCE_DIR) / "ui" / "i18n";
    CHECK(no_cr_trim(slurp(dir / "de.json")) == no_cr_trim(kDeJson), "i18n: eingebettetes Deutsch entspricht de.json (tools/embed_i18n.py ausfuehren)");
    CHECK(no_cr_trim(slurp(dir / "en.json")) == no_cr_trim(kEnJson), "i18n: eingebettetes Englisch entspricht en.json (tools/embed_i18n.py ausfuehren)");
}

void png_tests() {
    Canvas c(64, 48);
    c.clear(rgb(30, 60, 90));
    const fs::path p = fs::temp_directory_path() / "medic-ui-test.png";
    CHECK(write_png(p.string(), c), "PNG: schreiben");
    const std::string s = slurp(p);
    CHECK(s.size() > 100 && s.compare(1, 3, "PNG") == 0, "PNG: Signatur");
    CHECK(static_cast<unsigned char>(s[19]) == 64 && static_cast<unsigned char>(s[23]) == 48, "PNG: Breite und Hoehe im Kopf");
    CHECK(s.find("IEND") != std::string::npos, "PNG: Endmarke");
    CHECK(write_png(p.string(), c, 2), "PNG: verkleinert schreiben");
    std::error_code ec;
    fs::remove(p, ec);
}

void screen_flow_tests() {
    FakeService svc;
    svc.result.fw = "12.00";
    svc.result.model = "CFI-7021";
    svc.result.rules_version = "2026.11.08";
    svc.result.findings = {mk_finding("a.crit", Severity::Critical, true), mk_finding("b.warn", Severity::Warning, false), mk_finding("c.info", Severity::Info, true)};
    svc.plan = mk_plan();
    svc.hist = mk_history();
    Canvas cv(App::kWidth, App::kHeight);
    Strings::missing().clear();

    App app(svc);
    CHECK(app.screen() == App::Screen::Start && svc.checks == 0, "Flow: Start, noch nichts geprueft");
    app.render(cv);
    CHECK(distinct_colors(cv) > 8, "Flow: Start-Bildschirm ist nicht leer");

    run_check_via_ui(app, cv);
    CHECK(app.screen() == App::Screen::Findings && svc.checks == 1 && app.finding_count() == 3, "Flow: Check liefert die Befunde");
    app.render(cv);

    // Bericht (Quadrat): nur in der Befundliste; ausser dort legt keine Taste einen Bericht an
    CHECK(svc.reports == 0, "Flow: bis hierher wurde kein Bericht angelegt");
    press(app, {Button::Cross, Button::Square});
    CHECK(app.screen() == App::Screen::Detail && svc.reports == 0, "Flow: Quadrat in den Details legt keinen Bericht an");
    press(app, {Button::Circle, Button::Square});
    CHECK(app.screen() == App::Screen::Report && svc.reports == 1 && svc.last_report_lang == ReportLang::De, "Flow: Quadrat in der Liste oeffnet den Bericht (Deutsch)");
    app.render(cv);
    {  // QR-Code ist gezeichnet: genau schwarze und weisse Flaechen rechts
        bool white = false, black = false;
        for (int y = 150; y < 950; y += 3)
            for (int x = 1036; x < 1824; x += 3) {
                white = white || cv.at(x, y) == rgb(255, 255, 255);
                black = black || cv.at(x, y) == rgb(0, 0, 0);
            }
        CHECK(white && black, "Flow: der Bericht zeigt einen QR-Code (schwarz auf weiss)");
    }
    for (auto b : {Button::Cross, Button::Triangle, Button::Square, Button::L1, Button::R1, Button::Options, Button::Left, Button::Right, Button::Up, Button::Down})
        app.handle(b);
    CHECK(app.screen() == App::Screen::Report && svc.reports == 1 && svc.applies == 0 && svc.undos == 0, "Flow: im Bericht tut nur Kreis etwas");
    press(app, {Button::Circle});
    CHECK(app.screen() == App::Screen::Findings, "Flow: Kreis geht aus dem Bericht zur Liste");
    svc.report_result.saved = false;
    svc.report_result.path.clear();
    svc.report_result.error = "no_write";
    svc.report_result.truncated = true;
    press(app, {Button::Square});
    CHECK(app.screen() == App::Screen::Report && svc.reports == 2, "Flow: Bericht ohne Schreibzugriff laesst sich anzeigen");
    app.render(cv);
    svc.report_result.error = "write_failed";
    press(app, {Button::Circle, Button::Square});
    app.render(cv);
    svc.report_result.qr_text = std::string(5000, 'x');  // passt in keinen QR-Code
    press(app, {Button::Circle, Button::Square});
    CHECK(app.screen() == App::Screen::Report, "Flow: zu langer QR-Text bricht die Anzeige nicht");
    app.render(cv);
    press(app, {Button::Circle});

    // Auswahl
    press(app, {Button::Up});
    CHECK(app.selected() == 0, "Flow: Auswahl bleibt oben");
    press(app, {Button::Down, Button::Down, Button::Down, Button::Down});
    CHECK(app.selected() == 2, "Flow: Auswahl bleibt unten");
    press(app, {Button::Up, Button::Up});
    CHECK(app.selected() == 0, "Flow: Auswahl geht hoch");

    // Detail und zurueck
    press(app, {Button::Cross});
    CHECK(app.screen() == App::Screen::Detail, "Flow: Kreuz oeffnet Details");
    app.render(cv);
    press(app, {Button::Circle});
    CHECK(app.screen() == App::Screen::Findings, "Flow: Kreis geht zur Liste zurueck");

    // Reparatur ohne Fix: nichts passiert
    press(app, {Button::Down, Button::Triangle});
    CHECK(app.screen() == App::Screen::Findings && svc.plans == 0, "Flow: Dreieck ohne Fix tut nichts");

    // Reparatur abbrechen: nie geschrieben
    press(app, {Button::Up, Button::Triangle});
    CHECK(app.screen() == App::Screen::FixPreview && svc.plans == 1 && svc.applies == 0, "Flow: Dreieck zeigt die Vorschau, schreibt aber nicht");
    app.render(cv);
    press(app, {Button::Circle});
    CHECK(app.screen() == App::Screen::Findings && svc.applies == 0, "Flow: Abbrechen schreibt nichts");
    press(app, {Button::Cross, Button::Triangle});
    CHECK(app.screen() == App::Screen::FixPreview && svc.applies == 0, "Flow: Vorschau auch aus den Details");
    press(app, {Button::Circle});
    CHECK(app.screen() == App::Screen::Detail, "Flow: Abbrechen geht zu den Details zurueck");
    press(app, {Button::Circle});

    // Reparatur bestaetigen
    press(app, {Button::Triangle});
    for (auto b : {Button::Square, Button::Triangle, Button::L1, Button::R1, Button::Options, Button::Left, Button::Right, Button::Up, Button::Down})
        app.handle(b);
    CHECK(svc.applies == 0 && app.screen() == App::Screen::FixPreview, "Flow: nur das Kreuz bestaetigt");
    press(app, {Button::Cross});
    CHECK(svc.applies == 1 && app.screen() == App::Screen::FixDone, "Flow: Kreuz fuehrt die Reparatur aus");
    app.render(cv);
    press(app, {Button::Cross});  // erneut pruefen
    CHECK(app.screen() == App::Screen::Running, "Flow: nach der Reparatur erneut pruefen");
    app.render(cv);
    app.update();
    CHECK(svc.checks == 2 && app.screen() == App::Screen::Findings, "Flow: zweiter Check");

    // Fehlschlag
    svc.apply_result = {false, "", "Schreiben verweigert"};
    press(app, {Button::Triangle, Button::Cross});
    CHECK(app.screen() == App::Screen::FixDone && svc.applies == 2, "Flow: fehlgeschlagene Reparatur wird gemeldet");
    app.render(cv);
    press(app, {Button::Circle});
    CHECK(app.screen() == App::Screen::Start, "Flow: Kreis zurueck zum Start");

    // Leerer Plan: Kreuz bestaetigt nichts
    svc.plan = FixPlan{};
    svc.plan.fix_id = "fix_image_nesting_hint";
    svc.plan.hint_de = "Bitte den Ordner umbauen.";
    svc.plan.hint_en = "Please rearrange the folder.";
    press(app, {Button::Up});
    run_check_via_ui(app, cv);
    press(app, {Button::Triangle});
    app.render(cv);
    const int before = svc.applies;
    press(app, {Button::Cross});
    CHECK(svc.applies == before && app.screen() == App::Screen::FixPreview, "Flow: leerer Plan kann nicht bestaetigt werden");
    press(app, {Button::Circle, Button::Circle});
    svc.plan = mk_plan();

    // Verlauf und Rueckgaengig
    press(app, {Button::Down, Button::Cross, Button::Square});
    CHECK(app.screen() == App::Screen::History, "Flow: Verlauf oeffnen");
    app.render(cv);
    press(app, {Button::Down, Button::Cross});  // 'undone' ist nicht rueckgaengig machbar
    CHECK(app.screen() == App::Screen::History && svc.undos == 0, "Flow: bereits Rueckgaengiges laesst sich nicht erneut zuruecknehmen");
    press(app, {Button::Up, Button::Cross});
    CHECK(app.screen() == App::Screen::UndoConfirm && svc.undos == 0, "Flow: Rueckgaengig fragt erst nach");
    app.render(cv);
    press(app, {Button::Circle});
    CHECK(app.screen() == App::Screen::History && svc.undos == 0, "Flow: Abbrechen nimmt nichts zurueck");
    press(app, {Button::Cross, Button::Cross});
    CHECK(svc.undos == 1 && svc.last_undo == "20261108T100000Z-1" && app.screen() == App::Screen::FixDone, "Flow: Bestaetigen nimmt die richtige Transaktion zurueck");
    app.render(cv);
    press(app, {Button::Cross});
    CHECK(app.screen() == App::Screen::History, "Flow: nach dem Zuruecknehmen zurueck zum Verlauf");
    press(app, {Button::Circle});

    // Einstellungen und Sprache
    Canvas de(App::kWidth, App::kHeight), en(App::kWidth, App::kHeight);
    press(app, {Button::Circle, Button::Down, Button::Cross});  // aus dem Verlauf zurueck (Kreis: Verlauf, Check-ups, Start)
    CHECK(app.screen() == App::Screen::Settings && app.lang() == Lang::De, "Flow: Einstellungen, Deutsch");
    app.render(de);
    press(app, {Button::Right});
    CHECK(app.lang() == Lang::En, "Flow: Rechts wechselt die Sprache");
    app.render(en);
    bool differ = false;
    for (int y = 0; y < App::kHeight && !differ; y += 2)
        for (int x = 0; x < App::kWidth; x += 2)
            if (de.at(x, y) != en.at(x, y)) {
                differ = true;
                break;
            }
    CHECK(differ, "Flow: Sprachwechsel aendert die Darstellung");
    press(app, {Button::Circle});
    CHECK(app.screen() == App::Screen::Start, "Flow: Einstellungen schliessen");
    CHECK(!app.quit_requested(), "Flow: Zurueck aus Einstellungen beendet die App nicht");
    press(app, {Button::Circle});
    CHECK(app.quit_requested() && app.screen() == App::Screen::Start, "Flow: Kreis auf dem Start beendet die App");

    // Alle Bildschirme in beiden Sprachen ohne fehlende Texte
    for (Lang l : {Lang::De, Lang::En}) {
        App a(svc, l);
        a.render(cv);
        a.handle(Button::Cross);
        a.render(cv);
        a.update();
        a.render(cv);
        a.handle(Button::Cross);
        a.render(cv);
        a.handle(Button::Triangle);
        a.render(cv);
        a.handle(Button::Cross);
        a.render(cv);
        a.handle(Button::Cross);  // erneut pruefen
        a.render(cv);
        a.update();
        a.handle(Button::Circle);
        a.handle(Button::Down);
        a.handle(Button::Cross);
        a.render(cv);
        a.handle(Button::Cross);
        a.render(cv);
        a.handle(Button::Circle);
        a.handle(Button::Circle);
        a.handle(Button::Down);
        a.handle(Button::Cross);
        a.render(cv);
    }
    CHECK(Strings::missing().empty(), "Flow: alle gezeichneten Texte existieren in beiden Sprachen");
}

void edge_tests() {
    FakeService svc;
    Canvas cv(App::kWidth, App::kHeight);

    // Keine Befunde
    App a(svc);
    run_check_via_ui(a, cv);
    CHECK(a.screen() == App::Screen::Findings && a.finding_count() == 0, "Edge: leere Befundliste");
    a.render(cv);
    press(a, {Button::Down, Button::Up, Button::Triangle});
    CHECK(a.screen() == App::Screen::Findings, "Edge: ohne Befunde passiert bei Navigation nichts");
    press(a, {Button::Cross});
    CHECK(a.screen() == App::Screen::Running, "Edge: Kreuz prueft erneut");
    a.render(cv);
    a.update();
    press(a, {Button::Circle, Button::Down, Button::Cross, Button::Square});
    CHECK(a.screen() == App::Screen::History, "Edge: Verlauf ohne Eintraege");
    a.render(cv);
    press(a, {Button::Cross, Button::Down, Button::Up});
    CHECK(a.screen() == App::Screen::History, "Edge: Kreuz im leeren Verlauf tut nichts");

    // Viele und sehr lange Eintraege
    FakeService big;
    for (int i = 0; i < 25; ++i) big.result.findings.push_back(mk_finding("rule." + std::to_string(i), i % 3 == 0 ? Severity::Critical : (i % 3 == 1 ? Severity::Warning : Severity::Info), i % 2 == 0));
    big.result.findings[0].evidence.assign(40, std::string(300, 'x'));
    big.result.findings[0].text_de = std::string(5000, 'y');
    big.plan = mk_plan();
    for (int i = 0; i < 9; ++i) {
        Change c;
        c.kind = Change::Kind::Rename;
        c.path = "/mnt/usb0/homebrew/" + std::string(200, 'p') + std::to_string(i);
        c.to = c.path + "_to_delete";
        c.note_de = c.note_en = std::string(400, 'n');
        big.plan.changes.push_back(c);
    }
    big.plan.warnings = {std::string(500, 'w'), "zweite Warnung"};
    for (int i = 0; i < 30; ++i) big.hist.push_back(mk_history()[1]);
    App b(big);
    run_check_via_ui(b, cv);
    for (int i = 0; i < 30; ++i) b.handle(Button::Down);
    CHECK(b.selected() == 24, "Edge: Auswahl laeuft bis zum letzten Befund");
    b.render(cv);
    for (int i = 0; i < 30; ++i) b.handle(Button::Up);
    b.render(cv);
    press(b, {Button::Cross});
    b.render(cv);
    press(b, {Button::Triangle});
    CHECK(b.screen() == App::Screen::FixPreview, "Edge: Vorschau mit vielen langen Aenderungen");
    b.render(cv);
    press(b, {Button::Circle, Button::Circle, Button::Circle, Button::Down, Button::Cross, Button::Square});
    for (int i = 0; i < 40; ++i) b.handle(Button::Down);
    b.render(cv);
    CHECK(b.screen() == App::Screen::History, "Edge: langer Verlauf");
}

// Fund vom 8. Nov. auf der Konsole: App neu gestartet, ohne Check-up direkt in den Verlauf, Rueckgaengig scheiterte mit
// "Pfad nicht in der Whitelist", weil die Whitelist aus den Fakten des (nie gelaufenen) Check-ups kam.
void undo_in_new_session_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    TempCopy t("b8_pos");
    const auto before = tree(t.dir);
    auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
    Canvas cv(App::kWidth, App::kHeight);
    {  // erste Sitzung: Check-up und Reparatur
        LocalService s1(*platform, rules, [] { return std::string("20261108T120000Z"); });
        App app(s1);
        run_check_via_ui(app, cv);
        press(app, {Button::Triangle, Button::Cross});
        CHECK(app.screen() == App::Screen::FixDone && tree(t.dir) != before, "Neue Sitzung: Vorbereitung, die Reparatur ist angewendet");
    }
    // zweite Sitzung: neuer Dienst, kein Check-up
    LocalService s2(*platform, rules, [] { return std::string("20261108T130000Z"); });
    App app2(s2);
    press(app2, {Button::Down, Button::Cross, Button::Square});
    CHECK(app2.screen() == App::Screen::History, "Neue Sitzung: Verlauf ohne Check-up");
    app2.render(cv);
    press(app2, {Button::Cross});
    CHECK(app2.screen() == App::Screen::UndoConfirm, "Neue Sitzung: Rueckgaengig fragt nach");
    press(app2, {Button::Cross});
    CHECK(app2.screen() == App::Screen::FixDone, "Neue Sitzung: Ergebnis");
    app2.render(cv);
    CHECK(tree(t.dir) == before, "Neue Sitzung: Rueckgaengig ohne vorherigen Check-up stellt den Dateibaum wieder her");
}

void end_to_end_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    TempCopy t("b8_pos");
    const auto before = tree(t.dir);
    auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
    LocalService svc(*platform, rules, [] { return std::string("20261108T120000Z"); });
    App app(svc);
    Canvas cv(App::kWidth, App::kHeight);

    run_check_via_ui(app, cv);
    CHECK(app.finding_count() >= 1, "E2E: der Check findet die doppelte TITLE_ID");
    press(app, {Button::Triangle});
    CHECK(app.screen() == App::Screen::FixPreview, "E2E: Vorschau");
    app.render(cv);
    CHECK(tree(t.dir) == before, "E2E: die Vorschau veraendert nichts");
    press(app, {Button::Cross});
    CHECK(app.screen() == App::Screen::FixDone, "E2E: Reparatur ausgefuehrt");
    app.render(cv);
    CHECK(tree(t.dir) != before, "E2E: die Reparatur hat Dateien verschoben");

    press(app, {Button::Circle, Button::Down, Button::Cross, Button::Square});
    CHECK(app.screen() == App::Screen::History, "E2E: Verlauf");
    app.render(cv);
    press(app, {Button::Cross});
    CHECK(app.screen() == App::Screen::UndoConfirm, "E2E: Rueckgaengig fragt nach");
    app.render(cv);
    press(app, {Button::Cross});
    CHECK(app.screen() == App::Screen::FixDone, "E2E: Rueckgaengig ausgefuehrt");
    CHECK(tree(t.dir) == before, "E2E: nach dem Rueckgaengigmachen ist der Dateibaum wieder wie vorher");
}

std::set<std::string> full_tree(const fs::path& root) {  // anders als tree(): auch /data/ps5medic (ohne den Verlauf der Check-ups: jede Pruefung haengt an history.jsonl an)
    std::set<std::string> t;
    for (auto& e : fs::recursive_directory_iterator(root)) {
        std::string rel = fs::relative(e.path(), root).generic_string();
        if (rel == "fs/data/ps5medic/history.jsonl" || rel == "fs/data/ps5medic" || rel.rfind("fs/data/ps5medic/checks", 0) == 0) continue;  // der Ordner entsteht mit dem Verlauf; seine Dateien zaehlen weiter
        t.insert(std::move(rel));
    }
    return t;
}

// ---- Regel-Update ------------------------------------------------------------

void rules_update_flow_tests() {
    Strings::missing().clear();
    Canvas cv(App::kWidth, App::kHeight);
    using State = RuleUpdateOffer::State;
    const std::initializer_list<Button> others = {Button::Triangle, Button::L1, Button::R1, Button::Options, Button::Left, Button::Right, Button::Up, Button::Down};

    for (Lang l : {Lang::De, Lang::En}) {
        FakeService svc;
        svc.result.fw = "12.00";
        svc.result.rules_version = "2026.11.08";
        svc.result.findings = {mk_finding("a.crit", Severity::Critical, true)};
        svc.offer.old_version = "2026.11.08";
        svc.offer.old_rule_count = 22;
        App app(svc, l);
        run_check_via_ui(app, cv);
        press(app, {Button::Circle, Button::Down, Button::Down, Button::Cross});  // Start: Einstellungen ist der dritte Eintrag
        CHECK(app.screen() == App::Screen::Settings, "Update-Flow: Einstellungen");
        app.render(cv);
        press(app, {Button::Down});
        app.render(cv);
        press(app, {Button::Cross});
        CHECK(app.screen() == App::Screen::RulesUpdate && svc.update_checks == 1 && app.lang() == l, "Update-Flow: Kreuz auf Regel-Update oeffnet den Bildschirm (Sprache bleibt)");

        // keine Datei: nichts kann ausgeloest werden
        app.render(cv);
        for (auto b : others) app.handle(b);
        app.handle(Button::Cross);
        CHECK(app.screen() == App::Screen::RulesUpdate && svc.installs == 0 && svc.rollbacks == 0, "Update-Flow: ohne Datei tut keine Taste etwas ausser Kreis");
        press(app, {Button::Circle});
        CHECK(app.screen() == App::Screen::Settings, "Update-Flow: Kreis geht zu den Einstellungen");

        // abgelehnt: jeder Grund wird dargestellt
        svc.offer.state = State::Rejected;
        for (const char* reason : {"no_rules_file", "no_checksum", "checksum_mismatch", "invalid_rules", "not_newer", "too_large", "unreadable"}) {
            svc.offer.reason = reason;
            svc.offer.detail = std::string(reason) == "invalid_rules" ? "rule 'x': 'severity' fehlt" : "";
            press(app, {Button::Cross});
            app.render(cv);
            press(app, {Button::Cross});  // Kreuz auf einem abgelehnten Angebot installiert nichts
            CHECK(app.screen() == App::Screen::RulesUpdate && svc.installs == 0, std::string("Update-Flow: abgelehnt (") + reason + ") laesst sich nicht installieren");
            press(app, {Button::Circle});
        }

        // bereit: nur Kreuz installiert
        svc.offer.state = State::Ready;
        svc.offer.reason.clear();
        svc.offer.detail.clear();
        svc.offer.new_version = "2026.12.01";
        svc.offer.new_rule_count = 24;
        svc.offer.sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        press(app, {Button::Cross});
        app.render(cv);
        for (auto b : others) app.handle(b);
        CHECK(app.screen() == App::Screen::RulesUpdate && svc.installs == 0, "Update-Flow: bereit, aber nur das Kreuz installiert");
        press(app, {Button::Cross});
        CHECK(svc.installs == 1 && app.finding_count() == 0, "Update-Flow: Kreuz installiert, die alten Befunde gelten nicht mehr");
        app.render(cv);
        press(app, {Button::Cross});
        CHECK(app.screen() == App::Screen::Settings, "Update-Flow: nach der Installation zurueck zu den Einstellungen");
        press(app, {Button::Circle});
        app.render(cv);  // Start: kein Ergebnis mehr

        // Installation scheitert: Meldung, Befunde bleiben
        FakeService fail;
        fail.result = svc.result;
        fail.offer = svc.offer;
        fail.install_result = {false, "write_failed", "", ""};
        App app2(fail, l);
        run_check_via_ui(app2, cv);
        press(app2, {Button::Circle, Button::Down, Button::Down, Button::Cross, Button::Down, Button::Cross, Button::Cross});
        CHECK(fail.installs == 1 && app2.screen() == App::Screen::RulesUpdate, "Update-Flow: Fehlschlag wird angezeigt");
        app2.render(cv);
        press(app2, {Button::Cross});
        CHECK(app2.screen() == App::Screen::Settings, "Update-Flow: nach dem Fehlschlag zurueck");
        fail.install_result.error = "voellig_unbekannt";  // unbekannte Kennung: allgemeiner Text
        press(app2, {Button::Cross, Button::Cross});
        app2.render(cv);

        // Rollback: erst Rueckfrage, dann nur Kreuz
        svc.offer = RuleUpdateOffer{};
        svc.offer.old_version = "2026.12.01";
        svc.offer.can_rollback = true;
        svc.offer.installed_version = "2026.12.01";
        press(app, {Button::Cross, Button::Cross});  // Start steht auf Einstellungen, dort auf Regel-Update
        app.render(cv);
        press(app, {Button::Triangle});
        CHECK(svc.rollbacks == 0, "Update-Flow: Dreieck fragt nur nach, setzt nichts zurueck");
        app.render(cv);
        press(app, {Button::Circle});
        press(app, {Button::Triangle});
        for (auto b : {Button::Square, Button::Triangle, Button::L1, Button::R1, Button::Options, Button::Left, Button::Right, Button::Up, Button::Down}) app.handle(b);
        CHECK(svc.rollbacks == 0, "Update-Flow: in der Rueckfrage setzt nur das Kreuz zurueck");
        press(app, {Button::Cross});
        CHECK(svc.rollbacks == 1, "Update-Flow: Kreuz in der Rueckfrage setzt zurueck");
        app.render(cv);
        press(app, {Button::Cross});

        svc.rollback_result = {false, "nothing_to_roll_back", "", ""};
        press(app, {Button::Cross, Button::Triangle, Button::Cross});
        app.render(cv);
        CHECK(app.screen() == App::Screen::RulesUpdate, "Update-Flow: Rollback-Fehlschlag wird angezeigt");
    }
    CHECK(Strings::missing().empty(), "Update-Flow: alle gezeichneten Texte existieren in beiden Sprachen");
}

void rules_update_service_tests() {
    const std::string kPath = std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json";
    const RuleSet rules = load_rules_from_file(kPath);
    json nj = json::parse(slurp(kPath));
    nj["db_version"] = "2026.12.01";
    const std::string new_text = nj.dump(2) + "\n";
    auto spit = [](const fs::path& p, const std::string& t) {
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary | std::ios::trunc) << t;
    };
    auto clock = [] { return std::string("20261109T100000Z"); };
    auto put_update = [&](const fs::path& dir) {
        spit(dir / "fs/data/ps5medic/knowledge/update/rules.json", new_text);
        spit(dir / "fs/data/ps5medic/knowledge/update/rules.json.sha256", sha256_hex(new_text) + "  rules.json\n");
    };

    {  // ohne Datei im Wissensordner (es galten die mitgelieferten Regeln): Installation und Rollback ueber die UI
        TempCopy t("b2_pos");
        put_update(t.dir);
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, clock);
        const auto offer = svc.check_rule_update();
        CHECK(offer.state == RuleUpdateOffer::State::Ready && offer.new_version == "2026.12.01" && offer.old_version == rules.db_version && offer.old_rule_count == rules.rules.size(),
              "Update-E2E: Angebot kennt alte und neue Regeln");
        CHECK(svc.run_check().rules_version == rules.db_version, "Update-E2E: vorher gelten die alten Regeln");

        App app(svc);
        Canvas cv(App::kWidth, App::kHeight);
        press(app, {Button::Down, Button::Down, Button::Cross, Button::Down, Button::Cross});
        CHECK(app.screen() == App::Screen::RulesUpdate, "Update-E2E: Einstellungen -> Regel-Update");
        app.render(cv);
        press(app, {Button::Cross});
        app.render(cv);
        CHECK(slurp(t.dir / "fs/data/ps5medic/knowledge/rules.json") == new_text, "Update-E2E: Kreuz installiert die neue Datei");
        CHECK(svc.run_check().rules_version == "2026.12.01", "Update-E2E: die neuen Regeln gelten sofort");

        press(app, {Button::Cross, Button::Down, Button::Cross});  // zurueck, wieder oeffnen
        CHECK(svc.check_rule_update().can_rollback, "Update-E2E: Rollback ist moeglich");
        press(app, {Button::Triangle, Button::Cross});
        CHECK(!fs::exists(t.dir / "fs/data/ps5medic/knowledge/rules.json"), "Update-E2E: Rollback legt die Datei beiseite");
        CHECK(svc.run_check().rules_version == rules.db_version, "Update-E2E: nach dem Rollback gelten wieder die alten Regeln");
    }
    {  // mit vorhandener Datei im Wissensordner
        TempCopy t("b2_pos");
        spit(t.dir / "fs/data/ps5medic/knowledge/rules.json", slurp(kPath));
        put_update(t.dir);
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, clock);
        CHECK(svc.install_rule_update().ok && svc.run_check().rules_version == "2026.12.01", "Update-E2E: Installation mit vorhandener Datei");
        const auto rb = svc.rollback_rule_update();
        CHECK(rb.ok && slurp(t.dir / "fs/data/ps5medic/knowledge/rules.json") == slurp(kPath) && svc.run_check().rules_version == rules.db_version,
              "Update-E2E: Rollback stellt die vorherige Datei wieder her, die Regeln gelten sofort");
    }
    {  // abgelehnt: die Regeln bleiben, nichts wird geschrieben
        TempCopy t("b2_pos");
        put_update(t.dir);
        spit(t.dir / "fs/data/ps5medic/knowledge/update/rules.json.sha256", std::string(64, '0') + "\n");
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, clock);
        const auto r = svc.install_rule_update();
        CHECK(!r.ok && r.error == "checksum_mismatch" && svc.run_check().rules_version == rules.db_version && !fs::exists(t.dir / "fs/data/ps5medic/knowledge/rules.json"),
              "Update-E2E: falsche Pruefsumme -> keine Installation, alte Regeln bleiben");
    }
}


// Regel-Download in der Oberflaeche: Rueckfrage, Anzeige vor dem Abruf, Ergebnis.
void rules_download_flow_tests() {
    Strings::missing().clear();
    Canvas cv(App::kWidth, App::kHeight);
    using State = RuleUpdateOffer::State;

    for (Lang l : {Lang::De, Lang::En}) {
        FakeService svc;
        svc.offer.old_version = "2026.11.08";
        svc.offer.old_rule_count = 22;
        App app(svc, l);
        press(app, {Button::Down, Button::Down, Button::Cross, Button::Down, Button::Cross});  // Start -> Einstellungen -> Regel-Update
        CHECK(app.screen() == App::Screen::RulesUpdate && svc.downloads == 0, "Download-Flow: Regel-Update offen, noch nichts geholt");
        app.render(cv);

        // Ohne eingetragene Adresse: Anleitung, Kreuz tut nichts, Kreis geht zurueck
        press(app, {Button::Square});
        app.render(cv);
        app.update();
        press(app, {Button::Cross});
        app.render(cv);
        app.update();
        CHECK(svc.downloads == 0 && svc.update_checks == 1 && app.screen() == App::Screen::RulesUpdate, "Download-Flow: ohne Adresse wird nichts geholt");
        press(app, {Button::Circle});
        CHECK(app.screen() == App::Screen::RulesUpdate, "Download-Flow: Kreis geht aus der Anleitung zum Angebot zurueck");

        // Mit Adresse: Rueckfrage; Kreis bricht ab, ohne Netz zu benutzen
        svc.url = "http://192.168.1.50:8000/ps5medic/";
        press(app, {Button::Square});
        app.render(cv);
        press(app, {Button::Circle});
        app.update();
        CHECK(svc.downloads == 0, "Download-Flow: Abbrechen in der Rueckfrage holt nichts");

        // Bestaetigen: erst "Lade ..." zeichnen, dann abrufen (wie beim Check-up)
        press(app, {Button::Square, Button::Cross});
        app.update();
        CHECK(svc.downloads == 0, "Download-Flow: der Abruf beginnt erst, nachdem 'Lade ...' gezeichnet wurde");
        press(app, {Button::Cross, Button::Triangle, Button::Square, Button::Circle});  // waehrenddessen ohne Wirkung
        app.render(cv);
        CHECK(svc.downloads == 0, "Download-Flow: Zeichnen allein ruft nichts ab");
        svc.offer.state = State::Ready;
        svc.offer.new_version = "2026.12.01";
        svc.offer.new_rule_count = 23;
        svc.offer.auth = "signature";
        svc.offer.signed_by = "04de66c8c642d706";
        svc.offer.sha256 = std::string(64, 'a');
        app.update();
        CHECK(svc.downloads == 1 && svc.update_checks == 2 && svc.installs == 0 && app.screen() == App::Screen::RulesUpdate, "Download-Flow: ein Abruf, danach wird geprueft, installiert wird nichts");
        app.render(cv);
        press(app, {Button::Cross});
        CHECK(svc.installs == 1, "Download-Flow: installiert wird erst mit dem Kreuz auf dem Angebot");
        press(app, {Button::Cross});  // Fertig-Bildschirm -> Einstellungen

        // Fehlschlag: Meldung, nichts angeboten, Kreuz kehrt zurueck
        svc.download_result = {};
        svc.download_result.error = "http_status";
        svc.download_result.http_status = 404;
        svc.download_result.file = "rules.json";
        svc.offer = RuleUpdateOffer{};
        press(app, {Button::Down, Button::Cross});  // Einstellungen: Regel-Update
        CHECK(app.screen() == App::Screen::RulesUpdate, "Download-Flow: wieder im Regel-Update");
        press(app, {Button::Square, Button::Cross});
        app.render(cv);
        app.update();
        const int checks_before = svc.update_checks;
        CHECK(svc.downloads == 2, "Download-Flow: zweiter Abruf");
        app.render(cv);
        press(app, {Button::Cross});
        app.render(cv);
        CHECK(svc.update_checks == checks_before && svc.installs == 1 && app.screen() == App::Screen::RulesUpdate, "Download-Flow: nach einem Fehlschlag wird nichts geprueft oder installiert");

        // Jede Fehlerkennung hat einen Text
        for (const char* code : {"no_url", "bad_url", "https_unsupported", "no_write", "write_failed", "bad_timestamp", "no_network", "http_status", "net_resolve", "net_connect", "net_timeout",
                                 "net_network", "net_redirect", "net_too_large", "net_bad_response"}) {
            svc.download_result.error = code;
            press(app, {Button::Square, Button::Cross});
            app.render(cv);
            app.update();
            app.render(cv);
            press(app, {Button::Cross});
        }
        // Angebote mit Signaturproblemen: Text je Grund
        svc.offer.state = State::Rejected;
        for (const char* reason : {"sig_malformed", "sig_unknown_key", "sig_invalid"}) {
            svc.offer.reason = reason;
            svc.download_result = {};
            svc.download_result.ok = true;
            press(app, {Button::Square, Button::Cross});
            app.render(cv);
            app.update();
            app.render(cv);
            press(app, {Button::Cross});
            CHECK(svc.installs == 1, std::string("Download-Flow: abgelehnt (") + reason + ") laesst sich nicht installieren");
        }
    }
    CHECK(Strings::missing().empty(), "Download-Flow: kein Text fehlt (" + (Strings::missing().empty() ? std::string() : *Strings::missing().begin()) + ")");
    for (const char* key : {"update.error.no_url", "update.error.bad_url", "update.error.https_unsupported", "update.error.no_network", "update.error.net_connect", "update.error.net_timeout",
                            "update.error.net_resolve", "update.error.net_network", "update.error.net_redirect", "update.error.net_too_large", "update.error.net_bad_response",
                            "update.error.http_status", "update.rejected.sig_malformed", "update.rejected.sig_unknown_key", "update.rejected.sig_invalid", "update.auth_sig",
                            "update.confirm_download", "update.no_url", "update.downloading", "update.download_failed", "update.downloaded_from", "update.download"})
        CHECK(Strings::get(Lang::De).has(key) && Strings::get(Lang::En).has(key), std::string("Download-Flow: Text in beiden Sprachen: ") + key);
}

// Ersatz fuer das Netz: liefert vorbereitete Antworten.
struct ScriptedHttp : medic::IHttpClient {
    std::map<std::string, std::string> files;  // Adresse -> Inhalt; fehlt sie, kommt 404
    std::vector<std::string> asked;
    medic::HttpResponse get(const std::string& url, std::size_t, int) override {
        asked.push_back(url);
        medic::HttpResponse r;
        auto it = files.find(url);
        r.status = it == files.end() ? 404 : 200;
        if (it != files.end()) r.body = it->second;
        return r;
    }
};

// Ganz durch: config.ini -> Quadrat -> Kreuz -> Pruefung der Signatur -> Kreuz installiert -> neue Regeln gelten.
void rules_download_service_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    json nj = json::parse(slurp(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json"));
    nj["db_version"] = "2026.12.09";
    const std::string new_text = nj.dump(2) + "\n";
    medic::Ed25519Seed seed;
    seed.fill(0x33);
    const medic::Ed25519KeyPair key = medic::ed25519_keypair_from_seed(seed);
    const std::vector<medic::TrustedKey> trusted = {medic::make_trusted_key(key.pk)};
    const std::string base = "http://192.0.2.10:8000/regeln/";
    auto spit = [](const fs::path& p, const std::string& t) {
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary | std::ios::trunc) << t;
    };
    auto clock = [] { return std::string("20261109T140000Z"); };
    auto go_to_update = [](App& app) { press(app, {Button::Down, Button::Down, Button::Cross, Button::Down, Button::Cross}); };

    {  // gueltig signiert: angeboten und installiert
        TempCopy t("b2_pos");
        spit(t.dir / "fs/data/ps5medic/config.ini", "update_url=" + base + "\n");
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, clock);
        ScriptedHttp http;
        http.files[base + "rules.json"] = new_text;
        http.files[base + "rules.json.sig"] = medic::make_signature_file(key, new_text);
        svc.set_http_client(&http);
        svc.set_trusted_keys(trusted);
        App app(svc);
        Canvas cv(App::kWidth, App::kHeight);
        go_to_update(app);
        press(app, {Button::Square});
        app.render(cv);
        CHECK(http.asked.empty(), "Download-E2E: die Rueckfrage allein benutzt das Netz nicht");
        press(app, {Button::Cross});
        app.render(cv);
        app.update();
        CHECK(http.asked.size() == 3 && http.asked[0] == base + "rules.json" && http.asked[1] == base + "rules.json.sig" && http.asked[2] == base + "error_codes.json",
              "Download-E2E: rules.json, rules.json.sig und die Anfrage nach error_codes.json (404), nicht mehr");
        CHECK(!fs::exists(t.dir / "fs/data/ps5medic/knowledge/rules.json"), "Download-E2E: bis zur Bestaetigung bleiben die aktiven Regeln unberuehrt");
        app.render(cv);
        const auto offer = svc.check_rule_update();
        CHECK(offer.state == RuleUpdateOffer::State::Ready && offer.auth == "signature" && offer.signed_by == medic::key_id_of(key.pk), "Download-E2E: Angebot ist signiert");
        press(app, {Button::Cross});
        app.render(cv);
        CHECK(slurp(t.dir / "fs/data/ps5medic/knowledge/rules.json") == new_text && svc.run_check().rules_version == "2026.12.09", "Download-E2E: Kreuz installiert, die neuen Regeln gelten sofort");
    }
    {  // untergeschobene Datei: Signatur passt nicht -> abgelehnt, nichts installiert
        TempCopy t("b2_pos");
        spit(t.dir / "fs/data/ps5medic/config.ini", "update_url=" + base + "\n");
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, clock);
        ScriptedHttp http;
        const std::string evil = new_text + " ";
        http.files[base + "rules.json"] = evil;
        http.files[base + "rules.json.sig"] = medic::make_signature_file(key, new_text);
        svc.set_http_client(&http);
        svc.set_trusted_keys(trusted);
        App app(svc);
        Canvas cv(App::kWidth, App::kHeight);
        go_to_update(app);
        press(app, {Button::Square, Button::Cross});
        app.render(cv);
        app.update();
        app.render(cv);
        const auto offer = svc.check_rule_update();
        CHECK(offer.state == RuleUpdateOffer::State::Rejected && offer.reason == "sig_invalid", "Download-E2E: veraenderte Datei wird abgelehnt");
        press(app, {Button::Cross});
        CHECK(!fs::exists(t.dir / "fs/data/ps5medic/knowledge/rules.json") && svc.run_check().rules_version == rules.db_version && !svc.install_rule_update().ok,
              "Download-E2E: abgelehnte Datei laesst sich nicht installieren, alte Regeln gelten");
    }
    {  // ohne Netz-Client (PC-Vorschau) und ohne Adresse
        TempCopy t("b2_pos");
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, clock);
        CHECK(svc.rule_update_url().empty() && svc.download_rule_update().error == "no_network", "Download-E2E: ohne Adresse und ohne Netz-Client");
        spit(t.dir / "fs/data/ps5medic/config.ini", "update_url=" + base + "\n");
        CHECK(svc.rule_update_url() == base && svc.download_rule_update().error == "no_network", "Download-E2E: ohne Netz-Client kein Abruf, Kennung no_network");
    }
}

void report_service_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    auto clock = [] { return std::string("20261108T130000Z"); };
    const std::string report_file = "fs/data/ps5medic/reports/report_20261108T130000Z.md";

    {  // schreibbar: genau eine Datei im Berichtsordner, sonst bleibt alles wie es war
        TempCopy t("b2_pos");
        const auto before = full_tree(t.dir);
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, clock);
        svc.run_check();
        const ReportResult r = svc.make_report(ReportLang::En);
        CHECK(r.saved && r.error.empty() && r.path == "/data/ps5medic/reports/report_20261108T130000Z.md", "Bericht: gespeichert unter dem Zeitstempel-Namen");
        const std::string content = fs::exists(t.dir / report_file) ? slurp(t.dir / report_file) : std::string();
        CHECK(content.find("# PS5 Medic – Report") != std::string::npos && content.find("smp.parentheses_in_pfs_name") != std::string::npos &&
                  content.find("Firmware: 4.03") != std::string::npos,
              "Bericht: Datei enthaelt Kopf, Befund und Firmware (Englisch)");
        std::vector<std::string> added;
        for (auto& p : full_tree(t.dir))
            if (!before.count(p)) added.push_back(p);
        const bool only_reports = std::all_of(added.begin(), added.end(), [](const std::string& p) {
            return p == "fs/data/ps5medic" || p.rfind("fs/data/ps5medic/reports", 0) == 0;
        });
        CHECK(only_reports && std::find(added.begin(), added.end(), report_file) != added.end(), "Bericht: es kommt nur eine Datei unter /data/ps5medic/reports/ hinzu");
        CHECK(r.qr_text.find("Report: /data/ps5medic/reports/report_20261108T130000Z.md") != std::string::npos && !r.truncated, "Bericht: QR-Text nennt die Datei");
        CHECK(r.qr_text.find("smp.parentheses_in_pfs_name") != std::string::npos, "Bericht: QR-Text nennt die Befunde");
    }
    {  // nur lesend (z. B. PC-Vorschau): nichts wird geschrieben, den QR-Text gibt es trotzdem
        TempCopy t("b2_pos");
        const auto before = full_tree(t.dir);
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/false);
        LocalService svc(*platform, rules, clock);
        svc.run_check();
        const ReportResult r = svc.make_report(ReportLang::De);
        CHECK(!r.saved && r.error == "no_write" && r.path.empty() && !r.qr_text.empty() && full_tree(t.dir) == before, "Bericht: ohne Schreibzugriff wird nichts geschrieben");
        CHECK(r.qr_text.find("Bericht:") == std::string::npos, "Bericht: kein Dateipfad im QR-Text, wenn nichts gespeichert wurde");
    }
    {  // kaputte Uhr: kein Pfad, nichts geschrieben (der Pfad entsteht nur aus einem gueltigen Zeitstempel)
        TempCopy t("b2_pos");
        const auto before = full_tree(t.dir);
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, [] { return std::string("../../etc/x"); });
        svc.run_check();
        const ReportResult r = svc.make_report(ReportLang::De);
        CHECK(!r.saved && r.path.empty() && full_tree(t.dir) == before, "Bericht: ungueltiger Zeitstempel schreibt nichts");
    }
    {  // UI: englische Oberflaeche -> englischer Bericht; auch ohne Befunde moeglich
        FakeService svc;
        svc.result.fw = "12.00";
        App app(svc, Lang::En);
        Canvas cv(App::kWidth, App::kHeight);
        run_check_via_ui(app, cv);
        press(app, {Button::Square});
        CHECK(app.screen() == App::Screen::Report && svc.reports == 1 && svc.last_report_lang == ReportLang::En, "Bericht: Englisch und ohne Befunde");
        app.render(cv);
    }
}

// ---- Steuerung: linker Stick, Steuerkreuz-Wiederholung, L1/R1 ------------------------------------

using Buttons = std::vector<Button>;

void pad_nav_tests() {
    {  // Stick: Schwelle, sofortiger Schritt, Wiederholung nur fuer Hoch/Runter
        PadNav n;
        Buttons out;
        n.stick(0, 15000, 0, out);
        n.poll(1000, out);
        CHECK(out.empty(), "Stick: Ausschlag unter der Schwelle (Drift) loest nichts aus");
        n.stick(0, 20000, 1000, out);
        CHECK(out == Buttons{Button::Down}, "Stick: nach unten loest sofort einen Schritt aus");
        out.clear();
        n.poll(1399, out);
        CHECK(out.empty(), "Stick: vor Ablauf der Wartezeit keine Wiederholung");
        n.poll(1400, out);
        CHECK(out == Buttons{Button::Down}, "Stick: gehalten wiederholt Runter nach 400 ms");
        out.clear();
        n.poll(1450, out);
        CHECK(out.empty(), "Stick: Wiederholung im Takt, nicht in jedem Bild");
        n.poll(1510, out);
        CHECK(out == Buttons{Button::Down}, "Stick: danach alle 110 ms");
        out.clear();
        n.stick(0, 10000, 1520, out);  // Hysterese: zwischen kOff und kOn bleibt er gedrueckt
        n.poll(1620, out);
        CHECK(out == Buttons{Button::Down}, "Stick: Hysterese haelt die Richtung bei leichtem Zurueckgehen");
        out.clear();
        n.stick(0, 5000, 1630, out);
        n.poll(2000, out);
        CHECK(out.empty(), "Stick: losgelassen, keine Wiederholung mehr");
        n.stick(0, -30000, 2100, out);
        CHECK(out == Buttons{Button::Up}, "Stick: nach oben (Y negativ)");
    }
    {  // Links/Rechts: genau ein Schritt, keine Wiederholung (in den Einstellungen wuerde sonst die Sprache flackern)
        PadNav n;
        Buttons out;
        n.stick(-25000, 0, 0, out);
        n.poll(5000, out);
        n.poll(5200, out);
        CHECK(out == Buttons{Button::Left}, "Stick: links ist ein einzelner Schritt");
        out.clear();
        n.stick(0, 0, 5300, out);
        n.stick(25000, 3000, 5400, out);
        CHECK(out == Buttons{Button::Right}, "Stick: rechts");
    }
    {  // Diagonale: nur die staerkere Achse zaehlt; Richtungswechsel loest neu aus
        PadNav n;
        Buttons out;
        n.stick(20000, 22000, 0, out);
        CHECK(out == Buttons{Button::Down}, "Stick: Diagonale waehlt die staerkere Achse");
        out.clear();
        n.stick(26000, 22000, 10, out);
        CHECK(out == Buttons{Button::Right}, "Stick: Richtungswechsel loest sofort aus");
        out.clear();
        n.stick(-32768, 0, 20, out);  // kleinster moeglicher Achsenwert
        CHECK(out == Buttons{Button::Left}, "Stick: Extremwert -32768 ist gueltig");
    }
    {  // Steuerkreuz: sofort, Vorrang vor dem Stick, Wiederholung beim Halten
        PadNav n;
        Buttons out;
        n.dpad(Button::Down, true, 0, out);
        CHECK(out == Buttons{Button::Down}, "Kreuz: Druck loest sofort aus (auch ohne Bild dazwischen)");
        n.dpad(Button::Down, false, 10, out);
        n.poll(2000, out);
        CHECK(out == Buttons{Button::Down}, "Kreuz: kurzes Antippen ergibt genau einen Schritt");
        out.clear();
        n.dpad(Button::Down, true, 3000, out);
        out.clear();
        n.poll(3400, out);
        n.poll(3510, out);
        CHECK(out == (Buttons{Button::Down, Button::Down}), "Kreuz: gehalten wiederholt");
        out.clear();
        n.dpad(Button::Down, false, 3520, out);
        n.poll(4000, out);
        CHECK(out.empty(), "Kreuz: losgelassen hoert die Wiederholung auf");
    }
    {  // Kreuz hat Vorrang vor dem Stick; nach dem Loslassen kein Extra-Schritt
        PadNav n;
        Buttons out;
        n.stick(0, 30000, 0, out);
        out.clear();
        n.dpad(Button::Up, true, 100, out);
        CHECK(out == Buttons{Button::Up}, "Kreuz: hat Vorrang vor dem Stick");
        out.clear();
        n.dpad(Button::Up, false, 150, out);
        CHECK(out.empty(), "Kreuz: Loslassen loest keinen Schritt des noch gehaltenen Sticks aus");
        n.poll(600, out);
        CHECK(out == Buttons{Button::Down}, "Kreuz: der gehaltene Stick laeuft danach weiter");
    }
    {  // Zwei Kreuz-Tasten: die zuletzt gedrueckte gilt, beim Loslassen faellt sie auf die andere zurueck
        PadNav n;
        Buttons out;
        n.dpad(Button::Up, true, 0, out);
        n.dpad(Button::Down, true, 5, out);
        CHECK(out == (Buttons{Button::Up, Button::Down}), "Kreuz: zuletzt gedrueckte Richtung gilt");
        out.clear();
        n.dpad(Button::Down, false, 10, out);
        CHECK(out.empty(), "Kreuz: Rueckfall auf die noch gehaltene Taste ohne Extra-Schritt");
    }
    {  // Fremde Tasten und Zuruecksetzen
        PadNav n;
        Buttons out;
        n.dpad(Button::Cross, true, 0, out);
        n.dpad(Button::L1, true, 0, out);
        n.poll(10000, out);
        CHECK(out.empty(), "Kreuz: nur Richtungstasten werden verarbeitet (Stick und Nav loesen nie Kreuz/Kreis aus)");
        n.stick(0, 30000, 0, out);
        out.clear();
        n.reset();
        n.poll(10000, out);
        CHECK(out.empty(), "reset: nichts bleibt gedrueckt");
        n.stick(0, 30000, 10000, out);
        CHECK(out == Buttons{Button::Down}, "reset: der Stick funktioniert danach wieder");
    }
}

void shoulder_and_stick_flow_tests() {
    FakeService svc;
    svc.result.findings = {mk_finding("a.crit", Severity::Critical, true), mk_finding("b.warn", Severity::Warning, false), mk_finding("c.info", Severity::Info, true)};
    svc.plan = mk_plan();
    Canvas cv(App::kWidth, App::kHeight);
    Strings::missing().clear();

    {  // Start: L1/R1 wie Hoch/Runter, Links/Rechts zwischen den beiden kleinen Knoepfen
        App a(svc);
        press(a, {Button::R1, Button::Cross, Button::Square});
        CHECK(a.screen() == App::Screen::History, "Start: R1 waehlt den naechsten Eintrag (Verlauf)");
        press(a, {Button::Circle, Button::Circle, Button::R1, Button::Cross});
        CHECK(a.screen() == App::Screen::Settings, "Start: R1 noch einmal waehlt die Einstellungen");
        press(a, {Button::Circle, Button::Left, Button::Cross, Button::Square});
        CHECK(a.screen() == App::Screen::History, "Start: Links springt von Einstellungen zu Verlauf");
        press(a, {Button::Circle, Button::Circle, Button::Right, Button::Cross});
        CHECK(a.screen() == App::Screen::Settings, "Start: Rechts springt von Verlauf zu Einstellungen");
        press(a, {Button::Circle, Button::L1, Button::L1, Button::Left, Button::Right, Button::Cross});
        CHECK(a.screen() == App::Screen::Running, "Start: L1 geht hoch; oben bleiben Links/Rechts wirkungslos, Kreuz startet den Check");
    }
    {  // Einstellungen: L1/R1 wie Hoch/Runter
        App a(svc);
        press(a, {Button::Down, Button::Down, Button::Cross});
        CHECK(a.screen() == App::Screen::Settings, "Einstellungen offen");
        press(a, {Button::R1, Button::Cross});
        CHECK(a.screen() == App::Screen::RulesUpdate, "Einstellungen: R1 waehlt Regel-Update");
        press(a, {Button::Circle, Button::L1, Button::Right});
        CHECK(a.lang() == Lang::En, "Einstellungen: L1 waehlt die Sprache, Rechts wechselt sie");
    }
    {  // Details: zwischen den Befunden blaettern
        App a(svc);
        run_check_via_ui(a, cv);
        press(a, {Button::Cross});
        CHECK(a.screen() == App::Screen::Detail && a.selected() == 0, "Details: offen beim ersten Befund");
        press(a, {Button::R1});
        CHECK(a.screen() == App::Screen::Detail && a.selected() == 1, "Details: R1 zeigt den naechsten Befund");
        press(a, {Button::Right, Button::Right});
        CHECK(a.selected() == 2, "Details: Rechts blaettert weiter und stoppt am letzten Befund");
        a.render(cv);
        press(a, {Button::L1});
        CHECK(a.selected() == 1, "Details: L1 zeigt den vorherigen Befund");
        press(a, {Button::Left, Button::Left, Button::Left});
        CHECK(a.selected() == 0 && a.screen() == App::Screen::Detail, "Details: Links stoppt am ersten Befund");
        const int plans = svc.plans;
        press(a, {Button::R1, Button::Triangle});  // Befund 1 hat keine Reparatur
        CHECK(a.screen() == App::Screen::Detail && svc.plans == plans, "Details: Reparatur gilt dem angezeigten Befund (ohne Fix passiert nichts)");
        press(a, {Button::R1, Button::Triangle});  // Befund 2 hat eine
        CHECK(a.screen() == App::Screen::FixPreview && svc.plans == plans + 1, "Details: Reparatur gilt dem angezeigten Befund (mit Fix kommt die Vorschau)");
        press(a, {Button::Circle, Button::Circle});
        CHECK(a.screen() == App::Screen::Findings && a.selected() == 2, "Details: die Liste steht danach beim zuletzt gezeigten Befund");
    }
    {  // Befundliste: seitenweise
        FakeService big;
        for (int i = 0; i < 25; ++i) big.result.findings.push_back(mk_finding("rule." + std::to_string(i), Severity::Warning, false));
        App a(big);
        run_check_via_ui(a, cv);
        press(a, {Button::L1});
        CHECK(a.selected() == 0, "Liste: L1 auf der ersten Seite bleibt oben");
        press(a, {Button::R1});
        CHECK(a.selected() == 4, "Liste: R1 blaettert eine Seite (4 Zeilen)");
        a.render(cv);
        press(a, {Button::Down});
        press(a, {Button::R1});
        CHECK(a.selected() == 9, "Liste: R1 behaelt die Zeile auf der Seite bei");
        for (int i = 0; i < 10; ++i) a.handle(Button::R1);
        CHECK(a.selected() == 24, "Liste: R1 endet beim letzten Befund");
        a.render(cv);
        press(a, {Button::L1});
        CHECK(a.selected() == 20, "Liste: L1 geht eine Seite zurueck");
        for (int i = 0; i < 10; ++i) a.handle(Button::L1);
        CHECK(a.selected() == 0, "Liste: L1 endet beim ersten Befund");
        a.render(cv);
        press(a, {Button::Cross});
        CHECK(a.screen() == App::Screen::Detail && a.selected() == 0, "Liste: Kreuz oeffnet weiter den gewaehlten Befund");
    }
    {  // Kurze Liste: L1/R1 springen zum Anfang/Ende
        App a(svc);
        run_check_via_ui(a, cv);
        press(a, {Button::R1});
        CHECK(a.selected() == 2, "Liste: R1 in einer kurzen Liste springt zum Ende");
        press(a, {Button::L1});
        CHECK(a.selected() == 0, "Liste: L1 in einer kurzen Liste springt zum Anfang");
    }
    {  // Verlauf: seitenweise; die gewaehlte Zeile bestimmt, was rueckgaengig gemacht wird
        FakeService h;
        for (int i = 0; i < 14; ++i) {
            TxnInfo t = mk_history()[1];  // "undone"
            t.txn = "T" + std::to_string(i);
            t.state = i == 7 ? "applied" : "undone";  // die UI zeigt neueste zuerst: Index 7 steht an Position 6
            h.hist.push_back(t);
        }
        App a(h);
        press(a, {Button::Down, Button::Cross, Button::Square});
        CHECK(a.screen() == App::Screen::History, "Verlauf offen");
        press(a, {Button::R1, Button::Cross});
        CHECK(a.screen() == App::Screen::UndoConfirm, "Verlauf: R1 blaettert eine Seite (6 Zeilen) auf die 'applied'-Zeile");
        press(a, {Button::Cross});
        CHECK(h.undos == 1 && h.last_undo == "T7", "Verlauf: zurueckgenommen wird die gewaehlte Transaktion");
    }
    {  // Beschriftungen aller neuen Hinweise existieren in beiden Sprachen
        FakeService big;
        for (int i = 0; i < 25; ++i) big.result.findings.push_back(mk_finding("rule." + std::to_string(i), Severity::Info, true));
        for (int i = 0; i < 9; ++i) big.hist.push_back(mk_history()[1]);
        for (Lang l : {Lang::De, Lang::En}) {
            App a(big, l);
            run_check_via_ui(a, cv);
            a.render(cv);
            press(a, {Button::Cross});
            a.render(cv);
            press(a, {Button::Circle, Button::Circle, Button::Down, Button::Cross});
            a.render(cv);
        }
        CHECK(Strings::missing().empty(), "Hinweise: 'Seite' und 'Blaettern' gibt es in beiden Sprachen");
    }
    {  // Sicherheit: die Richtungstasten (also auch der Stick) bestaetigen und aendern nichts
        App a(svc);
        run_check_via_ui(a, cv);
        press(a, {Button::Triangle});
        CHECK(a.screen() == App::Screen::FixPreview, "Vorschau offen");
        PadNav n;
        Buttons out;
        n.stick(0, 30000, 0, out);
        n.stick(30000, 0, 10, out);
        n.stick(0, -30000, 20, out);
        n.poll(1000, out);
        for (Button b : out) a.handle(b);
        CHECK(a.screen() == App::Screen::FixPreview && svc.applies == 0, "Stick: bestaetigt keine Reparatur");
    }
}

// ---- Fehlercodes (Nachschlagen) ----------------------------------------------------

// n Codes; Code 0 hat viele lange Schritte (die Detailansicht muss scrollen), die uebrigen je einen kurzen.
ErrorCodeSet mk_errors(int n) {
    ErrorCodeSet s;
    s.db_version = "t";
    for (int i = 0; i < n; ++i) {
        ErrorCode c;
        char id[32];
        std::snprintf(id, sizeof id, "CE-1000%02d-1", i);
        c.id = id;
        c.title_de = "Titel " + c.id + " mit Umlauten äöü";
        c.title_en = "Title " + c.id;
        c.text_de = "Deutsche Erklärung zu " + c.id + ". Sie hat mehrere Sätze, damit der Umbruch greift. Dieser Satz ist der dritte.";
        c.text_en = "English explanation for " + c.id + ". It has several sentences so that wrapping applies. This is the third sentence.";
        const int steps = i == 0 ? 12 : 1;
        for (int k = 1; k <= steps; ++k) {
            c.steps_de.push_back("Schritt " + std::to_string(k) + ": ein längerer Satz, der sicher auf mehrere Zeilen umbricht, weil er viel Text enthält und die Zeile nur begrenzt breit ist.");
            c.steps_en.push_back("Step " + std::to_string(k) + ": a longer sentence that surely wraps onto several lines because it contains a lot of text and the line is only so wide.");
        }
        if (i == 1) {
            c.notes_de = {"Hinweis eins"};
            c.notes_en = {"Note one"};
            c.more_sources = {"https://example.org/weitere"};
        }
        c.source = "https://example.org/" + c.id;
        c.level = i % 3 == 2 ? "community" : "doc";
        s.codes.push_back(std::move(c));
    }
    return s;
}

std::uint64_t canvas_hash(const Canvas& c) {  // FNV-1a ueber alle Pixel
    std::uint64_t h = 1469598103934665603ull;
    const std::uint32_t* d = c.data();
    for (int i = 0; i < c.width() * c.height(); ++i) {
        h ^= d[i];
        h *= 1099511628211ull;
    }
    return h;
}

// Zeichnet das Zeichen mit der Schrift; sieht das Bild aus wie das Ersatzzeichen "?", fehlt das Glyph.
bool glyph_missing(const Font& f, const std::string& utf8) {
    if (utf8 == "?") return false;
    Canvas a(80, 60), b(80, 60);
    a.clear(rgb(0, 0, 0));
    b.clear(rgb(0, 0, 0));
    f.draw(a, 4, 4, utf8, rgb(255, 255, 255));
    f.draw(b, 4, 4, "?", rgb(255, 255, 255));
    return canvas_hash(a) == canvas_hash(b);
}

// Scrollt in der Detailansicht ganz nach unten und liefert die dort erreichte Zeile (0: der Text passt ohne Scrollen).
int scroll_to_end(App& a) {
    for (int i = 0; i < 300; ++i) a.handle(Button::Down);
    return a.error_scroll();
}

void open_error_codes_via_ui(App& a) { press(a, {Button::Down, Button::Down, Button::Down, Button::Cross}); }

void error_codes_flow_tests() {
    FakeService svc;
    svc.errors = mk_errors(12);
    Canvas cv(App::kWidth, App::kHeight);
    Strings::missing().clear();

    {  // vom Start erreichbar: Runter, Rechts, L1/R1; an den Raendern bleibt die Auswahl stehen
        App a(svc);
        open_error_codes_via_ui(a);
        CHECK(a.screen() == App::Screen::ErrorCodes && svc.error_loads >= 1, "Fehlercodes: Start, dreimal Runter, Kreuz oeffnet die Liste");
        press(a, {Button::Circle});
        CHECK(a.screen() == App::Screen::Start, "Fehlercodes: Kreis zurueck zum Start");
        press(a, {Button::Up, Button::Right, Button::Cross});  // von Fehlercodes (3) hoch zu den Einstellungen (2), Rechts zurueck
        CHECK(a.screen() == App::Screen::ErrorCodes, "Fehlercodes: Rechts von den Einstellungen waehlt Fehlercodes");
        press(a, {Button::Circle, Button::Left, Button::Cross});
        CHECK(a.screen() == App::Screen::Settings, "Fehlercodes: Links geht zurueck zu den Einstellungen");
        press(a, {Button::Circle, Button::Down, Button::Down, Button::Down, Button::Down, Button::Down, Button::Cross});
        CHECK(a.screen() == App::Screen::SelfTest, "Selbsttest: weiter nach unten bleibt beim letzten Knopf (Selbsttest)");
        press(a, {Button::Circle, Button::Up, Button::Up, Button::Up, Button::Up, Button::Up, Button::Cross});
        CHECK(a.screen() == App::Screen::Running, "Fehlercodes: Hoch bis oben, dort startet Kreuz den Check-up");
    }
    {  // Liste: Auswahl, Blaettern, Details, zurueck
        App a(svc);
        open_error_codes_via_ui(a);
        CHECK(a.error_selected() == 0, "Fehlercodes: Auswahl beginnt oben");
        press(a, {Button::Up});
        CHECK(a.error_selected() == 0, "Fehlercodes: oben bleibt die Auswahl oben");
        press(a, {Button::Down, Button::Down, Button::Down});
        CHECK(a.error_selected() == 3, "Fehlercodes: Runter waehlt den naechsten Code");
        a.render(cv);
        press(a, {Button::R1});
        CHECK(a.error_selected() > 3 && a.error_selected() <= 11, "Fehlercodes: R1 blaettert eine Seite weiter");
        press(a, {Button::R1});
        CHECK(a.error_selected() == 11, "Fehlercodes: R1 springt zum letzten Code");
        a.render(cv);
        press(a, {Button::Down, Button::Down});
        CHECK(a.error_selected() == 11, "Fehlercodes: unten bleibt die Auswahl unten");
        press(a, {Button::L1});
        CHECK(a.error_selected() < 11 && a.error_selected() >= 0, "Fehlercodes: L1 blaettert eine Seite zurueck");
        press(a, {Button::L1, Button::L1});
        CHECK(a.error_selected() == 0, "Fehlercodes: L1 springt zum ersten Code");

        press(a, {Button::Down, Button::Down, Button::Cross});
        CHECK(a.screen() == App::Screen::ErrorCodeDetail && a.error_selected() == 2 && a.error_scroll() == 0, "Fehlercodes: Kreuz zeigt die Details des gewaehlten Codes");
        a.render(cv);
        press(a, {Button::Right});
        CHECK(a.screen() == App::Screen::ErrorCodeDetail && a.error_selected() == 3, "Fehlercodes: Rechts blaettert zum naechsten Code");
        press(a, {Button::R1, Button::R1});
        CHECK(a.error_selected() == 5, "Fehlercodes: R1 blaettert ebenfalls");
        press(a, {Button::Left, Button::L1, Button::L1, Button::L1, Button::L1, Button::L1, Button::L1, Button::L1});
        CHECK(a.error_selected() == 0, "Fehlercodes: Links/L1 blaettern bis zum ersten Code und nicht weiter");
        press(a, {Button::Circle});
        CHECK(a.screen() == App::Screen::ErrorCodes && a.error_selected() == 0, "Fehlercodes: Kreis in den Details geht zur Liste");
        press(a, {Button::Down, Button::Down, Button::Down, Button::Cross, Button::Right, Button::Circle});
        CHECK(a.screen() == App::Screen::ErrorCodes && a.error_selected() == 4, "Fehlercodes: die Liste behaelt die beim Blaettern erreichte Auswahl");
        a.render(cv);
        press(a, {Button::Circle});
        CHECK(a.screen() == App::Screen::Start && !a.quit_requested(), "Fehlercodes: Kreis in der Liste geht zum Start");
    }
    {  // Scrollen in den Details: nur wenn der Text nicht passt, stoppt am Ende, springt beim Blaettern zurueck
        App a(svc);
        open_error_codes_via_ui(a);
        press(a, {Button::Cross});
        CHECK(a.screen() == App::Screen::ErrorCodeDetail && a.error_selected() == 0, "Scrollen: Code mit vielen Schritten offen");
        a.render(cv);
        const std::uint64_t top = canvas_hash(cv);
        press(a, {Button::Up});
        CHECK(a.error_scroll() == 0, "Scrollen: oben bleibt die Ansicht oben");
        press(a, {Button::Down});
        CHECK(a.error_scroll() == 1, "Scrollen: Runter schiebt den Text um eine Zeile");
        a.render(cv);
        CHECK(canvas_hash(cv) != top, "Scrollen: die Anzeige aendert sich");
        const int end = scroll_to_end(a);
        CHECK(end > 5, "Scrollen: ein langer Eintrag laesst sich bis zum Ende scrollen (" + std::to_string(end) + ")");
        press(a, {Button::Down});
        CHECK(a.error_scroll() == end, "Scrollen: am Ende ist Schluss");
        a.render(cv);
        press(a, {Button::Up});
        CHECK(a.error_scroll() == end - 1, "Scrollen: Hoch scrollt zurueck");
        for (int i = 0; i < 300; ++i) a.handle(Button::Up);
        CHECK(a.error_scroll() == 0, "Scrollen: bis ganz nach oben");
        scroll_to_end(a);
        press(a, {Button::Right});
        CHECK(a.error_selected() == 1 && a.error_scroll() == 0, "Scrollen: Blaettern beginnt wieder oben");
        CHECK(scroll_to_end(a) == 0, "Scrollen: ein kurzer Eintrag scrollt nicht");
        press(a, {Button::Left});
        CHECK(a.error_selected() == 0 && a.error_scroll() == 0, "Scrollen: zurueck zum langen Code beginnt oben");
    }
    {  // Nur lesen: weder Reparatur noch Check-up noch Bericht noch Update, egal welche Taste
        FakeService s2;
        s2.errors = mk_errors(5);
        s2.result.findings = {mk_finding("a.crit", Severity::Critical, true)};
        s2.plan = mk_plan();
        App a(s2);
        open_error_codes_via_ui(a);
        for (Button b : {Button::Triangle, Button::Square, Button::Options, Button::Left, Button::Right}) a.handle(b);
        CHECK(a.screen() == App::Screen::ErrorCodes, "Nur lesen: Dreieck, Quadrat, Options, Links, Rechts aendern in der Liste nichts");
        press(a, {Button::Cross});
        for (Button b : {Button::Options, Button::Cross}) a.handle(b);
        CHECK(a.screen() == App::Screen::ErrorCodeDetail, "Nur lesen: Options und Kreuz aendern in den Details nichts");
        press(a, {Button::Triangle});  // vermerkt den Code nur im Speicher fuer den Bericht
        CHECK(a.screen() == App::Screen::ErrorCodeDetail && a.reported_error() == a.selected_error_id(), "Nur lesen: Dreieck vermerkt den Code fuer den Bericht, sonst nichts");
        press(a, {Button::Square});  // QR-Code der Quelle: reine Anzeige
        CHECK(a.screen() == App::Screen::ErrorCodeSource, "Nur lesen: Quadrat zeigt die Quelle als QR-Code");
        a.render(cv);
        press(a, {Button::Triangle, Button::Square, Button::Cross, Button::Options});
        CHECK(a.screen() == App::Screen::ErrorCodeSource, "Nur lesen: in der Quellenansicht tun Dreieck, Quadrat, Kreuz und Options nichts");
        press(a, {Button::Circle});
        CHECK(a.screen() == App::Screen::ErrorCodeDetail, "Nur lesen: Kreis geht aus der Quellenansicht zum Eintrag");
        a.render(cv);
        CHECK(s2.checks == 0 && s2.plans == 0 && s2.applies == 0 && s2.undos == 0 && s2.reports == 0 && s2.update_checks == 0 && s2.installs == 0 &&
                  s2.rollbacks == 0 && s2.downloads == 0,
              "Nur lesen: der Dienst wurde nie zum Pruefen, Reparieren, Berichten oder Laden benutzt");
    }
    {  // keine Datei geladen: Hinweis statt Liste, nichts kaputt
        FakeService empty;
        App a(empty);
        open_error_codes_via_ui(a);
        CHECK(a.screen() == App::Screen::ErrorCodes, "Leer: Liste oeffnet sich");
        a.render(cv);
        press(a, {Button::Down, Button::Up, Button::L1, Button::R1, Button::Cross});
        CHECK(a.screen() == App::Screen::ErrorCodes && a.error_selected() == 0, "Leer: Kreuz und Blaettern tun nichts");
        a.render(cv);
        press(a, {Button::Circle});
        CHECK(a.screen() == App::Screen::Start, "Leer: Kreis zurueck");
    }
    {  // beide Sprachen: andere Anzeige, keine fehlenden Texte
        App de(svc, Lang::De), en(svc, Lang::En);
        open_error_codes_via_ui(de);
        open_error_codes_via_ui(en);
        de.render(cv);
        const std::uint64_t list_de = canvas_hash(cv);
        en.render(cv);
        CHECK(list_de != canvas_hash(cv), "Sprachen: die Liste unterscheidet sich");
        press(de, {Button::Down, Button::Cross});  // Code 1: mit Hinweis und weiterer Quelle
        press(en, {Button::Down, Button::Cross});
        de.render(cv);
        const std::uint64_t detail_de = canvas_hash(cv);
        en.render(cv);
        CHECK(detail_de != canvas_hash(cv), "Sprachen: die Details unterscheiden sich");
        // Community-Eintrag (Code 2) und Eintrag mit vielen Schritten (Code 0) in beiden Sprachen, auch am Ende der Scroll-Strecke
        for (Lang l : {Lang::De, Lang::En}) {
            App a(svc, l);
            open_error_codes_via_ui(a);
            press(a, {Button::Down, Button::Down, Button::Cross});
            a.render(cv);
            scroll_to_end(a);
            a.render(cv);
            press(a, {Button::Left, Button::Left});
            a.render(cv);
            scroll_to_end(a);
            a.render(cv);
        }
    }
    CHECK(Strings::missing().empty(), "Fehlercodes: alle gezeichneten Texte existieren in beiden Sprachen (" + (Strings::missing().empty() ? std::string() : Strings::missing().front()) + ")");
    Strings::missing().clear();

    // Texte der Bildschirme und der Startkarte: in beiden Sprachen vorhanden und nicht leer
    for (const char* key : {"start.errors", "start.errors_sub", "errors.title", "errors.empty", "errors.empty_sub", "errors.readonly", "errors.steps", "errors.steps_none",
                            "errors.notes", "errors.community", "errors.community_note", "errors.source_more", "errors.level.doc", "errors.level.community",
                            "errors.position", "errors.scroll"})
        for (Lang l : {Lang::De, Lang::En}) {
            const auto& S = Strings::get(l);
            CHECK(S.has(key) && !S(key).empty(), std::string("Fehlercodes: Text vorhanden: ") + key + (l == Lang::De ? " (de)" : " (en)"));
        }
    CHECK(Strings::get(Lang::De).fmt("errors.position", {{"i", "3"}, {"n", "20"}}) == "Code 3 von 20", "Fehlercodes: Positionsangabe (de)");
    CHECK(Strings::get(Lang::En).fmt("errors.position", {{"i", "3"}, {"n", "20"}}) == "Code 3 of 20", "Fehlercodes: Positionsangabe (en)");
    Strings::missing().clear();
}

// Mit der echten Datei und dem echten Dienst: jeder Code laesst sich in beiden Sprachen oeffnen, durchscrollen und zeichnen,
// ohne dass auf der (beschreibbaren) Konsole etwas geschieht; alle Zeichen der Texte gibt es in der Schrift.
void error_codes_real_data_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    const ErrorCodeSet codes = load_error_codes_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/error_codes.json");
    CHECK(codes.codes.size() >= 36, "Fehlercodes (Datei): mindestens die 36 belegten Codes");

    TempCopy t("b8_pos");
    const auto before = full_tree(t.dir);
    auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
    LocalService svc(*platform, rules, [] { return std::string("20261108T120000Z"); });
    CHECK(svc.error_codes().codes.empty(), "Fehlercodes (Datei): ohne set_error_codes ist die Liste leer");
    svc.set_error_codes(codes);
    CHECK(svc.error_codes().codes.size() == codes.codes.size(), "Fehlercodes (Datei): LocalService liefert die gesetzten Codes");

    Canvas cv(App::kWidth, App::kHeight);
    Strings::missing().clear();
    std::set<std::string> scrolling_codes;  // deutsche Anzeige: Eintraege, deren Text nicht auf einen Bildschirm passt
    for (Lang l : {Lang::De, Lang::En}) {
        App a(svc, l);
        open_error_codes_via_ui(a);
        CHECK(a.screen() == App::Screen::ErrorCodes, "Fehlercodes (Datei): Liste offen");
        a.render(cv);
        for (int i = 0; i < 30 && a.error_selected() != static_cast<int>(codes.codes.size()) - 1; ++i) press(a, {Button::R1});
        a.render(cv);
        CHECK(a.error_selected() == static_cast<int>(codes.codes.size()) - 1, "Fehlercodes (Datei): R1 bis zum letzten Code");
        for (int i = 0; i < 30 && a.error_selected() != 0; ++i) press(a, {Button::L1});
        CHECK(a.error_selected() == 0, "Fehlercodes (Datei): L1 bis zum ersten Code");
        press(a, {Button::Cross});
        for (std::size_t i = 0; i < codes.codes.size(); ++i) {
            CHECK(a.screen() == App::Screen::ErrorCodeDetail && a.error_selected() == static_cast<int>(i), "Fehlercodes (Datei): Detail " + codes.codes[i].id);
            a.render(cv);
            const std::uint64_t top = canvas_hash(cv);
            const int end = scroll_to_end(a);
            a.render(cv);
            if (end > 0) {
                CHECK(canvas_hash(cv) != top, "Fehlercodes (Datei): Scrollen aendert die Anzeige bei " + codes.codes[i].id);
                if (l == Lang::De) scrolling_codes.insert(codes.codes[i].id);
            }
            for (int k = 0; k < end; k += std::max(end / 3, 1)) {  // einige Zwischenstaende
                for (int j = 0; j < 300; ++j) a.handle(Button::Up);
                for (int j = 0; j < k; ++j) a.handle(Button::Down);
                a.render(cv);
            }
            press(a, {Button::Right});
        }
        CHECK(a.error_selected() == static_cast<int>(codes.codes.size()) - 1, "Fehlercodes (Datei): Rechts bleibt beim letzten Code stehen");
        press(a, {Button::Circle, Button::Circle});
        CHECK(a.screen() == App::Screen::Start, "Fehlercodes (Datei): zurueck zum Start");
    }
    CHECK(scrolling_codes.count("CE-108262-9") && !scrolling_codes.count("CE-100028-1"), "Fehlercodes (Datei): der laengste Eintrag (Safe Mode) scrollt, ein kurzer nicht");
    CHECK(Strings::missing().empty(), "Fehlercodes (Datei): keine fehlenden Texte in beiden Sprachen");
    Strings::missing().clear();
    CHECK(full_tree(t.dir) == before, "Fehlercodes (Datei): auf der Konsole wurde nichts geschrieben, auch mit beschreibbarer Plattform");

    // Alle Zeichen der Texte muessen in der eingebauten Schrift vorkommen, sonst erscheint "?" mitten im Hinweis.
    std::set<std::string> glyphs;
    auto collect = [&](const std::string& s) {
        for (std::size_t i = 0; i < s.size();) {
            const std::size_t start = i;
            next_codepoint(s, i);
            glyphs.insert(s.substr(start, i - start));
        }
    };
    for (const auto& c : codes.codes) {
        for (const auto* s : {&c.id, &c.title_de, &c.title_en, &c.text_de, &c.text_en, &c.source}) collect(*s);
        for (const auto* v : {&c.steps_de, &c.steps_en, &c.notes_de, &c.notes_en, &c.more_sources})
            for (const auto& s : *v) collect(s);
    }
    std::string missing;
    for (const auto& g : glyphs)
        for (auto id : {Font::Id::Small, Font::Id::Body})
            if (g != " " && glyph_missing(Font::get(id), g)) missing += g + " ";
    CHECK(missing.empty(), "Fehlercodes (Datei): alle Zeichen gibt es in der Schrift (fehlen: " + missing + ")");
}

// ---- Verweis aus einem Befund auf den Fehlercode-Eintrag -----------------------------------------------------------------------

Finding finding_mentioning(const std::string& id, const std::string& code) {
    Finding f = mk_finding(id, Severity::Warning, false);
    f.text_de += " Fehler " + code + " tritt auf.";
    f.text_en += " Error " + code + " occurs.";
    return f;
}

void error_link_tests() {
    FakeService svc;
    svc.errors = mk_errors(12);  // CE-100000-1 bis CE-100011-1
    svc.result.findings = {finding_mentioning("a.known", "CE-100007-1"), finding_mentioning("b.unknown", "CE-999999-9"), mk_finding("c.plain", Severity::Info, false),
                           finding_mentioning("d.glued", "XCE-100003-1"), finding_mentioning("e.longer", "CE-100003-12"), finding_mentioning("f.dashed", "CE-100003-1-2")};
    Canvas cv(App::kWidth, App::kHeight);
    Strings::missing().clear();

    {  // Quadrat im Detail oeffnet den Eintrag, Kreis kommt zum Befund zurueck
        App a(svc);
        run_check_via_ui(a, cv);
        CHECK(a.screen() == App::Screen::Findings && a.finding_count() == 6, "Verweis: Check-up liefert die Befunde");
        press(a, {Button::Square});
        CHECK(a.screen() == App::Screen::Report, "Verweis: Quadrat in der Liste bleibt der Bericht");
        press(a, {Button::Circle, Button::Cross});
        CHECK(a.screen() == App::Screen::Detail && a.selected() == 0, "Verweis: Befund mit Code offen");
        a.render(cv);
        press(a, {Button::Square});
        CHECK(a.screen() == App::Screen::ErrorCodeDetail && a.error_selected() == 7 && a.error_scroll() == 0, "Verweis: Quadrat zeigt den Eintrag zu CE-100007-1");
        a.render(cv);
        press(a, {Button::Left, Button::L1, Button::Right, Button::R1});
        CHECK(a.error_selected() == 7, "Verweis: im verlinkten Eintrag wird nicht geblaettert");
        press(a, {Button::Down, Button::Up});
        CHECK(a.screen() == App::Screen::ErrorCodeDetail && a.error_scroll() == 0, "Verweis: Scrollen geht (kurzer Eintrag bleibt oben)");
        press(a, {Button::Cross, Button::Options});
        CHECK(a.screen() == App::Screen::ErrorCodeDetail, "Verweis: Kreuz und Options tun im Eintrag nichts");
        press(a, {Button::Circle});
        CHECK(a.screen() == App::Screen::Detail && a.selected() == 0, "Verweis: Kreis geht zurueck zum Befund");
        a.render(cv);
        press(a, {Button::Circle});
        CHECK(a.screen() == App::Screen::Findings, "Verweis: Kreis im Befund geht zur Liste");

        // Befunde ohne passenden Code: Quadrat tut nichts
        for (int i = 1; i < 6; ++i) {
            press(a, {Button::Down, Button::Cross});
            CHECK(a.screen() == App::Screen::Detail && a.selected() == i, "Verweis: Befund " + std::to_string(i) + " offen");
            a.render(cv);
            press(a, {Button::Square});
            CHECK(a.screen() == App::Screen::Detail, "Verweis: Befund " + std::to_string(i) + " ohne bekannten Code, Quadrat tut nichts");
            press(a, {Button::Circle});
        }

        // Aus der Liste geoeffnete Eintraege blaettern wieder, auch nach einem Verweis
        press(a, {Button::Circle, Button::Down, Button::Down, Button::Down, Button::Cross, Button::Cross});
        CHECK(a.screen() == App::Screen::ErrorCodeDetail && a.error_selected() == 0, "Verweis: Eintrag aus der Liste offen");
        press(a, {Button::Right});
        CHECK(a.error_selected() == 1, "Verweis: aus der Liste geoeffnete Eintraege blaettern weiter");
        press(a, {Button::Circle});
        CHECK(a.screen() == App::Screen::ErrorCodes, "Verweis: Kreis geht dort zur Liste");
    }
    {  // Beide Sprachen, Anzeige: mit Nachschlagewerk gibt es Zeile und Taste, ohne nicht; unbekannter Code aendert nichts
        FakeService without;
        without.result = svc.result;  // gleiche Befunde, aber kein Nachschlagewerk geladen
        for (Lang l : {Lang::De, Lang::En}) {
            App with_errors(svc, l), no_errors(without, l);
            run_check_via_ui(with_errors, cv);
            run_check_via_ui(no_errors, cv);
            press(with_errors, {Button::Cross});
            press(no_errors, {Button::Cross});
            with_errors.render(cv);
            const std::uint64_t h_with = canvas_hash(cv);
            no_errors.render(cv);
            CHECK(h_with != canvas_hash(cv), "Verweis: mit Nachschlagewerk zeigt der Befund Zeile und Taste fuer den Code");
            press(with_errors, {Button::Right});  // unbekannter Code CE-999999-9
            press(no_errors, {Button::Right});
            with_errors.render(cv);
            const std::uint64_t h_unknown = canvas_hash(cv);
            no_errors.render(cv);
            CHECK(h_unknown == canvas_hash(cv), "Verweis: ein unbekannter Code wird nicht verlinkt");
            press(no_errors, {Button::Square});
            CHECK(no_errors.screen() == App::Screen::Detail, "Verweis: ohne Nachschlagewerk tut Quadrat nichts");
        }
    }
    CHECK(Strings::missing().empty(), "Verweis: alle gezeichneten Texte existieren in beiden Sprachen (" + (Strings::missing().empty() ? std::string() : Strings::missing().front()) + ")");
    Strings::missing().clear();
    for (const char* key : {"common.lookup", "detail.error_code"})
        for (Lang l : {Lang::De, Lang::En}) CHECK(Strings::get(l).has(key) && !Strings::get(l)(key).empty(), std::string("Verweis: Text vorhanden: ") + key);
    CHECK(Strings::get(Lang::De).fmt("detail.error_code", {{"code", "CE-1"}}).find("CE-1") != std::string::npos, "Verweis: Platzhalter {code}");
    Strings::missing().clear();
}

// Mit echten Regeln und echter Datei: die Befunde, deren Text einen Fehlercode nennt, fuehren zum passenden Eintrag.
void error_link_real_data_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    const ErrorCodeSet codes = load_error_codes_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/error_codes.json");
    Canvas cv(App::kWidth, App::kHeight);
    Strings::missing().clear();
    const std::pair<const char*, const char*> cases[] = {{"smp10_pos_leak", "CE-108262-9"}, {"c1_pos", "CE-105733-9"}};
    for (const auto& [fixture, code] : cases) {
        for (Lang l : {Lang::De, Lang::En}) {
            TempCopy t(fixture);
            const auto before = full_tree(t.dir);
            auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
            LocalService svc(*platform, rules, [] { return std::string("20261108T120000Z"); });
            svc.set_error_codes(codes);
            App a(svc, l);
            run_check_via_ui(a, cv);
            CHECK(a.finding_count() == 1, std::string("Verweis (Datei): ") + fixture + ": ein Befund");
            press(a, {Button::Cross});
            a.render(cv);
            press(a, {Button::Square});
            CHECK(a.screen() == App::Screen::ErrorCodeDetail && a.error_selected() >= 0 && codes.codes[static_cast<std::size_t>(a.error_selected())].id == code,
                  std::string("Verweis (Datei): ") + fixture + " fuehrt zu " + code);
            a.render(cv);
            press(a, {Button::Circle});
            CHECK(a.screen() == App::Screen::Detail, std::string("Verweis (Datei): ") + fixture + ": zurueck zum Befund");
            CHECK(full_tree(t.dir) == before, std::string("Verweis (Datei): ") + fixture + ": nichts geschrieben");
        }
    }
    CHECK(Strings::missing().empty(), "Verweis (Datei): keine fehlenden Texte");
    Strings::missing().clear();
}

// ---- Quelle als QR, Vermerk fuer den Bericht, Kontext, Scrollen, Update der Fehlercodes ------------------------------------------

void error_source_tests() {
    FakeService svc;
    svc.errors = mk_errors(12);
    svc.errors.codes[2].source = "https://example.org/" + std::string(3200, 'x');  // zu lang fuer einen QR-Code
    Canvas cv(App::kWidth, App::kHeight);
    Strings::missing().clear();

    App a(svc);
    open_error_codes_via_ui(a);
    press(a, {Button::Cross});
    a.render(cv);
    const std::uint64_t detail = canvas_hash(cv);
    press(a, {Button::Square});
    CHECK(a.screen() == App::Screen::ErrorCodeSource && a.error_source_index() == 0, "Quelle: Quadrat zeigt die Hauptquelle als QR-Code");
    a.render(cv);
    CHECK(canvas_hash(cv) != detail, "Quelle: die Anzeige unterscheidet sich vom Eintrag");
    press(a, {Button::Left, Button::Right, Button::L1, Button::R1});
    CHECK(a.screen() == App::Screen::ErrorCodeSource && a.error_source_index() == 0, "Quelle: mit nur einer Quelle blaettern Links/Rechts nichts");
    press(a, {Button::Circle});
    CHECK(a.screen() == App::Screen::ErrorCodeDetail && a.selected_error_id() == svc.errors.codes[0].id, "Quelle: Kreis geht zum Eintrag zurueck");

    press(a, {Button::Right, Button::Square});  // Code 1 hat eine weitere Quelle
    CHECK(a.screen() == App::Screen::ErrorCodeSource && a.error_source_index() == 0, "Quelle: Code mit weiterer Quelle beginnt bei der ersten");
    a.render(cv);
    const std::uint64_t first = canvas_hash(cv);
    press(a, {Button::Right});
    CHECK(a.error_source_index() == 1, "Quelle: Rechts zeigt die weitere Quelle");
    a.render(cv);
    CHECK(canvas_hash(cv) != first, "Quelle: andere Adresse, anderer QR-Code");
    press(a, {Button::Right, Button::R1});
    CHECK(a.error_source_index() == 1, "Quelle: nach der letzten bleibt es stehen");
    press(a, {Button::Left});
    CHECK(a.error_source_index() == 0, "Quelle: Links geht zur vorigen");
    press(a, {Button::Right, Button::Circle, Button::Square});
    CHECK(a.screen() == App::Screen::ErrorCodeSource && a.error_source_index() == 0, "Quelle: beim erneuten Oeffnen wieder die erste");
    press(a, {Button::Circle});

    press(a, {Button::Right, Button::Square});  // Code 2: Adresse zu lang fuer den QR-Code
    CHECK(a.screen() == App::Screen::ErrorCodeSource, "Quelle: zu lange Adresse oeffnet die Ansicht trotzdem");
    a.render(cv);
    press(a, {Button::Circle});

    // Aus einem Befund heraus: Kreis geht Schritt fuer Schritt zurueck.
    FakeService fsvc;
    fsvc.errors = mk_errors(12);
    fsvc.result.findings = {finding_mentioning("a.known", "CE-100007-1")};
    App b(fsvc);
    run_check_via_ui(b, cv);
    press(b, {Button::Cross, Button::Square, Button::Square});
    CHECK(b.screen() == App::Screen::ErrorCodeSource, "Quelle: auch aus einem verlinkten Eintrag");
    press(b, {Button::Circle});
    CHECK(b.screen() == App::Screen::ErrorCodeDetail, "Quelle: Kreis zum verlinkten Eintrag");
    press(b, {Button::Circle});
    CHECK(b.screen() == App::Screen::Detail, "Quelle: danach zum Befund");

    // Beide Sprachen
    for (Lang l : {Lang::De, Lang::En}) {
        App c(svc, l);
        open_error_codes_via_ui(c);
        press(c, {Button::Cross, Button::Right, Button::Square, Button::Right});
        c.render(cv);
    }
    CHECK(Strings::missing().empty(), "Quelle: alle gezeichneten Texte existieren in beiden Sprachen (" + (Strings::missing().empty() ? std::string() : Strings::missing().front()) + ")");
    Strings::missing().clear();
}

void error_report_mark_tests() {
    FakeService svc;
    svc.errors = mk_errors(12);
    svc.result.findings = {mk_finding("a.info", Severity::Info, false)};
    Canvas cv(App::kWidth, App::kHeight);
    Strings::missing().clear();

    App a(svc);
    open_error_codes_via_ui(a);
    press(a, {Button::Cross});
    const std::string id0 = a.selected_error_id();
    a.render(cv);
    const std::uint64_t plain = canvas_hash(cv);
    press(a, {Button::Triangle});
    CHECK(a.reported_error() == id0 && svc.reported == id0 && svc.reported_calls == 1, "Bericht: Dreieck vermerkt den Code (nur im Dienst, es wird nichts geschrieben)");
    a.render(cv);
    CHECK(canvas_hash(cv) != plain, "Bericht: der Eintrag zeigt den Vermerk (andere Taste, andere Kopfzeile)");
    press(a, {Button::Right});
    CHECK(a.reported_error() == id0 && a.selected_error_id() != id0, "Bericht: der Vermerk bleibt beim Blaettern bestehen");
    a.render(cv);
    press(a, {Button::Left, Button::Triangle});
    CHECK(a.reported_error().empty() && svc.reported.empty() && svc.reported_calls == 2, "Bericht: noch einmal Dreieck nimmt den Vermerk zurueck");
    press(a, {Button::Triangle, Button::Right, Button::Triangle});
    CHECK(a.reported_error() == a.selected_error_id() && svc.reported == a.reported_error(), "Bericht: es gibt nur einen vermerkten Code, ein anderer ersetzt ihn");
    CHECK(svc.reports == 0, "Bericht: Vermerken schreibt keinen Bericht");

    // Berichtsbildschirm nennt den Code
    press(a, {Button::Circle, Button::Circle, Button::Up, Button::Up, Button::Up});
    run_check_via_ui(a, cv);
    press(a, {Button::Square});
    CHECK(a.screen() == App::Screen::Report, "Bericht: Bericht geoeffnet");
    a.render(cv);
    const std::uint64_t with_code = canvas_hash(cv);
    FakeService other;
    other.result = svc.result;
    App b(other);
    run_check_via_ui(b, cv);
    press(b, {Button::Square});
    b.render(cv);
    CHECK(with_code != canvas_hash(cv), "Bericht: der Bildschirm nennt den vermerkten Code");
    for (Lang l : {Lang::De, Lang::En}) {
        App c(svc, l);
        open_error_codes_via_ui(c);
        press(c, {Button::Cross, Button::Triangle});
        c.render(cv);
        press(c, {Button::Circle, Button::Circle, Button::Up, Button::Up, Button::Up});
        run_check_via_ui(c, cv);
        press(c, {Button::Square});
        c.render(cv);
    }
    CHECK(Strings::missing().empty(), "Bericht: alle gezeichneten Texte existieren in beiden Sprachen");
    Strings::missing().clear();
}

// Mit dem echten Dienst: der vermerkte Code steht im Bericht (Markdown und Kurzfassung), in beiden Sprachen.
void error_report_service_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    const ErrorCodeSet codes = load_error_codes_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/error_codes.json");
    TempCopy t("b8_pos");
    auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
    LocalService svc(*platform, rules, [] { return std::string("20261108T120000Z"); });
    svc.set_error_codes(codes);
    Canvas cv(App::kWidth, App::kHeight);
    App app(svc);
    open_error_codes_via_ui(app);
    for (int i = 0; i < 80 && app.selected_error_id() != "CE-100028-1"; ++i) press(app, {Button::Down});
    press(app, {Button::Cross, Button::Triangle});
    CHECK(app.reported_error() == "CE-100028-1", "Bericht (Dienst): CE-100028-1 vermerkt");
    press(app, {Button::Circle, Button::Circle, Button::Up, Button::Up, Button::Up});
    run_check_via_ui(app, cv);
    press(app, {Button::Square});
    CHECK(app.screen() == App::Screen::Report, "Bericht (Dienst): Bericht erstellt");
    const std::string path = t.dir.string() + "/fs" + report_path_for("20261108T120000Z");
    const std::string de = slurp(path);
    CHECK(de.find("## Gemeldeter Fehlercode") != std::string::npos && de.find("`CE-100028-1`") != std::string::npos && de.find("Nicht genug freier Platz auf der SSD") != std::string::npos,
          "Bericht (Dienst): der deutsche Bericht nennt Code und Titel");
    const ReportResult en = svc.make_report(ReportLang::En);
    CHECK(slurp(path).find("## Reported error code") != std::string::npos && slurp(path).find("Not enough free space on the SSD") != std::string::npos,
          "Bericht (Dienst): der englische Bericht nennt Code und Titel");
    CHECK(en.qr_text.find("Error code CE-100028-1") != std::string::npos, "Bericht (Dienst): die Kurzfassung fuer den QR-Code nennt den Code");
    CHECK(svc.make_report(ReportLang::De).qr_text.find("Fehlercode CE-100028-1") != std::string::npos, "Bericht (Dienst): Kurzfassung deutsch");

    // Zurueckgenommen: nichts davon im Bericht
    press(app, {Button::Circle, Button::Circle});  // Report -> Findings -> Start
    open_error_codes_via_ui(app);
    for (int i = 0; i < 80 && app.selected_error_id() != "CE-100028-1"; ++i) press(app, {Button::Down});
    press(app, {Button::Cross, Button::Triangle});
    CHECK(app.reported_error().empty(), "Bericht (Dienst): Vermerk zurueckgenommen");
    const ReportResult none = svc.make_report(ReportLang::De);
    CHECK(none.qr_text.find("Fehlercode") == std::string::npos && slurp(path).find("Gemeldeter Fehlercode") == std::string::npos, "Bericht (Dienst): ohne Vermerk steht kein Fehlercode im Bericht");

    // Unbekannte Kennung (z. B. Datei ausgetauscht): ignoriert
    svc.set_reported_error("CE-999999-9");
    CHECK(svc.make_report(ReportLang::De).qr_text.find("CE-999999-9") == std::string::npos, "Bericht (Dienst): eine unbekannte Kennung kommt nicht in den Bericht");
}

// "Was Medic gerade sieht": aus den Fakten des letzten Check-ups, nur lesend.
void error_context_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    const ErrorCodeSet codes = load_error_codes_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/error_codes.json");
    auto kinds = [](const CodeContext& c) {
        std::vector<std::string> v;
        for (const auto& l : c.lines) v.push_back(l.kind);
        return v;
    };
    auto line = [](const CodeContext& c, const std::string& kind) -> const ContextLine* {
        for (const auto& l : c.lines)
            if (l.kind == kind) return &l;
        return nullptr;
    };
    auto context_for = [&](const std::string& fixture, const std::string& id, bool run_check) {
        TempCopy t(fixture);
        auto platform = make_snapshot_platform(t.dir.string());
        LocalService svc(*platform, rules);
        svc.set_error_codes(codes);
        if (run_check) svc.run_check();
        return svc.code_context(*codes.find(id));
    };

    {  // vor dem ersten Check-up gibt es nichts zu zeigen
        const CodeContext c = context_for("g1_pos", "CE-107520-5", false);
        CHECK(!c.available && c.lines.empty(), "Kontext: ohne Check-up nichts verfuegbar");
    }
    {  // Eintraege ohne context zeigen nie etwas
        const CodeContext c = context_for("g1_pos", "CE-108255-1", true);
        CHECK(!c.available && c.lines.empty(), "Kontext: Eintrag ohne context bleibt leer");
    }
    {  // Firmware und Titel, die eine neuere Firmware verlangen
        const CodeContext c = context_for("g1_pos", "CE-107520-5", true);
        CHECK(c.available && kinds(c) == std::vector<std::string>({"fw", "need_backport"}), "Kontext: Spiel-Code zeigt Firmware und betroffene Titel");
        const ContextLine* f = line(c, "fw");
        const ContextLine* n = line(c, "need_backport");
        CHECK(f && f->args.at("fw") == "12.00" && n && n->args.at("list") == "PPSA00001 (13.60)" && n->args.at("fw") == "12.00", "Kontext: 12.00 und PPSA00001 (13.60)");
        const CodeContext none = context_for("g1_neg", "CE-107520-5", true);
        CHECK(kinds(none) == std::vector<std::string>({"fw", "need_backport_none"}), "Kontext: ohne betroffene Titel steht das da");
        const CodeContext bp = context_for("g1_neg_backport", "CE-107520-5", true);
        CHECK(kinds(bp) == std::vector<std::string>({"fw", "need_backport_none"}), "Kontext: mit Backport-Ordner gilt der Titel als versorgt");
    }
    {  // Speicher: freier Platz je grossem Laufwerk
        const CodeContext c = context_for("g1_pos", "CE-100028-1", true);
        const ContextLine* s = line(c, "storage");
        CHECK(c.available && s && s->args.at("mount") == "/data" && s->args.at("free") == "465.7 GiB" && s->args.at("total") == "838.2 GiB" && s->args.at("pct") == "56",
              "Kontext: freier Platz /data mit GiB und Prozent");
    }
    {  // Laufwerke: nur echte USB-/Erweiterungs-Laufwerke, Platzhalter (winzige tmpfs) zaehlen nicht
        for (bool ro : {false, true}) {
            TempCopy t("g1_neg");
            json sys = {{"fw", "12.00"}, {"model", "CFI-TEST"},
                        {"storage", json::array({{{"mount", "/data"}, {"total", 900000000000LL}, {"free", 500000000000LL}},
                                                 {{"mount", "/mnt/usb0"}, {"total", 512000000000LL}, {"free", 100000000000LL}, {"fstype", "exfatfs"}, {"ro", ro}},
                                                 {{"mount", "/mnt/usb1"}, {"total", 2097152}, {"free", 0}, {"fstype", "tmpfs"}}})}};
            std::ofstream(t.dir / "system.json", std::ios::binary | std::ios::trunc) << sys.dump();
            auto platform = make_snapshot_platform(t.dir.string());
            LocalService svc(*platform, rules);
            svc.set_error_codes(codes);
            svc.run_check();
            const CodeContext c = svc.code_context(*codes.find("CE-109737-7"));
            const ContextLine* d = line(c, ro ? "drive_ro" : "drive");
            CHECK(c.lines.size() == 1 && d && d->args.at("mount") == "/mnt/usb0" && d->args.at("fstype") == "exfatfs" && d->args.at("total") == "476.8 GiB",
                  std::string("Kontext: Laufwerk /mnt/usb0 exfatfs, ohne den Platzhalter") + (ro ? " (schreibgeschuetzt)" : ""));
        }
        const CodeContext none = context_for("g1_pos", "CE-100006-7", true);
        CHECK(kinds(none) == std::vector<std::string>({"drive_none"}), "Kontext: ohne Laufwerk steht das da");
    }
    {  // ShadowMount+: Version und Befund zum Speicherleck
        const CodeContext leak = context_for("smp10_pos_leak", "CE-108262-9", true);
        CHECK(kinds(leak) == std::vector<std::string>({"smp", "smp_leak"}) && line(leak, "smp")->args.at("v") == "1.7beta10", "Kontext: ShadowMount+ 1.7beta10 ist vom Leck betroffen");
        const CodeContext fine = context_for("smp10_neg_beta11", "CE-108262-9", true);
        CHECK(kinds(fine) == std::vector<std::string>({"smp", "smp_no_leak"}), "Kontext: 1.7beta11 ohne Befund zum Leck");
        const CodeContext missing = context_for("g1_pos", "CE-108262-9", true);
        CHECK(kinds(missing) == std::vector<std::string>({"smp_missing"}), "Kontext: ohne ShadowMount+-Version steht das da");
    }

    // Anzeige: mit Check-up andere Seite als ohne, in beiden Sprachen ohne fehlende Texte
    Strings::missing().clear();
    Canvas cv(App::kWidth, App::kHeight);
    for (Lang l : {Lang::De, Lang::En}) {
        TempCopy t("g1_pos");
        auto platform = make_snapshot_platform(t.dir.string());
        LocalService svc(*platform, rules);
        svc.set_error_codes(codes);
        App a(svc, l);
        open_error_codes_via_ui(a);
        for (int i = 0; i < 80 && a.selected_error_id() != "CE-107520-5"; ++i) press(a, {Button::Down});
        press(a, {Button::Cross});
        for (int i = 0; i < 40; ++i) a.handle(Button::Down);
        a.render(cv);
        const std::uint64_t without = canvas_hash(cv);
        press(a, {Button::Circle, Button::Circle, Button::Up, Button::Up, Button::Up});
        run_check_via_ui(a, cv);
        press(a, {Button::Circle});  // Findings -> Start
        open_error_codes_via_ui(a);
        for (int i = 0; i < 80 && a.selected_error_id() != "CE-107520-5"; ++i) press(a, {Button::Down});
        press(a, {Button::Cross});
        for (int i = 0; i < 40; ++i) a.handle(Button::Down);
        a.render(cv);
        CHECK(canvas_hash(cv) != without, "Kontext: nach dem Check-up zeigt der Eintrag den Zustand der Konsole");
    }
    CHECK(Strings::missing().empty(), "Kontext: alle gezeichneten Texte existieren in beiden Sprachen (" + (Strings::missing().empty() ? std::string() : Strings::missing().front()) + ")");
    Strings::missing().clear();
    {  // jede Kontextart hat in beiden Sprachen ihren Text
        for (const char* kind : {"fw", "storage", "storage_none", "drive", "drive_ro", "drive_none", "smp", "smp_missing", "smp_leak", "smp_no_leak", "need_backport", "need_backport_none"})
            for (Lang l : {Lang::De, Lang::En}) CHECK(Strings::get(l).has(std::string("ctx.") + kind), std::string("Kontext: Text ctx.") + kind);
    }
    Strings::missing().clear();
}

// Beim Halten von Hoch/Runter scrollt der Text schneller; einzelne Druecke bleiben eine Zeile.
void error_scroll_accel_tests() {
    FakeService svc;
    svc.errors = mk_errors(12);
    for (int k = 13; k <= 40; ++k) {  // ein sehr langer Eintrag, damit das Beschleunigen nicht am Ende haengen bleibt
        svc.errors.codes[0].steps_de.push_back("Schritt " + std::to_string(k) + ": noch ein Satz, der auf mehrere Zeilen umbricht und viel Text enthaelt, damit der Eintrag lang wird.");
        svc.errors.codes[0].steps_en.push_back("Step " + std::to_string(k) + ": another sentence that wraps onto several lines and contains a lot of text so that the entry gets long.");
    }
    auto open_long = [&](App& a) {
        open_error_codes_via_ui(a);
        press(a, {Button::Cross});
    };
    App probe(svc);
    open_long(probe);
    const int end = scroll_to_end(probe);
    CHECK(end >= 24, "Scrollen: der Pruefling ist lang genug (" + std::to_string(end) + ")");

    {  // einzeln, mit Pause dazwischen: genau eine Zeile je Druck
        App a(svc);
        open_long(a);
        for (int i = 0; i < 10; ++i) {
            a.handle(Button::Down);
            for (int k = 0; k < 30; ++k) a.update();
        }
        CHECK(a.error_scroll() == 10, "Scrollen: zehn einzelne Druecke = zehn Zeilen");
    }
    {  // gehalten (Wiederholung etwa alle 7 Bilder): 1, 1, 1, 1, dann 2, dann 3 ...
        App a(svc);
        open_long(a);
        for (int i = 0; i < 10; ++i) {
            a.handle(Button::Down);
            for (int k = 0; k < 7; ++k) a.update();
        }
        CHECK(a.error_scroll() == 18, "Scrollen: zehn Wiederholungen beschleunigen auf 18 Zeilen (" + std::to_string(a.error_scroll()) + ")");
        a.handle(Button::Up);
        CHECK(a.error_scroll() == 17, "Scrollen: eine Richtungsaenderung beginnt wieder mit einer Zeile");
        for (int k = 0; k < 30; ++k) a.update();
        a.handle(Button::Down);
        CHECK(a.error_scroll() == 18, "Scrollen: nach einer Pause wieder eine Zeile");
        for (int i = 0; i < 100; ++i) {
            a.handle(Button::Down);
            for (int k = 0; k < 7; ++k) a.update();
        }
        CHECK(a.error_scroll() == end, "Scrollen: beschleunigt, aber nie ueber das Ende hinaus");
        for (int i = 0; i < 100; ++i) {
            a.handle(Button::Up);
            for (int k = 0; k < 7; ++k) a.update();
        }
        CHECK(a.error_scroll() == 0, "Scrollen: beschleunigt zurueck bis ganz oben, nie darueber");
    }
}

// ---- Update der Fehlercode-Datei im Bildschirm Regel-Update ---------------------------------------------------------------------

void codes_update_flow_tests() {
    Canvas cv(App::kWidth, App::kHeight);
    Strings::missing().clear();
    auto go_to_update = [](App& a) { press(a, {Button::Down, Button::Down, Button::Cross, Button::Down, Button::Cross}); };
    auto ready = [](const std::string& v, std::size_t n) {
        RuleUpdateOffer o;
        o.state = RuleUpdateOffer::State::Ready;
        o.new_version = v;
        o.new_rule_count = n;
        o.auth = "signature";
        o.signed_by = "0123456789abcdef";
        o.sha256 = std::string(64, 'a');
        return o;
    };
    FakeService base;
    base.errors = mk_errors(5);
    base.result.findings = {mk_finding("a.crit", Severity::Critical, false)};

    {  // nur Fehlercodes bereit
        FakeService svc = base;
        svc.codes_offer = ready("2026.12.10", 7);
        svc.errors_after_codes_install = mk_errors(3);
        App a(svc);
        run_check_via_ui(a, cv);
        press(a, {Button::Circle});
        go_to_update(a);
        CHECK(a.screen() == App::Screen::RulesUpdate, "Codes-Flow: Regel-Update offen");
        a.render(cv);
        press(a, {Button::Cross});
        CHECK(svc.codes_installs == 1 && svc.installs == 0, "Codes-Flow: Kreuz installiert nur die Fehlercodes");
        a.render(cv);
        press(a, {Button::Cross});
        CHECK(a.screen() == App::Screen::Settings, "Codes-Flow: Kreuz kommt zurueck");
        press(a, {Button::Circle});
        CHECK(a.screen() == App::Screen::Start && a.finding_count() == 1, "Codes-Flow: Fehlercodes aendern die Befunde nicht");
        open_error_codes_via_ui(a);
        CHECK(a.selected_error_id() == svc.errors_after_codes_install.codes[0].id && a.error_selected() == 0, "Codes-Flow: die neue Liste gilt sofort");
    }
    {  // beides bereit
        FakeService svc = base;
        svc.offer = ready("2026.12.20", 60);
        svc.codes_offer = ready("2026.12.10", 7);
        App a(svc);
        run_check_via_ui(a, cv);
        CHECK(a.finding_count() == 1, "Codes-Flow: Befund vorhanden");
        press(a, {Button::Circle});
        go_to_update(a);
        a.render(cv);
        press(a, {Button::Cross});
        CHECK(svc.installs == 1 && svc.codes_installs == 1 && a.finding_count() == 0, "Codes-Flow: Regeln und Fehlercodes zusammen, die alten Befunde sind danach veraltet");
        a.render(cv);
    }
    {  // Regeln gehen, Fehlercodes scheitern: Fehlerbildschirm, Regeln gelten trotzdem
        FakeService svc = base;
        svc.offer = ready("2026.12.20", 60);
        svc.codes_offer = ready("2026.12.10", 7);
        svc.codes_install_result = {false, "write_failed", "", ""};
        App a(svc);
        run_check_via_ui(a, cv);
        press(a, {Button::Circle});
        go_to_update(a);
        press(a, {Button::Cross});
        a.render(cv);
        CHECK(svc.installs == 1 && svc.codes_installs == 1 && a.finding_count() == 0, "Codes-Flow: gescheiterte Fehlercodes machen die installierten Regeln nicht rueckgaengig");
        press(a, {Button::Cross});
        CHECK(a.screen() == App::Screen::Settings, "Codes-Flow: Fehlerbildschirm schliesst mit Kreuz");
    }
    {  // Regeln scheitern, Fehlercodes gehen
        FakeService svc = base;
        svc.offer = ready("2026.12.20", 60);
        svc.install_result = {false, "verify_failed", "", ""};
        svc.codes_offer = ready("2026.12.10", 7);
        App a(svc);
        run_check_via_ui(a, cv);
        press(a, {Button::Circle});
        go_to_update(a);
        press(a, {Button::Cross});
        a.render(cv);
        CHECK(a.finding_count() == 1, "Codes-Flow: scheiternde Regeln lassen die Befunde stehen");
    }
    {  // Fehlercodes abgelehnt: angezeigt, nichts zu installieren
        FakeService svc = base;
        svc.codes_offer.state = RuleUpdateOffer::State::Rejected;
        svc.codes_offer.reason = "sig_invalid";
        svc.codes_offer.old_version = "2026.11.10";
        App a(svc);
        go_to_update(a);
        a.render(cv);
        press(a, {Button::Cross});
        CHECK(svc.codes_installs == 0 && svc.installs == 0 && a.screen() == App::Screen::RulesUpdate, "Codes-Flow: abgelehnte Datei wird nicht installiert");
    }
    {  // Zuruecknehmen: nur wo es etwas gibt
        FakeService svc = base;
        svc.codes_offer.can_rollback = true;
        svc.codes_offer.installed_version = "2026.12.10";
        App a(svc);
        go_to_update(a);
        a.render(cv);
        press(a, {Button::Triangle});
        a.render(cv);
        press(a, {Button::Cross});
        CHECK(svc.codes_rollbacks == 1 && svc.rollbacks == 0, "Codes-Flow: Rollback nur fuer die Fehlercodes");
        a.render(cv);
        FakeService both = base;
        both.offer.can_rollback = both.codes_offer.can_rollback = true;
        both.offer.installed_version = both.codes_offer.installed_version = "2026.12.10";
        App b(both);
        go_to_update(b);
        press(b, {Button::Triangle, Button::Cross});
        CHECK(both.rollbacks == 1 && both.codes_rollbacks == 1, "Codes-Flow: sind beide zuruecknehmbar, wird beides zurueckgenommen");
    }
    {  // nichts da: die Anleitung wie bisher
        FakeService svc = base;
        App a(svc);
        go_to_update(a);
        a.render(cv);
        press(a, {Button::Cross});
        CHECK(a.screen() == App::Screen::RulesUpdate && svc.installs == 0 && svc.codes_installs == 0, "Codes-Flow: ohne Angebot tut Kreuz nichts");
    }
    {  // Download: Fehlercodes nur teilweise geholt
        FakeService svc = base;
        svc.url = "http://192.0.2.10:8000/regeln/";
        svc.download_result.ok = true;
        svc.download_result.codes_error = "http_status";
        svc.download_result.codes_file = "error_codes.json.sig";
        svc.offer = ready("2026.12.20", 60);
        App a(svc);
        go_to_update(a);
        press(a, {Button::Square, Button::Cross});
        a.render(cv);
        a.update();
        a.render(cv);
        CHECK(a.screen() == App::Screen::RulesUpdate && svc.downloads == 1, "Codes-Flow: Download gelaufen");
        for (const char* err : {"net_timeout", "write_failed"}) {
            svc.download_result.codes_error = err;
            press(a, {Button::Circle, Button::Down, Button::Cross, Button::Square, Button::Cross});
            a.render(cv);
            a.update();
            a.render(cv);
        }
    }
    CHECK(Strings::missing().empty(), "Codes-Flow: alle gezeichneten Texte existieren in beiden Sprachen (" + (Strings::missing().empty() ? std::string() : Strings::missing().front()) + ")");
    Strings::missing().clear();
    for (Lang l : {Lang::De, Lang::En}) {  // in beiden Sprachen
        FakeService svc = base;
        svc.offer = ready("2026.12.20", 60);
        svc.codes_offer = ready("2026.12.10", 7);
        svc.codes_offer.can_rollback = svc.offer.can_rollback = true;
        App a(svc, l);
        go_to_update(a);
        a.render(cv);
        press(a, {Button::Triangle});
        a.render(cv);
        press(a, {Button::Circle, Button::Cross});
        a.render(cv);
        press(a, {Button::Cross});
        a.render(cv);
    }
    CHECK(Strings::missing().empty(), "Codes-Flow: auch auf Englisch");
    Strings::missing().clear();
}

// Echter Dienst, echte Dateien: Datei-Update, Rollback und Download mit Signatur ueber die Oberflaeche.
void codes_update_service_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    const std::string codes_path = std::string(MEDIC_SOURCE_DIR) + "/knowledge/error_codes.json";
    const ErrorCodeSet shipped = load_error_codes_from_file(codes_path);
    json cj = json::parse(slurp(codes_path));
    cj["db_version"] = "2026.12.31";
    const std::string new_text = cj.dump(2) + "\n";
    auto spit = [](const fs::path& p, const std::string& t) {
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary | std::ios::trunc) << t;
    };
    auto clock = [] { return std::string("20261109T170000Z"); };
    auto go_to_update = [](App& a) { press(a, {Button::Down, Button::Down, Button::Cross, Button::Down, Button::Cross}); };
    Canvas cv(App::kWidth, App::kHeight);
    Strings::missing().clear();

    {  // Datei-Update mit Pruefsumme, danach Rollback
        TempCopy t("b2_pos");
        spit(t.dir / "fs/data/ps5medic/knowledge/update/error_codes.json", new_text);
        spit(t.dir / "fs/data/ps5medic/knowledge/update/error_codes.json.sha256", sha256_hex(new_text) + "  error_codes.json\n");
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, clock);
        svc.set_error_codes(shipped);
        const auto offer = svc.check_codes_update();
        CHECK(offer.state == RuleUpdateOffer::State::Ready && offer.new_version == "2026.12.31" && offer.old_version == shipped.db_version && offer.old_rule_count == shipped.codes.size(),
              "Codes-E2E: Angebot kennt alte und neue Fehlercodes");
        CHECK(svc.check_rule_update().state == RuleUpdateOffer::State::None, "Codes-E2E: das Regel-Angebot bleibt leer");
        App a(svc);
        go_to_update(a);
        a.render(cv);
        press(a, {Button::Cross});
        a.render(cv);
        CHECK(slurp(t.dir / "fs/data/ps5medic/knowledge/error_codes.json") == new_text && svc.error_codes().db_version == "2026.12.31", "Codes-E2E: Kreuz installiert, die neuen Fehlercodes gelten sofort");
        CHECK(!fs::exists(t.dir / "fs/data/ps5medic/knowledge/rules.json"), "Codes-E2E: die Regeln bleiben unberuehrt");
        press(a, {Button::Cross, Button::Down, Button::Cross});  // zurueck, wieder oeffnen
        CHECK(svc.check_codes_update().can_rollback, "Codes-E2E: Rollback moeglich");
        a.render(cv);
        press(a, {Button::Triangle});
        a.render(cv);
        press(a, {Button::Cross});
        a.render(cv);
        CHECK(!fs::exists(t.dir / "fs/data/ps5medic/knowledge/error_codes.json") && svc.error_codes().db_version == shipped.db_version, "Codes-E2E: Rollback legt die Datei beiseite, die mitgelieferten Fehlercodes gelten wieder");
    }
    {  // Download mit Signatur: Regeln und Fehlercodes zusammen
        json nj = json::parse(slurp(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json"));
        nj["db_version"] = "2026.12.30";
        const std::string rules_text = nj.dump(2) + "\n";
        medic::Ed25519Seed seed;
        seed.fill(0x66);
        const medic::Ed25519KeyPair key = medic::ed25519_keypair_from_seed(seed);
        const std::vector<medic::TrustedKey> trusted = {medic::make_trusted_key(key.pk)};
        const std::string base = "http://192.0.2.10:8000/regeln/";
        TempCopy t("b2_pos");
        spit(t.dir / "fs/data/ps5medic/config.ini", "update_url=" + base + "\n");
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, clock);
        svc.set_error_codes(shipped);
        ScriptedHttp http;
        http.files[base + "rules.json"] = rules_text;
        http.files[base + "rules.json.sig"] = medic::make_signature_file(key, rules_text);
        http.files[base + "error_codes.json"] = new_text;
        http.files[base + "error_codes.json.sig"] = medic::make_signature_file(key, new_text);
        svc.set_http_client(&http);
        svc.set_trusted_keys(trusted);
        App a(svc);
        go_to_update(a);
        press(a, {Button::Square});
        a.render(cv);
        CHECK(http.asked.empty(), "Codes-E2E: die Rueckfrage allein benutzt das Netz nicht");
        press(a, {Button::Cross});
        a.render(cv);
        a.update();
        CHECK(http.asked.size() == 4 && http.asked[2] == base + "error_codes.json" && http.asked[3] == base + "error_codes.json.sig", "Codes-E2E: vier Abrufe, nur zur eingetragenen Adresse");
        a.render(cv);
        CHECK(svc.check_codes_update().state == RuleUpdateOffer::State::Ready && svc.check_codes_update().auth == "signature", "Codes-E2E: die geholten Fehlercodes sind mit Signatur geprueft");
        press(a, {Button::Cross});
        a.render(cv);
        CHECK(svc.run_check().rules_version == "2026.12.30" && svc.error_codes().db_version == "2026.12.31", "Codes-E2E: ein Kreuz installiert beides, beide gelten sofort");
        CHECK(slurp(t.dir / "fs/data/ps5medic/knowledge/error_codes.json") == new_text, "Codes-E2E: die Datei liegt im Wissensordner");
    }
    {  // untergeschobene Datei: ungueltige Signatur wird abgelehnt, nichts installiert
        medic::Ed25519Seed seed;
        seed.fill(0x66);
        const medic::Ed25519KeyPair key = medic::ed25519_keypair_from_seed(seed);
        medic::Ed25519Seed other_seed;
        other_seed.fill(0x77);
        const medic::Ed25519KeyPair other = medic::ed25519_keypair_from_seed(other_seed);
        const std::string base = "http://192.0.2.10:8000/regeln/";
        json nj = json::parse(slurp(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json"));
        nj["db_version"] = "2026.12.30";
        const std::string rules_text = nj.dump(2) + "\n";
        TempCopy t("b2_pos");
        spit(t.dir / "fs/data/ps5medic/config.ini", "update_url=" + base + "\n");
        auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
        LocalService svc(*platform, rules, clock);
        svc.set_error_codes(shipped);
        ScriptedHttp http;
        http.files[base + "rules.json"] = rules_text;
        http.files[base + "rules.json.sig"] = medic::make_signature_file(key, rules_text);
        http.files[base + "error_codes.json"] = new_text;
        http.files[base + "error_codes.json.sig"] = medic::make_signature_file(other, new_text);  // falscher Schluessel
        svc.set_http_client(&http);
        svc.set_trusted_keys({medic::make_trusted_key(key.pk)});
        App a(svc);
        go_to_update(a);
        press(a, {Button::Square, Button::Cross});
        a.render(cv);
        a.update();
        a.render(cv);
        CHECK(svc.check_codes_update().state == RuleUpdateOffer::State::Rejected && svc.check_codes_update().reason == "sig_unknown_key", "Codes-E2E: Fehlercodes mit fremdem Schluessel werden abgelehnt");
        CHECK(svc.check_rule_update().state == RuleUpdateOffer::State::Ready, "Codes-E2E: die gueltig signierten Regeln bleiben angeboten");
        press(a, {Button::Cross});
        a.render(cv);
        CHECK(svc.error_codes().db_version == shipped.db_version && !fs::exists(t.dir / "fs/data/ps5medic/knowledge/error_codes.json"), "Codes-E2E: die abgelehnte Datei wird nicht installiert");
    }
    CHECK(Strings::missing().empty(), "Codes-E2E: keine fehlenden Texte");
    Strings::missing().clear();
}

// Jeder Text der UI (beide Sprachen) besteht nur aus Zeichen, die es in der eingebauten Schrift gibt.
void i18n_glyph_tests() {
    std::string missing;
    std::set<std::string> glyphs;
    for (Lang l : {Lang::De, Lang::En}) {
        const auto& S = Strings::get(l);
        for (const auto& k : S.keys()) {
            const std::string s = S(k);
            for (std::size_t i = 0; i < s.size();) {
                const std::size_t start = i;
                next_codepoint(s, i);
                glyphs.insert(s.substr(start, i - start));
            }
        }
    }
    for (const auto& g : glyphs)
        for (auto id : {Font::Id::Small, Font::Id::Body})
            if (g != " " && glyph_missing(Font::get(id), g)) missing += g + " ";
    CHECK(missing.empty(), "i18n: alle Zeichen der Texte gibt es in der Schrift (fehlen: " + missing + ")");
}

// Ohne Befunde heisst "keine Befunde" nur dann "in Ordnung", wenn alle Pruefbereiche gelesen wurden (gruener Titel); sonst neutral und mit der Liste.
void coverage_ui_tests() {
    const Color ok_green = rgb(76, 208, 138);  // kOk in app.cpp
    auto has_color = [](const Canvas& c, Color col) {
        for (int y = 0; y < c.height(); y += 2)
            for (int x = 0; x < c.width(); x += 2)
                if (c.at(x, y) == col) return true;
        return false;
    };
    auto screen_after_check = [&](bool all_tested, Canvas& cv, Canvas& start_cv) {
        FakeService svc;
        svc.result.fw = "12.00";
        svc.result.model = "CFI-7021";
        svc.result.rules_version = "t";
        svc.result.coverage = {{"boot_log", "Startverlauf", "Boot history", true}, {"error_history", "Fehlerverlauf", "Error history", all_tested}};
        App app(svc);
        run_check_via_ui(app, cv);
        CHECK(app.screen() == App::Screen::Findings && app.finding_count() == 0, "Abdeckung: Check ohne Befunde");
        app.render(cv);
        press(app, {Button::Circle});
        app.render(start_cv);
    };
    Canvas clean(App::kWidth, App::kHeight), clean_start(App::kWidth, App::kHeight), partial(App::kWidth, App::kHeight), partial_start(App::kWidth, App::kHeight);
    screen_after_check(true, clean, clean_start);
    screen_after_check(false, partial, partial_start);
    CHECK(has_color(clean, ok_green) && has_color(clean_start, ok_green), "Abdeckung: voll gedeckt -> gruener Titel und gruene Startzeile");
    CHECK(!has_color(partial, ok_green) && !has_color(partial_start, ok_green), "Abdeckung: nicht alles gelesen -> nichts in Gruen (kein 'in Ordnung')");
    bool differs = false;
    for (int y = 0; y < partial.height() && !differs; y += 2)
        for (int x = 0; x < partial.width(); x += 2)
            if (partial.at(x, y) != clean.at(x, y)) {
                differs = true;
                break;
            }
    CHECK(differs, "Abdeckung: die Befundliste zeigt den Unterschied");
}

// Nach einer erfolgreichen Reparatur prueft die App erneut und sagt, ob der Befund noch gemeldet wird (Orange = weiterhin gemeldet).
void redetect_ui_tests() {
    const Color warn = rgb(255, 179, 71);  // kWarn in app.cpp
    auto has_color = [](const Canvas& c, Color col) {
        for (int y = 0; y < c.height(); y += 2)
            for (int x = 0; x < c.width(); x += 2)
                if (c.at(x, y) == col) return true;
        return false;
    };
    for (const bool fixed : {true, false}) {
        FakeService svc;
        svc.result.fw = "12.00";
        svc.result.model = "CFI-7021";
        svc.result.rules_version = "t";
        svc.result.findings = {mk_finding("a.crit", Severity::Critical, true)};
        svc.plan = mk_plan();
        svc.plan.rule_id = "a.crit";
        Canvas cv(App::kWidth, App::kHeight);
        App app(svc);
        run_check_via_ui(app, cv);
        press(app, {Button::Triangle, Button::Cross});
        const std::string tag = fixed ? "Re-Detect (behoben): " : "Re-Detect (besteht weiter): ";
        CHECK(app.screen() == App::Screen::FixDone && svc.applies == 1 && svc.checks == 1, tag + "Reparatur angewendet, noch keine zweite Pruefung");
        app.render(cv);
        CHECK(svc.checks == 1 && !has_color(cv, warn), tag + "'Pruefe erneut' ist gezeichnet, die Pruefung laeuft erst danach");
        if (fixed) svc.result.findings.clear();  // die Konsole meldet den Befund nach der Reparatur nicht mehr
        app.update();
        CHECK(svc.checks == 2 && app.screen() == App::Screen::FixDone, tag + "erneute Pruefung genau einmal");
        app.render(cv);
        CHECK(has_color(cv, warn) == !fixed, tag + (fixed ? "kein Orange: nichts mehr gemeldet" : "Orange: wird weiterhin gemeldet"));
        app.update();
        CHECK(svc.checks == 2, tag + "keine weitere Pruefung");
        press(app, {Button::Cross});
        CHECK(app.screen() == App::Screen::Findings && svc.checks == 2 && app.finding_count() == (fixed ? 0 : 1), tag + "Kreuz zeigt die frische Befundliste ohne neue Pruefung");
    }
}

// Die Detailansicht nennt die Verlaesslichkeit des Befunds: Gruen = hoch, Orange = niedrig, dazu der Grund der Regel.
void confidence_ui_tests() {
    const Color ok = rgb(76, 208, 138), warn = rgb(255, 179, 71);  // kOk, kWarn in app.cpp
    auto has_color = [](const Canvas& c, Color col) {
        for (int y = 0; y < c.height(); y += 1)
            for (int x = 0; x < c.width(); x += 2)
                if (c.at(x, y) == col) return true;
        return false;
    };
    auto detail = [&](const Confidence& conf, Canvas& cv) {
        FakeService svc;
        svc.result.fw = "12.00";
        svc.result.model = "CFI-7021";
        svc.result.rules_version = "t";
        Finding f = mk_finding("a.info", Severity::Info, false);  // Hinweis ohne Reparatur: keine anderen gruenen/orangen Stellen
        f.confidence = conf;
        svc.result.findings = {f};
        App app(svc);
        run_check_via_ui(app, cv);
        press(app, {Button::Cross});
        CHECK(app.screen() == App::Screen::Detail, "Verlaesslichkeit: Details offen");
        app.render(cv);
    };
    Canvas high(App::kWidth, App::kHeight), low(App::kWidth, App::kHeight), mid(App::kWidth, App::kHeight), mid_reason(App::kWidth, App::kHeight);
    detail({"high", "doc", "", ""}, high);
    detail({"low", "derived", "", ""}, low);
    detail({"medium", "community", "", ""}, mid);
    detail({"medium", "community", "Das ist der Grund dieser Regel fuer die Verlaesslichkeit.", "This is the rule's reason for the confidence."}, mid_reason);
    CHECK(has_color(high, ok) && !has_color(high, warn), "Verlaesslichkeit: hoch in Gruen");
    CHECK(has_color(low, warn) && !has_color(low, ok), "Verlaesslichkeit: niedrig in Orange");
    CHECK(!has_color(mid, ok) && !has_color(mid, warn), "Verlaesslichkeit: mittel in neutraler Farbe");
    auto differs = [](const Canvas& a, const Canvas& b) {
        for (int y = 0; y < a.height(); y += 2)
            for (int x = 0; x < a.width(); x += 2)
                if (a.at(x, y) != b.at(x, y)) return true;
        return false;
    };
    CHECK(differs(mid, mid_reason), "Verlaesslichkeit: der Grund der Regel erscheint in den Details");
    CHECK(differs(high, low), "Verlaesslichkeit: Stufen sind unterscheidbar");
}

// Verlauf: der Dienst schreibt jede Pruefung in history.jsonl und vergleicht mit der letzten; die Details zeigen Dringlichkeit und Verlauf.
void history_ui_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    TempCopy t("b8_pos");
    auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
    int tick = 0;
    LocalService svc(*platform, rules, [&] { return std::string("20261111T1") + std::to_string(10 + tick++) + "00Z"; });
    const auto r1 = svc.run_check();
    CHECK(!r1.findings.empty() && r1.findings[0].status.empty() && r1.resolved.empty(), "Verlauf: erste Pruefung ohne Vergleich");
    const auto r2 = svc.run_check();
    CHECK(r2.findings.size() == r1.findings.size() && r2.findings[0].status == "recurring" && r2.findings[0].seen_runs == 2, "Verlauf: zweite Pruefung meldet denselben Befund als wiederkehrend");
    CHECK(load_history(platform->fs()).size() == 2, "Verlauf: beide Pruefungen stehen in history.jsonl");
    const auto rep = svc.make_report(ReportLang::De);
    CHECK(rep.saved, "Verlauf: Bericht gespeichert");
    std::string body;
    platform->fs().read_file(rep.path, body);
    CHECK(body.find("Verlauf: seit 2026-11-11 (2 Prüfungen in Folge)") != std::string::npos, "Verlauf: der Bericht nennt die Folge");

    auto differs = [](const Canvas& a, const Canvas& b) {
        for (int y = 0; y < a.height(); y += 2)
            for (int x = 0; x < a.width(); x += 2)
                if (a.at(x, y) != b.at(x, y)) return true;
        return false;
    };
    auto detail = [&](const std::string& status, int runs, Canvas& cv) {
        FakeService fk;
        fk.result.fw = "12.00";
        fk.result.model = "CFI-7021";
        fk.result.rules_version = "t";
        Finding f = mk_finding("a.warn", Severity::Warning, false);
        f.status = status;
        f.first_seen = "20261105T080000Z";
        f.seen_runs = runs;
        fk.result.findings = {f};
        App app(fk);
        run_check_via_ui(app, cv);
        press(app, {Button::Cross});
        app.render(cv);
    };
    Canvas none(App::kWidth, App::kHeight), fresh(App::kWidth, App::kHeight), rec(App::kWidth, App::kHeight);
    detail("", 0, none);
    detail("new", 1, fresh);
    detail("recurring", 4, rec);
    CHECK(differs(none, fresh) && differs(fresh, rec) && differs(none, rec), "Verlauf: die Details zeigen neu und wiederkehrend verschieden");
    {  // vorlaeufig: confirm_runs 2 bei einer Pruefung sieht anders aus als sofort bestaetigt
        auto pending = [&](int confirm, Canvas& cv) {
            FakeService fk;
            fk.result.fw = "12.00";
            fk.result.model = "CFI-7021";
            fk.result.rules_version = "t";
            Finding f = mk_finding("a.info", Severity::Info, false);
            f.status = "new";
            f.seen_runs = 1;
            f.confirm_runs = confirm;
            fk.result.findings = {f};
            App app(fk);
            run_check_via_ui(app, cv);
            press(app, {Button::Cross});
            app.render(cv);
        };
        Canvas sure(App::kWidth, App::kHeight), prov(App::kWidth, App::kHeight);
        pending(1, sure);
        pending(2, prov);
        CHECK(differs(sure, prov), "Verlauf: ein vorlaeufiger Befund wird in den Details als vorlaeufig gezeigt");
    }
}

// Selbsttest in der UI: Liste, Hinweis vor dem Lauf, ein Test je update(), Abbruch nach dem laufenden Test, nur Schnelltests.
// Nutzer-Tests in der UI: Pad-Tasten und Anzeige-Muster; Ergebnisse gehen an den Dienst, nie ohne Zutun als "bestanden".
void usertest_ui_tests() {
    Canvas cv(App::kWidth, App::kHeight);
    auto open_list = [&](App& a) { press(a, {Button::Down, Button::Down, Button::Down, Button::Down, Button::Cross}); };
    {   // alle elf Tasten = bestanden
        FakeService svc;
        App a(svc);
        open_list(a);
        press(a, {Button::L1});
        CHECK(a.screen() == App::Screen::UserTest, "Nutzer-Test: L1 oeffnet den Pad-Test");
        a.render(cv);
        press(a, {Button::Up, Button::Down, Button::Left, Button::Right, Button::Cross, Button::Square, Button::Triangle, Button::L1, Button::R1, Button::Options});
        CHECK(a.screen() == App::Screen::UserTest && svc.recorded.empty(), "Nutzer-Test: zehn Tasten reichen nicht (Kreis fehlt noch)");
        press(a, {Button::Circle});
        CHECK(a.screen() == App::Screen::SelfTest && svc.recorded.size() == 1 && svc.recorded[0].id == "hw.pad.buttons" && svc.recorded[0].status == TestStatus::Passed, "Nutzer-Test: alle elf Tasten = bestanden, eingetragen");
    }
    {   // vorzeitig beenden: Kreis zweimal
        FakeService svc;
        App a(svc);
        open_list(a);
        press(a, {Button::L1, Button::Up, Button::Down, Button::Left, Button::Circle, Button::Circle});
        CHECK(a.screen() == App::Screen::SelfTest && svc.recorded.size() == 1 && svc.recorded[0].status == TestStatus::Warn && svc.recorded[0].reason == "buttons_missing", "Nutzer-Test: fehlende Tasten = Warnung (buttons_missing)");
    }
    {   // kaum getestet = uebersprungen, nie bestanden
        FakeService svc;
        App a(svc);
        open_list(a);
        press(a, {Button::L1, Button::Circle, Button::Circle});
        CHECK(svc.recorded.size() == 1 && svc.recorded[0].status == TestStatus::Skipped && svc.recorded[0].reason == "not_tested", "Nutzer-Test: fast nichts gedrueckt = skipped (not_tested)");
    }
    {   // Anzeige: sechs Muster, dann Urteil
        FakeService svc;
        App a(svc);
        open_list(a);
        press(a, {Button::R1});
        for (int i = 0; i < 6; ++i) {
            a.render(cv);
            press(a, {Button::Cross});
        }
        a.render(cv);
        CHECK(a.screen() == App::Screen::UserTest && svc.recorded.empty(), "Nutzer-Test: nach den Mustern kommt die Frage, noch kein Ergebnis");
        press(a, {Button::Triangle});
        CHECK(a.screen() == App::Screen::SelfTest && svc.recorded.size() == 1 && svc.recorded[0].id == "hw.display.pattern" && svc.recorded[0].status == TestStatus::Warn && svc.recorded[0].reason == "user_reported", "Nutzer-Test: Auffaelligkeit = Warnung (user_reported)");
        FakeService s2;
        App b(s2);
        open_list(b);
        press(b, {Button::R1, Button::Cross, Button::Cross, Button::Cross, Button::Cross, Button::Cross, Button::Cross, Button::Circle});
        CHECK(s2.recorded.size() == 1 && s2.recorded[0].status == TestStatus::Skipped && s2.recorded[0].reason == "unknown", "Nutzer-Test: keine Antwort (Kreis) = skipped, weiss nicht");
        FakeService s3;
        App d(s3);
        open_list(d);
        press(d, {Button::R1, Button::Cross, Button::Circle});
        CHECK(d.screen() == App::Screen::SelfTest && s3.recorded.empty(), "Nutzer-Test: Abbruch waehrend der Muster traegt nichts ein");
        FakeService s4;
        App e(s4);
        open_list(e);
        press(e, {Button::R1, Button::Cross, Button::Cross, Button::Cross, Button::Cross, Button::Cross, Button::Cross, Button::Cross});
        CHECK(s4.recorded.size() == 1 && s4.recorded[0].status == TestStatus::Passed, "Nutzer-Test: 'nichts aufgefallen' = bestanden (Urteil des Nutzers)");
    }
}

// Screenshot-Hook: ohne Anforderung passiert nichts; mit Anforderung genau ein neues PNG im eigenen Ordner, die Anforderung ist danach weg.
void shot_hook_tests() {
    const fs::path dir = fs::temp_directory_path() / ("medic-shot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const std::string request = (dir / "devloop-shot").string(), folder = (dir / "screenshots").string();
    Canvas cv(64, 48);
    cv.clear(rgb(10, 20, 30));
    std::string path;
    CHECK(!serve_shot_request(cv, request, folder, &path) && !fs::exists(folder), "Hook: ohne Anforderung passiert nichts (kein Ordner, kein Bild)");
    std::ofstream(request) << "x";
    CHECK(serve_shot_request(cv, request, folder, &path), "Hook: mit Anforderung wird ein Bild geschrieben");
    CHECK(!fs::exists(request), "Hook: die Anforderung ist danach entfernt");
    std::ifstream in(path, std::ios::binary);
    char sig[8] = {};
    in.read(sig, 8);
    CHECK(path.find(folder) == 0 && std::string(sig + 1, 3) == "PNG" && fs::file_size(path) > 64 * 48 * 3, "Hook: gueltige PNG-Datei im eigenen Ordner");
    CHECK(!serve_shot_request(cv, request, folder, &path), "Hook: eine Anforderung bedient genau ein Bild");
    for (int i = 0; i < kShotMaxFiles; ++i) std::ofstream((fs::path(folder) / ("alt" + std::to_string(i) + ".png")).string()) << "x";
    std::ofstream(request) << "x";
    CHECK(!serve_shot_request(cv, request, folder, &path) && !fs::exists(request), "Hook: liegen schon zu viele Bilder im Ordner, wird nichts mehr geschrieben");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

void selftest_ui_tests() {
    FakeService svc;
    Canvas cv(App::kWidth, App::kHeight);
    App a(svc);
    press(a, {Button::Down, Button::Down, Button::Down, Button::Down, Button::Cross});
    CHECK(a.screen() == App::Screen::SelfTest, "Selbsttest-UI: vierter Knopf oeffnet den Selbsttest");
    a.render(cv);
    press(a, {Button::Cross});  // Hinweis vor dem Lauf
    a.render(cv);
    CHECK(a.screen() == App::Screen::SelfTest, "Selbsttest-UI: Kreuz zeigt zuerst den Hinweis");
    press(a, {Button::Circle});
    press(a, {Button::Square});  // nur Schnelltests: laeuft sofort
    a.render(cv);
    int guard = 0;
    while (guard++ < 40) {  // jedes update() macht einen Schritt, danach wird gezeichnet
        a.update();
        a.render(cv);
    }
    const std::size_t quick = std::count_if(selftest_registry().begin(), selftest_registry().end(), [](const TestDef& d) { return d.tier == TestTier::Quick; });
    CHECK(svc.selftest_begins == 1 && svc.selftest_runs == static_cast<int>(quick) && svc.selftest_ends == 1 && !svc.last_cancelled, "Selbsttest-UI: Marker einmal, nur die Schnelltests laufen, Ende gemeldet");
    for (const auto& r : svc.last_results) CHECK(r.status != TestStatus::Passed || r.id.rfind("hw.storage.read", 0) != 0, "Selbsttest-UI: der Lesetest lief bei 'nur Schnelltests' nicht");
    press(a, {Button::Circle});
    CHECK(a.screen() == App::Screen::Start, "Selbsttest-UI: Kreis zurueck zum Start");

    // Alle Tests, Abbruch nach dem ersten
    FakeService svc2;
    App b(svc2);
    press(b, {Button::Down, Button::Down, Button::Down, Button::Down, Button::Cross, Button::Cross, Button::Cross});  // oeffnen, Hinweis, starten
    b.render(cv);
    b.update();  // Marker
    b.render(cv);
    b.update();  // erster Test
    b.render(cv);
    press(b, {Button::Circle});
    b.render(cv);
    for (int i = 0; i < 12; ++i) {
        b.update();
        b.render(cv);
    }
    CHECK(svc2.selftest_runs == 1 && svc2.selftest_ends == 1 && svc2.last_cancelled, "Selbsttest-UI: Kreis bricht nach dem laufenden Test ab");
    int cancelled = 0;
    for (const auto& r : svc2.last_results) cancelled += r.status == TestStatus::Cancelled ? 1 : 0;
    CHECK(cancelled == static_cast<int>(svc2.last_results.size()) - 1 && !svc2.last_results.empty(), "Selbsttest-UI: der Rest steht als abgebrochen im Ergebnis, nie als bestanden");

    // Gruendlich: nur ueber Dreieck und Bestaetigung; Standard laesst Last und Schreiben aus
    FakeService svc3;
    App c(svc3);
    press(c, {Button::Down, Button::Down, Button::Down, Button::Down, Button::Cross, Button::Cross, Button::Cross});  // Standard: Hinweis, starten
    for (int i = 0; i < 40; ++i) {
        c.render(cv);
        c.update();
    }
    int standard = 0, thorough = 0;
    int run_all = 0;
    for (const auto& d : selftest_registry()) {
        if (d.tier == TestTier::User) continue;  // Nutzer-Tests laufen nie im Ablauf
        (d.tier == TestTier::Thorough ? thorough : standard) += 1;
        ++run_all;
    }
    CHECK(svc3.selftest_runs == standard && thorough > 0, "Selbsttest-UI: Standard startet keine Last- und Schreibtests");
    FakeService svc4;
    App d4(svc4);
    press(d4, {Button::Down, Button::Down, Button::Down, Button::Down, Button::Cross, Button::Triangle});  // Gruendlich: Hinweis, noch nichts gestartet
    d4.render(cv);
    CHECK(svc4.selftest_begins == 0 && svc4.selftest_runs == 0, "Selbsttest-UI: Gruendlich zeigt zuerst den Hinweis, es laeuft nichts");
    press(d4, {Button::Circle});
    press(d4, {Button::Triangle, Button::Cross});
    for (int i = 0; i < 60; ++i) {
        d4.render(cv);
        d4.update();
    }
    CHECK(svc4.selftest_runs == run_all, "Selbsttest-UI: Gruendlich nach Bestaetigung laeuft alle Tests (ohne Nutzer-Tests)");
}

void checks_archive_tests() {
    const RuleSet rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    auto count_files = [](const fs::path& dir) {
        int n = 0;
        if (fs::exists(dir))
            for (auto& e : fs::directory_iterator(dir)) n += e.is_regular_file() ? 1 : 0;
        return n;
    };
    TempCopy t("b8_pos");
    auto platform = make_snapshot_platform(t.dir.string(), /*writable=*/true);
    int tick = 0;
    LocalService svc(*platform, rules, [&] { return std::string("20261111T1") + std::to_string(10 + tick++) + "00Z"; });
    CHECK(svc.checks().empty(), "Check-ups: anfangs keine gespeichert");
    const auto r1 = svc.run_check();
    const auto r2 = svc.run_check();
    const auto r3 = svc.run_check();
    CHECK(count_files(t.dir / "fs/data/ps5medic/checks") == 3, "Check-ups: jeder Check-up ist eine eigene Datei");
    const auto list = svc.checks();
    CHECK(list.size() == 3, "Check-ups: drei in der Liste");
    CHECK(list.size() == 3 && list[0].ts > list[1].ts && list[1].ts > list[2].ts, "Check-ups: neueste zuerst");
    CHECK(list.size() == 3 && list[0].total == static_cast<int>(r3.findings.size()) && list[0].fw == r3.fw && list[0].rules_version == r3.rules_version, "Check-ups: Zusammenfassung stimmt");
    StoredCheck sc;
    CHECK(list.size() == 3 && svc.load_check(list[0].id, sc) && sc.findings.size() == r3.findings.size() && sc.findings[0].rule_id == r3.findings[0].rule_id &&
              sc.findings[0].text_de == r3.findings[0].text_de && sc.findings[0].text_en == r3.findings[0].text_en && sc.findings[0].evidence == r3.findings[0].evidence &&
              sc.findings[0].fix == r3.findings[0].fix && sc.findings[0].severity == r3.findings[0].severity && sc.findings[0].status == r3.findings[0].status &&
              sc.findings[0].seen_runs == r3.findings[0].seen_runs && sc.coverage.size() == r3.coverage.size(),
          "Check-ups: ein geladener Check-up gleicht dem gemeldeten (Text, Beleg, Reparatur, Verlauf)");
    CHECK(list.size() == 3 && svc.load_check(list[2].id, sc) && sc.findings[0].status.empty(), "Check-ups: der erste Check-up hat keinen Vergleich (damals)");
    CHECK(!svc.load_check("../etc", sc) && !svc.load_check("gibt-es-nicht", sc) && !svc.load_check("", sc), "Check-ups: ungueltige oder fehlende id wird abgelehnt");
    // gleiche Zeit: nie ueberschreiben
    StoredCheck dup;
    dup.ts = list[2].ts;
    dup.fw = "dup";
    std::string id;
    CHECK(save_check(*platform, dup, &id) && id != list[2].ts && count_files(t.dir / "fs/data/ps5medic/checks") == 4, "Check-ups: gleiche Zeit ueberschreibt nichts");
    StoredCheck orig;
    CHECK(load_check(platform->fs(), list[2].id, orig) && orig.fw != "dup", "Check-ups: die alte Datei ist unveraendert");
    // beschaedigte Datei wird uebersprungen
    std::ofstream(t.dir / "fs/data/ps5medic/checks/20261111T125900Z.json", std::ios::binary) << "{kaputt";
    auto p2 = make_snapshot_platform(t.dir.string(), true);
    CHECK(list_checks(p2->fs()).size() == 4, "Check-ups: beschaedigte Datei wird uebersprungen");
    auto ro = make_snapshot_platform(t.dir.string(), false);
    CHECK(!save_check(*ro, dup), "Check-ups: ohne Schreibzugriff wird nichts gespeichert");

    // Vergleich
    StoredCheck a, b;
    auto fnd = [](const char* id, Severity sev) {
        Finding f;
        f.rule_id = id;
        f.severity = sev;
        return f;
    };
    a.findings = {fnd("r.both", Severity::Warning), fnd("r.new", Severity::Critical)};
    b.findings = {fnd("r.both", Severity::Warning), fnd("r.gone", Severity::Info)};
    const CheckDiff d = compare_checks(a, b);
    CHECK(d.added.size() == 1 && d.added[0].rule_id == "r.new" && d.resolved.size() == 1 && d.resolved[0].rule_id == "r.gone" && d.unchanged.size() == 1 && d.unchanged[0].rule_id == "r.both", "Vergleich: neu, behoben, unveraendert");
    CHECK(compare_checks(a, a).added.empty() && compare_checks(a, a).resolved.empty(), "Vergleich: gleicher Check-up ohne Unterschiede");

    // Oberflaeche: Liste, ansehen, nie reparieren, zurueck zum aktuellen, Vergleich
    {
        auto pl = make_snapshot_platform(t.dir.string(), true);
        LocalService s2(*pl, rules, [&] { return std::string("20261111T1") + std::to_string(10 + tick++) + "00Z"; });
        App app(s2);
        Canvas cv(App::kWidth, App::kHeight), cv2(App::kWidth, App::kHeight);
        run_check_via_ui(app, cv);  // frischer Check-up
        CHECK(app.screen() == App::Screen::Findings, "Check-up-Verlauf: aktueller Check-up da");
        const std::size_t live_n = app.finding_count();
        press(app, {Button::Circle, Button::Down, Button::Cross});
        CHECK(app.screen() == App::Screen::CheckList, "Check-up-Verlauf: Start -> Verlauf zeigt die Check-ups");
        app.render(cv);
        press(app, {Button::Down, Button::Down});
        press(app, {Button::Cross});
        CHECK(app.screen() == App::Screen::Findings, "Check-up-Verlauf: Kreuz zeigt den gewaehlten Check-up");
        app.render(cv);
        press(app, {Button::Triangle});
        CHECK(app.screen() == App::Screen::Findings, "Check-up-Verlauf: im alten Stand wird nie repariert (Dreieck tut nichts)");
        press(app, {Button::Square});
        CHECK(app.screen() == App::Screen::Findings, "Check-up-Verlauf: im alten Stand kein Bericht");
        press(app, {Button::Cross});
        CHECK(app.screen() == App::Screen::Detail, "Check-up-Verlauf: Details des alten Stands");
        app.render(cv2);
        press(app, {Button::Triangle});
        CHECK(app.screen() == App::Screen::Detail, "Check-up-Verlauf: auch im Detail keine Reparatur");
        press(app, {Button::Circle, Button::Circle});
        CHECK(app.screen() == App::Screen::CheckList && app.finding_count() == live_n, "Check-up-Verlauf: Kreis bringt den aktuellen Check-up zurueck");
        press(app, {Button::Triangle});
        CHECK(app.screen() == App::Screen::CheckCompare, "Check-up-Verlauf: Dreieck vergleicht mit dem davor");
        app.render(cv);
        press(app, {Button::Down, Button::Up, Button::R1, Button::L1, Button::Circle});
        CHECK(app.screen() == App::Screen::CheckList, "Check-up-Verlauf: Kreis aus dem Vergleich");
        press(app, {Button::Square});
        CHECK(app.screen() == App::Screen::History, "Check-up-Verlauf: Quadrat zeigt die Reparaturen");
        press(app, {Button::Circle});
        CHECK(app.screen() == App::Screen::CheckList, "Check-up-Verlauf: Kreis aus den Reparaturen zurueck zur Liste");
        press(app, {Button::Circle});
        CHECK(app.screen() == App::Screen::Start, "Check-up-Verlauf: Kreis zurueck zum Start");
    }
    // leere Liste und unlesbarer Eintrag
    {
        FakeService fk;
        App app(fk);
        Canvas cv(App::kWidth, App::kHeight);
        press(app, {Button::Down, Button::Cross});
        app.render(cv);
        press(app, {Button::Cross, Button::Triangle, Button::Down, Button::Up, Button::R1, Button::L1});
        CHECK(app.screen() == App::Screen::CheckList, "Check-up-Verlauf: leere Liste, Tasten tun nichts");
    }
}

void icon_tests() {
    // Jedes Symbol malt in jeder Groesse sichtbar und innerhalb seines Feldes; Akzentfarbe und Hauptfarbe kommen vor (wo das Symbol einen Akzent hat)
    const Color bg = rgb(0, 0, 0), main_c = rgb(255, 0, 0), acc_c = rgb(0, 0, 255);
    bool all_visible = true, inside_field = true;
    int with_accent = 0;
    for (int i = 0; i <= static_cast<int>(Icon::Shield); ++i)
        for (int size : {24, 48, 96}) {
            Canvas c(size + 40, size + 40);
            c.clear(bg);
            draw_icon(c, static_cast<Icon>(i), (size + 40) / 2, (size + 40) / 2, size, main_c, acc_c);
            int painted = 0, blue = 0, outside = 0;
            for (int y = 0; y < size + 40; ++y)
                for (int x = 0; x < size + 40; ++x) {
                    const Color px = c.at(x, y);
                    if ((px & 0xFFFFFF) == 0) continue;
                    ++painted;
                    if ((px & 0xFF) > 60) ++blue;
                    if (x < 20 || y < 20 || x >= 20 + size || y >= 20 + size) ++outside;
                }
            all_visible = all_visible && painted > size;  // mehr als ein Strich der Laenge "Feld"
            inside_field = inside_field && outside == 0;
            if (size == 96 && blue > 0) ++with_accent;
        }
    CHECK(all_visible, "Symbole: jedes Symbol ist in 24, 48 und 96 Punkten sichtbar");
    CHECK(inside_field, "Symbole: nichts ausserhalb des Feldes");
    CHECK(with_accent >= 15, "Symbole: viele Symbole nutzen die Akzentfarbe");
    Canvas c(200, 200);
    c.clear(rgb(0, 0, 0));
    draw_icon_badge(c, Icon::Gear, 20, 20, 100, rgb(60, 160, 255));
    CHECK(c.at(70, 24) != rgb(0, 0, 0) && c.at(70, 24) != c.at(70, 112) && c.at(5, 5) == rgb(0, 0, 0), "Symbolfeld: Verlauf von oben nach unten, nichts ausserhalb");
    CHECK(c.at(60, 20 + 100 + 10) == rgb(0, 0, 0), "Symbolfeld: begrenzt");
    CHECK((lighten(rgb(100, 100, 100), 128) & 0xFF) > 100 && (darken(rgb(100, 100, 100), 128) & 0xFF) < 100, "Symbole: heller und dunkler");
    // Verlaeufe und Linienzug
    Canvas g(40, 40);
    g.fill_vgradient(0, 0, 40, 40, rgb(0, 0, 0), rgb(200, 200, 200));
    CHECK((g.at(5, 0) & 0xFF) == 0 && (g.at(5, 39) & 0xFF) == 200 && (g.at(5, 20) & 0xFF) > 80 && (g.at(5, 20) & 0xFF) < 120, "Canvas: senkrechter Verlauf");
    Canvas l(40, 40);
    l.clear(rgb(0, 0, 0));
    l.stroke_polyline({{5, 20}, {35, 20}}, 4, rgb(255, 255, 255));
    CHECK((l.at(20, 20) & 0xFF) == 255 && (l.at(20, 5) & 0xFF) == 0, "Canvas: Linienzug");
    // Die Oberflaeche zeichnet reproduzierbar: zwei Bilder desselben Zustands sind gleich (Hintergrund aus dem Zwischenspeicher)
    FakeService fake;
    App app(fake);
    Canvas a(App::kWidth, App::kHeight), b(App::kWidth, App::kHeight);
    app.render(a);
    app.render(b);
    CHECK(a.at(10, 10) == b.at(10, 10) && a.at(App::kWidth / 2, 400) == b.at(App::kWidth / 2, 400) && std::equal(a.data(), a.data() + 4096, b.data()), "UI: Hintergrund wird reproduzierbar gezeichnet");
}

}  // namespace

void ui_tests() {
    canvas_tests();
    font_tests();
    i18n_tests();
    png_tests();
    screen_flow_tests();
    coverage_ui_tests();
    redetect_ui_tests();
    confidence_ui_tests();
    selftest_ui_tests();
    checks_archive_tests();
    icon_tests();
    shot_hook_tests();
    usertest_ui_tests();
    history_ui_tests();
    edge_tests();
    end_to_end_tests();
    undo_in_new_session_tests();
    report_service_tests();
    rules_update_flow_tests();
    rules_update_service_tests();
    rules_download_flow_tests();
    rules_download_service_tests();
    pad_nav_tests();
    shoulder_and_stick_flow_tests();
    error_codes_flow_tests();
    error_codes_real_data_tests();
    error_link_tests();
    error_link_real_data_tests();
    error_source_tests();
    error_report_mark_tests();
    error_report_service_tests();
    error_context_tests();
    error_scroll_accel_tests();
    codes_update_flow_tests();
    codes_update_service_tests();
    i18n_glyph_tests();
}
