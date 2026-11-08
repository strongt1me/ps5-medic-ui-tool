// Fakten-Store: ein verschachteltes JSON-Dokument.
//   fw.version, proc.running[], smp.config.<key>, smp.images[].{name,ext,path}, ...
// Pfade in Regeln: Punkt-getrennt, "[]" markiert ein Array, ueber das die Regel iteriert.
#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace medic {

class Facts {
public:
    using json = nlohmann::json;

    json& root() { return root_; }
    const json& root() const { return root_; }

    // Setzt einen Wert unter einem Punkt-Pfad ("smp.config.debug"), legt Objekte an.
    void set(const std::string& path, json value);
    // Gibt nullptr zurueck, wenn der Pfad fehlt. Keine "[]" erlaubt.
    const json* get(const std::string& path) const;

private:
    json root_ = json::object();
};

}  // namespace medic
