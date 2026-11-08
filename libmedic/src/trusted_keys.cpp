// SPDX-License-Identifier: GPL-3.0-or-later
// Die Schluessel, deren Signatur eine heruntergeladene Regeldatei haben muss. Nur oeffentliche Schluessel (32 Byte, hex).
// Der Privatschluessel liegt ausserhalb des Repositorys (tools/medic_sign.cpp erzeugt und benutzt ihn).
//
// Schluessel wechseln: neuen Schluessel hier ZUSAETZLICH eintragen und eine neue Version ausliefern; erst wenn die alte
// Version nicht mehr im Umlauf ist, den alten entfernen. Die Signaturdatei nennt per ID, welcher Schluessel gilt.
#include "medic/signature.hpp"

#include <cstdlib>

namespace medic {

namespace {

Ed25519Public from_hex(const char* hex) {
    Ed25519Public pk{};
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (std::size_t i = 0; i < pk.size(); ++i) pk[i] = static_cast<std::uint8_t>(v(hex[2 * i]) * 16 + v(hex[2 * i + 1]));
    return pk;
}

}  // namespace

const std::vector<TrustedKey>& trusted_rule_keys() {
    static const std::vector<TrustedKey> keys = [] {
        const std::vector<const char*> hex = {
            "a0d6222224ceb36399f46d84b49c9dd81b88684cc2c56a21075bef6c3c3231e9",  // Marc (strongt1me), Regeln, erzeugt 8. Nov. 2026
        };
        std::vector<TrustedKey> v;
        for (const char* h : hex) v.push_back(make_trusted_key(from_hex(h)));
        return v;
    }();
    return keys;
}

}  // namespace medic
