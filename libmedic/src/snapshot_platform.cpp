#include "medic/platform.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
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

nlohmann::json load_json_or_null(const fs::path& p) {
    std::string text;
    if (!slurp(p, text)) return nullptr;
    return nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
}

class SnapshotFs final : public IFileSystem {
public:
    explicit SnapshotFs(fs::path root) : root_(std::move(root)) {}

    bool exists(const std::string& path) const override {
        fs::path p;
        std::error_code ec;
        return map(path, p) && fs::exists(p, ec);
    }
    bool is_dir(const std::string& path) const override {
        fs::path p;
        std::error_code ec;
        return map(path, p) && fs::is_directory(p, ec);
    }
    std::vector<DirEntry> list_dir(const std::string& path) const override {
        std::vector<DirEntry> out;
        fs::path p;
        std::error_code ec;
        if (!map(path, p) || !fs::is_directory(p, ec)) return out;
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
    bool read_file(const std::string& path, std::string& out) const override {
        fs::path p;
        return map(path, p) && slurp(p, out);
    }

private:
    // Virtueller Pfad -> <root>/fs/<pfad>. ".." wird abgelehnt, damit nichts ausserhalb des Snapshots gelesen wird.
    bool map(const std::string& vpath, fs::path& out) const {
        if (vpath.empty() || vpath[0] != '/') return false;
        fs::path p = root_ / "fs";
        std::stringstream ss(vpath);
        std::string seg;
        while (std::getline(ss, seg, '/')) {
            if (seg.empty() || seg == ".") continue;
            if (seg == ".." || seg.find('\\') != std::string::npos) return false;
            p /= seg;
        }
        out = std::move(p);
        return true;
    }
    fs::path root_;
};

class SnapshotProcs final : public IProcessList {
public:
    explicit SnapshotProcs(const fs::path& root) {
        auto j = load_json_or_null(root / "procs.json");
        if (j.is_array())
            for (auto& v : j)
                if (v.is_string()) names_.push_back(v.get<std::string>());
    }
    std::vector<std::string> running() const override { return names_; }

private:
    std::vector<std::string> names_;
};

class SnapshotSys final : public ISystemInfo {
public:
    explicit SnapshotSys(const fs::path& root) {
        auto j = load_json_or_null(root / "system.json");
        if (!j.is_object()) return;
        fw_ = j.value("fw", std::string());
        model_ = j.value("model", std::string());
        if (j.contains("storage") && j["storage"].is_array()) {
            for (auto& s : j["storage"]) {
                if (!s.is_object()) continue;
                StorageInfo si;
                si.mount = s.value("mount", std::string());
                si.total_bytes = s.value("total", std::uint64_t{0});
                si.free_bytes = s.value("free", std::uint64_t{0});
                storage_.push_back(std::move(si));
            }
        }
    }
    std::string firmware() const override { return fw_; }
    std::string model() const override { return model_; }
    std::vector<StorageInfo> storage() const override { return storage_; }

private:
    std::string fw_, model_;
    std::vector<StorageInfo> storage_;
};

class SnapshotPlatform final : public Platform {
public:
    explicit SnapshotPlatform(const std::string& root) : fs_(fs::path(root)), procs_(fs::path(root)), sys_(fs::path(root)) {}
    const IFileSystem& fs() const override { return fs_; }
    const IProcessList& procs() const override { return procs_; }
    const ISystemInfo& sys() const override { return sys_; }

private:
    SnapshotFs fs_;
    SnapshotProcs procs_;
    SnapshotSys sys_;
};

}  // namespace

std::unique_ptr<Platform> make_snapshot_platform(const std::string& root_dir) {
    return std::make_unique<SnapshotPlatform>(root_dir);
}

}  // namespace medic
