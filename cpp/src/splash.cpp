// Firmament - the splash: the logo's dome of sky made large, a field of stars over a horizon, the name, the version,
// the credits and what's loading. Atelier greys and the Cursed pink, solid; drawn with ImGui's draw list.
#include "splash.h"

#include <imgui.h>

#include <cfloat>
#include <cmath>

#ifndef FIRMAMENT_VERSION
#define FIRMAMENT_VERSION "2.0.0"
#endif

namespace Splash {

static ImFont *titleFont = nullptr, *smallFont = nullptr;
void setFonts(ImFont* title, ImFont* small) { titleFont = title; smallFont = small; }

static const ImU32 BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), LINE = IM_COL32(0x33, 0x33, 0x33, 255),
                   TEXT = IM_COL32(0xd6, 0xd6, 0xd6, 255), DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255),
                   PINK = IM_COL32(0xe0, 0x40, 0x8a, 255), DEEP = IM_COL32(0x9e, 0x3a, 0x6a, 255);

static void text(ImDrawList* dl, ImFont* f, float size, ImVec2 p, ImU32 col, const char* s) { dl->AddText(f ? f : ImGui::GetFont(), size, p, col, s); }
static float textWidth(ImFont* f, float size, const char* s) { return (f ? f : ImGui::GetFont())->CalcTextSizeA(size, FLT_MAX, 0, s).x; }
static float hash(int i, int k) { unsigned h = (unsigned)i * 0x8da6b343u ^ (unsigned)k * 0xd8163841u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (h & 0xffffff) / 16777215.0f; }

void draw(ImDrawList* dl, float x, float y, const char* status, float progress) {
    const ImVec2 o(x, y), end(x + W, y + H);
    dl->PushClipRect(o, end, true);
    dl->AddRectFilled(o, end, BG);

    // the dome: a pink arc over the horizon on the right, the sky inside it full of stars
    const float cx = x + 470, cy = y + 250, r = 150;
    for (int i = 0; i < 260; ++i) {
        const float a = hash(i, 1) * 3.14159f, d = std::sqrt(hash(i, 2)) * (r - 14);
        const ImVec2 p(cx + std::cos(a) * d, cy - std::sin(a) * d);
        const float s = 0.6f + 1.8f * hash(i, 3) * hash(i, 3);
        const int g = (int)(0x60 + 0x90 * hash(i, 4));
        dl->AddCircleFilled(p, s, (i % 37 == 0) ? PINK : IM_COL32(g, g, g, 255), 8);
    }
    // a few stars outside the dome too, fainter
    for (int i = 0; i < 90; ++i) {
        const ImVec2 p(x + 300 + hash(i, 7) * 340, y + 12 + hash(i, 8) * 220);
        const float dx = p.x - cx, dy = p.y - cy;
        if (dx * dx + dy * dy < (r + 14) * (r + 14)) continue;
        dl->AddCircleFilled(p, 0.6f + hash(i, 9), IM_COL32(0x55, 0x55, 0x55, 255), 6);
    }
    dl->PathArcTo(ImVec2(cx, cy), r, 3.14159f, 6.28318f, 64);
    dl->PathStroke(PINK, 0, 16);
    dl->AddRectFilled(ImVec2(cx - r - 30, cy), ImVec2(cx + r + 30, cy + 8), IM_COL32(0x9a, 0x9a, 0x9a, 255));
    dl->AddRectFilled(ImVec2(cx - r + 20, cy + 30), ImVec2(cx + r - 20, cy + 36), LINE);

    // the name: "Firma" in the text colour, "ment" in pink
    const float ts = 50, tx = x + 40, ty = y + 92;
    text(dl, titleFont, ts, ImVec2(tx, ty), TEXT, "Firma");
    text(dl, titleFont, ts, ImVec2(tx + textWidth(titleFont, ts, "Firma"), ty), PINK, "ment");
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 62), DIM, "Version " FIRMAMENT_VERSION);
    text(dl, nullptr, 16, ImVec2(tx + 2, ty + 84), DIM, "360\xc2\xb0 skies by Cursed Entertainment");

    const float cs = 13, cry = y + 306;
    text(dl, smallFont, cs, ImVec2(tx, cry), DIM, "Krea 2 Turbo and mickmumpitz's 360\xc2\xb0 LoRA, run by stable-diffusion.cpp.");
    text(dl, smallFont, cs, ImVec2(tx, cry + 17), DIM, "Real-ESRGAN upscaling. Built with ggml, Dear ImGui and SDL2.");
    text(dl, smallFont, cs, ImVec2(tx, cry + 34), DIM, "Farica\xe2\x98\x86Kimora \xc2\xa9 Cursed Entertainment");

    if (status) text(dl, nullptr, 16, ImVec2(tx, y + 368), TEXT, status);
    if (progress >= 0) {
        const float p = progress > 1 ? 1 : progress;
        dl->AddRectFilled(ImVec2(x, end.y - 3), end, LINE);
        dl->AddRectFilled(ImVec2(x, end.y - 3), ImVec2(x + W * p, end.y), PINK);
    }
    dl->AddRect(o, end, LINE);
    dl->PopClipRect();
    (void)DEEP;
}

}
