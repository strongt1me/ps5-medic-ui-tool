// SPDX-License-Identifier: GPL-3.0-or-later
// Die Oberflaeche als native PS5-App (eigener Titel, kein geliehener FAKE00000): Bild ueber VideoOut, Pad ueber ScePad, beides ohne SDL und
// ohne Rechte-Anhebung. Gebaut mit title/build_native_ui.sh (ps5-native-tool, libc.prx), nicht mit dem Makefile-Ziel der UI.
//
// Eine sandboxed App sieht /data und /mnt nicht; die Daten kommen deshalb von einem Dienst-Payload auf 127.0.0.1 (siehe docs/NATIVE.md).
// Stand: Meilenstein 1 (auf der Konsole bestaetigt: Bild, Pad, PS-Taste) -- die UI laeuft mit einem Platzhalter-Dienst (StubService).
#include "medic_ui/app.hpp"
#include "medic_ui/pad_nav.hpp"
#include "medic_ui/splash.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
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

struct PadData {
    std::uint32_t buttons;
    std::uint8_t lx, ly, rx, ry;
    std::uint8_t l2, r2;
    std::uint16_t padding;
    float quat[4], vel[3], acell[3];
    std::uint8_t touch[20];
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

// ---- Platzhalter-Dienst -------------------------------------------------------------------------------------------------------------
// Meilenstein 1: nur Leerwerte. Der echte Dienst kommt per Socket vom Payload (RemoteService, Meilenstein 3).
class StubService final : public medic::ui::IMedicService {
public:
    medic::ui::CheckResult run_check() override {
        medic::ui::CheckResult r;
        r.rules_version = "native-stub";
        r.incomplete = true;  // ohne Dienst nie "keine Befunde" in Gruen
        return r;
    }
    medic::FixPlan plan_fix(const medic::Finding&) override { return {}; }
    medic::ApplyResult apply_fix(const medic::FixPlan&) override { return {}; }
    std::string read_text(const std::string&) override { return {}; }
    std::vector<medic::TxnInfo> history() override { return {}; }
    medic::UndoResult undo(const std::string&) override { return {}; }
    medic::ui::ReportResult make_report(medic::ReportLang) override { return {}; }
    medic::RuleUpdateOffer check_rule_update() override { return {}; }
    medic::RuleUpdateResult install_rule_update() override { return {}; }
    medic::RuleUpdateResult rollback_rule_update() override { return {}; }
    std::string rule_update_url() override { return {}; }
    medic::RuleDownloadResult download_rule_update() override { return {}; }
};

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
void copy_to_buffer(const medic::ui::Canvas& canvas, void* target) {
    if (!g_tile_index) build_tile_index();
    auto* out = static_cast<std::uint32_t*>(target);
    const std::uint32_t* in = canvas.data();
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

}  // namespace

int main() {
    using namespace medic::ui;
    g_log = std::fopen("/data/ps5medic/native.log", "a");
    trace("PS5 Medic (native App) startet");
    install_crash_handler();
    StubService service;
    trace("Dienst bereit");
    App app(service);
    trace("App erstellt");
    {
        Canvas background(App::kWidth, App::kHeight);
        trace("Zeichenflaeche fuer den Hintergrund angelegt");
        if (load_background(background)) app.set_background(background);
        trace("Hintergrund geladen");
        Canvas splash(App::kWidth, App::kHeight);
        trace("Zeichenflaeche fuer das Startbild angelegt");
        if (load_splash(splash)) app.show_splash(splash);
        trace("Startbild geladen");
    }

    (void)sceSystemServiceHideSplashScreen();
    trace("Startbildschirm der Konsole ausgeblendet");
    const int video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    trace("sceVideoOutOpen zurueck");
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

    // Pad: Benutzer-ID des Erstnutzers; das Oeffnen kann anfangs scheitern, deshalb jede Sekunde neu versuchen
    (void)sceUserServiceInitialize(nullptr);
    (void)scePadInit();
    int pad = -1;
    std::uint32_t next_pad_try = 0;
    std::uint32_t prev_buttons = 0;
    PadNav nav;
    std::vector<Button> pressed;
    Canvas canvas(App::kWidth, App::kHeight);
    std::int64_t frame = 0;
    trace("Bildausgabe und Pad bereit");
    // Wie der Demo-Renderer des Boilerplates: erst beide Puffer fuellen, dann das erste Bild (Puffer 0) anzeigen
    app.render(canvas);
    copy_to_buffer(canvas, buffers[0].data);
    copy_to_buffer(canvas, buffers[1].data);
    {
        const int rc = sceVideoOutSubmitFlip(video, 0, 1, 1);
        char line[64];
        std::snprintf(line, sizeof line, "Erstes Bild: SubmitFlip rc=%d", rc);
        trace(line);
        (void)sceVideoOutWaitVblank(video);
    }

    for (;;) {
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
                const std::uint32_t changed = prev_buttons ^ d.buttons;
                auto edge = [&](std::uint32_t bit, Button b) {
                    if ((changed & bit) && (d.buttons & bit)) pressed.push_back(b);
                };
                edge(kBtnCross, Button::Cross);
                edge(kBtnCircle, Button::Circle);
                edge(kBtnSquare, Button::Square);
                edge(kBtnTriangle, Button::Triangle);
                edge(kBtnL1, Button::L1);
                edge(kBtnR1, Button::R1);
                edge(kBtnOptions, Button::Options);
                auto dir = [&](std::uint32_t bit, Button b) {
                    if (changed & bit) nav.dpad(b, (d.buttons & bit) != 0, t, pressed);
                };
                dir(kBtnUp, Button::Up);
                dir(kBtnDown, Button::Down);
                dir(kBtnLeft, Button::Left);
                dir(kBtnRight, Button::Right);
                nav.stick((static_cast<int>(d.lx) - 128) * 256, (static_cast<int>(d.ly) - 128) * 256, t, pressed);
                prev_buttons = d.buttons;
            }
        }
        nav.poll(t, pressed);
        for (const Button b : pressed) app.handle(b);
        pressed.clear();
        if (app.quit_requested()) break;

        app.update();
        app.render(canvas);

        const std::size_t back = static_cast<std::size_t>(frame & 1) ^ 1U;
        copy_to_buffer(canvas, buffers[back].data);
        const int flip_rc = sceVideoOutSubmitFlip(video, static_cast<std::int32_t>(back), 1, frame + 2);
        if (frame < 5 || flip_rc < 0) {
            char line[64];
            std::snprintf(line, sizeof line, "Bild %lld: SubmitFlip rc=%d", static_cast<long long>(frame), flip_rc);
            if (frame < 5 || (frame % 300) == 0) trace(line);
        }
        if (flip_rc < 0) (void)sceKernelUsleep(4000);
        (void)sceVideoOutWaitVblank(video);
        (void)sceVideoOutWaitVblank(video);  // ~30 Bilder/s
        ++frame;
    }
    trace("Beenden");
    leave();
}
