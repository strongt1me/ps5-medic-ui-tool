<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Regel-Uebersicht

**Deutsch** · [English](en/RULE_TABLE.md)

Erzeugt von `tools/gen_rule_table.py` aus `knowledge/rules.json` (db_version 2026.11.61, 124 Regeln). Nicht von Hand aendern.

| Regel | Stufe | Firmware | Reparatur | Nachweis | Aussage |
|---|---|---|---|---|---|
| `ampr.stale_cache` | info | alle | - | todo | Im Ordner /data/ampr_cache liegen Reste des Plattencaches älterer ampr-emu-ng-Fassungen (bis 0.4.1); nach einem Absturz bleiben sie liege... |
| `ampr.trace_problem` | warning | alle | - | todo | Das Debug-Protokoll von ampr-emu-ng (/data/ampr_emu_trace.txt, letzter Spielstart) meldet einen Fehler: Index abgelehnt, Packs nicht verb... |
| `appdb.orphan_entry` | warning | alle | - | todo | Die Datenbank der Systemsoftware (app.db) führt einen Titel, dessen Ordner unter /user/app oder /system_ex/app fehlt (Evidenz: Titel-ID):... |
| `appdb.unregistered_folder` | info | alle | - | todo | Unter /user/app liegt ein Titelordner (PPSA/CUSA), den die Datenbank der Konsole nicht kennt; das Spiel fehlt in der Bibliothek. |
| `autoload.elfldr_without_delay` | warning | alle | `autoload_insert_delay` | doc | In der Autoload-Liste folgt auf den Loader (elfldr) sofort der nächste Eintrag. |
| `autoload.entry_duplicate` | info | alle | `autoload_remove_duplicates` | doc | Ein Eintrag steht mehrfach in der Autoload-Liste und wird jedes Mal gesendet (die Loader entfernen Doppelte nicht). |
| `autoload.entry_missing` | warning | alle | `autoload_fix_missing` | doc | Ein Eintrag der Autoload-Liste hat keine passende Payload-Datei (oder nur die Groß-/Kleinschreibung weicht ab; Namen sind case-sensitiv). |
| `autoload.pldmgr_not_listed` | warning | alle | `autoload_add_pldmgr` | doc | In /data/ps5_autoloader liegt pldmgr.elf, steht aber nicht in der autoload.txt. |
| `content.dlc_without_game` | info | alle | - | todo | Zusatzinhalte oder Updates ohne Spiel: Für diese Titel liegen Daten unter /user/addcont oder /user/patch, aber das Spiel ist weder instal... |
| `content.download_task_pending` | info | alle | - | todo | Unerledigte Download- oder Installationsaufträge: Unter /user/bgft/task liegen Aufträge der Systemsoftware. |
| `content.license_without_game` | info | alle | - | todo | Lizenzen ohne Spiel: Unter /user/license liegen Lizenzdateien (.rif) für Titel, die weder installiert noch als Quelle zu finden sind. |
| `db.header_invalid` | warning | alle | - | doc | Der Kopf einer Datenbank der Systemsoftware ist ungültig (fehlende Kacheln oder „Datenbank beschädigt“ möglich). |
| `db.integrity_failed` | warning | alle | - | doc | Eine Datenbank der Systemsoftware ist beschädigt (SQLite integrity_check auf einer Kopie). |
| `db.journal_pending` | info | alle | - | doc | Neben einer Datenbank der Systemsoftware liegt ein Journal mit offener Transaktion. |
| `db.truncated` | warning | alle | - | doc | Eine Datenbank der Systemsoftware ist kleiner, als ihr Kopf angibt (abgeschnitten), z. |
| `errors.recurring_system_app` | info | alle | - | todo | Eine Systemanwendung (Titel NPXS…) hat im Fehlerverlauf mehrfach denselben CE-Fehlercode gemeldet. |
| `errors.recurring_title` | warning | alle | - | todo | Der Fehlerverlauf (/system_data/priv/error/history) enthält denselben CE-Fehlercode mehrfach für denselben Titel: Der Titel startet oder... |
| `fan.double_controller` | warning | alle | - | todo | Zwei Lüfterregler laufen gleichzeitig (siehe Liste). |
| `forge.old_version` | info | alle | - | todo | Installiert ist Dump Forge vor 0.5.6. |
| `game.backport_incomplete` | warning | alle | - | todo | Bei diesen Spielen ist der Backport nur halb eingebaut: Die eboot.bin trägt schon ein SDK, das zur Konsole passt, aber mindestens ein Mod... |
| `game.compat_known_bad` | info | alle | - | community | Laut der Kompatibilitätsliste für ZSTD- und LZ4-Sicherungen läuft dieses Spiel bei Testern nicht oder nur mit widersprüchlichen Berichten... |
| `game.eboot_invalid` | warning | alle | - | todo | Die eboot.bin eines Spielordners ist unbrauchbar: kleiner als 64 Byte oder reiner Text (z. |
| `game.fakelib_layout` | info | alle | - | doc | Ein Spiel hat einen unklaren fakelib-Aufbau (Beleg: Pfad und Art). |
| `game.flex_memory_small` | info | alle | - | community | In sce_sys/param.json des Spiels steht kernel.flexibleMemorySize unter 429916160 (410 MiB), das Spiel nutzt die ng-Laufzeit ab 0.3.4, und... |
| `game.il2cpp_case` | warning | alle | `rename_case_prx` | community | Im Spielordner heißt die Datei Il2cppUserAssemblies.prx statt Il2CppUserAssemblies.prx (Unity-Spiel, Media/Modules). |
| `game.il2cpp_case_latent` | info | alle | `rename_case_prx` | community | Im Spielordner auf einem exFAT-/FAT-Laufwerk heißt die Datei Il2cppUserAssemblies.prx statt Il2CppUserAssemblies.prx (Unity-Spiel, Media/... |
| `game.image_undo_journal` | warning | alle | - | community | Neben einem Image liegt ein Undo-Journal (<Image>.forge-undo, *.orig.part oder <Image>.undo). |
| `game.incomplete_folder` | warning | alle | - | todo | Ein Ordner in einem Scanpfad sieht wie ein Spiel aus, ist aber unvollständig: es fehlt die eboot.bin oder die sce_sys/param.json, oder di... |
| `game.index_files_missing` | warning | alle | - | doc | In einem Spielordner fehlen Dateien, die die ampr_emu.index nennt (Beleg: Pfad und Zahl fehlender Dateien von den geprüften; Beispiele st... |
| `game.leftover_parts` | warning | alle | `quarantine_leftovers` | community | In einem Scanpfad liegen Reste abgebrochener Kopier- oder Packvorgänge (.part, .tmp, .teil, .neu, .unvollstaendig). |
| `game.needs_backport` | warning | alle | - | todo | Für diese Spiele verlangt die param.json eine höhere Firmware-Version als die der Konsole, und in den Scanpfaden fehlt der Ordner backpor... |
| `game.ng_index_too_big` | info | alle | - | doc | Ein sehr großes Spiel (ab etwa 200 000 Dateien laut ampr_emu.index) nutzt ampr-emu-ng 0.3.5 oder 0.4.0. |
| `game.ng_save_risk` | info | alle | - | community | Im Spiel liegt eine ampr-emu-ng-Fassung (0.3.5 oder 0.4.0, Release), bei der Nutzer Probleme beim Speichern melden, und die Konsole hat f... |
| `game.no_exec_permission` | warning | alle | `chmod_exec` | community | Programmdateien eines Spiels (eboot.bin, Module) haben kein Ausführungsrecht, zum Beispiel 0666 statt 0777. |
| `game.p5z_container` | info | alle | - | todo | In einem Scanpfad liegen .p5z-Dateien. |
| `game.p5z_truncated` | warning | alle | - | todo | Eine .p5z-Datei ist unvollständig: Der Kopf ist da, das Ende (der Fuß, den MaxPack zuletzt schreibt) fehlt. |
| `game.pack_incomplete` | warning | alle | - | doc | Die zstd-Pack-Dateien eines Spiels sind unvollständig: ein Teil (ampr_zstd-NNN.pak) fehlt, hat nicht die Größe laut Index, oder die ampr_... |
| `game.pack_no_emu_index` | info | alle | - | todo | Ein Spiel mit Packs hat keine ampr_emu.index. |
| `game.pack_runtime_mismatch` | warning | alle | - | doc | Ein Spiel mit gepackten Dateien (ampr_zstd.index oder ampr_assets.index) hat keine passende Laufzeit-Bibliothek: fakelib/libSceAmpr.sprx... |
| `game.partial_folder` | warning | alle | `quarantine_partial_folder` | community | In einem Scanpfad liegt ein Packordner mit Volumes (ampr_zstd-NNN.pak), aber ohne ampr_zstd.index. |
| `game.playgo_stub_missing` | info | alle | - | community | Das Spiel hat PlayGo-Dateien (sce_sys/playgo-scenario.json oder playgo-chunk.dat), aber weder libScePlayGo.sprx im fakelib noch eine Play... |
| `game.runtime_debug_build` | info | alle | - | todo | In diesen Spielen steckt die Debug-Fassung von ampr-emu-ng. |
| `game.runtime_old_version` | info | alle | - | doc | Im Spiel liegt eine alte ampr-emu-ng-Fassung (vor 0.3.4), und die Konsole hat für dieses Spiel einen Fehlercode verzeichnet. |
| `game.trophy_files_missing` | info | alle | - | community | Im Spielordner gibt es sce_sys/trophy2 mit Dateien, aber mindestens eine der Trophäen-Dateien fehlt (sce_sys/nptitle.dat, sce_sys/uds/npb... |
| `game.unsigned_elf` | info | alle | - | community | Mindestens eine Programmdatei des Spiels (eboot.bin, sce_module, prx oder fakelib) ist eine reine, nicht signierte ELF-Datei, und die Kon... |
| `hen.etahen_crashed` | warning | alle | - | doc | etaHEN meldet einen Absturz seines Hauptdienstes ('main etaHEN has crashed'); Toolbox und Plugins fehlen danach. |
| `hen.etahen_double_load` | warning | alle | - | doc | Es laufen mehrere etaHEN-Prozesse. |
| `hen.etahen_game_rest_kill` | info | alle | - | doc | In etaHEN ist 'Game_rest_kill' (oder 'Util_rest_kill') eingeschaltet: Beim Ruhemodus beendet etaHEN das offene Spiel bzw. |
| `hen.etahen_with_kstufflite_latest_fw` | critical | >=13.60 | - | community | etaHEN und kstuff-lite laufen gleichzeitig. |
| `hen.jb_too_early` | info | alle | - | community | Der erste Prozess der Jailbreak-Kette startete weniger als 60 Sekunden nach dem Einschalten. |
| `kraken.ampr_foreign` | info | alle | - | community | Im Ordner /data/krakenmount/ampr/ liegt eine von Hand abgelegte Datei („is not a bundled ampr-emu-ng: kept“). |
| `kraken.api_ampr_hand` | info | alle | - | todo | Kraken Mount nutzt eine von Hand eingelegte ampr-emu-ng-Laufzeit (Zustand „hand“) statt der mitgelieferten oder heruntergeladenen. |
| `kraken.api_ampr_missing` | warning | alle | - | todo | Kraken Mount hat keine brauchbare ampr-emu-ng-Laufzeit (Zustand „missing“ oder „error“). |
| `kraken.api_duplicate` | warning | alle | - | todo | Kraken Mount findet dieselbe Title-ID mehrfach (Status „duplicate“) und hängt die zusätzlichen Quellen nicht ein. |
| `kraken.api_game_error` | warning | alle | - | todo | Kraken Mount meldet für diese Spiele einen Fehler (Status „error“, die Meldung steht im Beleg). |
| `kraken.api_low_space` | warning | alle | - | todo | Kraken Mount meldet zu wenig freien Platz für seinen Cache. |
| `kraken.cache_block` | warning | alle | - | community | Kraken Mount erkennt eine .pkg, scheitert aber beim Aufbau des Caches („cache build … short compressed block“): Ein komprimierter Block r... |
| `kraken.game_crashed` | info | alle | - | todo | Kraken Mount hat für diese Spiele in den letzten 14 Tagen einen Absturz festgehalten (/data/krakenmount/crashes.json, mit Grund wie „kill... |
| `kraken.kstuff_missing` | warning | alle | - | todo | Kraken Mount konnte ein Spiel nicht starten, weil kstuff nicht lief („kstuff is not running“ in kraken.log). |
| `kraken.launch_failed` | warning | alle | - | todo | Beim Start eines Spiels konnte Kraken Mount etwas nicht einhängen: Overlay, beschreibbares /app0 oder Backport-Umleitung („… FAILED“ in k... |
| `kraken.pkg_sparse_extent` | info | alle | - | community | Kraken Mount hat eine .pkg abgelehnt: Das Layout der Paketdatei hat eine Lücke (sparse extent), und das Paket bekam deshalb keine Kachel;... |
| `kraken.pkg_truncated` | warning | alle | - | community | Kraken Mount meldet für eine .pkg „truncated?“: Die Datei ist kürzer, als ihr Kopf angibt, etwa nach einem abgebrochenen Download oder ei... |
| `kraken.pkg_unsupported` | warning | alle | - | community | Kraken Mount lehnt eine .pkg ab, weil sie kein Paket der Art ist, die es lesen kann (Meldung „unsupported package“ oder „not a PS5 packag... |
| `kraken.sysex_readonly` | warning | alle | - | community | Kraken Mount konnte einen neuen Titel nicht anmelden: Die Konsole hat /system_ex schreibgeschützt eingehängt (os error 30), die Meldung s... |
| `kraken.waiting_for_smp` | warning | alle | - | todo | Kraken Mount wartet und hängt nichts ein: Es startet erst, wenn ShadowMount+ beendet ist, und ShadowMount+ läuft gerade (Meldung „waiting... |
| `nanodns.log_problem` | info | alle | - | doc | Das nanoDNS-Log enthält Fehler: nanoDNS erreicht seine Upstream-DNS-Server nicht (dann scheitert jede Namensauflösung, Apps und Payload M... |
| `payload.elf_invalid` | warning | alle | `quarantine_broken_elf` | doc | Mindestens eine ELF-Datei ist unbrauchbar: zu klein, ohne ELF-Kennung (oft eine gespeicherte HTML-Fehlerseite) oder abgeschnitten (abgebr... |
| `payload.port_9021_closed` | warning | alle | - | todo | Port 9021 nimmt keine Verbindungen an, und weder elfldr noch etaHEN läuft: Es ist kein ELF-Loader aktiv, Payloads lassen sich nicht an di... |
| `pldmgr.autoload_disabled` | info | alle | `pldmgr_enable_autoload` | doc | Der Payload Manager hat Einträge in der Autoload-Liste, aber Autoload ist ausgeschaltet (AUTOLOAD_ENABLED=0): die Payloads werden beim St... |
| `pldmgr.autoload_syntax` | warning | alle | - | doc | Die Autoload-Liste des Payload Managers hat ein ungültiges Format. |
| `pldmgr.loose_elfs` | info | alle | - | doc | Im Ordner /data/pldmgr/payloads liegen ELF-Dateien lose. |
| `plugin.ps5theme_old_overlay` | warning | alle | - | todo | Installiert ist ein ps5-theme-Testbuild (Version zwischen 1.30 und 1.53), bei dem laut Release-Notes ein zurückgebliebenes Overlay den St... |
| `savedata.foreign_account` | warning | alle | - | todo | Diese Spielstände gehören zu einer anderen Konto-ID als der jetzt aktiven (z. |
| `selftest.failed` | warning | alle | - | todo | Ein Selbsttest ist fehlgeschlagen (Evidenz: Test, Grund). |
| `selftest.interrupted` | warning | alle | `selftest_clear_marker` | todo | Der letzte Selbsttest wurde nicht ordentlich beendet (Markierung „läuft“ noch in /data/ps5medic/selftest). |
| `selftest.net_local` | info | alle | - | todo | Im Netztest war die Verbindung zum Router auffällig: Mindestens 20 % der Verbindungsversuche blieben unbeantwortet oder der Router antwor... |
| `selftest.thermal` | warning | alle | - | todo | Im Wärmetest des Selbsttests fiel die Konsole unter kurzer CPU-Last auf: Sie erreichte 85 °C, stieg zu schnell an, kühlte langsam ab oder... |
| `selftest.write_failed` | critical | alle | - | todo | Schreibtest des Selbsttests fehlgeschlagen (Evidenz: Grund): Die Testdatei unter /data/ps5medic/selftest ließ sich nicht schreiben („writ... |
| `smp.api_not_responding` | info | alle | - | community | ShadowMount+ meldete beim Start seine HTTP-API als bereit, aber der Port nimmt keine Verbindungen mehr an. |
| `smp.autotoggle_crash_1360` | warning | =13.60 | `smp_set_key` | community | Diese Kombination (FW 13.60, ShadowMount+ 1.7beta3, kstuff-lite 1.11, kstuff_game_auto_toggle=1) wurde mit wiederkehrenden Spielabstürzen... |
| `smp.backpork_conflict` | warning | alle | `smp_disable_conflicting_backpork` | doc | BackPork läuft parallel zur Fakelib-Funktion von ShadowMount+. |
| `smp.backport_overlay_denied` | warning | alle | - | community | Der Backport-Overlay (Fakelib) ließ sich nicht einhängen ('Operation not permitted'): auf USB-Laufwerken (exFAT) ist das nicht erlaubt. |
| `smp.config_rejected` | critical | alle | `smp_comment_invalid_lines` | doc | ShadowMount+ 1.7beta12 verwirft die ganze Konfiguration, wenn auch nur eine Zeile ungültig ist: Beim Start beendet sich der Payload (Meld... |
| `smp.custom_scanpath_hides_defaults` | warning | alle | `smp_set_key` | doc | Es sind eigene Scanpfade gesetzt, daher werden die Standardpfade nicht mehr gescannt und ShadowMount+ findet diese Spiele nicht. |
| `smp.debug_off` | info | alle | `smp_set_key` | doc | debug=0: ohne Debug-Log ist keine Diagnose möglich. |
| `smp.duplicate_titleid` | warning | alle | `move_duplicate_titleid` | doc | Dieselbe TITLE_ID liegt in mehreren Quellen (z. |
| `smp.emulator_update_replaces_runtime` | warning | alle | `smp_set_key` | doc | ShadowMount+ hat Emulator-Updates eingeschaltet, und ein Spiel hat zstd-Packs. |
| `smp.exfat_cluster` | info | alle | - | doc | Dieses .exfat-Image hat Cluster kleiner als 64 KB und ist dadurch langsamer. |
| `smp.fakelib_handoff_stuck` | warning | alle | - | community | Ein beendeter Prozess blockiert die Aufräumschlange der Fakelib: danach starten Backport-Spiele ohne Fakelib und stürzen ab. |
| `smp.fakelib_off_with_runtime` | warning | alle | `smp_set_key` | doc | ShadowMount+ hat die fakelib-Funktion abgeschaltet ([fakelib] enabled = 0), aber mindestens ein Spiel hat Dateien in fakelib oder fakelib... |
| `smp.ffpfsc_direct_files` | warning | alle | `fix_image_nesting_hint` | doc | Der .ffpfsc-Container enthält direkt Spieldateien. |
| `smp.fw900_kstuff_fork` | info | =9.00 | - | community | Auf FW 9.00 starten Dumps mit der kstuff-lite-Version von EchoStretch laut Nutzerberichten teils nicht („can't launch the game or app“). |
| `smp.image_cache_full` | warning | alle | - | community | ShadowMount+ hat einen Mount zurückgenommen, weil der Image-Cache voll war (gemeldet bei 64 Einträgen und über 100 Images auf dem Laufwerk). |
| `smp.image_empty` | warning | alle | `quarantine_empty_image` | todo | Ein Image (.exfat, .ffpfs oder .ffpfsc) ist kleiner als 512 Byte, also leer oder nur ein Rest nach einem Abbruch, und lässt sich nicht ei... |
| `smp.image_extra_folder` | warning | alle | `fix_image_nesting_hint` | doc | Im Image liegt das Spiel in einem zusätzlichen Ordner (/<ORDNER>/sce_sys/param.json statt /sce_sys/param.json). |
| `smp.image_mount_0x60` | warning | alle | - | community | ShadowMount+ kann ein Image nicht einhängen (Fehler 0x00000060): Das Image ist angehängt, aber sein Dateisystem lässt sich nicht öffnen. |
| `smp.image_sector_autotune` | info | alle | - | doc | ShadowMount+ hat die Sektorgröße eines Images automatisch angepasst (ab 1.7beta12 steht der Wert in der profiles.ini unter [image:DATEI],... |
| `smp.image_truncated` | warning | alle | - | doc | Ein Image (.exfat, .ffpfs oder .ffpfsc) ist kleiner, als sein Kopf angibt (VolumeLength bzw. |
| `smp.kstuff_min_version` | critical | alle | - | doc | ShadowMount+ benötigt kstuff-lite ab Version 1.07. |
| `smp.kstuff_wait` | warning | alle | - | doc | ShadowMount+ wartet auf kstuff und startet bis dahin keine Dienste (kein Scan, keine Mounts). |
| `smp.launch_busy` | warning | alle | - | community | Spiele starten nicht mehr: ShadowMount+ meldet mehrfach 'launch mount unavailable … status=16 (Device busy)'. |
| `smp.leak_pre_beta11` | warning | alle | - | community | ShadowMount+ 1.7beta9 oder 1.7beta10 verliert bei jedem Scan Arbeitsspeicher (rund 5 bis 8 MB pro Minute); nach Stunden drohen Ruckeln, A... |
| `smp.parentheses_in_pfs_name` | info | alle | `rename_image_parentheses` | community | Der Name eines .ffpfs/.ffpfsc-Images enthält Klammern. |
| `smp.register_console_db` | warning | alle | - | community | ShadowMount+ konnte ein Spiel oder eine App nicht registrieren („Register failed: … code=0x80A30008“). |
| `smp.register_failed` | warning | alle | - | community | Die Registrierung eines Spiels ist fehlgeschlagen. |
| `smp.retry_limit` | warning | alle | - | doc | ShadowMount+ hat bei einem Titel oder Image nach mehreren Fehlversuchen aufgegeben (2 je Titel, 3 je Image) und überspringt es bis zum nä... |
| `smp.scan_depth` | info | alle | `smp_set_key` | doc | Diese Spiele liegen tiefer als die eingestellte Scan-Tiefe und werden nicht gefunden. |
| `smp.scanner_root_denied` | warning | alle | - | community | ShadowMount+ konnte einen Scanpfad nicht überwachen (Zugriff verweigert) und hat den Scanner beendet: Neue Spiele werden nicht mehr erkannt. |
| `smp.shellcore_hook_lost` | warning | alle | - | community | ShadowMount+ hat seinen Registrierungs-Haken in ShellCore verloren (gemeldet nach Ruhemodus oder Spielwechsel): Neue Spiele werden eingeb... |
| `smp.shellcore_hooks_failed` | warning | alle | - | community | ShadowMount+ konnte beim Start seine Haken in ShellCore nicht installieren (gemeldet auf den Firmwares 11.60 und 12.70 mit 1.7beta3 und 1... |
| `smp.source_not_stable` | info | alle | - | doc | debug.log meldet für dieselbe Quelle wiederholt „source not stable yet“: Das Kopieren läuft noch, oder stability_wait_seconds passt nicht... |
| `smp.usb_game_closed_on_rest` | info | alle | - | doc | ShadowMount+ hat beim Ruhemodus ein Spiel von einem USB-Laufwerk beendet (gewollt, kein Fehler). |
| `smp.v1_config_restored` | critical | alle | - | doc | Die config.ini ist im alten Format (v1), obwohl ShadowMount+ sie schon auf v2 umgestellt hat (config-v1-backup/ready.ini existiert), und... |
| `smp.v1_keys_in_v2` | warning | alle | `smp_translate_v1_keys` | doc | Die config.ini (Format v2) enthält Zeilen im alten flachen Format, z. |
| `storage.low_space_data` | warning | alle | - | todo | Auf /data sind weniger als 5 % Speicher frei; Mounts, Logs, Backups und Installationen können scheitern. |
| `storage.oversized_reservation` | info | alle | - | todo | Eine Homebrew-App reserviert mehrere GB Speicher (param.json: downloadDataSize), obwohl ihre Dateien zusammen unter 1 GB bleiben (z. |
| `storage.scanpath_drive_missing` | info | alle | - | doc | Ein eigener Scanpfad in der config.ini liegt auf einem nicht angeschlossenen Laufwerk (/mnt/usbN oder /mnt/extN): Die Spiele dort werden... |
| `storage.usb_mounted_ro` | info | alle | - | todo | Ein angeschlossenes Laufwerk ist schreibgeschützt eingehängt: Kopieren, Verschieben und Reparaturen gehen dort nicht. |
| `sys.clock_implausible` | warning | alle | - | todo | Die Uhr der Konsole geht falsch: Sie steht vor dem Erscheinungsdatum dieser Medic-Version und kann deshalb nicht stimmen (Uhrzeit und Dat... |
| `sys.temp_above_usual` | info | alle | - | todo | Die Konsole ist wärmer als sonst: CPU oder SoC liegen mehr als 12 °C über dem Median deiner letzten Prüfungen (mindestens fünf). |
| `sys.temp_high` | info | alle | - | todo | Die Konsole ist gerade sehr heiß (CPU oder SoC über 85 °C). |
| `sys.unclean_boot` | warning | alle | - | todo | Der Startverlauf (/system_data/priv/vshlog) zeigt in den letzten 14 Tagen mindestens drei Neustarts, vor denen kein Herunterfahren protok... |
| `sys.unclean_boot_history` | info | alle | - | todo | Der Startverlauf (/system_data/priv/vshlog) enthält Neustarts ohne protokolliertes Herunterfahren (z. |
| `update.pup_on_usb` | warning | alle | `block_update_file` | doc | Auf einem USB-Laufwerk liegt die Datei PS5/UPDATE/PS5UPDATE.PUP. |
