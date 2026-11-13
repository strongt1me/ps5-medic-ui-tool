// SPDX-License-Identifier: GPL-3.0-or-later
// Richtungseingabe: macht aus Steuerkreuz und linkem Stick Tastendruecke (Up/Down/Left/Right) mit Wiederholung.
// Plattformunabhaengig (Zeit und Achsenwerte kommen von aussen), damit sich das Verhalten am PC testen laesst.
//
// Regeln:
//  - Der Stick zaehlt erst ab kOn (rund die Haelfte des Ausschlags) und faellt erst unter kOff zurueck (Hysterese),
//    damit ein leicht driftender Stick nichts ausloest. Es gilt nur die staerkere Achse (keine Diagonalen).
//  - Das Steuerkreuz hat Vorrang vor dem Stick; bei mehreren gedrueckten Richtungen gilt die zuletzt gedrueckte.
//  - Eine Richtung loest sofort einmal aus. Halten wiederholt nur Hoch/Runter (Listen scrollen); Links/Rechts
//    bleiben Einzelschritte, weil sie in den Einstellungen die Sprache umschalten.
//  - Der Stick loest nur Richtungstasten aus, nie Kreuz oder Kreis: Bestaetigen bleibt allein dem Kreuz vorbehalten.
#pragma once
#include "medic_ui/input.hpp"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <vector>

namespace medic::ui {

class PadNav {
public:
    static constexpr int kOn = 16000;   // von 32767: ab hier gilt der Stick als gedrueckt
    static constexpr int kOff = 9000;   // darunter gilt er wieder als losgelassen
    static constexpr std::uint32_t kFirstDelayMs = 300;  // bis zur ersten Wiederholung
    static constexpr std::uint32_t kRepeatMs = 70;       // danach alle 70 ms

    // Steuerkreuz-Taste gedrueckt (down = true) bzw. losgelassen. Andere Tasten ignoriert die Funktion.
    void dpad(Button b, bool down, std::uint32_t now_ms, std::vector<Button>& out) {
        const int bit = bit_of(b);
        if (bit == 0) return;
        if (down) {
            dpad_mask_ |= bit;
            dpad_dir_ = b;
        } else {
            dpad_mask_ &= ~bit;
            if (dpad_dir_ && *dpad_dir_ == b) dpad_dir_ = any_in_mask();
        }
        // Beim Loslassen kein Extra-Schritt: haelt der Stick noch, laeuft seine Richtung still weiter.
        refresh(now_ms, out, /*emit=*/down);
    }

    // Neue Werte des linken Sticks (SDL: -32768..32767, Y positiv = unten).
    void stick(int x, int y, std::uint32_t now_ms, std::vector<Button>& out) {
        const int ax = std::abs(x), ay = std::abs(y);
        const int threshold = stick_dir_ ? kOff : kOn;
        if (ax < threshold && ay < threshold) {
            stick_dir_.reset();
        } else if (ax >= ay) {
            stick_dir_ = x < 0 ? Button::Left : Button::Right;
        } else {
            stick_dir_ = y < 0 ? Button::Up : Button::Down;
        }
        refresh(now_ms, out);
    }

    // Einmal pro Bild aufrufen: liefert die Wiederholungen einer gehaltenen Richtung.
    void poll(std::uint32_t now_ms, std::vector<Button>& out) {
        if (!active_ || !repeats(*active_)) return;
        if (static_cast<std::int32_t>(now_ms - next_ms_) < 0) return;
        out.push_back(*active_);
        next_ms_ = now_ms + kRepeatMs;
    }

    // Controller weg oder neu verbunden: nichts mehr gedrueckt.
    void reset() {
        dpad_mask_ = 0;
        dpad_dir_.reset();
        stick_dir_.reset();
        active_.reset();
    }

private:
    static int bit_of(Button b) {
        switch (b) {
            case Button::Up: return 1;
            case Button::Down: return 2;
            case Button::Left: return 4;
            case Button::Right: return 8;
            default: return 0;
        }
    }
    static bool repeats(Button b) { return b == Button::Up || b == Button::Down; }

    std::optional<Button> any_in_mask() const {
        for (Button b : {Button::Up, Button::Down, Button::Left, Button::Right})
            if (dpad_mask_ & bit_of(b)) return b;
        return std::nullopt;
    }

    // Aktive Richtung neu bestimmen; eine neue Richtung loest sofort aus (emit) und startet den Wiederholungs-Zeitgeber.
    void refresh(std::uint32_t now_ms, std::vector<Button>& out, bool emit = true) {
        const std::optional<Button> dir = dpad_dir_ ? dpad_dir_ : stick_dir_;
        if (dir == active_) return;
        active_ = dir;
        if (!dir) return;
        if (emit) out.push_back(*dir);
        next_ms_ = now_ms + kFirstDelayMs;
    }

    int dpad_mask_ = 0;
    std::optional<Button> dpad_dir_, stick_dir_, active_;
    std::uint32_t next_ms_ = 0;
};

}  // namespace medic::ui
