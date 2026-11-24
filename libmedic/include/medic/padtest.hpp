// SPDX-License-Identifier: GPL-3.0-or-later
// Controller-Test (Stufe 1: nur Eingaben, nichts wird an den Controller gesendet): Tasten, Sticks (Ruhe/Drift und Rundlauf), Trigger, Touchpad, Bewegungssensoren.
// Dazu der Akkustand (nur Anzeige, ohne Einfluss auf das Ergebnis).
// Reine Auswertung ohne Plattform: die Oberflaeche fuettert Pad-Zustaende (feed), wechselt die Schritte (next/skip) und holt am Ende das Ergebnis.
// So laeuft die ganze Auswertung am PC gegen erfundene Pad-Verlaeufe (tests/test_padtest.cpp). Die Schwellen sind Richtwerte, keine gemessene Norm.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "medic/selftest.hpp"

namespace medic {

// Tasten-Bits wie ScePad (SDL-Treiber der PS5). Create und PS-Taste erreichen keine App und fehlen deshalb.
constexpr std::uint32_t kPadL3 = 0x2, kPadR3 = 0x4, kPadOptions = 0x8, kPadUp = 0x10, kPadRight = 0x20, kPadDown = 0x40, kPadLeft = 0x80, kPadL2 = 0x100,
                        kPadR2 = 0x200, kPadL1 = 0x400, kPadR1 = 0x800, kPadTriangle = 0x1000, kPadCircle = 0x2000, kPadCross = 0x4000, kPadSquare = 0x8000,
                        kPadTouch = 0x100000;

// Ein Abbild des Controllers. extra = false: nur Tasten und Sticks (aeltere App), dann sind Trigger, Touchpad und Bewegung nicht pruefbar.
struct PadSample {
    std::uint32_t buttons = 0;
    std::uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
    std::uint8_t l2 = 0, r2 = 0;  // analog 0..255
    bool extra = false;
    int touch_count = 0;  // 0..2
    struct Finger {
        int x = 0, y = 0;  // Touchpad: x 0..1919, y 0..1079 (das DualSense meldet etwas weniger in y)
    } finger[2];
    // Zwei Bewegungs-Dreiergruppen: welche die Beschleunigung ist und welche der Gyro, steht in der Struktur der App nicht sicher; der Test erkennt es an der
    // Schwerkraft im Ruhezustand (die Gruppe mit rund 1 g bzw. 9,8 m/s^2).
    float va[3] = {0, 0, 0}, vb[3] = {0, 0, 0};
    // Akkustand (Stufe 2, nur lesen): Prozent 0..100 und der Statuswert, wie ihn die Konsole meldet; -1 = unbekannt (aeltere App, nicht lesbar, keine Angabe).
    int battery_pct = -1, battery_state = -1;
};

// Wertet die rohen 8 Bytes aus, die sceHidControlGetBatteryState liefert (Puffer vorher genullt): zwei 32-Bit-Werte, erst der Status, dann der Prozentwert. Das Layout ist auf der
// Konsole nicht bestaetigt (es stammt aus der Programmdatei eines fremden Payloads, die nur angesehen wurde); deshalb gilt nur diese eine Deutung und nur mit Prozent 0..100.
// Lauter Nullen heissen "keine Angabe" (die Konsole hat nichts geschrieben). Rueckgabe false = unbekannt; der Dienst schreibt die Rohbytes ins Protokoll.
bool decode_battery_state(const std::uint8_t* raw, std::size_t n, int& pct, int& state);

class PadTest {
public:
    enum class Stage { Buttons, StickRest, StickCircle, Triggers, Touchpad, Motion, Summary };
    enum class Verdict { Pending, Ok, Warn, Skipped };
    enum class MotionPhase { Settle, Rest, Move };
    struct StageResult {
        Verdict verdict = Verdict::Pending;
        std::string reason;  // bei Warn/Skipped: Kennung (buttons_missing, stick_drift, stick_range, trigger_range, touch_dead_zone, touch_one_finger, motion_none, motion_gravity, motion_weak, not_supported, not_tested)
    };

    static constexpr int kButtons = 16;  // Cross, Kreis, Quadrat, Dreieck, L1, R1, L2, R2, L3, R3, Options, Touchpad-Klick, Oben, Unten, Links, Rechts
    static constexpr int kSectors = 72;  // Rundlauf: je 5 Grad
    static constexpr int kCols = 6, kRows = 3;  // Touchpad-Felder
    static constexpr int kStageCount = 6;

    PadTest() = default;
    void reset() { *this = PadTest(); }

    // Neuer Pad-Zustand; now_ms ist eine laufende Uhr (Millisekunden). Wertet den aktuellen Schritt aus, wechselt ihn aber nie von selbst.
    void feed(const PadSample& s, std::uint32_t now_ms);
    // Der aktuelle Schritt hat alles gesehen, was er braucht (die Oberflaeche zeigt dann "Weiter").
    bool stage_done() const { return done_; }
    // Zum naechsten Schritt: ist der aktuelle nicht fertig, zaehlt, was bis dahin gemessen ist (nichts gemessen = uebersprungen).
    void next();
    // Zum naechsten Schritt, der aktuelle gilt als uebersprungen.
    void skip();

    Stage stage() const { return stage_; }
    int stage_index() const { return static_cast<int>(stage_); }  // 0..5, Summary = 6
    const StageResult& result_of(int stage_idx) const { return results_[static_cast<std::size_t>(stage_idx)]; }
    const PadSample& sample() const { return cur_; }

    // --- Tasten
    bool button_pressed(int i) const { return pressed_[static_cast<std::size_t>(i)]; }
    int buttons_pressed() const;
    // Kreis zweimal hintereinander beendet den Tasten-Schritt vorzeitig (ein einzelner Kreis ist selbst eine zu pruefende Taste).
    bool circle_exit_requested() const { return circle_exit_; }

    // --- Sticks in Ruhe: 0 = links, 1 = rechts
    double measure_progress() const;  // 0..1 des laufenden Messfensters (Sticks in Ruhe, Bewegung in Ruhe)
    int rest_restarts() const { return rest_restarts_; }  // wie oft ein Stick waehrend der Messung angefasst wurde
    bool rest_measured() const { return rest_done_; }
    double rest_offset_pct(int stick) const { return rest_dist_[static_cast<std::size_t>(stick)]; }  // mittlere Abweichung von der Mitte in Prozent des Hubs
    int rest_deadzone_pct(int stick) const;  // vorgeschlagene Totzone (aufgerundet, plus 2)
    static const char* drift_label(double pct);  // "ok", "light", "drift"

    // --- Rundlauf
    bool sector_reached(int stick, int sector) const { return circle_max_[static_cast<std::size_t>(stick)][static_cast<std::size_t>(sector)] >= kReach; }
    int sectors_reached(int stick) const;
    double circle_avg_radius(int stick) const;  // mittlerer weitester Ausschlag der erreichten Sektoren (0..1)

    // --- Trigger: 0 = L2, 1 = R2
    int trigger_max(int t) const { return trig_max_[static_cast<std::size_t>(t)]; }
    bool trigger_released(int t) const { return trig_released_[static_cast<std::size_t>(t)]; }

    // --- Touchpad
    bool cell_touched(int col, int row) const { return cells_[static_cast<std::size_t>(row * kCols + col)]; }
    int cells_touched() const;
    bool saw_two_fingers() const { return saw_two_; }
    bool saw_any_finger() const { return saw_finger_; }

    // --- Bewegung
    MotionPhase motion_phase() const { return mphase_; }
    // Welche Gruppe die Beschleunigung ist: 0 = noch unbekannt oder nicht eindeutig, 1 = va, 2 = vb
    int accel_group() const { return accel_group_; }
    bool motion_accel_moved() const { return accel_moved_; }
    bool motion_gyro_moved() const { return gyro_moved_; }
    double motion_accel_progress() const { return accel_prog_; }  // 0..1: wie weit der Ausschlag schon fuer "bewegt" reicht
    double motion_gyro_progress() const { return gyro_prog_; }

    // Gesamtergebnis fuer den Selbsttest (id "hw.pad.buttons"); started = Zeitstempel "YYYYMMDDTHHMMSSZ" (leer erlaubt).
    TestResult result(const std::string& started) const;

private:
    static constexpr float kReach = 0.80f;  // ein Sektor gilt als erreicht ab 80 % des Hubs

    void enter(Stage s);
    void finish_stage(bool user_skip);
    StageResult evaluate(Stage s) const;
    void feed_buttons(const PadSample& s);
    void feed_rest(const PadSample& s, std::uint32_t now);
    void feed_circle(const PadSample& s);
    void feed_triggers(const PadSample& s);
    void feed_touch(const PadSample& s);
    void feed_motion(const PadSample& s, std::uint32_t now);

    Stage stage_ = Stage::Buttons;
    bool done_ = false;
    PadSample cur_;
    std::array<StageResult, kStageCount> results_;

    std::array<bool, kButtons> pressed_{};
    bool circle_exit_ = false, last_was_circle_ = false;
    std::uint32_t prev_buttons_ = 0, prev_down_ = 0;
    bool buttons_seen_ = false;

    // Sticks in Ruhe
    std::uint32_t stage_t0_ = 0, win_t0_ = 0, last_now_ = 0;
    bool stage_started_ = false, win_started_ = false, rest_done_ = false;
    int rest_restarts_ = 0, rest_n_ = 0;
    std::array<double, 2> rest_sum_x_{}, rest_sum_y_{}, rest_dist_{}, rest_max_{};

    // Rundlauf
    std::array<std::array<float, kSectors>, 2> circle_max_{};

    // Trigger
    std::array<int, 2> trig_max_{};
    std::array<bool, 2> trig_released_{};

    // Touchpad
    std::array<bool, kCols * kRows> cells_{};
    bool saw_two_ = false, saw_finger_ = false;

    // Bewegung
    MotionPhase mphase_ = MotionPhase::Settle;
    int accel_group_ = 0;
    int motion_n_ = 0;
    std::array<double, 3> msum_a_{}, msum_b_{}, mmin_a_{}, mmax_a_{}, mmin_b_{}, mmax_b_{};
    std::array<double, 3> mean_a_{}, mean_b_{};
    double noise_a_ = 0, noise_b_ = 0, peak_a_ = 0, peak_b_ = 0, accel_prog_ = 0, gyro_prog_ = 0;
    bool accel_moved_ = false, gyro_moved_ = false, motion_zero_ = false;
    bool any_extra_ = false;  // irgendwann kamen Trigger, Touchpad und Bewegung mit (die App ist neu genug)
};

}  // namespace medic
