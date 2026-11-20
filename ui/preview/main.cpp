// SPDX-License-Identifier: GPL-3.0-or-later
// medic-ui-preview <snapshot-ordner> <ausgabe-ordner> [--lang de|en|fr|it|es] [--rules datei] [--scale N]
// Spielt einen festen Tastenablauf gegen einen Snapshot ab und schreibt jeden Bildschirm als PNG.
// Die Plattform ist nur lesend: Reparaturen werden angezeigt, aber nie ausgefuehrt. Die Fehlercodes kommen aus knowledge/error_codes.json.
#include "medic_ui/app.hpp"
#include "medic_ui/splash.hpp"
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
        std::cerr << "Aufruf: medic-ui-preview <snapshot-ordner> <ausgabe-ordner> [--lang de|en|fr|it|es] [--rules datei] [--scale N]\n";
        return 2;
    }
    std::string rules_path = MEDIC_DEFAULT_RULES;
    Lang lang = Lang::De;
    int scale = 1;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--lang" && i + 1 < argc) {
            if (!lang_from_code(argv[++i], lang)) {
                std::cerr << "Unbekannte Sprache (de|en|fr|it|es)\n";
                return 2;
            }
        } else if (a == "--rules" && i + 1 < argc) rules_path = argv[++i];
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
        Canvas backdrop(App::kWidth, App::kHeight);
        const bool has_backdrop = load_background(backdrop);
        if (has_backdrop) app.set_background(backdrop);
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
        int frames = 0;  // Zwischenbilder des Fortschritts (jeder 6. Schritt)
        app.set_redraw_hook([&] {
            if (frames++ % 6 == 5) shot("02_running_" + std::to_string(frames));
        });
        app.update();
        app.set_redraw_hook(nullptr);
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
        app.handle(Button::Circle);  // Start: das Statusfeld zeigt jetzt das Ergebnis
        shot("01b_start_result");
        app.handle(Button::Down);
        app.handle(Button::Down);
        app.handle(Button::Cross);  // Check-up-Verlauf (Kachel 2)
        shot("06a_checklist");
        app.handle(Button::Cross);  // den neuesten gespeicherten Check-up ansehen (die Liste ist zugeklappt, die erste Zeile ist ausgewaehlt)
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
        app.handle(Button::Cross);  // Mehr (Kachel 3)
        for (int i = 0; i < 6; ++i) app.handle(Button::Down);
        app.handle(Button::Cross);  // Einstellungen (siebter Eintrag)
        shot("07_settings");
        for (int i = 0; i < 7; ++i) app.handle(Button::Down);  // Zeile "Regel-Quelle": die Hilfe zeigt die Adresse
        shot("07b_settings_source");
        app.handle(Button::Circle);  // Mehr (Kreuz in den Einstellungen wuerde etwas umstellen)
        for (int i = 0; i < 4; ++i) app.handle(Button::Up);  // von "Einstellungen" (7.) zu "Regeln aktualisieren" (3.)
        app.handle(Button::Cross);  // Regel-Update (zeigt das Angebot, installiert nichts)
        shot("08_rules_update");
        app.handle(Button::Circle);  // Mehr
        app.handle(Button::Circle);  // Start
        app.handle(Button::Cross);   // Mehr (die Auswahl steht noch auf Kachel 3)
        for (int i = 0; i < 5; ++i) app.handle(Button::Down);
        shot("09_more_error_codes");
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
            if (has_backdrop) st.set_background(backdrop);
            auto png = [&](const char* name) {
                st.render(canvas);
                if (!write_png((out / name).string(), canvas, scale)) throw std::runtime_error(std::string("PNG nicht schreibbar: ") + name);
                ++shots;
            };
            for (int i = 0; i < 3; ++i) st.handle(Button::Down);
            st.handle(Button::Cross);  // Mehr
            st.handle(Button::Down);
            st.handle(Button::Cross);  // Selbsttest (zweiter Eintrag)
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
        {  // Pro Spiel: Liste, Einzelansicht, Befund aus der Einzelansicht
            App ga(svc, lang);
            if (has_backdrop) ga.set_background(backdrop);
            auto png = [&](const char* name) {
                ga.render(canvas);
                if (!write_png((out / name).string(), canvas, scale)) throw std::runtime_error(std::string("PNG nicht schreibbar: ") + name);
                ++shots;
            };
            ga.handle(Button::Down);  // Kachel 1: Spiele
            png("20_start_games.png");
            ga.handle(Button::Cross);  // ohne Check-up: erst pruefen
            ga.render(canvas);
            ga.update();
            png("21_game_list.png");
            ga.handle(Button::Cross);
            png("22_game_detail.png");
            ga.handle(Button::R1);
            png("22b_game_detail_next.png");
            ga.handle(Button::R1);
            png("22c_game_detail_next.png");
            ga.handle(Button::L1);
            ga.handle(Button::L1);
            ga.handle(Button::Cross);
            png("23_game_finding.png");
        }
        {  // Reparaturen und Wartung
            App ra(svc, lang);
            if (has_backdrop) ra.set_background(backdrop);
            auto png = [&](const char* name) {
                ra.render(canvas);
                if (!write_png((out / name).string(), canvas, scale)) throw std::runtime_error(std::string("PNG nicht schreibbar: ") + name);
                ++shots;
            };
            for (int i = 0; i < 3; ++i) ra.handle(Button::Down);
            ra.handle(Button::Cross);  // Mehr
            ra.handle(Button::Cross);  // Reparaturen (erster Eintrag): ohne Check-up erst pruefen, dann die Liste
            ra.render(canvas);
            ra.update();
            png("24_repairs.png");
            ra.handle(Button::Circle);  // zurueck zu Mehr
            png("25_maintenance.png");
        }
        {  // Startbild
            App sp(svc, lang);
            if (has_backdrop) sp.set_background(backdrop);
            Canvas img(App::kWidth, App::kHeight);
            if (load_splash(img)) {
                sp.show_splash(img);
                sp.render(canvas);
                if (!write_png((out / "00_splash.png").string(), canvas, scale)) throw std::runtime_error("PNG nicht schreibbar: 00_splash.png");
                ++shots;
            }
        }
        {  // Verlauf: Check-up-Liste und Reparaturen. Arbeitskopie des Snapshots, damit gespeichert werden darf (Uhr: je Check-up eine Minute weiter)
            const std::filesystem::path work = out / "_work";
            std::error_code ec;
            std::filesystem::remove_all(work, ec);
            std::filesystem::copy(argv[1], work, std::filesystem::copy_options::recursive, ec);
            if (!ec) {
                auto wp = medic::make_snapshot_platform(work.string(), true);
                int minute = 0;
                LocalService ws(*wp, medic::load_rules_from_file(rules_path), [&minute] {
                    char b[32];
                    std::snprintf(b, sizeof b, "20261112T18%02d00Z", 10 + minute++);
                    return std::string(b);
                });
                ws.set_error_codes(medic::load_error_codes_from_file(MEDIC_DEFAULT_ERROR_CODES));
                for (int k = 0; k < 3; ++k) {
                    CheckResult cr = ws.run_check();
                    if (k == 1)
                        for (const auto& f : cr.findings)
                            if (!f.fix.empty()) {
                                ws.apply_fix(ws.plan_fix(f));
                                break;
                            }
                }
                App wa(ws, lang);
                if (has_backdrop) wa.set_background(backdrop);
                auto png = [&](const char* name) {
                    wa.render(canvas);
                    if (!write_png((out / name).string(), canvas, scale)) throw std::runtime_error(std::string("PNG nicht schreibbar: ") + name);
                    ++shots;
                };
                wa.handle(Button::Down);
                wa.handle(Button::Down);
                wa.handle(Button::Cross);  // Verlauf (Kachel 2)
                png("30_check_list.png");
                wa.handle(Button::Down);  // Zeile "Fruehere Check-ups" -> aufklappen
                wa.handle(Button::Cross);
                png("30b_check_list_open.png");
                wa.handle(Button::Square);
                png("31_repairs.png");
            }
            std::filesystem::remove_all(work, ec);
        }
        std::cout << shots << " Bilder in " << out.string() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Fehler: " << e.what() << "\n";
        return 2;
    }
    return 0;
}
