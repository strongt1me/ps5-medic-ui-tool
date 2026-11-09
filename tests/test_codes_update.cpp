// SPDX-License-Identifier: GPL-3.0-or-later
// Tests fuer das Update der Fehlercode-Datei (KnowledgeKind::ErrorCodes in libmedic/src/rule_update.cpp) und ihren optionalen
// Download (libmedic/src/rule_download.cpp). Die Regel-Updates selbst pruefen test_update.cpp und test_sign.cpp.
#include "check.hpp"
#include "testutil.hpp"
#include "medic/error_codes.hpp"
#include "medic/http.hpp"
#include "medic/platform.hpp"
#include "medic/rule_download.hpp"
#include "medic/rule_update.hpp"
#include "medic/sha256.hpp"
#include "medic/signature.hpp"

#include <map>
#include <string>
#include <vector>

using namespace medic;
using namespace testutil;

namespace {

const std::string kCodesFile = std::string(MEDIC_SOURCE_DIR) + "/knowledge/error_codes.json";

// Die ausgelieferte Fehlercode-Datei mit anderer Version.
std::string codes_text_with_version(const std::string& version) {
    json j = json::parse(slurp(kCodesFile));
    j["db_version"] = version;
    return j.dump(2) + "\n";
}

std::string update_file(const char* name) { return std::string(kRulesUpdateDir) + "/" + name; }

void put_codes(const Sandbox& sb, const std::string& text, const std::string& sum_line) {
    spit(sb.real(update_file("error_codes.json")), text);
    if (!sum_line.empty()) spit(sb.real(update_file("error_codes.json.sha256")), sum_line);
}

std::string good_sum(const std::string& text, const char* name) { return sha256_hex(text) + "  " + name + "\n"; }

struct FakeHttp : IHttpClient {
    std::map<std::string, HttpResponse> answers;
    std::vector<std::string> asked;
    HttpResponse get(const std::string& url, std::size_t, int) override {
        asked.push_back(url);
        auto it = answers.find(url);
        if (it != answers.end()) return it->second;
        HttpResponse r;
        r.status = 404;
        return r;
    }
};

HttpResponse ok200(const std::string& body) {
    HttpResponse r;
    r.status = 200;
    r.body = body;
    return r;
}

HttpResponse net_error(const char* what) {
    HttpResponse r;
    r.error = what;
    return r;
}

void core_tests() {
    const std::string ts = "20261109T150000Z";
    const std::string shipped = slurp(kCodesFile);
    const ErrorCodeSet shipped_set = load_error_codes_from_string(shipped);
    const std::string new_ver = "2026.12.01", new_text = codes_text_with_version(new_ver);
    using State = RuleUpdateOffer::State;
    const auto kind = KnowledgeKind::ErrorCodes;

    {  // Angebot: neuere Datei mit Pruefsumme
        Sandbox sb;
        put_codes(sb, new_text, good_sum(new_text, "error_codes.json"));
        auto p = make_snapshot_platform(sb.dir.string());
        std::string verified;
        const auto o = check_rule_update(p->fs(), shipped_set.db_version, &verified, nullptr, kind);
        CHECK(o.state == State::Ready && o.new_version == new_ver && o.new_rule_count == shipped_set.codes.size() && o.auth == "checksum" && verified == new_text,
              "Codes-Update: neuere Datei mit passender Pruefsumme wird angeboten (Zahl der Codes steht im Angebot)");
        // Dieselben Dateien sind fuer die Regeln unsichtbar.
        CHECK(check_rule_update(p->fs(), "2026.11.12").state == State::None, "Codes-Update: das Regel-Update sieht die Fehlercode-Dateien nicht");
    }
    {  // ohne geladene Datei (leere Version) wird jede gueltige Datei angeboten
        Sandbox sb;
        put_codes(sb, new_text, good_sum(new_text, "error_codes.json"));
        auto p = make_snapshot_platform(sb.dir.string());
        CHECK(check_rule_update(p->fs(), "", nullptr, nullptr, kind).state == State::Ready, "Codes-Update: ohne bisherige Datei wird die neue angeboten");
    }
    struct Reject {
        const char* name;
        std::string text, sum, reason;
    };
    const Reject rejects[] = {
        {"falsche Pruefsumme", new_text, std::string(64, 'a') + "  error_codes.json\n", "checksum_mismatch"},
        {"keine Pruefsumme", new_text, "", "no_checksum"},
        {"kein JSON", "kein json", good_sum("kein json", "error_codes.json"), "invalid_rules"},
        {"eine Regeldatei in der Fehlercode-Datei", rules_text_with_version("2030.01.01"), good_sum(rules_text_with_version("2030.01.01"), "error_codes.json"), "invalid_rules"},
        {"Schema verletzt (id)", R"({"schema_version":1,"db_version":"2030.01.01","codes":[{"id":"xyz"}]})", good_sum(R"({"schema_version":1,"db_version":"2030.01.01","codes":[{"id":"xyz"}]})", "error_codes.json"), "invalid_rules"},
        {"gleiche Version", shipped, good_sum(shipped, "error_codes.json"), "not_newer"},
        {"aeltere Version", codes_text_with_version("2020.01.01"), good_sum(codes_text_with_version("2020.01.01"), "error_codes.json"), "not_newer"},
    };
    for (const auto& rj : rejects) {
        Sandbox sb;
        put_codes(sb, rj.text, rj.sum);
        auto p = make_snapshot_platform(sb.dir.string());
        const auto o = check_rule_update(p->fs(), shipped_set.db_version, nullptr, nullptr, kind);
        CHECK(o.state == State::Rejected && o.reason == rj.reason, std::string("Codes-Update abgelehnt: ") + rj.name + " (" + o.reason + ")");
    }
    {  // Gegenprobe: eine Fehlercode-Datei im Regel-Update faellt ebenfalls durch
        Sandbox sb;
        spit(sb.real(update_file("rules.json")), new_text);
        spit(sb.real(update_file("rules.json.sha256")), good_sum(new_text, "rules.json"));
        auto p = make_snapshot_platform(sb.dir.string());
        const auto o = check_rule_update(p->fs(), "2026.11.12");
        CHECK(o.state == State::Rejected && o.reason == "invalid_rules", "Codes-Update: eine Fehlercode-Datei laesst sich nicht als Regeldatei installieren");
    }

    {  // Installation ohne vorherige Datei, danach Rollback; die Regeln bleiben unberuehrt
        Sandbox sb;
        put_codes(sb, new_text, good_sum(new_text, "error_codes.json"));
        auto p = make_snapshot_platform(sb.dir.string(), /*writable=*/true);
        const auto r = install_rule_update(*p, shipped_set.db_version, ts, nullptr, kind);
        CHECK(r.ok && r.new_version == new_ver && r.backup_path.empty(), "Codes-Update: Installation ohne bisherige Datei");
        CHECK(slurp(sb.real(kErrorCodesPath)) == new_text, "Codes-Update: die aktive Datei ist die neue");
        const auto tree = sb.tree();
        CHECK(tree.count("fs/data/ps5medic/knowledge/update/installed/" + ts + "/error_codes.json") == 1 && tree.count("fs/data/ps5medic/knowledge/update/error_codes.json") == 0,
              "Codes-Update: die Update-Datei liegt danach unter installed/, nichts wurde geloescht");
        const std::string log = slurp(sb.real(kRulesUpdateLog));
        CHECK(log.find("\"type\":\"error_codes_update\"") != std::string::npos && log.find("rules_update") == std::string::npos, "Codes-Update: update.log vermerkt die Art");
        CHECK(!fs::exists(sb.real(kRulesPath)), "Codes-Update: die Regeldatei wurde nicht angefasst");

        const auto o = check_rule_update(p->fs(), new_ver, nullptr, nullptr, kind);
        CHECK(o.can_rollback && o.installed_version == new_ver, "Codes-Update: Zuruecknehmen ist moeglich");
        CHECK(!check_rule_update(p->fs(), "2026.11.12").can_rollback, "Codes-Update: fuer die Regeln gibt es nichts zurueckzunehmen");

        const auto rb = rollback_rule_update(*p, "20261109T151500Z", kind);
        CHECK(rb.ok && !fs::exists(sb.real(kErrorCodesPath)) && fs::exists(sb.real("/data/ps5medic/knowledge/backups/error_codes_rolledback_20261109T151500Z.json")),
              "Codes-Update: Rollback legt die Datei beiseite (es galt die mitgelieferte), nichts wird geloescht");
        CHECK(slurp(sb.real(kRulesUpdateLog)).find("error_codes_rollback") != std::string::npos, "Codes-Update: update.log vermerkt den Rollback");
        CHECK(!check_rule_update(p->fs(), shipped_set.db_version, nullptr, nullptr, kind).can_rollback, "Codes-Update: nach dem Rollback nichts mehr zurueckzunehmen");
    }
    {  // Installation mit vorhandener Datei: Sicherung, Rollback stellt sie wieder her
        Sandbox sb;
        const std::string old_text = codes_text_with_version("2026.11.01");
        spit(sb.real(kErrorCodesPath), old_text);
        put_codes(sb, new_text, good_sum(new_text, "error_codes.json"));
        auto p = make_snapshot_platform(sb.dir.string(), true);
        const auto r = install_rule_update(*p, "2026.11.01", ts, nullptr, kind);
        CHECK(r.ok && r.backup_path == "/data/ps5medic/knowledge/backups/error_codes_" + ts + ".json" && slurp(sb.real(r.backup_path)) == old_text, "Codes-Update: die bisherige Datei wird gesichert");
        const auto rb = rollback_rule_update(*p, "20261109T152000Z", kind);
        CHECK(rb.ok && slurp(sb.real(kErrorCodesPath)) == old_text && rb.new_version == "2026.11.01", "Codes-Update: Rollback stellt die gesicherte Datei wieder her");
    }
    {  // Regeln und Fehlercodes sind unabhaengig: eigene Hochwassermarke, eigene Sicherung, eigener Rollback
        Sandbox sb;
        const std::string rules_new = rules_text_with_version("2026.12.20");
        spit(sb.real(update_file("rules.json")), rules_new);
        spit(sb.real(update_file("rules.json.sha256")), good_sum(rules_new, "rules.json"));
        put_codes(sb, new_text, good_sum(new_text, "error_codes.json"));
        auto p = make_snapshot_platform(sb.dir.string(), true);
        CHECK(install_rule_update(*p, "2026.11.12", ts).ok && install_rule_update(*p, shipped_set.db_version, ts, nullptr, kind).ok, "Beides: Regeln und Fehlercodes lassen sich nacheinander installieren");
        // Die Codes-Version (2026.12.01) ist niedriger als die Regel-Version (2026.12.20): fuer die Codes zaehlt nur ihre eigene Marke.
        const std::string codes_next = codes_text_with_version("2026.12.10");
        put_codes(sb, codes_next, good_sum(codes_next, "error_codes.json"));
        CHECK(check_rule_update(p->fs(), new_ver, nullptr, nullptr, kind).state == State::Ready, "Beides: die Version der Regeln blockiert keine Fehlercodes");
        const auto rb = rollback_rule_update(*p, "20261109T153000Z", kind);
        CHECK(rb.ok && slurp(sb.real(kRulesPath)) == rules_new, "Beides: der Codes-Rollback laesst die Regeln, wie sie sind");
        CHECK(check_rule_update(p->fs(), "2026.12.20").can_rollback, "Beides: die Regel-Installation ist weiter zuruecknehmbar");
    }
    {  // ungueltiger Zeitstempel, kein Schreibzugriff
        Sandbox sb;
        put_codes(sb, new_text, good_sum(new_text, "error_codes.json"));
        auto p = make_snapshot_platform(sb.dir.string(), true);
        CHECK(install_rule_update(*p, "", "../x", nullptr, kind).error == "bad_timestamp", "Codes-Update: ungueltiger Zeitstempel");
        auto ro = make_snapshot_platform(sb.dir.string(), false);
        CHECK(install_rule_update(*ro, "", ts, nullptr, kind).error == "no_write", "Codes-Update: ohne Schreibzugriff nichts");
    }
    {  // Signatur: nur mit eingebautem bzw. angegebenem Schluessel, wie bei den Regeln
        Ed25519Seed seed;
        seed.fill(0x44);
        const Ed25519KeyPair key = ed25519_keypair_from_seed(seed);
        const std::vector<TrustedKey> trusted = {make_trusted_key(key.pk)};
        Sandbox sb;
        spit(sb.real(update_file("error_codes.json")), new_text);
        spit(sb.real(update_file("error_codes.json.sig")), make_signature_file(key, new_text));
        auto p = make_snapshot_platform(sb.dir.string(), true);
        const auto o = check_rule_update(p->fs(), shipped_set.db_version, nullptr, &trusted, kind);
        CHECK(o.state == State::Ready && o.auth == "signature" && o.signed_by == key_id_of(key.pk), "Codes-Update: gueltige Signatur genuegt");
        CHECK(check_rule_update(p->fs(), shipped_set.db_version, nullptr, nullptr, kind).reason == "sig_unknown_key", "Codes-Update: ohne vertrauten Schluessel abgelehnt");
        spit(sb.real(update_file("error_codes.json")), new_text + " ");
        CHECK(check_rule_update(p->fs(), shipped_set.db_version, nullptr, &trusted, kind).reason == "sig_invalid", "Codes-Update: veraenderte Datei -> Signatur ungueltig");
        // Eine Signatur ueber die Regeldatei passt nicht zur Fehlercode-Datei.
        spit(sb.real(update_file("error_codes.json")), new_text);
        spit(sb.real(update_file("error_codes.json.sig")), make_signature_file(key, rules_text_with_version("2030.01.01")));
        CHECK(check_rule_update(p->fs(), shipped_set.db_version, nullptr, &trusted, kind).reason == "sig_invalid", "Codes-Update: die Signatur einer anderen Datei gilt nicht");
    }
}

void download_tests() {
    const std::string ts = "20261109T160000Z";
    const std::string base = "http://192.168.1.50:8000/ps5medic";
    const std::string rules = rules_text_with_version("2026.12.30"), codes = codes_text_with_version("2026.12.30");
    Ed25519Seed seed;
    seed.fill(0x55);
    const Ed25519KeyPair key = ed25519_keypair_from_seed(seed);
    const std::vector<TrustedKey> trusted = {make_trusted_key(key.pk)};
    auto serve_rules = [&](FakeHttp& h) {
        h.answers[base + "/rules.json"] = ok200(rules);
        h.answers[base + "/rules.json.sig"] = ok200(make_signature_file(key, rules));
    };
    auto serve_codes = [&](FakeHttp& h) {
        h.answers[base + "/error_codes.json"] = ok200(codes);
        h.answers[base + "/error_codes.json.sig"] = ok200(make_signature_file(key, codes));
    };

    {  // der Server bietet beide an: vier Abrufe, beide Paare im Update-Ordner, danach angeboten
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        serve_rules(http);
        serve_codes(http);
        const auto r = download_rule_update(*p, http, base, ts);
        CHECK(r.ok && r.codes_ok && r.codes_error.empty(), "Codes-Download: Regeln und Fehlercodes geholt");
        CHECK(http.asked == std::vector<std::string>({base + "/rules.json", base + "/rules.json.sig", base + "/error_codes.json", base + "/error_codes.json.sig"}),
              "Codes-Download: genau vier Abrufe in fester Reihenfolge, alle zur eingestellten Adresse");
        CHECK(slurp(sb.real(update_file("error_codes.json"))) == codes && !slurp(sb.real(update_file("error_codes.json.sig"))).empty(), "Codes-Download: beide Dateien liegen im Update-Ordner");
        const std::string log = slurp(sb.real(kRulesUpdateLog));
        CHECK(log.find("error_codes_download") != std::string::npos && log.find("rules_download") != std::string::npos, "Codes-Download: update.log vermerkt beide Downloads");
        CHECK(check_rule_update(p->fs(), "", nullptr, &trusted, KnowledgeKind::ErrorCodes).state == RuleUpdateOffer::State::Ready, "Codes-Download: danach mit Signatur angeboten");
        CHECK(!fs::exists(sb.real(kErrorCodesPath)) && !fs::exists(sb.real(kRulesPath)), "Codes-Download: der Download allein aendert nichts Aktives");
    }
    {  // Server ohne Fehlercodes: 404 bei der Datei selbst ist kein Fehler
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        serve_rules(http);
        const auto r = download_rule_update(*p, http, base, ts);
        CHECK(r.ok && !r.codes_ok && r.codes_error.empty() && http.asked.size() == 3, "Codes-Download: ohne Fehlercodes beim Server bleibt es beim Regel-Update (kein Fehler)");
        CHECK(!fs::exists(sb.real(update_file("error_codes.json"))), "Codes-Download: dann liegt auch nichts davon im Update-Ordner");
    }
    {  // Datei da, Signatur fehlt: nichts ungeprueft annehmen, aber der Regel-Download gilt
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        serve_rules(http);
        http.answers[base + "/error_codes.json"] = ok200(codes);
        const auto r = download_rule_update(*p, http, base, ts);
        CHECK(r.ok && !r.codes_ok && r.codes_error == "http_status" && r.codes_file == "error_codes.json.sig", "Codes-Download: fehlende Signatur wird gemeldet, der Regel-Download bleibt gueltig");
        CHECK(!fs::exists(sb.real(update_file("error_codes.json"))) && fs::exists(sb.real(update_file("rules.json"))), "Codes-Download: ohne Signatur wird die Fehlercode-Datei nicht abgelegt");
    }
    {  // Netzfehler bei den Fehlercodes
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        serve_rules(http);
        http.answers[base + "/error_codes.json"] = net_error("timeout");
        const auto r = download_rule_update(*p, http, base, ts);
        CHECK(r.ok && !r.codes_ok && r.codes_error == "net_timeout" && r.codes_file == "error_codes.json", "Codes-Download: Netzfehler nur bei den Fehlercodes: Regeln gelten, Grund wird gemeldet");
    }
    {  // Scheitert schon der Regel-Download, werden die Fehlercodes gar nicht erst abgefragt
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        serve_codes(http);
        const auto r = download_rule_update(*p, http, base, ts);
        CHECK(!r.ok && http.asked.size() == 1 && !r.codes_ok, "Codes-Download: ohne Regeln kein weiterer Abruf");
    }
    {  // vorhandene Fehlercode-Dateien im Update-Ordner werden beiseite gelegt, nicht ueberschrieben
        Sandbox sb;
        spit(sb.real(update_file("error_codes.json")), "alt");
        spit(sb.real(update_file("error_codes.json.sha256")), "alt-summe");
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        serve_rules(http);
        serve_codes(http);
        const auto r = download_rule_update(*p, http, base, ts);
        const std::string aside = std::string(kRulesUpdateDir) + "/replaced/" + ts;
        CHECK(r.ok && r.codes_ok && slurp(sb.real(aside + "/error_codes.json")) == "alt" && slurp(sb.real(aside + "/error_codes.json.sha256")) == "alt-summe" &&
                  slurp(sb.real(update_file("error_codes.json"))) == codes,
              "Codes-Download: Altes wandert nach replaced/, nichts wird geloescht");
    }
}

}  // namespace

void codes_update_tests() {
    core_tests();
    download_tests();
}
