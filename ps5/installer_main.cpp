// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 Medic Installer (ps5medic-launcher.elf, `make send-launcher`): legt die native App an, ohne FTP und ohne Dateien von Hand zu kopieren.
//
// Die ELF steckt voll mit den Dateien der beiden Ordner und schreibt sie nach /data/homebrew (dorthin, wo alle Apps und Spiele liegen):
//   /data/homebrew/FAKE42007/   eboot.bin (die native App), sce_module/libc.prx, sce_sys/icon0.png, sce_sys/param.json
//   /data/homebrew/PS5MEDIC/    ps5medicservice.elf (der Dienst), rules.json, error_codes.json, sce_sys/icon0.png
// ShadowMount+ erkennt den Ordner FAKE42007 und legt die Kachel "PS5 Medic" unter "Medien" an. Die Kachel startet die App, die den Dienst
// beim ersten Start selbst an den ELF-Lader (Port 9021) sendet (docs/NATIVE.md). Dieser Installer startet nichts und braucht keine
// erhoehten Rechte (Lesen und Schreiben unter /data geht aus einem elfldr-Payload heraus, docs/PHASE3.md).
//
// Sicherheit: Es wird nichts geloescht und nichts ausserhalb dieser beiden Ordner und /data/ps5medic angefasst. Jede Datei wird zuerst
// als <name>.new geschrieben und dann umbenannt, damit eine gerade laufende App oder ein laufender Dienst nie eine halbe Datei sieht;
// sce_sys/param.json kommt zuletzt, damit ShadowMount+ keinen halben Ordner erkennt. Dateien, die schon gleich sind, bleiben unberuehrt.
// Das Protokoll steht in /data/ps5medic/installer.log.
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern "C" int sceKernelSendNotificationRequest(int device, void* request, size_t size, int blocking);

// Die Dateien (build-native/FAKE42007/, siehe title/build_native_ui.sh; PS5MEDIC-Dateien aus dem Repository), per .incbin in die ELF gebunden.
#define MEDIC_INCBIN(sym, path)                           \
    __asm__(".section .rodata\n.balign 16\n.global " #sym \
            "\n" #sym ":\n.incbin \"" path "\"\n.global " #sym "_end\n" #sym "_end:\n.previous\n")
MEDIC_INCBIN(app_eboot, MEDIC_TILE_DIR "/eboot.bin");
MEDIC_INCBIN(app_libc, MEDIC_TILE_DIR "/sce_module/libc.prx");
MEDIC_INCBIN(app_icon, MEDIC_TILE_DIR "/sce_sys/icon0.png");
MEDIC_INCBIN(app_param, MEDIC_TILE_DIR "/sce_sys/param.json");
MEDIC_INCBIN(svc_elf, MEDIC_SERVICE_ELF);
MEDIC_INCBIN(svc_rules, MEDIC_RULES_JSON);
MEDIC_INCBIN(svc_codes, MEDIC_ERROR_CODES_JSON);
MEDIC_INCBIN(svc_icon, MEDIC_FOLDER_ICON);
extern "C" const unsigned char app_eboot[], app_eboot_end[], app_libc[], app_libc_end[], app_icon[], app_icon_end[], app_param[], app_param_end[],
    svc_elf[], svc_elf_end[], svc_rules[], svc_rules_end[], svc_codes[], svc_codes_end[], svc_icon[], svc_icon_end[];

namespace fs = std::filesystem;

namespace {

constexpr const char* kAppDir = "/data/homebrew/FAKE42007";
constexpr const char* kSvcDir = "/data/homebrew/PS5MEDIC";
constexpr const char* kLogPath = "/data/ps5medic/installer.log";

std::FILE* g_log = nullptr;

void trace(const std::string& line) {
    std::puts(line.c_str());
    if (g_log) {
        std::fputs((line + "\n").c_str(), g_log);
        std::fflush(g_log);
    }
}

// Benachrichtigung auf dem Bildschirm (wie in ps5/native_ui_main.cpp); ASCII, damit nichts verstuemmelt wird.
void notify(const std::string& msg) {
    static struct {
        char reserved[45];
        char message[3075];
    } req;
    std::memset(&req, 0, sizeof req);
    std::strncpy(req.message, msg.c_str(), sizeof req.message - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

struct Blob {
    const char* dir;
    const char* rel;  // Pfad unterhalb des Ordners
    const unsigned char* data;
    std::size_t size;
};

// Stimmt die Datei schon mit dem Blob ueberein? (Gleiche Groesse, gleicher Inhalt.)
bool same_content(const std::string& path, const Blob& b) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool same = true;
    std::size_t done = 0;
    static unsigned char buf[1 << 15];
    while (same) {
        const std::size_t n = std::fread(buf, 1, sizeof buf, f);
        if (n == 0) break;
        if (done + n > b.size || std::memcmp(buf, b.data + done, n) != 0) same = false;
        done += n;
    }
    std::fclose(f);
    return same && done == b.size;
}

// Schreibt neben das Ziel (<pfad>.new) und benennt dann um: eine laufende App oder ein laufender Dienst behaelt seine alte Datei, bis sie
// geschlossen wird, und sieht nie eine halbe.
std::string write_blob(const std::string& path, const Blob& b) {
    const std::string tmp = path + ".new";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return path + " nicht schreibbar (errno " + std::to_string(errno) + ")";
    const bool wrote = std::fwrite(b.data, 1, b.size, f) == b.size;
    const bool closed = std::fclose(f) == 0;
    if (!wrote || !closed) {
        ::unlink(tmp.c_str());
        return path + " unvollstaendig geschrieben (Speicher voll?)";
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        const int e = errno;
        ::unlink(tmp.c_str());
        return path + " nicht umbenennbar (errno " + std::to_string(e) + ")";
    }
    return {};
}

}  // namespace

int main() {
    std::error_code ec;
    fs::create_directories("/data/ps5medic", ec);
    g_log = std::fopen(kLogPath, "w");
    trace(std::string("PS5 Medic Installer startet (Pid ") + std::to_string(::getpid()) + ")");

    // Reihenfolge: zuerst der Dienst, dann die App; param.json der App zuletzt (ShadowMount+ sieht den Ordner erst dann als Titel).
    auto sz = [](const unsigned char* a, const unsigned char* b) { return static_cast<std::size_t>(b - a); };
    const Blob blobs[] = {
        {kSvcDir, "/ps5medicservice.elf", svc_elf, sz(svc_elf, svc_elf_end)},
        {kSvcDir, "/rules.json", svc_rules, sz(svc_rules, svc_rules_end)},
        {kSvcDir, "/error_codes.json", svc_codes, sz(svc_codes, svc_codes_end)},
        {kSvcDir, "/sce_sys/icon0.png", svc_icon, sz(svc_icon, svc_icon_end)},
        {kAppDir, "/sce_module/libc.prx", app_libc, sz(app_libc, app_libc_end)},
        {kAppDir, "/eboot.bin", app_eboot, sz(app_eboot, app_eboot_end)},
        {kAppDir, "/sce_sys/icon0.png", app_icon, sz(app_icon, app_icon_end)},
        {kAppDir, "/sce_sys/param.json", app_param, sz(app_param, app_param_end)},
    };

    int written = 0, kept = 0;
    std::string error;
    for (const Blob& b : blobs) {
        const std::string path = std::string(b.dir) + b.rel;
        if (same_content(path, b)) {
            ++kept;
            trace("schon aktuell: " + path);
            continue;
        }
        fs::create_directories(fs::path(path).parent_path(), ec);
        if (ec) {
            error = fs::path(path).parent_path().string() + " nicht anlegbar (" + ec.message() + ")";
            break;
        }
        error = write_blob(path, b);
        if (!error.empty()) break;
        ++written;
        trace("geschrieben: " + path + " (" + std::to_string(b.size) + " Bytes)");
    }

    if (!error.empty()) {
        trace("FEHLER: " + error);
        notify("PS5 Medic: Installation fehlgeschlagen (" + error + "). Protokoll: /data/ps5medic/installer.log");
        if (g_log) std::fclose(g_log);
        return 1;
    }
    if (written == 0) {
        trace("Alles schon auf dem Stand (" + std::to_string(kept) + " Dateien)");
        notify("PS5 Medic: Kachel und Dienst sind schon auf dem Stand. Kachel unter Medien starten.");
    } else {
        trace("Fertig: " + std::to_string(written) + " Dateien geschrieben, " + std::to_string(kept) + " unveraendert. ShadowMount+ legt die Kachel an.");
        notify("PS5 Medic: Kachel ist angelegt. ShadowMount+ erkennt sie in Sekunden, dann unter Medien starten.");
    }
    if (g_log) std::fclose(g_log);
    return 0;
}
