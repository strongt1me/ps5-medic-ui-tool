// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/image_reader.hpp"

#include "medic/imgread.h"

#include <algorithm>

namespace medic {

int image_reader_read_cb(void* ctx, std::uint64_t off, void* buf, std::size_t n);

namespace {

std::uint64_t file_size_of(const IFileSystem& fs, const std::string& path) {
    const auto slash = path.rfind('/');
    const std::string dir = slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    for (const auto& e : fs.list_dir(dir))
        if (!e.is_dir && e.name == name) return e.size;
    return 0;
}

}  // namespace

int image_reader_read_cb(void* ctx, std::uint64_t off, void* buf, std::size_t n) {
    const ImageReader* r = static_cast<const ImageReader*>(ctx);
    std::string tmp;
    if (!r->fs_->read_range(r->path_, off, n, tmp) || tmp.size() != n) return -1;
    std::copy(tmp.begin(), tmp.end(), static_cast<char*>(buf));
    return 0;
}

std::unique_ptr<ImageReader> ImageReader::open(const IFileSystem& fs, const std::string& image_path) { return open(fs, image_path, file_size_of(fs, image_path)); }

std::unique_ptr<ImageReader> ImageReader::open(const IFileSystem& fs, const std::string& image_path, std::uint64_t size) {
    if (size == 0) return nullptr;
    std::unique_ptr<ImageReader> r(new ImageReader());
    r->fs_ = &fs;
    r->path_ = image_path;
    r->r_ = imgr_open_cb(image_reader_read_cb, r.get(), size, nullptr, 0);
    if (!r->r_) return nullptr;
    r->kind_ = imgr_kind(r->r_);
    return r;
}

ImageReader::~ImageReader() {
    if (r_) imgr_close(r_);
}

bool ImageReader::read_file(const std::string& inner_path, std::uint64_t offset, std::size_t len, std::string& out) const {
    if (!r_) return false;
    std::string buf(len, char(0));
    std::size_t got = 0;
    while (got < len) {
        const long n = imgr_pread(r_, inner_path.c_str(), offset + got, &buf[got], len - got);
        if (n <= 0) return false;
        got += static_cast<std::size_t>(n);
    }
    out = std::move(buf);
    return true;
}

bool ImageReader::read_some(const std::string& inner_path, std::uint64_t offset, std::size_t len, std::string& out) const {
    if (!r_ || len == 0) return false;
    std::string buf(len, char(0));
    std::size_t got = 0;
    while (got < len) {
        const long n = imgr_pread(r_, inner_path.c_str(), offset + got, &buf[got], len - got);
        if (n < 0) return false;
        if (n == 0) break;  // Dateiende
        got += static_cast<std::size_t>(n);
    }
    if (got == 0) return false;
    buf.resize(got);
    out = std::move(buf);
    return true;
}

namespace {
struct ListCtx {
    std::vector<ImageReader::Entry>* out;
    std::size_t max;
};
int list_cb(const char* name, int is_dir, void* ctx) {
    ListCtx* c = static_cast<ListCtx*>(ctx);
    if (c->out->size() >= c->max) return 1;  // genug: stoppen
    c->out->push_back({name ? name : "", is_dir != 0});
    return 0;
}
}  // namespace

bool ImageReader::list_dir(const std::string& inner_dir, std::vector<Entry>& out, std::size_t max) const {
    if (!r_) return false;
    std::vector<Entry> tmp;
    ListCtx ctx{&tmp, max};
    if (imgr_list(r_, inner_dir.c_str(), list_cb, &ctx) < 0) return false;
    out = std::move(tmp);
    return true;
}

}  // namespace medic
