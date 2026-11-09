// SPDX-License-Identifier: GPL-3.0-or-later
// Tests der UI: Canvas, Schrift/Umbruch, Texte, PNG, Bildschirmablauf (mit Fake-Dienst) und eine Ende-zu-Ende-Reparatur
// ueber LocalService gegen eine Fixture-Kopie.
#include "check.hpp"
#include "medic_ui/app.hpp"
#include "medic_ui/font.hpp"
#include "medic/http.hpp"
#include "medic/sha256.hpp"
#include "medic/signature.hpp"
#include "medic_ui/pad_nav.hpp"
#include "medic_ui/png.hpp"

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
    press(app, {Button::Down, Button::Cross});
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
    press(app, {Button::Down, Button::Cross});
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
    press(a, {Button::Circle, Button::Down, Button::Cross});
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
    press(b, {Button::Circle, Button::Circle, Button::Circle, Button::Down, Button::Cross});
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
    press(app2, {Button::Down, Button::Cross});
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

    press(app, {Button::Circle, Button::Down, Button::Cross});
    CHECK(app.screen() == App::Screen::History, "E2E: Verlauf");
    app.render(cv);
    press(app, {Button::Cross});
    CHECK(app.screen() == App::Screen::UndoConfirm, "E2E: Rueckgaengig fragt nach");
    app.render(cv);
    press(app, {Button::Cross});
    CHECK(app.screen() == App::Screen::FixDone, "E2E: Rueckgaengig ausgefuehrt");
    CHECK(tree(t.dir) == before, "E2E: nach dem Rueckgaengigmachen ist der Dateibaum wieder wie vorher");
}

std::set<std::string> full_tree(const fs::path& root) {  // anders als tree(): auch /data/ps5medic
    std::set<std::string> t;
    for (auto& e : fs::recursive_directory_iterator(root)) t.insert(fs::relative(e.path(), root).generic_string());
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
        CHECK(http.asked.size() == 2 && http.asked[0] == base + "rules.json" && http.asked[1] == base + "rules.json.sig", "Download-E2E: genau zwei Abrufe, rules.json und rules.json.sig");
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
        press(a, {Button::R1, Button::Cross});
        CHECK(a.screen() == App::Screen::History, "Start: R1 waehlt den naechsten Eintrag (Verlauf)");
        press(a, {Button::Circle, Button::R1, Button::Cross});
        CHECK(a.screen() == App::Screen::Settings, "Start: R1 noch einmal waehlt die Einstellungen");
        press(a, {Button::Circle, Button::Left, Button::Cross});
        CHECK(a.screen() == App::Screen::History, "Start: Links springt von Einstellungen zu Verlauf");
        press(a, {Button::Circle, Button::Right, Button::Cross});
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
        press(a, {Button::Down, Button::Cross});
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

}  // namespace

void ui_tests() {
    canvas_tests();
    font_tests();
    i18n_tests();
    png_tests();
    screen_flow_tests();
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
}
