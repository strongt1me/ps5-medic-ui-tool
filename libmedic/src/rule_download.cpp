// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/rule_download.hpp"

#include "medic/rule_update.hpp"
#include "medic/sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace medic {

namespace {

constexpr int kTimeoutMs = 15000;

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool valid_timestamp(const std::string& ts) {
    return !ts.empty() && ts.size() <= 40 &&
           std::all_of(ts.begin(), ts.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

std::string dir_file(const char* name) { return std::string(kRulesUpdateDir) + "/" + name; }

}  // namespace

std::string configured_update_url(const IFileSystem& fs) {
    std::string text;
    if (!fs.read_file(kConfigPath, text)) return {};
    std::istringstream in(text);
    std::string line, value;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (trim(line.substr(0, eq)) == "update_url") value = trim(line.substr(eq + 1));  // die letzte Zeile gilt
    }
    return value;
}

RuleDownloadResult download_rule_update(Platform& p, IHttpClient& http, const std::string& base_url, const std::string& timestamp) {
    RuleDownloadResult r;
    if (base_url.empty()) {
        r.error = "no_url";
        return r;
    }
    HttpUrl parsed;
    if (!parse_http_url(base_url, parsed, r.error)) return r;  // bad_url oder https_unsupported
    if (!p.fs_rw()) {
        r.error = "no_write";
        return r;
    }
    if (!valid_timestamp(timestamp)) {
        r.error = "bad_timestamp";
        return r;
    }

    // 1. Beide Dateien in den Speicher holen. Scheitert eine, bleibt der Dateibaum unberuehrt.
    struct Item {
        const char* name;
        std::size_t max;
        std::string body;
    } items[] = {{"rules.json", kMaxRulesBytes, {}}, {"rules.json.sig", kMaxSigBytes, {}}};
    for (auto& it : items) {
        const HttpResponse resp = http.get(join_url(base_url, it.name), it.max, kTimeoutMs);
        r.file = it.name;
        if (!resp.error.empty()) {
            r.error = "net_" + resp.error;
            return r;
        }
        r.http_status = resp.status;
        if (resp.status != 200) {
            r.error = "http_status";
            return r;
        }
        it.body = resp.body;
    }
    r.file.clear();
    r.http_status = 0;

    // 2. Was schon im Update-Ordner liegt, beiseite legen (nie loeschen), damit die neue Datei nicht mit alten Resten gemischt wird.
    const std::string aside = dir_file("replaced") + "/" + timestamp;
    for (const char* name : {"rules.json", "rules.json.sha256", "rules.json.sig"})
        if (p.fs().exists(dir_file(name)) && !p.fs_rw()->rename(dir_file(name), aside + "/" + name)) {
            r.error = "write_failed";
            r.file = name;
            return r;
        }

    // 3. Erst die Regeln, dann die Signatur. Bricht es dazwischen ab, fehlt der Signatur-Partner: check_rule_update lehnt das
    //    ab ("no_checksum"), es wird nie etwas ungeprueft angenommen.
    for (auto& it : items) {
        if (!p.fs_rw()->write_file(dir_file(it.name), it.body)) {
            r.error = "write_failed";
            r.file = it.name;
            return r;
        }
    }

    // Spur im Protokoll (update.log): nur Zeitpunkt, Adresse und Pruefsumme. Der Typ stoert die Rollback-Logik nicht.
    std::string log;
    p.fs().read_file(kRulesUpdateLog, log);
    if (!log.empty() && log.back() != '\n') log += '\n';
    log += nlohmann::json({{"type", "rules_download"}, {"ts", timestamp}, {"url", base_url}, {"bytes", items[0].body.size()}, {"sha256", sha256_hex(items[0].body)}}).dump() + "\n";
    p.fs_rw()->write_file(kRulesUpdateLog, log);

    r.ok = true;
    r.bytes = items[0].body.size();

    // 4. Fehlercodes: optional, beide Dateien oder keine. Ein Fehler hier macht den Regel-Download nicht ungueltig.
    Item codes[] = {{"error_codes.json", kMaxRulesBytes, {}}, {"error_codes.json.sig", kMaxSigBytes, {}}};
    bool offered = true;
    for (auto& it : codes) {
        const HttpResponse resp = http.get(join_url(base_url, it.name), it.max, kTimeoutMs);
        if (!resp.error.empty()) {
            r.codes_error = "net_" + resp.error;
            r.codes_file = it.name;
            offered = false;
            break;
        }
        if (resp.status != 200) {
            offered = false;
            if (&it != &codes[0] || resp.status != 404) {  // 404 bei der Datei selbst: der Server bietet keine Fehlercodes an
                r.codes_error = "http_status";
                r.codes_file = it.name;
            }
            break;
        }
        it.body = resp.body;
    }
    if (!offered) return r;
    for (const char* name : {"error_codes.json", "error_codes.json.sha256", "error_codes.json.sig"})
        if (p.fs().exists(dir_file(name)) && !p.fs_rw()->rename(dir_file(name), aside + "/" + name)) {
            r.codes_error = "write_failed";
            r.codes_file = name;
            return r;
        }
    for (auto& it : codes)
        if (!p.fs_rw()->write_file(dir_file(it.name), it.body)) {
            r.codes_error = "write_failed";
            r.codes_file = it.name;
            return r;
        }
    std::string log2;
    p.fs().read_file(kRulesUpdateLog, log2);
    if (!log2.empty() && log2.back() != '\n') log2 += '\n';
    log2 += nlohmann::json({{"type", "error_codes_download"}, {"ts", timestamp}, {"url", base_url}, {"bytes", codes[0].body.size()}, {"sha256", sha256_hex(codes[0].body)}}).dump() + "\n";
    p.fs_rw()->write_file(kRulesUpdateLog, log2);
    r.codes_ok = true;
    return r;
}

}  // namespace medic
