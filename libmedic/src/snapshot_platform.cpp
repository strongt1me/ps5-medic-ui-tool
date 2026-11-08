#include "medic/platform.hpp"
#include "medic/posix_fs.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace medic {
namespace {

nlohmann::json load_json_or_null(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return nullptr;
    std::ostringstream ss;
    ss << in.rdbuf();
    return nlohmann::json::parse(ss.str(), nullptr, /*allow_exceptions=*/false);
}

class SnapshotProcs final : public IProcessList {
public:
    explicit SnapshotProcs(const fs::path& root) {
        auto j = load_json_or_null(root / "procs.json");
        if (!j.is_array()) return;
        for (auto& v : j) {
            if (v.is_string()) {
                infos_.push_back({v.get<std::string>(), 0});
            } else if (v.is_object() && v.contains("name") && v["name"].is_string()) {
                infos_.push_back({v["name"].get<std::string>(), v.value("start", std::int64_t{0})});
            }
        }
    }
    std::vector<std::string> running() const override {
        std::vector<std::string> names;
        for (auto& i : infos_) names.push_back(i.name);
        return names;
    }
    std::vector<ProcInfo> running_info() const override { return infos_; }

private:
    std::vector<ProcInfo> infos_;
};

class SnapshotSys final : public ISystemInfo {
public:
    explicit SnapshotSys(const fs::path& root) {
        auto j = load_json_or_null(root / "system.json");
        if (!j.is_object()) return;
        fw_ = j.value("fw", std::string());
        model_ = j.value("model", std::string());
        boot_ = j.value("boot", std::int64_t{0});
        if (j.contains("ports") && j["ports"].is_object()) {
            for (auto& [port, open] : j["ports"].items()) {
                if (!open.is_boolean()) continue;
                const long n = std::strtol(port.c_str(), nullptr, 10);
                if (n > 0 && n < 65536) ports_[static_cast<int>(n)] = open.get<bool>() ? PortState::Open : PortState::Closed;
            }
        }
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
        if (j.contains("components") && j["components"].is_object()) {
            for (auto& [name, props] : j["components"].items()) {
                if (!props.is_object()) continue;
                for (auto& [k, v] : props.items())
                    if (v.is_string()) components_[name][k] = v.get<std::string>();
            }
        }
    }
    std::string firmware() const override { return fw_; }
    std::string model() const override { return model_; }
    std::vector<StorageInfo> storage() const override { return storage_; }
    std::map<std::string, std::map<std::string, std::string>> components() const override { return components_; }
    std::int64_t boot_epoch() const override { return boot_; }
    PortState local_port_state(int port) const override {
        auto it = ports_.find(port);
        return it == ports_.end() ? PortState::Unknown : it->second;
    }

private:
    std::int64_t boot_ = 0;
    std::map<int, PortState> ports_;
    std::string fw_, model_;
    std::vector<StorageInfo> storage_;
    std::map<std::string, std::map<std::string, std::string>> components_;
};

class SnapshotPlatform final : public Platform {
public:
    SnapshotPlatform(const std::string& root, bool writable)
        : fs_(fs::path(root) / "fs"), rw_(fs::path(root) / "fs"), writable_(writable), procs_(fs::path(root)), sys_(fs::path(root)) {}
    const IFileSystem& fs() const override { return fs_; }
    IFileSystemRW* fs_rw() override { return writable_ ? &rw_ : nullptr; }
    const IProcessList& procs() const override { return procs_; }
    const ISystemInfo& sys() const override { return sys_; }

private:
    PosixFs fs_;
    PosixRw rw_;
    bool writable_;
    SnapshotProcs procs_;
    SnapshotSys sys_;
};

}  // namespace

std::unique_ptr<Platform> make_snapshot_platform(const std::string& root_dir, bool writable) {
    return std::make_unique<SnapshotPlatform>(root_dir, writable);
}

}  // namespace medic
