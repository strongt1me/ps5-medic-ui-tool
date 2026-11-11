// SPDX-License-Identifier: GPL-3.0-or-later
// Plattform-Schnittstellen. Der Kern kennt nur diese Interfaces (siehe docs/ARCHITECTURE.md).
// Alle Pfade sind virtuelle, absolute PS5-Pfade (z. B. "/data/shadowmount/config.ini").
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace medic {

struct DirEntry {
    std::string name;
    bool is_dir = false;
    std::uint64_t size = 0;
};

// Nur lesender Zugriff. Schreibzugriffe kommen in Phase 1 ausschliesslich ueber die FixEngine.
class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual bool exists(const std::string& path) const = 0;
    virtual bool is_dir(const std::string& path) const = 0;
    // Sortiert nach Name. Leerer Vektor, wenn Pfad fehlt oder kein Verzeichnis ist.
    virtual std::vector<DirEntry> list_dir(const std::string& path) const = 0;
    virtual bool read_file(const std::string& path, std::string& out) const = 0;
    // Liest hoechstens `len` Bytes ab `offset` (fuer Image-Header, ohne die ganze Datei zu laden).
    virtual bool read_range(const std::string& path, std::uint64_t offset, std::size_t len, std::string& out) const = 0;
};

// Schreibzugriff. Wird ausschliesslich von der FixEngine benutzt (Pfad-Whitelist, Backup, Journal).
class IFileSystemRW {
public:
    virtual ~IFileSystemRW() = default;
    // Schreibt atomar (Temp-Datei + Umbenennen), legt fehlende Elternordner an.
    virtual bool write_file(const std::string& path, const std::string& content) = 0;
    // Benennt Datei oder Ordner um/verschiebt (gleiches Laufwerk). Scheitert, wenn `to` schon existiert.
    virtual bool rename(const std::string& from, const std::string& to) = 0;
    virtual bool make_dirs(const std::string& path) = 0;
    // Entfernt einen LEEREN Ordner (nur fuer Ordner, die die FixEngine selbst angelegt hat). Nie fuer Dateien/gefuellte Ordner.
    virtual bool remove_empty_dir(const std::string& path) = 0;
    // Haengt Text ans Dateiende (legt Datei und Ordner an) und sichert ihn auf den Datentraeger (fsync). Fuer das Journal:
    // ein Stromausfall beschneidet hoechstens die letzte Zeile, ohne die Datei jedes Mal komplett neu zu schreiben.
    // Standard: nicht unterstuetzt (false); die Engine schreibt dann die ganze Datei neu.
    virtual bool append_file(const std::string& path, const std::string& content) {
        (void)path;
        (void)content;
        return false;
    }
    // Ueberschreibt `content.size()` Bytes ab `offset` IN DER BESTEHENDEN Datei (legt sie an, falls sie fehlt; kuerzt oder loescht nie). Nur fuer
    // den Schreibtest des Selbsttests (selftest.hpp) im eigenen Ordner. sync = fsync danach. Standard: nicht unterstuetzt (false).
    virtual bool write_range(const std::string& path, std::uint64_t offset, const std::string& content, bool sync) {
        (void)path;
        (void)offset;
        (void)content;
        (void)sync;
        return false;
    }
};

struct ProcInfo {
    std::string name;
    std::int64_t start_epoch = 0;  // Startzeit in Sekunden seit 1970, 0 = unbekannt
};

class IProcessList {
public:
    virtual ~IProcessList() = default;
    virtual std::vector<std::string> running() const = 0;  // Prozessnamen
    // Wie running(), zusaetzlich mit Startzeit. Standard: ohne Startzeiten.
    virtual std::vector<ProcInfo> running_info() const {
        std::vector<ProcInfo> v;
        for (auto& n : running()) v.push_back({n, 0});
        return v;
    }
};

enum class PortState { Unknown, Closed, Open };

struct StorageInfo {
    std::string mount;
    std::uint64_t total_bytes = 0;
    std::uint64_t free_bytes = 0;
    std::string fstype;      // z. B. "exfatfs", "ufs", "msdosfs", "bfs"; leer = unbekannt
    bool read_only = false;  // schreibgeschuetzt eingehaengt (MNT_RDONLY)
};

// Temperaturen in Grad Celsius; -1 = nicht lesbar. Momentaufnahme zum Zeitpunkt des Checks.
struct Thermal {
    int cpu_c = -1;
    int soc_c = -1;
};

class ISystemInfo {
public:
    virtual ~ISystemInfo() = default;
    virtual std::string firmware() const = 0;  // z. B. "4.03", leer = unbekannt
    virtual std::string model() const = 0;
    virtual std::vector<StorageInfo> storage() const = 0;
    // Bekannte Komponenten mit Eigenschaften: {"kstuff": {"version": "1.10", "origin": "echostretch"}, ...}.
    // Vorlaeufig: auf der Konsole ist die Quelle noch zu klaeren (ROADMAP Phase 2).
    virtual std::map<std::string, std::map<std::string, std::string>> components() const = 0;
    // Zeitpunkt des letzten Konsolenstarts (Sekunden seit 1970), 0 = unbekannt.
    virtual std::int64_t boot_epoch() const { return 0; }
    // CPU- und SoC-Temperatur. Standard: unbekannt (Plattform kann nicht messen).
    virtual Thermal thermal() const { return {}; }
    // Nimmt ein lokaler TCP-Port Verbindungen an? Unknown, wenn die Plattform das nicht pruefen kann
    // oder die Antwort nicht eindeutig war (Zeitueberschreitung zaehlt nicht als geschlossen).
    virtual PortState local_port_state(int port) const {
        (void)port;
        return PortState::Unknown;
    }
    // Standard-Gateway (IPv4, Punktschreibweise) aus der Routentabelle; leer = unbekannt.
    virtual std::string default_gateway() const { return {}; }
    // Ein einzelner TCP-Verbindungsversuch zu einer Adresse im LOKALEN Netz (nur der Verbindungsaufbau, es werden keine Daten gesendet): Connected und
    // Refused zeigen, dass der Rechner antwortet (Refused = ein Port ohne Dienst antwortet mit RST); Timeout und Error nicht. rtt_ms = Dauer bis zur Antwort.
    enum class ProbeResult { Connected, Refused, Timeout, Error };
    struct ProbeOutcome {
        ProbeResult result = ProbeResult::Error;
        double rtt_ms = 0;
    };
    virtual ProbeOutcome tcp_probe(const std::string& ipv4, int port, int timeout_ms) const {
        (void)ipv4;
        (void)port;
        (void)timeout_ms;
        return {};
    }
    // Liest hoechstens `max_bytes` Rohdaten von einem lokalen TCP-Port (127.0.0.1), solange Daten kommen, hoechstens `timeout_ms`. Nur lesend:
    // es wird nichts gesendet. true, wenn die Verbindung zustande kam (auch ohne Daten). Gedacht fuer den Kernel-Log-Server (klogsrv, Port 3232):
    // Das Lesen VERBRAUCHT, was sich seit dem letzten Leser angesammelt hat (der Kernel-Log gibt jede Zeile nur einmal heraus).
    virtual bool read_local_stream(int port, std::size_t max_bytes, int timeout_ms, std::string& out) const {
        (void)port;
        (void)max_bytes;
        (void)timeout_ms;
        out.clear();
        return false;
    }
};

// Buendelt die drei Interfaces.
struct Platform {
    virtual ~Platform() = default;
    virtual const IFileSystem& fs() const = 0;
    virtual const IProcessList& procs() const = 0;
    virtual const ISystemInfo& sys() const = 0;
    // nullptr = nur lesend (Standard). Nur die FixEngine darf diesen Zugriff nutzen.
    virtual IFileSystemRW* fs_rw() { return nullptr; }
};

// PC-Implementierung: liest einen Fixture-Ordner
//   <root>/fs/...        simuliert das PS5-Dateisystem ("/data/..." -> <root>/fs/data/...)
//   <root>/procs.json    ["etaHEN", "shadowmountplus", ...]; Eintraege duerfen Objekte sein: {"name":"kstuff.elf","start":1794060758}
//   <root>/system.json   {"fw":"4.03","model":"...","storage":[{"mount":"/data","total":..,"free":..,"fstype":"bfs","ro":false}],
//                         "components":{"kstuff":{"version":"1.10","origin":"echostretch"},"smp":{"version":"1.7"}},
//                         "boot":1794060571, "ports":{"9021":true}, "temp":{"cpu":36,"soc":34}}
//                        (boot, ports, temp sowie fstype/ro je Laufwerk sind optional)
// Fehlende Dateien sind erlaubt (leerer Zustand).
// writable=true erlaubt Schreibzugriffe (fs_rw) direkt im Ordner -- Fixtures vorher kopieren!
std::unique_ptr<Platform> make_snapshot_platform(const std::string& root_dir, bool writable = false);

}  // namespace medic
