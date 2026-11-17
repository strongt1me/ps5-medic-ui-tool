#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Erzeugt die Bilder fuer die README (docs/images/<sprache>/) aus einem SYNTHETISCHEN Demo-Snapshot: kein Bild zeigt eine echte Konsole,
# echte Spielnamen oder Kennungen. Der Snapshot entsteht aus den Fixtures unter tests/fixtures/ (mehrere Befunde, eine reparierbare Konfiguration,
# zwei gespeicherte Check-ups und eine Reparatur im Journal fuer den Vergleich).
#
# Aufruf (aus dem Projektordner, nach `make host`):  bash tools/make_screenshots.sh
# Braucht build/medic-ui-preview und build/medic-cli (make host), python3.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

PREVIEW=build/medic-ui-preview
CLI=build/medic-cli
RULES=knowledge/rules.json
mkdir -p build
# frische Arbeitsordner je Lauf (nichts wird geloescht; build/ ist nicht im Repository)
DEMO=$(mktemp -d build/demo-snapshot.XXXXXX)
SHOTS=$(mktemp -d build/screens.XXXXXX)
[[ -x $PREVIEW && -x $CLI ]] || { echo "Zuerst: make host" >&2; exit 1; }

# 1. Demo-Snapshot: Grundlage mit Spielen, darauf weitere Positiv-Fixtures (jede Datei nur, wenn sie noch nicht da ist)
cp -r tests/fixtures/g1_pos_image_ebootnew/. "$DEMO"/
for f in b13_pos_v2 smp7_pos_busy gi1_pos gi2_pos gi4_pos gi5_pos gi9_pos t1_pos_above_usual sys_pos_temp s2_pos_thermal_ceiling e2_pos \
         hen_pos_crashed k1_pos_kstuff smp10_pos_leak al1_pos_loader gk_pos gp_pos a1_pos b5_pos d2_pos n1_pos_packet_loss gr_pos gs_pos gu_pos \
         ap1_pos_orphan elf_pos_tiny dns_pos_upstream; do
    [[ -d tests/fixtures/$f ]] && cp -rn tests/fixtures/$f/. "$DEMO"/
done

# 2. Zwei gespeicherte Check-ups und eine Reparatur im Journal (nur Regel-IDs und Zeiten, alles erfunden)
"$CLI" check "$DEMO" --rules "$RULES" --json > "$DEMO/.now.json"
python3 - "$DEMO" "$RULES" <<'PY'
import json, os, sys
demo, rules_path = sys.argv[1], sys.argv[2]
now = json.load(open(os.path.join(demo, '.now.json'), encoding='utf-8'))['findings']
os.remove(os.path.join(demo, '.now.json'))
rules = {r['id']: r for r in json.load(open(rules_path, encoding='utf-8'))['rules']}
present = [f['id'] for f in now]
fixed_id = next(i for i, r in rules.items() if r.get('fix') and r['fix'] not in ('fix_image_nesting_hint',) and i not in present)
gone_id = next(i for i, r in rules.items() if not r.get('fix') and i not in present and r['severity'] == 'info')

def stored(ts, ids, sev_override, setup, resolved):
    fs = []
    for i in ids:
        r = rules[i]
        fs.append({'rule_id': i, 'severity': sev_override.get(i, r['severity']), 'text_de': '', 'text_en': '', 'evidence': [], 'fix': '', 'status': '', 'seen_runs': 1})
    return {'version': 1, 'ts': ts, 'fw': '12.00', 'model': 'CFI-DEMO', 'rules_version': '2026.11.55', 'findings': fs, 'coverage': [], 'resolved': resolved, 'metrics': [], 'setup': setup}

old_ids = [i for i in present if i not in ('game.ng_index_too_big', 'nanodns.log_problem', 'smp.launch_busy')] + [fixed_id, gone_id]
changed = 'game.needs_backport' if 'game.needs_backport' in present else present[0]
older_sev = {changed: 'info'}
d = os.path.join(demo, 'fs', 'data', 'ps5medic', 'checks')
os.makedirs(d, exist_ok=True)
json.dump(stored('20261114T180000Z', old_ids, older_sev, {'smp.version': '1.7beta11', 'games.count': '8'}, []), open(os.path.join(d, '20261114T180000Z.json'), 'w'))
json.dump(stored('20261116T180000Z', present, {}, {'smp.version': '1.7beta12', 'games.count': '9'}, [fixed_id]), open(os.path.join(d, '20261116T180000Z.json'), 'w'))
journal = [{'type': 'commit', 'txn': '20261115T120000Z', 'ts': '20261115T120000Z', 'fix': rules[fixed_id]['fix'], 'rule': fixed_id, 'ops': 1}]
open(os.path.join(demo, 'fs', 'data', 'ps5medic', 'journal.jsonl'), 'w').write(''.join(json.dumps(x) + chr(10) for x in journal))
PY

# 3. Vorschau je Sprache, die ausgewaehlten Bilder nach docs/images/<sprache>/
PICK="01_start 03_findings 04_detail_0 03b_report 06d_compare 07_settings 10_error_codes 17_selftest_list 21_game_list 22_game_detail 24_repairs 25_maintenance 30b_check_list_open"
for lang in de en fr it es; do
    "$PREVIEW" "$DEMO" "$SHOTS/$lang" --lang "$lang" --scale "${SCALE:-2}" > /dev/null
    mkdir -p "docs/images/$lang"
    for n in $PICK; do cp "$SHOTS/$lang/$n.png" "docs/images/$lang/$n.png"; done
    fix=$(ls "$SHOTS/$lang" | grep -E '^05_fix_[0-9]+\.png$' | head -1 || true)
    [[ -n $fix ]] && cp "$SHOTS/$lang/$fix" "docs/images/$lang/05_fix_preview.png"
    echo "$lang: $(ls docs/images/$lang | wc -l) Bilder"
done
