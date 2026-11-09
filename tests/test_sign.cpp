// SPDX-License-Identifier: GPL-3.0-or-later
// Tests fuer Ed25519, die Signatur der Regeldatei, das signierte Update, den Regel-Download und den HTTP-Client.
#include "check.hpp"
#include "testutil.hpp"
#include "medic/ed25519.hpp"
#include "medic/http.hpp"
#include "medic/platform.hpp"
#include "medic/rule_download.hpp"
#include "medic/rule_update.hpp"
#include "medic/rules.hpp"
#include "medic/sha256.hpp"
#include "medic/signature.hpp"

#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace medic;
using namespace testutil;

namespace {

std::string unhex(const std::string& h) {
    std::string out;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2) out += static_cast<char>(std::stoi(h.substr(i, 2), nullptr, 16));
    return out;
}

std::string hex_of(const std::uint8_t* p, std::size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

Ed25519Seed seed_of(const std::string& hex) {
    Ed25519Seed s;
    const std::string raw = unhex(hex);
    std::copy(raw.begin(), raw.end(), s.begin());
    return s;
}

// Testschluessel: feste Seeds, nur fuer die Tests (nie ein echter Schluessel).
const Ed25519KeyPair& key_a() {
    static const Ed25519KeyPair k = ed25519_keypair_from_seed(seed_of(std::string(64, '1')));
    return k;
}
const Ed25519KeyPair& key_b() {
    static const Ed25519KeyPair k = ed25519_keypair_from_seed(seed_of(std::string(64, '2')));
    return k;
}

void put(const Sandbox& sb, const char* name, const std::string& text) { spit(sb.real(std::string(kRulesUpdateDir) + "/" + name), text); }

// ---- HTTP-Gegenstelle fuer die Tests: ein Thread, der je Verbindung eine vorbereitete Antwort schickt ----
struct TestServer {
    struct Step {
        std::string reply;     // Rohbytes der Antwort
        int hold_ms = 0;       // vor dem Schliessen so lange warten (fuer den Zeitueberschreitungs-Test)
        bool send = true;      // false: nichts senden
    };
    int listen_fd = -1;
    int port = 0;
    std::vector<Step> steps;
    std::vector<std::string> requests;
    std::thread th;

    explicit TestServer(std::vector<Step> s) : steps(std::move(s)) {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        bind(listen_fd, reinterpret_cast<sockaddr*>(&a), sizeof a);
        listen(listen_fd, 8);
        socklen_t len = sizeof a;
        getsockname(listen_fd, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
        th = std::thread([this] {
            for (const Step& st : steps) {
                const int c = accept(listen_fd, nullptr, nullptr);
                if (c < 0) return;
                std::string req;
                char buf[2048];
                while (req.find("\r\n\r\n") == std::string::npos) {
                    const ssize_t n = recv(c, buf, sizeof buf, 0);
                    if (n <= 0) break;
                    req.append(buf, static_cast<std::size_t>(n));
                }
                requests.push_back(req);
                if (st.send) send(c, st.reply.data(), st.reply.size(), 0);
                if (st.hold_ms) std::this_thread::sleep_for(std::chrono::milliseconds(st.hold_ms));
                close(c);
            }
        });
    }
    ~TestServer() {
        // Haengt der Thread noch im accept (weniger Anfragen als Schritte), mit einer Verbindung wecken.
        if (th.joinable()) {
            for (std::size_t i = requests.size(); i < steps.size(); ++i) {
                const int c = socket(AF_INET, SOCK_STREAM, 0);
                sockaddr_in a{};
                a.sin_family = AF_INET;
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                a.sin_port = htons(static_cast<unsigned short>(port));
                connect(c, reinterpret_cast<sockaddr*>(&a), sizeof a);
                close(c);
            }
            th.join();
        }
        close(listen_fd);
    }
    std::string url(const std::string& path = "/") const { return "http://127.0.0.1:" + std::to_string(port) + path; }
};

std::string ok_reply(const std::string& body) { return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body; }

// ---- Ersatz-Client fuer die Download-Tests ----
struct FakeHttp : IHttpClient {
    std::map<std::string, HttpResponse> answers;
    std::vector<std::string> asked;
    HttpResponse get(const std::string& url, std::size_t max_body, int) override {
        asked.push_back(url);
        auto it = answers.find(url);
        if (it == answers.end()) {
            HttpResponse r;
            r.status = 404;
            return r;
        }
        if (it->second.error.empty() && it->second.body.size() > max_body) {
            HttpResponse r;
            r.error = "too_large";
            return r;
        }
        return it->second;
    }
};

HttpResponse reply200(const std::string& body) {
    HttpResponse r;
    r.status = 200;
    r.body = body;
    return r;
}

}  // namespace

static void crypto_tests() {
    // RFC 8032, Abschnitt 7.1: Testvektoren 1 (leere Nachricht) und 2 (ein Byte).
    {
        const auto kp = ed25519_keypair_from_seed(seed_of("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"));
        CHECK(hex_of(kp.pk.data(), 32) == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "Ed25519: RFC 8032 Vektor 1, oeffentlicher Schluessel");
        const auto sig = ed25519_sign(kp.sk, "");
        CHECK(hex_of(sig.data(), 64) == "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
              "Ed25519: RFC 8032 Vektor 1, Signatur");
        CHECK(ed25519_verify(kp.pk, "", sig), "Ed25519: Vektor 1 wird bestaetigt");
    }
    {
        const auto kp = ed25519_keypair_from_seed(seed_of("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb"));
        CHECK(hex_of(kp.pk.data(), 32) == "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "Ed25519: RFC 8032 Vektor 2, oeffentlicher Schluessel");
        const auto sig = ed25519_sign(kp.sk, std::string("\x72", 1));
        CHECK(hex_of(sig.data(), 64) == "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
              "Ed25519: RFC 8032 Vektor 2, Signatur");
        CHECK(ed25519_verify(kp.pk, std::string("\x72", 1), sig), "Ed25519: Vektor 2 wird bestaetigt");
    }
    {  // Veraenderungen werden bemerkt
        const std::string msg = "{\"db_version\": \"1\"}";
        const auto sig = ed25519_sign(key_a().sk, msg);
        CHECK(ed25519_verify(key_a().pk, msg, sig), "Ed25519: eigene Signatur stimmt");
        CHECK(!ed25519_verify(key_a().pk, msg + " ", sig), "Ed25519: veraenderte Nachricht faellt durch");
        CHECK(!ed25519_verify(key_b().pk, msg, sig), "Ed25519: falscher Schluessel faellt durch");
        for (std::size_t i : {0u, 31u, 32u, 63u}) {
            auto bad = sig;
            bad[i] ^= 1;
            CHECK(!ed25519_verify(key_a().pk, msg, bad), "Ed25519: ein veraendertes Signaturbyte faellt durch (Byte " + std::to_string(i) + ")");
        }
        const std::string big(300000, 'x');  // groessere Nachricht (Regeldateien haben bis zu 2 MiB)
        CHECK(ed25519_verify(key_a().pk, big, ed25519_sign(key_a().sk, big)), "Ed25519: grosse Nachricht");
    }
    {  // Base64
        for (const std::string s : {std::string(), std::string("f"), std::string("fo"), std::string("foo"), std::string("foob"), std::string("fooba"), std::string("foobar"),
                                     std::string("\x00\xff\x10", 3)}) {
            std::string back;
            CHECK(base64_decode(base64_encode(s), back) == !s.empty() && (s.empty() || back == s), "Base64: Hin und zurueck (" + std::to_string(s.size()) + " Byte)");
        }
        CHECK(base64_encode("foobar") == "Zm9vYmFy" && base64_encode("fooba") == "Zm9vYmE=" && base64_encode("foob") == "Zm9vYg==", "Base64: bekannte Werte (RFC 4648)");
        std::string out;
        CHECK(!base64_decode("Zm9", out) && !base64_decode("Zm9v!A==", out) && !base64_decode("Zg=a", out) && !base64_decode("=Zg=", out) && !base64_decode("Zg==Zg==", out),
              "Base64: ungueltige Eingaben werden abgelehnt");
    }
    CHECK(!trusted_rule_keys().empty() && trusted_rule_keys()[0].id == key_id_of(trusted_rule_keys()[0].key) && trusted_rule_keys()[0].id.size() == 16,
          "Schluessel: mindestens ein eingebauter Schluessel, ID aus dem Schluessel abgeleitet");
}

static void signature_file_tests() {
    const std::string rules = "{\"x\": 1}";
    const std::vector<TrustedKey> keys = {make_trusted_key(key_a().pk)};
    const std::string good = make_signature_file(key_a(), rules);
    CHECK(good.rfind("ed25519 " + key_id_of(key_a().pk) + " ", 0) == 0 && good.back() == '\n', "Signaturdatei: Format 'ed25519 <id> <base64>'");
    CHECK(verify_rules_signature(rules, good, keys).result == SigResult::Valid, "Signaturdatei: gueltig");
    CHECK(verify_rules_signature(rules, "\n\n  " + good, keys).result == SigResult::Valid, "Signaturdatei: fuehrende Leerzeilen sind egal");
    CHECK(verify_rules_signature(rules + " ", good, keys).result == SigResult::BadSignature, "Signaturdatei: veraenderte Regeldatei");
    CHECK(verify_rules_signature(rules, make_signature_file(key_b(), rules), keys).result == SigResult::UnknownKey, "Signaturdatei: Signatur eines nicht vertrauten Schluessels");
    // Ein Angreifer nennt die ID des vertrauten Schluessels, signiert aber mit seinem eigenen.
    {
        std::string forged = make_signature_file(key_b(), rules);
        forged.replace(8, 16, key_id_of(key_a().pk));
        CHECK(verify_rules_signature(rules, forged, keys).result == SigResult::BadSignature, "Signaturdatei: fremde Signatur mit geklauter Schluessel-ID");
    }
    CHECK(verify_rules_signature(rules, good, {make_trusted_key(key_b().pk), make_trusted_key(key_a().pk)}).result == SigResult::Valid, "Signaturdatei: der passende Schluessel wird per ID gefunden");
    const char* malformed[] = {"", "\n", "ed25519", "ed25519 abc", "rsa 0011223344556677 AAAA", "ed25519 04de66c8c642d706 !!!!", "ed25519 04de66c8c642d706 Zm9v", good.c_str()};
    for (const char* m : {malformed[0], malformed[1], malformed[2], malformed[3], malformed[4], malformed[5], malformed[6]})
        CHECK(verify_rules_signature(rules, m, keys).result == SigResult::Malformed, std::string("Signaturdatei: kaputtes Format wird abgelehnt: '") + m + "'");
    CHECK(verify_rules_signature(rules, good.substr(0, good.size() - 1) + " zusaetzlich\n", keys).result == SigResult::Malformed, "Signaturdatei: ein Wort zu viel");
}

static void signed_update_tests() {
    using State = RuleUpdateOffer::State;
    const std::string ts = "20261109T120000Z";
    const std::string new_text = rules_text_with_version("2026.12.05");
    const std::string old_ver = load_rules_from_string(slurp(kShipped)).db_version;
    const std::vector<TrustedKey> keys = {make_trusted_key(key_a().pk)};
    const std::string sig = make_signature_file(key_a(), new_text);

    {  // gueltige Signatur, keine Pruefsumme noetig
        Sandbox sb;
        put(sb, "rules.json", new_text);
        put(sb, "rules.json.sig", sig);
        auto p = make_snapshot_platform(sb.dir.string());
        std::string verified;
        const auto o = check_rule_update(p->fs(), old_ver, &verified, &keys);
        CHECK(o.state == State::Ready && o.auth == "signature" && o.signed_by == key_id_of(key_a().pk) && verified == new_text, "Signatur-Update: gueltig, Schluessel wird genannt");
    }
    {  // Datei-Update ohne Signatur bleibt moeglich und sagt, dass nur die Pruefsumme galt
        Sandbox sb;
        put(sb, "rules.json", new_text);
        put(sb, "rules.json.sha256", sha256_hex(new_text) + "  rules.json\n");
        auto p = make_snapshot_platform(sb.dir.string());
        const auto o = check_rule_update(p->fs(), old_ver, nullptr, &keys);
        CHECK(o.state == State::Ready && o.auth == "checksum" && o.signed_by.empty(), "Datei-Update: nur Pruefsumme, ausdruecklich als solches gekennzeichnet");
    }
    struct Case {
        const char* name;
        std::string rules, sig, sum, reason;
    };
    const std::string tampered = rules_text_with_version("2026.12.05") + " ";
    const Case cases[] = {
        {"veraenderte Regeldatei", tampered, sig, "", "sig_invalid"},
        // Kein Herabstufen: eine passende Pruefsumme rettet eine ungueltige Signatur nicht.
        {"ungueltige Signatur trotz passender Pruefsumme", tampered, sig, sha256_hex(tampered) + "  rules.json\n", "sig_invalid"},
        {"Signatur eines fremden Schluessels", new_text, make_signature_file(key_b(), new_text), sha256_hex(new_text) + "  rules.json\n", "sig_unknown_key"},
        {"kaputte Signaturdatei", new_text, "ed25519 nur-zwei-woerter\n", sha256_hex(new_text) + "  rules.json\n", "sig_malformed"},
        {"Signatur fuer eine aeltere Version (Rollback-Angriff)", rules_text_with_version("2020.01.01"), make_signature_file(key_a(), rules_text_with_version("2020.01.01")), "", "not_newer"},
    };
    for (const auto& c : cases) {
        Sandbox sb;
        put(sb, "rules.json", c.rules);
        put(sb, "rules.json.sig", c.sig);
        if (!c.sum.empty()) put(sb, "rules.json.sha256", c.sum);
        auto p = make_snapshot_platform(sb.dir.string());
        const auto o = check_rule_update(p->fs(), old_ver, nullptr, &keys);
        CHECK(o.state == State::Rejected && o.reason == c.reason, std::string("Signatur-Update: ") + c.name + " -> " + c.reason + " (war: " + o.reason + ")");
    }
    {  // Signatur ohne Regeldatei
        Sandbox sb;
        put(sb, "rules.json.sig", sig);
        auto p = make_snapshot_platform(sb.dir.string());
        CHECK(check_rule_update(p->fs(), old_ver, nullptr, &keys).reason == "no_rules_file", "Signatur-Update: Signatur ohne Regeldatei");
    }
    {  // Regeldatei ohne Signatur und ohne Pruefsumme (Abbruch mitten im Download)
        Sandbox sb;
        put(sb, "rules.json", new_text);
        auto p = make_snapshot_platform(sb.dir.string());
        CHECK(check_rule_update(p->fs(), old_ver, nullptr, &keys).reason == "no_checksum", "Signatur-Update: halber Download wird nie angenommen");
    }
    {  // Die eingebauten Schluessel kennen die Testschluessel nicht
        Sandbox sb;
        put(sb, "rules.json", new_text);
        put(sb, "rules.json.sig", sig);
        auto p = make_snapshot_platform(sb.dir.string());
        CHECK(check_rule_update(p->fs(), old_ver).reason == "sig_unknown_key", "Signatur-Update: ohne Angabe gelten nur die eingebauten Schluessel");
    }
    {  // Installation: Signatur wandert mit nach installed/, das Protokoll nennt Art und Schluessel
        Sandbox sb;
        spit(sb.real(kRulesPath), slurp(kShipped));
        put(sb, "rules.json", new_text);
        put(sb, "rules.json.sig", sig);
        auto p = make_snapshot_platform(sb.dir.string(), true);
        const auto r = install_rule_update(*p, old_ver, ts, &keys);
        CHECK(r.ok && r.new_version == "2026.12.05", "Signatur-Update: Installation");
        const auto tree = sb.tree();
        CHECK(tree.count("fs/data/ps5medic/knowledge/update/installed/" + ts + "/rules.json.sig") == 1 && tree.count("fs/data/ps5medic/knowledge/update/rules.json.sig") == 0,
              "Signatur-Update: die Signaturdatei liegt danach unter installed/");
        const std::string log = slurp(sb.real(kRulesUpdateLog));
        CHECK(log.find("\"auth\":\"signature\"") != std::string::npos && log.find(key_id_of(key_a().pk)) != std::string::npos, "Signatur-Update: update.log nennt Art und Schluessel");
        CHECK(slurp(sb.real(kRulesPath)) == new_text, "Signatur-Update: aktive Datei ist die signierte");
    }
    {  // Installation lehnt eine veraenderte Datei ab, auch wenn vorher geprueft wurde
        Sandbox sb;
        spit(sb.real(kRulesPath), slurp(kShipped));
        put(sb, "rules.json", tampered);
        put(sb, "rules.json.sig", sig);
        auto p = make_snapshot_platform(sb.dir.string(), true);
        const auto before = sb.tree();
        const auto r = install_rule_update(*p, old_ver, ts, &keys);
        CHECK(!r.ok && r.error == "sig_invalid" && sb.tree() == before && slurp(sb.real(kRulesPath)) == slurp(kShipped), "Signatur-Update: Installation bricht bei ungueltiger Signatur ab, nichts aendert sich");
    }
}

static void download_tests() {
    const std::string ts = "20261109T130000Z";
    const std::string text = rules_text_with_version("2026.12.07");
    const std::string sigtext = make_signature_file(key_a(), text);
    const std::string base = "http://192.168.1.50:8000/ps5medic";
    const std::vector<TrustedKey> keys = {make_trusted_key(key_a().pk)};
    const std::string old_ver = load_rules_from_string(slurp(kShipped)).db_version;

    {  // Adresse aus der Konfiguration
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string());
        CHECK(configured_update_url(p->fs()).empty(), "Download: ohne config.ini keine Adresse");
        spit(sb.real(kConfigPath), "# Kommentar\nlang=de\n  update_url =  http://10.0.0.5:8000/regeln/  \n; noch einer\n");
        CHECK(configured_update_url(p->fs()) == "http://10.0.0.5:8000/regeln/", "Download: update_url aus config.ini, Leerraum und Kommentare ignoriert");
        spit(sb.real(kConfigPath), "update_url=http://a/\nupdate_url=http://b/\n");
        CHECK(configured_update_url(p->fs()) == "http://b/", "Download: die letzte update_url-Zeile gilt");
    }
    {  // Erfolg: beide Dateien, danach prueft check_rule_update und nimmt sie an
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        http.answers[base + "/rules.json"] = reply200(text);
        http.answers[base + "/rules.json.sig"] = reply200(sigtext);
        const auto r = download_rule_update(*p, http, base, ts);
        CHECK(r.ok && r.bytes == text.size() && http.asked.size() == 3 && http.asked[0] == base + "/rules.json" && http.asked[1] == base + "/rules.json.sig" && http.asked[2] == base + "/error_codes.json",
              "Download: holt rules.json und rules.json.sig, dann fragt er nach error_codes.json (der Server bietet sie hier nicht an: 404)");
        CHECK(!r.codes_ok && r.codes_error.empty() && !fs::exists(sb.real(std::string(kRulesUpdateDir) + "/error_codes.json")), "Download: ohne Fehlercodes beim Server bleibt es beim Regel-Update");
        CHECK(slurp(sb.real(std::string(kRulesUpdateDir) + "/rules.json")) == text && slurp(sb.real(std::string(kRulesUpdateDir) + "/rules.json.sig")) == sigtext, "Download: beide Dateien liegen im Update-Ordner");
        CHECK(slurp(sb.real(kRulesUpdateLog)).find("rules_download") != std::string::npos, "Download: update.log vermerkt ihn");
        const auto o = check_rule_update(p->fs(), old_ver, nullptr, &keys);
        CHECK(o.state == RuleUpdateOffer::State::Ready && o.auth == "signature", "Download: danach angeboten, mit Signatur geprueft");
        // Der Download allein aendert die aktiven Regeln nicht.
        CHECK(!fs::exists(sb.real(kRulesPath)), "Download: aktive Regeldatei bleibt unberuehrt");
        // Das Protokoll mit dem Download stoert den Rollback nicht: es gibt nichts zurueckzunehmen.
        CHECK(!o.can_rollback, "Download: allein noch nichts zum Zuruecknehmen");
    }
    {  // Basisadresse mit Schraegstrich am Ende
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        http.answers[base + "/rules.json"] = reply200(text);
        http.answers[base + "/rules.json.sig"] = reply200(sigtext);
        CHECK(download_rule_update(*p, http, base + "/", ts).ok, "Download: Adresse mit Schraegstrich am Ende");
    }
    {  // Was schon dort liegt, wird beiseite gelegt, nicht ueberschrieben
        Sandbox sb;
        put(sb, "rules.json", "alt");
        put(sb, "rules.json.sha256", "alt-summe");
        put(sb, "rules.json.sig", "alt-sig");
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        http.answers[base + "/rules.json"] = reply200(text);
        http.answers[base + "/rules.json.sig"] = reply200(sigtext);
        const auto r = download_rule_update(*p, http, base, ts);
        const std::string aside = std::string(kRulesUpdateDir) + "/replaced/" + ts;
        CHECK(r.ok && slurp(sb.real(aside + "/rules.json")) == "alt" && slurp(sb.real(aside + "/rules.json.sha256")) == "alt-summe" && slurp(sb.real(aside + "/rules.json.sig")) == "alt-sig" &&
                  !fs::exists(sb.real(std::string(kRulesUpdateDir) + "/rules.json.sha256")),
              "Download: vorhandene Update-Dateien wandern nach update/replaced/<zeit>/");
    }
    struct Fail {
        const char* name;
        std::string base;
        std::map<std::string, HttpResponse> answers;
        std::string error;
        std::string file;
    };
    HttpResponse not_found;
    not_found.status = 404;
    HttpResponse refused;
    refused.error = "connect";
    HttpResponse server_error;
    server_error.status = 500;
    const Fail fails[] = {
        {"keine Adresse", "", {}, "no_url", ""},
        {"https", "https://example.org/r", {}, "https_unsupported", ""},
        {"kaputte Adresse", "ftp://host/", {}, "bad_url", ""},
        {"Regeldatei nicht gefunden", base, {{base + "/rules.json", not_found}}, "http_status", "rules.json"},
        {"Signatur nicht gefunden", base, {{base + "/rules.json", reply200(text)}, {base + "/rules.json.sig", not_found}}, "http_status", "rules.json.sig"},
        {"Serverfehler", base, {{base + "/rules.json", server_error}}, "http_status", "rules.json"},
        {"Verbindung gescheitert", base, {{base + "/rules.json", refused}}, "net_connect", "rules.json"},
        {"Regeldatei zu gross", base, {{base + "/rules.json", reply200(std::string(kMaxRulesBytes + 1, 'x'))}}, "net_too_large", "rules.json"},
        {"Signatur zu gross", base, {{base + "/rules.json", reply200(text)}, {base + "/rules.json.sig", reply200(std::string(kMaxSigBytes + 1, 'x'))}}, "net_too_large", "rules.json.sig"},
    };
    for (const auto& f : fails) {
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        http.answers = f.answers;
        const auto before = sb.tree();
        const auto r = download_rule_update(*p, http, f.base, ts);
        CHECK(!r.ok && r.error == f.error && r.file == f.file && sb.tree() == before, std::string("Download: ") + f.name + " -> " + f.error + ", Dateibaum unveraendert (war: " + r.error + ")");
    }
    {  // ohne Schreibzugriff und mit ungueltigem Zeitstempel: vorher nichts geholt
        Sandbox sb;
        auto ro = make_snapshot_platform(sb.dir.string());
        auto rw = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        http.answers[base + "/rules.json"] = reply200(text);
        http.answers[base + "/rules.json.sig"] = reply200(sigtext);
        CHECK(download_rule_update(*ro, http, base, ts).error == "no_write" && download_rule_update(*rw, http, base, "../x").error == "bad_timestamp" && http.asked.empty(),
              "Download: ohne Schreibzugriff oder mit schlechtem Zeitstempel wird gar nichts geholt");
    }
    {  // Schreibgrenze: nur unter knowledge/
        Sandbox sb;
        auto p = make_snapshot_platform(sb.dir.string(), true);
        FakeHttp http;
        http.answers[base + "/rules.json"] = reply200(text);
        http.answers[base + "/rules.json.sig"] = reply200(sigtext);
        download_rule_update(*p, http, base, ts);
        bool outside = false;
        for (const auto& t : sb.tree())
            if (t != "fs" && t.rfind("fs/data/ps5medic/knowledge", 0) != 0 && t != "fs/data" && t != "fs/data/ps5medic") outside = true;
        CHECK(!outside, "Download: schreibt nur unter /data/ps5medic/knowledge/");
    }
}

static void http_client_tests() {
    HttpUrl u;
    std::string err;
    CHECK(parse_http_url("http://192.168.1.5:8000/a/b.json", u, err) && u.host == "192.168.1.5" && u.port == 8000 && u.path == "/a/b.json", "URL: Host, Port, Pfad");
    CHECK(parse_http_url("http://example.org", u, err) && u.host == "example.org" && u.port == 80 && u.path == "/", "URL: ohne Port und Pfad");
    CHECK(parse_http_url("HTTP://Host/x#frag", u, err) && u.path == "/x", "URL: Grossschreibung des Schemas, Fragment entfaellt");
    CHECK(!parse_http_url("https://example.org/", u, err) && err == "https_unsupported", "URL: https wird mit eigener Kennung abgelehnt");
    for (const char* bad : {"", "example.org", "ftp://h/", "http://", "http:///x", "http://h:/x", "http://h:0/x", "http://h:99999/x", "http://h:80a/x", "http://-h/x", "http://h h/x", "http://h/a b", "http://h/a\r\nHost: x", "http://us@er/x"})
        CHECK(!parse_http_url(bad, u, err) && err == "bad_url", std::string("URL: abgelehnt: '") + bad + "'");
    CHECK(join_url("http://h/a", "r.json") == "http://h/a/r.json" && join_url("http://h/a/", "r.json") == "http://h/a/r.json", "URL: Namen anhaengen");

    auto client = make_posix_http_client();
    {  // normale Antwort; die Anfrage enthaelt nur das Noetigste
        TestServer srv({{ok_reply("hallo")}});
        const auto r = client->get(srv.url("/regeln/rules.json"), 1000, 3000);
        CHECK(r.error.empty() && r.status == 200 && r.body == "hallo", "HTTP: 200 mit Content-Length");
        srv.th.join();
        const std::string& req = srv.requests.at(0);
        CHECK(req == "GET /regeln/rules.json HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(srv.port) + "\r\nUser-Agent: PS5-Medic\r\nAccept: */*\r\nConnection: close\r\n\r\n",
              "HTTP: die Anfrage enthaelt nur GET, Host, Kennung, Accept und Connection (nichts ueber Nutzer oder Konsole)");
    }
    {
        TestServer srv({{"HTTP/1.0 200 OK\r\n\r\nohne laenge"}});
        const auto r = client->get(srv.url(), 1000, 3000);
        CHECK(r.error.empty() && r.status == 200 && r.body == "ohne laenge", "HTTP: Koerper bis Verbindungsende");
    }
    {
        TestServer srv({{"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n2;ext=1\r\nde\r\n0\r\n\r\n"}});
        const auto r = client->get(srv.url(), 1000, 3000);
        CHECK(r.error.empty() && r.body == "abcde", "HTTP: chunked");
    }
    {
        TestServer srv({{"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"}});
        const auto r = client->get(srv.url(), 1000, 3000);
        CHECK(r.error.empty() && r.status == 404 && r.body.empty(), "HTTP: 404 ist eine vollstaendige Antwort mit Status");
    }
    {
        TestServer srv({{"HTTP/1.1 301 Moved Permanently\r\nLocation: http://elsewhere/\r\nContent-Length: 0\r\n\r\n"}});
        const auto r = client->get(srv.url(), 1000, 3000);
        CHECK(r.error == "redirect" && r.status == 301, "HTTP: Weiterleitungen werden nicht verfolgt");
        CHECK(srv.requests.size() <= 1, "HTTP: nach einer Weiterleitung keine zweite Anfrage");
    }
    {
        TestServer srv({{"HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nkurz"}});
        CHECK(client->get(srv.url(), 10, 3000).error == "too_large", "HTTP: Content-Length ueber der Grenze");
    }
    {
        TestServer srv({{"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc"}});
        CHECK(client->get(srv.url(), 1000, 3000).error == "bad_response", "HTTP: abgeschnittener Koerper");
    }
    {
        TestServer srv({{"HTTP/1.1 200 OK\r\n\r\n" + std::string(200000, 'x')}});
        CHECK(client->get(srv.url(), 1000, 3000).error == "too_large", "HTTP: ohne Laenge, aber zu viele Daten");
    }
    {
        TestServer srv({{"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFF\r\nabc"}});
        const auto r = client->get(srv.url(), 1000, 3000);
        CHECK(r.error == "too_large" || r.error == "bad_response", "HTTP: chunked mit gigantischer Blockgroesse");
    }
    {
        TestServer srv({{"das ist kein HTTP\r\n\r\n"}});
        CHECK(client->get(srv.url(), 1000, 3000).error == "bad_response", "HTTP: Muell statt Statuszeile");
    }
    {
        TestServer srv({{"", 0, false}});
        CHECK(client->get(srv.url(), 1000, 3000).error == "bad_response", "HTTP: Verbindung ohne Antwort geschlossen");
    }
    {
        TestServer srv({{ok_reply("spaet"), 2000, false}});
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = client->get(srv.url(), 1000, 400);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(r.error == "timeout" && ms < 1500, "HTTP: Zeitueberschreitung bricht rechtzeitig ab");
    }
    {  // Port ohne Dienst
        int port = 0;
        {
            TestServer tmp({});
            port = tmp.port;
        }
        const auto r = client->get("http://127.0.0.1:" + std::to_string(port) + "/", 1000, 2000);
        CHECK(r.error == "connect" && r.status == 0, "HTTP: nichts hoert zu -> connect");
    }
    CHECK(client->get("https://127.0.0.1/", 10, 100).error == "https_unsupported" && client->get("gibt-es-nicht", 10, 100).error == "bad_url", "HTTP: Adressfehler ohne Netzzugriff");
}

void sign_tests() {
    crypto_tests();
    signature_file_tests();
    signed_update_tests();
    download_tests();
    http_client_tests();
}
