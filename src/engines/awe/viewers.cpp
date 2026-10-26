// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/viewers.h"

#include "common/audio/decode.h"
#include "common/font.h"
#include "common/fs.h"
#include "common/text.h"
#include "common/video/bink_audio.h"
#include "engines/awe/camera.h"
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
            uint8_t* p = s.row(yy) + xx * 4;
            p[0] = uint8_t((c.r * alpha + p[0] * (255 - alpha)) / 255);
            p[1] = uint8_t((c.g * alpha + p[1] * (255 - alpha)) / 255);
            p[2] = uint8_t((c.b * alpha + p[2] * (255 - alpha)) / 255);
            p[3] = std::max(p[3], alpha); // for overlays, transparent where nothing is drawn
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

ScenePage::ScenePage(ViewerContext& ctx) : Page(ctx) { names_ = listFiles(ctx.files, {"locations/"}, {".ac2"}); }

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
    } else {
        scene_ = std::move(variant);
    }
    if (renderer_ && showCharacters_)
        loadCharacters();
    draw();
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
                    e = awe::loadAnimation(gr2, *anim);
                a = animations_.emplace(path, e ? nullptr : anim).first;
            }
            if (a->second) {
                c.animation = a->second;
                c.animationName = t;
                break;
            }
        }
        c.placement = characterMatrix(o->f("x"), o->f("y"), o->f("z"), o->f("rotY"));
        characters_.push_back(std::move(c));
    }
}

void ScenePage::draw() {
    const std::vector<const Ac2Record*> objects = scene_.objects();
    // Pictures back to front, by depth: the background at -10000, what's
    // behind its windows further still. A picture's d0, when set, is its
    // depth instead of z (the view outside a window: d0 -11000, z 0).
    std::vector<const Ac2Record*> pictures;
    for (const Ac2Record* o : objects)
        if (hasExtension(o->text("file"), {".jpg", ".png", ".tga", ".bmp"}))
            pictures.push_back(o);
    auto depth = [](const Ac2Record* o) { return o->f("d0") != 0 ? o->f("d0") : o->f("z"); };
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
                if (const image::Image& img = picture(o->text("file")); img.w > 0)
                    image::blit(ctx_.screen, img, int(o->f("x")), int(o->f("y")));
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
            if (const image::Image& img = picture(o->text("file")); img.w > 0)
                blitOver(ctx_.screen, img, int(o->f("x")), int(o->f("y")));
    if (hotspots_)
        for (const Ac2Record* o : objects) {
            const int l = o->i("left"), t = o->i("top"), r = o->i("right"), b = o->i("bottom");
            if (o->tag != "SPOT" || r <= l || b <= t)
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
        const image::Image& img = picture(o->text("file"));
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
    help(std::string("Left/Right: scene  Up/Down: object  H: hotspots  I: info") +
         (renderer_ ? "  C: characters" : "  (no 3D: no characters)"));
    if (!renderer_)
        present();
    ctx_.sys.setWindowTitle("Scenes: " + (names_.empty() ? std::string() : names_[size_t(index_)]));
}

void ScenePage::update(uint64_t nowMs) {
    if (!renderer_)
        return Page::update(nowMs);
    if (startMs_ == 0)
        startMs_ = nowMs;
    const float time = float(nowMs - startMs_) / 1000.0f;
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
        for (const Character& c : characters_)
            c.model->draw(poseAt(c.model->model(), c.animation.get(), time), c.placement, false, frameMeshes_);
    }
    prepareScreenshot(renderer_);
    renderer_->endFrame(&ctx_.screen);
    finishScreenshot(renderer_, &ctx_.screen);
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
    info_.clear();
    next_ = 0;
    paused_ = false;
    clear(0, 0, 0);
    if (names_.empty())
        return;
    const std::string& name = names_[size_t(index_)];
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
                    dec.decode(f.audio[0].data, f.audio[0].size, sound->samples);
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
            video_->toRgba(rgba_);
            clear(0, 0, 0);
            image::Image img;
            img.w = video_->width();
            img.h = video_->height();
            img.rgba = rgba_;
            image::blit(ctx_.screen, img, (kScreenW - img.w) / 2, (kScreenH - img.h) / 2);
        }
    }
    label(4, 2,
          (names_.empty() ? std::string("No movies") : names_[size_t(index_)]) + "  (" + std::to_string(index_ + 1) +
              "/" + std::to_string(names_.size()) + ")  " + info_ +
              (file_ ? "  frame " + std::to_string(next_) + "/" + std::to_string(file_->frameCount()) : ""),
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
