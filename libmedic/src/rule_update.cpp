// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/rule_update.hpp"

#include "medic/rules.hpp"
#include "medic/sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace medic {
namespace {

using json = nlohmann::json;

std::string join(const std::string& dir, const std::string& name) { return dir + "/" + name; }

std::string update_rules_path() { return join(kRulesUpdateDir, "rules.json"); }
std::string update_sum_path() { return join(kRulesUpdateDir, "rules.json.sha256"); }
std::string update_sig_path() { return join(kRulesUpdateDir, "rules.json.sig"); }

bool valid_timestamp(const std::string& ts) {
    return !ts.empty() && ts.size() <= 40 &&
           std::all_of(ts.begin(), ts.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Erstes Wort der ersten nicht leeren Zeile, wenn es genau 64 Hex-Ziffern sind (Format von `sha256sum`: "<hash>  <datei>").
std::string parse_checksum_file(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string first;
        if (!(words >> first)) continue;
        const bool hex64 = first.size() == 64 && std::all_of(first.begin(), first.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
        return hex64 ? lower(first) : std::string();
    }
    return {};
}

// update.log: eine JSON-Zeile je Ereignis. Liefert die letzte Installation, die noch nicht zurueckgenommen wurde.
bool last_open_install(const IFileSystem& fs, json& out) {
    std::string text;
    if (!fs.read_file(kRulesUpdateLog, text)) return false;
    std::istringstream in(text);
    std::string line;
    bool open = false;
    json last;
    while (std::getline(in, line)) {
        json j = json::parse(line, nullptr, false);
        if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) continue;
        if (j["type"] == "rules_update") {
            last = j;
            open = true;
        } else if (j["type"] == "rules_rollback") {
            open = false;
        }
    }
    if (open) out = last;
    return open;
}

bool append_log(Platform& p, const json& rec) {
    std::string text;
    p.fs().read_file(kRulesUpdateLog, text);
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += rec.dump() + "\n";
    return p.fs_rw()->write_file(kRulesUpdateLog, text);
}

std::string str_of(const json& j, const char* key) { return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string(); }

}  // namespace

RuleUpdateOffer check_rule_update(const IFileSystem& fs, const std::string& current_version, std::string* verified_text,
                                  const std::vector<TrustedKey>* keys) {
    RuleUpdateOffer o;
    o.old_version = current_version;

    if (json open; last_open_install(fs, open)) {
        const std::string backup = str_of(open, "backup");
        o.can_rollback = backup.empty() ? fs.exists(kRulesPath) : fs.exists(backup);
        o.installed_version = str_of(open, "to");
    }

    const bool has_rules = fs.exists(update_rules_path()), has_sum = fs.exists(update_sum_path()), has_sig = fs.exists(update_sig_path());
    if (!has_rules && !has_sum && !has_sig) return o;  // State::None
    auto reject = [&](const char* reason) {
        o.state = RuleUpdateOffer::State::Rejected;
        o.reason = reason;
        return o;
    };
    if (!has_rules) return reject("no_rules_file");

    std::string text;
    if (!fs.read_range(update_rules_path(), 0, kMaxRulesBytes + 1, text)) return reject("unreadable");
    if (text.size() > kMaxRulesBytes) return reject("too_large");

    o.sha256 = sha256_hex(text);
    if (has_sig) {
        // Eine Signatur entscheidet allein; eine daneben liegende Pruefsumme zaehlt nicht (sie koennte vom selben Server stammen).
        std::string sig_text;
        if (!fs.read_range(update_sig_path(), 0, 4096, sig_text)) return reject("unreadable");
        const SigInfo si = verify_rules_signature(text, sig_text, keys ? *keys : trusted_rule_keys());
        switch (si.result) {
            case SigResult::Valid: break;
            case SigResult::Malformed: return reject("sig_malformed");
            case SigResult::UnknownKey: return reject("sig_unknown_key");
            case SigResult::BadSignature: return reject("sig_invalid");
        }
        o.auth = "signature";
        o.signed_by = si.key_id;
    } else {
        std::string sum_text;
        if (!has_sum || !fs.read_file(update_sum_path(), sum_text)) return reject("no_checksum");
        const std::string expected = parse_checksum_file(sum_text);
        if (expected.empty()) return reject("no_checksum");
        if (o.sha256 != expected) return reject("checksum_mismatch");
        o.auth = "checksum";
    }

    RuleSet rs;
    try {
        rs = load_rules_from_string(text);
    } catch (const std::exception& e) {
        o.detail = e.what();
        return reject("invalid_rules");
    }
    o.new_version = rs.db_version;
    o.new_rule_count = rs.rules.size();
    if (compare_versions(rs.db_version, current_version) <= 0) return reject("not_newer");

    o.state = RuleUpdateOffer::State::Ready;
    if (verified_text) *verified_text = std::move(text);
    return o;
}

RuleUpdateResult install_rule_update(Platform& p, const std::string& current_version, const std::string& timestamp,
                                     const std::vector<TrustedKey>* keys) {
    RuleUpdateResult r;
    if (!p.fs_rw()) {
        r.error = "no_write";
        return r;
    }
    if (!valid_timestamp(timestamp)) {
        r.error = "bad_timestamp";
        return r;
    }
    std::string text;
    const RuleUpdateOffer offer = check_rule_update(p.fs(), current_version, &text, keys);
    if (offer.state != RuleUpdateOffer::State::Ready) {
        r.error = offer.state == RuleUpdateOffer::State::None ? "no_rules_file" : offer.reason;
        return r;
    }

    // 1. Bisherige Datei sichern (und das Backup gegenlesen), bevor etwas veraendert wird.
    std::string active;
    std::string backup_path, backup_sha;
    if (p.fs().read_file(kRulesPath, active)) {
        backup_path = join(kRulesBackupDir, "rules_" + timestamp + ".json");
        backup_sha = sha256_hex(active);
        std::string again;
        if (!p.fs_rw()->write_file(backup_path, active) || !p.fs().read_file(backup_path, again) || sha256_hex(again) != backup_sha) {
            r.error = "backup_failed";
            return r;
        }
    }

    // 2. Neue Datei atomar schreiben und gegenlesen.
    std::string written;
    if (!p.fs_rw()->write_file(kRulesPath, text)) {
        r.error = "write_failed";
        return r;
    }
    if (!p.fs().read_file(kRulesPath, written) || sha256_hex(written) != offer.sha256) {
        if (!backup_path.empty()) p.fs_rw()->write_file(kRulesPath, active);
        r.error = "verify_failed";
        return r;
    }

    // 3. Update-Dateien beiseite legen (nie loeschen), damit sie nicht erneut angeboten werden. Scheitert das, ist es
    //    harmlos: die installierte Version ist dann nicht mehr neuer als die laufende.
    const std::string done = join(join(kRulesUpdateDir, "installed"), timestamp);
    p.fs_rw()->rename(update_rules_path(), join(done, "rules.json"));
    p.fs_rw()->rename(update_sum_path(), join(done, "rules.json.sha256"));
    p.fs_rw()->rename(update_sig_path(), join(done, "rules.json.sig"));

    append_log(p, {{"type", "rules_update"}, {"ts", timestamp}, {"from", current_version}, {"to", offer.new_version},
                   {"sha256", offer.sha256}, {"auth", offer.auth}, {"key", offer.signed_by}, {"backup", backup_path}, {"backup_sha256", backup_sha}});
    r.ok = true;
    r.new_version = offer.new_version;
    r.backup_path = backup_path;
    return r;
}

RuleUpdateResult rollback_rule_update(Platform& p, const std::string& timestamp) {
    RuleUpdateResult r;
    if (!p.fs_rw()) {
        r.error = "no_write";
        return r;
    }
    if (!valid_timestamp(timestamp)) {
        r.error = "bad_timestamp";
        return r;
    }
    json open;
    if (!last_open_install(p.fs(), open)) {
        r.error = "nothing_to_roll_back";
        return r;
    }
    const std::string backup = str_of(open, "backup"), backup_sha = str_of(open, "backup_sha256");
    const std::string aside = join(kRulesBackupDir, "rules_rolledback_" + timestamp + ".json");

    std::string current;
    if (!p.fs().read_file(kRulesPath, current)) {  // die installierte Datei ist nicht mehr da: nichts zu ersetzen
        if (backup.empty()) {
            r.error = "nothing_to_roll_back";
            return r;
        }
        current.clear();
    }

    if (backup.empty()) {
        // Vor dem Update gab es keine Datei im Wissensordner (es galten die mitgelieferten Regeln): beiseite legen.
        if (!p.fs_rw()->rename(kRulesPath, aside)) {
            r.error = "write_failed";
            return r;
        }
    } else {
        std::string old;
        if (!p.fs().read_file(backup, old) || sha256_hex(old) != backup_sha) {
            r.error = "backup_failed";
            return r;
        }
        if (!current.empty() && !p.fs_rw()->write_file(aside, current)) {
            r.error = "write_failed";
            return r;
        }
        if (!p.fs_rw()->write_file(kRulesPath, old)) {
            r.error = "write_failed";
            return r;
        }
        r.backup_path = aside;
    }
    append_log(p, {{"type", "rules_rollback"}, {"ts", timestamp}, {"from", str_of(open, "to")}, {"to", str_of(open, "from")}, {"aside", backup.empty() ? aside : r.backup_path}});
    r.ok = true;
    r.new_version = str_of(open, "from");
    return r;
}

}  // namespace medic
