// SPDX-License-Identifier: GPL-3.0-or-later
// Protokoll zwischen der nativen UI-App (Anzeige und Pad, in der Sandbox) und dem Dienst-Payload (zeichnet die Oberflaeche, liest die Konsole,
// fuehrt Reparaturen aus) ueber TCP auf 127.0.0.1. Siehe docs/NATIVE.md. Gemeinsam fuer beide Seiten; nur Standardbibliothek und POSIX-Sockets.
//
// Aufbau jeder Nachricht: u8 Typ, u32 Laenge (little endian), danach `Laenge` Bytes.
//   Dienst -> App:  'H' Hallo       (u16 Breite, u16 Hoehe)
//                   'B' Streifen    (u16 y0, u16 Zeilen, dann Zeilen*Breite ARGB-Pixel, 0xAARRGGBB little endian)
//                   'P' Bild fertig (leer): jetzt anzeigen
//                   'Q' Ende        (leer): die Oberflaeche wurde beendet, App schliessen
//                   'G' Fortschritt (u8 Prozent 0..100): Ladestand des Dienstes, die App zeigt ihn im Balken des Startbilds
//   App -> Dienst:  'S' Pad-Zustand (u32 Tasten wie ScePad, u8 linker Stick x, u8 y, u8 rechter x, u8 y)
//                   'X' Dienst beenden (leer)
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace medic::proto {

constexpr int kPort = 9060;
constexpr int kLoaderPort = 9021;
constexpr int kWidth = 1920, kHeight = 1080;
constexpr int kBandRows = 8;  // Rastergroesse, in dem Aenderungen erkannt werden

constexpr char kHello = 'H', kBand = 'B', kPresent = 'P', kQuit = 'Q', kProgress = 'G', kPad = 'S', kShutdown = 'X';

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

// Verbindung zu 127.0.0.1:port; -1 bei Fehler. Das Sendezeitlimit verhindert endloses Warten.
inline int connect_local(int port, int send_timeout_s = 5) {
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sin_len = sizeof sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<std::uint16_t>(port));
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    timeval tv{send_timeout_s, 0};
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
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

inline bool send_message(int s, char type, const void* payload, std::uint32_t len) {
    std::uint8_t head[5];
    head[0] = static_cast<std::uint8_t>(type);
    put32(head + 1, len);
    return send_all(s, head, sizeof head) && (len == 0 || send_all(s, payload, len));
}

}  // namespace medic::proto
