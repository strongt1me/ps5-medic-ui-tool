// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/app.hpp"

#include "medic_ui/font.hpp"

#include "medic/history.hpp"

#include <algorithm>

namespace medic::ui {

namespace {

constexpr int W = App::kWidth, H = App::kHeight, M = 96;
constexpr int kListVisible = 4, kHistVisible = 6, kErrVisible = 8;

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

std::vector<ErrRow> error_rows(const medic::ErrorCode& e, Lang lang, const CodeContext& ctx) {
    const auto& S = Strings::get(lang);
    const bool de = lang == Lang::De;
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
    paragraph(de ? e.text_de : e.text_en, false, kText, "", 0);

    label(S("errors.steps"), 18);
    const auto& steps = de ? e.steps_de : e.steps_en;
    if (steps.empty()) paragraph(S("errors.steps_none"), false, kDim, "", 0);
    for (std::size_t i = 0; i < steps.size(); ++i) paragraph(steps[i], false, kText, std::to_string(i + 1) + ".", kStepIndent);

    const auto& notes = de ? e.notes_de : e.notes_en;
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

App::App(IMedicService& svc, Lang lang) : svc_(svc), lang_(lang) { errors_ = svc_.error_codes(); }

std::string App::finding_text(const medic::Finding& f) const { return lang_ == Lang::De ? f.text_de : f.text_en; }

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
    screen_ = Screen::Running;
    pending_check_ = true;
    running_rendered_ = false;
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
    switch (screen_) {
        case Screen::Start:
            if (b == Button::Up || b == Button::L1) start_sel_ = std::max(start_sel_ - 1, 0);
            else if (b == Button::Down || b == Button::R1) start_sel_ = std::min(start_sel_ + 1, 3);
            else if (b == Button::Left && start_sel_ >= 2) --start_sel_;   // die drei kleinen Knoepfe liegen nebeneinander
            else if (b == Button::Right && start_sel_ >= 1 && start_sel_ < 3) ++start_sel_;
            else if (b == Button::Cross) {
                if (start_sel_ == 0) start_check();
                else if (start_sel_ == 1) open_history();
                else if (start_sel_ == 2) screen_ = Screen::Settings;
                else open_error_codes();
            } else if (b == Button::Circle) quit_ = true;
            break;
        case Screen::Running:
            break;  // nicht unterbrechbar (dauert Sekunden)
        case Screen::Findings:
            if (b == Button::Up) move_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, -1);
            else if (b == Button::Down) move_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, 1);
            else if (b == Button::L1) page_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, -1);
            else if (b == Button::R1) page_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, 1);
            else if (b == Button::Cross) {
                if (result_.findings.empty()) start_check();
                else screen_ = Screen::Detail;
            } else if (b == Button::Triangle) begin_fix(sel_, Screen::Findings);
            else if (b == Button::Square) open_report();
            else if (b == Button::Circle) screen_ = Screen::Start;
            break;
        case Screen::Detail:
            // Zum vorherigen/naechsten Befund blaettern, ohne zur Liste zurueckzugehen (Steuerkreuz/Stick links-rechts oder L1/R1).
            if (b == Button::Left || b == Button::L1) move_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, -1);
            else if (b == Button::Right || b == Button::R1) move_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, 1);
            else if (b == Button::Triangle) begin_fix(sel_, Screen::Detail);
            else if (b == Button::Square) open_error_for_finding();
            else if (b == Button::Circle) screen_ = Screen::Findings;
            break;
        case Screen::FixPreview:
            if (b == Button::Cross && !plan_.empty()) {
                const medic::ApplyResult r = svc_.apply_fix(plan_);
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
            } else if (b == Button::Circle) screen_ = Screen::Start;
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
            else if (settings_sel_ == 0 && (b == Button::Left || b == Button::Right || b == Button::Cross)) lang_ = lang_ == Lang::De ? Lang::En : Lang::De;
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

void App::update() {
    ++tick_;
    if (pending_check_ && running_rendered_) {  // erst ausfuehren, wenn "Pruefe..." sichtbar war
        pending_check_ = false;
        result_ = svc_.run_check();
        has_result_ = true;
        sel_ = top_ = 0;
        screen_ = Screen::Findings;
    }
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
    c.clear(kBg);
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
        case Screen::Settings: draw_settings(c); break;
        case Screen::Report: draw_report(c); break;
        case Screen::RulesUpdate: draw_rules_update(c); break;
        case Screen::ErrorCodes: draw_error_codes(c); break;
        case Screen::ErrorCodeDetail: draw_error_detail(c); break;
        case Screen::ErrorCodeSource: draw_error_source(c); break;
    }
}

void App::draw_chrome(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title().draw(c, M, 28, S("app.title"), kText);
    const std::string sys = has_result_ && !result_.fw.empty() ? S.fmt("header.system", {{"fw", result_.fw}, {"model", result_.model}})
                                                                : (has_result_ ? S("header.unknown") : std::string());
    if (!sys.empty()) text_right(c, small(), W - M, 52, sys, kDim);
    c.fill_rect(M, 112, W - 2 * M, 2, kLine);
    c.fill_rect(M, 112, 160, 2, kAccent);
}

void App::draw_hints(Canvas& c, const std::vector<std::pair<PadGlyph, std::string>>& hints) const {
    int x = M;
    const int y = H - 92;
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
        x += 54 + body().width(label) + 56;
    }
}

void App::draw_start(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    // Grosser Knopf
    const bool sel0 = start_sel_ == 0;
    c.fill_round_rect(M, 160, W - 2 * M, 190, 24, sel0 ? kAccent : rgb(38, 82, 140));
    if (sel0) c.stroke_round_rect(M, 160, W - 2 * M, 190, 24, 4, rgb(190, 220, 255));
    title().draw(c, M + 48, 190, S("start.check"), rgb(255, 255, 255));
    body().draw(c, M + 48, 266, S("start.check_sub"), rgb(220, 234, 255), W - 2 * M - 96);

    // Drei kleine Knoepfe (Verlauf, Einstellungen, Fehlercodes)
    const int gap = 24, cw = (W - 2 * M - 2 * gap) / 3;
    const char* keys[3][2] = {{"start.history", "start.history_sub"}, {"start.settings", "start.settings_sub"}, {"start.errors", "start.errors_sub"}};
    for (int i = 0; i < 3; ++i) {
        const int x = M + i * (cw + gap);
        card(c, x, 380, cw, 150, start_sel_ == i + 1);
        body().draw(c, x + 32, 400, S(keys[i][0]), kText, cw - 64);
        int ty = 446;
        for (const auto& l : wrap_text(small(), S(keys[i][1]), cw - 64, 2)) {
            small().draw(c, x + 32, ty, l, kDim);
            ty += small().line_height() + 2;
        }
    }

    // Letztes Ergebnis
    c.fill_round_rect(M, 560, W - 2 * M, 240, 20, kPanel);
    small().draw(c, M + 36, 584, S("start.last"), kDim);
    if (!has_result_) {
        body().draw(c, M + 36, 636, S("start.last_none"), kText);
    } else if (result_.findings.empty()) {
        const int untested = static_cast<int>(std::count_if(result_.coverage.begin(), result_.coverage.end(), [](const medic::CoverageResult& r) { return !r.tested; }));
        body().draw(c, M + 36, 636, S(untested ? "start.last_clean_partial" : "start.last_clean"), untested ? kText : kOk);
        if (untested) small().draw(c, M + 36, 690, S.fmt("start.not_tested", {{"n", std::to_string(untested)}}), kDim);
    } else {
        int crit = 0, warn = 0, info = 0;
        for (auto& f : result_.findings) {
            if (f.severity == medic::Severity::Critical) ++crit;
            else if (f.severity == medic::Severity::Warning) ++warn;
            else ++info;
        }
        body().draw(c, M + 36, 636,
                    S.fmt("start.last_summary", {{"n", std::to_string(result_.findings.size())}, {"crit", std::to_string(crit)}, {"warn", std::to_string(warn)}, {"info", std::to_string(info)}}),
                    kText, W - 2 * M - 72);
        if (const int untested = static_cast<int>(std::count_if(result_.coverage.begin(), result_.coverage.end(), [](const medic::CoverageResult& r) { return !r.tested; })))
            small().draw(c, M + 36, 700, S.fmt("start.not_tested", {{"n", std::to_string(untested)}}), kDim);
    }
    if (has_result_ && !result_.rules_version.empty()) small().draw(c, M + 36, 744, S.fmt("start.rules", {{"v", result_.rules_version}}), kDim);

    draw_hints(c, {{PadGlyph::UpDown, S("common.select")}, {PadGlyph::Cross, S("common.open")}, {PadGlyph::Circle, S("common.quit")}});
}

void App::draw_running(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    c.fill_round_rect(M, 300, W - 2 * M, 330, 24, kPanel);
    std::string dots(static_cast<std::size_t>(tick_ % 4), '.');
    title().draw(c, M + 60, 360, S("running.title") + dots, kText);
    body().draw(c, M + 60, 450, S("running.sub"), kDim, W - 2 * M - 120);
    // Laufbalken
    const int bw = W - 2 * M - 120, seg = 260, pos = (tick_ * 40) % (bw + seg) - seg;
    c.fill_round_rect(M + 60, 540, bw, 12, 6, kLine);
    for (int x = std::max(pos, 0); x < std::min(pos + seg, bw); ++x) c.fill_rect(M + 60 + x, 540, 1, 12, kAccent);
}

void App::draw_findings(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    const auto& fs = result_.findings;
    const int n = static_cast<int>(fs.size());
    if (n == 0) {
        c.fill_round_rect(M, 300, W - 2 * M, 320, 24, kPanel);
        std::string untested_names;
        for (const auto& r : result_.coverage)
            if (!r.tested) untested_names += (untested_names.empty() ? "" : ", ") + (lang_ == Lang::De ? r.text_de : r.text_en);
        if (untested_names.empty()) {
            title().draw(c, M + 60, 372, S("findings.empty"), kOk);
            body().draw(c, M + 60, 460, S("findings.empty_sub"), kDim, W - 2 * M - 120);
        } else {
            title().draw(c, M + 60, 372, S("findings.empty_partial"), kText);
            body().draw(c, M + 60, 460, S.fmt("findings.empty_partial_sub", {{"list", untested_names}}), kDim, W - 2 * M - 120);
        }
        draw_hints(c, {{PadGlyph::Cross, S("common.retest")}, {PadGlyph::Square, S("common.report")}, {PadGlyph::Circle, S("common.back")}});
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
        const int w = small().width(label) + 50;
        c.fill_round_rect(cx, 142, w, 34, 17, kPanel);
        c.fill_round_rect(cx + 14, 154, 10, 10, 5, sev_color(sev));
        small().draw(c, cx + 34, 146, label, kText);
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
        const int bw = draw_badge(c, M + 36, y + 16, S(sev_key(f.severity)), sc);
        small().draw(c, M + 36 + bw + 16, y + 20, f.rule_id, kDim);
        if (!f.fix.empty()) text_right(c, small(), M + cw - 32, y + 20, S(is_hint_fix(f.fix) ? "findings.hintable" : "findings.fixable"), kOk);
        const auto lines = wrap_text(body(), finding_text(f), cw - 72, 2);
        for (std::size_t k = 0; k < lines.size(); ++k)
            body().draw(c, M + 36, y + 62 + static_cast<int>(k) * (body().line_height() + 2), lines[k], kText);
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
    if (const auto* f = current_finding(); f && !f->fix.empty()) hints.emplace_back(PadGlyph::Triangle, S(is_hint_fix(f->fix) ? "common.guide" : "common.repair"));
    hints.emplace_back(PadGlyph::Square, S("common.report"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_report(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title().draw(c, M, 132, S("report.title"), kText);

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
    const int bw = draw_badge(c, M, y, S(sev_key(f->severity)), sev_color(f->severity));
    body().draw(c, M + bw + 20, y, f->rule_id, kDim);
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

    const std::string& conf_reason = lang_ == Lang::De ? conf.reason_de : conf.reason_en;
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
    if (!f->fix.empty()) body().draw(c, M + 8, y, S.fmt(is_hint_fix(f->fix) ? "detail.hint_available" : "detail.fix_available", {{"name", fix_name(f->fix)}}), kOk, cw - 16);
    else body().draw(c, M + 8, y, S("detail.fix_none"), kDim, cw - 16);
    if (link >= 0) {
        y += body().line_height() + 12;
        body().draw(c, M + 8, y, S.fmt("detail.error_code", {{"code", errors_.codes[static_cast<std::size_t>(link)].id}}), kAccent, cw - 16);
    }

    std::vector<std::pair<PadGlyph, std::string>> hints;
    if (result_.findings.size() > 1) hints.emplace_back(PadGlyph::Shoulders, S("common.browse"));
    if (!f->fix.empty()) hints.emplace_back(PadGlyph::Triangle, S(is_hint_fix(f->fix) ? "common.guide" : "common.repair"));
    if (link >= 0) hints.emplace_back(PadGlyph::Square, S("common.lookup"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_fix_preview(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    const int cw = W - 2 * M;
    title().draw(c, M, 130, S("fixprev.title"), kText);
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
        const std::string note = lang_ == Lang::De ? ch.note_de : ch.note_en;
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
    const std::string& hint = lang_ == Lang::De ? plan_.hint_de : plan_.hint_en;
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
    title().draw(c, M + 60, 316, S(head), done_ok_ ? kOk : kCrit);
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
    title().draw(c, M, 130, S("history.title"), kText);
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
        small().draw(c, M + 32, y + 14, pretty_ts(t.ts.empty() ? t.txn : t.ts) + "   " + t.rule, kDim, cw - 400);
        body().draw(c, M + 32, y + 44, elide(body(), fix_name(t.fix), cw - 520), kText);
        const std::string skey = "history.state." + t.state;
        const Color sc = t.state == "applied" ? kOk : (t.state == "undone" ? kDim : kWarn);
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

void App::draw_undo_confirm(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    c.fill_round_rect(M, 260, W - 2 * M, 380, 24, kPanel);
    title().draw(c, M + 60, 310, S("undo.title"), kWarn);
    int y = 410;
    for (auto& l : wrap_text(body(), S.fmt("undo.body", {{"fix", fix_name(undo_target_.fix)}, {"ts", pretty_ts(undo_target_.ts.empty() ? undo_target_.txn : undo_target_.ts)}}), W - 2 * M - 120, 5)) {
        body().draw(c, M + 60, y, l, kText);
        y += body().line_height() + 6;
    }
    draw_hints(c, {{PadGlyph::Cross, S("common.confirm")}, {PadGlyph::Circle, S("common.cancel")}});
}

void App::draw_settings(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title().draw(c, M, 130, S("settings.title"), kText);
    card(c, M, 230, W - 2 * M, 150, settings_sel_ == 0);
    body().draw(c, M + 40, 262, S("settings.language"), kText);
    small().draw(c, M + 40, 316, S("settings.note"), kDim);
    int x = W - M - 40;
    for (Lang l : {Lang::En, Lang::De}) {
        const std::string name = S(l == Lang::De ? "settings.lang_de" : "settings.lang_en");
        const int w = body().width(name) + 56;
        x -= w;
        const bool on = l == lang_;
        c.fill_round_rect(x, 262, w, 56, 28, on ? kAccent : kLine);
        body().draw(c, x + 28, 262 + (56 - body().line_height()) / 2, name, on ? rgb(255, 255, 255) : kDim);
        x -= 16;
    }
    card(c, M, 410, W - 2 * M, 150, settings_sel_ == 1);
    body().draw(c, M + 40, 442, S("settings.rules_update"), kText);
    small().draw(c, M + 40, 496, S("settings.rules_update_sub"), kDim);
    if (has_result_ && !result_.rules_version.empty()) small().draw(c, M + 8, 590, S.fmt("settings.rules", {{"v", result_.rules_version}}), kDim);
    if (settings_sel_ == 0) draw_hints(c, {{PadGlyph::UpDown, S("common.select")}, {PadGlyph::LeftRight, S("common.toggle")}, {PadGlyph::Circle, S("common.back")}});
    else draw_hints(c, {{PadGlyph::UpDown, S("common.select")}, {PadGlyph::Cross, S("common.open")}, {PadGlyph::Circle, S("common.back")}});
}

void App::draw_rules_update(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    title().draw(c, M, 130, S("update.title"), kText);
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
    title().draw(c, M, 130, S("errors.title"), kText);
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
        body().draw(c, M + 32, ty, e.id, kText);
        const std::string level = S("errors.level." + e.level);
        const int lw = text_right(c, small(), M + cw - 32, y + (ch - small().line_height()) / 2, level, level_color(e));
        const int tx = M + 32 + 250;
        body().draw(c, tx, ty, elide(body(), lang_ == Lang::De ? e.title_de : e.title_en, M + cw - 32 - lw - 28 - tx), kDim);
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
    title().draw(c, M, 126, e->id, kText);
    const std::string level = S("errors.level." + e->level);
    const int bw = small().width(level) + 24;
    draw_badge(c, W - M - bw, 138, level, level_color(*e));
    {
        std::string pos = err_from_finding_ ? std::string() : S.fmt("errors.position", {{"i", std::to_string(err_sel_ + 1)}, {"n", std::to_string(n)}});
        if (reported_error_ == e->id) pos += (pos.empty() ? "" : " · ") + S("errors.in_report");
        text_right(c, small(), W - M, 204, pos, reported_error_ == e->id ? kOk : kDim);
    }
    body().draw(c, M, 196, elide(body(), lang_ == Lang::De ? e->title_de : e->title_en, cw - 220), kDim);

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
    title().draw(c, M, 132, S("errors.source_title"), kText);
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
