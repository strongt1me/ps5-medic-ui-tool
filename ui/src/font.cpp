// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/font.hpp"

#include "font_data.hpp"

#include <algorithm>

namespace medic::ui {

namespace {

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

}  // namespace

Font::Font(const fontdata::Blob& blob) : ascent_(blob.ascent), line_height_(blob.line_height) {
    glyphs_.reserve(blob.nrecs);
    for (std::size_t i = 0; i < blob.nrecs; ++i) {
        const auto& r = blob.recs[i];
        glyphs_.push_back({r.cp, r.bx, r.by, r.w, r.h, r.adv, r.off});
    }
    alpha_.resize(blob.hexlen / 2);
    for (std::size_t i = 0; i < alpha_.size(); ++i) alpha_[i] = static_cast<std::uint8_t>(hex_val(blob.hex[2 * i]) * 16 + hex_val(blob.hex[2 * i + 1]));
}

const Font& Font::get(Id id) {
    static const Font small(fontdata::small_blob), body(fontdata::body_blob), title(fontdata::title_blob);
    switch (id) {
        case Id::Small: return small;
        case Id::Title: return title;
        case Id::Body: break;
    }
    return body;
}

const Font::Glyph& Font::find(std::uint32_t cp) const {
    auto it = std::lower_bound(glyphs_.begin(), glyphs_.end(), cp, [](const Glyph& g, std::uint32_t v) { return g.cp < v; });
    if (it != glyphs_.end() && it->cp == cp) return *it;
    // Markenzeichen in Spielnamen ("Spider-Man™", "®", "©") fehlen in der Schrift: ohne Platzhalter weglassen statt "?" zu zeigen
    static const Glyph kBlank{0, 0, 0, 0, 0, 0, 0};
    if (cp == 0x2122 || cp == 0xAE || cp == 0xA9) return kBlank;
    if (cp != '?') return find('?');
    return glyphs_.front();
}

std::uint32_t next_codepoint(std::string_view s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    const int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (n == 0) return 0xFFFD;
    std::uint32_t cp = c & (0x3Fu >> n);
    for (int k = 0; k < n; ++k) {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return cp;
}

int Font::width(std::string_view s) const {
    int w = 0;
    for (std::size_t i = 0; i < s.size();) {
        std::uint32_t cp = next_codepoint(s, i);
        if (cp == '\t') cp = ' ';
        if (cp < 32) continue;
        w += find(cp).adv;
    }
    return w;
}

void Font::draw(Canvas& c, int x, int y_top, std::string_view s, Color color, int max_width) const {
    const int base = y_top + ascent_;
    int pen = 0;
    for (std::size_t i = 0; i < s.size();) {
        std::uint32_t cp = next_codepoint(s, i);
        if (cp == '\t') cp = ' ';
        if (cp < 32) continue;
        const Glyph& g = find(cp);
        if (max_width > 0 && pen + g.adv > max_width) break;
        const std::uint8_t* a = alpha_.data() + g.off;
        for (int gy = 0; gy < g.h; ++gy)
            for (int gx = 0; gx < g.w; ++gx) {
                const unsigned cov = a[static_cast<std::size_t>(gy) * g.w + gx];
                if (cov) c.put(x + pen + g.bx + gx, base - g.by + gy, color, cov);
            }
        pen += g.adv;
    }
}

namespace {

// Byte-Offsets der Zeichenanfaenge (plus Ende).
std::vector<std::size_t> char_starts(std::string_view s) {
    std::vector<std::size_t> v;
    for (std::size_t i = 0; i < s.size();) {
        v.push_back(i);
        next_codepoint(s, i);
    }
    v.push_back(s.size());
    return v;
}

}  // namespace

std::string elide(const Font& f, std::string_view text, int max_width) {
    if (f.width(text) <= max_width) return std::string(text);
    static const std::string kDots = "…";
    const int dots = f.width(kDots);
    auto starts = char_starts(text);
    std::size_t n = starts.size() - 1;
    while (n > 0 && f.width(text.substr(0, starts[n])) + dots > max_width) --n;
    return std::string(text.substr(0, starts[n])) + kDots;
}

std::vector<std::string> wrap_text(const Font& f, std::string_view text, int max_width, int max_lines) {
    std::vector<std::string> lines;
    const int space = f.width(" ");
    bool truncated = false;

    auto push = [&](std::string l) {
        if (max_lines > 0 && static_cast<int>(lines.size()) >= max_lines) {
            truncated = true;
            return;
        }
        lines.push_back(std::move(l));
    };

    std::size_t para_begin = 0;
    while (para_begin <= text.size() && !truncated) {
        std::size_t para_end = text.find('\n', para_begin);
        if (para_end == std::string_view::npos) para_end = text.size();
        std::string_view para = text.substr(para_begin, para_end - para_begin);
        if (!para.empty() && para.back() == '\r') para.remove_suffix(1);

        std::string cur;
        int cur_w = 0;
        std::size_t pos = 0;
        bool any = false;
        while (pos < para.size()) {
            while (pos < para.size() && para[pos] == ' ') ++pos;
            std::size_t end = para.find(' ', pos);
            if (end == std::string_view::npos) end = para.size();
            std::string_view word = para.substr(pos, end - pos);
            pos = end;
            if (word.empty()) break;
            any = true;
            // Zu langes Wort: zeichenweise trennen.
            while (f.width(word) > max_width) {
                if (!cur.empty()) {
                    push(cur);
                    cur.clear();
                    cur_w = 0;
                }
                auto starts = char_starts(word);
                std::size_t n = 1;
                while (n + 1 < starts.size() && f.width(word.substr(0, starts[n + 1])) <= max_width) ++n;
                push(std::string(word.substr(0, starts[n])));
                word = word.substr(starts[n]);
                if (word.empty()) break;
            }
            if (word.empty()) continue;
            const int ww = f.width(word);
            if (cur.empty()) {
                cur = std::string(word);
                cur_w = ww;
            } else if (cur_w + space + ww <= max_width) {
                cur += ' ';
                cur += word;
                cur_w += space + ww;
            } else {
                push(cur);
                cur = std::string(word);
                cur_w = ww;
            }
        }
        if (!cur.empty()) push(cur);
        else if (!any) push("");  // Leerzeile bleibt erhalten
        if (para_end >= text.size()) break;
        para_begin = para_end + 1;
    }

    if (truncated && !lines.empty()) lines.back() = elide(f, lines.back() + " …", max_width);
    return lines;
}

}  // namespace medic::ui
