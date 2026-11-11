// Firmament's page: a skybox Firmament made, in the same 360 view the app uses, and the year.
import { Viewer } from "./viewer.js";

const viewer = new Viewer(document.getElementById("sky"));
if (viewer.error) {
  document.querySelector(".hint").textContent = viewer.error;
} else {
  viewer.spin = true;                       // turns slowly until someone drags it
  viewer.load("images/sample.jpg");
}
const year = document.getElementById("year");
if (year) year.textContent = String(Math.max(2026, new Date().getFullYear()));
