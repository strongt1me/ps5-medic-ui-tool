#include "medic/posix_fs.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace medic {
namespace {

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
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool PosixRw::rename(const std::string& from, const std::string& to) {
    fs::path a, b;
    std::error_code ec;
    if (!map_virtual_path(base_, from, a) || !map_virtual_path(base_, to, b)) return false;
    if (!fs::exists(a, ec) || fs::exists(b, ec)) return false;
    fs::create_directories(b.parent_path(), ec);
    fs::rename(a, b, ec);
    return !ec;
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
