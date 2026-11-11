// Firmament's page: the form, the jobs, the gallery and the 360 view. Text comes from ui/lang/<code>.json.
"use strict";
import { Viewer } from "./viewer.js";

const $ = id => document.getElementById(id);
const LANGS = { en: "English", ja: "日本語", zh: "中文", es: "Español", ko: "한국어", fr: "Français", de: "Deutsch",
    ru: "Русский", sr: "Српски", af: "Afrikaans", yua: "Maaya t'aan", pdc: "Deitsch" };
// The chips fill in a scene in English (what the model knows best); their labels are translated.
const PRESETS = [
    ["space", "deep space with a violet nebula, a ringed gas giant and a dense field of stars"],
    ["sunset", "a calm tropical ocean at sunset, glowing orange clouds, gentle waves to the horizon"],
    ["mountains", "snowy alpine peaks above a sea of clouds at sunrise, clear blue sky"],
    ["desert", "a red alien desert at dusk with two moons over jagged mesas"],
    ["forest", "a misty enchanted forest clearing with giant ancient trees and shafts of light"],
    ["city", "a neon city rooftop at night in the rain, towers and flying lights all around"],
    ["storm", "a dark stormy sky over open grassland, lightning on the horizon"],
    ["underwater", "deep underwater on a coral reef, sunbeams from the surface, schools of fish"],
];

let strings = {}, english = {};
const t = (key, vars = {}) => (strings[key] ?? english[key] ?? key).replace(/\{(\w+)\}/g, (_, k) => vars[k] ?? "");

async function loadLang(code) {
    const get = async c => (await fetch(`/ui/lang/${c}.json`)).json();
    if (!Object.keys(english).length) english = await get("en");
    strings = code === "en" ? english : await get(code).catch(() => english);
    document.documentElement.lang = code;
    localStorage.setItem("firmament.lang", code);
    document.querySelectorAll("[data-i18n]").forEach(el => { el.textContent = t(el.dataset.i18n); });
    document.querySelectorAll("[data-i18n-ph]").forEach(el => { el.placeholder = t(el.dataset.i18nPh); });
    renderPresets();
    renderJobs();
    renderDetails();
    checkHealth();
}

// ---------- the view ----------
const viewer = new Viewer($("viewer"));
if (viewer.error) $("empty").textContent = viewer.error;
viewer.onSpin = on => $("spin").classList.toggle("on", on);
$("mode").onclick = () => {
    viewer.flat = !viewer.flat;
    $("mode").classList.toggle("on", viewer.flat);
};
$("spin").onclick = () => { viewer.spin = !viewer.spin; $("spin").classList.toggle("on", viewer.spin); };
$("full").onclick = () => {
    const v = document.querySelector(".view");
    document.fullscreenElement ? document.exitFullscreen() : v.requestFullscreen?.();
};

// ---------- ComfyUI's state ----------
async function checkHealth() {
    const box = $("status"), text = $("status-text");
    try {
        const h = await (await fetch("/api/health")).json();
        if (!h.comfy) { box.dataset.state = "bad"; text.textContent = t("status.nocomfy"); }
        else if (h.missing.length) { box.dataset.state = "bad"; text.textContent = t("status.missing", { names: h.missing.join(", ") }); }
        else { box.dataset.state = "ok"; text.textContent = t("status.ok"); }
    } catch {
        box.dataset.state = "bad"; text.textContent = t("status.noserver");
    }
}
setInterval(checkHealth, 15000);

// ---------- the form ----------
function renderPresets() {
    const box = $("presets");
    box.replaceChildren(...PRESETS.map(([key, scene]) => {
        const b = document.createElement("button");
        b.type = "button";
        b.textContent = t(`preset.${key}`);
        b.onclick = () => { $("scene").value = scene; $("scene").focus(); };
        return b;
    }));
}

async function post(url, body) {
    const r = await fetch(url, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body || {}) });
    const data = await r.json().catch(() => ({}));
    if (!r.ok) throw new Error(data.error || r.statusText);
    return data;
}

async function generate(scene, extra = {}) {
    const err = $("form-error");
    err.hidden = true;
    try {
        await post("/api/generate", {
            scene, style: $("style").value, width: +$("size").value, steps: +$("steps").value || 10,
            seed: $("seed").value === "" ? null : +$("seed").value, ...extra,
        });
        pollJobs();
    } catch (e) {
        err.textContent = e.message; err.hidden = false;
    }
}
$("generate").onclick = () => {
    const scene = $("scene").value.trim();
    if (!scene) { $("form-error").textContent = t("form.need_scene"); $("form-error").hidden = false; return; }
    generate(scene);
};
$("scene").addEventListener("keydown", e => { if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); $("generate").click(); } });

// ---------- jobs ----------
let jobs = [], pollTimer = null, seen = null;     // seen: finished jobs already shown (null until the first poll)

function renderJobs() {
    const box = $("jobs");
    const shown = jobs.filter(j => j.state !== "cancelled").slice(-6).reverse();
    if (!shown.length) { box.innerHTML = ""; const p = document.createElement("p"); p.className = "dim"; p.textContent = t("jobs.none"); box.append(p); return; }
    box.replaceChildren(...shown.map(j => {
        const card = document.createElement("div");
        card.className = "job"; card.dataset.state = j.state;
        const top = document.createElement("div"); top.className = "top";
        const label = document.createElement("span"); label.className = "label";
        label.textContent = `${t(`kind.${j.kind}`)}: ${j.label}`;
        const state = document.createElement("span"); state.className = "state";
        let s = t(`state.${j.state}`);
        if (j.state === "running" && j.phase) s = t(`phase.${j.phase}`);
        if (j.state === "running" && j.max && j.phase === "sampling") s += ` ${j.value}/${j.max}`;
        state.textContent = s;
        top.append(label, state);
        card.append(top);
        if (j.state === "running" || j.state === "waiting") {
            const bar = document.createElement("div"); bar.className = "bar2";
            const fill = document.createElement("i");
            fill.style.width = j.max ? `${Math.round(100 * j.value / j.max)}%` : "0";
            bar.append(fill); card.append(bar);
            const stop = document.createElement("button"); stop.textContent = t("jobs.cancel");
            stop.onclick = () => post(`/api/jobs/${j.id}/cancel`).then(pollJobs);
            card.append(stop);
        }
        if (j.error) { const e = document.createElement("div"); e.className = "err"; e.textContent = j.error; card.append(e); }
        return card;
    }));
}

async function pollJobs() {
    clearTimeout(pollTimer);
    try { jobs = await (await fetch("/api/jobs")).json(); } catch { jobs = []; }
    let fresh = null;
    const first = seen === null;
    seen ??= new Set();
    for (const j of jobs) {
        if (j.state === "done" && !seen.has(j.id)) { seen.add(j.id); if (!first) fresh = j.result; }
    }
    renderJobs();
    if (fresh) { await loadGallery(); select(fresh.name); }
    const busy = jobs.some(j => j.state === "running" || j.state === "waiting");
    pollTimer = setTimeout(pollJobs, busy ? 1000 : 5000);
}

// ---------- the gallery and the chosen skybox ----------
let items = [], current = null;

async function loadGallery() {
    items = await (await fetch("/api/skyboxes")).json();
    const box = $("gallery");
    box.replaceChildren(...items.map(it => {
        const fig = document.createElement("figure");
        fig.dataset.name = it.name;
        fig.classList.toggle("on", current?.name === it.name);
        const img = document.createElement("img"); img.loading = "lazy"; img.alt = it.scene || it.name;
        img.src = `/output/thumbs/${it.name}.jpg`;
        const cap = document.createElement("figcaption");
        cap.textContent = (it.op && it.op !== "generate" ? `${t(`op.${it.op}`)} · ` : "") + (it.scene || it.name);
        fig.append(img, cap);
        fig.onclick = () => select(it.name);
        return fig;
    }));
}

async function select(name) {
    current = items.find(i => i.name === name) || null;
    document.querySelectorAll(".gallery figure").forEach(f => f.classList.toggle("on", f.dataset.name === name));
    renderDetails();
    if (!current) return;
    $("empty").hidden = true;
    await viewer.load(`/output/${current.name}.png`);
}

function renderDetails() {
    const d = $("details");
    d.hidden = !current;
    if (!current) return;
    $("d-scene").textContent = current.scene || current.name;
    const bits = [`${current.width} × ${current.height}`, `${t("form.seed")} ${current.seed ?? "-"}`, t(`style.${current.style || "photo"}`)];
    if (current.seam !== undefined) bits.push(t(current.seam > 12 ? "seam.bad" : "seam.good", { n: current.seam }));
    $("d-meta").textContent = bits.join(" · ");
    const base = `/api/skyboxes/${current.name}`;
    $("a-pano").href = `${base}/panorama.png`;
    $("a-cube").href = `${base}/cubemap.zip`;
    $("a-cross").href = `${base}/cross.png`;
    $("a-up4").disabled = current.width > 2048;
    $("a-up2").disabled = current.width > 4096;
}

$("a-seam").onclick = () => current && post(`/api/skyboxes/${current.name}/seam`).then(pollJobs);
$("a-up2").onclick = () => current && post(`/api/skyboxes/${current.name}/upscale`, { scale: 2 }).then(pollJobs);
$("a-up4").onclick = () => current && post(`/api/skyboxes/${current.name}/upscale`, { scale: 4 }).then(pollJobs);
$("a-again").onclick = () => {
    if (!current) return;
    $("scene").value = current.scene || "";
    $("style").value = current.style || "photo";
    $("seed").value = "";
    generate(current.scene || "");
};
$("a-delete").onclick = async () => {
    if (!current || !confirm(t("act.delete_ask"))) return;
    await fetch(`/api/skyboxes/${current.name}`, { method: "DELETE" });
    current = null;
    renderDetails();
    await loadGallery();
    if (items.length) select(items[0].name); else { viewer.ready = false; $("empty").hidden = false; }
};

// ---------- open on a phone ----------
$("phone").onclick = async () => {
    const p = await (await fetch("/api/phone")).json();
    $("qr").innerHTML = p.svg || "";
    $("phone-url").textContent = p.url || t("phone.local_only");
    $("phone-box").showModal();
};

// ---------- start ----------
const langSel = $("lang");
for (const [code, name] of Object.entries(LANGS)) langSel.add(new Option(name, code));
const saved = localStorage.getItem("firmament.lang");
const start = saved || Object.keys(LANGS).find(c => navigator.language?.toLowerCase().startsWith(c)) || "en";
langSel.value = start;
langSel.onchange = () => loadLang(langSel.value);
await loadLang(start);
await loadGallery();
if (items.length) select(items[0].name);
pollJobs();
