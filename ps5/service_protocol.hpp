// SPDX-License-Identifier: GPL-3.0-or-later
// Protokoll zwischen der nativen UI-App (Anzeige und Pad, in der Sandbox) und dem Dienst-Payload (zeichnet die Oberflaeche, liest die Konsole,
// fuehrt Reparaturen aus) ueber TCP auf 127.0.0.1. Siehe docs/NATIVE.md. Gemeinsam fuer beide Seiten; nur Standardbibliothek und POSIX-Sockets.
//
// Aufbau jeder Nachricht: u8 Typ, u32 Laenge (little endian), danach `Laenge` Bytes.
//   Dienst -> App:  'H' Hallo       (u16 Breite, u16 Hoehe, danach optional die Build-Kennung des Dienstes als ASCII-Text; fehlt sie, ist der Dienst aelter)
//                   'B' Streifen    (u16 y0, u16 Zeilen, dann Zeilen*Breite ARGB-Pixel, 0xAARRGGBB little endian)
//                   'P' Bild fertig (leer): jetzt anzeigen
//                   'Q' Ende        (leer): die Oberflaeche wurde beendet, App schliessen
//                   'G' Fortschritt (u8 Prozent 0..100): Ladestand des Dienstes, die App zeigt ihn im Balken des Startbilds
//   App -> Dienst:  'S' Pad-Zustand (u32 Tasten wie ScePad, u8 linker Stick x, u8 y, u8 rechter x, u8 y)
//                   'T' Pad-Zusatz  (u8 L2, u8 R2, 24 Byte Touchpad wie PS5_PadTouchData, 3 float Beschleunigung, 3 float Winkelgeschwindigkeit; 50 Byte,
//                                    kommt vor dem 'S' desselben Zustands; fuer den Controller-Test. Ein aelterer Dienst ueberspringt die Nachricht.)
//                   'X' Dienst beenden (leer)
#pragma once
#include <cstddef>
#include <cerrno>
#include <cstdint>
#include <cstring>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace medic::proto {

// Build-Kennung: Makefile und title/build_native_ui.sh setzen dieselbe Kennung (git-Stand, "+" bei ungesicherten Aenderungen) in App und Dienst. Der Dienst nennt sie
// im Hallo; weicht sie von der der App ab, beendet die App den alten Dienst und sendet den neuen (einmal je Start). Ohne Angabe "dev".
#ifndef MEDIC_BUILD_ID
#define MEDIC_BUILD_ID "dev"
#endif
constexpr const char* kBuildId = MEDIC_BUILD_ID;
constexpr std::size_t kMaxBuildId = 40;  // laenger wird abgeschnitten (Hallo ist 4 Byte plus Kennung)

constexpr int kPort = 9060;
constexpr int kLoaderPort = 9021;
constexpr int kWidth = 1920, kHeight = 1080;
constexpr int kBandRows = 8;  // Rastergroesse, in dem Aenderungen erkannt werden

constexpr char kHello = 'H', kBand = 'B', kPresent = 'P', kQuit = 'Q', kProgress = 'G', kPad = 'S', kPadExtra = 'T', kShutdown = 'X';
constexpr std::uint32_t kPadExtraLen = 50;  // 2 + 24 + 12 + 12

inline void put32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline std::uint32_t get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 | static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
inline void put16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
inline std::uint16_t get16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }

// Kleine Nachrichten sofort senden: ohne TCP_NODELAY wartet der zweite kleine Send (Nutzlast hinter dem Kopf) auf die Bestaetigung des ersten, und die
// Gegenstelle verzoegert sie (bis zu 100 ms) -- Tasten kamen so gebuendelt und spaet an.
inline void set_nodelay(int s) {
    const int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

// Grosse Puffer fuer die Bildstreifen: ein ganzes Bild sind 8 MB, und mit dem Standardpuffer (einige 10 KB) lief es in vielen kleinen Schritten durch
// (Konsole, 14. Nov. 2026: Senden des ersten Bilds 1393 ms). Lehnt das System die Groesse ab, bleibt der Standard.
inline void set_stream_buffers(int s, int bytes = 1 << 20) {
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof bytes);
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof bytes);
}

// Sendezeitlimit je blockierendem send(): eine Gegenstelle, die nicht mehr liest, haelt den Sender sonst fuer immer fest.
inline void set_send_timeout(int s, int seconds) {
    timeval tv{seconds, 0};
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

// Verbindung zu 127.0.0.1:port; -1 bei Fehler. Das Sendezeitlimit verhindert endloses Warten. big_buffers: grosse Puffer fuer den Bildstrom (set_stream_buffers).
inline int connect_local(int port, int send_timeout_s = 5, bool big_buffers = false) {
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sin_len = sizeof sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<std::uint16_t>(port));
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    set_send_timeout(s, send_timeout_s);
    set_nodelay(s);
    if (big_buffers) set_stream_buffers(s);  // vor connect(): erst dann richtet sich das Empfangsfenster danach
    if (connect(s, reinterpret_cast<const sockaddr*>(&sa), sizeof sa) < 0) {
        close(s);
        return -1;
    }
    return s;
}

// Alles senden (bei Teilschreibungen weiter); false bei Fehler. Kein SIGPIPE, wenn die Gegenstelle weg ist.
inline bool send_all(int s, const void* data, std::size_t n) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    while (n) {
        const ssize_t w = send(s, p, n, MSG_NOSIGNAL);
        if (w <= 0) return false;
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return true;
}

// Nicht blockierend (fuer Momentaufnahmen wie den Pad-Zustand): ist der Sendepuffer voll, wird die Nachricht verworfen und true geliefert; false nur bei einem
// echten Fehler (Gegenstelle weg). Vorher blockierte die App bis zu 2 s im send(), wenn der Dienst gerade nicht las, kam nicht dazu, Bilder zu lesen, und
// beide warteten aufeinander, bis die App die Verbindung abbrach.
inline bool send_message_drop(int s, char type, const void* payload, std::uint32_t len) {
    if (len > 64) return false;
    std::uint8_t buf[5 + 64];
    buf[0] = static_cast<std::uint8_t>(type);
    put32(buf + 1, len);
    if (len) std::memcpy(buf + 5, payload, len);
    const std::size_t n = 5 + len;
    const ssize_t w = send(s, buf, n, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (w == static_cast<ssize_t>(n)) return true;
    if (w < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    return send_all(s, buf + w, n - static_cast<std::size_t>(w));  // Teilschreibung: den Rest nachsenden, sonst ist der Strom kaputt
}

inline bool send_message(int s, char type, const void* payload, std::uint32_t len) {
    std::uint8_t head[5];
    head[0] = static_cast<std::uint8_t>(type);
    put32(head + 1, len);
    if (len <= 64) {  // Kopf und kleine Nutzlast in einem Send (ein Paket)
        std::uint8_t buf[5 + 64];
        std::memcpy(buf, head, sizeof head);
        if (len) std::memcpy(buf + sizeof head, payload, len);
        return send_all(s, buf, sizeof head + len);
    }
    return send_all(s, head, sizeof head) && send_all(s, payload, len);
}

}  // namespace medic::proto
