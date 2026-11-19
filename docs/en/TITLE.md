# Tile and start without a homebrew loader: starter and launcher

[Deutsch](../TITLE.md) · **English**

> **Older approach (as of 12 Nov 2026):** The tile `FAKE42007` is now the native app ([docs/en/NATIVE.md](NATIVE.md)); the release no longer contains starter, launcher and SDL UI. This document describes the earlier approach, which stays in the repository as a fallback. `make send-launcher` now sends the installer of the **native** tile (NATIVE.md, "Installer to send"), no longer the launcher described here; the old starter (`make title`, `make deploy-title`) and the native app share the ID `FAKE42007`, never install both at the same time. `make deploy-title` therefore refuses to run unless `ALLOW_OLD_STARTER=1` is set.

As of 12 Nov 2026. PS5 Medic is a **folder app in `/data/homebrew/FAKE42007`**, like all other apps and games. ShadowMount+ detects the folder and creates the tile **PS5 Medic** under "Media". Neither websrv nor a homebrew loader nor a browser is needed. The starter in the folder app is a tiny native title (`eboot.bin`), just like the tiles of "PS5 Cooling & System Center": it sends the starter launcher to the ELF loader (port 9021), and the launcher starts the fullscreen UI with an app context (only then does the DualSense open, see `docs/en/PHASE3.md`).

| Way | How |
|---|---|
| **1. Send ELF** | (dropped; since 12 Nov 2026 `make send-launcher` creates the native tile, see NATIVE.md) |
| **2. Copy folder** | Copy the folder `FAKE42007` from the release (or `make deploy-title`) to `/data/homebrew/`; ShadowMount+ creates the tile, after that one press is enough, without sending any ELF. |

The UI itself (`ps5medicui.elf`) lies in `/data/homebrew/PS5MEDIC/` (`make deploy-ui`). The console itself (`/system_ex`, `/user/app`) is never touched; ShadowMount+ does the mounting.

## What happens when you press the tile

```
Tile  ->  Starter (eboot.bin, native title, sandboxed)  ->  sends ps5medic-start.elf to 127.0.0.1:9021
      ->  Launcher (payload)  ->  closes the starter (FAKE… app)  ->  starts ps5medicui.elf with app context
      ->  stays until the UI ends, then exits
```

1. **Starter** (`title/starter.c`): freestanding, only `libkernel`/`libSceSystemService`, the launcher ELF is embedded in the `eboot.bin` via `.incbin`. It needs the jailbreak's ELF loader (port 9021), otherwise it shows a message. Built with the **ps5-payload-sdk 0.45** and `ps5-native-tool` (ps5-native-app-boilerplate), procedure in `title/build_title.sh`.
2. **Launcher** (`ps5/launcher_main.cpp`): `ps5medic-start.elf` (embedded in the starter) only starts the UI. The earlier version with `-DMEDIC_TILE_INSTALL`, which wrote the tile folder, was dropped (12 Nov 2026); this job is done by the installer `ps5/installer_main.cpp` for the native tile.
3. **UI with app context:** The start uses the Big App loader from websrv (`third_party/websrv-hbldr/`, GPL-3.0-or-later, only the code, **not** the service): The system app `FAKE00000` is started via `sceSystemServiceLaunchApp` and swapped via ptrace against `ps5medicui.elf`. Beforehand, the launcher sets the system `authid` as websrv does and initializes the user service. If an app is currently being closed, it waits briefly and retries the start up to three times.
4. **No port, no network:** The launcher listens on nothing.

**Safety:** Only what is a system program (`NPXS…`) or a homebrew fake app (`FAKE…`, including the starter) is closed. A game or an app with an unknown title ID is never closed; the launcher then shows a notification ("An app is running in the foreground. Please close it first.").

## The folder app FAKE42007

`build-title/FAKE42007/` (in the release `FAKE42007/`) contains `eboot.bin`, `sce_module/libc.prx` (signed runtime shim from ps5-native-app-boilerplate, as with the other homebrew folder apps) and `sce_sys/{param.json,icon0.png}`; `applicationCategoryType 65536` means tile under "Media".

Earlier versions of this project created a second tile `FAKE42008` (with a program under `/system_ex/app`) or opened the browser via a link. Both are gone; a still existing tile `FAKE42008` can be removed in the PS5's storage settings.

## Building

```bash
make title                     # build-title/FAKE42007 (starter with embedded launcher)
make ps5-start                 # ps5medic-start.elf (embedded in the starter)
make deploy-ui deploy-title PS5_HOST=<ip>   # UI + launcher to PS5MEDIC, folder app to FAKE42007
```

Prerequisites in WSL: **ps5-payload-sdk 0.45** (`PS5_PAYLOAD_SDK`, default `~/ps5-sdk-045/PS5_PAYLOAD_SDK`; `ps5-payload-sdk.zip` from github.com/ps5-payload-dev/sdk, SHA-256 `5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c`, with SDL2 under `target/user/homebrew`), `ps5-native-tool` (`PS5_NATIVE_TOOL`, default `~/lmms-ps5/boilerplate/build/host/ps5-native-tool`), `libc.prx` of the boilerplate (`PS5_LIBC_PRX`, default `~/bfapp/dist/PPSA77505/sce_module/libc.prx`) and `clang-18`.

`make ps5-launch-title` builds a pure development tool (`ps5medic-launch-title.elf`): it starts a title like a press on the tile (title ID in `/data/ps5medic/launch_title.txt`, result in `launch_title.log`), so that the whole path can be checked without a TV.

## Logs

- `/data/ps5medic/launcher.log`: start, replacement of the old instance, every UI start including the attempt of which app was closed. Overwritten at every start.
- `/data/ps5medic/ui.log`: the UI; `ui.stdout`: its standard output.

## Restarting the console

The tile stays. ShadowMount+ and the ELF loader (port 9021) must be running; a launcher does not have to be sent beforehand.

## Removing the tile

Remove the folder `/data/homebrew/FAKE42007` (PS5 Medic never does this itself); ShadowMount+ then takes the tile away.

## License

The loader in `third_party/websrv-hbldr/` is GPL-3.0-or-later (John Törnblom), like PS5 Medic; changes and origin are in `THIRD_PARTY.md` and in the header of `hbldr.c`. Starter and launcher are own code. The build procedure of the starter (linker script, `ps5-native-tool`, `libc.prx`) follows ps5-native-app-boilerplate (BlackBearReloaded, GPL-3.0-or-later).
