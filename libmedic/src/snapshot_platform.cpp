// SPDX-License-Identifier: GPL-3.0-or-later
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
        {  // klog.txt: der simulierte Kernel-Log (Port 3232); fehlt die Datei, ist der Port nicht erreichbar
            std::ifstream in(root / "klog.txt", std::ios::binary);
            if (in) {
                std::ostringstream ss;
                ss << in.rdbuf();
                klog_ = ss.str();
                have_klog_ = true;
            }
        }
        auto j = load_json_or_null(root / "system.json");
        if (!j.is_object()) return;
        fw_ = j.value("fw", std::string());
        model_ = j.value("model", std::string());
        boot_ = j.value("boot", std::int64_t{0});
        now_ = j.value("now", std::int64_t{0});
        gateway_ = j.value("gateway", std::string());
        if (j.contains("net_probe") && j["net_probe"].is_object()) {  // Fixture: so antwortet das Gateway (reply, rtt_ms, loss_every = jeder n-te Versuch geht verloren)
            const auto& n = j["net_probe"];
            probe_scripted_ = true;
            probe_reply_ = n.value("reply", true);
            probe_rtt_ = n.value("rtt_ms", 2.0);
            probe_loss_every_ = n.value("loss_every", 0);
        }
        if (j.contains("local_api") && j["local_api"].is_object())  // Fixture: Antworten lokaler Dienste, Schluessel "<port><pfad>"
            for (auto& [key, v] : j["local_api"].items()) api_[key] = v.is_string() ? v.get<std::string>() : v.dump();
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
                if (s.contains("fstype") && s["fstype"].is_string()) si.fstype = s["fstype"].get<std::string>();
                if (s.contains("ro") && s["ro"].is_boolean()) si.read_only = s["ro"].get<bool>();
                storage_.push_back(std::move(si));
            }
        }
        if (j.contains("temp") && j["temp"].is_object()) {
            const auto& t = j["temp"];
            if (t.contains("cpu") && t["cpu"].is_number_integer()) thermal_.cpu_c = t["cpu"].get<int>();
            if (t.contains("soc") && t["soc"].is_number_integer()) thermal_.soc_c = t["soc"].get<int>();
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
    std::int64_t now_epoch() const override { return now_; }
    Thermal thermal() const override { return thermal_; }
    std::string default_gateway() const override { return gateway_; }
    ProbeOutcome tcp_probe(const std::string& ipv4, int port, int timeout_ms) const override {
        (void)ipv4;
        (void)port;
        (void)timeout_ms;
        if (!probe_scripted_) return {};
        ++probe_count_;
        if (!probe_reply_ || (probe_loss_every_ > 0 && probe_count_ % probe_loss_every_ == 0)) return {ProbeResult::Timeout, 1000.0};
        return {ProbeResult::Refused, probe_rtt_};
    }
    bool read_local_stream(int port, std::size_t max_bytes, int timeout_ms, std::string& out) const override {
        (void)timeout_ms;
        out.clear();
        if (port != 3232 || !have_klog_) return false;
        out = klog_.substr(0, max_bytes);
        return true;
    }
    bool local_api_get(int port, const std::string& path, std::string& body) const override {
        body.clear();
        if (port != 8097 || (path != "/api/state" && path != "/api/ampr")) return false;
        auto it = api_.find(std::to_string(port) + path);
        if (it == api_.end()) return false;
        body = it->second;
        return true;
    }
    PortState local_port_state(int port) const override {
        auto it = ports_.find(port);
        return it == ports_.end() ? PortState::Unknown : it->second;
    }

private:
    std::int64_t boot_ = 0, now_ = 0;
    std::string klog_, gateway_;
    bool probe_scripted_ = false, probe_reply_ = true;
    double probe_rtt_ = 2.0;
    int probe_loss_every_ = 0;
    mutable int probe_count_ = 0;
    bool have_klog_ = false;
    Thermal thermal_;
    std::map<int, PortState> ports_;
    std::map<std::string, std::string> api_;
    std::string fw_, model_;
    std::vector<StorageInfo> storage_;
    std::map<std::string, std::map<std::string, std::string>> components_;
};

// Schreibzugriff auf einen Fixture-Ordner: alles geht an PosixRw, nur set_mode nicht, wenn der Ordner eine Modustabelle (modes.json) hat: dann aendert es die Tabelle
// (sonst wuerden die echten Dateimodi des Git-Checkouts veraendert, die file_mode gar nicht liest)
class SnapshotRw final : public IFileSystemRW {
public:
    SnapshotRw(PosixRw& rw, PosixFs& fs) : rw_(rw), fs_(fs) {}
    bool write_file(const std::string& p, const std::string& c) override { return rw_.write_file(p, c); }
    bool rename(const std::string& a, const std::string& b) override { return rw_.rename(a, b); }
    bool make_dirs(const std::string& p) override { return rw_.make_dirs(p); }
    bool remove_empty_dir(const std::string& p) override { return rw_.remove_empty_dir(p); }
    bool append_file(const std::string& p, const std::string& c) override { return rw_.append_file(p, c); }
    bool write_range(const std::string& p, std::uint64_t off, const std::string& c, bool sync) override { return rw_.write_range(p, off, c, sync); }
    bool set_mode(const std::string& p, std::uint32_t mode) override {
        if (!fs_.has_mode_table()) return rw_.set_mode(p, mode);
        std::uint32_t cur = 0;
        if (!fs_.file_mode(p, cur)) return false;  // nur, was die Tabelle kennt
        fs_.table_set(p, mode);
        return true;
    }

private:
    PosixRw& rw_;
    PosixFs& fs_;
};

class SnapshotPlatform final : public Platform {
public:
    SnapshotPlatform(const std::string& root, bool writable)
        : fs_(fs::path(root) / "fs"), rw_(fs::path(root) / "fs"), writable_(writable), procs_(fs::path(root)), sys_(fs::path(root)) {
        // modes.json: {"/pfad/datei": 438 oder "0666", ...}. Ohne die Datei kennt der Fixture-Ordner keine Dateimodi (sie kaemen aus dem Git-Checkout).
        const auto j = load_json_or_null(fs::path(root) / "modes.json");
        if (j.is_object()) {
            std::map<std::string, std::uint32_t> table;
            for (auto it = j.begin(); it != j.end(); ++it) {
                if (it.value().is_number_integer()) table[it.key()] = it.value().get<std::uint32_t>() & 07777u;
                else if (it.value().is_string()) table[it.key()] = static_cast<std::uint32_t>(std::strtoul(it.value().get<std::string>().c_str(), nullptr, 8)) & 07777u;
            }
            fs_.set_mode_table(std::move(table));
        }
    }
    const IFileSystem& fs() const override { return fs_; }
    IFileSystemRW* fs_rw() override { return writable_ ? &srw_ : nullptr; }
    const IProcessList& procs() const override { return procs_; }
    const ISystemInfo& sys() const override { return sys_; }

private:
    PosixFs fs_;
    PosixRw rw_;
    SnapshotRw srw_{rw_, fs_};
    bool writable_;
    SnapshotProcs procs_;
    SnapshotSys sys_;
};

}  // namespace

std::unique_ptr<Platform> make_snapshot_platform(const std::string& root_dir, bool writable) {
    return std::make_unique<SnapshotPlatform>(root_dir, writable);
}

}  // namespace medic
