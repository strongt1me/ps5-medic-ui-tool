// SPDX-License-Identifier: GPL-3.0-or-later
// Hilfsprogramm fuer die Sensor-Erkundung (Batch 5): listet Prozesse mit PID, Namen, Threadzahl und verbrauchter CPU-Zeit (nur lesend)
// und beendet auf Wunsch GENAU EINEN Prozess, dessen PID zur Uebersetzungszeit angegeben ist (-DKILL_PID=<pid>), sonst nur Liste.
// Gedacht, um einen haengenden eigenen Probe-Prozess zu beenden; kein Produktfeature. Ausgabe: stdout (offene 9021-Verbindung).
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t len = 0;
    if (sysctl(mib, 4, nullptr, &len, nullptr, 0) != 0) return 1;
    std::vector<char> buf;
    for (int attempt = 0; attempt < 4; ++attempt) {
        len += len / 4 + 4096;
        buf.assign(len, 0);
        if (sysctl(mib, 4, buf.data(), &len, nullptr, 0) == 0) break;
        if (errno != ENOMEM) return 1;
    }
    struct Row {
        int pid, threads;
        unsigned long long runtime_us;
        char name[32];
    };
    std::vector<Row> rows;
    for (size_t off = 0; off + sizeof(int) <= len;) {
        const auto* ki = reinterpret_cast<const struct kinfo_proc*>(buf.data() + off);
        if (ki->ki_structsize <= 0 || off + static_cast<size_t>(ki->ki_structsize) > len) break;
        Row r{ki->ki_pid, ki->ki_numthreads, static_cast<unsigned long long>(ki->ki_runtime), {}};
        std::strncpy(r.name, ki->ki_comm, sizeof r.name - 1);
        rows.push_back(r);
        off += static_cast<size_t>(ki->ki_structsize);
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.runtime_us > b.runtime_us; });
    std::puts("PID  Threads  CPU-Sekunden  Name  (nach CPU-Zeit absteigend, oberste 12)");
    for (size_t i = 0; i < rows.size() && i < 12; ++i) std::printf("%5d %3d %10.1f  %s\n", rows[i].pid, rows[i].threads, static_cast<double>(rows[i].runtime_us) / 1e6, rows[i].name);
    std::fflush(stdout);
#ifdef KILL_PID
    std::printf("beende PID %d: ", KILL_PID);
    std::printf("%s\n", kill(KILL_PID, SIGKILL) == 0 ? "gesendet" : "fehlgeschlagen");
#endif
    return 0;
}
