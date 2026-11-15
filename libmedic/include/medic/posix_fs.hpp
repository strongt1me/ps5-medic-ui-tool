// SPDX-License-Identifier: GPL-3.0-or-later
// Dateisystem-Zugriff ueber std::filesystem, gespiegelt unter einem Basisordner.
//   base = "/"            -> echtes Dateisystem (PS5, Pfade wie "/data/shadowmount/config.ini")
//   base = "<snap>/fs"    -> Fixture-Ordner am PC (SnapshotPlatform)
// ".." und "\" in virtuellen Pfaden werden abgelehnt.
#pragma once
#include "medic/platform.hpp"

#include <filesystem>
#include <map>

namespace medic {

class PosixFs final : public IFileSystem {
public:
    // modes = true: file_mode() liefert den Modus (Konsole). Fixture-Ordner am PC lassen es aus: ihre Modi kommen aus dem Git-Checkout, nicht von einer Konsole.
    explicit PosixFs(std::filesystem::path base, bool modes = false) : base_(std::move(base)), modes_(modes) {}
    bool exists(const std::string& path) const override;
    bool is_dir(const std::string& path) const override;
    std::vector<DirEntry> list_dir(const std::string& path) const override;
    bool read_file(const std::string& path, std::string& out) const override;
    bool read_range(const std::string& path, std::uint64_t offset, std::size_t len, std::string& out) const override;
    bool file_mode(const std::string& path, std::uint32_t& mode) const override;
    // Fixture-Ordner am PC (modes.json): file_mode kennt dann nur die Pfade dieser Tabelle (virtueller Pfad -> Modus), nie die echten Modi des Checkouts.
    void set_mode_table(std::map<std::string, std::uint32_t> table) {
        table_ = std::move(table);
        use_table_ = true;
    }
    bool has_mode_table() const { return use_table_; }
    // Aendert einen Eintrag der Tabelle (Schreibzugriff auf einen Fixture-Ordner mit Modustabelle: die echten Dateien des Checkouts bleiben unberuehrt)
    void table_set(const std::string& path, std::uint32_t mode);

private:
    std::filesystem::path base_;
    bool modes_ = false;
    std::map<std::string, std::uint32_t> table_;
    bool use_table_ = false;
};

class PosixRw final : public IFileSystemRW {
public:
    explicit PosixRw(std::filesystem::path base) : base_(std::move(base)) {}
    bool write_file(const std::string& path, const std::string& content) override;
    bool rename(const std::string& from, const std::string& to) override;
    bool make_dirs(const std::string& path) override;
    bool remove_empty_dir(const std::string& path) override;
    bool append_file(const std::string& path, const std::string& content) override;
    bool write_range(const std::string& path, std::uint64_t offset, const std::string& content, bool sync) override;
    bool set_mode(const std::string& path, std::uint32_t mode) override;

private:
    std::filesystem::path base_;
};

// Virtueller Pfad -> <base>/<pfad>; false bei relativen Pfaden, ".." oder "\".
bool map_virtual_path(const std::filesystem::path& base, const std::string& vpath, std::filesystem::path& out);

}  // namespace medic
