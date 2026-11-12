// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 Medic Launcher: startet die Vollbild-UI (ps5medicui.elf) mit App-Kontext, ohne websrv und ohne Homebrew-Lader.
//
// Es gibt ihn in zwei Fassungen aus demselben Quelltext:
//
//  * ps5medic-start.elf (ohne MEDIC_TILE_INSTALL): wird vom Starter der Kachel (title/starter.c) an Port 9021 gesendet, sobald die
//    Kachel "PS5 Medic" geoeffnet wird. Er startet die UI und beendet sich, wenn die UI endet.
//  * ps5medic-launcher.elf (mit MEDIC_TILE_INSTALL): die ELF, die man von Hand an Port 9021 sendet (make send-launcher). Sie legt
//    zuerst die Kachel "PS5 Medic" (Titel-ID FAKE42008) an: ein echter nativer Titel, dessen eboot.bin der Starter ist (die Dateien
//    stecken in dieser ELF), installiert wie im Beispiel install_app des ps5-payload-sdk (eboot unter /system_ex/app, param.json
//    und Icon unter /user/app, dann sceAppInstUtilAppInstallTitleDir). Danach
//    startet sie die UI wie die erste Fassung. Die Kachel braucht danach nur noch den ELF-Lader, nichts anderes.
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
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/uio.h>
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
int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallAll(void*);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int app_id, char* title_id);
int sceKernelGetAppInfoByAppId(int app_id, void* info);
int sceKernelSendNotificationRequest(int device, void* request, size_t size, int blocking);
}

#ifdef MEDIC_TILE_INSTALL
// Die Dateien der Kachel (build-title/FAKE42008/, siehe title/build_title.sh), per .incbin in die ELF gebunden.
#define MEDIC_INCBIN(sym, file)                           \
    __asm__(".section .rodata\n.balign 16\n.global " #sym \
            "\n" #sym ":\n.incbin \"" MEDIC_TILE_DIR "/" file "\"\n.global " #sym "_end\n" #sym "_end:\n.previous\n")
MEDIC_INCBIN(tile_eboot, "eboot.bin");
MEDIC_INCBIN(tile_libc, "sce_module/libc.prx");
MEDIC_INCBIN(tile_param, "sce_sys/param.json");
MEDIC_INCBIN(tile_icon, "sce_sys/icon0.png");
extern "C" const unsigned char tile_eboot[], tile_eboot_end[], tile_libc[], tile_libc_end[], tile_param[], tile_param_end[], tile_icon[], tile_icon_end[];
#endif

namespace fs = std::filesystem;

namespace {

#ifdef MEDIC_TILE_INSTALL
constexpr const char* kTileTitle = "FAKE42008";
constexpr const char* kTileApp = "/user/app/FAKE42008";
constexpr const char* kTileSystem = "/system_ex/app/FAKE42008";
#endif
constexpr const char* kUiDir = "/data/homebrew/PS5MEDIC";
constexpr const char* kUiElf = "/data/homebrew/PS5MEDIC/ps5medicui.elf";
constexpr const char* kLogPath = "/data/ps5medic/launcher.log";
constexpr const char* kThreadName = "ps5medic-launch.elf";  // wie bei websrv: damit eine neue Instanz die alte findet
constexpr std::uint64_t kSystemAuthId = 0x4801000000000013ULL;  // wie websrv (src/ps5/sys.c): Voraussetzung fuer AppInstUtil und LaunchApp

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

// Die erste Zeile des vorigen Protokolls nennt die Pid der vorigen Instanz ("... (Pid 195)"): auch eine, die noch keinen
// Thread-Namen trug (aelterer Stand) und haengt, laesst sich so ablosen. Nur wenn der Name "payload.elf" lautet (elfldr
// benennt jeden Payload so), damit nie ein anderer Prozess getroffen wird.
pid_t previous_pid_from_log() {
    std::FILE* f = std::fopen(kLogPath, "r");
    if (!f) return -1;
    char line[160] = {0};
    const bool ok = std::fgets(line, sizeof line, f) != nullptr;
    std::fclose(f);
    if (!ok) return -1;
    const char* p = std::strstr(line, "(Pid ");
    return p ? static_cast<pid_t>(std::atoi(p + 5)) : -1;
}

void replace_previous_instance(pid_t from_log) {
    syscall(SYS_thr_set_name, -1, kThreadName);
    for (int round = 0; round < 5; ++round) {
        std::vector<pid_t> pids = find_pids(kThreadName);
        if (from_log > 0 && from_log != ::getpid())
            for (pid_t p : find_pids("payload.elf"))
                if (p == from_log) pids.push_back(p);
        if (pids.empty()) return;
        for (pid_t p : pids) {
            trace("Vorige Instanz (Pid " + std::to_string(p) + ") wird beendet");
            ::kill(p, SIGKILL);
        }
        ::sleep(1);
        from_log = -1;
    }
}

// ---- Titel-ID einer laufenden App ---------------------------------------------------------------------------------------------
// Erste Zeichenfolge im Puffer, die wie eine Titel-ID aussieht (vier Grossbuchstaben, fuenf Ziffern, z. B. FAKE42008, NPXS40106).
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

#ifdef MEDIC_TILE_INSTALL
// ---- Kachel ------------------------------------------------------------------------------------------------------------------------
struct Blob {
    const char* rel;  // Pfad unterhalb des Titelordners
    const unsigned char* data;
    std::size_t size;
    bool in_system;  // true: unter /system_ex/app/<ID>, false: unter /user/app/<ID>
};

// Stimmt die Datei schon mit dem Blob ueberein? (Gleiche Groesse, gleicher Inhalt.)
bool same_content(const std::string& path, const Blob& b) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool same = true;
    std::size_t done = 0;
    unsigned char buf[1 << 14];
    while (same) {
        const std::size_t n = std::fread(buf, 1, sizeof buf, f);
        if (n == 0) break;
        if (done + n > b.size || std::memcmp(buf, b.data + done, n) != 0) same = false;
        done += n;
    }
    std::fclose(f);
    return same && done == b.size;
}

bool write_blob(const std::string& path, const Blob& b) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data, 1, b.size, f) == b.size;
    return std::fclose(f) == 0 && ok;
}

int install_title_dir(const char* title_id, const char* dir) {
    int (*install)(const char*, const char*, void*) = nullptr;
    uint32_t handle = 0;
    if (!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle)) install = reinterpret_cast<int (*)(const char*, const char*, void*)>(kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE"));
    if (install) return install(title_id, dir, nullptr);
    return sceAppInstUtilAppInstallAll(nullptr);
}

// /system_ex beschreibbar machen (wie hbldr_launch es vor dem Anlegen der System-App FAKE00000 tut).
bool remount_system_ex() {
    struct iovec iov[] = {
        {const_cast<char*>("from"), 5},      {const_cast<char*>("/dev/ssd0.system_ex"), 20},
        {const_cast<char*>("fspath"), 7},    {const_cast<char*>("/system_ex"), 11},
        {const_cast<char*>("fstype"), 7},    {const_cast<char*>("exfatfs"), 8},
        {const_cast<char*>("large"), 6},     {const_cast<char*>("yes"), 4},
        {const_cast<char*>("timezone"), 9},  {const_cast<char*>("static"), 7},
        {const_cast<char*>("async"), 6},     {nullptr, 0},
        {const_cast<char*>("ignoreacl"), 10}, {nullptr, 0},
    };
    return nmount(iov, sizeof iov / sizeof iov[0], MNT_UPDATE) == 0;
}

// Legt die Kachel an (oder bringt eine aeltere Fassung auf den Stand dieser ELF). So macht es das Beispiel install_app des
// ps5-payload-sdk: das Programm (eboot.bin, sce_module) liegt unter /system_ex/app/<ID>, die Beschreibung (param.json, Icon) unter
// /user/app/<ID>/sce_sys, dann sceAppInstUtilAppInstallTitleDir. Ein reiner /user/app-Ordner mit eboot.bin wird nicht als startbare
// App registriert (sceSystemServiceLaunchApp: 0x80940033, Konsolentest 12. Nov. 2026). Nichts wird geloescht, nur eigene Dateien
// werden ueberschrieben; /system_ex bekommt nur den Ordner FAKE42008 (wie FAKE00000 beim UI-Start).
std::string ensure_tile() {
    const std::string sys = kTileSystem, usr = kTileApp;
    const Blob blobs[] = {
        {"/eboot.bin", tile_eboot, static_cast<std::size_t>(tile_eboot_end - tile_eboot), true},
        {"/sce_module/libc.prx", tile_libc, static_cast<std::size_t>(tile_libc_end - tile_libc), true},
        {"/sce_sys/param.json", tile_param, static_cast<std::size_t>(tile_param_end - tile_param), true},
        {"/sce_sys/param.json", tile_param, static_cast<std::size_t>(tile_param_end - tile_param), false},
        {"/sce_sys/icon0.png", tile_icon, static_cast<std::size_t>(tile_icon_end - tile_icon), false},
    };
    auto path_of = [&](const Blob& b) { return (b.in_system ? sys : usr) + b.rel; };
    bool current = true;
    for (const Blob& b : blobs)
        if (current && !same_content(path_of(b), b)) current = false;
    if (current) return std::string("Kachel ") + kTileTitle + ": schon installiert";

    if (!remount_system_ex()) return std::string("Kachel: /system_ex nicht beschreibbar (errno ") + std::to_string(errno) + ")";
    std::error_code ec;
    for (const char* d : {"/sce_sys", "/sce_module"}) {
        fs::create_directories(sys + d, ec);
        if (ec) return std::string("Kachel: ") + sys + d + " nicht anlegbar (" + ec.message() + ")";
    }
    fs::create_directories(usr + "/sce_sys", ec);
    if (ec) return std::string("Kachel: ") + usr + "/sce_sys nicht anlegbar (" + ec.message() + ")";
    for (const Blob& b : blobs) {
        const std::string path = path_of(b);
        if (same_content(path, b)) continue;
        if (!write_blob(path, b)) return std::string("Kachel: ") + path + " nicht schreibbar (errno " + std::to_string(errno) + ")";
    }
    if (sceAppInstUtilInitialize() != 0) return "Kachel: sceAppInstUtilInitialize fehlgeschlagen";
    const int rc = install_title_dir(kTileTitle, "/user/app/");
    return std::string("Kachel ") + kTileTitle + ": installiert (AppInstallTitleDir rc=" + std::to_string(rc) + ")";
}
#endif

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
    pid_t pid = -1;
    for (int attempt = 1; attempt <= 3 && pid < 0; ++attempt) {
        trace(std::string("UI-Start (") + why + "): hbldr_launch, Versuch " + std::to_string(attempt));
        const int out = ::open("/data/ps5medic/ui.stdout", O_WRONLY | O_CREAT | O_TRUNC, 0644);  // stdout/stderr der UI
        char* argv[] = {nullptr};
        char* envp[] = {nullptr};
        pid = hbldr_launch(kUiDir, kUiElf, out >= 0 ? out : 1, argv, envp);
        if (out >= 0) ::close(out);
        if (pid >= 0) break;
        trace("UI-Start: hbldr_launch fehlgeschlagen (errno " + std::to_string(errno) + ": " + std::strerror(errno) + ")");
        ::sleep(3);
    }
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
    const pid_t old_pid = previous_pid_from_log();
    g_log = std::fopen(kLogPath, "w");
    trace(std::string("PS5 Medic Launcher startet (Pid ") + std::to_string(::getpid()) + ")" +
#ifdef MEDIC_TILE_INSTALL
          " [ELF mit Kachel]"
#else
          " [Start ueber die Kachel]"
#endif
    );

    replace_previous_instance(old_pid);
    const int uerr = sceUserServiceInitialize(nullptr);
    if (uerr != 0) trace("sceUserServiceInitialize: " + std::to_string(uerr));
    kernel_set_ucred_authid(-1, kSystemAuthId);

#ifdef MEDIC_TILE_INSTALL
    trace(ensure_tile());
    notify("PS5 Medic: Kachel bereit, die Oberflaeche startet.");
#endif

    const pid_t ui = launch_ui(
#ifdef MEDIC_TILE_INSTALL
        "ELF gesendet"
#else
        "Kachel"
#endif
    );
    if (ui < 0) {
        if (g_log) std::fclose(g_log);
        ::_exit(1);
    }
    // Bei der UI bleiben, solange sie laeuft (wie websrv, das den gestarteten Prozess ebenfalls begleitet).
    for (;;) {
        ::sleep(2);
        if (::kill(ui, 0) != 0 && errno == ESRCH) break;
    }
    trace("UI beendet, der Launcher endet.");
    if (g_log) std::fclose(g_log);
    ::_exit(0);
}
