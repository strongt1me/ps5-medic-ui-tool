// SPDX-License-Identifier: GPL-3.0-or-later
// Dateisystem-Zugriff ueber std::filesystem, gespiegelt unter einem Basisordner.
//   base = "/"            -> echtes Dateisystem (PS5, Pfade wie "/data/shadowmount/config.ini")
//   base = "<snap>/fs"    -> Fixture-Ordner am PC (SnapshotPlatform)
// ".." und "\" in virtuellen Pfaden werden abgelehnt.
#pragma once
#include "medic/platform.hpp"

#include <filesystem>

namespace medic {

class PosixFs final : public IFileSystem {
public:
    explicit PosixFs(std::filesystem::path base) : base_(std::move(base)) {}
    bool exists(const std::string& path) const override;
    bool is_dir(const std::string& path) const override;
    std::vector<DirEntry> list_dir(const std::string& path) const override;
    bool read_file(const std::string& path, std::string& out) const override;
    bool read_range(const std::string& path, std::uint64_t offset, std::size_t len, std::string& out) const override;

private:
    std::filesystem::path base_;
};

class PosixRw final : public IFileSystemRW {
public:
    explicit PosixRw(std::filesystem::path base) : base_(std::move(base)) {}
    bool write_file(const std::string& path, const std::string& content) override;
    bool rename(const std::string& from, const std::string& to) override;
    bool make_dirs(const std::string& path) override;
    bool remove_empty_dir(const std::string& path) override;

private:
    std::filesystem::path base_;
};

// Virtueller Pfad -> <base>/<pfad>; false bei relativen Pfaden, ".." oder "\".
bool map_virtual_path(const std::filesystem::path& base, const std::string& vpath, std::filesystem::path& out);

}  // namespace medic
