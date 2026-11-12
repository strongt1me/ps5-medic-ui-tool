// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 Medic Launcher: startet die Vollbild-UI (ps5medicui.elf) mit App-Kontext, ohne websrv und ohne Homebrew-Lader.
//
// Er gehoert zum alten Weg (Ausweichweg, docs/TITLE.md): ps5medic-start.elf steckt im Starter der Kachel (title/starter.c) und wird an Port
// 9021 gesendet, sobald diese Kachel geoeffnet wird. Er startet die UI und beendet sich, wenn die UI endet. Die native App (docs/NATIVE.md)
// braucht ihn nicht; ihre Kachel legt der Installer (ps5/installer_main.cpp, make send-launcher) an.
//
// UI-Start: Der Lader dafuer stammt aus websrv (third_party/websrv-hbldr, GPL-3.0-or-later, nur der Code, nicht der Dienst): eine
// echte System-App (FAKE00000) wird per sceSystemServiceLaunchApp gestartet und per ptrace gegen die ELF getauscht. Das gibt der UI
// einen App-Kontext (nur damit oeffnet sich der DualSense) und keine Sandbox.
//
// Sicherheit: Es wird nur geschlossen, was ein System-Programm (NPXS...) oder eine Homebrew-Fake-App (FAKE...) ist; ein Spiel
// oder eine unbekannte App nie. Der Launcher oeffnet keinen Port und greift nicht ins Netz.
//
// Eine zweite Instanz (ELF erneut gesendet) loest die laufende ab (wie bei websrv: gleicher Thread-Name, die alte wird beendet).
// Alles Wichtige steht in /data/ps5medic/launcher.log.
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <ps5/kernel.h>

extern "C" {
#include "hbldr.h"  // third_party/websrv-hbldr: hbldr_launch()
int sceUserServiceInitialize(void*);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int app_id, char* title_id);
int sceKernelGetAppInfoByAppId(int app_id, void* info);
int sceKernelSendNotificationRequest(int device, void* request, size_t size, int blocking);
}

namespace fs = std::filesystem;

namespace {

constexpr const char* kUiDir = "/data/homebrew/PS5MEDIC";
constexpr const char* kUiElf = "/data/homebrew/PS5MEDIC/ps5medicui.elf";
constexpr const char* kLogPath = "/data/ps5medic/launcher.log";
constexpr const char* kThreadName = "ps5medic-launch.elf";  // wie bei websrv: damit eine neue Instanz die alte findet
constexpr std::uint64_t kSystemAuthId = 0x4801000000000013ULL;  // wie websrv (src/ps5/sys.c): Voraussetzung fuer LaunchApp

std::FILE* g_log = nullptr;

void trace(const std::string& line) {
    std::puts(line.c_str());
    if (g_log) {
        std::fputs((line + "\n").c_str(), g_log);
        std::fflush(g_log);
    }
}

// Benachrichtigung auf dem Bildschirm (wie im Starter-Titel title/starter.c); ASCII, damit nichts verstuemmelt wird.
void notify(const std::string& msg) {
    static struct {
        char reserved[45];
        char message[3075];
    } req;
    std::memset(&req, 0, sizeof req);
    std::strncpy(req.message, msg.c_str(), sizeof req.message - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

// ---- Prozesse (nach websrv src/ps5/sys.c) -------------------------------------------------------------------------------------------
// Alle Prozesse mit diesem Thread-Namen (ohne den eigenen).
std::vector<pid_t> find_pids(const char* name) {
    std::vector<pid_t> pids;
    int mib[4] = {1, 14, 8, 0};
    std::size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) != 0) return pids;
    std::vector<std::uint8_t> buf(size);
    if (sysctl(mib, 4, buf.data(), &size, nullptr, 0) != 0) return pids;
    const pid_t me = ::getpid();
    for (std::size_t off = 0; off + 460 < size;) {
        int structsize = 0;
        std::memcpy(&structsize, &buf[off], sizeof structsize);
        if (structsize <= 460) break;
        pid_t pid = 0;
        std::memcpy(&pid, &buf[off + 72], sizeof pid);
        const char* tdname = reinterpret_cast<const char*>(&buf[off + 447]);
        if (pid != me && std::strncmp(tdname, name, 32) == 0) pids.push_back(pid);
        off += static_cast<std::size_t>(structsize);
    }
    return pids;
}

// Die Pid aus dem Protokoll der vorigen Sitzung wird nicht mehr herangezogen (Audit 12. Nov. 2026): elfldr benennt jeden Payload "payload.elf",
// und nach einem Neustart kann dieselbe Pid zu kstuff, ShadowMount+ oder dem Luefterregler gehoeren. Beendet wird nur, was den
// eigenen Thread-Namen traegt.
// Phase des Launchers in einer Datei (Audit 12. Nov. 2026): "launching" = hbldr_launch laeuft (die Big App ist unter ptrace angehalten, der
// Tracer darf jetzt nicht sterben, sonst bleibt eine System-App halb umgebaut zurueck), "monitoring" = die UI laeuft, der Launcher wartet nur.
constexpr const char* kPhasePath = "/data/ps5medic/launcher.phase";

void set_phase(const char* phase) {
    std::FILE* f = std::fopen(kPhasePath, "w");
    if (!f) return;
    std::fputs(phase, f);
    std::fclose(f);
}

bool phase_is_launching() {
    char buf[32] = {0};
    std::FILE* f = std::fopen(kPhasePath, "r");
    if (!f) return false;
    std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    return std::strncmp(buf, "launching", 9) == 0;
}

// false: eine vorige Instanz steckt noch mitten im Start und wurde nicht beendet (der Aufrufer bricht ab).
bool replace_previous_instance() {
    syscall(SYS_thr_set_name, -1, kThreadName);
    // Laeuft die vorige Instanz noch im ptrace-Tausch, bis zu 90 s warten (ein Start dauert hoechstens etwa 75 s) statt sie zu beenden.
    for (int waited = 0; waited < 90 && phase_is_launching() && !find_pids(kThreadName).empty(); ++waited) {
        if (waited == 0) trace("Vorige Instanz startet gerade die UI, warte auf das Ende");
        ::sleep(1);
    }
    if (phase_is_launching() && !find_pids(kThreadName).empty()) {
        trace("Vorige Instanz steckt noch im Start, nichts wird beendet");
        return false;
    }
    for (int round = 0; round < 5; ++round) {
        std::vector<pid_t> pids = find_pids(kThreadName);
        if (pids.empty()) return true;
        for (pid_t p : pids) {
            trace("Vorige Instanz (Pid " + std::to_string(p) + ") wird beendet");
            ::kill(p, SIGKILL);
        }
        ::sleep(1);
    }
    return true;
}

// ---- Titel-ID einer laufenden App ---------------------------------------------------------------------------------------------
// Erste Zeichenfolge im Puffer, die wie eine Titel-ID aussieht (vier Grossbuchstaben, fuenf Ziffern, z. B. FAKE42007, NPXS40106).
// Das Layout der App-Info ist nicht dokumentiert; die Suche nach dem Muster haengt nicht daran.
std::string scan_title_id(const unsigned char* b, std::size_t n) {
    for (std::size_t i = 0; i + 9 <= n; ++i) {
        bool ok = true;
        for (int k = 0; k < 4 && ok; ++k) ok = std::isupper(b[i + k]) != 0;
        for (int k = 4; k < 9 && ok; ++k) ok = std::isdigit(b[i + k]) != 0;
        if (!ok) continue;
        if (i > 0 && std::isalnum(b[i - 1])) continue;
        if (i + 9 < n && std::isalnum(b[i + 9])) continue;
        return std::string(reinterpret_cast<const char*>(b + i), 9);
    }
    return {};
}

std::string hex_head(const unsigned char* b, std::size_t n) {
    std::string s;
    char buf[4];
    for (std::size_t i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof buf, "%02x", b[i]);
        s += buf;
    }
    return s;
}

std::string title_of_app(int app_id) {
    char title[64] = {0};
    const int rc1 = sceSystemServiceGetAppTitleId(app_id, title);
    if (rc1 == 0) {
        const std::string t = scan_title_id(reinterpret_cast<unsigned char*>(title), sizeof title);
        if (!t.empty()) return t;
    }
    alignas(8) unsigned char info[512] = {0};
    const int rc2 = sceKernelGetAppInfoByAppId(app_id, info);
    if (rc2 == 0) {
        const std::string t = scan_title_id(info, sizeof info);
        if (!t.empty()) return t;
    }
    trace("Titel-ID von App " + std::to_string(app_id) + " nicht lesbar (GetAppTitleId rc=" + std::to_string(rc1) + ", GetAppInfoByAppId rc=" +
          std::to_string(rc2) + ", Kopf " + hex_head(info, 48) + ")");
    return {};
}

// Was geschlossen werden darf, damit die UI starten kann: System-Programme und Homebrew-Fake-Apps (darunter der Starter der Kachel).
// Alles andere (Spiele, unbekannte Titel) nie.
bool may_close(const std::string& title) { return title.rfind("NPXS", 0) == 0 || title.rfind("FAKE", 0) == 0; }

// Pruefung direkt vor dem Schliessen in hbldr_launch (hbldr_kill_guard): zwischen der ersten Pruefung und dem Kill liegen Sekunden, in denen
// der Nutzer ein Spiel gestartet haben kann. Das Spiel wird dann nie beendet.
extern "C" int medic_kill_guard(int app_id) {
    const std::string title = title_of_app(app_id);
    if (!may_close(title)) {
        trace("Abbruch: App " + std::to_string(app_id) + " (" + (title.empty() ? "unbekannt" : title) + ") darf nicht geschlossen werden");
        return 0;
    }
    return 1;
}

// ---- UI starten --------------------------------------------------------------------------------------------------------------------
// Blockiert bis die UI laeuft (oder es scheitert). Gibt die Pid der UI zurueck oder -1.
pid_t launch_ui(const char* why) {
    struct stat st;
    if (::stat(kUiElf, &st) != 0) {
        trace(std::string("UI-Start (") + why + "): " + kUiElf + " fehlt");
        notify("PS5 Medic: ps5medicui.elf fehlt in /data/homebrew/PS5MEDIC/ (make deploy-ui).");
        return -1;
    }
    const int app = sceSystemServiceGetAppIdOfRunningBigApp();
    if (app > 0) {
        const std::string title = title_of_app(app);
        if (!may_close(title)) {
            trace("UI-Start (" + std::string(why) + "): App " + std::to_string(app) + " (" + (title.empty() ? "unbekannt" : title) + ") laeuft und wird nicht geschlossen");
            notify("PS5 Medic: Eine App laeuft im Vordergrund. Bitte erst schliessen.");
            return -1;
        }
        trace("UI-Start (" + std::string(why) + "): schliesst " + title + " (app_id " + std::to_string(app) + ")");
    }
    // Wurde gerade eine App geschlossen (der Starter der Kachel, eine vorige UI), braucht das System einen Moment: ohne Pause und
    // ohne zweiten Versuch scheitert hbldr_launch dann mit "Broken pipe" (Konsolentest 12. Nov. 2026, Kachel bei laufender UI).
    if (app > 0) ::sleep(2);
    hbldr_kill_guard = medic_kill_guard;
    // Marker-Datei zum Testen ohne neuen Bau: kein sys_set_budget(0) beim Start der Big App (Verdacht auf Kernel-Panic beim Neustart)
    struct stat mk;
    hbldr_skip_budget = ::stat("/data/ps5medic/no_budget_syscall", &mk) == 0 ? 1 : 0;
    if (hbldr_skip_budget) trace("Marker no_budget_syscall gefunden: Syscall sys_set_budget wird weggelassen");
    {  // Testdatei: Text "uid" und/oder "caps" in /data/ps5medic/raise_skip (Diagnose der PS-Taste, kein Dauerbetrieb)
        hbldr_raise_skip = 0;
        if (std::FILE* rf = std::fopen("/data/ps5medic/raise_skip", "r")) {
            char buf[64] = {0};
            std::fread(buf, 1, sizeof buf - 1, rf);
            std::fclose(rf);
            if (std::strstr(buf, "uid")) hbldr_raise_skip |= 1;
            if (std::strstr(buf, "caps")) hbldr_raise_skip |= 2;
            trace("raise_skip gefunden, Maske " + std::to_string(hbldr_raise_skip));
        }
    }
    set_phase("launching");
    pid_t pid = -1;
    // Hoechstens zwei Versuche, und nur wenn keine Big App laeuft: ein haengender LaunchApp-Aufruf kann sonst noch eine App starten, die
    // der zweite Versuch mitten im Hochfahren schliessen wuerde (doppelter Start).
    for (int attempt = 1; attempt <= 2 && pid < 0; ++attempt) {
        trace(std::string("UI-Start (") + why + "): hbldr_launch, Versuch " + std::to_string(attempt));
        const int out = ::open("/data/ps5medic/ui.stdout", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);  // stdout/stderr der UI
        char* argv[] = {nullptr};
        char* envp[] = {nullptr};
        pid = hbldr_launch(kUiDir, kUiElf, out >= 0 ? out : 1, argv, envp);
        if (out >= 0) ::close(out);
        if (pid >= 0) break;
        trace("UI-Start: hbldr_launch fehlgeschlagen (errno " + std::to_string(errno) + ": " + std::strerror(errno) + ")");
        if (sceSystemServiceGetAppIdOfRunningBigApp() > 0) {
            trace("UI-Start: eine Big App laeuft noch, kein zweiter Versuch");
            break;
        }
        ::sleep(3);
    }
    set_phase(pid < 0 ? "failed" : "monitoring");
    if (pid < 0) {
        notify("PS5 Medic: Die Oberflaeche liess sich nicht starten (Protokoll: /data/ps5medic/launcher.log).");
        return -1;
    }
    trace("UI-Start: Prozess " + std::to_string(pid));
    return pid;
}

}  // namespace

int main() {
    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGCHLD, SIG_IGN);
    std::error_code ec;
    fs::create_directories("/data/ps5medic", ec);
    g_log = std::fopen(kLogPath, "w");
    trace(std::string("PS5 Medic Launcher startet (Pid ") + std::to_string(::getpid()) + ") [Start ueber die Kachel]");

    if (!replace_previous_instance()) {
        notify("PS5 Medic: Ein vorheriger Start laeuft noch. Bitte kurz warten und noch einmal starten.");
        if (g_log) std::fclose(g_log);
        ::_exit(1);
    }
    const int uerr = sceUserServiceInitialize(nullptr);
    if (uerr != 0) trace("sceUserServiceInitialize: " + std::to_string(uerr));
    kernel_set_ucred_authid(-1, kSystemAuthId);

    const pid_t ui = launch_ui("Kachel");
    if (ui < 0) {
        if (g_log) std::fclose(g_log);
        ::_exit(1);
    }
    // Bei der UI bleiben, solange sie laeuft (wie websrv, das den gestarteten Prozess ebenfalls begleitet).
    for (;;) {
        ::sleep(2);
        if (::kill(ui, 0) != 0 && errno == ESRCH) break;
    }
    set_phase("done");
    trace("UI beendet, der Launcher endet.");
    if (g_log) std::fclose(g_log);
    ::_exit(0);
}
