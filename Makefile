# PS5 Medic - Host-Build (Linux/WSL). `make ps5` und `make deploy` folgen in Phase 2-4.
BUILD ?= build

.PHONY: host test clean ps5 deploy

host:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) -j

test: host
	$(BUILD)/medic-tests

clean:
	rm -rf $(BUILD)

ps5 deploy:
	@echo "Noch nicht implementiert (siehe docs/ROADMAP.md, Phase 2-4)"; exit 1
