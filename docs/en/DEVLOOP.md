# Checking the Medic UI with PS5 DevLoop (development, 11 Nov 2026)

[Deutsch](../DEVLOOP.md) · **English**

[PS5 DevLoop](https://github.com/griches/PS5_DevLoop) (Gary Riches, GPL-3.0-or-later) is a payload with a Python client: it creates a virtual DualSense (pressing buttons) and delivers images of the running title (`grab`) or of the whole screen (`shot`). This lets you operate and view the Medic UI **on the console**, instead of only in the PC preview (`medic-ui-preview`). **A development tool only**: Medic itself never feeds in inputs; the controller test (D14) needs a real pad.

## What is in the repository

The **screenshot hook** of the UI (`ui/include/medic_ui/shot_hook.hpp`, `ui/src/shot_hook.cpp`, called in `ps5/ui_main.cpp` every four frames): if `/data/ps5medic/devloop-shot` exists, the UI removes the file (the remove call is the check; it fails otherwise) and writes the **current image of its own canvas** (1920 × 1080, not the screen) as a new PNG file to `/data/ps5medic/screenshots/medic-<ms>.png`. A tool fetches it and deletes it. Safeguards: only on request, only in its own folder, at most 30 files there (after that the UI writes nothing more, because nobody is fetching). The image can show findings, paths and title names and leaves the console only if someone fetches it. Contract and idea come from DevLoop's `AGENTS.md`; no DevLoop code was taken over.

## How to use it (entry for DevLoop's `titles.json`)

```json
"medic": {
  "title_id": "FAKE42007",
  "request": "/data/ps5medic/devloop-shot",
  "folder": "/data/ps5medic/screenshots"
}
```

Then `tools/ps5loop.py grab --title medic` (image of the Medic UI) and `tools/ps5loop.py seq ...` for buttons. It also works by hand without DevLoop: put the file `devloop-shot` into `/data/ps5medic/` via FTP, wait briefly, fetch the PNG from `/data/ps5medic/screenshots/` and delete it.

## Reservations (weigh before every use on the console)

- According to its README, DevLoop is **only tested on firmware 13.60**; the test console has 12.00. The calls (`scePadVirtualDevice*`, Mbus) are undocumented and may differ.
- `open` disconnects the real controller pad until you press PS. `shot` holds the virtual Create button for 1.2 s, creates a file in the gallery and deletes it again (`sceContentDeleteByPath`).
- The payload listens **without authentication on port 9112** on the LAN. Only run it in a trusted network and quit it afterwards (`quit`).
- The client needs `JxrDecApp` and `magick` on the PC if images of the whole screen are decrypted.
- After the incident of 11 Nov 2026 (load test, see `docs/en/RULES.md`), DevLoop is only started on the test console with explicit approval.

## Checked

Tried on the test console (FW 12.00, 11 Nov 2026): `devloop-shot` created via FTP, after a few seconds the file was gone and `/data/ps5medic/screenshots/medic-<ms>.png` was there (1920 × 1080, about 6 MB, the start page of the UI); the image was fetched via FTP and deleted on the console. DevLoop itself (virtual pad, port 9112) was not started.
