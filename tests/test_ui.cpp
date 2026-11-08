// Tests der UI: Canvas, Schrift/Umbruch, Texte, PNG, Bildschirmablauf (mit Fake-Dienst) und eine Ende-zu-Ende-Reparatur
// ueber LocalService gegen eine Fixture-Kopie.
#include "check.hpp"
#include "medic_ui/app.hpp"
#include "medic_ui/font.hpp"
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

namespace {

struct FakeService : IMedicService {
    CheckResult result;
    FixPlan plan;
    ApplyResult apply_result{true, "T1", ""};
    std::vector<TxnInfo> hist;
    UndoResult undo_result{true, ""};
    int checks = 0, plans = 0, applies = 0, undos = 0;
    std::string last_undo;

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
    for (auto gl : {PadGlyph::Cross, PadGlyph::Circle, PadGlyph::Square, PadGlyph::Triangle, PadGlyph::UpDown, PadGlyph::LeftRight}) {
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

}  // namespace

void ui_tests() {
    canvas_tests();
    font_tests();
    i18n_tests();
    png_tests();
    screen_flow_tests();
    edge_tests();
    end_to_end_tests();
}
