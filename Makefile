# SPDX-License-Identifier: GPL-3.0-or-later
# PS5 Medic
#   make host     medic-cli und Tests fuer Linux/WSL
#   make test     Unit- und Regel-Tests gegen alle Fixtures
#   make ps5      Diagnose-Payload (ELF, nur lesend) mit dem ps5-payload-sdk bauen
#   make ps5-ui   Vollbild-UI-Payload bauen (braucht zusaetzlich ps5-payload-sdl2, siehe docs/PHASE3.md)
#   make ps5-launcher  Launcher-ELF bauen (ohne websrv: legt den Kachel-Ordner FAKE42007 an, startet die UI mit App-Kontext), siehe docs/TITLE.md
#   make send-launcher  Launcher an Port 9021 der Konsole senden (PS5_HOST=<ip> noetig; legt Kachel an und startet die UI)
#   make deploy    ps5medic.elf + Regeln per FTP (Port 2121) auf die Konsole legen   (PS5_HOST=<ip> noetig)
#   make deploy-ui ps5medicui.elf + Regeln + Fehlercodes + Launcher-Eintrag per FTP auf die Konsole legen (PS5_HOST=<ip> noetig)
#   make launch-ui (Alternative ohne Launcher) deploy-ui und danach per websrv (/hbldr, Port 8080) im App-Kontext starten
#   make dist      Release-Ordner und Zip unter dist/ bauen (nur lokal, es wird nichts veroeffentlicht)
#   make sign-rules   knowledge/rules.json und error_codes.json nach dist/rules/ kopieren und mit dem Signierschluessel signieren (SIGN_KEY=...)
#   make check-sources  knowledge/error_codes.json gegen die Sony-Seiten und weiteren Quellen pruefen (Netz noetig; --update: python3 tools/check_error_sources.py --update)
#   make serve-rules  dist/rules/ per HTTP im LAN anbieten (RULES_PORT, RULES_BIND); die Konsole holt sie unter Regel-Update
#   make set-update-url  update_url in /data/ps5medic/config.ini der Konsole setzen (PS5_HOST=<ip> UPDATE_URL=http://...)
#   make title     Starter-Titel (nativer Titel mit eboot.bin) fuer die Kachel unter "Medien" bauen: build-title/FAKE42007 (docs/TITLE.md)
#   make deploy-title Ordner-App FAKE42007 per FTP nach /data/homebrew/FAKE42007/ legen; ShadowMount+ legt die Kachel an (PS5_HOST=<ip> noetig)
#
# SDK: PS5_PAYLOAD_SDK zeigt auf den Ordner mit bin/, target/, toolchain/ (Standard: ~/ps5-sdk-045/PS5_PAYLOAD_SDK, ps5-payload-sdk 0.45).
# SDL2 fuer ps5-ui liegt im selben SDK-Sysroot unter target/user/homebrew/ (dort von `make deps` o.ae. installiert,
# siehe docs/PHASE3.md): nicht im Repo, kein Netzzugriff beim normalen Bauen noetig.
BUILD ?= build
PS5_BUILD ?= build-ps5
PS5_PAYLOAD_SDK ?= $(HOME)/ps5-sdk-045/PS5_PAYLOAD_SDK
PS5_HOMEBREW := $(PS5_PAYLOAD_SDK)/target/user/homebrew
PS5_HOST ?=
PS5_FTP_PORT ?= 2121
PS5_WEB_PORT ?= 8080

PS5_CXX := $(PS5_PAYLOAD_SDK)/bin/prospero-clang++
# -Iui/include -Iui/src und der SDL2-Include-Pfad gelten fuer alle Objekte -- schadet dem Diagnose-Payload nicht,
# der sie nicht benutzt, und erspart eine zweite CXXFLAGS-Variable je Ziel.
PS5_CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Ilibmedic/include -Ithird_party -Ips5 -Iui/include -Iui/src -I$(PS5_HOMEBREW)/include

PS5_CORE_SRCS := $(wildcard libmedic/src/*.cpp) third_party/qrcodegen/qrcodegen.cpp ps5/ps5_platform.cpp
PS5_C_SRCS := third_party/tweetnacl/tweetnacl.c third_party/sqlite3/sqlite3.c
# png.cpp und shot_hook.cpp: PNG-Schreiber fuer den Screenshot-Hook der UI (Entwicklung, siehe shot_hook.hpp).
PS5_UI_SRCS := ui/src/canvas.cpp ui/src/font.cpp ui/src/font_data.cpp ui/src/i18n.cpp ui/src/i18n_embedded.cpp \
               ui/src/icons.cpp ui/src/png.cpp ui/src/shot_hook.cpp ui/src/splash.cpp ui/src/app.cpp ui/src/local_service.cpp

PS5_OBJS := $(patsubst %.cpp,$(PS5_BUILD)/%.o,$(PS5_CORE_SRCS) ps5/main.cpp) $(patsubst %.c,$(PS5_BUILD)/%.o,$(PS5_C_SRCS))
PS5_UI_OBJS := $(patsubst %.cpp,$(PS5_BUILD)/%.o,$(PS5_CORE_SRCS) $(PS5_UI_SRCS) ps5/ui_main.cpp) $(patsubst %.c,$(PS5_BUILD)/%.o,$(PS5_C_SRCS))
PS5_ELF := $(PS5_BUILD)/ps5medic.elf
# Einmalige Sensor-Erkundung (docs/DIAGNOSE.md, Batch 5): eigenes ELF, kein Teil des Produkts. Senden: nc <ip> 9021 < build-ps5/ps5medic-probe.elf
PS5_PROBE_ELF := $(PS5_BUILD)/ps5medic-probe.elf
# Einmaliger Lauf der Gruendlich-Tests ohne UI (Waerme- und Schreibtest), nur mit ausdruecklicher Freigabe des Nutzers
PS5_SELFTEST_ELF := $(PS5_BUILD)/ps5medic-selftest.elf
PS5_SELFTEST_OBJS := $(patsubst %.cpp,$(PS5_BUILD)/%.o,$(PS5_CORE_SRCS) ps5/selftest_run.cpp) $(patsubst %.c,$(PS5_BUILD)/%.o,$(PS5_C_SRCS))
PS5_UI_ELF := $(PS5_BUILD)/ps5medicui.elf
# Launcher ohne websrv und ohne Homebrew-Lader (docs/TITLE.md). Der Lader (hbldr/elfldr/pt) stammt aus websrv, siehe third_party/websrv-hbldr/.
#   ps5medic-start.elf     startet nur die UI; steckt im Starter der Kachel (title/starter.c)
#   ps5medic-launcher.elf  dasselbe, legt aber vorher den Kachel-Ordner /data/homebrew/FAKE42007 an (Dateien aus build-title/FAKE42007); das ist die ELF zum Senden
PS5_START_ELF := $(PS5_BUILD)/ps5medic-start.elf
PS5_LAUNCHER_ELF := $(PS5_BUILD)/ps5medic-launcher.elf
PS5_LAUNCHER_C_SRCS := third_party/websrv-hbldr/hbldr.c third_party/websrv-hbldr/elfldr.c third_party/websrv-hbldr/pt.c
PS5_LAUNCHER_C_OBJS := $(patsubst %.c,$(PS5_BUILD)/%.o,$(PS5_LAUNCHER_C_SRCS))
PS5_START_OBJS := $(PS5_BUILD)/ps5/launcher_main.o $(PS5_LAUNCHER_C_OBJS)
PS5_LAUNCHER_OBJS := $(PS5_BUILD)/ps5/launcher_setup.o $(PS5_LAUNCHER_C_OBJS)
TILE_DIR := build-title/FAKE42007
PS5_UI_LIBS := -lSDL2 -lSceSystemService -lSceUserService -lScePad -lSceAudioOut -lsamplerate -lSceVideoOut \
               -lSceKeyboard -lSceImeDialog

.PHONY: host test clean ps5-launch-title ps5 ps5-probe ps5-selftest ps5-ui ps5-start ps5-launcher send-launcher deploy deploy-ui launch-ui preview dist title deploy-title sign-rules serve-rules set-update-url check-sources

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
	python3 tests/test_triage.py

clean:
	rm -rf $(BUILD) $(PS5_BUILD) dist build-title

ps5: $(PS5_ELF)
ps5-ui: $(PS5_UI_ELF)
ps5-start: $(PS5_START_ELF)
ps5-launcher: $(PS5_LAUNCHER_ELF)
ps5-probe: $(PS5_PROBE_ELF)
ps5-selftest: $(PS5_SELFTEST_ELF)

$(PS5_BUILD)/%.o: %.cpp
	@test -x $(PS5_CXX) || { echo "ps5-payload-sdk nicht gefunden: $(PS5_PAYLOAD_SDK) (PS5_PAYLOAD_SDK=... setzen)"; exit 1; }
	@mkdir -p $(dir $@)
	$(PS5_CXX) $(PS5_CXXFLAGS) -MMD -MP -c $< -o $@

# Fremdcode in C (TweetNaCl): als C uebersetzen, ohne Warnungen.
$(PS5_BUILD)/%.o: %.c
	@test -x $(PS5_CXX) || { echo "ps5-payload-sdk nicht gefunden: $(PS5_PAYLOAD_SDK) (PS5_PAYLOAD_SDK=... setzen)"; exit 1; }
	@mkdir -p $(dir $@)
	$(PS5_CXX) -x c -std=gnu11 -O2 -w $(PS5_C_EXTRA) -MMD -MP -c $< -o $@

# SQLite ohne Dateizugriff (nur Speicher-Datenbanken, siehe libmedic/src/sqlite_check.cpp)
$(PS5_BUILD)/third_party/sqlite3/sqlite3.o: PS5_C_EXTRA := -DSQLITE_OS_OTHER=1 -DSQLITE_THREADSAFE=0 -DSQLITE_TEMP_STORE=3 -DSQLITE_OMIT_LOAD_EXTENSION=1 -DSQLITE_OMIT_SHARED_CACHE=1 -DSQLITE_OMIT_DEPRECATED=1 -DSQLITE_OMIT_LOCALTIME=1 -DSQLITE_OMIT_PROGRESS_CALLBACK=1 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_DQS=0 -DSQLITE_MAX_MMAP_SIZE=0

$(PS5_ELF): $(PS5_OBJS)
	$(PS5_CXX) $^ -lkernel_sys -o $@

$(PS5_SELFTEST_ELF): $(PS5_SELFTEST_OBJS)
	$(PS5_CXX) $^ -lkernel_sys -o $@

$(PS5_PROBE_ELF): $(PS5_BUILD)/ps5/probe_sensors.o
	$(PS5_CXX) $^ -lkernel_sys -o $@

$(PS5_UI_ELF): $(PS5_UI_OBJS)
	@test -d $(PS5_HOMEBREW)/lib || { echo "SDL2 fuer die PS5 fehlt in $(PS5_HOMEBREW) (ps5-payload-sdl2 + ps5-payload-libsamplerate, siehe docs/PHASE3.md)"; exit 1; }
	$(PS5_CXX) $^ -L$(PS5_HOMEBREW)/lib $(PS5_UI_LIBS) -lkernel_sys -o $@

# Startbild: ui/assets/splash.jpg steckt per .incbin im Objekt (docs/PHASE3.md)
$(PS5_BUILD)/ui/src/splash.o: private PS5_CXXFLAGS += '-DMEDIC_SPLASH_PATH="$(CURDIR)/ui/assets/splash.jpg"'
$(PS5_BUILD)/ui/src/splash.o: ui/assets/splash.jpg

$(PS5_BUILD)/ps5/launcher_main.o: private PS5_CXXFLAGS += -Ithird_party/websrv-hbldr
$(PS5_BUILD)/ps5/launcher_setup.o: private PS5_CXXFLAGS += -Ithird_party/websrv-hbldr -DMEDIC_TILE_INSTALL '-DMEDIC_TILE_DIR="$(TILE_DIR)"'

$(PS5_BUILD)/ps5/launcher_setup.o: ps5/launcher_main.cpp $(TILE_DIR)/eboot.bin
	@mkdir -p $(dir $@)
	$(PS5_CXX) $(PS5_CXXFLAGS) -MMD -MP -c $< -o $@

$(PS5_START_ELF): $(PS5_START_OBJS)
	$(PS5_CXX) $^ -lkernel_sys -lSceSystemService -lSceUserService -o $@

# Der Starter der Kachel (nativer Titel, eboot.bin) traegt ps5medic-start.elf; build-title/FAKE42007 enthaelt die fertige Ordner-App.
$(TILE_DIR)/eboot.bin: $(PS5_START_ELF) title/starter.c title/build_title.sh title/title.ld title/symbols.map ps5/homebrew/sce_sys/icon0.png
	bash title/build_title.sh $(PS5_START_ELF)

# Entwicklungswerkzeug: startet einen Titel wie ein Druck auf die Kachel (ps5/launch_title.cpp), siehe docs/TITLE.md
PS5_LAUNCHTITLE_ELF := $(PS5_BUILD)/ps5medic-launch-title.elf
ps5-launch-title: $(PS5_LAUNCHTITLE_ELF)
$(PS5_LAUNCHTITLE_ELF): $(PS5_BUILD)/ps5/launch_title.o
	$(PS5_CXX) $^ -lkernel_sys -lSceSystemService -lSceUserService -o $@

$(PS5_LAUNCHER_ELF): $(PS5_LAUNCHER_OBJS)
	$(PS5_CXX) $^ -lkernel_sys -lSceSystemService -lSceUserService -o $@

-include $(PS5_OBJS:.o=.d) $(PS5_UI_OBJS:.o=.d) $(PS5_START_OBJS:.o=.d) $(PS5_BUILD)/ps5/launcher_setup.d

# Launcher an elfldr (Port 9021) senden. Danach: Kachel "PS5 Medic" (FAKE42007) erscheint, sobald ShadowMount+ den Ordner sieht, die UI startet. Protokoll: /data/ps5medic/launcher.log
send-launcher: $(PS5_LAUNCHER_ELF)
	@test -n "$(PS5_HOST)" || { echo "PS5_HOST=<ip der Konsole> setzen (elfldr muss auf Port 9021 lauschen)"; exit 1; }
	nc -w 3 $(PS5_HOST) 9021 < $(PS5_LAUNCHER_ELF)
	@echo "Gesendet. Protokoll: ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/ps5medic/launcher.log (UI: ui.log)"

deploy: $(PS5_ELF)
	@test -n "$(PS5_HOST)" || { echo "PS5_HOST=<ip der Konsole> setzen (FTP-Server auf Port $(PS5_FTP_PORT) muss laufen)"; exit 1; }
	curl --fail --ftp-create-dirs -T $(PS5_ELF) ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/homebrew/PS5MEDIC/ps5medic.elf
	curl --fail --ftp-create-dirs -T knowledge/rules.json ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/ps5medic/knowledge/rules.json
	@echo "Hochgeladen. ps5medic.elf ueber den Payload Manager starten; Ergebnis: /data/ps5medic/last_check.txt und snapshots/"

deploy-ui: $(PS5_UI_ELF) $(PS5_LAUNCHER_ELF)
	@test -n "$(PS5_HOST)" || { echo "PS5_HOST=<ip der Konsole> setzen (FTP-Server auf Port $(PS5_FTP_PORT) muss laufen)"; exit 1; }
	curl --fail --ftp-create-dirs -T $(PS5_UI_ELF) ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/homebrew/PS5MEDIC/ps5medicui.elf
	curl --fail --ftp-create-dirs -T $(PS5_LAUNCHER_ELF) ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/homebrew/PS5MEDIC/ps5medic-launcher.elf
	curl --fail --ftp-create-dirs -T ps5/homebrew/homebrew.js ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/homebrew/PS5MEDIC/homebrew.js
	curl --fail --ftp-create-dirs -T ps5/homebrew/sce_sys/icon0.png ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/homebrew/PS5MEDIC/sce_sys/icon0.png
	curl --fail --ftp-create-dirs -T knowledge/rules.json ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/ps5medic/knowledge/rules.json
	curl --fail --ftp-create-dirs -T knowledge/error_codes.json ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/ps5medic/knowledge/error_codes.json
	@echo "Hochgeladen. Starten: make send-launcher PS5_HOST=$(PS5_HOST) (legt den Kachel-Ordner an) oder die Kachel \"PS5 Medic\" druecken (make deploy-title fuer die Ordner-App)."

# Startet die UI ueber websrvs Big-App-Lader (/hbldr): nur so hat der Prozess App-Kontext, und nur dann oeffnet sich das Pad.
# curl endet mit 28 (Zeitlimit), weil die Ausgabe der laufenden App gestreamt wird -- das ist der Normalfall.
launch-ui: deploy-ui
	@curl -sf -m 5 http://$(PS5_HOST):$(PS5_WEB_PORT)/version > /dev/null || { echo "websrv antwortet nicht auf http://$(PS5_HOST):$(PS5_WEB_PORT) -- websrv.elf zuerst starten (Payload Manager oder Port 9021)"; exit 1; }
	curl -s -m 15 "http://$(PS5_HOST):$(PS5_WEB_PORT)/hbldr?path=/data/homebrew/PS5MEDIC/ps5medicui.elf&cwd=/data/homebrew/PS5MEDIC&pipe=1" || test $$? -eq 28
	@echo "Gestartet. Protokoll: ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/ps5medic/ui.log"

# Paket: Ordner PS5MEDIC (kommt nach /data/homebrew/), LICENSE, README, Drittanbieter-Liste und Pruefsummen, dazu ein Zip.
VERSION := $(shell sed -n 's/.*kMedicVersion = "\(.*\)";/\1/p' libmedic/include/medic/report.hpp)
DIST_NAME := ps5-medic-$(VERSION)
DIST_DIR := dist/$(DIST_NAME)

dist: ps5 ps5-ui ps5-launcher title
	@test -n "$(VERSION)" || { echo "Version nicht gefunden (kMedicVersion in libmedic/include/medic/report.hpp)"; exit 1; }
	rm -rf $(DIST_DIR) dist/$(DIST_NAME).zip
	mkdir -p $(DIST_DIR)/PS5MEDIC/sce_sys
	cp $(PS5_UI_ELF) $(DIST_DIR)/PS5MEDIC/ps5medicui.elf
	cp $(PS5_LAUNCHER_ELF) $(DIST_DIR)/PS5MEDIC/ps5medic-launcher.elf
	cp $(PS5_ELF) $(DIST_DIR)/PS5MEDIC/ps5medic.elf
	cp knowledge/rules.json $(DIST_DIR)/PS5MEDIC/rules.json
	cp knowledge/error_codes.json $(DIST_DIR)/PS5MEDIC/error_codes.json
	cp ps5/homebrew/homebrew.js $(DIST_DIR)/PS5MEDIC/homebrew.js
	cp ps5/homebrew/sce_sys/icon0.png $(DIST_DIR)/PS5MEDIC/sce_sys/icon0.png
	cp -r build-title/FAKE42007 $(DIST_DIR)/FAKE42007
	cp LICENSE README.md README.en.md THIRD_PARTY.md docs/TESTER.md $(DIST_DIR)/
	cd $(DIST_DIR) && find . -type f ! -name SHA256SUMS.txt | sort | xargs sha256sum > SHA256SUMS.txt
	cd dist && python3 -m zipfile -c $(DIST_NAME).zip $(DIST_NAME)
	@echo "Fertig: dist/$(DIST_NAME).zip (Ordner PS5MEDIC und FAKE42007 nach /data/homebrew/ kopieren). Lizenz GPL-3.0-or-later; bei Weitergabe den Quelltext anbieten."

# Kachel unter "Medien": ein winziger Starter-Titel (title/), der den Launcher an den ELF-Lader sendet. Siehe docs/TITLE.md.
title: $(TILE_DIR)/eboot.bin

TITLE_ID := FAKE42007
deploy-title: title
	@test -n "$(PS5_HOST)" || { echo "PS5_HOST=<ip der Konsole> setzen (FTP-Server auf Port $(PS5_FTP_PORT) muss laufen)"; exit 1; }
	for f in eboot.bin sce_module/libc.prx sce_sys/icon0.png sce_sys/param.json; do \
	    curl --fail --ftp-create-dirs -T build-title/$(TITLE_ID)/$$f ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/homebrew/$(TITLE_ID)/$$f || exit 1; \
	done
	@echo "Hochgeladen. ShadowMount+ legt die Kachel \"PS5 Medic\" unter Medien innerhalb von Sekunden an. Voraussetzung beim Start: der ELF-Lader (Port 9021) laeuft und der Ordner PS5MEDIC (make deploy-ui) liegt in /data/homebrew/."

# Signierte Regeln fuer den Download (nur am PC; der Privatschluessel liegt ausserhalb des Repositorys).
# Ablauf: Regeln aendern, db_version hochzaehlen, `make sign-rules`, `make serve-rules`; auf der Konsole: Einstellungen -> Regel-Update -> Herunterladen.
SIGN_KEY ?= $(HOME)/.ps5medic/rules-signing.key
RULES_OUT ?= dist/rules
RULES_PORT ?= 8000
RULES_BIND ?= 0.0.0.0

sign-rules: host
	@test -f "$(SIGN_KEY)" || { echo "Schluesseldatei fehlt: $(SIGN_KEY) -- neu erzeugen mit: $(BUILD)/medic-sign keygen <datei> (und den oeffentlichen Teil in libmedic/src/trusted_keys.cpp eintragen)"; exit 1; }
	mkdir -p $(RULES_OUT)
	cp knowledge/rules.json $(RULES_OUT)/rules.json
	$(BUILD)/medic-sign sign "$(SIGN_KEY)" $(RULES_OUT)/rules.json
	$(BUILD)/medic-sign verify $(RULES_OUT)/rules.json $(RULES_OUT)/rules.json.sig
	cp knowledge/error_codes.json $(RULES_OUT)/error_codes.json
	$(BUILD)/medic-sign sign "$(SIGN_KEY)" $(RULES_OUT)/error_codes.json
	$(BUILD)/medic-sign verify $(RULES_OUT)/error_codes.json $(RULES_OUT)/error_codes.json.sig
	@echo "Fertig: $(RULES_OUT)/rules.json und error_codes.json samt .sig. Anbieten: make serve-rules"

# Fehlercodes gegen ihre Quellen pruefen (braucht Netz; nicht Teil von make test). Danach ggf. die Eintraege neu lesen und --update.
check-sources:
	python3 tools/check_error_sources.py

serve-rules:
	@test -f $(RULES_OUT)/rules.json.sig || { echo "Erst make sign-rules"; exit 1; }
	@echo "Regeln unter http://<ip-dieses-rechners>:$(RULES_PORT)/ anbieten (Strg+C beendet). Diese Adresse als update_url auf der Konsole eintragen."
	cd $(RULES_OUT) && python3 -m http.server $(RULES_PORT) --bind $(RULES_BIND)

# update_url in config.ini der Konsole setzen (ersetzt die Datei; sie enthaelt sonst nichts).
UPDATE_URL ?=
set-update-url:
	@test -n "$(PS5_HOST)" -a -n "$(UPDATE_URL)" || { echo "PS5_HOST=<ip> und UPDATE_URL=http://<ip>:$(RULES_PORT)/ setzen"; exit 1; }
	printf 'update_url=%s\n' '$(UPDATE_URL)' | curl --fail --ftp-create-dirs -T - ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/ps5medic/config.ini
