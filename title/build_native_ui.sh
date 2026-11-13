#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Baut die Oberflaeche als native PS5-App (Ordner-App fuer /data/homebrew), in WSL oder Linux, mit dem ps5-payload-sdk 0.45.
# Gleiche Kette wie title/build_title.sh, aber mit der ganzen UI (ps5/native_ui_main.cpp, ui/src/*) statt des Starters. Die kleine C++-Laufzeit
# (app_crt.cpp, app_cpp_runtime.cpp), das Linkerskript und die Symbolliste stammen aus ps5-native-app-boilerplate (kein libc++-Laufzeitpaket,
# keine Ausnahmen, kein RTTI).
# Aufruf: bash title/build_native_ui.sh [TITEL-ID]     (Standard FAKE42007 = die Kachel "PS5 Medic"; andere IDs fuer Testkacheln)
# Braucht: PS5_PAYLOAD_SDK (SDK 0.45), PS5_BOILERPLATE (Standard ~/lmms-ps5/boilerplate, gebaut: build/host/ps5-native-tool), PS5_LIBC_PRX, clang-18.
# Ergebnis: build-native/<TITEL-ID>/ -> `make deploy-native` (FTP nach /data/homebrew/<TITEL-ID>).
set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
ID=${1:-FAKE42007}
SDK=${PS5_PAYLOAD_SDK:-$HOME/ps5-sdk-045/PS5_PAYLOAD_SDK}
BOILER=${PS5_BOILERPLATE:-$HOME/lmms-ps5/boilerplate}
TOOL=${PS5_NATIVE_TOOL:-$BOILER/build/host/ps5-native-tool}
LIBC=${PS5_LIBC_PRX:-$HOME/bfapp/dist/PPSA77505/sce_module/libc.prx}
CLANG=$(command -v clang-18 || true)
[[ -x $TOOL ]] || { echo "ps5-native-tool fehlt: $TOOL" >&2; exit 2; }
[[ -x $SDK/bin/prospero-lld ]] || { echo "SDK 0.45 fehlt: $SDK" >&2; exit 2; }
[[ -x $CLANG ]] || { echo "clang-18 fehlt" >&2; exit 2; }
[[ -f $LIBC ]] || { echo "Laufzeit-Shim fehlt: $LIBC" >&2; exit 2; }
NATIVE=$BOILER/tooling/native
for f in app_crt.cpp app_cpp_runtime.cpp ps5-pie.ld app-symbols.map; do [[ -f $NATIVE/$f ]] || { echo "fehlt: $NATIVE/$f" >&2; exit 2; }; done

MODULE_SDK=0x02000009
COMPANION_SDK=0x08050001
FSELF_MAGIC=0x1D3D154F

OUTROOT=$REPO/build-native
B=$OUTROOT/obj
rm -rf "$OUTROOT"
mkdir -p "$B"

common=(-target x86_64-sie-ps5 -fvisibility-nodllstorageclass=default -isysroot "$SDK"
        -isystem "$SDK/target/include/c++/v1" -isystem "$SDK/target/include"
        -fno-stack-protector -fno-plt -femulated-tls -O2 -ffunction-sections -fdata-sections -Wall -Wextra)
cxx=("$CLANG" "${common[@]}" -std=c++20 -fno-exceptions -fno-rtti
     -I"$REPO/libmedic/include" -I"$REPO/third_party" -I"$REPO/ps5" -I"$REPO/ui/include" -I"$REPO/ui/src"
     "-DMEDIC_SPLASH_PATH=\"$REPO/ui/assets/splash.jpg\"" "-DMEDIC_BACKGROUND_PATH=\"$REPO/ui/assets/background.jpg\""
     -DNDEBUG=1 -DMEDIC_NATIVE_APP=1 -DNLOHMANN_JSON_NOEXCEPTION=1)

sources=(
    ps5/native_ui_main.cpp ps5/native_shim.cpp ps5/native_runtime.cpp
    ui/src/canvas.cpp ui/src/font.cpp ui/src/font_data.cpp ui/src/splash.cpp
)
objs=()
for s in "${sources[@]}"; do
    o=$B/$(echo "$s" | tr '/' '_').o
    "${cxx[@]}" -MD -MF "$o.d" -c "$REPO/$s" -o "$o"
    objs+=("$o")
done
for f in app_crt; do
    "$CLANG" "${common[@]}" -std=c++20 -fno-exceptions -fno-rtti -c "$NATIVE/$f.cpp" -o "$B/$f.o"
    objs+=("$B/$f.o")
done

"$SDK/bin/prospero-lld" -T "$NATIVE/ps5-pie.ld" --eh-frame-hdr --version-script "$NATIVE/app-symbols.map" \
    -e _start -o "$B/pie.elf" "$B/app_crt.o" $(printf '%s ' "${objs[@]}" | sed "s#$B/app_crt.o##") \
    --why-extract="$B/why.txt" -Map="$B/map.txt" --error-limit=0 -L"$SDK/target/lib" --start-group "$SDK/target/lib/libc++.a" "$SDK/target/lib/libc++abi.a" "$SDK/target/lib/libpthread.a" --end-group --as-needed "$SDK"/target/lib/*.so

# Neue Importe vermeiden: Die App bindet beim Start nur ein, was die Laufzeit (libc.prx) und die Systembibliotheken auf der Konsole liefern. Ein Import,
# der dort fehlt (13. Nov. 2026: fgets), laesst die App vor main() abbrechen -- ohne Meldung. Die Liste title/native_imports.txt ist der auf der Konsole
# bestaetigte Stand; ein neuer Import bricht den Bau ab, bis er auf der Konsole geprueft und in die Liste aufgenommen ist (ALLOW_NEW_IMPORTS=1 zum Testen).
if command -v nm >/dev/null; then
    nm -u "$B/pie.elf" | sed 's/^ *U //' | sort > "$B/imports.txt"
    NEW=$(comm -13 "$REPO/title/native_imports.txt" "$B/imports.txt" || true)
    if [[ -n $NEW ]]; then
        echo "Neue Importe in der App (nicht in title/native_imports.txt):" >&2
        echo "$NEW" >&2
        if [[ ${ALLOW_NEW_IMPORTS:-0} != 1 ]]; then
            echo "Sie koennten auf der Konsole fehlen und die App vor main() abbrechen lassen. Auf der Konsole pruefen (ALLOW_NEW_IMPORTS=1 baut trotzdem), dann in die Liste eintragen; oder eigene Schleifen statt der Bibliotheksfunktion verwenden." >&2
            exit 3
        fi
    fi
fi

"$TOOL" link --in "$B/pie.elf" --out "$B/eboot.elf" --stub-dir "$SDK/target/lib" \
    --module-sdk "$MODULE_SDK" --companion-sdk "$COMPANION_SDK" --file-name eboot.elf
"$TOOL" self --sign --in "$B/eboot.elf" --out "$B/eboot.bin" --magic "$FSELF_MAGIC"
"$TOOL" self --inspect --file "$B/eboot.bin"

OUT=$OUTROOT/$ID
mkdir -p "$OUT/sce_sys" "$OUT/sce_module"
cp "$B/eboot.bin" "$OUT/eboot.bin"
cp "$LIBC" "$OUT/sce_module/libc.prx"
cp "$REPO/ps5/homebrew/sce_sys/icon0.png" "$OUT/sce_sys/icon0.png"
concept=${ID: -5}
NAME="PS5 Medic"; [[ $ID == FAKE42007 ]] || NAME="PS5 Medic (native $ID)"
cat > "$OUT/sce_sys/param.json" <<JSON
{
  "ageLevel": { "default": 0 },
  "applicationCategoryType": 65536,
  "applicationDrmType": "free",
  "attribute": 1644167168,
  "attribute2": 0,
  "attribute3": 1048576,
  "conceptId": "$concept",
  "contentBadgeType": 1,
  "contentId": "IV9999-${ID}_00-PS5MEDICSTART001",
  "contentVersion": "01.000.000",
  "downloadDataSize": 256,
  "gameIntent": { "permittedIntents": [ { "intentType": "launchActivity" } ] },
  "localizedParameters": {
    "defaultLanguage": "en-US",
    "en-US": { "titleName": "$NAME" },
    "de-DE": { "titleName": "$NAME" }
  },
  "masterVersion": "01.00",
  "pubtools": { "creationDate": "2026-11-12 00:00:00", "toolVersion": "2.00" },
  "requiredSystemSoftwareVersion": "0x0000000000000000",
  "sdkVersion": "0x0000000000000000",
  "titleId": "$ID",
  "versionFileUri": ""
}
JSON
ls -l "$OUT" "$OUT/sce_sys"
