// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/ini.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace medic {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

struct Line {
    std::string text;
    std::string section;  // Sektion, in der die Zeile steht ("" vor der ersten Sektion)
    bool is_header = false;
    bool is_pair = false;
    std::string key, value;  // key klein geschrieben
    std::string raw_key;
};

struct Doc {
    std::vector<Line> lines;
    std::string eol = "\n";
    bool final_eol = true;
    bool has_headers = false;
};

Doc parse(const std::string& text) {
    Doc d;
    d.eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    d.final_eol = text.empty() || text.back() == '\n';
    std::string cur, section;
    auto flush = [&](const std::string& raw) {
        Line l;
        l.text = raw;
        std::string t = trim(raw);
        if (!t.empty() && t[0] == '[') {
            auto end = t.find(']');
            section = end == std::string::npos ? std::string() : lower(trim(t.substr(1, end - 1)));
            l.is_header = true;
            d.has_headers = true;
        } else if (!t.empty() && t[0] != '#' && t[0] != ';') {
            auto eq = t.find('=');
            if (eq != std::string::npos) {
                l.is_pair = true;
                l.raw_key = trim(t.substr(0, eq));
                l.key = lower(l.raw_key);
                l.value = trim(t.substr(eq + 1));
            }
        }
        l.section = section;
        d.lines.push_back(std::move(l));
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            flush(cur);
            cur.clear();
        } else {
            cur += text[i];
        }
    }
    if (!cur.empty()) flush(cur);
    return d;
}

std::string render(const Doc& d) {
    std::string out;
    for (std::size_t i = 0; i < d.lines.size(); ++i) {
        out += d.lines[i].text;
        if (i + 1 < d.lines.size() || d.final_eol) out += d.eol;
    }
    return out;
}

struct Target {
    std::string section, key;  // key klein
};

Target resolve(const Doc& d, const IniTarget& t) {
    const std::string legacy = lower(t.legacy_key);
    const std::string key = lower(t.key);
    if (!legacy.empty()) {
        bool flat_exists = std::any_of(d.lines.begin(), d.lines.end(),
                                       [&](const Line& l) { return l.is_pair && l.section.empty() && l.key == legacy; });
        if (flat_exists || (!d.has_headers && !t.section.empty())) return {"", legacy};
    }
    return {lower(t.section), key};
}

// Einfuegeposition (Index, vor dem eingefuegt wird) fuer eine neue Zeile im Ziel.
// Rueckgabe true, wenn die Sektion neu angelegt werden muss.
bool insert_position(const Doc& d, const Target& tg, std::size_t& pos) {
    // 1. nach der letzten Zeile desselben Schluessels
    for (std::size_t i = d.lines.size(); i-- > 0;)
        if (d.lines[i].is_pair && d.lines[i].section == tg.section && d.lines[i].key == tg.key) {
            pos = i + 1;
            return false;
        }
    if (tg.section.empty()) {
        // flacher Bereich: vor der ersten Sektion, hinter der letzten nicht-leeren Zeile davor
        std::size_t end = d.lines.size();
        for (std::size_t i = 0; i < d.lines.size(); ++i)
            if (d.lines[i].is_header) {
                end = i;
                break;
            }
        while (end > 0 && trim(d.lines[end - 1].text).empty()) --end;
        pos = end;
        return false;
    }
    // vorhandene Sektion: hinter ihrer letzten nicht-leeren Zeile
    std::size_t last = d.lines.size();
    bool found = false;
    for (std::size_t i = 0; i < d.lines.size(); ++i)
        if (d.lines[i].is_header && d.lines[i].section == tg.section) {
            last = i;
            found = true;
        }
    if (!found) return true;
    for (std::size_t i = last + 1; i < d.lines.size() && !d.lines[i].is_header; ++i)
        if (!trim(d.lines[i].text).empty()) last = i;
    pos = last + 1;
    return false;
}

void insert_line(Doc& d, const Target& tg, const std::string& raw_key, const std::string& value) {
    std::size_t pos = 0;
    Line nl;
    nl.is_pair = true;
    nl.section = tg.section;
    nl.raw_key = raw_key;
    nl.key = tg.key;
    nl.value = value;
    nl.text = raw_key + "=" + value;
    if (insert_position(d, tg, pos)) {
        if (!d.lines.empty() && !trim(d.lines.back().text).empty()) d.lines.push_back(Line{});
        Line h;
        h.is_header = true;
        h.section = tg.section;
        h.text = "[" + tg.section + "]";
        d.lines.push_back(std::move(h));
        d.lines.push_back(std::move(nl));
        d.has_headers = true;
        d.final_eol = true;
        return;
    }
    d.lines.insert(d.lines.begin() + static_cast<std::ptrdiff_t>(pos), std::move(nl));
    if (pos + 1 == d.lines.size()) d.final_eol = true;
}

}  // namespace

std::string ini_set_value(const std::string& text, const IniTarget& t, const std::string& value) {
    Doc d = parse(text);
    Target tg = resolve(d, t);
    for (std::size_t i = d.lines.size(); i-- > 0;) {
        Line& l = d.lines[i];
        if (l.is_pair && l.section == tg.section && l.key == tg.key) {
            if (l.value == value) return text;  // schon gesetzt
            l.text = l.raw_key + "=" + value;
            l.value = value;
            return render(d);
        }
    }
    insert_line(d, tg, tg.section.empty() ? (lower(t.legacy_key) == tg.key ? t.legacy_key : t.key) : t.key, value);
    return render(d);
}

std::string ini_add_value(const std::string& text, const IniTarget& t, const std::string& value) {
    Doc d = parse(text);
    Target tg = resolve(d, t);
    for (const Line& l : d.lines)
        if (l.is_pair && l.section == tg.section && l.key == tg.key && l.value == value) return text;
    insert_line(d, tg, tg.section.empty() ? (lower(t.legacy_key) == tg.key ? t.legacy_key : t.key) : t.key, value);
    return render(d);
}

}  // namespace medic
