// Firmament - the engine: stable-diffusion.cpp running Krea 2 Turbo with the 360° panorama LoRA, kept loaded between
// skyboxes; repainting a band (the seam); upscaling with Real-ESRGAN. One job at a time, on its own thread.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct Image {                          // 8-bit RGB
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool empty() const { return px.empty(); }
};

namespace Engine {

struct Models {                         // where the files are
    std::string diffusion, llm, vae, lora, upscaler, upscalerAnime;
    std::vector<std::string> missing() const;   // names of the ones not on disk
};
Models findModels();
// an NVIDIA card and stable-diffusion.cpp's library both there (Firmament opens without them, but can't paint)
bool available(std::string& why);                    // F:\AIModels\krea2 and \upscale, else beside Firmament (models\)

struct Settings {
    std::string prompt;                 // the full prompt (template applied)
    int width = 2048;                   // height is width / 2
    int steps = 10;
    float cfg = 1.0f;                   // Krea 2 Turbo wants 1
    float loraStrength = 1.0f;
    float flowShift = 0;                // 0: the model's own
    int sampler = 0;                    // index into samplers()
    int scheduler = 0;                  // index into schedulers()
    int64_t seed = -1;                  // -1: random
    bool wrap = true;                   // generate with the left and right edges joined (circular)
    bool offload = true;                // weights in system RAM (for 6-8 GB graphics cards)
};
const std::vector<std::string>& samplers();
const std::vector<std::string>& schedulers();

struct Progress {
    std::atomic<int> step{0}, steps{0};
    std::atomic<bool> running{false}, cancel{false};
    std::string stage;                  // "Loading the models", "Painting", "Decoding"...
};

// a new panorama; returns "" or what went wrong. seedUsed gets the seed.
std::string generate(const Models& m, const Settings& s, Image& out, int64_t& seedUsed, Progress& p);
// repaint where mask is white (img2img at strength, the rest kept)
std::string repaint(const Models& m, const Settings& s, const Image& in, const Image& mask, float strength, Image& out, Progress& p);
// 2x or 4x (the model is 4x; 2x is it halved)
std::string upscale(const std::string& model, const Image& in, int scale, Image& out, Progress& p);
void cancel();                          // stop the painting as soon as it can
void unload();                          // frees the graphics memory
void setLog(std::function<void(const std::string&)> f);

}
