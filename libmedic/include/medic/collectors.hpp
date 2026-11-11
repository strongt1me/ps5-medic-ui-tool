// SPDX-License-Identifier: GPL-3.0-or-later
// Collectors sammeln Fakten. Sie lesen nur und kennen keine Regeln.
#pragma once
#include "medic/facts.hpp"
#include "medic/platform.hpp"

namespace medic {

void collect_firmware(const Platform& p, Facts& f);    // fw.version, fw.model
void collect_processes(const Platform& p, Facts& f);   // proc.running[] (klein geschrieben)
void collect_storage(const Platform& p, Facts& f);     // storage.mounts[].{mount,total,free,free_pct}
void collect_components(const Platform& p, Facts& f);  // kstuff.version, kstuff.origin, smp.version, ...
void collect_system(const Platform& p, Facts& f);      // net.port_9021, jb.seconds_after_boot, jb.first_process
void collect_shadowmount(const Platform& p, Facts& f); // smp.*
void collect_plugins(const Platform& p, Facts& f);     // ps5theme.version, ps5theme.source
void collect_apps(const Platform& p, Facts& f);        // apps.oversized[].{titleid,name,reserved_mib,content_bytes}
void collect_game_firmware(const Platform& p, Facts& f);  // games.fw[].{titleid,required_fw,sdk_fw,backport}, games.need_backport[].{titleid,required_fw,desc}
void collect_autoload(const Platform& p, Facts& f);       // autoload.problems[], pldmgr.autoload_enabled/_entries
void collect_payload_files(const Platform& p, Facts& f);  // payload.elf_files[], payload.broken_elfs[]
void collect_helpers(const Platform& p, Facts& f);        // nanodns.log.*, etahen.*
void collect_savedata(const Platform& p, Facts& f);       // savedata.current_account, savedata.foreign[].{uid,titleid,name,title,account,current,path,label}
void collect_system_logs(const Platform& p, Facts& f);    // boot.log_last, boot.unclean[], boot.unclean_total/_14d, errors.total, errors.pairs[].{code,titleid,count,last,label}
void collect_databases(const Platform& p, Facts& f);      // db.count, db.integrity_tested, db.files[].{path,name,size,readable,header_bad,truncated,unaligned,page_size,header_pages,journal_hot,journal_size,wal_size,integrity,integrity_note,integrity_skip}
void collect_game_files(const Platform& p, Facts& f);    // games.case_mismatch[].{titleid,path,expected,actual,fstype,case_sensitive}

// Fuehrt alle Collectors aus.
Facts collect_all(const Platform& p);

// Gesammelter Kernel-Log je Konsolenstart (D10b): Weil das Lesen von klogsrv verbraucht, haengt die App die neu gelesenen, gefilterten Zeilen
// (Fakt klog.new_lines) an diese Datei und liest sie beim naechsten Check-up wieder ein. Erste Zeile: "# boot <Startzeit>"; bei einem neuen
// Konsolenstart wird die alte Datei nach klog_boot_<alte Startzeit>.txt verschoben (nie geloescht) und eine neue begonnen.
constexpr const char* kKlogBootPath = "/data/ps5medic/klog_boot.txt";
constexpr std::size_t kKlogBootMaxBytes = 256 * 1024;
// Haengt die neuen Zeilen an (ohne Doppelte, hoechstens kKlogBootMaxBytes); false ohne Schreibzugriff, ohne neue Zeilen oder bei Fehler.
bool persist_klog(Platform& p, const Facts& facts);

}  // namespace medic
