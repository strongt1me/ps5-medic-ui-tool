#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Baut den PS5-Medic-Starter als nativen PS5-Titel (Kachel unter "Medien"), in WSL oder Linux, mit dem ps5-payload-sdk 0.50.
# Gleiche Kette wie die Kacheln von PS5 Cooling & System Center und ps5-native-app-boilerplate/tools/build.sh:
#   1. title/starter.c mit clang-18 und den Zielflags des SDK uebersetzen (der Starter-Launcher steckt als payload.elf darin)
#   2. mit dem prospero-lld des SDK zu einem Zwischen-PIE linken (title/title.ld)
#   3. ps5-native-tool: in einen PS5-Titel umwandeln (Prozessparameter) und signieren
# Aufruf: bash title/build_title.sh [ps5medic-start.elf]   (Standard: build-ps5/ps5medic-start.elf, `make ps5-start`)
# Braucht (Pfade per Umgebung aenderbar):
#   PS5_PAYLOAD_SDK  Standard ~/ps5-sdk-050/PS5_PAYLOAD_SDK   das SDK 0.50 (ps5-payload-sdk.zip, SHA-256 2993a69e...)
#   PS5_NATIVE_TOOL  Standard ~/lmms-ps5/boilerplate/build/host/ps5-native-tool   aus ps5-native-app-boilerplate
#   PS5_LIBC_PRX     Standard ~/bfapp/dist/PPSA77505/sce_module/libc.prx          signierter Laufzeit-Shim des Boilerplates
#   clang-18
# Ergebnis: build-title/FAKE42007/ - die fertige Ordner-App fuer /data/homebrew (ShadowMount+ legt die Kachel an), `make deploy-title`.
#   Dieselben Dateien stecken in ps5medic-launcher.elf, das den Ordner beim Senden selbst anlegt.
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SDK=${PS5_PAYLOAD_SDK:-$HOME/ps5-sdk-050/PS5_PAYLOAD_SDK}
TOOL=${PS5_NATIVE_TOOL:-$HOME/lmms-ps5/boilerplate/build/host/ps5-native-tool}
LIBC=${PS5_LIBC_PRX:-$HOME/bfapp/dist/PPSA77505/sce_module/libc.prx}
START_ELF=${1:-$REPO/build-ps5/ps5medic-start.elf}
CLANG=$(command -v clang-18 || true)
[[ -x $TOOL ]] || { echo "ps5-native-tool fehlt: $TOOL (PS5_NATIVE_TOOL=<pfad> setzen)" >&2; exit 2; }
[[ -x $SDK/bin/prospero-lld ]] || { echo "SDK 0.50 fehlt: $SDK (PS5_PAYLOAD_SDK=<pfad> setzen)" >&2; exit 2; }
[[ -x $CLANG ]] || { echo "clang-18 fehlt" >&2; exit 2; }
[[ -f $LIBC ]] || { echo "Laufzeit-Shim fehlt: $LIBC (PS5_LIBC_PRX=<pfad> setzen)" >&2; exit 2; }
[[ -f $START_ELF ]] || { echo "Starter-Launcher fehlt: $START_ELF (make ps5-start)" >&2; exit 2; }

MODULE_SDK=0x02000009
COMPANION_SDK=0x08050001
FSELF_MAGIC=0x1D3D154F

B=$REPO/build-title/obj
rm -rf "$REPO/build-title"
mkdir -p "$B"
cp "$START_ELF" "$REPO/build-title/payload.elf"   # .incbin in starter.c sucht neben der Quelldatei: -I zeigt hierher
cd "$REPO/title"

flags=(-target x86_64-sie-ps5 -fvisibility-nodllstorageclass=default -isysroot "$SDK"
       -isystem "$SDK/target/include" -fno-stack-protector -fno-plt -femulated-tls -fPIC
       -ffreestanding -fno-builtin -std=c11 -O2 -Wall -Wextra -Werror -I"$REPO/build-title"
       -funwind-tables -fasynchronous-unwind-tables)
"$CLANG" "${flags[@]}" -c starter.c -o "$B/starter.o"

"$SDK/bin/prospero-lld" -T title.ld --eh-frame-hdr \
    --version-script symbols.map -e _start -o "$B/pie.elf" "$B/starter.o" \
    --as-needed "$SDK"/target/lib/*.so

# nur diese Importe sind gewollt (libkernel, libSceSystemService); alles andere waere zur Laufzeit NULL
allowed="close connect sceKernelDebugOutText sceKernelSendNotificationRequest sceKernelUsleep sceSystemServiceLoadExec send setsockopt socket"
imports=$("$SDK/bin/llvm-nm" -D --undefined-only "$B/pie.elf" | awk '{ sub(/@.*/, "", $2); print $2 }' | sort -u)
for sym in $imports; do
    [[ " $allowed " == *" $sym "* ]] || { echo "unerwarteter Import: $sym" >&2; exit 1; }
done
echo "Importe: $(echo $imports)"

"$TOOL" link --in "$B/pie.elf" --out "$B/eboot.elf" --stub-dir "$SDK/target/lib" \
    --module-sdk "$MODULE_SDK" --companion-sdk "$COMPANION_SDK" --file-name eboot.elf
"$TOOL" self --sign --in "$B/eboot.elf" --out "$B/eboot.bin" --magic "$FSELF_MAGIC"
"$TOOL" self --inspect --file "$B/eboot.bin"

# Titelordner: applicationCategoryType 65536 = Kachel unter "Medien"
make_param() {  # $1 = Titel-ID, $2 = Konzept-ID
cat <<JSON
{
  "ageLevel": {
    "default": 0
  },
  "applicationCategoryType": 65536,
  "applicationDrmType": "free",
  "attribute": 1644167168,
  "attribute2": 0,
  "attribute3": 1048576,
  "conceptId": "$2",
  "contentBadgeType": 1,
  "contentId": "IV9999-$1_00-PS5MEDICSTART001",
  "contentVersion": "01.000.000",
  "downloadDataSize": 256,
  "gameIntent": {
    "permittedIntents": [
      {
        "intentType": "launchActivity"
      }
    ]
  },
  "localizedParameters": {
    "defaultLanguage": "en-US",
    "en-US": {
      "titleName": "PS5 Medic"
    },
    "de-DE": {
      "titleName": "PS5 Medic"
    }
  },
  "masterVersion": "01.00",
  "pubtools": {
    "creationDate": "2026-11-12 00:00:00",
    "toolVersion": "2.00"
  },
  "requiredSystemSoftwareVersion": "0x0000000000000000",
  "sdkVersion": "0x0000000000000000",
  "titleId": "$1",
  "versionFileUri": ""
}
JSON
}

for entry in FAKE42007:42007; do
    id=${entry%%:*}; concept=${entry##*:}
    OUT=$REPO/build-title/$id
    mkdir -p "$OUT/sce_sys" "$OUT/sce_module"
    cp "$B/eboot.bin" "$OUT/eboot.bin"
    cp "$LIBC" "$OUT/sce_module/libc.prx"
    cp "$REPO/ps5/homebrew/sce_sys/icon0.png" "$OUT/sce_sys/icon0.png"
    make_param "$id" "$concept" > "$OUT/sce_sys/param.json"
done
rm -f "$REPO/build-title/payload.elf"
ls -l "$REPO/build-title/FAKE42007" "$REPO/build-title/FAKE42007/sce_sys"
