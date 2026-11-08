#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Baut den PS5-Medic-Starter (FAKE42007) als nativen PS5-Titel (Kachel unter "Medien"), in WSL oder Linux.
# Gleiche Kette wie der BFpilot-Starter und ps5-native-app-boilerplate/tools/build.sh:
#   1. title/starter.c mit clang-18 und den Zielflags des SDK 0.45 uebersetzen
#   2. mit dem prospero-lld des SDK zu einem Zwischen-PIE linken (title/title.ld)
#   3. ps5-native-tool: in einen PS5-Titel umwandeln (Prozessparameter) und signieren
# Braucht: $BOILERPLATE (Standard ~/bfapp): ein ps5-native-app-boilerplate-Checkout mit SDK 0.45 in .deps und gebautem
# build/host/ps5-native-tool, dazu clang-18. Ergebnis: build-title/FAKE42007/ (per `make deploy-title` auf die Konsole).
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
APP=${BOILERPLATE:-$HOME/bfapp}
SDK=$APP/.deps/native/ps5-payload-sdk
TOOL=$APP/build/host/ps5-native-tool
CLANG=$(command -v clang-18 || true)
[[ -x $TOOL ]] || { echo "ps5-native-tool fehlt: $TOOL (BOILERPLATE=<pfad> setzen)" >&2; exit 2; }
[[ -x $SDK/bin/prospero-lld ]] || { echo "SDK 0.45 fehlt: $SDK" >&2; exit 2; }
[[ -x $CLANG ]] || { echo "clang-18 fehlt" >&2; exit 2; }
[[ -f $APP/dist/PPSA77505/sce_module/libc.prx ]] || { echo "Laufzeit-Shim fehlt: $APP/dist/PPSA77505/sce_module/libc.prx" >&2; exit 2; }

TITLE_ID=FAKE42007
MODULE_SDK=0x02000009
COMPANION_SDK=0x08050001
FSELF_MAGIC=0x1D3D154F

B=$REPO/build-title/obj
OUT=$REPO/build-title/$TITLE_ID
rm -rf "$REPO/build-title"
mkdir -p "$B"
cd "$REPO/title"

flags=(-target x86_64-sie-ps5 -fvisibility-nodllstorageclass=default -isysroot "$SDK"
       -isystem "$SDK/target/include" -fno-stack-protector -fno-plt -femulated-tls -fPIC
       -ffreestanding -fno-builtin -std=c11 -O2 -Wall -Wextra -Werror -I.
       -funwind-tables -fasynchronous-unwind-tables)
"$CLANG" "${flags[@]}" -c starter.c -o "$B/starter.o"

"$SDK/bin/prospero-lld" -T title.ld --eh-frame-hdr \
    --version-script symbols.map -e _start -o "$B/pie.elf" "$B/starter.o" \
    --as-needed "$SDK"/target/lib/*.so

# nur diese Importe sind gewollt (libkernel, libSceSystemService); alles andere waere zur Laufzeit NULL
allowed="close connect recv sceKernelDebugOutText sceKernelSendNotificationRequest sceKernelUsleep sceSystemServiceLoadExec send setsockopt socket"
imports=$("$SDK/bin/llvm-nm" -D --undefined-only "$B/pie.elf" | awk '{ sub(/@.*/, "", $2); print $2 }' | sort -u)
for sym in $imports; do
    [[ " $allowed " == *" $sym "* ]] || { echo "unerwarteter Import: $sym" >&2; exit 1; }
done
echo "Importe: $(echo $imports)"

"$TOOL" link --in "$B/pie.elf" --out "$B/eboot.elf" --stub-dir "$SDK/target/lib" \
    --module-sdk "$MODULE_SDK" --companion-sdk "$COMPANION_SDK" --file-name eboot.elf

mkdir -p "$OUT/sce_sys" "$OUT/sce_module"
"$TOOL" self --sign --in "$B/eboot.elf" --out "$OUT/eboot.bin" --magic "$FSELF_MAGIC"
"$TOOL" self --inspect --file "$OUT/eboot.bin"

# signierter Laufzeit-Shim des Boilerplates (unabhaengig von der Titel-ID)
cp "$APP/dist/PPSA77505/sce_module/libc.prx" "$OUT/sce_module/libc.prx"
cp "$REPO/ps5/homebrew/sce_sys/icon0.png" "$OUT/sce_sys/icon0.png"

# applicationCategoryType 65536 = Kachel unter "Medien" (wie beim BFpilot-Starter)
cat > "$OUT/sce_sys/param.json" <<'JSON'
{
  "ageLevel": {
    "default": 0
  },
  "applicationCategoryType": 65536,
  "applicationDrmType": "free",
  "attribute": 1644167168,
  "attribute2": 0,
  "attribute3": 1048576,
  "conceptId": "42007",
  "contentBadgeType": 1,
  "contentId": "IV9999-FAKE42007_00-PS5MEDICSTART001",
  "contentVersion": "00.400.001",
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
    "creationDate": "2026-11-08 00:00:00",
    "toolVersion": "2.00"
  },
  "requiredSystemSoftwareVersion": "0x0000000000000000",
  "sdkVersion": "0x0000000000000000",
  "titleId": "FAKE42007",
  "versionFileUri": ""
}
JSON

ls -laR "$OUT"
