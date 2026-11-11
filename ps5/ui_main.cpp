// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 Medic -- Vollbild-UI (Phase 3).
// Laeuft als Payload ueber elfldr/Payload Manager (kein Sony-Title, keine Sandbox, kein Jailbreak-Request --
// Begruendung: docs/PHASE3.md, Abschnitt "Korrektur"). Zeichnet medic::ui::App per SDL2 (ps5-payload-dev/SDL,
// siehe docs/PHASE3.md fuer den Build) in den Bildschirm und liest den DualSense als SDL_GameController.
// Reparaturen laufen ueber LocalService -> medic::apply_plan, also mit Backup/Journal/Undo wie am PC.
#include "ps5_platform.hpp"

#include "medic/error_codes.hpp"
#include "medic/rules.hpp"
#include "medic_ui/app.hpp"
#include "medic_ui/pad_nav.hpp"
#include "medic_ui/service.hpp"
#include "medic_ui/shot_hook.hpp"

#include <SDL2/SDL.h>

#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <ps5/klog.h>

using namespace medic::ui;

namespace {

// Erste vorhandene Datei gewinnt (per `make deploy-ui` abgelegt, siehe Makefile).
const char* const kRulesCandidates[] = {"/data/ps5medic/knowledge/rules.json", "/data/homebrew/PS5MEDIC/rules.json"};
const char* const kErrorCodesCandidates[] = {"/data/ps5medic/knowledge/error_codes.json", "/data/homebrew/PS5MEDIC/error_codes.json"};
const char* const kLogPath = "/data/ps5medic/ui.log";

std::FILE* g_log = nullptr;

// stdout (geht an den Sender, falls vorhanden), klog und Logdatei -- die Datei ist der verlaessliche Weg,
// weil klog auf der Test-Konsole nichts liefert und stdout nur bei offener Sende-Verbindung ankommt.
void trace(const std::string& line) {
    std::puts(line.c_str());
    klog_puts((line + "\n").c_str());
    if (g_log) {
        std::fputs((line + "\n").c_str(), g_log);
        std::fflush(g_log);
    }
}

void open_log() {
    std::error_code ec;
    std::filesystem::create_directories("/data/ps5medic", ec);
    g_log = std::fopen(kLogPath, "w");
}

extern "C" {
int sceKernelGetAppInfo(pid_t pid, void* info);
int sceSystemServiceKillApp(int app_id, int how, int reason, int core_dump);
}

// Gestartet ueber websrvs Big-App-Lader ist dieser Prozess fuer das System eine laufende App (app_id != 0). Endet nur der
// Prozess, bleibt die App im Vordergrund haengen, und man kommt nur mit PS-Taste und "Schliessen" heraus. Deshalb schliesst
// sich die UI beim Beenden ueber das System selbst, so wie websrv eine laufende Big App schliesst. Gefragt wird nach der
// eigenen PID: als reiner Payload (app_id 0) gibt es nichts zu schliessen, und eine fremde App, etwa ein laufendes Spiel,
// kann so nie getroffen werden.
void close_own_app() {
    alignas(8) unsigned char info[256] = {0};
    if (sceKernelGetAppInfo(getpid(), info) != 0) {
        trace("Beenden: App-Info nicht lesbar, nur Prozessende");
        return;
    }
    std::uint32_t app_id = 0;
    std::memcpy(&app_id, info, sizeof app_id);
    if (app_id == 0) {
        trace("Beenden: kein App-Kontext (Payload), nur Prozessende");
        return;
    }
    trace("Beenden: schliesse die eigene App (app_id " + std::to_string(app_id) + ")");
    const int rc = sceSystemServiceKillApp(static_cast<int>(app_id), -1, 0, 0);
    trace("Beenden: KillApp rc=" + std::to_string(rc));
}

// Jede UI-Instanz traegt ihre PID in die Besitzerdatei ein und beendet sich, sobald dort eine fremde PID steht:
// eine neu gestartete (z. B. per Port 9021 gesendete) Version loest die laufende ab. Prozessnamen taugen dafuer
// nicht -- elfldr nennt jeden Payload "payload.elf".
const char* const kOwnerPath = "/data/ps5medic/ui.owner";

void claim_ownership() {
    if (std::FILE* f = std::fopen(kOwnerPath, "w")) {
        std::fprintf(f, "%d\n", static_cast<int>(getpid()));
        std::fclose(f);
    }
}

bool owned_by_other() {
    std::FILE* f = std::fopen(kOwnerPath, "r");
    if (!f) return false;
    int pid = 0;
    const bool read = std::fscanf(f, "%d", &pid) == 1;
    std::fclose(f);
    return read && pid != static_cast<int>(getpid());
}

// Die Vorgaenger-Instanz braucht einen Moment, bis sie Bildschirm und Speicher freigibt.
bool init_sdl_with_retry() {
    for (int attempt = 1; attempt <= 12; ++attempt) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0) return true;
        trace("SDL_Init (Versuch " + std::to_string(attempt) + "): " + SDL_GetError());
        SDL_Quit();
        SDL_Delay(500);
    }
    return false;
}

// SDL meldet den DualSense als generischen Controller mit Standard-Layout (siehe SDL_ps5joystick.c:
// a=Kreuz, b=Kreis, x=Viereck, y=Dreieck, start=Options, Schultertasten/Steuerkreuz wie erwartet).
std::optional<Button> map_button(Uint8 sdl_button) {
    switch (sdl_button) {
        case SDL_CONTROLLER_BUTTON_A: return Button::Cross;
        case SDL_CONTROLLER_BUTTON_B: return Button::Circle;
        case SDL_CONTROLLER_BUTTON_X: return Button::Square;
        case SDL_CONTROLLER_BUTTON_Y: return Button::Triangle;
        case SDL_CONTROLLER_BUTTON_START: return Button::Options;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return Button::L1;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return Button::R1;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: return Button::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return Button::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return Button::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Button::Right;
        default: return std::nullopt;
    }
}

medic::RuleSet load_rules() {
    for (const char* path : kRulesCandidates) {
        if (!std::filesystem::exists(path)) continue;
        try {
            auto rules = medic::load_rules_from_file(path);
            trace("Regeln: " + rules.db_version + " (" + std::to_string(rules.rules.size()) + ") aus " + path);
            return rules;
        } catch (const std::exception& e) {
            trace(std::string("Regeldatei ungueltig (") + path + "): " + e.what());
        }
    }
    trace("Keine Regeldatei gefunden -- Check-up findet keine Befunde.");
    return {};
}

// Fehlercode-Nachschlagewerk (nur Text zum Lesen). Fehlt die Datei, zeigt der Bildschirm einen Hinweis; der Rest laeuft normal.
medic::ErrorCodeSet load_error_codes() {
    for (const char* path : kErrorCodesCandidates) {
        if (!std::filesystem::exists(path)) continue;
        try {
            auto codes = medic::load_error_codes_from_file(path);
            trace("Fehlercodes: " + codes.db_version + " (" + std::to_string(codes.codes.size()) + ") aus " + path);
            return codes;
        } catch (const std::exception& e) {
            trace(std::string("Fehlercode-Datei ungueltig (") + path + "): " + e.what());
        }
    }
    trace("Keine Fehlercode-Datei gefunden -- der Bildschirm Fehlercodes bleibt leer.");
    return {};
}

extern "C" {
int sceKernelAllocateMainDirectMemory(size_t, size_t, int, intptr_t*);
int sceKernelReleaseDirectMemory(intptr_t, size_t);
}

// Nur im Fehlerfall von SDL_Init: SDLs VideoOut-Treiber fordert den Bildspeicher als Direktspeicher (Typ 3) an,
// und die Test-Konsole gab 64 MB am Stueck nicht her (EAGAIN, 32 MB gingen). Zeigt, wie viel hier moeglich ist.
void log_direct_memory_limits() {
    for (size_t mb : {64, 32, 24, 16}) {
        intptr_t phys = 0;
        errno = 0;
        const int rc = sceKernelAllocateMainDirectMemory(mb << 20, 0x20000, 3, &phys);
        trace("  Direktspeicher " + std::to_string(mb) + " MB: " + (rc == 0 ? std::string("ok") : std::strerror(errno)));
        if (rc == 0) sceKernelReleaseDirectMemory(phys, mb << 20);
    }
}

const char* screen_name(App::Screen s) {
    switch (s) {
        case App::Screen::Start: return "Start";
        case App::Screen::Running: return "Pruefung laeuft";
        case App::Screen::Findings: return "Befunde";
        case App::Screen::Detail: return "Detail";
        case App::Screen::FixPreview: return "Reparatur-Vorschau";
        case App::Screen::FixDone: return "Ergebnis";
        case App::Screen::History: return "Verlauf";
        case App::Screen::UndoConfirm: return "Rueckgaengig-Frage";
        case App::Screen::Settings: return "Einstellungen";
        case App::Screen::Report: return "Bericht";
        case App::Screen::RulesUpdate: return "Regel-Update";
        case App::Screen::ErrorCodes: return "Fehlercodes";
        case App::Screen::ErrorCodeDetail: return "Fehlercode-Detail";
        case App::Screen::ErrorCodeSource: return "Fehlercode-Quelle";
    }
    return "?";
}

// Beendet den Prozess nach dem Loggen: ein haengender Payload wuerde Direktspeicher und Bildschirm weiter belegen.
[[noreturn]] void fail(const std::string& reason) {
    trace("PS5 Medic UI: FATAL -- " + reason);
    SDL_Quit();
    std::exit(1);
}

}  // namespace

int main() {
    open_log();
    claim_ownership();
    trace("PS5 Medic UI startet");

    auto platform = medic::make_ps5_platform(/*writable=*/true);
    medic::RuleSet rules = load_rules();
    medic::ui::LocalService service(*platform, std::move(rules));
    service.set_error_codes(load_error_codes());
    // Der Regel-Download ist die einzige Netzfunktion: Abruf nur nach Bestaetigung im Bildschirm Regel-Update, nur zur Adresse aus config.ini.
    const auto http = medic::make_posix_http_client();
    service.set_http_client(http.get());
    App app(service);

    if (!init_sdl_with_retry()) {
        log_direct_memory_limits();
        fail("Bildschirm nicht verfuegbar");
    }
    trace("SDL_Init ok");

    SDL_Window* window = SDL_CreateWindow("PS5 Medic", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, App::kWidth,
                                           App::kHeight, SDL_WINDOW_FULLSCREEN);
    if (!window) fail(std::string("SDL_CreateWindow: ") + SDL_GetError());
    trace("Fenster erstellt");

    // Das Oeffnen kann anfangs scheitern (Pad noch von einem Vorgaenger belegt, Controller noch nicht da):
    // deshalb jede Sekunde erneut versuchen, bis es klappt, und jede neue Fehlermeldung einmal loggen.
    SDL_GameController* pad = nullptr;
    Uint32 next_pad_try = 0;
    int last_joystick_count = -1;
    std::string last_pad_error;

    Canvas canvas(App::kWidth, App::kHeight);
    bool running = true;
    unsigned frame = 0, shot_calls = 0;
    int logged_buttons = 0;  // nur die ersten Tasten loggen, damit die Datei nicht waechst
    int logged_axes = 0;     // ebenso die ersten Stick-Ausschlaege (zeigt, ob der Treiber die Achsen meldet)
    int logged_screens = 0;
    PadNav nav;              // Steuerkreuz und linker Stick -> Richtungstasten mit Wiederholung
    int stick_x = 0, stick_y = 0;
    std::vector<Button> pressed;  // Tasten dieses Durchlaufs, in Reihenfolge
    App::Screen last_screen = app.screen();
    bool first_frame_logged = false;
    bool present_error_logged = false;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                    next_pad_try = 0;
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (pad && ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                        SDL_GameControllerClose(pad);
                        pad = nullptr;
                        nav.reset();
                        stick_x = stick_y = 0;
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP: {
                    const bool down = ev.type == SDL_CONTROLLERBUTTONDOWN;
                    const auto b = map_button(ev.cbutton.button);
                    if (down && logged_buttons < 20) {
                        trace("Taste " + std::to_string(ev.cbutton.button) + (b ? "" : " (unbelegt)"));
                        ++logged_buttons;
                    }
                    if (!b) break;
                    if (*b == Button::Up || *b == Button::Down || *b == Button::Left || *b == Button::Right) nav.dpad(*b, down, SDL_GetTicks(), pressed);
                    else if (down) pressed.push_back(*b);
                    break;
                }
                case SDL_CONTROLLERAXISMOTION:
                    // Nur der linke Stick steuert (wie das Steuerkreuz); Sticks und Trigger der rechten Seite bleiben ohne Wirkung.
                    if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                        if (logged_axes < 10 && std::abs(ev.caxis.value) > PadNav::kOn) {
                            trace("Stick-Achse " + std::to_string(ev.caxis.axis) + ": " + std::to_string(ev.caxis.value));
                            ++logged_axes;
                        }
                        (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX ? stick_x : stick_y) = ev.caxis.value;
                        nav.stick(stick_x, stick_y, SDL_GetTicks(), pressed);
                    }
                    break;
                default: break;
            }
        }
        nav.poll(SDL_GetTicks(), pressed);  // gehaltenes Hoch/Runter wiederholen
        for (const Button b : pressed) app.handle(b);
        pressed.clear();

        if (!pad && SDL_GetTicks() >= next_pad_try) {
            next_pad_try = SDL_GetTicks() + 1000;
            const int joysticks = SDL_NumJoysticks();
            if (joysticks != last_joystick_count) {
                trace("Joysticks: " + std::to_string(joysticks));
                last_joystick_count = joysticks;
            }
            for (int i = 0; i < joysticks && !pad; ++i) {
                if (!SDL_IsGameController(i)) continue;
                pad = SDL_GameControllerOpen(i);
                if (pad) {
                    const char* name = SDL_GameControllerName(pad);
                    trace(std::string("Controller verbunden: ") + (name ? name : "?"));
                } else if (const std::string err = SDL_GetError(); err != last_pad_error) {
                    trace("Controller nicht oeffnbar: " + err);
                    last_pad_error = err;
                }
            }
        }

        if (app.quit_requested()) break;
        if (++frame % 15 == 0 && owned_by_other()) {
            trace("Neuere Instanz uebernimmt -- beende mich.");
            break;
        }

        app.update();
        if (app.screen() != last_screen && logged_screens < 200) {
            last_screen = app.screen();
            ++logged_screens;
            trace(std::string("Bildschirm: ") + screen_name(last_screen) + ", Befunde: " + std::to_string(app.finding_count()));
        }
        app.render(canvas);
        if (++shot_calls % 4 == 0) {  // Entwicklung: Bild des eigenen Canvas auf Anforderung (shot_hook.hpp); sonst nur ein fehlschlagendes unlink
            std::string shot_path;
            if (medic::ui::serve_shot_request(canvas, medic::ui::kShotRequestPath, medic::ui::kShotFolder, &shot_path)) trace("Screenshot-Anforderung bedient: " + shot_path);
        }

        SDL_Surface* frame = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<void*>(static_cast<const void*>(canvas.data())),
                                                                 canvas.width(), canvas.height(), 32, canvas.width() * 4,
                                                                 SDL_PIXELFORMAT_ARGB8888);
        SDL_Surface* target = SDL_GetWindowSurface(window);
        if (frame && target) {
            const int blit = SDL_BlitSurface(frame, nullptr, target, nullptr);
            const int flip = SDL_UpdateWindowSurface(window);
            if (!first_frame_logged) {
                trace("Erstes Bild: Blit=" + std::to_string(blit) + " Flip=" + std::to_string(flip) + " Ziel " +
                      std::to_string(target->w) + "x" + std::to_string(target->h));
                first_frame_logged = true;
            }
            if ((blit != 0 || flip != 0) && !present_error_logged) {
                trace(std::string("Bildausgabe fehlgeschlagen: ") + SDL_GetError());
                present_error_logged = true;
            }
        } else if (!present_error_logged) {
            trace(std::string("Kein Bildpuffer: ") + SDL_GetError());
            present_error_logged = true;
        }
        if (frame) SDL_FreeSurface(frame);

        SDL_Delay(33);  // ~30 Bilder/s: reicht fuer eine Menu-UI, schont die CPU
    }

    trace("Beenden: raeume SDL auf");
    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyWindow(window);
    SDL_Quit();
    close_own_app();
    trace("Beenden: fertig");
    return 0;
}
