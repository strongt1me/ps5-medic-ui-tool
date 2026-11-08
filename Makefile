# PS5 Medic
#   make host    medic-cli und Tests fuer Linux/WSL
#   make test    Unit- und Regel-Tests gegen alle Fixtures
#   make ps5     Diagnose-Payload (ELF) mit dem ps5-payload-sdk bauen
#   make deploy  ELF und Regeln per FTP (Port 2121) auf die Konsole legen   (PS5_HOST=<ip> noetig)
#
# SDK: PS5_PAYLOAD_SDK zeigt auf den Ordner mit bin/, target/, toolchain/ (Standard: ~/ps5-sdk/PS5_PAYLOAD_SDK).
BUILD ?= build
PS5_BUILD ?= build-ps5
PS5_PAYLOAD_SDK ?= $(HOME)/ps5-sdk/PS5_PAYLOAD_SDK
PS5_HOST ?=
PS5_FTP_PORT ?= 2121

PS5_CXX := $(PS5_PAYLOAD_SDK)/bin/prospero-clang++
PS5_CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Ilibmedic/include -Ithird_party -Ips5
PS5_SRCS := $(wildcard libmedic/src/*.cpp) $(wildcard ps5/*.cpp)
PS5_OBJS := $(patsubst %.cpp,$(PS5_BUILD)/%.o,$(PS5_SRCS))
PS5_ELF := $(PS5_BUILD)/ps5medic.elf

.PHONY: host test clean ps5 deploy

host:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) -j

test: host
	$(BUILD)/medic-tests

clean:
	rm -rf $(BUILD) $(PS5_BUILD)

ps5: $(PS5_ELF)

$(PS5_BUILD)/%.o: %.cpp
	@test -x $(PS5_CXX) || { echo "ps5-payload-sdk nicht gefunden: $(PS5_PAYLOAD_SDK) (PS5_PAYLOAD_SDK=... setzen)"; exit 1; }
	@mkdir -p $(dir $@)
	$(PS5_CXX) $(PS5_CXXFLAGS) -MMD -MP -c $< -o $@

$(PS5_ELF): $(PS5_OBJS)
	$(PS5_CXX) $^ -lkernel_sys -o $@

-include $(PS5_OBJS:.o=.d)

deploy: $(PS5_ELF)
	@test -n "$(PS5_HOST)" || { echo "PS5_HOST=<ip der Konsole> setzen (FTP-Server auf Port $(PS5_FTP_PORT) muss laufen)"; exit 1; }
	curl --fail --ftp-create-dirs -T $(PS5_ELF) ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/homebrew/PS5MEDIC/ps5medic.elf
	curl --fail --ftp-create-dirs -T knowledge/rules.json ftp://$(PS5_HOST):$(PS5_FTP_PORT)/data/ps5medic/knowledge/rules.json
	@echo "Hochgeladen. ps5medic.elf ueber den Payload Manager starten; Ergebnis: /data/ps5medic/last_check.txt und snapshots/"
