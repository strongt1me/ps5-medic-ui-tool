// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/signature.hpp"

#include "medic/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace medic {

namespace {
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64_encode(const std::string& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const std::uint32_t b0 = static_cast<unsigned char>(in[i]);
        const std::uint32_t b1 = i + 1 < in.size() ? static_cast<unsigned char>(in[i + 1]) : 0;
        const std::uint32_t b2 = i + 2 < in.size() ? static_cast<unsigned char>(in[i + 2]) : 0;
        const std::uint32_t v = (b0 << 16) | (b1 << 8) | b2;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < in.size() ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < in.size() ? kB64[v & 63] : '=';
    }
    return out;
}

bool base64_decode(const std::string& text, std::string& bytes) {
    bytes.clear();
    if (text.empty() || text.size() % 4 != 0) return false;
    auto val = [](char c) -> int {
        const char* p = std::strchr(kB64, c);
        return (c != '\0' && p) ? static_cast<int>(p - kB64) : -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 4) {
        int v[4];
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text[i + k];
            if (c == '=') {
                // Fuellzeichen nur am Ende der letzten Gruppe
                if (i + 4 != text.size() || k < 2) return false;
                v[k] = 0;
                ++pad;
            } else {
                if (pad) return false;
                v[k] = val(c);
                if (v[k] < 0) return false;
            }
        }
        const std::uint32_t n = (static_cast<std::uint32_t>(v[0]) << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
        bytes += static_cast<char>((n >> 16) & 255);
        if (pad < 2) bytes += static_cast<char>((n >> 8) & 255);
        if (pad < 1) bytes += static_cast<char>(n & 255);
    }
    return true;
}

std::string key_id_of(const Ed25519Public& pk) {
    return sha256_hex(std::string(reinterpret_cast<const char*>(pk.data()), pk.size())).substr(0, 16);
}

TrustedKey make_trusted_key(const Ed25519Public& pk) { return {key_id_of(pk), pk}; }

SigInfo verify_rules_signature(const std::string& rules_text, const std::string& sig_file_text, const std::vector<TrustedKey>& keys) {
    SigInfo info;
    // Erste nicht leere Zeile, drei Woerter: "ed25519" <id> <base64>
    std::istringstream in(sig_file_text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream probe(line);
        std::string w;
        if (probe >> w) break;
        line.clear();
    }
    std::istringstream words(line);
    std::string algo, id, b64, extra;
    if (!(words >> algo >> id >> b64) || algo != "ed25519" || (words >> extra)) return info;
    info.key_id = id;
    std::string raw;
    if (!base64_decode(b64, raw) || raw.size() != 64) return info;
    Ed25519Signature sig;
    std::memcpy(sig.data(), raw.data(), 64);

    auto it = std::find_if(keys.begin(), keys.end(), [&](const TrustedKey& k) { return k.id == id; });
    if (it == keys.end()) {
        info.result = SigResult::UnknownKey;
        return info;
    }
    info.result = ed25519_verify(it->key, rules_text, sig) ? SigResult::Valid : SigResult::BadSignature;
    return info;
}

std::string make_signature_file(const Ed25519KeyPair& kp, const std::string& rules_text) {
    const Ed25519Signature sig = ed25519_sign(kp.sk, rules_text);
    return "ed25519 " + key_id_of(kp.pk) + " " + base64_encode(std::string(reinterpret_cast<const char*>(sig.data()), sig.size())) + "\n";
}

}  // namespace medic
