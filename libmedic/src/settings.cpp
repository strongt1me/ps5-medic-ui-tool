// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/settings.hpp"

#include "medic/fixes.hpp"
#include "medic/rule_download.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace medic {

namespace {

bool is_on(const std::string& v, bool dflt) {
    if (v == "on") return true;
    if (v == "off") return false;
    return dflt;
}

bool parse_offset(const std::string& v, int& out) {
    if (v.empty() || v.size() > 5) return false;
    std::size_t i = (v[0] == '-' || v[0] == '+') ? 1 : 0;
    if (i >= v.size()) return false;
    for (std::size_t k = i; k < v.size(); ++k)
        if (v[k] < '0' || v[k] > '9') return false;
    const int n = std::atoi(v.c_str());
    if (n < kUtcOffsetMin || n > kUtcOffsetMax) return false;
    out = n;
    return true;
}

// Tage seit 1970-01-01 (gregorianisch), und zurueck
long days_from_civil(long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
}

void civil_from_days(long z, long& y, unsigned& m, unsigned& d) {
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}

constexpr std::size_t kBackupMaxEntries = 20000;  // mehr zaehlt die Anzeige nicht: bei Abbruch ist die Groesse "unbekannt"

}  // namespace

AppSettings load_app_settings(const IFileSystem& fs) {
    AppSettings s;
    s.large_text = configured_value(fs, "text_size") == "large";
    s.high_contrast = configured_value(fs, "contrast") == "high";
    int off = 0;
    if (parse_offset(configured_value(fs, "utc_offset"), off)) s.utc_offset_min = off;
    s.autostart_check = is_on(configured_value(fs, "autostart_check"), false);
    s.rule_download = is_on(configured_value(fs, "rule_download"), true);
    s.net_test = is_on(configured_value(fs, "net_test"), true);
    s.custom_url = configured_update_url(fs);
    s.custom_source = configured_value(fs, "update_source") != "default";
    s.effective_url = effective_update_url(fs);
    return s;
}

bool set_app_setting(Platform& p, const std::string& key, const std::string& value) {
    bool ok = false;
    if (key == "text_size") ok = value == "normal" || value == "large";
    else if (key == "contrast") ok = value == "normal" || value == "high";
    else if (key == "utc_offset") {
        int n = 0;
        ok = parse_offset(value, n);
    } else if (key == "autostart_check" || key == "rule_download" || key == "net_test") ok = value == "on" || value == "off";
    else if (key == "update_source") ok = value == "default" || value == "custom";
    return ok && set_config_value(p, key, value);
}

BackupUsage backup_usage(const IFileSystem& fs) {
    BackupUsage u;
    if (!fs.is_dir(kBackupRoot)) {  // noch keine Reparatur: nichts belegt
        u.known = true;
        return u;
    }
    std::size_t seen = 0;
    std::vector<std::string> todo;
    for (const auto& e : fs.list_dir(kBackupRoot)) {
        if (e.is_dir) {
            ++u.sets;
            todo.push_back(std::string(kBackupRoot) + "/" + e.name);
        } else u.bytes += e.size;
        ++seen;
    }
    while (!todo.empty()) {
        const std::string dir = todo.back();
        todo.pop_back();
        for (const auto& e : fs.list_dir(dir)) {
            if (++seen > kBackupMaxEntries) return u;  // known bleibt false
            if (e.is_dir) todo.push_back(dir + "/" + e.name);
            else u.bytes += e.size;
        }
    }
    u.known = true;
    return u;
}

std::string shift_timestamp(const std::string& ts, int offset_min) {
    // YYYYMMDDTHHMMSS[Z]
    if (ts.size() < 15 || ts[8] != 'T') return ts;
    for (int i : {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14})
        if (ts[static_cast<std::size_t>(i)] < '0' || ts[static_cast<std::size_t>(i)] > '9') return ts;
    auto num = [&](std::size_t pos, std::size_t len) { return std::atoi(ts.substr(pos, len).c_str()); };
    const long y = num(0, 4);
    const unsigned mo = static_cast<unsigned>(num(4, 2)), d = static_cast<unsigned>(num(6, 2));
    const int hh = num(9, 2), mi = num(11, 2), ss = num(13, 2);
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || hh > 23 || mi > 59 || ss > 60) return ts;
    long secs = days_from_civil(y, mo, d) * 86400L + hh * 3600L + mi * 60L + ss + static_cast<long>(offset_min) * 60L;
    long days = secs / 86400;
    long rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    long ny;
    unsigned nm, nd;
    civil_from_days(days, ny, nm, nd);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%04ld%02u%02uT%02ld%02ld%02ld", ny, nm, nd, rem / 3600, rem % 3600 / 60, rem % 60);
    return std::string(buf) + ts.substr(15);
}

}  // namespace medic
