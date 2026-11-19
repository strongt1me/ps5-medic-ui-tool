// SPDX-License-Identifier: GPL-3.0-or-later
// Tests fuer den Verlauf der Check-ups (libmedic/src/history.cpp): Vergleich mit der letzten Pruefung, Dringlichkeit, Schreiben, Beiseitelegen.
#include "check.hpp"
#include "testutil.hpp"
#include "medic/history.hpp"
#include "medic/platform.hpp"
#include "medic/report.hpp"
#include "medic/rules.hpp"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace medic;

namespace {

using namespace testutil;

Finding mk(const std::string& id, Severity s) {
    Finding f;
    f.rule_id = id;
    f.severity = s;
    f.verified = "doc";
    return f;
}

HistoryRun run(const std::string& ts, std::initializer_list<const char*> ids) {
    HistoryRun r;
    r.ts = ts;
    r.rules_version = "t";
    r.fw = "12.00";
    for (const char* id : ids) r.findings.emplace_back(id, "warning");
    return r;
}

}  // namespace

void history_tests() {
    {  // Vergleich mit der letzten Pruefung
        std::vector<Finding> now = {mk("a.x", Severity::Warning), mk("c.new", Severity::Info)};
        CHECK(annotate_with_history(now, {}, "T9").empty() && now[0].status.empty() && now[0].seen_runs == 0, "Verlauf: ohne fruehere Pruefung kein Vergleich");

        const std::vector<HistoryRun> h = {run("T1", {"a.x", "b.gone"}), run("T2", {"a.x"}), run("T3", {"a.x", "b.gone"})};
        now = {mk("a.x", Severity::Warning), mk("c.new", Severity::Info)};
        const auto resolved = annotate_with_history(now, h, "T4");
        CHECK(now[0].status == "recurring" && now[0].first_seen == "T1" && now[0].seen_runs == 4, "Verlauf: a.x seit T1, vier Pruefungen in Folge");
        CHECK(now[1].status == "new" && now[1].first_seen == "T4" && now[1].seen_runs == 1, "Verlauf: c.new ist neu");
        CHECK(resolved.size() == 1 && resolved[0] == "b.gone", "Verlauf: b.gone ist seit der letzten Pruefung behoben");

        // Eine Luecke unterbricht die Folge: b.gone fehlte in T2, die Folge beginnt wieder bei T3
        std::vector<Finding> again = {mk("b.gone", Severity::Warning)};
        annotate_with_history(again, h, "T4");
        CHECK(again[0].status == "recurring" && again[0].first_seen == "T3" && again[0].seen_runs == 2, "Verlauf: eine Luecke beendet die Folge");
    }
    {  // Dringlichkeit
        Finding f = mk("a", Severity::Critical);
        CHECK(urgency_of(f) == "high" && urgency_of(mk("a", Severity::Warning)) == "medium" && urgency_of(mk("a", Severity::Info)) == "low", "Dringlichkeit: nach Schweregrad");
        Finding rec = mk("a", Severity::Info);
        rec.status = "recurring";
        rec.seen_runs = 2;
        CHECK(urgency_of(rec) == "low", "Dringlichkeit: zwei Pruefungen in Folge reichen nicht");
        rec.seen_runs = 3;
        CHECK(urgency_of(rec) == "medium", "Dringlichkeit: ab drei Pruefungen in Folge eine Stufe hoeher");
        rec.confidence = {"low", "unknown", "", ""};
        CHECK(urgency_of(rec) == "low", "Dringlichkeit: bei niedriger Verlaesslichkeit keine Anhebung");
        Finding crit = mk("a", Severity::Critical);
        crit.status = "recurring";
        crit.seen_runs = 9;
        CHECK(urgency_of(crit) == "high", "Dringlichkeit: nie ueber 'hoch'");
    }
    {  // Lesen ist tolerant, Schreiben haengt an
        Sandbox sb;
        spit(sb.real(kHistoryPath), "kein json\n{\"ts\":\"T1\",\"findings\":[{\"id\":\"a.x\",\"sev\":\"info\"}]}\n{\"ts\":5}\n\n{\"ts\":\"T2\",\"findings\":[]}\n");
        auto ro = make_snapshot_platform(sb.dir.string(), false);
        const auto h = load_history(ro->fs());
        CHECK(h.size() == 2 && h[0].ts == "T1" && h[0].findings.size() == 1 && h[1].ts == "T2", "Verlauf: kaputte Zeilen werden uebersprungen");
        CHECK(!append_history(*ro, run("T3", {"a.x"})), "Verlauf: ohne Schreibzugriff wird nichts geschrieben");

        auto rw = make_snapshot_platform(sb.dir.string(), true);
        CHECK(append_history(*rw, run("20261111T100000Z", {"a.x", "b.y"})), "Verlauf: Pruefung angehaengt");
        CHECK(!append_history(*rw, run("", {})) && !append_history(*rw, run("../x", {})), "Verlauf: ungueltiger Zeitstempel wird abgelehnt");
        const auto h2 = load_history(rw->fs());
        CHECK(h2.size() == 3 && h2.back().ts == "20261111T100000Z" && h2.back().findings.size() == 2 && h2.back().rules_version == "t", "Verlauf: gelesen ergibt es drei Pruefungen");
    }
    {  // Ueber der Groessengrenze wird beiseite gelegt, nie geloescht
        Sandbox sb;
        std::string big;
        const std::string pad(900, 'x');
        for (int i = 0; i < 400; ++i) big += "{\"ts\":\"T" + std::to_string(i) + "\",\"rules\":\"" + pad + "\",\"findings\":[{\"id\":\"a.x\",\"sev\":\"info\"}]}\n";
        CHECK(big.size() > kHistoryMaxBytes, "Verlauf: Testdatei ist gross genug");
        spit(sb.real(kHistoryPath), big);
        auto rw = make_snapshot_platform(sb.dir.string(), true);
        CHECK(append_history(*rw, run("20261111T110000Z", {"a.x"})), "Verlauf: Anhaengen ueber der Grenze");
        const auto h = load_history(rw->fs());
        CHECK(h.size() == kHistoryKeepRuns + 1 && h.back().ts == "20261111T110000Z" && h.front().ts == "T" + std::to_string(400 - static_cast<int>(kHistoryKeepRuns)), "Verlauf: neue Datei beginnt mit den juengsten Pruefungen");
        CHECK(slurp(sb.real("/data/ps5medic/history_20261111T110000Z.jsonl")) == big, "Verlauf: die alte Datei liegt unveraendert daneben");
    }
    {  // Messwerte: Veraenderung seit der letzten Pruefung und uebliches Niveau (Median)
        const RuleSet rs = load_rules_from_string(R"({"schema_version":1,"db_version":"t","rules":[],"metrics":[{"id":"cpu","fact":"sys.temp.cpu_c","unit":"°C","text":{"de":"CPU","en":"CPU"}}]})");
        auto with = [](const char* ts, double v) {
            HistoryRun r = run(ts, {});
            r.metrics["cpu"] = v;
            return r;
        };
        auto c0 = metric_changes(rs, {{"cpu", 50}}, {});
        CHECK(c0.size() == 1 && !c0[0].has_prev && !c0[0].has_typical && c0[0].now == 50, "Messwerte: ohne Verlauf nur der jetzige Wert");
        CHECK(metric_changes(rs, {{"unbekannt", 1}}, {}).empty(), "Messwerte: unbekannte id wird ignoriert");
        const std::vector<HistoryRun> h = {with("T1", 40), with("T2", 60), run("T3", {}), with("T4", 44), with("T5", 46)};
        const auto c = metric_changes(rs, {{"cpu", 70}}, h);
        CHECK(c.size() == 1 && c[0].has_prev && c[0].prev == 46 && c[0].has_typical && c[0].typical == 45 && c[0].samples == 4 && c[0].unit == "°C",
              "Messwerte: letzte Pruefung mit Wert und Median (Pruefung ohne Wert zaehlt nicht)");
        Sandbox sb;
        auto rw = make_snapshot_platform(sb.dir.string(), true);
        CHECK(append_history(*rw, make_history_run("20261111T100000Z", "t", "12.00", {}, {{"cpu", 41.5}})), "Messwerte: angehaengt");
        const auto back = load_history(rw->fs());
        CHECK(back.size() == 1 && back[0].metrics.count("cpu") && back[0].metrics.at("cpu") == 41.5, "Messwerte: aus der Datei gelesen");
    }
    {  // vorlaeufig -> bestaetigt
        Finding f = mk("a", Severity::Info);
        f.confirm_runs = 2;
        CHECK(is_pending(f), "Vorlaeufig: ohne Verlauf (seen_runs 0) ist ein Befund mit confirm_runs 2 vorlaeufig");
        f.seen_runs = 1;
        CHECK(is_pending(f), "Vorlaeufig: eine Pruefung reicht nicht");
        f.seen_runs = 2;
        CHECK(!is_pending(f), "Vorlaeufig: zwei Pruefungen in Folge bestaetigen");
        CHECK(!is_pending(mk("b", Severity::Info)), "Vorlaeufig: Standard ist sofort bestaetigt");
    }
    {  // Bericht: Messwerte und Vorlaeufig
        ReportInput in;
        in.created_utc = "20261111T120000Z";
        in.rules_version = "t";
        MetricChange m;
        m.id = "cpu";
        m.unit = "°C";
        m.text_de = "CPU-Temperatur";
        m.text_en = "CPU temperature";
        m.now = 70;
        m.has_prev = true;
        m.prev = 46;
        m.has_typical = true;
        m.typical = 45.5;
        m.samples = 4;
        MetricChange free_pct;
        free_pct.id = "free";
        free_pct.unit = "%";
        free_pct.text_de = "Freier Platz";
        free_pct.text_en = "Free space";
        free_pct.now = 42.04;
        in.metrics = {m, free_pct};
        Finding p = mk("t.pending", Severity::Info);
        p.confirm_runs = 2;
        p.status = "new";
        p.seen_runs = 1;
        in.findings = {p};
        auto has = [](const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; };
        const std::string de = build_report(in, ReportLang::De), en = build_report(in, ReportLang::En);
        CHECK(has(de, "## Messwerte\n\n- CPU-Temperatur: 70 °C (zuletzt 46, üblich ≈ 45.5 (aus 4 Prüfungen))\n- Freier Platz: 42 %\n"), "Bericht DE: Messwerte mit Veraenderung und Niveau");
        CHECK(has(en, "- CPU temperature: 70 °C (last 46, usual ≈ 45.5 (from 4 checks))\n"), "Bericht EN: Messwerte");
        CHECK(has(de, "Vorläufig: wird erst nach 2 Prüfungen in Folge bestätigt") && has(en, "Provisional: confirmed only after 2 checks in a row"), "Bericht: vorlaeufiger Befund");
    }
    {  // Bericht
        ReportInput in;
        in.created_utc = "20261111T120000Z";
        in.rules_version = "t";
        Finding a = mk("a.x", Severity::Warning);
        a.status = "recurring";
        a.first_seen = "20261105T080000Z";
        a.seen_runs = 4;
        Finding b = mk("b.y", Severity::Info);
        b.status = "new";
        in.findings = {a, b};
        in.resolved = {"c.gone"};
        const std::string de = build_report(in, ReportLang::De), en = build_report(in, ReportLang::En);
        auto has = [](const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; };
        CHECK(has(de, "- Dringlichkeit: hoch. Verlauf: seit 05.11.2026 (4 Prüfungen in Folge)\n") && has(de, "- Dringlichkeit: niedrig. Verlauf: neu seit der letzten Prüfung\n"), "Bericht DE: Dringlichkeit und Verlauf");
        CHECK(has(de, "## Seit der letzten Prüfung behoben (1)\n\n- c.gone\n"), "Bericht DE: behobene Regeln");
        CHECK(has(en, "- Urgency: high. History: since 2026-11-05 (4 checks in a row)\n") && has(en, "## Resolved since the last check (1)\n\n- c.gone\n"), "Bericht EN");
        in.resolved.clear();
        CHECK(!has(build_report(in, ReportLang::De), "behoben ("), "Bericht: ohne behobene Regeln kein Abschnitt");
    }
}
