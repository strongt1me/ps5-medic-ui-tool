// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 Medic -- einmaliger Lauf der Gruendlich-Tests (Waermetest und Schreibtest) ohne UI, fuer die Freigabe durch den Nutzer.
// Derselbe Kern wie die UI (selftest.hpp): Marker, Ergebnis in /data/ps5medic/selftest/last.json, Schutzregeln (Start nur unter 70 Grad,
// Abbruch bei 85 Grad, ein Schreibtest je Tag, eine Testdatei, nie loeschen). Kein fork; Rechenthreads mit Frist. Fortschritt auf stdout.
// Senden: nc <ip> 9021 < build-ps5/ps5medic-selftest.elf (Verbindung offen lassen, der Lauf dauert gut zwei Minuten).
#include "ps5_platform.hpp"

#include "medic/collectors.hpp"
#include "medic/selftest.hpp"

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace {

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

}  // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("PS5 Medic - Gruendlich-Tests (einmaliger Lauf, mit Freigabe)");
    auto platform = medic::make_ps5_platform(/*writable=*/true);
    const std::string ts = timestamp();
    std::printf("Zeit: %s\n", ts.c_str());
    const medic::Facts facts = medic::collect_all(*platform);  // vor dem Marker, sonst sieht der Lauf seinen eigenen Marker
    medic::persist_klog(*platform, facts);                      // das gelesene Kernel-Log nicht verwerfen
    const medic::Thermal t0 = platform->sys().thermal();
    std::printf("Ruhewert: CPU %d C, SoC %d C\n", t0.cpu_c, t0.soc_c);

    const std::vector<std::string> ids = {"hw.thermal.short_soak", "hw.mem.userland_pattern", "hw.storage.write_verify"};
    const bool marker = medic::selftest_begin(*platform, ts, ids);
    std::printf("Marker gesetzt: %s\n", marker ? "ja" : "nein");

    medic::TestContext ctx;
    ctx.today = ts.substr(0, 8);
    std::vector<medic::TestResult> results;
    for (const auto& id : ids) {
        const medic::TestDef* def = medic::find_test(id);
        if (!def) continue;
        std::printf("== %s\n", id.c_str());
        auto session = medic::begin_selftest_session(*platform, facts, *def, ctx);
        int last_pct = -1;
        while (!session->step()) {
            const int pct = static_cast<int>(session->progress() * 100);
            const medic::Thermal t = platform->sys().thermal();
            if (pct != last_pct) {
                std::printf("  %3d %%  CPU %d C  SoC %d C\n", pct, t.cpu_c, t.soc_c);
                last_pct = pct;
            }
        }
        medic::TestResult r = session->result();
        r.id = id;
        std::printf("  Ergebnis: %s%s%s\n", medic::to_string(r.status), r.reason.empty() ? "" : " ", r.reason.c_str());
        for (const auto& m : r.measurements) std::printf("    %s = %.2f %s\n", m.name.c_str(), m.value, m.unit.c_str());
        results.push_back(std::move(r));
    }
    const bool fin = medic::selftest_finish(*platform, timestamp(), results, false);
    const medic::Thermal t1 = platform->sys().thermal();
    std::printf("Ergebnis gespeichert: %s. Temperatur danach: CPU %d C, SoC %d C\nEnde.\n", fin ? "ja" : "nein", t1.cpu_c, t1.soc_c);
    return 0;
}
