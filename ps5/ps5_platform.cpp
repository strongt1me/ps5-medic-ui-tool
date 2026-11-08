#include "ps5_platform.hpp"

#include "medic/fwver.hpp"
#include "medic/posix_fs.hpp"

#include <sys/mount.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include <ps5/kernel.h>

// Aus dem ps5-payload-sdk (samples/hwinfo): nicht in den Headern deklariert.
extern "C" int sceKernelGetHwModelName(char*);

namespace medic {
namespace {

class Ps5Procs final : public IProcessList {
public:
    std::vector<std::string> running() const override {
        std::vector<std::string> names;
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
        size_t len = 0;
        if (sysctl(mib, 4, nullptr, &len, nullptr, 0) != 0) return names;
        std::vector<char> buf;
        for (int attempt = 0; attempt < 4; ++attempt) {  // Prozessliste kann zwischen den Aufrufen wachsen
            len += len / 4 + 4096;
            buf.assign(len, 0);
            if (sysctl(mib, 4, buf.data(), &len, nullptr, 0) == 0) break;
            if (errno != ENOMEM) return names;
        }
        for (size_t off = 0; off + sizeof(int) <= len;) {
            const auto* ki = reinterpret_cast<const struct kinfo_proc*>(buf.data() + off);
            if (ki->ki_structsize <= 0 || off + static_cast<size_t>(ki->ki_structsize) > len) break;
            names.emplace_back(ki->ki_comm);
            off += static_cast<size_t>(ki->ki_structsize);
        }
        return names;
    }
};

class Ps5Sys final : public ISystemInfo {
public:
    std::string firmware() const override { return format_ps5_fw(kernel_get_fw_version()); }

    std::string model() const override {
        char s[256] = {0};
        return sceKernelGetHwModelName(s) == 0 ? std::string(s) : std::string();
    }

    std::vector<StorageInfo> storage() const override {
        std::vector<StorageInfo> out;
        std::vector<std::string> mounts = {"/data"};
        for (int i = 0; i < 8; ++i) mounts.push_back("/mnt/usb" + std::to_string(i));
        for (int i = 0; i < 2; ++i) mounts.push_back("/mnt/ext" + std::to_string(i));
        for (const auto& m : mounts) {
            struct statfs sfs;
            std::memset(&sfs, 0, sizeof sfs);
            if (statfs(m.c_str(), &sfs) != 0) continue;
            StorageInfo si;
            si.mount = m;
            si.total_bytes = static_cast<std::uint64_t>(sfs.f_blocks) * sfs.f_bsize;
            si.free_bytes = static_cast<std::uint64_t>(sfs.f_bavail > 0 ? sfs.f_bavail : 0) * sfs.f_bsize;
            out.push_back(std::move(si));
        }
        return out;
    }

    // Versionen von kstuff, ShadowMount+, etaHEN: auf der Konsole gibt es noch keine verifizierte Quelle
    // (siehe docs/ROADMAP.md Phase 2). Bis dahin leer; Regeln, die Versionen brauchen, feuern dann nicht.
    std::map<std::string, std::map<std::string, std::string>> components() const override { return {}; }
};

class Ps5Platform final : public Platform {
public:
    explicit Ps5Platform(bool writable) : fs_("/"), rw_("/"), writable_(writable) {}
    const IFileSystem& fs() const override { return fs_; }
    IFileSystemRW* fs_rw() override { return writable_ ? &rw_ : nullptr; }
    const IProcessList& procs() const override { return procs_; }
    const ISystemInfo& sys() const override { return sys_; }

private:
    PosixFs fs_;
    PosixRw rw_;
    bool writable_;
    Ps5Procs procs_;
    Ps5Sys sys_;
};

}  // namespace

std::unique_ptr<Platform> make_ps5_platform(bool writable) { return std::make_unique<Ps5Platform>(writable); }

}  // namespace medic
