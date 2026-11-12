// Firmament - the engine (see engine.h), on stable-diffusion.cpp's C API
#include "engine.h"

#include <stable-diffusion.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <random>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace Engine {

static std::function<void(const std::string&)> logFn;
void setLog(std::function<void(const std::string&)> f) { logFn = std::move(f); }

static std::string exeDir() {
#ifdef _WIN32
    wchar_t b[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, b, (DWORD)(sizeof b / sizeof b[0]));
    return fs::path(std::wstring(b, n)).parent_path().u8string();
#else
    return ".";
#endif
}

Models findModels() {
    Models m;
    std::error_code ec;
    // the first folder that has each file: the shared model drive, then models\ beside Firmament
    const std::vector<fs::path> roots = {fs::path("F:/AIModels"), fs::u8path(exeDir()) / "models"};
    // each model under any of the names it goes by (what Firmament downloads, and what ComfyUI setups call it)
    auto find = [&](const char* sub, std::initializer_list<const char*> files) {
        for (const auto& r : roots)
            for (const char* f : files)
                for (const fs::path& p : {r / sub / f, r / f})
                    if (fs::exists(p, ec)) return p.u8string();
        return (roots.back() / sub / *files.begin()).u8string();   // where it would go
    };
    m.diffusion = find("krea2/diffusion_models", {"Krea-2-Turbo-Q4_K_M.gguf", "krea2_turbo_bf16-Q4_0.gguf", "Krea-2-Turbo-Q3_K_M.gguf", "Krea-2-Turbo-Q5_K_S.gguf", "Krea-2-Turbo-Q6_K.gguf", "Krea-2-Turbo-Q8_0.gguf"});
    m.llm = find("krea2/text_encoders", {"qwen3vl_4b_fp8_scaled.safetensors", "qwen3vl_4b_int8_convrot.safetensors", "qwen3vl_4b_bf16.safetensors"});
    m.vae = find("krea2/vae", {"wan_2.1_vae.safetensors", "qwen_image_vae.safetensors"});
    m.lora = find("krea2/loras", {"krea2_t2i_360_erp_lora_v1.safetensors"});
    m.upscaler = find("upscale", {"RealESRGAN_x4plus.pth"});
    m.upscalerAnime = find("upscale", {"RealESRGAN_x4plus_anime_6B.pth"});
    return m;
}

bool available(std::string& why) {
#ifdef _WIN32
    static int known = -1;
    static std::string reason;
    if (known < 0) {
        known = 0;
        if (!LoadLibraryW(L"nvcuda.dll")) reason = "Firmament paints on an NVIDIA graphics card, and this PC doesn't have one (or its driver).";
        else if (!LoadLibraryW(L"stable-diffusion.dll")) reason = "stable-diffusion.dll (or the CUDA libraries beside it) is missing: please install Firmament again.";
        else known = 1;
    }
    why = reason;
    return known == 1;
#else
    why.clear();
    return true;
#endif
}

std::vector<std::string> Models::missing() const {
    std::vector<std::string> v;
    std::error_code ec;
    for (const std::string* p : {&diffusion, &llm, &vae, &lora})
        if (!fs::exists(fs::u8path(*p), ec)) v.push_back(fs::u8path(*p).filename().u8string());
    return v;
}

const std::vector<std::string>& samplers() {
    static const std::vector<std::string> v = {"er_sde", "euler", "euler_a", "res_multistep", "dpm++2m", "heun", "ipndm", "lcm", "ddim_trailing", "tcd"};
    return v;
}
const std::vector<std::string>& schedulers() {
    static const std::vector<std::string> v = {"simple", "discrete", "karras", "exponential", "sgm_uniform", "smoothstep", "beta", "kl_optimal"};
    return v;
}

// ---- the loaded model, kept between jobs
static std::mutex mtx;
static sd_ctx_t* ctx = nullptr;
static std::string loadedKey;
static Progress* current = nullptr;

static void onLog(enum sd_log_level_t level, const char* text, void*) {
    if (level < SD_LOG_INFO || !logFn || !text) return;
    std::string s(text);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    if (!s.empty()) logFn(s);
}
static void onProgress(int step, int steps, float, void*) {
    if (current) { current->step = step; current->steps = steps; }
}

static bool ensureLoaded(const Models& m, bool offload, Progress& p, std::string& err) {
    const std::string key = m.diffusion + "|" + m.llm + "|" + m.vae + (offload ? "|off" : "|on");
    if (ctx && key == loadedKey) return true;
    if (ctx) { free_sd_ctx(ctx); ctx = nullptr; }
    p.stage = "Loading the models (the first time takes a minute)";
    sd_set_log_callback(onLog, nullptr);
    sd_set_progress_callback(onProgress, nullptr);
    sd_ctx_params_t c;
    sd_ctx_params_init(&c);
    c.diffusion_model_path = m.diffusion.c_str();
    c.llm_path = m.llm.c_str();
    c.vae_path = m.vae.c_str();
    c.n_threads = std::max(1, (int)sd_get_num_physical_cores());
    c.lora_apply_mode = LORA_APPLY_AUTO;
    c.flash_attn = true;
    c.diffusion_flash_attn = true;
    if (offload) c.params_backend = "*=cpu";   // the weights stay in system memory; the card only computes
    ctx = new_sd_ctx(&c);
    if (!ctx) { err = "The models didn't load (see the log): is there enough memory free?"; return false; }
    loadedKey = key;
    return true;
}

static sd_image_t view(const Image& im) { sd_image_t s; s.width = im.w; s.height = im.h; s.channel = 3; s.data = (uint8_t*)im.px.data(); return s; }

static std::string run(const Models& m, const Settings& s, const Image* init, const Image* mask, float strength, Image& out, int64_t* seedUsed, Progress& p) {
    std::lock_guard<std::mutex> g(mtx);
    std::string err;
    current = &p;
    p.running = true; p.cancel = false; p.step = 0; p.steps = s.steps;
    if (!ensureLoaded(m, s.offload, p, err)) { p.running = false; current = nullptr; return err; }
    sd_img_gen_params_t g2;
    sd_img_gen_params_init(&g2);
    sd_lora_t lora{false, s.loraStrength, m.lora.c_str()};
    g2.loras = &lora; g2.lora_count = s.loraStrength > 0 ? 1 : 0;
    g2.prompt = s.prompt.c_str();
    g2.negative_prompt = "";
    g2.width = init ? init->w : s.width;
    g2.height = init ? init->h : s.width / 2;
    sd_sample_params_init(&g2.sample_params);
    g2.sample_params.sample_steps = s.steps;
    g2.sample_params.guidance.txt_cfg = s.cfg;
    g2.sample_params.sample_method = str_to_sample_method(samplers()[std::clamp(s.sampler, 0, (int)samplers().size() - 1)].c_str());
    g2.sample_params.scheduler = str_to_scheduler(schedulers()[std::clamp(s.scheduler, 0, (int)schedulers().size() - 1)].c_str());
    if (s.flowShift > 0) g2.sample_params.flow_shift = s.flowShift;
    int64_t seed = s.seed;
    if (seed < 0) seed = (int64_t)(std::random_device{}() & 0x7fffffff) << 20 ^ (int64_t)std::random_device{}();
    if (seed < 0) seed = -seed;
    g2.seed = seed;
    g2.batch_count = 1;
    g2.circular_x = s.wrap;           // the panorama's left and right edges are one
    g2.vae_tiling_params.enabled = g2.width * g2.height > 1024 * 1024;   // big ones are decoded in tiles (6 GB cards)
    if (init) { g2.init_image = view(*init); g2.strength = strength; }
    if (mask) g2.mask_image = sd_image_t{(uint32_t)mask->w, (uint32_t)mask->h, 3, (uint8_t*)mask->px.data()};
    p.stage = init ? "Repainting" : "Painting the sky";
    sd_image_generation_result_t* r = nullptr;
    const bool ok = generate_image(ctx, &g2, &r);
    if (!ok || !r || r->count < 1 || !r->items[0].image.data) err = p.cancel ? "Stopped" : "The picture didn't come out (see the log)";
    else {
        const sd_image_t& im = r->items[0].image;
        out.w = (int)im.width; out.h = (int)im.height;
        out.px.resize((size_t)out.w * out.h * 3);
        for (size_t i = 0; i < (size_t)out.w * out.h; ++i) for (int c = 0; c < 3; ++c) out.px[i * 3 + c] = im.data[i * im.channel + c];
        if (seedUsed) *seedUsed = seed;
    }
    if (r) free_sd_image_generation_result(r);
    p.running = false; current = nullptr;
    return err;
}

std::string generate(const Models& m, const Settings& s, Image& out, int64_t& seedUsed, Progress& p) {
    return run(m, s, nullptr, nullptr, 1, out, &seedUsed, p);
}

std::string repaint(const Models& m, const Settings& s, const Image& in, const Image& mask, float strength, Image& out, Progress& p) {
    return run(m, s, &in, &mask, strength, out, nullptr, p);
}

std::string upscale(const std::string& model, const Image& in, int scale, Image& out, Progress& p) {
    std::lock_guard<std::mutex> g(mtx);
    p.running = true; p.stage = "Upscaling"; p.step = 0; p.steps = 1;
    // the diffusion model's memory first: the upscaler wants the card to itself
    if (ctx) { free_sd_ctx(ctx); ctx = nullptr; loadedKey.clear(); }
    upscaler_ctx_t* u = new_upscaler_ctx(model.c_str(), false, std::max(1, (int)sd_get_num_physical_cores()), 256, nullptr, nullptr);
    if (!u) { p.running = false; return "The upscaler didn't load: " + model; }
    sd_image_generation_result_t* r = nullptr;
    std::string err;
    if (!::upscale(u, view(in), 4, &r) || !r || r->count < 1) err = "The upscale didn't work (see the log)";
    else {
        const sd_image_t& im = r->items[0].image;
        Image big; big.w = (int)im.width; big.h = (int)im.height; big.px.resize((size_t)big.w * big.h * 3);
        for (size_t i = 0; i < (size_t)big.w * big.h; ++i) for (int c = 0; c < 3; ++c) big.px[i * 3 + c] = im.data[i * im.channel + c];
        if (scale >= 4) out = std::move(big);
        else {   // 2x: the 4x halved with a box filter (sharper than a 2x model would make it)
            out.w = big.w / 2; out.h = big.h / 2; out.px.resize((size_t)out.w * out.h * 3);
            for (int y = 0; y < out.h; ++y)
                for (int x = 0; x < out.w; ++x)
                    for (int c = 0; c < 3; ++c) {
                        const auto at = [&](int xx, int yy) { return (int)big.px[((size_t)yy * big.w + xx) * 3 + c]; };
                        out.px[((size_t)y * out.w + x) * 3 + c] = (uint8_t)((at(2 * x, 2 * y) + at(2 * x + 1, 2 * y) + at(2 * x, 2 * y + 1) + at(2 * x + 1, 2 * y + 1) + 2) / 4);
                    }
        }
    }
    if (r) free_sd_image_generation_result(r);
    free_upscaler_ctx(u);
    p.running = false;
    return err;
}

void cancel() {
    if (current) current->cancel = true;
    if (ctx) sd_cancel_generation(ctx, SD_CANCEL_ALL);
}

void unload() {
    std::lock_guard<std::mutex> g(mtx);
    if (ctx) { free_sd_ctx(ctx); ctx = nullptr; loadedKey.clear(); }
}

}
