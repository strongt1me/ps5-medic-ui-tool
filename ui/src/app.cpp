#include "medic_ui/app.hpp"

#include "medic_ui/font.hpp"

#include <algorithm>

namespace medic::ui {

namespace {

constexpr int W = App::kWidth, H = App::kHeight, M = 96;
constexpr int kListVisible = 4, kHistVisible = 6;

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

}  // namespace

App::App(IMedicService& svc, Lang lang) : svc_(svc), lang_(lang) {}

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

void App::open_history() {
    hist_ = svc_.history();
    std::reverse(hist_.begin(), hist_.end());  // neueste zuerst
    hist_sel_ = hist_top_ = 0;
    screen_ = Screen::History;
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
            if (b == Button::Up) start_sel_ = std::max(start_sel_ - 1, 0);
            else if (b == Button::Down) start_sel_ = std::min(start_sel_ + 1, 2);
            else if (b == Button::Cross) {
                if (start_sel_ == 0) start_check();
                else if (start_sel_ == 1) open_history();
                else screen_ = Screen::Settings;
            }
            break;
        case Screen::Running:
            break;  // nicht unterbrechbar (dauert Sekunden)
        case Screen::Findings:
            if (b == Button::Up) move_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, -1);
            else if (b == Button::Down) move_selection(sel_, top_, static_cast<int>(result_.findings.size()), kListVisible, 1);
            else if (b == Button::Cross) {
                if (result_.findings.empty()) start_check();
                else screen_ = Screen::Detail;
            } else if (b == Button::Triangle) begin_fix(sel_, Screen::Findings);
            else if (b == Button::Circle) screen_ = Screen::Start;
            break;
        case Screen::Detail:
            if (b == Button::Triangle) begin_fix(sel_, Screen::Detail);
            else if (b == Button::Circle) screen_ = Screen::Findings;
            break;
        case Screen::FixPreview:
            if (b == Button::Cross && !plan_.empty()) {
                const medic::ApplyResult r = svc_.apply_fix(plan_);
                done_kind_ = DoneKind::Fix;
                done_ok_ = r.ok;
                done_text_ = r.ok ? r.txn : r.error;
                screen_ = Screen::FixDone;
            } else if (b == Button::Circle) screen_ = fix_origin_;
            break;
        case Screen::FixDone:
            if (done_kind_ == DoneKind::Fix) {
                if (b == Button::Cross) start_check();  // Befunde sind nach einer Reparatur veraltet
                else if (b == Button::Circle) screen_ = Screen::Start;
            } else if (b == Button::Cross || b == Button::Circle) {
                open_history();
            }
            break;
        case Screen::History:
            if (b == Button::Up) move_selection(hist_sel_, hist_top_, static_cast<int>(hist_.size()), kHistVisible, -1);
            else if (b == Button::Down) move_selection(hist_sel_, hist_top_, static_cast<int>(hist_.size()), kHistVisible, 1);
            else if (b == Button::Cross) {
                if (hist_sel_ >= 0 && hist_sel_ < static_cast<int>(hist_.size()) && hist_[static_cast<std::size_t>(hist_sel_)].state == "applied") {
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
                screen_ = Screen::FixDone;
            } else if (b == Button::Circle) screen_ = Screen::History;
            break;
        case Screen::Settings:
            if (b == Button::Left || b == Button::Right || b == Button::Cross) lang_ = lang_ == Lang::De ? Lang::En : Lang::De;
            else if (b == Button::Circle) screen_ = Screen::Start;
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
        case Screen::FixDone: draw_fix_done(c); break;
        case Screen::History: draw_history(c); break;
        case Screen::UndoConfirm: draw_undo_confirm(c); break;
        case Screen::Settings: draw_settings(c); break;
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

    // Zwei kleine Knoepfe
    const int cw = (W - 2 * M - 32) / 2;
    const char* keys[2][2] = {{"start.history", "start.history_sub"}, {"start.settings", "start.settings_sub"}};
    for (int i = 0; i < 2; ++i) {
        const int x = M + i * (cw + 32);
        card(c, x, 380, cw, 130, start_sel_ == i + 1);
        body().draw(c, x + 36, 404, S(keys[i][0]), kText);
        small().draw(c, x + 36, 452, S(keys[i][1]), kDim, cw - 72);
    }

    // Letztes Ergebnis
    c.fill_round_rect(M, 550, W - 2 * M, 250, 20, kPanel);
    small().draw(c, M + 36, 576, S("start.last"), kDim);
    if (!has_result_) {
        body().draw(c, M + 36, 630, S("start.last_none"), kText);
    } else if (result_.findings.empty()) {
        body().draw(c, M + 36, 630, S("start.last_clean"), kOk);
    } else {
        int crit = 0, warn = 0, info = 0;
        for (auto& f : result_.findings) {
            if (f.severity == medic::Severity::Critical) ++crit;
            else if (f.severity == medic::Severity::Warning) ++warn;
            else ++info;
        }
        body().draw(c, M + 36, 630,
                    S.fmt("start.last_summary", {{"n", std::to_string(result_.findings.size())}, {"crit", std::to_string(crit)}, {"warn", std::to_string(warn)}, {"info", std::to_string(info)}}),
                    kText, W - 2 * M - 72);
    }
    if (has_result_ && !result_.rules_version.empty()) small().draw(c, M + 36, 740, S.fmt("start.rules", {{"v", result_.rules_version}}), kDim);

    draw_hints(c, {{PadGlyph::UpDown, S("common.select")}, {PadGlyph::Cross, S("common.open")}});
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
        title().draw(c, M + 60, 372, S("findings.empty"), kOk);
        body().draw(c, M + 60, 460, S("findings.empty_sub"), kDim, W - 2 * M - 120);
        draw_hints(c, {{PadGlyph::Cross, S("common.retest")}, {PadGlyph::Circle, S("common.back")}});
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

    std::vector<std::pair<PadGlyph, std::string>> hints = {{PadGlyph::UpDown, S("common.select")}, {PadGlyph::Cross, S("common.open")}};
    if (const auto* f = current_finding(); f && !f->fix.empty()) hints.emplace_back(PadGlyph::Triangle, S(is_hint_fix(f->fix) ? "common.guide" : "common.repair"));
    hints.emplace_back(PadGlyph::Circle, S("common.back"));
    draw_hints(c, hints);
}

void App::draw_detail(Canvas& c) const {
    const auto& S = Strings::get(lang_);
    const auto* f = current_finding();
    if (!f) return;
    const int cw = W - 2 * M;
    int y = 140;
    const int bw = draw_badge(c, M, y, S(sev_key(f->severity)), sev_color(f->severity));
    body().draw(c, M + bw + 20, y, f->rule_id, kDim);
    const std::string vkey = "verified." + (f->verified.empty() ? std::string("todo") : f->verified);
    text_right(c, small(), W - M, y + 4, Strings::get(lang_).has(vkey) ? S(vkey) : f->verified, kDim);
    y += 58;

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

    // Evidenz
    {
        constexpr std::size_t kMaxEvidence = 5;
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

    // Quelle und Reparatur
    if (!f->source.empty()) {
        small().draw(c, M + 8, y, elide(small(), S("detail.source") + ": " + f->source, cw - 16), kDim);
        y += small().line_height() + 14;
    }
    if (!f->fix.empty()) body().draw(c, M + 8, y, S.fmt(is_hint_fix(f->fix) ? "detail.hint_available" : "detail.fix_available", {{"name", fix_name(f->fix)}}), kOk, cw - 16);
    else body().draw(c, M + 8, y, S("detail.fix_none"), kDim, cw - 16);

    std::vector<std::pair<PadGlyph, std::string>> hints;
    if (!f->fix.empty()) hints.emplace_back(PadGlyph::Triangle, S(is_hint_fix(f->fix) ? "common.guide" : "common.repair"));
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
    std::vector<std::pair<PadGlyph, std::string>> hints;
    if (fix) {
        hints.emplace_back(PadGlyph::Cross, S("common.retest"));
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
    const bool can_undo = hist_sel_ >= 0 && hist_sel_ < n && hist_[static_cast<std::size_t>(hist_sel_)].state == "applied";
    std::vector<std::pair<PadGlyph, std::string>> hints = {{PadGlyph::UpDown, S("common.select")}};
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
    card(c, M, 230, W - 2 * M, 150, true);
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
    if (has_result_ && !result_.rules_version.empty()) small().draw(c, M + 8, 420, S.fmt("settings.rules", {{"v", result_.rules_version}}), kDim);
    draw_hints(c, {{PadGlyph::LeftRight, S("common.toggle")}, {PadGlyph::Circle, S("common.back")}});
}

}  // namespace medic::ui
