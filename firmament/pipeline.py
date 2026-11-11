"""Firmament's jobs on ComfyUI: a skybox from a prompt, the seam repainted, an upscale."""
import json
import random
import uuid
from pathlib import Path

from . import comfy, skybox

WORKFLOWS = Path(__file__).resolve().parent.parent / "workflows"
LORA = "krea2_t2i_360_erp_lora_v1.safetensors"
MAX_SEED = 2 ** 53

# The LoRA's prompt template (mickmumpitz/Krea2-360-ERP-LoRAs); the style takes the place of "photoreal".
STYLES = {
    "photo": "photoreal",
    "painting": "painterly digital matte painting",
    "game": "stylized video game environment art",
    "anime": "anime background painting",
}
TEMPLATE = ("img-txt-2-360 A full 360 degree equirectangular panorama in 2:1 spherical projection, {scene}, "
            "{style}, seamless wrap, level horizon.")


def prompt_for(scene, style):
    return TEMPLATE.format(scene=scene.strip().rstrip(".,"), style=STYLES.get(style, STYLES["photo"]))


def _load(name):
    return json.loads((WORKFLOWS / name).read_text(encoding="utf-8"))


def _seed(seed):
    return random.randrange(MAX_SEED) if seed in (None, "", -1) else int(seed) % MAX_SEED


def generate(server, scene, *, style="photo", seed=None, steps=10, width=2048, on_progress=None, on_queued=None):
    """A new panorama (width x width/2). Returns (array, seed, prompt)."""
    seed = _seed(seed)
    width = max(512, min(4096, int(width) // 64 * 64))
    wf = _load("krea2_360_t2i_api.json")
    text = prompt_for(scene, style)
    wf["4"]["inputs"]["text"] = text
    wf["5"]["inputs"].update(width=width, height=width // 2)
    wf["6"]["inputs"].update(seed=seed, steps=int(steps))
    images = comfy.run(server, wf, on_progress=on_progress, on_queued=on_queued)
    return skybox.load(images[0][1]), seed, text


def fix_seam(server, arr, prompt, *, seed=None, steps=10, strength=0.55, on_progress=None, on_queued=None):
    """Repaint a band across the left/right seam so the panorama wraps cleanly."""
    strip, mask, x0 = skybox.seam_strip(arr)
    tag = uuid.uuid4().hex[:8]
    strip_name = comfy.upload_image(server, f"firmament_strip_{tag}.png", skybox.png(strip))
    mask_name = comfy.upload_image(server, f"firmament_strip_{tag}_mask.png", skybox.png(mask))
    wf = _load("krea2_360_seam_api.json")
    wf["4"]["inputs"]["text"] = prompt
    wf["11"]["inputs"]["image"] = strip_name
    wf["12"]["inputs"]["image"] = mask_name
    wf["6"]["inputs"].update(seed=_seed(seed), steps=int(steps), denoise=float(strength))
    images = comfy.run(server, wf, on_progress=on_progress, on_queued=on_queued)
    return skybox.seam_merge(arr, skybox.load(images[0][1]), x0)


UPSCALE_MODEL = "4x-UltraSharpV2_Lite.safetensors"
PAD = 64


def upscale(server, arr, *, scale=2, on_progress=None, on_queued=None):
    """2x or 4x with the upscale model, padded with the other edge first so the seam stays seamless."""
    scale = 4 if int(scale) >= 4 else 2
    name = comfy.upload_image(server, f"firmament_up_{uuid.uuid4().hex[:8]}.png", skybox.png(skybox.wrap_pad(arr, PAD)))
    wf = _load("upscale_api.json")
    wf["1"]["inputs"]["image"] = name
    wf["2"]["inputs"]["model_name"] = UPSCALE_MODEL
    if scale == 4:
        del wf["4"]
        wf["5"]["inputs"]["images"] = ["3", 0]
    else:
        wf["4"]["inputs"]["scale_by"] = scale / 4
    images = comfy.run(server, wf, on_progress=on_progress, on_queued=on_queued)
    return skybox.unpad(skybox.load(images[0][1]), PAD, scale)


def check(server):
    """What's missing for Firmament to work: [] when ComfyUI is up and has every model."""
    if not comfy.ping(server):
        return ["comfyui"]
    missing = []
    wanted = [("UnetLoaderGGUF", "unet_name", "krea2_turbo_bf16-Q4_0.gguf"),
              ("CLIPLoader", "clip_name", "qwen3vl_4b_fp8_scaled.safetensors"),
              ("VAELoader", "vae_name", "wan_2.1_vae.safetensors"),
              ("LoraLoaderModelOnly", "lora_name", LORA),
              ("UpscaleModelLoader", "model_name", UPSCALE_MODEL)]
    for node, field, name in wanted:
        if name not in comfy.list_models(server, node, field):
            missing.append(name)
    return missing
