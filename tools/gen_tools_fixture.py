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

Aufruf: python3 tools/gen_tools_fixture.py
"""
import json
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIX = os.path.join(ROOT, "tests", "fixtures")

NOW = 1795651200  # 2026-11-26 00:00 UTC
PARAM = '{"titleId": "%s", "requiredSystemSoftwareVersion": "0x0900000000000000", "sdkVersion": "0x0900000000000000"}'
STORAGE = '"storage": [{"mount": "/data", "total": 900000000000, "free": 500000000000}]'


def put(path, data=b""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def make(name, expect, neg_for=(), procs=("shadowmountplus",), files=None, api=None):
    dst = os.path.join(FIX, name)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)
    extra = ', "local_api": %s' % json.dumps(api) if api else ""
    put(os.path.join(dst, "system.json"), '{"fw": "12.00", "model": "CFI-TEST", "now": %d, %s%s}' % (NOW, STORAGE, extra))
    put(os.path.join(dst, "procs.json"), "[" + ", ".join('"%s"' % p for p in procs) + "]")
    for rel, data in (files or {}).items():
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
    print("ok")


if __name__ == "__main__":
    main()
