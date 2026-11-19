# Phase 5 – Community

[Deutsch](../PHASE5.md) · **English**

Status 8 Nov 2026. Rule update (file and signed download), unknown error lines, issue template and tester guide are in place. **Decided (8 Nov):** private, no GitHub release, no forum posts; **changed on 16 Nov 2026:** The repository is going public, `v0.4.0` is the latest release (see `docs/en/PHASE4.md`).

## 1. Rule update (file or signed download)

`libmedic/src/rule_update.cpp` (check, install, reset), `rule_download.cpp` and `http.cpp`/`posix_http.cpp` (download), `signature.cpp`/`ed25519.cpp` (signature); in the UI under More → *Update rules*.

**Two routes into the same folder** `/data/ps5medic/knowledge/update/`:
- **File update (without network):** The user puts `rules.json` and `rules.json.sha256` there themselves (FTP, USB copy). The checksum protects against damaged copies, not against a deliberately planted file together with its checksum; anyone who can put files on the console can do more than that anyway.
- **Download (decision by Marc, 8 Nov 2026):** □ in the *Rule update* screen, confirmation prompt, ✕. PS5 Medic fetches `rules.json` and `rules.json.sig` from `update_url` and puts them in the update folder. **It is only accepted with a valid signature.**

**Why a signature and not just the checksum:** A checksum that comes from the same server as the file only protects against transmission errors. Rules control repairs (`fix`, `fix_params`); a planted rules file would be a real attack path. That is why the author signs the rules file with Ed25519; the public key is built into PS5 Medic (`libmedic/src/trusted_keys.cpp`), the private key is never in the repository.

**What the network function does and does not do** (`CLAUDE.md`, guideline 5):
- only on explicit command (□, then ✕); never at startup, never in the background
- only to the address the user enters themselves in `/data/ps5medic/config.ini` (`update_url=http://host:port/path/` or `https://…`), **otherwise to the default address** `https://raw.githubusercontent.com/strongt1me/ps5-medic-ui-tool/main/rules-update/` (`kDefaultUpdateUrl`, decision by Marc, 17 Nov 2026): the signed files in the public repository
- two GET requests (`<url>/rules.json`, `<url>/rules.json.sig`), followed by at most two more for the optional error codes (`<url>/error_codes.json`, `<url>/error_codes.json.sig`; a 404 on the file itself means: the server does not offer it, which is not an error) with the headers `Host`, `User-Agent: PS5-Medic`, `Accept`, `Connection: close`; no cookies, no credentials, nothing about the console or the user (a test checks the request byte for byte)
- `http://` over BSD sockets (as before, `posix_http.cpp`) and `https://` with **BearSSL** (`third_party/bearssl`, MIT) over the same sockets, identical on the PC and on the console (`ps5/ps5_http.cpp` only passes the socket client through). The root certificates are built in (`libmedic/src/tls_roots.c`: ISRG Root X1/X2, DigiCert Global Root G2, USERTrust RSA/ECC; in 2026 GitHub delivers certificates from Let's Encrypt, chain YR1 → Root YR → ISRG Root X1; generated with `brssl ta`). What is checked: chain, validity (the console's date and time must be correct) and server name, TLS 1.2 only. The authenticity of the rules is secured independently of the channel by the file's signature. Errors appear on screen as the identifier `tls` with "Technical detail: BearSSL 62 (certificate not trusted)". **Why not the system's TLS (`libSceHttp2`/`libSceSsl`):** the console rejected GitHub's chain with `0x8095f00c`, and without a header for the system library this could not be configured (a call with a guessed flag hung the process; 18 Nov 2026). If a built-in certificate expires or GitHub changes the chain, `tls_roots.c` must be renewed. A small server of your own on the LAN still works (`make serve-rules`)
- no redirects, at most 2 MiB for the rules and 4 KiB for the signature, 15 s per request
- writes only under `/data/ps5medic/knowledge/`; update files already there are moved to `update/replaced/<time>/`; `update.log` gets a line `rules_download`
- the download checks and installs nothing; both follow the known chain (check, offer, confirmation, backup, undo)

**Signature and key**
- Format of `rules.json.sig`: one line `ed25519 <key-id> <base64 of the 64 bytes>`. The ID is the first 16 hex characters of SHA-256 over the public key and selects the key from the built-in list. Exactly the bytes of `rules.json` are signed.
- A checksum lying next to it then no longer counts: if a `.sig` is in the folder, it alone decides (no downgrade to the checksum). A test covers this.
- Rollback protection: Even a validly signed but older file is rejected (`not_newer`). A server can withhold updates or serve an old signed version (ineffective), but cannot invent a new rules file.
- Ed25519 lives in `third_party/tweetnacl` (TweetNaCl 20140427, public domain, unchanged, compiled as C). Checked with the RFC 8032 test vectors and, independently, with OpenSSL against the real signature of the shipped rules. In this version, TweetNaCl does not check whether the scalar S of the signature is canonical; at most this allows a second valid signature for the **same** file and is irrelevant here.
- **Private key:** `~/.ps5medic/rules-signing.key` (WSL, permissions 600), generated with `build/medic-sign keygen`; a second copy is at `C:\Users\stron\.ps5medic\` (protection against resetting the WSL distribution, **not** a substitute for a copy on a stick or in the password manager). It does **not** belong in the repository (`.gitignore` excludes `*.key`) and should be backed up; without it no more rules can be signed, and a new key needs a new PS5 Medic version. Whoever has the file can sign rules that every console accepts.
- **Key rotation:** add the new public key to `trusted_keys.cpp` **in addition**, ship a version, and only later remove the old one. There is no revocation list or expiry date; for a lost or compromised key, the only help is a new version with a new key.

**Error codes via the same route (9 Nov 2026):** `knowledge/error_codes.json` can be updated in the same way (`KnowledgeKind::ErrorCodes` in `rule_update.cpp`): separate files in the update folder (`error_codes.json`, `.sha256`, `.sig`), the same keys and checks (signature, schema of the error code file, only a newer `db_version`), its own backup (`backups/error_codes_<time>.json`), its own entries in `update.log` (`error_codes_update`, `error_codes_rollback`, `error_codes_download`) and its own high-water mark. A rules file can never be installed as an error code file and vice versa (the schemas differ), and a signature over one file does not apply to the other. In the *Rule update* screen, the offer for the error codes appears below the one for the rules; ✕ installs what is ready, △ rolls back where there is something to roll back. Downloading the error codes is optional: if they are missing on the server (404) or only this part fails, the rules still apply and the screen names the reason. **`CLAUDE.md` still says "only two GET requests" and would have to be changed to "up to four"** (Marc decides; not changed).

**Publishing on GitHub (default route):** change rules, increment `db_version`, `make test`, `make publish-rules` (signs into `rules-update/`, checks the signature), commit and push; only after that is the state retrievable via `raw.githubusercontent.com` (the cache there takes up to about 5 minutes). A test (`published_rules_tests`) checks that the files in `rules-update/` are validly signed and not newer than `knowledge/rules.json`.

**Procedure for the author (own server):** change rules, increment `db_version`, `make test`, `make sign-rules` (writes `dist/rules/rules.json` and `error_codes.json` together with `.sig` and checks them against the key; for the error codes, increment `db_version` when the content changes), `make serve-rules` (or put `dist/rules/` on any HTTP server). Write the address to the console once: `make set-update-url PS5_HOST=<ip> UPDATE_URL=http://<ip-of-the-server>:8000/`.

**Confirmed on the console (8 Nov 2026, PS5 Pro, FW 12.00):** Download from `http://192.168.1.98:8000/` (Python server on the PC, bound to the LAN address): The server log shows both requests from the console, the screen offered the file with "Signature valid (key 04de66c8…)", the installation is in `update.log` with `auth: signature` and the key ID. A manipulated file (newer version, old signature) was rejected with "The signature does not match the file" and not offered. Rollback was already confirmed with the file update. **Name resolution** (`getaddrinfo`, checked with a one-shot payload): `localhost` and the PC's host name on the LAN are resolved (the name yielded 192.168.1.98); public names (`example.org`) do **not** resolve on this console, presumably its DNS is deliberately restricted. The UI reports this as "The address name cannot be resolved. Use an IP address." **Not checked on the console:** behavior with a blocked firewall beyond the messages; **`https://` via `libSceHttp2` (built in on 17 Nov 2026, builds and links, test on the console pending)**. Name resolution of public names did not work on the test console on 8 Nov (restricted DNS); if the console does not resolve `raw.githubusercontent.com`, the only way left is via an address of your own.

**Check** (`check_rule_update`, writes nothing), in this order; every error names an identifier that the UI translates:
1. File there? (`no_rules_file`) · at most 2 MiB (`too_large`) · readable (`unreadable`)
2. If `rules.json.sig` is there: read and check the signature (`sig_malformed`, `sig_unknown_key`, `sig_invalid`); the checksum is then ignored, result "Signature valid (key …)"
3. Otherwise (file update): `rules.json.sha256` present and with 64 hex characters as the first word, as `sha256sum` writes it (`no_checksum`), and SHA-256 matches (`checksum_mismatch`); result "checksum only"
4. Rule checker (`load_rules_from_string`) accepts the file, including the schema (`invalid_rules`, with message)
5. `db_version` is **newer** than the running one (`not_newer`; equal or older is rejected)

**Installation** (`install_rule_update`, only after ✕): checks again · backs up the existing `/data/ps5medic/knowledge/rules.json` to `backups/rules_<time>.json` and reads the backup back for comparison · writes the new file atomically and reads it back for comparison (on a mismatch the old one is written back) · puts the update files into `update/installed/<time>/` (never delete) · writes a line into `knowledge/update.log`. The running app loads the new rules immediately; the previous findings no longer apply, the start screen shows "not checked yet".

**Rollback** (`rollback_rule_update`, △ then ✕): restores the backed-up file byte for byte, the replaced one is kept as `backups/rules_rolledback_<time>.json`. If there was no file in the knowledge folder before (the bundled rules applied), the installed one is set aside.

**Write boundary:** Everything lies under `/data/ps5medic/knowledge/`; a test checks that nothing else changes. Without write access, with a wrong checksum or an invalid timestamp, the file tree stays unchanged.

Where the app looks for rules and error codes: in `/data/ps5medic/knowledge/` and in the bundled `/data/homebrew/PS5MEDIC/`; **the file with the higher `db_version` applies**, on a tie the one in `knowledge/` (since 13 Nov 2026; before that `knowledge/` always took precedence, and an old copy from `make deploy-ui` hid the new delivery: 40 instead of 153 codes). `make launch-ui` overwrites the file in `knowledge/` with the state of the repository, which is the development route. **Whoever changes rules must increment `db_version`**, otherwise an older copy with the same number wins.

## 2. Unknown error lines and issue template

`unknown_error_lines` in `libmedic/src/report.cpp`. The report (□) gets a section **Unknown error lines**: lines of the ShadowMount+ log that look like errors but belong to **no finding**.

The detection is deliberately narrow. Measured on the real log of the test console (10,163 lines): a rough "error/fail/crash" would have returned 85 lines, almost all noise ("game error monitoring", `error=0` on successful unmounts, the game title "Jump-and-Run"). Therefore only these count:
- error words `failed, failure, panic, fatal, cannot, can't, unable, denied, corrupt(ed), timed out, aborted, exception, invalid` (case-insensitive),
- `error:`/`error=` with a value **not equal to 0** (also `0x16`),
- the capitalized word `ERROR`.

Lines that already appear as evidence of a finding in the report are dropped. Similar lines form a group: without time, numbers and identifiers (hex strings of 6 or more characters with a digit) as `#`. Up to 10 groups are shown, most frequent first, with the first line as an example. On the real log, exactly the two real line types remain (`image source cache update failed` for the renamed "Dirt" images).

`.github/ISSUE_TEMPLATE/problem.md` (bilingual): description, expectation, setup, paste report, unknown lines. The note there asks to read the report before sending, because file and folder names are included.

## 3. Testers

`docs/en/TESTER.md`: what is being looked for (firmware matrix), steps, privacy, what the maintainers do with a snapshot (synthetic fixture, never real names), how to try the rule update, plus a **draft** for a forum post. The draft is not published and has a placeholder for the release link.

## Open

- **Posts** on GBAtemp/Discord: dropped for now (decision of 8 Nov 2026; the repository has been public since 16 Nov 2026, forum posts only on Marc's instruction). The draft in `docs/en/TESTER.md` remains as a template.
- **GitHub release:** since 16 Nov 2026 `v0.4.0` (repository public). License: GPL-3.0-or-later (see `docs/en/PHASE4.md`). The issue template only makes sense in a repository with issues.
- **Tester fixtures** only come into being once there are reports.
