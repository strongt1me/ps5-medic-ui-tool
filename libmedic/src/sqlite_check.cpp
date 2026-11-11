// SPDX-License-Identifier: GPL-3.0-or-later
// SQLite-Pruefung im Arbeitsspeicher, siehe sqlite_check.hpp.
//
// Aufbau nach dem Vorbild von PS5 Cleanup 1.0.7 (da_Chris86, GPL-3.0-or-later, db/db_sqlite.c und db/db_pruefen.c): SQLite ohne Dateisystem-Teil
// uebersetzen, die Datenbank per sqlite3_deserialize aus einer Kopie oeffnen und PRAGMA integrity_check ausfuehren. Der Code hier ist eigen
// geschrieben; die Form des VFS-Ersatzes ergibt sich aus der SQLite-Schnittstelle. Siehe THIRD_PARTY.md.
#include "medic/sqlite_check.hpp"

#include "sqlite3/sqlite3.h"

#include <cstdint>
#include <cstring>
#include <ctime>

namespace medic {
namespace {

constexpr std::size_t kMaxImageBytes = 64u << 20;

// VFS-Ersatz "medic-readonly": oeffnet nichts, loescht nichts. Speicher-Datenbanken brauchen kein Dateisystem.
int vfs_open(sqlite3_vfs*, sqlite3_filename, sqlite3_file* f, int, int*) {
    f->pMethods = nullptr;
    return SQLITE_CANTOPEN;
}
int vfs_delete(sqlite3_vfs*, const char*, int) { return SQLITE_IOERR_DELETE; }
int vfs_access(sqlite3_vfs*, const char*, int, int* res) {
    *res = 0;
    return SQLITE_OK;
}
int vfs_fullpath(sqlite3_vfs*, const char* z, int n, char* out) {
    sqlite3_snprintf(n, out, "%s", z);
    return SQLITE_OK;
}
int vfs_random(sqlite3_vfs*, int n, char* out) {
    static unsigned long x = 2463534242UL;
    x ^= static_cast<unsigned long>(std::time(nullptr));
    for (int i = 0; i < n; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        out[i] = static_cast<char>(x & 0xFF);
    }
    return n;
}
int vfs_sleep(sqlite3_vfs*, int us) { return us; }
int vfs_time(sqlite3_vfs*, double* t) {
    *t = 2440587.5 + static_cast<double>(std::time(nullptr)) / 86400.0;
    return SQLITE_OK;
}
int vfs_last_error(sqlite3_vfs*, int, char*) { return 0; }

sqlite3_vfs g_vfs = {1, static_cast<int>(sizeof(sqlite3_file)), 512, nullptr, "medic-readonly", nullptr, vfs_open, vfs_delete, vfs_access, vfs_fullpath,
                     nullptr, nullptr, nullptr, nullptr, vfs_random, vfs_sleep, vfs_time, vfs_last_error, nullptr, nullptr, nullptr, nullptr};

std::uint32_t be32(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) | (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
std::uint32_t le32(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[3]) << 24) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
}

// Pruefsumme des WAL (sqlite.org/fileformat2.html, "Checksum Algorithm"): 32-Bit-Woerter paarweise, fortlaufend ueber Kopf und Eintraege.
void wal_sum(const unsigned char* d, std::size_t n, bool big, std::uint32_t& s1, std::uint32_t& s2) {
    for (std::size_t i = 0; i + 8 <= n; i += 8) {
        const std::uint32_t x0 = big ? be32(d + i) : le32(d + i), x1 = big ? be32(d + i + 4) : le32(d + i + 4);
        s1 += x0 + s2;
        s2 += x1 + s1;
    }
}

// Rechnet die gueltigen, abgeschlossenen Eintraege eines WAL in eine Kopie der Datenbank ein (wie SQLite beim Oeffnen der Datei). Ein WAL mit
// ungueltigem Kopf (Kennung, Seitengroesse, Pruefsumme) ignoriert SQLite, ebenso alles hinter dem ersten ungueltigen Eintrag und alle Eintraege
// nach dem letzten Abschluss-Eintrag (Seitenzahl der Datenbank im Eintrag ungleich 0).
std::string apply_wal(const std::string& image, const std::string& wal, unsigned page) {
    if (wal.size() < 32) return image;
    const auto* w = reinterpret_cast<const unsigned char*>(wal.data());
    const std::uint32_t magic = be32(w);
    if (magic != 0x377f0682u && magic != 0x377f0683u) return image;
    const bool big = (magic & 1u) != 0;
    if (be32(w + 8) != page) return image;
    std::uint32_t s1 = 0, s2 = 0;
    wal_sum(w, 24, big, s1, s2);
    if (s1 != be32(w + 24) || s2 != be32(w + 28)) return image;
    const std::uint32_t salt1 = be32(w + 16), salt2 = be32(w + 20);
    const std::size_t frame = 24 + static_cast<std::size_t>(page);
    long valid = 0, last_commit = -1;
    std::uint32_t commit_pages = 0;
    for (std::size_t off = 32; off + frame <= wal.size(); off += frame) {
        const unsigned char* fr = w + off;
        if (be32(fr + 8) != salt1 || be32(fr + 12) != salt2) break;
        wal_sum(fr, 8, big, s1, s2);
        wal_sum(fr + 24, page, big, s1, s2);
        if (s1 != be32(fr + 16) || s2 != be32(fr + 20)) break;
        if (be32(fr) == 0) break;
        if (be32(fr + 4) != 0) {
            last_commit = valid;
            commit_pages = be32(fr + 4);
        }
        ++valid;
    }
    if (last_commit < 0 || commit_pages == 0) return image;
    std::string out = image;
    out.resize(static_cast<std::size_t>(commit_pages) * page, '\0');
    for (long i = 0; i <= last_commit; ++i) {
        const unsigned char* fr = w + 32 + static_cast<std::size_t>(i) * frame;
        const std::uint32_t pg = be32(fr);
        if (pg == 0 || pg > commit_pages) continue;
        std::memcpy(&out[static_cast<std::size_t>(pg - 1) * page], fr + 24, page);
    }
    return out;
}

std::string one_line(const char* s) {
    std::string out = s ? s : "?";
    for (char& c : out)
        if (c == '\n' || c == '\r') c = ' ';
    if (out.size() > 200) out.resize(200);
    return out;
}

}  // namespace
}  // namespace medic

// SQLITE_OS_OTHER: SQLite ruft diese beiden Funktionen selbst auf.
extern "C" int sqlite3_os_init(void) { return sqlite3_vfs_register(&medic::g_vfs, 1); }
extern "C" int sqlite3_os_end(void) { return SQLITE_OK; }

namespace medic {

SqliteCheck sqlite_integrity_check(const std::string& image, const std::string& wal) {
    SqliteCheck r;
    if (image.size() < 512 || image.size() > kMaxImageBytes) return r;
    unsigned page = (static_cast<unsigned>(static_cast<unsigned char>(image[16])) << 8) | static_cast<unsigned char>(image[17]);
    if (page == 1) page = 65536;
    const std::string work = wal.size() >= 32 && page >= 512 ? apply_wal(image, wal, page) : image;
    if (work.size() < 512 || work.size() > kMaxImageBytes) return r;
    auto* buf = static_cast<unsigned char*>(sqlite3_malloc64(work.size()));
    if (!buf) return r;
    std::memcpy(buf, work.data(), work.size());
    // Eine Speicher-Datenbank kann nicht im WAL-Modus laufen; die Arbeitskopie wird auf Rollback-Journal umgestellt (Bytes 18 und 19 des Kopfs).
    // Die echte Datei bleibt unberuehrt; WAL-Eintraege gibt es hier nicht (der Aufrufer prueft das vorher).
    if (buf[18] == 2) buf[18] = 1;
    if (buf[19] == 2) buf[19] = 1;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        sqlite3_free(buf);
        return r;
    }
    const int rc = sqlite3_deserialize(db, "main", buf, static_cast<sqlite3_int64>(work.size()), static_cast<sqlite3_int64>(work.size()),
                                       SQLITE_DESERIALIZE_FREEONCLOSE | SQLITE_DESERIALIZE_READONLY);
    if (rc != SQLITE_OK) {  // bei einem Fehler gibt sqlite3_deserialize den Puffer selbst frei (FREEONCLOSE)
        sqlite3_close(db);
        return r;
    }
    r.ran = true;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA integrity_check(3)", -1, &st, nullptr) != SQLITE_OK) {
        r.ok = false;
        r.note = one_line(sqlite3_errmsg(db));
    } else {
        int rows = 0, step = 0;
        bool first_ok = false;
        while ((step = sqlite3_step(st)) == SQLITE_ROW) {
            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
            ++rows;
            if (rows == 1 && t && std::strcmp(t, "ok") == 0) first_ok = true;
            else if (r.note.empty()) r.note = one_line(t);
        }
        if (step != SQLITE_DONE && r.note.empty()) r.note = one_line(sqlite3_errmsg(db));
        r.ok = step == SQLITE_DONE && rows == 1 && first_ok;
    }
    sqlite3_finalize(st);
    sqlite3_close(db);
    return r;
}

std::string sqlite_broken_sample() {
    std::string out;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return out;
    }
    sqlite3_exec(db, "PRAGMA page_size=512", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "CREATE TABLE t(a INTEGER PRIMARY KEY, b TEXT)", nullptr, nullptr, nullptr);
    sqlite3_exec(db,
                 "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x<200) INSERT INTO t(b) SELECT 'sample-row-' || x FROM c",
                 nullptr, nullptr, nullptr);
    sqlite3_int64 size = 0;
    unsigned char* ser = sqlite3_serialize(db, "main", &size, 0);
    if (ser && size >= 1536) {
        out.assign(reinterpret_cast<const char*>(ser), static_cast<std::size_t>(size));
        out.replace(512, 512, std::string(512, '\xA5'));  // Seite 2 (Wurzel der Tabelle t) zerstoeren
    }
    sqlite3_free(ser);
    sqlite3_close(db);
    return out;
}

}  // namespace medic
