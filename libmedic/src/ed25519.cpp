// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/ed25519.hpp"

#include <cstdlib>
#include <cstring>
#include <vector>

// TweetNaCl (third_party/tweetnacl/tweetnacl.c, als C uebersetzt, unveraendert). `randombytes` wird nur beim Erzeugen eines
// Schluesselpaars gerufen; wir geben den Seed vor und lassen jeden anderen Aufruf abbrechen, statt stillschweigend schwache
// Zufallszahlen zu liefern.
extern "C" {
#include "tweetnacl/tweetnacl.h"
}

namespace {
const unsigned char* g_seed = nullptr;
}

extern "C" void randombytes(unsigned char* out, unsigned long long n) {
    if (!g_seed || n != 32) std::abort();
    std::memcpy(out, g_seed, 32);
}

namespace medic {

bool ed25519_verify(const Ed25519Public& pk, const std::string& message, const Ed25519Signature& sig) {
    // TweetNaCl erwartet Signatur und Nachricht hintereinander (sm = sig || m) und einen gleich grossen Ausgabepuffer.
    const std::size_t n = sig.size() + message.size();
    std::vector<unsigned char> sm(n), out(n);
    std::memcpy(sm.data(), sig.data(), sig.size());
    if (!message.empty()) std::memcpy(sm.data() + sig.size(), message.data(), message.size());
    unsigned long long mlen = 0;
    return crypto_sign_ed25519_tweet_open(out.data(), &mlen, sm.data(), n, pk.data()) == 0;
}

Ed25519KeyPair ed25519_keypair_from_seed(const Ed25519Seed& seed) {
    Ed25519KeyPair kp;
    g_seed = seed.data();
    crypto_sign_ed25519_tweet_keypair(kp.pk.data(), kp.sk.data());
    g_seed = nullptr;
    return kp;
}

Ed25519Signature ed25519_sign(const Ed25519Secret& sk, const std::string& message) {
    const std::size_t n = 64 + message.size();
    std::vector<unsigned char> sm(n);
    unsigned long long smlen = 0;
    crypto_sign_ed25519_tweet(sm.data(), &smlen, reinterpret_cast<const unsigned char*>(message.data()), message.size(), sk.data());
    Ed25519Signature sig;
    std::memcpy(sig.data(), sm.data(), 64);
    return sig;
}

}  // namespace medic
