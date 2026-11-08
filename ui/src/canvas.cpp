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

void Canvas::draw_pad_glyph(PadGlyph g, int cx, int cy, int size, Color c) {
    struct Seg {
        float ax, ay, bx, by;
    };
    const float s = static_cast<float>(size), fx = static_cast<float>(cx), fy = static_cast<float>(cy);
    const float half = std::max(1.4f, 0.075f * s);  // halbe Strichstaerke
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
