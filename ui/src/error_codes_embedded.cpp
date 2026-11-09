// SPDX-License-Identifier: GPL-3.0-or-later
// GENERIERT von tools/embed_error_codes.py aus knowledge/error_codes.json -- nicht von Hand aendern.
#include "medic_ui/error_data.hpp"

namespace medic::ui {

const char* const kErrorCodesJson = R"JSON({
 "db_version": "2026.11.09",
 "source": "https://www.playstation.com/en-us/support/error-codes/ps5/",
 "url_template": "https://www.playstation.com/en-us/support/error-codes/ps5/{code_lower}/",
 "prefixes": {
  "CE": {
   "de": "Konsole, Spiele und Apps, Datenträger, Speicher und Verbindung",
   "en": "Console, games and apps, discs, storage and connection"
  },
  "NP": {
   "de": "PlayStation-Konto, Updates und Dienste",
   "en": "PlayStation account, updates and services"
  },
  "WS": {
   "de": "PlayStation-Server, Konto-Sperren und Wartung",
   "en": "PlayStation servers, account suspensions and maintenance"
  },
  "NW": {
   "de": "Netzwerkverbindung zu PlayStation",
   "en": "Network connection to PlayStation"
  },
  "WV": {
   "de": "Netzwerkverbindung",
   "en": "Network connection"
  },
  "SU": {
   "de": "Systemsoftware, Laufwerk und Start der Konsole",
   "en": "System software, drive and console start-up"
  },
  "E2": {
   "de": "Server nicht erreichbar",
   "en": "Server not reachable"
  }
 },
 "kinds": {
  "app_error": {
   "title": {
    "de": "Spiel oder App startet nicht oder stürzt ab",
    "en": "Game or app does not start or crashes"
   },
   "steps": {
    "de": [
     "Konsole neu starten und das Spiel noch einmal öffnen.",
     "Systemsoftware und das betroffene Spiel bzw. die App aktualisieren.",
     "Spiel oder App in der Speicherverwaltung löschen und neu laden (Spielstände vorher sichern).",
     "Bleibt der Fehler: im abgesicherten Modus die Datenbank neu aufbauen."
    ],
    "en": [
     "Restart the console and open the game again.",
     "Update the system software and the affected game or app.",
     "Delete the game or app in the storage settings and download it again (back up save data first).",
     "If it persists: rebuild the database in safe mode."
    ]
   },
   "homebrew": {
    "de": "Bei Spielen, die ShadowMount+ einhängt, gibt es kein Neuinstallieren: Image bzw. Ordner und Pfade prüfen. Ein Check-up findet typische Ursachen der Homebrew-Umgebung (kstuff-Version, BackPork-Konflikt, doppelt geladenes etaHEN).",
    "en": "Games mounted by ShadowMount+ cannot be reinstalled: check the image or folder and the paths. A check-up finds typical causes in the homebrew setup (kstuff version, BackPork conflict, etaHEN loaded twice)."
   },
   "check": true,
   "related_rules": [
    "smp.kstuff_min_version",
    "smp.backpork_conflict",
    "hen.etahen_double_load",
    "smp.launch_busy",
    "smp.retry_limit"
   ]
  },
  "generic": {
   "title": {
    "de": "Allgemeiner Fehler",
    "en": "General error"
   },
   "steps": {
    "de": [
     "Konsole neu starten und die Aktion wiederholen.",
     "Systemsoftware und betroffene Software aktualisieren.",
     "Später noch einmal versuchen; bei dauerhaftem Fehler im abgesicherten Modus die Datenbank neu aufbauen.",
     "Bleibt der Fehler: den PlayStation-Support kontaktieren."
    ],
    "en": [
     "Restart the console and repeat the action.",
     "Update the system software and the affected software.",
     "Try again later; if it keeps happening, rebuild the database in safe mode.",
     "If it persists: contact PlayStation Support."
    ]
   },
   "homebrew": {
    "de": "Auf einer gejailbreakten Konsole kann auch die Homebrew-Umgebung dahinterstecken. Ein Check-up zeigt bekannte Ursachen.",
    "en": "On a jailbroken console the homebrew setup can be behind it. A check-up shows known causes."
   },
   "check": true
  },
  "system_data": {
   "title": {
    "de": "Systemsoftware oder Daten nicht lesbar",
    "en": "System software or data cannot be read"
   },
   "steps": {
    "de": [
     "Systemsoftware auf die neueste Version aktualisieren.",
     "Tritt der Fehler nach dem Start eines Spiels auf: es löschen und neu installieren.",
     "Im abgesicherten Modus die Datenbank neu aufbauen (Cache leeren).",
     "Hilft das nicht: Konsole zurücksetzen (vorher alles sichern, der interne Speicher wird gelöscht), dann neu installieren."
    ],
    "en": [
     "Update the system software to the latest version.",
     "If the error appears after starting a game: delete it and install it again.",
     "Rebuild the database in safe mode (clears the cache).",
     "If that does not help: reset the console (back everything up first, internal storage is erased), then reinstall."
    ]
   },
   "homebrew": {
    "de": "Achtung: Ein Systemupdate kann den Jailbreak dauerhaft beenden, und ein Zurücksetzen löscht auch /data (Medic-Backups, Payload-Konfiguration, Homebrew). Vorher sichern und prüfen, ob die Firmware weiter unterstützt wird.",
    "en": "Warning: a system update can end the jailbreak for good, and a reset also erases /data (Medic backups, payload settings, homebrew). Back up first and check that the firmware stays supported."
   },
   "check": true
  },
  "disc_error": {
   "title": {
    "de": "Disc wird nicht gelesen",
    "en": "Disc cannot be read"
   },
   "steps": {
    "de": [
     "Disc mit einem weichen, fusselfreien Tuch von der Mitte nach außen reinigen und neu einlegen.",
     "Steht die Konsole hochkant: die Disc mit dem Etikett nach links einlegen.",
     "Zum Gegenprüfen eine andere Disc probieren: liegt es am Laufwerk, hilft nur der Sony-Reparaturservice."
    ],
    "en": [
     "Wipe the disc from the centre outwards with a soft, lint-free cloth and insert it again.",
     "If the console stands upright: insert the disc with the label facing left.",
     "To cross-check, try another disc: if the drive is at fault, only Sony repair helps."
    ]
   }
  },
  "disc_drive": {
   "title": {
    "de": "Blu-ray-Laufwerk wird nicht erkannt",
    "en": "Blu-ray drive is not detected"
   },
   "steps": {
    "de": [
     "Konsole ganz ausschalten (nicht in den Ruhemodus), Netzkabel ziehen, einige Minuten warten und neu starten.",
     "Bleibt der Fehler: vermutlich ein Hardwaredefekt, der Sony-Reparaturservice hilft weiter."
    ],
    "en": [
     "Turn the console fully off (not rest mode), unplug the power cable, wait a few minutes and start again.",
     "If it persists: probably a hardware fault, Sony repair service can help."
    ]
   }
  },
  "install_update": {
   "title": {
    "de": "Download, Installation oder Update schlägt fehl",
    "en": "Download, installation or update fails"
   },
   "steps": {
    "de": [
     "Konsole neu starten und es noch einmal versuchen.",
     "Genug freien Speicher sicherstellen.",
     "Internetverbindung prüfen; den Download fortsetzen oder neu starten.",
     "Ausstehende Updates für Systemsoftware und Spiel installieren.",
     "Bleibt der Fehler: den unvollständigen Eintrag löschen und neu laden."
    ],
    "en": [
     "Restart the console and try again.",
     "Make sure there is enough free storage.",
     "Check the internet connection; resume or restart the download.",
     "Install pending updates for the system software and the game.",
     "If it persists: delete the incomplete item and download it again."
    ]
   },
   "homebrew": {
    "de": "Zu wenig Platz auf /data oder dem Spiele-Laufwerk lässt Installationen und Homebrew scheitern. Der Check-up zeigt den freien Speicher.",
    "en": "Too little space on /data or the games drive makes installs and homebrew fail. The check-up shows free storage."
   },
   "check": true,
   "related_rules": [
    "storage.low_space_data"
   ]
  },
  "storage_space": {
   "title": {
    "de": "Nicht genug freier Speicher",
    "en": "Not enough free storage"
   },
   "steps": {
    "de": [
     "In den Einstellungen unter Speicher nicht mehr benötigte Spiele, Apps und Aufnahmen entfernen.",
     "Spiele auf einen Erweiterungsspeicher verschieben, falls vorhanden.",
     "Danach den Vorgang wiederholen."
    ],
    "en": [
     "In the storage settings remove games, apps and captures you no longer need.",
     "Move games to extended storage if you have one.",
     "Then repeat the action."
    ]
   },
   "homebrew": {
    "de": "Der Check-up warnt, wenn auf /data weniger als 5 % frei sind, und nennt nicht mehr benötigte Homebrew-Dateien und alte Backups.",
    "en": "The check-up warns when /data has less than 5 % free and names homebrew files and old backups you no longer need."
   },
   "check": true,
   "related_rules": [
    "storage.low_space_data"
   ]
  },
  "storage_usb": {
   "title": {
    "de": "Externes Laufwerk wird nicht erkannt",
    "en": "External drive is not recognised"
   },
   "steps": {
    "de": [
     "Laufwerk abziehen und wieder anschließen, einen anderen USB-Anschluss und ein anderes Kabel probieren.",
     "Das Laufwerk am PC auf Fehler prüfen.",
     "Konsole neu starten und das Laufwerk erneut anschließen."
    ],
    "en": [
     "Unplug and reconnect the drive, try another USB port and another cable.",
     "Check the drive for errors on a PC.",
     "Restart the console and connect the drive again."
    ]
   },
   "homebrew": {
    "de": "Der Check-up zeigt Laufwerke, die schreibgeschützt eingehängt sind, und Scanpfade auf fehlenden Laufwerken.",
    "en": "The check-up shows drives that are mounted read-only and scan paths on drives that are missing."
   },
   "check": true,
   "related_rules": [
    "storage.usb_mounted_ro",
    "storage.scanpath_drive_missing"
   ]
  },
  "network": {
   "title": {
    "de": "Verbindung zu PlayStation oder Server klappt nicht",
    "en": "Cannot connect to PlayStation or the server"
   },
   "steps": {
    "de": [
     "Auf der PSN-Statusseite prüfen, ob eine Störung oder Wartung vorliegt.",
     "Konsole neu starten und möglichst per LAN-Kabel verbinden.",
     "Router und Modem vom Strom trennen, mindestens 5 Minuten warten, wieder einschalten.",
     "Bei WLAN näher an den Router rücken. Ist die Router-Firmware aktuell?",
     "Testweise einen öffentlichen DNS-Server eintragen oder eine feste IP-Adresse vergeben.",
     "Hilft nichts: etwas warten, der Dienst kann überlastet sein."
    ],
    "en": [
     "Check the PSN status page for an outage or maintenance.",
     "Restart the console and connect by LAN cable if possible.",
     "Unplug router and modem, wait at least 5 minutes, switch them on again.",
     "On Wi-Fi move closer to the router. Is the router firmware up to date?",
     "As a test enter a public DNS server or assign a fixed IP address.",
     "If nothing helps: wait a while, the service can be busy."
    ]
   },
   "homebrew": {
    "de": "Sperrt dein nanoDNS, ein DNS-Blocker oder eine Hosts-Datei Sony-Domains, sind solche Fehler gewollt: Sperre lockern oder normalen DNS eintragen. Der Check-up liest das nanoDNS-Log.",
    "en": "If your nanoDNS, a DNS blocker or a hosts file blocks Sony domains, these errors are intended: loosen the block or enter a normal DNS. The check-up reads the nanoDNS log."
   },
   "check": true,
   "related_rules": [
    "nanodns.log_problem"
   ]
  },
  "maintenance": {
   "title": {
    "de": "Dienst wird gewartet",
    "en": "Service under maintenance"
   },
   "steps": {
    "de": [
     "Auf der PSN-Statusseite nachsehen, ob Wartung angekündigt ist.",
     "Später noch einmal versuchen: an der Konsole ist nichts zu reparieren."
    ],
    "en": [
     "Check the PSN status page for announced maintenance.",
     "Try again later: there is nothing to repair on the console."
    ]
   }
  },
  "suspended": {
   "title": {
    "de": "Zugriff auf PlayStation ist gesperrt",
    "en": "PlayStation access is blocked"
   },
   "steps": {
    "de": [
     "Am Handy oder PC bei PlayStation anmelden: so sieht man, ob das Konto gesperrt ist.",
     "Die E-Mail von Sony an die Anmelde-Adresse lesen, sie nennt Einzelheiten.",
     "Fragen zur Sperre klärt der PlayStation-Support."
    ],
    "en": [
     "Sign in to PlayStation on a phone or PC to see whether the account is blocked.",
     "Read the email Sony sent to the sign-in address, it gives details.",
     "Questions about the block are handled by PlayStation Support."
    ]
   },
   "homebrew": {
    "de": "Eine Sperre geht von Sony aus. PS5 Medic kann sie weder beheben noch umgehen.",
    "en": "A block comes from Sony. PS5 Medic can neither fix nor bypass it."
   }
  },
  "account_signin": {
   "title": {
    "de": "Anmeldung oder Konto-Problem",
    "en": "Sign-in or account problem"
   },
   "steps": {
    "de": [
     "Anmelde-ID und Passwort prüfen, z. B. durch Anmelden am Handy oder PC.",
     "Bei Bedarf das Passwort über die offizielle PlayStation-Seite zurücksetzen.",
     "Die Einstellungen der Bestätigung in zwei Schritten prüfen.",
     "Abmelden, Konsole neu starten und erneut anmelden."
    ],
    "en": [
     "Check sign-in ID and password, e.g. by signing in on a phone or PC.",
     "If needed reset the password on the official PlayStation site.",
     "Check the 2-step verification settings.",
     "Sign out, restart the console and sign in again."
    ]
   }
  },
  "license": {
   "title": {
    "de": "Lizenz, Abo oder Kauf nicht bestätigt",
    "en": "Licence, subscription or purchase not confirmed"
   },
   "steps": {
    "de": [
     "Internetverbindung prüfen und angemeldet sein.",
     "In den Konto-Einstellungen die Lizenzen wiederherstellen.",
     "Abo und Zahlungsart auf der Konto-Seite prüfen.",
     "Später noch einmal versuchen."
    ],
    "en": [
     "Check the internet connection and make sure you are signed in.",
     "Restore licences in the account settings.",
     "Check the subscription and payment method on the account page.",
     "Try again later."
    ]
   }
  },
  "system_update": {
   "title": {
    "de": "Systemsoftware-Update nötig oder fehlgeschlagen",
    "en": "System software update needed or failed"
   },
   "steps": {
    "de": [
     "Die Konsole mit dem Internet verbinden und auf die neueste Systemsoftware aktualisieren.",
     "Schlägt das Update fehl: Router bzw. Modem und Konsole neu starten und noch einmal versuchen.",
     "Alternativ die Update-Datei am PC auf einen USB-Stick laden und von dort installieren.",
     "Ohne Internet hilft eine Spiel-Disc, die neuere Systemsoftware enthält."
    ],
    "en": [
     "Connect the console to the internet and update to the latest system software.",
     "If the update fails: restart router or modem and the console, then try again.",
     "Alternatively download the update file on a PC to a USB drive and install it from there.",
     "Without internet a game disc with newer system software helps."
    ]
   },
   "homebrew": {
    "de": "Achtung: Ein Systemupdate kann den Jailbreak dauerhaft beenden, neuere Firmware ist oft nicht jailbreakbar. Vorher prüfen, ob deine Firmware weiter unterstützt wird. Oft heißt der Fehler nur, dass ein Spiel neuere Firmware verlangt.",
    "en": "Warning: a system update can end the jailbreak for good, newer firmware often cannot be jailbroken. Check first that your firmware stays supported. Often the error only means a game wants newer firmware."
   },
   "related_rules": [
    "hen.etahen_with_kstufflite_latest_fw"
   ]
  },
  "peripheral": {
   "title": {
    "de": "Zubehör oder Audiogerät",
    "en": "Accessory or audio device"
   },
   "steps": {
    "de": [
     "Das Gerät abziehen, wieder anschließen und die Konsole neu starten.",
     "Anderen Anschluss bzw. anderes Kabel probieren.",
     "Software des Geräts aktualisieren, soweit angeboten."
    ],
    "en": [
     "Unplug the device, connect it again and restart the console.",
     "Try another port or cable.",
     "Update the device software where offered."
    ]
   }
  },
  "cloud_save": {
   "title": {
    "de": "Spielstand lässt sich nicht mit der Cloud abgleichen",
    "en": "Save data cannot sync with the cloud"
   },
   "steps": {
    "de": [
     "Internetverbindung prüfen.",
     "Prüfen, ob im Cloud-Speicher noch Platz ist und das Abo aktiv ist.",
     "Konsole neu starten und den Abgleich später wiederholen."
    ],
    "en": [
     "Check the internet connection.",
     "Check that cloud storage has room and the subscription is active.",
     "Restart the console and repeat the sync later."
    ]
   }
  },
  "restriction": {
   "title": {
    "de": "Funktion durch Altersgrenze gesperrt",
    "en": "Feature restricted by age limits"
   },
   "steps": {
    "de": [
     "In der Familienverwaltung die Alters- und Kinderschutz-Einstellungen des Kontos prüfen.",
     "Nur ein Erwachsenen-Konto darf sie ändern."
    ],
    "en": [
     "Check the age and parental settings of the account in family management.",
     "Only an adult account may change them."
    ]
   }
  },
  "boot_problem": {
   "title": {
    "de": "Konsole ist vielleicht nicht richtig gestartet",
    "en": "The console may not have started properly"
   },
   "steps": {
    "de": [
     "Konsole ausschalten, Netzkabel ziehen, fünf Minuten warten, wieder anschließen und einschalten.",
     "Bleibt der Fehler: im abgesicherten Modus die Konsole zurücksetzen und die Systemsoftware neu installieren.",
     "Geht auch das nicht oder startet der abgesicherte Modus nicht: Hardware-Reparatur bei PlayStation."
    ],
    "en": [
     "Turn the console off, unplug the power cable, wait five minutes, plug it in and switch it on.",
     "If it persists: in safe mode reset the console and reinstall the system software.",
     "If that fails or safe mode does not start: hardware repair through PlayStation."
    ]
   },
   "homebrew": {
    "de": "Zurücksetzen löscht den internen Speicher inklusive /data. Danach den Jailbreak neu ausführen und die Homebrew-Dateien neu aufspielen.",
    "en": "A reset erases internal storage including /data. Afterwards run the jailbreak again and copy the homebrew files back."
   }
  },
  "info": {
   "title": {
    "de": "Kein Fehler",
    "en": "Not an error"
   },
   "steps": {
    "de": [],
    "en": []
   }
  }
 },
 "codes": [
  {
   "code": "CE-107928-7",
   "kind": "install_update",
   "text": {
    "de": "Ein Fehler hat den Download eines Spiels gestoppt.",
    "en": "An error stopped a game download."
   }
  },
  {
   "code": "CE-108889-4",
   "kind": "system_update",
   "text": {
    "de": "Die neueste Systemsoftware muss installiert werden.",
    "en": "The latest system software must be installed."
   }
  },
  {
   "code": "CE-108862-5",
   "kind": "network",
   "text": {
    "de": "Die Konsole erreicht den Server nicht.",
    "en": "The console cannot reach the server."
   }
  },
  {
   "code": "CE-111161-1",
   "kind": "peripheral",
   "text": {
    "de": "Die Verbindung der Kamera (HD-Kamera oder PS Camera) ist zu prüfen.",
    "en": "The camera connection (HD camera or PS Camera) needs checking."
   }
  },
  {
   "code": "CE-108360-8",
   "kind": "peripheral",
   "text": {
    "de": "Die Verbindung der Kamera (HD-Kamera oder PS Camera) ist zu prüfen.",
    "en": "The camera connection (HD camera or PS Camera) needs checking."
   }
  },
  {
   "code": "CE-105799-1",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "CE-117722-0",
   "kind": "network",
   "text": {
    "de": "Problem bei der Streaming-Verbindung.",
    "en": "Problem with the streaming connection."
   }
  },
  {
   "code": "CE-107520-5",
   "kind": "system_update",
   "text": {
    "de": "Die neueste Systemsoftware muss installiert werden.",
    "en": "The latest system software must be installed."
   }
  },
  {
   "code": "CE-110538-8",
   "kind": "disc_error",
   "text": {
    "de": "Die Blu-ray-Disc konnte nicht gelesen werden.",
    "en": "The Blu-ray disc could not be read."
   }
  },
  {
   "code": "CE-108255-1",
   "kind": "app_error",
   "text": {
    "de": "Spiel oder Anwendung hat einen Fehler gemeldet.",
    "en": "The game or application reported an error."
   }
  },
  {
   "code": "CE-112069-9",
   "kind": "network",
   "text": {
    "de": "Die Datenübertragung im Netzwerk ist fehlgeschlagen.",
    "en": "The network data transfer failed."
   }
  },
  {
   "code": "CE-108262-9",
   "kind": "system_data",
   "text": {
    "de": "Systemsoftware oder App-Daten konnten nicht gelesen werden.",
    "en": "System software or app data could not be read."
   }
  },
  {
   "code": "CE-107649-7",
   "kind": "disc_error",
   "text": {
    "de": "Die eingelegte Disc passt nicht zu den installierten Daten.",
    "en": "The inserted disc does not match the installed data."
   }
  },
  {
   "code": "CE-100005-6",
   "kind": "app_error",
   "text": {
    "de": "Spiel oder App lässt sich nicht laden.",
    "en": "The game or app fails to load."
   }
  },
  {
   "code": "CE-106485-4",
   "kind": "system_data",
   "text": {
    "de": "Problem beim Lesen von Systemsoftware oder Daten.",
    "en": "Problem reading system software or data."
   }
  },
  {
   "code": "CE-100002-3",
   "kind": "install_update",
   "text": {
    "de": "Das App-Update ist fehlgeschlagen.",
    "en": "The app update failed."
   }
  },
  {
   "code": "CE-112839-4",
   "kind": "network",
   "text": {
    "de": "Fehler beim Verbinden mit PlayStation.",
    "en": "Error connecting to PlayStation."
   }
  },
  {
   "code": "CE-107938-8",
   "kind": "install_update",
   "text": {
    "de": "Das App-Update ist fehlgeschlagen.",
    "en": "The app update failed."
   }
  },
  {
   "code": "CE-112841-7",
   "kind": "account_signin",
   "text": {
    "de": "Der Benutzer ist nicht angemeldet.",
    "en": "The user is not signed in."
   }
  },
  {
   "code": "CE-100096-6",
   "kind": "app_error",
   "text": {
    "de": "Die Anwendung lässt sich nicht laden.",
    "en": "The application failed to load."
   }
  },
  {
   "code": "CE-107857-8",
   "kind": "app_error",
   "text": {
    "de": "Fehler beim Laden oder Aktualisieren der Anwendung.",
    "en": "Error loading or updating the application."
   }
  },
  {
   "code": "CE-100006-7",
   "kind": "storage_usb",
   "text": {
    "de": "Das externe Laufwerk wird nicht erkannt.",
    "en": "The external storage drive is not recognised."
   }
  },
  {
   "code": "CE-109737-7",
   "kind": "storage_usb",
   "text": {
    "de": "Das USB-Speichergerät wird nicht erkannt.",
    "en": "The USB storage device is not recognised."
   }
  },
  {
   "code": "CE-105771-1",
   "kind": "generic",
   "text": {
    "de": "Ein allgemeiner Fehler ist aufgetreten.",
    "en": "A general error occurred."
   }
  },
  {
   "code": "CE-110555-7",
   "kind": "disc_error",
   "text": {
    "de": "Die Disc kann nicht gelesen werden.",
    "en": "The disc cannot be read."
   }
  },
  {
   "code": "CE-100022-5",
   "kind": "generic",
   "text": {
    "de": "Ein allgemeiner Fehler ist aufgetreten.",
    "en": "A general error occurred."
   }
  },
  {
   "code": "CE-113227-6",
   "kind": "generic",
   "text": {
    "de": "Ein allgemeiner Fehler ist aufgetreten.",
    "en": "A general error occurred."
   }
  },
  {
   "code": "CE-100008-9",
   "kind": "generic",
   "text": {
    "de": "Ein allgemeiner Fehler ist aufgetreten.",
    "en": "A general error occurred."
   }
  },
  {
   "code": "CE-113511-2",
   "kind": "generic",
   "text": {
    "de": "Ein allgemeiner Fehler ist aufgetreten.",
    "en": "A general error occurred."
   }
  },
  {
   "code": "CE-105841-9",
   "kind": "app_error",
   "text": {
    "de": "Fehler innerhalb der Anwendung.",
    "en": "An error occurred within the application."
   }
  },
  {
   "code": "CE-110551-3",
   "kind": "disc_error",
   "text": {
    "de": "Die Disc konnte nicht gelesen werden.",
    "en": "The disc could not be read."
   }
  },
  {
   "code": "CE-107750-0",
   "kind": "app_error",
   "text": {
    "de": "Beim Spielen ist ein Fehler aufgetreten.",
    "en": "An error occurred while playing the game."
   }
  },
  {
   "code": "CE-110552-4",
   "kind": "disc_error",
   "text": {
    "de": "Die Disc konnte nicht gelesen werden.",
    "en": "The disc could not be read."
   }
  },
  {
   "code": "CE-106444-9",
   "kind": "network",
   "text": {
    "de": "Fehler beim Verbinden mit PlayStation.",
    "en": "Error connecting to PlayStation."
   }
  },
  {
   "code": "CE-106439-3",
   "kind": "app_error",
   "text": {
    "de": "Fehler im Betrieb der Anwendung.",
    "en": "An error occurred while the application was running."
   }
  },
  {
   "code": "CE-107527-2",
   "kind": "install_update",
   "text": {
    "de": "Die Installation ist fehlgeschlagen.",
    "en": "The installation failed."
   }
  },
  {
   "code": "CE-110032-7",
   "kind": "account_signin",
   "text": {
    "de": "Die Online-ID-Angaben stimmen nicht überein.",
    "en": "The Online ID information does not match."
   }
  },
  {
   "code": "CE-106449-4",
   "kind": "network",
   "text": {
    "de": "Fehler bei der Verbindung zum Server.",
    "en": "Error connecting to the server."
   }
  },
  {
   "code": "CE-105638-3",
   "kind": "license",
   "text": {
    "de": "Die Aktivierung des Inhalts ist fehlgeschlagen.",
    "en": "Content activation failed."
   }
  },
  {
   "code": "CE-107255-0",
   "kind": "license",
   "text": {
    "de": "Das PS-Plus-Abo konnte nicht bestätigt werden.",
    "en": "The PS Plus subscription could not be confirmed."
   }
  },
  {
   "code": "CE-116799-3",
   "kind": "license",
   "text": {
    "de": "Das PS-Plus-Abo konnte nicht bestätigt werden.",
    "en": "The PS Plus subscription could not be confirmed."
   }
  },
  {
   "code": "CE-109245-1",
   "kind": "license",
   "text": {
    "de": "Die Lizenzdaten des Inhalts lassen sich nicht bestätigen.",
    "en": "The content licence information cannot be confirmed."
   }
  },
  {
   "code": "CE-109258-5",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "CE-107622-8",
   "kind": "app_error",
   "text": {
    "de": "Die Anwendung konnte nicht gestartet werden.",
    "en": "The application failed to launch."
   }
  },
  {
   "code": "CE-112988-9",
   "kind": "license",
   "text": {
    "de": "Der Inhalt kann nicht gekauft werden.",
    "en": "The content cannot be purchased."
   }
  },
  {
   "code": "CE-107863-5",
   "kind": "network",
   "text": {
    "de": "Netzwerkverbindungsfehler.",
    "en": "Network connection error."
   }
  },
  {
   "code": "CE-107872-5",
   "kind": "storage_space",
   "text": {
    "de": "Nicht genug freier Speicher auf der Konsole.",
    "en": "Not enough free storage on the console."
   }
  },
  {
   "code": "CE-100028-1",
   "kind": "storage_space",
   "text": {
    "de": "Auf der internen SSD ist zu wenig Platz für den Download oder die Aktion.",
    "en": "The internal SSD has too little free space for the download or action."
   }
  },
  {
   "code": "CE-113212-0",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "CE-118446-4",
   "kind": "system_update",
   "text": {
    "de": "Problem beim Aktualisieren des Systems.",
    "en": "Problem updating the system."
   }
  },
  {
   "code": "CE-106487-6",
   "kind": "install_update",
   "text": {
    "de": "Problem beim Herunterladen und Installieren von Inhalten.",
    "en": "Problem downloading and installing content."
   }
  },
  {
   "code": "CE-100095-5",
   "kind": "app_error",
   "text": {
    "de": "Problem beim Starten der Anwendung.",
    "en": "Problem starting the application."
   }
  },
  {
   "code": "CE-118415-0",
   "kind": "license",
   "text": {
    "de": "Das Abo muss eventuell überprüft werden.",
    "en": "The subscription may need to be verified."
   }
  },
  {
   "code": "CE-113338-9",
   "kind": "system_update",
   "text": {
    "de": "Fehler beim Aktualisieren der PS5-Systemsoftware.",
    "en": "Error updating the PS5 system software."
   }
  },
  {
   "code": "CE-109503-8",
   "kind": "network",
   "text": {
    "de": "Fehler beim Verbinden mit PlayStation.",
    "en": "Error connecting to PlayStation."
   }
  },
  {
   "code": "CE-100009-0",
   "kind": "install_update",
   "text": {
    "de": "Problem mit der Update-Datei.",
    "en": "Problem with the update file."
   }
  },
  {
   "code": "CE-117740-0",
   "kind": "app_error",
   "text": {
    "de": "Das Spiel hat möglicherweise einen Fehler.",
    "en": "The game may have an error."
   }
  },
  {
   "code": "CE-119224-9",
   "kind": "peripheral",
   "text": {
    "de": "Fehler beim Aktualisieren der Software des PS-VR2-Sense-Controllers.",
    "en": "Error updating the PS VR2 Sense controller software."
   }
  },
  {
   "code": "CE-108877-1",
   "kind": "network",
   "text": {
    "de": "Fehler bei der Verbindung zum Server.",
    "en": "Error connecting to the server."
   }
  },
  {
   "code": "CE-112840-6",
   "kind": "network",
   "text": {
    "de": "Problem bei der Verbindung zum Server.",
    "en": "Problem connecting to the server."
   }
  },
  {
   "code": "CE-118527-4",
   "kind": "network",
   "text": {
    "de": "Der Hostname konnte nicht aufgelöst werden (DNS).",
    "en": "The hostname could not be resolved (DNS)."
   }
  },
  {
   "code": "CE-107891-6",
   "kind": "install_update",
   "text": {
    "de": "Der Inhalt wird nicht richtig installiert.",
    "en": "The content is not installing properly."
   }
  },
  {
   "code": "CE-109573-5",
   "kind": "app_error",
   "text": {
    "de": "Fehler beim Starten des Spiels.",
    "en": "Error starting the game."
   }
  },
  {
   "code": "CE-112987-8",
   "kind": "install_update",
   "text": {
    "de": "Problem beim Kaufen, Herunterladen oder Installieren von Inhalten.",
    "en": "Problem purchasing, downloading or installing content."
   }
  },
  {
   "code": "CE-117773-6",
   "kind": "generic",
   "text": {
    "de": "Allgemeiner Konsolenfehler.",
    "en": "General console error."
   }
  },
  {
   "code": "CE-120706-6",
   "kind": "install_update",
   "text": {
    "de": "Das Spiel-Update konnte nicht installiert werden.",
    "en": "The game update failed to install."
   }
  },
  {
   "code": "CE-109506-1",
   "kind": "network",
   "text": {
    "de": "Fehler beim Verbinden mit PlayStation.",
    "en": "Error connecting to PlayStation."
   }
  },
  {
   "code": "CE-109531-9",
   "kind": "peripheral",
   "text": {
    "de": "Problem bei der Verbindung eines Audiogeräts.",
    "en": "Problem connecting an audio device."
   }
  },
  {
   "code": "CE-118851-4",
   "kind": "cloud_save",
   "text": {
    "de": "Der Spielstand lässt sich nicht mit dem Cloud-Speicher abgleichen.",
    "en": "Save data cannot sync with cloud storage."
   }
  },
  {
   "code": "CE-122154-5",
   "kind": "cloud_save",
   "text": {
    "de": "Der Spielstand lässt sich nicht mit dem Cloud-Speicher abgleichen.",
    "en": "Save data cannot sync with cloud storage."
   }
  },
  {
   "code": "CE-102159-8",
   "kind": "peripheral",
   "text": {
    "de": "Die Softwareversionen von PS Link-Audio und USB-Adapter passen nicht zusammen.",
    "en": "The PS Link audio and USB adapter software versions do not match."
   }
  },
  {
   "code": "CE-113524-6",
   "kind": "network",
   "text": {
    "de": "Verbindung zu PlayStation nicht möglich.",
    "en": "Cannot connect to PlayStation."
   }
  },
  {
   "code": "CE-108863-6",
   "kind": "network",
   "text": {
    "de": "Die Konsole konnte keine Verbindung zum Server herstellen.",
    "en": "The console could not connect to the server."
   }
  },
  {
   "code": "NP-102946-2",
   "kind": "system_update",
   "text": {
    "de": "Vor dem Fortfahren muss die neueste Update-Datei installiert werden.",
    "en": "The latest update file must be installed before continuing."
   }
  },
  {
   "code": "NP-102944-0",
   "kind": "system_update",
   "text": {
    "de": "Die neueste Systemsoftware muss installiert werden.",
    "en": "The latest system software must be installed."
   }
  },
  {
   "code": "NP-102945-1",
   "kind": "system_update",
   "text": {
    "de": "Die neueste Systemsoftware muss installiert werden.",
    "en": "The latest system software must be installed."
   }
  },
  {
   "code": "NP-102942-8",
   "kind": "restriction",
   "text": {
    "de": "Die Funktion ist durch Altersgrenzen eingeschränkt.",
    "en": "The feature is restricted by age limits."
   }
  },
  {
   "code": "NP-102955-2",
   "kind": "account_signin",
   "text": {
    "de": "Die eingegebenen Kontodaten sind falsch.",
    "en": "The account information entered is incorrect."
   }
  },
  {
   "code": "NP-103107-2",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "NP-102947-3",
   "kind": "system_update",
   "text": {
    "de": "Die Internetverbindung ist für das Update zu langsam oder instabil.",
    "en": "The internet connection is too slow or unstable for the update."
   }
  },
  {
   "code": "NP-103105-0",
   "kind": "maintenance",
   "text": {
    "de": "Der Dienst wird gewartet.",
    "en": "The service is under maintenance."
   }
  },
  {
   "code": "NP-103109-4",
   "kind": "maintenance",
   "text": {
    "de": "Der Dienst wird gewartet.",
    "en": "The service is under maintenance."
   }
  },
  {
   "code": "NP-103111-7",
   "kind": "maintenance",
   "text": {
    "de": "Der Dienst wird gewartet.",
    "en": "The service is under maintenance."
   }
  },
  {
   "code": "NP-103117-3",
   "kind": "maintenance",
   "text": {
    "de": "Der Dienst wird gewartet.",
    "en": "The service is under maintenance."
   }
  },
  {
   "code": "NP-102937-2",
   "kind": "account_signin",
   "text": {
    "de": "Der Benutzer ist abgemeldet.",
    "en": "The user is signed out."
   }
  },
  {
   "code": "NP-103539-1",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "NP-103028-4",
   "kind": "account_signin",
   "text": {
    "de": "Beim Anmelden ist ein Problem aufgetreten.",
    "en": "A problem occurred while signing in."
   }
  },
  {
   "code": "NP-104433-5",
   "kind": "account_signin",
   "text": {
    "de": "Problem mit dem Anmeldestatus.",
    "en": "Problem with the sign-in status."
   }
  },
  {
   "code": "NP-102943-9",
   "kind": "account_signin",
   "text": {
    "de": "Die Anwendung kann für diesen Benutzer nicht gestartet werden.",
    "en": "The application cannot launch for this user."
   }
  },
  {
   "code": "NP-103047-5",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "NP-103172-4",
   "kind": "network",
   "text": {
    "de": "Netzwerkverbindungsfehler.",
    "en": "Network connection error."
   }
  },
  {
   "code": "NP-103019-4",
   "kind": "app_error",
   "text": {
    "de": "Anwendungsfehler.",
    "en": "Application error."
   }
  },
  {
   "code": "NP-104303-1",
   "kind": "network",
   "text": {
    "de": "Problem bei der Kommunikation mit dem Server.",
    "en": "Problem communicating with the server."
   }
  },
  {
   "code": "NP-104291-7",
   "kind": "license",
   "text": {
    "de": "Die Lizenzdaten des Inhalts lassen sich nicht bestätigen.",
    "en": "The content licence information cannot be confirmed."
   }
  },
  {
   "code": "NP-103191-5",
   "kind": "network",
   "text": {
    "de": "Die Online-Verbindung ist fehlgeschlagen.",
    "en": "The online connection failed."
   }
  },
  {
   "code": "NP-104627-0",
   "kind": "network",
   "text": {
    "de": "Die Verbindung zum Server ist abgerissen.",
    "en": "The connection to the server was lost."
   }
  },
  {
   "code": "NP-103148-7",
   "kind": "network",
   "text": {
    "de": "Problem beim Verbinden der Konsole mit dem Internet.",
    "en": "Problem connecting the console to the internet."
   }
  },
  {
   "code": "WS-116331-5",
   "kind": "suspended",
   "text": {
    "de": "Der Zugriff auf PlayStation ist vorübergehend ausgesetzt.",
    "en": "PlayStation access is temporarily suspended."
   }
  },
  {
   "code": "WS-116328-1",
   "kind": "system_update",
   "text": {
    "de": "Die neueste Systemsoftware muss installiert werden.",
    "en": "The latest system software must be installed."
   }
  },
  {
   "code": "WS-116332-6",
   "kind": "suspended",
   "text": {
    "de": "Der Zugriff der Konsole ist möglicherweise gesperrt oder vorübergehend nicht verfügbar.",
    "en": "Console access may be blocked or temporarily unavailable."
   }
  },
  {
   "code": "WS-116367-4",
   "kind": "suspended",
   "text": {
    "de": "Der Zugriff auf PlayStation ist ausgesetzt.",
    "en": "PlayStation access is suspended."
   }
  },
  {
   "code": "WS-116330-4",
   "kind": "suspended",
   "text": {
    "de": "Der Zugriff auf PlayStation ist vorübergehend ausgesetzt.",
    "en": "PlayStation access is temporarily suspended."
   }
  },
  {
   "code": "WS-116449-5",
   "kind": "maintenance",
   "text": {
    "de": "Der Dienst wird gewartet.",
    "en": "The service is under maintenance."
   }
  },
  {
   "code": "WS-115019-7",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "WS-116521-6",
   "kind": "maintenance",
   "text": {
    "de": "Der Dienst wird gewartet.",
    "en": "The service is under maintenance."
   }
  },
  {
   "code": "WS-115338-1",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "WS-115195-2",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "WS-116439-4",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "WS-116522-7",
   "kind": "system_update",
   "text": {
    "de": "Das Update der Systemsoftware ist fehlgeschlagen.",
    "en": "The system software update failed."
   }
  },
  {
   "code": "WS-116420-4",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "WS-117224-7",
   "kind": "account_signin",
   "text": {
    "de": "Möglicherweise stimmt etwas mit den Einstellungen der Bestätigung in zwei Schritten nicht.",
    "en": "There may be a problem with the 2-step verification settings."
   }
  },
  {
   "code": "WS-117176-3",
   "kind": "account_signin",
   "text": {
    "de": "Möglicherweise stimmt etwas mit den Einstellungen der Bestätigung in zwei Schritten nicht.",
    "en": "There may be a problem with the 2-step verification settings."
   }
  },
  {
   "code": "WS-116329-2",
   "kind": "account_signin",
   "text": {
    "de": "Anmelde-ID und Passwort passen nicht zusammen.",
    "en": "The sign-in ID and password do not match."
   }
  },
  {
   "code": "WS-116518-2",
   "kind": "generic",
   "text": {
    "de": "Interner Fehler.",
    "en": "Internal error."
   }
  },
  {
   "code": "WS-116414-7",
   "kind": "network",
   "text": {
    "de": "Netzwerkverbindungsfehler.",
    "en": "Network connection error."
   }
  },
  {
   "code": "WS-114241-3",
   "kind": "network",
   "text": {
    "de": "Fehler bei der Verbindung zum Server.",
    "en": "Error connecting to the server."
   }
  },
  {
   "code": "WS-114298-5",
   "kind": "network",
   "text": {
    "de": "Die Konsole ist möglicherweise nicht online.",
    "en": "The console may not be online."
   }
  },
  {
   "code": "WS-116483-3",
   "kind": "network",
   "text": {
    "de": "Problem bei der Verbindung zum Server.",
    "en": "Problem connecting to the server."
   }
  },
  {
   "code": "WS-116486-6",
   "kind": "network",
   "text": {
    "de": "Problem bei der Verbindung zum Server.",
    "en": "Problem connecting to the server."
   }
  },
  {
   "code": "WS-116489-9",
   "kind": "network",
   "text": {
    "de": "Fehler beim Verbinden mit PlayStation.",
    "en": "Error connecting to PlayStation."
   }
  },
  {
   "code": "WS-113947-5",
   "kind": "license",
   "text": {
    "de": "Die hinterlegte Zahlungsart ist möglicherweise falsch.",
    "en": "The registered payment method may be incorrect."
   }
  },
  {
   "code": "WS-116334-8",
   "kind": "generic",
   "text": {
    "de": "Fehler bei der Anmeldung bei Sony Pictures Core.",
    "en": "Error signing in to Sony Pictures Core."
   }
  },
  {
   "code": "WS-119699-5",
   "kind": "account_signin",
   "text": {
    "de": "Das Konto konnte nicht angelegt werden.",
    "en": "The account could not be created."
   }
  },
  {
   "code": "WS-116417-0",
   "kind": "network",
   "text": {
    "de": "Fehler beim Verbinden mit PlayStation.",
    "en": "Error connecting to PlayStation."
   }
  },
  {
   "code": "WS-116129-0",
   "kind": "generic",
   "text": {
    "de": "Der Inhalt wurde nicht gefunden oder ist gerade nicht verfügbar.",
    "en": "The content was not found or is currently unavailable."
   }
  },
  {
   "code": "WS-116520-5",
   "kind": "network",
   "text": {
    "de": "Fehler bei der Verbindung zum Server.",
    "en": "Error connecting to the server."
   }
  },
  {
   "code": "NW-102636-8",
   "kind": "network",
   "text": {
    "de": "Zeitüberschreitung bei der Verbindung zu PlayStation.",
    "en": "Connection to PlayStation timed out."
   }
  },
  {
   "code": "NW-102265-6",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "NW-102254-4",
   "kind": "network",
   "text": {
    "de": "Fehler beim Verbinden mit PlayStation.",
    "en": "Error connecting to PlayStation."
   }
  },
  {
   "code": "NW-102633-5",
   "kind": "network",
   "text": {
    "de": "Fehler beim Verbinden mit PlayStation.",
    "en": "Error connecting to PlayStation."
   }
  },
  {
   "code": "NW-102314-1",
   "kind": "network",
   "text": {
    "de": "Kommunikationsfehler bei der Verbindung zu PlayStation.",
    "en": "Communication error while connecting to PlayStation."
   }
  },
  {
   "code": "NW-102311-8",
   "kind": "network",
   "text": {
    "de": "Kommunikationsfehler bei der Verbindung zu PlayStation.",
    "en": "Communication error while connecting to PlayStation."
   }
  },
  {
   "code": "NW-102307-3",
   "kind": "network",
   "text": {
    "de": "Kommunikationsfehler bei der Verbindung zu PlayStation.",
    "en": "Communication error while connecting to PlayStation."
   }
  },
  {
   "code": "NW-102261-2",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "NW-102315-2",
   "kind": "network",
   "text": {
    "de": "Allgemeiner Kommunikationsfehler.",
    "en": "General communication error."
   }
  },
  {
   "code": "NW-102650-4",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "NW-102417-5",
   "kind": "network",
   "text": {
    "de": "Zeitüberschreitung der Netzwerkverbindung.",
    "en": "The network connection timed out."
   }
  },
  {
   "code": "NW-102308-4",
   "kind": "network",
   "text": {
    "de": "Allgemeiner Kommunikationsfehler.",
    "en": "General communication error."
   }
  },
  {
   "code": "NW-102655-9",
   "kind": "app_error",
   "text": {
    "de": "Fehler mit der Anwendung.",
    "en": "Error with the application."
   }
  },
  {
   "code": "NW-102216-2",
   "kind": "network",
   "text": {
    "de": "Die Konsole erreicht die PlayStation-Server nicht.",
    "en": "The console cannot reach the PlayStation servers."
   }
  },
  {
   "code": "NW-102379-2",
   "kind": "network",
   "text": {
    "de": "Fehler beim Verbinden mit PlayStation.",
    "en": "Error connecting to PlayStation."
   }
  },
  {
   "code": "NW-102468-1",
   "kind": "network",
   "text": {
    "de": "Problem beim Verbinden der Konsole mit dem Netzwerk.",
    "en": "Problem connecting the console to the network."
   }
  },
  {
   "code": "WV-109168-5",
   "kind": "network",
   "text": {
    "de": "Fehler beim Verbinden mit PlayStation.",
    "en": "Error connecting to PlayStation."
   }
  },
  {
   "code": "WV-109144-9",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "WV-109166-3",
   "kind": "network",
   "text": {
    "de": "Keine Verbindung zum Internet.",
    "en": "Cannot connect to the internet."
   }
  },
  {
   "code": "WV-109145-0",
   "kind": "network",
   "text": {
    "de": "Keine Verbindung zum Internet.",
    "en": "Cannot connect to the internet."
   }
  },
  {
   "code": "WV-109153-9",
   "kind": "network",
   "text": {
    "de": "Zeitüberschreitung der Netzwerkverbindung.",
    "en": "The network connection timed out."
   }
  },
  {
   "code": "WV-109146-1",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "SU-101495-0",
   "kind": "info",
   "text": {
    "de": "Das System ist bereits auf dem neuesten Stand.",
    "en": "The system is already up to date."
   }
  },
  {
   "code": "SU-101193-5",
   "kind": "system_update",
   "text": {
    "de": "Die Systemsoftware soll auf die neueste Version aktualisiert werden.",
    "en": "The system software should be updated to the latest version."
   }
  },
  {
   "code": "SU-101312-8",
   "kind": "disc_drive",
   "text": {
    "de": "Das Blu-ray-Laufwerk wird nicht erkannt.",
    "en": "The Blu-ray drive is not detected."
   }
  },
  {
   "code": "SU-101337-5",
   "kind": "boot_problem",
   "text": {
    "de": "Die Konsole ist möglicherweise nicht richtig gestartet.",
    "en": "The console may not have started properly."
   }
  },
  {
   "code": "E2-8223B400",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "E2-8223B089",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  },
  {
   "code": "E2-817B122B",
   "kind": "network",
   "text": {
    "de": "Der Server ist nicht erreichbar.",
    "en": "The server cannot be reached."
   }
  }
 ]
}
)JSON";

}  // namespace medic::ui
