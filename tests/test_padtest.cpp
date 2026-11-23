// SPDX-License-Identifier: GPL-3.0-or-later
// Tests fuer den Controller-Test (libmedic/src/padtest.cpp): erfundene Pad-Verlaeufe gegen die Auswertung von Tasten, Sticks, Triggern, Touchpad und Bewegung.
#include "check.hpp"
#include "medic/padtest.hpp"

#include <cmath>
#include <string>

using namespace medic;

namespace {

struct Drive {
    PadTest t;
    std::uint32_t now = 1000;
    void feed(const PadSample& s, int ms = 16) {
        t.feed(s, now);
        now += static_cast<std::uint32_t>(ms);
    }
    void hold(const PadSample& s, int total_ms) {
        for (int e = 0; e < total_ms; e += 16) feed(s);
    }
};

PadSample neutral(bool extra = true) {
    PadSample s;
    s.extra = extra;
    return s;
}

PadSample pressed(std::uint32_t bits) {
    PadSample s = neutral();
    s.buttons = bits;
    return s;
}

PadSample sticks(int lx, int ly, int rx, int ry) {
    PadSample s = neutral();
    s.lx = static_cast<std::uint8_t>(lx);
    s.ly = static_cast<std::uint8_t>(ly);
    s.rx = static_cast<std::uint8_t>(rx);
    s.ry = static_cast<std::uint8_t>(ry);
    return s;
}

// Alle Schritte bis zu `to` ueberspringen
void skip_to(Drive& d, PadTest::Stage to) {
    while (d.t.stage() != to) d.t.skip();
}

}  // namespace

void padtest_tests() {
    const std::uint32_t all_bits[PadTest::kButtons] = {kPadCross, kPadCircle, kPadSquare, kPadTriangle, kPadL1,   kPadR1,   kPadL2,   kPadR2,
                                                       kPadL3,    kPadR3,     kPadOptions, kPadTouch,   kPadUp,   kPadDown, kPadLeft, kPadRight};
    {  // Tasten: jede einzeln, dann fertig
        Drive d;
        CHECK(d.t.stage() == PadTest::Stage::Buttons && !d.t.stage_done(), "Padtest: beginnt bei den Tasten");
        d.feed(pressed(kPadCross));  // die Taste, die den Test gestartet hat, ist beim ersten Zustand noch gedrueckt: zaehlt nicht
        CHECK(!d.t.button_pressed(0), "Padtest: beim Start gehaltene Taste zaehlt nicht");
        d.feed(neutral());
        for (int i = 0; i < PadTest::kButtons; ++i) {
            CHECK(!d.t.button_pressed(i), "Padtest: Taste noch nicht gesehen");
            d.feed(pressed(all_bits[i]));
            d.feed(neutral());
            CHECK(d.t.button_pressed(i), "Padtest: Taste gesehen");
        }
        CHECK(d.t.buttons_pressed() == 16 && d.t.stage_done(), "Padtest: alle 16 Tasten = Schritt fertig");
        d.t.next();
        CHECK(d.t.result_of(0).verdict == PadTest::Verdict::Ok && d.t.stage() == PadTest::Stage::StickRest, "Padtest: Tasten ok, weiter zu den Sticks");
    }
    {  // L2/R2 zaehlen auch ohne digitalen Bit, wenn der Trigger analog durchgedrueckt wird
        Drive d;
        d.feed(neutral());
        PadSample s = neutral();
        s.l2 = 255;
        s.r2 = 230;
        d.feed(s);
        CHECK(d.t.button_pressed(6) && d.t.button_pressed(7), "Padtest: L2/R2 aus dem Analogwert");
        Drive e;
        PadSample old = neutral(false);  // aeltere App: kein Analogwert
        old.l2 = 255;
        e.feed(old);
        CHECK(!e.t.button_pressed(6), "Padtest: ohne extra kein Analogwert");
    }
    {  // Kreis zweimal hintereinander = vorzeitig beenden; mit einer Taste dazwischen nicht
        Drive d;
        d.feed(neutral());
        d.feed(pressed(kPadCircle));
        d.feed(neutral());
        CHECK(!d.t.circle_exit_requested() && d.t.button_pressed(1), "Padtest: ein Kreis ist eine Taste, kein Ausstieg");
        d.feed(pressed(kPadCross));
        d.feed(neutral());
        d.feed(pressed(kPadCircle));
        d.feed(neutral());
        CHECK(!d.t.circle_exit_requested(), "Padtest: Kreis, Kreuz, Kreis beendet nicht");
        d.feed(pressed(kPadCircle));
        d.feed(neutral());
        CHECK(d.t.circle_exit_requested(), "Padtest: Kreis zweimal hintereinander beendet");
        d.t.next();
        CHECK(d.t.result_of(0).verdict == PadTest::Verdict::Warn && d.t.result_of(0).reason == "buttons_missing", "Padtest: fehlende Tasten = Warnung");
    }
    {  // Tasten: nichts gedrueckt = uebersprungen
        Drive d;
        d.feed(neutral());
        d.t.next();
        CHECK(d.t.result_of(0).verdict == PadTest::Verdict::Skipped && d.t.result_of(0).reason == "not_tested", "Padtest: keine Taste = nicht getestet");
    }
    {  // Sticks in Ruhe: gesunde Sticks
        Drive d;
        skip_to(d, PadTest::Stage::StickRest);
        d.hold(sticks(129, 127, 128, 129), 5000);
        CHECK(d.t.stage_done() && d.t.rest_measured(), "Padtest: Ruhemessung nach rund 4 Sekunden fertig");
        CHECK(d.t.rest_offset_pct(0) < 2.0 && d.t.rest_offset_pct(1) < 2.0, "Padtest: kleiner Abstand von der Mitte");
        CHECK(std::string(PadTest::drift_label(d.t.rest_offset_pct(0))) == "ok", "Padtest: Beschriftung ok");
        d.t.next();
        CHECK(d.t.result_of(1).verdict == PadTest::Verdict::Ok, "Padtest: Ruhe ok");
    }
    {  // Drift am rechten Stick (rund 9 %)
        Drive d;
        skip_to(d, PadTest::Stage::StickRest);
        d.hold(sticks(128, 128, 140, 128), 5000);
        CHECK(d.t.rest_offset_pct(1) > 9.0 && d.t.rest_offset_pct(1) < 10.0, "Padtest: Drift gemessen (12/127)");
        CHECK(std::string(PadTest::drift_label(d.t.rest_offset_pct(1))) == "light" && std::string(PadTest::drift_label(12.0)) == "drift", "Padtest: leichter Drift unter 10 %, Drift ab 10 %");
        CHECK(d.t.rest_deadzone_pct(1) == 12, "Padtest: vorgeschlagene Totzone = aufgerundet + 2");
        d.t.next();
        CHECK(d.t.result_of(1).verdict == PadTest::Verdict::Warn && d.t.result_of(1).reason == "stick_drift", "Padtest: Drift = Warnung");
    }
    {  // Stick wird waehrend der Messung angefasst: von vorn
        Drive d;
        skip_to(d, PadTest::Stage::StickRest);
        d.hold(sticks(128, 128, 128, 128), 2500);
        d.hold(sticks(220, 128, 128, 128), 160);
        CHECK(d.t.rest_restarts() >= 1 && !d.t.stage_done(), "Padtest: angefasst = Messung beginnt neu");
        d.hold(sticks(128, 128, 128, 128), 5000);
        CHECK(d.t.stage_done() && d.t.rest_offset_pct(0) < 1.0, "Padtest: danach gilt die ruhige Messung");
    }
    {  // Sticks in Ruhe uebersprungen
        Drive d;
        skip_to(d, PadTest::Stage::StickRest);
        d.t.next();
        CHECK(d.t.result_of(1).verdict == PadTest::Verdict::Skipped, "Padtest: Ruhe ohne Messung = nicht getestet");
    }
    {  // Rundlauf: beide Sticks einmal ganz herum
        Drive d;
        skip_to(d, PadTest::Stage::StickCircle);
        for (int deg = 0; deg < 360; deg += 2) {
            const double a = deg * 3.14159265358979 / 180.0;
            const int x = static_cast<int>(std::lround(128 + 127 * std::cos(a))), y = static_cast<int>(std::lround(128 + 127 * std::sin(a)));
            d.feed(sticks(x, y, x, y));
        }
        CHECK(d.t.sectors_reached(0) == 72 && d.t.sectors_reached(1) == 72, "Padtest: alle 72 Sektoren erreicht");
        CHECK(d.t.stage_done() && d.t.circle_avg_radius(0) > 0.95, "Padtest: Rundlauf fertig, voller Ausschlag");
        d.t.next();
        CHECK(d.t.result_of(2).verdict == PadTest::Verdict::Ok, "Padtest: Rundlauf ok");
    }
    {  // Rundlauf nur halb (linker Stick), rechter bleibt in der Mitte
        Drive d;
        skip_to(d, PadTest::Stage::StickCircle);
        for (int deg = 0; deg < 180; deg += 2) {
            const double a = deg * 3.14159265358979 / 180.0;
            d.feed(sticks(static_cast<int>(std::lround(128 + 127 * std::cos(a))), static_cast<int>(std::lround(128 + 127 * std::sin(a))), 128, 128));
        }
        CHECK(d.t.sectors_reached(0) >= 34 && d.t.sectors_reached(0) <= 38 && d.t.sectors_reached(1) == 0 && !d.t.stage_done(), "Padtest: halber Rundlauf, Schritt nicht fertig");
        d.t.next();
        CHECK(d.t.result_of(2).verdict == PadTest::Verdict::Warn && d.t.result_of(2).reason == "stick_range", "Padtest: unvollstaendiger Rundlauf = Warnung");
    }
    {  // Rundlauf mit schwachem Ausschlag (nur 70 %): kein Sektor gilt als erreicht
        Drive d;
        skip_to(d, PadTest::Stage::StickCircle);
        for (int deg = 0; deg < 360; deg += 2) {
            const double a = deg * 3.14159265358979 / 180.0;
            d.feed(sticks(static_cast<int>(std::lround(128 + 89 * std::cos(a))), static_cast<int>(std::lround(128 + 89 * std::sin(a))), 128, 128));
        }
        CHECK(d.t.sectors_reached(0) == 0, "Padtest: 70 % Ausschlag reicht nicht fuer einen Sektor");
    }
    {  // Trigger: voll durch und wieder los
        Drive d;
        skip_to(d, PadTest::Stage::Triggers);
        for (int v = 0; v <= 255; v += 5) {
            PadSample s = neutral();
            s.l2 = s.r2 = static_cast<std::uint8_t>(v);
            d.feed(s);
        }
        PadSample full = neutral();
        full.l2 = full.r2 = 255;
        d.feed(full);
        CHECK(!d.t.stage_done() && d.t.trigger_max(0) == 255, "Padtest: Trigger voll, noch nicht losgelassen");
        d.feed(neutral());
        CHECK(d.t.stage_done() && d.t.trigger_released(0) && d.t.trigger_released(1), "Padtest: losgelassen = fertig");
        d.t.next();
        CHECK(d.t.result_of(3).verdict == PadTest::Verdict::Ok, "Padtest: Trigger ok");
    }
    {  // Trigger kommt nur bis 200
        Drive d;
        skip_to(d, PadTest::Stage::Triggers);
        PadSample s = neutral();
        s.l2 = 200;
        s.r2 = 255;
        d.feed(s);
        d.feed(neutral());
        d.t.next();
        CHECK(d.t.result_of(3).verdict == PadTest::Verdict::Warn && d.t.result_of(3).reason == "trigger_range", "Padtest: Trigger erreicht den Endwert nicht = Warnung");
    }
    {  // Trigger ohne Zusatzdaten (aeltere App)
        Drive d;
        skip_to(d, PadTest::Stage::Triggers);
        d.feed(neutral(false));
        d.t.next();
        CHECK(d.t.result_of(3).verdict == PadTest::Verdict::Skipped && d.t.result_of(3).reason == "not_supported", "Padtest: ohne Zusatzdaten nicht pruefbar");
    }
    {  // Touchpad: alle Felder, zwei Finger
        Drive d;
        skip_to(d, PadTest::Stage::Touchpad);
        for (int r = 0; r < PadTest::kRows; ++r)
            for (int c = 0; c < PadTest::kCols; ++c) {
                PadSample s = neutral();
                s.touch_count = 1;
                s.finger[0].x = c * 320 + 160;
                s.finger[0].y = r * 360 + 180;
                d.feed(s);
            }
        CHECK(d.t.cells_touched() == 18 && !d.t.stage_done(), "Padtest: alle 18 Felder, aber noch nicht zwei Finger");
        PadSample two = neutral();
        two.touch_count = 2;
        two.finger[0].x = 300;
        two.finger[0].y = 300;
        two.finger[1].x = 1500;
        two.finger[1].y = 700;
        d.feed(two);
        CHECK(d.t.saw_two_fingers() && d.t.stage_done(), "Padtest: zwei Finger = fertig");
        d.t.next();
        CHECK(d.t.result_of(4).verdict == PadTest::Verdict::Ok, "Padtest: Touchpad ok");
    }
    {  // Touchpad: Feld unten rechts fehlt
        Drive d;
        skip_to(d, PadTest::Stage::Touchpad);
        for (int r = 0; r < PadTest::kRows; ++r)
            for (int c = 0; c < PadTest::kCols; ++c) {
                if (r == 2 && c == 5) continue;
                PadSample s = neutral();
                s.touch_count = 1;
                s.finger[0].x = c * 320 + 160;
                s.finger[0].y = r * 300 + 150;  // das DualSense meldet in y weniger als 1080: unterste Reihe ab y = 720
                if (r == 2) s.finger[0].y = 900;
                d.feed(s);
            }
        d.t.next();
        CHECK(d.t.cells_touched() == 17 && d.t.result_of(4).verdict == PadTest::Verdict::Warn && d.t.result_of(4).reason == "touch_dead_zone", "Padtest: totes Feld = Warnung");
    }
    {  // Touchpad: nie angefasst
        Drive d;
        skip_to(d, PadTest::Stage::Touchpad);
        d.feed(neutral());
        d.t.next();
        CHECK(d.t.result_of(4).verdict == PadTest::Verdict::Skipped, "Padtest: Touchpad nicht angefasst = nicht getestet");
    }
    {  // Bewegung: va = Gyro (ruhig, rauscht kaum), vb = Beschleunigung in m/s^2
        Drive d;
        skip_to(d, PadTest::Stage::Motion);
        PadSample rest = neutral();
        rest.vb[2] = 9.8f;
        d.hold(rest, 4000);
        CHECK(d.t.motion_phase() == PadTest::MotionPhase::Move && d.t.accel_group() == 2, "Padtest: Beschleunigung an der Schwerkraft erkannt (zweite Gruppe)");
        CHECK(!d.t.stage_done(), "Padtest: ruhig = noch nicht fertig");
        PadSample move = rest;
        move.va[0] = 2.5f;  // Drehung
        move.vb[0] = 5.0f;  // Schuetteln
        d.feed(move);
        CHECK(d.t.motion_gyro_moved() && d.t.motion_accel_moved() && d.t.stage_done(), "Padtest: Bewegung erkannt");
        d.t.next();
        CHECK(d.t.result_of(5).verdict == PadTest::Verdict::Ok, "Padtest: Bewegung ok");
    }
    {  // Bewegung: die erste Gruppe ist die Beschleunigung, in g
        Drive d;
        skip_to(d, PadTest::Stage::Motion);
        PadSample rest = neutral();
        rest.va[1] = 1.0f;
        d.hold(rest, 4000);
        CHECK(d.t.accel_group() == 1, "Padtest: Beschleunigung in g, erste Gruppe");
        PadSample move = rest;
        move.va[1] = 0.2f;
        move.vb[2] = 3.0f;
        d.feed(move);
        CHECK(d.t.stage_done(), "Padtest: auch hier fertig");
    }
    {  // Bewegung: nur gedreht, nicht bewegt
        Drive d;
        skip_to(d, PadTest::Stage::Motion);
        PadSample rest = neutral();
        rest.vb[2] = 9.8f;
        d.hold(rest, 4000);
        PadSample move = rest;
        move.va[2] = 3.0f;
        d.feed(move);
        d.t.next();
        CHECK(d.t.result_of(5).verdict == PadTest::Verdict::Warn && d.t.result_of(5).reason == "motion_weak", "Padtest: nur eine Bewegungsart = Warnung");
    }
    {  // Bewegung: nur Nullen
        Drive d;
        skip_to(d, PadTest::Stage::Motion);
        d.hold(neutral(), 4000);
        d.t.next();
        CHECK(d.t.result_of(5).verdict == PadTest::Verdict::Warn && d.t.result_of(5).reason == "motion_none", "Padtest: alles null = keine Bewegungsdaten");
    }
    {  // Bewegung: keine Schwerkraft in einer der Gruppen
        Drive d;
        skip_to(d, PadTest::Stage::Motion);
        PadSample rest = neutral();
        rest.va[0] = 3.3f;
        rest.vb[1] = 4.4f;
        d.hold(rest, 4000);
        d.t.next();
        CHECK(d.t.result_of(5).verdict == PadTest::Verdict::Warn && d.t.result_of(5).reason == "motion_gravity", "Padtest: keine Schwerkraft = Warnung");
    }
    {  // Bewegung vor Ablauf der Ruhezeit beendet = nicht getestet
        Drive d;
        skip_to(d, PadTest::Stage::Motion);
        PadSample rest = neutral();
        rest.vb[2] = 9.8f;
        d.hold(rest, 500);
        d.t.next();
        CHECK(d.t.result_of(5).verdict == PadTest::Verdict::Skipped, "Padtest: Bewegung zu frueh beendet = nicht getestet");
    }
    {  // Gesamtergebnis
        Drive d;
        for (int i = 0; i < PadTest::kStageCount; ++i) d.t.skip();
        CHECK(d.t.stage() == PadTest::Stage::Summary, "Padtest: nach sechs Schritten die Zusammenfassung");
        TestResult none = d.t.result("20261120T100000Z");
        CHECK(none.id == "hw.pad.buttons" && none.status == TestStatus::Skipped && none.reason == "not_tested" && none.started == "20261120T100000Z", "Padtest: nichts getestet = uebersprungen");
        d.t.skip();
        CHECK(d.t.stage() == PadTest::Stage::Summary, "Padtest: in der Zusammenfassung bleibt es dort");

        Drive e;
        e.feed(neutral());
        for (int i = 0; i < PadTest::kButtons; ++i) {
            e.feed(pressed(all_bits[i]));
            e.feed(neutral());
        }
        e.t.next();  // Tasten ok
        for (int i = 1; i < PadTest::kStageCount; ++i) e.t.skip();
        TestResult part = e.t.result("");
        CHECK(part.status == TestStatus::Passed && part.reason == "partial", "Padtest: nur Tasten getestet und ok = bestanden, teilweise");

        Drive f;
        f.feed(neutral());
        for (int i = 0; i < PadTest::kButtons; ++i) {
            f.feed(pressed(all_bits[i]));
            f.feed(neutral());
        }
        f.t.next();
        f.hold(sticks(128, 128, 150, 128), 5000);
        f.t.next();  // Drift rechts: Warnung
        for (int i = 2; i < PadTest::kStageCount; ++i) f.t.skip();
        TestResult w = f.t.result("");
        CHECK(w.status == TestStatus::Warn && w.reason == "stick_drift", "Padtest: eine Warnung genuegt");
        bool has_rest = false, has_buttons = false;
        for (const auto& m : w.measurements) {
            if (m.name == "stick_rest_right_pct" && m.value > 17.0 && m.value < 18.0) has_rest = true;
            if (m.name == "buttons_pressed" && m.value == 16) has_buttons = true;
        }
        CHECK(has_rest && has_buttons, "Padtest: Messwerte stehen im Ergebnis");
    }
    {  // reset
        Drive d;
        d.feed(pressed(kPadCross));
        d.t.reset();
        CHECK(d.t.buttons_pressed() == 0 && d.t.stage() == PadTest::Stage::Buttons, "Padtest: reset setzt alles zurueck");
    }
}
