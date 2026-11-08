// SPDX-License-Identifier: GPL-3.0-or-later
// Gemeinsame Hilfen fuer die Update-, Signatur- und Download-Tests: Dateien lesen/schreiben, leere virtuelle Konsole im
// Temp-Ordner, ausgelieferte Regeln mit anderer Version.
#pragma once
#include "medic/rules.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace testutil {

namespace fs = std::filesystem;
using nlohmann::json;

inline std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline void spit(const fs::path& p, const std::string& text) {
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

inline const std::string kShipped = std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json";

// Die ausgelieferten Regeln mit anderer Version.
inline std::string rules_text_with_version(const std::string& version) {
    json j = json::parse(slurp(kShipped));
    j["db_version"] = version;
    return j.dump(2) + "\n";
}


}  // namespace testutil
