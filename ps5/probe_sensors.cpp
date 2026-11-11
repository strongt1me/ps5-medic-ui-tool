// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 Medic -- Sensor-Erkundung (Batch 5 der Diagnose, docs/DIAGNOSE.md): einmaliger Probelauf, KEIN Produktfeature.
// Prueft fuer eine feste Liste nur LESENDER Kernel-Funktionen (laut Symboltabelle der SDK-Stubs vorhanden, Signatur unbekannt),
//   1. ob sie auf der Konsole exportiert sind (dlsym) und
//   2. was sie liefern, wenn man sie mit einem Zeiger auf einen genullten Puffer aufruft.
// Jeder Aufruf laeuft in einem EIGENEN KINDPROZESS (fork): stuerzt er ab oder haengt er, meldet der Elternprozess das und
// macht weiter; ohne fork wird nichts aufgerufen. Es gibt keine Schreib-, Setz- oder Steuerfunktionen in der Liste (keine
// Icc*Set*/Control*, kein SetBaseModeClock o. ae.). Ausgabe: stdout und /data/ps5medic/probe_sensors.txt.
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

extern "C" int sceKernelDlsym(int handle, const char* name, void** addr);

namespace {

struct Candidate {
    const char* name;
    const char* what;  // wofuer die Messung taugen koennte
    bool indexed;      // nur dann auch f(0, buf): der erste Parameter ist ein Index (sonst saehe ein NULL-Zeiger nach einem Absturz aus)
};

// Nur Lesefunktionen. Die ersten beiden sind Kontrollfaelle mit bekannter Signatur (SDK-Beispiel hwinfo).
const Candidate kCandidates[] = {
    {"sceKernelGetCpuTemperature", "Kontrolle: CPU-Temperatur (int*)", false},
    {"sceKernelGetSocSensorTemperature", "Kontrolle: SoC-Temperatur (int, int*)", true},
    {"sceKernelGetCpuFrequency", "Kontrolle: CPU-Takt in Hz (long, ohne Argument)", false},
    {"sceKernelGetCurrentFanDuty", "Luefter-Auslastung", false},
    {"sceKernelGetSocPowerConsumption", "Leistungsaufnahme des SoC", false},
    {"sceKernelGetCpuCoreClock", "Takt je Kern", false},
    {"sceKernelGetSocClock", "SoC-Takt", false},
    {"sceKernelGetCpuUsage", "CPU-Auslastung", false},
    {"sceKernelGetCpumode", "CPU-Modus", false},
    {"sceKernelIccGetPowerOperatingTime", "Betriebszeit der Konsole", false},
    {"sceKernelIccGetPowerNumberOfBootShutdown", "Anzahl Starts und Abschaltungen", false},
    {"sceKernelIccGetPowerUpCause", "Grund des letzten Einschaltens", false},
    {"sceKernelIsM2DeviceAttached", "M.2-Erweiterung vorhanden", false},
    {"sceKernelGetMainSocId", "SoC-Kennung", false},
    {"sceKernelGetTscFrequency", "Zaehlerfrequenz", false},
    {"sceKernelAvailableFlexibleMemorySize", "freier flexibler Speicher", false},
    {"sceKernelGetBioUsageAll", "Auslastung der Speicher-E/A", false},
};

using Fn = long (*)(void*, void*, void*);

struct Result {
    long ret = 0;
    int err = 0;
    std::uint64_t words[8] = {};
};

void* resolve(const char* name) {
    if (void* p = dlsym(RTLD_DEFAULT, name)) return p;
    void* addr = nullptr;
    for (int handle : {0x2001, 0x2002})  // Handle von libkernel (nicht belegt, daher beide versucht)
        if (sceKernelDlsym(handle, name, &addr) == 0 && addr) return addr;
    return nullptr;
}

// Ruft fn in einem Kindprozess auf. form 0: fn(buf), form 1: fn(0, buf). Rueckgabe: 0 = ok, 1 = Kind beendet ohne Ergebnis,
// 2 = Signal, 3 = Zeitueberschreitung, 4 = fork/pipe nicht moeglich.
int call_in_child(Fn fn, int form, Result& r, int& signal_no) {
    int fds[2];
    if (pipe(fds) != 0) return 4;
    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return 4;
    }
    if (pid == 0) {
        close(fds[0]);
        alignas(16) std::uint64_t buf[32] = {};
        errno = 0;
        const long ret = form == 0 ? fn(buf, nullptr, nullptr) : fn(nullptr, buf, nullptr);
        Result out;
        out.ret = ret;
        out.err = errno;
        std::memcpy(out.words, buf, sizeof out.words);
        (void)!write(fds[1], &out, sizeof out);
        _exit(0);
    }
    close(fds[1]);
    int result = 1;
    pollfd pfd{fds[0], POLLIN, 0};
    if (poll(&pfd, 1, 3000) > 0) {
        if (read(fds[0], &r, sizeof r) == static_cast<ssize_t>(sizeof r)) result = 0;
    } else {
        kill(pid, SIGKILL);
        result = 3;
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (result != 0 && WIFSIGNALED(status)) {
        signal_no = WTERMSIG(status);
        result = 2;
    }
    return result;
}

// Jede Zeile sofort in die Datei haengen: ein abgestuerzter Kindprozess kann auf der Konsole eine Absturzbehandlung ausloesen,
// die auch den Elternprozess beenden kann; dann bleibt wenigstens alles bis dahin erhalten.
void out(const std::string& line) {
    std::puts(line.c_str());
    std::fflush(stdout);
    if (FILE* fp = std::fopen("/data/ps5medic/probe_sensors.txt", "a")) {
        std::fputs((line + "\n").c_str(), fp);
        std::fclose(fp);
    }
}

}  // namespace

int main() {
    if (FILE* fp = std::fopen("/data/ps5medic/probe_sensors.txt", "w")) std::fclose(fp);  // neue Datei je Lauf (eigener Ordner)
    out("PS5 Medic - Sensor-Erkundung (nur lesende Funktionen, je Aufruf ein Kindprozess)");
    {
        std::time_t t = std::time(nullptr);
        char b[40];
        std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
        out(std::string("Zeit: ") + b);
    }
    for (const auto& c : kCandidates) {
        void* sym = resolve(c.name);
        if (!sym) {
            out(std::string(c.name) + ": nicht exportiert");
            continue;
        }
        out(std::string(c.name) + ": exportiert  (" + c.what + ")");
        for (int form = 0; form < (c.indexed ? 2 : 1); ++form) {
            Result r;
            int sig = 0;
            const int rc = call_in_child(reinterpret_cast<Fn>(sym), form, r, sig);
            char line[256];
            const char* f = form == 0 ? "f(buf)   " : "f(0,buf) ";
            if (rc == 0)
                std::snprintf(line, sizeof line, "  %s ret=%ld errno=%d  w0=%llu w1=%llu  i0=%d i1=%d", f, r.ret, r.err, static_cast<unsigned long long>(r.words[0]),
                              static_cast<unsigned long long>(r.words[1]), static_cast<int>(r.words[0] & 0xffffffffu), static_cast<int>(r.words[0] >> 32));
            else if (rc == 2) std::snprintf(line, sizeof line, "  %s Kindprozess mit Signal %d beendet", f, sig);
            else if (rc == 3) std::snprintf(line, sizeof line, "  %s Zeitueberschreitung (Kindprozess beendet)", f);
            else if (rc == 4) std::snprintf(line, sizeof line, "  %s fork/pipe nicht moeglich: nichts aufgerufen", f);
            else std::snprintf(line, sizeof line, "  %s Kindprozess ohne Ergebnis beendet", f);
            out(line);
            if (rc == 4) break;
        }
    }
    out("Ende der Liste.");
    return 0;
}
