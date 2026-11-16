// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/service.hpp"

#include "medic/collectors.hpp"
#include "medic/history.hpp"
#include "medic/jsondump.hpp"
#include "medic/support.hpp"
#include "medic/checks.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <set>
#include <string>
#include <nlohmann/json.hpp>

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

namespace {

// Alle Fakten, die eine Regel in `when` abfragt (Schluessel "fact"; bei "a.b[].c" gilt das Feld bis zur eckigen Klammer).
void collect_rule_facts(const nlohmann::json& j, std::set<std::string>& out) {
    if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() == "fact" && it.value().is_string()) {
                std::string f = it.value().get<std::string>();
                if (const auto br = f.find('['); br != std::string::npos) f.resize(br);
                out.insert(f);
            } else {
                collect_rule_facts(it.value(), out);
            }
        }
    } else if (j.is_array()) {
        for (const auto& e : j) collect_rule_facts(e, out);
    }
}

}  // namespace

CheckResult LocalService::run_check() {
    // Die Sammler brauchen fast die ganze Zeit; Auswerten und Speichern bekommen das letzte Zehntel.
    facts_ = medic::collect_all(p_, [this](int done, int total) {
        if (progress_ && total > 0) progress_(0.9 * done / total);
    });
    if (progress_) progress_(0.92);
    medic::persist_klog(p_, facts_);  // gelesene Kernel-Log-Zeilen sichern (verbrauchendes Lesen, D10b)
    CheckResult r;
    r.fw = p_.sys().firmware();
    r.model = p_.sys().model();
    r.rules_version = rules_.db_version;
    r.findings = medic::evaluate(rules_, facts_);
    r.coverage = medic::evaluate_coverage(rules_, facts_);
    {
        const nlohmann::json* failed = facts_.get("collector.failed");
        r.incomplete = rules_.rules.empty() || (failed && failed->is_array() && !failed->empty());
    }
    // Verlauf: gegen die letzte Pruefung vergleichen, dann diese Pruefung anhaengen (ohne Schreibzugriff nur der Vergleich).
    const std::string now = clock_();
    const auto hist = medic::load_history(p_.fs());
    prev_run_ts_ = hist.empty() ? std::string() : hist.back().ts;
    const auto metrics = medic::evaluate_metrics(rules_, facts_);
    r.resolved = medic::annotate_with_history(r.findings, hist, now);
    // "Behoben" nur, wenn die Regel auch ausgewertet werden konnte: fehlt einer ihrer Fakten (Lesefehler, USB gezogen), ist sie nicht behoben,
    // sondern in dieser Pruefung nicht geprueft worden.
    r.resolved.erase(std::remove_if(r.resolved.begin(), r.resolved.end(),
                                    [&](const std::string& id) {
                                        for (const auto& rule : rules_.rules) {
                                            if (rule.id != id) continue;
                                            std::set<std::string> names;
                                            collect_rule_facts(rule.when, names);
                                            for (const auto& n : names)
                                                if (!facts_.get(n)) return true;
                                        }
                                        return false;
                                    }),
                     r.resolved.end());
    r.metrics = medic::metric_changes(rules_, metrics, hist);
    if (progress_) progress_(0.97);
    bool saved = medic::append_history(p_, medic::make_history_run(now, rules_.db_version, r.fw, r.findings, metrics));
    {  // jeder Check-up als eigene Datei, damit man ihn spaeter einzeln ansehen und vergleichen kann
        medic::StoredCheck sc;
        sc.ts = now;
        sc.fw = r.fw;
        sc.model = r.model;
        sc.rules_version = r.rules_version;
        sc.findings = r.findings;
        sc.coverage = r.coverage;
        sc.resolved = r.resolved;
        sc.metrics = r.metrics;
        // Einrichtung fuer den Vergleich zweier Check-ups: Versionen der Bausteine und Zahl der Spiele
        const auto text_fact = [&](const char* k) {
            const nlohmann::json* v = facts_.get(k);
            return v && v->is_string() ? v->get<std::string>() : std::string();
        };
        if (const std::string v = text_fact("smp.version"); !v.empty()) sc.setup.emplace_back("smp.version", v);
        if (const std::string v = text_fact("kstuff.file_version"); !v.empty()) sc.setup.emplace_back("kstuff.file_version", v);
        if (const nlohmann::json* t = facts_.get("smp.titles"); t && t->is_array()) sc.setup.emplace_back("games.count", std::to_string(t->size()));
        if (const nlohmann::json* rt = facts_.get("games.runtime"); rt && rt->is_array()) {
            std::set<std::string> vers;
            for (const auto& e : *rt)
                if (e.is_object() && e.value("lib_kind", std::string()) == "ng" && e.contains("lib_version") && e["lib_version"].is_string() && !e["lib_version"].get<std::string>().empty())
                    vers.insert(e["lib_version"].get<std::string>());
            std::string joined;
            for (const auto& v : vers) joined += (joined.empty() ? "" : ", ") + v;
            if (!joined.empty()) sc.setup.emplace_back("ampr.versions", joined);
        }
        saved = medic::save_check(p_, sc) && saved;
    }
    // Die Fakten zu Spielen, Resten, nanoDNS, Kraken und kstuff dieses Check-ups (nur auf der Konsole, nicht im Bericht): zeigen, was Medic gelesen hat, auch wenn
    // kein Befund entstand (Laufzeit-Version in Images, Pack-Zustand, Beleg-Zeilen)
    if (saved && p_.fs_rw() && p_.fs().is_dir("/data/ps5medic")) {  // nur auf der Konsole (Medic-Ordner vorhanden) und zusammen mit dem gespeicherten Check-up
        nlohmann::json out = console_facts_json();
        out["ts"] = now;
        p_.fs_rw()->write_file("/data/ps5medic/last_facts.json", medic::dump_safe(out, 1) + "\n");
    }
    r.history_saved = saved;
    last_findings_ = r.findings;
    last_resolved_ = r.resolved;
    last_metrics_ = r.metrics;
    return r;
}

std::vector<medic::GameInfo> LocalService::games() {
    if (!facts_.get("fw.version")) return {};  // noch kein Check-up
    return medic::collect_game_infos(facts_, last_findings_);
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

std::vector<medic::CheckSummary> LocalService::checks() { return medic::list_checks(p_.fs()); }

bool LocalService::load_check(const std::string& id, medic::StoredCheck& out) { return medic::load_check(p_.fs(), id, out); }

medic::UndoResult LocalService::undo(const std::string& txn) {
    ensure_facts();
    return medic::undo_txn(p_, medic::policy_from_facts(facts_), txn, clock_());
}

// Die Pfad-Whitelist von Reparatur und Rueckgaengig kommt aus den Scanpfaden der Fakten. Ohne Check-up in dieser Sitzung
// (App neu gestartet, direkt in den Verlauf) sind die Fakten leer, und jedes Rueckgaengigmachen ausserhalb der Standardpfade
// scheiterte mit "Pfad nicht in der Whitelist". Deshalb vorher sammeln, wenn noch nichts da ist.
void LocalService::ensure_facts() {
    if (facts_.get("smp.scan_roots")) return;
    facts_ = medic::collect_all(p_);
    medic::persist_klog(p_, facts_);  // collect_all liest das Kernel-Log verbrauchend: die gelesenen Zeilen nicht verwerfen, sondern sichern
}

namespace {

// Die Datei mit der hoeheren db_version aus mehreren Orten (wie libmedic/newest.hpp, aber ueber die Dateisystem-Schnittstelle der Plattform).
// false: keine brauchbare Datei.
template <class Set, class Loader>
bool read_newest(const medic::IFileSystem& fs, const std::vector<std::string>& paths, Loader load, Set& out) {
    bool found = false;
    for (const std::string& path : paths) {
        std::string text;
        if (!fs.read_file(path, text)) continue;
        try {
            Set s = load(text);
            if (!found || medic::compare_versions(s.db_version, out.db_version) > 0) {
                out = std::move(s);
                found = true;
            }
        } catch (const std::exception&) {
        }
    }
    return found;
}

}  // namespace

bool LocalService::reload_rules() {
    // nach einem Update liegt die neue Datei unter knowledge/; die mitgelieferte zaehlt, wenn sie neuer ist (wie beim Start des Dienstes)
    medic::RuleSet s;
    if (!read_newest<medic::RuleSet>(p_.fs(), {medic::kRulesPath, "/data/homebrew/PS5MEDIC/rules.json"}, [](const std::string& x) { return medic::load_rules_from_string(x); }, s)) return false;
    rules_ = std::move(s);
    return true;
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
        if (!reload_rules()) r.error = "reload_failed";  // installiert, gilt aber erst nach einem Neustart
    }
    return r;
}

medic::RuleUpdateResult LocalService::rollback_rule_update() {
    medic::RuleUpdateResult r = medic::rollback_rule_update(p_, clock_());
    if (!r.ok) return r;
    if (!r.backup_path.empty()) {                                   // die bisherige Datei ist wieder aktiv
        if (!reload_rules()) r.error = "reload_failed";
    }
    else if (rules_before_install_) rules_ = *rules_before_install_;  // es galten die mitgelieferten Regeln
    else (void)reload_rules();  // neue Sitzung seit der Installation: die mitgelieferte Datei einlesen
    return r;
}

bool LocalService::reload_error_codes() {
    medic::ErrorCodeSet s;
    if (!read_newest<medic::ErrorCodeSet>(p_.fs(), {medic::kErrorCodesPath, "/data/homebrew/PS5MEDIC/error_codes.json"}, [](const std::string& x) { return medic::load_error_codes_from_string(x); }, s)) return false;
    error_codes_ = std::move(s);
    return true;
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
        if (!reload_error_codes()) r.error = "reload_failed";
    }
    return r;
}

medic::RuleUpdateResult LocalService::rollback_codes_update() {
    medic::RuleUpdateResult r = medic::rollback_rule_update(p_, clock_(), medic::KnowledgeKind::ErrorCodes);
    if (!r.ok) return r;
    if (!r.backup_path.empty()) {                                          // die bisherige Datei ist wieder aktiv
        if (!reload_error_codes()) r.error = "reload_failed";
    }
    else if (codes_before_install_) error_codes_ = *codes_before_install_;  // es galt die mitgelieferte Datei
    else {  // neue Sitzung seit der Installation: die mitgelieferte Datei einlesen
        std::string text;
        if (p_.fs().read_file("/data/homebrew/PS5MEDIC/error_codes.json", text)) {
            try {
                error_codes_ = medic::load_error_codes_from_string(text);
            } catch (const std::exception&) {
            }
        }
    }
    return r;
}

std::string LocalService::rule_update_url() { return medic::configured_update_url(p_.fs()); }

std::string LocalService::language() { return medic::configured_value(p_.fs(), "language"); }

bool LocalService::set_language(const std::string& code) { return medic::set_config_value(p_, "language", code); }

medic::RuleDownloadResult LocalService::download_rule_update() {
    if (!http_) {
        medic::RuleDownloadResult r;
        r.error = "no_network";
        return r;
    }
    return medic::download_rule_update(p_, *http_, rule_update_url(), clock_());
}

std::vector<medic::TestResult> LocalService::selftest_last() { return medic::load_last_selftest(p_.fs()); }

bool LocalService::selftest_begin(const std::vector<std::string>& ids) {
    selftest_facts_ = medic::collect_all(p_);  // erst sammeln, dann den Marker setzen (sonst saehe der Lauf seinen eigenen Marker als Absturz)
    medic::persist_klog(p_, selftest_facts_);  // das Kernel-Log ist gelesen (verbrauchend): sichern, sonst fehlen die Zeilen im naechsten Check-up
    return medic::selftest_begin(p_, clock_(), ids);
}

medic::TestResult LocalService::selftest_run(const std::string& id) {
    const medic::TestDef* d = medic::find_test(id);
    if (!d) {
        medic::TestResult r;
        r.id = id;
        r.status = medic::TestStatus::Error;
        r.reason = "unknown_test";
        return r;
    }
    medic::TestContext ctx;
    ctx.today = clock_().substr(0, 8);
    return medic::run_selftest(p_, selftest_facts_, *d, ctx);
}

void LocalService::selftest_start(const std::string& id) {
    selftest_id_ = id;
    selftest_session_.reset();
    if (const medic::TestDef* d = medic::find_test(id)) {
        medic::TestContext ctx;
        ctx.today = clock_().substr(0, 8);
        selftest_session_ = medic::begin_selftest_session(p_, selftest_facts_, *d, ctx);
    }
}

SelftestStep LocalService::selftest_step() {
    SelftestStep s;
    s.done = true;
    s.progress = 1;
    if (!selftest_session_) {
        s.result.id = selftest_id_;
        s.result.status = medic::TestStatus::Error;
        s.result.reason = "unknown_test";
        return s;
    }
    const auto t0 = std::chrono::steady_clock::now();
    if (selftest_t0_.time_since_epoch().count() == 0) selftest_t0_ = t0;
    try {
        s.done = selftest_session_->step();
    } catch (const std::exception&) {
        s.result.id = selftest_id_;
        s.result.status = medic::TestStatus::Error;  // ein Fehler im Test ist nie "passed"
        s.result.reason = "exception";
        selftest_session_.reset();
        selftest_t0_ = {};  // sonst enthielte die Dauer des naechsten Tests die Zeit des abgebrochenen
        return s;
    }
    s.progress = selftest_session_->progress();
    if (s.done) {
        s.result = selftest_session_->result();
        s.result.id = selftest_id_;
        s.result.duration_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - selftest_t0_).count();
        selftest_session_.reset();
        selftest_t0_ = {};
    }
    return s;
}

void LocalService::selftest_record(const medic::TestResult& r) { medic::selftest_record(p_, clock_(), r); }

void LocalService::selftest_abort() {
    if (selftest_session_) selftest_session_->abort("cancelled");
}

bool LocalService::selftest_end(const std::vector<medic::TestResult>& results, bool cancelled) { return medic::selftest_finish(p_, clock_(), results, cancelled); }

nlohmann::json LocalService::console_facts_json() const {
    nlohmann::json out = nlohmann::json::object();
    for (const char* k : {"games.runtime", "games.pack_problems", "games.index_problems", "games.noexec", "games.incomplete", "games.runtime_skipped", "leftovers.files", "games.partial_folders",
                          "games.undo_journals", "games.fakelib_layout", "games.with_fakelib", "games.unsigned_elf", "games.flex_small", "games.playgo_missing", "games.trophy_missing", "nanodns.log.problems",
                          "kraken.log_present", "kraken.sparse_extent", "kraken.sysex_readonly", "kraken.pkg_other", "kraken.pkg_truncated", "kraken.cache_block", "kraken.ampr_foreign", "kstuff.file", "kstuff.file_version"})
        if (const nlohmann::json* v = facts_.get(k)) out[k] = *v;
    return out;
}

SupportResult LocalService::make_support() {
    SupportResult r;
    const std::string ts = clock_();
    const std::string zip_path = medic::support_path_for(ts), key_path = medic::support_key_path_for(ts);
    if (!p_.fs_rw() || zip_path.empty()) {
        r.error = "no_write";
        return r;
    }
    medic::ReportInput in = medic::make_report_input(p_, facts_, rules_, last_findings_, ts);
    in.resolved = last_resolved_;
    in.prev_ts = prev_run_ts_;
    in.metrics = last_metrics_;
    // Vergleich mit dem vorigen Check-up (nur Regelnamen, Schweregrade, Einrichtung), wenn es einen gibt
    std::string compare;
    const auto list = medic::list_checks(p_.fs());
    medic::StoredCheck a, b;
    if (list.size() >= 2 && medic::load_check(p_.fs(), list[0].id, a) && medic::load_check(p_.fs(), list[1].id, b)) {
        const medic::CheckDiff d = medic::compare_checks(a, b);
        compare = "Check-up " + a.ts + " gegen " + b.ts + " / check-up " + a.ts + " vs " + b.ts + "\n";
        for (const auto& f : d.added) compare += "+ " + f.rule_id + " (" + medic::to_string(f.severity) + ")\n";
        std::vector<std::string> gone;
        for (const auto& f : d.resolved) gone.push_back(f.rule_id);
        const auto repairs = medic::repairs_between(medic::history(p_), gone, b.ts, a.ts);
        for (const auto& f : d.resolved) {
            compare += "- " + f.rule_id + " (" + medic::to_string(f.severity) + ")";
            for (const auto& l : repairs)
                if (l.rule_id == f.rule_id) compare += " by repair: " + l.fix + " (" + l.ts + ")";
            compare += "\n";
        }
        for (const auto& s : d.severity_changed) compare += "~ " + s.now.rule_id + " (" + medic::to_string(s.before) + " -> " + medic::to_string(s.now.severity) + ")\n";
        for (const auto& s : d.setup_changed) compare += "setup " + s.key + ": " + (s.from.empty() ? "-" : s.from) + " -> " + (s.to.empty() ? "-" : s.to) + "\n";
        if (d.added.empty() && d.resolved.empty() && d.severity_changed.empty() && d.setup_changed.empty()) compare += "keine Unterschiede / no differences\n";
    }
    const medic::SupportBundle bundle = medic::build_support_bundle(in, medic::dump_safe(console_facts_json(), 1) + "\n", compare);
    // Neue Dateien, nie ueberschreiben: gibt es sie schon (zweiter Aufruf in derselben Sekunde), wird nichts geschrieben
    if (p_.fs().exists(zip_path) || p_.fs().exists(key_path) || !p_.fs_rw()->write_file(key_path, bundle.key) || !p_.fs_rw()->write_file(zip_path, bundle.zip)) {
        r.error = "write_failed";
        return r;
    }
    r.saved = true;
    r.path = zip_path;
    r.key_path = key_path;
    r.titles = bundle.titles;
    r.names = bundle.names;
    return r;
}

ReportResult LocalService::make_report(medic::ReportLang lang) {
    ReportResult r;
    const std::string ts = clock_();
    medic::ReportInput in = medic::make_report_input(p_, facts_, rules_, last_findings_, ts);
    in.resolved = last_resolved_;
    in.prev_ts = prev_run_ts_;
    in.metrics = last_metrics_;
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
