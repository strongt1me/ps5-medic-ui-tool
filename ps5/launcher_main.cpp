// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 Medic Launcher (ps5medic-launcher.elf): eine ELF, die man an Port 9021 sendet. Sie braucht weder websrv noch einen
// Starter-Titel noch ShadowMount+.
//
//  1. Kachel anlegen: installiert die Kachel "PS5 Medic" (Titel-ID FAKE42008) ueber sceAppInstUtilAppInstallTitleDir, so wie
//     websrv seine Kachel "Homebrew Launcher" anlegt. Die Kachel hat keinen eigenen Code, sondern einen deeplinkUri auf
//     http://127.0.0.1:9056/: ein Druck oeffnet den Browser der Konsole mit dieser Adresse.
//  2. Dienst: der Launcher bleibt laufen und beantwortet diese Adresse. Kommt die Anfrage, startet er die Vollbild-UI
//     (ps5medicui.elf) und schliesst dabei den Browser. Die UI bekommt einen App-Kontext (nur damit oeffnet sich der DualSense)
//     und keine Sandbox. Der Lader dafuer stammt aus websrv (third_party/websrv-hbldr, GPL-3.0-or-later): eine echte System-App
//     (FAKE00000) wird per sceSystemServiceLaunchApp gestartet und per ptrace gegen die ELF getauscht.
//  3. Beim ersten Start (ELF gesendet) startet er die UI gleich, wenn gerade keine App im Vordergrund ist.
//
// Sicherheit: Es wird nur geschlossen, was ein System-Programm (NPXS...) oder eine Homebrew-Fake-App (FAKE...) ist; ein Spiel
// oder eine unbekannte App nie. Der Dienst lauscht nur auf 127.0.0.1.
//
// Eine zweite Instanz (ELF erneut gesendet) loest die laufende ab (wie bei websrv: gleicher Thread-Name, die alte wird beendet).
// Alles Wichtige steht in /data/ps5medic/launcher.log.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
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

namespace fs = std::filesystem;

namespace {

constexpr const char* kTileTitle = "FAKE42008";
constexpr const char* kTileApp = "/user/app/FAKE42008";
constexpr const char* kUiDir = "/data/homebrew/PS5MEDIC";
constexpr const char* kUiElf = "/data/homebrew/PS5MEDIC/ps5medicui.elf";
constexpr const char* kUiIcon = "/data/homebrew/PS5MEDIC/sce_sys/icon0.png";
constexpr const char* kLogPath = "/data/ps5medic/launcher.log";
constexpr const char* kThreadName = "ps5medic-launch.elf";  // wie bei websrv: damit eine neue Instanz die alte findet
constexpr int kPort = 9056;
constexpr std::uint64_t kSystemAuthId = 0x4801000000000013ULL;  // wie websrv (src/ps5/sys.c): Voraussetzung fuer AppInstUtil und LaunchApp

// Die Kachel: ein deeplinkUri auf den Dienst dieses Launchers (nach dem Muster von websrvs param.json).
constexpr const char* kParamJson = R"JSON({
    "titleId": "FAKE42008",
    "deeplinkUri": "http://127.0.0.1:9056/",
    "localizedParameters": {
        "defaultLanguage": "en-US",
        "en-US": { "titleName": "PS5 Medic" },
        "de-DE": { "titleName": "PS5 Medic" }
    }
}
)JSON";

std::FILE* g_log = nullptr;
std::atomic<bool> g_launching{false};
std::atomic<bool> g_quit{false};

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

// Was geschlossen werden darf, damit die UI starten kann: System-Programme (der Browser, der die Kachel geoeffnet hat) und
// Homebrew-Fake-Apps. Alles andere (Spiele, unbekannte Titel) nie.
bool may_close(const std::string& title) { return title.rfind("NPXS", 0) == 0 || title.rfind("FAKE", 0) == 0; }

// ---- Kachel ------------------------------------------------------------------------------------------------------------------------
bool read_all(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[1 << 14];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    const bool ok = !std::ferror(f);
    std::fclose(f);
    return ok;
}

bool write_file(const std::string& path, const std::string& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

int install_title_dir(const char* title_id, const char* dir) {
    int (*install)(const char*, const char*, void*) = nullptr;
    uint32_t handle = 0;
    if (!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle)) install = reinterpret_cast<int (*)(const char*, const char*, void*)>(kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE"));
    if (install) return install(title_id, dir, nullptr);
    return sceAppInstUtilAppInstallAll(nullptr);
}

// Legt die Kachel an (oder aktualisiert sie, wenn sich param.json geaendert hat). Nichts wird geloescht.
std::string ensure_tile() {
    std::error_code ec;
    const std::string dir = kTileApp, sys = dir + "/sce_sys";
    std::string have;
    const bool installed = fs::exists("/user/appmeta/" + std::string(kTileTitle) + "/param.json", ec) || (read_all(sys + "/param.json", have) && have == kParamJson);
    if (installed && fs::exists(sys + "/icon0.png", ec)) return std::string("Kachel ") + kTileTitle + ": schon installiert";

    fs::create_directories(sys, ec);
    if (ec) return std::string("Kachel: ") + sys + " nicht anlegbar (" + ec.message() + ")";
    if (!write_file(sys + "/param.json", kParamJson)) return "Kachel: param.json nicht schreibbar";
    std::string icon;
    if (!read_all(kUiIcon, icon) || !write_file(sys + "/icon0.png", icon)) return std::string("Kachel: Icon fehlt (") + kUiIcon + " nicht lesbar; make deploy-ui)";
    if (sceAppInstUtilInitialize() != 0) return "Kachel: sceAppInstUtilInitialize fehlgeschlagen";
    const int rc = install_title_dir(kTileTitle, "/user/app/");
    return std::string("Kachel ") + kTileTitle + ": installiert (AppInstallTitleDir rc=" + std::to_string(rc) + ")";
}

// ---- UI starten --------------------------------------------------------------------------------------------------------------------
// Blockiert bis die UI laeuft (oder es scheitert). Nur ein Start gleichzeitig.
void launch_ui(const char* why) {
    if (g_launching.exchange(true)) {
        trace(std::string("UI-Start (") + why + "): laeuft schon, ignoriert");
        return;
    }
    struct Reset {
        ~Reset() { g_launching = false; }
    } reset;

    struct stat st;
    if (::stat(kUiElf, &st) != 0) {
        trace(std::string("UI-Start (") + why + "): " + kUiElf + " fehlt");
        notify("PS5 Medic: ps5medicui.elf fehlt in /data/homebrew/PS5MEDIC/ (make deploy-ui).");
        return;
    }
    const int app = sceSystemServiceGetAppIdOfRunningBigApp();
    if (app > 0) {
        const std::string title = title_of_app(app);
        if (!may_close(title)) {
            trace("UI-Start (" + std::string(why) + "): App " + std::to_string(app) + " (" + (title.empty() ? "unbekannt" : title) + ") laeuft und wird nicht geschlossen");
            notify("PS5 Medic: Eine App laeuft im Vordergrund. Bitte erst schliessen.");
            return;
        }
        trace("UI-Start (" + std::string(why) + "): schliesst " + title + " (app_id " + std::to_string(app) + ")");
    }
    trace(std::string("UI-Start (") + why + "): hbldr_launch");
    const int out = ::open("/data/ps5medic/ui.stdout", O_WRONLY | O_CREAT | O_TRUNC, 0644);  // stdout/stderr der UI
    char* argv[] = {nullptr};
    char* envp[] = {nullptr};
    const pid_t pid = hbldr_launch(kUiDir, kUiElf, out >= 0 ? out : 1, argv, envp);
    if (out >= 0) ::close(out);
    if (pid < 0) {
        trace("UI-Start: hbldr_launch fehlgeschlagen (errno " + std::to_string(errno) + ": " + std::strerror(errno) + ")");
        notify("PS5 Medic: Die Oberflaeche liess sich nicht starten (Protokoll: /data/ps5medic/launcher.log).");
        return;
    }
    trace("UI-Start: Prozess " + std::to_string(pid));
}

// ---- Dienst (127.0.0.1:9056) ------------------------------------------------------------------------------------------------------
int listen_local() {
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    const int on = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sin_len = sizeof sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(kPort);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa) != 0 || ::listen(s, 8) != 0) {
        ::close(s);
        return -1;
    }
    return s;
}

void respond(int c, int status, const char* reason, const std::string& body) {
    const std::string head = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                             std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
    ::send(c, head.data(), head.size(), MSG_NOSIGNAL);
    ::send(c, body.data(), body.size(), MSG_NOSIGNAL);
}

const char* kStartPage = "<!doctype html><html><head><meta charset=\"utf-8\"><title>PS5 Medic</title></head><body style=\"background:#0e1218;color:#e8edf2;font-family:sans-serif;"
                         "font-size:40px;padding:80px\"><h1>PS5 Medic</h1><p>Die Oberfl&auml;che wird gestartet &hellip;</p></body></html>";

// Eine Anfrage: GET / und GET /launch starten die UI, GET /quit beendet den Dienst, alles andere (z. B. /favicon.ico) ist 404.
void handle_client(int c) {
    timeval tv{2, 0};
    ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char buf[1024] = {0};
    const ssize_t n = ::recv(c, buf, sizeof buf - 1, 0);
    if (n <= 0) {
        ::close(c);
        return;
    }
    const std::string req(buf, static_cast<std::size_t>(n));
    if (req.compare(0, 4, "QUIT") == 0 || req.compare(0, 10, "GET /quit ") == 0) {
        respond(c, 200, "OK", "bye\n");
        ::close(c);
        trace("Beenden angefordert: eine neuere Instanz uebernimmt");
        g_quit = true;
        return;
    }
    if (req.compare(0, 6, "GET / ") == 0 || req.compare(0, 12, "GET /launch ") == 0) {
        respond(c, 200, "OK", kStartPage);
        ::close(c);
        trace("Anfrage der Kachel: UI starten");
        std::thread([] { launch_ui("Kachel"); }).detach();  // nicht im Anfrage-Pfad: das Starten dauert Sekunden
        return;
    }
    respond(c, 404, "Not Found", "nicht gefunden\n");
    ::close(c);
}

}  // namespace

int main() {
    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGCHLD, SIG_IGN);
    std::error_code ec;
    fs::create_directories("/data/ps5medic", ec);
    const pid_t old_pid = previous_pid_from_log();
    g_log = std::fopen(kLogPath, "w");
    trace("PS5 Medic Launcher startet (Pid " + std::to_string(::getpid()) + ")");

    replace_previous_instance(old_pid);
    const int uerr = sceUserServiceInitialize(nullptr);
    if (uerr != 0) trace("sceUserServiceInitialize: " + std::to_string(uerr));
    kernel_set_ucred_authid(-1, kSystemAuthId);

    int ctl = listen_local();
    for (int i = 0; i < 25 && ctl < 0; ++i) {  // bis zu 5 s: die vorige Instanz gibt den Port frei
        ::usleep(200 * 1000);
        ctl = listen_local();
    }
    if (ctl < 0) {
        trace("Port 127.0.0.1:" + std::to_string(kPort) + " nicht zu bekommen: Ende");
        notify("PS5 Medic Launcher: Port 9056 ist belegt, Ende.");
        return 1;
    }

    trace(ensure_tile());

    // Erster Start: keine App im Vordergrund -> UI gleich starten, sonst bleibt es bei der Kachel.
    const int app = sceSystemServiceGetAppIdOfRunningBigApp();
    if (app <= 0) {
        trace("Keine App im Vordergrund");
        notify("PS5 Medic: Kachel angelegt, die Oberflaeche startet.");
        std::thread([] { launch_ui("ELF gesendet"); }).detach();
    } else {
        const std::string title = title_of_app(app);
        trace("Vordergrund: app_id " + std::to_string(app) + (title.empty() ? "" : " (" + title + ")") + ": UI startet mit der Kachel");
        notify("PS5 Medic: Kachel angelegt. Die Oberflaeche startet mit der Kachel.");
    }
    trace("Dienst laeuft auf 127.0.0.1:" + std::to_string(kPort));

    while (!g_quit) {
        pollfd pfd{ctl, POLLIN, 0};
        if (::poll(&pfd, 1, 500) > 0 && (pfd.revents & POLLIN)) {
            const int c = ::accept(ctl, nullptr, nullptr);
            if (c >= 0) handle_client(c);
        }
    }
    ::close(ctl);
    if (g_log) std::fclose(g_log);
    ::_exit(0);  // auch wenn noch ein Start-Thread haengt
}
