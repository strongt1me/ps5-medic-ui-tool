// SPDX-License-Identifier: GPL-3.0-or-later
// Nur lesender Zugriff auf Dateien in Spiel-Images, ohne das Image einzuhaengen: .exfat (exFAT), .ffpfs und .ffpfsc (PFS-Behaelter mit einem
// exFAT-Image, unkomprimiert bzw. PFSC-komprimiert) und .ffpkg (UFS2). Der Aufbau stammt aus dem Leser von PS5 Cooling & System Center
// (libmedic/src/imgread.c); hier haengt er ueber IFileSystem::read_range an, damit der Kern auch am PC gegen Fixtures laeuft.
//
// Das Format erkennt der Leser an den ersten Bloecken, nicht am Dateinamen. Alles Unerwartete (beschaedigtes Image, anderes Format) endet
// mit false bzw. nullptr, nie mit einem wilden Lesezugriff.
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "medic/platform.hpp"

struct imgr;

namespace medic {

class ImageReader {
public:
    // nullptr, wenn die Datei nicht lesbar ist oder kein bekanntes Image-Format hat.
    static std::unique_ptr<ImageReader> open(const IFileSystem& fs, const std::string& image_path);
    // Wie open, mit bekannter Dateigroesse (spart das Auflisten des ganzen Ordners, in dem das Image liegt)
    static std::unique_ptr<ImageReader> open(const IFileSystem& fs, const std::string& image_path, std::uint64_t size);
    ~ImageReader();
    ImageReader(const ImageReader&) = delete;
    ImageReader& operator=(const ImageReader&) = delete;

    const std::string& kind() const { return kind_; }  // "exfat", "ffpfsc", "ffpfs" oder "ffpkg"

    // Liest genau `len` Bytes der Datei `inner_path` (z. B. "/eboot.bin", Namen ohne Beachtung der Gross-/Kleinschreibung) ab `offset`.
    // false, wenn es die Datei nicht gibt, der Bereich ueber das Ende hinausgeht oder ein Lesefehler auftritt.
    bool read_file(const std::string& inner_path, std::uint64_t offset, std::size_t len, std::string& out) const;

    // Liest hoechstens `len` Bytes ab `offset` (am Dateiende weniger); false, wenn es die Datei nicht gibt, nichts gelesen werden konnte oder ein Lesefehler auftritt.
    // Fuer Dateien, deren Groesse man nicht kennt (z. B. eine Laufzeit-Bibliothek, von der man nur den Anfang braucht).
    bool read_some(const std::string& inner_path, std::uint64_t offset, std::size_t len, std::string& out) const;

    // Eintraege eines Ordners im Image (Name, Ordner ja/nein), hoechstens `max`; false, wenn es den Ordner nicht gibt. Namen ohne Beachtung der Schreibweise
    // aufloesen muss der Aufrufer (hier stehen sie so, wie das Image sie fuehrt).
    struct Entry {
        std::string name;
        bool is_dir = false;
    };
    bool list_dir(const std::string& inner_dir, std::vector<Entry>& out, std::size_t max = 4096) const;

private:
    ImageReader() = default;
    struct imgr* r_ = nullptr;
    const IFileSystem* fs_ = nullptr;  // fs_ und path_ sind der Rueckruf-Kontext des Lesers; der ImageReader liegt nur im unique_ptr (feste Adresse)
    std::string path_, kind_;
    friend int image_reader_read_cb(void*, std::uint64_t, void*, std::size_t);
};

}  // namespace medic
