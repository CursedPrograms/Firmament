"""Firmament: 360-degree skyboxes from a prompt, on your own GPU (Krea 2 Turbo + a 360 LoRA, through ComfyUI).

    python app.py        then open http://127.0.0.1:7880 (or scan the QR code it prints with your phone)

Settings (environment): FIRMAMENT_PORT (7880), FIRMAMENT_HOST (0.0.0.0: the home network can open it;
127.0.0.1: only this PC), COMFY_SERVER (http://127.0.0.1:8188).
"""
import io
import json
import os
import queue
import re
import threading
import time
import uuid
import zipfile
from pathlib import Path

from flask import Flask, abort, jsonify, request, send_file, send_from_directory

from firmament import comfy, lan, pipeline, skybox

ROOT = Path(__file__).resolve().parent
UI = ROOT / "ui"
OUT = ROOT / "output"
THUMBS = OUT / "thumbs"
EXPORTS = OUT / "exports"
for d in (OUT, THUMBS, EXPORTS):
    d.mkdir(parents=True, exist_ok=True)

SERVER = os.environ.get("COMFY_SERVER", "http://127.0.0.1:8188").rstrip("/")
HOST = os.environ.get("FIRMAMENT_HOST", "0.0.0.0")
PORT = int(os.environ.get("FIRMAMENT_PORT", 7880))
NAME_RE = re.compile(r"^[A-Za-z0-9_\-]+$")

app = Flask(__name__, static_folder=None)


# ---------- the library: output/<name>.png + <name>.json, thumbs/<name>.jpg ----------
def _slug(text, n=40):
    s = re.sub(r"[^A-Za-z0-9]+", "-", text.lower()).strip("-")
    return s[:n].rstrip("-") or "skybox"


def save_skybox(arr, meta):
    name = f"{time.strftime('%Y%m%d-%H%M%S')}_{_slug(meta.get('scene', ''))}_{uuid.uuid4().hex[:4]}"
    (OUT / f"{name}.png").write_bytes(skybox.png(arr))
    (THUMBS / f"{name}.jpg").write_bytes(skybox.jpeg(skybox.thumbnail(arr)))
    meta = {**meta, "name": name, "width": int(arr.shape[1]), "height": int(arr.shape[0]),
            "created": time.time(), "seam": round(skybox.seam_error(arr), 1)}
    (OUT / f"{name}.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    return meta


def load_meta(name):
    if not NAME_RE.match(name or "") or not (OUT / f"{name}.png").exists():
        abort(404)
    p = OUT / f"{name}.json"
    return json.loads(p.read_text(encoding="utf-8")) if p.exists() else {"name": name}


def load_array(name):
    load_meta(name)
    return skybox.load((OUT / f"{name}.png").read_bytes())


# ---------- jobs: one at a time, in order (the GPU only does one thing well) ----------
jobs = {}
work = queue.Queue()


def new_job(kind, run, label):
    jid = uuid.uuid4().hex[:12]
    jobs[jid] = {"id": jid, "kind": kind, "label": label, "state": "waiting", "phase": None,
                 "value": 0, "max": 0, "result": None, "error": None, "created": time.time(), "prompt_id": None}
    work.put((jid, run))
    return jobs[jid]


def worker():
    while True:
        jid, run = work.get()
        job = jobs.get(jid)
        if not job or job["state"] == "cancelled":
            continue
        job["state"] = "running"

        def progress(ev, job=job):
            if ev.get("phase"):
                job["phase"] = ev["phase"]
            if "value" in ev:
                job["value"], job["max"] = ev["value"], ev["max"]

        def queued(pid, job=job):
            job["prompt_id"] = pid

        try:
            job["result"] = run(progress, queued)
            job["state"] = "done"
        except comfy.Cancelled:
            job["state"] = "cancelled"
        except Exception as e:  # shown on the job card
            job["state"], job["error"] = "failed", str(e)


threading.Thread(target=worker, daemon=True).start()


# ---------- pages and files ----------
@app.get("/")
def index():
    return send_from_directory(UI, "index.html")


@app.get("/ui/<path:path>")
def ui(path):
    return send_from_directory(UI, path)


@app.get("/output/<path:path>")
def output(path):
    return send_from_directory(OUT, path)


# ---------- the API ----------
@app.get("/api/health")
def health():
    missing = pipeline.check(SERVER)
    return jsonify({"comfy": "comfyui" not in missing, "missing": [m for m in missing if m != "comfyui"],
                    "server": SERVER})


@app.get("/api/phone")
def phone():
    url = lan.phone_url(PORT, HOST)
    return jsonify({"url": url, "svg": lan.qr_svg(url) if url else None})


@app.get("/api/styles")
def styles():
    return jsonify(list(pipeline.STYLES))


@app.get("/api/skyboxes")
def list_skyboxes():
    items = []
    for p in sorted(OUT.glob("*.json"), reverse=True):
        try:
            items.append(json.loads(p.read_text(encoding="utf-8")))
        except ValueError:
            pass
    return jsonify(items)


@app.delete("/api/skyboxes/<name>")
def delete_skybox(name):
    load_meta(name)
    for p in [OUT / f"{name}.png", OUT / f"{name}.json", THUMBS / f"{name}.jpg", *EXPORTS.glob(f"{name}_*")]:
        p.unlink(missing_ok=True)
    return jsonify({"ok": True})


@app.post("/api/generate")
def generate():
    body = request.get_json(force=True)
    scene = (body.get("scene") or "").strip()
    if not scene:
        return jsonify({"error": "Describe the scene first."}), 400
    style = body.get("style", "photo")
    steps = max(4, min(30, int(body.get("steps") or 10)))
    width = int(body.get("width") or 2048)

    def run(progress, queued):
        arr, seed, prompt = pipeline.generate(SERVER, scene, style=style, seed=body.get("seed"), steps=steps,
                                              width=width, on_progress=progress, on_queued=queued)
        return save_skybox(arr, {"scene": scene, "style": style, "seed": seed, "steps": steps, "prompt": prompt,
                                 "op": "generate"})

    return jsonify(new_job("generate", run, scene))


@app.post("/api/skyboxes/<name>/seam")
def seam(name):
    meta = load_meta(name)

    def run(progress, queued):
        arr = load_array(name)
        prompt = meta.get("prompt") or pipeline.prompt_for(meta.get("scene", "a landscape"), meta.get("style", "photo"))
        fixed = pipeline.fix_seam(SERVER, arr, prompt, steps=meta.get("steps", 10), on_progress=progress,
                                  on_queued=queued)
        return save_skybox(fixed, {**{k: meta.get(k) for k in ("scene", "style", "seed", "steps", "prompt")},
                                   "op": "seam", "parent": name})

    return jsonify(new_job("seam", run, meta.get("scene", name)))


@app.post("/api/skyboxes/<name>/upscale")
def upscale(name):
    meta = load_meta(name)
    scale = 4 if int((request.get_json(silent=True) or {}).get("scale", 2)) >= 4 else 2

    def run(progress, queued):
        big = pipeline.upscale(SERVER, load_array(name), scale=scale, on_progress=progress, on_queued=queued)
        return save_skybox(big, {**{k: meta.get(k) for k in ("scene", "style", "seed", "steps", "prompt")},
                                 "op": f"upscale{scale}", "parent": name})

    return jsonify(new_job("upscale", run, meta.get("scene", name)))


@app.get("/api/jobs")
def list_jobs():
    return jsonify(sorted(jobs.values(), key=lambda j: j["created"]))


@app.post("/api/jobs/<jid>/cancel")
def cancel_job(jid):
    job = jobs.get(jid) or abort(404)
    if job["state"] == "waiting":
        job["state"] = "cancelled"
    elif job["state"] == "running" and job["prompt_id"]:
        comfy.cancel(SERVER, job["prompt_id"])
    return jsonify(job)


@app.post("/api/jobs/clear")
def clear_jobs():
    for jid in [j for j, job in jobs.items() if job["state"] in ("done", "failed", "cancelled")]:
        del jobs[jid]
    return jsonify({"ok": True})


# ---------- exports ----------
@app.get("/api/skyboxes/<name>/cubemap.zip")
def cubemap_zip(name):
    path = EXPORTS / f"{name}_cubemap.zip"
    if not path.exists():
        faces = skybox.cube_faces(load_array(name))
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_STORED) as z:
            for f, arr in faces.items():
                z.writestr(f"{f}.png", skybox.png(arr))
            z.writestr("README.txt", "Cube map faces from Firmament (OpenGL layout): px nx = +X -X, py ny = +Y -Y "
                                     "(up, down), pz nz = +Z -Z. The panorama's centre is -Z (nz).\n")
        path.write_bytes(buf.getvalue())
    return send_file(path, as_attachment=True, download_name=f"{name}_cubemap.zip")


@app.get("/api/skyboxes/<name>/cross.png")
def cubemap_cross(name):
    path = EXPORTS / f"{name}_cross.png"
    if not path.exists():
        path.write_bytes(skybox.png(skybox.cross(skybox.cube_faces(load_array(name)))))
    return send_file(path, as_attachment=True, download_name=f"{name}_cross.png")


@app.get("/api/skyboxes/<name>/panorama.png")
def panorama(name):
    load_meta(name)
    return send_file(OUT / f"{name}.png", as_attachment=True, download_name=f"{name}.png")


if __name__ == "__main__":
    print(f"Firmament on http://127.0.0.1:{PORT}  (ComfyUI: {SERVER})")
    url = lan.phone_url(PORT, HOST)
    if url:
        lan.print_qr(url, "Firmament")
    from waitress import serve
    serve(app, host=HOST, port=PORT, threads=8)
