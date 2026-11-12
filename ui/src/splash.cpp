// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic_ui/splash.hpp"

#include <cstdint>
#include <cstdlib>

// stb_image (Sean Barrett, Public Domain / MIT, third_party/stb): nur JPEG und PNG, nur aus dem Speicher.
#define STB_IMAGE_IMPLEMENTATION 1
#define STBI_ONLY_PNG 1
#define STBI_ONLY_JPEG 1
#define STBI_NO_STDIO 1
#define STBI_NO_LINEAR 1
#define STBI_NO_THREAD_LOCALS 1
#include "stb/stb_image.h"

#ifdef MEDIC_SPLASH_PATH
// Das Bild steckt per .incbin in der Programmdatei (Pfad vom Build gesetzt).
__asm__(".section .rodata\n.balign 16\n.global medic_splash_jpg\nmedic_splash_jpg:\n.incbin \"" MEDIC_SPLASH_PATH "\"\n.global medic_splash_jpg_end\nmedic_splash_jpg_end:\n.previous\n");
extern "C" const unsigned char medic_splash_jpg[], medic_splash_jpg_end[];
#endif

namespace medic::ui {

bool decode_image(const unsigned char* data, std::size_t size, Canvas& out) {
    if (!data || size == 0 || size > 0x7fffffff) return false;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &n, 3);
    if (!px) return false;
    const int ow = out.width(), oh = out.height();
    for (int y = 0; y < oh; ++y) {
        const int sy = h == oh ? y : static_cast<int>(static_cast<std::int64_t>(y) * h / oh);
        for (int x = 0; x < ow; ++x) {
            const int sx = w == ow ? x : static_cast<int>(static_cast<std::int64_t>(x) * w / ow);
            const unsigned char* p = px + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(sx)) * 3;
            out.put(x, y, rgb(p[0], p[1], p[2]));
        }
    }
    stbi_image_free(px);
    return true;
}

bool load_splash(Canvas& out) {
#ifdef MEDIC_SPLASH_PATH
    return decode_image(medic_splash_jpg, static_cast<std::size_t>(medic_splash_jpg_end - medic_splash_jpg), out);
#else
    (void)out;
    return false;
#endif
}

}  // namespace medic::ui
