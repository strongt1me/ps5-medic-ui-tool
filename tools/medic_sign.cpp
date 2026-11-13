// SPDX-License-Identifier: GPL-3.0-or-later
// medic-sign: Schluessel erzeugen und die Regeldatei signieren (nur am PC, nie auf der Konsole).
//
//   medic-sign keygen <schluesseldatei>              neues Schluesselpaar; verweigert das Ueberschreiben
//   medic-sign pubkey <schluesseldatei>              oeffentlichen Schluessel, ID und den Quelltext-Eintrag anzeigen
//   medic-sign sign <schluesseldatei> <rules.json> [<ausgabe>]   schreibt <rules.json>.sig und prueft sie gleich gegen
//   medic-sign verify <rules.json> <rules.json.sig>  prueft gegen die in PS5 Medic eingebauten Schluessel
//
// Die Schluesseldatei enthaelt den geheimen Seed (eine Zeile "ps5medic-ed25519-seed <64 Hex>"). Sie gehoert NICHT ins
// Repository; wer sie hat, kann Regeln signieren, die jede PS5 Medic mit dem eingebauten Schluessel annimmt.
#include "medic/error_codes.hpp"
#include "medic/rule_update.hpp"
#include "medic/rules.hpp"
#include "medic/signature.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace medic;

namespace {

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string to_hex(const std::uint8_t* p, std::size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

bool from_hex(const std::string& s, std::uint8_t* out, std::size_t n) {
    if (s.size() != n * 2) return false;
    auto v = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < n; ++i) {
        const int a = v(s[2 * i]), b = v(s[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = static_cast<std::uint8_t>(a * 16 + b);
    }
    return true;
}

bool load_seed(const std::string& path, Ed25519Seed& seed) {
    std::string text;
    if (!read_file(path, text)) {
        std::cerr << "Schluesseldatei nicht lesbar: " << path << "\n";
        return false;
    }
    std::istringstream in(text);
    std::string tag, hex;
    if (!(in >> tag >> hex) || tag != "ps5medic-ed25519-seed" || !from_hex(hex, seed.data(), seed.size())) {
        std::cerr << "Keine gueltige Schluesseldatei: " << path << "\n";
        return false;
    }
    return true;
}

void print_public(const Ed25519KeyPair& kp) {
    std::cout << "Oeffentlicher Schluessel: " << to_hex(kp.pk.data(), kp.pk.size()) << "\n"
              << "Schluessel-ID:            " << key_id_of(kp.pk) << "\n";
}

int usage() {
    std::cerr << "Aufruf:\n  medic-sign keygen <schluesseldatei>\n  medic-sign pubkey <schluesseldatei>\n"
                 "  medic-sign sign <schluesseldatei> <rules.json> [<ausgabe>]\n  medic-sign verify <rules.json> <rules.json.sig>\n";
    return 2;
}

int cmd_keygen(const std::string& path) {
    if (std::ifstream(path).good()) {
        std::cerr << "Existiert schon, wird nicht ueberschrieben: " << path << "\n";
        return 1;
    }
    Ed25519Seed seed{};
    umask(077);  // die Schluesseldatei ist von Anfang an nur fuer den Besitzer lesbar (chmod folgt erst nach dem Schreiben)
    std::ifstream rnd("/dev/urandom", std::ios::binary);
    if (!rnd.read(reinterpret_cast<char*>(seed.data()), seed.size())) {
        std::cerr << "/dev/urandom nicht lesbar\n";
        return 1;
    }
    const Ed25519KeyPair kp = ed25519_keypair_from_seed(seed);
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            std::cerr << "Kann nicht schreiben: " << path << "\n";
            return 1;
        }
        out << "ps5medic-ed25519-seed " << to_hex(seed.data(), seed.size()) << "\n";
    }
    chmod(path.c_str(), 0600);
    std::cout << "Schluesseldatei geschrieben: " << path << "\nSICHERN, nicht ins Repository legen. Ohne sie lassen sich keine Regeln mehr signieren.\n";
    print_public(kp);
    return 0;
}

int cmd_pubkey(const std::string& path) {
    Ed25519Seed seed{};
    if (!load_seed(path, seed)) return 1;
    const Ed25519KeyPair kp = ed25519_keypair_from_seed(seed);
    print_public(kp);
    std::cout << "Eintrag fuer libmedic/src/trusted_keys.cpp:\n    \"" << to_hex(kp.pk.data(), kp.pk.size()) << "\",\n";
    return 0;
}

int cmd_sign(const std::string& keyfile, const std::string& rules_path, std::string out_path) {
    Ed25519Seed seed{};
    if (!load_seed(keyfile, seed)) return 1;
    std::string rules;
    if (!read_file(rules_path, rules)) {
        std::cerr << "rules.json nicht lesbar: " << rules_path << "\n";
        return 1;
    }
    if (rules.size() > kMaxRulesBytes) {  // jede Konsole lehnt eine groessere Datei mit "too_large" ab
        std::cerr << "Datei zu gross (" << rules.size() << " Byte, erlaubt sind " << kMaxRulesBytes << "): " << rules_path << "\n";
        return 1;
    }
    // Die Datei kann die Regeln (rules.json) oder die Fehlercodes (error_codes.json) sein; beide werden mit demselben Schluessel
    // signiert und von der Konsole gleich geprueft. Welche es ist, steht am Dateinamen.
    const std::string codes_name = "error_codes.json";
    const bool is_codes = rules_path.size() >= codes_name.size() && rules_path.compare(rules_path.size() - codes_name.size(), codes_name.size(), codes_name) == 0;
    try {
        if (is_codes) {
            const ErrorCodeSet set = load_error_codes_from_string(rules);
            std::cout << "Fehlercodes: Version " << set.db_version << ", " << set.codes.size() << " Codes, " << rules.size() << " Bytes\n";
        } else {
            const RuleSet rs = load_rules_from_string(rules);
            std::cout << "Regeln: Version " << rs.db_version << ", " << rs.rules.size() << " Regeln, " << rules.size() << " Bytes\n";
        }
    } catch (const std::exception& e) {
        std::cerr << rules_path << " ist ungueltig, wird nicht signiert: " << e.what() << "\n";
        return 1;
    }
    const Ed25519KeyPair kp = ed25519_keypair_from_seed(seed);
    const std::string sig = make_signature_file(kp, rules);
    if (out_path.empty()) out_path = rules_path + ".sig";
    {
        std::ofstream out(out_path, std::ios::binary);
        if (!out) {
            std::cerr << "Kann nicht schreiben: " << out_path << "\n";
            return 1;
        }
        out << sig;
    }
    // Gegenprobe mit genau dem Schluessel, der signiert hat.
    const SigInfo check = verify_rules_signature(rules, sig, {make_trusted_key(kp.pk)});
    if (check.result != SigResult::Valid) {
        std::cerr << "Interner Fehler: die frisch erzeugte Signatur besteht die Pruefung nicht.\n";
        return 1;
    }
    std::cout << "Signatur geschrieben: " << out_path << " (Schluessel " << key_id_of(kp.pk) << ")\n";
    const SigInfo built_in = verify_rules_signature(rules, sig, trusted_rule_keys());
    if (built_in.result != SigResult::Valid)
        std::cout << "HINWEIS: Dieser Schluessel ist in dieser PS5-Medic-Version NICHT eingebaut (trusted_keys.cpp); "
                     "eine Konsole mit dieser Version nimmt die Datei nicht an.\n";
    return 0;
}

int cmd_verify(const std::string& rules_path, const std::string& sig_path) {
    std::string rules, sig;
    if (!read_file(rules_path, rules) || !read_file(sig_path, sig)) {
        std::cerr << "Dateien nicht lesbar\n";
        return 1;
    }
    const SigInfo r = verify_rules_signature(rules, sig, trusted_rule_keys());
    const char* what = r.result == SigResult::Valid ? "gueltig" : r.result == SigResult::UnknownKey ? "unbekannter Schluessel"
                       : r.result == SigResult::BadSignature ? "Signatur passt nicht" : "Format ungueltig";
    std::cout << what << " (Schluessel " << (r.key_id.empty() ? "?" : r.key_id) << ")\n";
    return r.result == SigResult::Valid ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    if (cmd == "keygen" && argc == 3) return cmd_keygen(argv[2]);
    if (cmd == "pubkey" && argc == 3) return cmd_pubkey(argv[2]);
    if (cmd == "sign" && (argc == 4 || argc == 5)) return cmd_sign(argv[2], argv[3], argc == 5 ? argv[4] : "");
    if (cmd == "verify" && argc == 4) return cmd_verify(argv[2], argv[3]);
    return usage();
}
