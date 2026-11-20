// SPDX-License-Identifier: GPL-3.0-or-later
// Einstellungen der App (Datei /data/ps5medic/config.ini, Zeilen "schluessel=wert"). Neben `language` und `update_url` (siehe rule_download.hpp)
// kennt die App diese Schluessel; unbekannte Werte gelten wie der Standard, die Datei wird nie ueber die hier genannten Schluessel hinaus veraendert.
//
//   text_size        normal | large        Schriftgroesse der Oberflaeche
//   contrast         normal | high         hellere Nebentexte und Linien
//   utc_offset       Minuten (-720..840)   Zeitanzeige = UTC + Versatz (nur Anzeige; Bericht und Dateien bleiben UTC)
//   autostart_check  on | off              Check-up beim Start der App (liest nur)
//   rule_download    on | off              Regel-Download aus dem Netz erlaubt (Standard on; off: kein Netzzugriff beim Regel-Update)
//   net_test         on | off              lokaler Netztest des Selbsttests (nur das Gateway; Standard on)
//   update_source    default | custom      Quelle des Regel-Downloads: Standardadresse (GitHub) oder update_url aus config.ini
#pragma once
#include "medic/platform.hpp"

#include <cstdint>
#include <string>

namespace medic {

constexpr int kUtcOffsetMin = -720, kUtcOffsetMax = 840;

struct AppSettings {
    bool large_text = false;
    bool high_contrast = false;
    int utc_offset_min = 0;
    bool autostart_check = false;
    bool rule_download = true;
    bool net_test = true;
    bool custom_source = true;  // update_source != "default"; wirkt nur, wenn update_url eingetragen ist
    std::string custom_url;     // update_url aus config.ini (leer: keine eigene Adresse)
    std::string effective_url;  // die Adresse, die der Regel-Download benutzt
};

// Liest die Einstellungen (fehlende oder ungueltige Werte: Standard).
AppSettings load_app_settings(const IFileSystem& fs);

// Schreibt einen Schluessel nach config.ini, aber nur einen der oben genannten mit gueltigem Wert (sonst false, nichts geschrieben).
bool set_app_setting(Platform& p, const std::string& key, const std::string& value);

// Platz der eigenen Sicherungen unter /data/ps5medic/backups/ (nur zur Anzeige; Medic loescht nichts).
struct BackupUsage {
    bool known = false;       // false: Ordner nicht lesbar oder die Zaehlung wurde abgebrochen (zu viele Eintraege)
    int sets = 0;             // Ordner der obersten Ebene (je Reparatur einer)
    std::uint64_t bytes = 0;  // Summe der Dateigroessen darin
};
BackupUsage backup_usage(const IFileSystem& fs);

// Zeitstempel "YYYYMMDDTHHMMSSZ" um `offset_min` Minuten verschieben (gleiche Form zurueck). Ungueltige Eingabe: unveraendert.
std::string shift_timestamp(const std::string& ts, int offset_min);

}  // namespace medic
