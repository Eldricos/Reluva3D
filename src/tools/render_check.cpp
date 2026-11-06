// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-render-check: draws a test frame through the 3D renderer (the
// SDL3 GPU backend: Vulkan, or Metal on macOS) and compares it with a
// reference image, for checking a backend on a machine nobody plays on
// (continuous integration).
//
// The frame is a grid of 80x80 cells, each trying one feature: textures and
// their filtering, vertex colours, blending, the alpha test, depth, culling,
// lighting, mipmaps, texture coordinate rows, the stencil and vertex
// deforms; a second frame tries the brightness pass. Each has probes: small
// blocks whose average colour must be within a tolerance of the
// reference's. The whole frame must also match, in 4x4 blocks, but for a
// few (edges rasterize a little differently from one GPU to another).
//
//   reluva-render-check <reference folder> [--out <folder>] [--update]
//
// --out writes the frames drawn (and, if they differ, how) as TGA files;
// --update writes them into the reference folder instead of comparing.

#include "backends/sdl3/sdl_system.h"
#include "common/gfx3d.h"
#include "common/image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using namespace reluva;
using image::Image;
using image::decode;
using image::encodeTga;

constexpr int kWidth = 320, kHeight = 240, kCell = 80;
constexpr Color kClear{16, 16, 16};

// Pixels to clip space: x right, y down, z (0 near, 1 far) as given.
gfx::Mat4 pixelProjection() {
    gfx::Mat4 m{};
    m[0] = 2.0f / kWidth;
    m[5] = -2.0f / kHeight;
    m[10] = 1;
    m[12] = -1;
    m[13] = 1;
    m[15] = 1;
    return m;
}

struct Rgba {
    uint8_t r, g, b, a;
};

// A probe: the average of the 6x6 block around (x, y), and how far each
// channel may stray from the reference's.
struct Probe {
    const char* what;
    int x, y;
    int tolerance = 12;
    bool needsStencil = false;
};

class Scene {
public:
    explicit Scene(gfx::Renderer3D& r) : r_(r) {}
    ~Scene() {
        for (gfx::MeshId m : meshes_)
            r_.destroyMesh(m);
        for (gfx::TextureId t : textures_)
            r_.destroyTexture(t);
    }

    gfx::TextureId texture(int w, int h, const std::vector<Rgba>& pixels, gfx::TextureParams params) {
        gfx::TextureId id = 0;
        if (Error err = r_.createTexture(w, h, reinterpret_cast<const uint8_t*>(pixels.data()), params, id))
            std::fprintf(stderr, "createTexture: %s\n", err.message.c_str());
        textures_.push_back(id);
        return id;
    }

    // A rectangle in pixels at depth z, its corners' colours (top left,
    // top right, bottom right, bottom left), texture coordinates (u0, v0)
    // to (u1, v1); `flip` winds it the other way round.
    gfx::MeshId quad(float x0, float y0, float x1, float y1, float z, const std::array<Rgba, 4>& colors,
                     float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1, bool flip = false,
                     gfx::Vec3 normal = {0, 0, 1}) {
        const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
        const float us[4] = {u0, u1, u1, u0}, vs[4] = {v0, v0, v1, v1};
        std::vector<gfx::Vertex> v(4);
        for (int i = 0; i < 4; ++i) {
            v[size_t(i)] = {{xs[i], ys[i], z}, {us[i], vs[i]}, {us[i], vs[i]}, {normal[0], normal[1], normal[2]},
                            {colors[size_t(i)].r, colors[size_t(i)].g, colors[size_t(i)].b, colors[size_t(i)].a}};
        }
        std::vector<uint32_t> idx = flip ? std::vector<uint32_t>{0, 2, 1, 0, 3, 2} : std::vector<uint32_t>{0, 1, 2, 0, 2, 3};
        return mesh(v, idx);
    }
    gfx::MeshId quad(float x0, float y0, float x1, float y1, float z, Rgba color) {
        return quad(x0, y0, x1, y1, z, {color, color, color, color});
    }
    gfx::MeshId triangle(gfx::Vec3 a, gfx::Vec3 b, gfx::Vec3 c, Rgba color) {
        std::vector<gfx::Vertex> v(3);
        const gfx::Vec3 p[3] = {a, b, c};
        for (int i = 0; i < 3; ++i)
            v[size_t(i)] = {{p[i][0], p[i][1], p[i][2]}, {0, 0}, {0, 0}, {0, 0, 1}, {color.r, color.g, color.b, color.a}};
        return mesh(v, {0, 1, 2});
    }

    gfx::MeshId mesh(const std::vector<gfx::Vertex>& v, const std::vector<uint32_t>& idx) {
        gfx::MeshId id = 0;
        if (Error err = r_.createMesh(v, idx, id))
            std::fprintf(stderr, "createMesh: %s\n", err.message.c_str());
        meshes_.push_back(id);
        return id;
    }

    // A draw of a whole mesh: vertex colours unless told otherwise, opaque,
    // no culling, depth tested and written.
    static gfx::DrawCall call(gfx::MeshId mesh, uint32_t indices = 6) {
        gfx::DrawCall c;
        c.mesh = mesh;
        c.indexCount = indices;
        c.rgbSource = c.alphaSource = gfx::ColorSource::Vertex;
        c.state.cull = gfx::Cull::None;
        return c;
    }

private:
    gfx::Renderer3D& r_;
    std::vector<gfx::MeshId> meshes_;
    std::vector<gfx::TextureId> textures_;
};

constexpr Rgba kRed{255, 0, 0, 255}, kGreen{0, 255, 0, 255}, kBlue{0, 0, 255, 255}, kWhite{255, 255, 255, 255},
    kGrey{64, 64, 64, 255}, kMagenta{255, 0, 255, 255}, kBlack{0, 0, 0, 255};

// The cell at column c, row r: its top left corner.
float cx(int c) { return float(c * kCell); }
float cy(int r) { return float(r * kCell); }

std::vector<Probe> probes() {
    auto at = [](int c, int r, int x, int y) { return std::pair{c * kCell + x, r * kCell + y}; };
    std::vector<Probe> out;
    auto add = [&](const char* what, std::pair<int, int> p, int tolerance = 12, bool stencil = false) {
        out.push_back({what, p.first, p.second, tolerance, stencil});
    };
    add("texture, nearest: top left (red)", at(0, 0, 20, 20));
    add("texture, nearest: top right (green)", at(0, 0, 60, 20));
    add("texture, nearest: bottom left (blue)", at(0, 0, 20, 60));
    add("texture, nearest: bottom right (white)", at(0, 0, 60, 60));
    add("vertex colours: centre", at(1, 0, 40, 40), 16);
    add("vertex colours: near top left", at(1, 0, 10, 10), 16);
    add("additive blend: added", at(2, 0, 20, 40));
    add("additive blend: alone", at(2, 0, 60, 40));
    add("alpha blend: half white over blue", at(3, 0, 40, 40));
    add("alpha test: kept (yellow)", at(0, 1, 20, 40));
    add("alpha test: discarded (magenta under)", at(0, 1, 60, 40));
    add("depth: nearer drawn first stays", at(1, 1, 20, 20));
    add("depth: farther where nothing nearer", at(1, 1, 60, 20));
    add("depth equal: pass over the nearer", at(1, 1, 20, 60));
    add("depth equal: not over the farther", at(1, 1, 60, 60));
    add("culling: left triangle (one winding)", at(2, 1, 12, 25));
    add("culling: right triangle (the other)", at(2, 1, 52, 25));
    add("lighting: facing the light", at(3, 1, 20, 40));
    add("lighting: facing away", at(3, 1, 60, 40));
    add("mipmaps: minified checker averages", at(0, 2, 40, 40), 24);
    add("texture rows: shifted half a texture", at(1, 2, 20, 20));
    add("stencil: drawn inside the mark", at(2, 2, 40, 40), 12, true);
    add("stencil: not outside it", at(2, 2, 8, 8), 12, true);
    add("deform move: moved right", at(3, 2, 50, 40));
    add("deform move: left behind", at(3, 2, 10, 40));
    return out;
}

void drawFrame(gfx::Renderer3D& r, Scene& s) {
    using gfx::Blend;
    const gfx::Mat4 proj = pixelProjection();
    r.beginFrame(proj, {0, 0, -1}, kClear);

    // (0,0) A 2x2 texture, nearest and clamped: four solid quarters.
    {
        gfx::TextureParams p;
        p.nearest = true;
        p.clamp = true;
        p.mipmaps = false;
        const gfx::TextureId t = s.texture(2, 2, {kRed, kGreen, kBlue, kWhite}, p);
        gfx::DrawCall c = Scene::call(s.quad(cx(0), cy(0), cx(1), cy(1), 0.5f, kWhite));
        c.texture = t;
        c.rgbSource = c.alphaSource = gfx::ColorSource::Constant;
        r.draw(c);
    }
    // (1,0) Vertex colours, blended across.
    r.draw(Scene::call(s.quad(cx(1), cy(0), cx(2), cy(1), 0.5f, {kRed, kGreen, kBlue, kWhite})));
    // (2,0) Additive: orange added over the left half of a grey square.
    {
        r.draw(Scene::call(s.quad(cx(2), cy(0), cx(3), cy(1), 0.5f, kGrey)));
        gfx::DrawCall c = Scene::call(s.quad(cx(2), cy(0), cx(2) + 40, cy(1), 0.4f, Rgba{128, 64, 0, 255}));
        c.state.src = Blend::One;
        c.state.dst = Blend::One;
        c.state.depthWrite = false;
        r.draw(c);
    }
    // (3,0) Alpha blending: white at half alpha (the constant's) over blue.
    {
        r.draw(Scene::call(s.quad(cx(3), cy(0), cx(4), cy(1), 0.5f, kBlue)));
        gfx::DrawCall c = Scene::call(s.quad(cx(3), cy(0), cx(4), cy(1), 0.4f, kWhite));
        c.rgbSource = c.alphaSource = gfx::ColorSource::Constant;
        c.color = {1, 1, 1, 0.5f};
        c.state.src = Blend::SrcAlpha;
        c.state.dst = Blend::OneMinusSrcAlpha;
        c.state.depthWrite = false;
        r.draw(c);
    }
    // (0,1) The alpha test: a texture opaque on the left, clear on the
    // right, over magenta; only alpha >= 128 is drawn.
    {
        r.draw(Scene::call(s.quad(cx(0), cy(1), cx(1), cy(2), 0.5f, kMagenta)));
        gfx::TextureParams p;
        p.nearest = true;
        p.clamp = true;
        p.mipmaps = false;
        const gfx::TextureId t = s.texture(2, 1, {Rgba{255, 255, 0, 255}, Rgba{0, 255, 255, 0}}, p);
        gfx::DrawCall c = Scene::call(s.quad(cx(0), cy(1), cx(1), cy(2), 0.4f, kWhite));
        c.texture = t;
        c.rgbSource = c.alphaSource = gfx::ColorSource::Constant;
        c.state.alphaTest = gfx::AlphaTest::Ge128;
        r.draw(c);
    }
    // (1,1) Depth: a nearer red left half drawn first, then a farther green
    // square; then blue added only where the depth equals the nearer's.
    {
        r.draw(Scene::call(s.quad(cx(1), cy(1), cx(1) + 40, cy(2), 0.2f, kRed)));
        r.draw(Scene::call(s.quad(cx(1), cy(1), cx(2), cy(2), 0.8f, kGreen)));
        gfx::DrawCall c = Scene::call(s.quad(cx(1), cy(1) + 40, cx(2), cy(2), 0.2f, kBlue));
        c.state.depthEqual = true;
        c.state.depthWrite = false;
        c.state.src = Blend::One;
        c.state.dst = Blend::One;
        r.draw(c);
    }
    // (2,1) Culling of back faces: the same triangles wound both ways.
    {
        const float x = cx(2), y = cy(1);
        gfx::DrawCall a = Scene::call(s.triangle({x + 4, y + 10, 0.5f}, {x + 36, y + 10, 0.5f}, {x + 4, y + 70, 0.5f}, kGreen), 3);
        gfx::DrawCall b = Scene::call(s.triangle({x + 44, y + 10, 0.5f}, {x + 44, y + 70, 0.5f}, {x + 76, y + 10, 0.5f}, kRed), 3);
        a.state.cull = b.state.cull = gfx::Cull::Back;
        r.draw(a);
        r.draw(b);
    }
    // (3,1) Lighting: ambient 0.2 and 0.6 from the light, the left half
    // facing it, the right half away.
    {
        for (int half = 0; half < 2; ++half) {
            const gfx::Vec3 normal = half == 0 ? gfx::Vec3{0, 0, 1} : gfx::Vec3{0, 0, -1};
            gfx::DrawCall c = Scene::call(s.quad(cx(3) + 40.0f * float(half), cy(1), cx(3) + 40.0f * float(half + 1), cy(2),
                                                 0.5f, {kWhite, kWhite, kWhite, kWhite}, 0, 0, 1, 1, false, normal));
            c.rgbSource = gfx::ColorSource::Lighting;
            c.alphaSource = gfx::ColorSource::Constant;
            c.ambientLight = {0.2f, 0.2f, 0.2f};
            c.directedLight = {0.6f, 0.6f, 0.6f};
            c.lightDirection = {0, 0, 1};
            r.draw(c);
        }
    }
    // (0,2) Mipmaps: a black and white checker of 8-pixel squares repeated
    // eight times over the cell, so the smaller levels average it grey.
    {
        std::vector<Rgba> checker(64 * 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                checker[size_t(y * 64 + x)] = ((x / 8) + (y / 8)) % 2 ? kWhite : kBlack;
        const gfx::TextureId t = s.texture(64, 64, checker, gfx::TextureParams{});
        gfx::DrawCall c = Scene::call(s.quad(cx(0), cy(2), cx(1), cy(3), 0.5f, {kWhite, kWhite, kWhite, kWhite}, 0, 0, 8, 8));
        c.texture = t;
        c.rgbSource = c.alphaSource = gfx::ColorSource::Constant;
        r.draw(c);
    }
    // (1,2) Texture coordinate rows: the four-colour texture shifted half
    // its width, so its right column (green over white) fills the left.
    {
        gfx::TextureParams p;
        p.nearest = true;
        p.clamp = true;
        p.mipmaps = false;
        const gfx::TextureId t = s.texture(2, 2, {kRed, kGreen, kBlue, kWhite}, p);
        gfx::DrawCall c = Scene::call(s.quad(cx(1), cy(2), cx(2), cy(3), 0.5f, kWhite));
        c.texture = t;
        c.rgbSource = c.alphaSource = gfx::ColorSource::Constant;
        c.postRow0 = {1, 0, 0.5f};
        r.draw(c);
    }
    // (2,2) The stencil: the middle marked (drawn red), then green drawn
    // over the whole cell only inside the mark.
    if (r.hasStencil()) {
        r.setStencilMode(gfx::Renderer3D::StencilMode::Mark);
        r.draw(Scene::call(s.quad(cx(2) + 20, cy(2) + 20, cx(2) + 60, cy(2) + 60, 0.5f, kRed)));
        r.setStencilMode(gfx::Renderer3D::StencilMode::Inside);
        gfx::DrawCall c = Scene::call(s.quad(cx(2), cy(2), cx(3), cy(3), 0.1f, kGreen));
        r.draw(c);
        r.setStencilMode(gfx::Renderer3D::StencilMode::None);
    }
    // (3,2) A vertex deform: a quad over the left 30 pixels moved 30 to the
    // right (by a constant wave).
    {
        gfx::DrawCall c = Scene::call(s.quad(cx(3), cy(2), cx(3) + 30, cy(3), 0.5f, kBlue));
        c.deforms[0].type = gfx::Deform::Type::Move;
        c.deforms[0].vector = {30, 0, 0};
        c.deforms[0].wave = {gfx::WaveFunc::Sin, 1, 0, 0, 0};
        r.draw(c);
    }
}

// The brightness pass: a frame of mid grey shown at gamma 2.
void drawGammaFrame(gfx::Renderer3D& r, Scene& s) {
    r.beginFrame(pixelProjection(), {0, 0, -1}, kClear);
    r.setGamma(2);
    r.draw(Scene::call(s.quad(0, 0, kWidth, kHeight, 0.5f, Rgba{64, 128, 192, 255})));
}

std::vector<Probe> gammaProbes() { return {{"gamma 2: colours raised to 1/2", kWidth / 2, kHeight / 2, 12, false}}; }

bool capture(SdlSystem& system, gfx::Renderer3D& r, Scene& s, void (*draw)(gfx::Renderer3D&, Scene&), Image& out) {
    r.captureNextFrame();
    draw(r, s);
    r.endFrame(nullptr);
    std::vector<uint8_t> rgba;
    if (!r.takeCapture(rgba) || rgba.size() != size_t(kWidth) * kHeight * 4)
        return false;
    out.w = kWidth;
    out.h = kHeight;
    out.rgba = std::move(rgba);
    for (size_t i = 3; i < out.rgba.size(); i += 4)
        out.rgba[i] = 255;
    r.setGamma(1);
    Event ev;
    while (system.pollEvent(ev)) {
    }
    return true;
}

std::array<int, 3> average(const Image& img, int x, int y) {
    std::array<int, 3> sum{};
    int n = 0;
    for (int dy = -3; dy < 3; ++dy)
        for (int dx = -3; dx < 3; ++dx) {
            const int px = std::clamp(x + dx, 0, img.w - 1), py = std::clamp(y + dy, 0, img.h - 1);
            const uint8_t* p = img.pixel(px, py);
            for (int c = 0; c < 3; ++c)
                sum[size_t(c)] += p[c];
            ++n;
        }
    for (int& v : sum)
        v /= n;
    return sum;
}

bool writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(out);
}

bool readImage(const std::filesystem::path& path, Image& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return !decode(data, out) && out.w == kWidth && out.h == kHeight;
}

// The frame against its reference: each probe, then the whole frame. True
// if it passes.
bool compare(const char* name, const Image& got, const Image& want, const std::vector<Probe>& list, bool stencil,
             const std::filesystem::path& outDir) {
    bool ok = true;
    for (const Probe& p : list) {
        if (p.needsStencil && !stencil) {
            std::printf("  skip  %-48s (no stencil buffer)\n", p.what);
            continue;
        }
        const auto g = average(got, p.x, p.y), w = average(want, p.x, p.y);
        int worst = 0;
        for (int c = 0; c < 3; ++c)
            worst = std::max(worst, std::abs(g[size_t(c)] - w[size_t(c)]));
        const bool pass = worst <= p.tolerance;
        ok = ok && pass;
        std::printf("  %s  %-48s got %3d %3d %3d, want %3d %3d %3d\n", pass ? "ok  " : "FAIL", p.what, g[0], g[1], g[2],
                    w[0], w[1], w[2]);
    }
    // The whole frame, in 4x4 blocks (their averages, so fine detail a
    // GPU filters its own way, as the minified checker, doesn't count):
    // blocks off by more than 48 in a channel, at most 2% of them (edges
    // differ a little between GPUs). The diff image shows each pixel's.
    Image diff;
    diff.w = kWidth;
    diff.h = kHeight;
    diff.rgba.assign(got.rgba.size(), 0);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const uint8_t *g = got.pixel(x, y), *w = want.pixel(x, y);
            int worst = 0;
            for (int c = 0; c < 3; ++c)
                worst = std::max(worst, std::abs(int(g[c]) - int(w[c])));
            uint8_t* d = diff.pixel(x, y);
            d[0] = d[1] = d[2] = uint8_t(std::min(255, worst * 4));
            d[3] = 255;
        }
    constexpr int kBlock = 4;
    int off = 0, blocks = 0;
    for (int by = 0; by < kHeight; by += kBlock)
        for (int bx = 0; bx < kWidth; bx += kBlock) {
            int worst = 0;
            for (int c = 0; c < 3; ++c) {
                int sg = 0, sw = 0;
                for (int y = by; y < by + kBlock; ++y)
                    for (int x = bx; x < bx + kBlock; ++x) {
                        sg += got.pixel(x, y)[c];
                        sw += want.pixel(x, y)[c];
                    }
                worst = std::max(worst, std::abs(sg - sw) / (kBlock * kBlock));
            }
            off += worst > 48;
            ++blocks;
        }
    const double share = double(off) / blocks;
    const bool frameOk = share <= 0.02;
    ok = ok && frameOk;
    std::printf("  %s  %-48s %d of %d 4x4 blocks (%.2f%%) differ by more than 48\n", frameOk ? "ok  " : "FAIL",
                "whole frame", off, blocks, share * 100);
    if (!outDir.empty() && off > 0)
        writeFile(outDir / (std::string(name) + "-diff.tga"), encodeTga(diff));
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path refDir, outDir;
    bool update = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc)
            outDir = argv[++i];
        else if (a == "--update")
            update = true;
        else if (refDir.empty())
            refDir = a;
        else {
            std::fprintf(stderr, "unknown argument %s\n", a.c_str());
            return 2;
        }
    }
    if (refDir.empty()) {
        std::fprintf(stderr, "usage: reluva-render-check <reference folder> [--out <folder>] [--update]\n");
        return 2;
    }

    SdlSystem system;
    if (Error err = system.init()) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    gfx::Renderer3D* r = nullptr;
    if (Error err = system.init3D(kWidth, kHeight, r)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::printf("Renderer: %s, stencil: %s\n", r->deviceName().c_str(), r->hasStencil() ? "yes" : "no");

    struct Frame {
        const char* name;
        void (*draw)(gfx::Renderer3D&, Scene&);
        std::vector<Probe> probes;
    };
    const Frame frames[] = {{"render-check", drawFrame, probes()}, {"render-check-gamma", drawGammaFrame, gammaProbes()}};
    bool ok = true;
    for (const Frame& f : frames) {
        Image got;
        {
            Scene scene(*r);
            if (!capture(system, *r, scene, f.draw, got)) {
                std::fprintf(stderr, "%s: the frame couldn't be read back\n", f.name);
                return 1;
            }
        }
        if (!outDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(outDir, ec);
            writeFile(outDir / (std::string(f.name) + ".tga"), encodeTga(got));
        }
        const std::filesystem::path ref = refDir / (std::string(f.name) + ".tga");
        if (update) {
            std::error_code ec;
            std::filesystem::create_directories(refDir, ec);
            if (!writeFile(ref, encodeTga(got))) {
                std::fprintf(stderr, "couldn't write %s\n", ref.string().c_str());
                return 1;
            }
            std::printf("%s: reference written\n", f.name);
            continue;
        }
        Image want;
        if (!readImage(ref, want)) {
            std::fprintf(stderr, "%s: no reference image %s (make one with --update)\n", f.name, ref.string().c_str());
            return 1;
        }
        std::printf("%s:\n", f.name);
        ok = compare(f.name, got, want, f.probes, r->hasStencil(), outDir) && ok;
    }
    std::printf(update ? "References written.\n" : ok ? "All checks passed.\n" : "Some checks failed.\n");
    return ok || update ? 0 : 1;
}
