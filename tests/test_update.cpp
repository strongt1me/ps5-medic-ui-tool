// Tests fuer das manuelle Regel-Update (libmedic/src/rule_update.cpp).
#include "check.hpp"
#include "medic/platform.hpp"
#include "medic/rule_update.hpp"
#include "medic/rules.hpp"
#include "medic/sha256.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace medic;
using nlohmann::json;

namespace {

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void spit(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

// Leerer Snapshot-Ordner; die virtuelle Konsole liegt unter <dir>/fs.
struct Sandbox {
    fs::path dir;
    Sandbox() {
        static int n = 0;
        dir = fs::temp_directory_path() / ("medic-update-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++n));
        fs::remove_all(dir);
        fs::create_directories(dir / "fs");
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    fs::path real(const std::string& vpath) const { return dir / "fs" / vpath.substr(1); }
    std::set<std::string> tree() const {
        std::set<std::string> t;
        for (auto& e : fs::recursive_directory_iterator(dir)) t.insert(fs::relative(e.path(), dir).generic_string());
        return t;
    }
};

const std::string kShipped = std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json";

// Die ausgelieferten Regeln mit anderer Version.
std::string rules_text_with_version(const std::string& version) {
    json j = json::parse(slurp(kShipped));
    j["db_version"] = version;
    return j.dump(2) + "\n";
}

void put_update(const Sandbox& sb, const std::string& text, const std::string& sum_line) {
    spit(sb.real(std::string(kRulesUpdateDir) + "/rules.json"), text);
    if (!sum_line.empty()) spit(sb.real(std::string(kRulesUpdateDir) + "/rules.json.sha256"), sum_line);
}

std::string good_sum(const std::string& text) { return sha256_hex(text) + "  rules.json\n"; }

}  // namespace

void update_tests() {
    const std::string ts = "20261109T100000Z";
    const std::string new_ver = "2026.12.01";
    const std::string old_text = slurp(kShipped), new_text = rules_text_with_version(new_ver);
    const std::string old_ver = load_rules_from_string(old_text).db_version;  // die mitgelieferte Version, nicht fest verdrahtet
    const std::size_t shipped_count = load_rules_from_string(old_text).rules.size();

    using State = RuleUpdateOffer::State;

    {  // keine Update-Datei
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string());
        const auto o = check_rule_update(p->fs(), old_ver);
        CHECK(o.state == State::None && !o.can_rollback && o.old_version == old_ver, "Update: ohne Dateien passiert nichts");
    }
    {  // gueltiges, neueres Update
        Sandbox sb;
        put_update(sb, new_text, good_sum(new_text));
        auto p = make_snapshot_platform(sb.dir.string());
        std::string verified;
        const auto o = check_rule_update(p->fs(), old_ver, &verified);
        CHECK(o.state == State::Ready && o.new_version == new_ver && o.new_rule_count == shipped_count && o.sha256 == sha256_hex(new_text) && verified == new_text,
              "Update: gueltige Datei mit passender Pruefsumme wird angeboten");
    }
    {  // Pruefsummen-Datei: Grossbuchstaben und blosser Hash ohne Dateinamen
        Sandbox sb;
        std::string upper = sha256_hex(new_text);
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
        put_update(sb, new_text, "\n" + upper + "\n");
        auto p = make_snapshot_platform(sb.dir.string());
        CHECK(check_rule_update(p->fs(), old_ver).state == State::Ready, "Update: Pruefsumme in Grossbuchstaben und ohne Dateiname genuegt");
    }
    struct Reject {
        const char* name;
        std::string text, sum, reason;
    };
    const std::string wrong = std::string(64, 'a') + "  rules.json\n";
    const Reject rejects[] = {
        {"falsche Pruefsumme", new_text, wrong, "checksum_mismatch"},
        {"Pruefsumme einer anderen Datei", new_text, good_sum(old_text), "checksum_mismatch"},
        {"abgeschnittene Datei", new_text.substr(0, new_text.size() / 2), good_sum(new_text), "checksum_mismatch"},
        {"keine Pruefsummen-Datei", new_text, "", "no_checksum"},
        {"kaputte Pruefsummen-Datei", new_text, "das ist kein hash\n", "no_checksum"},
        {"zu kurzer Hash", new_text, sha256_hex(new_text).substr(0, 63) + "\n", "no_checksum"},
        {"kein gueltiges JSON", "kein json", good_sum("kein json"), "invalid_rules"},
        {"Schema verletzt", R"({"schema_version":2,"db_version":"2027.01.01","rules":[]})", good_sum(R"({"schema_version":2,"db_version":"2027.01.01","rules":[]})"), "invalid_rules"},
        {"gleiche Version", old_text, good_sum(old_text), "not_newer"},
        {"aeltere Version", rules_text_with_version("2025.01.01"), good_sum(rules_text_with_version("2025.01.01")), "not_newer"},
    };
    for (const auto& rj : rejects) {
        Sandbox sb;
        put_update(sb, rj.text, rj.sum);
        auto p = make_snapshot_platform(sb.dir.string());
        const auto o = check_rule_update(p->fs(), old_ver);
        CHECK(o.state == State::Rejected && o.reason == rj.reason, std::string("Update abgelehnt: ") + rj.name + " (" + o.reason + ")");
    }
    {  // Schema-Fehler nennt den Grund
        Sandbox sb;
        const std::string bad = R"({"schema_version":1,"db_version":"2027.01.01","rules":[{"id":"A B"}]})";
        put_update(sb, bad, good_sum(bad));
        auto p = make_snapshot_platform(sb.dir.string());
        const auto o = check_rule_update(p->fs(), old_ver);
        CHECK(o.reason == "invalid_rules" && !o.detail.empty(), "Update: ungueltige Regel nennt die Meldung des Pruefers");
    }
    {  // nur die Pruefsumme ohne Regeldatei; zu grosse Datei
        Sandbox sb;
        spit(sb.real(std::string(kRulesUpdateDir) + "/rules.json.sha256"), good_sum(new_text));
        auto p = make_snapshot_platform(sb.dir.string());
        CHECK(check_rule_update(p->fs(), old_ver).reason == "no_rules_file", "Update: Pruefsumme ohne Regeldatei");
        Sandbox big;
        const std::string huge(kMaxRulesBytes + 1, 'x');
        put_update(big, huge, good_sum(huge));
        auto pb = make_snapshot_platform(big.dir.string());
        CHECK(check_rule_update(pb->fs(), old_ver).reason == "too_large", "Update: zu grosse Datei");
    }

    {  // Installation mit vorhandener aktiver Datei, danach Rollback
        Sandbox sb;
        spit(sb.real(kRulesPath), old_text);
        put_update(sb, new_text, good_sum(new_text));
        auto p = make_snapshot_platform(sb.dir.string(), /*writable=*/true);
        const auto r = install_rule_update(*p, old_ver, ts);
        CHECK(r.ok && r.new_version == new_ver && r.backup_path == "/data/ps5medic/knowledge/backups/rules_" + ts + ".json", "Update: Installation gelingt");
        CHECK(slurp(sb.real(kRulesPath)) == new_text, "Update: die aktive Datei ist die neue");
        CHECK(fs::exists(sb.real(r.backup_path)) && slurp(sb.real(r.backup_path)) == old_text, "Update: die bisherige Datei liegt unveraendert im Backup");
        CHECK(!fs::exists(sb.real(std::string(kRulesUpdateDir) + "/rules.json")) && !fs::exists(sb.real(std::string(kRulesUpdateDir) + "/rules.json.sha256")),
              "Update: die Update-Dateien liegen nicht mehr im Eingangsordner");
        CHECK(slurp(sb.real(std::string(kRulesUpdateDir) + "/installed/" + ts + "/rules.json")) == new_text &&
                  fs::exists(sb.real(std::string(kRulesUpdateDir) + "/installed/" + ts + "/rules.json.sha256")),
              "Update: ... sondern beiseite gelegt, nicht geloescht");
        const std::string log = slurp(sb.real(kRulesUpdateLog));
        CHECK(log.find("\"type\":\"rules_update\"") != std::string::npos && log.find(sha256_hex(new_text)) != std::string::npos, "Update: update.log haelt die Installation fest");

        const auto after = check_rule_update(p->fs(), new_ver);
        CHECK(after.state == State::None && after.can_rollback && after.installed_version == new_ver, "Update: danach nichts mehr angeboten, Rollback moeglich");

        const auto rb = rollback_rule_update(*p, "20261109T110000Z");
        CHECK(rb.ok && rb.new_version == old_ver && slurp(sb.real(kRulesPath)) == old_text, "Rollback: die bisherige Datei ist wieder aktiv, Byte fuer Byte");
        CHECK(slurp(sb.real(rb.backup_path)) == new_text, "Rollback: die ersetzte Datei bleibt erhalten");
        CHECK(!check_rule_update(p->fs(), old_ver).can_rollback && !rollback_rule_update(*p, "20261109T120000Z").ok &&
                  rollback_rule_update(*p, "20261109T120000Z").error == "nothing_to_roll_back",
              "Rollback: ein zweites Mal ist nichts mehr zurueckzunehmen");
        CHECK(slurp(sb.real(kRulesPath)) == old_text, "Rollback: der Versuch eines zweiten Rollbacks aendert nichts");
    }
    {  // Erstinstallation ohne aktive Datei (es galten die mitgelieferten Regeln)
        Sandbox sb;
        put_update(sb, new_text, good_sum(new_text));
        auto p = make_snapshot_platform(sb.dir.string(), true);
        const auto r = install_rule_update(*p, old_ver, ts);
        CHECK(r.ok && r.backup_path.empty() && slurp(sb.real(kRulesPath)) == new_text, "Update: Erstinstallation ohne Backup");
        CHECK(check_rule_update(p->fs(), new_ver).can_rollback, "Update: Rollback auch nach der Erstinstallation");
        const auto rb = rollback_rule_update(*p, "20261109T110000Z");
        CHECK(rb.ok && !fs::exists(sb.real(kRulesPath)) && fs::exists(sb.real("/data/ps5medic/knowledge/backups/rules_rolledback_20261109T110000Z.json")),
              "Rollback: die Datei wird beiseite gelegt (es gelten wieder die mitgelieferten Regeln)");
    }
    {  // Verweigerungen aendern nichts am Dateibaum
        Sandbox sb;
        spit(sb.real(kRulesPath), old_text);
        put_update(sb, new_text, wrong);  // falsche Pruefsumme
        auto p = make_snapshot_platform(sb.dir.string(), true);
        const auto before = sb.tree();
        const auto r = install_rule_update(*p, old_ver, ts);
        CHECK(!r.ok && r.error == "checksum_mismatch" && sb.tree() == before && slurp(sb.real(kRulesPath)) == old_text, "Update: falsche Pruefsumme -> nichts wird veraendert");
        CHECK(install_rule_update(*p, old_ver, "../x").error == "bad_timestamp" && install_rule_update(*p, old_ver, "").error == "bad_timestamp" && sb.tree() == before,
              "Update: ungueltiger Zeitstempel -> nichts wird veraendert");
        CHECK(rollback_rule_update(*p, ts).error == "nothing_to_roll_back" && sb.tree() == before, "Rollback ohne Installation: nichts zu tun");

        Sandbox ro;
        put_update(ro, new_text, good_sum(new_text));
        auto pr = make_snapshot_platform(ro.dir.string(), /*writable=*/false);
        const auto before_ro = ro.tree();
        CHECK(install_rule_update(*pr, old_ver, ts).error == "no_write" && rollback_rule_update(*pr, ts).error == "no_write" && ro.tree() == before_ro,
              "Update: ohne Schreibzugriff wird nichts veraendert");
    }
    {  // Es werden nur Dateien unter /data/ps5medic/knowledge angelegt oder bewegt
        Sandbox sb;
        spit(sb.real(kRulesPath), old_text);
        spit(sb.real("/data/shadowmount/config.ini"), "debug=1\n");
        put_update(sb, new_text, good_sum(new_text));
        const auto before = sb.tree();
        auto p = make_snapshot_platform(sb.dir.string(), true);
        CHECK(install_rule_update(*p, old_ver, ts).ok, "Update: Installation im vollen Baum");
        std::vector<std::string> changed;
        for (auto& t : sb.tree())
            if (!before.count(t)) changed.push_back(t);
        for (auto& t : before)
            if (!sb.tree().count(t)) changed.push_back(t);
        CHECK(std::all_of(changed.begin(), changed.end(), [](const std::string& t) { return t.rfind("fs/data/ps5medic/knowledge", 0) == 0; }) &&
                  slurp(sb.real("/data/shadowmount/config.ini")) == "debug=1\n",
              "Update: es aendert sich nichts ausserhalb von /data/ps5medic/knowledge");
    }
}
