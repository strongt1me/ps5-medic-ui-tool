// SPDX-License-Identifier: GPL-3.0-or-later
// qrcodegen (third_party, MIT) fuer die native App ohne Ausnahmen und ohne iostream (das zoege die ganze Locale-Bibliothek des libc++ nach):
// alle Standard-Header zuerst einlesen, dann wird jedes `throw` im Fremdcode zu einem Abbruch und der Fehlertext-Strom (nur fuer die
// Meldung "Data too long") zu einem Platzhalter. Die Aufrufer halten den Text unter kQrMaxBytes, so dass nie ein Fehler entsteht.
#define NDEBUG 1
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "qrcodegen/qrcodegen.hpp"

#define _LIBCPP_SSTREAM  // libc++-<sstream> nicht einlesen
namespace std {
struct qr_fake_stream {
    template <class T>
    qr_fake_stream& operator<<(const T&) { return *this; }
    std::string str() const { return std::string(); }
};
}  // namespace std
#define ostringstream qr_fake_stream
#define throw __builtin_trap(),(void)
#include "qrcodegen/qrcodegen.cpp"
