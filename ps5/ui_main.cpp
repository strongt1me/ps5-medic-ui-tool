// PS5 Medic -- Vollbild-UI (Phase 3).
// Laeuft als Payload ueber elfldr/Payload Manager (kein Sony-Title, keine Sandbox, kein Jailbreak-Request --
// Begruendung: docs/PHASE3.md, Abschnitt "Korrektur"). Zeichnet medic::ui::App per SDL2 (ps5-payload-dev/SDL,
// siehe docs/PHASE3.md fuer den Build) in den Bildschirm und liest den DualSense als SDL_GameController.
// Reparaturen laufen ueber LocalService -> medic::apply_plan, also mit Backup/Journal/Undo wie am PC.
#include "ps5_platform.hpp"

#include "medic/rules.hpp"
#include "medic_ui/app.hpp"
#include "medic_ui/service.hpp"

#include <SDL2/SDL.h>

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

#include <ps5/klog.h>

using namespace medic::ui;

namespace {

// Erste vorhandene Datei gewinnt (per `make deploy-ui` abgelegt, siehe Makefile).
const char* const kRulesCandidates[] = {"/data/ps5medic/knowledge/rules.json", "/data/homebrew/PS5MEDIC/rules.json"};
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
    unsigned frame = 0;
    int logged_buttons = 0;  // nur die ersten Tasten loggen, damit die Datei nicht waechst
    int logged_screens = 0;
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
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    if (logged_buttons < 20) {
                        trace("Taste " + std::to_string(ev.cbutton.button) + (map_button(ev.cbutton.button) ? "" : " (unbelegt)"));
                        ++logged_buttons;
                    }
                    if (auto b = map_button(ev.cbutton.button)) app.handle(*b);
                    break;
                default: break;
            }
        }

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

    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
