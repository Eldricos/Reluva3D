// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool storm3d viewer-shot <game folder> <out.png> [key...]: runs the
// data viewer on a game without a window, presses the keys in turn (each
// a tenth of a second after the last), and writes the last screen as PNG.
// Keys are a letter or digit, or tab, left, right, up, down, pageup,
// pagedown, home, end, space.

#include "common/headless_system.h"
#include "common/text.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/map_page.h"
#include "engines/storm3d/scene/map_scene.h"
#include "engines/viewer_registry.h"
#include "devtools/soft_renderer.h"
#include "devtools/tool.h"
#include "image/image.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace reluva;

// Presses the keys at set times, then Esc; keeps the last screen.
class ShotSystem : public HeadlessSystem {
public:
    std::vector<uint32_t> keys;
    Surface last;
    Surface shot;
    std::string title;

    // 3D pages are drawn in software, so their frames can be saved too.
    std::unique_ptr<tools::SoftRenderer3D> renderer;
    bool lastIn3D = false;

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        last = s;
    }
    Error initGraphics(int width, int height, PixelFormat format) override {
        lastIn3D = false;
        return HeadlessSystem::initGraphics(width, height, format);
    }
    Error init3D(int width, int height, gfx::Renderer3D *&out) override {
        if (!renderer || renderer->width() != width || renderer->height() != height)
            renderer = std::make_unique<tools::SoftRenderer3D>(width, height);
        out = renderer.get();
        lastIn3D = true;
        return Error::success();
    }
    void setWindowTitle(const std::string &t) override { title = t; }
    bool pollEvent(Event &ev) override {
        if (getMillis() < _nextAt)
            return false;
        _nextAt = getMillis() + 100;
        ev.type = EventType::kKeyDown;
        ev.key = _next < keys.size() ? keys[_next] : key::kEscape;
        if (_next == keys.size()) // the screen as it is, before Escape leaves it
            shot = lastIn3D && renderer ? renderer->frame() : last;
        ++_next;
        return true;
    }

private:
    size_t _next = 0;
    uint64_t _nextAt = 100;
};

int cmdViewerShot(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d viewer-shot <game folder> <out.png> [key...]\n");
        return 2;
    }
    const std::map<std::string, uint32_t> named = {
        {"tab", key::kTab},   {"left", key::kLeft},     {"right", key::kRight},       {"up", key::kUp},
        {"down", key::kDown}, {"pageup", key::kPageUp}, {"pagedown", key::kPageDown}, {"home", key::kHome},
        {"end", key::kEnd},   {"space", key::kSpace}};
    ShotSystem sys;
    for (int i = 4; i < argc; ++i) {
        if (const auto it = named.find(argv[i]); it != named.end())
            sys.keys.push_back(it->second);
        else if (std::strlen(argv[i]) == 1)
            sys.keys.push_back(uint32_t(argv[i][0]));
        else {
            std::fprintf(stderr, "no key %s\n", argv[i]);
            return 2;
        }
    }
    const MetaViewer *viewer = findViewer("storm3d");
    if (!viewer)
        return 1;
    GameTarget target{"armada", "storm3d", "armada", "Star Trek: Armada", argv[2], {}, {}};
    if (Error err = viewer->create(sys, target)->run()) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const Surface &shown = sys.shot.pixels.empty() ? sys.last : sys.shot;
    image::Image img;
    img.w = shown.w;
    img.h = shown.h;
    img.rgba = shown.pixels;
    const std::vector<uint8_t> png = image::encodePng(img);
    std::ofstream out(argv[3], std::ios::binary);
    out.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
    std::printf("%s\n", sys.title.c_str());
    return out ? 0 : 1;
}

// The game's screen, for setting the Maps page beside the original's frames.
constexpr int kGameWidth = 640;
constexpr int kGameHeight = 480;

// A 3D-only System: the software renderer at the game's size.
class SoftSystem : public HeadlessSystem {
public:
    tools::SoftRenderer3D renderer{kGameWidth, kGameHeight};
    Error init3D(int width, int height, gfx::Renderer3D *&out) override {
        if (width != renderer.width() || height != renderer.height())
            return Error::fail("the software renderer is another size");
        out = &renderer;
        return Error::success();
    }
};

int cmdMapShot(int argc, char **argv) {
    if (argc < 11) {
        std::fprintf(stderr, "usage: reluva-tool storm3d map-shot <game folder> <map> <out.png> <eye x y z> "
                             "<forward x y z> [field of view, degrees, top to bottom] [key...]\n");
        return 2;
    }
    storm3d::GameFiles files;
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    storm3d::MapScene::Camera camera;
    for (size_t i = 0; i < 3; ++i) {
        camera.eye[i] = float(std::atof(argv[5 + i]));
        camera.forward[i] = float(std::atof(argv[8 + i]));
    }
    // Up: the world's, made square to the way the camera looks.
    gfx::Vec3 &f = camera.forward;
    const float len = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len <= 0) {
        std::fprintf(stderr, "the camera looks nowhere\n");
        return 2;
    }
    for (float &v : f)
        v /= len;
    camera.up = {-f[1] * f[0], 1 - f[1] * f[1], -f[1] * f[2]};
    const float upLen =
        std::sqrt(camera.up[0] * camera.up[0] + camera.up[1] * camera.up[1] + camera.up[2] * camera.up[2]);
    for (float &v : camera.up)
        v /= upLen > 0 ? upLen : 1;
    if (argc > 11)
        camera.fovY = float(std::atof(argv[11]) * 3.14159265358979 / 180);
    SoftSystem sys;
    Surface screen;
    screen.create(kGameWidth, kGameHeight, PixelFormat::kRGBA32);
    storm3d::MapPage page(sys, screen, files);
    page.activate();
    std::string name = argv[3];
    if (!reluva::text::hasSuffixIgnoreCase(name, ".bzn"))
        name += ".bzn";
    if (!page.select(name) && !page.select("bzn/" + name)) {
        std::fprintf(stderr, "no map %s\n", argv[3]);
        return 1;
    }
    // Keys the page takes before the shot (r: the starting units), each a letter.
    for (int a = 12; a < argc; ++a)
        page.handleKey(uint32_t(static_cast<unsigned char>(argv[a][0])));
    page.setCamera(camera);
    const Surface &shown = sys.renderer.frame();
    image::Image img;
    img.w = shown.w;
    img.h = shown.h;
    img.rgba = shown.pixels;
    const std::vector<uint8_t> png = image::encodePng(img);
    std::ofstream out(argv[4], std::ios::binary);
    out.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
    page.deactivate();
    std::printf("%s\n", sys.title().c_str());
    return out ? 0 : 1;
}

const tool::Commands kCommands("storm3d",
                               {
                                   {"viewer-shot", "<game folder> <out.png> [key...]",
                                    "the data viewer run without a window, keys pressed, its last screen "
                                    "written as PNG",
                                    cmdViewerShot},
                                   {"map-shot",
                                    "<game folder> <map> <out.png> <eye x y z> <forward x y z> [degrees] [key...]",
                                    "a map drawn in software from a camera given exactly, at the game's "
                                    "640 x 480: to set beside the original's frames",
                                    cmdMapShot},
                               });

} // namespace
