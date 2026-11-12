// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/http.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace medic {

namespace {

bool host_char_ok(unsigned char c) { return std::isalnum(c) || c == '.' || c == '-'; }

}  // namespace

bool parse_http_url(const std::string& url, HttpUrl& out, std::string& error) {
    error.clear();
    std::string lower_prefix = url.substr(0, 8);
    std::transform(lower_prefix.begin(), lower_prefix.end(), lower_prefix.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower_prefix == "https://") {
        error = "https_unsupported";
        return false;
    }
    if (lower_prefix.substr(0, 7) != "http://") {
        error = "bad_url";
        return false;
    }
    std::string rest = url.substr(7);
    if (const auto hash = rest.find('#'); hash != std::string::npos) rest.erase(hash);
    std::string hostport = rest, path = "/";
    if (const auto slash = rest.find('/'); slash != std::string::npos) {
        hostport = rest.substr(0, slash);
        path = rest.substr(slash);
    }
    for (unsigned char c : path)
        if (c <= ' ' || c == 0x7f) {  // Leerzeichen und Steuerzeichen haetten in der Anfragezeile nichts verloren
            error = "bad_url";
            return false;
        }
    std::string host = hostport;
    int port = 80;
    if (const auto colon = hostport.rfind(':'); colon != std::string::npos) {
        host = hostport.substr(0, colon);
        const std::string ps = hostport.substr(colon + 1);
        if (ps.empty() || ps.size() > 5 || !std::all_of(ps.begin(), ps.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            error = "bad_url";
            return false;
        }
        port = std::atoi(ps.c_str());
        if (port < 1 || port > 65535) {
            error = "bad_url";
            return false;
        }
    }
    if (host.empty() || host.size() > 253 || !std::all_of(host.begin(), host.end(), [](unsigned char c) { return host_char_ok(c); }) || host.front() == '.' ||
        host.front() == '-') {
        error = "bad_url";
        return false;
    }
    out.host = host;
    out.port = port;
    out.path = path;
    return true;
}

std::string join_url(const std::string& base, const std::string& name) {
    if (base.empty()) return name;
    return base.back() == '/' ? base + name : base + "/" + name;
}

}  // namespace medic
