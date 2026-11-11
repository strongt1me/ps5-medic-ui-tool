// SPDX-License-Identifier: GPL-3.0-or-later
#include "ps5_platform.hpp"

#include "medic/fwver.hpp"
#include "medic/posix_fs.hpp"

#include <sys/mount.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/user.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include <ps5/kernel.h>

// Aus dem ps5-payload-sdk (samples/hwinfo): nicht in den Headern deklariert.
extern "C" int sceKernelGetHwModelName(char*);
// Ebenfalls aus samples/hwinfo; auf der Test-Konsole am 9. Nov. 2026 gelesen (CPU 36 und SoC 34 Grad im Leerlauf).
extern "C" int sceKernelGetCpuTemperature(int*);
extern "C" int sceKernelGetSocSensorTemperature(int, int*);

namespace medic {
namespace {

class Ps5Procs final : public IProcessList {
public:
    std::vector<std::string> running() const override {
        std::vector<std::string> names;
        for (auto& i : running_info()) names.push_back(std::move(i.name));
        return names;
    }

    std::vector<ProcInfo> running_info() const override {
        std::vector<ProcInfo> procs;
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
        size_t len = 0;
        if (sysctl(mib, 4, nullptr, &len, nullptr, 0) != 0) return procs;
        std::vector<char> buf;
        for (int attempt = 0; attempt < 4; ++attempt) {  // Prozessliste kann zwischen den Aufrufen wachsen
            len += len / 4 + 4096;
            buf.assign(len, 0);
            if (sysctl(mib, 4, buf.data(), &len, nullptr, 0) == 0) break;
            if (errno != ENOMEM) return procs;
        }
        for (size_t off = 0; off + sizeof(int) <= len;) {
            const auto* ki = reinterpret_cast<const struct kinfo_proc*>(buf.data() + off);
            if (ki->ki_structsize <= 0 || off + static_cast<size_t>(ki->ki_structsize) > len) break;
            procs.push_back({ki->ki_comm, static_cast<std::int64_t>(ki->ki_start.tv_sec)});
            off += static_cast<size_t>(ki->ki_structsize);
        }
        return procs;
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
            si.fstype = sfs.f_fstypename;
            si.read_only = (sfs.f_flags & MNT_RDONLY) != 0;
            out.push_back(std::move(si));
        }
        return out;
    }

    // Nur plausible Werte (1..150 Grad) zaehlen; alles andere bleibt "unbekannt".
    Thermal thermal() const override {
        Thermal t;
        int v = 0;
        if (sceKernelGetCpuTemperature(&v) == 0 && v >= 1 && v <= 150) t.cpu_c = v;
        v = 0;
        if (sceKernelGetSocSensorTemperature(0, &v) == 0 && v >= 1 && v <= 150) t.soc_c = v;
        return t;
    }

    // Versionen von kstuff, ShadowMount+, etaHEN: auf der Konsole gibt es noch keine verifizierte Quelle
    // (siehe docs/ROADMAP.md Phase 2). Bis dahin leer; Regeln, die Versionen brauchen, feuern dann nicht.
    std::map<std::string, std::map<std::string, std::string>> components() const override { return {}; }

    // kern.boottime; falls die Konsole den Wert nicht liefert, gilt die Startzeit von PID 1 (mini-syscore.elf) als Konsolenstart.
    std::int64_t boot_epoch() const override {
        struct timeval tv;
        std::memset(&tv, 0, sizeof tv);
        size_t len = sizeof tv;
        int mib[2] = {CTL_KERN, KERN_BOOTTIME};
        if (sysctl(mib, 2, &tv, &len, nullptr, 0) == 0 && tv.tv_sec > 0) return static_cast<std::int64_t>(tv.tv_sec);

        struct kinfo_proc ki;
        std::memset(&ki, 0, sizeof ki);
        len = sizeof ki;
        int pid1[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, 1};
        if (sysctl(pid1, 4, &ki, &len, nullptr, 0) == 0 && ki.ki_structsize > 0 && ki.ki_start.tv_sec > 0)
            return static_cast<std::int64_t>(ki.ki_start.tv_sec);
        return 0;
    }

    // Verbindungsversuch auf 127.0.0.1 (ohne etwas zu senden). Nur "Verbindung abgelehnt" zaehlt als geschlossen;
    // Zeitueberschreitung (Loader gerade beschaeftigt) oder andere Fehler bleiben Unknown.
    PortState local_port_state(int port) const override {
        const int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return PortState::Unknown;
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
        struct sockaddr_in sa;
        std::memset(&sa, 0, sizeof sa);
        sa.sin_len = sizeof sa;
        sa.sin_family = AF_INET;
        sa.sin_port = htons(static_cast<uint16_t>(port));
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        PortState state = PortState::Unknown;
        if (connect(s, reinterpret_cast<const struct sockaddr*>(&sa), sizeof sa) == 0) {
            state = PortState::Open;
        } else if (errno == ECONNREFUSED) {
            state = PortState::Closed;
        } else if (errno == EINPROGRESS) {
            struct pollfd pfd = {s, POLLOUT, 0};
            if (poll(&pfd, 1, 300) == 1) {
                int err = 0;
                socklen_t elen = sizeof err;
                if (getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &elen) == 0) {
                    if (err == 0) state = PortState::Open;
                    else if (err == ECONNREFUSED) state = PortState::Closed;
                }
            }
        }
        close(s);
        return state;
    }

    bool read_local_stream(int port, std::size_t max_bytes, int timeout_ms, std::string& out) const override {
        out.clear();
        const int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return false;
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
        struct sockaddr_in sa;
        std::memset(&sa, 0, sizeof sa);
        sa.sin_len = sizeof sa;
        sa.sin_family = AF_INET;
        sa.sin_port = htons(static_cast<uint16_t>(port));
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bool connected = connect(s, reinterpret_cast<const struct sockaddr*>(&sa), sizeof sa) == 0;
        if (!connected && errno == EINPROGRESS) {
            struct pollfd pfd = {s, POLLOUT, 0};
            int err = 1;
            socklen_t elen = sizeof err;
            if (poll(&pfd, 1, 500) == 1 && getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err == 0) connected = true;
        }
        if (!connected) {
            close(s);
            return false;
        }
        // Lesen, solange Daten kommen: nach einer Pause von 300 ms ohne Daten ist der Rueckstau abgearbeitet; insgesamt hoechstens timeout_ms.
        const auto start = std::chrono::steady_clock::now();
        char buf[8192];
        while (out.size() < max_bytes) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeout_ms) break;
            struct pollfd pfd = {s, POLLIN, 0};
            const int pr = poll(&pfd, 1, static_cast<int>(std::min<long long>(300, timeout_ms - elapsed)));
            if (pr <= 0) break;
            const ssize_t n = recv(s, buf, std::min(sizeof buf, max_bytes - out.size()), 0);
            if (n <= 0) break;
            out.append(buf, static_cast<std::size_t>(n));
        }
        close(s);
        return true;
    }
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
