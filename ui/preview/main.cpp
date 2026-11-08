// SPDX-License-Identifier: GPL-3.0-or-later
// medic-ui-preview <snapshot-ordner> <ausgabe-ordner> [--lang de|en] [--rules datei] [--scale N]
// Spielt einen festen Tastenablauf gegen einen Snapshot ab und schreibt jeden Bildschirm als PNG.
// Die Plattform ist nur lesend: Reparaturen werden angezeigt, aber nie ausgefuehrt.
#include "medic_ui/app.hpp"
#include "medic_ui/png.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifndef MEDIC_DEFAULT_RULES
#define MEDIC_DEFAULT_RULES "knowledge/rules.json"
#endif

using namespace medic::ui;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Aufruf: medic-ui-preview <snapshot-ordner> <ausgabe-ordner> [--lang de|en] [--rules datei] [--scale N]\n";
        return 2;
    }
    std::string rules_path = MEDIC_DEFAULT_RULES;
    Lang lang = Lang::De;
    int scale = 1;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--lang" && i + 1 < argc) lang = std::string(argv[++i]) == "en" ? Lang::En : Lang::De;
        else if (a == "--rules" && i + 1 < argc) rules_path = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
        else {
            std::cerr << "Unbekannte Option: " << a << "\n";
            return 2;
        }
    }
    try {
        auto platform = medic::make_snapshot_platform(argv[1]);
        LocalService svc(*platform, medic::load_rules_from_file(rules_path));
        App app(svc, lang);
        Canvas canvas(App::kWidth, App::kHeight);
        const std::filesystem::path out = argv[2];
        std::filesystem::create_directories(out);
        int shots = 0;
        auto shot = [&](const std::string& name) {
            app.render(canvas);
            if (!write_png((out / (name + ".png")).string(), canvas, scale)) throw std::runtime_error("PNG nicht schreibbar: " + name);
            ++shots;
        };

        shot("01_start");
        app.handle(Button::Cross);  // Check-up starten
        shot("02_running");
        app.update();
        shot("03_findings");
        app.handle(Button::Square);  // Bericht (die Vorschau-Plattform ist nur lesend: nichts wird gespeichert, den QR-Code gibt es trotzdem)
        shot("03b_report");
        app.handle(Button::Circle);
        const int n = static_cast<int>(app.finding_count());
        for (int i = 0; i < std::min(n, 3); ++i) {
            app.handle(Button::Cross);
            shot("04_detail_" + std::to_string(i));
            app.handle(Button::Triangle);
            if (app.screen() == App::Screen::FixPreview) {
                shot("05_fix_" + std::to_string(i));
                app.handle(Button::Circle);
            }
            app.handle(Button::Circle);
            app.handle(Button::Down);
        }
        app.handle(Button::Circle);  // Start
        app.handle(Button::Down);
        app.handle(Button::Cross);  // Verlauf
        shot("06_history");
        app.handle(Button::Circle);
        app.handle(Button::Down);
        app.handle(Button::Cross);  // Einstellungen
        shot("07_settings");
        app.handle(Button::Down);
        shot("07b_settings_rules_update");
        app.handle(Button::Cross);  // Regel-Update (zeigt das Angebot, installiert nichts)
        shot("08_rules_update");
        std::cout << shots << " Bilder in " << out.string() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Fehler: " << e.what() << "\n";
        return 2;
    }
    return 0;
}
