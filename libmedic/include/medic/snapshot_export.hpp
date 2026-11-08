// SPDX-License-Identifier: GPL-3.0-or-later
// Snapshot-Export: schreibt den vom Medic gesehenen Zustand in einen Fixture-Ordner
// (system.json, procs.json, fs/...), den `medic-cli check <ordner>` am PC wieder einliest.
//
// Exportiert wird nur, was Collectors und Regeln lesen:
//  * /data/shadowmount/{config.ini, debug.log (Ende), autotune.ini, profiles.ini, manual.lst}
//  * Autoload-Listen (/data/ps5_autoloader/autoload.txt, /data/pldmgr/autoload.txt)
//  * Verzeichnisbaum der Scanpfade und von /mnt/shadowmnt: Namen als leere Dateien, bei Spielordnern nur
//    sce_sys/param.json, bei .exfat-Images die ersten 512 Bytes (Cluster-Groesse)
// Die Snapshot-Datei enthaelt also Datei- und Ordnernamen (z. B. Spieltitel). Vor dem Teilen pruefen.
#pragma once
#include "medic/facts.hpp"
#include "medic/platform.hpp"

#include <string>
#include <vector>

namespace medic {

struct ExportResult {
    bool ok = false;
    std::string error;
    std::size_t entries = 0;            // geschriebene Dateien und Ordner (ohne Metadaten)
    std::vector<std::string> warnings;  // z. B. abgeschnittene Verzeichnisse
};

// dest_dir wird angelegt; ein vorhandener, nicht leerer Ordner wird abgelehnt (nichts wird ueberschrieben).
ExportResult export_snapshot(const Platform& p, const Facts& facts, const std::string& dest_dir, const std::string& timestamp);

}  // namespace medic
