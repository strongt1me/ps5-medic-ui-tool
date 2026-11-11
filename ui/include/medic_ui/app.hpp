// SPDX-License-Identifier: GPL-3.0-or-later
// Vollbild-App: Bildschirme, Eingabe und Darstellung. Plattformunabhaengig (rendert auf eine Canvas, 1920x1080).
// Ablauf: handle() fuer jede Taste, update() einmal pro Bild, render() zeichnet den aktuellen Bildschirm.
//
// Sicherheitsregel: Geschrieben wird nur ueber IMedicService::apply_fix/undo, und nur nach der Bestaetigung
// (Kreuz) auf dem Vorschau- bzw. Rueckgaengig-Bildschirm. Einzige Ausnahme: die Quadrat-Taste in der Befundliste
// legt einen Bericht nach /data/ps5medic/reports/ ab (IMedicService::make_report); sonst loest keine Taste Schreiben aus.
// Das Regel-Update (Einstellungen) schreibt ebenfalls nur nach Kreuz auf dem Angebot bzw. auf der Rueckfrage.
// "Verlauf zuruecksetzen" (Dreieck im Verlauf) legt Journal und Check-up-Verlauf nur nach Kreuz auf der Rueckfrage beiseite (verschieben, nie loeschen).
// Die Fehlercodes (Start) sind ein reines Nachschlagewerk: sie zeigen Text, kein Knopf dort loest etwas aus.
#pragma once
#include "medic/diff.hpp"
#include "medic/qr.hpp"
#include "medic_ui/canvas.hpp"
#include "medic_ui/i18n.hpp"
#include "medic_ui/input.hpp"
#include "medic_ui/service.hpp"

#include <memory>
#include <string>
#include <vector>

namespace medic::ui {

class App {
public:
    enum class Screen { Start, Running, Findings, Detail, FixPreview, FixDone, History, UndoConfirm, Settings, Report, RulesUpdate, ErrorCodes, ErrorCodeDetail, ErrorCodeSource, SelfTest, UserTest, HistoryReset };

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
    int error_selected() const { return err_sel_; }  // Fehlercodes: gewaehlter Eintrag
    int error_scroll() const { return err_scroll_; } // Fehlercodes, Detail: erste sichtbare Textzeile
    std::string selected_error_id() const { return current_error() ? current_error()->id : std::string(); }
    const std::string& reported_error() const { return reported_error_; }  // fuer den Bericht vermerkter Code (leer: keiner)
    int error_source_index() const { return err_src_; }                     // Fehlercodes, Quelle: welche Quelle der QR-Code zeigt

private:
    struct Preview {  // Darstellung einer geplanten Aenderung
        std::vector<medic::DiffLine> changed;  // nur + und - Zeilen (WriteFile)
        bool diff_ok = true;
    };
    enum class DoneKind { Fix, Undo, Reset };

    void start_check();
    void begin_fix(int index, Screen origin);
    void move_selection(int& sel, int& top, int count, int visible, int delta);
    void page_selection(int& sel, int& top, int count, int visible, int dir);
    void open_history();
    void open_report();
    void open_rules_update();
    void open_error_codes();
    void open_error_for_finding();
    int linked_error_index() const;
    int error_scroll_max() const;
    const medic::ErrorCode* current_error() const;
    void refresh_error_context();
    void open_error_source();
    void open_selftest();
    void start_selftest(int tier_limit);  // 0 Schnell, 1 Standard, 2 Gruendlich
    void step_selftest();
    void scroll_error_detail(int dir);
    void install_updates();
    void rollback_updates();
    void finish_updates();

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
    void draw_history_reset(Canvas& c) const;
    void draw_settings(Canvas& c) const;
    void draw_report(Canvas& c) const;
    void draw_rules_update(Canvas& c) const;
    void draw_error_codes(Canvas& c) const;
    void draw_error_detail(Canvas& c) const;
    void draw_error_source(Canvas& c) const;
    void draw_selftest(Canvas& c) const;
    void draw_usertest(Canvas& c) const;
    void open_usertest(bool pad);
    void handle_usertest(Button b);
    void finish_usertest(medic::TestStatus st, const std::string& reason, double missing);

    std::string finding_text(const medic::Finding& f) const;
    std::string fix_name(const std::string& id) const;
    const medic::Finding* current_finding() const;

    IMedicService& svc_;
    mutable std::unique_ptr<Canvas> bg_;  // Hintergrund (Verlauf, Lichtflecken), einmal gezeichnet
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

    // Erneute Pruefung nach einer erfolgreichen Reparatur ("Re-Detect"): laeuft erst, wenn "Pruefe erneut ..." einmal gezeichnet war, und sagt,
    // ob die Regel des reparierten Befunds noch gemeldet wird. Nur im Speicher; die Reparatur selbst ist im Journal.
    enum class Verify { None, Pending, Fixed, Persists };
    Verify verify_ = Verify::None;
    std::string verify_rule_;
    mutable bool verify_rendered_ = false;

    std::vector<medic::TxnInfo> hist_;
    int hist_sel_ = 0, hist_top_ = 0;
    medic::TxnInfo undo_target_;

    int settings_sel_ = 0;  // 0 Sprache, 1 Regel-Update

    // Selbsttest: List (letzte Ergebnisse), Confirm (Hinweis vor dem Lauf), Running (ein Test je update()), Done
    enum class StState { List, Confirm, Running, Done };
    StState st_state_ = StState::List;
    std::vector<medic::TestDef> st_defs_;
    std::vector<medic::TestResult> st_last_;                  // zuletzt gespeichertes oder gerade beendetes Ergebnis
    std::vector<medic::TestResult> st_run_;                   // Ergebnisse des laufenden Laufs
    std::vector<std::size_t> st_ids_;                         // Indizes (in st_defs_) der Tests dieses Laufs
    std::size_t st_next_ = 0;
    bool st_cancel_ = false, st_begun_ = false, st_item_started_ = false, st_abort_sent_ = false;
    int st_tier_ = 1;           // gewaehlte Stufe: 0 Schnell, 1 Standard, 2 Gruendlich
    double st_progress_ = 0;    // Fortschritt des laufenden Tests (0 bis 1)

    // Nutzer-Tests: Pad-Tasten (jede Taste einmal druecken) und Anzeige (Farbflaechen, dann Urteil)
    bool ut_pad_ = true;
    bool ut_pressed_[11] = {};
    int ut_presses_ = 0;
    bool ut_last_circle_ = false;
    int ut_color_ = 0;
    bool ut_asking_ = false;
    mutable bool st_rendered_ = false;  // die Fortschrittsanzeige muss vor jedem Test gezeichnet worden sein

    enum class UpdateStage { Offer, ConfirmRollback, ConfirmDownload, Downloading, DownloadFail, DoneOk, DoneFail };
    medic::RuleUpdateOffer offer_;
    medic::RuleUpdateResult update_result_;
    medic::RuleUpdateOffer codes_offer_;     // dasselbe fuer die Fehlercode-Datei (error_codes.json)
    medic::RuleUpdateResult codes_result_;
    bool rules_attempted_ = false, codes_attempted_ = false;  // was die letzte Installation bzw. Zuruecknahme betraf
    UpdateStage update_stage_ = UpdateStage::Offer;
    bool update_was_rollback_ = false;
    std::string download_url_;  // Adresse aus config.ini, beim Oeffnen der Rueckfrage gelesen
    medic::RuleDownloadResult download_result_;
    bool downloaded_ = false;   // das angezeigte Angebot stammt aus einem Download auf diesem Bildschirm
    bool pending_download_ = false;
    mutable bool download_rendered_ = false;  // "Lade herunter ..." muss einmal gezeichnet worden sein, bevor der Abruf blockiert

    medic::ErrorCodeSet errors_;  // beim Oeffnen vom Dienst geholt
    int err_sel_ = 0, err_top_ = 0;
    int err_scroll_ = 0;  // Detail: Index der ersten sichtbaren Textzeile
    CodeContext err_ctx_;          // "Was Medic gerade sieht" zum gewaehlten Code (beim Oeffnen geholt)
    std::string reported_error_;   // per Dreieck fuer den Bericht vermerkt
    int err_src_ = 0;              // Quelle (0 = Hauptquelle, danach more_sources), deren QR-Code gezeigt wird
    medic::QrCode src_qr_;
    bool src_qr_ok_ = false;
    int scroll_dir_ = 0, scroll_streak_ = 0, last_scroll_tick_ = -1000;  // Scrollen beschleunigt beim Halten
    bool err_from_finding_ = false;  // Code kam per Quadrat aus einem Befund: Kreis geht dorthin zurueck, kein Blaettern

    ReportResult report_;
    medic::QrCode qr_;       // einmal beim Oeffnen kodiert, nicht in jedem Bild
    bool qr_ok_ = false;
};

}  // namespace medic::ui
