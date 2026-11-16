// Firmament - the 360° view (see viewer.h): one quad, a shader that turns each pixel into a direction and reads the
// panorama there (the same convention as pano.h). OpenGL 3.3 core, its functions fetched through SDL.
#include "viewer.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef APIENTRY
#define APIENTRY
#endif
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GLF(ret, name, args) typedef ret(APIENTRY* PFN_##name) args; static PFN_##name name##_ = nullptr;
GLF(GLuint, glCreateShader, (GLenum))
GLF(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))
GLF(void, glCompileShader, (GLuint))
GLF(GLuint, glCreateProgram, (void))
GLF(void, glAttachShader, (GLuint, GLuint))
GLF(void, glLinkProgram, (GLuint))
GLF(void, glUseProgram, (GLuint))
GLF(GLint, glGetUniformLocation, (GLuint, const GLchar*))
GLF(void, glUniform1i, (GLint, GLint))
GLF(void, glUniform1f, (GLint, GLfloat))
GLF(void, glUniform2f, (GLint, GLfloat, GLfloat))
GLF(void, glGenVertexArrays, (GLsizei, GLuint*))
GLF(void, glBindVertexArray, (GLuint))
GLF(void, glGenFramebuffers, (GLsizei, GLuint*))
GLF(void, glBindFramebuffer, (GLenum, GLuint))
GLF(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
GLF(void, glGenerateMipmap, (GLenum))
GLF(void, glActiveTexture, (GLenum))
#undef GLF
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_TEXTURE0 0x84C0
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_LINEAR_MIPMAP_LINEAR
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#endif

namespace Viewer {

static Look lk;
Look& look() { return lk; }
static GLuint prog = 0, vao = 0, tex = 0, fbo = 0, fboTex = 0, beforeTex = 0;
static int fbW = 0, fbH = 0, imW = 0, imH = 0;
static bool hasBefore = false, draggingWipe = false;
static void upload(GLuint& t, const Image* im) {
    if (!t) glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, im->w, im->h, 0, GL_RGB, GL_UNSIGNED_BYTE, im->px.data());
    glGenerateMipmap_(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);          // round the back, no seam
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
void setBefore(const Image* im) { hasBefore = im && !im->empty(); if (hasBefore) upload(beforeTex, im); }
bool comparing() { return hasBefore && imW; }

bool init() {
#define L(name) name##_ = (PFN_##name)SDL_GL_GetProcAddress(#name); if (!name##_) return false;
    L(glCreateShader) L(glShaderSource) L(glCompileShader) L(glCreateProgram) L(glAttachShader) L(glLinkProgram) L(glUseProgram)
    L(glGetUniformLocation) L(glUniform1i) L(glUniform1f) L(glUniform2f) L(glGenVertexArrays) L(glBindVertexArray)
    L(glGenFramebuffers) L(glBindFramebuffer) L(glFramebufferTexture2D) L(glGenerateMipmap) L(glActiveTexture)
#undef L
    const char* vs = R"(#version 330
out vec2 q;
void main() { vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0); q = p; gl_Position = vec4(p, 0, 1); })";
    const char* fs = R"(#version 330
in vec2 q; out vec4 o;
uniform sampler2D pano, before; uniform float yaw, pitch, fov, aspect, wipe, widthPx; uniform int flatMode, useBefore;
const float PI = 3.14159265;
void main() {
  vec2 uv;
  if (flatMode == 1) {
    // the whole map, letterboxed to 2:1
    vec2 s = q * 0.5 + 0.5;
    float a = aspect / 2.0;
    if (a > 1.0) s.x = (s.x - 0.5) * a + 0.5; else s.y = (s.y - 0.5) / a + 0.5;
    if (s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0) { o = vec4(0.08, 0.08, 0.08, 1); return; }
    uv = vec2(s.x, 1.0 - s.y);
  } else {
    float t = tan(radians(fov) * 0.5);
    vec3 d = normalize(vec3(q.x * t * aspect, q.y * t, -1.0));
    float cp = cos(pitch), sp = sin(pitch);
    d = vec3(d.x, d.y * cp + d.z * sp, -d.y * sp + d.z * cp);    // look up / down
    float cy = cos(yaw), sy = sin(yaw);
    d = vec3(d.x * cy + d.z * sy, d.y, -d.x * sy + d.z * cy);   // look round
    float lon = atan(d.x, -d.z), lat = asin(clamp(d.y, -1.0, 1.0));
    uv = vec2(0.5 + lon / (2.0 * PI), 0.5 - lat / PI);
  }
  vec2 gx = dFdx(uv), gy = dFdy(uv);
  gx.x -= round(gx.x); gy.x -= round(gy.x);   // no blurred line where u jumps from 1 back to 0
  // the wipe: before left of the line, after right of it (the same place in both: one look round, two skies)
  float sx = q.x * 0.5 + 0.5;
  bool b = useBefore == 2 || (useBefore == 1 && sx < wipe);
  o = vec4((b ? textureGrad(before, uv, gx, gy) : textureGrad(pano, uv, gx, gy)).rgb, 1);
  if (useBefore == 1 && abs(sx - wipe) * widthPx < 1.0) o = vec4(1.0, 0.36, 0.67, 1);

})";
    auto sh = [](GLenum t, const char* src) { GLuint s = glCreateShader_(t); glShaderSource_(s, 1, &src, nullptr); glCompileShader_(s); return s; };
    prog = glCreateProgram_();
    glAttachShader_(prog, sh(GL_VERTEX_SHADER, vs)); glAttachShader_(prog, sh(GL_FRAGMENT_SHADER, fs));
    glLinkProgram_(prog);
    glGenVertexArrays_(1, &vao);
    return prog != 0;
}

void setImage(const Image* im) {
    if (!im || im->empty()) { imW = imH = 0; return; }
    upload(tex, im);
    imW = im->w; imH = im->h;
}

unsigned render(int w, int h, bool hovered) {
    if (w < 8 || h < 8) return fboTex;
    ImGuiIO& io = ImGui::GetIO();
    // the wipe's line: a drag that starts on it moves it (on the flat map, a drag anywhere does); else the drag looks round
    const bool wiping = comparing() && lk.compare == 0;
    const float x0 = ImGui::GetItemRectMin().x;   // (the view's invisible button, just before this)
    if (wiping && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        draggingWipe = lk.flat || std::fabs(io.MousePos.x - (x0 + lk.wipe * w)) < 14;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !wiping) draggingWipe = false;
    if (draggingWipe) lk.wipe = std::clamp((io.MousePos.x - x0) / w, 0.0f, 1.0f);
    if (wiping && hovered && std::fabs(io.MousePos.x - (x0 + lk.wipe * w)) < 14) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (hovered && !lk.flat && !draggingWipe) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const float k = lk.fov / 90.0f * 0.005f;
            lk.yaw += io.MouseDelta.x * k; lk.pitch = std::clamp(lk.pitch + io.MouseDelta.y * k, -1.5f, 1.5f);
        }
        if (io.MouseWheel != 0) lk.fov = std::clamp(lk.fov * std::pow(0.9f, io.MouseWheel), 20.0f, 140.0f);
    }
    if (lk.turn && !lk.flat) lk.yaw += io.DeltaTime * 0.08f;
    if (w != fbW || h != fbH) {
        if (!fbo) { glGenFramebuffers_(1, &fbo); glGenTextures(1, &fboTex); }
        glBindTexture(GL_TEXTURE_2D, fboTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
        fbW = w; fbH = h;
    }
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glClearColor(0x14 / 255.0f, 0x14 / 255.0f, 0x14 / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (imW) {
        glUseProgram_(prog);
        glActiveTexture_(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i_(glGetUniformLocation_(prog, "pano"), 0);
        glActiveTexture_(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, comparing() ? beforeTex : tex);
        glUniform1i_(glGetUniformLocation_(prog, "before"), 1);
        glActiveTexture_(GL_TEXTURE0);
        glUniform1i_(glGetUniformLocation_(prog, "useBefore"), comparing() ? (lk.compare == 0 ? 1 : lk.compare == 2 ? 2 : 0) : 0);
        glUniform1f_(glGetUniformLocation_(prog, "wipe"), lk.wipe);
        glUniform1f_(glGetUniformLocation_(prog, "widthPx"), (float)w);
        glUniform1f_(glGetUniformLocation_(prog, "yaw"), lk.yaw);
        glUniform1f_(glGetUniformLocation_(prog, "pitch"), lk.pitch);
        glUniform1f_(glGetUniformLocation_(prog, "fov"), lk.fov);
        glUniform1f_(glGetUniformLocation_(prog, "aspect"), (float)w / h);
        glUniform1i_(glGetUniformLocation_(prog, "flatMode"), lk.flat ? 1 : 0);
        glBindVertexArray_(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray_(0);
        glUseProgram_(0);
    }
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    return fboTex;
}

void readPixels(std::vector<unsigned char>& rgba, int& w, int& h) {
    w = fbW; h = fbH;
    std::vector<unsigned char> raw((size_t)w * h * 4);
    rgba.resize(raw.size());
    glBindFramebuffer_(GL_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data());
    glBindFramebuffer_(GL_FRAMEBUFFER, 0);
    for (int y = 0; y < h; ++y) memcpy(&rgba[(size_t)y * w * 4], &raw[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
}

}
