// SPDX-License-Identifier: GPL-3.0-or-later
// Ed25519 (Signaturen). Gebaut auf TweetNaCl (third_party/tweetnacl, Public Domain). Auf der Konsole wird nur geprueft;
// Schluessel erzeugen und signieren braucht nur das Werkzeug tools/medic_sign.cpp am PC (und die Tests).
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace medic {

using Ed25519Public = std::array<std::uint8_t, 32>;
using Ed25519Secret = std::array<std::uint8_t, 64>;  // Seed (32) + oeffentlicher Schluessel (32), wie in TweetNaCl
using Ed25519Signature = std::array<std::uint8_t, 64>;
using Ed25519Seed = std::array<std::uint8_t, 32>;

// Prueft `sig` ueber genau den Bytes von `message`.
bool ed25519_verify(const Ed25519Public& pk, const std::string& message, const Ed25519Signature& sig);

struct Ed25519KeyPair {
    Ed25519Public pk{};
    Ed25519Secret sk{};
};

// Leitet das Schluesselpaar deterministisch aus dem Seed ab. Nicht thread-sicher (gemeinsamer Zustand fuer randombytes).
Ed25519KeyPair ed25519_keypair_from_seed(const Ed25519Seed& seed);
Ed25519Signature ed25519_sign(const Ed25519Secret& sk, const std::string& message);

}  // namespace medic
