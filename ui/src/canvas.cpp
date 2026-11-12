// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/canvas.hpp"

#include <algorithm>
#include <cmath>

namespace medic::ui {

Color blend(Color dst, Color src, unsigned cover) {
    const unsigned a = ((src >> 24) & 0xFF) * cover / 255;
    if (a >= 255) return 0xFF000000u | (src & 0xFFFFFFu);
    if (a == 0) return dst;
    auto mix = [&](unsigned shift) {
        const unsigned d = (dst >> shift) & 0xFF, s = (src >> shift) & 0xFF;
        return (d * (255 - a) + s * a + 127) / 255;
    };
    return 0xFF000000u | (mix(16) << 16) | (mix(8) << 8) | mix(0);
}

Canvas::Canvas(int w, int h) : w_(std::max(w, 1)), h_(std::max(h, 1)), px_(static_cast<std::size_t>(w_) * h_, 0xFF000000u) {}

Color Canvas::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
    return px_[static_cast<std::size_t>(y) * w_ + x];
}

void Canvas::clear(Color c) { std::fill(px_.begin(), px_.end(), 0xFF000000u | (c & 0xFFFFFFu)); }

void Canvas::put(int x, int y, Color c, unsigned cover) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_ || cover == 0) return;
    auto& p = px_[static_cast<std::size_t>(y) * w_ + x];
    p = blend(p, c, cover);
}

void Canvas::fill_rect(int x, int y, int w, int h, Color c) {
    const int x0 = std::max(x, 0), y0 = std::max(y, 0), x1 = std::min(x + w, w_), y1 = std::min(y + h, h_);
    const bool opaque = (c >> 24) == 0xFF;
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            auto& p = px_[static_cast<std::size_t>(py) * w_ + px];
            p = opaque ? c : blend(p, c);
        }
}

namespace {

// Vorzeichenbehaftete Entfernung zum abgerundeten Rechteck (negativ = innen).
float sdf_round_box(float px, float py, float cx, float cy, float hw, float hh, float r) {
    const float qx = std::fabs(px - cx) - (hw - r), qy = std::fabs(py - cy) - (hh - r);
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}

float clamp01(float v) { return std::min(std::max(v, 0.0f), 1.0f); }

float seg_dist(float px, float py, float ax, float ay, float bx, float by) {
    const float dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    const float t = len2 > 0 ? clamp01(((px - ax) * dx + (py - ay) * dy) / len2) : 0.0f;
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

}  // namespace

void Canvas::fill_round_rect(int x, int y, int w, int h, int radius, Color c) {
    if (w <= 0 || h <= 0) return;
    const float r = static_cast<float>(std::min(radius, std::min(w, h) / 2));
    const float cx = x + w / 2.0f, cy = y + h / 2.0f, hw = w / 2.0f, hh = h / 2.0f;
    for (int py = std::max(y, 0); py < std::min(y + h, h_); ++py)
        for (int px = std::max(x, 0); px < std::min(x + w, w_); ++px) {
            const float sd = sdf_round_box(px + 0.5f, py + 0.5f, cx, cy, hw, hh, r);
            put(px, py, c, static_cast<unsigned>(clamp01(0.5f - sd) * 255.0f));
        }
}

void Canvas::stroke_round_rect(int x, int y, int w, int h, int radius, int thickness, Color c) {
    if (w <= 0 || h <= 0 || thickness <= 0) return;
    const float r = static_cast<float>(std::min(radius, std::min(w, h) / 2));
    const float t = static_cast<float>(thickness);
    const float cx = x + w / 2.0f, cy = y + h / 2.0f, hw = w / 2.0f, hh = h / 2.0f;
    for (int py = std::max(y, 0); py < std::min(y + h, h_); ++py)
        for (int px = std::max(x, 0); px < std::min(x + w, w_); ++px) {
            const float sd = sdf_round_box(px + 0.5f, py + 0.5f, cx, cy, hw, hh, r);
            const float cov = clamp01(0.5f - sd) * clamp01(0.5f + sd + t);  // Ring innerhalb der Kante
            put(px, py, c, static_cast<unsigned>(cov * 255.0f));
        }
}

void Canvas::fill_vgradient(int x, int y, int w, int h, Color top, Color bottom) {
    if (w <= 0 || h <= 0) return;
    for (int py = std::max(y, 0); py < std::min(y + h, h_); ++py) {
        const unsigned t = static_cast<unsigned>(255 * (py - y) / std::max(h - 1, 1));
        const Color row = blend(top | 0xFF000000u, bottom | 0xFF000000u, t);
        for (int px = std::max(x, 0); px < std::min(x + w, w_); ++px) px_[static_cast<std::size_t>(py) * w_ + px] = row;
    }
}

void Canvas::fill_round_rect_vgradient(int x, int y, int w, int h, int radius, Color top, Color bottom) {
    if (w <= 0 || h <= 0) return;
    const float r = static_cast<float>(std::min(radius, std::min(w, h) / 2));
    const float cx = x + w / 2.0f, cy = y + h / 2.0f, hw = w / 2.0f, hh = h / 2.0f;
    for (int py = std::max(y, 0); py < std::min(y + h, h_); ++py) {
        const unsigned t = static_cast<unsigned>(255 * (py - y) / std::max(h - 1, 1));
        const Color row = blend(top | 0xFF000000u, bottom | 0xFF000000u, t);
        for (int px = std::max(x, 0); px < std::min(x + w, w_); ++px) {
            const float sd = sdf_round_box(px + 0.5f, py + 0.5f, cx, cy, hw, hh, r);
            put(px, py, row, static_cast<unsigned>(clamp01(0.5f - sd) * 255.0f));
        }
    }
}

void Canvas::fill_disc(float cx, float cy, float r, Color c) {
    for (int py = std::max(static_cast<int>(cy - r) - 1, 0); py <= std::min(static_cast<int>(cy + r) + 1, h_ - 1); ++py)
        for (int px = std::max(static_cast<int>(cx - r) - 1, 0); px <= std::min(static_cast<int>(cx + r) + 1, w_ - 1); ++px)
            put(px, py, c, static_cast<unsigned>(clamp01(r + 0.5f - std::hypot(px + 0.5f - cx, py + 0.5f - cy)) * 255.0f));
}

void Canvas::glow(int cx, int cy, int radius, Color c, unsigned max_alpha) {
    if (radius <= 0) return;
    const float rr = static_cast<float>(radius);
    for (int py = std::max(cy - radius, 0); py < std::min(cy + radius, h_); ++py)
        for (int px = std::max(cx - radius, 0); px < std::min(cx + radius, w_); ++px) {
            const float t = std::hypot(static_cast<float>(px - cx), static_cast<float>(py - cy)) / rr;
            if (t >= 1.0f) continue;
            const float f = (1.0f - t) * (1.0f - t);  // quadratisch ausklingend: weicher Rand
            put(px, py, c, static_cast<unsigned>(f * static_cast<float>(max_alpha)));
        }
}

void Canvas::stroke_polyline(const std::vector<std::pair<float, float>>& pts, float width, Color c) {
    // Je Teilstrecke nur ihr eigenes Rechteck; an den Gelenken wird zweimal gemischt, darum fuer deckende Farben gedacht.
    const float half = width / 2.0f;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const float ax = pts[i].first, ay = pts[i].second, bx = pts[i + 1].first, by = pts[i + 1].second;
        for (int py = std::max(static_cast<int>(std::min(ay, by) - half) - 1, 0); py <= std::min(static_cast<int>(std::max(ay, by) + half) + 1, h_ - 1); ++py)
            for (int px = std::max(static_cast<int>(std::min(ax, bx) - half) - 1, 0); px <= std::min(static_cast<int>(std::max(ax, bx) + half) + 1, w_ - 1); ++px) {
                const float d = seg_dist(px + 0.5f, py + 0.5f, ax, ay, bx, by);
                const unsigned cov = static_cast<unsigned>(clamp01(half + 0.5f - d) * 255.0f);
                if (cov == 0) continue;
                auto& p = px_[static_cast<std::size_t>(py) * w_ + px];
                p = blend(p, c, cov);
            }
    }
}

void Canvas::copy_from(const Canvas& o) {
    if (o.w_ == w_ && o.h_ == h_) px_ = o.px_;
}

void Canvas::draw_pad_glyph(PadGlyph g, int cx, int cy, int size, Color c) {
    struct Seg {
        float ax, ay, bx, by;
    };
    const float s = static_cast<float>(size), fx = static_cast<float>(cx), fy = static_cast<float>(cy);
    float half = std::max(1.4f, 0.075f * s);  // halbe Strichstaerke
    std::vector<Seg> segs;
    bool circle = false;
    switch (g) {
        case PadGlyph::Circle: circle = true; break;
        case PadGlyph::Cross: {
            const float k = 0.28f * s;
            segs = {{fx - k, fy - k, fx + k, fy + k}, {fx - k, fy + k, fx + k, fy - k}};
            break;
        }
        case PadGlyph::Square: {
            const float k = 0.29f * s;
            segs = {{fx - k, fy - k, fx + k, fy - k}, {fx + k, fy - k, fx + k, fy + k}, {fx + k, fy + k, fx - k, fy + k}, {fx - k, fy + k, fx - k, fy - k}};
            break;
        }
        case PadGlyph::Triangle: {
            const float ax = fx, ay = fy - 0.36f * s, bx = fx - 0.36f * s, by = fy + 0.27f * s, cx2 = fx + 0.36f * s;
            segs = {{ax, ay, bx, by}, {bx, by, cx2, by}, {cx2, by, ax, ay}};
            break;
        }
        case PadGlyph::UpDown: {
            const float k = 0.20f * s;
            segs = {{fx - k, fy - 0.14f * s, fx, fy - 0.34f * s}, {fx, fy - 0.34f * s, fx + k, fy - 0.14f * s},
                    {fx - k, fy + 0.14f * s, fx, fy + 0.34f * s}, {fx, fy + 0.34f * s, fx + k, fy + 0.14f * s}};
            break;
        }
        case PadGlyph::LeftRight: {
            const float k = 0.20f * s;
            segs = {{fx - 0.14f * s, fy - k, fx - 0.34f * s, fy}, {fx - 0.34f * s, fy, fx - 0.14f * s, fy + k},
                    {fx + 0.14f * s, fy - k, fx + 0.34f * s, fy}, {fx + 0.34f * s, fy, fx + 0.14f * s, fy + k}};
            break;
        }
        case PadGlyph::Shoulders: {  // zwei Tasten-Plaettchen mit "L" und "R" (wie die Schultertasten des Controllers), feinere Striche als die Formen
            half = std::max(1.2f, 0.045f * s);
            auto line = [&](float ax, float ay, float bx, float by) { segs.push_back({ax, ay, bx, by}); };
            auto arc = [&](float acx, float acy, float r, float a0, float a1) {  // Winkel in Grad, 0 = rechts, 90 = unten
                const int n = 7;
                float px = acx + r * std::cos(a0 * 3.14159265f / 180.0f), py = acy + r * std::sin(a0 * 3.14159265f / 180.0f);
                for (int i = 1; i <= n; ++i) {
                    const float a = (a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(n)) * 3.14159265f / 180.0f;
                    const float qx = acx + r * std::cos(a), qy = acy + r * std::sin(a);
                    line(px, py, qx, qy);
                    px = qx;
                    py = qy;
                }
            };
            auto pill = [&](float x0, float x1) {  // abgerundetes Rechteck
                const float y0 = fy - 0.30f * s, y1 = fy + 0.30f * s, r = 0.13f * s;
                line(x0 + r, y0, x1 - r, y0);
                line(x1, y0 + r, x1, y1 - r);
                line(x1 - r, y1, x0 + r, y1);
                line(x0, y1 - r, x0, y0 + r);
                arc(x1 - r, y0 + r, r, 270, 360);
                arc(x1 - r, y1 - r, r, 0, 90);
                arc(x0 + r, y1 - r, r, 90, 180);
                arc(x0 + r, y0 + r, r, 180, 270);
            };
            const float pl0 = fx - 0.52f * s, pl1 = fx - 0.13f * s, pr0 = fx + 0.13f * s, pr1 = fx + 0.52f * s;  // deutliche Luecke zwischen den Plaettchen
            pill(pl0, pl1);
            pill(pr0, pr1);
            const float lh = 0.15f * s, lw = 0.09f * s;  // halbe Buchstabenhoehe, halbe Buchstabenbreite
            const float lcx = (pl0 + pl1) / 2.0f, rcx = (pr0 + pr1) / 2.0f;
            line(lcx - lw, fy - lh, lcx - lw, fy + lh);  // L
            line(lcx - lw, fy + lh, lcx + lw, fy + lh);
            line(rcx - lw, fy - lh, rcx - lw, fy + lh);  // R: Stamm, Bogen, Bein
            line(rcx - lw, fy - lh, rcx + lw * 0.2f, fy - lh);
            arc(rcx + lw * 0.2f, fy - lh + lh * 0.5f, lh * 0.5f, 270, 450);
            line(rcx + lw * 0.2f, fy, rcx - lw, fy);
            line(rcx - lw * 0.1f, fy, rcx + lw, fy + lh);
            break;
        }
    }
    const int pad = static_cast<int>(half) + 2;
    for (int py = cy - size / 2 - pad; py <= cy + size / 2 + pad; ++py)
        for (int px = cx - size / 2 - pad; px <= cx + size / 2 + pad; ++px) {
            const float ppx = px + 0.5f, ppy = py + 0.5f;
            float d;
            if (circle) {
                d = std::fabs(std::hypot(ppx - fx, ppy - fy) - 0.33f * s);
            } else {
                d = 1e9f;
                for (const auto& sg : segs) d = std::min(d, seg_dist(ppx, ppy, sg.ax, sg.ay, sg.bx, sg.by));
            }
            put(px, py, c, static_cast<unsigned>(clamp01(half + 0.5f - d) * 255.0f));
        }
}

}  // namespace medic::ui
