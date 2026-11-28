#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Erzeugt die synthetischen Fixtures zum Abgleich mit Kraken Mount 0.4.2, ampr-emu-ng 0.5.1, Dump Forge 0.6.0 und MaxPack (28. Nov. 2026) unter tests/fixtures/.
Aufbau: siehe collect_kraken, collect_ampr_trace, collect_dumpforge und collect_p5z in libmedic/src/collectors_games.cpp. Nur erfundene Titel-IDs und Namen.

  kraken_wait_pos / _neg      Log sagt zuletzt "wartet" und ShadowMount+ laeuft / Log sagt danach "ready"
  kraken_crash_pos / _neg     Absturz vor 2 Tagen / vor 30 Tagen (crashes.json)
  kraken_launch_pos / _neg    "kstuff is not running" und "overlay mount FAILED" / nur "kstuff probe: answered"
  ampr_trace_pos / _neg       "idx Invalid" in der Black Box / nur "idx Loaded", "idx Missing" und "rw on"
  ampr_cache_pos / _neg       /data/ampr_cache mit Inhalt und ng 0.4.1 / ng 0.5.1 (raeumt selbst auf)
  ampr_debug_pos / _neg       Debug-Laufzeit (Marke ampr_emu_tune.txt) / Release-Laufzeit
  forge_old_pos / _neg        Dump Forge 0.5.5 / 0.6.0 (Marke in der Payload)
  p5z_pos / p5z_neg           vollstaendige und abgeschnittene .p5z / Datei mit .p5z-Endung ohne MaxPack-Kopf
  kraken_api_pos              lokale API: Spiel mit Fehler, doppelte Title-ID, wenig Platz, ampr fehlt
  kraken_api_hand_pos         lokale API: ampr von Hand eingelegt
  kraken_api_neg              lokale API: alles in Ordnung (Status ok, Platz genug, ampr mitgeliefert)

Fehlalarme mit PS5 Cooling Center 1.99 (28. Nov. 2026, an der Test-Konsole gefunden):
  cc_app0_neg                 nur "sandbox /app0 rw mount FAILED" (harmlos, das Spiel startet)
  cc_pkg_shared_neg           PS4-Paket (Name mit Leerzeichen) und Retail-Paket im Hauptordner von usb0 (fuer PS4Mount bzw. andere Werkzeuge)
  cc_pkg_own_pos              Retail-Paket im Ordner Kraken-Games (dort bleibt es ein Befund)
  cc_backup_depth_neg         Spiel tief im Sicherungsordner PS5-Sicherung/Spiele (absichtlich ausser Reichweite von ShadowMount+)
  cc_payload_msg_neg          vier CE-108256-2 mit eigenem Text (Payload-Meldungen) von NPXS40087, kein Systemfehler
  cc_two_mount_log_neg        "not ours, or stale" in kraken.log, aber das Cooling Center verwaltet Kraken (Uebergabe der Ordner von ShadowMount+)
  cc_two_mount_dup_pos        dasselbe, aber der Titel liegt wirklich in Kraken-Games und bei ShadowMount+ (bleibt ein Befund)

Runde 2 (28. Nov. 2026): r2_* je Regel aus Packern/Konvertern, App-Datenbank-Werkzeugen, PS5_DevTools, APR Emu Updater, kstuff-a53, autokstuff, pgo_stub
(Quellen in docs/RULES.md, Nachtrag Runde 2).

Aufruf: python3 tools/gen_tools_fixture.py
"""
import json
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIX = os.path.join(ROOT, "tests", "fixtures")

NOW = 1795824000  # 2026-11-28 00:00 UTC (Erscheinen 0.6.0; die Uhr gilt erst ab einen Tag vor dem Erscheinen als plausibel)
PARAM = '{"titleId": "%s", "requiredSystemSoftwareVersion": "0x0900000000000000", "sdkVersion": "0x0900000000000000"}'
STORAGE = '"storage": [{"mount": "/data", "total": 900000000000, "free": 500000000000}]'


def put(path, data=b""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def make(name, expect, neg_for=(), procs=("shadowmountplus",), files=None, api=None, comps=None):
    dst = os.path.join(FIX, name)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)
    extra = ', "local_api": %s' % json.dumps(api) if api else ""
    if comps:
        extra += ', "components": %s' % json.dumps(comps)
    put(os.path.join(dst, "system.json"), '{"fw": "12.00", "model": "CFI-TEST", "now": %d, %s%s}' % (NOW, STORAGE, extra))
    put(os.path.join(dst, "procs.json"), "[" + ", ".join('"%s"' % p for p in procs) + "]")
    for rel, data in (files or {}).items():
        put(os.path.join(dst, "fs", rel), data)
    parts = ['"expect": [%s]' % ", ".join('"%s"' % r for r in expect)]
    if neg_for:
        parts.append('"neg_for": [%s]' % ", ".join('"%s"' % r for r in neg_for))
    put(os.path.join(dst, "expect.json"), "{" + ", ".join(parts) + "}\n")


def clone(src, name, expect, neg_for, files):
    # Kopie eines vorhandenen Fixtures mit zusaetzlichen Dateien und eigener Erwartung
    dst = os.path.join(FIX, name)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(FIX, src), dst)
    for rel, data in files.items():
        put(os.path.join(dst, "fs", rel), data)
    parts = ['"expect": [%s]' % ", ".join('"%s"' % r for r in expect)]
    if neg_for:
        parts.append('"neg_for": [%s]' % ", ".join('"%s"' % r for r in neg_for))
    put(os.path.join(dst, "expect.json"), "{" + ", ".join(parts) + "}\n")


def runtime_lib(text):
    # wie die echten Dateien: SELF-Kopf, darin die Kennung der Laufzeit (classify_lib sucht nur Texte)
    b = bytearray(b"\x4f\x15\x3d\x1d" + b"\0" * 60)
    b += text.encode("ascii") + b"\0" * 32
    return bytes(b)


def game(tid, lib):
    base = "data/homebrew/%s-Test/" % tid
    return {base + "eboot.bin": b"\x7fELF" + b"\0" * 124, base + "sce_sys/param.json": PARAM % tid, base + "fakelib/libSceAmpr.sprx": runtime_lib(lib)}


def p5z(complete):
    head = b"MAXPKP5Z" + (1).to_bytes(4, "little") + (64).to_bytes(4, "little") + b"\0" * 48
    body = b"\0" * 256
    foot = b"P5ZFOOT1" + b"\0" * 24 if complete else b"\0" * 32
    return head + body + foot


def main():
    wt, cr, ks, lf = "kraken.waiting_for_smp", "kraken.game_crashed", "kraken.kstuff_missing", "kraken.launch_failed"
    tr, sc, db, fo, pc, pt = "ampr.trace_problem", "ampr.stale_cache", "game.runtime_debug_build", "forge.old_version", "game.p5z_container", "game.p5z_truncated"
    log = "data/krakenmount/kraken.log"
    make("kraken_wait_pos", [wt], files={log: "[10:00:01] kraken-mount 0.4.2 starting\n[10:00:02] waiting before start-up: shadowmount\n"})
    make("kraken_wait_neg", [], [wt], files={log: "[10:00:02] waiting before start-up: shadowmount\n[10:00:30] shadowmount: ready; continuing start-up\n"})
    make("kraken_crash_pos", [cr], files={"data/krakenmount/crashes.json":
         '{"PPSA00071": {"at": %d, "process": "eboot.bin", "folder": "PPSA00071_1", "status": "killed by signal 11"}}' % (NOW - 2 * 86400)})
    make("kraken_crash_neg", [], [cr], files={"data/krakenmount/crashes.json":
         '{"PPSA00071": {"at": %d, "process": "eboot.bin", "folder": "PPSA00071_1", "status": "killed by signal 11"}}' % (NOW - 30 * 86400)})
    make("kraken_launch_pos", [ks, lf], files={log:
         "[10:01:00] PPSA00072 did not start (no game process): kstuff is not running (probe rax=0x0, carry=true)\n"
         "[10:02:00] PPSA00073 overlay mount FAILED errno=1\n"
         "[10:02:05] PPSA00073 overlay mount FAILED errno=1\n"})
    make("kraken_launch_neg", [], [ks, lf], files={log: "[10:00:03] kstuff probe: answered\n"})
    make("ampr_trace_pos", [tr], files={"data/ampr_emu_trace.txt": "build 0.5.1-debug\nrw on\nidx Invalid files=0\n"})
    make("ampr_trace_neg", [], [tr], files={"data/ampr_emu_trace.txt": "build 0.5.1-debug\nrw on\nidx Missing files=0\nidx Loaded files=1a\n"})
    make("ampr_cache_pos", [sc], files=dict(game("PPSA00074", "ampr-emu-ng 0.4.1"), **{"data/ampr_cache/0001.bin": "x"}))
    make("ampr_cache_neg", [], [sc], files=dict(game("PPSA00074", "ampr-emu-ng 0.5.1"), **{"data/ampr_cache/0001.bin": "x"}))
    make("ampr_debug_pos", [db], files=game("PPSA00075", "ampr-emu-ng 0.5.1 /data/ampr_emu_tune.txt"))
    make("ampr_debug_neg", [], [db], files=game("PPSA00075", "ampr-emu-ng 0.5.1"))
    make("forge_old_pos", [fo], files={"data/dumpforge/ps5-dump-forge.elf": b"\x7fELF" + b"\0" * 60 + b"ps5-dump-forge-version:0.5.5\0"})
    make("forge_old_neg", [], [fo], files={"data/dumpforge/ps5-dump-forge.elf": b"\x7fELF" + b"\0" * 60 + b"ps5-dump-forge-version:0.6.0\0"})
    make("p5z_pos", [pc, pt], files={"data/homebrew/Spiel.p5z": p5z(True), "data/homebrew/Ordner/Abbruch.p5z": p5z(False)})
    make("p5z_neg", [], [pc, pt], files={"data/homebrew/Kein.p5z": b"\0" * 200})
    ae, ad, al, am, ah = "kraken.api_game_error", "kraken.api_duplicate", "kraken.api_low_space", "kraken.api_ampr_missing", "kraken.api_ampr_hand"
    def state(games, low):
        return {"version": "0.4.2", "busy": False, "games": games, "cache": {"path": "/data/krakenmount/caches", "size": 1, "free": 2000000000 if low else 400000000000, "low_space": low}}
    bad = [{"title_id": "PPSA00081", "title_name": "Testspiel", "status": "error", "error": "cache build: short compressed block", "path": "/data/FPKG-Games/a.pkg"},
           {"title_id": "PPSA00082", "title_name": "Testspiel 2", "status": "duplicate", "error": "", "path": "/mnt/usb0/b.pkg"},
           {"title_id": "PPSA00083", "title_name": "Testspiel 3", "status": "ready", "error": "", "path": "/data/FPKG-Games/c.pkg"}]
    good = [bad[2]]
    make("kraken_api_pos", [ae, ad, al, am], api={"8097/api/state": state(bad, True), "8097/api/ampr": {"kind": "missing", "version": "", "updating": False}})
    make("kraken_api_hand_pos", [ah], api={"8097/api/state": state(good, False), "8097/api/ampr": {"kind": "hand", "version": "0.5.1", "updating": False}})
    make("kraken_api_neg", [], [ae, ad, al, am, ah], api={"8097/api/state": state(good, False), "8097/api/ampr": {"kind": "bundled", "version": "0.5.1", "updating": False}})
    # Fehlalarme mit PS5 Cooling Center 1.99
    nl = chr(10)
    make("cc_app0_neg", [], [lf], files={log: nl.join([
         "2026-11-28 12:00:38Z [346072] [launch] PPSA00091 overlay MOUNTED (sandbox, nmount 0 ms): /data/krakenmount/caches/PPSA00091/fakelib -> /mnt/sandbox/PPSA00091_000/x/common/lib",
         "2026-11-28 12:00:38Z [346077] [launch] PPSA00091 sandbox /app0 rw mount FAILED errno=11 (Resource deadlock avoided (os error 11)): /system_ex/app/PPSA00091 -> /mnt/sandbox/PPSA00091_000/app0", ""])})
    ku = "kraken.pkg_unsupported"
    ps4 = "mnt/usb0/CUSA00092 - Testspiel - game.pkg"
    make("cc_pkg_shared_neg", [], [ku], files={ps4: "CNT test", "mnt/usb0/PS5_PPSA00093_v1.00.pkg": "FIH test", log: nl.join([
         "2026-11-28 09:31:27Z [889175] /%s: not a PS5 package: a PS4 package (x7FCNT at offset 0)" % ps4,
         "2026-11-28 09:31:28Z [889585] /mnt/usb0/PS5_PPSA00093_v1.00.pkg: unsupported package: a retail package (signed byte 0x80, debug packages carry 0x00)", ""])})
    make("cc_pkg_own_pos", [ku], files={"mnt/usb0/Kraken-Games/PS5_PPSA00094_v1.00.pkg": "FIH test", log: nl.join([
         "2026-11-28 09:31:28Z [889585] /mnt/usb0/Kraken-Games/PS5_PPSA00094_v1.00.pkg: unsupported package: a retail package (signed byte 0x80, debug packages carry 0x00)", ""])})
    make("cc_backup_depth_neg", [], ["smp.scan_depth"], procs=("etaHEN", "kstuff", "shadowmountplus"), files={
         "data/shadowmount/config.ini": "debug=1" + nl, "data/homebrew/PS5-Sicherung/Spiele/PPSA00095-app/sce_sys/param.json": '{"titleId": "PPSA00095"}'})
    hist = "system_data/priv/error/history/%08d.json"
    msgs = ["Welcome to Testpayload", "Testpayload: DNS set", "Testpayload: patch applied", "Testpayload: no games found"]
    make("cc_payload_msg_neg", [], ["errors.recurring_system_app"], files={hist % i: json.dumps(
         {"internalErrorCode": -2135293939, "messageBuffer": t, "shortError": "CE-108256-2", "titleId": "NPXS40087", "tick": 63931317706412000 + i * 1000000, "natType": 2})
         for i, t in enumerate(msgs)})
    tm = "games.two_mounters"
    zustand = "data/PS5-Cooling-Center/kraken/zustand.txt"
    stale = "[12:00:01] scan: PPSA00096 /system_ex/app/PPSA00096 is mounted but not ours, or stale; unmounting it" + nl
    make("cc_two_mount_log_neg", [], [tm], procs=(), files={log: stale, zustand: "an" + nl})
    make("cc_two_mount_dup_pos", [tm], procs=("shadowmountplus",), files={log: stale, zustand: "an" + nl,
         "data/Kraken-Games/PPSA00097-Spiel/sce_sys/param.json": '{"titleId": "PPSA00097"}',
         "data/homebrew/PPSA00097-Spiel/sce_sys/param.json": '{"titleId": "PPSA00097"}'})
    # Runde 2 (28. Nov. 2026): Packer/Konverter, App-Datenbank-Werkzeuge, DevTools/APR/kstuff
    hb = "data/homebrew/"
    smp17 = {"smp": {"version": "1.7beta12"}}
    smp16 = {"smp": {"version": "1.6beta16"}}
    make("r2_gc_journal_pos", ["gc.destructive_interrupted"], files={hb + ".Spiel.exfat.gc-compress.tmp.stream-journal": "GCSTRM2"})
    make("r2_gc_hidden_pos", ["gc.hidden_original"], files={hb + ".gc-hide-move-PPSA00102-ab12-01": "x", hb + "Spiel.exfat.original": "x"})
    make("r2_gc_hidden_neg", [], ["gc.hidden_original", "gc.destructive_interrupted"], files={hb + "notes.original": "x", hb + ".versteckt": "x"})
    vdir = "data/GameCompressor/validations/"
    make("r2_gc_bad_pos", ["gc.bad_blocks"], files={vdir + "PPSA00103.json": '{"status": "bad-blocks-found"}'})
    make("r2_gc_bad_neg", [], ["gc.bad_blocks"], files={vdir + "PPSA00103.json": '{"status": "compression-stats"}'})
    gclog = {"data/GameCompressor/logs/log.txt": "[10:00:00] ShadowMountPlus did not remount PPSA00104; mount.lnk=missing" + nl}
    make("r2_gc_mount_pos", ["gc.smp17_remount_failed"], files=gclog, comps=smp17)
    make("r2_gc_mount_neg", [], ["gc.smp17_remount_failed"], files=gclog, comps=smp16)
    make("r2_lz4_inc_pos", ["game.lz4_incomplete_output"], files={hb + "PPSA00105-app_AMPR/INCOMPLETE.txt": "Packing failed or cancelled.", hb + "PPSA00105-app_AMPR/ampr_assets.index": "AMPRPAK4"})
    make("r2_lz4_unv_pos", ["game.lz4_unverified"], files={hb + "PPSA00105-app_AMPR/UNVERIFIED.txt": "x", hb + "PPSA00105-app_AMPR/.ampr-owned": "AMPRStudio"})
    make("r2_lz4_neg", [], ["game.lz4_incomplete_output", "game.lz4_unverified"], files={hb + "Notizen/INCOMPLETE.txt": "x", hb + "Notizen/UNVERIFIED.txt": "x"})
    make("r2_e2a_pos", ["game.e2a_unfinished", "ampr.trace_mode_left_on"], files={hb + "PPSA00106/.e2a-state/state.bin": "x", hb + "PPSA00106/fakelib2/.e2a-trace": ""})
    e2alog = "data/exfat2ampr/exfat2ampr.log"
    make("r2_e2a_log_pos", ["e2a.launch_check_failed"], files={e2alog: "launch check /data/homebrew/PPSA00107: would not start: no_emulator, backport_ampr" + nl, hb + "PPSA00107/x.bin": "x"})
    make("r2_e2a_log_neg", [], ["e2a.launch_check_failed"], files={e2alog: "launch check /data/homebrew/PPSA00199: would not start: no_emulator" + nl})
    make("r2_alembic_pos", ["game.alembic_resumable"], files={"data/alembic/resume/0011223344556677.json": '{"partial": "/data/homebrew/Aus.exfat.alembic-partial", "state": "paused"}',
         hb + "Aus.exfat.alembic-partial": "x"})
    make("r2_alembic_neg", [], ["game.alembic_resumable"], files={"data/alembic/resume/0011223344556677.json": '{"partial": "/data/homebrew/Weg.exfat.alembic-partial"}'})
    make("r2_tabshifter_pos", ["tools.tabshifter_used", "tools.tabshifter_old"], files={"data/tabshifter/tile_installed": "", "data/payloads/TabShifter.elf": "ELF test PS5 TabShifter v1.3.0 Ready!"})
    make("r2_tabshifter_neg", [], ["tools.tabshifter_used", "tools.tabshifter_old"], files={"data/payloads/TabShifter.elf": "ELF test PS5 TabShifter v1.4.0 Ready!"})
    make("r2_homehider_pos", ["tools.homehider_used", "tools.homehider_folder_tile"], files={"data/homehider/state.db": "x", "user/app/HHFD00001/sce_sys/param.json": '{"titleId": "HHFD00001"}'})
    ytlog = "data/appinfo-block.log"
    make("r2_youtube_pos", ["tools.youtube_update_blocked"], files={ytlog: "done: YouTube updates blocked. Reboot the console now." + nl, "data/appinfo.db.orig": "SQLite format 3"})
    make("r2_youtube_err_pos", ["tools.youtube_blocker_write_failed"], files={ytlog: "ERROR: cannot write /system_data/priv/mms/appinfo.db" + nl})
    make("r2_youtube_err_neg", ["tools.youtube_update_blocked"], ["tools.youtube_blocker_write_failed"], files={ytlog: "ERROR: cannot write /system_data/priv/mms/appinfo.db" + nl + "already blocked, nothing to do" + nl})
    crash = '{"format": 1, "title_id": "PPSA00108", "signal_name": "SIGSEGV", "where": "libSceAmpr.sprx+0x10 (called from)", "outcome": "crashed"}'
    make("r2_devtools_pos", ["devtools.title_crashed", "devtools.crash_in_runtime", "devtools.attach_failed"], files={"data/devtools/crashes/20261125-101010-PPSA00108-123.json": crash,
         "data/devtools/devtools.log": "attach PPSA00109 pid 55 failed: errno 16" + nl})
    make("r2_devtools_neg", [], ["devtools.title_crashed", "devtools.crash_in_runtime", "devtools.attach_failed"], files={
         "data/devtools/crashes/20261001-101010-PPSA00108-123.json": crash, "data/devtools/crashes/20261125-101010-PPSA00109-124.json": crash.replace('"crashed"', '"handled"')})
    make("r2_apr_pos", ["apr.updater_present"], files={"data/apr-emu-updater/apr-emu/builds.json": "{}"})
    ks = "data/kstuff-startup.log"
    make("r2_a53_pos", ["kstuff.a53_ppr_failed"], files={ks: nl.join(["[0.0] start: 0 ms", "[0.1] A53 init: 5 ms", "[0.9] A53/PPR total: 900 ms", ""])})
    make("r2_a53_neg", [], ["kstuff.a53_ppr_failed"], files={ks: nl.join(["[0.0] start: 0 ms", "[0.9] A53/PPR total: 900 ms", "",
         "[0.0] start: 0 ms", "[0.9] A53/PPR total: 800 ms", "[1.0] A53/PPR and loader: 950 ms", "[2.0] total to kstuff ready: 2000 ms", ""])})
    make("r2_autokstuff_pos", ["game.autokstuff_delay_invalid"], files={hb + "PPSA00110-Test/autokstuff": b"\xef\xbb\xbf30", "data/autokstuff/PPSA00111": "0"})
    make("r2_autokstuff_neg", [], ["game.autokstuff_delay_invalid"], files={hb + "PPSA00110-Test/autokstuff": "30" + nl, "data/autokstuff/PPSA00111": " 45"})
    make("r2_playgo_pos", ["game.playgo_stub_log_build", "game.playgo_stub_dat_example"], files={hb + "PPSA00112-Test/playlgo.log": "x", hb + "PPSA00112-Test/playgo_stub.dat": "80 <---- number of chunks" + nl})
    make("r2_playgo_neg", [], ["game.playgo_stub_log_build", "game.playgo_stub_dat_example"], files={hb + "PPSA00112-Test/playgo_stub.dat": "80" + nl + "1" + nl})
    # Medic-Fehler: PlayGo-Stub im Backport-Ordner zaehlt (Kopie von gh14_pos), Emulator-Updates mit APR Emu Updater (Kopie von gi5_pos)
    clone("gh14_pos", "r2_playgo_backport_neg", [], ["game.playgo_stub_missing"], {"data/homebrew/backports/PPSA00094/fakelib/libScePlayGo.sprx": "x"})
    clone("gi5_pos", "r2_apr_emu_pos", ["smp.emulator_update_apr_updater", "apr.updater_present"], ["smp.emulator_update_replaces_runtime"], {"data/apr-emu-updater/state.json": "{}"})
    clone("gi5_pos", "r2_apr_emu_neg", ["smp.emulator_update_replaces_runtime"], ["smp.emulator_update_apr_updater", "apr.updater_present"], {"data/apr-emu-updater.txt": "kein Ordner"})
    # Gegenstuecke: aehnliche Namen, aber nicht die Spur des Werkzeugs
    make("r2_tools_neg", [], ["game.e2a_unfinished", "ampr.trace_mode_left_on", "tools.homehider_used", "tools.homehider_folder_tile", "tools.youtube_update_blocked",
                              "tools.youtube_blocker_write_failed", "apr.updater_present"], files={
         hb + "PPSA00106/e2a-state/x": "x", hb + "PPSA00106/fakelib2/.trace": "x", "data/homehider/settings.txt": "x", "user/app/HHFD00002/icon0.png": "x",
         ytlog: "ERROR: patch failed: no YouTube row" + nl, "data/apr-emu-updater.txt": "x"})
    print("ok")


if __name__ == "__main__":
    main()
