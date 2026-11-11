// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/icons.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <utility>

namespace medic::ui {

namespace {

constexpr float kPi = 3.14159265f;

struct Pt {
    float x, y;
};

// Eine Form im Einheitsquadrat (-1..1, y nach unten): Linienzug (Strich) oder Polygon (Fuellung). Formen werden der Reihe nach angewendet;
// "erase" nimmt von allem davor etwas weg (Aussparung).
struct Shape {
    std::vector<Pt> pts;
    bool closed = false, fill = false, erase = false, accent = false;
    float w = 0.11f;  // Strichbreite
    Shape& acc() {
        accent = true;
        return *this;
    }
    Shape& er() {
        erase = true;
        return *this;
    }
    Shape& wd(float v) {
        w = v;
        return *this;
    }
};

class Builder {
public:
    std::vector<Shape> shapes;

    Shape& line(std::initializer_list<Pt> p) {
        shapes.push_back({std::vector<Pt>(p)});
        return shapes.back();
    }
    Shape& poly(std::initializer_list<Pt> p) {  // geschlossener Strich
        shapes.push_back({std::vector<Pt>(p)});
        shapes.back().closed = true;
        return shapes.back();
    }
    Shape& fillpoly(std::initializer_list<Pt> p) {
        shapes.push_back({std::vector<Pt>(p)});
        shapes.back().closed = shapes.back().fill = true;
        return shapes.back();
    }
    // Ellipsenbogen (Winkel in Grad, im Uhrzeigersinn, 0 = rechts) als Strich; 0..360 = geschlossen
    Shape& arc(float cx, float cy, float rx, float ry, float a0 = 0, float a1 = 360) {
        Shape s;
        const int n = std::max(10, static_cast<int>(std::fabs(a1 - a0) / 5.0f));
        for (int i = 0; i <= n; ++i) {
            const float a = (a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(n)) * kPi / 180.0f;
            s.pts.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
        }
        if (std::fabs(a1 - a0) >= 360.0f) {
            s.pts.pop_back();
            s.closed = true;
        }
        shapes.push_back(std::move(s));
        return shapes.back();
    }
    Shape& ring(float cx, float cy, float r) { return arc(cx, cy, r, r); }
    Shape& disc(float cx, float cy, float r) {
        Shape& s = arc(cx, cy, r, r);
        s.fill = true;
        return s;
    }
    Shape& rrect(float x, float y, float w, float h, float r, bool fill = false) {
        Shape s;
        r = std::min(r, std::min(w, h) / 2);
        const float cxs[4] = {x + w - r, x + w - r, x + r, x + r}, cys[4] = {y + r, y + h - r, y + h - r, y + r};
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i <= 6; ++i) {
                const float a = (static_cast<float>(k) * 90.0f - 90.0f + 90.0f * static_cast<float>(i) / 6.0f) * kPi / 180.0f;
                s.pts.push_back({cxs[k] + r * std::cos(a), cys[k] + r * std::sin(a)});
            }
        s.closed = true;
        s.fill = fill;
        shapes.push_back(std::move(s));
        return shapes.back();
    }
    // Kubische Bezierkurve als Punktfolge (fuer geschlossene Umrisse)
    static void curve(std::vector<Pt>& out, Pt p0, Pt c1, Pt c2, Pt p3) {
        for (int i = 1; i <= 12; ++i) {
            const float t = static_cast<float>(i) / 12.0f, u = 1 - t;
            out.push_back({u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p3.x, u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p3.y});
        }
    }
    Shape& path(std::vector<Pt> pts, bool fill) {
        Shape s;
        s.pts = std::move(pts);
        s.closed = true;
        s.fill = fill;
        shapes.push_back(std::move(s));
        return shapes.back();
    }
};

std::vector<Shape> build(Icon icon) {
    Builder b;
    switch (icon) {
        case Icon::Medic:
            b.rrect(-0.2f, -0.72f, 0.4f, 1.44f, 0.12f, true);
            b.rrect(-0.72f, -0.2f, 1.44f, 0.4f, 0.12f, true);
            break;
        case Icon::Pulse:
            b.line({{-0.95f, 0.08f}, {-0.45f, 0.08f}, {-0.25f, -0.6f}, {0.12f, 0.7f}, {0.34f, -0.22f}, {0.46f, 0.08f}, {0.95f, 0.08f}}).wd(0.13f);
            break;
        case Icon::Stethoscope:
            b.arc(0, -0.1f, 0.6f, 0.6f, 0, 180);
            b.line({{-0.6f, -0.1f}, {-0.6f, -0.8f}});
            b.line({{0.6f, -0.1f}, {0.6f, -0.8f}});
            b.disc(-0.6f, -0.82f, 0.09f);
            b.disc(0.6f, -0.82f, 0.09f);
            b.line({{0, 0.5f}, {0, 0.68f}, {0.3f, 0.68f}});
            b.ring(0.55f, 0.68f, 0.24f).acc();
            b.disc(0.55f, 0.68f, 0.1f).acc();
            break;
        case Icon::History:
            b.ring(0, 0, 0.85f);
            b.line({{0, -0.5f}, {0, 0}, {0.36f, 0.22f}});
            break;
        case Icon::Gear:
            for (int k = 0; k < 8; ++k) {
                const float a = static_cast<float>(k) * kPi / 4.0f, ca = std::cos(a), sa = std::sin(a);
                b.line({{0.6f * ca, 0.6f * sa}, {0.86f * ca, 0.86f * sa}}).wd(0.27f);
            }
            b.ring(0, 0, 0.58f).wd(0.16f);
            b.ring(0, 0, 0.2f).acc().wd(0.11f);
            break;
        case Icon::Book:
            b.poly({{0, -0.58f}, {-0.88f, -0.74f}, {-0.88f, 0.66f}, {0, 0.82f}, {0.88f, 0.66f}, {0.88f, -0.74f}});
            b.line({{0, -0.58f}, {0, 0.82f}});
            b.line({{-0.6f, -0.3f}, {-0.28f, -0.25f}}).acc();
            b.line({{-0.6f, 0.05f}, {-0.28f, 0.1f}}).acc();
            b.line({{0.6f, -0.3f}, {0.28f, -0.25f}}).acc();
            b.line({{0.6f, 0.05f}, {0.28f, 0.1f}}).acc();
            break;
        case Icon::Checklist:
            b.rrect(-0.66f, -0.78f, 1.32f, 1.68f, 0.15f);
            b.rrect(-0.3f, -0.96f, 0.6f, 0.3f, 0.1f, true);
            b.line({{-0.34f, 0.08f}, {-0.1f, 0.34f}, {0.36f, -0.22f}}).acc().wd(0.13f);
            break;
        case Icon::Critical:
            b.ring(0, 0, 0.86f);
            b.line({{-0.34f, -0.34f}, {0.34f, 0.34f}}).acc().wd(0.13f);
            b.line({{-0.34f, 0.34f}, {0.34f, -0.34f}}).acc().wd(0.13f);
            break;
        case Icon::Warning:
            b.poly({{0, -0.86f}, {0.92f, 0.7f}, {-0.92f, 0.7f}}).wd(0.13f);
            b.line({{0, -0.28f}, {0, 0.16f}}).acc().wd(0.13f);
            b.disc(0, 0.45f, 0.08f).acc();
            break;
        case Icon::Info:
            b.ring(0, 0, 0.86f);
            b.disc(0, -0.4f, 0.09f).acc();
            b.line({{0, -0.1f}, {0, 0.44f}}).acc().wd(0.13f);
            break;
        case Icon::Ok:
            b.ring(0, 0, 0.86f);
            b.line({{-0.4f, 0.02f}, {-0.1f, 0.32f}, {0.42f, -0.26f}}).acc().wd(0.14f);
            break;
        case Icon::Skip:
            b.ring(0, 0, 0.86f);
            b.line({{-0.36f, 0}, {0.36f, 0}}).acc().wd(0.13f);
            break;
        case Icon::Circle:
            b.ring(0, 0, 0.86f);
            break;
        case Icon::Drive:
            b.rrect(-0.92f, -0.2f, 1.84f, 0.96f, 0.2f);
            b.line({{-0.74f, -0.2f}, {-0.52f, -0.8f}, {0.52f, -0.8f}, {0.74f, -0.2f}});
            b.line({{-0.6f, 0.28f}, {-0.1f, 0.28f}}).acc();
            b.disc(0.56f, 0.28f, 0.09f).acc();
            break;
        case Icon::Thermometer:
            b.rrect(-0.22f, -0.92f, 0.44f, 1.3f, 0.22f);
            b.disc(0, 0.58f, 0.38f);
            b.line({{0, 0.5f}, {0, -0.4f}}).acc().wd(0.14f);
            b.disc(0, 0.58f, 0.2f).acc();
            break;
        case Icon::Database:
            b.arc(0, -0.58f, 0.72f, 0.26f);
            b.line({{-0.72f, -0.58f}, {-0.72f, 0.58f}});
            b.line({{0.72f, -0.58f}, {0.72f, 0.58f}});
            b.arc(0, 0.58f, 0.72f, 0.26f, 0, 180);
            b.arc(0, 0.0f, 0.72f, 0.26f, 0, 180).acc();
            break;
        case Icon::File:
            b.line({{0.2f, -0.88f}, {-0.58f, -0.88f}, {-0.58f, 0.88f}, {0.58f, 0.88f}, {0.58f, -0.5f}, {0.2f, -0.88f}, {0.2f, -0.5f}, {0.58f, -0.5f}});
            b.line({{-0.28f, 0.02f}, {0.28f, 0.02f}}).acc();
            b.line({{-0.28f, 0.4f}, {0.28f, 0.4f}}).acc();
            break;
        case Icon::Gauge:
            b.arc(0, 0.28f, 0.86f, 0.86f, 180, 360);
            b.line({{-0.86f, 0.28f}, {0.86f, 0.28f}}).wd(0.09f);
            b.line({{0, 0.28f}, {0.42f, -0.3f}}).acc().wd(0.14f);
            b.disc(0, 0.28f, 0.12f).acc();
            break;
        case Icon::Wifi:
            b.arc(0, 0.58f, 0.34f, 0.34f, 228, 312);
            b.arc(0, 0.58f, 0.66f, 0.66f, 228, 312);
            b.arc(0, 0.58f, 0.98f, 0.98f, 228, 312);
            b.disc(0, 0.58f, 0.1f).acc();
            break;
        case Icon::Flame: {
            std::vector<Pt> p = {{0, 0.95f}};
            Builder::curve(p, {0, 0.95f}, {0.36f, 0.95f}, {0.62f, 0.75f}, {0.62f, 0.45f});
            Builder::curve(p, {0.62f, 0.45f}, {0.62f, 0.1f}, {0.46f, -0.1f}, {0.3f, -0.28f});
            Builder::curve(p, {0.3f, -0.28f}, {0.22f, -0.5f}, {0.14f, -0.72f}, {0.02f, -0.95f});
            Builder::curve(p, {0.02f, -0.95f}, {0.0f, -0.7f}, {-0.1f, -0.5f}, {-0.18f, -0.34f});
            Builder::curve(p, {-0.18f, -0.34f}, {-0.28f, -0.4f}, {-0.38f, -0.46f}, {-0.46f, -0.6f});
            Builder::curve(p, {-0.46f, -0.6f}, {-0.7f, -0.2f}, {-0.62f, 0.15f}, {-0.62f, 0.45f});
            Builder::curve(p, {-0.62f, 0.45f}, {-0.62f, 0.75f}, {-0.36f, 0.95f}, {0, 0.95f});
            b.path(p, false);
            b.path({{0, 0.2f}, {0.2f, 0.5f}, {0.2f, 0.62f}, {0, 0.78f}, {-0.2f, 0.62f}, {-0.2f, 0.5f}}, true).acc();
            break;
        }
        case Icon::Chip:
            b.rrect(-0.52f, -0.52f, 1.04f, 1.04f, 0.12f);
            for (float v : {-0.26f, 0.26f}) {
                b.line({{v, -0.52f}, {v, -0.88f}});
                b.line({{v, 0.52f}, {v, 0.88f}});
                b.line({{-0.52f, v}, {-0.88f, v}});
                b.line({{0.52f, v}, {0.88f, v}});
            }
            b.rrect(-0.2f, -0.2f, 0.4f, 0.4f, 0.06f, true).acc();
            break;
        case Icon::Pencil:
            b.poly({{-0.78f, 0.78f}, {-0.58f, 0.2f}, {0.38f, -0.76f}, {0.74f, -0.4f}, {-0.22f, 0.56f}}).wd(0.12f);
            b.line({{-0.58f, 0.2f}, {-0.22f, 0.56f}}).wd(0.1f);
            b.line({{0.1f, -0.52f}, {0.5f, -0.12f}}).acc().wd(0.1f);
            break;
        case Icon::Gamepad:
            b.rrect(-0.96f, -0.5f, 1.92f, 1.18f, 0.52f);
            b.line({{-0.68f, 0.09f}, {-0.28f, 0.09f}}).acc().wd(0.13f);
            b.line({{-0.48f, -0.11f}, {-0.48f, 0.29f}}).acc().wd(0.13f);
            b.disc(0.38f, -0.02f, 0.09f).acc();
            b.disc(0.6f, 0.2f, 0.09f).acc();
            break;
        case Icon::Display:
            b.rrect(-0.92f, -0.7f, 1.84f, 1.2f, 0.14f);
            b.line({{0, 0.5f}, {0, 0.8f}});
            b.line({{-0.42f, 0.84f}, {0.42f, 0.84f}});
            b.line({{-0.5f, -0.15f}, {-0.1f, -0.15f}}).acc();
            break;
        case Icon::Wrench:
            b.ring(0.38f, -0.38f, 0.42f).wd(0.16f);
            b.line({{0.1f, -0.1f}, {-0.72f, 0.72f}}).wd(0.24f);
            b.fillpoly({{0.38f, -0.38f}, {1.0f, -0.12f}, {0.8f, -1.0f}}).er();
            break;
        case Icon::Globe:
            b.ring(0, 0, 0.86f);
            b.arc(0, 0, 0.38f, 0.86f).acc().wd(0.09f);
            b.line({{-0.86f, 0}, {0.86f, 0}}).acc().wd(0.09f);
            b.line({{-0.7f, -0.44f}, {0.7f, -0.44f}}).acc().wd(0.08f);
            b.line({{-0.7f, 0.44f}, {0.7f, 0.44f}}).acc().wd(0.08f);
            break;
        case Icon::Download:
            b.line({{0, -0.82f}, {0, 0.3f}}).wd(0.13f);
            b.line({{-0.4f, -0.1f}, {0, 0.34f}, {0.4f, -0.1f}}).wd(0.13f);
            b.line({{-0.8f, 0.34f}, {-0.8f, 0.82f}, {0.8f, 0.82f}, {0.8f, 0.34f}}).acc();
            break;
        case Icon::Report:
            b.rrect(-0.6f, -0.9f, 1.2f, 1.8f, 0.12f);
            b.line({{-0.3f, -0.45f}, {0.3f, -0.45f}}).acc();
            b.line({{-0.3f, -0.05f}, {0.3f, -0.05f}}).acc();
            b.line({{-0.3f, 0.35f}, {0.05f, 0.35f}}).acc();
            break;
        case Icon::Lightbulb:
            b.arc(0, -0.2f, 0.52f, 0.56f, 125, 415);
            b.line({{-0.3f, 0.26f}, {-0.2f, 0.62f}, {0.2f, 0.62f}, {0.3f, 0.26f}});
            b.line({{-0.16f, 0.88f}, {0.16f, 0.88f}}).acc();
            break;
        case Icon::Undo:
            b.arc(0.05f, 0.1f, 0.62f, 0.62f, 185, 440);
            b.fillpoly({{-0.98f, 0.1f}, {-0.3f, 0.1f}, {-0.64f, -0.36f}}).acc();
            break;
        case Icon::Shield:
            b.poly({{0, -0.95f}, {0.8f, -0.62f}, {0.8f, 0.1f}, {0.55f, 0.58f}, {0, 0.95f}, {-0.55f, 0.58f}, {-0.8f, 0.1f}, {-0.8f, -0.62f}});
            b.line({{-0.32f, 0.0f}, {-0.08f, 0.26f}, {0.36f, -0.24f}}).acc().wd(0.13f);
            break;
    }
    return std::move(b.shapes);
}

float seg_dist(float px, float py, Pt a, Pt b) {
    const float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    const float t = l2 > 0 ? std::min(std::max(((px - a.x) * dx + (py - a.y) * dy) / l2, 0.0f), 1.0f) : 0.0f;
    return std::hypot(px - (a.x + t * dx), py - (a.y + t * dy));
}

bool inside(const std::vector<Pt>& p, float x, float y) {  // gerade-ungerade Regel
    bool in = false;
    for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
        if ((p[i].y > y) != (p[j].y > y) && x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) + p[i].x) in = !in;
    return in;
}

struct Raster {
    int size = 0;
    std::vector<std::uint8_t> main, accent;
};

Raster rasterize(Icon icon, int size) {
    Raster r;
    r.size = size;
    r.main.assign(static_cast<std::size_t>(size) * size, 0);
    r.accent.assign(r.main.size(), 0);
    const auto shapes = build(icon);
    const float half = static_cast<float>(size) / 2.0f;
    const float scale = half * 0.86f;  // das Einheitsquadrat fuellt 86 % des Feldes: Rand fuer die Strichbreite
    const float px_unit = 1.0f / scale;
    for (const auto& s : shapes) {
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        for (const auto& p : s.pts) {
            minx = std::min(minx, p.x);
            maxx = std::max(maxx, p.x);
            miny = std::min(miny, p.y);
            maxy = std::max(maxy, p.y);
        }
        const float pad = (s.fill ? 0.0f : s.w / 2) + 2.0f * px_unit;
        const int x0 = std::max(0, static_cast<int>(std::floor((minx - pad) * scale + half))), x1 = std::min(size - 1, static_cast<int>(std::ceil((maxx + pad) * scale + half)));
        const int y0 = std::max(0, static_cast<int>(std::floor((miny - pad) * scale + half))), y1 = std::min(size - 1, static_cast<int>(std::ceil((maxy + pad) * scale + half)));
        const std::size_t n = s.pts.size();
        for (int py = y0; py <= y1; ++py)
            for (int px = x0; px <= x1; ++px) {
                const float ux = (static_cast<float>(px) + 0.5f - half) / scale, uy = (static_cast<float>(py) + 0.5f - half) / scale;
                float d = 1e9f;
                for (std::size_t i = 0; i + 1 < n; ++i) d = std::min(d, seg_dist(ux, uy, s.pts[i], s.pts[i + 1]));
                if (s.closed && n > 1) d = std::min(d, seg_dist(ux, uy, s.pts[n - 1], s.pts[0]));
                float cov;  // Deckkraft 0..1
                if (s.fill) {
                    const float sd = inside(s.pts, ux, uy) ? -d : d;
                    cov = std::min(std::max(0.5f - sd * scale, 0.0f), 1.0f);
                } else {
                    cov = std::min(std::max((s.w / 2 - d) * scale + 0.5f, 0.0f), 1.0f);
                }
                if (cov <= 0) continue;
                const std::size_t k = static_cast<std::size_t>(py) * size + px;
                const auto c8 = static_cast<unsigned>(cov * 255.0f + 0.5f);
                if (s.erase) {
                    r.main[k] = static_cast<std::uint8_t>(r.main[k] * (255 - c8) / 255);
                    r.accent[k] = static_cast<std::uint8_t>(r.accent[k] * (255 - c8) / 255);
                } else if (s.accent) {
                    r.accent[k] = static_cast<std::uint8_t>(std::max<unsigned>(r.accent[k], c8));
                    r.main[k] = static_cast<std::uint8_t>(r.main[k] * (255 - c8) / 255);  // Akzent ersetzt die Hauptfarbe darunter
                } else {
                    r.main[k] = static_cast<std::uint8_t>(std::max<unsigned>(r.main[k], c8));
                    r.accent[k] = static_cast<std::uint8_t>(r.accent[k] * (255 - c8) / 255);
                }
            }
    }
    return r;
}

const Raster& cached(Icon icon, int size) {
    static std::mutex mu;
    static std::map<std::pair<int, int>, Raster> cache;
    std::lock_guard<std::mutex> lock(mu);
    auto key = std::make_pair(static_cast<int>(icon), size);
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, rasterize(icon, size)).first;
    return it->second;  // std::map: Verweise bleiben beim Einfuegen gueltig
}

}  // namespace

Color lighten(Color c, unsigned amount) { return blend(c | 0xFF000000u, 0xFFFFFFFFu, amount); }
Color darken(Color c, unsigned amount) { return blend(c | 0xFF000000u, 0xFF000000u, amount); }

void draw_icon(Canvas& c, Icon icon, int cx, int cy, int size, Color color, Color accent) {
    if (size < 8) return;
    const Raster& r = cached(icon, size);
    if (accent == 0) accent = color;
    const int x0 = cx - size / 2, y0 = cy - size / 2;
    for (int py = 0; py < size; ++py)
        for (int px = 0; px < size; ++px) {
            const std::size_t k = static_cast<std::size_t>(py) * size + px;
            if (r.main[k]) c.put(x0 + px, y0 + py, color, r.main[k]);
            if (r.accent[k]) c.put(x0 + px, y0 + py, accent, r.accent[k]);
        }
}

void draw_icon_badge(Canvas& c, Icon icon, int x, int y, int size, Color tint) {
    c.fill_round_rect_vgradient(x, y, size, size, size * 28 / 100, lighten(tint, 40), darken(tint, 55));
    draw_icon(c, icon, x + size / 2, y + size / 2, size * 60 / 100, rgb(255, 255, 255), rgb(255, 255, 255));
}

}  // namespace medic::ui
