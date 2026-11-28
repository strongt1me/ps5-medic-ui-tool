#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Erzeugt die synthetischen Fixtures zu Luefterreglern, Konsolenuhr und verwaisten Inhalten unter tests/fixtures/ (Aufbau: siehe collect_fan_controller,
collect_clock und collect_content_leftovers in libmedic/src). Nur erfundene Titel-IDs und Namen.

  fan_double_pos      Breeze und fan_target laufen: Regel fan.double_controller
  fan_single_neg      nur Breeze (mit Hilfsprozess) laeuft, ps5-fan-control und das Cooling Center sind nur installiert: kein Doppelregler
  clock_pos           Konsolenuhr im Jahr 2000: Regel sys.clock_implausible
  clock_neg           Konsolenuhr fuenf Tage nach dem Erscheinen der Version: keine Regel
  content_all_pos     Zusatzinhalt und Update ohne Spiel, Lizenz ohne Spiel, ein unerledigter Auftrag: die drei content.*-Regeln
  content_neg         Zusatzinhalt mit installiertem Spiel, Zusatzinhalt mit Kraken-Cache, Lizenz mit installiertem Spiel, bgft/task nur mit einer Datei

Aufruf: python3 tools/gen_content_fixture.py
"""
import os
import shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIX = os.path.join(ROOT, "tests", "fixtures")

PARAM = '{"titleId": "%s", "requiredSystemSoftwareVersion": "0x0900000000000000", "sdkVersion": "0x0900000000000000"}'
RELEASE_PLUS_5_DAYS = 1795737600  # 2026-11-27 00:00 UTC (Name historisch; muss mindestens einen Tag vor dem Erscheinen dieser Version liegen, 0.6.0: 2026-11-28)


def put(path, data=b""):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def make(name, expect, neg_for=(), procs=(), system=None, files=None):
    dst = os.path.join(FIX, name)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    os.makedirs(dst)
    sys_json = '{"fw": "12.00", "model": "CFI-TEST"%s}' % (", " + system if system else "")
    put(os.path.join(dst, "system.json"), sys_json)
    put(os.path.join(dst, "procs.json"), "[" + ", ".join('"%s"' % p for p in procs) + "]")
    for rel, data in (files or {}).items():
        put(os.path.join(dst, "fs", rel), data)
    parts = ['"expect": [%s]' % ", ".join('"%s"' % r for r in expect)]
    if neg_for:
        parts.append('"neg_for": [%s]' % ", ".join('"%s"' % r for r in neg_for))
    put(os.path.join(dst, "expect.json"), "{" + ", ".join(parts) + "}\n")


def main():
    dbl, ck, dlc, lic, task = "fan.double_controller", "sys.clock_implausible", "content.dlc_without_game", "content.license_without_game", "content.download_task_pending"
    make("fan_double_pos", [dbl], procs=["breeze", "fan_target", "kstuff"])
    make("fan_single_neg", [], [dbl], procs=["breeze", "breeze-guard", "kstuff"],
         files={"data/fan_control.ini": "", "data/PS5-Cooling-Center/config.json": '{"mode": "automatic", "target_temp_c": 68}'})
    make("clock_pos", [ck], [], system='"now": 946684800')
    make("clock_neg", [], [ck], system='"now": %d' % RELEASE_PLUS_5_DAYS)
    make("content_all_pos", [dlc, lic, task], files={
        "user/app/PPSA00001/sce_sys/param.json": PARAM % "PPSA00001",
        "user/addcont/PPSA00002/medic_entitlement.dat": "x",   # Zusatzinhalt ohne Spiel
        "user/patch/PPSA00003/medic_patch.dat": "x",           # Update ohne Spiel
        "user/license/EP9000-PPSA00009_00.rif": "",            # Lizenz ohne Spiel (nur der Dateiname zaehlt)
        "user/bgft/task/4242/medic_task.dat": "x",             # ein Auftrag
    })
    make("content_neg", [], [dlc, lic, task], files={
        "user/app/PPSA00001/sce_sys/param.json": PARAM % "PPSA00001",
        "user/addcont/PPSA00001/medic_entitlement.dat": "x",    # Spiel installiert
        "user/addcont/PPSA00004/medic_entitlement.dat": "x",    # Spiel nicht installiert, aber als Kraken-Cache bekannt
        "data/krakenmount/caches/PPSA00004/.done": "{}",
        "user/license/EP9000-PPSA00001_00.rif": "",             # Lizenz mit installiertem Spiel
        "user/bgft/task/medic_note.txt": "x",                   # nur eine Datei, kein Auftragsordner
    })
    print("ok")


if __name__ == "__main__":
    main()
