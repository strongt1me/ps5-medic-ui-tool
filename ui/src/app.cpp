// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/app.hpp"

#include "medic_ui/font.hpp"
#include "medic_ui/icons.hpp"

#include "medic/history.hpp"

#include <algorithm>
#include <iterator>
#include <cmath>

namespace medic::ui {

namespace {

constexpr int W = App::kWidth, H = App::kHeight, M = 96;
constexpr int kListVisible = 4, kHistVisible = 6, kErrVisible = 8, kChkVisible = 8, kCmpVisible = 13, kGameVisible = 8, kGdVisible = 5;

const Color kBg = rgb(14, 18, 24), kPanel = rgb(23, 29, 38), kPanelHi = rgb(33, 42, 56), kLine = rgb(42, 52, 66);
const Color kText = rgb(232, 237, 242), kDim = rgb(141, 153, 168), kAccent = rgb(76, 154, 255);
const Color kCrit = rgb(255, 92, 92), kWarn = rgb(255, 179, 71), kInfo = rgb(92, 184, 255), kOk = rgb(76, 208, 138);

const Font& body() { return Font::get(Font::Id::Body); }
const Font& small() { return Font::get(Font::Id::Small); }
const Font& title() { return Font::get(Font::Id::Title); }

Color sev_color(medic::Severity s) {
    switch (s) {
        case medic::Severity::Critical: return kCrit;
        case medic::Severity::Warning: return kWarn;
        case medic::Severity::Info: break;
    }
    return kInfo;
}

const char* sev_key(medic::Severity s) {
    switch (s) {
        case medic::Severity::Critical: return "sev.critical";
        case medic::Severity::Warning: return "sev.warning";
        case medic::Severity::Info: break;
    }
    return "sev.info";
}

// Fixes mit der Endung "_hint" aendern nichts, sie zeigen nur eine Anleitung (z. B. fix_image_nesting_hint).
bool is_hint_fix(const std::string& id) { return id.size() > 5 && id.compare(id.size() - 5, 5, "_hint") == 0; }

// Rueckgaengig geht bei abgeschlossenen Fixes und bei abgebrochenen ("incomplete": die Engine rollt sie zurueck).
bool undoable(const std::string& state) { return state == "applied" || state == "incomplete"; }

// "20261108T120000Z" -> "2026-11-08 12:00 UTC"
std::string pretty_ts(const std::string& ts) {
    if (ts.size() >= 15 && ts[8] == 'T')
        return ts.substr(0, 4) + "-" + ts.substr(4, 2) + "-" + ts.substr(6, 2) + " " + ts.substr(9, 2) + ":" + ts.substr(11, 2) + " UTC";
    return ts;
}

int draw_badge(Canvas& c, int x, int y, const std::string& label, Color col) {
    const int w = small().width(label) + 24;
    c.fill_round_rect(x, y, w, 34, 8, col);
    small().draw(c, x + 12, y + 4, label, kBg);
    return w;
}

int text_right(Canvas& c, const Font& f, int right, int y, const std::string& s, Color col) {
    const int w = f.width(s);
    f.draw(c, right - w, y, s, col);
    return w;
}

void card(Canvas& c, int x, int y, int w, int h, bool selected) {
    c.fill_round_rect(x, y, w, h, 16, selected ? kPanelHi : kPanel);
    if (selected) c.stroke_round_rect(x, y, w, h, 16, 3, kAccent);
}

Color rgba(unsigned r, unsigned g, unsigned b, unsigned a) { return (a << 24) | (r << 16) | (g << 8) | b; }

const Color kTintBlue = rgb(76, 154, 255), kTintViolet = rgb(167, 139, 250), kTintTeal = rgb(45, 212, 191), kTintOrange = rgb(251, 146, 60),
            kTintSlate = rgb(125, 146, 178), kTintGreen = rgb(76, 208, 138), kTintPink = rgb(244, 114, 182);

Icon sev_icon(medic::Severity s) {
    switch (s) {
        case medic::Severity::Critical: return Icon::Critical;
        case medic::Severity::Warning: return Icon::Warning;
        case medic::Severity::Info: break;
    }
    return Icon::Info;
}

// Symbol und Farbe je Selbsttest (nach der ID; unbekannte bekommen ein Haeckchen-Brett)
std::pair<Icon, Color> test_look(const std::string& id) {
    if (id == "sw.storage.mounts") return {Icon::Drive, kTintTeal};
    if (id == "hw.sensors.plausible") return {Icon::Thermometer, kTintOrange};
    if (id == "sw.db.headers") return {Icon::Database, kTintViolet};
    if (id == "sw.files.sanity") return {Icon::File, kTintBlue};
    if (id == "hw.storage.read_throughput") return {Icon::Gauge, kTintTeal};
    if (id == "sw.net.local") return {Icon::Wifi, kTintBlue};
    if (id == "hw.thermal.short_soak") return {Icon::Flame, kTintOrange};
    if (id == "hw.mem.userland_pattern") return {Icon::Chip, kTintViolet};
    if (id == "hw.storage.write_verify") return {Icon::Pencil, kTintPink};
    if (id == "hw.pad.buttons") return {Icon::Gamepad, kTintGreen};
    if (id == "hw.display.pattern") return {Icon::Display, kTintGreen};
    return {Icon::Checklist, kTintSlate};
}

Icon status_icon(medic::TestStatus st) {
    switch (st) {
        case medic::TestStatus::Passed: return Icon::Ok;
        case medic::TestStatus::Warn: return Icon::Warning;
        case medic::TestStatus::Failed:
        case medic::TestStatus::Error: return Icon::Critical;
        case medic::TestStatus::Skipped:
        case medic::TestStatus::Cancelled: return Icon::Skip;
        default: break;
    }
    return Icon::Circle;
}

// Titel mit Symbolfeld davor (Bildschirmtitel bei y = 130); liefert die x-Position des Textes
int title_icon(Canvas& c, Icon icon, Color tint, const std::string& text, int y = 130) {
    draw_icon_badge(c, icon, M, y - 4, 64, tint);
    title().draw(c, M + 88, y, text, kText);
    return M + 88;
}

// Herzschlaglinie: Verlauf einer Welle ueber [x0, x0 + width], phase verschiebt sie (fuer die Bewegung)
std::vector<std::pair<float, float>> ecg_points(float x0, float width, float base, float amp, float period, float phase) {
    static const float tpl[][2] = {{0.00f, 0}, {0.28f, 0}, {0.33f, -0.14f}, {0.38f, 0}, {0.45f, 0}, {0.48f, 0.2f}, {0.53f, -1.0f}, {0.58f, 0.6f}, {0.62f, 0}, {0.74f, 0}, {0.80f, -0.26f}, {0.86f, 0}, {1.00f, 0}};
    auto wave = [&](float t) {
        t -= std::floor(t);
        for (int i = 0; i + 1 < 13; ++i)
            if (t <= tpl[i + 1][0]) return tpl[i][1] + (tpl[i + 1][1] - tpl[i][1]) * (t - tpl[i][0]) / (tpl[i + 1][0] - tpl[i][0]);
        return 0.0f;
    };
    std::vector<std::pair<float, float>> pts;
    for (float x = 0; x <= width; x += 3.0f) pts.emplace_back(x0 + x, base + amp * wave((x + phase) / period));
    return pts;
}

// Hintergrund: senkrechter Verlauf mit zwei weichen Lichtflecken, mit Raster-Dithering, damit es keine sichtbaren Stufen gibt (8 Bit je Farbe)
void build_background(Canvas& bg, const Canvas* image) {
    static const int bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    struct Glow {
        float cx, cy, r, cr, cg, cb, a;
    };
    const int w = bg.width(), h = bg.height();
    const Glow glows[2] = {{static_cast<float>(w - 160), 40.0f, 620.0f, 40, 96, 190, 0.20f}, {120.0f, static_cast<float>(h - 60), 520.0f, 60, 50, 140, 0.12f}};
    const float top[3] = {18, 25, 38}, bottom[3] = {9, 12, 18};
    for (int y = 0; y < h; ++y) {
        const float ty = static_cast<float>(y) / static_cast<float>(h - 1);
        for (int x = 0; x < w; ++x) {
            float col[3];
            for (int k = 0; k < 3; ++k) col[k] = top[k] + (bottom[k] - top[k]) * ty;
            for (const auto& g : glows) {
                const float dx = static_cast<float>(x) - g.cx, dy = static_cast<float>(y) - g.cy;
                if (dx > g.r || dx < -g.r || dy > g.r || dy < -g.r) continue;  // ausserhalb des Quadrats um den Lichtfleck: kein Einfluss
                const float t = std::sqrt(dx * dx + dy * dy) / g.r;  // statt std::hypot (auf der Konsole sehr langsam, der Aufbau kostete 1,8 s)
                if (t >= 1.0f) continue;
                const float f = (1.0f - t) * (1.0f - t) * g.a;
                col[0] += (g.cr - col[0]) * f;
                col[1] += (g.cg - col[1]) * f;
                col[2] += (g.cb - col[2]) * f;
            }
            if (image) {
                // Bild unter den einfachen Verlauf mischen: insgesamt abgedunkelt, am oberen Rand (Titelzeile) und links noch etwas mehr, damit
                // Titel, Beschriftungen und Karten lesbar bleiben; die hellen Kanten der Konsole links werden so zu einem Schimmer.
                const Color px = image->at(x, y);
                const float ix = static_cast<float>((px >> 16) & 0xFF), iy = static_cast<float>((px >> 8) & 0xFF), iz = static_cast<float>(px & 0xFF);
                const float fx = static_cast<float>(x) / static_cast<float>(w - 1);
                float mix = 0.62f;
                mix *= 1.0f - 0.45f * std::clamp(1.0f - fx * 2.2f, 0.0f, 1.0f);        // links (Konsole) zurueckhaltender
                mix *= 1.0f - 0.60f * std::clamp(1.0f - static_cast<float>(y) / 300.0f, 0.0f, 1.0f);  // oben (Titelzeile, Kopf der Listen)
                mix *= 1.0f - 0.35f * std::clamp((static_cast<float>(y) - static_cast<float>(h - 140)) / 140.0f, 0.0f, 1.0f);  // unten (Hinweisleiste)
                {  // das Wappen mit dem Kreuz (oben in der Mitte) liegt sonst hinter Titelzeilen und Listenkoepfen
                    const float ex = (static_cast<float>(x) - 962.0f) / 360.0f, ey = (static_cast<float>(y) - 215.0f) / 250.0f;
                    const float e2 = ex * ex + ey * ey;
                    if (e2 < 1.0f) mix *= 1.0f - 0.80f * (1.0f - std::sqrt(e2));  // ausserhalb der Ellipse ist t = 0
                }
                const float dim = 0.60f;
                col[0] += (ix * dim - col[0]) * mix;
                col[1] += (iy * dim - col[1]) * mix;
                col[2] += (iz * dim - col[2]) * mix;
            }
            const float d = static_cast<float>(bayer[y & 3][x & 3]) / 16.0f;  // 0 .. 0.94
            auto q = [&](float v) { return static_cast<unsigned>(std::clamp(v + d, 0.0f, 255.0f)); };
            bg.put(x, y, rgb(q(col[0]), q(col[1]), q(col[2])));
        }
    }
}

// ---- Fehlercodes: Textzeilen der Detailansicht ---------------------------------
// Der Text eines Codes wird in Zeilen zerlegt, die per Hoch/Runter gescrollt werden. Nichts wird gekuerzt: Schritte und
// Hinweise wie "vorher Daten sichern" duerfen nie abgeschnitten werden.
constexpr int kErrPanelY = 250, kErrPanelH = 690, kErrPadY = 22, kErrPadX = 36, kErrBar = 28;
constexpr int kErrTextW = W - 2 * M - 2 * kErrPadX - kErrBar;
constexpr int kErrAvail = kErrPanelH - 2 * kErrPadY;  // Hoehe des Textbereichs im Panel
constexpr int kStepIndent = 64;                       // Platz fuer die Nummer vor einem Schritt

struct ErrRow {
    std::string text, marker;  // marker: Nummer bzw. "-" vor der ersten Zeile eines Schritts/Hinweises
    bool small = false;
    Color col = 0;
    int indent = 0, gap = 0;   // gap: Abstand ueber der Zeile (zwischen Abschnitten)
    bool label = false;        // Abschnittsueberschrift: steht nie allein am unteren Rand
};

int row_height(const ErrRow& r) { return (r.small ? small() : body()).line_height() + 4 + r.gap; }

// Text einer Kontextzeile: ctx.<kind> mit den Werten der Zeile.
std::string format_ctx(const Strings& S, const ContextLine& l) {
    std::string s = S("ctx." + l.kind);
    for (const auto& [name, value] : l.args) {
        const std::string token = "{" + name + "}";
        for (std::size_t pos = s.find(token); pos != std::string::npos; pos = s.find(token, pos + value.size())) s.replace(pos, token.size(), value);
    }
    return s;
}

// Text in der Sprache der Oberflaeche: de/en stehen in den Daten selbst, fr/it/es in der Ueberlagerung (ui/src/i18n.cpp, Knowledge),
// Englisch dort, wo ein Eintrag fehlt.
std::string pick_text(Lang lang, const std::string& de, const std::string& en, const char* section, const std::string& id, const char* field) {
    if (lang == Lang::De) return de;
    if (lang == Lang::En) return en;
    const std::string* t = Knowledge::get(lang).text(section, id, field);
    return t ? *t : en;
}

const std::vector<std::string>& pick_list(Lang lang, const std::vector<std::string>& de, const std::vector<std::string>& en, const char* section, const std::string& id,
                                          const char* field) {
    if (lang == Lang::De) return de;
    if (lang == Lang::En) return en;
    const std::vector<std::string>* t = Knowledge::get(lang).list(section, id, field);
    return t && t->size() == en.size() ? *t : en;
}

std::vector<ErrRow> error_rows(const medic::ErrorCode& e, Lang lang, const CodeContext& ctx) {
    const auto& S = Strings::get(lang);
    std::vector<ErrRow> rows;
    auto label = [&](const std::string& text, int gap) {
        ErrRow r{text, "", true, kDim, 0, gap};
        r.label = true;
        rows.push_back(r);
    };
    auto paragraph = [&](const std::string& text, bool small_font, Color col, const std::string& marker, int indent) {
        const auto lines = wrap_text(small_font ? small() : body(), text, kErrTextW - indent);
        for (std::size_t i = 0; i < lines.size(); ++i) rows.push_back({lines[i], i == 0 ? marker : std::string(), small_font, col, indent, 0});
    };

    label(S("detail.explanation"), 0);
    paragraph(pick_text(lang, e.text_de, e.text_en, "error_codes", e.id, "text"), false, kText, "", 0);

    label(S("errors.steps"), 18);
    const auto& steps = pick_list(lang, e.steps_de, e.steps_en, "error_codes", e.id, "steps");
    if (steps.empty()) paragraph(S("errors.steps_none"), false, kDim, "", 0);
    for (std::size_t i = 0; i < steps.size(); ++i) paragraph(steps[i], false, kText, std::to_string(i + 1) + ".", kStepIndent);

    const auto& notes = pick_list(lang, e.notes_de, e.notes_en, "error_codes", e.id, "notes");
    if (!notes.empty()) {
        label(S("errors.notes"), 18);
        for (const auto& n : notes) paragraph(n, false, kText, "-", kStepIndent);
    }

    if (!e.context.empty()) {  // Angaben der Konsole aus dem letzten Check-up (nur lesen)
        label(S("errors.live"), 18);
        if (!ctx.available) paragraph(S("errors.live_none"), false, kDim, "", 0);
        else
            for (const auto& l : ctx.lines) paragraph(format_ctx(S, l), false, kText, "-", kStepIndent);
    }

    if (e.level == "community") {
        label(S("errors.community"), 18);
        paragraph(S("errors.community_note"), false, kWarn, "", 0);
    }

    label(S("detail.source"), 18);
    paragraph(e.source, true, kText, "", 0);
    for (const auto& s : e.more_sources) {
        label(S("errors.source_more"), 8);
        paragraph(s, true, kText, "", 0);
    }
    label(S("errors.checked"), 8);
    paragraph(e.checked, true, kText, "", 0);
    return rows;
}

// Kleinster Zeilenindex, ab dem der Rest noch in den Textbereich passt: weiter zu scrollen ist sinnlos.
int max_first_row(const std::vector<ErrRow>& rows) {
    int h = 0, first = static_cast<int>(rows.size());
    while (first > 0 && h + row_height(rows[static_cast<std::size_t>(first - 1)]) <= kErrAvail) h += row_height(rows[static_cast<std::size_t>(--first)]);
    return first;
}

// QR-Code schwarz auf weiss mit Ruhezone von 4 Modulen (wie fuer Scanner noetig), mittig im Feld (ax, ay, aw, ah).
void draw_qr_code(Canvas& c, const medic::QrCode& qr, int ax, int ay, int aw, int ah) {
    const int quiet = 4, modules = qr.size + 2 * quiet;
    const int px = std::clamp(std::min(aw, ah) / modules, 3, 9);
    const int side = modules * px, ox = ax + (aw - side) / 2, oy = ay + (ah - side) / 2;
    const Color white = rgb(255, 255, 255), black = rgb(0, 0, 0);
    c.fill_rect(ox, oy, side, side, white);
    for (int row = 0; row < qr.size; ++row)
        for (int col = 0; col < qr.size;) {
            if (!qr.dark(col, row)) {
                ++col;
                continue;
            }
            int run = 1;
            while (qr.dark(col + run, row)) ++run;
            c.fill_rect(ox + (quiet + col) * px, oy + (quiet + row) * px, run * px, px, black);
            col += run;
        }
}

// Quelle des Eintrags und weitere Quellen, in dieser Reihenfolge.
std::vector<std::string> error_sources(const medic::ErrorCode& e) {
    std::vector<std::string> v = {e.source};
    v.insert(v.end(), e.more_sources.begin(), e.more_sources.end());
    return v;
}

// Index des ersten Fehlercodes im Text (Muster XX-123456-1, z. B. CE-108262-9), den das Nachschlagewerk kennt; sonst -1.
int find_error_index(const medic::ErrorCodeSet& set, const std::string& text) {
    auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    constexpr std::size_t kLen = 11;
    for (std::size_t i = 0; i + kLen <= text.size(); ++i) {
        const char* s = text.c_str() + i;
        bool ok = upper(s[0]) && upper(s[1]) && s[2] == '-' && s[9] == '-' && digit(s[10]);
        for (int k = 3; ok && k < 9; ++k) ok = digit(s[k]);
        if (ok && i > 0 && (upper(text[i - 1]) || digit(text[i - 1]))) ok = false;  // mitten in einem laengeren Wort
        if (ok && i + kLen < text.size() && (digit(text[i + kLen]) || text[i + kLen] == '-')) ok = false;
        if (!ok) continue;
        const std::string id = text.substr(i, kLen);
        for (std::size_t j = 0; j < set.codes.size(); ++j)
            if (set.codes[j].id == id) return static_cast<int>(j);
    }
    return -1;
}

Color level_color(const medic::ErrorCode& e) { return e.level == "doc" ? kOk : kWarn; }

}  // namespace

App::App(IMedicService& svc, Lang lang) : svc_(svc), lang_(lang) {
    errors_ = svc_.error_codes();
    Lang saved;
    if (lang_from_code(svc_.language(), saved)) lang_ = saved;  // die in config.ini gespeicherte Sprache hat Vorrang vor dem Standard
}

std::string App::finding_text(const medic::Finding& f) const { return pick_text(lang_, f.text_de, f.text_en, "rules", f.rule_id, "text"); }

std::string App::fix_name(const std::string& id) const {
    const auto& S = Strings::get(lang_);
    const std::string key = "fix." + id;
    return S.has(key) ? S(key) : id;
}

const medic::Finding* App::current_finding() const {
    if (sel_ < 0 || sel_ >= static_cast<int>(result_.findings.size())) return nullptr;
    return &result_.findings[static_cast<std::size_t>(sel_)];
}

// ---- Logik ------------------------------------------------------------------

void App::start_check() {
    detail_from_game_ = false;
    screen_ = Screen::Running;
    pending_check_ = true;
    running_rendered_ = false;
    check_progress_ = 0;
}

void App::move_selection(int& sel, int& top, int count, int visible, int delta) {
    if (count <= 0) {
        sel = top = 0;
        return;
    }
    sel = std::clamp(sel + delta, 0, count - 1);
    if (sel < top) top = sel;
    if (sel >= top + visible) top = sel - visible + 1;
    top = std::clamp(top, 0, std::max(count - visible, 0));
}

// L1/R1: eine ganze Seite weiter bzw. zurueck. Der Ausschnitt springt um eine Seite, die Auswahl bleibt in der Zeile
// auf der Seite (an den Enden wird begrenzt).
void App::page_selection(int& sel, int& top, int count, int visible, int dir) {
    if (count <= 0) {
        sel = top = 0;
        return;
    }
    const int max_top = std::max(count - visible, 0);
    const int new_top = std::clamp(top + dir * visible, 0, max_top);
    if (new_top == top) {  // schon auf der ersten bzw. letzten Seite: zum Anfang bzw. Ende springen
        sel = dir > 0 ? std::min(top + visible, count) - 1 : top;
        return;
    }
    sel = std::clamp(sel + (new_top - top), 0, count - 1);
    top = new_top;
    if (sel < top) sel = top;
    if (sel >= top + visible) sel = std::min(top + visible - 1, count - 1);
}

void App::open_history() {
    hist_ = svc_.history();
    std::reverse(hist_.begin(), hist_.end());  // neueste zuerst
    hist_sel_ = hist_top_ = 0;
    screen_ = Screen::History;
}

void App::open_checks() {
    chk_ = svc_.checks();
    chk_sel_ = chk_top_ = 0;
    chk_unreadable_ = false;
    screen_ = Screen::CheckList;
}

// Zeigt einen gespeicherten Check-up im Befund-Bildschirm: Der aktuelle bleibt zur Seite (live_result_) und kommt mit Kreis zurueck.
void App::open_stored(int index) {
    if (index < 0 || index >= static_cast<int>(chk_.size())) return;
    medic::StoredCheck sc;
    chk_unreadable_ = !svc_.load_check(chk_[static_cast<std::size_t>(index)].id, sc);
    if (chk_unreadable_) return;
    if (!viewing_old_) {
        live_result_ = result_;
        live_has_ = has_result_;
        live_sel_ = sel_;
        live_top_ = top_;
    }
    result_ = CheckResult{};
    result_.fw = sc.fw;
    result_.model = sc.model;
    result_.rules_version = sc.rules_version;
    result_.findings = sc.findings;
    result_.coverage = sc.coverage;
    result_.resolved = sc.resolved;
    result_.metrics = sc.metrics;
    has_result_ = true;
    viewing_old_ = true;
    view_ts_ = sc.ts;
    sel_ = top_ = 0;
    screen_ = Screen::Findings;
}

void App::leave_stored() {
    if (viewing_old_) {
        result_ = live_result_;
        has_result_ = live_has_;
        sel_ = live_sel_;
        top_ = live_top_;
        live_result_ = CheckResult{};
    }
    viewing_old_ = false;
    view_ts_.clear();
    screen_ = Screen::CheckList;
}

// Vergleich des gewaehlten Check-ups mit dem naechstaelteren in der Liste
void App::open_compare(int index) {
    if (index < 0 || index + 1 >= static_cast<int>(chk_.size())) return;
    chk_unreadable_ = !svc_.load_check(chk_[static_cast<std::size_t>(index)].id, cmp_new_) || !svc_.load_check(chk_[static_cast<std::size_t>(index) + 1].id, cmp_old_);
    if (chk_unreadable_) return;
    cmp_diff_ = medic::compare_checks(cmp_new_, cmp_old_);
    cmp_top_ = 0;
    screen_ = Screen::CheckCompare;
}

void App::open_games() {
    games_ = svc_.games();
    game_sel_ = game_top_ = gd_sel_ = gd_top_ = 0;
    screen_ = Screen::GameList;
}

void App::open_report() {
    report_ = svc_.make_report(lang_ == Lang::De ? medic::ReportLang::De : medic::ReportLang::En);
    qr_ok_ = medic::qr_encode(report_.qr_text, qr_);
    screen_ = Screen::Report;
}

void App::open_error_codes() {
    errors_ = svc_.error_codes();
    err_sel_ = err_top_ = err_scroll_ = 0;
    err_from_finding_ = false;
    screen_ = Screen::ErrorCodes;
}

// Nennt der Text des gewaehlten Befunds einen Fehlercode aus dem Nachschlagewerk, dessen Index (sonst -1).
int App::linked_error_index() const {
    const medic::Finding* f = current_finding();
    return f ? find_error_index(errors_, finding_text(*f)) : -1;
}

// Quadrat im Befund-Detail: den Eintrag zum genannten Fehlercode lesen. Nur Anzeige; Kreis kommt zum Befund zurueck.
void App::open_error_for_finding() {
    const int idx = linked_error_index();
    if (idx < 0) return;
    const int n = static_cast<int>(errors_.codes.size());
    err_sel_ = idx;
    err_top_ = std::clamp(idx - (kErrVisible - 1) / 2, 0, std::max(n - kErrVisible, 0));
    err_scroll_ = 0;
    err_from_finding_ = true;
    refresh_error_context();
    screen_ = Screen::ErrorCodeDetail;
}

// Angaben der Konsole zum gewaehlten Code (nur lesen, aus dem letzten Check-up) und Zustand des Scrollens neu setzen.
void App::refresh_error_context() {
    err_ctx_ = current_error() ? svc_.code_context(*current_error()) : CodeContext{};
    scroll_streak_ = 0;
}

// Quadrat im Detail: Quelle als QR-Code. Medic ruft die Adresse nie selbst auf; das Handy liest sie.
void App::open_error_source() {
    const medic::ErrorCode* e = current_error();
    if (!e) return;
    err_src_ = 0;
    src_qr_ok_ = medic::qr_encode(e->source, src_qr_);
    screen_ = Screen::ErrorCodeSource;
}

// Hoch/Runter im Detail: eine Zeile pro Druck, beim Halten (Wiederholungen kommen im Abstand weniger Bilder) bis zu vier Zeilen.
void App::scroll_error_detail(int dir) {
    constexpr int kRepeatTicks = 12;
    if (dir != scroll_dir_ || tick_ - last_scroll_tick_ > kRepeatTicks) scroll_streak_ = 0;
    else ++scroll_streak_;
    scroll_dir_ = dir;
    last_scroll_tick_ = tick_;
    const int step = 1 + std::min(scroll_streak_ / 4, 3);
    err_scroll_ = std::clamp(err_scroll_ + dir * step, 0, error_scroll_max());
}

const medic::ErrorCode* App::current_error() const {
    if (err_sel_ < 0 || err_sel_ >= static_cast<int>(errors_.codes.size())) return nullptr;
    return &errors_.codes[static_cast<std::size_t>(err_sel_)];
}

void App::open_rules_update() {
    offer_ = svc_.check_rule_update();
    codes_offer_ = svc_.check_codes_update();
    update_stage_ = UpdateStage::Offer;
    downloaded_ = false;
    screen_ = Screen::RulesUpdate;
}

// Installiert, was geprueft und angeboten ist (Regeln und/oder Fehlercodes). Aufgerufen nur nach Kreuz auf dem Angebot.
void App::install_updates() {
    rules_attempted_ = offer_.state == medic::RuleUpdateOffer::State::Ready;
    codes_attempted_ = codes_offer_.state == medic::RuleUpdateOffer::State::Ready;
    update_result_ = rules_attempted_ ? svc_.install_rule_update() : medic::RuleUpdateResult{};
    codes_result_ = codes_attempted_ ? svc_.install_codes_update() : medic::RuleUpdateResult{};
    update_was_rollback_ = false;
    finish_updates();
}

// Nimmt die letzte Installation zurueck, je Datei nur wenn es dort etwas zuruckzunehmen gibt. Nur nach Kreuz auf der Rueckfrage.
void App::rollback_updates() {
    rules_attempted_ = offer_.can_rollback;
    codes_attempted_ = codes_offer_.can_rollback;
    update_result_ = rules_attempted_ ? svc_.rollback_rule_update() : medic::RuleUpdateResult{};
    codes_result_ = codes_attempted_ ? svc_.rollback_codes_update() : medic::RuleUpdateResult{};
    update_was_rollback_ = true;
    finish_updates();
}

// Nach einer Installation oder einem Rollback der Regeln gelten andere Regeln: die bisherigen Befunde sind veraltet.
// Neue Fehlercodes ersetzen die geladene Liste (die Auswahl dort beginnt wieder oben).
void App::finish_updates() {
    const bool rules_ok = rules_attempted_ && update_result_.ok;
    const bool codes_ok = codes_attempted_ && codes_result_.ok;
    update_stage_ = (!rules_attempted_ || update_result_.ok) && (!codes_attempted_ || codes_result_.ok) ? UpdateStage::DoneOk : UpdateStage::DoneFail;
    if (codes_ok) {
        errors_ = svc_.error_codes();
        err_sel_ = err_top_ = err_scroll_ = 0;
    }
    if (rules_ok) {
        has_result_ = false;
        result_ = CheckResult{};
        sel_ = top_ = 0;
    }
}

void App::begin_fix(int index, Screen origin) {
    if (viewing_old_) return;  // nie auf dem Stand eines alten Check-ups reparieren
    if (index < 0 || index >= static_cast<int>(result_.findings.size())) return;
    const medic::Finding& f = result_.findings[static_cast<std::size_t>(index)];
    if (f.fix.empty()) return;
    plan_ = svc_.plan_fix(f);
    previews_.assign(plan_.changes.size(), Preview{});
    for (std::size_t i = 0; i < plan_.changes.size(); ++i) {
        const auto& ch = plan_.changes[i];
        if (ch.kind != medic::Change::Kind::WriteFile) continue;
        std::vector<medic::DiffLine> all;
        previews_[i].diff_ok = medic::diff_lines(svc_.read_text(ch.path), ch.content, all);
        for (auto& d : all)
            if (d.tag != ' ') previews_[i].changed.push_back(d);
    }
    fix_origin_ = origin;
    screen_ = Screen::FixPreview;
}

void App::handle(Button b) {
    if (screen_ == Screen::Splash) {  // jede Taste ueberspringt das Startbild
        screen_ = Screen::Start;
        return;
    }
    switch (screen_) {
        case Screen::Start:
            // Die grosse Kachel (0) liegt ueber den fuenf kleinen (1..5), die nebeneinander liegen: Hoch von jeder kleinen Kachel fuehrt
            // gleich zur grossen, Runter von der grossen wieder zu der kleinen Kachel, von der man gekommen ist (start_small_).
            if (b == Button::Up) {
                if (start_sel_ >= 1) start_small_ = start_sel_;
                start_sel_ = 0;
            } else if (b == Button::L1) start_sel_ = std::max(start_sel_ - 1, 0);
            else if (b == Button::Down && start_sel_ == 0) start_sel_ = std::clamp(start_small_, 1, 5);
            else if (b == Button::Down || b == Button::R1) {
                start_sel_ = std::min(start_sel_ + 1, 5);
            } else if (b == Button::Left && start_sel_ >= 2) --start_sel_;
            else if (b == Button::Right && start_sel_ >= 1 && start_sel_ < 5) ++start_sel_;
            else if (b == Button::Cross) {
                if (start_sel_ == 0) {
                    start_small_ = 1;  // wer den Check-up startet, beginnt beim naechsten Mal wieder bei der ersten kleinen Kachel
                    start_check();
                }
                else if (start_sel_ == 1) open_checks();
                else if (start_sel_ == 2) screen_ = Screen::Settings;
                else if (start_sel_ == 3) open_error_codes();
                else if (start_sel_ == 4) open_selftest();
                else if (has_result_) open_games();
                else {  // ohne Check-up gibt es nichts zu zeigen: erst pruefen, dann die Liste
                    after_check_games_ = true;
                    start_check();
                }
            } else if (b == Button::Circle) quit_ = true;
            break;
        case Screen::Running:
            break;  // nicht unterbrechbar (dauert Sekunden)
        case Screen::Splash:
            break;  // oben behandelt: jede Taste ueberspringt das Startbild
        case Screen::Findings:
            if (b == Button::Up) move_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, -1);
            else if (b == Button::Down) move_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, 1);
            else if (b == Button::L1) page_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, -1);
            else if (b == Button::R1) page_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, 1);
            else if (b == Button::Cross) {
                if (result_.findings.empty()) {
                    if (!viewing_old_) start_check();
                } else screen_ = Screen::Detail;
            } else if (b == Button::Triangle) begin_fix(sel_, Screen::Findings);
            else if (b == Button::Square) {
                if (!viewing_old_) open_report();
            } else if (b == Button::Circle) {
                if (viewing_old_) leave_stored();
                else screen_ = Screen::Start;
            }
            break;
        case Screen::Detail:
            // Zum vorherigen/naechsten Befund blaettern, ohne zur Liste zurueckzugehen (Steuerkreuz/Stick links-rechts oder L1/R1).
            if (b == Button::Left || b == Button::L1) move_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, -1);
            else if (b == Button::Right || b == Button::R1) move_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, 1);
            else if (b == Button::Triangle) {
                if (!viewing_old_) begin_fix(sel_, Screen::Detail);
            }
            else if (b == Button::Square) open_error_for_finding();
            else if (b == Button::Circle) screen_ = detail_from_game_ ? Screen::GameDetail : Screen::Findings;
            break;
        case Screen::FixPreview:
            if (b == Button::Cross && !plan_.empty()) {
                const medic::ApplyResult r = svc_.apply_fix(plan_);
                detail_from_game_ = false;
                done_kind_ = DoneKind::Fix;
                done_ok_ = r.ok;
                done_text_ = r.ok ? r.txn : r.error;
                verify_ = r.ok ? Verify::Pending : Verify::None;  // nach einer Reparatur erneut pruefen
                verify_rule_ = plan_.rule_id;
                verify_rendered_ = false;
                screen_ = Screen::FixDone;
            } else if (b == Button::Circle) screen_ = fix_origin_;
            break;
        case Screen::FixDone:
            if (done_kind_ == DoneKind::Fix) {
                if (b == Button::Cross) {
                    // Ist die erneute Pruefung schon gelaufen, sind die Befunde frisch; sonst (noch nicht gezeichnet) wie bisher neu pruefen.
                    if (verify_ == Verify::Fixed || verify_ == Verify::Persists) screen_ = Screen::Findings;
                    else start_check();
                }
                else if (b == Button::Circle) screen_ = Screen::Start;
            } else if (b == Button::Cross || b == Button::Circle) {
                open_history();
            }
            break;
        case Screen::CheckList:
            if (b == Button::Up) move_selection(chk_sel_, chk_top_, static_cast<int>(chk_.size()), kChkVisible, -1);
            else if (b == Button::Down) move_selection(chk_sel_, chk_top_, static_cast<int>(chk_.size()), kChkVisible, 1);
            else if (b == Button::L1) page_selection(chk_sel_, chk_top_, static_cast<int>(chk_.size()), kChkVisible, -1);
            else if (b == Button::R1) page_selection(chk_sel_, chk_top_, static_cast<int>(chk_.size()), kChkVisible, 1);
            else if (b == Button::Cross) open_stored(chk_sel_);
            else if (b == Button::Triangle) open_compare(chk_sel_);
            else if (b == Button::Square) open_history();
            else if (b == Button::Circle) screen_ = Screen::Start;
            break;
        case Screen::GameList:
            if (b == Button::Up) move_selection(game_sel_, game_top_, static_cast<int>(games_.size()), kGameVisible, -1);
            else if (b == Button::Down) move_selection(game_sel_, game_top_, static_cast<int>(games_.size()), kGameVisible, 1);
            else if (b == Button::L1) page_selection(game_sel_, game_top_, static_cast<int>(games_.size()), kGameVisible, -1);
            else if (b == Button::R1) page_selection(game_sel_, game_top_, static_cast<int>(games_.size()), kGameVisible, 1);
            else if (b == Button::Cross) {
                if (!games_.empty()) {
                    gd_sel_ = gd_top_ = 0;
                    screen_ = Screen::GameDetail;
                }
            } else if (b == Button::Circle) screen_ = Screen::Start;
            break;
        case Screen::GameDetail: {
            const int n = game_sel_ >= 0 && game_sel_ < static_cast<int>(games_.size()) ? static_cast<int>(games_[static_cast<std::size_t>(game_sel_)].findings.size()) : 0;
            if (b == Button::Up) move_selection(gd_sel_, gd_top_, n, kGdVisible, -1);
            else if (b == Button::Down) move_selection(gd_sel_, gd_top_, n, kGdVisible, 1);
            else if (b == Button::L1 || b == Button::R1 || b == Button::Left || b == Button::Right) {  // vorheriges bzw. naechstes Spiel
                move_selection(game_sel_, game_top_, static_cast<int>(games_.size()), kGameVisible, (b == Button::L1 || b == Button::Left) ? -1 : 1);
                gd_sel_ = gd_top_ = 0;
            } else if (b == Button::Cross) {
                if (n > 0) {
                    const std::size_t idx = games_[static_cast<std::size_t>(game_sel_)].findings[static_cast<std::size_t>(gd_sel_)];
                    if (idx < result_.findings.size()) {
                        sel_ = static_cast<int>(idx);
                        top_ = std::clamp(sel_ - kListVisible + 1, 0, std::max(static_cast<int>(result_.findings.size()) - kListVisible, 0));
                        detail_from_game_ = true;
                        screen_ = Screen::Detail;
                    }
                }
            } else if (b == Button::Circle) screen_ = Screen::GameList;
            break;
        }
        case Screen::CheckCompare: {
            const int rows = static_cast<int>(cmp_diff_.added.size() + cmp_diff_.resolved.size() + cmp_diff_.unchanged.size()) + 3;
            const int max_top = std::max(rows - kCmpVisible, 0);
            if (b == Button::Up) cmp_top_ = std::max(cmp_top_ - 1, 0);
            else if (b == Button::Down) cmp_top_ = std::min(cmp_top_ + 1, max_top);
            else if (b == Button::L1) cmp_top_ = std::max(cmp_top_ - kCmpVisible, 0);
            else if (b == Button::R1) cmp_top_ = std::min(cmp_top_ + kCmpVisible, max_top);
            else if (b == Button::Circle) screen_ = Screen::CheckList;
            break;
        }
        case Screen::History:
            if (b == Button::Up) move_selection(hist_sel_, hist_top_, static_cast<int>(hist_.size()), kHistVisible, -1);
            else if (b == Button::Down) move_selection(hist_sel_, hist_top_, static_cast<int>(hist_.size()), kHistVisible, 1);
            else if (b == Button::L1) page_selection(hist_sel_, hist_top_, static_cast<int>(hist_.size()), kHistVisible, -1);
            else if (b == Button::R1) page_selection(hist_sel_, hist_top_, static_cast<int>(hist_.size()), kHistVisible, 1);
            else if (b == Button::Cross) {
                // "incomplete" (Stromausfall/Abbruch mitten im Fix): die Engine rollt anhand des Journals zurueck.
                if (hist_sel_ >= 0 && hist_sel_ < static_cast<int>(hist_.size()) && undoable(hist_[static_cast<std::size_t>(hist_sel_)].state)) {
                    undo_target_ = hist_[static_cast<std::size_t>(hist_sel_)];
                    screen_ = Screen::UndoConfirm;
                }
            } else if (b == Button::Circle) open_checks();
            break;
        case Screen::UndoConfirm:
            if (b == Button::Cross) {
                const medic::UndoResult r = svc_.undo(undo_target_.txn);
                done_kind_ = DoneKind::Undo;
                done_ok_ = r.ok;
                done_text_ = r.ok ? undo_target_.txn : r.error;
                verify_ = Verify::None;
                screen_ = Screen::FixDone;
            } else if (b == Button::Circle) screen_ = Screen::History;
            break;
        case Screen::Settings:
            if (b == Button::Up || b == Button::L1) settings_sel_ = 0;
            else if (b == Button::Down || b == Button::R1) settings_sel_ = 1;
            else if (settings_sel_ == 0 && (b == Button::Left || b == Button::Right || b == Button::Cross)) {
                const int n = static_cast<int>(sizeof kAllLangs / sizeof kAllLangs[0]);
                int i = 0;
                while (i < n && kAllLangs[i] != lang_) ++i;
                lang_ = kAllLangs[(i + (b == Button::Left ? n - 1 : 1)) % n];
                (void)svc_.set_language(lang_code(lang_));  // scheitert das Speichern, gilt die Sprache nur fuer diese Sitzung
            }
            else if (settings_sel_ == 1 && b == Button::Cross) open_rules_update();
            else if (b == Button::Circle) screen_ = Screen::Start;
            break;
        case Screen::RulesUpdate:
            switch (update_stage_) {
                case UpdateStage::Offer:
                    if (b == Button::Cross && (offer_.state == medic::RuleUpdateOffer::State::Ready || codes_offer_.state == medic::RuleUpdateOffer::State::Ready)) {
                        install_updates();
                    } else if (b == Button::Triangle && (offer_.can_rollback || codes_offer_.can_rollback)) {
                        update_stage_ = UpdateStage::ConfirmRollback;
                    } else if (b == Button::Square) {
                        download_url_ = svc_.rule_update_url();
                        update_stage_ = UpdateStage::ConfirmDownload;
                    } else if (b == Button::Circle) {
                        screen_ = Screen::Settings;
                    }
                    break;
                case UpdateStage::ConfirmDownload:
                    if (b == Button::Cross && !download_url_.empty()) {
                        update_stage_ = UpdateStage::Downloading;
                        pending_download_ = true;
                        download_rendered_ = false;
                    } else if (b == Button::Circle) {
                        update_stage_ = UpdateStage::Offer;
                    }
                    break;
                case UpdateStage::Downloading:
                    break;  // nicht unterbrechbar (hoechstens ein paar Sekunden)
                case UpdateStage::DownloadFail:
                    if (b == Button::Cross || b == Button::Circle) update_stage_ = UpdateStage::Offer;
                    break;
                case UpdateStage::ConfirmRollback:
                    if (b == Button::Cross) {
                        rollback_updates();
                    } else if (b == Button::Circle) {
                        update_stage_ = UpdateStage::Offer;
                    }
                    break;
                case UpdateStage::DoneOk:
                case UpdateStage::DoneFail:
                    if (b == Button::Cross || b == Button::Circle) screen_ = Screen::Settings;
                    break;
            }
            break;
        case Screen::Report:
            if (b == Button::Circle) screen_ = Screen::Findings;
            break;
        case Screen::ErrorCodes:  // Nachschlagen: nur lesen, keine Taste loest etwas aus
            if (b == Button::Up) move_selection(err_sel_, err_top_, static_cast<int>(errors_.codes.size()), kErrVisible, -1);
            else if (b == Button::Down) move_selection(err_sel_, err_top_, static_cast<int>(errors_.codes.size()), kErrVisible, 1);
            else if (b == Button::L1) page_selection(err_sel_, err_top_, static_cast<int>(errors_.codes.size()), kErrVisible, -1);
            else if (b == Button::R1) page_selection(err_sel_, err_top_, static_cast<int>(errors_.codes.size()), kErrVisible, 1);
            else if (b == Button::Cross) {
                if (current_error()) {
                    err_scroll_ = 0;
                    err_from_finding_ = false;
                    refresh_error_context();
                    screen_ = Screen::ErrorCodeDetail;
                }
            } else if (b == Button::Circle) screen_ = Screen::Start;
            break;
        case Screen::ErrorCodeDetail:
            // Hoch/Runter scrollt den Text, Links/Rechts und L1/R1 blaettern zum vorherigen/naechsten Code.
            if (b == Button::Up || b == Button::Down) {
                scroll_error_detail(b == Button::Down ? 1 : -1);
            } else if (!err_from_finding_ && (b == Button::Left || b == Button::L1 || b == Button::Right || b == Button::R1)) {
                const int before = err_sel_;
                move_selection(err_sel_, err_top_, static_cast<int>(errors_.codes.size()), kErrVisible, (b == Button::Left || b == Button::L1) ? -1 : 1);
                if (err_sel_ != before) {
                    err_scroll_ = 0;
                    refresh_error_context();
                }
            } else if (b == Button::Square) {
                open_error_source();
            } else if (b == Button::Triangle) {  // fuer den Bericht vermerken bzw. wieder entfernen (nur im Speicher, geschrieben wird erst beim Bericht)
                if (const medic::ErrorCode* e = current_error()) {
                    reported_error_ = reported_error_ == e->id ? std::string() : e->id;
                    svc_.set_reported_error(reported_error_);
                }
            } else if (b == Button::Circle) {
                screen_ = err_from_finding_ ? Screen::Detail : Screen::ErrorCodes;
                err_from_finding_ = false;
            }
            break;
        case Screen::SelfTest:
            if (st_state_ == StState::List || st_state_ == StState::Done) {
                if (b == Button::Cross) {
                    st_tier_ = 1;
                    st_state_ = StState::Confirm;
                } else if (b == Button::Triangle) {
                    st_tier_ = 2;
                    st_state_ = StState::Confirm;
                } else if (b == Button::Square) start_selftest(0);
                else if (b == Button::L1) open_usertest(true);
                else if (b == Button::R1) open_usertest(false);
                else if (b == Button::Circle) screen_ = Screen::Start;
            } else if (st_state_ == StState::Confirm) {
                if (b == Button::Cross) start_selftest(st_tier_);
                else if (b == Button::Circle) st_state_ = StState::List;
            } else if (b == Button::Circle) {
                st_cancel_ = true;  // wirkt nach dem laufenden Test
            }
            break;
        case Screen::UserTest: handle_usertest(b); break;
        case Screen::ErrorCodeSource:  // QR-Code der Quelle: nur Anzeige
            if (const medic::ErrorCode* e = current_error()) {
                const int n = static_cast<int>(error_sources(*e).size());
                if ((b == Button::Left || b == Button::L1 || b == Button::Right || b == Button::R1) && n > 1) {
                    err_src_ = std::clamp(err_src_ + ((b == Button::Left || b == Button::L1) ? -1 : 1), 0, n - 1);
                    src_qr_ok_ = medic::qr_encode(error_sources(*e)[static_cast<std::size_t>(err_src_)], src_qr_);
                }
            }
            if (b == Button::Circle) screen_ = Screen::ErrorCodeDetail;
            break;
    }
}

void App::set_background(const Canvas& image) {
    bgimage_ = std::make_unique<Canvas>(image.width(), image.height());
    bgimage_->copy_from(image);
    bg_.reset();  // wird beim naechsten Zeichnen neu gebaut
}

void App::show_splash(const Canvas& image) {
    splash_ = std::make_unique<Canvas>(image.width(), image.height());
    splash_->copy_from(image);
    splash_until_ = tick_ + 80;  // etwa 2,7 Sekunden bei rund 30 Bildern/s
    screen_ = Screen::Splash;
}

CheckResult App::run_check_with_progress() {
    check_progress_ = 0;
    svc_.set_progress([this](double v) {
        check_progress_ = std::clamp(v, 0.0, 1.0);
        ++tick_;  // die Punkte hinter "Pruefe" laufen mit
        if (redraw_) redraw_();
    });
    CheckResult r = svc_.run_check();
    svc_.set_progress(nullptr);
    return r;
}

void App::update() {
    ++tick_;
    if (screen_ == Screen::Splash && tick_ >= splash_until_) screen_ = Screen::Start;
    if (pending_check_ && running_rendered_) {  // erst ausfuehren, wenn "Pruefe..." sichtbar war
        pending_check_ = false;
        result_ = run_check_with_progress();
        has_result_ = true;
        sel_ = top_ = 0;
        screen_ = Screen::Findings;
        if (after_check_games_) {
            after_check_games_ = false;
            open_games();
        }
    }
    if (screen_ == Screen::SelfTest && st_state_ == StState::Running && st_rendered_) step_selftest();
    if (verify_ == Verify::Pending && verify_rendered_ && screen_ == Screen::FixDone) {  // Re-Detect nach der Reparatur
        result_ = svc_.run_check();
        has_result_ = true;
        sel_ = top_ = 0;
        const bool still = std::any_of(result_.findings.begin(), result_.findings.end(), [&](const medic::Finding& f) { return f.rule_id == verify_rule_; });
        verify_ = still ? Verify::Persists : Verify::Fixed;
    }
    if (pending_download_ && download_rendered_) {  // erst ausfuehren, wenn "Lade herunter ..." sichtbar war
        pending_download_ = false;
        download_result_ = svc_.download_rule_update();
        if (download_result_.ok) {
            offer_ = svc_.check_rule_update();  // Echtheit, Schema und Version prueft der Kern, nicht der Download
            codes_offer_ = svc_.check_codes_update();
            downloaded_ = true;
            update_stage_ = UpdateStage::Offer;
        } else {
            update_stage_ = UpdateStage::DownloadFail;
        }
    }
}

// ---- Darstellung ------------------------------------------------------------

void App::render(Canvas& c) const {
    if (!bg_ || bg_->width() != c.width() || bg_->height() != c.height()) {  // Hintergrund einmal bauen: Verlauf und zwei weiche Lichtflecken
        bg_ = std::make_unique<Canvas>(c.width(), c.height());
        build_background(*bg_, bgimage_ && bgimage_->width() == c.width() && bgimage_->height() == c.height() ? bgimage_.get() : nullptr);
    }
    if (screen_ == Screen::Splash && splash_) {
        c.copy_from(*splash_);
        return;
    }
    c.copy_from(*bg_);
    draw_chrome(c);
    switch (screen_) {
        case Screen::Start: draw_start(c); break;
        case Screen::Running: running_rendered_ = true; draw_running(c); break;
        case Screen::Findings: draw_findings(c); break;
        case Screen::Detail: draw_detail(c); break;
        case Screen::FixPreview: draw_fix_preview(c); break;
        case Screen::FixDone:
            if (verify_ == Verify::Pending) verify_rendered_ = true;
            draw_fix_done(c);
            break;
        case Screen::History: draw_history(c); break;
        case Screen::UndoConfirm: draw_undo_confirm(c); break;
        case Screen::CheckList: draw_checklist(c); break;
        case Screen::CheckCompare: draw_compare(c); break;
        case Screen::GameList: draw_gamelist(c); break;
        case Screen::GameDetail: draw_gamedetail(c); break;
        case Screen::Splash: break;  // unten: das Bild ersetzt alles
        case Screen::Settings: draw_settings(c); break;
        case Screen::Report: draw_report(c); break;
        case Screen::RulesUpdate: draw_rules_update(c); break;
        case Screen::ErrorCodes: draw_error_codes(c); break;
        case Screen::ErrorCodeDetail: draw_error_detail(c); break;
        case Screen::ErrorCodeSource: draw_error_source(c); break;
        case Screen::SelfTest:
            if (st_state_ == StState::Running) st_rendered_ = true;
            draw_selftest(c);
            break;
        case Screen::UserTest: draw_usertest(c); break;
    }
}

void App::open_selftest() {
    st_defs_ = svc_.selftest_defs();
    st_last_ = svc_.selftest_last();
    st_state_ = StState::List;
    screen_ = Screen::SelfTest;
}

void App::start_selftest(int tier_limit) {
    st_tier_ = tier_limit;
    st_ids_.clear();
    for (std::size_t i = 0; i < st_defs_.size(); ++i)
        if (static_cast<int>(st_defs_[i].tier) <= tier_limit) st_ids_.push_back(i);
    st_run_.assign(st_ids_.size(), medic::TestResult{});
    for (std::size_t i = 0; i < st_ids_.size(); ++i) st_run_[i].id = st_defs_[st_ids_[i]].id;
    st_next_ = 0;
    st_cancel_ = st_begun_ = st_item_started_ = st_abort_sent_ = false;
    st_progress_ = 0;
    st_rendered_ = false;
    st_state_ = StState::Running;
}

// Ein Schritt je update(): erst der Marker, dann ein Test nach dem anderen, jeder in kleinen Schritten (Fortschritt, Abbruch jederzeit); nach jedem
// Schritt wird neu gezeichnet. Kreis meldet den Abbruch an den laufenden Test; der Rest steht danach als abgebrochen im Ergebnis.
void App::step_selftest() {
    if (!st_begun_) {
        std::vector<std::string> ids;
        for (std::size_t i : st_ids_) ids.push_back(st_defs_[i].id);
        svc_.selftest_begin(ids);
        st_begun_ = true;
        st_rendered_ = false;
        return;
    }
    if (st_cancel_ && !st_abort_sent_ && st_item_started_) {
        svc_.selftest_abort();
        st_abort_sent_ = true;
    }
    if (st_next_ >= st_run_.size() || (st_cancel_ && !st_item_started_)) {
        for (std::size_t i = st_next_; i < st_run_.size(); ++i) {  // Rest: abgebrochen, mit Grund, nie "bestanden"
            st_run_[i].status = medic::TestStatus::Cancelled;
            st_run_[i].reason = "cancelled";
        }
        svc_.selftest_end(st_run_, st_cancel_);
        st_last_ = st_run_;
        st_state_ = StState::Done;
        return;
    }
    if (!st_item_started_) {
        svc_.selftest_start(st_run_[st_next_].id);
        st_item_started_ = true;
        st_progress_ = 0;
    }
    const SelftestStep info = svc_.selftest_step();
    st_progress_ = info.progress;
    if (info.done) {
        st_run_[st_next_] = info.result;
        st_run_[st_next_].id = st_defs_[st_ids_[st_next_]].id;
        ++st_next_;
        st_item_started_ = false;
        st_abort_sent_ = false;
        st_progress_ = 0;
    }
    st_rendered_ = false;
}

void App::draw_selftest(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    auto status_color = [](medic::TestStatus st) {
        switch (st) {
            case medic::TestStatus::Passed: return kOk;
            case medic::TestStatus::Warn: return kWarn;
            case medic::TestStatus::Failed:
            case medic::TestStatus::Error: return kCrit;
            default: return kDim;
        }
    };
    title_icon(c, Icon::Checklist, kTintTeal, S("selftest.title"));

    if (st_state_ == StState::Confirm) {
        int total = 0;
        bool reads = false;
        for (const auto& d : st_defs_) {
            if (static_cast<int>(d.tier) > st_tier_) continue;
            total += d.est_seconds;
            reads = reads || d.risk == "read";
        }
        c.fill_round_rect(M, 230, W - 2 * M, 460, 24, kPanel);
        draw_icon_badge(c, st_tier_ >= 2 ? Icon::Flame : Icon::Checklist, M + 48, 254, 72, st_tier_ >= 2 ? kTintOrange : kTintTeal);
        body().draw(c, M + 144, 254 + (72 - body().line_height()) / 2, S(st_tier_ >= 2 ? "selftest.confirm_title_thorough" : "selftest.confirm_title"), st_tier_ >= 2 ? kWarn : kText);
        int y = 346;
        for (auto& l : wrap_text(body(), S.fmt(st_tier_ >= 2 ? "selftest.confirm_thorough" : (reads ? "selftest.confirm_read" : "selftest.confirm_none"), {{"s", std::to_string(total)}}), W - 2 * M - 96, 9)) {
            body().draw(c, M + 48, y, l, kDim);
            y += body().line_height() + 6;
        }
        draw_hints(c, {{PadGlyph::Cross, S("selftest.start")}, {PadGlyph::Circle, S("common.back")}});
        return;
    }

    if (st_state_ == StState::Running) {
        const std::size_t n = st_run_.size();
        c.fill_round_rect(M, 230, W - 2 * M, 330, 24, kPanel);
        const std::size_t shown = std::min(st_next_, n ? n - 1 : 0);
        const std::string name = n ? pick_text(lang_, st_defs_[st_ids_[shown]].name_de, st_defs_[st_ids_[shown]].name_en, "selftests", st_defs_[st_ids_[shown]].id, "name") : std::string();
        {
            const auto look = test_look(n ? st_defs_[st_ids_[shown]].id : std::string());
            draw_icon_badge(c, look.first, M + 48, 256, 120, look.second);
        }
        body().draw(c, M + 204, 270, S.fmt("selftest.running", {{"i", std::to_string(std::min(st_next_ + 1, n))}, {"n", std::to_string(n)}}), kText);
        small().draw(c, M + 204, 330, name, kDim);
        const int bw = W - 2 * M - 96;
        c.fill_round_rect(M + 48, 400, bw, 20, 10, kLine);
        if (n) c.fill_round_rect(M + 48, 400, std::max(20, static_cast<int>(bw * (static_cast<double>(st_next_) + st_progress_) / static_cast<double>(n))), 20, 10, kAccent);
        if (st_cancel_) small().draw(c, M + 48, 450, S("selftest.cancelling"), kWarn);
        draw_hints(c, {{PadGlyph::Circle, S("selftest.abort")}});
        return;
    }

    // Liste: letztes Ergebnis je Test (bei Done das frische)
    small().draw(c, M + 4, 196, S("selftest.sub"), kDim);
    int y = 230;
    int passed = 0, warn = 0, failed = 0, skipped = 0;
    for (const auto& d : st_defs_) {
        const medic::TestResult* r = nullptr;
        for (const auto& x : st_last_)
            if (x.id == d.id) r = &x;
        const medic::TestStatus st = r ? r->status : medic::TestStatus::NotRun;
        if (st == medic::TestStatus::Passed) ++passed;
        else if (st == medic::TestStatus::Warn) ++warn;
        else if (st == medic::TestStatus::Failed || st == medic::TestStatus::Error) ++failed;
        else if (st == medic::TestStatus::Skipped) ++skipped;
        c.fill_round_rect(M, y, W - 2 * M, 58, 14, kPanel);
        {
            const auto look = test_look(d.id);
            draw_icon_badge(c, look.first, M + 12, y + 5, 48, look.second);
        }
        body().draw(c, M + 80, y + 0, pick_text(lang_, d.name_de, d.name_en, "selftests", d.id, "name"), kText, W - 2 * M - 480);
        std::string sub = S(d.tier == medic::TestTier::Quick ? "selftest.tier_quick" : (d.tier == medic::TestTier::Standard ? "selftest.tier_standard" : (d.tier == medic::TestTier::Thorough ? "selftest.tier_thorough" : "selftest.tier_user"))) + " · " + S.fmt("selftest.est", {{"s", std::to_string(d.est_seconds)}});
        if (r && !r->reason.empty()) sub += " · " + (S.has("selftest.reason." + r->reason) ? S("selftest.reason." + r->reason) : r->reason);
        small().draw(c, M + 80, y + 29, elide(small(), sub, W - 2 * M - 480), kDim);
        const std::string label = S(std::string("selftest.status.") + medic::to_string(st));
        const int bx = W - M - 32 - (small().width(label) + 24);
        draw_badge(c, bx, y + 12, label, status_color(st));
        draw_icon(c, status_icon(st), bx - 28, y + 29, 30, status_color(st), status_color(st));
        y += 63;
    }
    if (!st_last_.empty())
        small().draw(c, M + 4, y + 6, S.fmt("selftest.summary", {{"p", std::to_string(passed)}, {"w", std::to_string(warn)}, {"f", std::to_string(failed)}, {"s", std::to_string(skipped)}}), kDim);
    else small().draw(c, M + 4, y + 6, S("selftest.none_yet"), kDim);
    draw_hints(c, {{PadGlyph::Cross, S("selftest.all")}, {PadGlyph::Square, S("selftest.quick")}, {PadGlyph::Triangle, S("selftest.thorough")}, {PadGlyph::Shoulders, S("selftest.user")}, {PadGlyph::Circle, S("common.back")}});
}

void App::open_usertest(bool pad) {
    ut_pad_ = pad;
    for (bool& x : ut_pressed_) x = false;
    ut_presses_ = 0;
    ut_last_circle_ = false;
    ut_color_ = 0;
    ut_asking_ = false;
    screen_ = Screen::UserTest;
}

void App::finish_usertest(medic::TestStatus st, const std::string& reason, double missing) {
    medic::TestResult r;
    r.id = ut_pad_ ? "hw.pad.buttons" : "hw.display.pattern";
    r.status = st;
    r.reason = reason;
    if (ut_pad_) {
        r.measurements.push_back({"buttons_pressed", static_cast<double>(ut_presses_), ""});
        r.measurements.push_back({"buttons_missing", missing, ""});
    }
    if (st != medic::TestStatus::NotRun) {
        svc_.selftest_record(r);
        bool found = false;
        for (auto& x : st_last_)
            if (x.id == r.id) {
                x = r;
                found = true;
            }
        if (!found) st_last_.push_back(r);
    }
    screen_ = Screen::SelfTest;
}

// Pad: jede der elf Tasten einmal druecken; Kreis zweimal hintereinander beendet vorzeitig (ein einzelner Kreis zaehlt als Taste). Anzeige: Kreuz
// blaettert durch sechs Farbflaechen, dann das Urteil (Kreuz nichts aufgefallen, Dreieck Auffaelligkeit, Kreis weiss nicht = uebersprungen).
void App::handle_usertest(Button b) {
    if (ut_pad_) {
        const int idx = static_cast<int>(b);
        if (idx >= 0 && idx < 11 && !ut_pressed_[idx]) {
            ut_pressed_[idx] = true;
            ++ut_presses_;
        }
        const bool again = b == Button::Circle && ut_last_circle_;
        ut_last_circle_ = b == Button::Circle;
        if (ut_presses_ == 11) {
            finish_usertest(medic::TestStatus::Passed, "", 0);
        } else if (again) {
            const double missing = 11 - ut_presses_;
            if (ut_presses_ >= 3) finish_usertest(medic::TestStatus::Warn, "buttons_missing", missing);
            else finish_usertest(medic::TestStatus::Skipped, "not_tested", missing);
        }
        return;
    }
    if (!ut_asking_) {
        if (b == Button::Cross) {
            if (++ut_color_ >= 6) ut_asking_ = true;
        } else if (b == Button::Circle) {
            screen_ = Screen::SelfTest;  // abgebrochen: kein Ergebnis
        }
        return;
    }
    if (b == Button::Cross) finish_usertest(medic::TestStatus::Passed, "", 0);
    else if (b == Button::Triangle) finish_usertest(medic::TestStatus::Warn, "user_reported", 0);
    else if (b == Button::Circle) finish_usertest(medic::TestStatus::Skipped, "unknown", 0);
}

void App::draw_usertest(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    if (!ut_pad_ && !ut_asking_) {
        // Vollbild-Farbflaechen (ruhig, ohne Blinken): schwarz, weiss, rot, gruen, blau, Verlauf
        const Color cols[5] = {rgb(0, 0, 0), rgb(255, 255, 255), rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255)};
        if (ut_color_ < 5) {
            c.clear(cols[ut_color_]);
        } else {
            c.clear(rgb(0, 0, 0));
            for (int x = 0; x < W; ++x) {
                const int v = x * 255 / (W - 1);
                c.fill_rect(x, 0, 1, H / 3, rgb(v, v, v));
                c.fill_rect(x, H / 3, 1, H / 3, rgb(v, 0, 0));
                c.fill_rect(x, 2 * H / 3, 1, H - 2 * H / 3, rgb(0, 0, v));
            }
        }
        const Color hint = ut_color_ == 1 ? rgb(0, 0, 0) : rgb(255, 255, 255);
        small().draw(c, 40, H - 60, S.fmt("usertest.display_hint", {{"i", std::to_string(ut_color_ + 1)}, {"n", "6"}}), hint);
        return;
    }
    title_icon(c, ut_pad_ ? Icon::Gamepad : Icon::Display, kTintGreen, S(ut_pad_ ? "usertest.pad_title" : "usertest.display_title"));
    if (ut_pad_) {
        small().draw(c, M + 4, 192, S("usertest.pad_sub"), kDim);
        const char* names[11] = {"usertest.btn.up", "usertest.btn.down", "usertest.btn.left", "usertest.btn.right", "usertest.btn.cross", "usertest.btn.circle",
                                 "usertest.btn.square", "usertest.btn.triangle", "usertest.btn.l1", "usertest.btn.r1", "usertest.btn.options"};
        const int cols = 4, gap = 24, cw = (W - 2 * M - (cols - 1) * gap) / cols, ch = 130;
        for (int i = 0; i < 11; ++i) {
            const int x = M + (i % cols) * (cw + gap), y = 250 + (i / cols) * (ch + gap);
            c.fill_round_rect(x, y, cw, ch, 16, ut_pressed_[i] ? kOk : kPanel);
            body().draw(c, x + 32, y + 44, S(names[i]), ut_pressed_[i] ? kBg : kText, cw - 120);
            draw_icon(c, ut_pressed_[i] ? Icon::Ok : Icon::Circle, x + cw - 48, y + 65, 40, ut_pressed_[i] ? kBg : kLine, ut_pressed_[i] ? kBg : kLine);
        }
        small().draw(c, M + 4, 250 + 3 * (ch + gap) + 6, S.fmt("usertest.pad_count", {{"n", std::to_string(ut_presses_)}}), kDim);
        draw_hints(c, {{PadGlyph::Circle, S("usertest.pad_exit")}});
        return;
    }
    c.fill_round_rect(M, 230, W - 2 * M, 330, 24, kPanel);
    int y = 270;
    for (auto& l : wrap_text(body(), S("usertest.display_question"), W - 2 * M - 96, 6)) {
        body().draw(c, M + 48, y, l, kText);
        y += body().line_height() + 6;
    }
    draw_hints(c, {{PadGlyph::Cross, S("usertest.nothing")}, {PadGlyph::Triangle, S("usertest.noticed")}, {PadGlyph::Circle, S("usertest.dontknow")}});
}

void App::draw_chrome(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    draw_icon_badge(c, Icon::Medic, M, 24, 66, kTintBlue);
    title().draw(c, M + 90, 28, S("app.title"), kText);
    const std::string sys = has_result_ && !result_.fw.empty() ? S.fmt("header.system", {{"fw", result_.fw}, {"model", result_.model}})
                                                                : (has_result_ ? S("header.unknown") : std::string());
    int right = W - M;
    if (!sys.empty()) {
        const int w = small().width(sys);
        c.fill_round_rect(W - M - w - 64, 36, w + 64, 44, 22, rgba(255, 255, 255, 14));
        draw_icon(c, Icon::Gamepad, W - M - w - 64 + 28, 58, 26, kDim, kDim);
        small().draw(c, W - M - w - 18, 44, sys, kDim);
        right = W - M - w - 64 - 16;
    }
    if (viewing_old_) {  // ein gespeicherter Check-up: gut sichtbar, damit man ihn nie fuer den aktuellen haelt
        const std::string t = S.fmt("checks.old_banner", {{"ts", pretty_ts(view_ts_)}});
        const int w = small().width(t) + 64;
        c.fill_round_rect(right - w, 36, w, 44, 22, rgba(255, 179, 71, 40));
        draw_icon(c, Icon::History, right - w + 28, 58, 26, kWarn, kWarn);
        small().draw(c, right - w + 50, 44, t, kWarn);
    }
    c.fill_rect(M, 112, W - 2 * M, 2, kLine);
    c.fill_rect(M, 112, 160, 2, kAccent);
}

void App::draw_hints(Canvas& c, const std::vector<std::pair<PadGlyph, std::string>>& hints) const {
    int x = M;
    const int y = H - 92;
    c.fill_round_rect(40, H - 108, W - 80, 80, 22, rgba(255, 255, 255, 12));
    for (const auto& [g, label] : hints) {
        Color col = kText;
        switch (g) {
            case PadGlyph::Cross: col = rgb(110, 168, 254); break;
            case PadGlyph::Circle: col = rgb(255, 107, 107); break;
            case PadGlyph::Square: col = rgb(229, 139, 200); break;
            case PadGlyph::Triangle: col = rgb(94, 212, 160); break;
            default: col = rgb(200, 208, 218); break;
        }
        c.draw_pad_glyph(g, x + 22, y + 22, 44, col);
        body().draw(c, x + 54, y + (44 - body().line_height()) / 2, label, kText);
        const int item_w = 54 + body().width(label);
        x += item_w + 56;
    }
}

void App::draw_start(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    // Grosse Kachel: Check-up starten (Verlauf, Herzschlaglinie als Bild, Stethoskop)
    const bool sel0 = start_sel_ == 0;
    c.fill_round_rect_vgradient(M, 160, W - 2 * M, 190, 28, sel0 ? rgb(88, 170, 255) : rgb(56, 104, 176), sel0 ? rgb(38, 92, 210) : rgb(28, 58, 112));
    c.stroke_polyline(ecg_points(M + 1020, W - 2 * M - 1080, 262, 58, 400, 0), 6, sel0 ? rgb(176, 214, 255) : rgb(96, 136, 196));
    if (sel0) c.stroke_round_rect(M, 160, W - 2 * M, 190, 28, 4, rgb(200, 226, 255));
    c.fill_round_rect(M + 36, 188, 134, 134, 38, rgba(255, 255, 255, sel0 ? 46 : 30));
    draw_icon(c, Icon::Stethoscope, M + 103, 255, 98, rgb(255, 255, 255), rgb(190, 224, 255));
    title().draw(c, M + 204, 186, S("start.check"), rgb(255, 255, 255));
    {
        int ty = 252;
        for (const auto& l : wrap_text(body(), S("start.check_sub"), 780, 2)) {
            body().draw(c, M + 204, ty, l, rgb(224, 236, 255));
            ty += body().line_height() + 2;
        }
    }

    // Fuenf kleine Kacheln mit Symbolfeld (Verlauf, Einstellungen, Fehlercodes, Selbsttest, Spiele)
    const int gap = 18, cw = (W - 2 * M - 4 * gap) / 5;
    const char* keys[5][2] = {{"start.history", "start.history_sub"}, {"start.settings", "start.settings_sub"}, {"start.errors", "start.errors_sub"}, {"start.selftest", "start.selftest_sub"}, {"start.games", "start.games_sub"}};
    const Icon icons[5] = {Icon::History, Icon::Gear, Icon::Book, Icon::Checklist, Icon::Gamepad};
    const Color tints[5] = {kTintViolet, kTintSlate, kTintOrange, kTintTeal, kTintPink};
    for (int i = 0; i < 5; ++i) {
        const int x = M + i * (cw + gap);
        card(c, x, 380, cw, 190, start_sel_ == i + 1);
        draw_icon_badge(c, icons[i], x + 22, 404, 64, tints[i]);
        body().draw(c, x + 98, 404 + (64 - body().line_height()) / 2, S(keys[i][0]), kText, cw - 112);
        int ty = 488;
        for (const auto& l : wrap_text(small(), S(keys[i][1]), cw - 44, 2)) {
            small().draw(c, x + 22, ty, l, kDim);
            ty += small().line_height() + 2;
        }
    }

    // Letztes Ergebnis: grosses Statussymbol links, Text rechts davon
    c.fill_round_rect(M, 600, W - 2 * M, 236, 22, kPanel);
    Icon st_icon = Icon::Info;
    Color st_col = kDim;
    if (has_result_ && result_.findings.empty()) {
        const bool partial = result_.incomplete || std::any_of(result_.coverage.begin(), result_.coverage.end(), [](const medic::CoverageResult& r) { return !r.tested; });
        st_icon = partial ? Icon::Info : Icon::Ok;  // nicht alles gelesen: nie "in Ordnung" in Gruen
        st_col = partial ? kInfo : kOk;
    } else if (has_result_) {
        medic::Severity worst = medic::Severity::Info;
        for (auto& f : result_.findings)
            if (f.severity == medic::Severity::Critical || (f.severity == medic::Severity::Warning && worst == medic::Severity::Info)) worst = f.severity;
        st_icon = sev_icon(worst);
        st_col = sev_color(worst);
    }
    c.fill_round_rect(M + 36, 650, 136, 136, 40, rgba((st_col >> 16) & 0xFF, (st_col >> 8) & 0xFF, st_col & 0xFF, 34));
    draw_icon(c, st_icon, M + 104, 718, 92, st_col, st_col);
    const int tx = M + 204, tw = W - 2 * M - 204 - 40;
    small().draw(c, tx, 624, S("start.last"), kDim);
    if (!has_result_) {
        body().draw(c, tx, 676, S("start.last_none"), kText);
    } else if (result_.findings.empty()) {
        const int untested = static_cast<int>(std::count_if(result_.coverage.begin(), result_.coverage.end(), [](const medic::CoverageResult& r) { return !r.tested; }));
        body().draw(c, tx, 676, S(result_.incomplete ? "start.last_incomplete" : untested ? "start.last_clean_partial" : "start.last_clean"), (untested || result_.incomplete) ? kText : kOk, tw);
        if (untested) small().draw(c, tx, 740, S.fmt("start.not_tested", {{"n", std::to_string(untested)}}), kDim);
    } else {
        int crit = 0, warn = 0, info = 0;
        for (auto& f : result_.findings) {
            if (f.severity == medic::Severity::Critical) ++crit;
            else if (f.severity == medic::Severity::Warning) ++warn;
            else ++info;
        }
        body().draw(c, tx, 676,
                    S.fmt("start.last_summary", {{"n", std::to_string(result_.findings.size())}, {"crit", std::to_string(crit)}, {"warn", std::to_string(warn)}, {"info", std::to_string(info)}}),
                    kText, tw);
        if (const int untested = static_cast<int>(std::count_if(result_.coverage.begin(), result_.coverage.end(), [](const medic::CoverageResult& r) { return !r.tested; })))
            small().draw(c, tx, 740, S.fmt("start.not_tested", {{"n", std::to_string(untested)}}), kDim);
    }
    if (has_result_ && !result_.history_saved) small().draw(c, tx, 784, S("start.history_failed"), kWarn, tw);  // der seltene Fehlerfall ersetzt die Regelversion
    else if (has_result_ && !result_.rules_version.empty()) small().draw(c, tx, 784, S.fmt("start.rules", {{"v", result_.rules_version}}), kDim);

    draw_hints(c, {{PadGlyph::UpDown, S("common.select")}, {PadGlyph::Cross, S("common.open")}, {PadGlyph::Circle, S("common.quit")}});
}

void App::draw_running(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    c.fill_round_rect(M, 300, W - 2 * M, 330, 24, kPanel);
    c.fill_round_rect(M + 48, 336, 150, 150, 44, rgba(76, 154, 255, 40));
    draw_icon(c, Icon::Stethoscope, M + 123, 411, 108, kText, kAccent);
    std::string dots(static_cast<std::size_t>(tick_ % 4), '.');
    title().draw(c, M + 240, 350, S("running.title") + dots, kText);
    body().draw(c, M + 240, 430, S("running.sub"), kDim, W - 2 * M - 300);
    // Herzschlaglinie als Fortschrittsanzeige: der helle Teil waechst von links nach rechts mit dem Check-up, vorn sitzt ein heller Punkt
    const int bw = W - 2 * M - 120;
    const auto pts = ecg_points(static_cast<float>(M + 60), static_cast<float>(bw), 560.0f, 46.0f, 420.0f, 0.0f);
    c.stroke_polyline(pts, 5, rgb(52, 70, 98));
    const double pr = std::clamp(check_progress_, 0.0, 1.0);
    const std::size_t upto = std::min(pts.size(), std::max<std::size_t>(2, static_cast<std::size_t>(static_cast<double>(pts.size()) * pr) + 1));
    c.stroke_polyline(std::vector<std::pair<float, float>>(pts.begin(), pts.begin() + static_cast<long>(upto)), 6, kAccent);
    const auto tip = pts[upto - 1];
    c.fill_round_rect(static_cast<int>(tip.first) - 11, static_cast<int>(tip.second) - 11, 22, 22, 11, kText);
    char pct[16];
    std::snprintf(pct, sizeof pct, "%d %%", static_cast<int>(pr * 100.0 + 0.5));
    text_right(c, small(), W - M - 40, 580, pct, kDim);
}

void App::draw_findings(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    const auto& fs = result_.findings;
    const int n = static_cast<int>(fs.size());
    if (n == 0) {
        c.fill_round_rect(M, 300, W - 2 * M, 320, 24, kPanel);
        std::string untested_names;
        for (const auto& r : result_.coverage)
            if (!r.tested) untested_names += (untested_names.empty() ? "" : ", ") + pick_text(lang_, r.text_de, r.text_en, "coverage", r.id, "text");
        if (untested_names.empty()) {
            title().draw(c, M + 60, 372, S("findings.empty"), kOk);
            body().draw(c, M + 60, 460, S("findings.empty_sub"), kDim, W - 2 * M - 120);
        } else {
            title().draw(c, M + 60, 372, S("findings.empty_partial"), kText);
            body().draw(c, M + 60, 460, S.fmt("findings.empty_partial_sub", {{"list", untested_names}}), kDim, W - 2 * M - 120);
        }
        if (viewing_old_) draw_hints(c, {{PadGlyph::Circle, S("common.back")}});
        else draw_hints(c, {{PadGlyph::Cross, S("common.retest")}, {PadGlyph::Square, S("common.report")}, {PadGlyph::Circle, S("common.back")}});
        return;
    }

    // Kopfzeile: Anzahl und Chips je Schweregrad
    body().draw(c, M, 140, S.fmt("findings.summary", {{"n", std::to_string(n)}}), kText);
    int cx = M + body().width(S.fmt("findings.summary", {{"n", std::to_string(n)}})) + 36;
    for (auto sev : {medic::Severity::Critical, medic::Severity::Warning, medic::Severity::Info}) {
        int count = 0;
        for (auto& f : fs) count += f.severity == sev;
        if (!count) continue;
        const std::string label = S(sev_key(sev)) + " " + std::to_string(count);
        const int w = small().width(label) + 62;
        c.fill_round_rect(cx, 142, w, 34, 17, kPanel);
        draw_icon(c, sev_icon(sev), cx + 24, 159, 22, sev_color(sev), sev_color(sev));
        small().draw(c, cx + 44, 146, label, kText);
        cx += w + 14;
    }
    const int last = std::min(n, top_ + kListVisible);
    if (n > kListVisible)
        text_right(c, small(), W - M, 148, S.fmt("findings.range", {{"from", std::to_string(top_ + 1)}, {"to", std::to_string(last)}, {"n", std::to_string(n)}}), kDim);

    const int cw = W - 2 * M, ch = 148, gap = 14;
    int y = 200;
    for (int i = top_; i < last; ++i, y += ch + gap) {
        const auto& f = fs[static_cast<std::size_t>(i)];
        const Color sc = sev_color(f.severity);
        card(c, M, y, cw, ch, i == sel_);
        c.fill_round_rect(M, y + 16, 10, ch - 32, 5, sc);
        c.fill_round_rect(M + 34, y + (ch - 76) / 2, 76, 76, 24, rgba((sc >> 16) & 0xFF, (sc >> 8) & 0xFF, sc & 0xFF, 40));
        draw_icon(c, sev_icon(f.severity), M + 72, y + ch / 2, 54, sc, sc);
        const int tx = M + 134;
        const int bw = draw_badge(c, tx, y + 16, S(sev_key(f.severity)), sc);
        small().draw(c, tx + bw + 16, y + 20, f.rule_id, kDim);
        if (!f.fix.empty() && !viewing_old_) {
            const std::string label = S(is_hint_fix(f.fix) ? "findings.hintable" : "findings.fixable");
            const int lw = text_right(c, small(), M + cw - 32, y + 20, label, kOk);
            draw_icon(c, is_hint_fix(f.fix) ? Icon::Lightbulb : Icon::Wrench, M + cw - 32 - lw - 22, y + 33, 28, kOk, kOk);
        }
        const auto lines = wrap_text(body(), finding_text(f), cw - 134 - 36, 2);
        for (std::size_t k = 0; k < lines.size(); ++k)
            body().draw(c, tx, y + 62 + static_cast<int>(k) * (body().line_height() + 2), lines[k], kText);
    }
    if (n > kListVisible) {  // Scrollleiste
        const int track_h = kListVisible * (ch + gap) - gap;
        const int thumb_h = std::max(40, track_h * kListVisible / n);
        const int thumb_y = 200 + (track_h - thumb_h) * top_ / std::max(n - kListVisible, 1);
        c.fill_round_rect(W - 64, 200, 8, track_h, 4, kPanel);
        c.fill_round_rect(W - 64, thumb_y, 8, thumb_h, 4, kDim);
    }

    std::vector<std::pair<PadGlyph, std::string>> hints = {{PadGlyph::UpDown, S("common.select")}};
    if (n > kListVisible) hints.emplace_back(PadGlyph::Shoulders, S("common.page"));
    hints.emplace_back(PadGlyph::Cross, S("common.open"));
    if (const auto* f = current_finding(); f && !f->fix.empty() && !viewing_old_) hints.emplace_back(PadGlyph::Triangle, S(is_hint_fix(f->fix) ? "common.guide" : "common.repair"));
    if (!viewing_old_) hints.emplace_back(PadGlyph::Square, S("common.report"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_report(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title_icon(c, Icon::Report, kTintBlue, S("report.title"), 132);

    // Links: Status und Hinweise
    const int px0 = M, py0 = 230, pw = 880, pad = 36, tw = pw - 2 * pad;
    c.fill_round_rect(px0, py0, pw, 700, 20, kPanel);
    int y = py0 + 30;
    auto paragraph = [&](const Font& f, const std::string& text, Color col, int max_lines) {
        const auto lines = wrap_text(f, text, tw, max_lines);
        for (const auto& l : lines) {
            f.draw(c, px0 + pad, y, l, col);
            y += f.line_height() + 4;
        }
        y += 22;
    };
    if (report_.saved) paragraph(body(), S.fmt("report.saved", {{"path", report_.path}}), kOk, 3);
    else paragraph(body(), S(std::string("report.err.") + (report_.error.empty() ? "write_failed" : report_.error)), kWarn, 5);
    paragraph(body(), S("report.scan"), kText, 4);
    if (report_.truncated) paragraph(small(), S("report.truncated"), kWarn, 3);
    paragraph(small(), S("report.anon"), kDim, 5);
    if (!reported_error_.empty()) paragraph(small(), S.fmt("report.error_code", {{"code", reported_error_}}), kOk, 2);

    // Rechts: QR-Code
    const int ax = 1036, ay = 150, aw = 788, ah = 800;
    if (!qr_ok_) {
        c.fill_round_rect(ax, ay + 80, aw, 240, 20, kPanel);
        body().draw(c, ax + pad, ay + 140, S("report.qr_failed"), kWarn, aw - 2 * pad);
    } else {
        draw_qr_code(c, qr_, ax, ay, aw, ah);
    }
    draw_hints(c, {{PadGlyph::Circle, S("common.back")}});
}

void App::draw_detail(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    const auto* f = current_finding();
    if (!f) return;
    const int cw = W - 2 * M;
    int y = 140;
    draw_icon(c, sev_icon(f->severity), M + 24, y + 17, 46, sev_color(f->severity), sev_color(f->severity));
    const int bw = draw_badge(c, M + 62, y, S(sev_key(f->severity)), sev_color(f->severity));
    body().draw(c, M + 62 + bw + 20, y, f->rule_id, kDim);
    const medic::Confidence conf = medic::effective_confidence(*f);
    {
        // Verlaesslichkeit (hoch/mittel/niedrig) mit der Grundlage; Farbe: Gruen = hoch, Orange = niedrig (nie "in Ordnung" suggerieren)
        const std::string lvl_key = "confidence." + conf.level, basis_key = "basis." + conf.basis;
        const std::string line = S.fmt("confidence.line", {{"level", S.has(lvl_key) ? S(lvl_key) : conf.level}, {"basis", S.has(basis_key) ? S(basis_key) : conf.basis}});
        text_right(c, small(), W - M, y + 4, line, conf.level == "high" ? kOk : (conf.level == "low" ? kWarn : kText));
    }
    {
        // Dringlichkeit und Verlauf (neu / seit wann), eine kleine Zeile unter der Kopfzeile
        const std::string urg = medic::urgency_of(*f);
        std::string line = S.fmt("detail.urgency", {{"u", S("urgency." + urg)}});
        if (f->status == "new") line += "  ·  " + S("detail.history_new");
        else if (f->status == "recurring") {
            const std::string day = f->first_seen.size() >= 8 ? f->first_seen.substr(0, 4) + "-" + f->first_seen.substr(4, 2) + "-" + f->first_seen.substr(6, 2) : f->first_seen;
            line += "  ·  " + S.fmt("detail.history_recurring", {{"since", day}, {"n", std::to_string(f->seen_runs)}});
        }
        if (medic::is_pending(*f)) line += "  ·  " + S.fmt("detail.pending", {{"k", std::to_string(f->seen_runs < 1 ? 1 : f->seen_runs)}, {"n", std::to_string(f->confirm_runs)}});
        small().draw(c, M + 4, y + 44, line, urg == "high" ? kWarn : kDim);
        y += 82;
    }

    // Erklaerung
    {
        const auto lines = wrap_text(body(), finding_text(*f), cw - 72, 6);
        const int lh = body().line_height() + 4;
        const int ph = 22 + small().line_height() + 10 + static_cast<int>(lines.size()) * lh + 22;
        c.fill_round_rect(M, y, cw, ph, 18, kPanel);
        small().draw(c, M + 36, y + 20, S("detail.explanation"), kDim);
        int ty = y + 20 + small().line_height() + 10;
        for (const auto& l : lines) {
            body().draw(c, M + 36, ty, l, kText);
            ty += lh;
        }
        y += ph + 16;
    }

    const std::string conf_reason = pick_text(lang_, conf.reason_de, conf.reason_en, "rules", f->rule_id, "confidence_reason");
    const int link = linked_error_index();

    // Evidenz (mit Begruendung der Verlaesslichkeit bleibt weniger Platz: dann nur drei Zeilen)
    {
        const std::size_t kMaxEvidence = conf_reason.empty() ? 5 : 3;
        const int lh = small().line_height() + 6;
        const int rows = f->evidence.empty() ? 1 : static_cast<int>(std::min(f->evidence.size(), kMaxEvidence)) + (f->evidence.size() > kMaxEvidence ? 1 : 0);
        const int ph = 22 + small().line_height() + 10 + rows * lh + 18;
        c.fill_round_rect(M, y, cw, ph, 18, kPanel);
        small().draw(c, M + 36, y + 20, S("detail.evidence"), kDim);
        int ty = y + 20 + small().line_height() + 10;
        if (f->evidence.empty()) {
            small().draw(c, M + 36, ty, S("detail.evidence_none"), kDim);
        } else {
            for (std::size_t i = 0; i < std::min(f->evidence.size(), kMaxEvidence); ++i, ty += lh)
                small().draw(c, M + 36, ty, elide(small(), f->evidence[i], cw - 72), kText);
            if (f->evidence.size() > kMaxEvidence)
                small().draw(c, M + 36, ty, S.fmt("detail.evidence_more", {{"n", std::to_string(f->evidence.size() - kMaxEvidence)}}), kDim);
        }
        y += ph + 16;
    }

    // Grund der Verlaesslichkeit (nur wenn die Regel einen nennt)
    if (!conf_reason.empty()) {
        // So viele Zeilen (hoechstens drei), wie bis zur Hinweisleiste uebrig bleiben, wenn Quelle, Reparatur und Fehlercode noch folgen.
        const int rest = (f->source.empty() ? 0 : small().line_height() + 14) + body().line_height() + (link >= 0 ? body().line_height() + 12 : 0);
        const int line_h = small().line_height() + 4;
        const int lines_fit = std::clamp((H - 92 - 20 - rest - y - 10) / line_h, 0, 3);
        if (lines_fit > 0) {
            for (const auto& l : wrap_text(small(), conf_reason, cw - 16, lines_fit)) {
                small().draw(c, M + 8, y, l, kDim);
                y += line_h;
            }
            y += 10;
        }
    }

    // Quelle und Reparatur
    if (!f->source.empty()) {
        small().draw(c, M + 8, y, elide(small(), S("detail.source") + ": " + f->source, cw - 16), kDim);
        y += small().line_height() + 14;
    }
    if (!f->fix.empty() && viewing_old_) body().draw(c, M + 8, y, S("detail.fix_old"), kDim, cw - 16);
    else if (!f->fix.empty()) body().draw(c, M + 8, y, S.fmt(is_hint_fix(f->fix) ? "detail.hint_available" : "detail.fix_available", {{"name", fix_name(f->fix)}}), kOk, cw - 16);
    else body().draw(c, M + 8, y, S("detail.fix_none"), kDim, cw - 16);
    if (link >= 0) {
        y += body().line_height() + 12;
        body().draw(c, M + 8, y, S.fmt("detail.error_code", {{"code", errors_.codes[static_cast<std::size_t>(link)].id}}), kAccent, cw - 16);
    }

    std::vector<std::pair<PadGlyph, std::string>> hints;
    if (result_.findings.size() > 1) hints.emplace_back(PadGlyph::Shoulders, S("common.browse"));
    if (!f->fix.empty() && !viewing_old_) hints.emplace_back(PadGlyph::Triangle, S(is_hint_fix(f->fix) ? "common.guide" : "common.repair"));
    if (link >= 0) hints.emplace_back(PadGlyph::Square, S("common.lookup"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_fix_preview(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    const int cw = W - 2 * M;
    title_icon(c, Icon::Wrench, kTintOrange, S("fixprev.title"));
    small().draw(c, M + 4, 192, plan_.fix_id.empty() ? std::string() : fix_name(plan_.fix_id), kDim);

    int y = 232;
    const int y_limit = H - 210;
    int shown = 0;
    if (plan_.empty()) {
        c.fill_round_rect(M, y, cw, 110, 18, kPanel);
        body().draw(c, M + 36, y + 34, S("fixprev.nothing"), kText);
        y += 126;
    }
    for (std::size_t i = 0; i < plan_.changes.size(); ++i) {
        const auto& ch = plan_.changes[i];
        const auto& pv = previews_[i];
        const std::string note = lang_ == Lang::De ? ch.note_de : ch.note_en;  // fr/it/es: Englisch (dynamische Texte der Fix-Engine)
        const bool rename = ch.kind == medic::Change::Kind::Rename;
        const auto path_lines = wrap_text(small(), ch.path, cw - 72, 2);
        const auto to_lines = rename ? wrap_text(small(), ch.to, cw - 72, 2) : std::vector<std::string>();
        const auto note_lines = wrap_text(small(), note, cw - 72, 2);
        const int diff_rows = rename ? 0 : static_cast<int>(std::min<std::size_t>(pv.changed.size(), 6)) + (pv.changed.size() > 6 ? 1 : 0);
        const int lh = small().line_height() + 4;
        const int ph = 20 + 34 + 10 + static_cast<int>(path_lines.size() + to_lines.size() + note_lines.size() + diff_rows) * lh + (rename ? lh : 0) + 18;
        if (y + ph > y_limit) {
            small().draw(c, M + 8, y, S.fmt("fixprev.changes_more", {{"n", std::to_string(plan_.changes.size() - i)}}), kDim);
            y += lh;
            break;
        }
        ++shown;
        c.fill_round_rect(M, y, cw, ph, 18, kPanel);
        draw_badge(c, M + 30, y + 18, S(rename ? "fixprev.rename" : "fixprev.write"), kAccent);
        int ty = y + 18 + 34 + 10;
        for (auto& l : path_lines) {
            small().draw(c, M + 36, ty, l, kText);
            ty += lh;
        }
        if (rename) {
            small().draw(c, M + 36, ty, S("fixprev.to"), kDim);
            ty += lh;
            for (auto& l : to_lines) {
                small().draw(c, M + 36, ty, l, kOk);
                ty += lh;
            }
        }
        for (auto& l : note_lines) {
            small().draw(c, M + 36, ty, l, kDim);
            ty += lh;
        }
        for (std::size_t k = 0; k < std::min<std::size_t>(pv.changed.size(), 6); ++k, ty += lh) {
            const auto& d = pv.changed[k];
            small().draw(c, M + 36, ty, elide(small(), std::string(1, d.tag) + " " + d.text, cw - 72), d.tag == '+' ? kOk : kCrit);
        }
        if (pv.changed.size() > 6) small().draw(c, M + 36, ty, S.fmt("fixprev.diff_more", {{"n", std::to_string(pv.changed.size() - 6)}}), kDim);
        y += ph + 14;
    }
    (void)shown;

    for (std::size_t i = 0; i < std::min<std::size_t>(plan_.warnings.size(), 2); ++i) {
        small().draw(c, M + 8, y, elide(small(), S("fixprev.warnings") + ": " + plan_.warnings[i], cw - 16), kWarn);
        y += small().line_height() + 6;
    }
    const std::string& hint = lang_ == Lang::De ? plan_.hint_de : plan_.hint_en;  // fr/it/es: Englisch
    if (!hint.empty())
        for (auto& l : wrap_text(small(), hint, cw - 16, 2)) {
            small().draw(c, M + 8, y, l, kText);
            y += small().line_height() + 6;
        }

    if (!plan_.empty()) small().draw(c, M + 8, H - 150, elide(small(), S("fixprev.backup"), cw - 16), kDim);
    std::vector<std::pair<PadGlyph, std::string>> hints;
    if (!plan_.empty()) hints.emplace_back(PadGlyph::Cross, S("common.confirm"));
    hints.emplace_back(PadGlyph::Circle, S("common.cancel"));
    draw_hints(c, hints);
}

void App::draw_fix_done(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    const bool fix = done_kind_ == DoneKind::Fix;
    const char* head = done_ok_ ? (fix ? "fixdone.ok" : "undo.ok") : (fix ? "fixdone.fail" : "undo.fail");
    std::string sub;
    if (done_ok_) sub = fix ? S.fmt("fixdone.ok_sub", {{"txn", done_text_}}) : S.fmt("undo.ok_sub", {{"txn", done_text_}});
    else sub = S.fmt("fixdone.fail_sub", {{"error", done_text_}});

    c.fill_round_rect(M, 260, W - 2 * M, 400, 24, kPanel);
    {
        const Color rc = done_ok_ ? kOk : kCrit;
        c.fill_round_rect(M + 60, 290, 104, 104, 32, rgba((rc >> 16) & 0xFF, (rc >> 8) & 0xFF, rc & 0xFF, 40));
        draw_icon(c, done_ok_ ? Icon::Ok : Icon::Critical, M + 112, 342, 72, rc, rc);
    }
    title().draw(c, M + 196, 320, S(head), done_ok_ ? kOk : kCrit);
    int y = 410;
    for (auto& l : wrap_text(body(), sub, W - 2 * M - 120, 5)) {
        body().draw(c, M + 60, y, l, kText);
        y += body().line_height() + 6;
    }
    if (fix && done_ok_ && verify_ != Verify::None) {  // Ergebnis der erneuten Pruefung
        y += 14;
        const bool pending = verify_ == Verify::Pending, fixed = verify_ == Verify::Fixed;
        const std::string text = pending ? S("fixdone.verifying") : S.fmt(fixed ? "fixdone.verified_fixed" : "fixdone.verified_persists", {{"rule", verify_rule_}});
        for (auto& l : wrap_text(body(), text, W - 2 * M - 120, 4)) {
            body().draw(c, M + 60, y, l, pending ? kDim : (fixed ? kOk : kWarn));
            y += body().line_height() + 6;
        }
    }
    std::vector<std::pair<PadGlyph, std::string>> hints;
    if (fix) {
        hints.emplace_back(PadGlyph::Cross, S(verify_ == Verify::Fixed || verify_ == Verify::Persists ? "common.to_list" : "common.retest"));
        hints.emplace_back(PadGlyph::Circle, S("common.home"));
    } else {
        hints.emplace_back(PadGlyph::Cross, S("common.back"));
    }
    draw_hints(c, hints);
}

void App::draw_history(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title_icon(c, Icon::History, kTintViolet, S("history.title"));
    const int n = static_cast<int>(hist_.size());
    if (n == 0) {
        c.fill_round_rect(M, 260, W - 2 * M, 160, 20, kPanel);
        body().draw(c, M + 40, 320, S("history.empty"), kDim);
        draw_hints(c, {{PadGlyph::Circle, S("common.back")}});
        return;
    }
    const int cw = W - 2 * M, ch = 96, gap = 12;
    const int last = std::min(n, hist_top_ + kHistVisible);
    int y = 220;
    for (int i = hist_top_; i < last; ++i, y += ch + gap) {
        const auto& t = hist_[static_cast<std::size_t>(i)];
        card(c, M, y, cw, ch, i == hist_sel_);
        const Color sc = t.state == "applied" ? kOk : (t.state == "undone" ? kDim : kWarn);
        c.fill_round_rect(M + 24, y + 18, 60, 60, 18, rgba((sc >> 16) & 0xFF, (sc >> 8) & 0xFF, sc & 0xFF, 40));
        draw_icon(c, t.state == "applied" ? Icon::Ok : (t.state == "undone" ? Icon::Undo : Icon::Warning), M + 54, y + 48, 40, sc, sc);
        small().draw(c, M + 108, y + 14, pretty_ts(t.ts.empty() ? t.txn : t.ts) + "   " + t.rule, kDim, cw - 476);
        body().draw(c, M + 108, y + 44, elide(body(), fix_name(t.fix), cw - 596), kText);
        const std::string skey = "history.state." + t.state;
        text_right(c, body(), M + cw - 32, y + 14, S.has(skey) ? S(skey) : t.state, sc);
        text_right(c, small(), M + cw - 32, y + 56, S.fmt("history.ops", {{"n", std::to_string(t.ops)}}), kDim);
    }
    const bool can_undo = hist_sel_ >= 0 && hist_sel_ < n && undoable(hist_[static_cast<std::size_t>(hist_sel_)].state);
    std::vector<std::pair<PadGlyph, std::string>> hints = {{PadGlyph::UpDown, S("common.select")}};
    if (n > kHistVisible) hints.emplace_back(PadGlyph::Shoulders, S("common.page"));
    if (can_undo) hints.emplace_back(PadGlyph::Cross, S("common.undo"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_checklist(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title_icon(c, Icon::History, kTintViolet, S("checks.title"));
    const int n = static_cast<int>(chk_.size());
    if (n == 0) {
        c.fill_round_rect(M, 260, W - 2 * M, 180, 20, kPanel);
        body().draw(c, M + 40, 320, S("checks.empty"), kDim, W - 2 * M - 80);
        draw_hints(c, {{PadGlyph::Square, S("checks.repairs")}, {PadGlyph::Circle, S("common.back")}});
        return;
    }
    small().draw(c, M + 4, 196, S("checks.sub"), kDim);
    const int last = std::min(n, chk_top_ + kChkVisible);
    if (n > kChkVisible) text_right(c, small(), W - M, 148, S.fmt("findings.range", {{"from", std::to_string(chk_top_ + 1)}, {"to", std::to_string(last)}, {"n", std::to_string(n)}}), kDim);
    const int cw = W - 2 * M, ch = 76, gap = 8, y0 = 232;
    int y = y0;
    for (int i = chk_top_; i < last; ++i, y += ch + gap) {
        const auto& e = chk_[static_cast<std::size_t>(i)];
        card(c, M, y, cw, ch, i == chk_sel_);
        const bool partial = e.untested > 0;
        Icon ic = Icon::Ok;
        Color col = kOk;
        if (e.critical > 0) { ic = Icon::Critical; col = kCrit; }
        else if (e.warning > 0) { ic = Icon::Warning; col = kWarn; }
        else if (e.info > 0) { ic = Icon::Info; col = kInfo; }
        else if (partial) { ic = Icon::Info; col = kInfo; }  // nicht alles gelesen: nie "in Ordnung" in Gruen
        c.fill_round_rect(M + 18, y + 10, 56, 56, 18, rgba((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF, 40));
        draw_icon(c, ic, M + 46, y + 38, 38, col, col);
        body().draw(c, M + 94, y + 6, pretty_ts(e.ts), kText);
        if (i == 0) {
            const int bx = M + 94 + body().width(pretty_ts(e.ts)) + 20;
            draw_badge(c, bx, y + 8, S("checks.latest"), kAccent);
        }
        small().draw(c, M + 94, y + 42, S.fmt("checks.line", {{"fw", e.fw.empty() ? "?" : e.fw}, {"rules", e.rules_version.empty() ? "?" : e.rules_version}}), kDim, cw - 700);
        // rechts: Zahl der Befunde je Schweregrad, sonst "keine Befunde"
        int x = M + cw - 32;
        if (e.total == 0) {
            x -= text_right(c, body(), x, y + 20, S(partial ? "checks.untested" : "checks.none"), partial ? kDim : kOk);
        } else {
            const std::pair<int, Severity> parts[3] = {{e.info, Severity::Info}, {e.warning, Severity::Warning}, {e.critical, Severity::Critical}};
            for (const auto& pr : parts) {
                if (!pr.first) continue;
                const std::string num = std::to_string(pr.first);
                const int w = body().width(num);
                body().draw(c, x - w, y + 20, num, kText);
                draw_icon(c, sev_icon(pr.second), x - w - 26, y + 38, 34, sev_color(pr.second), sev_color(pr.second));
                x -= w + 26 + 34;
            }
        }
    }
    if (n > kChkVisible) {  // Scrollleiste
        const int track_h = kChkVisible * (ch + gap) - gap;
        const int thumb_h = std::max(40, track_h * kChkVisible / n);
        const int thumb_y = y0 + (track_h - thumb_h) * chk_top_ / std::max(n - kChkVisible, 1);
        c.fill_round_rect(W - 64, y0, 8, track_h, 4, kPanel);
        c.fill_round_rect(W - 64, thumb_y, 8, thumb_h, 4, kDim);
    }
    if (chk_unreadable_) small().draw(c, M + 8, H - 138, S("checks.unreadable"), kWarn);
    std::vector<std::pair<PadGlyph, std::string>> hints = {{PadGlyph::UpDown, S("common.select")}};
    if (n > kChkVisible) hints.emplace_back(PadGlyph::Shoulders, S("common.page"));
    hints.emplace_back(PadGlyph::Cross, S("checks.view"));
    if (chk_sel_ + 1 < n) hints.emplace_back(PadGlyph::Triangle, S("checks.compare"));
    hints.emplace_back(PadGlyph::Square, S("checks.repairs"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_gamelist(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title_icon(c, Icon::Gamepad, kTintPink, S("games.title"));
    const int n = static_cast<int>(games_.size());
    if (n == 0) {
        c.fill_round_rect(M, 260, W - 2 * M, 200, 20, kPanel);
        int ty = 300;
        for (const auto& l : wrap_text(body(), S("games.empty"), W - 2 * M - 80, 3)) {
            body().draw(c, M + 40, ty, l, kDim);
            ty += body().line_height() + 4;
        }
        draw_hints(c, {{PadGlyph::Circle, S("common.back")}});
        return;
    }
    small().draw(c, M + 4, 196, elide(small(), S("games.sub"), W - 2 * M - 300), kDim);
    const int last = std::min(n, game_top_ + kGameVisible);
    if (n > kGameVisible) text_right(c, small(), W - M, 148, S.fmt("findings.range", {{"from", std::to_string(game_top_ + 1)}, {"to", std::to_string(last)}, {"n", std::to_string(n)}}), kDim);
    const int cw = W - 2 * M, ch = 76, gap = 8, y0 = 232;
    int y = y0;
    for (int i = game_top_; i < last; ++i, y += ch + gap) {
        const auto& g = games_[static_cast<std::size_t>(i)];
        card(c, M, y, cw, ch, i == game_sel_);
        const bool has = !g.findings.empty();
        const Icon ic = has ? sev_icon(g.worst) : Icon::Gamepad;
        const Color col = has ? sev_color(g.worst) : kTintSlate;
        c.fill_round_rect(M + 18, y + 10, 56, 56, 18, rgba((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF, 40));
        draw_icon(c, ic, M + 46, y + 38, 38, col, col);
        body().draw(c, M + 94, y + 6, elide(body(), g.name.empty() ? g.titleid : g.name, cw - 640), kText);
        std::string kind = S("games.kind_none");
        if (!g.sources.empty()) {
            const auto it = std::find_if(g.sources.begin(), g.sources.end(), [](const medic::GameSource& s2) { return s2.active; });
            kind = S((it != g.sources.end() ? it->kind : g.sources.front().kind) == "image" ? "games.kind_image" : "games.kind_folder");
        }
        small().draw(c, M + 94, y + 42, elide(small(), (g.name.empty() ? std::string() : g.titleid + "  ·  ") + kind, cw - 640), kDim);
        int x = M + cw - 32;
        if (!has) {
            text_right(c, small(), x, y + 24, S("games.no_trace"), kDim);
        } else {
            int cnt[3] = {0, 0, 0};  // Info, Warnung, Kritisch
            for (const auto fi : g.findings)
                if (fi < result_.findings.size()) {
                    const auto sv = result_.findings[fi].severity;
                    ++cnt[sv == medic::Severity::Critical ? 2 : (sv == medic::Severity::Warning ? 1 : 0)];
                }
            const medic::Severity order[3] = {medic::Severity::Info, medic::Severity::Warning, medic::Severity::Critical};
            for (int k = 0; k < 3; ++k) {
                if (!cnt[k]) continue;
                const std::string num = std::to_string(cnt[k]);
                const int w = body().width(num);
                body().draw(c, x - w, y + 20, num, kText);
                draw_icon(c, sev_icon(order[k]), x - w - 26, y + 38, 34, sev_color(order[k]), sev_color(order[k]));
                x -= w + 26 + 34;
            }
        }
    }
    if (n > kGameVisible) {  // Scrollleiste
        const int track_h = kGameVisible * (ch + gap) - gap;
        const int thumb_h = std::max(40, track_h * kGameVisible / n);
        const int thumb_y = y0 + (track_h - thumb_h) * game_top_ / std::max(n - kGameVisible, 1);
        c.fill_round_rect(W - 64, y0, 8, track_h, 4, kPanel);
        c.fill_round_rect(W - 64, thumb_y, 8, thumb_h, 4, kDim);
    }
    std::vector<std::pair<PadGlyph, std::string>> hints = {{PadGlyph::UpDown, S("common.select")}};
    if (n > kGameVisible) hints.emplace_back(PadGlyph::Shoulders, S("common.page"));
    hints.emplace_back(PadGlyph::Cross, S("common.open"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_gamedetail(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    if (game_sel_ < 0 || game_sel_ >= static_cast<int>(games_.size())) return;
    const auto& g = games_[static_cast<std::size_t>(game_sel_)];
    const int total = static_cast<int>(games_.size());
    title_icon(c, Icon::Gamepad, kTintPink, elide(title(), g.name.empty() ? g.titleid : g.name, W - 2 * M - 420));
    text_right(c, small(), W - M, 148, S.fmt("games.pos", {{"i", std::to_string(game_sel_ + 1)}, {"n", std::to_string(total)}}), kDim);
    small().draw(c, M + 4, 196, (g.name.empty() ? std::string() : g.titleid + "  ·  ") + S.fmt("games.console_fw", {{"fw", result_.fw.empty() ? "?" : result_.fw}}), kDim);

    // Links: was Medic zu diesem Titel gefunden hat
    const int px = M, py = 232, pw = 840, ph = 694, pad = 32, lw = 230, vw = pw - 2 * pad - lw;
    c.fill_round_rect(px, py, pw, ph, 20, kPanel);
    int y = py + 26;
    const int pitch = body().line_height() + 14;
    auto row = [&](const std::string& label, const std::string& value, Color col, bool first = true) {
        if (first) small().draw(c, px + pad, y + (body().line_height() - small().line_height()) / 2 + 2, label, kDim);
        body().draw(c, px + pad + lw, y, elide(body(), value, vw), col);
        y += pitch;
    };
    // Gefunden in
    if (g.sources.empty()) {
        row(S("games.d.where"), S("games.d.where_none"), kDim);
    } else {
        for (std::size_t i = 0; i < g.sources.size() && i < 2; ++i) {
            const auto& s = g.sources[i];
            std::string v = S(s.kind == "image" ? "games.kind_image" : "games.kind_folder") + (s.active ? "  " + S("games.d.active") : std::string());
            row(S("games.d.where"), v, s.active ? kText : kDim, i == 0);
            if (!s.path.empty()) {
                small().draw(c, px + pad + lw, y - 8, elide(small(), s.path, vw), kDim);
                y += small().line_height() + 2;
            }
        }
        if (g.sources.size() > 2) row("", S.fmt("games.d.more", {{"n", std::to_string(g.sources.size() - 2)}}), kDim, false);
        if (g.sources.front().dup_count > 1) row("", S.fmt("games.d.dup", {{"n", std::to_string(g.sources.front().dup_count)}}), kWarn, false);
    }
    // Systemdatenbank
    if (g.orphan) row(S("games.d.reg"), S("games.d.reg_orphan"), kWarn);
    else if (g.registered == 1) row(S("games.d.reg"), S("games.d.reg_yes"), kText);
    else if (g.registered == 0) row(S("games.d.reg"), S("games.d.reg_no"), kWarn);
    else row(S("games.d.reg"), S("games.d.reg_unknown"), kDim);
    // Firmware
    if (g.required_fw.empty()) {
        row(S("games.d.fw"), S("games.d.fw_none"), kDim);
    } else {
        const std::string fw = result_.fw.empty() ? "?" : result_.fw;
        if (g.needs_backport) row(S("games.d.fw"), S.fmt("games.d.fw_need", {{"req", g.required_fw}, {"fw", fw}}), kWarn);
        else if (g.backport == 1) row(S("games.d.fw"), S.fmt(g.backport_inline ? "games.d.fw_backport_inline" : "games.d.fw_backport", {{"req", g.required_fw}, {"fw", fw}}), kText);
        else row(S("games.d.fw"), S.fmt("games.d.fw_line", {{"req", g.required_fw}, {"fw", fw}}), kText);
    }
    // Dateien
    {
        bool any = false;
        auto file_row = [&](const char* key, Color col) {
            row(S("games.d.files"), S(key), col, !any);
            any = true;
        };
        if (g.eboot_bad) file_row("games.d.eboot", kCrit);
        if (g.case_mismatch) file_row("games.d.case", kWarn);
        if (g.oversized) file_row("games.d.oversized", kInfo);
        if (!any) row(S("games.d.files"), S("games.d.files_none"), kDim);
    }
    // Fehler der Konsole
    if (g.errors.empty()) {
        row(S("games.d.errors"), S("games.d.errors_none"), kDim);
    } else {
        for (std::size_t i = 0; i < g.errors.size() && i < 2; ++i) {
            const auto& e = g.errors[i];
            std::string v = g.errors.size() > 2 && i == 1 ? S.fmt("games.d.errors_more", {{"n", std::to_string(g.errors.size() - 1)}}) : g.errors[i].code + "   " + S.fmt("games.d.error_n", {{"n", std::to_string(e.count)}}) + (e.last.empty() ? std::string() : ", " + S.fmt("games.d.error_last", {{"day", e.last}}));
            row(S("games.d.errors"), v, kWarn, i == 0);
        }
    }
    if (g.foreign_saves > 0) row(S("games.d.saves"), S.fmt("games.d.saves_foreign", {{"n", std::to_string(g.foreign_saves)}}), kWarn);
    // Log
    if (!g.log_lines.empty()) {
        y += 6;
        small().draw(c, px + pad, y, S("games.d.log"), kDim);
        y += small().line_height() + 6;
        for (const auto& l : g.log_lines) {
            small().draw(c, px + pad, y, elide(small(), l, pw - 2 * pad), kText);
            y += small().line_height() + 4;
        }
    }

    // Rechts: moegliche Gruende = die Befunde dieses Titels
    const int rx = M + pw + 24, rw = W - M - rx;
    small().draw(c, rx + 4, py - 2, S("games.d.reasons"), kDim);
    const int n = static_cast<int>(g.findings.size());
    if (n == 0) {
        c.fill_round_rect(rx, py + 36, rw, 250, 20, kPanel);
        int ty = py + 64;
        for (const auto& l : wrap_text(body(), S("games.d.reasons_none"), rw - 64, 6)) {
            body().draw(c, rx + 32, ty, l, kDim);
            ty += body().line_height() + 4;
        }
    } else {
        const int ch = 120, gap = 10;
        int cy = py + 36;
        const int last = std::min(n, gd_top_ + kGdVisible);
        for (int i = gd_top_; i < last; ++i, cy += ch + gap) {
            const std::size_t fi = g.findings[static_cast<std::size_t>(i)];
            if (fi >= result_.findings.size()) continue;
            const auto& f = result_.findings[fi];
            const Color sc = sev_color(f.severity);
            card(c, rx, cy, rw, ch, i == gd_sel_);
            c.fill_round_rect(rx, cy + 14, 8, ch - 28, 4, sc);
            draw_icon(c, sev_icon(f.severity), rx + 44, cy + 36, 40, sc, sc);
            const int tx = rx + 82;
            const int bw = draw_badge(c, tx, cy + 12, S(sev_key(f.severity)), sc);
            small().draw(c, tx + bw + 14, cy + 16, elide(small(), f.rule_id, rw - 82 - bw - 40), kDim);
            const auto lines = wrap_text(small(), finding_text(f), rw - 82 - 24, 2);
            for (std::size_t k = 0; k < lines.size(); ++k) small().draw(c, tx, cy + 54 + static_cast<int>(k) * (small().line_height() + 2), lines[k], kText);
        }
        if (n > kGdVisible) {
            const int track_h = kGdVisible * (ch + gap) - gap;
            const int thumb_h = std::max(40, track_h * kGdVisible / n);
            const int thumb_y = py + 36 + (track_h - thumb_h) * gd_top_ / std::max(n - kGdVisible, 1);
            c.fill_round_rect(W - 64, py + 36, 8, track_h, 4, kPanel);
            c.fill_round_rect(W - 64, thumb_y, 8, thumb_h, 4, kDim);
        }
    }
    std::vector<std::pair<PadGlyph, std::string>> hints;
    if (n > 1) hints.emplace_back(PadGlyph::UpDown, S("common.select"));
    if (total > 1) hints.emplace_back(PadGlyph::Shoulders, S("games.browse"));
    if (n > 0) hints.emplace_back(PadGlyph::Cross, S("games.open_finding"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_compare(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title_icon(c, Icon::Report, kTintBlue, S("compare.title"));
    small().draw(c, M + 4, 196, S.fmt("compare.sub", {{"newer", pretty_ts(cmp_new_.ts)}, {"older", pretty_ts(cmp_old_.ts)}}), kDim);
    // Zeilen: Abschnittskopf und je Befund eine Zeile
    struct Row {
        bool head;
        std::string text;
        Color col;
        const medic::Finding* f;
    };
    std::vector<Row> rows;
    auto section = [&](const char* key, const std::vector<medic::Finding>& v, Color col) {
        rows.push_back({true, S.fmt(key, {{"n", std::to_string(v.size())}}), col, nullptr});
        for (const auto& f : v) rows.push_back({false, f.rule_id, col, &f});
    };
    section("compare.added", cmp_diff_.added, kWarn);
    section("compare.resolved", cmp_diff_.resolved, kOk);
    section("compare.unchanged", cmp_diff_.unchanged, kDim);
    const int total = static_cast<int>(rows.size());
    c.fill_round_rect(M, 232, W - 2 * M, 700, 20, kPanel);
    int y = 244;
    for (int i = cmp_top_; i < std::min(total, cmp_top_ + kCmpVisible); ++i) {
        const Row& r = rows[static_cast<std::size_t>(i)];
        if (r.head) {
            body().draw(c, M + 36, y, r.text, r.col);
        } else {
            draw_icon(c, sev_icon(r.f->severity), M + 56, y + 20, 30, sev_color(r.f->severity), sev_color(r.f->severity));
            small().draw(c, M + 92, y + 8, r.text, kText);
            small().draw(c, M + 560, y + 8, elide(small(), finding_text(*r.f), W - 2 * M - 560 - 80), kDim);
        }
        y += 50;
    }
    if (cmp_diff_.added.empty() && cmp_diff_.resolved.empty()) small().draw(c, M + 36, 870, S("compare.same"), kDim);
    small().draw(c, M + 8, H - 138, S("compare.note"), kDim);
    if (total > kCmpVisible) {
        const int track_h = 676, thumb_h = std::max(40, track_h * kCmpVisible / total);
        const int thumb_y = 244 + (track_h - thumb_h) * cmp_top_ / std::max(total - kCmpVisible, 1);
        c.fill_round_rect(W - 64, 244, 8, track_h, 4, kPanelHi);
        c.fill_round_rect(W - 64, thumb_y, 8, thumb_h, 4, kDim);
    }
    std::vector<std::pair<PadGlyph, std::string>> hints = {{PadGlyph::UpDown, S("errors.scroll")}};
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_undo_confirm(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    c.fill_round_rect(M, 260, W - 2 * M, 380, 24, kPanel);
    c.fill_round_rect(M + 60, 286, 104, 104, 32, rgba(255, 179, 71, 40));
    draw_icon(c, Icon::Undo, M + 112, 338, 72, kWarn, kWarn);
    title().draw(c, M + 196, 312, S("undo.title"), kWarn);
    int y = 410;
    for (auto& l : wrap_text(body(), S.fmt("undo.body", {{"fix", fix_name(undo_target_.fix)}, {"ts", pretty_ts(undo_target_.ts.empty() ? undo_target_.txn : undo_target_.ts)}}), W - 2 * M - 120, 5)) {
        body().draw(c, M + 60, y, l, kText);
        y += body().line_height() + 6;
    }
    draw_hints(c, {{PadGlyph::Cross, S("common.confirm")}, {PadGlyph::Circle, S("common.cancel")}});
}

void App::draw_settings(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title_icon(c, Icon::Gear, kTintSlate, S("settings.title"));
    card(c, M, 230, W - 2 * M, 150, settings_sel_ == 0);
    draw_icon_badge(c, Icon::Globe, M + 36, 266, 78, kTintViolet);
    body().draw(c, M + 144, 262, S("settings.language"), kText);
    small().draw(c, M + 144, 316, S("settings.note"), kDim);
    int x = W - M - 40;
    for (auto it = std::rbegin(kAllLangs); it != std::rend(kAllLangs); ++it) {  // von rechts nach links: Es, It, Fr, En, De
        const Lang l = *it;
        const std::string name = S(std::string("settings.lang_") + lang_code(l));
        const int w = body().width(name) + 48;
        x -= w;
        const bool on = l == lang_;
        c.fill_round_rect(x, 262, w, 56, 28, on ? kAccent : kLine);
        body().draw(c, x + 24, 262 + (56 - body().line_height()) / 2, name, on ? rgb(255, 255, 255) : kDim);
        x -= 12;
    }
    card(c, M, 410, W - 2 * M, 150, settings_sel_ == 1);
    draw_icon_badge(c, Icon::Download, M + 36, 446, 78, kTintTeal);
    body().draw(c, M + 144, 442, S("settings.rules_update"), kText);
    small().draw(c, M + 144, 496, S("settings.rules_update_sub"), kDim);
    if (has_result_ && !result_.rules_version.empty()) small().draw(c, M + 8, 590, S.fmt("settings.rules", {{"v", result_.rules_version}}), kDim);
    if (settings_sel_ == 0) draw_hints(c, {{PadGlyph::UpDown, S("common.select")}, {PadGlyph::LeftRight, S("common.toggle")}, {PadGlyph::Circle, S("common.back")}});
    else draw_hints(c, {{PadGlyph::UpDown, S("common.select")}, {PadGlyph::Cross, S("common.open")}, {PadGlyph::Circle, S("common.back")}});
}

void App::draw_rules_update(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title_icon(c, Icon::Download, kTintTeal, S("update.title"));
    const int pad = 40, pw = W - 2 * M, tw = pw - 2 * pad;
    c.fill_round_rect(M, 230, pw, 500, 20, kPanel);
    int y = 262;
    auto paragraph = [&](const Font& f, const std::string& text, Color col, int max_lines) {
        for (const auto& l : wrap_text(f, text, tw, max_lines)) {
            f.draw(c, M + pad, y, l, col);
            y += f.line_height() + 4;
        }
        y += 22;
    };
    // Fehlertext: bekannte Kennung (Fehler oder Ablehnung), sonst allgemein
    auto reason_text = [&](const std::string& code) {
        for (const char* prefix : {"update.error.", "update.rejected."})
            if (S.has(prefix + code)) return S(prefix + code);
        return S("update.failed");
    };

    paragraph(small(), S.fmt("update.current", {{"v", offer_.old_version.empty() ? "?" : offer_.old_version}, {"n", std::to_string(offer_.old_rule_count)}}), kDim, 1);
    if (codes_offer_.state != medic::RuleUpdateOffer::State::None || !codes_offer_.old_version.empty())
        paragraph(small(), S.fmt("update.current_codes", {{"v", codes_offer_.old_version.empty() ? "?" : codes_offer_.old_version}, {"n", std::to_string(codes_offer_.old_rule_count)}}), kDim, 1);

    std::vector<std::pair<PadGlyph, std::string>> hints;
    switch (update_stage_) {
        case UpdateStage::Offer:
            if (downloaded_) paragraph(small(), S.fmt("update.downloaded_from", {{"url", download_url_}}), kDim, 2);
            if (offer_.state == medic::RuleUpdateOffer::State::None) {
                if (codes_offer_.state == medic::RuleUpdateOffer::State::None) {  // gar nichts da: Anleitung
                    paragraph(body(), S("update.none"), kText, 2);
                    paragraph(small(), S("update.howto"), kDim, 5);
                }
            } else if (offer_.state == medic::RuleUpdateOffer::State::Ready) {
                paragraph(body(), S.fmt("update.ready", {{"v", offer_.new_version}, {"n", std::to_string(offer_.new_rule_count)}}), kOk, 2);
                if (offer_.auth == "signature") paragraph(small(), S.fmt("update.auth_sig", {{"key", offer_.signed_by}, {"sha", offer_.sha256.substr(0, 16)}}), kDim, 2);
                else paragraph(small(), S.fmt("update.sha", {{"sha", offer_.sha256.substr(0, 16)}}), kDim, 2);
                paragraph(small(), S("update.backup_note"), kDim, 3);
            } else {
                paragraph(body(), reason_text(offer_.reason), kWarn, 4);
                if (!offer_.detail.empty()) paragraph(small(), offer_.detail, kDim, 3);
            }
            if (codes_offer_.state == medic::RuleUpdateOffer::State::Ready) {  // dasselbe fuer die Fehlercode-Datei
                paragraph(body(), S.fmt("update.codes_ready", {{"v", codes_offer_.new_version}, {"n", std::to_string(codes_offer_.new_rule_count)}}), kOk, 2);
                if (codes_offer_.auth == "signature") paragraph(small(), S.fmt("update.auth_sig", {{"key", codes_offer_.signed_by}, {"sha", codes_offer_.sha256.substr(0, 16)}}), kDim, 2);
                else paragraph(small(), S.fmt("update.sha", {{"sha", codes_offer_.sha256.substr(0, 16)}}), kDim, 2);
            } else if (codes_offer_.state == medic::RuleUpdateOffer::State::Rejected) {
                paragraph(body(), S("update.codes_label") + " " + reason_text(codes_offer_.reason), kWarn, 3);
                if (!codes_offer_.detail.empty()) paragraph(small(), codes_offer_.detail, kDim, 2);
            }
            if (downloaded_ && !download_result_.codes_error.empty()) {
                const std::string reason = download_result_.codes_error == "http_status" ? S.fmt("update.error.http_file", {{"file", download_result_.codes_file}})
                                                                                       : reason_text(download_result_.codes_error);
                paragraph(small(), S.fmt("update.codes_download_failed", {{"reason", reason}}), kWarn, 2);
            }
            if (offer_.state == medic::RuleUpdateOffer::State::Ready || codes_offer_.state == medic::RuleUpdateOffer::State::Ready)
                hints.emplace_back(PadGlyph::Cross, S("update.install"));
            if (offer_.can_rollback) paragraph(small(), S.fmt("update.can_rollback", {{"v", offer_.installed_version}}), kDim, 3);
            if (codes_offer_.can_rollback) paragraph(small(), S.fmt("update.can_rollback_codes", {{"v", codes_offer_.installed_version}}), kDim, 3);
            if (offer_.can_rollback || codes_offer_.can_rollback) hints.emplace_back(PadGlyph::Triangle, S("update.rollback"));
            hints.emplace_back(PadGlyph::Square, S("update.download"));
            hints.emplace_back(PadGlyph::Circle, S("common.back"));
            break;
        case UpdateStage::ConfirmDownload:
            if (download_url_.empty()) {
                paragraph(body(), S("update.no_url"), kText, 6);
                hints = {{PadGlyph::Circle, S("common.back")}};
            } else {
                paragraph(body(), S.fmt("update.confirm_download", {{"url", download_url_}}), kText, 6);
                hints = {{PadGlyph::Cross, S("update.download")}, {PadGlyph::Circle, S("common.cancel")}};
            }
            break;
        case UpdateStage::Downloading:
            download_rendered_ = true;
            paragraph(body(), S("update.downloading"), kText, 2);
            paragraph(small(), download_url_, kDim, 2);
            break;
        case UpdateStage::DownloadFail:
            paragraph(body(), S("update.download_failed"), kWarn, 2);
            if (download_result_.error == "http_status")
                paragraph(small(), S.fmt("update.error.http_status", {{"file", download_result_.file}, {"status", std::to_string(download_result_.http_status)}}), kDim, 3);
            else paragraph(small(), reason_text(download_result_.error), kDim, 4);
            hints = {{PadGlyph::Cross, S("common.back")}};
            break;
        case UpdateStage::ConfirmRollback:
            paragraph(body(), S("update.confirm_rollback"), kText, 4);
            hints = {{PadGlyph::Cross, S("update.rollback")}, {PadGlyph::Circle, S("common.cancel")}};
            break;
        case UpdateStage::DoneOk:
        case UpdateStage::DoneFail: {
            // Je Datei, die versucht wurde: Erfolg oder Fehler. Nur Regeln: wie bisher.
            if (rules_attempted_) {
                if (update_result_.ok) {
                    paragraph(body(), S.fmt(update_was_rollback_ ? "update.rolled_back" : "update.installed", {{"v", update_result_.new_version}}), kOk, 2);
                    paragraph(small(), S("update.next"), kDim, 2);
                    if (update_result_.error == "reload_failed") paragraph(small(), S("update.reload_failed"), kWarn, 3);
                    if (!update_result_.backup_path.empty()) paragraph(small(), S.fmt("update.backup_path", {{"path", update_result_.backup_path}}), kDim, 3);
                } else {
                    paragraph(body(), S("update.failed"), kWarn, 2);
                    paragraph(small(), reason_text(update_result_.error), kDim, 4);
                }
            }
            if (codes_attempted_) {
                if (codes_result_.ok) {
                    paragraph(body(), S.fmt(update_was_rollback_ ? "update.codes_rolled_back" : "update.codes_installed", {{"v", codes_result_.new_version}}), kOk, 2);
                    if (!codes_result_.backup_path.empty()) paragraph(small(), S.fmt("update.backup_path", {{"path", codes_result_.backup_path}}), kDim, 3);
                    if (codes_result_.error == "reload_failed") paragraph(small(), S("update.reload_failed"), kWarn, 3);
                } else {
                    paragraph(body(), S("update.codes_failed"), kWarn, 2);
                    paragraph(small(), reason_text(codes_result_.error), kDim, 4);
                }
            }
            hints = {{PadGlyph::Cross, S("common.back")}};
            break;
        }
    }
    draw_hints(c, hints);
}

int App::error_scroll_max() const {
    const medic::ErrorCode* e = current_error();
    return e ? max_first_row(error_rows(*e, lang_, err_ctx_)) : 0;
}

void App::draw_error_codes(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title_icon(c, Icon::Book, kTintOrange, S("errors.title"));
    const int n = static_cast<int>(errors_.codes.size());
    if (n == 0) {
        c.fill_round_rect(M, 260, W - 2 * M, 300, 20, kPanel);
        body().draw(c, M + 40, 300, S("errors.empty"), kWarn, W - 2 * M - 80);
        int y = 360;
        for (const auto& l : wrap_text(small(), S("errors.empty_sub"), W - 2 * M - 80, 6)) {
            small().draw(c, M + 40, y, l, kDim);
            y += small().line_height() + 4;
        }
        draw_hints(c, {{PadGlyph::Circle, S("common.back")}});
        return;
    }
    const int last = std::min(n, err_top_ + kErrVisible);
    text_right(c, small(), W - M, 148, S.fmt("findings.range", {{"from", std::to_string(err_top_ + 1)}, {"to", std::to_string(last)}, {"n", std::to_string(n)}}), kDim);

    const int cw = W - 2 * M, ch = 76, gap = 10, y0 = 200;
    int y = y0;
    for (int i = err_top_; i < last; ++i, y += ch + gap) {
        const auto& e = errors_.codes[static_cast<std::size_t>(i)];
        card(c, M, y, cw, ch, i == err_sel_);
        const int ty = y + (ch - body().line_height()) / 2;
        draw_icon(c, e.level == "doc" ? Icon::Ok : Icon::Info, M + 52, y + ch / 2, 40, level_color(e), level_color(e));
        body().draw(c, M + 94, ty, e.id, kText);
        const std::string level = S("errors.level." + e.level);
        const int lw = text_right(c, small(), M + cw - 32, y + (ch - small().line_height()) / 2, level, level_color(e));
        const int tx = M + 94 + 250;
        body().draw(c, tx, ty, elide(body(), pick_text(lang_, e.title_de, e.title_en, "error_codes", e.id, "title"), M + cw - 32 - lw - 28 - tx), kDim);
    }
    if (n > kErrVisible) {  // Scrollleiste
        const int track_h = kErrVisible * (ch + gap) - gap;
        const int thumb_h = std::max(40, track_h * kErrVisible / n);
        const int thumb_y = y0 + (track_h - thumb_h) * err_top_ / std::max(n - kErrVisible, 1);
        c.fill_round_rect(W - 64, y0, 8, track_h, 4, kPanel);
        c.fill_round_rect(W - 64, thumb_y, 8, thumb_h, 4, kDim);
    }
    small().draw(c, M + 8, H - 138, elide(small(), S("errors.readonly"), cw - 16), kDim);

    std::vector<std::pair<PadGlyph, std::string>> hints = {{PadGlyph::UpDown, S("common.select")}};
    if (n > kErrVisible) hints.emplace_back(PadGlyph::Shoulders, S("common.page"));
    hints.emplace_back(PadGlyph::Cross, S("common.open"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_error_detail(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    const medic::ErrorCode* e = current_error();
    if (!e) return;
    const int cw = W - 2 * M, n = static_cast<int>(errors_.codes.size());

    // Kopf: Code, Herkunft der Angaben (Sony-Seite oder Community), Kurztitel, Position
    draw_icon_badge(c, Icon::Book, M, 124, 60, kTintOrange);
    title().draw(c, M + 80, 126, e->id, kText);
    const std::string level = S("errors.level." + e->level);
    const int bw = small().width(level) + 24;
    draw_badge(c, W - M - bw, 138, level, level_color(*e));
    {
        std::string pos = err_from_finding_ ? std::string() : S.fmt("errors.position", {{"i", std::to_string(err_sel_ + 1)}, {"n", std::to_string(n)}});
        if (reported_error_ == e->id) pos += (pos.empty() ? "" : " · ") + S("errors.in_report");
        text_right(c, small(), W - M, 204, pos, reported_error_ == e->id ? kOk : kDim);
    }
    body().draw(c, M + 80, 196, elide(body(), pick_text(lang_, e->title_de, e->title_en, "error_codes", e->id, "title"), cw - 300), kDim);

    // Text: Zeilen ab err_scroll_, soweit sie in das Panel passen
    c.fill_round_rect(M, kErrPanelY, cw, kErrPanelH, 20, kPanel);
    const auto rows = error_rows(*e, lang_, err_ctx_);
    const int max_first = max_first_row(rows);
    const int first = std::clamp(err_scroll_, 0, max_first);
    int y = kErrPanelY + kErrPadY;
    const int y_end = y + kErrAvail;
    for (std::size_t i = static_cast<std::size_t>(first); i < rows.size(); ++i) {
        const ErrRow& r = rows[i];
        const int h = row_height(r);
        if (y + h > y_end) break;
        if (r.label && i + 1 < rows.size() && y + h + row_height(rows[i + 1]) > y_end) break;  // Ueberschrift nicht allein am Rand
        const Font& f = r.small ? small() : body();
        if (!r.marker.empty()) f.draw(c, M + kErrPadX, y + r.gap, r.marker, kDim);
        f.draw(c, M + kErrPadX + r.indent, y + r.gap, r.text, r.col);
        y += h;
    }
    if (max_first > 0) {  // Scrollleiste im Panel
        const int track_h = kErrAvail, nrows = static_cast<int>(rows.size());
        const int thumb_h = std::max(40, track_h * (nrows - max_first) / std::max(nrows, 1));
        const int thumb_y = kErrPadY + kErrPanelY + (track_h - thumb_h) * first / max_first;
        c.fill_round_rect(M + cw - 24, kErrPanelY + kErrPadY, 8, track_h, 4, kPanelHi);
        c.fill_round_rect(M + cw - 24, thumb_y, 8, thumb_h, 4, kDim);
    }
    small().draw(c, M + 8, kErrPanelY + kErrPanelH + 10, elide(small(), S("errors.readonly"), cw - 16), kDim);

    std::vector<std::pair<PadGlyph, std::string>> hints;
    if (max_first > 0) hints.emplace_back(PadGlyph::UpDown, S("errors.scroll"));
    if (n > 1 && !err_from_finding_) hints.emplace_back(PadGlyph::Shoulders, S("common.browse"));
    hints.emplace_back(PadGlyph::Square, S("errors.hint_source"));
    hints.emplace_back(PadGlyph::Triangle, S(reported_error_ == e->id ? "errors.report_remove" : "errors.report_add"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

// QR-Code der Quelle: Die Konsole kann keine Adresse oeffnen, das Handy liest sie. PS5 Medic ruft sie nie selbst auf.
void App::draw_error_source(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    const medic::ErrorCode* e = current_error();
    if (!e) return;
    const auto sources = error_sources(*e);
    const int idx = std::clamp(err_src_, 0, static_cast<int>(sources.size()) - 1);
    title_icon(c, Icon::Book, kTintOrange, S("errors.source_title"), 132);
    small().draw(c, M + 4, 192, e->id, kDim);

    const int px0 = M, py0 = 230, pw = 880, pad = 36, tw = pw - 2 * pad;
    c.fill_round_rect(px0, py0, pw, 700, 20, kPanel);
    int y = py0 + 30;
    small().draw(c, px0 + pad, y, S.fmt("errors.source_of", {{"i", std::to_string(idx + 1)}, {"n", std::to_string(sources.size())}}), kDim);
    y += small().line_height() + 14;
    for (const auto& l : wrap_text(body(), sources[static_cast<std::size_t>(idx)], tw, 8)) {
        body().draw(c, px0 + pad, y, l, kText);
        y += body().line_height() + 4;
    }
    y += 26;
    for (const auto& l : wrap_text(small(), S("errors.source_scan"), tw, 5)) {
        small().draw(c, px0 + pad, y, l, kDim);
        y += small().line_height() + 4;
    }

    const int ax = 1036, ay = 150, aw = 788, ah = 800;
    if (!src_qr_ok_) {
        c.fill_round_rect(ax, ay + 80, aw, 240, 20, kPanel);
        body().draw(c, ax + pad, ay + 140, S("errors.source_qr_failed"), kWarn, aw - 2 * pad);
    } else {
        draw_qr_code(c, src_qr_, ax, ay, aw, ah);
    }
    std::vector<std::pair<PadGlyph, std::string>> hints;
    if (sources.size() > 1) hints.emplace_back(PadGlyph::Shoulders, S("common.browse"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

}  // namespace medic::ui
