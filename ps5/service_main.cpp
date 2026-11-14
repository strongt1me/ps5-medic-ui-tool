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
#include <exception>
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
std::vector<std::string> g_pending;  // Zeilen, die vor dem Binden des Ports anfielen (siehe main)

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
    } else if (g_pending.size() < 200) {
        g_pending.push_back(line);
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
    auto rules = medic::load_newest_peek<medic::RuleSet>({std::begin(kRulesCandidates), std::end(kRulesCandidates)},
                                                    [](const std::string& p) {
                                                        const std::uint32_t t0 = now_ms();  // Messung: das Laden samt Textpruefung dauert auf der Konsole spuerbar (Marc, 14. Nov. 2026)
                                                        auto r = medic::load_rules_from_file(p);
                                                        trace("  Regeldatei " + p + " geladen in " + std::to_string(now_ms() - t0) + " ms");
                                                        return r;
                                                    },
                                                    [](const std::string& p) { return medic::peek_db_version(p); }, &chosen, &errors);
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
    auto codes = medic::load_newest_peek<medic::ErrorCodeSet>({std::begin(kErrorCodesCandidates), std::end(kErrorCodesCandidates)},
                                                         [](const std::string& p) {
                                                             const std::uint32_t t0 = now_ms();
                                                             auto r = medic::load_error_codes_from_file(p);
                                                             trace("  Fehlercode-Datei " + p + " geladen in " + std::to_string(now_ms() - t0) + " ms");
                                                             return r;
                                                         },
                                                         [](const std::string& p) { return medic::peek_db_version(p); }, &chosen, &errors);
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
            // Offen lassen, bis die aeltere Instanz sie schliesst: sie laedt zuerst Daten, schickt ein Hallo (das bei geschlossener Gegenseite scheitert)
            // und liest erst dann das 'X'. Hoechstens drei Sekunden.
            pollfd pfd{old, POLLIN, 0};
            for (int waited = 0; waited < 30; ++waited) {
                if (poll(&pfd, 1, 100) > 0) {
                    char drop[64];
                    if (recv(old, drop, sizeof drop, MSG_DONTWAIT) <= 0) break;  // geschlossen oder Fehler
                }
            }
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
    bool discard_pad = false;  // nach einer langen Operation: die in der Zwischenzeit aufgelaufenen Pad-Zustaende verwerfen (nur als neue Basislinie lesen)
    std::uint32_t last_update = 0, last_emit = 0;
    // Bild zeichnen und die geaenderten Streifen an die App senden. Auch der Hook der App ruft es mitten im Check-up (Fortschritt).
    std::uint32_t first_update_ms = 0;  // Messung fuers Protokoll: erste update()-Zeit, erste render()-Zeit (Marc, 14. Nov. 2026: Start dauert vom "bereit" bis zum ersten Bild 1,8 s)
    auto emit_frame = [&]() -> bool {
        const std::uint32_t render_from = now_ms();
        app.render(canvas);
        const std::uint32_t render_ms = now_ms() - render_from;

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
            trace("Erstes Bild gesendet (update " + std::to_string(first_update_ms) + " ms, Zeichnen " + std::to_string(render_ms) + " ms, Senden " +
                  std::to_string(now_ms() - render_from - render_ms) + " ms)");
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
            if (n == 0) {
                trace("Sitzung endet: App hat die Verbindung geschlossen");
                return false;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            trace("Sitzung endet: Lesefehler (errno " + std::to_string(errno) + ")");
            return false;
        }
        const bool stale = discard_pad;  // alles, was bis hierher im Socket lag, stammt aus der Wartezeit der letzten langen Operation
        discard_pad = false;
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
            if (type == proto::kPad && len >= 8 && stale) {
                const std::uint32_t b = proto::get32(pl);
                // Nur Kreis bleibt wirksam (er bricht ab oder geht zurueck): ein Selbsttest-Schritt kann laenger als 250 ms dauern, und "Kreis: Abbrechen" muss dann trotzdem gehen
                if (have_pad && ((prev_buttons ^ b) & kBtnCircle) && (b & kBtnCircle)) pressed.push_back(Button::Circle);
                prev_buttons = b;
                have_pad = true;
            } else if (type == proto::kPad && len >= 8) {
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
        const bool had_input = !pressed.empty();
        const std::uint32_t busy_from = now_ms();
        for (const Button b : pressed) app.handle(b);
        pressed.clear();
        if (app.quit_requested()) {
            proto::send_message(client, proto::kQuit, nullptr, 0);
            // Nicht sofort schliessen: liegt noch ein Pad-Paket ungelesen im Puffer, wuerde close() ein RST senden statt FIN, und die App verwirft das 'Q'
            // (sie meldete dann "Verbindung verloren" und startete den Dienst neu). Schreibseite beenden und warten, bis die App ihre Seite schliesst.
            shutdown(client, SHUT_WR);
            for (int i = 0; i < 30; ++i) {  // hoechstens ~0,5 s
                pollfd in{client, POLLIN, 0};
                if (poll(&in, 1, 16) > 0) {
                    const ssize_t n = recv(client, buf, sizeof buf, MSG_DONTWAIT);
                    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) break;
                }
            }
            trace("Oberflaeche beendet");
            return true;
        }

        // update() zaehlt die Takte der Oberflaeche (Startbild, Punkte hinter "Pruefe", Schritte des Selbsttests): wie bisher rund 30 pro Sekunde,
        // auch wenn der Dienst jetzt oefter zeichnet, um Eingaben schneller zu zeigen
        if (t0 - last_update >= 33) {
            last_update = t0;
            app.update();
            if (!first_logged) first_update_ms = now_ms() - t0;
        }
        if (now_ms() - busy_from > 250) {  // Check-up, Download oder Test: die App hat inzwischen weiter Pad-Zustaende geschickt, die nicht mehr zum Bildschirm gehoeren
            discard_pad = true;
            nav.reset();
        }
        // Gezeichnet wird sofort nach einer Taste, sonst alle 16 ms (die App schickt den Pad-Zustand staendig, auch unveraendert: das allein loest kein Bild aus)
        if (!link_ok) {
            trace("Sitzung endet: Senden eines Bilds fehlgeschlagen (Check-up)");
            return false;
        }
        if (had_input || t0 - last_emit >= 16) {
            last_emit = t0;
            if (!emit_frame()) {
                trace("Sitzung endet: Senden eines Bilds fehlgeschlagen (errno " + std::to_string(errno) + ")");
                return false;
            }
        }

        // Warten, bis die App etwas sendet (weckt sofort), hoechstens bis zum naechsten Takt; vorher schlief der Dienst fest 33 ms: bis zu 33 ms Verzoegerung pro Tastendruck
        const std::uint32_t since = now_ms() - last_emit;
        if (since < 16) {
            pollfd in{client, POLLIN, 0};
            poll(&in, 1, static_cast<int>(16 - since));
        }
    }
}

}  // namespace

int main() {
    std::error_code ec;
    std::filesystem::create_directories("/data/ps5medic", ec);
    signal(SIGPIPE, SIG_IGN);
    trace("PS5 Medic Dienst startet");

    const int ls = listen_local();
    if (ls < 0) {
        // Eine zweite Instanz, die den Port nicht bekommt, darf das Protokoll der laufenden nicht wegnehmen: nur anhaengen
        if (std::FILE* f = std::fopen(kLogPath, "a")) {
            std::fputs("--- weitere Instanz konnte den Port nicht binden ---\n", f);
            for (const auto& l : g_pending) std::fputs((l + "\n").c_str(), f);
            std::fclose(f);
        }
        return 1;
    }
    std::rename(kLogPath, "/data/ps5medic/service.log.1");  // das Protokoll des vorigen Laufs bleibt eine Runde erhalten
    g_log = std::fopen(kLogPath, "a");  // "a": eine zweite Instanz, die danach noch anhaengt, vermischt sich nicht mit den Zeilen dieser
    if (g_log) {
        for (const auto& l : g_pending) std::fputs((l + "\n").c_str(), g_log);
        std::fflush(g_log);
    }
    g_pending.clear();
    trace("Dienst wartet auf 127.0.0.1:" + std::to_string(proto::kPort));

    auto platform = medic::make_ps5_platform(/*writable=*/true);
    const auto http = std::shared_ptr<medic::IHttpClient>(medic::make_posix_http_client().release());

    std::uint32_t last_client = now_ms();
    for (;;) {
        pollfd pfd{ls, POLLIN, 0};
        const int r = poll(&pfd, 1, 1000);
        if (r > 0 && (pfd.revents & POLLIN)) {
            const int client = accept(ls, nullptr, nullptr);
            if (client < 0) {
                usleep(100000);  // dauerhafter Fehler (EMFILE): nicht mit voller Rechenzeit weiterdrehen
                continue;
            }
            trace("App verbunden");
            proto::set_nodelay(client);
            proto::set_stream_buffers(client);
            proto::set_send_timeout(client, 120);  // liest die App nicht mehr, bleibt der Dienst nicht fuer immer in send() haengen (120 s, Marc 13. Nov. 2026: eine App im Hintergrund nach der PS-Taste soll bei einem laengeren Test nicht ihre Sitzung verlieren)
            bool finished = false;
            try {
                finished = serve(client, *platform, http);
            } catch (const std::exception& e) {  // z. B. eine unerwartete Ausnahme in einer Pruefung: die App verbindet sich neu, der Dienst lebt weiter
                trace(std::string("Sitzung endet: Ausnahme: ") + e.what());
            } catch (...) {
                trace("Sitzung endet: unbekannte Ausnahme");
            }
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
