// Firmament - the panorama maths, ported from firmament/skybox.py: equirectangular panoramas (2:1), their seam, wrap
// padding and cube maps. The convention (the viewer uses the same): the picture's centre looks along -Z, x grows to
// the right, y is up. longitude = atan2(x, -z), u = 0.5 + longitude / 2pi; latitude = asin(y), v = 0.5 - latitude / pi.
#pragma once
#include "engine.h"

#include <map>
#include <string>

namespace Pano {

constexpr int SEAM_STRIP = 1024, SEAM_BAND = 192, SEAM_FEATHER = 48;

Image halfTurn(const Image& a);                              // the left/right seam to the middle
// the strip to repaint round the seam and its mask (white: repaint); x0: where the strip sits in the turned panorama
void seamStrip(const Image& a, int band, Image& strip, Image& mask, int& x0);
Image seamMerge(const Image& a, const Image& repainted, int x0, int band, int feather);
float seamError(const Image& a);                             // 0..255: how far the edges disagree (worth fixing above ~12)

Image wrapPad(const Image& a, int pad);                      // the other edge on each side (so an upscale wraps)
Image unpad(const Image& a, int pad);

Image resize(const Image& a, int w, int h);                  // bilinear (a box average when shrinking a lot)
Image sample(const Image& a, int size, int face);            // one cube face (0..5: px nx py ny pz nz), size x size
std::map<std::string, Image> cubeFaces(const Image& a, int size);
Image cross(const std::map<std::string, Image>& f);          // the horizontal cross, 4 x 3 faces

bool savePng(const Image& a, const std::string& path);
bool saveJpg(const Image& a, const std::string& path, int quality = 92);
bool load(const std::string& path, Image& a);
std::string pngBytes(const Image& a);
// a zip (stored, not compressed: PNGs are compressed already) of name -> bytes
bool saveZip(const std::string& path, const std::vector<std::pair<std::string, std::string>>& files);

}
