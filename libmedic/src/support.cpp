// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/support.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <set>

namespace medic {
namespace {

using json = nlohmann::json;

bool valid_ts(const std::string& ts) {
    return !ts.empty() && ts.size() <= 32 && std::all_of(ts.begin(), ts.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

std::string base_name(const std::string& path) {
    std::string p = path;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    const auto s = p.rfind('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}

std::string strip_ext(const std::string& name) {
    const auto d = name.rfind('.');
    return d == std::string::npos || d == 0 ? name : name.substr(0, d);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Namen, die kein Spiel verraten und ueberall vorkommen (sonst wuerde "games" im ganzen Bericht ersetzt)
bool generic_name(const std::string& n) {
    static const std::set<std::string> kGeneric = {"data", "homebrew", "games", "game", "ps5", "ps4", "usb0", "usb1", "ext0", "ext1", "mnt", "user", "system", "shadowmount",
                                                   "sce_sys", "sce_module", "fakelib", "fakelib2", "prx", "eboot", "param", "config", "images", "image", "backups", "apps", "app"};
    return kGeneric.count(lower(n)) != 0;
}

std::string two_digits(std::size_t i) {
    char b[16];
    std::snprintf(b, sizeof b, "%02u", static_cast<unsigned>(i + 1));
    return b;
}

// "PPSA12345" / "CUSA12345" (und die Kennung mit Unterstrich/Bindestrich-Rahmen) im Text finden
std::vector<std::string> find_title_ids(const std::string& s) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i + 9 <= s.size(); ++i) {
        const bool pfx = s.compare(i, 4, "PPSA") == 0 || s.compare(i, 4, "CUSA") == 0;
        if (!pfx) continue;
        if (i > 0 && std::isalnum(static_cast<unsigned char>(s[i - 1]))) continue;
        bool digits = true;
        for (std::size_t k = 4; k < 9; ++k)
            if (!std::isdigit(static_cast<unsigned char>(s[i + k]))) digits = false;
        if (!digits) continue;
        if (i + 9 < s.size() && std::isalnum(static_cast<unsigned char>(s[i + 9]))) continue;
        out.push_back(s.substr(i, 9));
    }
    return out;
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

void put_le16(std::string& o, std::uint16_t v) {
    o.push_back(static_cast<char>(v & 0xff));
    o.push_back(static_cast<char>(v >> 8));
}
void put_le32(std::string& o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

}  // namespace

std::string support_path_for(const std::string& ts) { return valid_ts(ts) ? std::string(kSupportDir) + "/support_" + ts + ".zip" : std::string(); }
std::string support_key_path_for(const std::string& ts) { return valid_ts(ts) ? std::string(kSupportDir) + "/support_" + ts + "_key.txt" : std::string(); }

std::string Pseudonyms::title_placeholder(std::size_t i) const { return "[TITLE-" + two_digits(i) + "]"; }
std::string Pseudonyms::name_placeholder(std::size_t i) const { return "[GAME-" + two_digits(i) + "]"; }

void Pseudonyms::add_title_id(const std::string& id) {
    if (id.empty() || std::find(titles_.begin(), titles_.end(), id) != titles_.end()) return;
    titles_.push_back(id);
}

void Pseudonyms::add_name(const std::string& name) {
    if (name.size() < 4 || generic_name(name) || std::find(names_.begin(), names_.end(), name) != names_.end()) return;
    names_.push_back(name);
}

void Pseudonyms::collect(const Facts& facts) {
    const auto name_of_path = [&](const json& e) {
        if (!e.is_object() || !e.contains("path") || !e["path"].is_string()) return;
        const std::string b = base_name(e["path"].get<std::string>());
        add_name(b);
        add_name(strip_ext(b));
    };
    const auto walk = [&](const char* fact) {
        const json* a = facts.get(fact);
        if (!a || !a->is_array()) return;
        for (const auto& e : *a) {
            if (!e.is_object()) continue;
            // Homebrew-Kacheln (Titel-ID "FAKE...", auch PS5 Medic selbst) sind keine Spiele und verraten nichts
            if (e.contains("titleid") && e["titleid"].is_string() && e["titleid"].get<std::string>().rfind("FAKE", 0) == 0) continue;
            if (e.contains("titleid") && e["titleid"].is_string()) add_title_id(e["titleid"].get<std::string>());
            if (e.contains("name") && e["name"].is_string()) add_name(e["name"].get<std::string>());
            name_of_path(e);
        }
    };
    for (const char* f : {"smp.titles", "smp.images", "smp.deep_sources", "games.runtime", "games.pack_problems", "games.index_problems", "games.noexec", "games.incomplete",
                          "games.partial_folders", "games.unsigned_elf", "games.flex_small", "games.playgo_missing", "games.trophy_missing", "games.fakelib_layout", "games.with_fakelib",
                          "leftovers.files", "kraken.sparse_extent", "kraken.pkg_other", "kraken.pkg_truncated"})
        walk(f);
}

std::string Pseudonyms::apply(const std::string& text) {
    std::string out = text;
    // Titel-IDs aus dem Text selbst aufnehmen (z. B. in Logzeilen), damit jede Kennung einen Platzhalter bekommt
    for (const auto& id : find_title_ids(out)) add_title_id(id);
    // Namen: laengste zuerst, damit "Spiel Name Teil 2" nicht von "Spiel Name" angefressen wird
    std::vector<std::size_t> order(names_.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return names_[a].size() > names_[b].size(); });
    for (std::size_t i : order) replace_all(out, names_[i], name_placeholder(i));
    for (std::size_t i = 0; i < titles_.size(); ++i) replace_all(out, titles_[i], title_placeholder(i));
    return out;
}

std::string Pseudonyms::key_text() const {
    std::string o = "Schluessel (nur fuer dich, NICHT weitergeben) / key (only for you, do NOT share):\n";
    for (std::size_t i = 0; i < titles_.size(); ++i) o += title_placeholder(i) + " = " + titles_[i] + "\n";
    for (std::size_t i = 0; i < names_.size(); ++i) o += name_placeholder(i) + " = " + names_[i] + "\n";
    return o;
}

std::uint32_t crc32_ieee(const std::string& data) {
    // Die Tabelle entsteht einmal beim ersten Aufruf (C++11: die Initialisierung einer lokalen static-Variable ist threadsicher)
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> tb{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tb[i] = c;
        }
        return tb;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = table[(c ^ b) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string make_zip(const std::vector<ZipEntry>& entries) {
    std::string out, central;
    constexpr std::uint16_t kDosTime = 0, kDosDate = (0 << 9) | (1 << 5) | 1;  // 1980-01-01 00:00
    for (const auto& e : entries) {
        const std::uint32_t crc = crc32_ieee(e.data), size = static_cast<std::uint32_t>(e.data.size());
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        // lokaler Kopf
        put_le32(out, 0x04034b50);
        put_le16(out, 20);  // Version
        put_le16(out, 0x0800);  // Flags: Namen UTF-8
        put_le16(out, 0);       // stored
        put_le16(out, kDosTime);
        put_le16(out, kDosDate);
        put_le32(out, crc);
        put_le32(out, size);
        put_le32(out, size);
        put_le16(out, static_cast<std::uint16_t>(e.name.size()));
        put_le16(out, 0);
        out += e.name;
        out += e.data;
        // zentrales Verzeichnis
        put_le32(central, 0x02014b50);
        put_le16(central, 20);
        put_le16(central, 20);
        put_le16(central, 0x0800);
        put_le16(central, 0);
        put_le16(central, kDosTime);
        put_le16(central, kDosDate);
        put_le32(central, crc);
        put_le32(central, size);
        put_le32(central, size);
        put_le16(central, static_cast<std::uint16_t>(e.name.size()));
        put_le16(central, 0);  // Extra
        put_le16(central, 0);  // Kommentar
        put_le16(central, 0);  // Platte
        put_le16(central, 0);  // interne Attribute
        put_le32(central, 0);  // externe Attribute
        put_le32(central, offset);
        central += e.name;
    }
    const std::uint32_t cd_offset = static_cast<std::uint32_t>(out.size());
    out += central;
    put_le32(out, 0x06054b50);
    put_le16(out, 0);
    put_le16(out, 0);
    put_le16(out, static_cast<std::uint16_t>(entries.size()));
    put_le16(out, static_cast<std::uint16_t>(entries.size()));
    put_le32(out, static_cast<std::uint32_t>(central.size()));
    put_le32(out, cd_offset);
    put_le16(out, 0);
    return out;
}

SupportBundle build_support_bundle(const ReportInput& in, const std::string& facts_json, const std::string& compare_text) {
    Pseudonyms ps;
    ps.collect(in.facts);
    // Der Fusssatz des Berichts sagt "Namen sind enthalten"; im Paket sind sie ersetzt
    const auto swap_footer = [](std::string s) {
        const auto rep = [&](const char* from, const char* to) {
            const auto i = s.find(from);
            if (i != std::string::npos) s.replace(i, std::char_traits<char>::length(from), to);
        };
        rep("Datei- und Ordnernamen sind enthalten – vor dem Teilen prüfen.", "Titel-IDs und Namen von Spielen, Ordnern und Images sind durch Platzhalter ersetzt (siehe README).");
        rep("File and folder names are included – check before sharing.", "Title IDs and names of games, folders and images are replaced by placeholders (see README).");
        return s;
    };
    const auto clean = [&](const std::string& s) { return ps.apply(anonymize(s)); };

    std::vector<ZipEntry> files;
    files.push_back({"README.txt", ""});  // Platz fuer die README (steht am Anfang des Pakets), Inhalt unten
    files.push_back({"report_de.md", clean(swap_footer(build_report(in, ReportLang::De)))});
    files.push_back({"report_en.md", clean(swap_footer(build_report(in, ReportLang::En)))});
    if (!facts_json.empty()) files.push_back({"facts.json", clean(facts_json)});
    if (!compare_text.empty()) files.push_back({"compare.txt", clean(compare_text)});

    std::string readme =
        "PS5 Medic - Support-Paket / support package\n"
        "Erstellt / created: " + in.created_utc + "  (Regeln / rules " + in.rules_version + ")\n\n"
        "DE: Dieses Paket enthaelt den Bericht (Deutsch und Englisch) und die gelesenen Fakten. IP-/MAC-Adressen, Konto-IDs und Seriennummern sind\n"
        "    ersetzt. Titel-IDs und Namen von Spielen, Spielordnern und Images stehen als [TITLE-nn] bzw. [GAME-nn]. Der Schluessel dazu liegt\n"
        "    nur bei dir (Datei *_key.txt) und gehoert nicht in dieses Paket. Es sind keine Spielinhalte, Keys oder Firmware enthalten.\n"
        "EN: This package holds the report (German and English) and the facts Medic read. IP/MAC addresses, account IDs and serial numbers are\n"
        "    replaced. Title IDs and names of games, game folders and images appear as [TITLE-nn] / [GAME-nn]. The key stays with you only\n"
        "    (file *_key.txt) and is not part of this package. No game contents, keys or firmware are included.\n\n"
        "report_de.md / report_en.md: Befunde mit Belegen / findings with evidence\n"
        "facts.json: gelesene Fakten zu Spielen, Kraken und kstuff / facts read about games, Kraken and kstuff\n"
        "compare.txt: Vergleich mit dem vorigen Check-up / comparison with the previous check-up (nur wenn vorhanden / only if available)\n";
    files[0].data = readme;

    SupportBundle b;
    b.titles = ps.title_count();
    b.names = ps.name_count();
    b.key = ps.key_text();
    b.zip = make_zip(files);
    return b;
}

}  // namespace medic
