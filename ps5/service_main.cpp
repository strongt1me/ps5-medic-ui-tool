// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 Medic -- Dienst fuer die native UI-App (docs/NATIVE.md). Laeuft als Payload ueber elfldr (volle Rechte), zeichnet die Oberflaeche
// (medic::ui::App mit LocalService, also derselbe Code wie die SDL-UI) und schickt die geaenderten Bildstreifen an die App, die sie nur anzeigt;
// zurueck kommt der Pad-Zustand. Reparaturen laufen ueber LocalService -> Backup/Journal/Undo wie immer.
//
// Lebensdauer: Der Dienst wartet auf die App (Port 9060), bedient eine Verbindung nach der anderen und endet, wenn die Oberflaeche beendet wird
// (Kreis auf dem Startbildschirm), die App 120 s nach dem Verbindungsabbruch nicht wiederkommt oder eine neuere Instanz ihn per 'X' bittet.
#include "ps5_platform.hpp"
#include "service_protocol.hpp"

#include "medic/error_codes.hpp"
#include "medic/http.hpp"
#include "medic/newest.hpp"
#include "medic/rules.hpp"
#include "medic_ui/app.hpp"
#include "medic_ui/pad_nav.hpp"
#include "medic_ui/service.hpp"
#include "medic_ui/splash.hpp"

#include <poll.h>
#include <signal.h>
#include <time.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <ps5/klog.h>

extern "C" int sceSystemServiceParamGetInt(int param_id, int* value);

using namespace medic::ui;
namespace proto = medic::proto;

namespace {

const char* const kRulesCandidates[] = {"/data/ps5medic/knowledge/rules.json", "/data/homebrew/PS5MEDIC/rules.json"};
const char* const kErrorCodesCandidates[] = {"/data/ps5medic/knowledge/error_codes.json", "/data/homebrew/PS5MEDIC/error_codes.json"};
const char* const kLogPath = "/data/ps5medic/service.log";

std::FILE* g_log = nullptr;

std::uint32_t now_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

const std::uint32_t g_t0 = now_ms();

// Jede Zeile mit Millisekunden seit dem Start des Dienstes (Diagnose der Startdauer)
void trace(const std::string& raw) {
    const std::string line = "[" + std::to_string(now_ms() - g_t0) + " ms] " + raw;
    std::puts(line.c_str());
    klog_puts((line + "\n").c_str());
    if (g_log) {
        std::fputs((line + "\n").c_str(), g_log);
        std::fflush(g_log);
    }
}

// Sprache der Konsole (Wert 1 von sceSystemServiceParamGetInt = SCE_SYSTEM_SERVICE_PARAM_ID_LANG). Gibt es sie in der App nicht, oder schlaegt die
// Abfrage fehl, ist es Englisch.
Lang console_language() {
    static bool known = false;  // die Systemsprache aendert sich nicht mitten in einer Dienstsitzung: einmal lesen, einmal ablegen
    static Lang cached = Lang::En;
    if (known) return cached;
    int id = -1;
    const int rc = sceSystemServiceParamGetInt(1, &id);
    cached = lang_from_system_id(rc == 0 ? id : -1);
    known = true;
    trace("Systemsprache: rc=" + std::to_string(rc) + " Wert=" + std::to_string(id) + " -> " + lang_code(cached));
    // Die native App kennt die Systemsprache selbst nicht (kein Import dafuer); sie liest diese Datei fuer ihre Meldungen vor dem Dienst.
    // Geschrieben wird nur, wenn sich der Inhalt aendert.
    char old[16] = {0};
    if (std::FILE* f = std::fopen("/data/ps5medic/system_language", "r")) {
        if (!std::fgets(old, sizeof old, f)) old[0] = 0;
        std::fclose(f);
    }
    if (std::strcmp(old, lang_code(cached)) != 0) {
        if (std::FILE* f = std::fopen("/data/ps5medic/system_language", "w")) {
            std::fputs(lang_code(cached), f);
            std::fclose(f);
        }
    }
    return cached;
}

// Beide Orte werden gelesen; es gilt die Datei mit der neueren db_version (libmedic/newest.hpp, mit PC-Test).
medic::RuleSet load_rules() {
    std::string chosen;
    std::vector<std::string> errors;
    auto rules = medic::load_newest<medic::RuleSet>({std::begin(kRulesCandidates), std::end(kRulesCandidates)},
                                                    [](const std::string& p) { return medic::load_rules_from_file(p); }, &chosen, &errors);
    for (const auto& e : errors) trace("Regeldatei ungueltig (" + e + ")");
    if (chosen.empty()) {
        trace("Keine Regeldatei gefunden -- Check-up meldet 'unvollstaendig'.");
        return {};
    }
    trace("Regeln: " + rules.db_version + " (" + std::to_string(rules.rules.size()) + ") aus " + chosen);
    return rules;
}

medic::ErrorCodeSet load_error_codes() {
    std::string chosen;
    std::vector<std::string> errors;
    auto codes = medic::load_newest<medic::ErrorCodeSet>({std::begin(kErrorCodesCandidates), std::end(kErrorCodesCandidates)},
                                                         [](const std::string& p) { return medic::load_error_codes_from_file(p); }, &chosen, &errors);
    for (const auto& e : errors) trace("Fehlercode-Datei ungueltig (" + e + ")");
    if (chosen.empty()) {
        trace("Keine Fehlercode-Datei gefunden.");
        return {};
    }
    trace("Fehlercodes: " + codes.db_version + " (" + std::to_string(codes.codes.size()) + ") aus " + chosen);
    return codes;
}

// Eine Taste des Pads (Bits wie ScePad, siehe SDL-Treiber der PS5)
constexpr std::uint32_t kBtnOptions = 0x0008, kBtnUp = 0x0010, kBtnRight = 0x0020, kBtnDown = 0x0040, kBtnLeft = 0x0080, kBtnL1 = 0x0400,
                        kBtnR1 = 0x0800, kBtnTriangle = 0x1000, kBtnCircle = 0x2000, kBtnCross = 0x4000, kBtnSquare = 0x8000;

// Bindet 127.0.0.1:Port. Ist er belegt (eine aeltere Instanz), wird diese per 'X' beendet und es noch zweimal versucht.
int listen_local() {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return -1;
        int one = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in sa;
        std::memset(&sa, 0, sizeof sa);
        sa.sin_len = sizeof sa;
        sa.sin_family = AF_INET;
        sa.sin_port = htons(proto::kPort);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof sa) == 0 && listen(s, 1) == 0) return s;
        const int err = errno;
        close(s);
        if (err != EADDRINUSE || attempt == 3) {
            trace(std::string("bind/listen fehlgeschlagen: ") + std::strerror(err));
            return -1;
        }
        trace("Port belegt: bitte die aeltere Instanz zu enden");
        if (const int old = proto::connect_local(proto::kPort, 2); old >= 0) {
            proto::send_message(old, proto::kShutdown, nullptr, 0);
            close(old);
        }
        sleep(1);
    }
    return -1;
}

// Eine Verbindung bedienen. Rueckgabe: true = Oberflaeche beendet (Dienst endet), false = Verbindung weg (auf die App warten)
bool serve(int client, medic::Platform& platform, const std::shared_ptr<medic::IHttpClient>& http) {
    auto progress = [&](int percent) {
        const std::uint8_t p = static_cast<std::uint8_t>(percent);
        proto::send_message(client, proto::kProgress, &p, 1);  // Fehler hier faellt beim ersten Bild auf
    };
    progress(5);
    medic::RuleSet rules = load_rules();
    progress(40);
    LocalService service(platform, std::move(rules));
    service.set_error_codes(load_error_codes());
    progress(70);
    service.set_http_client(http.get());
    App app(service, console_language());  // die in config.ini gespeicherte Sprache hat im App-Konstruktor Vorrang
    {
        // Kein eigenes Startbild: die App zeigt es waehrend des Ladens (mit dem Fortschrittsbalken), danach folgt gleich der Startbildschirm.
        Canvas background(App::kWidth, App::kHeight);
        if (load_background(background)) app.set_background(background);
    }
    progress(95);

    trace("Sitzung: Regeln und Fehlercodes geladen, Oberflaeche und Bilder bereit");
    std::uint8_t hello[4];
    proto::put16(hello, proto::kWidth);
    proto::put16(hello + 2, proto::kHeight);
    if (!proto::send_message(client, proto::kHello, hello, sizeof hello)) return false;

    Canvas canvas(proto::kWidth, proto::kHeight);
    std::vector<std::uint32_t> sent(static_cast<std::size_t>(proto::kWidth) * proto::kHeight, 0);
    bool first = true;
    PadNav nav;
    std::vector<Button> pressed;
    std::uint32_t prev_buttons = 0;
    bool have_pad = false;  // der erste Zustand ist nur die Basislinie: eine beim Verbinden schon gehaltene Taste ist kein Tastendruck
    std::vector<std::uint8_t> rx;  // noch nicht verarbeitete Bytes der App

    bool first_logged = false;
    // Bild zeichnen und die geaenderten Streifen an die App senden. Auch der Hook der App ruft es mitten im Check-up (Fortschritt).
    auto emit_frame = [&]() -> bool {
        app.render(canvas);

        // Geaenderte Streifen senden (Raster von kBandRows Zeilen; zusammenhaengende Streifen in einer Nachricht)
        const std::uint32_t* px = canvas.data();
        const std::size_t row_px = proto::kWidth;
        int run_start = -1;
        bool ok = true;
        auto flush_run = [&](int end_row) {
            if (run_start < 0) return;
            const int rows = end_row - run_start;
            const std::uint32_t bytes = static_cast<std::uint32_t>(4 + static_cast<std::size_t>(rows) * row_px * 4);
            std::uint8_t head[9];
            head[0] = static_cast<std::uint8_t>(proto::kBand);
            proto::put32(head + 1, bytes);
            proto::put16(head + 5, static_cast<std::uint16_t>(run_start));
            proto::put16(head + 7, static_cast<std::uint16_t>(rows));
            ok = ok && proto::send_all(client, head, sizeof head) && proto::send_all(client, px + static_cast<std::size_t>(run_start) * row_px, static_cast<std::size_t>(rows) * row_px * 4);
            std::memcpy(&sent[static_cast<std::size_t>(run_start) * row_px], px + static_cast<std::size_t>(run_start) * row_px, static_cast<std::size_t>(rows) * row_px * 4);
            run_start = -1;
        };
        bool any = false;
        for (int y = 0; y < proto::kHeight && ok; y += proto::kBandRows) {
            const int rows = std::min(proto::kBandRows, proto::kHeight - y);
            const bool changed = first || std::memcmp(px + static_cast<std::size_t>(y) * row_px, &sent[static_cast<std::size_t>(y) * row_px], static_cast<std::size_t>(rows) * row_px * 4) != 0;
            if (changed) {
                if (run_start < 0) run_start = y;
                any = true;
            } else {
                flush_run(y);
            }
        }
        flush_run(proto::kHeight);
        if (!ok) return false;
        if (any || first) {
            if (!proto::send_message(client, proto::kPresent, nullptr, 0)) return false;
        }
        if (!first_logged) {
            first_logged = true;
            trace("Erstes Bild gesendet");
        }
        first = false;
        return true;
    };
    bool link_ok = true;
    app.set_redraw_hook([&] {
        if (link_ok && !emit_frame()) link_ok = false;  // der Hauptlauf bemerkt es nach dem Check-up
    });

    for (;;) {
        const std::uint32_t t0 = now_ms();

        // Eingaben der App lesen (nicht blockierend)
        std::uint8_t buf[512];
        for (;;) {
            const ssize_t n = recv(client, buf, sizeof buf, MSG_DONTWAIT);
            if (n > 0) {
                rx.insert(rx.end(), buf, buf + n);
                continue;
            }
            if (n == 0) return false;  // App hat geschlossen
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            return false;
        }
        while (rx.size() >= 5) {
            const std::uint32_t len = proto::get32(rx.data() + 1);
            if (len > 4096) return false;  // kein gueltiges Protokoll
            if (rx.size() < 5 + len) break;
            const char type = static_cast<char>(rx[0]);
            const std::uint8_t* pl = rx.data() + 5;
            if (type == proto::kShutdown) {
                trace("Beenden angefordert (neuere Instanz)");
                return true;
            }
            if (type == proto::kPad && len >= 8) {
                const std::uint32_t b = proto::get32(pl);
                if (!have_pad) {
                    prev_buttons = b;
                    have_pad = true;
                }
                const std::uint32_t changed = prev_buttons ^ b;
                auto edge = [&](std::uint32_t bit, Button btn) {
                    if ((changed & bit) && (b & bit)) pressed.push_back(btn);
                };
                edge(kBtnCross, Button::Cross);
                edge(kBtnCircle, Button::Circle);
                edge(kBtnSquare, Button::Square);
                edge(kBtnTriangle, Button::Triangle);
                edge(kBtnL1, Button::L1);
                edge(kBtnR1, Button::R1);
                edge(kBtnOptions, Button::Options);
                auto dir = [&](std::uint32_t bit, Button btn) {
                    if (changed & bit) nav.dpad(btn, (b & bit) != 0, t0, pressed);
                };
                dir(kBtnUp, Button::Up);
                dir(kBtnDown, Button::Down);
                dir(kBtnLeft, Button::Left);
                dir(kBtnRight, Button::Right);
                nav.stick((static_cast<int>(pl[4]) - 128) * 256, (static_cast<int>(pl[5]) - 128) * 256, t0, pressed);
                prev_buttons = b;
            }
            rx.erase(rx.begin(), rx.begin() + 5 + len);
        }
        nav.poll(t0, pressed);
        for (const Button b : pressed) app.handle(b);
        pressed.clear();
        if (app.quit_requested()) {
            proto::send_message(client, proto::kQuit, nullptr, 0);
            trace("Oberflaeche beendet");
            return true;
        }

        app.update();
        if (!link_ok || !emit_frame()) return false;

        const std::uint32_t used = now_ms() - t0;
        if (used < 33) usleep((33 - used) * 1000);  // ~30 Bilder/s
    }
}

}  // namespace

int main() {
    std::error_code ec;
    std::filesystem::create_directories("/data/ps5medic", ec);
    g_log = std::fopen(kLogPath, "w");
    signal(SIGPIPE, SIG_IGN);
    trace("PS5 Medic Dienst startet");

    const int ls = listen_local();
    if (ls < 0) return 1;
    trace("Dienst wartet auf 127.0.0.1:" + std::to_string(proto::kPort));

    auto platform = medic::make_ps5_platform(/*writable=*/true);
    const auto http = std::shared_ptr<medic::IHttpClient>(medic::make_posix_http_client().release());

    std::uint32_t last_client = now_ms();
    for (;;) {
        pollfd pfd{ls, POLLIN, 0};
        const int r = poll(&pfd, 1, 1000);
        if (r > 0 && (pfd.revents & POLLIN)) {
            const int client = accept(ls, nullptr, nullptr);
            if (client < 0) continue;
            trace("App verbunden");
            const bool finished = serve(client, *platform, http);
            close(client);
            last_client = now_ms();
            if (finished) break;
            trace("Verbindung beendet, warte auf die App");
        } else if (now_ms() - last_client > 120000) {
            trace("Keine App seit 120 s, der Dienst endet");
            break;
        }
    }
    close(ls);
    trace("Dienst beendet");
    if (g_log) std::fclose(g_log);
    return 0;
}
