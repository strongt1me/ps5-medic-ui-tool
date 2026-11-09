// SPDX-License-Identifier: GPL-3.0-or-later
// Vollbild-App: Bildschirme, Eingabe und Darstellung. Plattformunabhaengig (rendert auf eine Canvas, 1920x1080).
// Ablauf: handle() fuer jede Taste, update() einmal pro Bild, render() zeichnet den aktuellen Bildschirm.
//
// Sicherheitsregel: Geschrieben wird nur ueber IMedicService::apply_fix/undo, und nur nach der Bestaetigung
// (Kreuz) auf dem Vorschau- bzw. Rueckgaengig-Bildschirm. Einzige Ausnahme: die Quadrat-Taste in der Befundliste
// legt einen Bericht nach /data/ps5medic/reports/ ab (IMedicService::make_report); sonst loest keine Taste Schreiben aus.
// Das Regel-Update (Einstellungen) schreibt ebenfalls nur nach Kreuz auf dem Angebot bzw. auf der Rueckfrage.
// Das Fehlercode-Nachschlagen (Start) ist reines Wissen und schreibt nichts; das Dreieck dort startet nur den Check-up.
#pragma once
#include "medic/diff.hpp"
#include "medic/error_codes.hpp"
#include "medic/qr.hpp"
#include "medic_ui/canvas.hpp"
#include "medic_ui/i18n.hpp"
#include "medic_ui/input.hpp"
#include "medic_ui/service.hpp"

#include <string>
#include <vector>

namespace medic::ui {

class App {
public:
    enum class Screen { Start, Running, Findings, Detail, FixPreview, FixDone, History, UndoConfirm, Settings, Report, RulesUpdate, ErrorCodes };

    static constexpr int kWidth = 1920, kHeight = 1080;

    explicit App(IMedicService& svc, Lang lang = Lang::De);

    void handle(Button b);
    void update();
    void render(Canvas& c) const;

    Screen screen() const { return screen_; }
    Lang lang() const { return lang_; }
    std::size_t finding_count() const { return result_.findings.size(); }
    int selected() const { return sel_; }
    bool quit_requested() const { return quit_; }  // Kreis auf dem Startbildschirm
    // Fuer Tests: hat die letzte Darstellung der Fehlercodes Schritte weggelassen, weil sie nicht mehr in den Kasten passten?
    bool error_view_clipped() const { return err_clipped_; }

private:
    struct Preview {  // Darstellung einer geplanten Aenderung
        std::vector<medic::DiffLine> changed;  // nur + und - Zeilen (WriteFile)
        bool diff_ok = true;
    };
    enum class DoneKind { Fix, Undo };

    void start_check();
    void begin_fix(int index, Screen origin);
    void move_selection(int& sel, int& top, int count, int visible, int delta);
    void page_selection(int& sel, int& top, int count, int visible, int dir);
    void open_history();
    void open_report();
    void open_rules_update();
    void open_error_codes();
    void select_error_family(int index);
    void finish_rules_change(bool ok);

    void draw_chrome(Canvas& c) const;
    void draw_hints(Canvas& c, const std::vector<std::pair<PadGlyph, std::string>>& hints) const;
    void draw_start(Canvas& c) const;
    void draw_running(Canvas& c) const;
    void draw_findings(Canvas& c) const;
    void draw_detail(Canvas& c) const;
    void draw_fix_preview(Canvas& c) const;
    void draw_fix_done(Canvas& c) const;
    void draw_history(Canvas& c) const;
    void draw_undo_confirm(Canvas& c) const;
    void draw_settings(Canvas& c) const;
    void draw_report(Canvas& c) const;
    void draw_rules_update(Canvas& c) const;
    void draw_error_codes(Canvas& c) const;

    std::string finding_text(const medic::Finding& f) const;
    std::string fix_name(const std::string& id) const;
    const medic::Finding* current_finding() const;

    IMedicService& svc_;
    Lang lang_;
    Screen screen_ = Screen::Start;
    int tick_ = 0;
    bool quit_ = false;

    int start_sel_ = 0;

    bool has_result_ = false;
    CheckResult result_;
    bool pending_check_ = false;
    mutable bool running_rendered_ = false;  // der Bildschirm "Pruefe..." muss mindestens einmal gezeichnet worden sein
    int sel_ = 0, top_ = 0;

    medic::FixPlan plan_;
    std::vector<Preview> previews_;
    Screen fix_origin_ = Screen::Findings;

    DoneKind done_kind_ = DoneKind::Fix;
    bool done_ok_ = false;
    std::string done_text_;  // Transaktions-ID bzw. Fehlermeldung

    std::vector<medic::TxnInfo> hist_;
    int hist_sel_ = 0, hist_top_ = 0;
    medic::TxnInfo undo_target_;

    int settings_sel_ = 0;  // 0 Sprache, 1 Regel-Update

    // Fehlercodes nachschlagen: reines Wissen aus knowledge/error_codes.json (eingebettet), schreibt und liest nichts auf der Konsole.
    const medic::ErrorDb& errors_;
    int err_family_ = 0;                          // Index in errors_.prefixes
    std::vector<const medic::ErrorCode*> err_list_;  // Codes der gewaehlten Familie
    int err_sel_ = 0, err_top_ = 0;
    mutable bool err_clipped_ = false;

    enum class UpdateStage { Offer, ConfirmRollback, ConfirmDownload, Downloading, DownloadFail, DoneOk, DoneFail };
    medic::RuleUpdateOffer offer_;
    medic::RuleUpdateResult update_result_;
    UpdateStage update_stage_ = UpdateStage::Offer;
    bool update_was_rollback_ = false;
    std::string download_url_;  // Adresse aus config.ini, beim Oeffnen der Rueckfrage gelesen
    medic::RuleDownloadResult download_result_;
    bool downloaded_ = false;   // das angezeigte Angebot stammt aus einem Download auf diesem Bildschirm
    bool pending_download_ = false;
    mutable bool download_rendered_ = false;  // "Lade herunter ..." muss einmal gezeichnet worden sein, bevor der Abruf blockiert

    ReportResult report_;
    medic::QrCode qr_;       // einmal beim Oeffnen kodiert, nicht in jedem Bild
    bool qr_ok_ = false;
};

}  // namespace medic::ui
