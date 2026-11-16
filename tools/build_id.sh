#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Build-Kennung von App und Dienst: die ersten 10 Hex-Stellen eines Hashes ueber die Quelldateien, aus denen beide entstehen. Dieselbe Kennung bei
# gleichem Quellstand, eine andere nach jeder Aenderung, ohne git (das Worktree-Verzeichnis ist in der WSL nicht lesbar). Vom Makefile und von
# title/build_native_ui.sh benutzt (ps5/service_protocol.hpp: der Dienst nennt sie im Hallo, die App vergleicht).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
find libmedic ps5 ui/src ui/include ui/i18n knowledge -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' -o -name '*.inc' -o -name '*.json' \) -print0 \
    | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum | cut -c1-10
