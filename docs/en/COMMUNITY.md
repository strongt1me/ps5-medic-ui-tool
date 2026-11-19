# Community: submitting reports, new rules, rule updates

[Deutsch](../COMMUNITY.md) · **English**

As of 12 Nov 2026. This is how new rules arise from errors that other users run into. **None of this changes the app without review**: the console only sends raw material; a rule is written, checked and signed by the maintainer (Marc).

```
User: report (□)  ->  issue in the repository  ->  bot pre-sorts  ->  maintainer reviews  ->  rule + fixture + test + translations
     ->  sign rules.json (make sign-rules)  ->  User: Update rules (More)
```

## 1. Submitting a report

The □ button in the findings list saves the report (`/data/ps5medic/reports/`) and shows a QR code. The report is anonymized (IP/MAC, account IDs, serial numbers). **File and folder names stay in, including game names**: read it before pasting and strike them out. The text goes into an issue using the "Report a problem" template. The "Unknown error lines" section is the most important one: these are lines in the ShadowMount+ log that look like errors and match no rule.

An "upload" button in the app is **not built yet**. It would need an intermediate server (a token in the app could be read by anyone), a public repository for reports, HTTPS on the console and an explicit decision to extend the network rule (today: network only for the rule download). Until then: copy the report and paste it into the issue.

## 2. Pre-sorting (bot)

`.github/workflows/triage.yml` runs on every new or edited issue and calls `tools/triage_report.py`:

- reads the report **as data** (the issue text reaches the tool only via an environment variable, never in a shell line; mentions and HTML are defused, lengths are limited),
- writes a comment: firmware, model, versions, findings, unknown lines, labels set (`bericht`, `neue-zeilen`, `sauber`, `ohne-bericht`),
- forms a **fingerprint** for each unknown line (numbers, times, paths and title IDs removed) so that similar lines from different reports collapse together,
- creates an issue "Unbekannte Fehlerzeilen: Rangliste" on Mondays (label `rangliste`): in how many reports, on which firmwares.

The bot only has the permissions `contents: read` and `issues: write`, no secrets, and runs nothing from the issue. No AI is called. GitHub creates the labels `bericht`, `neue-zeilen`, `sauber`, `ohne-bericht` and `rangliste` itself the first time they are set.

## 3. Turning a line into a rule (maintainer)

From two to three independent reports with the same fingerprint, a rule is worthwhile:

```bash
gh issue list --state all --limit 500 --json number,title,body,url > issues.json
python3 tools/triage_report.py cluster issues.json                   # ranking
python3 tools/triage_report.py draft   issues.json <fingerabdruck>   # rule draft (JSON with TODO fields)
```

The draft is only a scaffold (id `community.<hash>`, condition on `smp.log.lines`, status `todo`). The rule is finished only with the usual obligations (`README.md`, "Contributing"): **primary source** (issue, README, source code; not just a report), plain-language text in German and English (plus French, Italian, Spanish in `knowledge/l10n/`, `tools/l10n_check.py`), a **synthetic** fixture and `expect.json` (no real file or game names from a report), a test, and a fix only if it is reversible. The text policy applies: never recommend a system software update.

## 4. Publishing

Increment `db_version` in `knowledge/rules.json`, run `make sign-rules`, and provide the four files (`rules.json`, `error_codes.json`, each `.sig`): today on an http server (`make serve-rules` or a web space) or as files to copy onto the console. The signature ensures that only rules from the maintainer are accepted, no matter where they come from. See `docs/en/PHASE5.md`.

## What never happens

- No rule is created automatically from a report.
- The bot does not execute any text from reports, calls no network other than GitHub itself, and changes neither code nor rules.
- Reports are collected only in the project's repository; the console sends nothing on its own.
