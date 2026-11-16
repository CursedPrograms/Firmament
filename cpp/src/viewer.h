// Firmament - the 360° view: the panorama round you, drag to look, wheel to zoom, Turn to spin, Flat for the map.
#pragma once
#include "engine.h"

namespace Viewer {
bool init();                                   // after the GL context exists
void setImage(const Image* im);                // shown until replaced (copied to the graphics card)
void setBefore(const Image* im);               // the sky this one was made from (seam repair, upscale); null: none
bool comparing();                              // there's a before to compare with
// compare: 0 the wipe (the sky it was made from left of the line, this one right), 1 after only, 2 before only
struct Look { float yaw = 0, pitch = 0, fov = 90; bool flat = false, turn = false; float wipe = 0.5f; int compare = 0; };
Look& look();
unsigned render(int w, int h, bool hovered);   // into its own texture; returns it for ImGui::Image
void readPixels(std::vector<unsigned char>& rgba, int& w, int& h);
}
