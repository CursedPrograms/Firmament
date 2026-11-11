"""Firmament's image maths: equirectangular panoramas (2:1), their seam, wrap padding and cube maps.

The panorama's convention (the viewer uses the same one): the image centre looks along -Z, x grows to the right,
y is up. Longitude = atan2(x, -z), u = 0.5 + longitude / 2pi; latitude = asin(y), v = 0.5 - latitude / pi.
"""
import io

import numpy as np
from PIL import Image

# ---------- the left/right seam ----------
SEAM_STRIP = 1024      # the strip sent to ComfyUI, centred on the seam (after turning the panorama half round)
SEAM_BAND = 192        # how wide a band ComfyUI repaints
SEAM_FEATHER = 48      # how softly the repainted band blends in, on each side


def load(data):
    return np.asarray(Image.open(io.BytesIO(data)).convert("RGB"))


def png(arr):
    out = io.BytesIO()
    Image.fromarray(arr).save(out, "PNG")
    return out.getvalue()


def jpeg(arr, quality=88):
    out = io.BytesIO()
    Image.fromarray(arr).save(out, "JPEG", quality=quality)
    return out.getvalue()


def half_turn(arr):
    """Turn the panorama half way round: the left/right seam moves to the middle."""
    return np.roll(arr, arr.shape[1] // 2, axis=1)


def seam_strip(arr):
    """The strip to repaint and its mask (white = repaint), plus where the strip sits in the turned panorama."""
    turned = half_turn(arr)
    h, w = turned.shape[:2]
    sw = min(SEAM_STRIP, w)
    x0 = w // 2 - sw // 2
    strip = turned[:, x0:x0 + sw].copy()
    mask = np.zeros((h, sw), np.uint8)
    b0 = sw // 2 - SEAM_BAND // 2
    mask[:, b0:b0 + SEAM_BAND] = 255
    return strip, np.dstack([mask] * 3), x0


def seam_merge(arr, repainted, x0):
    """Blend the repainted strip's band back in (feathered) and turn the panorama back."""
    turned = half_turn(arr).astype(np.float32)
    h, w = turned.shape[:2]
    sw = min(SEAM_STRIP, w)
    if repainted.shape[:2] != (h, sw):   # ComfyUI rounds sizes to multiples of 8
        repainted = np.asarray(Image.fromarray(repainted).resize((sw, h), Image.LANCZOS))
    xs = np.arange(sw, dtype=np.float32)
    centre, half = sw / 2, SEAM_BAND / 2
    alpha = np.clip((half - np.abs(xs - centre)) / SEAM_FEATHER + 0.5, 0, 1)[None, :, None]
    region = turned[:, x0:x0 + sw]
    turned[:, x0:x0 + sw] = region * (1 - alpha) + repainted.astype(np.float32) * alpha
    back = np.roll(turned, -(turned.shape[1] // 2), axis=1)
    return np.clip(back + 0.5, 0, 255).astype(np.uint8)


def seam_error(arr):
    """How badly the left and right edges disagree (mean difference, 0..255): a seam worth fixing is above ~12."""
    a = arr[:, :2].astype(np.float32).mean(axis=1)
    b = arr[:, -2:].astype(np.float32).mean(axis=1)
    return float(np.abs(a - b).mean())


# ---------- wrap padding (for upscaling without a seam) ----------
def wrap_pad(arr, pad):
    return np.concatenate([arr[:, -pad:], arr, arr[:, :pad]], axis=1)


def unpad(arr, pad, scale):
    p = int(round(pad * scale))
    return arr[:, p:arr.shape[1] - p]


# ---------- cube maps ----------
FACES = ("px", "nx", "py", "ny", "pz", "nz")


def _face_dirs(face, n):
    """The view direction through each pixel of a cube face (OpenGL's cube map layout)."""
    c = (np.arange(n, dtype=np.float32) + 0.5) / n * 2 - 1
    s, t = np.meshgrid(c, c)              # s: right, t: down
    one = np.ones_like(s)
    return {
        "px": (one, -t, -s), "nx": (-one, -t, s),
        "py": (s, one, t), "ny": (s, -one, -t),
        "pz": (s, -t, one), "nz": (-s, -t, -one),
    }[face]


def _sample(arr, u, v):
    """Bilinear sample of the panorama at u, v (0..1), wrapping left/right."""
    h, w = arr.shape[:2]
    x = u * w - 0.5
    y = np.clip(v * h - 0.5, 0, h - 1)
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx = (x - x0)[..., None]
    fy = (y - y0)[..., None]
    x0 %= w
    x1 = (x0 + 1) % w
    y1 = np.minimum(y0 + 1, h - 1)
    a = arr.astype(np.float32)
    top = a[y0, x0] * (1 - fx) + a[y0, x1] * fx
    bottom = a[y1, x0] * (1 - fx) + a[y1, x1] * fx
    return np.clip(top * (1 - fy) + bottom * fy + 0.5, 0, 255).astype(np.uint8)


def cube_faces(arr, size=None):
    """The six cube faces (px nx py ny pz nz), each size x size (default: a quarter of the panorama's width)."""
    size = size or arr.shape[1] // 4
    faces = {}
    for f in FACES:
        x, y, z = _face_dirs(f, size)
        norm = np.sqrt(x * x + y * y + z * z)
        lon = np.arctan2(x, -z)
        lat = np.arcsin(np.clip(y / norm, -1, 1))
        faces[f] = _sample(arr, 0.5 + lon / (2 * np.pi), 0.5 - lat / np.pi)
    return faces


def cross(faces):
    """The faces as a horizontal cross (4 x 3): +Y on top, -X +Z +X -Z across the middle, -Y below."""
    n = faces["px"].shape[0]
    out = np.zeros((3 * n, 4 * n, 3), np.uint8)
    for f, (col, row) in {"py": (1, 0), "nx": (0, 1), "pz": (1, 1), "px": (2, 1), "nz": (3, 1), "ny": (1, 2)}.items():
        out[row * n:(row + 1) * n, col * n:(col + 1) * n] = faces[f]
    return out


def thumbnail(arr, width=512):
    img = Image.fromarray(arr)
    img.thumbnail((width, width // 2), Image.LANCZOS)
    return np.asarray(img)
