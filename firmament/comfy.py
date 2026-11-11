"""Firmament's ComfyUI client: queue a workflow, follow its progress, fetch the images.

Standard library only (websocket-client, if installed, adds live step progress).
"""
import json
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

try:  # optional: live step progress over ComfyUI's websocket
    import websocket
except ImportError:
    websocket = None


class ComfyError(RuntimeError):
    """ComfyUI is unreachable, rejected the graph, failed or timed out."""


class Cancelled(ComfyError):
    """The job was cancelled."""


_cancelled = set()


def _get(server, path, timeout=10):
    with urllib.request.urlopen(f"{server}{path}", timeout=timeout) as r:
        return json.load(r)


def _post(server, path, body, timeout=30):
    req = urllib.request.Request(f"{server}{path}", data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        raw = r.read()
    return json.loads(raw) if raw else None


def ping(server, timeout=3):
    try:
        _get(server, "/system_stats", timeout=timeout)
        return True
    except Exception:
        return False


def list_models(server, node, field):
    """The filenames ComfyUI offers for one loader input, e.g. every LoRA it can see."""
    try:
        spec = _get(server, f"/object_info/{node}")[node]["input"]["required"][field]
        if spec[0] == "COMBO":
            return list(spec[1].get("options", []))
        return list(spec[0])
    except Exception:
        return []


def upload_image(server, filename, data):
    """Put image bytes in ComfyUI's input folder; returns the name to load it by."""
    boundary = uuid.uuid4().hex
    body = b"".join([
        f"--{boundary}\r\n".encode(),
        f'Content-Disposition: form-data; name="image"; filename="{filename}"\r\n'.encode(),
        b"Content-Type: application/octet-stream\r\n\r\n", data,
        f"\r\n--{boundary}\r\n".encode(),
        b'Content-Disposition: form-data; name="overwrite"\r\n\r\ntrue',
        f"\r\n--{boundary}--\r\n".encode(),
    ])
    req = urllib.request.Request(f"{server}/upload/image", data=body,
                                 headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    try:
        with urllib.request.urlopen(req) as r:
            res = json.load(r)
    except urllib.error.HTTPError as e:
        raise ComfyError(f"ComfyUI rejected the upload ({e.code}): {e.read().decode(errors='replace')}") from e
    except urllib.error.URLError as e:
        raise ComfyError(f"Can't reach ComfyUI at {server}: {e.reason}") from e
    return f"{res['subfolder']}/{res['name']}" if res.get("subfolder") else res["name"]


def _missing_model(detail):
    """A readable message when ComfyUI rejects a graph because a model file isn't in its list."""
    try:
        errors = json.loads(detail).get("node_errors", {})
    except (ValueError, AttributeError):
        return None
    for node in errors.values():
        for err in node.get("errors", []):
            info = err.get("extra_info", {})
            if err.get("type") == "value_not_in_list" and "name" in info.get("input_name", ""):
                return (f"ComfyUI can't find the model {info.get('received_value')!r}. Download it (see the "
                        "README), or restart ComfyUI if you've just added it.")
    return None


def queue(server, wf, client_id):
    try:
        return _post(server, "/prompt", {"prompt": wf, "client_id": client_id})["prompt_id"]
    except urllib.error.HTTPError as e:
        detail = e.read().decode(errors="replace")
        raise ComfyError(_missing_model(detail) or f"ComfyUI rejected the workflow ({e.code}): {detail}") from e
    except urllib.error.URLError as e:
        raise ComfyError(f"Can't reach ComfyUI at {server}: {e.reason}") from e


def cancel(server, prompt_id):
    """Drop the job from ComfyUI's queue, or interrupt it if it's running."""
    _cancelled.add(prompt_id)
    for path, body in (("/queue", {"delete": [prompt_id]}), ("/interrupt", {"prompt_id": prompt_id})):
        try:
            _post(server, path, body, timeout=10)
        except Exception:
            pass


def _queue_state(server, prompt_id):
    q = _get(server, "/queue")
    if any(item[1] == prompt_id for item in q.get("queue_running", [])):
        return "running"
    if any(item[1] == prompt_id for item in q.get("queue_pending", [])):
        return "pending"
    return None


def _check_failed(record):
    status = record.get("status", {})
    if status.get("status_str") != "error":
        return record
    for kind, data in status.get("messages", []):
        if kind == "execution_error":
            msg = (data.get("exception_message") or "").strip().splitlines()
            raise ComfyError(f"ComfyUI's {data.get('node_type', 'node')} failed: "
                             f"{msg[0] if msg else data.get('exception_type', 'error')}")
    raise ComfyError("ComfyUI reported an error for this job (see its window).")


def _wait(server, prompt_id, timeout):
    """Wait for the job's history record. The timeout counts from when it starts running."""
    deadline, missing = None, 0
    while True:
        if prompt_id in _cancelled:
            _cancelled.discard(prompt_id)
            raise Cancelled("Cancelled.")
        try:
            hist = _get(server, f"/history/{prompt_id}")
            if prompt_id in hist:
                return _check_failed(hist[prompt_id])
            state = _queue_state(server, prompt_id)
        except urllib.error.URLError as e:
            raise ComfyError(f"Lost contact with ComfyUI: {e.reason}") from e
        if state == "running" and deadline is None:
            deadline = time.time() + timeout
        missing = missing + 1 if state is None else 0
        if missing >= 5:
            raise ComfyError("The job left ComfyUI's queue without a result.")
        if deadline is not None and time.time() > deadline:
            cancel(server, prompt_id)
            _cancelled.discard(prompt_id)
            raise ComfyError(f"The job ran longer than {timeout} s and was stopped.")
        time.sleep(1.0)


# what the job is doing while ComfyUI runs a node of this class
PHASES = {
    "UnetLoaderGGUF": "loading", "CLIPLoader": "loading", "VAELoader": "loading",
    "LoraLoaderModelOnly": "loading", "UpscaleModelLoader": "loading",
    "CLIPTextEncode": "prompt", "ConditioningZeroOut": "prompt",
    "LoadImage": "image", "LoadImageMask": "image", "VAEEncode": "image", "SetLatentNoiseMask": "image",
    "KSampler": "sampling", "VAEDecode": "decoding",
    "ImageUpscaleWithModel": "upscaling", "ImageScaleBy": "upscaling", "SaveImage": "saving",
}


def _watch(server, ws, prompt_id, timeout, report, wf):
    """Pass on ComfyUI's events for this job until it ends (wait() gives the verdict)."""
    deadline, quiet = None, 0
    ws.settimeout(5)
    while deadline is None or time.time() < deadline:
        if prompt_id in _cancelled:
            return
        try:
            msg = ws.recv()
        except websocket.WebSocketTimeoutException:
            quiet += 1
            if deadline is None and quiet % 6 == 0:
                try:
                    if _queue_state(server, prompt_id) is None:
                        return
                except Exception:
                    return
            continue
        except Exception:
            return
        if not isinstance(msg, str):
            continue  # latent previews
        event = json.loads(msg)
        data = event.get("data", {})
        if data.get("prompt_id") != prompt_id:
            continue
        kind = event.get("type")
        if kind == "executing":
            if data.get("node") is None:
                return
            if deadline is None:
                deadline = time.time() + timeout
            node = wf.get(str(data["node"]), {})
            report({"state": "running", "phase": PHASES.get(node.get("class_type"), "working")})
        elif kind == "progress":
            report({"state": "running", "value": data["value"], "max": data["max"]})
        elif kind in ("execution_success", "execution_error", "execution_interrupted"):
            return


def run(server, wf, *, timeout=1800, on_progress=None, on_queued=None):
    """Queue wf, follow it, and return the saved images as [(filename, bytes)]."""
    client_id = uuid.uuid4().hex
    ws = None
    if websocket is not None:
        try:
            ws = websocket.create_connection(server.replace("http", "ws", 1) + f"/ws?clientId={client_id}", timeout=5)
        except Exception:
            ws = None
    try:
        prompt_id = queue(server, wf, client_id)
        if on_queued:
            on_queued(prompt_id)
        if ws and on_progress:
            on_progress({"state": "queued"})
            _watch(server, ws, prompt_id, timeout, on_progress, wf)
    finally:
        if ws:
            ws.close()
    record = _wait(server, prompt_id, timeout)
    images = []
    for node in record.get("outputs", {}).values():
        for img in node.get("images", []):
            if img.get("type", "output") != "output":
                continue
            with urllib.request.urlopen(f"{server}/view?{urllib.parse.urlencode(img)}") as r:
                images.append((img["filename"], r.read()))
    if not images:
        raise ComfyError("ComfyUI finished but saved no image.")
    return images
