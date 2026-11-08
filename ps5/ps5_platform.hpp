// PS5-Plattform: echtes Dateisystem, Prozessliste per sysctl, Firmware und Modell ueber ps5-payload-sdk.
// Nur mit dem PS5-Toolchain-Build (make ps5) uebersetzbar.
#pragma once
#include "medic/platform.hpp"

#include <memory>

namespace medic {

// writable=false: nur lesender Zugriff (Check-up, Snapshot-Export). Reparaturen brauchen writable=true.
std::unique_ptr<Platform> make_ps5_platform(bool writable);

}  // namespace medic
