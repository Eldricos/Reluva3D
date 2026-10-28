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
#include <utility>

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

void Page::drawSubtitles(const std::vector<Subtitle>& subtitles, double seconds) {
    // Those due at that time, at the bottom in the game's dialogue style.
    const TextStyle* style = ctx_.fonts ? ctx_.fonts->style("dialogue") : nullptr;
    if (!style || !style->font)
        return;
    std::vector<std::string> lines;
    for (const Subtitle& sub : subtitles)
        if (seconds >= sub.start && seconds < sub.end)
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

// The part after the last slash or backslash.
std::string fileNameOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string folderOf(const std::string& name) {
    const size_t slash = name.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : name.substr(0, slash);
}

// The rows named for the movie: the game starts its subtitles as the
// conversation of that name (0x42ae7d), so logos.txt, a copy of intro's,
// shows nothing.
std::vector<Subtitle> moviesOwn(std::vector<Subtitle> all, const std::string& movie) {
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&](const Subtitle& s) { return !s.name.empty() && fs::toLower(s.name) != movie; }),
              all.end());
    return all;
}

} // namespace

ScenePage::ScenePage(ViewerContext& ctx) : Page(ctx) {
    names_ = listFiles(ctx.files, {"locations/"}, {".ac2"});
    state_.values["moving"] = 1;
    runner_ = std::make_unique<ScriptRunner>(state_, *this);
    dialogue_ = std::make_unique<DialogueManager>(state_, *this, *this);
    dialogue_->onLog = [this](const std::string& l) { logScript("    " + shorten(l, 150)); };
    story_ = std::make_unique<Story>(state_, *runner_);
    loadOptions();
    state_.action = [this](const std::string& w) {
        if (optionsMenu_)
            return true;
        if (puzzleActive())
            return puzzleCondition(w);
        return slotCondition(w);
    };
    state_.canTalk = [this](const std::vector<std::string>& words) {
        loadDialogue();
        const TalkArgs t = parseTalkArgs(words, true);
        return dialogue_->canTalk(t.name, t.part, t.thread, t.nest, t.gen, t.number, t.plus, t.greet);
    };
    {
        std::vector<uint8_t> data;
        if (!ctx.files.read("text/acts.txt", data))
            story_->setActs(readActs(parseTable(decodeText(data))));
    }
    story_->onActTables = [this](const std::string&) { loadDialogue(); };
    story_->onLoadLocation = [this](const std::string& location, bool flush) {
        // Reluva reads scenes as they're shown: loading a location only
        // forgets, for a new place, how the scenes were left.
        logScript("  (location " + location + (flush ? ", a new place" : "") + ")");
        if (flush) {
            hiddenNames_.clear();
            positions_.clear();
        }
        beginLoad(location, true);
    };
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
    applyAntiAliasing();
    load();
}

void ScenePage::deactivate() {
    if (!systemCursor_)
        ctx_.sys.setMouseCursorVisible(systemCursor_ = true);
    releaseGpu();
    if (renderer_)
        renderer_->setMultisampling(1); // the other pages draw without
    renderer_ = nullptr;
}

void ScenePage::releaseGpu() {
    characters_.clear();
    models_.clear();
    animations_.clear();
    if (renderer_) {
        for (gfx::MeshId m : frameMeshes_)
            renderer_->destroyMesh(m);
        for (gfx::TextureId t : frameTextures_)
            renderer_->destroyTexture(t);
        for (PictureLayer& l : layers_)
            if (l.texture)
                renderer_->destroyTexture(l.texture);
        if (quad_)
            renderer_->destroyMesh(quad_);
    }
    frameMeshes_.clear();
    frameTextures_.clear();
    layers_.clear();
    characterOrder_.clear();
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
    scrolls_.clear();
    if (!playing_)
        state_.act.clear();
    grid_ = WalkGrid();
    path_.clear();
    hover_ = {};
    scriptObject_ = nullptr;
    resumeAfter_ = nullptr;
    sceneShown_ = true;
    leadGoal_ = {};
    slots_.reset();
    optionsMenu_.reset();
    closePuzzle();
    shownNames_.clear();
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
    if (!base.empty() && ctx_.files.has(base) && !read(base, scene_)) {
        // Merged as 0x435990's mode 2 does: the base's camera and settings
        // stay (the variant's CAMR, SHOW, PUZZ... are read and dropped:
        // Calais.14-15_14.02_06a and _06b have one); music and events are
        // added; an object is added unless the base has it already (the
        // same tag, file and place).
        base_ = base;
        if (!scene_.records.empty() && scene_.records.back().tag == "DONE")
            scene_.records.pop_back();
        auto same = [](const Ac2Record& a, const Ac2Record& b) {
            // (the player, PCXX, by its file alone)
            return a.tag == b.tag && fs::toLower(a.text("file")) == fs::toLower(b.text("file")) &&
                   (a.tag == "PCXX" || (a.f("x") == b.f("x") && a.f("y") == b.f("y") && a.f("z") == b.f("z")));
        };
        const size_t baseCount = scene_.records.size();
        for (const Ac2Record& r : variant.records) {
            if (r.tag == "CAMR" || r.tag == "SHOW" || r.tag == "PUZZ" || r.tag == "FAKE" || r.tag == "OUTD" ||
                r.tag == "NVNT" || r.tag == "FOOT" || r.tag == "SCEN")
                continue;
            if (isAc2Object(r.tag) && !r.text("file").empty() &&
                std::any_of(scene_.records.begin(), scene_.records.begin() + long(baseCount),
                            [&](const Ac2Record& b) { return same(r, b); }))
                continue;
            scene_.records.push_back(r);
        }
        // The act it's for: "salon_01_06a.ac2" is act 06a's.
        if (!playing_)
            state_.act = name.substr(name.rfind('_') + 1, 3);
    } else {
        scene_ = std::move(variant);
    }
    if (const Ac2Record* cam = scene_.camera())
        grid_.load(*cam);
    // This scene's name before its characters load: where they were kept
    // is looked up by it.
    state_.scene = fs::toLower(name.substr(name.find_last_of('/') + 1));
    {
        // Its folder is its location: locations/<location>/<scene>.ac2.
        const std::string lower = fs::toLower(name);
        const size_t from = lower.find('/') + 1;
        location_ = state_.location = lower.substr(from, lower.find('/', from) - from);
    }
    // In a game at once; when browsing, once the page stays on the scene
    // (they take seconds to load).
    charactersDue_ = renderer_ && showCharacters_;
    if (charactersDue_ && playing_) {
        charactersDue_ = false;
        loadCharacters();
    }
    charactersAt_ = lastMs_;
    loadLoops();
    for (const Ac2Record* o : scene_.objects())
        if (o->tag == "PART" || o->tag == "RAIN" || o->tag == "SNOW") {
            ParticleSystem& system =
                particles_.emplace(o, ParticleSystem(ParticleSettings::fromRecord(*o))).first->second;
            // SYST warmUp: run for 4.5 s in steps of 0.01 at load, so the
            // snow is already falling (0x42e68b, 0x42cbb0).
            if (const Ac2Record* syst = o->part("SYST"); syst && syst->i("warmUp") != 0)
                for (float t = 0; t < 4.5f; t += 0.01f)
                    system.update(0.01f);
        } else if (const Ac2Record* sprt = o->part("SPRT"); sprt && sprt->i("scroll") != 0) {
            scrolls_[o] = Scroll{o->f("x"), o->f("y"), 0};
        }
    if (playing_) {
        enterScene();
    } else {
        // Browsing: the scene's puzzle works too (its scripts' talk and
        // tags as in a game).
        for (const Ac2Record& r : scene_.records)
            if (r.tag == "PUZZ") {
                const std::string kind = fs::toLower(r.text("value"));
                if (kind == "tornletter" || kind == "safe" || kind == "puzzlebox")
                    openPuzzle(kind);
            }
    }
    draw();
}

void ScenePage::enterScene() {
    playMusic();
    // The save and load screens (PUZZ save, load).
    for (const Ac2Record& r : scene_.records)
        if (r.tag == "PUZZ" && (fs::toLower(r.text("value")) == "save" || fs::toLower(r.text("value")) == "load"))
            openSlots(fs::toLower(r.text("value")) == "save");
        else if (r.tag == "PUZZ" && fs::toLower(r.text("value")) == "options")
            openOptions();
        else if (r.tag == "PUZZ")
            openPuzzle(fs::toLower(r.text("value")));
    // A saved game being restored runs no scene events (0x436331).
    if (restoring_)
        return;
    // The first event whose condition holds, queued (0x436a90).
    for (const Ac2Record& r : scene_.records)
        if (r.tag == "EVNT")
            if (const Ac2Value* v = r.field("events"))
                for (const auto& [condition, script] : v->pairs)
                    if (testCondition(condition, state_)) {
                        logScript("scene event: " + shorten(script, 120));
                        runner_->run(script);
                        goto queued;
                    }
queued:
    if (restoring_)
        return;
    // The visit, after the events: "!beenhere this" holds during the
    // first visit's.
    std::string& acts = state_.texts[state_.scene];
    if ((" " + acts + " ").find(" " + state_.act + " ") == std::string::npos)
        acts += (acts.empty() ? "" : " ") + state_.act;
}

void ScenePage::playMusic() {
    // Every MUSC entry whose condition holds plays, looping; what played
    // before and isn't among them stops (0x436900). Shared ambient loops
    // go on from scene to scene.
    std::set<std::string> want;
    for (const Ac2Record& r : scene_.records)
        if (r.tag == "MUSC")
            if (const Ac2Value* v = r.field("music"))
                for (const auto& [condition, file] : v->pairs)
                    if (testCondition(condition, state_))
                        want.insert(fs::toLower(file));
    for (auto it = music_.begin(); it != music_.end();) {
        if (!want.count(it->first)) {
            if (ctx_.mixer)
                ctx_.mixer->stop(it->second);
            it = music_.erase(it);
        } else {
            ++it;
        }
    }
    for (const std::string& file : want)
        playLooping(file, file.find("music") != std::string::npos);
}

void ScenePage::playLooping(const std::string& file, bool music) {
    if (music_.count(file))
        return;
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (!ctx_.mixer || ctx_.files.read(file, data) || audio::decodeSound(data, *sound))
        return;
    music_[file] = ctx_.mixer->play(sound, music ? audio::SoundType::Music : audio::SoundType::Effects,
                                    audio::kMaxVolume, 0, true);
}

bool ScenePage::startMovie(ScriptRunner& runner, const ScriptEvent& e, bool wait) {
    const std::string name = fs::toLower(e.arg(1));
    auto flag = [&](const char* f) { return fs::toLower(e.arg(2)) == f || fs::toLower(e.arg(3)) == f; };
    // FMV\, then Movies\ (0x42ac70); none: the event ends at once.
    std::string path;
    for (const std::string& p : {"fmv/" + name + ".bik", "movies/" + name + ".bik", name + ".bik"})
        if (path.empty() && ctx_.files.has(p))
            path = p;
    std::vector<uint8_t> data;
    if (path.empty() || ctx_.files.read(path, data)) {
        logScript("    (no movie " + name + ")");
        return false;
    }
    // Unless "inscene", the scene is left for it and every sound stops
    // (0x436410, 0x425250); "silent" plays it without its own.
    if (!flag("inscene")) {
        sceneShown_ = false;
        for (const auto& [file, handle] : music_)
            if (ctx_.mixer)
                ctx_.mixer->stop(handle);
        music_.clear();
    }
    auto player = std::make_unique<MoviePlayer>(ctx_.mixer);
    if (Error err = player->open(std::move(data), lastMs_, !flag("silent") && ctx_.mixer)) { // no sound to decode without a mixer
        logScript("    (" + name + ": " + err.message + ")");
        return false;
    }
    movie_ = std::move(player);
    movieName_ = name;
    // Its subtitles: Text\Movies\<name>.txt (the game plays them through
    // the dialogue in its movie mode).
    movieSubtitles_.clear();
    std::vector<uint8_t> text;
    if (!ctx_.files.read("text/movies/" + name + ".txt", text))
        movieSubtitles_ = moviesOwn(parseSubtitles(decodeText(text)), name);
    movieRunner_ = wait ? &runner : nullptr;
    return wait;
}

void ScenePage::endMovie() {
    movie_.reset();
    movieSubtitles_.clear();
    if (ScriptRunner* r = movieRunner_) {
        movieRunner_ = nullptr;
        r->finished("movie");
    }
    draw();
}

std::vector<std::string> ScenePage::locationFiles(const std::string& location) const {
    // A file's suffix (after its last '_', when it has two) applies if it's
    // the act's, or night, snow or moving when those hold; a base with a
    // variant that applies is loaded with it (0x43e3d0..0x43e520).
    auto suffix = [](const std::string& stem) {
        const size_t first = stem.find('_'), last = stem.rfind('_');
        return first != last && last != std::string::npos ? stem.substr(last + 1) : std::string();
    };
    auto value = [&](const char* name) {
        const auto it = state_.values.find(name);
        return it == state_.values.end() ? 0 : it->second;
    };
    auto applies = [&](const std::string& sfx) {
        return sfx == state_.act || (sfx == "night" && value("time")) || (sfx == "snow" && value("weather")) ||
               (sfx == "moving" && value("moving"));
    };
    std::vector<std::string> stems, files;
    const std::string folder = "locations/" + location + "/";
    for (const std::string& n : names_) {
        const std::string lower = fs::toLower(n);
        if (lower.compare(0, folder.size(), folder) == 0 && lower.find('/', folder.size()) == std::string::npos)
            stems.push_back(lower.substr(folder.size(), lower.size() - folder.size() - 4));
    }
    for (size_t i = 0; i < stems.size(); i++) {
        const std::string sfx = suffix(stems[i]);
        if (!sfx.empty()) {
            if (applies(sfx))
                files.push_back(folder + stems[i] + ".ac2");
            continue;
        }
        bool variant = false;
        for (const std::string& other : stems)
            if (other.size() > stems[i].size() + 1 && other.compare(0, stems[i].size() + 1, stems[i] + "_") == 0 &&
                applies(suffix(other)))
                variant = true;
        if (!variant)
            files.push_back(folder + stems[i] + ".ac2");
    }
    return files;
}

void ScenePage::beginLoad(const std::string& location, bool forAct) {
    // 0x422790: the loading screen when more than one file is to load,
    // faded in if the screen was faded out.
    const LocationLoad keep = load_;
    load_ = LocationLoad();
    load_.scene = keep.scene, load_.verb = keep.verb, load_.entry = keep.entry, load_.runner = keep.runner;
    load_.walkIn = keep.walkIn;
    load_.restore = keep.state != 0 && keep.restore; // a load under way's, not one over
    load_.forAct = forAct;
    load_.location = location;
    load_.files = locationFiles(location);
    load_.state = 2;
    if (load_.files.size() > 1) {
        load_.screen = true;
        if (fade_ >= 1) {
            fadeTarget_ = 0;
            load_.fadedIn = true;
        }
    }
    draw();
}

void ScenePage::updateLoad() {
    LocationLoad& l = load_;
    if (l.state == 1) {
        // A scene in another place: once faded out.
        if (fade_ >= 1) {
            if (l.restore) {
                // A saved game's location (states 7, 2, 3, 8).
                const SaveGame::Act& a = restoring_->act;
                ActRow row;
                row.act = state_.act;
                row.preLoad = a.preLoad, row.location = fs::toLower(a.location), row.postLoad = a.postLoad;
                row.completion = a.completion, row.event = a.event, row.music = a.music;
                row.time = a.timeOfDay, row.weather = a.weather, row.moving = a.moving;
                story_->restore(row, l.location);
                return;
            }
            if (playing_)
                story_->leavingScene();
            beginLoad(l.location, false);
        }
        return;
    }
    if (l.state == 2) {
        // One file an update (0x423b30), the wheel turning.
        if (l.next < l.files.size()) {
            // (Read for the time it takes: each is read again when shown,
            // so a tool that doesn't draw skips it.)
            std::vector<uint8_t> data;
            Ac2Scene scene;
            if (drawing_ && !ctx_.files.read(l.files[l.next], data))
                parseAc2(data, scene);
            l.next++;
            l.wheel++;
            draw();
            return;
        }
        l.state = 3;
        if (l.fadedIn)
            fadeTarget_ = 1;
        return;
    }
    if (l.state == 3) {
        if (fade_ != fadeTarget_)
            return;
        l.screen = false;
        location_ = state_.location = l.location; // its scenes are now the current location's
        if (l.forAct) {
            l.state = 0;
            story_->locationLoaded();
            if (l.restore && restoring_) {
                l.restore = false; // done: a later act's load isn't one
                finishRestore();
                if (ScriptRunner* r = l.runner) {
                    l.runner = nullptr;
                    r->finished(l.verb);
                }
            }
            return;
        }
        // The scene, faded in (states 6, 9).
        changeScene(l.scene, l.entry, l.walkIn);
        fadeTarget_ = 0;
        l.state = 4;
        return;
    }
    if (l.state == 4 && fade_ == fadeTarget_) {
        l.state = 0;
        if (ScriptRunner* r = l.runner) {
            l.runner = nullptr;
            r->finished(l.verb);
        }
    }
}

void ScenePage::newGame() {
    // As the game starts (0x4026e9): the main menu, whose event is
    // "reset"; its Play is "act", starting act 01A.
    playing_ = true;
    runner_->clear();
    // The state afresh; the dialogue's test of parts told and the screens'
    // "action" test stay, the tables are read again.
    auto partTold = std::move(state_.partTold);
    auto action = std::move(state_.action);
    auto canTalk = std::move(state_.canTalk);
    auto onTest = std::move(state_.onTest);
    state_ = GameState{};
    state_.partTold = std::move(partTold);
    state_.action = std::move(action);
    state_.canTalk = std::move(canTalk);
    state_.onTest = std::move(onTest);
    state_.values["master"] = 1; // the debug switch, off ([0x48b084] = 1)
    dialogueAct_ = "-";
    hiddenNames_.clear();
    positions_.clear();
    scriptLog_.clear();
    fade_ = fadeTarget_ = 0;
    movie_.reset();
    movieRunner_ = nullptr;
    soundRunner_ = nullptr;
    interfaceOff_ = false;
    letterbox_ = Letterbox();
    ui_ = Interface();
    sceneShown_ = true;
    load_ = LocationLoad();
    restoring_.reset();
    npcPositions_.clear();
    gameScene_.clear();
    previousMenu_.clear();
    yesNo_.reset();
    slots_.reset();
    optionsMenu_.reset();
    closePuzzle();
    logScript("new game");
    runner_->run("musica motoetheme, movie logos inscene, scene menu.main_01 1 0");
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
    if (!drawing_)
        return; // (only seen)
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
        // "Animated Effects" off: the flagged videos stay on their frame.
        if (frozen(*o) && !loop.frame.rgba.empty())
            continue;
        const size_t count = loop.file->frameCount();
        if (count == 0 || loop.file->fps() <= 0)
            continue;
        const size_t played = size_t(double(std::max(0.0f, time - loop.start)) * loop.file->fps());
        // BNKX loop 0 (its default is 1): played once, then held on its
        // last frame (0x4061d8).
        const Ac2Record* bnkx = o->part("BNKX");
        const bool once = bnkx && bnkx->field("loop") && bnkx->i("loop") == 0;
        const size_t want = once ? std::min(played, count - 1) : played % count;
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
    if (!renderer_ || !camr)
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
            Error e = openGr2(ctx_.files, file, gr2);
            if (!e)
                e = loadModel(gr2, model);
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
        const std::string folder = folderOf(file);
        std::string who = file.substr(folder.size() + 1);
        who = who.substr(0, who.find("_model"));
        c.x = o->f("x"), c.y = o->f("y"), c.z = o->f("z"), c.rotY = o->f("rotY");
        c.player = o->tag == "PCXX";
        c.who = fs::toLower(who);
        c.record = o;
        // A character following the player (FLLW, on its COND) arrives as
        // the player does (0x440070).
        const Ac2Record* fllw = o->part("FLLW");
        const Ac2Record* cond = o->part("COND");
        c.follower = !c.player && fllw && fllw->i("follow") != 0 &&
                     testCondition(cond ? cond->text("condition") : std::string(), state_);
        // Arriving by "scene <name> <n>": at its walk point n, else its
        // first; with -1 where they were when the scene was left
        // (0x440690).
        if ((c.player || c.follower) && entryPoint_ >= 0) {
            if (const Ac2Record* w = o->part("WALK"); w && !w->parts.empty()) {
                const Ac2Record* at = &w->parts.front();
                for (const Ac2Record& pt : w->parts)
                    if (pt.i("id") == entryPoint_)
                        at = &pt;
                c.x = at->f("x"), c.z = at->f("z"), c.rotY = at->f("rotY");
            }
        } else if (c.player && playing_) {
            if (const auto kept = positions_.find(state_.scene); kept != positions_.end())
                c.x = kept->second[0], c.z = kept->second[1], c.rotY = kept->second[2];
        }
        if (!c.player)
            if (const auto kept = npcPositions_.find(state_.scene + "|" + fs::toLower(fileNameOf(file)));
                kept != npcPositions_.end()) {
                const SaveGame::Placement& p = kept->second;
                c.x = p.position[0], c.y = p.position[1], c.z = p.position[2], c.rotY = p.rotation[1];
            }
        c.home = normalizeDegrees(c.rotY * 57.29578f);
        if (const Ac2Record* m = o->part("MODL"); m && m->field("shadow"))
            c.shadow = m->i("shadow") != 0;
        // Its animations: <who>_<name>.gr2 beside its model, loaded as
        // they're asked for.
        auto loader = [this, folder, who](const std::string& name) -> std::shared_ptr<const Animation> {
            const std::string path = folder + "/" + who + "_" + name + ".gr2";
            auto a = animations_.find(path);
            if (a == animations_.end()) {
                std::shared_ptr<Animation> anim;
                if (ctx_.files.has(path)) {
                    Gr2File gr2;
                    anim = std::make_shared<Animation>();
                    if (openGr2(ctx_.files, path, gr2) || awe::loadAnimation(gr2, *anim))
                        anim.reset();
                }
                a = animations_.emplace(path, anim).first;
            }
            return a->second;
        };
        c.actor = std::make_unique<Actor>(c.model->model(), o, loader,
                                          [this](const std::string& cond) { return testCondition(cond, state_); },
                                          unsigned(characters_.size() + 1));
        Actor* actor = c.actor.get();
        actor->onAnimDone = [this, actor](const std::string& name) {
            for (Character& ch : characters_)
                if (ch.actor.get() == actor && ch.waiting && ch.waitVerb == "anim" &&
                    (name == ch.waitAnim || name == ch.waitAnimAs)) {
                    ScriptRunner* r = ch.waiting;
                    ch.waiting = nullptr;
                    r->finished("anim");
                }
        };
        actor->onRotateDone = [this, actor]() {
            for (Character& ch : characters_)
                if (ch.actor.get() == actor && ch.waiting && ch.waitVerb == "rotate") {
                    ScriptRunner* r = ch.waiting;
                    ch.waiting = nullptr;
                    r->finished("rotate");
                }
        };
        actor->activate();
        // Walking in (0x440808): the player (and a follower) straight ahead
        // from the walk point, 100 units (a follower 50), unless the scene
        // was entered with its flag 0.
        if ((c.player || c.follower) && entryPoint_ >= 0 && entryWalk_ && startWalk(c)) {
            c.walkIn = c.follower ? 50.0f : 100.0f;
            logScript("    (" + c.who + " walks in)");
        }
        // Models MODL attaches at load, on their conditions (Poirot's hat,
        // Antoinette's coat; her snowshoes with "tag snowshoeson").
        if (const Ac2Record* m = o->part("MODL"))
            if (const Ac2Value* list = m->field("list"))
                for (const auto& [condition, path] : list->pairs) {
                    const std::string prop = normalizeArchiveName(path);
                    if (!testCondition(condition, state_))
                        continue;
                    if (auto pm = propModel(prop))
                        c.props.push_back({prop, pm});
                }
        placeCharacter(c);
        if (o->tag == "NPCX") {
            c.routine = std::make_unique<ScriptRunner>(state_, *this);
            c.routine->setCharacter(c.who);
            queueRoutine(c);
        }
        characters_.push_back(std::move(c));
    }
}

void ScenePage::draw() {
    if (!drawing_)
        return;
    const std::vector<const Ac2Record*> objects = scene_.objects();
    // Pictures and video loops back to front, by depth: the background at
    // -10000, what's behind its windows further still. A picture's d0,
    // when set, is its depth instead of z (the view outside a window: d0
    // -11000, z 0). Those whose condition doesn't hold now are left out
    // (the countryside going by: "moving >0").
    std::vector<const Ac2Record*> pictures;
    for (const Ac2Record* o : objects) {
        if (hidden(*o) || fogged(*o))
            continue;
        if (const auto ps = particles_.find(o); ps != particles_.end()) {
            // "Particle Effects" off: none drawn (0x42d4be).
            if (options_.get(GameOptions::Rain) && testCondition(ps->second.settings().condition, state_))
                pictures.push_back(o);
        } else if ((hasExtension(o->text("file"), {".jpg", ".png", ".tga", ".bmp"}) || loops_.count(o)) &&
                   (testCondition(o->text("condition"), state_) || shown(*o)) && !puzzleDraws(*o)) {
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
        if (const auto ov = overrides_.find(o); ov != overrides_.end()) {
            // Moved and turned by a puzzle: about its centre, tinted if
            // picked.
            const PictureOverride& p = ov->second;
            const float c = std::cos(p.roll), sn = std::sin(p.roll);
            t.roll = p.roll;
            t.x = p.x + p.cx - (p.cx * c - p.cy * sn);
            t.y = p.y + p.cy - (p.cx * sn + p.cy * c);
            if (p.tint) {
                image::Image tinted = img;
                for (size_t i = 2; i < tinted.rgba.size(); i += 4)
                    tinted.rgba[i] = uint8_t(tinted.rgba[i] * 3 / 4);
                return drawSprite(ctx_.screen, tinted, t, keepAlpha);
            }
            return drawSprite(ctx_.screen, img, t, keepAlpha);
        }
        if (optionsMenu_)
            // The options screen's slider thumbs, at their values.
            for (int i = 0; i < 3; i++)
                if (const Ac2Record* thumb = [&]() -> const Ac2Record* {
                        int n = i;
                        for (const Ac2Record* x : objects)
                            if (fs::toLower(fileNameOf(x->text("file"))) == "slider.jpg" && n-- == 0)
                                return x;
                        return nullptr;
                    }();
                    thumb == o)
                    t.x = float(thumbX(i));
        if (o->field("spriteScaleX"))
            t.scaleX = o->f("spriteScaleX"), t.scaleY = o->f("spriteScaleY");
        if (const Ac2Record* p = o->part(o->tag == "BINK" ? "BNKX" : "SPRT"))
            t.roll = p->f("roll");
        if (const auto sc = scrolls_.find(o); sc != scrolls_.end()) {
            // Scrolling: where it has got to, and again a width or height
            // behind, so the two cover the way (0x437db6).
            t.x = sc->second.x, t.y = sc->second.y;
            drawSprite(ctx_.screen, img, t, keepAlpha);
            const int way = o->part("SPRT")->i("scroll");
            t.x += way == 1 ? float(img.w) : way == 2 ? -float(img.w) : 0.0f;
            t.y += way == 3 ? float(img.h) : way == 4 ? -float(img.h) : 0.0f;
            return drawSprite(ctx_.screen, img, t, keepAlpha);
        }
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
        if (const auto ov = overrides_.find(o); ov != overrides_.end())
            return ov->second.depth;
        return o->f("depth") != 0 ? o->f("depth") : o->f("z");
    };
    std::stable_sort(pictures.begin(), pictures.end(),
                     [&](const Ac2Record* a, const Ac2Record* b) { return depth(a) < depth(b); });
    // With 3D, the pictures go in layers between the characters: the game
    // draws its objects back to front by depth, a character's being its z
    // (0x435d90, 0x4104b0), the object list's order where they're equal.
    // Layer k is drawn after the k-th character in that order (0: the
    // background, behind them all); the labels go into the overlay.
    const bool layered = renderer_ != nullptr;
    clear(0, 0, 0);
    if (layered) {
        auto objectIndex = [&](const Ac2Record* r) {
            const auto it = std::find(objects.begin(), objects.end(), r);
            return size_t(it - objects.begin());
        };
        // Footprints are objects too, added after the scene's own.
        characterOrder_.clear();
        if (charactersShown())
            for (size_t i = 0; i < characters_.size(); i++) {
                characterOrder_.push_back({i, -1});
                for (size_t k = 0; k < characters_[i].footprints.size(); k++)
                    characterOrder_.push_back({i, int(k)});
            }
        auto slotZ = [&](const ModelSlot& s) {
            const Character& c = characters_[s.character];
            return s.footprint < 0 ? c.z : c.footprints[size_t(s.footprint)].z;
        };
        auto slotIndex = [&](const ModelSlot& s) {
            const Character& c = characters_[s.character];
            return s.footprint < 0 ? objectIndex(c.record)
                                   : objects.size() + c.footprints[size_t(s.footprint)].serial;
        };
        std::stable_sort(characterOrder_.begin(), characterOrder_.end(), [&](const ModelSlot& a, const ModelSlot& b) {
            const float za = slotZ(a), zb = slotZ(b);
            return za != zb ? za < zb : slotIndex(a) < slotIndex(b);
        });
        auto layerOf = [&](const Ac2Record* o) {
            const float d = depth(o);
            const size_t at = objectIndex(o);
            size_t n = 0;
            for (const ModelSlot& s : characterOrder_) {
                const float z = slotZ(s);
                n += z < d || (z == d && slotIndex(s) < at);
            }
            return n;
        };
        // A picture drawn after a character takes the shadows off under
        // it, where its alpha isn't 0 (0x437b00): unless it's a fog or
        // cloud layer (SPRT scroll set) or at depth 10000 or more. Particles
        // don't (0x42d470).
        auto resetsShadows = [&](const Ac2Record* o) {
            if (particles_.count(o) || depth(o) >= 10000)
                return false;
            const Ac2Record* sprite = o->part("SPRT");
            return !sprite || sprite->i("scroll") == 0;
        };
        const bool shadows = options_.get(GameOptions::Shadows);
        for (PictureLayer& l : layers_)
            if (l.texture)
                renderer_->destroyTexture(l.texture);
        layers_.assign(characterOrder_.size() + 1, PictureLayer());
        std::vector<size_t> layerOfPicture;
        for (const Ac2Record* o : pictures)
            layerOfPicture.push_back(layerOf(o));
        gfx::TextureParams params;
        params.clamp = true;
        params.mipmaps = false;
        for (size_t k = 0; k < layers_.size(); k++) {
            PictureLayer& layer = layers_[k];
            bool any = false, allReset = true;
            for (size_t p = 0; p < pictures.size(); p++)
                if (layerOfPicture[p] == k)
                    any = true, allReset &= resetsShadows(pictures[p]);
            // A character's breath, drawn just after it (0x425e48); none
            // with "Particle Effects" off.
            const ParticleSystem* breath = nullptr;
            if (k > 0 && characterOrder_[k - 1].footprint < 0 && options_.get(GameOptions::Rain))
                breath = characters_[characterOrder_[k - 1].character].breath.get();
            any |= breath != nullptr;
            allReset &= breath == nullptr; // particles take no shadows off
            if (!any && k > 0)
                continue;
            auto takeResets = [&] {
                layer.resets.assign(size_t(kScreenW) * kScreenH, 0);
                for (size_t i = 0; i < layer.resets.size(); i++)
                    layer.resets[i] = ctx_.screen.pixels[i * 4 + 3] != 0;
            };
            const bool resets = shadows && k > 0;
            if (resets && !allReset) {
                // Those pictures alone first, for where they reset.
                std::fill(ctx_.screen.pixels.begin(), ctx_.screen.pixels.end(), 0);
                for (size_t p = 0; p < pictures.size(); p++)
                    if (layerOfPicture[p] == k && resetsShadows(pictures[p]))
                        place(pictures[p], true);
                takeResets();
            }
            // The background's layer opaque, the others over what's behind.
            if (k == 0)
                clear(0, 0, 0);
            else
                std::fill(ctx_.screen.pixels.begin(), ctx_.screen.pixels.end(), 0);
            if (breath)
                drawParticles(*breath, true);
            for (size_t p = 0; p < pictures.size(); p++)
                if (layerOfPicture[p] == k)
                    place(pictures[p], k > 0);
            if (resets && allReset)
                takeResets();
            applyBrightness();
            renderer_->createTexture(kScreenW, kScreenH, ctx_.screen.pixels.data(), params, layer.texture);
        }
        std::fill(ctx_.screen.pixels.begin(), ctx_.screen.pixels.end(), 0);
    } else {
        for (const Ac2Record* o : pictures)
            place(o, false);
        applyBrightness();
    }
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
                g += ", path of " + std::to_string(path_.size()) + " points";
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
                        (state_.values["weather"] ? "on" : "off") + (state_.values["time"] ? ", night" : "") +
                        (state_.act.empty() ? "" : ", act " + state_.act));
        if (const Ac2Record* cam = scene_.camera()) {
            char buf[200];
            std::snprintf(buf, sizeof buf, "camera about (%g, %g, %g), turned (%g, %g, %g), back (%g, %g, %g), fov %.1f deg",
                          cam->f("x"), cam->f("y"), cam->f("z"), cam->f("pitch"), cam->f("yaw"), cam->f("roll"),
                          cam->f("offsetX"), cam->f("offsetY"), cam->f("offsetZ"), cam->f("fov") * 57.29578f);
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
                // Where it is now, else where the scene puts it.
                float x = o->f("x"), y = o->f("y"), z = o->f("z"), rotY = o->f("rotY");
                for (const Character& c : characters_)
                    if (c.record == o && c.actor) {
                        x = c.x, y = c.y, z = c.z, rotY = c.rotY;
                        for (const Animator::Slot& sl : c.actor->animator().slots())
                            if (sl.state != Animator::Stopped)
                                anim += (anim.empty() ? ", playing " : " ") + sl.name;
                    }
                std::snprintf(buf, sizeof buf, "%s %s at (%g, %g, %g), facing %g%s", o->tag.c_str(),
                              o->text("file").c_str(), x, y, z, rotY, anim.c_str());
                lines.push_back(buf);
            }
        int y = 2;
        for (const std::string& l : lines) {
            label(4, y, shorten(l, 168), l == error_ ? Color{255, 120, 120} : Color{230, 230, 230});
            y += font::kLineHeight;
        }
    }
    // The bar and the inventory window, then a card held up (0x415227,
    // 0x418040): under the dialogue box.
    if (drawing_ && !load_.screen && !movie_) {
        drawInterface();
        // The letterbox over the bar, under a card (0x416ff0, at
        // 0x4155ef): black, from just above the top and up from the
        // bottom.
        if (letterbox_.mode != 0) {
            const int h = int(letterbox_.height);
            fill(0, 0, kScreenW, h, Color{0, 0, 0}, 255);
            fill(0, kScreenH - h, kScreenW, h, Color{0, 0, 0}, 255);
        }
        drawCards();
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
        // "Text Captions" off: no box or line unless choosing, or the radio
        // (0x43cf90).
        const bool radio = said && (said->name == "dia_barnabyham" || said->name == "hamradio");
        if (!options_.get(GameOptions::Captions) && !dialogue_->choosing() && !radio)
            said = nullptr;
        const auto areas = drawDialogueBox(ctx_.screen, *ctx_.fonts, pics, dialogue_->boxTop(),
                                           said ? said->text : std::string(), ctx_.fonts->colour(kColours[c]),
                                           dialogue_->choosing() ? choices : std::vector<std::string>(),
                                           dialogue_->highlighted());
        std::vector<std::pair<int, int>> rows;
        for (const auto& a : areas)
            rows.push_back({a.y, a.h});
        dialogue_->setChoiceAreas(std::move(rows));
    }
    if (slots_ && !load_.screen && !movie_)
        drawSlots();
    if (optionsMenu_ && !load_.screen && !movie_)
        drawOptions();
    if (box_ && !load_.screen && !movie_)
        drawPuzzleBox();
    if (messageTime_ > 0 && !yesNo_ && !load_.screen && !movie_)
        drawMessage();
    if (yesNo_ && ctx_.fonts) {
        // The box: the question and Yes / No as the dialogue's choices.
        DialogueBoxPictures pics;
        pics.window = &picture("interface/dialoguewin01.png");
        pics.bullet = &picture("interface/dialoguewin04.png");
        pics.bulletOn = &picture("interface/dialoguewin05.png");
        const auto areas = drawDialogueBox(ctx_.screen, *ctx_.fonts, pics, kScreenH - 200, yesNo_->text,
                                           ctx_.fonts->colour("popup menu text"),
                                           {interfaceText("yes"), interfaceText("no")}, -1);
        yesNo_->rows.clear();
        for (const auto& a : areas)
            yesNo_->rows.push_back({a.y, a.h});
    }
    if (load_.screen) {
        // The location's picture, the wheel turning at its foot (0x422ff0).
        const image::Image& back = picture("interface/loading/loading_" +
                                           [&] {
                                               const std::string& l = load_.location;
                                               const size_t dot = l.find('.');
                                               if (dot == std::string::npos)
                                                   return l;
                                               return l.find("exterior") != std::string::npos ||
                                                              l.find("sirkeci") != std::string::npos
                                                          ? l.substr(dot + 1)
                                                          : l.substr(0, dot);
                                           }() + ".jpg");
        fill(0, 0, kScreenW, kScreenH, Color{0, 0, 0});
        image::blit(ctx_.screen, back.w ? back : picture("interface/loading/loading_generic.jpg"), 0, 0);
        char wheel[48];
        std::snprintf(wheel, sizeof wheel, "interface/loading/wheels%02d.png", load_.wheel % 7 + 1);
        const image::Image& w = picture(wheel);
        image::blit(ctx_.screen, w, (kScreenW - w.w) / 2, 550);
        if (fade_ > 0)
            fill(0, 0, kScreenW, kScreenH, Color{0, 0, 0}, uint8_t(fade_ * 255));
    } else if (movie_ || !sceneShown_) {
        // The movie over everything, else black: no scene is shown.
        fill(0, 0, kScreenW, kScreenH, Color{0, 0, 0});
        if (movie_ && movie_->frame().w) {
            const image::Image& f = movie_->frame();
            image::blit(ctx_.screen, f, (kScreenW - f.w) / 2, (kScreenH - f.h) / 2);
            if (options_.get(GameOptions::Captions))
                drawSubtitles(movieSubtitles_, movie_->seconds());
        }
    } else if (fade_ > 0) {
        fill(0, 0, kScreenW, kScreenH, Color{0, 0, 0}, uint8_t(fade_ * 255));
    }
    help(std::string("Left/Right: scene  Up/Down: object  H: hotspots  I (F9 in a game): info  M: moving  W: weather  "
                     "T: night  G: grid  N: new game  click: walk") +
         (renderer_ ? "  C: characters" : "  (no 3D: no characters)"));
    // The game's cursor over everything; the system's hidden in a game
    // (0x413f1f), and while the game's shows.
    drawCursor();
    if (const bool system = !playing_ && !cursorShown(); system != systemCursor_)
        ctx_.sys.setMouseCursorVisible(systemCursor_ = system);
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
    if (movie_) {
        // Over when its last frame's time has gone by (0x40c490).
        if (movie_->update(nowMs))
            draw();
        if (movie_->done(nowMs))
            endMovie();
    }
    if (load_.state)
        updateLoad();
    if (slots_)
        slots_->sinceClick += dt;
    // The message's time and the caret's blink go on in every scene
    // (0x414400).
    if (messageTime_ > 0) {
        messageTime_ = std::max(0.0f, messageTime_ - dt);
        if (messageTime_ == 0)
            draw();
    }
    if ((caretTime_ -= dt) <= 0) {
        caretTime_ = 0.33f;
        caretOn_ = !caretOn_;
        if (slots_ && slots_->save)
            draw();
    }
    if (puzzleActive())
        puzzleUpdate(dt);
    if (charactersDue_ && nowMs >= charactersAt_ + 400) {
        charactersDue_ = false;
        loadCharacters();
        draw();
    }
    if (playing_)
        story_->update(dt);
    if (letterbox_.mode >= 2) {
        // 250 pixels a second, to 70 or to nothing (0x416f40).
        Letterbox& l = letterbox_;
        const bool in = l.mode == 2;
        l.height = in ? l.height + dt * 250 : l.height - dt * 250;
        if (in ? l.height >= 70 : l.height <= 0) {
            l.height = in ? 70.0f : 0.0f;
            l.mode = in ? 1 : 0;
            if (ScriptRunner* r = l.runner) {
                l.runner = nullptr;
                r->finished("letterbox");
            }
        }
        draw();
    }
    if (fade_ != fadeTarget_) {
        // 8 of 255 every 0.01 s (0x40c33b, 0x4158e0): about a third of a
        // second. The game takes at most a step a frame, so it's slower
        // where frames are: 0.27 to 0.47 s in a recording of it.
        const float step = dt * 8 / 255 / 0.01f;
        fade_ = fadeTarget_ > fade_ ? std::min(fadeTarget_, fade_ + step) : std::max(fadeTarget_, fade_ - step);
        if (fade_ == fadeTarget_ && fadeRunner_) {
            ScriptRunner* r = fadeRunner_;
            fadeRunner_ = nullptr;
            r->finished("fade");
        }
        draw();
    }
    if (soundRunner_ && !(ctx_.mixer && ctx_.mixer->isPlaying(soundWaited_))) {
        ScriptRunner* r = soundRunner_;
        soundRunner_ = nullptr;
        r->finished("sound");
    }
    if (interfaceUpdate(dt) || (ui_.open && ui_.page == 0 && !state_.fresh.empty() && caretTime_ == 0.33f))
        draw();
    runner_->update(dt);
    // The clicked character's routine goes on once the main script is
    // over (0x409cab).
    if (resumeAfter_ && runner_->idle()) {
        for (Character& c : characters_)
            if (c.record == resumeAfter_)
                c.paused = false;
        resumeAfter_ = nullptr;
    }
    if (dialogue_->active()) {
        dialogue_->update(dt);
        draw(); // the box moves, lines change
    }
    bool changed = drawing_ && advanceLoops(time); // nothing to decode for a tool not drawing
    for (Character& c : characters_) {
        // Its routine, unless paused; its animations and any turn.
        if (c.routine && !c.paused)
            c.routine->update(dt);
        float heading = c.rotY * 57.29578f;
        c.actor->setConversation(dialogue_->active());
        const bool turning = c.actor->rotating();
        c.actor->update(dt, heading);
        if (turning)
            c.rotY = heading / 57.29578f;
        updateOutdoors(c, dt, !c.path.empty() || c.walkIn > 0);
        changed = true;
        if (c.walkIn > 0 && c.path.empty()) {
            // Straight ahead along its heading; any overshoot backed off,
            // then idle1 eased in over half a second (0x4422c0, 0x442690).
            const float step = std::min(c.walkIn, c.walkSpeed * dt);
            c.x += std::sin(c.rotY) * step, c.z += std::cos(c.rotY) * step;
            c.walkIn -= step;
            if (c.walkIn <= 0) {
                c.walkIn = 0;
                c.actor->play("idle1", 0, 0.5f);
                char at[96];
                std::snprintf(at, sizeof at, "    (%s in, at %.0f, %.0f)", c.who.c_str(), c.x, c.z);
                logScript(at);
            }
        }
        if (c.follower && c.path.empty() && c.walkIn <= 0)
            follow(c);
        if (c.path.empty()) {
            placeCharacter(c);
            continue;
        }
        // On towards the next point, turning to face it (at most a turn
        // and a half a second, a guess; the game's planner turns along
        // its path).
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
            if (c.path.empty()) {
                // There: standing, and a script waiting on the walk goes on.
                c.actor->play("idle1", 0, 0);
                if (c.waiting && (c.waitVerb == "walk" || c.waitVerb == "run")) {
                    ScriptRunner* r = c.waiting;
                    const std::string verb = c.waitVerb;
                    c.waiting = nullptr;
                    r->finished(verb);
                }
            }
        } else {
            c.x += dx / distance * stepLength, c.z += dz / distance * stepLength;
        }
        placeCharacter(c);
    }
    for (auto& [o, system] : particles_)
        if (drawing_ && options_.get(GameOptions::Rain) && testCondition(system.settings().condition, state_)) {
            system.update(dt);
            changed = true;
        }
    changed |= scrollPictures(dt);
    updateHotspots();
    changed |= std::exchange(cursorMoved_, false);
    if (changed)
        draw();
    // A page not drawing (a tool's) makes no 3D frame either: its
    // characters still move, and are picked by their bones.
    if (!renderer_ || !drawing_) {
        if (drawing_)
            Page::update(nowMs);
        return;
    }
    for (gfx::MeshId m : frameMeshes_)
        renderer_->destroyMesh(m);
    frameMeshes_.clear();
    // The pictures in their layers, each a quad over the whole frame,
    // between the characters (draw()).
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
    auto drawLayer = [&](size_t k) {
        if (k >= layers_.size() || !layers_[k].texture || !quad_)
            return;
        renderer_->setView(gfx::identity(), {0, 0, 0});
        gfx::DrawCall call;
        call.mesh = quad_;
        call.indexCount = 6;
        call.texture = layers_[k].texture;
        if (k > 0) {
            call.state.src = gfx::Blend::SrcAlpha;
            call.state.dst = gfx::Blend::OneMinusSrcAlpha;
        }
        call.state.depthTest = false;
        call.state.depthWrite = false;
        call.state.cull = gfx::Cull::None;
        renderer_->draw(call);
    };
    drawLayer(0);
    for (gfx::TextureId t : frameTextures_)
        renderer_->destroyTexture(t);
    frameTextures_.clear();
    size_t layer = 1;
    // The characters, through the scene's camera, in depth order.
    if (const Ac2Record* camr = scene_.camera(); camr && charactersShown()) {
        const SceneCamera cam = SceneCamera::fromRecord(*camr);
        // Their poses, and their props'.
        std::vector<Pose> poses(characters_.size());
        std::vector<std::vector<Pose>> propPoses(characters_.size());
        for (size_t i = 0; i < characters_.size(); i++) {
            const Character& c = characters_[i];
            c.actor->animator().pose(poses[i]);
            for (const auto& [name, prop] : c.props) {
                propPoses[i].emplace_back();
                c.actor->animator().poseFor(prop->model(), propPoses[i].back());
            }
        }
        // The shadows (docs/formats/awe/shadows.md): the game's stencil,
        // made on the CPU as the frame is drawn.
        const bool shadows = options_.get(GameOptions::Shadows);
        if (shadows) {
            if (!stencil_)
                stencil_ = std::make_unique<ShadowStencil>(kScreenW, kScreenH);
            stencil_->clear();
        }
        const gfx::Mat4 viewProjection = cam.viewProjection();
        renderer_->clearDepth();
        // Brightness: the game's gamma ramp is the whole screen's, the
        // characters' too (slope 32g + 97 over 257).
        const float bright = float(32 * brightnessLevel() + 97) / 257.0f;
        std::vector<ModelSlot> order = characterOrder_;
        size_t shownCharacters = 0;
        bool stale = false;
        for (const ModelSlot& s : order) {
            stale |= s.character >= characters_.size() ||
                     (s.footprint >= 0 && size_t(s.footprint) >= characters_[s.character].footprints.size());
            shownCharacters += s.footprint < 0;
        }
        if (stale || shownCharacters != characters_.size()) {
            order.clear();
            for (size_t i = 0; i < characters_.size(); i++)
                order.push_back({i, -1});
        }
        // Lit by the scene's lights, each with its own material, which its
        // props share (docs/formats/awe/lighting.md).
        const SceneLighting lighting = sceneLighting(scene_, state_);
        for (const ModelSlot& slot : order) {
            const size_t i = slot.character;
            const Character& c = characters_[i];
            if (slot.footprint >= 0) {
                renderer_->setView(viewProjection, cam.eye());
                drawFootprint(c.footprints[size_t(slot.footprint)], lighting, bright);
            } else if (shadows) {
                // 0x428836: its pixels' stencil 2 for the player, 0 for the
                // others.
                const uint8_t ref = c.player ? 2 : 0;
                stencilModel(viewProjection, *c.model, poses[i], c.placement, c.shadow, ref, *stencil_);
                for (size_t k = 0; k < c.props.size(); k++)
                    stencilModel(viewProjection, *c.props[k].second, propPoses[i][k], c.placement, c.shadow, ref,
                                 *stencil_);
            }
            if (slot.footprint < 0) {
                renderer_->setView(viewProjection, cam.eye());
                const Material material = c.record ? materialOf(*c.record) : Material();
                c.model->draw(poses[i], c.placement, false, frameMeshes_, bright, &lighting, material);
                for (size_t k = 0; k < c.props.size(); k++)
                    c.props[k].second->draw(propPoses[i][k], c.placement, false, frameMeshes_, bright, &lighting,
                                            material);
            }
            // The pictures after it.
            if (layer < layers_.size()) {
                drawLayer(layer);
                if (shadows)
                    stencil_->reset(layers_[layer].resets);
                layer++;
            }
        }
        if (shadows) {
            // Where the stencil is 1, darkened (0x402ba0: black at alpha
            // 63); then the player washed toward white with distance, in
            // the snow (the camera's whiteoutStart and whiteoutEnd, 0x402e20).
            if (!shadowMask_)
                shadowMask_ = std::make_unique<CoverageMask>(kScreenW, kScreenH);
            stencil_->select(1, *shadowMask_);
            renderer_->setView(gfx::identity(), cam.eye());
            drawMask(*shadowMask_, Color{0, 0, 0}, 63);
            drawWhiteOut();
        }
    }
    for (; layer < layers_.size(); layer++)
        drawLayer(layer);
    prepareScreenshot(renderer_);
    if (grabDue_)
        renderer_->captureNextFrame();
    renderer_->endFrame(&ctx_.screen);
    if (grabDue_) {
        // The view for a save's thumbnail, the 2D over the 3D.
        image::Image img;
        img.w = kScreenW, img.h = kScreenH;
        if (renderer_->takeCapture(img.rgba)) {
            const auto& o = ctx_.screen.pixels;
            if (o.size() == img.rgba.size())
                for (size_t i = 0; i < img.rgba.size(); i += 4) {
                    const int a = o[i + 3];
                    for (size_t c = 0; c < 3; c++)
                        img.rgba[i + c] = uint8_t((o[i + c] * a + img.rgba[i + c] * (255 - a)) / 255);
                }
            grab_ = std::move(img);
            grabDue_ = false;
        }
    }
    finishScreenshot(renderer_, &ctx_.screen);
}

void ScenePage::stencilModel(const gfx::Mat4& viewProjection, const GpuModel& model, const Pose& pose,
                             const gfx::Mat4& placement, bool shadow, uint8_t ref, ShadowStencil& stencil) const {
    // 0x428500: each mesh with materials (those without are skipped,
    // shadow and all), its shadow first if the character casts one, then
    // its body. A shadow sets the stencil to 1 where it passes the depth
    // test, writing no depth; a body sets it to `ref`, and its depth.
    std::vector<std::array<float, 3>> positions, normals, screen;
    std::vector<uint8_t> ok;
    auto toScreen = [&](const float p[3], std::array<float, 3>& out) {
        const gfx::Mat4& m = viewProjection;
        float clip[4];
        for (size_t row = 0; row < 4; row++)
            clip[row] = m[row] * p[0] + m[4 + row] * p[1] + m[8 + row] * p[2] + m[12 + row];
        if (clip[3] <= 0)
            return uint8_t(0);
        out = {(clip[0] / clip[3] + 1) * 0.5f * float(kScreenW), (1 - clip[1] / clip[3]) * 0.5f * float(kScreenH),
               clip[2] / clip[3]};
        return uint8_t(1);
    };
    auto fill = [&](const ModelMesh& mesh, uint8_t value, bool writeDepth) {
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
            if (a < ok.size() && b < ok.size() && c < ok.size() && ok[a] && ok[b] && ok[c])
                stencil.fillTriangle(screen[a].data(), screen[b].data(), screen[c].data(), value, writeDepth);
        }
    };
    const std::array<float, 3> light = shadowLight(scene_);
    for (const ModelMesh& mesh : model.model().meshes) {
        bool textured = false;
        for (const auto& g : mesh.groups)
            textured |= g.texture >= 0;
        if (!textured)
            continue;
        skin(mesh, pose, positions, normals);
        screen.resize(positions.size());
        ok.assign(positions.size(), 0);
        if (shadow && mesh.rigid && !mesh.vertices.empty()) {
            // A rigid mesh (the cleaver's, the luggage's) is drawn with its
            // bone's matrix as the world, which the shadow replaces
            // (0x4285f0, 0x428250): its own vertices (turned to the game's
            // axes) are flattened, onto the plane at the bone's height,
            // from a light moved by the bone's position. So its shadow
            // lies about the world's origin, not under the prop.
            const size_t bone = mesh.vertices[0].bones[0];
            const gfx::Mat4 world =
                bone < pose.skinning.size() ? gfx::multiply(placement, pose.skinning[bone]) : placement;
            const float origin[3] = {world[12], world[13], world[14]};
            const std::array<float, 3> moved = shadowLight(scene_, origin);
            for (size_t v = 0; v < mesh.vertices.size(); v++) {
                const float* q = mesh.vertices[v].position;
                const float p[3] = {q[0], q[2], -q[1]};
                ok[v] = toScreen(flattenOntoPlane(moved, origin[1], p).data(), screen[v]);
            }
            fill(mesh, 1, false);
        } else if (shadow) {
            // A skinned mesh is already in the world: flattened onto the
            // floor.
            for (size_t v = 0; v < positions.size(); v++) {
                const gfx::Vec3 w = gfx::transformPoint(placement, {positions[v][0], positions[v][1], positions[v][2]});
                const float p[3] = {w[0], w[1], w[2]};
                ok[v] = toScreen(flattenOntoFloor(light, p).data(), screen[v]);
            }
            fill(mesh, 1, false);
        }
        for (size_t v = 0; v < positions.size(); v++) {
            const gfx::Vec3 w = gfx::transformPoint(placement, {positions[v][0], positions[v][1], positions[v][2]});
            const float p[3] = {w[0], w[1], w[2]};
            ok[v] = toScreen(p, screen[v]);
        }
        fill(mesh, ref, true);
    }
}

void ScenePage::drawMask(const CoverageMask& mask, Color colour, uint8_t alpha) {
    // A quad over the mask's box, its texture the colour where covered.
    if (mask.empty())
        return;
    const int x0 = mask.minX(), y0 = mask.minY(), w = mask.maxX() - x0 + 1, h = mask.maxY() - y0 + 1;
    std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (mask.at(x0 + x, y0 + y)) {
                uint8_t* p = rgba.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
                p[0] = colour.r, p[1] = colour.g, p[2] = colour.b, p[3] = alpha;
            }
    gfx::TextureId tex = 0;
    gfx::TextureParams params;
    params.clamp = true;
    params.mipmaps = false;
    params.nearest = true;
    if (renderer_->createTexture(w, h, rgba.data(), params, tex))
        return;
    frameTextures_.push_back(tex);
    const float l = 2.0f * float(x0) / kScreenW - 1, r = 2.0f * float(x0 + w) / kScreenW - 1;
    const float t = 1 - 2.0f * float(y0) / kScreenH, b = 1 - 2.0f * float(y0 + h) / kScreenH;
    std::vector<gfx::Vertex> v(4);
    const float corners[4][4] = {{l, t, 0, 0}, {r, t, 1, 0}, {r, b, 1, 1}, {l, b, 0, 1}};
    for (size_t i = 0; i < 4; i++) {
        v[i] = {};
        v[i].pos[0] = corners[i][0], v[i].pos[1] = corners[i][1], v[i].pos[2] = 0.5f;
        v[i].uv[0] = corners[i][2], v[i].uv[1] = corners[i][3];
        v[i].color[0] = v[i].color[1] = v[i].color[2] = v[i].color[3] = 255;
    }
    gfx::MeshId mesh = 0;
    if (renderer_->createMesh(v, {0, 2, 1, 0, 3, 2}, mesh))
        return;
    frameMeshes_.push_back(mesh);
    gfx::DrawCall call;
    call.mesh = mesh;
    call.indexCount = 6;
    call.texture = tex;
    call.state.src = gfx::Blend::SrcAlpha;
    call.state.dst = gfx::Blend::OneMinusSrcAlpha;
    call.state.depthTest = false;
    call.state.depthWrite = false;
    call.state.cull = gfx::Cull::None;
    renderer_->draw(call);
}

void ScenePage::drawWhiteOut() {
    // 0x402e20: a white quad where the stencil is 2 (the player's pixels
    // still showing), more opaque the further it has walked past A, fully
    // at B.
    const Ac2Record* camr = scene_.camera();
    const float a = camr->f("whiteoutStart"), b = camr->f("whiteoutEnd");
    if (a == b)
        return;
    for (const Character& c : characters_) {
        if (!c.player || c.z >= a)
            continue;
        const int alpha = c.z < b ? 255 : int((a - c.z) / (a - b) * 255);
        stencil_->select(2, *shadowMask_);
        drawMask(*shadowMask_, Color{255, 255, 255}, uint8_t(std::clamp(alpha, 0, 255)));
        return;
    }
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

bool ScenePage::startWalk(Character& c, bool run) {
    if (!c.actor || c.actor->play(run ? "run" : "walk", 0, 0).empty())
        return false;
    // The walk carries the body forward (the biped's root, Bip01, moves
    // the whole loop's distance): that is how fast it walks. The game's
    // characters move by that much, while the loop starts again from the
    // same place.
    c.walkSpeed = 0;
    if (const Animator::Slot* s = c.actor->animator().base(); s && s->animation->duration > 0) {
        const Actor::Motion m = c.actor->motion(*s->animation);
        c.walkSpeed = std::sqrt(m.drift[0] * m.drift[0] + m.drift[1] * m.drift[1] + m.drift[2] * m.drift[2]) /
                      s->animation->duration;
    }
    if (c.walkSpeed <= 1)
        c.walkSpeed = run ? 300.0f : 150.0f; // a walk in place: a guess at a pace
    return true;
}

void ScenePage::follow(Character& f) {
    // 0x4400a0: while the player walks somewhere, a follower 250 units or
    // more away, and further from the goal than the player, walks there by
    // the path-finder, stopping 125 units short (0x442780).
    const Character* p = nullptr;
    for (const Character& c : characters_)
        if (c.player)
            p = &c;
    if (!p || p->path.empty() || !leadGoal_.valid())
        return;
    float gx, gz;
    grid_.centre(leadGoal_, gx, gz);
    auto dist = [](float ax, float az, float bx, float bz) { return std::hypot(ax - bx, az - bz); };
    if (dist(p->x, p->z, f.x, f.z) < 250 || dist(p->x, p->z, gx, gz) >= dist(f.x, f.z, gx, gz))
        return;
    const std::vector<WalkGrid::Cell> keep = path_;
    if (walkCharacter(f, leadGoal_, false) && grid_.at(leadGoal_.a, leadGoal_.b) != 2) {
        // The last leg shortened; under 10 units it isn't walked.
        const float fx = f.path.size() > 1 ? f.path[f.path.size() - 2].first : f.x;
        const float fz = f.path.size() > 1 ? f.path[f.path.size() - 2].second : f.z;
        auto& last = f.path.back();
        const float leg = dist(fx, fz, last.first, last.second);
        if (leg - 125 < 10) {
            f.path.pop_back();
        } else {
            last.first = fx + (last.first - fx) * (leg - 125) / leg;
            last.second = fz + (last.second - fz) * (leg - 125) / leg;
        }
        if (f.path.empty())
            f.actor->play("idle1", 0, 0);
    }
    path_ = keep; // the overlay shows the player's
}

void ScenePage::placeCharacter(Character& c) const {
    c.placement = characterMatrix(c.x, c.y, c.z, c.rotY);
    const Animator::Slot* s = c.actor ? c.actor->animator().base() : nullptr;
    if (!s || s->animation->duration <= 0 ||
        !(s->name.find("walk") != std::string::npos || s->name.find("run") != std::string::npos ||
          s->name.find("rotate") != std::string::npos))
        return;
    // Back by how far the walk (or turn) has carried the body this loop,
    // as the character itself moves (or turns).
    const Actor::Motion m = c.actor->motion(*s->animation);
    const float loop = std::fmod(s->clock, s->animation->duration) / s->animation->duration;
    gfx::Mat4 back = gfx::identity();
    const float a = -loop * m.turn / 57.29578f;
    back[0] = std::cos(a), back[1] = std::sin(a), back[4] = -std::sin(a), back[5] = std::cos(a);
    for (int k = 0; k < 3; k++)
        back[size_t(12 + k)] = -loop * m.drift[k];
    c.placement = gfx::multiply(c.placement, back);
}

bool ScenePage::scrollPictures(float dt) {
    // 0x4378b0: scrollSpeed / 20 pixels a second, in whole pixels; past
    // its own width or height (or the screen's edge) a picture goes back
    // by that much. 1 scrolls left, 2 right, 3 up, 4 down.
    bool moved = false;
    for (auto& [o, s] : scrolls_) {
        const image::Image& img = picture(o->text("file"));
        const int way = o->part("SPRT")->i("scroll");
        const float sum = s.part + dt * o->part("SPRT")->f("scrollSpeed") * 0.05f;
        s.part = sum;
        const int n = int(sum);
        if (n <= 0 || img.w <= 0 || img.h <= 0)
            continue;
        const float w = float(img.w), h = float(img.h);
        if (way == 1) {
            s.x -= float(n);
            while (s.x <= -w)
                s.x += w;
        } else if (way == 2) {
            s.x += float(n);
            while (s.x >= 1024.0f)
                s.x -= w;
        } else if (way == 3) {
            s.y -= float(n);
            while (s.y <= -h)
                s.y += h;
        } else if (way == 4) {
            s.y += float(n);
            while (s.y >= 768.0f)
                s.y -= h;
        }
        s.part = sum - float(n);
        moved = true;
    }
    return moved;
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
    // A player whose MODL clickToWalk is 0 stays put (0x416343).
    if (who && who->player && who->record)
        if (const Ac2Record* modl = who->record->part("MODL"); modl && modl->field("clickToWalk") &&
                                                              modl->i("clickToWalk") == 0)
            return;
    WalkGrid::Cell start{32, 32};
    if (who)
        start = grid_.cellAt(who->x, who->z);
    else
        for (const Ac2Record* o : scene_.objects())
            if (o->tag == "PCXX")
                start = grid_.cellAt(o->f("x"), o->f("z"));
    path_ = grid_.findPath(start, goal);
    if (who && who->player && path_.size() > 1)
        leadGoal_ = goal;
    if (who && path_.size() > 1 && startWalk(*who)) {
        who->path.clear();
        for (size_t i = 1; i < path_.size(); i++) {
            float px, pz;
            grid_.centre(path_[i], px, pz);
            who->path.push_back({px, pz});
        }
    }
    draw();
}

std::vector<std::string> ScenePage::objectKeys(const Ac2Record& o) {
    // What "hide" and "show" may call it: its name, its file, the file's
    // name, with or without its extension ("hide sceorangejuice.jpg",
    // "show handle").
    std::vector<std::string> keys;
    if (!o.text("name").empty())
        keys.push_back(fs::toLower(o.text("name")));
    const std::string file = fs::toLower(normalizeArchiveName(o.text("file")));
    if (!file.empty()) {
        keys.push_back(file);
        const std::string name = file.substr(file.find_last_of('/') + 1);
        keys.push_back(name);
        keys.push_back(name.substr(0, name.rfind('.')));
    }
    return keys;
}

bool ScenePage::hidden(const Ac2Record& o) const {
    if (hiddenNames_.empty())
        return false;
    for (const std::string& key : objectKeys(o))
        if (hiddenNames_.count(key))
            return true;
    return false;
}

bool ScenePage::shown(const Ac2Record& o) const {
    // "show" puts an object up whatever its condition (the puzzles' "0"
    // pictures: show handle, fade in paperfull).
    if (shownNames_.empty())
        return false;
    for (const std::string& key : objectKeys(o))
        if (shownNames_.count(key))
            return true;
    return false;
}

void ScenePage::showObject(const std::string& name, bool show) {
    if (show) {
        hiddenNames_.erase(name);
        shownNames_.insert(name);
        // A video starts from its first frame.
        for (auto& [o, loop] : loops_)
            for (const std::string& key : objectKeys(*o))
                if (key == name)
                    loop.start = time_;
    } else {
        shownNames_.erase(name);
        hiddenNames_.insert(name);
    }
}

namespace {

// Whether a screen point is in an object's outline (listD: hotspots and
// characters have one), by the crossings of a ray to the right; false if
// it has none.
bool inOutline(const Ac2Record& o, int sx, int sy) {
    const Ac2Value* outline = o.field("listD");
    if (!outline || outline->numbers.size() < 3)
        return false;
    bool inside = false;
    const auto& pts = outline->numbers;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const float xi = float(pts[i].first), yi = float(pts[i].second);
        const float xj = float(pts[j].first), yj = float(pts[j].second);
        if ((yi > float(sy)) != (yj > float(sy)) && float(sx) < (xj - xi) * (float(sy) - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}

} // namespace

const Ac2Record* ScenePage::hotspotAt(int sx, int sy) const {
    const Ac2Record* hit = nullptr;
    for (const Ac2Record* o : scene_.objects()) {
        if (o->tag != "SPOT" || hidden(*o) || !testCondition(o->text("condition"), state_))
            continue;
        const Ac2Value* outline = o->field("listD");
        const bool inside = outline && outline->numbers.size() >= 3
                                ? inOutline(*o, sx, sy)
                                : sx >= o->i("left") && sx < o->i("right") && sy >= o->i("top") && sy < o->i("bottom");
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

ScenePage::Character* ScenePage::scriptCharacter(const ScriptRunner& runner) {
    // setchar's, else the player's (0x4365d0).
    const std::string who = fs::toLower(runner.character());
    Character* player = nullptr;
    for (Character& c : characters_) {
        if (!who.empty() && c.who == who)
            return &c;
        if (c.player)
            player = &c;
    }
    return who.empty() || who == "antoinette" ? player : nullptr;
}

ScenePage::Character* ScenePage::characterNamed(const std::string& who) {
    const std::string w = fs::toLower(who);
    for (Character& c : characters_)
        if (c.who == w || fs::toLower(c.record ? c.record->text("name") : "") == w)
            return &c;
    return nullptr;
}

bool ScenePage::objectAt(const std::string& name, float& x, float& z) const {
    const std::string n = fs::toLower(name);
    for (const Character& c : characters_)
        if (c.who == n) {
            x = c.x, z = c.z;
            return true;
        }
    for (const Ac2Record* o : scene_.objects())
        if (fs::toLower(o->text("name")) == n) {
            x = o->f("x"), z = o->f("z");
            return true;
        }
    return false;
}

void ScenePage::queueRoutine(Character& c) {
    // The first IDLE entry whose condition holds (0x44087d; "loop" queues
    // it again: 0x405940).
    if (!c.routine || !c.record)
        return;
    for (const Ac2Record& part : c.record->parts)
        if (const Ac2Value* idles = part.field("idles"))
            for (const auto& [condition, script] : idles->pairs)
                if (!script.empty() && testCondition(condition, state_)) {
                    c.routine->run(script);
                    return;
                }
}

bool ScenePage::walkCharacter(Character& c, WalkGrid::Cell goal, bool toPoint, bool run) {
    c.walkIn = 0; // a walk replaces walking in
    const WalkGrid::Cell start = grid_.cellAt(c.x, c.z);
    path_ = grid_.findPath(start, goal, toPoint);
    if (path_.size() < 2 || !startWalk(c, run))
        return false;
    c.path.clear();
    for (size_t i = 1; i < path_.size(); i++) {
        float px, pz;
        grid_.centre(path_[i], px, pz);
        c.path.push_back({px, pz});
    }
    return true;
}

bool ScenePage::changeScene(const std::string& name, int entry, bool walkIn) {
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
    // Leaving (0x423390): where the player was is kept (the game keeps
    // the scene itself), and the act's time, weather and moving apply.
    for (const Character& c : characters_) {
        if (c.player)
            positions_[state_.scene] = {c.x, c.z, c.rotY};
        else if (c.record)
            // The game keeps the scene, so its characters stay where they
            // are (and are saved there: POSI).
            npcPositions_[state_.scene + "|" + fs::toLower(fileNameOf(c.name))] = {{c.x, c.y, c.z}, {0, c.rotY, 0}};
    }
    state_.lastScene = state_.scene;
    story_->leavingScene();
    index_ = found;
    entryPoint_ = entry;
    entryWalk_ = walkIn;
    load();
    entryPoint_ = -1;
    entryWalk_ = false;
    return true;
}

void ScenePage::playSound(const std::string& name) { startSound(name, false); }

audio::SoundHandle ScenePage::startSound(const std::string& name, bool loop) {
    // sound\<name>.wav: found by its file's name, wherever it is.
    std::string key = fs::toLower(name);
    if (const size_t slash = key.find_last_of("\\/"); slash != std::string::npos)
        key = key.substr(slash + 1);
    if (soundFiles_.empty())
        for (const std::string& f : listFiles(ctx_.files, {"sound/"}, {".wav", ".mp3"})) {
            const std::string file = f.substr(f.find_last_of('/') + 1);
            soundFiles_.emplace(fs::toLower(file.substr(0, file.rfind('.'))), f);
        }
    const auto it = soundFiles_.find(key);
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (!ctx_.mixer || it == soundFiles_.end() || ctx_.files.read(it->second, data) || audio::decodeSound(data, *sound))
        return audio::kNoSound;
    const audio::SoundHandle h = ctx_.mixer->play(sound, audio::SoundType::Effects, audio::kMaxVolume, 0, loop);
    // Kept for "stop", those still playing.
    for (auto s = sounds_.begin(); s != sounds_.end();)
        s = ctx_.mixer->isPlaying(s->second) ? std::next(s) : sounds_.erase(s);
    sounds_.emplace_back(key, h);
    return h;
}

void ScenePage::logScript(const std::string& line) {
    if (onLog)
        onLog(line);
    scriptLog_.push_back(line);
    while (scriptLog_.size() > 8)
        scriptLog_.pop_front();
}

// --- Character verbs ----------------------------------------------------------

std::shared_ptr<GpuModel> ScenePage::propModel(const std::string& file) {
    // Kept with the characters' models: loading one decodes its textures.
    if (file.empty() || !renderer_)
        return nullptr;
    auto found = models_.find(file);
    if (found == models_.end()) {
        Gr2File gr2;
        Model m;
        std::shared_ptr<GpuModel> gpu;
        if (ctx_.files.has(file) && !openGr2(ctx_.files, file, gr2) && !loadModel(gr2, m))
            gpu = std::make_shared<GpuModel>(*renderer_, std::move(m));
        found = models_.emplace(file, gpu).first;
    }
    return found->second;
}

bool ScenePage::startCharacterVerb(ScriptRunner& runner, Character& c, const ScriptEvent& e, bool wait,
                                   bool& handled) {
    const std::string v = e.verb();
    auto lower = [](const std::string& t) { return fs::toLower(t); };
    const std::string a1 = lower(e.arg(1)), a2 = lower(e.arg(2)), a3 = lower(e.arg(3));
    const float body = normalizeDegrees(c.rotY * 57.29578f);
    // The heading from the character to a point (0x425b10): 0 faces +Z.
    auto headingTo = [&](float x, float z) { return normalizeDegrees(std::atan2(x - c.x, z - c.z) * 57.29578f); };
    auto cellHeading = [&](const std::string& a, const std::string& b) {
        float x, z;
        grid_.centre({std::atoi(a.c_str()), std::atoi(b.c_str())}, x, z);
        return headingTo(x, z);
    };
    auto upDown = [](const std::string& w) { return w == "up" ? 1 : w == "down" ? 2 : 0; };
    handled = true;
    if (v == "turn") {
        // The head (0x40b7a8); it never waits.
        if (a1 == "home" && a2 == "all") {
            for (Character& ch : characters_)
                ch.actor->headHome();
        } else if (a1 == "home") {
            c.actor->headHome();
        } else if ((a1 == "up" || a1 == "down") && a2.empty()) {
            c.actor->headAnimation("head" + a1, e.arg(2).empty() ? 0.5f : float(std::atof(e.arg(2).c_str())));
        } else if (!a3.empty() || (!a2.empty() && a2 != "up" && a2 != "down")) {
            // "turn a b [up|down]": toward a cell.
            c.actor->turnHead(cellHeading(a1, a2), body, upDown(a3));
        } else {
            // Toward an object, else so many degrees from the body (more
            // to its left).
            float x, z;
            const float heading =
                objectAt(a1, x, z) ? headingTo(x, z) : normalizeDegrees(float(std::atof(a1.c_str())) + body);
            c.actor->turnHead(heading, body, upDown(a2));
        }
        return false;
    }
    if (v == "look") {
        // The eyes (0x40a5f1); a strength, not seconds; it never waits.
        c.actor->look(a1, e.arg(2).empty() ? 1.0f : float(std::atof(e.arg(2).c_str())));
        return false;
    }
    if (v == "rotate") {
        // The body (0x40bfca): to a cell, a walk point's heading ("-sp2"
        // the other way: facing the door), home, the camera, an object,
        // or a heading in degrees.
        float heading = 0;
        bool have = true;
        if (!a2.empty()) {
            heading = cellHeading(a1, a2);
        } else if (std::atof(a1.c_str()) == 0 && a1 != "0") {
            const bool back = !a1.empty() && a1[0] == '-';
            const std::string name = back ? a1.substr(1) : a1;
            have = false;
            if (name == "home") {
                heading = c.home, have = true;
            } else if (name.size() > 2 && name.compare(0, 2, "sp") == 0 && c.record) {
                if (const Ac2Record* w = c.record->part("WALK"))
                    for (const Ac2Record& pt : w->parts)
                        if (pt.i("id") == std::atoi(name.c_str() + 2)) {
                            heading = normalizeDegrees(pt.f("rotY") * 57.29578f), have = true;
                            if (back)
                                heading += heading <= 180 ? 180 : -180;
                        }
            } else if (name == "camera" && scene_.camera()) {
                heading = normalizeDegrees(scene_.camera()->f("yaw") * 57.29578f), have = true; // a guess
            } else {
                float x, z;
                if (objectAt(name, x, z))
                    heading = headingTo(x, z), have = true;
            }
        } else {
            heading = float(std::atof(a1.c_str()));
        }
        if (!have || !c.path.empty() || !c.actor->rotateTo(heading, body))
            return false;
        if (wait)
            c.waiting = &runner, c.waitVerb = "rotate";
        return wait;
    }
    if (v == "anim") {
        // anim <name> [weight] [loops] [ease] (0x40c367). Done when it
        // ends; at once if there's no such file (the game would wait for
        // ever).
        const float weight = float(std::atof(e.arg(2).c_str()));
        const int loops = e.arg(3).empty() ? 1 : std::atoi(e.arg(3).c_str());
        const std::string played = c.actor->anim(a1, weight, loops, float(std::atof(e.arg(4).c_str())));
        if (played.empty() || loops == 0 || !wait)
            return false;
        c.waiting = &runner, c.waitVerb = "anim", c.waitAnim = a1, c.waitAnimAs = played;
        return true;
    }
    if (v == "animate") {
        c.actor->advance(float(std::atof(a1.c_str())));
        return false;
    }
    if (v == "pause" || v == "resume") {
        // The character's routine stops, its walk or turn with it, and
        // starts its interrupted event again when resumed (0x4057f0).
        if (v == "pause" && c.routine && !c.paused) {
            c.paused = true;
            c.routine->restart();
            if (c.waiting == c.routine.get())
                c.waiting = nullptr;
            c.path.clear();
            c.actor->play("idle1", 0, 0);
        } else if (v == "resume") {
            c.paused = false;
        }
        return false;
    }
    if (v == "loop") {
        queueRoutine(c);
        return false;
    }
    if (v == "attach" || v == "detach") {
        // models\objects\<name>_model.gr2, moved by the character's own
        // animations (0x426290, 0x4262d0).
        if (v == "detach") {
            c.props.erase(std::remove_if(c.props.begin(), c.props.end(),
                                         [&](const auto& p) { return p.first.find(a1) != std::string::npos; }),
                          c.props.end());
            return false;
        }
        const std::string path = "models/objects/" + a1 + "_model.gr2";
        for (const auto& p : c.props)
            if (p.first == path)
                return false;
        if (auto m = propModel(path))
            c.props.push_back({path, m});
        return false;
    }
    if (v == "hideshadow" || v == "showshadow") {
        // The character's shadow flag (MODL shadow: 0x40a2e6, 0x40af33).
        c.shadow = v == "showshadow";
        return false;
    }
    handled = false;
    return false;
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
        // Each read once (they're read again with every act and saved game).
        auto it = dialogueTables_.find(f);
        if (it == dialogueTables_.end()) {
            std::vector<uint8_t> data;
            Table table;
            if (!ctx_.files.read(f, data))
                table = parseTable(decodeText(data));
            it = dialogueTables_.emplace(f, std::move(table)).first;
        }
        if (!it->second.headings.empty())
            book.add(it->second);
    }
}

bool ScenePage::startTalk(ScriptRunner& runner, const ScriptEvent& e) {
    loadDialogue();
    const TalkArgs t = parseTalkArgs({e.words.begin() + 1, e.words.end()}, runner.fromCharacter());
    interfaceTalk(t.name);
    if (dialogue_->talk(runner, t.name, t.part, t.thread, t.nest, t.gen, t.number, t.plus, t.greet))
        return true;
    return t.gen && dialogue_->noTalk(runner);
}

bool ScenePage::playVoice(const std::string& file) {
    stopVoice();
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (!ctx_.mixer || ctx_.files.read(file, data) || audio::decodeSound(data, *sound))
        return false;
    voice_ = ctx_.mixer->play(sound, audio::SoundType::Speech);
    voiceSound_ = sound;
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
    // A line's Target (0x43bd30): "up"/"down" [w], a cell "a b [up|down]",
    // someone "[up|down]"; empty: home again.
    Character* c = characterNamed(speaker);
    if (!c && fs::toLower(speaker) == "poirot")
        c = characterNamed("poirotbed");
    if (!c)
        return;
    std::vector<std::string> w;
    for (const Command& cmd : parseCommands(target))
        for (const std::string& word : cmd.words)
            w.push_back(fs::toLower(word));
    if (w.empty() || w[0] == "0") {
        c->actor->headHome();
        return;
    }
    ScriptEvent e{{"turn"}};
    e.words.insert(e.words.end(), w.begin(), w.end());
    bool handled = false;
    startCharacterVerb(*runner_, *c, e, false, handled);
}

void ScenePage::lineStarted(const std::string& speaker) {
    if (Character* c = characterNamed(speaker))
        c->actor->lineStart();
}

void ScenePage::lineEnded(const std::string& speaker) {
    if (Character* c = characterNamed(speaker))
        c->actor->lineEnd();
}

void ScenePage::mouth(const std::string& speaker, bool on) {
    if (Character* c = characterNamed(speaker))
        c->actor->mouth(on);
}

int ScenePage::voiceLevel() {
    // How loud the voice is where it plays: its samples over a thirtieth
    // of a second, 0 when about silent.
    if (!ctx_.mixer || !voiceSound_ || voice_ == audio::kNoSound || voiceSound_->channels <= 0)
        return -1;
    const size_t at = size_t(ctx_.mixer->position(voice_));
    const size_t ch = size_t(voiceSound_->channels), frames = voiceSound_->frames();
    const size_t span = size_t(std::max(1, voiceSound_->rate / 30));
    double sum = 0;
    size_t n = 0;
    for (size_t f = at; f < std::min(frames, at + span); f++, n++)
        sum += std::abs(double(voiceSound_->samples[f * ch]));
    return n && sum / double(n) > 600 ? 1 : 0;
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
        // Its outline on the screen, if it has one.
        if (const Ac2Value* outline = o->field("listD"); outline && outline->numbers.size() >= 3) {
            if (inOutline(*o, sx, sy))
                hit = o;
            continue;
        }
        // Else where it stands: from its feet to a head's height, about a
        // third as wide.
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
    {
        bool handled = false;
        Character* c = scriptCharacter(runner);
        if (v == "pause" || v == "resume" || v == "loop" || v == "attach" || v == "detach")
            c = v == "loop" ? nullptr : characterNamed(e.arg(1)) ? characterNamed(e.arg(1)) : c;
        if (v == "loop")
            for (Character& ch : characters_)
                if (ch.routine.get() == &runner)
                    c = &ch;
        if (c) {
            const bool waits = startCharacterVerb(runner, *c, e, wait, handled);
            if (handled)
                return waits;
        }
    }
    if (v == "walk" || v == "run") {
        Character* c = scriptCharacter(runner);
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
        if (!walkCharacter(*c, goal, toPoint, v == "run"))
            return false;
        if (c->player)
            leadGoal_ = goal;
        if (wait)
            c->waiting = &runner, c->waitVerb = v;
        return wait;
    }
    if (v == "scene" || v == "gotoscene") {
        // gotoscene: the scene "savescene" kept, its ".ac2" taken off.
        std::vector<std::string> words = e.words;
        if (v == "gotoscene") {
            words = {"scene"};
            for (const Command& c : parseCommands(state_.texts["savedscene"]))
                words.insert(words.end(), c.words.begin(), c.words.end());
            if (words.size() < 2)
                return false;
            if (words[1].size() > 4)
                words[1].resize(words[1].size() - 4);
        }
        const ScriptEvent scene{words};
        const std::string name = lower(scene.arg(1));
        const int entry = scene.arg(2).empty() ? 1 : std::atoi(scene.arg(2).c_str());
        // The flag (default 1) has the player walk in.
        const bool walkIn = scene.arg(3).empty() ? true : std::atoi(scene.arg(3).c_str()) != 0;
        // In another place (its name doesn't hold the current location's):
        // a fade out, the location loaded, the scene shown and faded in;
        // the event waits till then (0x423d40, 0x422640).
        if (playing_ && wait && name.find(location_) == std::string::npos && name.find("global") == std::string::npos) {
            load_ = LocationLoad();
            load_.state = 1;
            load_.location = name.substr(0, name.find('_'));
            load_.scene = name, load_.verb = v, load_.entry = entry, load_.runner = &runner;
            load_.walkIn = walkIn;
            fadeTarget_ = 1;
            return true;
        }
        changeScene(name, entry, walkIn);
        return false;
    }
    if (v == "act") {
        story_->startAct(lower(e.arg(1)));
        return false;
    }
    if (v == "movie")
        return startMovie(runner, e, wait);
    if (v == "preload") {
        // preload quick | slotN (0x40a54a): the quick save, else the
        // developers' text\scripts\slotN.ac2.
        const std::string what = lower(e.arg(1));
        if (what == "quick")
            preload(savesFolder() / "quick.ac2", false);
        else
            preload("text/scripts/" + what + ".ac2", true);
        return false;
    }
    if (v == "load") {
        // To the save preloaded (0x412570): its act, its location behind
        // the loading screen, its scene; the event waits till then.
        if (!restoring_)
            return false;
        state_.act = actName(restoring_->actMajor, restoring_->actMinor);
        loadDialogue();
        load_ = LocationLoad();
        load_.state = 1;
        load_.restore = true;
        load_.location = fs::toLower(restoring_->location);
        // trailing spaces in the saved cells ("calais.hall ")
        while (!load_.location.empty() && load_.location.back() == ' ')
            load_.location.pop_back();
        load_.verb = v;
        load_.runner = wait ? &runner : nullptr;
        fadeTarget_ = 1;
        return wait;
    }
    if (v == "menu") {
        openMenu(lower(e.arg(1)));
        return false;
    }
    if (v == "previous") {
        previous(lower(e.arg(1)) == "scene");
        return false;
    }
    if (v == "action") {
        if (optionsMenu_)
            optionsAction(lower(e.arg(1)));
        else if (puzzleActive())
            puzzleAction(lower(e.arg(1)));
        else if (slots_)
            slotAction(lower(e.arg(1)), lower(e.arg(2)));
        return false;
    }
    if (v == "yesno") {
        yesNo_ = std::make_unique<YesNo>();
        yesNo_->text = interfaceText(lower(e.arg(1)));
        yesNo_->kind = std::atoi(e.arg(2).c_str());
        draw();
        return false;
    }
    if (v == "quit") {
        quit_ = true;
        return false;
    }
    if (v == "musica" || v == "music") {
        // sound\music\<name>.mp3, looping (0x40a860).
        playLooping("sound\\music\\" + lower(e.arg(1)) + ".mp3", true);
        return false;
    }
    if (v == "fade" && !e.arg(2).empty()) {
        // fade in|out <object>: that object shown or hidden.
        showObject(lower(e.arg(2)), lower(e.arg(1)) != "out");
        draw();
        return false;
    }
    if (v == "fade") {
        // fade in|out: the screen, over about a third of a second.
        fadeTarget_ = lower(e.arg(1)) == "out" ? 1.0f : 0.0f;
        if (fade_ == fadeTarget_)
            return false;
        if (wait)
            fadeRunner_ = &runner;
        return wait;
    }
    if (v == "sound") {
        // sound\<name>.wav, looping with "loop"; "sound" waits till it's
        // over (0x40bd45, 0x425400).
        const audio::SoundHandle h = startSound(e.arg(1), lower(e.arg(2)) == "loop");
        if (h == audio::kNoSound || !wait)
            return false;
        soundRunner_ = &runner, soundWaited_ = h;
        return true;
    }
    if (v == "stop") {
        // The first sound playing whose name has the word in it (0x4251c0).
        const std::string name = lower(e.arg(1));
        for (auto it = sounds_.begin(); it != sounds_.end(); ++it)
            if (it->first.find(name) != std::string::npos && ctx_.mixer && ctx_.mixer->isPlaying(it->second)) {
                ctx_.mixer->stop(it->second);
                sounds_.erase(it);
                break;
            }
        return false;
    }
    if (v == "startpass" || v == "startprint" || v == "endpass" || v == "endprint") {
        // A passport or fingerprint card held up, and put away
        // (scene_interface.cpp: 0x417ba0, 0x418090).
        return startCard(runner, e);
    }
    if (v == "interface") {
        // The interface (the bar along the top, the inventory) off or on
        // for a cutscene ([0x4986f4]).
        interfaceOff_ = lower(e.arg(1)) == "off" ? true : lower(e.arg(1)) == "on" ? false : interfaceOff_;
        return false;
    }
    if (v == "letterbox") {
        // 0x40a3db, 0x416ef0: "on" and "off" at once; "start" and "end"
        // slide the bars in from nothing or out to it, and wait till
        // they're there. (Nothing in the install uses it.)
        const std::string how = lower(e.arg(1));
        letterbox_.runner = nullptr;
        if (how == "on")
            letterbox_.mode = 1, letterbox_.height = 70;
        else if (how == "off")
            letterbox_.mode = 0;
        else if (how == "start" || how == "end") {
            letterbox_.mode = how == "start" ? 2 : 3;
            if (how == "start")
                letterbox_.height = 0;
            letterbox_.runner = &runner;
            draw();
            return true;
        }
        draw();
        return false;
    }
    if (v == "accumulation" || v == "test") {
        // Snow gathering on the character set to (+0x14dc), and a
        // leftover of the earlier games (0x40c650): nothing to see here.
        return false;
    }
    if (v == "hide" || v == "show") {
        std::string name = lower(e.arg(1));
        if (name == "this" && scriptObject_)
            name = lower(!scriptObject_->text("name").empty() ? scriptObject_->text("name")
                                                               : normalizeArchiveName(scriptObject_->text("file")));
        showObject(name, v == "show");
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
            addItem(state_, item, 1); // just taken (0x40b1de)
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
    // A character's verb with no character to do it (none in the scene by
    // that name, or characters not loaded: no 3D): over at once.
    for (const char* verb : {"turn", "look", "rotate", "anim", "animate", "pause", "resume", "loop", "attach", "detach",
                             "hideshadow", "showshadow"})
        if (v == verb) {
            logScript("    (" + v + ": no character)");
            return false;
        }
    // The rest isn't done here yet: logged, and over.
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
    if (ev.type == EventType::MouseMove || ev.type == EventType::MouseDown || ev.type == EventType::MouseUp) {
        lastMouseX_ = ev.mouseX, lastMouseY_ = ev.mouseY;
        cursorMoved_ |= cursorShown();
    }
    if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left && puzzleActive())
        puzzleRelease(ev.mouseX, ev.mouseY);
    if (optionsMenu_ && optionsMenu_->dragging) {
        if (ev.type == EventType::MouseMove)
            draw();
        else if (ev.type == EventType::MouseUp)
            optionsRelease();
    }
    if (yesNo_) {
        // The yes/no box takes every click and key while it's up.
        if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
            for (size_t i = 0; i < yesNo_->rows.size(); i++)
                if (ev.mouseY >= yesNo_->rows[i].first && ev.mouseY < yesNo_->rows[i].first + yesNo_->rows[i].second)
                    return answer(i == 0);
        } else if (ev.type == EventType::KeyDown && (ev.key == key::Return || ev.key == key::Escape)) {
            answer(ev.key == key::Return);
        }
        return;
    }
    if (movie_) {
        // Escape skips it, unless it's the legal notice (0x409860); clicks
        // don't.
        if (ev.type == EventType::KeyDown && ev.key == key::Escape && movieName_.find("legal") == std::string::npos)
            endMovie();
        return;
    }
    if (messageTime_ > 0 && (ev.type == EventType::KeyDown ||
                             (ev.type == EventType::MouseDown &&
                              (ev.button == MouseButton::Left || ev.button == MouseButton::Right)))) {
        // A key or a click (left or right) while the message shows only
        // puts it away (0x4164e0, 0x416000, 0x416450).
        messageTime_ = 0;
        draw();
        return;
    }
    if (dialogue_->active() &&
        (ev.type == EventType::MouseMove || ev.type == EventType::MouseDown || ev.type == EventType::MouseWheel)) {
        if (ev.type == EventType::MouseMove)
            dialogue_->mouseMove(ev.mouseX, ev.mouseY);
        else if (ev.type == EventType::MouseWheel)
            dialogue_->wheel(ev.wheelY, lastMouseX_, lastMouseY_); // a notch is 120 (0x401431)
        else if (ev.button == MouseButton::Middle)
            dialogue_->middleClick(); // WM_MBUTTONDOWN (0x401557, 0x4163c0)
        else if (ev.button == MouseButton::Left)
            dialogue_->click(ev.mouseX, ev.mouseY);
        draw();
        return;
    }
    // The interface: the bar, the inventory, the item in hand
    // (scene_interface.cpp).
    if (interfaceEvent(ev))
        return;
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
        // The game takes no clicks while a script runs, but during its
        // walks and turns and after an "unlock"; one taken drops what was
        // running (0x409760).
        if (runner_->locked())
            return;
        if (!runner_->idle() && !slots_ && !puzzleActive())
            runner_->clear();
        if (slots_ && !hotspotAt(ev.mouseX, ev.mouseY) && slotClick(ev.mouseX, ev.mouseY))
            return;
        // A puzzle sees the click first, then the hotspot under it runs
        // (0x4161c1).
        if (puzzleActive())
            puzzleClick(ev.mouseX, ev.mouseY);
        runner_->setFromCharacter(false);
        // The object the game's pick finds there (0x41613c); one with
        // nothing to run is walked to like the floor. An item in hand was
        // used by the interface (interfaceEvent) before this.
        const Ac2Record* picked = pickAllowed(ev.mouseY) ? pickAt(ev.mouseX, ev.mouseY) : nullptr;
        if (picked)
            hotspotPressed(*picked);
        if (picked && clickable(*picked)) {
            if (picked->tag == "NPCX") {
                // A character's own script: a talk then offers its generic
                // lines and goodbyes (0x4162a3).
                runner_->setFromCharacter(true);
                resumeAfter_ = picked;
            }
            runScript(*picked);
        } else if (pickAllowed(ev.mouseY)) {
            // The floor only where the pick could be (0x416300).
            walkTo(ev.mouseX, ev.mouseY);
        }
        return;
    }
    if (ev.type != EventType::KeyDown)
        return;
    if (playing_ && gameKey(ev.key))
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
    } else if (ev.key == 'n') {
        newGame();
    } else if (ev.key == 'g') {
        showGrid_ = !showGrid_;
        draw();
    } else if (ev.key == 'm' || ev.key == 'w' || ev.key == 't') {
        int& v = state_.values[ev.key == 'm' ? "moving" : ev.key == 'w' ? "weather" : "time"];
        v = !v;
        draw();
    } else if (ev.key == 'i' || ev.key == key::F1 + 8) {
        // (In a game I is the inventory's: F9.)
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

MoviePage::MoviePage(ViewerContext& ctx) : Page(ctx), player_(ctx.mixer) {
    names_ = listFiles(ctx.files, {"movies/", "elements/", "locations/"}, {".bik"});
}

MoviePage::~MoviePage() { player_.stop(); }

void MoviePage::activate() {
    begin2D();
    open();
}

void MoviePage::deactivate() { player_.stop(); }

void MoviePage::open() {
    player_.stop();
    subtitles_.clear();
    info_.clear();
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
            subtitles_ = moviesOwn(parseSubtitles(decodeText(text)), fs::toLower(stem));
    }
    std::vector<uint8_t> data;
    Error e = ctx_.files.read(name, data);
    if (!e)
        e = player_.open(std::move(data), ctx_.sys.getMillis());
    if (e) {
        info_ = e.message;
        return;
    }
    const video::BinkFile& file = *player_.file();
    char buf[160];
    std::snprintf(buf, sizeof buf, "%dx%d, %zu frames at %.2f fps, revision %c", file.width(), file.height(),
                  file.frameCount(), file.fps(), file.revision());
    info_ = buf;
    if (player_.hasSound())
        info_ += ", sound " + std::to_string(file.audioTracks()[0].sampleRate) + " Hz";
    if (!player_.error().empty())
        info_ += ", " + player_.error();
    ctx_.sys.setWindowTitle("Movies: " + name);
}

void MoviePage::update(uint64_t nowMs) {
    player_.update(nowMs);
    // Video loops (no sound) start over.
    if (player_.loaded() && !player_.hasSound() && !player_.paused() && player_.done(nowMs)) {
        open();
        return;
    }
    clear(0, 0, 0);
    const image::Image& frame = player_.frame();
    if (frame.w)
        image::blit(ctx_.screen, frame, (kScreenW - frame.w) / 2, (kScreenH - frame.h) / 2);
    drawSubtitles(subtitles_, player_.seconds());
    label(4, 2,
          (names_.empty() ? std::string("No movies") : names_[size_t(index_)]) + "  (" + std::to_string(index_ + 1) +
              "/" + std::to_string(names_.size()) + ")  " + info_ +
              (player_.loaded() ? "  frame " + std::to_string(player_.next()) + "/" +
                                      std::to_string(player_.file()->frameCount())
                                : "") +
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
    } else if (ev.key == key::Space && player_.loaded()) {
        player_.pause(!player_.paused(), ctx_.sys.getMillis());
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
