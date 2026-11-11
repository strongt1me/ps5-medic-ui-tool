// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/service.hpp"

#include "medic/collectors.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace medic::ui {

namespace {

std::string utc_now() {
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

}  // namespace

LocalService::LocalService(medic::Platform& platform, medic::RuleSet rules, std::function<std::string()> clock)
    : p_(platform), rules_(std::move(rules)), clock_(clock ? std::move(clock) : std::function<std::string()>(utc_now)) {}

CheckResult LocalService::run_check() {
    facts_ = medic::collect_all(p_);
    CheckResult r;
    r.fw = p_.sys().firmware();
    r.model = p_.sys().model();
    r.rules_version = rules_.db_version;
    r.findings = medic::evaluate(rules_, facts_);
    r.coverage = medic::evaluate_coverage(rules_, facts_);
    last_findings_ = r.findings;
    return r;
}

medic::FixPlan LocalService::plan_fix(const medic::Finding& f) { return medic::plan_fix(f, facts_, p_.fs()); }

medic::ApplyResult LocalService::apply_fix(const medic::FixPlan& plan) {
    medic::ApplyOptions opt;
    opt.timestamp = clock_();
    opt.confirmed = true;  // die UI hat den Plan gezeigt und das Kreuz abgewartet
    ensure_facts();
    return medic::apply_plan(plan, p_, medic::policy_from_facts(facts_), opt);
}

std::string LocalService::read_text(const std::string& path) {
    std::string s;
    return p_.fs().read_file(path, s) ? s : std::string();
}

std::vector<medic::TxnInfo> LocalService::history() { return medic::history(p_); }

medic::UndoResult LocalService::undo(const std::string& txn) {
    ensure_facts();
    return medic::undo_txn(p_, medic::policy_from_facts(facts_), txn, clock_());
}

// Die Pfad-Whitelist von Reparatur und Rueckgaengig kommt aus den Scanpfaden der Fakten. Ohne Check-up in dieser Sitzung
// (App neu gestartet, direkt in den Verlauf) sind die Fakten leer, und jedes Rueckgaengigmachen ausserhalb der Standardpfade
// scheiterte mit "Pfad nicht in der Whitelist". Deshalb vorher sammeln, wenn noch nichts da ist.
void LocalService::ensure_facts() {
    if (!facts_.get("smp.scan_roots")) facts_ = medic::collect_all(p_);
}

bool LocalService::reload_rules() {
    std::string text;
    if (!p_.fs().read_file(medic::kRulesPath, text)) return false;
    try {
        rules_ = medic::load_rules_from_string(text);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

medic::RuleUpdateOffer LocalService::check_rule_update() {
    medic::RuleUpdateOffer o = medic::check_rule_update(p_.fs(), rules_.db_version, nullptr, keys_ ? &*keys_ : nullptr);
    o.old_rule_count = rules_.rules.size();
    return o;
}

medic::RuleUpdateResult LocalService::install_rule_update() {
    const medic::RuleSet before = rules_;
    medic::RuleUpdateResult r = medic::install_rule_update(p_, rules_.db_version, clock_(), keys_ ? &*keys_ : nullptr);
    if (r.ok) {
        rules_before_install_ = before;
        reload_rules();
    }
    return r;
}

medic::RuleUpdateResult LocalService::rollback_rule_update() {
    medic::RuleUpdateResult r = medic::rollback_rule_update(p_, clock_());
    if (!r.ok) return r;
    if (!r.backup_path.empty()) reload_rules();                     // die bisherige Datei ist wieder aktiv
    else if (rules_before_install_) rules_ = *rules_before_install_;  // es galten die mitgelieferten Regeln
    return r;
}

bool LocalService::reload_error_codes() {
    std::string text;
    if (!p_.fs().read_file(medic::kErrorCodesPath, text)) return false;
    try {
        error_codes_ = medic::load_error_codes_from_string(text);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

namespace {

std::string gib_text(double bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f GiB", bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

std::string fact_string(const medic::Facts& f, const char* path) {
    const nlohmann::json* v = f.get(path);
    return v && v->is_string() ? v->get<std::string>() : std::string();
}

bool starts_with(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

}  // namespace

// "Was Medic gerade sieht": Fakten des letzten Check-ups, ausgewaehlt nach dem `context` des Eintrags. Es wird nichts neu gesammelt
// (ein Check-up dauert Sekunden) und nichts geschrieben.
CodeContext LocalService::code_context(const medic::ErrorCode& code) {
    CodeContext c;
    if (code.context.empty() || !facts_.get("fw.version")) return c;  // kein Kontext verlangt bzw. noch kein Check-up
    c.available = true;
    const std::string fw = fact_string(facts_, "fw.version");
    for (const std::string& kind : code.context) {
        if (kind == "fw") {
            c.lines.push_back({"fw", {{"fw", fw.empty() ? std::string("?") : fw}}});
        } else if (kind == "storage" || kind == "drives") {
            const nlohmann::json* m = facts_.get("storage.mounts");
            bool any = false;
            if (m && m->is_array())
                for (const auto& s : *m) {
                    if (!s.is_object() || !s.contains("total") || !s["total"].is_number()) continue;
                    const double total = s["total"].get<double>(), free_b = s.contains("free") && s["free"].is_number() ? s["free"].get<double>() : 0;
                    if (total < 1e9) continue;  // leere Mountpunkte melden die Groesse eines winzigen Dateisystems
                    const std::string mount = s.value("mount", std::string("?")), fstype = s.value("fstype", std::string());
                    const bool external = starts_with(mount, "/mnt/usb") || starts_with(mount, "/mnt/ext");
                    char pct[16];
                    std::snprintf(pct, sizeof pct, "%.0f", 100.0 * free_b / total);
                    if (kind == "storage") {
                        c.lines.push_back({"storage", {{"mount", mount}, {"free", gib_text(free_b)}, {"total", gib_text(total)}, {"pct", pct}}});
                        any = true;
                    } else if (external) {
                        c.lines.push_back({s.value("readonly", false) ? "drive_ro" : "drive", {{"mount", mount}, {"fstype", fstype.empty() ? std::string("?") : fstype}, {"total", gib_text(total)}}});
                        any = true;
                    }
                }
            if (!any) c.lines.push_back({kind == "storage" ? "storage_none" : "drive_none", {}});
        } else if (kind == "smp") {
            const std::string v = fact_string(facts_, "smp.version");
            if (v.empty()) {
                c.lines.push_back({"smp_missing", {}});
            } else {
                c.lines.push_back({"smp", {{"v", v}}});
                const bool leak = std::any_of(last_findings_.begin(), last_findings_.end(), [](const medic::Finding& f) { return f.rule_id == "smp.leak_pre_beta11"; });
                c.lines.push_back({leak ? "smp_leak" : "smp_no_leak", {}});
            }
        } else if (kind == "need_backport") {
            const nlohmann::json* n = facts_.get("games.need_backport");
            if (n && n->is_array() && !n->empty()) {
                std::string list;
                for (const auto& g : *n) list += (list.empty() ? "" : ", ") + g.value("titleid", std::string("?")) + " (" + g.value("required_fw", std::string("?")) + ")";
                c.lines.push_back({"need_backport", {{"list", list}, {"fw", fw}}});
            } else {
                c.lines.push_back({"need_backport_none", {{"fw", fw.empty() ? std::string("?") : fw}}});
            }
        }
    }
    return c;
}

medic::RuleUpdateOffer LocalService::check_codes_update() {
    medic::RuleUpdateOffer o = medic::check_rule_update(p_.fs(), error_codes_.db_version, nullptr, keys_ ? &*keys_ : nullptr, medic::KnowledgeKind::ErrorCodes);
    o.old_rule_count = error_codes_.codes.size();
    return o;
}

medic::RuleUpdateResult LocalService::install_codes_update() {
    const medic::ErrorCodeSet before = error_codes_;
    medic::RuleUpdateResult r = medic::install_rule_update(p_, error_codes_.db_version, clock_(), keys_ ? &*keys_ : nullptr, medic::KnowledgeKind::ErrorCodes);
    if (r.ok) {
        codes_before_install_ = before;
        reload_error_codes();
    }
    return r;
}

medic::RuleUpdateResult LocalService::rollback_codes_update() {
    medic::RuleUpdateResult r = medic::rollback_rule_update(p_, clock_(), medic::KnowledgeKind::ErrorCodes);
    if (!r.ok) return r;
    if (!r.backup_path.empty()) reload_error_codes();                      // die bisherige Datei ist wieder aktiv
    else if (codes_before_install_) error_codes_ = *codes_before_install_;  // es galt die mitgelieferte Datei
    return r;
}

std::string LocalService::rule_update_url() { return medic::configured_update_url(p_.fs()); }

medic::RuleDownloadResult LocalService::download_rule_update() {
    if (!http_) {
        medic::RuleDownloadResult r;
        r.error = "no_network";
        return r;
    }
    return medic::download_rule_update(p_, *http_, rule_update_url(), clock_());
}

ReportResult LocalService::make_report(medic::ReportLang lang) {
    ReportResult r;
    const std::string ts = clock_();
    medic::ReportInput in = medic::make_report_input(p_, facts_, rules_, last_findings_, ts);
    if (const medic::ErrorCode* e = error_codes_.find(reported_error_)) {
        in.error_code = e->id;
        in.error_title_de = e->title_de;
        in.error_title_en = e->title_en;
    }

    // Der einzige Schreibzugriff ausserhalb der Fix-Engine: eine Markdown-Datei in /data/ps5medic/reports/.
    const std::string path = medic::report_path_for(ts);
    if (!p_.fs_rw() || path.empty()) {
        r.error = "no_write";
    } else if (!p_.fs_rw()->write_file(path, medic::build_report(in, lang))) {
        r.error = "write_failed";
    } else {
        r.saved = true;
        r.path = path;
        in.saved_path = path;
    }
    r.qr_text = medic::build_report_compact(in, lang, kQrMaxBytes, &r.truncated);
    return r;
}

}  // namespace medic::ui
