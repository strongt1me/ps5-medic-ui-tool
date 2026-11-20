// SPDX-License-Identifier: GPL-3.0-or-later
// Tests fuer die Einstellungen der App (libmedic/src/settings.cpp): Lesen aus config.ini, Schreiben nur gueltiger Werte, Quelle des Regel-Downloads,
// Platz der Sicherungen, Verschiebung der Zeitanzeige.
#include "check.hpp"
#include "testutil.hpp"
#include "medic/fixes.hpp"
#include "medic/http.hpp"
#include "medic/platform.hpp"
#include "medic/rule_download.hpp"
#include "medic/settings.hpp"

#include <string>

using namespace medic;
using namespace testutil;

void settings_tests() {
    {  // Standardwerte ohne config.ini
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        const AppSettings s = load_app_settings(p->fs());
        CHECK(!s.large_text && !s.high_contrast && s.utc_offset_min == 0 && !s.autostart_check && s.rule_download && s.net_test, "Einstellungen: Standardwerte");
        CHECK(s.custom_url.empty() && s.effective_url == kDefaultUpdateUrl, "Einstellungen: ohne eigene Adresse gilt die Standardquelle");
    }
    {  // Lesen
        Sandbox sb;
        spit(sb.real(kConfigPath),
             "language=de\n# Kommentar\ntext_size=large\ncontrast=high\nutc_offset=-90\nautostart_check=on\nrule_download=off\nnet_test=off\nupdate_url=http://example.test/r/\n");
        auto p = make_snapshot_platform(sb.dir.string(), true);
        const AppSettings s = load_app_settings(p->fs());
        CHECK(s.large_text && s.high_contrast && s.utc_offset_min == -90 && s.autostart_check && !s.rule_download && !s.net_test, "Einstellungen: Werte aus config.ini");
        CHECK(s.custom_url == "http://example.test/r/" && s.custom_source && s.effective_url == "http://example.test/r/", "Einstellungen: eigene Adresse gilt (update_source fehlt)");
        spit(sb.real(kConfigPath), "update_url=http://example.test/r/\nupdate_source=default\n");
        auto p2 = make_snapshot_platform(sb.dir.string(), true);
        const AppSettings d = load_app_settings(p2->fs());
        CHECK(!d.custom_source && d.custom_url == "http://example.test/r/" && d.effective_url == kDefaultUpdateUrl, "Einstellungen: update_source=default umgeht die eigene Adresse, ohne sie zu loeschen");
        CHECK(effective_update_url(p2->fs()) == kDefaultUpdateUrl, "Einstellungen: effective_update_url folgt update_source");
    }
    {  // Ungueltige Werte gelten wie der Standard
        Sandbox sb;
        spit(sb.real(kConfigPath), "text_size=huge\ncontrast=\nutc_offset=abc\nautostart_check=maybe\nrule_download=2\nnet_test=\n");
        auto p = make_snapshot_platform(sb.dir.string(), true);
        const AppSettings s = load_app_settings(p->fs());
        CHECK(!s.large_text && !s.high_contrast && s.utc_offset_min == 0 && !s.autostart_check && s.rule_download && s.net_test, "Einstellungen: ungueltige Werte = Standard");
        spit(sb.real(kConfigPath), "utc_offset=99999\n");
        auto p2 = make_snapshot_platform(sb.dir.string(), true);
        CHECK(load_app_settings(p2->fs()).utc_offset_min == 0, "Einstellungen: Zeitzone ausserhalb des Bereichs wird verworfen");
    }
    {  // Schreiben: nur gueltige Schluessel und Werte; andere Zeilen bleiben
        Sandbox sb;
        spit(sb.real(kConfigPath), "language=fr\nupdate_url=http://example.test/r/\n");
        auto p = make_snapshot_platform(sb.dir.string(), true);
        CHECK(set_app_setting(*p, "text_size", "large") && set_app_setting(*p, "utc_offset", "60") && set_app_setting(*p, "rule_download", "off"), "Einstellungen: gueltige Werte werden gespeichert");
        CHECK(!set_app_setting(*p, "text_size", "huge") && !set_app_setting(*p, "utc_offset", "9999") && !set_app_setting(*p, "net_test", "kaputt") &&
                  !set_app_setting(*p, "language", "de") && !set_app_setting(*p, "update_url", "http://evil/") && !set_app_setting(*p, "irgendwas", "on"),
              "Einstellungen: ungueltige Werte und fremde Schluessel werden abgelehnt (auch language und update_url nicht ueber diesen Weg)");
        const std::string text = slurp(sb.real(kConfigPath));
        CHECK(text.find("language=fr\n") != std::string::npos && text.find("update_url=http://example.test/r/\n") != std::string::npos, "Einstellungen: andere Zeilen bleiben stehen");
        CHECK(text.find("text_size=large\n") != std::string::npos && text.find("utc_offset=60\n") != std::string::npos && text.find("rule_download=off\n") != std::string::npos, "Einstellungen: die Zeilen stehen in config.ini");
        CHECK(set_app_setting(*p, "text_size", "normal"), "Einstellungen: zweites Speichern");
        const std::string again = slurp(sb.real(kConfigPath));
        CHECK(again.find("text_size=normal\n") != std::string::npos && again.find("text_size=large") == std::string::npos, "Einstellungen: der Schluessel wird ersetzt, nicht verdoppelt");
        auto ro = make_snapshot_platform(sb.dir.string(), false);
        CHECK(!set_app_setting(*ro, "text_size", "large"), "Einstellungen: ohne Schreibzugriff false");
    }
    {  // Sicherungen
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        BackupUsage none = backup_usage(p->fs());
        CHECK(none.known && none.sets == 0 && none.bytes == 0, "Sicherungen: noch keine Reparatur = nichts belegt");
        spit(sb.real(std::string(kBackupRoot) + "/20261108T100000Z-1/data/a.ini"), std::string(1000, 'x'));
        spit(sb.real(std::string(kBackupRoot) + "/20261108T100000Z-1/data/sub/b.ini"), std::string(24, 'y'));
        spit(sb.real(std::string(kBackupRoot) + "/20261109T100000Z-2/data/c.ini"), std::string(100, 'z'));
        auto p2 = make_snapshot_platform(sb.dir.string(), true);
        BackupUsage u = backup_usage(p2->fs());
        CHECK(u.known && u.sets == 2 && u.bytes == 1124, "Sicherungen: zwei Saetze, Summe der Dateigroessen");
    }
    {  // Zeitanzeige verschieben
        CHECK(shift_timestamp("20261119T233000Z", 60) == "20261120T003000Z", "Zeit: +1 h ueber Mitternacht");
        CHECK(shift_timestamp("20261101T003000Z", -60) == "20261031T233000Z", "Zeit: -1 h ueber den Monatswechsel");
        CHECK(shift_timestamp("20270101T000000Z", -15) == "20261231T234500Z", "Zeit: -15 min ueber den Jahreswechsel");
        CHECK(shift_timestamp("20240228T230000Z", 120) == "20240229T010000Z", "Zeit: Schaltjahr");
        CHECK(shift_timestamp("20261119T120000Z", 0) == "20261119T120000Z" && shift_timestamp("20261119T120000", 330) == "20261119T173000", "Zeit: ohne Versatz unveraendert, ohne Z bleibt es ohne Z (+5:30)");
        CHECK(shift_timestamp("kein Zeitstempel", 60) == "kein Zeitstempel" && shift_timestamp("20261319T120000Z", 60) == "20261319T120000Z", "Zeit: ungueltige Eingabe bleibt unveraendert");
    }
}
