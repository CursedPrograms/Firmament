// Firmament's 360 viewer: one WebGL2 quad whose shader turns each pixel into a view direction and looks it up in
// the equirectangular panorama. Drag (or swipe) to look round, wheel (or pinch) to zoom, "flat" shows the whole map.
// The convention matches firmament/skybox.py: the image centre looks along -Z, y is up.
"use strict";

const VERT = `#version 300 es
in vec2 pos; out vec2 uv;
void main() { uv = pos; gl_Position = vec4(pos, 0.0, 1.0); }`;

const FRAG = `#version 300 es
precision highp float;
in vec2 uv; out vec4 color;
uniform sampler2D pano; uniform float yaw, pitch, tanHalf, aspect; uniform int flatView;
const float PI = 3.14159265358979;
void main() {
    vec2 t;
    if (flatView == 1) {
        t = vec2(uv.x * 0.5 + 0.5, 0.5 - uv.y * 0.5);
    } else {
        vec3 d = normalize(vec3(uv.x * tanHalf * aspect, uv.y * tanHalf, -1.0));
        float cp = cos(pitch), sp = sin(pitch);
        d = vec3(d.x, d.y * cp + d.z * sp, -d.y * sp + d.z * cp);        // look up / down
        float cy = cos(yaw), sy = sin(yaw);
        d = vec3(d.x * cy + d.z * sy, d.y, -d.x * sy + d.z * cy);        // turn left / right
        t = vec2(0.5 + atan(d.x, -d.z) / (2.0 * PI), 0.5 - asin(clamp(d.y, -1.0, 1.0)) / PI);
    }
    // no mipmaps, so the wrap at the seam needs no special gradients
    color = texture(pano, t);
}`;

export class Viewer {
    constructor(canvas) {
        this.canvas = canvas;
        this.yaw = 0; this.pitch = 0; this.fov = 90; this.flat = false; this.spin = false;
        this.ready = false;
        const gl = canvas.getContext("webgl2", { antialias: false, preserveDrawingBuffer: false });
        if (!gl) { this.error = "WebGL2 isn't available in this browser."; return; }
        const prog = gl.createProgram();
        for (const [type, src] of [[gl.VERTEX_SHADER, VERT], [gl.FRAGMENT_SHADER, FRAG]]) {
            const s = gl.createShader(type);
            gl.shaderSource(s, src); gl.compileShader(s);
            // the page still works without the view: the error is shown where the view would be
            if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) { this.error = `The 360 view failed: ${gl.getShaderInfoLog(s)}`; return; }
            gl.attachShader(prog, s);
        }
        this.gl = gl;
        gl.linkProgram(prog);
        gl.useProgram(prog);
        this.prog = prog;
        const buf = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, buf);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
        const loc = gl.getAttribLocation(prog, "pos");
        gl.enableVertexAttribArray(loc);
        gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
        this.tex = gl.createTexture();
        this.u = {};
        for (const n of ["yaw", "pitch", "tanHalf", "aspect", "flatView"]) this.u[n] = gl.getUniformLocation(prog, n);
        this._input();
        this._loop = this._loop.bind(this);
        requestAnimationFrame(this._loop);
    }

    load(url) {
        if (!this.gl) return Promise.resolve();
        return new Promise((resolve, reject) => {
            const img = new Image();
            img.onload = () => {
                const gl = this.gl;
                const max = gl.getParameter(gl.MAX_TEXTURE_SIZE);
                let src = img;
                if (img.width > max) {      // very large upscales: shrink to what the GPU takes
                    const c = document.createElement("canvas");
                    c.width = max; c.height = max / 2;
                    c.getContext("2d").drawImage(img, 0, 0, c.width, c.height);
                    src = c;
                }
                gl.bindTexture(gl.TEXTURE_2D, this.tex);
                gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, src);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
                gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
                this.ready = true;
                resolve();
            };
            img.onerror = reject;
            img.src = url;
        });
    }

    reset() { this.yaw = 0; this.pitch = 0; this.fov = 90; }

    _input() {
        const c = this.canvas;
        const pointers = new Map();
        let pinch = 0;
        c.addEventListener("pointerdown", e => { c.setPointerCapture(e.pointerId); pointers.set(e.pointerId, [e.clientX, e.clientY]); this.spin = false; this.onSpin?.(false); });
        c.addEventListener("pointermove", e => {
            if (!pointers.has(e.pointerId)) return;
            const [px, py] = pointers.get(e.pointerId);
            pointers.set(e.pointerId, [e.clientX, e.clientY]);
            if (pointers.size === 2) {
                const [a, b] = [...pointers.values()];
                const d = Math.hypot(a[0] - b[0], a[1] - b[1]);
                if (pinch) this.zoom(pinch / d);
                pinch = d;
                return;
            }
            if (this.flat) return;
            const k = (this.fov / c.clientHeight) * Math.PI / 180;
            // the sky follows the finger: dragging right turns the view left, dragging down looks up
            this.yaw += (e.clientX - px) * k;
            this.pitch = Math.max(-1.55, Math.min(1.55, this.pitch - (e.clientY - py) * k));
        });
        const up = e => { pointers.delete(e.pointerId); if (pointers.size < 2) pinch = 0; };
        c.addEventListener("pointerup", up);
        c.addEventListener("pointercancel", up);
        c.addEventListener("wheel", e => { e.preventDefault(); this.zoom(e.deltaY > 0 ? 1.08 : 1 / 1.08); }, { passive: false });
    }

    zoom(f) { this.fov = Math.max(30, Math.min(120, this.fov * f)); }

    _loop(t) {
        requestAnimationFrame(this._loop);
        const gl = this.gl, c = this.canvas;
        const dpr = Math.min(window.devicePixelRatio || 1, 2);
        const w = Math.round(c.clientWidth * dpr), h = Math.round(c.clientHeight * dpr);
        if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
        if (this.spin && !this.flat) this.yaw += 0.0015;
        gl.viewport(0, 0, w, h);
        gl.clearColor(0.1, 0.1, 0.1, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);
        if (!this.ready) return;
        gl.uniform1f(this.u.yaw, this.yaw);
        gl.uniform1f(this.u.pitch, this.pitch);
        gl.uniform1f(this.u.tanHalf, Math.tan(this.fov * Math.PI / 360));
        gl.uniform1f(this.u.aspect, w / h);
        gl.uniform1i(this.u.flatView, this.flat ? 1 : 0);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
    }
}
