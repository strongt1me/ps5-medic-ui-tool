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

#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

#include <ps5/klog.h>

using namespace medic::ui;

namespace {

// Erste vorhandene Datei gewinnt (per `make deploy-ui` abgelegt, siehe Makefile).
const char* const kRulesCandidates[] = {"/data/ps5medic/knowledge/rules.json", "/data/homebrew/PS5MEDIC/rules.json"};

void trace(const std::string& line) {
    std::puts(line.c_str());
    klog_puts((line + "\n").c_str());
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

// Blockiert die Konsole sichtbar still, statt dass der Payload kommentarlos verschwindet.
[[noreturn]] void halt(const std::string& reason) {
    trace("PS5 Medic UI: FATAL -- " + reason);
    for (;;) SDL_Delay(1000);
}

}  // namespace

int main() {
    trace("PS5 Medic UI startet");

    auto platform = medic::make_ps5_platform(/*writable=*/true);
    medic::RuleSet rules = load_rules();
    medic::ui::LocalService service(*platform, std::move(rules));
    App app(service);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) halt(std::string("SDL_Init: ") + SDL_GetError());
    trace("SDL_Init ok");

    SDL_Window* window = SDL_CreateWindow("PS5 Medic", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, App::kWidth,
                                           App::kHeight, SDL_WINDOW_FULLSCREEN);
    if (!window) halt(std::string("SDL_CreateWindow: ") + SDL_GetError());
    trace("Fenster erstellt");

    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            break;
        }
    trace(pad ? "Controller verbunden" : "Kein Controller gefunden (wird bei Verbindung automatisch geoeffnet)");

    Canvas canvas(App::kWidth, App::kHeight);
    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (!pad) pad = SDL_GameControllerOpen(ev.cdevice.which);
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (pad && ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                        SDL_GameControllerClose(pad);
                        pad = nullptr;
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    if (auto b = map_button(ev.cbutton.button)) app.handle(*b);
                    break;
                default: break;
            }
        }

        app.update();
        app.render(canvas);

        SDL_Surface* frame = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<void*>(static_cast<const void*>(canvas.data())),
                                                                 canvas.width(), canvas.height(), 32, canvas.width() * 4,
                                                                 SDL_PIXELFORMAT_ARGB8888);
        if (frame) {
            SDL_BlitSurface(frame, nullptr, SDL_GetWindowSurface(window), nullptr);
            SDL_FreeSurface(frame);
            SDL_UpdateWindowSurface(window);
        }

        SDL_Delay(33);  // ~30 Bilder/s: reicht fuer eine Menu-UI, schont die CPU
    }

    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
