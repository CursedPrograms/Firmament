// Firmament - 360° skyboxes from a prompt, made on your own graphics card, in C++. Part of Atelier.
// Describe a sky and the world under it: Krea 2 Turbo with mickmumpitz's 360° LoRA paints the whole sphere round you
// as an equirectangular panorama (its left and right edges joined as it's made). Look round it, repair the seam,
// upscale it 2x or 4x, and export the panorama, the six cube faces or a cross. Every setting is a slider.
//
//   Firmament [panorama.png]
//   Firmament --capture out.bmp [--make "scene"] [--width N] [--steps N]   a screenshot, then quit (after the skybox)
//   Firmament --capture out.bmp --try-tools        repair the newest sky's seam, upscale it 2x, export its cube faces
#include "engine.h"
#include "http.h"
#include "pano.h"
#include "splash.h"
#include "viewer.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <stb_image_write.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace fs = std::filesystem;

#ifndef FIRMAMENT_VERSION
#define FIRMAMENT_VERSION "2.0.0"
#endif

// ---------- the Atelier look ----------
static const ImU32 C_BG = IM_COL32(0x1a, 0x1a, 0x1a, 255), C_PANEL = IM_COL32(0x24, 0x24, 0x24, 255),
                   C_LINE = IM_COL32(0x3a, 0x3a, 0x3a, 255), C_TEXT = IM_COL32(0xe6, 0xe6, 0xe6, 255),
                   C_DIM = IM_COL32(0x9a, 0x9a, 0x9a, 255), C_PINK = IM_COL32(0x00, 0x7b, 0xb7, 255),
                   C_DEEP = IM_COL32(0x00, 0x51, 0x84, 255), C_WELL = IM_COL32(0x14, 0x14, 0x14, 255);
static ImVec4 col4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

static void applyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.FrameRounding = 2; s.GrabRounding = 2; s.PopupRounding = 2; s.ScrollbarRounding = 2; s.TabRounding = 2;
    s.WindowBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1;
    s.WindowPadding = ImVec2(14, 12); s.FramePadding = ImVec2(8, 5); s.ItemSpacing = ImVec2(7, 8);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = col4(C_TEXT); c[ImGuiCol_TextDisabled] = col4(C_DIM);
    c[ImGuiCol_WindowBg] = col4(C_PANEL); c[ImGuiCol_ChildBg] = col4(C_PANEL); c[ImGuiCol_PopupBg] = col4(IM_COL32(0x21, 0x21, 0x21, 255));
    c[ImGuiCol_Border] = col4(C_LINE); c[ImGuiCol_Separator] = col4(C_LINE);
    c[ImGuiCol_FrameBg] = col4(C_WELL); c[ImGuiCol_FrameBgHovered] = col4(IM_COL32(0x2e, 0x2e, 0x2e, 255)); c[ImGuiCol_FrameBgActive] = col4(IM_COL32(0x33, 0x33, 0x33, 255));
    c[ImGuiCol_MenuBarBg] = col4(C_BG);
    c[ImGuiCol_CheckMark] = col4(C_PINK); c[ImGuiCol_SliderGrab] = col4(C_PINK); c[ImGuiCol_SliderGrabActive] = col4(C_DEEP);
    c[ImGuiCol_Button] = col4(IM_COL32(0x33, 0x33, 0x33, 255)); c[ImGuiCol_ButtonHovered] = col4(C_DEEP); c[ImGuiCol_ButtonActive] = col4(C_PINK);
    c[ImGuiCol_Header] = col4(IM_COL32(0x33, 0x33, 0x33, 255)); c[ImGuiCol_HeaderHovered] = col4(C_DEEP); c[ImGuiCol_HeaderActive] = col4(C_PINK);
    c[ImGuiCol_Tab] = col4(C_PANEL); c[ImGuiCol_TabActive] = col4(C_DEEP); c[ImGuiCol_TabHovered] = col4(C_PINK);
    c[ImGuiCol_TabUnfocusedActive] = col4(C_DEEP); c[ImGuiCol_TabUnfocused] = col4(C_PANEL);
    c[ImGuiCol_PlotHistogram] = col4(C_PINK);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
}

static bool fileExists(const char* f) { if (FILE* t = fopen(f, "rb")) { fclose(t); return true; } return false; }
static ImFont* bigFont = nullptr;

static void loadFont() {
    ImGuiIO& io = ImGui::GetIO();
    const char* f = "C:/Windows/Fonts/segoeui.ttf";
    if (!fileExists(f)) { io.Fonts->AddFontDefault(); return; }
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x2013, 0x2026, 0x2190, 0x21FF, 0x2600, 0x27BF, 0};
    auto add = [&](const char* file, float size) { return io.Fonts->AddFontFromFileTTF(file, size, nullptr, ranges); };
    add(f, 17.0f);
    bigFont = add(fileExists("C:/Windows/Fonts/seguisb.ttf") ? "C:/Windows/Fonts/seguisb.ttf" : f, 21.0f);
    const char* light = fileExists("C:/Windows/Fonts/segoeuil.ttf") ? "C:/Windows/Fonts/segoeuil.ttf" : f;
    Splash::setFonts(add(light, 50.0f), add(f, 13.0f));
}

// ---------- the prompt: the LoRA's template (mickmumpitz/Krea2-360-ERP-LoRAs), the style in place of "photoreal" ----------
static const char* STYLE_NAMES[] = {"Photo", "Matte painting", "Game art", "Anime"};
static const char* STYLE_WORDS[] = {"photoreal", "painterly digital matte painting", "stylized video game environment art", "anime background painting"};
static std::string promptFor(const std::string& scene0, int style) {
    std::string scene = scene0;
    while (!scene.empty() && (scene.back() == '.' || scene.back() == ',' || scene.back() == ' ' || scene.back() == '\n')) scene.pop_back();
    while (!scene.empty() && (scene.front() == ' ' || scene.front() == '\n')) scene.erase(scene.begin());
    return "img-txt-2-360 A full 360 degree equirectangular panorama in 2:1 spherical projection, " + scene + ", " + STYLE_WORDS[std::clamp(style, 0, 3)] + ", seamless wrap, level horizon.";
}

struct Preset { const char* name; const char* scene; };
static const Preset PRESETS[] = {
    {"Space", "deep space with a violet nebula, a ringed gas giant and a dense field of stars"},
    {"Ocean sunset", "a calm tropical ocean at sunset, glowing orange clouds, gentle waves to the horizon"},
    {"Mountains", "snowy alpine peaks above a sea of clouds at sunrise, clear blue sky"},
    {"Alien desert", "a red alien desert at dusk with two moons over jagged mesas"},
    {"Enchanted forest", "a misty enchanted forest clearing with giant ancient trees and shafts of light"},
    {"Neon city", "a neon city rooftop at night in the rain, towers and flying lights all around"},
    {"Storm", "a dark stormy sky over open grassland, lightning on the horizon"},
    {"Underwater", "deep underwater on a coral reef, sunbeams from the surface, schools of fish"},
};

// ---------- where skyboxes go: Pictures\Firmament, unless Windows guards Pictures, then the user's own folder ----------
static bool canWrite(const fs::path& d) {
    std::error_code ec;
    fs::create_directories(d, ec);
    const fs::path t = d / ".firmament-write-test";
#ifdef _WIN32
    FILE* f = _wfopen(t.wstring().c_str(), L"wb");
#else
    FILE* f = fopen(t.c_str(), "wb");
#endif
    if (!f) return false;
    fclose(f); fs::remove(t, ec);
    return true;
}
static std::string outputDir() {
    static std::string dir;
    if (!dir.empty()) return dir;
#ifdef _WIN32
    if (const wchar_t* e = _wgetenv(L"FIRMAMENT_OUTPUT")) if (*e && canWrite(fs::path(e))) return dir = fs::path(e).u8string();
    for (const KNOWNFOLDERID& id : {FOLDERID_Pictures, FOLDERID_Profile}) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) {
            const fs::path d = fs::path(p) / "Firmament"; CoTaskMemFree(p);
            if (canWrite(d)) return dir = d.u8string();
        }
    }
#endif
    return dir = "output";
}

#ifdef _WIN32
static std::string fileDialog(bool save, const wchar_t* filter, const wchar_t* ext, const std::string& start) {
    std::vector<wchar_t> buf(4096, 0);
    const std::wstring s = fs::u8path(start).wstring();
    wcsncpy(buf.data(), s.c_str(), buf.size() - 1);
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o; o.lpstrFilter = filter; o.lpstrFile = buf.data(); o.nMaxFile = (DWORD)buf.size(); o.lpstrDefExt = ext;
    o.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR) : (OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR);
    if (!(save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o))) return "";
    return fs::path(buf.data()).u8string();
}
static void openInExplorer(const std::string& p) { ShellExecuteW(nullptr, L"open", fs::u8path(p).wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
#else
static std::string fileDialog(bool, const wchar_t*, const wchar_t*, const std::string&) { return ""; }
static void openInExplorer(const std::string&) {}
#endif

// ---------- state ----------
static SDL_Window* window = nullptr;
static bool quit = false;
static Engine::Models models;
static Engine::Settings settings;
static Engine::Progress progress;
static char scene[1024] = "";
static int style = 0;
static int64_t lockedSeed = -1;                 // -1: a new one each time
static bool lockSeed = false;
static float seamStrength = 0.55f; static int seamBand = 192;
static int cubeSize = 0;                         // 0: a quarter of the panorama's width
static int upscaleModel = 0;                     // 0 photo, 1 anime

struct Skybox {
    std::string path, prompt, scene; int style = 0; int64_t seed = 0; int steps = 0; float seam = 0;
    std::string from;                            // the sky it was made from (seam repair, upscale): the wipe's before
    Image img;                                   // loaded when shown
    GLuint thumb = 0; Image thumbImg;
};
static std::vector<Skybox> gallery;
static int current = -1;

static std::mutex mtx;
static std::vector<std::function<void()>> onMain;
static std::deque<std::string> logLines;
static void post(std::function<void()> f) { std::lock_guard<std::mutex> g(mtx); onMain.push_back(std::move(f)); }
static std::string outputDir();
// the log is kept in firmament.log too (in the output folder), so a failed picture can be looked into after closing
static FILE* logFile = nullptr;
static void logLine(const std::string& s) {
    std::lock_guard<std::mutex> g(mtx); logLines.push_back(s); if (logLines.size() > 2000) logLines.pop_front();
    if (!logFile) logFile = _wfopen((fs::u8path(outputDir()) / "firmament.log").wstring().c_str(), L"ab");
    if (logFile) { fprintf(logFile, "%s\n", s.c_str()); fflush(logFile); }
}
static std::string toast; static double toastUntil = 0;
static double now() { return SDL_GetTicks() / 1000.0; }
static void say(const std::string& s) { std::lock_guard<std::mutex> g(mtx); toast = s; toastUntil = now() + 7; }
static std::thread job; static std::atomic<bool> busy{false};
static std::chrono::steady_clock::time_point jobStart;

static void runJob(std::function<void()> f) {
    if (busy) return;
    if (job.joinable()) job.join();
    busy = true; jobStart = std::chrono::steady_clock::now();
    job = std::thread([f] { f(); post([] { busy = false; }); });   // not busy once what the job posted is in
}

static std::string slug(const std::string& s) {
    std::string r;
    for (char c : s) { if (isalnum((unsigned char)c)) r += (char)tolower((unsigned char)c); else if (!r.empty() && r.back() != '-') r += '-'; if (r.size() >= 40) break; }
    while (!r.empty() && r.back() == '-') r.pop_back();
    return r.empty() ? "skybox" : r;
}

// every skybox: its PNG and a little text file beside it with how it was made
static void saveMeta(const Skybox& s) {
    std::ofstream o(fs::u8path(s.path + ".txt"));
    o << "scene " << s.scene << "\nstyle " << s.style << "\nseed " << s.seed << "\nsteps " << s.steps << "\nseam " << s.seam << "\nprompt " << s.prompt << "\n";
    if (!s.from.empty()) o << "from " << s.from << "\n";
}

static Image thumbOf(const Image& a) { return Pano::resize(a, 320, 160); }

static void loadGallery() {
    gallery.clear();
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(fs::u8path(outputDir()), ec))
        if (e.path().extension() == ".png" && e.path().stem().u8string().find("_face_") == std::string::npos && e.path().stem().u8string().find("_cross") == std::string::npos) files.push_back(e.path());
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { std::error_code e; return fs::last_write_time(a, e) > fs::last_write_time(b, e); });
    for (const auto& f : files) {
        Skybox s; s.path = f.u8string();
        std::ifstream in(fs::u8path(s.path + ".txt"));
        std::string line;
        while (std::getline(in, line)) {
            const size_t sp = line.find(' ');
            if (sp == std::string::npos) continue;
            const std::string k = line.substr(0, sp), v = line.substr(sp + 1);
            if (k == "scene") s.scene = v; else if (k == "prompt") s.prompt = v; else if (k == "seed") s.seed = atoll(v.c_str());
            else if (k == "style") s.style = atoi(v.c_str()); else if (k == "steps") s.steps = atoi(v.c_str()); else if (k == "seam") s.seam = (float)atof(v.c_str());
            else if (k == "from") s.from = v;
        }
        gallery.push_back(std::move(s));
    }
    // thumbnails in the background
    std::vector<std::string> paths; for (auto& g : gallery) paths.push_back(g.path);
    std::thread([paths] {
        for (const auto& p : paths) {
            Image im;
            if (!Pano::load(p, im)) continue;
            Image t = thumbOf(im);
            post([p, t = std::move(t)]() mutable { for (auto& g : gallery) if (g.path == p && !g.thumb) g.thumbImg = std::move(t); });
        }
    }).detach();
}

static void show(int i) {
    if (i < 0 || i >= (int)gallery.size()) return;
    current = i;
    Skybox& s = gallery[i];
    if (s.img.empty() && !Pano::load(s.path, s.img)) { say("Couldn't open " + s.path); return; }
    Viewer::setImage(&s.img);
    static Image beforeImg;   // the sky it was made from, for the wipe
    beforeImg = Image();
    std::error_code ec;
    if (!s.from.empty() && fs::exists(fs::u8path(s.from), ec)) {
        for (const auto& g : gallery) if (g.path == s.from && !g.img.empty()) { beforeImg = g.img; break; }
        if (beforeImg.empty()) Pano::load(s.from, beforeImg);
    }
    Viewer::setBefore(beforeImg.empty() ? nullptr : &beforeImg);
    SDL_SetWindowTitle(window, ("Firmament - " + fs::u8path(s.path).filename().u8string()).c_str());
}

// a finished picture becomes a skybox in the gallery (and on disk)
static void addSkybox(Image img, const std::string& scn, int sty, int64_t seed, int steps, const std::string& prompt, const std::string& suffix, const std::string& from = "") {
    Skybox s; s.from = from;
    s.scene = scn; s.style = sty; s.seed = seed; s.steps = steps; s.prompt = prompt;
    s.seam = Pano::seamError(img);
    char stamp[32]; const time_t t = time(nullptr); tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &lt);
    s.path = (fs::u8path(outputDir()) / (slug(scn) + "_" + std::to_string(seed) + "_" + stamp + suffix + ".png")).u8string();
    if (!Pano::savePng(img, s.path)) say("Couldn't save " + s.path);
    saveMeta(s);
    s.thumbImg = thumbOf(img);
    s.img = std::move(img);
    gallery.insert(gallery.begin(), std::move(s));
    show(0);
}

static void makeSkybox() {
    if (busy) return;
    if (!scene[0]) { say("Describe the sky and the world under it first (or pick a ready-made scene)"); return; }
    std::string why;
    if (!Engine::available(why)) { say(why); return; }
    const auto missing = models.missing();
    if (!missing.empty()) { say("Some models are missing: get them in the Models tab"); return; }
    Engine::Settings s = settings;
    s.prompt = promptFor(scene, style);
    s.seed = lockSeed ? lockedSeed : -1;
    const std::string scn = scene; const int sty = style;
    { std::lock_guard<std::mutex> g(mtx); logLines.clear(); }
    logLine("---- " + s.prompt + " (" + std::to_string(s.width) + " wide, " + std::to_string(s.steps) + " steps" + (s.offload ? ", offload" : "") + ")");
    runJob([s, scn, sty] {
        Image out; int64_t seed = 0;
        const std::string err = Engine::generate(models, s, out, seed, progress);
        if (!err.empty()) { logLine("FAILED: " + err); say(err); return; }
        post([out = std::move(out), scn, sty, seed, s]() mutable {
            if (lockSeed) lockedSeed = seed;
            addSkybox(std::move(out), scn, sty, seed, s.steps, s.prompt, "");
            say("A new sky. Drag to look round.");
        });
    });
}

static void fixSeam() {
    if (busy || current < 0) return;
    Skybox& c = gallery[current];
    Image strip, mask; int x0 = 0;
    Pano::seamStrip(c.img, seamBand, strip, mask, x0);
    Engine::Settings s = settings;
    s.prompt = c.prompt.empty() ? promptFor(c.scene, c.style) : c.prompt;
    s.wrap = false;   // a strip, not the whole round
    const Image base = c.img; const std::string src = c.path; const std::string scn = c.scene; const int sty = c.style; const int64_t seed = c.seed; const float str = seamStrength; const int band = seamBand;
    runJob([s, strip, mask, x0, base, src, scn, sty, seed, str, band] {
        Image rep;
        const std::string err = Engine::repaint(models, s, strip, mask, str, rep, progress);
        if (!err.empty()) { say(err); return; }
        Image merged = Pano::seamMerge(base, rep, x0, band, Pano::SEAM_FEATHER);
        post([merged = std::move(merged), src, scn, sty, seed, s]() mutable {
            const float before = gallery.empty() ? 0 : gallery[current].seam;
            addSkybox(std::move(merged), scn, sty, seed, s.steps, s.prompt, "_seam", src);
            char m[128]; snprintf(m, sizeof m, "Seam repaired: %.1f before, %.1f now", before, gallery[0].seam);
            say(m);
        });
    });
}

static void upscaleCurrent(int scale) {
    if (busy || current < 0) return;
    const Skybox& c = gallery[current];
    const std::string model = upscaleModel ? models.upscalerAnime : models.upscaler;
    std::error_code ec;
    if (!fs::exists(fs::u8path(model), ec)) { say("The upscaler model is missing: get it in the Models tab"); return; }
    const Image padded = Pano::wrapPad(c.img, 64);   // the other edge on each side, so the upscale wraps without a seam
    const std::string scn = c.scene, prompt = c.prompt, src = c.path; const int sty = c.style, steps = c.steps; const int64_t seed = c.seed;
    runJob([model, padded, scale, src, scn, prompt, sty, steps, seed] {
        Image big;
        const std::string err = Engine::upscale(model, padded, scale, big, progress);
        if (!err.empty()) { say(err); return; }
        Image out = Pano::unpad(big, 64 * scale);
        post([out = std::move(out), src, scn, prompt, sty, steps, seed, scale]() mutable {
            addSkybox(std::move(out), scn, sty, seed, steps, prompt, "_x" + std::to_string(scale), src);
            say("Upscaled " + std::to_string(scale) + "x");
        });
    });
}

// ---------- model downloads (the Models tab) ----------
struct Download { const char* label; const char* sub; const char* file; const char* url; int mb; };
static const Download DOWNLOADS[] = {
    {"Krea 2 Turbo (the painter)", "krea2/diffusion_models", "Krea-2-Turbo-Q4_K_M.gguf", "https://huggingface.co/realrebelai/KREA-2_GGUFs/resolve/main/TURBO/Krea-2-Turbo-Q4_K_M.gguf", 6883},
    {"Qwen3-VL 4B (reads your words)", "krea2/text_encoders", "qwen3vl_4b_fp8_scaled.safetensors", "https://huggingface.co/Comfy-Org/Krea-2/resolve/main/text_encoders/qwen3vl_4b_fp8_scaled.safetensors", 5000},
    {"Wan 2.1 VAE (turns it into pixels)", "krea2/vae", "wan_2.1_vae.safetensors", "https://huggingface.co/Comfy-Org/Wan_2.1_ComfyUI_repackaged/resolve/main/split_files/vae/wan_2.1_vae.safetensors", 242},
    {"360\xc2\xb0 panorama LoRA (mickmumpitz)", "krea2/loras", "krea2_t2i_360_erp_lora_v1.safetensors", "https://huggingface.co/mickmumpitz/Krea2-360-ERP-LoRAs/resolve/main/krea2_t2i_360_erp_lora_v1.safetensors", 218},
    {"Real-ESRGAN x4 (upscale photos)", "upscale", "RealESRGAN_x4plus.pth", "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.1.0/RealESRGAN_x4plus.pth", 64},
    {"Real-ESRGAN x4 anime (upscale art)", "upscale", "RealESRGAN_x4plus_anime_6B.pth", "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.2.4/RealESRGAN_x4plus_anime_6B.pth", 17},
};
static std::atomic<float> dlFrac[6];
static std::atomic<bool> dlStop{false};
static std::string modelsRoot() {
    std::error_code ec;
    if (fs::exists("F:/AIModels", ec)) return "F:/AIModels";
    char* bp = SDL_GetBasePath(); std::string r = bp ? std::string(bp) + "models" : "models"; if (bp) SDL_free(bp);
    return r;
}
static bool haveDownload(const Download& d) {
    const std::string have[] = {models.diffusion, models.llm, models.vae, models.lora, models.upscaler, models.upscalerAnime};
    std::error_code ec;
    for (const auto& h : have) if (fs::u8path(h).filename().u8string() == d.file && fs::exists(fs::u8path(h), ec)) return true;
    // another name for the same model counts too
    const int i = (int)(&d - DOWNLOADS);
    return i < 6 && fs::exists(fs::u8path(have[i]), ec);
}
static void fetchMissing() {
    if (busy) return;
    std::vector<int> want;
    for (int i = 0; i < 6; ++i) if (!haveDownload(DOWNLOADS[i])) want.push_back(i);
    if (want.empty()) { say("Every model is here"); return; }
    runJob([want] {
        for (int i : want) {
            const Download& d = DOWNLOADS[i];
            dlFrac[i] = 0;
            std::string err;
            const std::string dest = (fs::u8path(modelsRoot()) / d.sub / d.file).u8string();
            const bool ok = Http::download(d.url, dest, [i](int64_t a, int64_t t) { dlFrac[i] = t > 0 ? (float)a / t : 0.0f; }, &dlStop, err);
            dlFrac[i] = ok ? 1.0f : -1.0f;
            if (!ok) { say(std::string(d.file) + ": " + err); return; }
        }
        post([] { models = Engine::findModels(); say("The models are here"); });
    });
}

// ---------- the window ----------
static bool showAbout = false;
// File > Preferences, kept in %APPDATA%\Firmament\preferences.txt ("key value" lines)
static struct Prefs { bool reopenLast = false; } prefs;   // reopenLast: the newest sky shown at the start (else an empty view)
static bool showPrefs = false;
static fs::path prefsFile() {
    const char* ad = getenv("APPDATA");
    const fs::path d = fs::u8path(ad ? ad : ".") / "Firmament";
    std::error_code ec; fs::create_directories(d, ec);
    return d / "preferences.txt";
}
static void loadPrefs() { std::ifstream f(prefsFile()); std::string k; int v; while (f >> k >> v) if (k == "reopenLast") prefs.reopenLast = v != 0; }
static void savePrefs() { std::ofstream(prefsFile()) << "reopenLast " << (prefs.reopenLast ? 1 : 0) << "\n"; }
static int tab = 0, forcedTab = -1;

static void heading(const char* s) { ImGui::Spacing(); ImGui::PushStyleColor(ImGuiCol_Text, col4(C_PINK)); ImGui::TextUnformatted(s); ImGui::PopStyleColor(); }
static void tip(const char* s) { if (ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::PushTextWrapPos(380); ImGui::TextUnformatted(s); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); } }

static void leftPanel(float w) {
    heading("THE SKY");
    ImGui::TextDisabled("Describe the sky and the world around you");
    ImGui::InputTextMultiline("##scene", scene, sizeof scene, ImVec2(w, 96));
    for (int i = 0; i < 8; ++i) {
        const float bw = (w - 7) / 2;
        if (i % 2) ImGui::SameLine();
        if (ImGui::Button(PRESETS[i].name, ImVec2(bw, 0))) snprintf(scene, sizeof scene, "%s", PRESETS[i].scene);
        tip(PRESETS[i].scene);
    }
    heading("STYLE");
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine(0, 4);
        const bool on = style == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, col4(C_DEEP));
        if (ImGui::Button(STYLE_NAMES[i], ImVec2((w - 12) / 4, 0))) style = i;
        if (on) ImGui::PopStyleColor();
    }
    heading("SIZE AND DETAIL");
    ImGui::PushItemWidth(w);
    int wsel = settings.width / 64;
    char wfmt[96];
    snprintf(wfmt, sizeof wfmt, "%d x %d%s", settings.width, settings.width / 2, settings.width <= 1024 ? " (quick draft)" : settings.width >= 3072 ? " (slow on 6 GB cards)" : "");
    if (ImGui::SliderInt("##w", &wsel, 16, 64, wfmt)) settings.width = wsel * 64;
    tip("The panorama's size. 2048 x 1024 is the usual; upscale afterwards for 4K and 8K.");
    ImGui::SliderInt("##steps", &settings.steps, 4, 40, "steps: %d"); tip("More steps: more detail, slower (Krea 2 Turbo is made for about 10)");
    ImGui::Checkbox("Same seed every time", &lockSeed); tip("Keep the seed to try small changes to the words");
    if (lockSeed) { ImGui::SameLine(); ImGui::SetNextItemWidth(w - ImGui::GetCursorPosX() + 14); ImGui::InputScalar("##seed", ImGuiDataType_S64, &lockedSeed); }
    if (ImGui::CollapsingHeader("More settings")) {
        ImGui::SliderFloat("##cfg", &settings.cfg, 1.0f, 8.0f, "guidance (CFG): %.1f"); tip("Turbo models want 1; higher follows the words harder but needs more steps");
        ImGui::SliderFloat("##lora", &settings.loraStrength, 0.0f, 1.5f, "360\xc2\xb0 LoRA: %.2f"); tip("How strongly it keeps to the 360\xc2\xb0 panorama shape");
        ImGui::SliderFloat("##shift", &settings.flowShift, 0.0f, 10.0f, settings.flowShift > 0 ? "shift: %.1f" : "shift: the model's own"); tip("Where the steps spend their effort: higher favours the big shapes");
        const auto& sm = Engine::samplers(); const auto& sc = Engine::schedulers();
        if (ImGui::BeginCombo("##sampler", ("sampler: " + sm[settings.sampler]).c_str())) { for (int i = 0; i < (int)sm.size(); ++i) if (ImGui::Selectable(sm[i].c_str(), settings.sampler == i)) settings.sampler = i; ImGui::EndCombo(); }
        if (ImGui::BeginCombo("##sched", ("schedule: " + sc[settings.scheduler]).c_str())) { for (int i = 0; i < (int)sc.size(); ++i) if (ImGui::Selectable(sc[i].c_str(), settings.scheduler == i)) settings.scheduler = i; ImGui::EndCombo(); }
        ImGui::Checkbox("Join the left and right edges", &settings.wrap); tip("Paints the panorama as a ring, so it has no seam at the back");
        ImGui::Checkbox("Keep the model in system memory", &settings.offload); tip("For 6-8 GB graphics cards. Off is faster on cards with 16 GB or more.");
    }
    ImGui::PopItemWidth();
    ImGui::Spacing();
    if (!busy) {
        ImGui::PushStyleColor(ImGuiCol_Button, col4(C_PINK)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col4(C_DEEP));
        ImGui::PushFont(bigFont);
        if (ImGui::Button("Make the sky", ImVec2(w, 46))) makeSkybox();
        ImGui::PopFont(); ImGui::PopStyleColor(2);
    } else {
        const int st = progress.step, n = std::max(1, (int)progress.steps);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - jobStart).count();
        char ov[96]; snprintf(ov, sizeof ov, "%s  %d/%d  %d:%02d", st > 0 ? "" : "", st, n, (int)secs / 60, (int)secs % 60);
        ImGui::ProgressBar(st > 0 ? (float)st / n : 0.0f, ImVec2(w, 26), ov);
        ImGui::TextUnformatted(progress.stage.c_str());
        if (ImGui::Button("Stop", ImVec2(w, 30))) Engine::cancel();
    }

    if (current >= 0 && current < (int)gallery.size()) {
        Skybox& c = gallery[current];
        heading("THIS SKY");
        ImGui::PushTextWrapPos(w + 14); ImGui::TextDisabled("%s", c.scene.c_str()); ImGui::PopTextWrapPos();
        ImGui::TextDisabled("%d x %d, seed %lld", c.img.w, c.img.h, (long long)c.seed);
        ImGui::SameLine();
        if (ImGui::SmallButton("Use")) { snprintf(scene, sizeof scene, "%s", c.scene.c_str()); style = c.style; lockSeed = true; lockedSeed = c.seed; }
        tip("Its words, style and seed back in the boxes above");
        const bool bad = c.seam > 12;
        ImGui::TextColored(col4(bad ? C_PINK : C_DIM), "Seam: %.1f %s", c.seam, bad ? "(worth repairing)" : "(clean)");
        ImGui::BeginDisabled(busy);
        ImGui::PushItemWidth(w);
        ImGui::SliderFloat("##seamstr", &seamStrength, 0.2f, 0.9f, "repaint strength: %.2f");
        ImGui::SliderInt("##seamband", &seamBand, 64, 512, "repaint band: %d px");
        ImGui::PopItemWidth();
        if (ImGui::Button("Repair the seam", ImVec2(w, 0))) fixSeam();
        tip("Turns the panorama half round, repaints a band across the join and blends it back in");
        heading("BIGGER");
        const char* um[] = {"photo", "art / anime"};
        ImGui::SetNextItemWidth(w); ImGui::Combo("##um", &upscaleModel, um, 2);
        if (ImGui::Button("2x", ImVec2((w - 7) / 2, 0))) upscaleCurrent(2);
        ImGui::SameLine();
        if (ImGui::Button("4x", ImVec2((w - 7) / 2, 0))) upscaleCurrent(4);
        ImGui::EndDisabled();
        heading("SAVE");
        ImGui::SetNextItemWidth(w);
        int cs = cubeSize ? cubeSize : c.img.w / 4;
        if (ImGui::SliderInt("##cube", &cs, 256, std::max(512, c.img.w / 2), "cube faces: %d px")) cubeSize = cs;
        const std::string base = fs::u8path(c.path).replace_extension().u8string();
        if (ImGui::Button("Panorama PNG...", ImVec2(w, 0))) {
            const std::string p = fileDialog(true, L"PNG\0*.png\0\0", L"png", base + ".png");
            if (!p.empty()) say(Pano::savePng(c.img, p) ? "Saved " + fs::u8path(p).filename().u8string() : "Couldn't save " + p);
        }
        tip("Unity, Unreal, Godot and Blender take the panorama directly");
        if (ImGui::Button("Cube faces (zip)...", ImVec2((w - 7) / 2, 0))) {
            const std::string p = fileDialog(true, L"Zip\0*.zip\0\0", L"zip", base + "_cube.zip");
            if (!p.empty()) {
                auto f = Pano::cubeFaces(c.img, cs);
                std::vector<std::pair<std::string, std::string>> files;
                for (const char* n : {"px", "nx", "py", "ny", "pz", "nz"}) files.push_back({std::string(n) + ".png", Pano::pngBytes(f[n])});
                say(Pano::saveZip(p, files) ? "Saved the six faces (OpenGL layout: px nx py ny pz nz)" : "Couldn't save " + p);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cross PNG...", ImVec2((w - 7) / 2, 0))) {
            const std::string p = fileDialog(true, L"PNG\0*.png\0\0", L"png", base + "_cross.png");
            if (!p.empty()) say(Pano::savePng(Pano::cross(Pano::cubeFaces(c.img, cs)), p) ? "Saved the cross" : "Couldn't save " + p);
        }
    }
}

static void viewTab(ImVec2 avail) {
    Viewer::Look& lk = Viewer::look();
    ImGui::Checkbox("Turn", &lk.turn); ImGui::SameLine();
    ImGui::Checkbox("Flat", &lk.flat); ImGui::SameLine();
    ImGui::SetNextItemWidth(160); ImGui::SliderFloat("##fov", &lk.fov, 20, 140, "field of view: %.0f\xc2\xb0"); ImGui::SameLine();
    if (ImGui::Button("Straight ahead")) { lk.yaw = 0; lk.pitch = 0; lk.fov = 90; }
    if (Viewer::comparing()) {   // a repaired or upscaled sky: the one it came from, as Mimic's wipe (keys 1 2 3)
        ImGui::SameLine(); ImGui::RadioButton("Before | after", &lk.compare, 0); tip("Drag the pink line: the sky it was made from on the left, this one on the right (1)");
        ImGui::SameLine(); ImGui::RadioButton("After", &lk.compare, 1); tip("This sky only (2)");
        ImGui::SameLine(); ImGui::RadioButton("Before", &lk.compare, 2); tip("The sky it was made from (3)");
        if (!ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_1)) lk.compare = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_2)) lk.compare = 1;
            if (ImGui::IsKeyPressed(ImGuiKey_3)) lk.compare = 2;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Picture...") && current >= 0) {
        std::vector<unsigned char> px; int w, h; Viewer::readPixels(px, w, h);
        const std::string p = fileDialog(true, L"PNG\0*.png\0\0", L"png", (fs::u8path(outputDir()) / "view.png").u8string());
        if (!p.empty()) { stbi_write_png(fs::u8path(p).string().c_str(), w, h, 4, px.data(), w * 4); say("Saved the view"); }
    }
    const ImVec2 a = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("view", size, ImGuiButtonFlags_MouseButtonLeft);
    const unsigned tex = Viewer::render((int)size.x, (int)size.y, ImGui::IsItemHovered() || ImGui::IsItemActive());
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)tex, a, ImVec2(a.x + size.x, a.y + size.y), ImVec2(0, 1), ImVec2(1, 0));
    if (current < 0) {
        const char* m = models.missing().empty() ? "Describe a sky on the left (or pick a scene) and press Make the sky." : "First, the models: open the Models tab and press Get the models.";
        const ImVec2 ts = ImGui::CalcTextSize(m);
        ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + (size.x - ts.x) / 2, a.y + size.y / 2), C_DIM, m);
    } else ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + 12, a.y + size.y - 26), C_DIM, Viewer::comparing() && lk.compare == 0 ? "Drag the pink line to compare, elsewhere to look round" : "Drag to look round, wheel to zoom");
    if (current >= 0 && Viewer::comparing()) {   // the labels, as in Mimic
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto label = [&](const char* t, float x) { const ImVec2 ts = ImGui::CalcTextSize(t); dl->AddRectFilled(ImVec2(x - 6, a.y + 10), ImVec2(x + ts.x + 6, a.y + 14 + ts.y), IM_COL32(20, 20, 20, 200), 4); dl->AddText(ImVec2(x, a.y + 12), IM_COL32(230, 230, 230, 255), t); };
        if (lk.compare == 0) { label("Before", a.x + 14); label("After", a.x + size.x - 14 - ImGui::CalcTextSize("After").x); }
        else label(lk.compare == 2 ? "Before" : "After", a.x + 14);
    }
    (void)avail;
}

static void galleryTab() {
    if (gallery.empty()) { ImGui::TextDisabled("Your skies land here, kept in %s", outputDir().c_str()); return; }
    ImGui::TextDisabled("%zu skies in %s", gallery.size(), outputDir().c_str()); ImGui::SameLine();
    if (ImGui::SmallButton("Open the folder")) openInExplorer(outputDir());
    const float S = 320, H = 160, pad = 10;
    const int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + pad) / (S + pad)));
    ImGui::BeginChild("g");
    for (size_t i = 0; i < gallery.size(); ++i) {
        Skybox& g = gallery[i];
        if (i % cols) ImGui::SameLine(0, pad);
        if (!g.thumb && !g.thumbImg.empty()) {
            glGenTextures(1, &g.thumb); glBindTexture(GL_TEXTURE_2D, g.thumb);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, g.thumbImg.w, g.thumbImg.h, 0, GL_RGB, GL_UNSIGNED_BYTE, g.thumbImg.px.data());
        }
        ImGui::PushID((int)i);
        ImGui::BeginGroup();
        const ImVec2 a = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("t", ImVec2(S, H))) { show((int)i); forcedTab = 0; }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, ImVec2(a.x + S, a.y + H), C_WELL);
        if (g.thumb) dl->AddImage((ImTextureID)(intptr_t)g.thumb, a, ImVec2(a.x + S, a.y + H));
        if ((int)i == current) dl->AddRect(a, ImVec2(a.x + S, a.y + H), C_PINK, 0, 0, 2);
        std::string name = g.scene.empty() ? fs::u8path(g.path).stem().u8string() : g.scene;
        if (name.size() > 44) name = name.substr(0, 42) + "...";
        ImGui::TextDisabled("%s", name.c_str());
        ImGui::EndGroup();
        if (ImGui::BeginPopupContextItem("ctx")) {
            if (ImGui::MenuItem("Show in the folder")) openInExplorer(fs::u8path(g.path).parent_path().u8string());
            if (ImGui::MenuItem("Delete")) {
                std::error_code ec; fs::remove(fs::u8path(g.path), ec); fs::remove(fs::u8path(g.path + ".txt"), ec);
                if (g.thumb) glDeleteTextures(1, &g.thumb);
                gallery.erase(gallery.begin() + i);
                if (current == (int)i) { current = -1; Viewer::setImage(nullptr); } else if (current > (int)i) --current;
                ImGui::EndPopup(); ImGui::PopID(); break;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

static void modelsTab() {
    heading("THE MODELS");
    ImGui::TextDisabled("Firmament runs these on your own graphics card. They download once (about 12 GB in all).");
    ImGui::TextDisabled("Kept in %s", modelsRoot().c_str());
    for (int i = 0; i < 6; ++i) {
        const Download& d = DOWNLOADS[i];
        const bool have = haveDownload(d);
        ImGui::TextColored(col4(have ? C_TEXT : C_DIM), "%s %s", have ? "\xe2\x9c\x93" : "\xc2\xb7", d.label);
        ImGui::SameLine(460); ImGui::TextDisabled("%s, %.1f GB", d.file, d.mb / 1024.0);
        const float f = dlFrac[i];
        if (busy && f > 0 && f < 1) { ImGui::SameLine(); ImGui::ProgressBar(f, ImVec2(160, 0)); }
    }
    ImGui::Spacing();
    bool all = true; for (int i = 0; i < 6; ++i) all &= haveDownload(DOWNLOADS[i]);
    ImGui::BeginDisabled(busy || all);
    if (ImGui::Button(all ? "Every model is here" : "Get the models", ImVec2(260, 36))) fetchMissing();
    { std::string why; if (!Engine::available(why)) { ImGui::PushTextWrapPos(0); ImGui::TextColored(col4(C_PINK), "%s", why.c_str()); ImGui::PopTextWrapPos(); } }
    ImGui::EndDisabled();
    if (busy) { ImGui::SameLine(); if (ImGui::Button("Stop", ImVec2(100, 36))) dlStop = true; }
    ImGui::TextDisabled("Downloads carry on where they stopped if the connection drops.");
}

static void frame() {
    { std::vector<std::function<void()>> todo; { std::lock_guard<std::mutex> g(mtx); todo.swap(onMain); } for (auto& f : todo) f(); }
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float top = 0;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open a panorama...")) {
                const std::string p = fileDialog(false, L"Panoramas\0*.png;*.jpg;*.jpeg\0\0", nullptr, "");
                Image im;
                if (!p.empty() && Pano::load(p, im)) { Skybox s; s.path = p; s.seam = Pano::seamError(im); s.thumbImg = thumbOf(im); s.img = std::move(im); gallery.insert(gallery.begin(), std::move(s)); show(0); }
            }
            if (ImGui::MenuItem("Open the skies folder")) openInExplorer(outputDir());
            if (ImGui::MenuItem("Open recent", nullptr, false, !gallery.empty() && current != 0)) { show(0); forcedTab = 0; }
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...")) showPrefs = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) quit = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) { if (ImGui::MenuItem("About Firmament")) showAbout = true; ImGui::EndMenu(); }
        top = ImGui::GetWindowSize().y;
        ImGui::EndMainMenuBar();
    }
    const float statusH = 26, leftW = 380;
    const ImGuiWindowFlags fixed = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowPos(ImVec2(0, top)); ImGui::SetNextWindowSize(ImVec2(leftW, H - top - statusH));
    ImGui::Begin("left", nullptr, fixed);
    leftPanel(leftW - 28);
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2(leftW, top)); ImGui::SetNextWindowSize(ImVec2(W - leftW, H - top - statusH));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(C_BG));
    ImGui::Begin("main", nullptr, fixed);
    if (ImGui::BeginTabBar("tabs")) {
        const char* names[] = {"360\xc2\xb0 view", "Gallery", "Models", "Log"};
        for (int k = 0; k < 4; ++k) if (ImGui::BeginTabItem(names[k], nullptr, forcedTab == k ? ImGuiTabItemFlags_SetSelected : 0)) { tab = k; ImGui::EndTabItem(); }
        forcedTab = -1;
        ImGui::EndTabBar();
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (tab == 0) viewTab(avail);
    else if (tab == 1) galleryTab();
    else if (tab == 2) modelsTab();
    else {
        ImGui::BeginChild("log", ImVec2(0, 0), true);
        { std::lock_guard<std::mutex> g(mtx); for (const auto& l : logLines) ImGui::TextUnformatted(l.c_str()); }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::SetNextWindowPos(ImVec2(0, H - statusH)); ImGui::SetNextWindowSize(ImVec2(W, statusH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(C_BG));
    ImGui::Begin("status", nullptr, fixed);
    { std::lock_guard<std::mutex> g(mtx); if (now() < toastUntil) ImGui::TextColored(col4(C_PINK), "%s", toast.c_str()); else ImGui::TextDisabled("Skies kept in %s", outputDir().c_str()); }
    ImGui::End();
    ImGui::PopStyleColor(); ImGui::PopStyleVar();
    if (showPrefs) {
        ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Preferences", &showPrefs, ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::Checkbox("Open the newest sky at the start", &prefs.reopenLast)) savePrefs();
            tip("Off: Firmament starts on an empty view; your skies are in the Gallery and File > Open recent");
        }
        ImGui::End();
    }
    if (showAbout) { ImGui::OpenPopup("about"); showAbout = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::BeginPopupModal("about", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        Splash::draw(ImGui::GetWindowDrawList(), at.x, at.y, nullptr, -1);
        ImGui::InvisibleButton("splash", ImVec2(Splash::W, Splash::H));
        if (ImGui::IsItemClicked() || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

int main(int argc, char** argv) {
    std::string capture, file, makeScene;
    int tools = 0;   // --try-tools: 1 seam, 2 upscale, 3 export, 4 done
    std::vector<std::string> args;
#ifdef _WIN32
    { int n; LPWSTR* wa = CommandLineToArgvW(GetCommandLineW(), &n); if (wa) { for (int i = 1; i < n; ++i) args.push_back(fs::path(wa[i]).u8string()); LocalFree(wa); } }
#else
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
#endif
    (void)argc; (void)argv;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--capture" && i + 1 < args.size()) capture = args[++i];
        else if (args[i] == "--make" && i + 1 < args.size()) makeScene = args[++i];
        else if (args[i] == "--width" && i + 1 < args.size()) settings.width = atoi(args[++i].c_str()) / 64 * 64;
        else if (args[i] == "--steps" && i + 1 < args.size()) settings.steps = atoi(args[++i].c_str());
        else if (args[i] == "--tab" && i + 1 < args.size()) forcedTab = atoi(args[++i].c_str());
        else if (args[i] == "--try-tools") tools = 1;
        else file = args[i];
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("Firmament", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1500, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    if (!window) return 1;
    SDL_SetWindowMinimumSize(window, 1100, 650);
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);
    char* bp = SDL_GetBasePath();
    SDL_Surface* icon = bp ? SDL_LoadBMP((std::string(bp) + "assets/icon.bmp").c_str()) : nullptr;
    if (bp) SDL_free(bp);
    if (icon) SDL_SetWindowIcon(window, icon);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    loadFont();
    applyStyle();
    SDL_Window* splashWin = nullptr;
    if (capture.empty()) {
        splashWin = SDL_CreateWindow("Firmament", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)Splash::W, (int)Splash::H, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
        if (splashWin && SDL_GL_MakeCurrent(splashWin, gl) != 0) { SDL_DestroyWindow(splashWin); splashWin = nullptr; SDL_GL_MakeCurrent(window, gl); }
        if (splashWin && icon) SDL_SetWindowIcon(splashWin, icon);
    }
    if (icon) SDL_FreeSurface(icon);
    ImGui_ImplSDL2_InitForOpenGL(splashWin ? splashWin : window, gl);
    ImGui_ImplOpenGL3_Init("#version 330");
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    Engine::setLog(logLine);

    static std::atomic<bool> ready{false};
    static std::string status = "Starting...";
    std::thread([] {
        status = "Looking for the models...";
        Engine::Models m = Engine::findModels();
        post([m] { models = m; });
        status = "Ready";
        ready = true;
    }).detach();
    if (splashWin) {
        const Uint32 start = SDL_GetTicks();
        bool skip = false;
        for (;;) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                ImGui_ImplSDL2_ProcessEvent(&e);
                if (e.type == SDL_QUIT) quit = true;
                if (e.type == SDL_MOUSEBUTTONDOWN || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) skip = true;
            }
            const Uint32 t = SDL_GetTicks() - start;
            if ((skip && ready) || quit || (ready && t >= 1500) || t >= 15000) break;
            ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
            Splash::draw(ImGui::GetBackgroundDrawList(), 0, 0, status.c_str(), ready ? 1.0f : std::min(0.9f, t / 1500.0f));
            ImGui::Render();
            int sw, sh; SDL_GL_GetDrawableSize(splashWin, &sw, &sh);
            glViewport(0, 0, sw, sh); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(splashWin);
        }
        ImGui_ImplSDL2_Shutdown();
        SDL_DestroyWindow(splashWin);
        SDL_GL_MakeCurrent(window, gl);
        ImGui_ImplSDL2_InitForOpenGL(window, gl);
    } else while (!ready) SDL_Delay(10);
    { std::vector<std::function<void()>> todo; { std::lock_guard<std::mutex> g(mtx); todo.swap(onMain); } for (auto& f : todo) f(); }
    Viewer::init();
    loadPrefs();
    loadGallery();
    int already = -1;   // a file that's in the gallery: that one (it keeps what it was made from)
    for (size_t i = 0; i < gallery.size() && !file.empty(); ++i) { std::error_code e; if (fs::equivalent(fs::u8path(gallery[i].path), fs::u8path(file), e)) already = (int)i; }
    if (already >= 0) show(already);
    else if (!file.empty()) { Image im; if (Pano::load(file, im)) { Skybox s; s.path = file; s.seam = Pano::seamError(im); s.thumbImg = thumbOf(im); s.img = std::move(im); gallery.insert(gallery.begin(), std::move(s)); show(0); } }
    else if (!gallery.empty() && (tools || prefs.reopenLast)) show(0);   // (else it opens empty: the gallery holds your skies, you pick one)
    if (!makeScene.empty()) { snprintf(scene, sizeof scene, "%s", makeScene.c_str()); makeSkybox(); }
    if (capture.empty()) { SDL_ShowWindow(window); SDL_MaximizeWindow(window); SDL_RaiseWindow(window); }

    int frames = 0;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_DROPFILE) {
                Image im; const std::string p = e.drop.file;
                if (Pano::load(p, im)) { Skybox s; s.path = p; s.seam = Pano::seamError(im); s.thumbImg = thumbOf(im); s.img = std::move(im); gallery.insert(gallery.begin(), std::move(s)); show(0); }
                SDL_free(e.drop.file);
            }
        }
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL2_NewFrame(); ImGui::NewFrame();
        if (tools && !busy && current >= 0) {
            if (tools == 1) { fixSeam(); tools = 2; }
            else if (tools == 2) { upscaleCurrent(2); tools = 3; }
            else if (tools == 3) {
                const Skybox& c = gallery[current];
                const std::string base = fs::u8path(c.path).replace_extension().u8string();
                auto f = Pano::cubeFaces(c.img, c.img.w / 4);
                std::vector<std::pair<std::string, std::string>> files;
                for (const char* n : {"px", "nx", "py", "ny", "pz", "nz"}) files.push_back({std::string(n) + ".png", Pano::pngBytes(f[n])});
                Pano::saveZip(base + "_cube.zip", files);
                Pano::savePng(Pano::cross(f), base + "_cross.png");
                say("Tools tried: seam, 2x, cube faces and cross saved");
                tools = 4;
            }
        }
        frame();
        ImGui::Render();
        int w, h; SDL_GL_GetDrawableSize(window, &w, &h);
        glViewport(0, 0, w, h); glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ++frames;
        if (!capture.empty() && frames >= 30 && !busy && (makeScene.empty() || frames > 60) && (tools == 0 || tools == 4)) {
            std::vector<unsigned char> px((size_t)w * h * 4);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            std::vector<unsigned char> flip(px.size());
            for (int y = 0; y < h; ++y) memcpy(&flip[(size_t)y * w * 4], &px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flip.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
            SDL_SaveBMP(s, capture.c_str());
            SDL_FreeSurface(s);
            break;
        }
        SDL_GL_SwapWindow(window);
    }
    Engine::cancel(); dlStop = true;
    if (job.joinable()) job.join();
    Engine::unload();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL2_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl); SDL_DestroyWindow(window); SDL_Quit();
    return 0;
}
