// SPDX-License-Identifier: GPL-3.0-or-later
// Die Oberflaeche als native PS5-App (eigener Titel, kein geliehener FAKE00000): Bild ueber VideoOut, Pad ueber ScePad, beides ohne SDL und
// ohne Rechte-Anhebung. Gebaut mit title/build_native_ui.sh (ps5-native-tool, libc.prx), nicht mit dem Makefile-Ziel der UI.
//
// Die App ist nur Anzeige und Pad: Eine sandboxed App sieht /mnt und die Systemordner nicht. Die Oberflaeche selbst zeichnet der Dienst-Payload
// (ps5/service_main.cpp) und schickt die geaenderten Bildstreifen ueber 127.0.0.1:9060 (ps5/service_protocol.hpp); die App schickt den Pad-Zustand
// zurueck. Laeuft der Dienst nicht, sendet die App ihn aus /data/homebrew/PS5MEDIC/ps5medicservice.elf an den ELF-Lader (Port 9021). docs/NATIVE.md.
#include "medic_ui/canvas.hpp"
#include "medic_ui/font.hpp"
#include "service_protocol.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <ctime>
#include <vector>
#include <unistd.h>

extern "C" {
std::size_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(std::int64_t search_start, std::int64_t search_end, std::size_t length, std::size_t alignment, int memory_type,
                                  std::int64_t* physical_address);
int sceKernelMapDirectMemory(void** address, std::size_t length, int protection, int flags, std::int64_t physical_address, std::size_t alignment);
int sceKernelUsleep(std::uint32_t microseconds);
int sceKernelSendNotificationRequest(std::uint32_t device, void* request, std::size_t size, int blocking);
int sceSystemServiceHideSplashScreen();
int sceSystemServiceLoadExec(const char* path, const char* const* argv);
int sceVideoOutOpen(std::int32_t user_id, std::int32_t bus_type, std::int32_t index, const void* param);
int sceVideoOutSetFlipRate(std::int32_t handle, std::int32_t rate);
int sceVideoOutSubmitFlip(std::int32_t handle, std::int32_t buffer_index, std::uint32_t flip_mode, std::int64_t flip_argument);
int sceVideoOutWaitVblank(std::int32_t handle);
int sceUserServiceInitialize(void*);
int sceUserServiceGetInitialUser(int* user_id);
int scePadInit();
int scePadOpen(int user_id, int type, int index, void* param);
int scePadReadState(int handle, void* data);
}

namespace {

// ---- VideoOut (wie der Demo-Renderer des Boilerplates) ------------------------------------------------------------------------------
constexpr unsigned kW = 1920, kH = 1080;
constexpr std::size_t kFrameBytes = 0x1000000;  // 16 MiB je Bild (1920*1080*4 = 8,3 MB, ausgerichtet)
constexpr std::size_t kMemBytes = kFrameBytes * 2;
constexpr std::size_t kMemAlign = 0x200000;
constexpr int kMemTypeWcGarlic = 3;
constexpr int kMapProtection = 0x33;
constexpr std::uint64_t kPixelFormatRgba8Srgb = UINT64_C(0x8000000022000000);

struct VideoBuffer {
    void* data;
    void* metadata;
    void* reserved0;
    void* reserved1;
};
struct VideoAttribute {
    std::uint8_t reserved[80];
};

extern "C" void sceVideoOutSetBufferAttribute2(VideoAttribute*, std::uint64_t pixel_format, std::uint32_t tiling_mode, std::uint32_t width,
                                               std::uint32_t height, std::uint64_t option, std::uint32_t dcc_control, std::uint64_t dcc_clear_color);
extern "C" int sceVideoOutRegisterBuffers2(std::int32_t handle, std::int32_t set_index, std::int32_t buffer_index_start, VideoBuffer* buffers,
                                           std::int32_t buffer_count, VideoAttribute* attribute, std::int32_t category, void* option);

void flush_range(void* address, std::size_t length) {
    auto* at = static_cast<std::uint8_t*>(address);
    const auto* end = at + length;
    for (; at < end; at += 64) __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}

void notify(const char* msg) {
    static struct {
        char reserved[45];
        char message[3075];
    } req;
    std::memset(&req, 0, sizeof req);
    std::strncpy(req.message, msg, sizeof req.message - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

// Das Protokoll liegt unter /data/ps5medic/ nur, wenn die Sandbox es erlaubt; sonst bleibt es still.
std::FILE* g_log = nullptr;
void trace(const char* line) {
    if (!g_log) return;
    std::fputs(line, g_log);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

// ---- Pad (Layout wie der SDL-Treiber fuer die PS5) ----------------------------------------------------------------------------------
constexpr std::uint32_t kBtnOptions = 0x0008, kBtnUp = 0x0010, kBtnRight = 0x0020, kBtnDown = 0x0040, kBtnLeft = 0x0080, kBtnL1 = 0x0400,
                        kBtnR1 = 0x0800, kBtnTriangle = 0x1000, kBtnCircle = 0x2000, kBtnCross = 0x4000, kBtnSquare = 0x8000;

static_assert(sizeof(float) == 4, "Layout wie PS5_PadData des SDL-Treibers");
struct PadData {
    std::uint32_t buttons;
    std::uint8_t lx, ly, rx, ry;
    std::uint8_t l2, r2;
    std::uint16_t padding;
    float quat[4], vel[3], acell[3];
    std::uint8_t touch[24];  // PS5_PadTouchData: 4 (Finger) + 4 + 2 * 8 (Beruehrungen); mit 20 verrutschte `connected` und die App sah nie ein Pad
    std::uint8_t connected;
    std::uint64_t timestamp;
    std::uint8_t ext[16];
    std::uint8_t count;
    std::uint8_t unknown[15];
};

std::uint32_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}


// Absturzmeldung: Signal, Befehlszeiger, Stapel und Adresse in die Protokolldatei (FreeBSD-amd64-Layout der ucontext: Signalmaske 16 Byte, dann
// mcontext; mc_addr bei 152, mc_rip bei 176, mc_rsp bei 200). `on_crash` wird mitgeschrieben, damit sich der Befehlszeiger gegen die Datei umrechnen laesst.
void on_crash(int sig, siginfo_t*, void* uc) {
    const auto* b = static_cast<const std::uint8_t*>(uc);
    std::uint64_t addr, rip, rsp;
    std::memcpy(&addr, b + 152, 8);
    std::memcpy(&rip, b + 176, 8);
    std::memcpy(&rsp, b + 200, 8);
    char line[200];
    std::snprintf(line, sizeof line, "ABSTURZ Signal %d rip=%llx rsp=%llx addr=%llx handler=%llx", sig, static_cast<unsigned long long>(rip),
                  static_cast<unsigned long long>(rsp), static_cast<unsigned long long>(addr), static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(&on_crash)));
    trace(line);
    // Rohwerte der Signal-Umgebung (zum Entschluesseln, falls die Offsets oben nicht stimmen)
    for (int row = 0; row < 40; row += 4) {
        std::uint64_t w[4];
        std::memcpy(w, b + row * 8, sizeof w);
        std::snprintf(line, sizeof line, "ctx[%02d] %016llx %016llx %016llx %016llx", row, static_cast<unsigned long long>(w[0]), static_cast<unsigned long long>(w[1]),
                      static_cast<unsigned long long>(w[2]), static_cast<unsigned long long>(w[3]));
        trace(line);
    }
    _exit(1);
}

void install_crash_handler() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO;
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGABRT, SIGFPE, SIGSYS}) sigaction(sig, &sa, nullptr);
}

// Der Bildspeicher ist gekachelt (Kachelmodus 0, Kacheln zu 512x128 Pixeln in der Anordnung aus SDL_ps5tilemap.inc des SDL-Treibers fuer die
// PS5; Konsolentest 12. Nov. 2026: lineares Schreiben ergab ein zerstueckeltes Bild). Die Zielposition jedes Pixels wird einmal berechnet.
#include "native_tilemap.inc"

std::uint32_t* g_tile_index = nullptr;  // fuer Pixel i (zeilenweise) der Index im Bildspeicher

void build_tile_index() {
    g_tile_index = new std::uint32_t[static_cast<std::size_t>(kW) * kH];
    const double tiles_per_row = static_cast<double>(kW) / PS5_TILE_WIDTH;
    for (unsigned y = 0; y < kH; ++y)
        for (unsigned x = 0; x < kW; ++x) {
            const int tx = static_cast<int>(x / PS5_TILE_WIDTH), ty = static_cast<int>(y / PS5_TILE_HEIGHT);
            const int t = static_cast<int>(PS5_TILE_SIZE * (tx + ty * tiles_per_row));
            g_tile_index[static_cast<std::size_t>(y) * kW + x] = static_cast<std::uint32_t>(t + PS5_tilemap[y % PS5_TILE_HEIGHT][x % PS5_TILE_WIDTH]);
        }
}

// ARGB (0xAARRGGBB) -> RGBA (Bytes R,G,B,A = 0xAABBGGRR als 32-Bit-Wert) an die gekachelte Zielposition, dann aus dem Cache in den Bildspeicher
void copy_to_buffer(const std::uint32_t* in, void* target) {
    if (!g_tile_index) build_tile_index();
    auto* out = static_cast<std::uint32_t*>(target);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kW) * kH; ++i) {
        const std::uint32_t p = in[i];
        out[g_tile_index[i]] = (p & 0xFF00FF00u) | ((p & 0x00FF0000u) >> 16) | ((p & 0x000000FFu) << 16);
    }
    flush_range(target, kFrameBytes);
}

[[noreturn]] void leave() {
    notify("PS5 Medic: beendet.");
    sceSystemServiceLoadExec("exit", nullptr);
    for (;;) sceKernelUsleep(1000000);
}

// ---- Verbindung zum Dienst ----------------------------------------------------------------------------------------------------------
using medic::ui::Canvas;
using medic::ui::Font;

// Einfache Statusseite (Dienst startet, Fehler), solange noch kein Bild vom Dienst da ist
void draw_status(Canvas& c, const char* line1, const char* line2) {
    c.clear(medic::ui::rgb(12, 18, 32));
    const Font& title = Font::get(Font::Id::Title);
    const Font& body = Font::get(Font::Id::Body);
    title.draw(c, 120, 400, "PS5 Medic", medic::ui::rgb(240, 245, 255));
    body.draw(c, 120, 500, line1, medic::ui::rgb(200, 210, 230));
    if (line2 && *line2) body.draw(c, 120, 550, line2, medic::ui::rgb(240, 180, 90));
}

// Sendet den Dienst-Payload aus der Datei an den ELF-Lader. 0 = gesendet, sonst 1 Datei fehlt, 2 Lader nicht erreichbar, 3 Senden fehlgeschlagen.
int send_service_elf() {
    std::FILE* f = std::fopen("/data/homebrew/PS5MEDIC/ps5medicservice.elf", "rb");
    if (!f) return 1;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (64l << 20)) {
        std::fclose(f);
        return 1;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    const bool read_ok = std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    if (!read_ok) return 1;
    const int s = medic::proto::connect_local(medic::proto::kLoaderPort, 10);
    if (s < 0) return 2;
    const bool sent = medic::proto::send_all(s, data.data(), data.size());
    close(s);
    return sent ? 0 : 3;
}

// Liest den Strom des Dienstes in `screen` (1920x1080 ARGB); meldet Ereignisse ueber frame_ready und quit.
class StreamReader {
public:
    explicit StreamReader(std::uint32_t* screen) : screen_(screen) {}
    bool frame_ready = false;
    bool quit = false;

    // false: Verbindung weg
    bool pump(int fd) {
        for (int guard = 0; guard < 4096; ++guard) {
            std::uint8_t* dst;
            std::size_t want;
            if (head_got_ < 5) {
                dst = head_ + head_got_;
                want = 5 - head_got_;
            } else if (type_ == medic::proto::kBand && sub_got_ < 4) {
                dst = sub_ + sub_got_;
                want = 4 - sub_got_;
            } else if (type_ == medic::proto::kBand && body_left_ > 0) {
                dst = reinterpret_cast<std::uint8_t*>(screen_) + band_off_;
                want = body_left_;
            } else if (body_left_ > 0) {
                dst = skip_;
                want = body_left_ < sizeof skip_ ? body_left_ : sizeof skip_;
            } else {
                finish_message();
                continue;
            }
            const ssize_t n = recv(fd, dst, want, MSG_DONTWAIT);
            if (n > 0) {
                advance(static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0) return false;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
            if (errno == EINTR) continue;
            return false;
        }
        return true;
    }

private:
    void advance(std::size_t n) {
        if (head_got_ < 5) {
            head_got_ += n;
            if (head_got_ == 5) {
                type_ = static_cast<char>(head_[0]);
                body_left_ = medic::proto::get32(head_ + 1);
                sub_got_ = 0;
                if (type_ == medic::proto::kBand && body_left_ < 4) body_left_ = 0;  // ungueltig: ignorieren
            }
            return;
        }
        if (type_ == medic::proto::kBand && sub_got_ < 4) {
            sub_got_ += n;
            body_left_ -= n;
            if (sub_got_ == 4) {
                const unsigned y0 = medic::proto::get16(sub_), rows = medic::proto::get16(sub_ + 2);
                const std::size_t bytes = static_cast<std::size_t>(rows) * kW * 4;
                if (y0 + rows > kH || bytes != body_left_) {  // passt nicht: Rest verwerfen
                    type_ = '?';
                } else {
                    band_off_ = static_cast<std::size_t>(y0) * kW * 4;
                }
            }
            return;
        }
        body_left_ -= n;
        if (type_ == medic::proto::kBand) band_off_ += n;
    }
    void finish_message() {
        if (type_ == medic::proto::kPresent) frame_ready = true;
        if (type_ == medic::proto::kQuit) quit = true;
        head_got_ = 0;
        type_ = 0;
    }

    std::uint32_t* screen_;
    std::uint8_t head_[5] = {};
    std::uint8_t sub_[4] = {};
    std::uint8_t skip_[512] = {};
    std::size_t head_got_ = 0, sub_got_ = 0, body_left_ = 0, band_off_ = 0;
    char type_ = 0;
};

}  // namespace

int main() {
    g_log = std::fopen("/data/ps5medic/native.log", "a");
    trace("PS5 Medic (native App) startet");
    install_crash_handler();

    (void)sceSystemServiceHideSplashScreen();
    const int video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (video < 0) {
        notify("PS5 Medic: Bildausgabe nicht verfuegbar.");
        trace("sceVideoOutOpen fehlgeschlagen");
        leave();
    }
    const std::size_t pool = sceKernelGetDirectMemorySize();
    std::int64_t phys = 0;
    if (pool < kMemBytes || sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool), kMemBytes, kMemAlign, kMemTypeWcGarlic, &phys) < 0) {
        notify("PS5 Medic: Bildspeicher nicht verfuegbar.");
        trace("Direktspeicher fehlgeschlagen");
        leave();
    }
    void* mapped = nullptr;
    if (sceKernelMapDirectMemory(&mapped, kMemBytes, kMapProtection, 0, phys, kMemAlign) < 0) {
        notify("PS5 Medic: Bildspeicher nicht abbildbar.");
        trace("MapDirectMemory fehlgeschlagen");
        leave();
    }
    auto* frames = static_cast<std::uint8_t*>(mapped);
    std::array<VideoBuffer, 2> buffers{{{frames, nullptr, nullptr, nullptr}, {frames + kFrameBytes, nullptr, nullptr, nullptr}}};
    VideoAttribute attr{};
    (void)sceVideoOutSetFlipRate(video, 0);
    sceVideoOutSetBufferAttribute2(&attr, kPixelFormatRgba8Srgb, 0, kW, kH, 0, 0, 0);
    if (sceVideoOutRegisterBuffers2(video, 0, 0, buffers.data(), 2, &attr, 0, nullptr) < 0) {
        notify("PS5 Medic: Puffer nicht registrierbar.");
        trace("RegisterBuffers fehlgeschlagen");
        leave();
    }

    (void)sceUserServiceInitialize(nullptr);
    (void)scePadInit();
    int pad = -1;
    std::uint32_t next_pad_try = 0;

    Canvas status(kW, kH);
    std::vector<std::uint32_t> screen(static_cast<std::size_t>(kW) * kH, 0xFF000000u);
    StreamReader reader(screen.data());
    std::int64_t frame = 0;

    auto show = [&](const std::uint32_t* pixels) {
        const std::size_t back = static_cast<std::size_t>(frame & 1) ^ 1U;
        copy_to_buffer(pixels, buffers[back].data);
        const int rc = sceVideoOutSubmitFlip(video, static_cast<std::int32_t>(back), 1, frame + 1);
        if (rc < 0) (void)sceKernelUsleep(4000);
        (void)sceVideoOutWaitVblank(video);
        ++frame;
    };
    auto show_status = [&](const char* a, const char* b) {
        draw_status(status, a, b);
        show(status.data());
    };

    // Ein Bild zeigen, damit der Bildschirm nicht schwarz bleibt, waehrend der Dienst startet
    show_status("Dienst wird gestartet ...", "");
    show_status("Dienst wird gestartet ...", "");

    int sock = -1;
    for (;;) {
        // --- Verbindung herstellen (notfalls den Dienst starten) ---
        if (sock < 0) {
            sock = medic::proto::connect_local(medic::proto::kPort, 2);
            if (sock < 0) {
                const int rc = send_service_elf();
                trace(rc == 0 ? "Dienst-ELF an den Lader gesendet" : "Dienst-ELF konnte nicht gesendet werden");
                if (rc != 0) {
                    show_status(rc == 1 ? "Dienstdatei fehlt: /data/homebrew/PS5MEDIC/ps5medicservice.elf" : "ELF-Lader (Port 9021) nicht erreichbar.",
                                "Jailbreak/Lader pruefen, dann die App neu starten.");
                    sceKernelUsleep(3000000);
                    continue;
                }
                for (int i = 0; i < 40 && sock < 0; ++i) {
                    show_status("Dienst wird gestartet ...", "");
                    sock = medic::proto::connect_local(medic::proto::kPort, 2);
                    if (sock < 0) sceKernelUsleep(500000);
                }
            }
            if (sock < 0) {
                trace("Dienst nicht erreichbar");
                show_status("Der Dienst antwortet nicht.", "Protokoll: /data/ps5medic/service.log");
                sceKernelUsleep(3000000);
                continue;
            }
            trace("Mit dem Dienst verbunden");
            reader = StreamReader(screen.data());
        }

        // --- Pad lesen und senden ---
        const std::uint32_t t = now_ms();
        if (pad < 0 && t >= next_pad_try) {
            next_pad_try = t + 1000;
            int uid = 0;
            if (sceUserServiceGetInitialUser(&uid) == 0) {
                pad = scePadOpen(uid, 0, 0, nullptr);
                if (pad >= 0) trace("Pad geoeffnet");
            }
        }
        if (pad >= 0) {
            PadData d{};
            if (scePadReadState(pad, &d) == 0 && d.connected) {
                std::uint8_t msg[8];
                medic::proto::put32(msg, d.buttons);
                msg[4] = d.lx;
                msg[5] = d.ly;
                msg[6] = d.rx;
                msg[7] = d.ry;
                if (!medic::proto::send_message(sock, medic::proto::kPad, msg, sizeof msg)) {
                    close(sock);
                    sock = -1;
                    continue;
                }
            }
        }

        // --- Bilder des Dienstes lesen ---
        if (!reader.pump(sock)) {
            trace("Verbindung zum Dienst verloren");
            close(sock);
            sock = -1;
            show_status("Verbindung zum Dienst verloren ...", "Neuer Versuch");
            continue;
        }
        if (reader.quit) {
            trace("Dienst meldet: beendet");
            break;
        }
        if (reader.frame_ready) {
            reader.frame_ready = false;
            show(screen.data());
        } else {
            (void)sceKernelUsleep(8000);
        }
    }
    trace("Beenden");
    leave();
}
