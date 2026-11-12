// Firmament - the panorama maths (see pano.h)
#include "pano.h"

#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace Pano {

static const float PI = 3.14159265358979f;

static FILE* openU8(const std::string& p, const char* mode) {
#ifdef _WIN32
    return _wfopen(fs::u8path(p).wstring().c_str(), std::wstring(mode, mode + strlen(mode)).c_str());
#else
    return fopen(p.c_str(), mode);
#endif
}

Image halfTurn(const Image& a) {
    Image o = a;
    const int s = a.w / 2;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x)
            memcpy(&o.px[((size_t)y * a.w + (x + s) % a.w) * 3], &a.px[((size_t)y * a.w + x) * 3], 3);
    return o;
}

void seamStrip(const Image& a, int band, Image& strip, Image& mask, int& x0) {
    const Image t = halfTurn(a);
    const int sw = std::min(SEAM_STRIP, a.w);
    x0 = a.w / 2 - sw / 2;
    strip.w = mask.w = sw; strip.h = mask.h = a.h;
    strip.px.resize((size_t)sw * a.h * 3); mask.px.assign((size_t)sw * a.h * 3, 0);
    const int b0 = sw / 2 - band / 2;
    for (int y = 0; y < a.h; ++y) {
        memcpy(&strip.px[(size_t)y * sw * 3], &t.px[((size_t)y * a.w + x0) * 3], (size_t)sw * 3);
        for (int x = std::max(0, b0); x < std::min(sw, b0 + band); ++x) memset(&mask.px[((size_t)y * sw + x) * 3], 255, 3);
    }
}

Image seamMerge(const Image& a, const Image& rep0, int x0, int band, int feather) {
    Image t = halfTurn(a);
    const int sw = std::min(SEAM_STRIP, a.w);
    const Image rep = (rep0.w != sw || rep0.h != a.h) ? resize(rep0, sw, a.h) : rep0;   // the model rounds sizes
    const float centre = sw / 2.0f, half = band / 2.0f;
    for (int x = 0; x < sw; ++x) {
        const float al = std::clamp((half - std::fabs(x + 0.5f - centre)) / std::max(1, feather) + 0.5f, 0.0f, 1.0f);
        if (al <= 0) continue;
        for (int y = 0; y < a.h; ++y)
            for (int c = 0; c < 3; ++c) {
                uint8_t& d = t.px[((size_t)y * a.w + x0 + x) * 3 + c];
                d = (uint8_t)std::clamp(d * (1 - al) + rep.px[((size_t)y * sw + x) * 3 + c] * al + 0.5f, 0.0f, 255.0f);
            }
    }
    return halfTurn(t);   // turning half round twice: back where it was (for even widths)
}

float seamError(const Image& a) {
    double s = 0;
    for (int y = 0; y < a.h; ++y)
        for (int c = 0; c < 3; ++c) {
            const auto at = [&](int x) { return (float)a.px[((size_t)y * a.w + x) * 3 + c]; };
            s += std::fabs((at(0) + at(1)) / 2 - (at(a.w - 2) + at(a.w - 1)) / 2);
        }
    return (float)(s / (a.h * 3.0));
}

Image wrapPad(const Image& a, int pad) {
    Image o; o.w = a.w + 2 * pad; o.h = a.h; o.px.resize((size_t)o.w * o.h * 3);
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < o.w; ++x)
            memcpy(&o.px[((size_t)y * o.w + x) * 3], &a.px[((size_t)y * a.w + ((x - pad) % a.w + a.w) % a.w) * 3], 3);
    return o;
}

Image unpad(const Image& a, int pad) {
    Image o; o.w = a.w - 2 * pad; o.h = a.h; o.px.resize((size_t)o.w * o.h * 3);
    for (int y = 0; y < a.h; ++y) memcpy(&o.px[(size_t)y * o.w * 3], &a.px[((size_t)y * a.w + pad) * 3], (size_t)o.w * 3);
    return o;
}

Image resize(const Image& a, int w, int h) {
    Image o; o.w = w; o.h = h; o.px.resize((size_t)w * h * 3);
    const float kx = (float)a.w / w, ky = (float)a.h / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (kx > 2 || ky > 2) {   // shrinking a lot: the average of the pixels it covers
                const int x0 = (int)(x * kx), x1 = std::max(x0 + 1, (int)((x + 1) * kx)), y0 = (int)(y * ky), y1 = std::max(y0 + 1, (int)((y + 1) * ky));
                unsigned s[3] = {0, 0, 0}, n = 0;
                for (int yy = y0; yy < y1 && yy < a.h; ++yy) for (int xx = x0; xx < x1 && xx < a.w; ++xx) { for (int c = 0; c < 3; ++c) s[c] += a.px[((size_t)yy * a.w + xx) * 3 + c]; ++n; }
                for (int c = 0; c < 3; ++c) o.px[((size_t)y * w + x) * 3 + c] = (uint8_t)(s[c] / std::max(1u, n));
                continue;
            }
            const float sx = std::clamp((x + 0.5f) * kx - 0.5f, 0.0f, a.w - 1.0f), sy = std::clamp((y + 0.5f) * ky - 0.5f, 0.0f, a.h - 1.0f);
            const int x0 = (int)sx, y0 = (int)sy, x1 = std::min(x0 + 1, a.w - 1), y1 = std::min(y0 + 1, a.h - 1);
            const float fx = sx - x0, fy = sy - y0;
            for (int c = 0; c < 3; ++c) {
                const auto p = [&](int xx, int yy) { return (float)a.px[((size_t)yy * a.w + xx) * 3 + c]; };
                o.px[((size_t)y * w + x) * 3 + c] = (uint8_t)(p(x0, y0) * (1 - fx) * (1 - fy) + p(x1, y0) * fx * (1 - fy) + p(x0, y1) * (1 - fx) * fy + p(x1, y1) * fx * fy + 0.5f);
            }
        }
    return o;
}

// bilinear at u, v (0..1), wrapping left/right
static void sampleUV(const Image& a, float u, float v, uint8_t* out) {
    const float x = u * a.w - 0.5f, y = std::clamp(v * a.h - 0.5f, 0.0f, a.h - 1.0f);
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
    const float fx = x - x0, fy = y - y0;
    x0 = ((x0 % a.w) + a.w) % a.w;
    const int x1 = (x0 + 1) % a.w, y1 = std::min(y0 + 1, a.h - 1);
    for (int c = 0; c < 3; ++c) {
        const auto p = [&](int xx, int yy) { return (float)a.px[((size_t)yy * a.w + xx) * 3 + c]; };
        out[c] = (uint8_t)std::clamp((p(x0, y0) * (1 - fx) + p(x1, y0) * fx) * (1 - fy) + (p(x0, y1) * (1 - fx) + p(x1, y1) * fx) * fy + 0.5f, 0.0f, 255.0f);
    }
}

Image sample(const Image& a, int n, int face) {
    // the view direction through each pixel of a cube face (OpenGL's cube map layout)
    Image o; o.w = o.h = n; o.px.resize((size_t)n * n * 3);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const float s = (i + 0.5f) / n * 2 - 1, t = (j + 0.5f) / n * 2 - 1;
            float x, y, z;
            switch (face) {
            case 0: x = 1; y = -t; z = -s; break;      // px
            case 1: x = -1; y = -t; z = s; break;      // nx
            case 2: x = s; y = 1; z = t; break;        // py
            case 3: x = s; y = -1; z = -t; break;      // ny
            case 4: x = s; y = -t; z = 1; break;       // pz
            default: x = -s; y = -t; z = -1; break;    // nz
            }
            const float len = std::sqrt(x * x + y * y + z * z);
            const float lon = std::atan2(x, -z), lat = std::asin(std::clamp(y / len, -1.0f, 1.0f));
            sampleUV(a, 0.5f + lon / (2 * PI), 0.5f - lat / PI, &o.px[((size_t)j * n + i) * 3]);
        }
    return o;
}

std::map<std::string, Image> cubeFaces(const Image& a, int size) {
    static const char* names[] = {"px", "nx", "py", "ny", "pz", "nz"};
    std::map<std::string, Image> f;
    for (int k = 0; k < 6; ++k) f[names[k]] = sample(a, size, k);
    return f;
}

Image cross(const std::map<std::string, Image>& f) {
    const int n = f.at("px").w;
    Image o; o.w = 4 * n; o.h = 3 * n; o.px.assign((size_t)o.w * o.h * 3, 0);
    const std::pair<const char*, std::pair<int, int>> at[] = {{"py", {1, 0}}, {"nx", {0, 1}}, {"pz", {1, 1}}, {"px", {2, 1}}, {"nz", {3, 1}}, {"ny", {1, 2}}};
    for (const auto& [name, cr] : at) {
        const Image& im = f.at(name);
        for (int y = 0; y < n; ++y) memcpy(&o.px[((size_t)(cr.second * n + y) * o.w + cr.first * n) * 3], &im.px[(size_t)y * n * 3], (size_t)n * 3);
    }
    return o;
}

static void toVector(void* ctx, void* data, int size) { static_cast<std::string*>(ctx)->append((const char*)data, size); }

std::string pngBytes(const Image& a) {
    std::string s;
    stbi_write_png_to_func(toVector, &s, a.w, a.h, 3, a.px.data(), a.w * 3);
    return s;
}

static bool writeAll(const std::string& path, const std::string& bytes) {
    FILE* f = openU8(path, "wb");
    if (!f) return false;
    const bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    return ok;
}

bool savePng(const Image& a, const std::string& path) { return writeAll(path, pngBytes(a)); }

bool saveJpg(const Image& a, const std::string& path, int quality) {
    std::string s;
    stbi_write_jpg_to_func(toVector, &s, a.w, a.h, 3, a.px.data(), quality);
    return writeAll(path, s);
}

bool load(const std::string& path, Image& a) {
    FILE* f = openU8(path, "rb");
    if (!f) return false;
    int w, h, n;
    stbi_set_flip_vertically_on_load(0);
    uint8_t* px = stbi_load_from_file(f, &w, &h, &n, 3);
    fclose(f);
    if (!px) return false;
    a.w = w; a.h = h; a.px.assign(px, px + (size_t)w * h * 3);
    stbi_image_free(px);
    return true;
}

// ---- a stored zip: local headers, the data, the central directory
static uint32_t crc32(const std::string& d) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) { for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; table[i] = c; } made = true; }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : d) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool saveZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, dir;
    auto u16 = [](std::string& s, uint16_t v) { s.push_back((char)(v & 255)); s.push_back((char)(v >> 8)); };
    auto u32 = [](std::string& s, uint32_t v) { for (int i = 0; i < 4; ++i) s.push_back((char)((v >> (8 * i)) & 255)); };
    for (const auto& [name, data] : files) {
        const uint32_t crc = crc32(data), off = (uint32_t)out.size(), n = (uint32_t)data.size();
        u32(out, 0x04034b50); u16(out, 20); u16(out, 0); u16(out, 0); u16(out, 0); u16(out, 0x21);
        u32(out, crc); u32(out, n); u32(out, n); u16(out, (uint16_t)name.size()); u16(out, 0);
        out += name; out += data;
        u32(dir, 0x02014b50); u16(dir, 20); u16(dir, 20); u16(dir, 0); u16(dir, 0); u16(dir, 0); u16(dir, 0x21);
        u32(dir, crc); u32(dir, n); u32(dir, n); u16(dir, (uint16_t)name.size()); u16(dir, 0); u16(dir, 0); u16(dir, 0); u16(dir, 0);
        u32(dir, 0); u32(dir, off); dir += name;
    }
    const uint32_t dirOff = (uint32_t)out.size();
    out += dir;
    u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, (uint16_t)files.size()); u16(out, (uint16_t)files.size());
    u32(out, (uint32_t)dir.size()); u32(out, dirOff); u16(out, 0);
    return writeAll(path, out);
}

}
