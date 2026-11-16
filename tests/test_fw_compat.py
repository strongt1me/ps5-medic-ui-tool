# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests fuer tools/fw_compat.py (nur Standardbibliothek): python3 tests/test_fw_compat.py"""
import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import fw_compat as fc  # noqa: E402


class Nid(unittest.TestCase):
    def test_bekannte_nids(self):
        # aus der Sony-Bibliotheksdokumentation bekannt; schlaegt die Byte-Reihenfolge oder das Salz falsch an, stimmt keine der beiden
        self.assertEqual(fc.nid("sceKernelUsleep"), "1jfXLRVzisc")
        self.assertEqual(fc.nid("scePadReadState"), "YndgXqQVV7c")

    def test_firmware_sortierung(self):
        self.assertEqual(sorted(["12.00", "4.03", "10.01", "4.00"], key=fc.fw_key), ["4.00", "4.03", "10.01", "12.00"])


class Auswertung(unittest.TestCase):
    def setUp(self):
        a, b, c = fc.nid("funcA"), fc.nid("funcB"), fc.nid("fromLoader")
        # 12.00 kennt A und B (der Massstab), 11.00 nur A, 10.00 hat keine lesbare Bibliothek, "fromLoader" kommt aus keiner Bibliothek
        self.exports = {"12.00": {"libx": {a, b}}, "11.00": {"libx": {a}}, "10.00": {}}
        self.names = ["funcA", "funcB", "fromLoader"]

    def test_fehlende_funktion_wird_gemeldet(self):
        res = fc.evaluate(self.names, {"libx"}, self.exports, ["10.00", "11.00", "12.00"], "12.00")
        self.assertEqual(res["12.00"], ("ok", []))
        self.assertEqual(res["11.00"], ("fehlt", ["funcB"]))
        self.assertEqual(res["10.00"], ("unlesbar", []))

    def test_was_der_massstab_nicht_aufloest_zaehlt_nicht(self):
        res = fc.evaluate(["fromLoader"], {"libx"}, self.exports, ["11.00", "12.00"], "12.00")
        self.assertEqual(res["11.00"], ("ok", []))


class Aufruf(unittest.TestCase):
    def test_eingabefehler(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(fc.main(["--fw-root", d, "--parser", os.path.join(d, "fehlt.py")]), 2)
            self.assertEqual(fc.main(["--fw-root", d, "--parser", os.path.join(d, "fehlt.py"), "--native-imports", os.path.join(d, "n.txt"), "--native-libs", "libx"]), 2)


if __name__ == "__main__":
    unittest.main()
