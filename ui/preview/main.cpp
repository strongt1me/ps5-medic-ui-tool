// SPDX-License-Identifier: GPL-3.0-or-later
// medic-ui-preview <snapshot-ordner> <ausgabe-ordner> [--lang de|en] [--rules datei] [--scale N]
// Spielt einen festen Tastenablauf gegen einen Snapshot ab und schreibt jeden Bildschirm als PNG.
// Die Plattform ist nur lesend: Reparaturen werden angezeigt, aber nie ausgefuehrt. Die Fehlercodes kommen aus knowledge/error_codes.json.
#include "medic_ui/app.hpp"
#include "medic_ui/icons.hpp"
#include "medic_ui/png.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifndef MEDIC_DEFAULT_RULES
#define MEDIC_DEFAULT_RULES "knowledge/rules.json"
#endif
#ifndef MEDIC_DEFAULT_ERROR_CODES
#define MEDIC_DEFAULT_ERROR_CODES "knowledge/error_codes.json"
#endif

using namespace medic::ui;

namespace {
// Symbolbogen: alle Symbole in Hauptfarbe/Akzent und als Farbfeld, zur Sichtpruefung.
int write_icon_sheet(const std::string& path) {
    Canvas c(1920, 1080);
    c.clear(rgb(14, 18, 24));
    const Color tints[] = {rgb(76, 154, 255), rgb(167, 139, 250), rgb(45, 212, 191), rgb(251, 146, 60), rgb(255, 92, 92), rgb(76, 208, 138)};
    const int n = static_cast<int>(Icon::Shield) + 1;
    for (int i = 0; i < n; ++i) {
        const int col = i % 8, row = i / 8, x = 40 + col * 232, y = 20 + row * 260;
        c.fill_round_rect(x, y, 212, 244, 16, rgb(23, 29, 38));
        draw_icon(c, static_cast<Icon>(i), x + 106, y + 72, 100, rgb(232, 237, 242), rgb(76, 154, 255));
        draw_icon_badge(c, static_cast<Icon>(i), x + 20, y + 144, 80, tints[i % 6]);
        draw_icon(c, static_cast<Icon>(i), x + 160, y + 184, 36, rgb(141, 153, 168));
    }
    return write_png(path, c, 1) ? 0 : 2;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--icons") return write_icon_sheet(argv[2]);
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
        svc.set_error_codes(medic::load_error_codes_from_file(MEDIC_DEFAULT_ERROR_CODES));
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
        app.handle(Button::Cross);  // Check-up-Verlauf
        shot("06a_checklist");
        app.handle(Button::Down);
        app.handle(Button::Cross);  // einen alten Check-up ansehen
        shot("06c_stored_check");
        app.handle(Button::Circle);
        app.handle(Button::Triangle);  // mit dem davor vergleichen
        shot("06d_compare");
        app.handle(Button::Circle);
        app.handle(Button::Square);  // Reparaturen
        shot("06_history");
        app.handle(Button::Circle);  // zurueck zur Liste
        app.handle(Button::Circle);
        app.handle(Button::Down);
        app.handle(Button::Cross);  // Einstellungen
        shot("07_settings");
        app.handle(Button::Down);
        shot("07b_settings_rules_update");
        app.handle(Button::Cross);  // Regel-Update (zeigt das Angebot, installiert nichts)
        shot("08_rules_update");
        app.handle(Button::Circle);  // Einstellungen
        app.handle(Button::Circle);  // Start
        app.handle(Button::Down);
        app.handle(Button::Down);
        app.handle(Button::Down);
        shot("09_start_error_codes");
        app.handle(Button::Cross);  // Fehlercodes (nur Text zum Lesen)
        shot("10_error_codes");
        app.handle(Button::Down);
        app.handle(Button::Cross);
        // Zu einem Code blaettern: erst vorwaerts, sonst rueckwaerts (die Liste ist nach id sortiert).
        auto goto_code = [&](const std::string& id) {
            for (int i = 0; i < 80 && app.selected_error_id() != id; ++i) app.handle(Button::Right);
            for (int i = 0; i < 80 && app.selected_error_id() != id; ++i) app.handle(Button::Left);
        };
        shot("11_error_code_detail");
        goto_code("CE-108262-9");  // der laengste Eintrag (scrollt)
        shot("12_error_code_long_top");
        for (int i = 0; i < 40; ++i) app.handle(Button::Down);
        shot("13_error_code_long_end");
        app.handle(Button::Square);  // Quelle als QR-Code
        shot("13b_error_code_source_qr");
        app.handle(Button::Circle);
        goto_code("CE-105733-9");  // ohne Sony-Seite, nur Community-Angabe
        shot("14_error_code_community");
        goto_code("CE-107520-5");  // Spiel braucht Backport oder AMPR Emu, kein Systemupdate; Firmware und betroffene Titel aus dem Check-up
        shot("15_error_code_game_backport");
        for (int i = 0; i < 12; ++i) app.handle(Button::Down);
        shot("15b_error_code_game_backport_live");
        goto_code("CE-100028-1");  // Speicher: freier Platz aus dem Check-up
        app.handle(Button::Triangle);  // fuer den Bericht vermerken
        shot("16_error_code_storage_live");
        {  // Selbsttest: Liste, Hinweis, Lauf der Schnelltests, Ergebnis (frische Anwendung, die Vorschau-Plattform ist nur lesend)
            App st(svc, lang);
            auto png = [&](const char* name) {
                st.render(canvas);
                if (!write_png((out / name).string(), canvas, scale)) throw std::runtime_error(std::string("PNG nicht schreibbar: ") + name);
                ++shots;
            };
            for (int i = 0; i < 4; ++i) st.handle(Button::Down);
            st.handle(Button::Cross);
            png("17_selftest_list.png");
            st.handle(Button::Cross);
            png("18_selftest_confirm.png");
            st.handle(Button::Circle);
            st.handle(Button::Triangle);  // Gruendlich: Hinweis (Last und Schreiben)
            png("18b_selftest_confirm_thorough.png");
            st.handle(Button::Circle);
            st.handle(Button::Square);  // nur Schnelltests
            for (int i = 0; i < 12; ++i) {
                st.render(canvas);
                st.update();
            }
            png("19_selftest_done.png");
        }
        std::cout << shots << " Bilder in " << out.string() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Fehler: " << e.what() << "\n";
        return 2;
    }
    return 0;
}
