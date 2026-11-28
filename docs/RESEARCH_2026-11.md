# Recherche: Fehlerberichte der PS5-JB-Szene (Stand 28. Nov. 2026)

Kandidaten für neue Regeln in `knowledge/rules.json`. Nichts davon ist schon eine Regel.
Bereits vorhandene Regeln (49, siehe `docs/RULES.md`) wurden ausgeklammert, außer es gibt neue Infos dazu.

Vertrauen: **doc** = vom Entwickler/Maintainer dokumentiert oder bestätigt · **community** = Nutzer-Lösung, mehrfach berichtet · **unsicher** = Einzelbericht oder Ursache offen.
Spalte „Auto-Fix": ob Medic das reversibel (Backup + Undo, nie löschen) reparieren könnte.

Grenzen aus CLAUDE.md gelten weiter: Keine Exploit-Inhalte. Die Einträge in Abschnitt 1 beschreiben nur Symptome, Konfiguration und Abhilfe.

---

## 1. Jailbreak-Kette, Autoloader, Loader

### 1.1 Payload Manager startet nicht, weil autoload.txt existiert
- Symptom: Der Payload Manager erscheint nach dem Jailbreak nicht.
- Betroffen: itsPLK ps5-bdjb-/y2jb-/lua-autoloader (Unified Autoloader).
- Ursache (doc): Ohne `autoload.txt` startet der Payload Manager automatisch, mit `autoload.txt` nicht.
- Erkennung: `ps5_autoloader/autoload.txt` existiert, `pldmgr.elf` steht nicht darin.
- Behebung: Empfohlen wird, `pldmgr.elf` als einzigen Eintrag in `autoload.txt` zu setzen. Alternativ `autoload.txt` entfernen.
- Auto-Fix: ja (Zeile `pldmgr.elf` ergänzen, mit Backup).
- Quelle: https://github.com/itsPLK/ps5-bdjb-autoloader, https://github.com/itsPLK/ps5_lua_autoloader · Vertrauen: doc

### 1.2 Payload in autoload.txt nicht gefunden (Groß-/Kleinschreibung)
- Symptom: Ein Payload aus der Liste wird stumm übersprungen.
- Ursache (doc): Dateinamen unterscheiden Groß- und Kleinschreibung.
- Erkennung: Jeden Eintrag in `autoload.txt` mit den Dateien im selben Ordner vergleichen. Ein Treffer nur ohne Beachtung der Schreibweise bedeutet: Tippfehler.
- Behebung: Eintrag an den echten Dateinamen anpassen.
- Auto-Fix: ja (Eintrag korrigieren).
- Quelle: https://github.com/itsPLK/ps5_lua_autoloader · Vertrauen: doc
- Hinweis: Ergänzt die vorhandene Regel `autoload.entry_missing`.

### 1.3 Suchreihenfolge der Autoloader-Konfiguration / veraltete Kopie gewinnt
- Symptom: Änderungen an `autoload.txt` wirken nicht.
- Ursache (doc): Gesucht wird in dieser Reihenfolge: `/mnt/usb?/ps5_autoloader`, dann `/data/ps5_autoloader`, dann `/mnt/disc/ps5_autoloader`. Ein alter USB-Stick überdeckt also `/data`. Seit y2jb-autoloader v0.9 wird `autoload.txt` im YouTube-Cache nicht mehr gelesen. `@sync` kopiert die Konfiguration von USB nach `/data`. Ein Update kommt über `/mnt/usb?/ps5_autoloader_update.zip` und überschreibt `/data/ps5_autoloader`.
- Erkennung: Mehrere `ps5_autoloader`-Ordner gefunden. Melden, welcher aktiv ist (erster in der Reihenfolge) und ob sich die Inhalte unterscheiden. Eine `autoload.txt` im YouTube-Cache-Pfad ist veraltet.
- Behebung: Nur einen Ort pflegen oder `@sync` nutzen.
- Auto-Fix: nein (Hinweis).
- Quelle: https://newreleases.io/project/github/owendswang/BD-UN-JB-Poops-Autoloader/release/v2.2, https://newreleases.io/project/github/itsPLK/ps5-y2jb-autoloader/release/v0.9-2b2e5f6 · Vertrauen: doc

### 1.4 Eigener elfldr ohne Wartezeit danach
- Symptom: Payloads nach `elfldr.elf` kommen nicht an.
- Ursache (doc): Der Loader braucht nach dem Start Zeit. Die README nennt `!4000` direkt danach.
- Erkennung: In `autoload.txt` steht `elfldr.elf`, und die nächste Zeile ist kein `!<ms>`.
- Hinweis: Das deckt schon die vorhandene Regel `autoload.elfldr_without_delay` ab. Der Wert 4000 ms ist jetzt belegt.
- Quelle: https://github.com/itsPLK/ps5_lua_autoloader · Vertrauen: doc

### 1.5 Port 9021 lehnt Verbindungen von anderen Geräten ab
- Symptom: „Connection refused“ beim Senden vom PC, obwohl elfldr läuft.
- Ursache (doc): Der Standard-elfldr der Autoloader akzeptiert nur localhost.
- Erkennung: Port 9021 nimmt auf 127.0.0.1 an, aber nicht auf der LAN-Adresse.
- Behebung: Einen eigenen elfldr mit Netzwerkzugriff über autoload laden.
- Auto-Fix: nein.
- Quelle: https://github.com/itsPLK/ps5-bdjb-autoloader · Vertrauen: doc
- Hinweis: Das verfeinert die vorhandene Regel `payload.port_9021_closed` und passt zu CLAUDE.md, wo 9021 nach dem Unified JB nicht lauscht.

### 1.6 Kernel-Panic durch Payloads, die zu früh geladen werden (alte Autoloader-Versionen)
- Symptom: Kernel-Panic beim Laden bestimmter Payloads oder beim späteren Spielstart.
- Betroffen: y2jb-autoloader vor v0.9 (lädt, während YouTube noch läuft) und bdjb-autoloader vor v1.4.0 (lädt, während der Disc Player läuft).
- Ursache (doc, laut Autor „should be resolved"): Payloads werden geladen, solange die Einstiegs-App aktiv ist. Der Payload Manager hält etaHEN und kstuff zurück, bis die App geschlossen ist.
- Erkennung: Autoloader-Version, falls lesbar, z. B. aus dem Release-Ordner oder Log. Sonst nur Nutzerangabe.
- Behebung: Autoloader aktualisieren und `pldmgr.elf` als einzigen Autoload-Eintrag verwenden.
- Auto-Fix: nein.
- Quelle: https://newreleases.io/project/github/itsPLK/ps5-y2jb-autoloader/release/v0.9-2b2e5f6, https://newreleases.io/project/github/itsPLK/ps5-bdjb-autoloader/release/v1.4.0-c27881d · Vertrauen: doc

### 1.7 Zu viele Autoload-Payloads → Kernel-Panic
- Symptom: Wiederholte Panics direkt nach dem Jailbreak.
- Ursache (community): Einer der Payloads ist instabil oder passt nicht zur Firmware.
- Erkennung: Anzahl der Einträge in `autoload.txt` bzw. in der Payload-Manager-Liste. Eine Panic-Häufung aus dem Bericht bzw. der Nutzerangabe.
- Behebung: Autoload auf ein Minimum reduzieren, z. B. elfldr, kstuff-lite und SM+. Dann Payloads einzeln wieder hinzufügen, um den Auslöser zu finden.
- Auto-Fix: nein (Anleitung). Optional reversibles Auskommentieren mit Undo.
- Quelle: https://jab-guyver.co.kr/7425 (9. Sep. 2026) · Vertrauen: community

### 1.8 Bekannte Kernel-Panics in alten Y2JB-/P2JB-Versionen
- Symptom: Panic beim Starten eines PS5-Dumps oder beim Schließen der YouTube-App.
- Betroffen: Y2JB vor 1.5 (Dump-Start, Ursache im kexp) und P2JB vor 2.3 (Schließen von YouTube).
- Behebung: Neuere Version verwenden. P2JB braucht rund 40–50 Minuten mit der Anzeige „Running kernel exploit…“. Das ist normal und kein Hänger. Geht die Konsole nach etwa 50 Minuten aus, war es eine Panic.
- Erkennung: nur Nutzerangabe bzw. Versionsangabe.
- Auto-Fix: nein.
- Quelle: https://newreleases.io/project/github/owendswang/Y2JB-P2JB-bdj_unpatch/release/v1.3, https://jab-guyver.co.kr/7425 · Vertrauen: doc

### 1.9 BD-UN-JB-Poops-Autoloader v2.2: falscher Dateiname in der ISO
- Symptom: kstuff wird nicht geladen.
- Ursache (doc): Upload-Fehler in v2.2 („wrong stuff file name“). Korrigiert in v2.2b; der kexp-Fehler ist in der Version für poops v1.8 behoben.
- Erkennung: nur Nutzerangabe.
- Behebung: v2.2b oder neuer.
- Quelle: https://newreleases.io/project/github/owendswang/BD-UN-JB-Poops-Autoloader/release/v2.2 · Vertrauen: doc

### 1.10 Kein Netzwerk-Interface auf FW 10.20–13.60
- Symptom: Exploit-Auswahl oder Verbindung schlägt fehl.
- Ursache (doc): Auf diesen Firmwares braucht der Lua-Autoloader eine aktive Netzwerkschnittstelle (WLAN oder LAN). Internetzugang ist nicht nötig.
- Erkennung: nur Nutzerangabe. Medic läuft erst nach dem Jailbreak.
- Hinweis: Widerspricht teilweise dem Community-Tipp „Netzwerk aus“ bei P2JB (FW 11.00/12.40). Beides ist je nach Kette verschieden.
- Quelle: https://github.com/itsPLK/ps5_lua_autoloader · Vertrauen: doc

### 1.11 Y2JB: YouTube-Update / appinfo.db-Eingriff → Datenbankschaden
- Symptom: Nach einem Eingriff in `appinfo.db`, der das YouTube-Update blockieren soll, verschwinden alle fPKGs und Spielstände.
- Ursache (doc, Warnung in der README): Die Änderung an der Systemdatenbank kann die Datenbank beschädigen.
- Erkennung: Zeitstempel bzw. Größe von `/system_data/priv/mms/appinfo.db` nur lesen. Bei fehlenden Einträgen auf eine Panic oder einen DB-Schaden hinweisen.
- Behebung: Vorher Spielstände sichern und YouTube schließen. Nach dem Schaden: Safe Mode → „Rebuild Database“.
- Auto-Fix: nein (System-DB nie anfassen).
- Quelle: https://github.com/Gezine/Y2JB (archiviert am 9. Nov. 2026) · Vertrauen: doc

### 1.12 Wiederherstellung nach Kernel-Panic / Safe-Mode-Hänger
- Abhilfe (community): Komplett ausschalten und den Strom kurz trennen. Danach im Safe Mode „Clear System Software Cache“, dann bei Bedarf „Rebuild Database“. Bleibt die Konsole im Safe Mode hängen, hilft laut Autor nur ein vollständiger Reset (Datenverlust).
- Medic-Nutzen: Text für den Bericht bzw. die Hilfeseite nach einer Panic-Erkennung.
- Quelle: https://jab-guyver.co.kr/7425, https://newreleases.io/project/github/owendswang/BD-UN-JB-Poops-Autoloader/release/v2.2 · Vertrauen: community

### 1.13 Relapse-/WebKit-Autoloader: mitgelieferte Payloads entfernt
- Info: Laut GBAtemp-Thread wurde die Relapse-Payload-Option entfernt, weil ihre Payloads instabil waren. Nutzer müssen kstuff-lite 1.11 und SM+ selbst bereitstellen.
- Medic-Nutzen: Versionsprüfung von kstuff-lite und SM+ ist bei diesem Setup besonders wichtig.
- Quelle: https://gbatemp.net/posts/10916515/ (nur Suchergebnis, die Seite liefert 403) · Vertrauen: unsicher

### 1.14 Firmware-Zuordnung (für die Plausibilitätsprüfung)
- Lapse bis 10.01 (gepatcht in 10.20). Darüber braucht es einen anderen ELF-Loader auf 9021.
- UMTX2 1.00–5.50.
- Y2JB 4.03–12.40 mit YouTube 01.000.003, ab 12.60 YouTube 01.000.030.
- Relapse laut Presse 7.00–13.60, nicht 14.00. Tethered, also nach jedem Neustart neu laden.
- Medic-Nutzen: Wenn FW und erkannte Kette nicht zusammenpassen, Hinweis geben.
- Quelle: https://github.com/Gezine/Y2JB, https://github.com/idlesauce/UMTX2, https://wololo.net/2025/05/10/ps5-ps4-lapse-kernel-exploit-released-compatible-up-to-ps4-12-02-and-ps5-10-01-but/ · Vertrauen: doc (Presse: unsicher)

---

## 2. Spiele laden: ShadowMount+, Dumps, Backports

Stand: Die neueste SM+-Version ist 1.7beta12 vom 08.11.2026. Ein 1.7 final gibt es nicht. Vom Maintainer bestätigt sind nur #156, #159, #169, #198 und #201.

### 2.1 Ungültiger Konfigurationswert → SM+ 1.7beta12 beendet sich ohne Meldung
- Symptom: Der Payload Manager meldet „loaded“, aber es läuft kein Prozess und es kommt keine Benachrichtigung. Im klog steht `shadowmountplus.elf calls exit() exit_value=1`.
- Ursache (doc): beta12 bricht bei einem ungültigen Wert ab, bevor `sm_notifications_init()` läuft. Die Meldung `[CFG] Configuration could not be loaded/migrated: ... Original files retained.` geht nur nach stdout.
- Erkennung: In `/data/shadowmount/debug.log(.1)` nach `[CFG] invalid` suchen, z. B. `[CFG] invalid scan depth at line 2: scan_depth=3 (range: 1..2)`. Die Zeilennummer stimmt nicht mit config.ini überein. Außerdem `[scan] depth` außerhalb von 1..2 und der Prozess `shadowmountplus.elf` fehlt.
- Behebung: Ungültigen Wert auf den Standard setzen, z. B. `depth=2`, und SM+ neu laden.
- Auto-Fix: ja (`smp_set_key` mit Backup).
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/231, /236 (24.–27.11.2026) · Vertrauen: doc
- Hinweis: Neue Info zu `smp.config_rejected`.

### 2.2 Ordner-Dump ohne Ausführungsrechte (errno 13)
- Symptom: „Can't start the game or app“ mit CE-107750-0. Im SM+-Log steht `launch mount ready`, dann `runtime release start`, aber kein `[GAME] started`.
- Betroffen: FW 13.20/13.60, SM+ 1.7beta10–12. Ordnerspiele, die per FTP, aus einem RAR oder per lokaler Kopie kamen (Rechte 0644). Über SMB kopierte Dateien bekommen 0777.
- Erkennung: Mit stat() das x-Bit von `eboot.bin`, `sce_module/*`, `Media/Modules/*`, `Media/Plugins/*` und `fakelib/*` prüfen. Im klog `error=0x80aa001a errno=13` bzw. `launchApp: LNC_ISOK::0x80aa001a`.
- Behebung: Spielordner rekursiv auf 0755 setzen. PR #226 ist nicht gemergt.
- Auto-Fix: **ja**. `chmod -R 0755`; der Journal-Eintrag speichert die alten Modi für das Undo.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/189, /219 · Vertrauen: community

### 2.3 rtld „mount flag / attribute error“
- Symptom: `[rtld] ERROR self_load_shared_object:2644: mount flag / attribute error (path=/app0/Media/Modules/…prx)`.
- Betroffen: FW 13.60, SM+ 1.7beta11/12, Unity-Ordnerspiele.
- Ursache: Teilweise fehlende Ausführungsrechte (dann hilft 0755). In anderen Fällen ist sie offen.
- Erkennung: Pfad aus der Zeile ziehen, dann Rechte und Schreibweise dieser Datei prüfen.
- Behebung: 0755 setzen. Bleibt der Fehler, das Spiel als Image (.exfat/.ffpfsc) testen und einen klog über klogsrv (Port 3232) erfassen.
- Auto-Fix: teilweise (nur die Rechte).
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/189, /192 · Vertrauen: community/unsicher

### 2.4 Groß-/Kleinschreibung von Modulnamen auf internem UFS
- Symptom: SIGSEGV etwa 0,4 s nach `EXEC /app0/eboot.bin` mit `rip: 0`. Als .ffpfsc oder auf exFAT läuft dasselbe Spiel.
- Betroffen: SM+ 1.7beta3–5fix1 bis heute, FW 13.00–13.60.
- Ursache (doc): UFS unterscheidet die Schreibweise. Im Dump liegt `Il2cppUserAssemblies.prx`, geladen wird `Il2CppUserAssemblies.prx`. Die Case-Reparatur von SM+ greift auf nullfs-Ordnern nicht; `link()` auf `/data` liefert EOPNOTSUPP (#229).
- Erkennung: `Media/Modules/Il2cppUserAssemblies.prx` existiert, `Il2CppUserAssemblies.prx` nicht.
- Behebung: Datei umbenennen (Maintainer: „Rename it“) oder das Spiel als Image bzw. auf exFAT betreiben.
- Auto-Fix: **ja** (rename(), nicht link(); reversibel).
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/159, /229 · Vertrauen: doc

### 2.5 requiredSystemSoftwareVersion bzw. sdkVersion über der Konsolen-FW
- Symptom: CE-108255-1 mit `0xA0020101 (PRX_NOT_RESOLVED_FUNCTION)` etwa 6 s nach dem Start. Mount und Registrierung laufen normal.
- Ursache (community, gut begründet): Das Update verlangt eine höhere System-FW als die Konsole hat. Eine nicht gestubbte System-Lib-Funktion fehlt.
- Erkennung: In `sce_sys/param.json` `requiredSystemSoftwareVersion` und `sdkVersion` mit der FW vergleichen. Prüfen, ob `/data/homebrew/backports/<ID>/` oder `<spiel>/fakelib/` existiert.
- Behebung: Ein passendes Backport-Set verwenden und Sets nicht mischen. Oder das Basisspiel ohne das zu neue Update spielen.
- Auto-Fix: nein (Warnung). Medic verändert keine Spielinhalte.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/197, /225, https://github.com/BestPig/BackPork · Vertrauen: community

### 2.6 Fakelib „Full“ bricht den Start ab, „Emulators“ läuft (defektes Backport)
- Symptom: CE-108255-1 etwa 1 s nach der Backport-Benachrichtigung.
- Ursache (doc, Maintainer): Defektes Backport bzw. defekte Fakelib im Dump.
- Erkennung: Fakelib-Modus `full` (`[fakelib] default_mode`), Ordner `fakelib/` vorhanden, Spiel beendet sich schnell.
- Behebung: In `profiles.ini` für den Titel `fakelib = emulators` setzen.
- Auto-Fix: ja, mit Rückfrage.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/169, /173 · Vertrauen: doc

### 2.7 Spielstände unter Full-Fakelib nicht erkannt
- Symptom: Load/Continue ausgegraut, nur „New Game“ wählbar.
- Erkennung: `/user/home/<uid>/savedata_prospero/<ID>/sdimg_*` existiert und der Fakelib-Modus ist full.
- Behebung: `fakelib = emulators` für den Titel setzen und Spielstände vorher sichern.
- Auto-Fix: teilweise.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/177, /179 · Vertrauen: community

### 2.8 PlayGo: „remaining game data has not finished downloading"
- Symptom: Fehler nach einem bestimmten Spielabschnitt, danach Rückkehr zum Titelbildschirm.
- Ursache (community): PlayGo-Stub fehlt oder das Chunk-Layout ist falsch.
- Erkennung: `fakelib/libScePlayGo.sprx` bzw. `playgo_stub.dat` fehlen; `sce_sys/playgo-chunk.dat` existiert.
- Behebung: PlayGo-Stub aus drakmor/pgo_stub 0.6 in `fakelib/` legen, `playgo_stub.dat` neben `eboot.bin` anlegen und die Konsole neu starten.
- Auto-Fix: teilweise (Hinweis). Die Stub-Datei liefert Medic nicht mit.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/212 · Vertrauen: community

### 2.9 AMPR/PlayGo-Emulator fehlt
- Erkennung: `/data/shadowmount/emus/` leer, `[emulators] auto_update_ampr` aus, `ampr_emu.index` fehlt im Image-Root.
- Behebung: Emulatoren bereitstellen. Einmal `mount_mode = rw` setzen, starten, danach wieder ro. Notfalls als FPKG installieren.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/209, /213, /178 · Vertrauen: community

### 2.10 libSceAmpr: SELF-Authentifizierung schlägt fehl (Fehler 46) auf 13.60
- klog: `sceSblAuthMgrAuthHeader returned unexpected error 46`, `B: error 46 (libSceAmpr.sprx)`, `0xa0020104 (PRX_PROCESS_STARTUP_FAILURE)`.
- Kein bestätigter Fix. Eine andere libSceAmpr- bzw. ampr-emu-ng-Variante testen. Medic kann Größe und Magic von `fakelib/libSceAmpr.sprx` melden.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/227 · Vertrauen: unsicher

### 2.11 mountppfs error=28 (G6-Metadaten)
- klog: `allocate_pkg_g6metadata_memory() … error=28`, `mountppfs() … error=28`. Vermutlich ist ein Kernel-Pool erschöpft, obwohl genug Platz frei ist.
- Behebung offen: Nur mit elfldr, kstuff und SM+ testen; Medic kann viele residente ELFs melden.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/237 · Vertrauen: unsicher

### 2.12 .ffpfsc mit verschachteltem Member über 128 GiB
- Symptom: `Returned from main with non-zero` etwa 6 s nach dem Start. Als .exfat läuft das Spiel.
- Erkennung: Größe des Members (aus `Mounted (pfsc) ... total=` oder per stat) über 128 GiB → Warnung.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/175 · Vertrauen: unsicher

### 2.13 Ruckler bei großen komprimierten .ffpfsc
- Laut 1.7beta1 ist PFS experimentell; empfohlen ist UFS (.ffpkg) oder .exfat. Ab etwa 100 GB .ffpfsc einen Performance-Hinweis geben.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/160, https://newreleases.io/project/github/drakmor/ShadowMountPlus/release/1.7beta1 · Vertrauen: community

### 2.14 Register failed code=0x80A30008
- Ursache: Laut Maintainer die Konsolen-Datenbank. Laut Community verfälscht FAT32 die Schreibweise der Dateinamen.
- Erkennung: `[REG] FAIL: 0x80a30008` im Log, Quelle liegt auf msdosfs (FAT32).
- Behebung: Safe Mode → „Rebuild Database“ oder USB als exFAT formatieren.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/156, /164 · Vertrauen: doc/community
- Hinweis: Neue Info zu `smp.register_failed`. Der Fehlercode lässt sich jetzt gezielt auswerten.

### 2.15 [REG] FAIL 0x80f50005 (AppInstallAll)
- Ursache (vermutet): Verschachtelter Spiel-Root oder Backport am falschen Ort.
- Erkennung: Auf welcher Ebene liegt `sce_sys/param.json`? Liegt ein Backport außerhalb von `/data/homebrew/backports/<ID>/`?
- Behebung: Root nach `/data/homebrew/<ID>/<ID>-app/` und Backport nach `/data/homebrew/backports/<ID>/` verschieben.
- Auto-Fix: teilweise (verschieben, reversibel).
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/141 · Vertrauen: community

### 2.16 PS4-FPKG: CE-108262-9, wenn etaHEN parallel zu SM+ läuft
- Symptom: Systemfehler CE-108262-9 nach 30–60 Minuten, nur bei PS4-Spielen (`/user/app/CUSA…`).
- Erkennung: etaHEN neben `shadowmountplus.elf` in der Prozessliste.
- Behebung: etaHEN aus dem Autoload entfernen, nur kstuff-lite plus SM+ verwenden. Laut Release Notes ab 1.7beta5 kein etaHEN.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/143 · Vertrauen: community/doc

### 2.17 M.2-Spiele (/mnt/ext1) werden beim Ruhemodus beendet
- Log: `[SLEEP] killing USB-backed game before suspend` mit einem Pfad unter `/mnt/ext1`. Behoben in 1.7beta12.
- Behebung: Auf 1.7beta12 aktualisieren.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/198 · Vertrauen: doc
- Hinweis: Neue Info zu `smp.usb_game_closed_on_rest`.

### 2.18 „[LINK] source eboot.bin missing“
- Log: `[LINK] source eboot.bin missing for <ID>: <pfad>`, `[LINK] nullfs mount failed`. Der Dump ist unvollständig.
- Erkennung: Prüfen, ob `eboot.bin` existiert und größer als 0 ist.
- Behebung: Dump vollständig neu kopieren.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/154 · Vertrauen: community

### 2.19 Verwaister Mount nach PS5 Game Compressor
- Erkennung: Mounts unter `/mnt/shadowmnt/` ohne laufenden Titel.
- Behebung: Titel in SM+ unmounten oder SM+ neu laden.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/188 · Vertrauen: unsicher

### 2.20 Konsolenabsturz beim Öffnen der Details eines Ordnerspiels mit mehr als 220.000 Dateien
- Erkennung: Dateien zählen; ab etwa 100.000 Dateien warnen.
- Behebung: Die Detailseite im Web-UI meiden oder das Spiel in ein Image umwandeln.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/205 · Vertrauen: community

### 2.21 ps5-app-dumper 2.x: 99 % / leere pakchunk-Dateien
- Erkennung: 0-Byte-Dateien im Dump.
- Behebung: Eine der folgenden Möglichkeiten:
  - alle Sprachpakete installieren
  - `enable_webui = 0` und `auto_start = 1` setzen und bei laufendem Spiel dumpen
  - Version 1.11 verwenden
- Quelle: https://github.com/EchoStretch/ps5-app-dumper/issues/26, /35, /38 · Vertrauen: community

### 2.22 ps5-app-dumper: doppelte config.ini
- Erkennung: Sowohl `/data/homebrew/ps5-app-dumper/config.ini` als auch `/mnt/usbX/homebrew/…/config.ini` existieren, mit unterschiedlichem `split=`.
- Behebung: Nur eine Config behalten.
- Auto-Fix: teilweise (Duplikat nach `_to_delete/` verschieben).
- Quelle: https://github.com/EchoStretch/ps5-app-dumper/issues/3 · Vertrauen: unsicher

### 2.23 Falsches SELF-Format von eboot.bin
- Erkennung (zuverlässig): Die ersten 4 Byte von `eboot.bin` lesen. `54 14 F5 EE` = PS5-SELF, `4F 15 3D 1D` = PS4-SELF, `7F 45 4C 46` = rohe ELF. Ein CUSA-Titel mit PS5-Magic ist fehlerhaft.
- Behebung: Ohne die FSELF-Funktion des Dumpers neu dumpen.
- Quelle: https://github.com/EchoStretch/ps5-app-dumper/issues/36 · Vertrauen: community

### 2.24 .ffpkg in /data/homebrew → Kernel-Panic / Dump überdeckt Originalspiel
- Erkennung: `*.ffpkg` in `/data/homebrew`; eine Title-ID sowohl in `/user/app` als auch als Dump.
- Behebung: `.ffpkg` auf USB/ext verschieben und `/data/shadowmount` nach Rückfrage sichern bzw. umbenennen.
- Quelle: https://alex-free.github.io/ps5-game-backups-guide/ · Vertrauen: unsicher

---

## 3. HEN-Schicht: etaHEN, kstuff-lite, Plugins, Itemzflow

Bekannte etaHEN-Dateien und -Ports (README/Changelog):
- Config: `/data/etaHEN/config.ini`; der 13.60-Fork nutzt stattdessen `config-multifw-experimental.ini`.
- Log: `/data/etaHEN/etaHEN.log`.
- Ports: FTP 1337, Klog 9081, DPI 9090, DPIv2-WebUI 12800, Discord-RPC 8000.
- Bekannte config-Schlüssel: `PS5Debug, FTP, discord_rpc, toolbox_auto_start, Allow_data_in_sandbox, DPI, DPI_v2, Klog, ALLOW_FTP_DEV_ACCESS, StartOption, Rest_Mode_Delay_Seconds, Util_rest_kill, Game_rest_kill, disable_toolbox_auto_start_for_rest_mode, libhijacker_cheats, launch_itemzflow, testkit, Display_tids, APP_JB_Debug_Msg, etaHEN_Game_Options, auto_eject_disc, overlay_*`.
- OnionHEN (Ableger von etaHEN): `/data/OnionHEN/OnionHEN.log`, `OnionHEN_crash.log`, `config.ini`, `kstuff.elf`. Nützlich für die Regel „zwei HENs gleichzeitig".

### 3.1 etaHEN ab 2.4B startet nicht ohne Payload-SDK-elfldr
- Symptom: Der Payload wird gesendet, aber es kommt weder Benachrichtigung noch Toolbox.
- Ursache (doc): Laut Changelog 2.4B ist der Payload-SDK-ELFLDR jetzt Pflicht („REQUIRED").
- Erkennung: Dateien von etaHEN ab 2.4B sind vorhanden, aber kein elfldr-Prozess läuft und Port 9021 ist zu.
- Behebung: Zuerst elfldr laden, dann etaHEN.
- Auto-Fix: nein.
- Quelle: https://github.com/LightningMods/etaHEN/releases (2.4B) · doc

### 3.2 Offizielles etaHEN 2.5B auf FW ab 11.60: Toolbox fehlt
- Symptom: Die Benachrichtigung kommt, die Toolbox nicht, oder es passiert gar nichts.
- Ursache (community): Das offizielle etaHEN ist auf diesen Firmwares inkompatibel.
- Erkennung: FW ab 11.60 und offizieller Build (keine Fork-Config vorhanden).
- Behebung: Den Fork GronedWaffel/etahen-13.60 (Unified 11.00–13.60) verwenden oder nur kstuff-lite plus SM+ laden.
- Quelle: https://github.com/etaHEN/etaHEN/issues/84, /89, /90 · community
- Hinweis: Ergänzt die vorhandene Regel A1.

### 3.3 Veraltete config.ini nach etaHEN-Update
- Ursache (doc): Changelog 2.3B: „delete your config to reflect new default values". Es gibt keine Migration der alten Datei.
- Erkennung: `/data/etaHEN/config.ini` gegen die bekannte Schlüsselliste abgleichen (fehlende oder unbekannte Schlüssel).
- Behebung: Datei sichern, etaHEN legt sie neu an, dann eigene Werte übernehmen.
- Auto-Fix: **ja** (nach `config.ini.bak` umbenennen, Undo möglich).
- Quelle: https://github.com/LightningMods/etaHEN · doc

### 3.4 Vergessene Steuerdateien `no_kstuff` / `toolbox_auto_start`
- Symptom: fPKG/fSELF, Cheats oder Plugins gehen nicht, oder die Toolbox startet nicht automatisch.
- Ursache (doc): Eine dieser Dateien liegt noch herum:
  - `/data/etaHEN/no_kstuff` oder `/mnt/usb*/no_kstuff` verhindert, dass kstuff geladen wird.
  - `/mnt/usb*/toolbox_auto_start` schaltet den Toolbox-Autostart ab.
- Erkennung: Existenz dieser Dateien prüfen.
- Auto-Fix: **ja** (nach Rückfrage in `*.disabled` umbenennen).
- Quelle: etaHEN-Releases 2.0b und 2.1B · doc

### 3.5 Toolbox-Injektion scheitert sporadisch (2.5B, FW 3.xx/4.xx)
- Symptom: „failed to load eta hen toolbox". Seit 2.3B wird ShellUI nach 10 s Fehlschlag neu gestartet.
- Behebung: etaHEN über Itemzflow starten oder bei Browser-Einstieg den Browser vorher schließen. Nie doppelt injizieren, sondern neu starten.
- Erkennung: etaHEN läuft, aber es gibt keinen Toolbox-Eintrag. In `etaHEN.log` nach Injektionsfehlern suchen.
- Quelle: https://github.com/etaHEN/etaHEN/issues/78, /94 · community

### 3.6 etaHEN-Menü nach Ruhemodus weg
- Erkennung: Diese Schlüssel lesen: `Rest_Mode_Delay_Seconds`, `disable_toolbox_auto_start_for_rest_mode`, `toolbox_auto_start`.
- Behebung: `disable_toolbox_auto_start_for_rest_mode=1` setzen oder die Verzögerung auf 10–20 s erhöhen. Sonst nach dem Aufwachen neu jailbreaken.
- Auto-Fix: teilweise (Schlüssel setzen; Wirkung unbestätigt).
- Quelle: https://github.com/GronedWaffel/etahen-13.60/issues/4, etaHEN 2.0b · community/unsicher

### 3.7 Spiel nach Suspend/Resume kaputt (Mount weg)
- Symptom: Nach dem Aufwachen fehlen Texturen, das Spiel crasht oder Debug-Text bleibt auf dem Bild.
- Erkennung: Ein Spielprozess läuft ohne zugehörigen Image-Mount; `Game_rest_kill` lesen.
- Behebung: Spiele vor dem Ruhemodus schließen oder `Game_rest_kill=1` setzen.
- Auto-Fix: teilweise.
- Quelle: https://github.com/etaHEN/etaHEN/issues/74 · community

### 3.8 PS4-fPKG auf 13.42: harter Absturz nach ~1 s (kstuff-lite 1.11)
- klog-Zeilen:
  - `PrepareProcessLaunchPkg() ret = 80a4000c`
  - `trophyCheck() ret = 0x805516b8`
  - danach `EXEC … abi=ps4` und Abbruch
- Ursache (vermutet): Der ShellCore-Patch hat nur Offsets für 13.60.
- Behebung: Auf 13.42 keine PS4-fPKGs starten, bis ein passender kstuff-Build erscheint.
- Hinweis: `[PFAuthClient] 0x8041013d` ist bei per DNS blockiertem PSN normal; nicht als Fehler werten.
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/102 · community

### 3.9 Ruhemodus hängt nach PS4-fPKG (13.42)
- Erkennung: `/data/shadowmount/debug.log` endet nach `MAIN_ON_STANDBY` / `SUSPEND_ON_GOING`, danach Kaltstart. Ein CUSA-Titel ist installiert.
- Behebung: Nach PS4-fPKG-Sessions ausschalten statt Ruhemodus.
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/110 · community

### 3.10 PS4-Homebrew startet auf 13.60 nicht (0x80A40086), kstuff-lite 1.11
- Betroffen: Itemzflow, PS5-Xplorer, FPKGi und ähnliche Tools starten nicht.
- klog-Zeilen:
  - `CheckPrepareProcessLaunchPkgApp() ret = 80a40086`
  - `preLaunchCheck: LNC_ISOK::0x80a40086`
- Ursache (vermutet, per Byte-Vergleich gestützt): Im veröffentlichten v1.11-ELF fehlen die PS4-Launch-Gate-Patches.
- Behebung: Neueren kstuff-Build verwenden oder PS5-native Varianten der Tools nutzen.
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/103 · community

### 3.11 PS5-fPKG auf 13.00/13.60: „Can't start game or app" (PFAuthClient)
- klog-Zeilen:
  - `pf_auth_client_request_for_ppr.cpp(464) Failed(ret=0x8041013d)`
  - `sceFsMountPprPkg() ret = 80020016`
  - `launchApp: LNC_ISOK::0x80020016`
- Behebung: Bisher keine. Ordner- oder Image-Mount über SM+ statt installiertem fPKG nutzen.
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/96 · community

### 3.12 Installiertes fPKG auf 13.60: Schwarzbild / Kernel-Crash, CE-100005-6
- Ursache (vermutet): Das Exec-Gate lehnt den Paid-Wert ab (`0xa002030a`).
- Behebung: Image- oder Ordner-Mount statt Installation.
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/104 · unsicher

### 3.13 kstuff-lite `patch_shellui failed` → Spielabstürze, Trophäen fehlen
- Logzeilen:
  - `Failed to get libSceNpTrophy handle, patch_shellui failed`
  - `shellui_patch.c:746:sceKernelGetAppInfo: Broken pipe`
- Folgen: CE-108255-1 / `GPU_BREAK_ASYNC` nach 4–5 Minuten oder nach Autosaves. Ein Savedata-Remount schlägt mit `0x809f8032` fehl.
- Ursache (vermutet): `NOTE_EXEC` matcht die Title-ID `NPXS40087`, die WebKit-Helfer (`SceNKWebProcess`) mit ShellUI teilen.
- Erkennung: Logzeilen prüfen; laufende `SceNKWebProcess` in der Prozessliste.
- Behebung: Browser vor dem Spielstart schließen (abgeleitet). kstuff während des Spiels pausieren (SM+-Auto-Toggle, auf 13.60 aber Regel B3 beachten).
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/95, /106 · community

### 3.14 PS4-Spiel über SM+ auf 13.60: SIGSEGV nach 0x80A40005
- Ursache (doc): Fehlender ShellCore-Patch, gemergt in PR #99 am 20.10.2026. Das Release 1.11 vom 20.09. enthält ihn nicht.
- Erkennung: Im klog `0x80A40005` in Launch-Context-Abfragen, dann eboot-SIGSEGV. kstuff-lite ist das 1.11-Release.
- Behebung: kstuff-lite-Build mit PR #99 verwenden.
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/98 · doc

### 3.15 fPKG auf 11.60 hängt bei „Please wait…"
- Behebung (community): Payload „Titan" statt kstuff-lite verwenden oder auf ein Update warten.
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/100 · community

### 3.16 kstuff-Test-Builds im Autoload (z. B. `kstuff-1.15-dr-test3`)
- Symptome:
  - auf 13.00: `GPU_FAULT_WAVEFRONT_ERROR_ASYNC (0xa0d0c012)`
  - auf 10.20: `mountppfs() error=28` (siehe 2.11)
- Erkennung: Dateiname des kstuff im Autoload enthält `test` oder `dr`.
- Behebung: Zurück zur stabilen Version.
- Auto-Fix: nein (Hinweis „Test-Build erkannt").
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/114, /115 (27.11.2026) · community

### 3.17 kstuff-lite auf FW 1.xx/2.xx → Kernel-Panic
- Behebung: Dort etaHEN mit Byepervisor-Pfad verwenden statt kstuff-lite.
- Erkennung: FW unter 3.00 und kstuff-lite im Autoload.
- Auto-Fix: teilweise (kstuff aus dem Autoload nehmen, reversibel).
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/101 · community/unsicher

### 3.18 PS1-/PS4-fPKG wird nach Minuten schwarz, fremdes Icon am Titel
- Ursache (vom Reporter durch die Lösung bestätigt): Korrupter Cache bzw. korrupte Datenbank, nicht kstuff.
- Behebung: Im Safe Mode „Clear Cache and Rebuild Database".
- Erkennung: nur Nutzerangabe.
- Quelle: https://github.com/EchoStretch/kstuff-lite/issues/90 · community

### 3.19 itemzflow_xml_patches deaktiviert sich selbst
- Erkennung:
  - `/data/etaHEN/plugins/itemzflow_xml_patches.plugin` vorhanden?
  - `/data/etaHEN/patches/xml/<TITLEID>.xml` vorhanden?
  - `Allow_data_in_sandbox=1` gesetzt?
- Behebung: Plugin neu anlegen lassen (Itemzflow installiert es neu) und `Allow_data_in_sandbox=1` setzen.
- Auto-Fix: teilweise (Plugin verschieben, Schlüssel setzen).
- Quelle: https://github.com/LightningMods/Itemzflow/issues/131, /147, https://github.com/etaHEN/etaHEN/issues/79 · community

### 3.20 Cheats: „Unable to detect patch version"
- Ursache: Die Cheat-Datei passt nicht zur installierten Spielversion.
- Erkennung: `/data/etaHEN/cheats/` mit Title-ID und Version aus param.json abgleichen.
- Hinweis: Ein Reporter meldet, dass Itemzflow 1.14 beim Scan die Spieleliste bzw. Datenbank korrumpiert und bleibt deshalb bei 1.13.
- Quelle: https://github.com/etaHEN/etaHEN/issues/86 · community

### 3.21 Cheat-Update löscht eigene Trainer
- Ursache: Das Update ersetzt `/data/etaHEN/cheats`, ohne zusammenzuführen.
- Auto-Fix: **ja**. Vor dem Update ein Backup von `/data/etaHEN/cheats` anlegen und fehlende Dateien danach wiederherstellen.
- Quelle: https://github.com/etaHEN/etaHEN/issues/83 · community

### 3.22 PS4-FPS-Zähler zeigt nur „LOADING"
- Erkennung: `/data/etaHEN/fps_counter.sprx` fehlt.
- Behebung: Datei bereitstellen (Quelle nicht mehr verfügbar). Der 13.60-Fork hat ein funktionierendes Overlay.
- Quelle: https://github.com/etaHEN/etaHEN/issues/70 · community/unsicher

### 3.23 Itemzflow-Scan: param.json-Parse-Fehler auf erweitertem Speicher
- Logzeilen:
  - `[json.exception.parse_error.101] ... unexpected end of input`
  - `Failed to get param.json for PPSA…`
- Erkennung: Medic parst die param.json unter `/mnt/ext*/` selbst. Sind sie gültig, liegt der Itemzflow-Bug vor.
- Behebung: Scan wiederholen. Ein Neuaufbau von `PS5_Apps.db` hilft nicht.
- Quelle: https://github.com/LightningMods/Itemzflow/issues/129 · community

### 3.24 etaHEN-Fork: Toolbox-Karte fehlt („stage: ownership", „result: -2")
- Erkennung:
  - Fork-Logs `bootstrap-experimental.log`, `toolbox-card-install.json`, `trace.log` (Pfad vermutlich `/data/etaHEN/`).
  - Doppeltes etaHEN oder doppelter kstuff in der Prozessliste.
- Behebung: Nur einmal nach frischem Boot laden, keinen zweiten kstuff starten, sonst auf „13.60 r3" zurückrollen.
- Quelle: https://github.com/GronedWaffel/etahen-13.60/issues/10 · community/doc

### 3.25 Fork 13.60 r3: „This PS4 game or demo is not compatible with PS5."
- Behebung: Neueren Fork-Build (r5 oder Unified) verwenden.
- Erkennung: FW, Fork-Release und kstuff-Version auslesen.
- Quelle: https://github.com/GronedWaffel/etahen-13.60/issues/7, /2 · community

### 3.26 Kernel-Panic bei jedem Mount nach erneutem Jailbreak (alter SM+-Zustand)
- Behebung:
  1. Jailbreak ohne SM+ ausführen.
  2. `/data/shadowmount` sichern bzw. umbenennen.
  3. Betroffene Kachel löschen.
  4. SM+ neu laden.
- Auto-Fix: **ja**, wenn SM+ nicht läuft (umbenennen statt löschen).
- Quelle: https://alex-free.github.io/ps5-game-backups-guide/ · community

Fehlercodes ohne belastbare Homebrew-Quelle: CE-107857-8, CE-100096-6, CE-113212-0. CE-100022-5 kommt in kstuff-lite #91 vor (FW 13.20, unbeantwortet).

---

## 4. Payload Manager, elfldr, websrv, klogsrv

Bekannte Dateien und Ports:
- Payload Manager:
  - Web-UI auf Port 8084; `/version` liefert die Version.
  - `/data/pldmgr/` mit `payloads/{local,repo,.layout}`, `pldmgr_config.txt` und `repo_cache_*.json`.
- elfldr: Port 9021, Prozessname `elfldr.elf`. Injiziert sich per ptrace in `SceRedisServer`.
- websrv: Port 8080, `/hbldr`.
- klogsrv: Port 3232.
- shsrv: Port 2323.
- Homebrew-Pfade für websrv: `/data/homebrew`, `/mnt/usb%d/homebrew`, `/mnt/ext%d/homebrew`. Pro Ordner `eboot.elf`, `sce_sys/icon0.png` und optional `homebrew.js`.

Zu 1.1–1.5 gibt es neue Details:
- Y2JB kennt Ordner pro Spiel, `ps5_autoloader_<TITLE_ID>`, mit Vorrang vor dem allgemeinen Ordner.
- In `autoload.txt` werden Zeilen mit `#`, `!` oder `@` übersprungen. Erlaubt sind nur `.elf` und `.bin`.
- Ob der Payload Manager läuft, zeigt Port 8084.

### 4.1 Payloads direkt nach kstuff scheitern („Loader connection failed")
- Symptom: Beim Kaltstart scheitern immer die 1–2 Einträge nach kstuff. Der Fehler wandert mit der Position, nicht mit dem Payload.
- Ursache (vermutet): Port 9021 ist nach kstuff kurz zu; die feste Pause von 500 ms reicht nicht.
- Erkennung (Log des Payload Managers):
  - `[PLDMGR] Loader connection failed, timed out, or cancelled.`
  - `[Autoload] Missing, failed or cancelled payload: local/<name>`
  - Der Eintrag davor ist kstuff.
- Behebung: `!3000` direkt nach kstuff einfügen.
- Auto-Fix: ja bei `autoload.txt`. Bei der internen Liste des Payload Managers nur, wenn das Format bekannt ist.
- Quelle: https://github.com/itsPLK/ps5-payload-manager/issues/154 · community

### 4.2 Autoload hängt bei etaHEN + kstuff („conflict :ETAHEN - KSTUFFACTIVE")
- Erkennung: Beide stehen im Autoload, der String erscheint im Log und der Payload Manager ist 0.5.x.
- Behebung: Nur einen der beiden laden. Den Payload Manager auf einen Stand nach dem 13.10.2026 aktualisieren.
- Quelle: https://github.com/itsPLK/ps5-payload-manager/issues/98 · community

### 4.3 Payload Manager nach Ruhemodus weg
- Erkennung: `elfldr.elf` läuft, Port 8084 ist zu. Im Log des Autoloaders steht `ELF loader is already active on 127.0.0.1:9021. Skipping kernel exploit and autoload.`
- Behebung: Internetverbindung in den Netzwerkeinstellungen aus- und wieder einschalten, oder das LAN-Kabel kurz ziehen. Der Payload Manager kommt dann sofort zurück.
- Auto-Fix: nein (Hinweis).
- Quelle: https://github.com/itsPLK/ps5-payload-manager/issues/166, /61 · community

### 4.4 Keine „Serving …"-Meldung nach Ruhemodus oder IP-Wechsel
- Ursache (doc, Quellcode): elfldr und klogsrv melden ihre Adresse nur beim Start. Laut README läuft elfldr nach dem Ruhemodus weiter.
- Medic-Nutzen: Selbst eine Benachrichtigung mit aktueller IP und dem Status der Ports 9021, 3232, 8080 und 8084 ausgeben.
- Quelle: https://github.com/ps5-payload-dev/elfldr (socksrv.c), https://github.com/ps5-payload-dev/klogsrv · doc

### 4.5 Payload läuft doppelt
- Ursache (doc): Der grüne Haken im Payload Manager heißt nur „kürzlich gestartet", nicht „läuft". elfldr und klogsrv beenden alte Instanzen selbst; websrv und ftpsrv tun das nicht (der zweite Start scheitert an `bind`).
- Erkennung: Mehrere PIDs mit gleichem Prozessnamen, z. B. 2× websrv oder 2× ftpsrv.
- Auto-Fix: ja, mit Bestätigung (SIGTERM, dann SIGKILL; elfldr und klogsrv auslassen). Prozesse zu beenden ist kein Dateieingriff; trotzdem ins Journal schreiben.
- Quelle: https://github.com/itsPLK/ps5-payload-manager/issues/81, Quellcode von elfldr/klogsrv/websrv · doc

### 4.6 Payload Manager: „FAILED TO SCAN INSTALLED SLOTS"
- Ursache (vermutet): Versionen gemischt oder fremde Einträge unter `/data/pldmgr`.
- Erkennung: Struktur von `/data/pldmgr` prüfen; nicht lesbare oder unerwartete Einträge melden.
- Behebung: `/data/pldmgr` umbenennen, dann pldmgr neu starten; er legt einen sauberen Ordner an.
- Auto-Fix: **ja** (Umbenennen in `/data/pldmgr.bak-<datum>`, Undo möglich).
- Quelle: https://github.com/itsPLK/ps5-payload-manager/issues/151, /168 · community

### 4.7 Payload Manager ab 0.6: ELFs direkt in `/data/pldmgr/payloads/` werden ignoriert
- Ursache (doc, Contributor): v0.6 migriert das Layout einmalig nach `payloads/local` und `payloads/repo`. Ein Downgrade danach wird nicht unterstützt. Ab 0.6 wird der Payload Manager nur noch über das Repository aktualisiert.
- Erkennung: `.elf`-Dateien direkt in `payloads/` statt in `local/` oder `repo/`. Oder ein pldmgr.elf älter als 0.6 im Autoload, obwohl `.layout` existiert.
- Behebung: Eigene ELFs über den Upload der Web-UI hinzufügen.
- Auto-Fix: nein; Medic meldet die verwaisten ELFs.
- Quelle: https://github.com/itsPLK/ps5-payload-manager/issues/122, /125, /132 · community

### 4.8 PS5-Browser zeigt alte Oberfläche des Payload Managers (AppCache)
- Erkennung: `/version` meldet die neue Version, die Nutzerangabe nennt eine alte UI.
- Cache-Dateien: `/user/home/<USERID>/webkit/shell/appcache/ApplicationCache.db` plus `-wal` und `-shm`.
- Behebung: Die drei Dateien entfernen und die Konsole neu starten. `shell/local/` nicht anfassen.
- Auto-Fix: ja, mit Bestätigung (nach `_to_delete/` verschieben).
- Quelle: https://github.com/itsPLK/ps5-payload-manager/issues/136 · unsicher

### 4.9 Repository des Payload Managers: „Could not resolve hostname" (lokaler DNS)
- Erkennung:
  - Log: `Verified download failed: … (Could not resolve hostname)`
  - Prozess `nanodns` läuft; `nanodns.ini` (`bind=`, `bind6=`)
  - Der DNS der Konsole steht auf 127.0.0.x.
- Behebung: Die nanoDNS-Regeln prüfen oder den DNS testweise umstellen.
- Auto-Fix: nein.
- Quelle: https://github.com/itsPLK/ps5-payload-manager/issues/155 · community
- Hinweis: Passt zur vorhandenen Regel `nanodns.log_problem`.

### 4.10 Payload Manager: 403 „Mutation origin or request marker rejected"
- Ursache (doc): Strengere Origin- und CSRF-Prüfung ab v0.6. Ein Reverse Proxy verändert die Origin- und Referer-Header.
- Behebung: Im Proxy die Header entfernen oder `http://<PS5-IP>:8084` direkt aufrufen.
- Erkennung: nur Nutzerangabe. Gut für den Hilfetext.
- Quelle: https://github.com/itsPLK/ps5-payload-manager/issues/128 · community

### 4.11 websrv: Homebrew-Eintrag unvollständig oder Dateinamen in Großbuchstaben
- Erkennung: `eboot.elf` bzw. `sce_sys/icon0.png` fehlen in `*/homebrew/<ordner>/`, oder sie passen nur ohne Beachtung der Schreibweise (`EBOOT.ELF`, `HOMEBREW.JS`).
- Behebung: In Kleinschreibung umbenennen. Die Reload-Schleife durch `HOMEBREW.JS` ist ab websrv 0.28 behoben.
- Auto-Fix: **ja** (umbenennen).
- Quelle: https://github.com/ps5-payload-dev/websrv, /issues/21 · doc
- Hinweis: Gilt auch für den eigenen Eintrag `/data/homebrew/PS5MEDIC/`.

### 4.12 websrv ab 0.28.1 auf FW 9.40: Homebrews stürzen sofort ab
- Ursache (vermutet): Regression durch Commit e67711e; Version 0.28.0 läuft.
- Behebung: websrv 0.28.0 verwenden. Teilweise hilft `chmod 777` auf den Homebrew-Ordner.
- Erkennung: FW 9.40 und websrv ab 0.28.1 laut Banner.
- Quelle: https://github.com/ps5-payload-dev/websrv/issues/48 · community

### 4.13 websrv: Homebrew stürzt ab, weil ein vorheriges noch läuft
- Ursache (doc, README): „Homebrew sometimes crashes when a previous homebrew is still running."
- Erkennung: Ein über `/hbldr` gestartetes Homebrew läuft noch. `/hbldr` antwortet mit 503, wenn der Start fehlschlägt.
- Behebung: Das alte Homebrew beenden. Bei kaputtem Frontend auf niedriger FW den Launcher vom PC öffnen: `http://<PS5-IP>:8080/`.
- Quelle: https://github.com/ps5-payload-dev/websrv · doc
- Hinweis: Relevant für `make launch-ui`.

### 4.14 Falsche elfldr-Version für Einstiegspunkt oder FW
- Bekannte Kombinationen:
  - v0.20.1 auf FW 6.02: Kernel-Panic, behoben in v0.20.2.
  - v0.22.1 mit dem JS-Loader von UMTX2 auf 4.51: „There is not enough free system memory.“ Mit v0.21 geht es.
  - v0.21.x auf 10.01 mit Y2JB: Kernel-Panic, ungeklärt.
- Erkennung (klog):
  - `No heat beat from Redis Server for 5 sec.`
  - `0xa0024402 (SYSTEM_SOFTWARE_TIMEOUT_ASYNC)`
  - `Force abort Redis Server since it has frozen for 31 sec.`
- Behebung: Eine passende elfldr-Version wählen. ELFs per FTP nur im Binärmodus übertragen.
- Quelle: https://github.com/ps5-payload-dev/elfldr/issues/3, /5, /7 · community

### 4.15 Kernel-Panic beim Spiel- oder Homebrew-Start nach Update des BD-JB-Autoloaders (v1.2)
- Behebung: Zurück auf v1.1. Workaround (von Gezine bestätigt): Payloads laden, Konsole in den Ruhemodus versetzen, wieder aufwecken.
- Quelle: https://github.com/ps5-payload-dev/websrv/issues/33, BD-JB v1.2 Release Notes · community
- Hinweis: Ergänzt 1.6.

Sonstiges: ps5upload v5.41.0 meldet, dass sich der Standard-elfldr leicht auf Port 9021 aufhängt; Ausweichweg ist der Payload Manager (https://newreleases.io/project/github/phantomptr/ps5upload/release/v5.41.0).

---

## 5. System, Speicher, Netzwerk, Wartung

### 5.1 Teilweise gepatchte Y2JB-Updatesperre (DB inkonsistent)
- Gepatcht werden vier Stellen:
  - `tbl_appinfo` in `/system_data/priv/mms/appinfo.db`
  - `tbl_contentinfo.AppInfoJson` in `app.db`
  - `/system_data/priv/appmeta/PPSA01650/param.json`
  - `/user/appmeta/PPSA01650/param.json`
- Sollwerte: `VERSION_FILE_URI` bzw. `versionFileUri` = `http://127.0.0.2`, `CONTENT_VERSION` = `99.999.999`.
- Ursache (doc): Läuft der Patch bei offenem YouTube oder während einer Installation, oder bricht er halb ab, wird die DB inkonsistent. YT-Blocker legt kein Backup an.
- Erkennung: Die vier Stellen **nur lesend** vergleichen (Teilpatch) und `PRAGMA integrity_check` auf beiden DBs ausführen.
- Behebung: Vorher Savedata und beide DBs sichern. Bei echtem Schaden das Backup zurückspielen, sonst im Safe Mode „Rebuild Database“.
- Auto-Fix: nur ein Lese-Backup der DBs nach `/data/ps5medic/backups/`. Kein Schreiben in `/system_data` (CLAUDE.md: keine Systemschreibzugriffe; `/system_data` vorher klären).
- Quelle: https://github.com/Gezine/Y2JB, https://gist.github.com/SvenGDK/6525ec4af64e45a847e35af45da46ab4, https://github.com/Epinorr/y2jb-update-blocker · doc

### 5.2 Y2JB-Softlock nach YouTube-Update
- Sollversion: Bis FW 12.40 braucht es YouTube `01.000.003`, ab 12.60 `01.000.030`.
- Erkennung:
  - `contentVersion` in `/user/appmeta/PPSA01650/param.json` passt nicht zur FW.
  - `/user/download/PPSA01650/download0.dat` fehlt.
  - Der DNS-Primärserver ist nicht 127.0.0.2.
- Behebung: Anleitung aus der Y2JB-README.
- Auto-Fix: nein.
- Quelle: https://github.com/Gezine/Y2JB · doc

### 5.3 Move per SM+: Platzmangel und veraltete manual.lst
- Symptom: Der Move bricht bei 99,4 % mit „No space left on device“ ab. Der Overhead liegt real bei etwa 20 %.
- Folge: Danach steht in jedem Scan `[MANUAL] source unavailable`, weil manual.lst nicht aktualisiert wurde.
- Erkennung:
  - freien Platz am Ziel gegen Quellgröße × 1,2 prüfen
  - prüfen, ob die Pfade in `/data/shadowmount/manual.lst` existieren
  - die Logzeile `[MANUAL] source unavailable` suchen
- Auto-Fix: teilweise. Tote Einträge in manual.lst auskommentieren, mit Backup.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/224 · community

### 5.4 Spielwechsel: „Device busy“ (0x80940033)
- Log: `launch mount unavailable: <title> status=16 (Device busy)`.
- Ursache: Eine parallel laufende Streaming-Host-App (`applicationCategoryType: 65536`) belegt den Slot.
- Behebung: Diese App schließen.
- Quelle: https://github.com/drakmor/ShadowMountPlus/issues/216 · community

### 5.5 DNS-Updatesperre wirkungslos (Provider fängt DNS ab)
- Erkennung: Auf der Konsole `manuals.playstation.net` und `*.update.playstation.net` auflösen. Erwartet wird `0.0.0.0` oder `127.0.0.x`.
- Hinweis: Die Prüfung bleibt lokal, also kein Netz nach außen; der eingestellte bzw. lokale Resolver wird gefragt. Mit CLAUDE.md abgleichen.
- Behebung: Eigenen DNS betreiben oder WAN für die PS5 im Router sperren. Auto-Updates aus.
- Quelle: https://gist.github.com/Al-Azif/6982e30a9653f48c88d0f5a6a8778d05 · doc

### 5.6 nanoDNS blockt legitime Homebrew-Dienste
- Ursache: Die Standardregeln (`*.playstation.com`, `*.playstation.net`, `*.psndl.net` → `0.0.0.0`) oder zu breite eigene Regeln treffen auch fremde Domains.
- Erkennung: `/data/nanodns/nanodns.ini` lesen (Regeln, Ausnahmen, Bind). Für die betroffene Domain eine Testabfrage stellen.
- Auto-Fix: ja, eine Ausnahme ergänzen, mit Backup. Die Update-Sperrregeln nie anfassen.
- Quelle: https://github.com/drakmor/nanoDNS · doc

### 5.7 Update-Datei auf USB
- Erkennung: `/mnt/usb*/PS5/UPDATE/PS5UPDATE.PUP` suchen, ohne Beachtung der Groß- und Kleinschreibung.
- Auto-Fix: **ja**, in `PS5UPDATE.PUP.blocked` umbenennen (reversibel). Das schützt vor einem versehentlichen FW-Update, mit dem der Jailbreak-Pfad verloren ginge.
- Quelle: https://www.playstation.com/en-us/support/hardware/update-playstation-system-software-safe-mode/ · doc

### 5.8 PSN-Login scheitert unter FW 14.10 (kein Bann)
- Ursache: Laut Presse sperrt Sony seit dem 01.10.2026 PSN für FW unter 14.10 serverseitig. `WS-116332-6` dagegen deutet auf einen echten Bann.
- Medic-Nutzen: Text zur Einordnung, gilt für den vorhandenen Fehlercode-Katalog.
- Quelle: https://tech-insider.org/ps5-firmware-14-10-psn-jailbreak-lockout-2026/ · community

### 5.9 Offline-Aktivierung fehlt
- Folgen: Remote Play lässt sich nicht koppeln, das Save-Management über USB ist gesperrt.
- Hinweis: Y2JB läuft nicht mit einem PSN-aktivierten Konto.
- Erkennung: `/data/homebrew/OffAct` vorhanden? Konten unter `/user/home/<ID>` auflisten. Den Aktivierungsstatus nur per Nutzerangabe.
- Behebung: Offline-Konto anlegen, mit offact oder dem Toolbox-Menü aktivieren, neu starten.
- Quelle: https://github.com/Gezine/Y2JB, https://github.com/ps5-payload-dev/offact · doc

### 5.10 Savegame-Kopie über USB verlangt PSN-Anmeldung
- Behebung: Internet trennen, ohne Netz neu starten, dann kopieren. Alternative: der Payload PS5Save. Apollo hat **keine** PS5-Version.
- Medic-Nutzen: Vorsorglich ein Lese-Backup von `/user/home/<ID>/savedata_prospero/` und `savedata/` anlegen, bevor riskante Fixes laufen.
- Quelle: https://alex-free.github.io/ps5-jb-12.70fw-tutorial, https://github.com/heni0xyz/PS5Save/wiki · community

### 5.11 FTP: Verzeichnisliste fehlt (Active Mode) / doppelte FTP-Server
- Log: `502 Command not recognized` bzw. `550 Bad file descriptor` nach `PORT`.
- Erkennung: Die Ports 2121 (ftpsrv) und 1337 (etaHEN-FTP) lauschen beide; `FTP=` in der etaHEN-config.
- Behebung: Passiv-Modus verwenden und nur einen Server betreiben. ftpsrv ist viel schneller.
- Quelle: https://github.com/ps5-payload-dev/ftpsrv/issues/15 · community

### 5.12 FTP: Uploads unvollständig (ASCII-Modus)
- Log: „ASCII transfer mode is not support". Behebung: Binärmodus verwenden.
- Medic-Nutzen: Dateigrößen bzw. ELF-Magic prüfen. Eine kaputte ELF erkennt die vorhandene Regel `payload.elf_invalid`.
- Quelle: https://github.com/ps5-payload-dev/ftpsrv/issues/13 · doc

### 5.13 /dev/klog belegt
- Ursache (vermutet): Ein anderer Leser hält `/dev/klog`, z. B. etaHEN mit `Klog=1` (Port 9081) oder klogsrv (3232).
- Medic-Nutzen: Wichtig für Medic selbst. Ist das klog belegt, auf Port 9081 bzw. 3232 ausweichen und im Bericht vermerken, dass klog-basierte Regeln nicht geprüft wurden.
- Quelle: Jailbreak-Doctor-KB (KLOG_BUSY) · unsicher

### 5.14 Lüfterschwelle (etaHEN „Manual Fan threshold")
- Kein belegter PS5-Default; der Config-Schlüssel ist unbekannt. Zur vorhandenen Regel `sys.temp_high` nur als Hinweistext.
- Quelle: https://github.com/etaHEN/etaHEN/releases · unsicher

Ohne Quelle geblieben: Ort heruntergeladener Updates im internen Speicher, ein jailbreak-spezifischer Fix für die Safe-Mode-Schleife, M.2-spezifische Fehler, Datums- bzw. Zertifikatsprobleme beim Browser-Exploit.

---

## 6. Empfehlung: nächste Regeln für `rules.json`

Auswahl nach Nutzen und guter Erkennbarkeit; die Erkennung läuft über Dateien, Logs und Prozesse.

| Prio | Kandidat | Erkennung | Fix |
|---|---|---|---|
| 1 | 2.2 Ordner-Dump ohne x-Bit | stat() + klog `0x80aa001a` | `chmod 0755` (alte Modi im Journal) |
| 1 | 2.4 Modul-Schreibweise `Il2cppUserAssemblies.prx` | Datei-Existenz | rename |
| 1 | 2.1 ungültiger SM+-Wert (beta12 bricht ab) | `[CFG] invalid` + Prozess fehlt | `smp_set_key` auf den Default |
| 1 | 5.7 `PS5UPDATE.PUP` auf USB | Datei-Existenz | rename `.blocked` |
| 1 | 1.1 / 4.x `pldmgr.elf` fehlt in `autoload.txt` | autoload.txt + Port 8084 | Zeile ergänzen |
| 2 | 1.2 / 4.x Schreibweise in autoload.txt | Vergleich mit Ordnerinhalt | Zeile korrigieren |
| 2 | 4.1 Delay nach kstuff fehlt | PLDMGR-Log + Reihenfolge | `!3000` einfügen |
| 2 | 3.4 `no_kstuff` / `toolbox_auto_start` vergessen | Datei-Existenz | rename `.disabled` |
| 2 | 3.1 etaHEN ≥ 2.4B ohne elfldr | Prozess + Port 9021 | Hinweis |
| 2 | 2.6 Fakelib full → defektes Backport | profiles.ini + Exit | `fakelib = emulators` |
| 2 | 2.5 `requiredSystemSoftwareVersion` > FW | param.json | Hinweis |
| 2 | 4.5 Payload doppelt | Prozessliste | Prozess beenden (mit Bestätigung) |
| 3 | 3.10 / 3.11 / 3.8 / 3.14 kstuff-1.11-Signaturen | klog-Zeilen | Hinweis (Update/Workaround) |
| 3 | 4.6 `/data/pldmgr` defekt | Struktur | Ordner umbenennen |
| 3 | 4.11 websrv-Eintrag in Großbuchstaben | Dateinamen | rename |
| 3 | 5.13 klog belegt | Öffnen schlägt fehl | Fallback Port 9081 / 3232 |

**Stand 28. Nov. 2026 (Branch `feat/prio1-regeln`, auf `origin/main` 0.5.0):** Prio 1 ist erledigt. 2.2 (`game.no_exec_permission`, Fix `chmod_exec`) und 2.4 (`game.il2cpp_case`, Fix `rename_case_prx`) gab es schon; neu sind der Fix `smp_comment_invalid_lines` für 2.1 sowie die Regeln `update.pup_on_usb` (5.7) und `autoload.pldmgr_not_listed` (1.1) mit Fix. Einzelheiten im Nachtrag vom 28. Nov. in `docs/RULES.md`. Marc hat chmod und das Beenden von Prozessen erlaubt (für Prio 2, 4.5).

Offene Fragen vor der Umsetzung (Stand vor dem 28. Nov.):
- Darf Medic `chmod` ausführen? Die Fix-Engine kennt bisher nur Verschieben und Umbenennen; ein Journal-Typ für Dateimodi fehlt.
- Darf Medic Prozesse beenden?
- Darf Medic aus `/system_data` lesen (DB-Integritätsprüfung)?


---

## 7. Aus dem eigenen Projekt: PS5 Cooling & System Center (v1.98.0, Stand 28. Nov. 2026)

Quellen: Release Notes v1.90.0 bis v1.98.0 (https://github.com/strongt1me/ps5-cooling-system-center-pro/releases) und `docs/HANDBUCH.md` des Projekts. Vieles ist dort **an der Konsole bestätigt** (Slim/Pro, FW 12.00). Abgeglichen mit Medic auf dem Stand von PR #2 (124 Regeln); Themen, die Medic schon hat (Kraken-Logs und -API, ampr, Dump Forge, `game.no_exec_permission`, `fan.double_controller`), sind weggelassen.

### 7.1 Korrektur: `smp.launch_busy` gilt auch für 1.7beta12
- Medic sagt heute „bis 1.7beta11, behoben in 1.7beta12“. Laut Handbuch (an der Konsole nachgestellt am 14.11.2026 mit SMP 1.7beta12): Ein über ShadowMount+ **eingehängtes, aber nicht gestartetes** Spiel hält den einen Einhänge-Platz (`claim_prepared_title` in `sm_shellcore_service.c`). Dann startet kein anderes Spiel und keine Kachel, Log `launch mount unavailable: <ID> status=16 (Device busy)`, Konsole `LaunchFlowError`.
- Behebung: Spiel aushängen (Seite von SM+ oder Cooling Center) oder ShadowMount+ neu senden.
- Erkennung zusätzlich: `/data/PS5-Cooling-Center/smp-mounted.txt` listet die vom Cooling Center eingehängten Spiele; läuft das Cooling Center nicht und stehen dort Einträge, halten diese womöglich den Platz.
- Auto-Fix: nein (Aushängen geht nur über die API von SM+). **Nur Text und Bedingung korrigieren.**

### 7.2 Lüfter: ShadowMount+ und Cooling Center regeln gleichzeitig
- Steht in der SM+-`config.ini` `[fan] target_temperature` als Zahl (nicht `system`) und läuft im Cooling Center die Automatik, setzt das Cooling Center seine Schwelle binnen ~2 s wieder durch; der Wert von SM+ hält nur kurz (Handbuch „Vergleich mit ShadowMountPlus“). `fan.double_controller` zählt SM+ bisher nicht mit.
- Erkennung: `smp.config.fan.target_temperature` ist eine Zahl **und** `fan.controller.name` = PS5 Cooling Center mit automatischem Modus (Modusname in der `config.json` noch zu prüfen).
- Auto-Fix: ja, `smp_set_key` → `[fan] target_temperature=system` (Backup, Rückgängig). Alternative für den Nutzer: im Cooling Center „Beobachten“ wählen.

### 7.3 Derselbe Titel bei zwei Einhängern (Kraken Mount, ShadowMount+, PS4Mount)
- Liegt dieselbe Title-ID bei Kraken Mount und bei ShadowMount+ (Beispiel 14.11.2026: `.pkg` bei Kraken, Abbild bei SM+), hängt Kraken die Einhängung von SM+ aus (`kraken.log`: „not ours, or stale; unmounting it“), es gibt `Device busy`, die Kachel wechselt.
- Erkennung: Logzeile in `/data/krakenmount/kraken.log` **oder** Title-ID sowohl in `smp.titles` als auch in den Kraken-Spielen (`/api/state` bzw. Kraken-Ordner).
- Behebung: nur eine Quelle behalten (Kraken-Spiele nur in `Kraken-Games`, SM+-Spiele in `homebrew`). Auto-Fix: ggf. `move_duplicate_titleid`-artig (nach Rückfrage, welche bleibt).

### 7.4 Kraken Mount durchsucht Ordner von ShadowMount+
- Stellt man Kraken Mount auf Ordner um, die auch SM+ durchsucht (etwa `/data/homebrew`), wird jedes Ordner-Spiel doppelt angemeldet (Handbuch).
- Erkennung: Suchordner von Kraken (Einstellungen bzw. `/api/state`) ∩ `smp.scan_roots`. Pfad der Kraken-Einstellungsdatei noch zu klären.
- Auto-Fix: nein (Einstellung auf der Seite von Kraken, Port 8097).

### 7.5 PS4-Spiel im Ordner `Kraken-Games`
- Kraken Mount spielt nur PS5-Spiele; ein PS4-Spiel (CUSA…) dort bekommt keine spielbare Kachel.
- Erkennung: `CUSA`-Title-ID (param.sfo/param.json oder Name) unter `/data/Kraken-Games` bzw. `/mnt/*/Kraken-Games`.
- Behebung: PS4-Spiele über PS4Mount (`.pkg` auf exFAT-USB) oder installieren. Auto-Fix: nein.

### 7.6 Alter Ordner `FPKG-Games` neben `Kraken-Games`
- Ab Cooling Center 1.98.0 heißt der Ordner `Kraken-Games`; die App benennt ihn nur um, wenn es sicher ist (Kraken beendet, kein Spiel, nichts aus dem alten Ordner eingehängt), sonst wartet sie. Kraken behält seine alte Ordnerliste bis zum nächsten Start.
- Erkennung: `FPKG-Games` (intern oder Laufwerk) mit Spielen **und** `Kraken-Games` vorhanden → Spiele im alten Ordner werden nicht gefunden.
- Behebung: Kraken Mount einmal aus- und einschalten (dann benennt die App um). Auto-Fix: nein (das erledigt das Cooling Center selbst).

### 7.7 Dateien im Platzhalter eines abgezogenen Laufwerks
- Ist ein Laufwerk weg, ist `/mnt/usbN` nur ein kleiner Platzhalter auf der Systempartition (Medic kennt das schon: fstype `tmpfs`). Was dort hineingeschrieben wird, liegt nicht auf dem Stick und füllt die kleine Partition (Cooling Center 1.90.0 sperrt das deshalb).
- Erkennung: Einträge unter `/mnt/usbN` bei Speicherart `tmpfs`.
- Behebung: Dateien nach `/data` retten, Laufwerk wieder anstecken. Auto-Fix: nein (über Dateisystemgrenzen nur kopieren, nicht verschieben).

### 7.8 Kraken-Abbild auf USB hält das Laufwerk
- Kraken Mount ≥ 0.4.2 hängt Abbilder über `/dev/lvdN` auf `/mnt/krakenmount/…` ein; die Einhängung bleibt auch nach dem Ausschalten von Kraken stehen, und SM+ gibt sie beim Auswerfen nicht frei (Cooling Center: Protokoll `usb_eject_held`). Abziehen zieht dem Spiel das Laufwerk weg.
- Erkennung: Einhängungen unter `/mnt/krakenmount/` mit Quelle auf `/mnt/usbN`.
- Behebung: Laufwerk zuerst auf der Seite von Kraken aushängen. Auto-Fix: nein (Hinweis).

### 7.9 Kraken Mount auf dem Port von ShadowMount+ (10101)
- Kraken Mount ≥ 0.4.1 beantwortet auch die SM+-Schnittstelle auf `127.0.0.1:10101`. Läuft eine eigenständige Kraken-Fassung neben SM+, kann ein Werkzeug (und Medics eigene Prüfung `smp.api_*`) mit Kraken statt SM+ reden. Das Cooling Center verlegt das in seiner Fassung auf 18097 (`smp_port_kraken`).
- Medic-Nutzen: `smp.api_not_responding` bzw. die API-Fakten gegen diesen Fall absichern (Kraken-Prozess ohne Cooling Center + Port 10101 offen).

### 7.10 PS4Mount: PS4-Spiele auf der M.2-Erweiterung
- PS4-Spiele auf der M.2-Erweiterung werden nicht angemeldet; die Konsole bricht ihren Start ab (Cooling Center 1.93.0).
- Erkennung: Kachel `/user/app/CUSA…`, deren Quelle unter `/mnt/ext*` liegt (Layout von PS4Mount zu prüfen).
- Weitere PS4Mount-Hinweise aus dem Handbuch: vor dem Ruhemodus PS4Mount-Spiele schließen (Laufwerk schaltet ab, Spiel hängt); höchstens ~30 Pakete je Spiel (Grenze der Konsole); zwei Laufwerke/Partitionen können die Oberfläche träge machen.

### 7.11 Beschädigtes Kachelbild
- `sce_sys/icon0.png`, `pic0.png`, `pic1.png`, `pic2.png` kein gültiges PNG → Bild fehlt auf dem Startbildschirm (Cooling Center 1.92.0 prüft Kopf und Chunks).
- Erkennung: PNG-Kopf in den Spielordnern aus `smp.titles`. Auto-Fix: nein (Inhalt).

### 7.12 Alte Cooling-Center-Fassungen mit Datenverlust-Fehlern
- < 1.95.0: „Ersetzen“ verlor die alte Datei, wenn die Kopie scheiterte; abgebrochenes Entpacken ließ Reste liegen. < 1.97.0: Spielstand lesen/zurückspielen konnte die App beenden; Lüfter blieb nach dem Wächter auf voll.
- Erkennung: Version des Cooling Centers (Quelle zu klären: `config.json`, Datei oder Prozess). Auto-Fix: nein (Hinweis auf neuere Fassung der App, kein Systemupdate).

**Stand 28. Nov. 2026:** Prio 1 (7.1, 7.2, 7.3) ist umgesetzt (Branch `feat/cooling-prio1`, Nachtrag in `docs/RULES.md`).

| Prio | Kandidat | Aufwand |
|---|---|---|
| 1 | 7.1 Korrektur `smp.launch_busy` (Text + `smp-mounted.txt`) | klein |
| 1 | 7.2 SM+-Lüfterziel + Cooling-Center-Automatik, Fix `target_temperature=system` | klein (Modusname prüfen) |
| 1 | 7.3 Titel bei zwei Einhängern | mittel |
| 2 | 7.5 PS4-Spiel in `Kraken-Games`, 7.6 alter `FPKG-Games`-Ordner, 7.7 Dateien im Platzhalter | klein |
| 2 | 7.9 Kraken auf Port 10101 (Absicherung der SM+-API-Prüfung) | klein |
| 3 | 7.4, 7.8, 7.10, 7.11, 7.12 | Pfade/Layouts erst klären |
