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
};

class IProcessList {
public:
    virtual ~IProcessList() = default;
    virtual std::vector<std::string> running() const = 0;  // Prozessnamen
};

struct StorageInfo {
    std::string mount;
    std::uint64_t total_bytes = 0;
    std::uint64_t free_bytes = 0;
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
//   <root>/procs.json    ["etaHEN", "shadowmountplus", ...]
//   <root>/system.json   {"fw":"4.03","model":"...","storage":[{"mount":"/data","total":..,"free":..}],
//                         "components":{"kstuff":{"version":"1.10","origin":"echostretch"},"smp":{"version":"1.7"}}}
// Fehlende Dateien sind erlaubt (leerer Zustand).
// writable=true erlaubt Schreibzugriffe (fs_rw) direkt im Ordner -- Fixtures vorher kopieren!
std::unique_ptr<Platform> make_snapshot_platform(const std::string& root_dir, bool writable = false);

}  // namespace medic
