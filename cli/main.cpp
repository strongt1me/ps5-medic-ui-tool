// SPDX-License-Identifier: GPL-3.0-or-later
// medic-cli
//   check   <snapshot-dir> [--rules f] [--json] [--lang de|en]
//   fix     <snapshot-dir> <regel-id> [--keep pfad] [--apply --yes] [--rules f] [--lang de|en]
//   undo    <snapshot-dir> <txn>
//   history <snapshot-dir>
//   export  <snapshot-dir> <zielordner>   (Snapshot erneut schreiben, z. B. zum Pruefen des Exports)
//   report  <snapshot-dir> [--out datei] [--qr] [--rules f] [--lang de|en]   (Bericht als Markdown; --qr: Kurzfassung als QR-Code im Terminal)
// "fix" ohne --apply zeigt nur den Plan (Dry-Run). Mit --apply wird DIREKT im Snapshot-Ordner geschrieben -- Kopie benutzen!
#include "medic/collectors.hpp"
#include "medic/jsondump.hpp"
#include "medic/diff.hpp"
#include "medic/fixes.hpp"
#include "medic/history.hpp"
#include "medic/platform.hpp"
#include "medic/qr.hpp"
#include "medic/report.hpp"
#include "medic/rules.hpp"
#include "medic/snapshot_export.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

#ifndef MEDIC_DEFAULT_RULES
#define MEDIC_DEFAULT_RULES "knowledge/rules.json"
#endif

namespace {

void usage() {
    std::cerr << "Aufruf:\n"
                 "  medic-cli check   <snapshot-ordner> [--rules <rules.json>] [--json] [--lang de|en]\n"
                 "  medic-cli fix     <snapshot-ordner> <regel-id> [--keep <pfad>] [--apply --yes] [--rules <rules.json>] [--lang de|en]\n"
                 "  medic-cli undo    <snapshot-ordner> <txn>\n"
                 "  medic-cli history <snapshot-ordner>\n"
                 "  medic-cli export  <snapshot-ordner> <zielordner>\n"
                 "  medic-cli report  <snapshot-ordner> [--out <datei>] [--qr] [--rules <rules.json>] [--lang de|en]\n";
}

const char* label(medic::Severity s) {
    switch (s) {
        case medic::Severity::Critical: return "KRITISCH";
        case medic::Severity::Warning: return "WARNUNG";
        case medic::Severity::Info: return "HINWEIS";
    }
    return "";
}

std::string utc_timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

// Zeigt nur geaenderte Zeilen (config.ini, Autoload-Listen).
void print_diff(const std::string& old_text, const std::string& new_text) {
    std::vector<medic::DiffLine> d;
    if (!medic::diff_lines(old_text, new_text, d)) {
        std::cout << "      (Datei zu gross fuer die Diff-Anzeige)\n";
        return;
    }
    for (const auto& l : d)
        if (l.tag != ' ') std::cout << "      " << l.tag << " " << l.text << "\n";
}

struct Args {
    std::string dir, rules = MEDIC_DEFAULT_RULES, lang = "de", rule_id, keep, txn, out;
    bool json = false, apply = false, yes = false, qr = false;
};

bool parse_flags(int argc, char** argv, int from, Args& a) {
    for (int i = from; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--json") a.json = true;
        else if (s == "--apply") a.apply = true;
        else if (s == "--yes") a.yes = true;
        else if (s == "--qr") a.qr = true;
        else if (s == "--out" && i + 1 < argc) a.out = argv[++i];
        else if (s == "--rules" && i + 1 < argc) a.rules = argv[++i];
        else if (s == "--lang" && i + 1 < argc) a.lang = argv[++i];
        else if (s == "--keep" && i + 1 < argc) a.keep = argv[++i];
        else return false;
    }
    return a.lang == "de" || a.lang == "en";
}

int cmd_check(const Args& a) {
    auto rules = medic::load_rules_from_file(a.rules);
    auto platform = medic::make_snapshot_platform(a.dir);
    medic::Facts facts = medic::collect_all(*platform);
    auto findings = medic::evaluate(rules, facts);
    const auto coverage = medic::evaluate_coverage(rules, facts);

    if (a.json) {
        nlohmann::json out = nlohmann::json::object();
        out["snapshot"] = a.dir;
        out["rules_version"] = rules.db_version;
        out["findings"] = nlohmann::json::array();
        out["not_tested"] = nlohmann::json::array();
        for (auto& c : coverage)
            if (!c.tested) out["not_tested"].push_back(c.id);
        for (auto& f : findings) {
            const medic::Confidence conf = medic::effective_confidence(f);
            out["findings"].push_back({{"id", f.rule_id},
                                       {"severity", medic::to_string(f.severity)},
                                       {"text", a.lang == "de" ? f.text_de : f.text_en},
                                       {"evidence", f.evidence},
                                       {"fix", f.fix.empty() ? nlohmann::json(nullptr) : nlohmann::json(f.fix)},
                                       {"source", f.source},
                                       {"verified", f.verified},
                                       {"urgency", medic::urgency_of(f)},
                                       {"confidence", {{"level", conf.level}, {"basis", conf.basis}, {"reason", a.lang == "de" ? conf.reason_de : conf.reason_en}}}});
        }
        std::cout << medic::dump_safe(out, 2) << "\n";
        return 0;
    }

    std::cout << "PS5 Medic - Check-up: " << a.dir << "\n";
    const auto* fw = facts.get("fw.version");
    std::cout << "Firmware: " << (fw && fw->is_string() && !fw->get<std::string>().empty() ? fw->get<std::string>() : "unbekannt")
              << " | Regeln: " << rules.db_version << " (" << rules.rules.size() << ")\n\n";
    std::string untested;
    for (auto& c : coverage)
        if (!c.tested) untested += (untested.empty() ? "" : ", ") + (a.lang == "de" ? c.text_de : c.text_en);
    if (findings.empty()) {
        std::cout << "Keine Befunde.\n";
        if (!untested.empty()) std::cout << "Nicht geprueft (heisst nicht \"in Ordnung\"): " << untested << "\n";
        return 0;
    }
    for (auto& f : findings) {
        std::cout << "[" << label(f.severity) << "] " << f.rule_id << "\n  " << (a.lang == "de" ? f.text_de : f.text_en) << "\n";
        for (auto& e : f.evidence) std::cout << "  Evidenz: " << e << "\n";
        if (!f.fix.empty()) std::cout << "  Fix:     " << f.fix << "\n";
        std::cout << "  Quelle:  " << f.source << " (" << f.verified << ")\n";
        const medic::Confidence conf = medic::effective_confidence(f);
        const std::string& reason = a.lang == "de" ? conf.reason_de : conf.reason_en;
        std::cout << "  Verlaesslichkeit: " << conf.level << " (" << conf.basis << ")" << (reason.empty() ? "" : " - " + reason) << "\n\n";
    }
    std::cout << findings.size() << " Befund(e).\n";
    if (!untested.empty()) std::cout << "Nicht geprueft: " << untested << "\n";
    return 0;
}

int cmd_fix(const Args& a) {
    auto rules = medic::load_rules_from_file(a.rules);
    auto platform = medic::make_snapshot_platform(a.dir, /*writable=*/a.apply);
    medic::Facts facts = medic::collect_all(*platform);
    auto findings = medic::evaluate(rules, facts);
    auto it = std::find_if(findings.begin(), findings.end(), [&](auto& f) { return f.rule_id == a.rule_id; });
    if (it == findings.end()) {
        std::cerr << "Kein Befund mit der Regel-ID '" << a.rule_id << "' in diesem Snapshot.\n";
        return 2;
    }
    medic::FixOptions opt;
    opt.keep = a.keep;
    medic::FixPlan plan = medic::plan_fix(*it, facts, platform->fs(), opt);

    std::cout << "Plan fuer " << a.rule_id << " (Fix: " << (plan.fix_id.empty() ? "-" : plan.fix_id) << ")\n";
    for (auto& c : plan.changes) {
        const std::string& note = a.lang == "de" ? c.note_de : c.note_en;
        if (c.kind == medic::Change::Kind::Rename) {
            std::cout << "  VERSCHIEBEN  " << c.path << "\n          nach  " << c.to << "\n          " << note << "\n";
        } else {
            std::cout << "  SCHREIBEN    " << c.path << "\n          " << note << "\n";
            std::string old;
            platform->fs().read_file(c.path, old);
            print_diff(old, c.content);
        }
    }
    for (auto& w : plan.warnings) std::cout << "  Hinweis: " << w << "\n";
    const std::string& hint = a.lang == "de" ? plan.hint_de : plan.hint_en;
    if (!hint.empty()) std::cout << "  " << hint << "\n";
    if (plan.empty()) {
        std::cout << "Keine Aenderungen geplant.\n";
        return 0;
    }
    if (!a.apply) {
        std::cout << "\nDry-Run: es wurde nichts veraendert. Mit --apply --yes ausfuehren.\n";
        return 0;
    }
    if (!a.yes) {
        std::cerr << "--apply braucht --yes (ausdrueckliche Bestaetigung).\n";
        return 2;
    }
    medic::ApplyOptions ao;
    ao.timestamp = utc_timestamp();
    ao.confirmed = true;
    auto res = medic::apply_plan(plan, *platform, medic::policy_from_facts(facts), ao);
    if (!res.ok) {
        std::cerr << "Fehler: " << res.error << "\n";
        return 2;
    }
    std::cout << "\nAngewendet. Transaktion: " << res.txn << "\nRueckgaengig: medic-cli undo " << a.dir << " " << res.txn << "\n";
    return 0;
}

int cmd_undo(const Args& a) {
    auto platform = medic::make_snapshot_platform(a.dir, true);
    medic::Facts facts = medic::collect_all(*platform);
    auto res = medic::undo_txn(*platform, medic::policy_from_facts(facts), a.txn, utc_timestamp());
    if (!res.ok) {
        std::cerr << "Fehler: " << res.error << "\n";
        return 2;
    }
    std::cout << "Transaktion " << a.txn << " rueckgaengig gemacht.\n";
    return 0;
}

int cmd_export(const Args& a) {
    auto platform = medic::make_snapshot_platform(a.dir);
    medic::Facts facts = medic::collect_all(*platform);
    auto res = medic::export_snapshot(*platform, facts, a.txn, utc_timestamp());
    if (!res.ok) {
        std::cerr << "Fehler: " << res.error << "\n";
        return 2;
    }
    std::cout << "Snapshot geschrieben: " << a.txn << " (" << res.entries << " Eintraege)\n";
    for (auto& w : res.warnings) std::cout << "  Hinweis: " << w << "\n";
    return 0;
}

int cmd_report(const Args& a) {
    auto rules = medic::load_rules_from_file(a.rules);
    auto platform = medic::make_snapshot_platform(a.dir);
    medic::Facts facts = medic::collect_all(*platform);
    const auto lang = a.lang == "de" ? medic::ReportLang::De : medic::ReportLang::En;
    const std::string ts = utc_timestamp();
    medic::ReportInput in = medic::make_report_input(*platform, facts, rules, medic::evaluate(rules, facts), ts);
    if (!a.out.empty()) in.saved_path = medic::report_path_for(ts);  // so wuerde der Pfad auf der Konsole heissen

    const std::string report = medic::build_report(in, lang);
    if (!a.out.empty()) {
        std::ofstream f(a.out, std::ios::binary | std::ios::trunc);
        f << report;
        if (!f) {
            std::cerr << "Fehler: " << a.out << " nicht schreibbar\n";
            return 2;
        }
        std::cout << "Bericht geschrieben: " << a.out << " (" << report.size() << " Byte)\n";
    } else if (!a.qr) {
        std::cout << report;
    }
    if (a.qr) {
        bool truncated = false;
        const std::string compact = medic::build_report_compact(in, lang, 640, &truncated);
        medic::QrCode qr;
        if (!medic::qr_encode(compact, qr)) {
            std::cerr << "Fehler: Kurzfassung passt nicht in einen QR-Code\n";
            return 2;
        }
        std::cout << medic::qr_to_text(qr) << "\n" << compact << (truncated ? "(gekuerzt)\n" : "");
    }
    return 0;
}

int cmd_history(const Args& a) {
    auto platform = medic::make_snapshot_platform(a.dir);
    auto h = medic::history(*platform);
    if (h.empty()) {
        std::cout << "Noch keine Reparaturen.\n";
        return 0;
    }
    for (auto& t : h) std::cout << t.txn << "  " << t.state << "  " << t.fix << "  (" << t.rule << ", " << t.ops << " Aenderung(en))\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    const std::string cmd = argv[1];
    Args a;
    a.dir = argv[2];
    int flags_from = 3;
    if (cmd == "fix" || cmd == "undo" || cmd == "export") {
        if (argc < 4) {
            usage();
            return 2;
        }
        (cmd == "fix" ? a.rule_id : a.txn) = argv[3];  // fuer export: Zielordner in a.txn
        flags_from = 4;
    }
    if ((cmd != "check" && cmd != "fix" && cmd != "undo" && cmd != "history" && cmd != "export" && cmd != "report") ||
        !parse_flags(argc, argv, flags_from, a)) {
        usage();
        return 2;
    }
    try {
        if (cmd == "check") return cmd_check(a);
        if (cmd == "fix") return cmd_fix(a);
        if (cmd == "undo") return cmd_undo(a);
        if (cmd == "export") return cmd_export(a);
        if (cmd == "report") return cmd_report(a);
        return cmd_history(a);
    } catch (const std::exception& e) {
        std::cerr << "Fehler: " << e.what() << "\n";
        return 2;
    }
}
