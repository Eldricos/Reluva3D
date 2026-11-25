// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/credits_screen.h"

#include "common/text.h"
#include "engines/storm3d/formats/sod.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace reluva::storm3d {

namespace {

// Fitted to the original's draws of pages at scales 0.8 and 0.5, all at
// one depth: pixels a font unit at scale 1, the letters and a space's
// width scaling with the page; the gap after each letter the same in
// pixels at both; the lines' pitch that gap and a scaled height; and how
// far below the screen's middle the block's middle baseline falls, in
// pixels and pixels a unit of scale.
constexpr float kPixelsPerUnit = 3.084f;
constexpr float kGapPixels = 7.19f;
constexpr float kSpace = 7.0f;
constexpr float kLineHeight = 10.755f;
constexpr float kBelowPixels = 0.67f;
constexpr float kBelowPerScale = 8.72f;
// The font model's baseline: its lower case letters' feet.
constexpr float kBaseline = 84.403f;
constexpr float kCentreX = 319.5f;
constexpr float kCentreY = 239.5f;
// A page's turn, as the original's frames time it: nothing for 3 s, a
// fade in over 1 s, the hold, then 3 s going away, the last second
// fading out.
constexpr double kBlank = 3;
constexpr double kFadeIn = 1;
constexpr double kAway = 3;
constexpr double kFadeOut = 1;
// The camera: at the origin looking along +z, 45 degrees high, as the
// loading screen's (the stars' background matches the frame so).
constexpr float kFieldOfView = 0.7853982f;
// The caption: a box, then the sprite font's letters, in yellow.
constexpr int kCaptionX = 5;
constexpr int kCaptionY = 5;
constexpr int kCaptionRight = 119;
constexpr int kCaptionBottom = 15;
constexpr uint8_t kBoxColour[3] = {0x50, 0x00, 0x30};
constexpr uint8_t kCaptionColour[3] = {255, 255, 0};
constexpr const char *kCaption = "Press any key to exit";

// D3D's screen coordinates (a pixel's centre at whole numbers) to clip space.
std::array<float, 2> clip(const std::array<float, 2> &p) {
    return {(p[0] + 0.5f) / 320.0f - 1.0f, 1.0f - (p[1] + 0.5f) / 240.0f};
}

} // namespace

std::vector<CreditsScreen::Placed> CreditsScreen::layout(const CreditsScript::Page &page,
                                                         const std::map<char, Glyph> &font) {
    const float k = kPixelsPerUnit * page.scale[0];
    const float pitch = kGapPixels + kLineHeight * k;
    const size_t n = page.lines.size();
    std::vector<Placed> placed;
    for (size_t i = 0; i < n; ++i) {
        const std::string &line = page.lines[i];
        float width = 0;
        for (char c : line) {
            auto g = font.find(c);
            width += (g != font.end() ? g->second.width : kSpace) * k + kGapPixels;
        }
        float pen = kCentreX - width / 2;
        const float baseline =
            kCentreY + (float(i) - float(n - 1) / 2) * pitch + kBelowPixels + kBelowPerScale * page.scale[0];
        for (char c : line) {
            auto g = font.find(c);
            if (g == font.end()) {
                pen += kSpace * k + kGapPixels;
                continue;
            }
            Placed p;
            for (const auto &corner : g->second.corners)
                p.corners.push_back({pen + (corner[0] - g->second.left) * k, baseline - (corner[1] - kBaseline) * k});
            placed.push_back(std::move(p));
            pen += g->second.width * k + kGapPixels;
        }
    }
    return placed;
}

std::array<float, 3> CreditsScreen::litColour(const std::array<float, 3> &colour) {
    // Fitted to the first page's vertices: (0, 0.3, 0.6) drawn as (128,
    // 168, 208), half grey and a little over half the material's colour.
    std::array<float, 3> out{};
    for (size_t i = 0; i < 3; ++i)
        out[i] = std::min(1.0f, 0.502f + 0.5255f * colour[i]);
    return out;
}

CreditsScreen::Moment CreditsScreen::momentAt(const CreditsScript::Page &page, double seconds) {
    Moment m;
    const double in = seconds - kBlank;
    const double away = in - kFadeIn - page.hold;
    m.over = away >= kAway;
    if (in < 0)
        return m;
    m.alpha = float(std::min(1.0, in / kFadeIn));
    if (away > 0) {
        // Going away at a steady speed: as far again every second, so its
        // size on the screen is 1 / (1 + seconds).
        m.scale = float(1 / (1 + away));
        m.alpha = float(std::clamp((kAway - away) / kFadeOut, 0.0, 1.0));
    }
    return m;
}

Error CreditsScreen::start(gfx::Renderer3D *renderer, uint64_t millis) {
    stop();
    std::vector<uint8_t> data;
    if (Error err = _files.read("Credits.txt", data))
        return err;
    _script = CreditsScript::parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()));
    SodModel font;
    if (Error err = _files.read("SOD/font.sod", data))
        return err;
    if (Error err = font.parse(data))
        return Error::fail("font.sod: " + err.message);
    // Each letter is a node named by its character's code ("65" for A).
    _font.clear();
    for (const SodModel::Node &node : font.nodes) {
        const int code = std::atoi(node.name.c_str());
        if (node.mesh < 0 || code <= 32 || code > 126)
            continue;
        const SodModel::Mesh &mesh = font.meshes[size_t(node.mesh)];
        Glyph g;
        float right = -1e9f;
        g.left = 1e9f;
        for (const SodModel::Group &group : mesh.groups)
            for (const SodModel::Face &face : group.faces)
                for (uint16_t v : face.vertex) {
                    const auto &p = mesh.vertices[v];
                    g.corners.push_back({p[0], p[1]});
                    g.left = std::min(g.left, p[0]);
                    right = std::max(right, p[0]);
                }
        g.width = right - g.left;
        _font[char(code)] = std::move(g);
    }
    if (_caption.load(_files))
        _caption = SpriteFont{};
    _renderer = renderer;
    _meshes.attach(renderer);
    _background = _meshes.model("mbgstars");
    _overlay.create(640, 480, PixelFormat::kRGBA32);
    _page = 0;
    _built = SIZE_MAX;
    _pageStart = millis;
    return Error::success();
}

void CreditsScreen::stop() {
    if (_renderer && _pageMesh)
        _renderer->destroyMesh(_pageMesh);
    _pageMesh = 0;
    _meshes.clear();
    _meshes.attach(nullptr);
    _renderer = nullptr;
    _background = nullptr;
}

void CreditsScreen::buildPage(size_t page) {
    if (_pageMesh)
        _renderer->destroyMesh(_pageMesh);
    _pageMesh = 0;
    _built = page;
    const CreditsScript::Page &p = _script.pages[page];
    const std::array<float, 3> colour = litColour(p.colour);
    std::vector<gfx::Vertex> vertices;
    std::vector<uint32_t> indices;
    for (const Placed &letter : layout(p, _font))
        for (const auto &corner : letter.corners) {
            gfx::Vertex v{};
            const auto c = clip(corner);
            v.pos[0] = c[0];
            v.pos[1] = c[1];
            v.pos[2] = 0.5f;
            for (size_t i = 0; i < 3; ++i)
                v.color[i] = uint8_t(std::lround(colour[i] * 255));
            v.color[3] = 255;
            indices.push_back(uint32_t(vertices.size()));
            vertices.push_back(v);
        }
    _pageIndices = uint32_t(indices.size());
    if (!vertices.empty() && _renderer->createMesh(vertices, indices, _pageMesh))
        _pageMesh = 0;
}

bool CreditsScreen::draw(uint64_t millis) {
    if (!_renderer || _page >= _script.pages.size())
        return false;
    Moment moment = momentAt(_script.pages[_page], double(millis - _pageStart) / 1000);
    if (moment.over) {
        _pageStart = millis;
        if (++_page >= _script.pages.size())
            return false;
        moment = Moment{};
    }
    if (_built != _page)
        buildPage(_page);
    const gfx::Mat4 viewProj = gfx::multiply(gfx::multiply(gfx::perspective(kFieldOfView, 640.0f / 480.0f, 1, 1000),
                                                           gfx::lookAlong({0, 0, 0}, {0, 0, -1}, {0, 1, 0})),
                                             SodMeshes::mirrorZ());
    _renderer->beginFrame(viewProj, {0, 0, 0}, Color{0, 0, 0});
    _meshes.setView({1, 0, 0}, {0, 1, 0}, double(millis) / 1000);
    if (_background)
        _meshes.draw(*_background, gfx::identity(), true);
    if (_pageMesh) {
        // The letters lie in the screen's own plane: placed already, not
        // through the camera.
        _renderer->setView(gfx::identity(), {0, 0, 0});
        gfx::DrawCall call;
        call.mesh = _pageMesh;
        call.indexCount = _pageIndices;
        // Going away: smaller about the screen's centre, where clip space's
        // origin is.
        call.model = gfx::identity();
        call.model[0] = call.model[5] = moment.scale;
        call.rgbSource = gfx::ColorSource::kVertex;
        call.color = {1, 1, 1, moment.alpha};
        call.state.cull = gfx::Cull::kNone;
        call.state.depthTest = false;
        call.state.depthWrite = false;
        call.state.src = gfx::Blend::kSrcAlpha;
        call.state.dst = gfx::Blend::kOneMinusSrcAlpha;
        _renderer->draw(call);
    }
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), uint8_t(0));
    for (int y = kCaptionY; y < kCaptionBottom; ++y)
        for (int x = kCaptionX; x < kCaptionRight; ++x) {
            uint8_t *p = _overlay.row(y) + size_t(x) * 4;
            p[0] = kBoxColour[0], p[1] = kBoxColour[1], p[2] = kBoxColour[2], p[3] = 255;
        }
    _caption.draw(_overlay, kCaptionX, kCaptionY, kCaption, {kCaptionColour[0], kCaptionColour[1], kCaptionColour[2]});
    _renderer->endFrame(&_overlay);
    return true;
}

} // namespace reluva::storm3d
