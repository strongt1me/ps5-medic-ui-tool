// SHA-256 ohne Fremdabhaengigkeit (FIPS 180-4). Fuer Journal und Backup-Pruefung.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace medic {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t len);
    std::string hex();  // schliesst ab; danach nicht mehr update() aufrufen

private:
    void block(const std::uint8_t* p);
    std::uint32_t h_[8];
    std::uint8_t buf_[64];
    std::size_t buf_len_ = 0;
    std::uint64_t total_ = 0;
};

std::string sha256_hex(const std::string& data);

}  // namespace medic
