# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests fuer tools/triage_report.py (nur Standardbibliothek): python3 tests/test_triage.py"""
import json
import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import triage_report as t  # noqa: E402


def read(name):
    with open(os.path.join(ROOT, "tests", "triage", name), encoding="utf-8") as f:
        return f.read()


class Triage(unittest.TestCase):
    def test_parse_de(self):
        p = t.parse(read("report_de.md"))
        self.assertTrue(p["valid"])
        self.assertEqual((p["firmware"], p["model"], p["rules_version"]), ("12.00", "CFI-7021", "2026.11.36"))
        self.assertEqual([f["rule_id"] for f in p["findings"]], ["game.needs_backport", "smp.duplicate_titleid"])
        self.assertEqual([f["severity"] for f in p["findings"]], ["warning", "info"])
        self.assertEqual(p["error_code"], "CE-108262-9")
        self.assertEqual(len(p["unknown_lines"]), 2)
        self.assertEqual(p["unknown_lines"][0]["count"], 3)
        self.assertEqual(len(p["not_checked"]), 1)

    def test_fingerprint_ignores_numbers_times_paths_ids(self):
        a = t.fingerprint("[2026-11-11 21:04:33] mount failed for PPSA00001: errno 22 at /mnt/usb0/Spiel/image.exfat")
        b = t.fingerprint("[2026-11-12 08:00:01] mount failed for PPSA00099: errno 5 at /mnt/usb1/Other/disc.exfat")
        self.assertEqual(a, b)
        self.assertNotEqual(a, t.fingerprint("register failed for PPSA00001"))

    def test_comment_is_safe_and_labelled(self):
        c = t.comment(read("report_de.md"))
        self.assertNotIn("<script>", c)
        self.assertNotIn("@alle", c)  # Erwaehnungen sind entschaerft
        self.assertIn("triage:label=bericht,neue-zeilen", c)
        self.assertIn("2 Hinweis" if False else "Befunde: 0 kritisch, 1 Warnung, 1 Hinweis", c)

    def test_not_a_report(self):
        c = t.comment("Mein Spiel startet nicht, bitte helft mir")
        self.assertIn("triage:label=ohne-bericht", c)
        self.assertFalse(t.parse("")["valid"])

    def test_clean_report_gets_label(self):
        c = t.comment("# PS5 Medic – Report\n\n- PS5 Medic 0.4.0-dev, rules 2026.11.36 (76)\n\n## Findings (0)\n\nNo findings.\n")
        self.assertIn("sauber", c)

    def test_cluster_and_draft(self):
        issues = [{"number": 1, "body": read("report_de.md")}, {"number": 2, "body": read("report_en2.md")}, {"number": 3, "body": "kein Bericht"}]
        fp = t.fingerprint("[2026-11-11 21:04:33] mount failed for PPSA00001: errno 22 at /mnt/usb0/Spiel/image.exfat")
        groups = t.cluster(issues)
        self.assertEqual(groups[fp]["issues"], {1, 2})
        self.assertEqual(groups[fp]["firmware"], {"12.00", "13.60"})
        md = t.cluster_markdown(issues)
        self.assertIn(fp, md)
        d = t.draft(issues, fp)
        self.assertEqual(d["id"], "community." + fp)
        self.assertEqual(d["verified"], "todo")
        self.assertIsNone(t.draft(issues, "00000000"))
        json.dumps(d)  # serialisierbar

    def test_limits(self):
        big = "# PS5 Medic – Bericht\n\n- PS5 Medic 0.4.0-dev, Regelbasis 1 (1)\n\n## Unbekannte Fehlerzeilen (9)\n\n" + "".join("- 1× `zeile %d x`\n" % i for i in range(1000))
        self.assertLessEqual(len(t.parse(big)["unknown_lines"]), t.MAX_LINES)

    def test_env_input(self):
        os.environ["TRIAGE_BODY"] = read("report_en2.md")
        self.assertTrue(t.parse(t.read_input("-"))["valid"])


if __name__ == "__main__":
    unittest.main()
