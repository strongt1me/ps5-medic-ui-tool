// medic-cli check <snapshot-dir> [--rules <rules.json>] [--json] [--lang de|en]
#include "medic/collectors.hpp"
#include "medic/platform.hpp"
#include "medic/rules.hpp"

#include <cstring>
#include <iostream>
#include <string>

#ifndef MEDIC_DEFAULT_RULES
#define MEDIC_DEFAULT_RULES "knowledge/rules.json"
#endif

namespace {

void usage() {
    std::cerr << "Aufruf: medic-cli check <snapshot-ordner> [--rules <rules.json>] [--json] [--lang de|en]\n";
}

const char* label(medic::Severity s) {
    switch (s) {
        case medic::Severity::Critical: return "KRITISCH";
        case medic::Severity::Warning: return "WARNUNG";
        case medic::Severity::Info: return "HINWEIS";
    }
    return "";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3 || std::strcmp(argv[1], "check") != 0) {
        usage();
        return 2;
    }
    std::string dir = argv[2], rules_path = MEDIC_DEFAULT_RULES, lang = "de";
    bool as_json = false;
    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--json") as_json = true;
        else if (a == "--rules" && i + 1 < argc) rules_path = argv[++i];
        else if (a == "--lang" && i + 1 < argc) lang = argv[++i];
        else {
            usage();
            return 2;
        }
    }
    if (lang != "de" && lang != "en") {
        usage();
        return 2;
    }

    try {
        auto rules = medic::load_rules_from_file(rules_path);
        auto platform = medic::make_snapshot_platform(dir);
        medic::Facts facts = medic::collect_all(*platform);
        auto findings = medic::evaluate(rules, facts);

        if (as_json) {
            nlohmann::json out = nlohmann::json::object();
            out["snapshot"] = dir;
            out["rules_version"] = rules.db_version;
            out["findings"] = nlohmann::json::array();
            for (auto& f : findings)
                out["findings"].push_back({{"id", f.rule_id},
                                           {"severity", medic::to_string(f.severity)},
                                           {"text", lang == "de" ? f.text_de : f.text_en},
                                           {"evidence", f.evidence},
                                           {"fix", f.fix.empty() ? nlohmann::json(nullptr) : nlohmann::json(f.fix)},
                                           {"source", f.source},
                                           {"verified", f.verified}});
            std::cout << out.dump(2) << "\n";
            return 0;
        }

        std::cout << "PS5 Medic - Check-up: " << dir << "\n";
        const auto* fw = facts.get("fw.version");
        std::cout << "Firmware: " << (fw && fw->is_string() && !fw->get<std::string>().empty() ? fw->get<std::string>() : "unbekannt")
                  << " | Regeln: " << rules.db_version << " (" << rules.rules.size() << ")\n\n";
        if (findings.empty()) {
            std::cout << "Keine Befunde.\n";
            return 0;
        }
        for (auto& f : findings) {
            std::cout << "[" << label(f.severity) << "] " << f.rule_id << "\n  " << (lang == "de" ? f.text_de : f.text_en) << "\n";
            for (auto& e : f.evidence) std::cout << "  Evidenz: " << e << "\n";
            if (!f.fix.empty()) std::cout << "  Fix:     " << f.fix << "\n";
            std::cout << "  Quelle:  " << f.source << " (" << f.verified << ")\n\n";
        }
        std::cout << findings.size() << " Befund(e).\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fehler: " << e.what() << "\n";
        return 2;
    }
}
