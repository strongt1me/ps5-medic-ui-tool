#include "medic_ui/service.hpp"

#include "medic/collectors.hpp"

#include <chrono>
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
    last_findings_ = r.findings;
    return r;
}

medic::FixPlan LocalService::plan_fix(const medic::Finding& f) { return medic::plan_fix(f, facts_, p_.fs()); }

medic::ApplyResult LocalService::apply_fix(const medic::FixPlan& plan) {
    medic::ApplyOptions opt;
    opt.timestamp = clock_();
    opt.confirmed = true;  // die UI hat den Plan gezeigt und das Kreuz abgewartet
    return medic::apply_plan(plan, p_, medic::policy_from_facts(facts_), opt);
}

std::string LocalService::read_text(const std::string& path) {
    std::string s;
    return p_.fs().read_file(path, s) ? s : std::string();
}

std::vector<medic::TxnInfo> LocalService::history() { return medic::history(p_); }

medic::UndoResult LocalService::undo(const std::string& txn) {
    return medic::undo_txn(p_, medic::policy_from_facts(facts_), txn, clock_());
}

ReportResult LocalService::make_report(medic::ReportLang lang) {
    ReportResult r;
    const std::string ts = clock_();
    medic::ReportInput in = medic::make_report_input(p_, facts_, rules_, last_findings_, ts);

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
