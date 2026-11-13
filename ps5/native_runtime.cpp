// SPDX-License-Identifier: GPL-3.0-or-later
// Speicherverwaltung der nativen App (ersetzt app_cpp_runtime.cpp des Boilerplates): kleine Bloecke wie dort ueber malloc/free, grosse
// (ab 256 KiB, vor allem die 8-MB-Zeichenflaechen der UI) ueber eigene anonyme Speicherseiten. Grund (Konsolentest 12. Nov. 2026): der
// Heap der Sandbox-App ist klein (Probe: 4 bis 512 MB einzeln gingen, aber nach den ersten 25 MB Belegung scheiterte ein weiterer 8-MB-Block ->
// operator new brach per Befehl ud2 ab). Die App kann keinen eigenen Heap-Umfang exportieren (app-symbols.map: keine Exporte).
// Ohne Ausnahmen und RTTI, wie der Rest der App.
#include <cstddef>
#include <cstdint>
#include <climits>
#include <new>
#include <sys/mman.h>

extern "C" {
void* malloc(std::size_t size);
void free(void* address);
int posix_memalign(void** address, std::size_t alignment, std::size_t size);
}

namespace {

constexpr std::size_t kBigThreshold = 256u << 10;
constexpr std::size_t kHeader = 64;  // haelt Groesse und Kennung; der Nutzzeiger liegt dahinter (64-Byte-ausgerichtet)
constexpr std::uint64_t kMagic = 0x4D45444943424947ull;  // "MEDICBIG"
constexpr std::size_t kMaxBig = 1024;
void* g_big[kMaxBig];  // alle ausgegebenen Grossbloecke (Zeiger hinter dem Kopf)

struct Header {
    std::uint64_t magic;
    std::uint64_t total;  // Bytes inklusive Kopf, auf Seiten aufgerundet
};

[[noreturn]] void fail() noexcept { __builtin_trap(); }

void* big_alloc(std::size_t size) noexcept {
    if (size > SIZE_MAX - kHeader - 4095) return nullptr;  // sonst liefe die Rundung ueber und der Block wuerde winzig
    const std::size_t total = (size + kHeader + 4095) & ~static_cast<std::size_t>(4095);
    void* base = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) return nullptr;
    auto* h = static_cast<Header*>(base);
    h->magic = kMagic;
    h->total = total;
    void* user = static_cast<std::uint8_t*>(base) + kHeader;
    for (std::size_t i = 0; i < kMaxBig; ++i)
        if (!g_big[i]) {
            g_big[i] = user;
            return user;
        }
    munmap(base, total);  // Verzeichnis voll (mehr als 1024 Grossbloecke gleichzeitig): nicht ausgeben
    return nullptr;
}

// true, wenn `p` ein Grossblock war (und freigegeben wurde)
bool big_free(void* p) noexcept {
    if ((reinterpret_cast<std::uintptr_t>(p) & 4095u) != kHeader) return false;
    auto* h = reinterpret_cast<Header*>(static_cast<std::uint8_t*>(p) - kHeader);
    if (h->magic != kMagic) return false;
    for (std::size_t i = 0; i < kMaxBig; ++i)
        if (g_big[i] == p) {
            g_big[i] = nullptr;
            const std::uint64_t total = h->total;
            h->magic = 0;
            munmap(h, total);
            return true;
        }
    return false;
}

void* alloc(std::size_t size) noexcept {
    if (size == 0) size = 1;
    if (size >= kBigThreshold)
        if (void* p = big_alloc(size)) return p;  // misslingt das (mmap, Verzeichnis voll), versucht es malloc noch
    return malloc(size);
}

void* alloc_aligned(std::size_t size, std::size_t alignment) noexcept {
    if (alignment <= kHeader && size >= kBigThreshold)
        if (void* big = big_alloc(size)) return big;
    void* address = nullptr;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if ((alignment & (alignment - 1)) != 0) return nullptr;
    return posix_memalign(&address, alignment, size == 0 ? 1 : size) == 0 ? address : nullptr;
}

void release(void* p) noexcept {
    if (!p) return;
    if (!big_free(p)) free(p);
}

}  // namespace

void* operator new(std::size_t size) {
    if (void* p = alloc(size)) return p;
    fail();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return alloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return alloc(size); }
void* operator new(std::size_t size, std::align_val_t a) {
    if (void* p = alloc_aligned(size, static_cast<std::size_t>(a))) return p;
    fail();
}
void* operator new[](std::size_t size, std::align_val_t a) { return ::operator new(size, a); }
void* operator new(std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept { return alloc_aligned(size, static_cast<std::size_t>(a)); }
void* operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept { return alloc_aligned(size, static_cast<std::size_t>(a)); }

void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::align_val_t) noexcept { release(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { release(p); }
