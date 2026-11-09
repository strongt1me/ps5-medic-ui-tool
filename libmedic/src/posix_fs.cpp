// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/posix_fs.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace medic {
namespace {

bool write_all(int fd, const char* data, std::size_t n) {
    while (n > 0) {
        const ssize_t w = ::write(fd, data, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        data += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

// fsync; "nicht unterstuetzt" (EINVAL/ENOTSUP) ist kein Fehler, ein echter Schreibfehler (EIO, ENOSPC) schon.
bool sync_fd(int fd) {
    if (::fsync(fd) == 0) return true;
    return errno == EINVAL || errno == ENOTSUP;
}

// Verzeichniseintrag (Anlegen/Umbenennen) auf den Datentraeger bringen. Best effort: nicht jedes Dateisystem erlaubt das.
void sync_dir(const fs::path& dir) {
    const int fd = ::open(dir.c_str(), O_RDONLY);
    if (fd < 0) return;
    (void)::fsync(fd);
    ::close(fd);
}

bool slurp(const fs::path& p, std::string& out) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

}  // namespace

bool map_virtual_path(const fs::path& base, const std::string& vpath, fs::path& out) {
    if (vpath.empty() || vpath[0] != '/') return false;
    fs::path p = base;
    std::stringstream ss(vpath);
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == ".." || seg.find(char(92)) != std::string::npos) return false;
        p /= seg;
    }
    out = std::move(p);
    return true;
}

// ---- lesen ---------------------------------------------------------------

bool PosixFs::exists(const std::string& path) const {
    fs::path p;
    std::error_code ec;
    return map_virtual_path(base_, path, p) && fs::exists(p, ec);
}

bool PosixFs::is_dir(const std::string& path) const {
    fs::path p;
    std::error_code ec;
    return map_virtual_path(base_, path, p) && fs::is_directory(p, ec);
}

std::vector<DirEntry> PosixFs::list_dir(const std::string& path) const {
    std::vector<DirEntry> out;
    fs::path p;
    std::error_code ec;
    if (!map_virtual_path(base_, path, p) || !fs::is_directory(p, ec)) return out;
    for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
        DirEntry e;
        e.name = it->path().filename().string();
        e.is_dir = it->is_directory(ec);
        if (!e.is_dir) e.size = it->file_size(ec);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
    return out;
}

bool PosixFs::read_file(const std::string& path, std::string& out) const {
    fs::path p;
    return map_virtual_path(base_, path, p) && slurp(p, out);
}

bool PosixFs::read_range(const std::string& path, std::uint64_t offset, std::size_t len, std::string& out) const {
    fs::path p;
    std::error_code ec;
    if (!map_virtual_path(base_, path, p) || !fs::is_regular_file(p, ec)) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    in.seekg(static_cast<std::streamoff>(offset));
    out.assign(len, ' ');
    in.read(out.data(), static_cast<std::streamsize>(len));
    out.resize(static_cast<std::size_t>(in.gcount()));
    return !out.empty();
}

// ---- schreiben (nur ueber die FixEngine) -------------------------------------

bool PosixRw::write_file(const std::string& path, const std::string& content) {
    fs::path p, tmp;
    std::error_code ec;
    if (!map_virtual_path(base_, path, p)) return false;
    fs::create_directories(p.parent_path(), ec);
    tmp = p;
    tmp += ".medic-tmp";
    // Rest eines frueheren Abbruchs wegraeumen (nur unsere Temp-Datei; ein Symlink dieses Namens wird selbst entfernt,
    // nicht sein Ziel) und neu anlegen, ohne einem Symlink zu folgen.
    fs::remove(tmp, ec);
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0666);
    if (fd < 0) return false;
    bool ok = write_all(fd, content.data(), content.size()) && sync_fd(fd);
    if (::close(fd) != 0) ok = false;
    if (ok) {
        fs::rename(tmp, p, ec);
        ok = !ec;
    }
    if (!ok) {
        fs::remove(tmp, ec);
        return false;
    }
    sync_dir(p.parent_path());
    return true;
}

bool PosixRw::append_file(const std::string& path, const std::string& content) {
    fs::path p;
    std::error_code ec;
    if (!map_virtual_path(base_, path, p)) return false;
    fs::create_directories(p.parent_path(), ec);
    const int fd = ::open(p.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW, 0666);
    if (fd < 0) return false;
    const off_t end = ::lseek(fd, 0, SEEK_END);
    // Endet die Datei mitten in einer Zeile (abgebrochener Schreibvorgang), beginnt der neue Eintrag in einer eigenen Zeile.
    std::string data;
    if (end > 0) {
        const int rfd = ::open(p.c_str(), O_RDONLY);
        if (rfd >= 0) {
            char last = '\n';
            if (::pread(rfd, &last, 1, end - 1) == 1 && last != '\n') data = "\n";
            ::close(rfd);
        }
    }
    data += content;
    bool ok = write_all(fd, data.data(), data.size()) && sync_fd(fd);
    if (::close(fd) != 0) ok = false;
    if (ok && end == 0) sync_dir(p.parent_path());
    return ok;
}

bool PosixRw::rename(const std::string& from, const std::string& to) {
    fs::path a, b;
    std::error_code ec;
    if (!map_virtual_path(base_, from, a) || !map_virtual_path(base_, to, b)) return false;
    if (fs::is_symlink(a, ec)) return false;  // ein Symlink kann ausserhalb der erlaubten Ordner zeigen
    if (!fs::exists(a, ec) || fs::exists(b, ec)) return false;
    fs::create_directories(b.parent_path(), ec);
    fs::rename(a, b, ec);
    if (ec) return false;
    sync_dir(b.parent_path());
    if (a.parent_path() != b.parent_path()) sync_dir(a.parent_path());
    return true;
}

bool PosixRw::make_dirs(const std::string& path) {
    fs::path p;
    std::error_code ec;
    if (!map_virtual_path(base_, path, p)) return false;
    fs::create_directories(p, ec);
    return !ec;
}

bool PosixRw::remove_empty_dir(const std::string& path) {
    fs::path p;
    std::error_code ec;
    if (!map_virtual_path(base_, path, p) || !fs::is_directory(p, ec) || !fs::is_empty(p, ec)) return false;
    return fs::remove(p, ec);
}

}  // namespace medic
