// SPDX-License-Identifier: GPL-3.0-or-later
// Collectors sammeln Fakten. Sie lesen nur und kennen keine Regeln.
#pragma once
#include "medic/facts.hpp"
#include "medic/platform.hpp"

#include <functional>

namespace medic {

void collect_firmware(const Platform& p, Facts& f);    // fw.version, fw.model
void collect_clock(const Platform& p, Facts& f);       // sys.clock_epoch, sys.clock_ok, sys.clock_desc (Konsolenuhr gegen das Erscheinungsdatum dieser Version)
void collect_content_leftovers(const Platform& p, Facts& f);  // content.installed_ids[], content.source_ids[], content.dlc_without_game[], content.license_without_game[], content.download_tasks[] (collectors_games.cpp)
void collect_processes(const Platform& p, Facts& f);   // proc.running[] (klein geschrieben)
void collect_storage(const Platform& p, Facts& f);     // storage.mounts[].{mount,total,free,free_pct}
void collect_components(const Platform& p, Facts& f);  // kstuff.version, kstuff.origin, smp.version, ...
void collect_system(const Platform& p, Facts& f);      // net.port_9021, jb.seconds_after_boot, jb.first_process
void collect_shadowmount(const Platform& p, Facts& f); // smp.*
void collect_plugins(const Platform& p, Facts& f);     // ps5theme.version, ps5theme.source
void collect_apps(const Platform& p, Facts& f);        // apps.oversized[].{titleid,name,reserved_mib,content_bytes}
void collect_game_firmware(const Platform& p, Facts& f);  // games.fw[].{titleid,name,required_fw,sdk_fw,eboot_sdk,module_sdk,backport,backport_inline,backport_marker,backport_marker_fw,backport_incomplete,sdk_dir}, games.need_backport[].{titleid,required_fw,desc}, games.backport_incomplete[].{titleid,required_fw,eboot_sdk,module_sdk,desc}
void collect_autoload(const Platform& p, Facts& f);       // autoload.problems[], pldmgr.autoload_enabled/_entries
void collect_payload_files(const Platform& p, Facts& f);  // payload.elf_files[], payload.broken_elfs[]
void collect_helpers(const Platform& p, Facts& f);        // nanodns.log.*, etahen.*, update.pup_files[].{path,dir,size}
void collect_savedata(const Platform& p, Facts& f);       // savedata.current_account, savedata.foreign[].{uid,titleid,name,title,account,current,path,label}
void collect_system_logs(const Platform& p, Facts& f);    // boot.log_last, boot.unclean[], boot.unclean_total/_14d, errors.total, errors.pairs[].{code,titleid,count,last,label}
void collect_databases(const Platform& p, Facts& f);      // db.count, db.integrity_tested, db.files[].{path,name,size,readable,header_bad,truncated,unaligned,page_size,header_pages,journal_hot,journal_size,wal_size,integrity,integrity_note,integrity_skip}
void collect_game_files(const Platform& p, Facts& f);    // games.case_mismatch[].{titleid,path,expected,actual,fstype,case_sensitive}
void collect_game_runtime(const Platform& p, Facts& f);   // games.runtime[], games.pack_problems[]: libSceAmpr.sprx und Packs in Spielordnern und Images (collectors_games.cpp)
void collect_game_folders(const Platform& p, Facts& f);   // games.incomplete[]: Ordner in Scanpfaden, die wie ein Spiel aussehen, aber unvollstaendig sind
void collect_kstuff_file(const Platform& p, Facts& f);    // kstuff.file, kstuff.file_version (nur Information)
void collect_game_layout(const Platform& p, Facts& f);     // games.partial_folders[], games.undo_journals[], games.fakelib_layout[], games.with_fakelib[]: Aufbau von Spielordnern und Reste (nur Namen)
void collect_game_hints(const Platform& p, Facts& f);      // games.unsigned_elf[], games.flex_small[], games.playgo_missing[], games.trophy_missing[]: Hinweise (S07, S08, S14, S24), nur mit Spur
void collect_compat(const Platform& p, Facts& f);         // games.compat[].{titleid,status,format}, games.compat_bad[].{titleid,status,format,desc}: Treffer aus der eingebetteten Kompatibilitaetsliste (nach smp.titles und games.fw)
void collect_ampr_trace(const Platform& p, Facts& f);  // ampr.trace_present, ampr.trace_problems[].{kind,line} (/data/ampr_emu_trace.txt), ampr.stale_cache
void collect_dumpforge(const Platform& p, Facts& f);   // forge.version, forge.path, forge.before_056 (Marke in der Dump-Forge-Payload)
void collect_p5z(const Platform& p, Facts& f);         // games.p5z[].{path,name,truncated,desc}: MaxPack-Container in den Scanpfaden
void collect_kraken_api(const Platform& p, Facts& f);  // kraken.api.*: lokale API von Kraken Mount (nur GET 127.0.0.1:8097 /api/state, /api/ampr)
void collect_converters(const Platform& p, Facts& f);   // tools.gc_*, games.lz4_*, games.e2a_*, tools.e2a_launch_blocked, tools.alembic_resume (Runde 2)
void collect_appdb_tools(const Platform& p, Facts& f);  // tools.tabshifter_*, tools.homehider_*, tools.youtube_block_* (nur Dateispuren, D17)
void collect_tool_traces(const Platform& p, Facts& f);  // devtools.*, apr.updater_present, kstuff.a53_ppr_failed, games.autokstuff_invalid, games.playgo_*
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
