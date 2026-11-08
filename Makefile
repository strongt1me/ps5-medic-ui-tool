# PS5 Medic
#   make host     medic-cli und Tests fuer Linux/WSL
#   make test     Unit- und Regel-Tests gegen alle Fixtures
#   make ps5      Diagnose-Payload (ELF, nur lesend) mit dem ps5-payload-sdk bauen
#   make ps5-ui   Vollbild-UI-Payload bauen (braucht zusaetzlich ps5-payload-sdl2, siehe docs/PHASE3.md)
#   make deploy    ps5medic.elf + Regeln per FTP (Port 2121) auf die Konsole legen   (PS5_HOST=<ip> noetig)
#   make deploy-ui ps5medicui.elf + Regeln per FTP auf die Konsole legen             (PS5_HOST=<ip> noetig)
#
# SDK: PS5_PAYLOAD_SDK zeigt auf den Ordner mit bin/, target/, toolchain/ (Standard: ~/ps5-sdk/PS5_PAYLOAD_SDK).
# SDL2 fuer ps5-ui liegt im selben SDK-Sysroot unter target/user/homebrew/ (dort von `make deps` o.ae. installiert,
# siehe docs/PHASE3.md): nicht im Repo, kein Netzzugriff beim normalen Bauen noetig.
BUILD ?= build
PS5_BUILD ?= build-ps5
PS5_PAYLOAD_SDK ?= $(HOME)/ps5-sdk/PS5_PAYLOAD_SDK
PS5_HOMEBREW := $(PS5_PAYLOAD_SDK)/target/user/homebrew
PS5_HOST ?=
PS5_FTP_PORT ?= 2121

PS5_CXX := $(PS5_PAYLOAD_SDK)/bin/prospero-clang++
# -Iui/include -Iui/src und der SDL2-Include-Pfad gelten fuer alle Objekte -- schadet dem Diagnose-Payload nicht,
# der sie nicht benutzt, und erspart eine zweite CXXFLAGS-Variable je Ziel.
PS5_CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Ilibmedic/include -Ithird_party -Ips5 -Iui/include -Iui/src -I$(PS5_HOMEBREW)/include

PS5_CORE_SRCS := $(wildcard libmedic/src/*.cpp) ps5/ps5_platform.cpp
# png.cpp bleibt aussen vor: nur fuer die PC-Vorschau (medic-ui-preview) gebraucht, nicht auf der Konsole.
PS5_UI_SRCS := ui/src/canvas.cpp ui/src/font.cpp ui/src/font_data.cpp ui/src/i18n.cpp ui/src/i18n_embedded.cpp \
               ui/src/app.cpp ui/src/local_service.cpp

PS5_OBJS := $(patsubst %.cpp,$(PS5_BUILD)/%.o,$(PS5_CORE_SRCS) ps5/main.cpp)
PS5_UI_OBJS := $(patsubst %.cpp,$(PS5_BUILD)/%.o,$(PS5_CORE_SRCS) $(PS5_UI_SRCS) ps5/ui_main.cpp)
PS5_ELF := $(PS5_BUILD)/ps5medic.elf
PS5_UI_ELF := $(PS5_BUILD)/ps5medicui.elf
PS5_UI_LIBS := -lSDL2 -lSceSystemService -lSceUserService -lScePad -lSceAudioOut -lsamplerate -lSceVideoOut \
               -lSceKeyboard -lSceImeDialog

.PHONY: host test clean ps5 ps5-ui deploy deploy-ui preview

# UI-Vorschau am PC: make preview SNAP=<snapshot-ordner> [OUT=build/preview] [PREVIEW_ARGS="--lang en --scale 2"]
SNAP ?=
OUT ?= $(BUILD)/preview
PREVIEW_ARGS ?=
preview: host
	@test -n "$(SNAP)" || { echo "SNAP=<snapshot-ordner> setzen (z. B. tests/fixtures/b8_pos)"; exit 1; }
	$(BUILD)/medic-ui-preview $(SNAP) $(OUT) $(PREVIEW_ARGS)

host:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) -j

test: host
	$(BUILD)/medic-tests

clean:
	rm -rf $(BUILD) $(PS5_BUILD)

ps5: $(PS5_ELF)
ps5-ui: $(PS5_UI_ELF)

$(PS5_BUILD)/%.o: %.cpp
	@test -x $(PS5_CXX) || { echo "ps5-payload-sdk nicht gefunden: $(PS5_PAYLOAD_SDK) (PS5_PAYLOAD_SDK=... setzen)"; exit 1; }
	@mkdir -p $(dir $@)
	$(PS5_CXX) $(PS5_CXXFLAGS) -MMD -MP -c $< -o $@

$(PS5_ELF): $(PS5_OBJS)
	$(PS5_CXX) $^ -lkernel_sys -o $@

$(PS5_UI_ELF): $(PS5_UI_OBJS)
	@test -d $(PS5_HOMEBREW)/lib || { echo "SDL2 fuer die PS5 fehlt in $(PS5_HOMEBREW) (ps5-payload-sdl2 + ps5-payload-libsamplerate, siehe docs/PHASE3.md)"; exit 1; }
	$(PS5_CXX) $^ -L$(PS5_HOMEBREW)/lib $(PS5_UI_LIBS) -lkernel_sys -o $@

-include $(PS5_OBJS:.o=.d) $(PS5_UI_OBJS:.o=.d)

deploy: $(PS5_ELF)
	@test -n "$(PS5_HOST)" || { echo "PS5_HOST=<ip der Konsole> setzen (FTP-Server auf Port $(PS5_FTP_PORT) muss laufen)"; exit 1; }
	curl --fail --ftp-create-dirs -T $(PS5_ELF) ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/homebrew/PS5MEDIC/ps5medic.elf
	curl --fail --ftp-create-dirs -T knowledge/rules.json ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/ps5medic/knowledge/rules.json
	@echo "Hochgeladen. ps5medic.elf ueber den Payload Manager starten; Ergebnis: /data/ps5medic/last_check.txt und snapshots/"

deploy-ui: $(PS5_UI_ELF)
	@test -n "$(PS5_HOST)" || { echo "PS5_HOST=<ip der Konsole> setzen (FTP-Server auf Port $(PS5_FTP_PORT) muss laufen)"; exit 1; }
	curl --fail --ftp-create-dirs -T $(PS5_UI_ELF) ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/homebrew/PS5MEDIC/ps5medicui.elf
	curl --fail --ftp-create-dirs -T knowledge/rules.json ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/ps5medic/knowledge/rules.json
	@echo "Hochgeladen. ps5medicui.elf ueber den Payload Manager starten."
