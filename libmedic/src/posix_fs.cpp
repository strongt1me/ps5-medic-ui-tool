// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/posix_fs.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
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
    // Obergrenze gegen Speichermangel bei riesigen Dateien (Logs lesen Aufrufer mit read_range, wenn sie nur das Ende brauchen)
    const std::uintmax_t size = fs::file_size(p, ec);
    if (ec || size > (256u << 20)) return false;
    // fread und ferror statt "ss << in.rdbuf()": bei einem Lesefehler (EIO) setzt der Dateistrom dort kein badbit, und eine unlesbare Datei galt als leer.
    std::FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) return false;
    std::string buf;
    char chunk[1 << 16];
    for (std::size_t n; (n = std::fread(chunk, 1, sizeof chunk, f)) > 0;) buf.append(chunk, n);
    const bool failed = std::ferror(f) != 0;
    std::fclose(f);
    if (failed) return false;  // Lesefehler: nicht als Teilstueck "erfolgreich" melden
    out = std::move(buf);
    return true;
}

// Ab der dritten Pfadstufe (/data/<Wurzel>/<Unterordner>/...) darf kein Ordner auf dem Weg ein Symlink sein: O_NOFOLLOW und der Test beim Umbenennen
// schuetzen nur die letzte Komponente; ein Symlink dazwischen (z. B. /data/<Scanwurzel>/x -> /user/app) wuerde Schreiben und Verschieben aus
// den erlaubten Ordnern hinausfuehren. Die ersten beiden Stufen (/data, /mnt/usb0 ...) bleiben frei, weil sie auf anderen Firmwares Einhaengepunkte
// sein koennen. `count_all` zaehlt auch die letzte Komponente mit (Ordner, die angelegt werden).
bool ancestors_clean(const fs::path& base, const std::string& vpath, bool count_all) {
    std::vector<std::string> segs;
    std::stringstream ss(vpath);
    std::string seg;
    while (std::getline(ss, seg, '/'))
        if (!seg.empty() && seg != ".") segs.push_back(seg);
    const std::size_t last = count_all ? segs.size() : (segs.empty() ? 0 : segs.size() - 1);
    fs::path cur = base;
    std::error_code ec;
    for (std::size_t i = 0; i < last; ++i) {
        cur /= segs[i];
        if (i >= 2 && fs::is_symlink(cur, ec)) return false;
    }
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

namespace {
std::string normalized_virtual(const std::string& path) {  // doppelte und abschliessende Schraegstriche weglassen
    std::string norm;
    std::stringstream ss(path);
    std::string seg;
    while (std::getline(ss, seg, '/'))
        if (!seg.empty() && seg != ".") norm += "/" + seg;
    return norm;
}
}  // namespace

void PosixFs::table_set(const std::string& path, std::uint32_t mode) { table_[normalized_virtual(path)] = mode & 07777u; }

bool PosixFs::file_mode(const std::string& path, std::uint32_t& mode) const {
    if (use_table_) {
        const auto it = table_.find(normalized_virtual(path));
        if (it == table_.end()) return false;
        mode = it->second;
        return true;
    }
    fs::path p;
    struct stat st;
    if (!modes_ || !map_virtual_path(base_, path, p) || ::lstat(p.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) return false;  // Symlinks und Sonderdateien: unbekannt
    mode = static_cast<std::uint32_t>(st.st_mode & 07777);
    return true;
}

std::vector<DirEntry> PosixFs::list_dir(const std::string& path) const {
    std::vector<DirEntry> out;
    fs::path p;
    std::error_code ec;
    if (!map_virtual_path(base_, path, p) || !fs::is_directory(p, ec)) return out;
    for (fs::directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
        DirEntry e;
        e.name = it->path().filename().string();
        std::error_code entry_ec;  // ein einzelner nicht lesbarer Eintrag beendet das Auflisten nicht
        e.is_dir = it->is_directory(entry_ec);
        if (entry_ec) e.is_dir = false;
        if (!e.is_dir) {
            const std::uintmax_t size = it->file_size(entry_ec);
            e.size = entry_ec ? 0 : size;  // im Fehlerfall liefert file_size 2^64-1
        }
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
    if (!map_virtual_path(base_, path, p) || !ancestors_clean(base_, path, false)) return false;
    // Ein Symlink als Ziel wuerde durch eine normale Datei ersetzt, und "rueckgaengig" stellte den Link nicht wieder her: ablehnen
    if (fs::is_symlink(p, ec)) return false;
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

bool PosixRw::set_mode(const std::string& path, std::uint32_t mode) {
    fs::path p;
    if (!map_virtual_path(base_, path, p) || !ancestors_clean(base_, path, false) || (mode & ~07777u) != 0) return false;
    // O_NOFOLLOW + fchmod statt lstat + chmod: zwischen Pruefen und Setzen kann kein Symlink untergeschoben werden
    const int fd = ::open(p.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return false;
    struct stat st;
    const bool ok = ::fstat(fd, &st) == 0 && (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) && ::fchmod(fd, static_cast<mode_t>(mode)) == 0;
    ::close(fd);
    return ok;
}

bool PosixRw::write_range(const std::string& path, std::uint64_t offset, const std::string& content, bool sync) {
    fs::path p;
    std::error_code ec;
    if (!map_virtual_path(base_, path, p) || !ancestors_clean(base_, path, false)) return false;
    fs::create_directories(p.parent_path(), ec);
    const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW, 0666);  // ohne O_TRUNC: die Datei wird nur ueberschrieben, nie gekuerzt
    if (fd < 0) return false;
    bool ok = true;
    std::size_t done = 0;
    while (ok && done < content.size()) {
        const ssize_t w = ::pwrite(fd, content.data() + done, content.size() - done, static_cast<off_t>(offset + done));
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) ok = false;
        else done += static_cast<std::size_t>(w);
    }
    if (ok && sync) ok = sync_fd(fd);
    if (::close(fd) != 0) ok = false;
    return ok;
}

bool PosixRw::append_file(const std::string& path, const std::string& content) {
    fs::path p;
    std::error_code ec;
    if (!map_virtual_path(base_, path, p) || !ancestors_clean(base_, path, false)) return false;
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
    if (!ancestors_clean(base_, from, false) || !ancestors_clean(base_, to, false)) return false;
    if (fs::is_symlink(a, ec)) return false;  // ein Symlink kann ausserhalb der erlaubten Ordner zeigen
    if (!fs::exists(a, ec)) return false;
    if (fs::exists(fs::symlink_status(b, ec))) {  // auch ein haengender Symlink als Ziel zaehlt (exists() loest ihn auf und meldete "frei")
        // Auf exFAT/FAT (ohne Unterscheidung der Schreibweise) ist das Ziel bei einer Umbenennung nur der Schreibweise dieselbe Datei: erlaubt.
        // Alles andere, was schon dort liegt, bleibt tabu.
        std::string na = a.filename().string(), nb = b.filename().string();
        auto fold = [](std::string& s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
        fold(na);
        fold(nb);
        std::error_code ec2;
        if (a == b || a.parent_path() != b.parent_path() || na != nb || !fs::equivalent(a, b, ec2) || ec2) return false;
    }
    // Nur auf demselben Dateisystem (Marc, 14. Nov. 2026; Dump Forge: ein rename() ueber Dateisysteme hinweg kann den PS5-Kernel abstuerzen lassen). Alle Fixes
    // verschieben in den Ordner daneben oder in einen Ordner im selben Scanpfad; das ist hier die letzte Absicherung. Geprueft wird gegen den ersten vorhandenen
    // Ordner auf dem Weg zum Ziel, BEVOR neue Ordner entstehen (sonst bliebe bei einer Ablehnung ein leerer Ordner zurueck).
    {
        fs::path anc = b.parent_path();
        while (anc.has_parent_path() && anc != anc.parent_path() && !fs::exists(anc, ec)) anc = anc.parent_path();
        struct stat sa, sp;
        if (::lstat(a.c_str(), &sa) != 0 || ::stat(anc.c_str(), &sp) != 0 || sa.st_dev != sp.st_dev) return false;
    }
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
    if (!map_virtual_path(base_, path, p) || !ancestors_clean(base_, path, true)) return false;
    fs::create_directories(p, ec);
    return !ec;
}

bool is_check_file_path(const std::string& path) {
    static const std::string dir = "/data/ps5medic/checks/";
    if (path.size() <= dir.size() + 5 || path.compare(0, dir.size(), dir) != 0 || path.compare(path.size() - 5, 5, ".json") != 0) return false;
    const std::string name = path.substr(dir.size(), path.size() - dir.size() - 5);
    if (name.size() > 48) return false;
    for (unsigned char ch : name)
        if (!std::isalnum(ch) && ch != '-' && ch != '_') return false;
    return true;
}

bool PosixRw::remove_check_file(const std::string& path) {
    fs::path p;
    std::error_code ec;
    if (!is_check_file_path(path) || !map_virtual_path(base_, path, p) || !ancestors_clean(base_, path, true)) return false;
    if (fs::is_symlink(p, ec) || !fs::is_regular_file(p, ec)) return false;
    return fs::remove(p, ec) && !ec;
}

bool PosixRw::remove_empty_dir(const std::string& path) {
    fs::path p;
    std::error_code ec;
    // Wie make_dirs: ein Symlink auf dem Weg oder als Ordner selbst fuehrte aus den erlaubten Ordnern hinaus
    if (!map_virtual_path(base_, path, p) || !ancestors_clean(base_, path, true) || fs::is_symlink(p, ec)) return false;
    if (!fs::is_directory(p, ec) || !fs::is_empty(p, ec)) return false;
    return fs::remove(p, ec);
}

}  // namespace medic
