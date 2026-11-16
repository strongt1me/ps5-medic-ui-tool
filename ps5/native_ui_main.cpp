// SPDX-License-Identifier: GPL-3.0-or-later
// Die Oberflaeche als native PS5-App (eigener Titel, kein geliehener FAKE00000): Bild ueber VideoOut, Pad ueber ScePad, beides ohne SDL und
// ohne Rechte-Anhebung. Gebaut mit title/build_native_ui.sh (ps5-native-tool, libc.prx), nicht mit dem Makefile-Ziel der UI.
//
// Die App ist nur Anzeige und Pad: Eine sandboxed App sieht /mnt und die Systemordner nicht. Die Oberflaeche selbst zeichnet der Dienst-Payload
// (ps5/service_main.cpp) und schickt die geaenderten Bildstreifen ueber 127.0.0.1:9060 (ps5/service_protocol.hpp); die App schickt den Pad-Zustand
// zurueck. Laeuft der Dienst nicht, sendet die App ihn aus /data/homebrew/PS5MEDIC/ps5medicservice.elf an den ELF-Lader (Port 9021). docs/NATIVE.md.
#include "medic_ui/canvas.hpp"
#include "medic_ui/font.hpp"
#include "medic_ui/splash.hpp"
#include "lang_detect.hpp"
#include "service_protocol.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
int scePadClose(int handle);
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

// ---- Sprache der Meldungen vor dem Dienst ---------------------------------------------------------------------------------------------
// Die App hat keinen Zugriff auf die Systemfunktion fuer die Konsolensprache (ein weiterer Import wuerde auf anderen Firmwares das Starten
// riskieren). Sie liest stattdessen die Sprache, die der Dienst schreibt: zuerst die gewaehlte Sprache aus config.ini (language=xx), sonst die
// Systemsprache, die der Dienst nach jedem Start in /data/ps5medic/system_language ablegt, sonst Englisch (Marc, 13. Nov. 2026).
enum Msg { kMsgBye, kMsgNoVideo, kMsgNoFrameMem, kMsgNoMap, kMsgNoRegister, kMsgNoServiceFile, kMsgNoLoader, kMsgCheckLoader, kMsgNoAnswer, kMsgLogHint,
           kMsgLost, kMsgRetry, kMsgCount };
// Zeilen 0 bis 4 (Benachrichtigungen) bleiben ASCII, damit nichts verstuemmelt wird; die Statuszeilen nutzen die Schrift der UI (mit Akzenten).
constexpr const char* kMsgs[5][kMsgCount] = {
    {"PS5 Medic: beendet.", "PS5 Medic: Bildausgabe nicht verfuegbar.", "PS5 Medic: Bildspeicher nicht verfuegbar.", "PS5 Medic: Bildspeicher nicht abbildbar.",
     "PS5 Medic: Puffer nicht registrierbar.", "Dienstdatei fehlt: ", "ELF-Lader (Port 9021) nicht erreichbar.", "Jailbreak/Lader pruefen, dann die App neu starten.",
     "Der Dienst antwortet nicht.", "Protokoll: ", "Verbindung zum Dienst verloren ...", "Neuer Versuch"},
    {"PS5 Medic: closed.", "PS5 Medic: video output unavailable.", "PS5 Medic: frame memory unavailable.", "PS5 Medic: frame memory cannot be mapped.",
     "PS5 Medic: buffer cannot be registered.", "Service file missing: ", "ELF loader (port 9021) not reachable.", "Check the jailbreak/loader, then restart the app.",
     "The service is not responding.", "Log: ", "Connection to the service lost ...", "Trying again"},
    {"PS5 Medic : ferme.", "PS5 Medic : sortie video indisponible.", "PS5 Medic : memoire d'image indisponible.", "PS5 Medic : memoire d'image non mappable.",
     "PS5 Medic : tampon non enregistrable.", "Fichier du service introuvable : ", "Chargeur ELF (port 9021) injoignable.",
     "Vérifiez le jailbreak/chargeur, puis relancez l'application.", "Le service ne répond pas.", "Journal : ", "Connexion au service perdue ...", "Nouvel essai"},
    {"PS5 Medic: chiuso.", "PS5 Medic: uscita video non disponibile.", "PS5 Medic: memoria immagine non disponibile.", "PS5 Medic: memoria immagine non mappabile.",
     "PS5 Medic: buffer non registrabile.", "File del servizio mancante: ", "ELF loader (porta 9021) non raggiungibile.",
     "Controllare jailbreak/loader, poi riavviare l'app.", "Il servizio non risponde.", "Registro: ", "Connessione al servizio persa ...", "Nuovo tentativo"},
    {"PS5 Medic: cerrado.", "PS5 Medic: salida de video no disponible.", "PS5 Medic: memoria de imagen no disponible.", "PS5 Medic: memoria de imagen no mapeable.",
     "PS5 Medic: bufer no registrable.", "Falta el archivo del servicio: ", "Cargador ELF (puerto 9021) inaccesible.",
     "Comprueba el jailbreak/cargador y reinicia la aplicación.", "El servicio no responde.", "Registro: ", "Conexión con el servicio perdida ...", "Reintentando"},
};
int g_lang = 1;  // 0 de, 1 en, 2 fr, 3 it, 4 es; bis zur Erkennung Englisch
const char* tr(Msg m) { return kMsgs[g_lang][m]; }

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
std::uint32_t now_ms();
std::uint32_t g_t0 = 0;
// Jede Zeile mit Millisekunden seit dem Start der App (Diagnose der Startdauer)
void trace(const char* line) {
    if (!g_log) return;
    std::fprintf(g_log, "[%u ms] %s\n", static_cast<unsigned>(now_ms() - g_t0), line);
    std::fflush(g_log);
}

// ---- Pad (Layout wie der SDL-Treiber fuer die PS5; die Tasten wertet der Dienst aus) ----------------------------------------------------------------------------------


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
    notify(tr(kMsgBye));
    sceSystemServiceLoadExec("exit", nullptr);
    for (;;) sceKernelUsleep(1000000);
}

// ---- Verbindung zum Dienst ----------------------------------------------------------------------------------------------------------
using medic::ui::Canvas;
using medic::ui::Font;

// Statusseite (Dienst startet, Fehler), solange noch kein Bild vom Dienst da ist: das Startbild der UI mit einer Zeile am unteren Rand. Das erste
// Bild des Dienstes ist dasselbe Startbild, der Uebergang faellt also nicht auf. Ohne Startbild ein einfacher dunkler Hintergrund.
Canvas* g_splash = nullptr;

// Der Fortschrittsbalken des Startbilds (ui/assets/splash.jpg, unten links, 1920x1080): innen x 237..657, y 962..980. Er wird mit einem eigenen
// Balken aus 14 Segmenten uebermalt, die sich nach dem Fortschritt fuellen (Farben aus dem Bild).
void draw_progress(Canvas& c, float percent) {
    using medic::ui::rgb;
    if (g_splash) c.copy_from(*g_splash);
    else c.clear(rgb(12, 18, 32));
    constexpr int kX = 237, kY = 962, kH = 19, kSegments = 14, kPitch = 30, kSegW = 26;
    c.fill_rect(kX - 2, kY - 1, kSegments * kPitch + 2, kH + 2, rgb(15, 32, 58));
    const float full = percent / 100.0f * kSegments;
    for (int k = 0; k < kSegments; ++k) {
        const int x = kX + k * kPitch;
        if (static_cast<float>(k + 1) <= full) {
            c.fill_vgradient(x, kY, kSegW, kH, rgb(71, 201, 253), rgb(20, 106, 181));
        } else if (static_cast<float>(k) < full) {  // das gerade fuellende Segment: anteilig, vorn mit hellem Rand
            const int w = static_cast<int>((full - static_cast<float>(k)) * kSegW);
            c.fill_rect(x, kY, kSegW, kH, rgb(22, 46, 78));
            if (w > 0) {
                c.fill_vgradient(x, kY, w, kH, rgb(71, 201, 253), rgb(20, 106, 181));
                c.fill_rect(x + w - 2 < x ? x : x + w - 2, kY, 2, kH, rgb(163, 198, 230));
            }
        } else {
            c.fill_rect(x, kY, kSegW, kH, rgb(22, 46, 78));
        }
    }
}

void draw_status(Canvas& c, const char* line1, const char* line2) {
    if (g_splash) c.copy_from(*g_splash);
    else c.clear(medic::ui::rgb(12, 18, 32));
    const Font& body = Font::get(Font::Id::Body);
    const bool two = line2 && *line2;
    const int h = two ? 120 : 76;
    c.fill_round_rect(80, 1080 - 60 - h, 1760, h, 20, 0xCC0C1220u);
    body.draw(c, 120, 1080 - 60 - h + 20, line1, medic::ui::rgb(230, 238, 250));
    if (two) body.draw(c, 120, 1080 - 60 - h + 64, line2, medic::ui::rgb(240, 180, 90));
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
    int progress = 0;  // letzter 'G'-Wert des Dienstes (0..100)
    std::size_t received = 0;  // bisher gelesene Bytes (zeigt, dass der Dienst noch sendet)
    bool hello_seen = false;   // das Hallo des Dienstes ist angekommen
    // Build-Kennung des Dienstes aus dem Hallo; leer bei einem Dienst ohne Kennung (aelter als diese Pruefung)
    std::string service_build() const { return hello_got_ > 4 ? std::string(reinterpret_cast<const char*>(hello_) + 4, hello_got_ - 4) : std::string(); }

    // false: Verbindung weg
    bool pump(int fd) {
        bool waiting = false;  // in diesem Aufruf schon auf den Rest einer Nachricht gewartet? (eine eigene Marke: now_ms() kann 0 sein, und "| 1" lag bei geraden Zeiten hinter der Uhr)
        std::uint32_t wait_from = 0;  // Beginn dieses Wartens
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
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Mitten in einer Nachricht oder in einem Bild (Streifen da, 'P' fehlt noch): kurz auf den Rest warten, statt erst nach einem ganzen Bildtakt
                // (16 ms) weiterzulesen. Sonst kam ein Vollbild nur etwa 100 KB je Takt an (erstes Bild 1,4 s, Konsole 14. Nov. 2026). Hoechstens 50 ms je Aufruf.
                // Ein fertiges Bild (frame_ready) wird nicht aufgehalten, auch wenn schon der Kopf des naechsten da ist.
                if (!frame_ready && (head_got_ > 0 || in_frame_)) {
                    if (!waiting) {
                        waiting = true;
                        wait_from = now_ms();
                    }
                    if (now_ms() - wait_from < 50u) {
                        (void)sceKernelUsleep(300);
                        continue;
                    }
                }
                return true;
            }
            if (errno == EINTR) continue;
            return false;
        }
        return true;
    }

private:
    void advance(std::size_t n) {
        received += n;
        if (head_got_ < 5) {
            head_got_ += n;
            if (head_got_ == 5) {
                type_ = static_cast<char>(head_[0]);
                body_left_ = medic::proto::get32(head_ + 1);
                sub_got_ = 0;
                if (type_ == medic::proto::kBand) in_frame_ = true;
                if (type_ == medic::proto::kBand && body_left_ < 4) type_ = '?';  // ungueltig: die wenigen Bytes ueberspringen
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
        if (type_ == medic::proto::kHello)  // das Hallo samt Build-Kennung des Dienstes aufheben (kommt in beliebig vielen Stuecken an)
            for (std::size_t i = 0; i < n && hello_got_ < sizeof hello_; ++i) hello_[hello_got_++] = skip_[i];
        body_left_ -= n;
        if (type_ == medic::proto::kBand) band_off_ += n;
    }
    void finish_message() {
        if (type_ == medic::proto::kPresent) {
            frame_ready = true;
            in_frame_ = false;
        }
        if (type_ == medic::proto::kQuit) quit = true;
        if (type_ == medic::proto::kHello) hello_seen = true;
        if (type_ == medic::proto::kProgress && skip_[0] <= 100) progress = skip_[0];
        head_got_ = 0;
        type_ = 0;
    }

    std::uint32_t* screen_;
    std::uint8_t head_[5] = {};
    std::uint8_t sub_[4] = {};
    std::uint8_t skip_[512] = {};
    std::uint8_t hello_[4 + medic::proto::kMaxBuildId] = {};
    std::size_t hello_got_ = 0;
    std::size_t head_got_ = 0, sub_got_ = 0, body_left_ = 0, band_off_ = 0;
    char type_ = 0;
    bool in_frame_ = false;  // Streifen eines Bildes empfangen, das abschliessende 'P' fehlt noch
};

constexpr std::uint32_t kBtnCircle = 0x2000;  // wie im Dienst (service_main.cpp)

}  // namespace

int main() {
    g_t0 = now_ms();
    g_log = std::fopen("/data/ps5medic/native.log", "a");
    trace("PS5 Medic (native App) startet");
    g_lang = medic::lang::detect();
    trace(g_lang == 0 ? "Sprache der Meldungen: de" : g_lang == 1 ? "Sprache der Meldungen: en" : g_lang == 2 ? "Sprache der Meldungen: fr" : g_lang == 3 ? "Sprache der Meldungen: it" : "Sprache der Meldungen: es");
    install_crash_handler();

    (void)sceSystemServiceHideSplashScreen();
    const int video = sceVideoOutOpen(0xff, 0, 0, nullptr);
    if (video < 0) {
        notify(tr(kMsgNoVideo));
        trace("sceVideoOutOpen fehlgeschlagen");
        leave();
    }
    const std::size_t pool = sceKernelGetDirectMemorySize();
    std::int64_t phys = 0;
    if (pool < kMemBytes || sceKernelAllocateDirectMemory(0, static_cast<std::int64_t>(pool), kMemBytes, kMemAlign, kMemTypeWcGarlic, &phys) < 0) {
        notify(tr(kMsgNoFrameMem));
        trace("Direktspeicher fehlgeschlagen");
        leave();
    }
    void* mapped = nullptr;
    if (sceKernelMapDirectMemory(&mapped, kMemBytes, kMapProtection, 0, phys, kMemAlign) < 0) {
        notify(tr(kMsgNoMap));
        trace("MapDirectMemory fehlgeschlagen");
        leave();
    }
    auto* frames = static_cast<std::uint8_t*>(mapped);
    std::array<VideoBuffer, 2> buffers{{{frames, nullptr, nullptr, nullptr}, {frames + kFrameBytes, nullptr, nullptr, nullptr}}};
    VideoAttribute attr{};
    (void)sceVideoOutSetFlipRate(video, 0);
    sceVideoOutSetBufferAttribute2(&attr, kPixelFormatRgba8Srgb, 0, kW, kH, 0, 0, 0);
    if (sceVideoOutRegisterBuffers2(video, 0, 0, buffers.data(), 2, &attr, 0, nullptr) < 0) {
        notify(tr(kMsgNoRegister));
        trace("RegisterBuffers fehlgeschlagen");
        leave();
    }

    (void)sceUserServiceInitialize(nullptr);
    (void)scePadInit();
    int pad = -1;
    std::uint32_t next_pad_try = 0;
    bool pad_sent = false;  // zuletzt wurde ein echter Zustand gesendet (nach einem Verlust folgt einmal der Ruhezustand)
    auto open_pad = [&]() {
        const std::uint32_t t = now_ms();
        if (pad >= 0 || t < next_pad_try) return;
        next_pad_try = t + 1000;
        int uid = 0;
        if (sceUserServiceGetInitialUser(&uid) == 0) {
            pad = scePadOpen(uid, 0, 0, nullptr);
            if (pad >= 0) trace("Pad geoeffnet");
        }
    };
    // Das Pad wird geschlossen und neu geoeffnet, wenn das Lesen 5 s lang fehlschlaegt (Handle ungueltig, anderer Benutzer) oder es 30 s lang
    // nur "nicht verbunden" meldet (ein ausgeschalteter oder abgezogener Controller soll nicht dauernd neu geoeffnet werden; meldet er sich
    // wieder, wird bad_since 0 und nichts geschieht).
    std::uint32_t bad_since = 0;
    bool bad_is_error = false;
    auto pad_ok = [&](int rc, bool connected) {
        if (rc == 0 && connected) {
            bad_since = 0;
            return;
        }
        const std::uint32_t t = now_ms();
        const bool is_error = rc != 0;
        if (bad_since == 0 || (is_error && !bad_is_error)) {  // neuer Zustand (ein Lesefehler zaehlt ab jetzt mit der kuerzeren Frist)
            bad_since = t ? t : 1;
            bad_is_error = is_error;
        }
        if (pad >= 0 && t - bad_since >= (bad_is_error ? 5000u : 30000u)) {
            (void)scePadClose(pad);
            pad = -1;
            next_pad_try = 0;
            bad_since = 0;
            trace("Pad neu geoeffnet (keine Antwort)");
        }
    };
    // Wartet ms Millisekunden, waehrend ein Fehlerbild steht; Kreis beendet die App (sonst ginge das nur ueber die PS-Taste)
    auto wait_with_circle = [&](int ms) {
        for (int waited = 0; waited < ms; waited += 50) {
            open_pad();
            if (pad >= 0) {
                PadData d{};
                if (scePadReadState(pad, &d) == 0 && d.connected && (d.buttons & kBtnCircle)) {
                    trace("Beenden mit Kreis");
                    leave();
                }
            }
            sceKernelUsleep(50000);
        }
    };

    Canvas status(kW, kH);
    Canvas splash(kW, kH);
    if (medic::ui::load_splash(splash)) g_splash = &splash;
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
    // Balken des Startbilds: zeigt weich nachlaufend dem Ziel entgegen (nie rueckwaerts, nie voll vor dem ersten Bild)
    float shown = 0, target = 4;
    auto bar_frame = [&](float to) {
        if (to > target) target = to;
        if (target > 99) target = 99;
        shown += (target - shown) * 0.10f + 0.04f;  // naehert sich dem Ziel, bleibt aber nie stehen
        if (shown > target) shown = target;
        draw_progress(status, shown);
        show(status.data());
    };

    // Ein Bild zeigen, damit der Bildschirm nicht schwarz bleibt, waehrend der Dienst startet
    bar_frame(4);
    bar_frame(4);
    trace("Startbild mit Balken angezeigt");

    int sock = -1;
    bool first_frame_logged = false;
    std::uint32_t connected_at = 0;  // Zeitpunkt der Verbindung bzw. der letzten Daten des Dienstes: kommt 60 s lang nichts und kein erstes Bild, bedient der Dienst diese Verbindung nicht
    bool build_checked = false;        // die Build-Kennung des Dienstes ist auf dieser Verbindung schon verglichen
    bool restarted_for_build = false;  // den alten Dienst gab es schon einmal zu beenden: kein zweiter Versuch (keine Endlosschleife, falls die Kennungen nie gleich werden)
    std::size_t seen_bytes = 0;      // reader.received beim letzten Blick (Daten zuruecksetzen connected_at: eine App im Hintergrund hat nach der Rueckkehr volle Puffer, keinen haengenden Dienst)
    for (;;) {
        // --- Verbindung herstellen (notfalls den Dienst starten) ---
        if (sock < 0) {
            bar_frame(8);
            sock = medic::proto::connect_local(medic::proto::kPort, 2, /*big_buffers=*/true);
            if (sock < 0) {
                const int rc = send_service_elf();
                trace(rc == 0 ? "Dienst-ELF an den Lader gesendet" : "Dienst-ELF konnte nicht gesendet werden");
                if (rc != 0) {
                    const std::string line1 = rc == 1 ? std::string(tr(kMsgNoServiceFile)) + "/data/homebrew/PS5MEDIC/ps5medicservice.elf" : std::string(tr(kMsgNoLoader));
                    show_status(line1.c_str(), tr(kMsgCheckLoader));
                    wait_with_circle(3000);
                    continue;
                }
                for (int i = 0; i < 400 && sock < 0; ++i) {  // bis etwa 20 s, der Balken laeuft dabei weiter
                    for (int k = 0; k < 3; ++k) bar_frame(14);
                    if (i % 5 == 0) sock = medic::proto::connect_local(medic::proto::kPort, 2, /*big_buffers=*/true);
                }
            }
            if (sock < 0) {
                trace("Dienst nicht erreichbar");
                const std::string hint = std::string(tr(kMsgLogHint)) + "/data/ps5medic/service.log";
                show_status(tr(kMsgNoAnswer), hint.c_str());
                wait_with_circle(3000);
                continue;
            }
            trace("Mit dem Dienst verbunden");
            build_checked = false;
            connected_at = now_ms();
            reader = StreamReader(screen.data());
            seen_bytes = 0;
            first_frame_logged = false;
            bar_frame(22);
        }

        // --- Pad lesen und senden ---
        open_pad();
        if (pad >= 0) {
            PadData d{};
            std::uint8_t msg[8];
            bool send = false;
            const int read_rc = scePadReadState(pad, &d);
            const bool pad_good = read_rc == 0 && d.connected;
            pad_ok(read_rc, d.connected != 0);
            if (pad_good) {
                medic::proto::put32(msg, d.buttons);
                msg[4] = d.lx;
                msg[5] = d.ly;
                msg[6] = d.rx;
                msg[7] = d.ry;
                send = pad_sent = true;
            } else if (pad_sent) {  // Pad weg oder Lesefehler: einmal den Ruhezustand senden, sonst liefe eine gehaltene Richtung im Dienst weiter
                medic::proto::put32(msg, 0);
                msg[4] = msg[5] = msg[6] = msg[7] = 128;
                send = true;
                pad_sent = false;
            }
            if (send && !medic::proto::send_message_drop(sock, medic::proto::kPad, msg, sizeof msg)) {
                char why[80];
                std::snprintf(why, sizeof why, "Senden an den Dienst fehlgeschlagen (errno %d)", errno);
                trace(why);
                // Der Dienst hat vielleicht gerade 'Q' gesendet und geschlossen: vor dem Neuverbinden noch einmal lesen
                (void)reader.pump(sock);
                if (reader.quit) {
                    trace("Dienst meldet: beendet");
                    break;
                }
                close(sock);
                sock = -1;
                continue;
            }
        }

        // --- Bilder des Dienstes lesen ---
        const bool alive = reader.pump(sock);
        if (reader.received != seen_bytes) {  // der Dienst sendet noch: die 60 s ohne erstes Bild zaehlen ab dem letzten Datenempfang
            seen_bytes = reader.received;
            connected_at = now_ms();
        }
        // Die Ende-Meldung zuerst auswerten: der Dienst schliesst die Verbindung gleich nach 'Q', das ist kein Verbindungsverlust
        if (reader.quit) {
            trace("Dienst meldet: beendet");
            break;
        }
        if (!alive) {
            trace("Verbindung zum Dienst verloren");
            close(sock);
            sock = -1;
            show_status(tr(kMsgLost), tr(kMsgRetry));
            wait_with_circle(2500);  // lesbar stehen lassen, bevor der neue Versuch das Bild ersetzt
            continue;
        }
        // Versionsabgleich: laeuft noch ein Dienst aus einer aelteren Fassung (nach einem Update, App nicht ordentlich beendet), wuerde er bedient und der neue nie geladen
        if (reader.hello_seen && !build_checked) {
            build_checked = true;
            const std::string theirs = reader.service_build();
            if (theirs != medic::proto::kBuildId && !restarted_for_build) {
                restarted_for_build = true;
                const std::string why = "Dienst hat eine andere Fassung (" + (theirs.empty() ? std::string("ohne Kennung") : theirs) + ") als die App (" + medic::proto::kBuildId + "): beende ihn und starte den neuen";
                trace(why.c_str());
                (void)medic::proto::send_message(sock, medic::proto::kShutdown, nullptr, 0);
                close(sock);
                sock = -1;
                for (int i = 0; i < 40; ++i) {  // bis zu vier Sekunden warten, bis der alte Dienst den Port freigibt
                    const int s = medic::proto::connect_local(medic::proto::kPort, 1);
                    if (s < 0) break;
                    close(s);
                    (void)sceKernelUsleep(100000);
                }
                continue;
            }
        }
        if (reader.frame_ready) {
            reader.frame_ready = false;
            if (!first_frame_logged) {  // der Balken laeuft kurz voll, dann erscheint die Oberflaeche
                first_frame_logged = true;
                target = 100;
                for (int k = 0; k < 6; ++k) {
                    shown += (100 - shown) * 0.5f;
                    draw_progress(status, shown);
                    show(status.data());
                }
                trace("Erstes Bild des Dienstes angezeigt");
            }
            show(screen.data());
        } else if (!first_frame_logged) {
            if (now_ms() - connected_at > 60000u) {  // verbunden, aber nie ein Bild: der Dienst haengt oder bedient noch eine andere App
                trace("Kein erstes Bild vom Dienst nach 60 s");
                close(sock);
                sock = -1;
                const std::string hint = std::string(tr(kMsgLogHint)) + "/data/ps5medic/service.log";
                show_status(tr(kMsgNoAnswer), hint.c_str());
                wait_with_circle(3000);  // Kreis beendet die App, sonst folgt ein neuer Versuch
                continue;
            }
            bar_frame(22.0f + static_cast<float>(reader.progress) * 0.73f);  // 22 % (verbunden) bis 95 % (Dienst bereit)
        } else {
            (void)sceKernelUsleep(3000);  // Pad und neue Bilder oefter abfragen (vorher 8 ms)
        }
    }
    if (sock >= 0) close(sock);  // der Dienst wartet nach 'Q' auf das Schliessen (service_main.cpp): nicht erst mit dem Prozessende
    trace("Beenden");
    leave();
}
