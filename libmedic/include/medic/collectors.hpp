// SPDX-License-Identifier: GPL-3.0-or-later
// Collectors sammeln Fakten. Sie lesen nur und kennen keine Regeln.
#pragma once
#include "medic/facts.hpp"
#include "medic/platform.hpp"

#include <functional>

namespace medic {

void collect_firmware(const Platform& p, Facts& f);    // fw.version, fw.model
void collect_processes(const Platform& p, Facts& f);   // proc.running[] (klein geschrieben)
void collect_storage(const Platform& p, Facts& f);     // storage.mounts[].{mount,total,free,free_pct}
void collect_components(const Platform& p, Facts& f);  // kstuff.version, kstuff.origin, smp.version, ...
void collect_system(const Platform& p, Facts& f);      // net.port_9021, jb.seconds_after_boot, jb.first_process
void collect_shadowmount(const Platform& p, Facts& f); // smp.*
void collect_plugins(const Platform& p, Facts& f);     // ps5theme.version, ps5theme.source
void collect_apps(const Platform& p, Facts& f);        // apps.oversized[].{titleid,name,reserved_mib,content_bytes}
void collect_game_firmware(const Platform& p, Facts& f);  // games.fw[].{titleid,name,required_fw,sdk_fw,backport}, games.need_backport[].{titleid,required_fw,desc}
void collect_autoload(const Platform& p, Facts& f);       // autoload.problems[], pldmgr.autoload_enabled/_entries
void collect_payload_files(const Platform& p, Facts& f);  // payload.elf_files[], payload.broken_elfs[]
void collect_helpers(const Platform& p, Facts& f);        // nanodns.log.*, etahen.*
void collect_savedata(const Platform& p, Facts& f);       // savedata.current_account, savedata.foreign[].{uid,titleid,name,title,account,current,path,label}
void collect_system_logs(const Platform& p, Facts& f);    // boot.log_last, boot.unclean[], boot.unclean_total/_14d, errors.total, errors.pairs[].{code,titleid,count,last,label}
void collect_databases(const Platform& p, Facts& f);      // db.count, db.integrity_tested, db.files[].{path,name,size,readable,header_bad,truncated,unaligned,page_size,header_pages,journal_hot,journal_size,wal_size,integrity,integrity_note,integrity_skip}
void collect_game_files(const Platform& p, Facts& f);    // games.case_mismatch[].{titleid,path,expected,actual,fstype,case_sensitive}
void collect_game_runtime(const Platform& p, Facts& f);   // games.runtime[], games.pack_problems[]: libSceAmpr.sprx und Packs in Spielordnern und Images (collectors_games.cpp)
void collect_game_folders(const Platform& p, Facts& f);   // games.incomplete[]: Ordner in Scanpfaden, die wie ein Spiel aussehen, aber unvollstaendig sind
void collect_kraken(const Platform& p, Facts& f);         // kraken.log_present, kraken.sparse_extent[]: Meldungen aus /data/krakenmount/kraken.log
void collect_leftovers(const Platform& p, Facts& f);       // leftovers.files[].{path,name,size,is_dir,root}: Reste abgebrochener Kopier-/Packvorgaenge in den Scanpfaden

// Wuerde ShadowMount+ 1.7beta12 diesen Wert in einer v2-Datei annehmen? (Unbekannte Paare gelten als gueltig.) Auch die Fixes pruefen damit, was sie schreiben:
// ein einziger ungueltiger Wert laesst SM+ die ganze Datei verwerfen.
bool smp_v2_value_valid(const std::string& section, const std::string& key, const std::string& value);

// Fuehrt alle Collectors aus. progress (optional) wird nach jedem Schritt mit (erledigt, gesamt) gerufen, damit die Oberflaeche einen
// Fortschritt zeigen kann; der Aufruf laeuft im selben Thread zwischen zwei Collectors.
Facts collect_all(const Platform& p, const std::function<void(int, int)>& progress = {});

// Gesammelter Kernel-Log je Konsolenstart (D10b): Weil das Lesen von klogsrv verbraucht, haengt die App die neu gelesenen, gefilterten Zeilen
// (Fakt klog.new_lines) an diese Datei und liest sie beim naechsten Check-up wieder ein. Erste Zeile: "# boot <Startzeit>"; bei einem neuen
// Konsolenstart wird die alte Datei nach klog_boot_<alte Startzeit>.txt verschoben (nie geloescht) und eine neue begonnen.
constexpr const char* kKlogBootPath = "/data/ps5medic/klog_boot.txt";
constexpr std::size_t kKlogBootMaxBytes = 256 * 1024;
// Haengt die neuen Zeilen an (ohne Doppelte, hoechstens kKlogBootMaxBytes); false ohne Schreibzugriff, ohne neue Zeilen oder bei Fehler.
bool persist_klog(Platform& p, const Facts& facts);

}  // namespace medic
