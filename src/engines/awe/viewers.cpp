// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/viewers.h"

#include "common/audio/decode.h"
#include "common/font.h"
#include "common/fs.h"
#include "common/text.h"
#include "common/video/bink_audio.h"
#include "engines/awe/camera.h"
#include "engines/awe/dialogue_box.h"
#include "engines/awe/sprite.h"
#include "engines/awe/tables.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace reluva::awe {

namespace {

bool hasExtension(const std::string& name, std::initializer_list<const char*> exts) {
    const std::string lower = fs::toLower(name);
    for (const char* e : exts)
        if (lower.size() > std::strlen(e) && lower.compare(lower.size() - std::strlen(e), std::string::npos, e) == 0)
            return true;
    return false;
}

bool startsWith(const std::string& name, const char* prefix) {
    const std::string lower = fs::toLower(name);
    return lower.compare(0, std::strlen(prefix), prefix) == 0;
}

// The game's files under `folders` with one of `exts`, sorted.
std::vector<std::string> listFiles(const Archive& files, std::initializer_list<const char*> folders,
                                   std::initializer_list<const char*> exts) {
    std::vector<std::string> out;
    for (const std::string& n : files.list()) {
        bool inFolder = folders.size() == 0;
        for (const char* f : folders)
            inFolder |= startsWith(n, f);
        if (inFolder && hasExtension(n, exts))
            out.push_back(n);
    }
    std::sort(out.begin(), out.end(),
              [](const std::string& a, const std::string& b) { return fs::toLower(a) < fs::toLower(b); });
    return out;
}

std::string shorten(const std::string& s, size_t max) {
    return s.size() <= max ? s : s.substr(0, max - 3) + "...";
}

} // namespace

// --- Page ---------------------------------------------------------------------

void Page::begin2D() {
    ctx_.sys.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32);
    ctx_.sys.setMouseCursorVisible(true);
}

void Page::clear(uint8_t r, uint8_t g, uint8_t b) {
    for (size_t i = 0; i < ctx_.screen.pixels.size(); i += 4) {
        ctx_.screen.pixels[i] = r;
        ctx_.screen.pixels[i + 1] = g;
        ctx_.screen.pixels[i + 2] = b;
        ctx_.screen.pixels[i + 3] = 255;
    }
}

void Page::fill(int x, int y, int w, int h, Color c, uint8_t alpha) {
    Surface& s = ctx_.screen;
    for (int yy = std::max(0, y); yy < std::min(s.h, y + h); yy++)
        for (int xx = std::max(0, x); xx < std::min(s.w, x + w); xx++) {
            // Over what's there, by its alpha too: on an overlay, transparent
            // where nothing is drawn, the colour isn't darkened by the
            // emptiness under it.
            uint8_t* p = s.row(yy) + xx * 4;
            const int under = p[3] * (255 - alpha) / 255, out = alpha + under;
            if (out == 0)
                continue;
            p[0] = uint8_t((c.r * alpha + p[0] * under) / out);
            p[1] = uint8_t((c.g * alpha + p[1] * under) / out);
            p[2] = uint8_t((c.b * alpha + p[2] * under) / out);
            p[3] = uint8_t(out);
        }
}

void Page::frame(int x, int y, int w, int h, Color c) {
    fill(x, y, w, 1, c);
    fill(x, y + h - 1, w, 1, c);
    fill(x, y, 1, h, c);
    fill(x + w - 1, y, 1, h, c);
}

int Page::text(int x, int y, const std::string& s, Color c, int scale) {
    return font::drawText(ctx_.screen, x, y, s, c, scale);
}

void Page::label(int x, int y, const std::string& s, Color c) {
    fill(x - 2, y - 1, int(font::length(s)) * font::kAdvance + 4, font::kLineHeight + 1, {0, 0, 0}, 170);
    text(x, y, s, c);
}

void Page::help(const std::string& keys) {
    fill(0, kScreenH - font::kLineHeight - 3, kScreenW, font::kLineHeight + 3, {0, 0, 0}, 200);
    text(4, kScreenH - font::kLineHeight - 1, keys + "   Tab: next page   Esc: launcher", {170, 190, 210});
}

void Page::finishScreenshot(gfx::Renderer3D* renderer, const Surface* overlay) {
    if (shotFile_.empty())
        return;
    image::Image img;
    img.w = kScreenW;
    img.h = kScreenH;
    if (renderer) {
        if (!renderer->takeCapture(img.rgba))
            return; // not kept yet: try the next frame
        if (overlay && overlay->pixels.size() == img.rgba.size())
            for (size_t i = 0; i < img.rgba.size(); i += 4) {
                const int a = overlay->pixels[i + 3];
                for (size_t c = 0; c < 3; c++)
                    img.rgba[i + c] = uint8_t((overlay->pixels[i + c] * a + img.rgba[i + c] * (255 - a)) / 255);
            }
    } else if (overlay) {
        img.rgba = overlay->pixels;
    }
    std::error_code ec;
    std::filesystem::create_directories(shotFile_.parent_path(), ec);
    const std::vector<uint8_t> tga = image::encodeTga(img);
    std::ofstream out(shotFile_, std::ios::binary);
    out.write(reinterpret_cast<const char*>(tga.data()), std::streamsize(tga.size()));
    ctx_.sys.setWindowTitle((out ? "Saved " : "Couldn't save ") + shotFile_.string());
    shotFile_.clear();
}

int Page::step(uint32_t key, int index, int count) {
    if (count <= 0)
        return 0;
    int to = index;
    if (key == key::Left)
        to = index - 1;
    else if (key == key::Right)
        to = index + 1;
    else if (key == key::PageUp)
        to = index - 10;
    else if (key == key::PageDown)
        to = index + 10;
    else if (key == key::Home)
        to = 0;
    else if (key == key::End)
        to = count - 1;
    else
        return 0;
    to = ((to % count) + count) % count;
    return to - index == 0 ? 0 : to - index;
}

const image::Image& Page::picture(const std::string& name) {
    const std::string key = normalizeArchiveName(name);
    auto it = pictures_.find(key);
    if (it != pictures_.end())
        return it->second;
    if (pictures_.size() > 64)
        pictures_.clear();
    image::Image& img = pictures_[key];
    std::vector<uint8_t> data;
    if (ctx_.files.read(key, data) || image::decode(data, img))
        img = image::Image();
    return img;
}

// --- Pictures ---------------------------------------------------------------------

PicturePage::PicturePage(ViewerContext& ctx) : Page(ctx) {
    names_ = listFiles(ctx.files, {"locations/", "interface/", "inventory/", "elements/", "models/"},
                       {".jpg", ".png", ".tga", ".bmp"});
}

void PicturePage::activate() {
    begin2D();
    show();
}

void PicturePage::show() {
    clear(24, 24, 28);
    if (names_.empty())
        return;
    const std::string& name = names_[size_t(index_)];
    const image::Image& img = picture(name);
    if (img.w > 0) {
        // Scaled down (nearest) to fit, over a checkerboard that shows alpha.
        const int areaH = kScreenH - 2 * font::kLineHeight - 8;
        const float scale = std::min({1.0f, float(kScreenW) / float(img.w), float(areaH) / float(img.h)});
        const int w = std::max(1, int(float(img.w) * scale)), h = std::max(1, int(float(img.h) * scale));
        const int x0 = (kScreenW - w) / 2, y0 = font::kLineHeight + 4 + (areaH - h) / 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const uint8_t* src = img.pixel(std::min(img.w - 1, int(float(x) / scale)),
                                               std::min(img.h - 1, int(float(y) / scale)));
                const uint8_t check = ((x / 8 + y / 8) & 1) ? 90 : 60;
                uint8_t* dst = ctx_.screen.row(y0 + y) + (x0 + x) * 4;
                for (int c = 0; c < 3; c++)
                    dst[c] = uint8_t((src[c] * src[3] + check * (255 - src[3])) / 255);
            }
        label(4, 2,
              name + "  " + std::to_string(img.w) + "x" + std::to_string(img.h) + "  (" + std::to_string(index_ + 1) +
                  "/" + std::to_string(names_.size()) + ")");
    } else {
        label(4, 2, name + ": can't be decoded", {255, 120, 120});
    }
    help("Left/Right: picture  PgUp/PgDn: 10  Home/End");
    present();
    ctx_.sys.setWindowTitle("Pictures: " + name);
}

void PicturePage::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    if (const int d = step(ev.key, index_, int(names_.size()))) {
        index_ += d;
        show();
    }
}

// --- Scenes ---------------------------------------------------------------------

namespace {

// Draws `img` over an overlay (transparent where nothing is), keeping
// alpha so the overlay can go over the 3D frame.
void blitOver(Surface& dst, const image::Image& img, int x, int y) {
    for (int sy = std::max(0, -y); sy < img.h && y + sy < dst.h; sy++) {
        uint8_t* row = dst.row(y + sy);
        for (int sx = std::max(0, -x); sx < img.w && x + sx < dst.w; sx++) {
            const uint8_t* s = img.pixel(sx, sy);
            uint8_t* d = row + (x + sx) * 4;
            const int a = s[3], da = d[3] * (255 - a) / 255, out = a + da;
            if (out == 0)
                continue;
            for (int c = 0; c < 3; c++)
                d[c] = uint8_t((s[c] * a + d[c] * da) / out);
            d[3] = uint8_t(out);
        }
    }
}

// A scene's act variant ("Salon_01_06a.ac2" for act 6A): the base scene's
// name, or "" if it isn't one.
std::string baseSceneOf(const std::string& name) {
    const size_t dot = name.rfind('.');
    const size_t u = name.rfind('_');
    if (dot == std::string::npos || u == std::string::npos || u + 4 != dot)
        return "";
    if (!std::isdigit(uint8_t(name[u + 1])) || !std::isdigit(uint8_t(name[u + 2])) ||
        !std::isalpha(uint8_t(name[u + 3])))
        return "";
    return name.substr(0, u) + name.substr(dot);
}

std::string folderOf(const std::string& name) {
    const size_t slash = name.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : name.substr(0, slash);
}

} // namespace

ScenePage::ScenePage(ViewerContext& ctx) : Page(ctx) {
    names_ = listFiles(ctx.files, {"locations/"}, {".ac2"});
    state_.values["moving"] = 1;
    runner_ = std::make_unique<ScriptRunner>(state_, *this);
    dialogue_ = std::make_unique<DialogueManager>(state_, *this, *this);
    dialogue_->onLog = [this](const std::string& l) { logScript("    " + shorten(l, 150)); };
    runner_->onEvent = [this](const ScriptEvent& e) {
        std::string line;
        for (const std::string& w : e.words)
            line += (line.empty() ? "" : " ") + w;
        logScript("  " + line);
    };
}

ScenePage::~ScenePage() { releaseGpu(); }

void ScenePage::activate() {
    if (ctx_.sys.init3D(kScreenW, kScreenH, renderer_))
        renderer_ = nullptr, begin2D();
    load();
}

void ScenePage::deactivate() {
    releaseGpu();
    renderer_ = nullptr;
}

void ScenePage::releaseGpu() {
    characters_.clear();
    models_.clear();
    animations_.clear();
    if (renderer_) {
        for (gfx::MeshId m : frameMeshes_)
            renderer_->destroyMesh(m);
        if (back_)
            renderer_->destroyTexture(back_);
        if (quad_)
            renderer_->destroyMesh(quad_);
    }
    frameMeshes_.clear();
    back_ = 0;
    quad_ = 0;
}

void ScenePage::load() {
    selected_ = -1;
    error_.clear();
    base_.clear();
    scene_ = Ac2Scene();
    characters_.clear();
    loops_.clear();
    particles_.clear();
    state_.act.clear();
    grid_ = WalkGrid();
    path_.clear();
    hover_ = {};
    scriptObject_ = nullptr;
    if (names_.empty())
        return draw();
    auto read = [&](const std::string& name, Ac2Scene& out) {
        std::vector<uint8_t> data;
        Error e = ctx_.files.read(name, data);
        if (!e)
            e = parseAc2(data, out);
        return e;
    };
    const std::string& name = names_[size_t(index_)];
    Ac2Scene variant;
    if (Error e = read(name, variant))
        error_ = e.message;
    // An act's variant changes its base scene: shown over it, as the base
    // has the camera and the pictures.
    const std::string base = baseSceneOf(name);
    if (!variant.camera() && !base.empty() && ctx_.files.has(base) && !read(base, scene_)) {
        base_ = base;
        if (!scene_.records.empty() && scene_.records.back().tag == "DONE")
            scene_.records.pop_back();
        scene_.records.insert(scene_.records.end(), variant.records.begin(), variant.records.end());
        // The act it's for: "salon_01_06a.ac2" is act 06a's.
        state_.act = name.substr(name.rfind('_') + 1, 3);
    } else {
        scene_ = std::move(variant);
    }
    if (const Ac2Record* cam = scene_.camera())
        grid_.load(*cam);
    if (renderer_ && showCharacters_)
        loadCharacters();
    loadLoops();
    for (const Ac2Record* o : scene_.objects())
        if (o->tag == "PART" || o->tag == "RAIN" || o->tag == "SNOW")
            particles_.emplace(o, ParticleSystem(ParticleSettings::fromRecord(*o)));
    draw();
}

void ScenePage::drawParticles(const ParticleSystem& system, bool keepAlpha) {
    const ParticleSettings& s = system.settings();
    const Ac2Record* camr = scene_.camera();
    if (!camr && !s.screenSpace)
        return;
    const image::Image* img = &picture(s.texture);
    if (img->w <= 0) {
        // The texture isn't there: a soft white dot instead.
        if (dot_.w == 0) {
            dot_.w = dot_.h = 16;
            dot_.rgba.resize(16 * 16 * 4);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    const float dx = (float(x) - 7.5f) / 8, dy = (float(y) - 7.5f) / 8;
                    uint8_t* p = dot_.pixel(x, y);
                    p[0] = p[1] = p[2] = 255;
                    p[3] = uint8_t(255 * std::clamp(1 - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f));
                }
        }
        img = &dot_;
    }
    const SceneCamera cam = camr ? SceneCamera::fromRecord(*camr) : SceneCamera();
    const gfx::Vec3 eye = cam.eye();
    std::vector<ParticleSystem::Point> points;
    system.points(points);
    for (const ParticleSystem::Point& p : points) {
        float x, y, distance;
        if (s.screenSpace) {
            // An orthographic 1024x768 view about the screen's centre, y up.
            x = float(kScreenW) / 2 + p.position[0];
            y = float(kScreenH) / 2 - p.position[1];
            distance = std::sqrt(p.position[0] * p.position[0] + p.position[1] * p.position[1] +
                                 p.position[2] * p.position[2]);
        } else {
            if (!cam.project(p.position, kScreenW, kScreenH, x, y))
                continue;
            const float d[3] = {p.position[0] - eye[0], p.position[1] - eye[1], p.position[2] - eye[2]};
            distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        }
        // Direct3D's point scale with A = B = 0, C = 1: the size over the
        // distance, times the screen's height; at most 64 pixels.
        const float size = std::min(64.0f, distance > 0 ? s.size * float(kScreenH) / distance : 64.0f);
        drawPointSprite(ctx_.screen, *img, x, y, size, p.rgba, !s.alphaBlend, keepAlpha);
    }
}

void ScenePage::loadLoops() {
    for (const Ac2Record* o : scene_.objects()) {
        if (!hasExtension(o->text("file"), {".bik"}))
            continue;
        Loop loop;
        loop.file = std::make_unique<video::BinkFile>();
        loop.video = std::make_unique<video::BinkVideoDecoder>();
        std::vector<uint8_t> data;
        Error e = ctx_.files.read(o->text("file"), data);
        if (!e)
            e = loop.file->open(std::move(data));
        if (!e)
            e = loop.video->init(loop.file->width(), loop.file->height(), loop.file->revision(), loop.file->flags());
        if (e) {
            error_ = o->text("file") + ": " + e.message;
            continue;
        }
        loops_.emplace(o, std::move(loop));
    }
    advanceLoops(0);
}

bool ScenePage::advanceLoops(float time) {
    bool changed = false;
    for (auto& [o, loop] : loops_) {
        const size_t count = loop.file->frameCount();
        if (count == 0 || loop.file->fps() <= 0)
            continue;
        const size_t want = size_t(double(time) * loop.file->fps()) % count;
        if (want + 1 == loop.next && !loop.frame.rgba.empty())
            continue;
        // Frames build on the one before: from the start again when it
        // has gone round.
        if (want + 1 < loop.next || loop.next == 0) {
            loop.video->init(loop.file->width(), loop.file->height(), loop.file->revision(), loop.file->flags());
            loop.next = 0;
        }
        for (; loop.next <= want; loop.next++) {
            video::BinkFile::Frame f;
            if (loop.file->frame(loop.next, f) || loop.video->decode(f.video.data, f.video.size))
                break;
        }
        loop.frame.w = loop.video->width();
        loop.frame.h = loop.video->height();
        loop.video->toRgba(loop.frame.rgba);
        changed = true;
    }
    return changed;
}

void ScenePage::loadCharacters() {
    const Ac2Record* camr = scene_.camera();
    if (!renderer_ || !ctx_.granny || !camr)
        return;
    bool any = false;
    for (const Ac2Record* o : scene_.objects())
        any |= o->tag == "NPCX" || o->tag == "PCXX";
    if (!any)
        return;
    // Loading them takes a few seconds each the first time (granny2.dll
    // decodes their textures): say so.
    std::fill(ctx_.screen.pixels.begin(), ctx_.screen.pixels.end(), 0);
    font::drawText(ctx_.screen, 8, 8, "Loading the characters...", {255, 230, 150}, 2);
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    renderer_->endFrame(&ctx_.screen);

    for (const Ac2Record* o : scene_.objects()) {
        if (o->tag != "NPCX" && o->tag != "PCXX")
            continue;
        const std::string file = normalizeArchiveName(o->text("file"));
        Character c;
        c.name = file;
        auto found = models_.find(file);
        if (found == models_.end()) {
            Gr2File gr2;
            Model model;
            Error e = openGr2(ctx_.files, file, *ctx_.granny, gr2);
            if (!e)
                e = loadModel(gr2, ctx_.granny, model);
            if (e) {
                error_ = file + ": " + e.message;
                found = models_.emplace(file, nullptr).first;
            } else {
                found = models_.emplace(file, std::make_shared<GpuModel>(*renderer_, std::move(model))).first;
            }
        }
        c.model = found->second;
        if (!c.model)
            continue;
        // Its animation: MODL's (sit), else standing idle, as files of the
        // model's folder named <who>_<animation>.gr2.
        const std::string folder = folderOf(file);
        std::string who = file.substr(folder.size() + 1);
        who = who.substr(0, who.find("_model"));
        std::vector<std::string> tries;
        if (const Ac2Record* m = o->part("MODL"); m && !m->text("anim").empty()) {
            const std::string a = fs::toLower(m->text("anim"));
            tries = {who + "_" + a, who + "_" + a + "idle1", who + "_" + a + "1"};
        }
        tries.insert(tries.end(), {who + "_idle1", who + "_idle"});
        for (const std::string& t : tries) {
            const std::string path = folder + "/" + t + ".gr2";
            if (!ctx_.files.has(path))
                continue;
            auto a = animations_.find(path);
            if (a == animations_.end()) {
                Gr2File gr2;
                auto anim = std::make_shared<Animation>();
                Error e = openGr2(ctx_.files, path, *ctx_.granny, gr2);
                if (!e)
                    e = awe::loadAnimation(gr2, *anim, ctx_.granny);
                a = animations_.emplace(path, e ? nullptr : anim).first;
            }
            if (a->second) {
                c.animation = a->second;
                c.animationName = t;
                break;
            }
        }
        c.x = o->f("x"), c.y = o->f("y"), c.z = o->f("z"), c.rotY = o->f("rotY");
        c.player = o->tag == "PCXX";
        c.who = who;
        c.record = o;
        // Arriving by "scene <name> <n>": at its walk point n.
        if (c.player && entryPoint_ >= 0)
            if (const Ac2Record* w = o->part("WALK"))
                for (const Ac2Record& pt : w->parts)
                    if (pt.i("id") == entryPoint_)
                        c.x = pt.f("x"), c.z = pt.f("z"), c.rotY = pt.f("rotY");
        placeCharacter(c, 0);
        characters_.push_back(std::move(c));
    }
}

void ScenePage::draw() {
    const std::vector<const Ac2Record*> objects = scene_.objects();
    // Pictures and video loops back to front, by depth: the background at
    // -10000, what's behind its windows further still. A picture's d0,
    // when set, is its depth instead of z (the view outside a window: d0
    // -11000, z 0). Those whose condition doesn't hold now are left out
    // (the countryside going by: "moving >0").
    std::vector<const Ac2Record*> pictures;
    for (const Ac2Record* o : objects) {
        if (hidden(*o))
            continue;
        if (const auto ps = particles_.find(o); ps != particles_.end()) {
            if (testCondition(ps->second.settings().condition, state_))
                pictures.push_back(o);
        } else if ((hasExtension(o->text("file"), {".jpg", ".png", ".tga", ".bmp"}) || loops_.count(o)) &&
                   testCondition(o->text("condition"), state_)) {
            pictures.push_back(o);
        }
    }
    auto imageOf = [&](const Ac2Record* o) -> const image::Image& {
        const auto loop = loops_.find(o);
        return loop != loops_.end() ? loop->second.frame : picture(o->text("file"));
    };
    // Placed as the game's sprites are: scaled, turned about the top-left
    // corner, moved (docs/formats/awe/ac2.md).
    auto place = [&](const Ac2Record* o, bool keepAlpha) {
        if (const auto ps = particles_.find(o); ps != particles_.end())
            return drawParticles(ps->second, keepAlpha);
        const image::Image& img = imageOf(o);
        if (img.w <= 0)
            return;
        SpriteTransform t;
        t.x = o->f("x"), t.y = o->f("y");
        if (o->field("spriteScaleX"))
            t.scaleX = o->f("spriteScaleX"), t.scaleY = o->f("spriteScaleY");
        if (const Ac2Record* p = o->part(o->tag == "BINK" ? "BNKX" : "SPRT"))
            t.roll = p->f("roll");
        if (!t.plain())
            drawSprite(ctx_.screen, img, t, keepAlpha);
        else if (keepAlpha)
            blitOver(ctx_.screen, img, int(t.x), int(t.y));
        else
            image::blit(ctx_.screen, img, int(t.x), int(t.y));
    };
    auto depth = [&](const Ac2Record* o) {
        if (const auto ps = particles_.find(o); ps != particles_.end())
            return ps->second.settings().depth;
        return o->f("d0") != 0 ? o->f("d0") : o->f("z");
    };
    std::stable_sort(pictures.begin(), pictures.end(),
                     [&](const Ac2Record* a, const Ac2Record* b) { return depth(a) < depth(b); });
    // With 3D, the characters go between the pictures behind them (depth
    // below 0: the background and what's behind it) and those in front
    // (cutouts and overlays), which go with the labels into the overlay.
    const bool layered = renderer_ != nullptr;
    clear(0, 0, 0);
    if (layered) {
        for (const Ac2Record* o : pictures)
            if (depth(o) < 0)
                place(o, false);
        if (back_)
            renderer_->destroyTexture(back_);
        back_ = 0;
        gfx::TextureParams params;
        params.clamp = true;
        params.mipmaps = false;
        renderer_->createTexture(kScreenW, kScreenH, ctx_.screen.pixels.data(), params, back_);
        std::fill(ctx_.screen.pixels.begin(), ctx_.screen.pixels.end(), 0);
    }
    for (const Ac2Record* o : pictures)
        if (!layered || depth(o) >= 0)
            place(o, layered);
    if (showGrid_)
        drawGrid();
    if (hotspots_)
        for (const Ac2Record* o : objects) {
            const int l = o->i("left"), t = o->i("top"), r = o->i("right"), b = o->i("bottom");
            if (o->tag != "SPOT" || r <= l || b <= t || hidden(*o) || !testCondition(o->text("condition"), state_))
                continue;
            // Its outline where it has one (listD), else its rectangle.
            const Ac2Value* outline = o->field("listD");
            if (outline && outline->numbers.size() >= 3) {
                const auto& pts = outline->numbers;
                for (size_t i = 0; i < pts.size(); i++)
                    line(int(pts[i].first), int(pts[i].second), int(pts[(i + 1) % pts.size()].first),
                         int(pts[(i + 1) % pts.size()].second), {255, 220, 60});
            } else {
                frame(l, t, r - l, b - t, {255, 220, 60});
            }
            const Ac2Record* hot = o->part("HOTS");
            std::string name = o->text("name");
            if (name.empty() && hot)
                name = hot->text("name");
            label(l + 2, t + 2, name.empty() ? shorten(o->text("script"), 60) : name, {255, 220, 60});
        }
    if (selected_ >= 0 && size_t(selected_) < objects.size()) {
        const Ac2Record* o = objects[size_t(selected_)];
        const image::Image& img = imageOf(o);
        if (img.w > 0)
            frame(int(o->f("x")), int(o->f("y")), img.w, img.h, {90, 255, 120});
        // Its fields, as the dump shows them.
        int y = kScreenH - 2 * font::kLineHeight - 6;
        std::vector<std::string> lines;
        std::string text;
        for (const auto& [name, v] : o->fields) {
            const std::string s = v.str();
            if (s == "0" || s == "\"\"" || s == "[]")
                continue;
            const std::string item = name + "=" + shorten(s, 60) + "  ";
            if (font::length(text + item) * font::kAdvance > size_t(kScreenW - 8))
                lines.push_back(text), text.clear();
            text += item;
        }
        for (const Ac2Record& p : o->parts) {
            text += p.tag + "{";
            for (const auto& [name, v] : p.fields)
                if (v.str() != "0" && v.str() != "\"\"" && v.str() != "[]")
                    text += name + "=" + shorten(v.str(), 40) + " ";
            text += "} ";
        }
        lines.push_back(text);
        y -= int(lines.size()) * font::kLineHeight;
        for (const std::string& l : lines) {
            label(4, y, l, {150, 255, 170});
            y += font::kLineHeight;
        }
    }
    if (info_) {
        std::vector<std::string> lines;
        lines.push_back((names_.empty() ? std::string("No scenes") : names_[size_t(index_)]) + "  (" +
                        std::to_string(index_ + 1) + "/" + std::to_string(names_.size()) + ")" +
                        (base_.empty() ? "" : "  over " + base_));
        if (!error_.empty())
            lines.push_back(error_);
        std::map<std::string, int> tags;
        for (const Ac2Record* o : objects)
            tags[o->tag]++;
        std::string counts = std::to_string(objects.size()) + " objects:";
        for (const auto& [t, n] : tags)
            counts += " " + std::to_string(n) + " " + t;
        lines.push_back(counts);
        if (!grid_.empty()) {
            std::string g = "walk grid: cells of " + std::to_string(grid_.cellSize());
            if (hover_.valid())
                g += ", under the mouse (" + std::to_string(hover_.a) + ", " + std::to_string(hover_.b) +
                     ") = " + std::to_string(grid_.at(hover_.a, hover_.b));
            if (!path_.empty())
                g += ", path of " + std::to_string(path_.size()) + " points by " + pathSource_;
            lines.push_back(g);
        }
        if (!scriptLog_.empty()) {
            std::string tags;
            for (const auto& [t, v] : state_.tags)
                tags += " " + t + "=" + std::to_string(v);
            std::string items;
            for (const std::string& i : state_.items)
                items += " " + i;
            lines.push_back("script: tags" + (tags.empty() ? std::string(" none") : tags) + "; items" +
                            (items.empty() ? std::string(" none") : items));
            for (const std::string& l : scriptLog_)
                lines.push_back(l);
        }
        lines.push_back(std::string("train ") + (state_.values["moving"] ? "moving" : "stopped") + ", weather " +
                        (state_.values["weather"] ? "on" : "off") + (state_.act.empty() ? "" : ", act " + state_.act));
        if (const Ac2Record* cam = scene_.camera()) {
            char buf[200];
            std::snprintf(buf, sizeof buf, "camera about (%g, %g, %g), turned (%g, %g, %g), back (%g, %g, %g), fov %.1f deg",
                          cam->f("x"), cam->f("y"), cam->f("z"), cam->f("pitch"), cam->f("yaw"), cam->f("roll"),
                          cam->f("74"), cam->f("78"), cam->f("7c"), cam->f("fov") * 57.29578f);
            lines.push_back(buf);
        }
        for (const Ac2Record& r : scene_.records)
            if (r.tag == "MUSC" || r.tag == "EVNT")
                if (const Ac2Value* v = r.fields.empty() ? nullptr : &r.fields[0].second; v && !v->pairs.empty())
                    lines.push_back(r.tag + " " + shorten(v->str(), 150));
        for (const Ac2Record* o : objects)
            if (o->tag == "NPCX" || o->tag == "PCXX") {
                char buf[200];
                std::string anim;
                for (const Character& c : characters_)
                    if (c.name == normalizeArchiveName(o->text("file")) && !c.animationName.empty())
                        anim = ", posed as " + c.animationName;
                std::snprintf(buf, sizeof buf, "%s %s at (%g, %g, %g), facing %g%s", o->tag.c_str(),
                              o->text("file").c_str(), o->f("x"), o->f("y"), o->f("z"), o->f("rotY"), anim.c_str());
                lines.push_back(buf);
            }
        int y = 2;
        for (const std::string& l : lines) {
            label(4, y, shorten(l, 168), l == error_ ? Color{255, 120, 120} : Color{230, 230, 230});
            y += font::kLineHeight;
        }
    }
    if (dialogue_->boxShown() && ctx_.fonts) {
        DialogueBoxPictures pics;
        pics.window = &picture(dialogue_->window() == 1 ? "interface/dialoguewin02.png" : "interface/dialoguewin01.png");
        pics.bullet = &picture("interface/dialoguewin04.png");
        pics.bulletOn = &picture("interface/dialoguewin05.png");
        // fonts.txt's colours by their place: 6 choices, 7 the one under
        // the mouse, 8 Antoinette, 9 and 10 the others.
        static const char* const kColours[] = {"dialogue text when choosing", "highlighted dialogue text when choosing",
                                               "dialogue for main character",
                                               "dialogue text for second character in conversation",
                                               "dialogue text for third character in conversation"};
        const int c = std::clamp(dialogue_->colour() - 6, 0, 4);
        std::vector<std::string> choices;
        for (const auto& ch : dialogue_->choices())
            choices.push_back(ch.text);
        const DialogueLine* said = dialogue_->line();
        const auto areas = drawDialogueBox(ctx_.screen, *ctx_.fonts, pics, dialogue_->boxTop(),
                                           said ? said->text : std::string(), ctx_.fonts->colour(kColours[c]),
                                           dialogue_->choosing() ? choices : std::vector<std::string>(),
                                           dialogue_->highlighted());
        std::vector<std::pair<int, int>> rows;
        for (const auto& a : areas)
            rows.push_back({a.y, a.h});
        dialogue_->setChoiceAreas(std::move(rows));
    }
    help(std::string("Left/Right: scene  Up/Down: object  H: hotspots  I: info  M: moving  W: weather  G: grid  "
                     "click: walk") +
         (renderer_ ? "  C: characters" : "  (no 3D: no characters)"));
    if (!renderer_)
        present();
    ctx_.sys.setWindowTitle("Scenes: " + (names_.empty() ? std::string() : names_[size_t(index_)]));
}

void ScenePage::update(uint64_t nowMs) {
    if (startMs_ == 0)
        startMs_ = nowMs;
    const float time = float(nowMs - startMs_) / 1000.0f;
    // Particles move on by the time since the last frame (at most a tenth
    // of a second, so a pause doesn't empty them).
    const float dt = lastMs_ ? std::min(0.1f, float(nowMs - lastMs_) / 1000.0f) : 0.0f;
    lastMs_ = nowMs;
    time_ = time;
    runner_->update(dt);
    if (dialogue_->active()) {
        dialogue_->update(dt);
        draw(); // the box moves, lines change
    }
    bool changed = advanceLoops(time);
    for (Character& c : characters_) {
        if (c.path.empty())
            continue;
        // On towards the next point, turning to face it (at most a turn
        // and a half a second, a guess; the game's planner turns along
        // its path).
        changed = true;
        const float dx = c.path.front().first - c.x, dz = c.path.front().second - c.z;
        const float distance = std::sqrt(dx * dx + dz * dz), stepLength = c.walkSpeed * dt;
        if (distance > 0.001f) {
            constexpr float kTwoPi = 6.2831853f;
            float turn = std::remainder(std::atan2(dx, dz) - c.rotY, kTwoPi);
            const float most = 9.4f * dt;
            turn = std::clamp(turn, -most, most);
            c.rotY = std::remainder(c.rotY + turn, kTwoPi);
        }
        if (stepLength >= distance) {
            c.x = c.path.front().first, c.z = c.path.front().second;
            c.path.erase(c.path.begin());
            // There: a script waiting on the walk goes on.
            if (c.path.empty() && !walkVerb_.empty() && walkRunner_) {
                const std::string verb = walkVerb_;
                walkVerb_.clear();
                walkRunner_->finished(verb);
            }
        } else {
            c.x += dx / distance * stepLength, c.z += dz / distance * stepLength;
        }
        placeCharacter(c, time);
    }
    for (auto& [o, system] : particles_)
        if (testCondition(system.settings().condition, state_)) {
            system.update(dt);
            changed = true;
        }
    if (changed)
        draw();
    if (!renderer_)
        return Page::update(nowMs);
    for (gfx::MeshId m : frameMeshes_)
        renderer_->destroyMesh(m);
    frameMeshes_.clear();
    // The pictures behind, as a quad over the whole frame.
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    if (!quad_) {
        std::vector<gfx::Vertex> v(4);
        const float corners[4][4] = {{-1, 1, 0, 0}, {1, 1, 1, 0}, {1, -1, 1, 1}, {-1, -1, 0, 1}};
        for (size_t i = 0; i < 4; i++) {
            v[i] = {};
            v[i].pos[0] = corners[i][0], v[i].pos[1] = corners[i][1], v[i].pos[2] = 0.5f;
            v[i].uv[0] = corners[i][2], v[i].uv[1] = corners[i][3];
            v[i].color[0] = v[i].color[1] = v[i].color[2] = v[i].color[3] = 255;
        }
        renderer_->createMesh(v, {0, 2, 1, 0, 3, 2}, quad_);
    }
    if (back_ && quad_) {
        gfx::DrawCall call;
        call.mesh = quad_;
        call.indexCount = 6;
        call.texture = back_;
        call.state.depthTest = false;
        call.state.depthWrite = false;
        call.state.cull = gfx::Cull::None;
        renderer_->draw(call);
    }
    // The characters, through the scene's camera.
    if (const Ac2Record* camr = scene_.camera(); camr && showCharacters_) {
        const SceneCamera cam = SceneCamera::fromRecord(*camr);
        renderer_->setView(cam.viewProjection(), cam.eye());
        renderer_->clearDepth();
        for (const Character& c : characters_) {
            const bool walking = !c.path.empty() && c.walk;
            const Pose pose = walking ? poseAt(c.model->model(), c.walk.get(), time - c.walkStart)
                                      : poseAt(c.model->model(), c.animation.get(), time);
            c.model->draw(pose, c.placement, false, frameMeshes_);
        }
    }
    prepareScreenshot(renderer_);
    renderer_->endFrame(&ctx_.screen);
    finishScreenshot(renderer_, &ctx_.screen);
}

void ScenePage::fillPolygon(const std::vector<std::pair<float, float>>& points, Color c, uint8_t alpha) {
    if (points.size() < 3)
        return;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& [x, y] : points) {
        minX = std::min(minX, x), maxX = std::max(maxX, x);
        minY = std::min(minY, y), maxY = std::max(maxY, y);
    }
    const int x0 = std::max(0, int(std::floor(minX))), x1 = std::min(kScreenW - 1, int(std::ceil(maxX)));
    const int y0 = std::max(0, int(std::floor(minY))), y1 = std::min(kScreenH - 1, int(std::ceil(maxY)));
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            // Inside if on the same side of every edge.
            const float px = float(x) + 0.5f, py = float(y) + 0.5f;
            int sign = 0;
            bool inside = true;
            for (size_t i = 0; i < points.size() && inside; i++) {
                const auto& [ax, ay] = points[i];
                const auto& [bx, by] = points[(i + 1) % points.size()];
                const float cross = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
                const int s2 = cross > 0 ? 1 : cross < 0 ? -1 : 0;
                if (s2 != 0) {
                    if (sign != 0 && s2 != sign)
                        inside = false;
                    sign = s2;
                }
            }
            if (inside)
                fill(x, y, 1, 1, c, alpha);
        }
}

void ScenePage::drawGrid() {
    const Ac2Record* camr = scene_.camera();
    if (!camr || grid_.empty())
        return;
    const SceneCamera cam = SceneCamera::fromRecord(*camr);
    const float size = float(grid_.cellSize());
    auto corner = [&](float x, float z, std::pair<float, float>& out) {
        const float p[3] = {x, 0, z};
        return cam.project(p, kScreenW, kScreenH, out.first, out.second);
    };
    // A cell's square on the floor, on the screen.
    auto square = [&](WalkGrid::Cell cell, std::vector<std::pair<float, float>>& pts) {
        const float x0 = float(cell.a - 32) * size, z1 = float(32 - cell.b) * size;
        pts.assign(4, {});
        return corner(x0, z1 - size, pts[0]) && corner(x0 + size, z1 - size, pts[1]) &&
               corner(x0 + size, z1, pts[2]) && corner(x0, z1, pts[3]);
    };
    std::vector<std::pair<float, float>> pts;
    for (int a = 0; a < WalkGrid::kSize; a++)
        for (int b = 0; b < WalkGrid::kSize; b++) {
            const uint8_t v = grid_.at(a, b);
            if (v == 0 || !square({a, b}, pts))
                continue;
            // 1 blocked, 2 blocked unless allowed, 3 blocked at first.
            const Color c = v == 1 ? Color{230, 50, 50} : v == 2 ? Color{240, 150, 40} : Color{240, 230, 60};
            fillPolygon(pts, c, 90);
        }
    if (hover_.valid() && square(hover_, pts))
        for (size_t i = 0; i < 4; i++)
            line(int(pts[i].first), int(pts[i].second), int(pts[(i + 1) % 4].first), int(pts[(i + 1) % 4].second),
                 grid_.blocked(hover_, true) ? Color{255, 80, 80} : Color{80, 255, 255});
    for (size_t i = 0; i + 1 < path_.size(); i++) {
        float x0, z0, x1, z1;
        grid_.centre(path_[i], x0, z0);
        grid_.centre(path_[i + 1], x1, z1);
        std::pair<float, float> p0, p1;
        if (corner(x0, z0, p0) && corner(x1, z1, p1))
            line(int(p0.first), int(p0.second), int(p1.first), int(p1.second), {90, 255, 120});
    }
}

bool ScenePage::loadWalk(Character& c) {
    if (c.walk)
        return true;
    if (!ctx_.granny)
        return false;
    // <who>_walk.gr2 beside its model.
    const std::string folder = folderOf(c.name);
    std::string who = c.name.substr(folder.size() + 1);
    who = who.substr(0, who.find("_model"));
    const std::string path = folder + "/" + who + "_walk.gr2";
    if (!ctx_.files.has(path))
        return false;
    Gr2File gr2;
    auto anim = std::make_shared<Animation>();
    if (openGr2(ctx_.files, path, *ctx_.granny, gr2) || awe::loadAnimation(gr2, *anim, ctx_.granny) ||
        anim->duration <= 0)
        return false;
    // The walk carries the body forward (the biped's root, Bip01, moves
    // the whole loop's distance): that is how fast it walks. The game's
    // characters move by that much, while the loop starts again from the
    // same place.
    const Model& model = c.model->model();
    int root = -1;
    for (size_t b = 0; b < model.bones.size(); b++)
        if (model.bones[b].name == "Bip01")
            root = int(b);
    if (root >= 0) {
        Pose start, end;
        computePose(model, anim.get(), 0, start);
        computePose(model, anim.get(), anim->duration, end);
        for (int k = 0; k < 3; k++)
            c.walkDrift[k] = end.world[size_t(root)][size_t(12 + k)] - start.world[size_t(root)][size_t(12 + k)];
        c.walkSpeed = std::sqrt(c.walkDrift[0] * c.walkDrift[0] + c.walkDrift[1] * c.walkDrift[1] +
                                c.walkDrift[2] * c.walkDrift[2]) /
                      anim->duration;
    }
    if (c.walkSpeed <= 1) {
        c.walkSpeed = 150; // a walk in place: a guess at a walking pace
        c.walkDrift[0] = c.walkDrift[1] = c.walkDrift[2] = 0;
    }
    c.walk = anim;
    return true;
}

void ScenePage::placeCharacter(Character& c, float time) const {
    c.placement = characterMatrix(c.x, c.y, c.z, c.rotY);
    if (c.path.empty() || !c.walk)
        return;
    // Back by how far the walk has carried the body this loop, as the
    // character itself moves.
    const float loop = std::fmod(std::max(0.0f, time - c.walkStart), c.walk->duration) / c.walk->duration;
    gfx::Mat4 back = gfx::identity();
    for (int k = 0; k < 3; k++)
        back[size_t(12 + k)] = -loop * c.walkDrift[k];
    c.placement = gfx::multiply(c.placement, back);
}

void ScenePage::walkTo(int sx, int sy) {
    const Ac2Record* camr = scene_.camera();
    float x, z;
    if (!camr || grid_.empty() ||
        !SceneCamera::fromRecord(*camr).floorAt(float(sx), float(sy), kScreenW, kScreenH, x, z))
        return;
    const WalkGrid::Cell goal = grid_.cellAt(x, z);
    // From the player's character (else the first character, else the
    // grid's centre: the path alone is shown).
    Character* who = nullptr;
    for (Character& c : characters_)
        if (c.player || !who)
            who = &c;
    WalkGrid::Cell start{32, 32};
    if (who)
        start = grid_.cellAt(who->x, who->z);
    else
        for (const Ac2Record* o : scene_.objects())
            if (o->tag == "PCXX")
                start = grid_.cellAt(o->f("x"), o->f("z"));
    if (!triedGamePaths_) {
        triedGamePaths_ = true;
        std::vector<uint8_t> exe;
        auto finder = std::make_unique<GamePathFinder>();
        if (!ctx_.files.read("motoe.exe", exe) && !finder->load(exe))
            gamePaths_ = std::move(finder);
    }
    if (gamePaths_) {
        path_ = gamePaths_->findPath(grid_, start, goal, false);
        pathSource_ = "the game's path-finder";
    } else {
        path_ = grid_.findPath(start, goal);
        pathSource_ = "Reluva's A*";
    }
    if (who && path_.size() > 1 && loadWalk(*who)) {
        who->path.clear();
        for (size_t i = 1; i < path_.size(); i++) {
            float px, pz;
            grid_.centre(path_[i], px, pz);
            who->path.push_back({px, pz});
        }
        who->walkStart = time_;
    }
    draw();
}

bool ScenePage::hidden(const Ac2Record& o) const {
    if (hiddenNames_.empty())
        return false;
    for (const std::string& key : {o.text("name"), normalizeArchiveName(o.text("file"))})
        if (!key.empty() && hiddenNames_.count(fs::toLower(key)))
            return true;
    // "hide sceorangejuice.jpg": by the file's name alone.
    const std::string file = normalizeArchiveName(o.text("file"));
    return !file.empty() && hiddenNames_.count(fs::toLower(file.substr(file.find_last_of('/') + 1)));
}

const Ac2Record* ScenePage::hotspotAt(int sx, int sy) const {
    const Ac2Record* hit = nullptr;
    for (const Ac2Record* o : scene_.objects()) {
        if (o->tag != "SPOT" || hidden(*o) || !testCondition(o->text("condition"), state_))
            continue;
        const Ac2Value* outline = o->field("listD");
        bool inside = false;
        if (outline && outline->numbers.size() >= 3) {
            // Its outline, by the crossings of a ray to the right.
            const auto& pts = outline->numbers;
            for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
                const float xi = float(pts[i].first), yi = float(pts[i].second);
                const float xj = float(pts[j].first), yj = float(pts[j].second);
                if ((yi > float(sy)) != (yj > float(sy)) && float(sx) < (xj - xi) * (float(sy) - yi) / (yj - yi) + xi)
                    inside = !inside;
            }
        } else {
            inside = sx >= o->i("left") && sx < o->i("right") && sy >= o->i("top") && sy < o->i("bottom");
        }
        if (inside)
            hit = o; // the last drawn is on top
    }
    return hit;
}

void ScenePage::runScript(const Ac2Record& object) {
    // The first conditional script whose condition holds, else its own.
    std::string script = object.text("script");
    if (const Ac2Value* alternatives = object.field("listB"))
        for (const auto& [condition, run] : alternatives->pairs)
            if (testCondition(condition, state_)) {
                script = run;
                break;
            }
    scriptObject_ = &object;
    const Ac2Record* hot = object.part("HOTS");
    const std::string name = !object.text("name").empty() ? object.text("name") : hot ? hot->text("name") : "";
    logScript(name + ": " + shorten(script, 120));
    runner_->run(script);
}

ScenePage::Character* ScenePage::scriptCharacter() {
    Character* player = nullptr;
    for (Character& c : characters_) {
        if (!runner_->character().empty() && c.who == runner_->character())
            return &c;
        if (c.player)
            player = &c;
    }
    return runner_->character().empty() || runner_->character() == "antoinette" ? player : nullptr;
}

bool ScenePage::walkCharacter(Character& c, WalkGrid::Cell goal, bool toPoint) {
    const WalkGrid::Cell start = grid_.cellAt(c.x, c.z);
    if (!triedGamePaths_) {
        triedGamePaths_ = true;
        std::vector<uint8_t> exe;
        auto finder = std::make_unique<GamePathFinder>();
        if (!ctx_.files.read("motoe.exe", exe) && !finder->load(exe))
            gamePaths_ = std::move(finder);
    }
    if (gamePaths_) {
        path_ = gamePaths_->findPath(grid_, start, goal, toPoint);
        pathSource_ = "the game's path-finder";
    } else {
        path_ = grid_.findPath(start, goal);
        pathSource_ = "Reluva's A*";
    }
    if (path_.size() < 2 || !loadWalk(c))
        return false;
    c.path.clear();
    for (size_t i = 1; i < path_.size(); i++) {
        float px, pz;
        grid_.centre(path_[i], px, pz);
        c.path.push_back({px, pz});
    }
    c.walkStart = time_;
    return true;
}

bool ScenePage::changeScene(const std::string& name, int entry) {
    // The scene's file by its name, the act's variant if there is one.
    const std::string want = fs::toLower(name) + ".ac2";
    const std::string variant = state_.act.empty() ? "" : fs::toLower(name) + "_" + state_.act + ".ac2";
    int found = -1;
    for (size_t i = 0; i < names_.size(); i++) {
        const std::string file = fs::toLower(names_[i].substr(names_[i].find_last_of('/') + 1));
        if (file == variant || (file == want && found < 0))
            found = int(i);
    }
    if (found < 0) {
        logScript("  (no scene " + name + ")");
        return false;
    }
    dialogue_->leaveScene(sceneName());
    index_ = found;
    entryPoint_ = entry;
    load();
    entryPoint_ = -1;
    return true;
}

void ScenePage::playSound(const std::string& name) {
    if (soundFiles_.empty())
        for (const std::string& f : listFiles(ctx_.files, {"sound/"}, {".wav", ".mp3"})) {
            const std::string file = f.substr(f.find_last_of('/') + 1);
            soundFiles_.emplace(fs::toLower(file.substr(0, file.rfind('.'))), f);
        }
    const auto it = soundFiles_.find(fs::toLower(name));
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (!ctx_.mixer || it == soundFiles_.end() || ctx_.files.read(it->second, data) || audio::decodeSound(data, *sound))
        return;
    ctx_.mixer->play(sound, audio::SoundType::Effects);
}

void ScenePage::logScript(const std::string& line) {
    scriptLog_.push_back(line);
    while (scriptLog_.size() > 8)
        scriptLog_.pop_front();
}

// --- Dialogue -----------------------------------------------------------------

void ScenePage::loadDialogue() {
    // The gen_ tables and the act's (the game loads the act's when it
    // starts: 0x4224fb); with no act known, all of them in turn.
    if (dialogueAct_ == state_.act)
        return;
    dialogueAct_ = state_.act;
    DialogueBook& book = dialogue_->book();
    book.clear();
    std::vector<std::string> files = {"text/dialogue/gen_antoinette.txt", "text/dialogue/gen_comments.txt",
                                      "text/dialogue/gen_characters.txt"};
    if (!state_.act.empty())
        files.push_back("text/dialogue/act" + state_.act + ".txt");
    else
        for (const std::string& f : listFiles(ctx_.files, {"text/dialogue/act"}, {".txt"}))
            files.push_back(f);
    for (const std::string& f : files) {
        std::vector<uint8_t> data;
        if (!ctx_.files.read(f, data))
            book.add(parseTable(decodeText(data)));
    }
}

bool ScenePage::startTalk(ScriptRunner& runner, const ScriptEvent& e) {
    loadDialogue();
    // talk <name> <code> [options] (0x40ba25): the code's letters are the
    // part, thread and nest; a number n the part n + 99; a "+" at its end
    // goes on to the next part.
    const std::string code = fs::toLower(e.arg(2));
    int part = 100, thread = -1, nest = -1;
    if (!code.empty() && std::isalpha(uint8_t(code[0]))) {
        part = std::toupper(uint8_t(code[0])) - 'A';
        if (code.size() > 1 && code[1] != '+')
            thread = std::toupper(uint8_t(code[1])) - 'A';
        if (code.size() > 2 && code[2] != '+')
            nest = std::toupper(uint8_t(code[2])) - 'A';
    } else if (!code.empty()) {
        part = std::atoi(code.c_str()) + 0x63;
    }
    const bool plus = !code.empty() && code.back() == '+';
    bool gen = runner.fromCharacter(), greet = true;
    int number = -1;
    for (const std::string& opt : {fs::toLower(e.arg(3)), fs::toLower(e.arg(4))}) {
        if (opt.empty())
            continue;
        if (opt == "nogen")
            gen = false;
        else if (opt == "forcegen")
            gen = true;
        else if (opt == "nogreet")
            greet = false;
        else
            number = std::atoi(opt.c_str());
    }
    if (dialogue_->talk(runner, e.arg(1), part, thread, nest, gen, number, plus, greet))
        return true;
    return gen && dialogue_->noTalk(runner);
}

bool ScenePage::playVoice(const std::string& file) {
    stopVoice();
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (!ctx_.mixer || ctx_.files.read(file, data) || audio::decodeSound(data, *sound))
        return false;
    voice_ = ctx_.mixer->play(sound, audio::SoundType::Speech);
    return voice_ != audio::kNoSound;
}

bool ScenePage::voicePlaying() { return ctx_.mixer && voice_ != audio::kNoSound && ctx_.mixer->isPlaying(voice_); }

void ScenePage::stopVoice() {
    if (ctx_.mixer && voice_ != audio::kNoSound)
        ctx_.mixer->stop(voice_);
    voice_ = audio::kNoSound;
}

std::string ScenePage::readText(const std::string& file) {
    std::vector<uint8_t> data;
    return ctx_.files.read(file, data) ? std::string() : decodeText(data);
}

std::string ScenePage::sceneName() {
    if (names_.empty())
        return {};
    const std::string& n = names_[size_t(index_)];
    const std::string file = n.substr(n.find_last_of('/') + 1);
    return fs::toLower(file.substr(0, file.rfind('.')));
}

void ScenePage::turnHead(const std::string& speaker, const std::string& target) {
    logScript("    (" + speaker + " looks " + (target.empty() ? std::string("home") : target) + ")");
}

const Ac2Record* ScenePage::characterAt(int sx, int sy) const {
    const Ac2Record* camr = scene_.camera();
    if (!camr)
        return nullptr;
    const SceneCamera cam = SceneCamera::fromRecord(*camr);
    const Ac2Record* hit = nullptr;
    for (const Ac2Record* o : scene_.objects()) {
        if (o->tag != "NPCX" || o->text("script").empty() || !testCondition(o->text("condition"), state_))
            continue;
        // Where it stands: from its feet to a head's height, about a third
        // as wide.
        float x = o->f("x"), z = o->f("z");
        for (const Character& c : characters_)
            if (c.record == o)
                x = c.x, z = c.z;
        const float feet[3] = {x, o->f("y"), z}, head[3] = {x, o->f("y") + 170, z};
        float fx, fy, hx, hy;
        if (!cam.project(feet, kScreenW, kScreenH, fx, fy) || !cam.project(head, kScreenW, kScreenH, hx, hy))
            continue;
        const float h = fy - hy, half = h * 0.18f;
        if (float(sy) >= hy && float(sy) <= fy && std::abs(float(sx) - fx) <= half)
            hit = o;
    }
    return hit;
}

std::string ScenePage::scriptText(const std::string& name) {
    std::vector<uint8_t> data;
    if (ctx_.files.read("text/scripts/" + fs::toLower(name) + ".txt", data))
        return {};
    return decodeText(data);
}

bool ScenePage::start(ScriptRunner& runner, const ScriptEvent& e, bool wait) {
    const std::string& v = e.verb();
    auto lower = [](const std::string& t) { return fs::toLower(t); };
    if (v == "walk" || v == "run") {
        Character* c = scriptCharacter();
        if (!c || grid_.empty())
            return false;
        WalkGrid::Cell goal;
        bool toPoint = false;
        const std::string target = lower(e.arg(1));
        if (!target.empty() && e.arg(2).empty() && !std::isdigit(uint8_t(target[0]))) {
            // "walk sp3": the character's walk point 3; else an object.
            float x = 0, z = 0;
            bool have = false;
            if (target.size() > 2 && target.compare(0, 2, "sp") == 0 && c->record) {
                if (const Ac2Record* w = c->record->part("WALK"))
                    for (const Ac2Record& pt : w->parts)
                        if (pt.i("id") == std::atoi(target.c_str() + 2))
                            x = pt.f("x"), z = pt.f("z"), have = true;
                toPoint = true;
            } else {
                for (const Character& other : characters_)
                    if (other.who == target)
                        x = other.x, z = other.z, have = true;
            }
            if (!have)
                return false;
            goal = grid_.cellAt(x, z);
        } else {
            goal = {std::atoi(e.arg(1).c_str()), std::atoi(e.arg(2).c_str())};
        }
        if (!walkCharacter(*c, goal, toPoint))
            return false;
        if (wait)
            walkVerb_ = v, walkRunner_ = &runner;
        return wait;
    }
    if (v == "scene") {
        changeScene(e.arg(1), e.arg(2).empty() ? 1 : std::atoi(e.arg(2).c_str()));
        return false;
    }
    if (v == "sound") {
        playSound(e.arg(1));
        return false;
    }
    if (v == "hide" || v == "show") {
        std::string name = lower(e.arg(1));
        if (name == "this" && scriptObject_)
            name = lower(!scriptObject_->text("name").empty() ? scriptObject_->text("name")
                                                               : normalizeArchiveName(scriptObject_->text("file")));
        if (v == "hide")
            hiddenNames_.insert(name);
        else
            hiddenNames_.erase(name);
        draw();
        return false;
    }
    if (v == "take") {
        // The item comes and the object goes (0x40b12f). "take this": the
        // hotspot's, its item named by its picture ("...\scegun.png").
        std::string item = lower(e.arg(1));
        if (item == "this" && scriptObject_) {
            const std::string file = lower(scriptObject_->text("file"));
            const size_t sce = file.find("\\sce");
            if (sce != std::string::npos)
                item = file.substr(sce + 4, file.find('.', sce) - sce - 4);
            hiddenNames_.insert(lower(!scriptObject_->text("name").empty() ? scriptObject_->text("name")
                                                                           : normalizeArchiveName(scriptObject_->text("file"))));
        } else {
            hiddenNames_.insert(item);
        }
        if (item != "this")
            state_.items.insert(item);
        draw();
        return false;
    }
    if (v == "update") {
        // The scene's objects looked at again, for conditions now holding.
        draw();
        return false;
    }
    if (v == "talk")
        return startTalk(runner, e) && wait;
    // The rest isn't done here yet (turn, anim...): logged, and over.
    logScript("    (not done yet: " + v + ")");
    return false;
}

void ScenePage::line(int x0, int y0, int x1, int y1, Color c) {
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    for (int err = dx + dy;;) {
        fill(x0, y0, 1, 1, c);
        if (x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy)
            err += dy, x0 += sx;
        if (e2 <= dx)
            err += dx, y0 += sy;
    }
}

void ScenePage::handleEvent(const Event& ev) {
    if (dialogue_->active() && (ev.type == EventType::MouseMove || ev.type == EventType::MouseDown)) {
        if (ev.type == EventType::MouseMove)
            dialogue_->mouseMove(ev.mouseX, ev.mouseY);
        else if (ev.button == MouseButton::Left)
            dialogue_->click(ev.mouseX, ev.mouseY);
        draw();
        return;
    }
    if (ev.type == EventType::MouseMove && showGrid_) {
        float x, z;
        WalkGrid::Cell cell;
        if (const Ac2Record* cam = scene_.camera();
            cam && SceneCamera::fromRecord(*cam).floorAt(float(ev.mouseX), float(ev.mouseY), kScreenW, kScreenH, x, z))
            cell = grid_.cellAt(x, z);
        if (!(cell == hover_)) {
            hover_ = cell;
            draw();
        }
        return;
    }
    if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        if (!runner_->idle())
            return; // the game takes no clicks while a script runs
        runner_->setFromCharacter(false);
        if (const Ac2Record* spot = hotspotAt(ev.mouseX, ev.mouseY)) {
            runScript(*spot);
        } else if (const Ac2Record* who = characterAt(ev.mouseX, ev.mouseY)) {
            // A character's own script: a talk then offers its generic
            // lines and goodbyes (0x4162a3).
            runner_->setFromCharacter(true);
            runScript(*who);
        }
        else
            walkTo(ev.mouseX, ev.mouseY);
        return;
    }
    if (ev.type != EventType::KeyDown)
        return;
    if (const int d = step(ev.key, index_, int(names_.size()))) {
        index_ += d;
        load();
        return;
    }
    const int n = int(scene_.objects().size());
    if (ev.key == key::Up || ev.key == key::Down) {
        if (n > 0)
            selected_ = ev.key == key::Down ? (selected_ + 1 >= n ? -1 : selected_ + 1)
                                            : (selected_ < 0 ? n - 1 : selected_ - 1);
        draw();
    } else if (ev.key == 'h') {
        hotspots_ = !hotspots_;
        draw();
    } else if (ev.key == 'g') {
        showGrid_ = !showGrid_;
        draw();
    } else if (ev.key == 'm' || ev.key == 'w') {
        int& v = state_.values[ev.key == 'm' ? "moving" : "weather"];
        v = !v;
        draw();
    } else if (ev.key == 'i') {
        info_ = !info_;
        draw();
    } else if (ev.key == 'c' && renderer_) {
        showCharacters_ = !showCharacters_;
        if (showCharacters_ && characters_.empty())
            loadCharacters();
        draw();
    }
}

// --- Movies ---------------------------------------------------------------------

MoviePage::MoviePage(ViewerContext& ctx) : Page(ctx) {
    names_ = listFiles(ctx.files, {"movies/", "elements/", "locations/"}, {".bik"});
}

MoviePage::~MoviePage() { stop(); }

void MoviePage::activate() {
    begin2D();
    open();
}

void MoviePage::deactivate() { stop(); }

void MoviePage::stop() {
    if (ctx_.mixer && sound_ != audio::kNoSound)
        ctx_.mixer->stop(sound_);
    sound_ = audio::kNoSound;
}

void MoviePage::open() {
    stop();
    file_.reset();
    video_.reset();
    soundtrack_.reset();
    frame_ = image::Image();
    subtitles_.clear();
    info_.clear();
    next_ = 0;
    paused_ = false;
    clear(0, 0, 0);
    if (names_.empty())
        return;
    const std::string& name = names_[size_t(index_)];
    {
        // Its subtitles, if it has some: Text/Movies/<its name>.txt.
        std::string stem = name.substr(name.find_last_of('/') + 1);
        stem = stem.substr(0, stem.rfind('.'));
        std::vector<uint8_t> text;
        if (!ctx_.files.read("text/movies/" + fs::toLower(stem) + ".txt", text))
            subtitles_ = parseSubtitles(decodeText(text));
    }
    std::vector<uint8_t> data;
    auto file = std::make_unique<video::BinkFile>();
    auto video = std::make_unique<video::BinkVideoDecoder>();
    Error e = ctx_.files.read(name, data);
    if (!e)
        e = file->open(std::move(data));
    if (!e)
        e = video->init(file->width(), file->height(), file->revision(), file->flags());
    if (e) {
        info_ = e.message;
        return;
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "%dx%d, %zu frames at %.2f fps, revision %c", file->width(), file->height(),
                  file->frameCount(), file->fps(), file->revision());
    info_ = buf;
    // The soundtrack, decoded whole and played alongside.
    if (!file->audioTracks().empty()) {
        const video::BinkAudioTrack& t = file->audioTracks()[0];
        video::BinkAudioDecoder dec;
        auto sound = std::make_shared<audio::Sound>();
        if (Error ae = dec.init(t.sampleRate, t.channels, t.dct)) {
            info_ += ", sound: " + ae.message;
        } else {
            for (size_t i = 0; i < file->frameCount(); i++) {
                video::BinkFile::Frame f;
                if (!file->frame(i, f) && !f.audio.empty() && f.audio[0].size)
                    dec.decode(f.audio[0].data, f.audio[0].size, sound->samples, f.audioBytes[0]);
            }
            sound->rate = t.sampleRate;
            sound->channels = t.channels;
            sound->format = 0;
            info_ += ", sound " + std::to_string(t.sampleRate) + " Hz";
            soundtrack_ = sound;
            if (ctx_.mixer)
                sound_ = ctx_.mixer->play(sound, audio::SoundType::Speech);
        }
    }
    file_ = std::move(file);
    video_ = std::move(video);
    startMs_ = ctx_.sys.getMillis();
    ctx_.sys.setWindowTitle("Movies: " + name);
}

void MoviePage::update(uint64_t nowMs) {
    if (file_ && !paused_ && file_->frameCount() > 0 && file_->fps() > 0) {
        // Decode up to the frame due now (skipping none: each builds on the last).
        const size_t due = size_t(double(nowMs - startMs_) * file_->fps() / 1000.0);
        bool drawn = false;
        while (next_ <= due && next_ < file_->frameCount()) {
            video::BinkFile::Frame f;
            Error e = file_->frame(next_, f);
            if (!e)
                e = video_->decode(f.video.data, f.video.size);
            if (e)
                info_ = "frame " + std::to_string(next_) + ": " + e.message;
            next_++;
            drawn = true;
        }
        if (next_ >= file_->frameCount() && due >= file_->frameCount() && loop_ && !soundtrack_) {
            // Video loops (no sound) start over.
            open();
            return;
        }
        if (drawn) {
            video_->toRgba(frame_.rgba);
            frame_.w = video_->width();
            frame_.h = video_->height();
        }
    }
    clear(0, 0, 0);
    if (frame_.w)
        image::blit(ctx_.screen, frame_, (kScreenW - frame_.w) / 2, (kScreenH - frame_.h) / 2);
    // The subtitles due at the frame shown, at the bottom in the game's
    // dialogue style.
    const TextStyle* style = ctx_.fonts ? ctx_.fonts->style("dialogue") : nullptr;
    if (style && style->font && file_ && file_->fps() > 0) {
        const double t = double(next_) / file_->fps();
        std::vector<std::string> lines;
        for (const Subtitle& sub : subtitles_)
            if (t >= sub.start && t < sub.end)
                for (const std::string& l : style->font->wrap(sub.line, style->pixels, kScreenW - 160))
                    lines.push_back(l);
        const int lh = style->font->lineHeight(style->pixels);
        int y = kScreenH - 40 - int(lines.size()) * lh;
        for (const std::string& l : lines) {
            ctx_.fonts->draw(ctx_.screen, *style, (kScreenW - style->font->width(l, style->pixels)) / 2, y, l,
                             {255, 255, 255});
            y += lh;
        }
    }
    label(4, 2,
          (names_.empty() ? std::string("No movies") : names_[size_t(index_)]) + "  (" + std::to_string(index_ + 1) +
              "/" + std::to_string(names_.size()) + ")  " + info_ +
              (file_ ? "  frame " + std::to_string(next_) + "/" + std::to_string(file_->frameCount()) : "") +
              (subtitles_.empty() ? "" : ", " + std::to_string(subtitles_.size()) + " subtitles"),
          {255, 230, 150});
    help("Left/Right: movie  Space: pause  Enter: restart");
    Page::update(nowMs);
}

void MoviePage::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    if (const int d = step(ev.key, index_, int(names_.size()))) {
        index_ += d;
        open();
    } else if (ev.key == key::Return) {
        open();
    } else if (ev.key == key::Space && file_) {
        paused_ = !paused_;
        if (paused_) {
            stop();
        } else {
            // Carry on from the frame shown, the sound from the same time.
            startMs_ = ctx_.sys.getMillis() - uint64_t(double(next_) * 1000.0 / file_->fps());
            if (soundtrack_ && ctx_.mixer) {
                auto rest = std::make_shared<audio::Sound>(*soundtrack_);
                const size_t from = std::min(rest->samples.size(),
                                             size_t(double(next_) / file_->fps() * rest->rate) * size_t(rest->channels));
                rest->samples.erase(rest->samples.begin(), rest->samples.begin() + ptrdiff_t(from));
                sound_ = ctx_.mixer->play(rest, audio::SoundType::Speech);
            }
        }
    }
}

// --- Text ---------------------------------------------------------------------

TextPage::TextPage(ViewerContext& ctx) : Page(ctx) { names_ = listFiles(ctx.files, {"text/"}, {".txt"}); }

void TextPage::activate() {
    begin2D();
    load();
}

void TextPage::load() {
    scroll_ = 0;
    lines_.clear();
    if (names_.empty())
        return draw();
    std::vector<uint8_t> data;
    if (Error e = ctx_.files.read(names_[size_t(index_)], data)) {
        lines_.push_back(e.message);
        return draw();
    }
    const std::string text = decodeText(data);
    const size_t width = size_t(kScreenW - 8) / font::kAdvance;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // A table's empty rows are only tabs.
        if (line.find_first_not_of("\t ") == std::string::npos && !line.empty()) {
            start = end + 1;
            continue;
        }
        for (size_t i; (i = line.find('\t')) != std::string::npos;)
            line.replace(i, 1, " | ");
        // Wrapped to the screen's width.
        do {
            size_t cut = line.size();
            if (font::length(line) > width) {
                cut = 0;
                for (size_t chars = 0, i = 0; i < line.size() && chars < width; chars++)
                    text::nextUtf8(line, i), cut = i;
            }
            lines_.push_back(line.substr(0, cut));
            line = cut < line.size() ? "    " + line.substr(cut) : std::string();
        } while (!line.empty());
        start = end + 1;
    }
    draw();
}

void TextPage::draw() {
    clear(20, 22, 30);
    const int rows = (kScreenH - 2 * font::kLineHeight - 8) / font::kLineHeight;
    label(4, 2,
          (names_.empty() ? std::string("No text files") : names_[size_t(index_)]) + "  (" +
              std::to_string(index_ + 1) + "/" + std::to_string(names_.size()) + "), lines " +
              std::to_string(scroll_ + 1) + "-" + std::to_string(std::min(int(lines_.size()), scroll_ + rows)) +
              " of " + std::to_string(lines_.size()),
          {255, 230, 150});
    for (int i = 0; i < rows && size_t(scroll_ + i) < lines_.size(); i++)
        text(4, font::kLineHeight + 6 + i * font::kLineHeight, lines_[size_t(scroll_ + i)]);
    help("Left/Right: file  Up/Down, PgUp/PgDn (with Shift: file by 10): scroll");
    present();
    ctx_.sys.setWindowTitle("Text: " + (names_.empty() ? std::string() : names_[size_t(index_)]));
}

void TextPage::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    const int rows = (kScreenH - 2 * font::kLineHeight - 8) / font::kLineHeight;
    const int maxScroll = std::max(0, int(lines_.size()) - rows);
    const bool shift = ev.mods & keymod::Shift;
    if (ev.key == key::Left || ev.key == key::Right || ((ev.key == key::PageUp || ev.key == key::PageDown) && shift)) {
        if (const int d = step(ev.key, index_, int(names_.size()))) {
            index_ += d;
            load();
        }
    } else if (ev.key == key::Up || ev.key == key::Down || ev.key == key::PageUp || ev.key == key::PageDown ||
               ev.key == key::Home || ev.key == key::End) {
        const int by = ev.key == key::Up ? -1 : ev.key == key::Down ? 1 : ev.key == key::PageUp ? -rows
                     : ev.key == key::PageDown ? rows : ev.key == key::Home ? -int(lines_.size()) : int(lines_.size());
        scroll_ = std::clamp(scroll_ + by, 0, maxScroll);
        draw();
    }
}

// --- Sounds ---------------------------------------------------------------------

SoundPage::SoundPage(ViewerContext& ctx) : Page(ctx) {
    names_ = listFiles(ctx.files, {"sound/", "models/"}, {".wav", ".mp3"});
}

void SoundPage::activate() {
    begin2D();
    draw();
}

void SoundPage::deactivate() {
    if (ctx_.mixer)
        ctx_.mixer->stopAll();
    playing_ = audio::kNoSound;
}

void SoundPage::play() {
    info_.clear();
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (Error e = ctx_.files.read(names_[size_t(index_)], data)) {
        info_ = e.message;
    } else if (Error e2 = audio::decodeSound(data, *sound)) {
        info_ = e2.message;
    } else {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s, %d Hz, %d channel%s, %.2f s", audio::formatName(sound->format),
                      sound->rate, sound->channels, sound->channels == 1 ? "" : "s",
                      sound->rate ? double(sound->frames()) / sound->rate : 0.0);
        info_ = buf;
        if (ctx_.mixer) {
            ctx_.mixer->stopAll();
            playing_ = ctx_.mixer->play(sound, audio::SoundType::Effects);
        } else {
            info_ += " (no audio device)";
        }
    }
    draw();
}

void SoundPage::draw() {
    clear(18, 24, 22);
    const int rows = (kScreenH - 4 * font::kLineHeight - 8) / font::kLineHeight;
    const int first = std::clamp(index_ - rows / 2, 0, std::max(0, int(names_.size()) - rows));
    label(4, 2, std::to_string(names_.size()) + " sounds" + (info_.empty() ? "" : ":  " + info_), {255, 230, 150});
    for (int i = 0; i < rows && first + i < int(names_.size()); i++) {
        const bool current = first + i == index_;
        const int y = 2 * font::kLineHeight + 4 + i * font::kLineHeight;
        if (current)
            fill(0, y - 1, kScreenW, font::kLineHeight, {60, 80, 70});
        text(8, y, names_[size_t(first + i)], current ? Color{255, 255, 255} : Color{190, 200, 195});
    }
    help("Up/Down (Left/Right): sound  PgUp/PgDn: 10  Space/Enter: play  S: stop");
    present();
    ctx_.sys.setWindowTitle("Sounds");
}

void SoundPage::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown || names_.empty())
        return;
    const uint32_t k = ev.key == key::Up ? key::Left : ev.key == key::Down ? key::Right : ev.key;
    if (const int d = step(k, index_, int(names_.size()))) {
        index_ += d;
        info_.clear();
        draw();
    } else if (ev.key == key::Space || ev.key == key::Return) {
        play();
    } else if (ev.key == 's' && ctx_.mixer) {
        ctx_.mixer->stopAll();
    }
}

} // namespace reluva::awe
