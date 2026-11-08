// Collectors sammeln Fakten. Sie lesen nur und kennen keine Regeln.
#pragma once
#include "medic/facts.hpp"
#include "medic/platform.hpp"

namespace medic {

void collect_firmware(const Platform& p, Facts& f);    // fw.version, fw.model
void collect_processes(const Platform& p, Facts& f);   // proc.running[] (klein geschrieben)
void collect_storage(const Platform& p, Facts& f);     // storage.mounts[].{mount,total,free,free_pct}
void collect_components(const Platform& p, Facts& f);  // kstuff.version, kstuff.origin, smp.version, ...
void collect_shadowmount(const Platform& p, Facts& f); // smp.*

// Fuehrt alle Collectors aus.
Facts collect_all(const Platform& p);

}  // namespace medic
