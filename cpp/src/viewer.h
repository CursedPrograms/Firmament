// Firmament - the 360° view: the panorama round you, drag to look, wheel to zoom, Turn to spin, Flat for the map.
#pragma once
#include "engine.h"

namespace Viewer {
bool init();                                   // after the GL context exists
void setImage(const Image* im);                // shown until replaced (copied to the graphics card)
struct Look { float yaw = 0, pitch = 0, fov = 90; bool flat = false, turn = false; };
Look& look();
unsigned render(int w, int h, bool hovered);   // into its own texture; returns it for ImGui::Image
void readPixels(std::vector<unsigned char>& rgba, int& w, int& h);
}
