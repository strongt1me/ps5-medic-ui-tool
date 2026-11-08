// SPDX-License-Identifier: GPL-3.0-or-later
// Signatur der Regeldatei. Eine Regeldatei, die aus dem Netz kommt, wird nur angenommen, wenn eine Ed25519-Signatur
// ueber genau ihre Bytes zu einem eingebauten, vertrauenswuerdigen Schluessel passt (trusted_rule_keys()).
//
// Signaturdatei `rules.json.sig`: eine Textzeile
//     ed25519 <schluessel-id> <base64 der 64 Signaturbytes>
// Die Schluessel-ID sind die ersten 16 Hex-Zeichen von SHA-256 ueber den oeffentlichen Schluessel. Sie waehlt den Schluessel
// aus der eingebauten Liste (so laesst sich spaeter ein neuer Schluessel einfuehren, ohne dass alte Signaturen brechen).
#pragma once
#include "medic/ed25519.hpp"

#include <string>
#include <vector>

namespace medic {

struct TrustedKey {
    std::string id;  // 16 Hex-Zeichen, aus dem Schluessel abgeleitet (key_id_of)
    Ed25519Public key{};
};

std::string key_id_of(const Ed25519Public& pk);
TrustedKey make_trusted_key(const Ed25519Public& pk);

// Die in PS5 Medic eingebauten Schluessel (libmedic/src/trusted_keys.cpp). Der Privatschluessel liegt nie im Repository.
const std::vector<TrustedKey>& trusted_rule_keys();

enum class SigResult { Valid, Malformed, UnknownKey, BadSignature };

struct SigInfo {
    SigResult result = SigResult::Malformed;
    std::string key_id;  // aus der Signaturdatei, soweit lesbar
};

// Pruefung: Format lesen, Schluessel in `keys` suchen, Signatur ueber `rules_text` pruefen.
SigInfo verify_rules_signature(const std::string& rules_text, const std::string& sig_file_text, const std::vector<TrustedKey>& keys);

// Gegenstueck fuer das Signierwerkzeug und die Tests.
std::string make_signature_file(const Ed25519KeyPair& kp, const std::string& rules_text);

std::string base64_encode(const std::string& bytes);
bool base64_decode(const std::string& text, std::string& bytes);  // false bei ungueltigen Zeichen oder Laenge

}  // namespace medic
