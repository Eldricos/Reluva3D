// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/pages/viewers.h"

#include "audio/decode.h"
#include "graphics/font.h"
#include "common/parallel.h"
#include "common/fs.h"
#include "common/text.h"
#include "engines/awe/game/camera.h"
#include "engines/awe/ui/dialogue_box.h"
#include "engines/awe/render/sprite.h"
#include "engines/awe/formats/tables.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

namespace reluva::awe {

namespace {

// RELUVA_PERF=1: where a frame's time goes, written out every few seconds
// (docs/engines/awe/README.md).
struct FramePerf {
    bool on = std::getenv("RELUVA_PERF") != nullptr;
    double total = 0, draw2d = 0, layers = 0, characters = 0, skin = 0, present = 0;
    int frames = 0, draws = 0;
    std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    static double ms(std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
    }
    void report(const std::string &scene) {
        if (!on || ms(since) < 3000 || frames == 0)
            return;
        const double n = frames;
        std::fprintf(stderr,
                     "perf %s: %.1f fps; per frame %.2f ms: 2D %.2f (%.2f draws; layers %.2f), characters %.2f "
                     "(skinning and lighting %.2f), present %.2f\n",
                     scene.c_str(), n * 1000 / ms(since), total / n, draw2d / n, draws / n, layers / n, characters / n,
                     skin / n, present / n);
        *this = FramePerf();
    }
};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): frame timings summed over every page, for the profile
FramePerf g_perf;

} // namespace

namespace {

bool hasExtension(const std::string &name, std::initializer_list<const char *> exts) {
    const std::string lower = fs::toLower(name);
    for (const char *e : exts)
        if (lower.size() > std::strlen(e) && lower.compare(lower.size() - std::strlen(e), std::string::npos, e) == 0)
            return true;
    return false;
}

bool startsWith(const std::string &name, const char *prefix) {
    const std::string lower = fs::toLower(name);
    return lower.compare(0, std::strlen(prefix), prefix) == 0;
}

// The game's files under `folders` with one of `exts`, sorted.
std::vector<std::string> listFiles(const Archive &files, std::initializer_list<const char *> folders,
                                   std::initializer_list<const char *> exts) {
    std::vector<std::string> out;
    for (const std::string &n : files.list()) {
        bool inFolder = folders.size() == 0;
        for (const char *f : folders)
            inFolder |= startsWith(n, f);
        if (inFolder && hasExtension(n, exts))
            out.push_back(n);
    }
    std::sort(out.begin(), out.end(),
              [](const std::string &a, const std::string &b) { return fs::toLower(a) < fs::toLower(b); });
    return out;
}

std::string shorten(const std::string &s, size_t max) {
    return s.size() <= max ? s : s.substr(0, max - 3) + "...";
}

} // namespace

// --- Page ---------------------------------------------------------------------

void Page::begin2D() {
    _ctx.sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32);
    _ctx.sys.setMouseCursorVisible(true);
}

void Page::clear(uint8_t r, uint8_t g, uint8_t b) {
    // A pixel at a time, as one word (a byte at a time took milliseconds).
    const uint8_t px[4] = {r, g, b, 255};
    uint32_t word;
    std::memcpy(&word, px, 4);
    const size_t n = _ctx.screen.pixels.size() / 4;
    std::vector<uint32_t> row(size_t(std::max(_ctx.screen.w, 1)), word);
    for (size_t i = 0; i < n; i += row.size())
        std::memcpy(_ctx.screen.pixels.data() + i * 4, row.data(), std::min(row.size(), n - i) * 4);
}

void Page::fill(int x, int y, int w, int h, Color c, uint8_t alpha) {
    Surface &s = _ctx.screen;
    for (int yy = std::max(0, y); yy < std::min(s.h, y + h); yy++)
        for (int xx = std::max(0, x); xx < std::min(s.w, x + w); xx++) {
            // Over what's there, by its alpha too: on an overlay, transparent
            // where nothing is drawn, the colour isn't darkened by the
            // emptiness under it.
            uint8_t *p = s.row(yy) + xx * 4;
            const int under = p[3] * (255 - alpha) / 255;
            const int out = alpha + under;
            if (out == 0)
                continue;
            p[0] = uint8_t((c.r * alpha + p[0] * under) / out);
            p[1] = uint8_t((c.g * alpha + p[1] * under) / out);
            p[2] = uint8_t((c.b * alpha + p[2] * under) / out);
            p[3] = uint8_t(out);
        }
}

void Page::drawSubtitles(const std::vector<Subtitle> &subtitles, double seconds) {
    // The game plays them as a conversation in its movie mode (0x43a610):
    // no window, the line due wrapped at 944 and centred in the bottom 64
    // pixels (0x43cffc, 0x43d2a8), coloured as a conversation's are
    // (0x43c070: Antoinette 8, the others 9, then 10 when the speaker
    // changes).
    const TextStyle *style = _ctx.fonts ? _ctx.fonts->style("dialogue") : nullptr;
    if (!style || !style->font)
        return;
    const Subtitle *due = nullptr;
    int colour = 8;
    int dueColour = 8;
    std::string lastSpeaker;
    for (const Subtitle &sub : subtitles) {
        const std::string speaker = fs::toLower(sub.speaker);
        if (speaker.find("antoinette") != std::string::npos) {
            colour = 8;
        } else {
            if (!lastSpeaker.empty() && speaker != lastSpeaker)
                colour = colour == 9 ? 10 : 9;
            else if (colour != 9 && colour != 10)
                colour = 9;
            lastSpeaker = speaker;
        }
        if (seconds >= sub.start && seconds < sub.end) {
            due = &sub;
            dueColour = colour;
        }
    }
    if (!due)
        return;
    static const char *const kColours[] = {"dialogue for main character",
                                           "dialogue text for second character in conversation",
                                           "dialogue text for third character in conversation"};
    const Color c = _ctx.fonts->colour(kColours[dueColour - 8]);
    const std::vector<std::string> lines = gameRows(*style->font, style->pixels, due->line, 944);
    const int lh = style->font->lineHeight(style->pixels);
    int y = kScreenH - 64 + (64 - int(lines.size()) * lh) / 2;
    for (const std::string &l : lines) {
        _ctx.fonts->draw(_ctx.screen, *style, (kScreenW - style->font->width(l, style->pixels)) / 2, y, l, c);
        y += lh;
    }
}

void Page::frame(int x, int y, int w, int h, Color c) {
    fill(x, y, w, 1, c);
    fill(x, y + h - 1, w, 1, c);
    fill(x, y, 1, h, c);
    fill(x + w - 1, y, 1, h, c);
}

int Page::text(int x, int y, const std::string &s, Color c, int scale) {
    return font::drawText(_ctx.screen, x, y, s, c, scale);
}

void Page::label(int x, int y, const std::string &s, Color c) {
    fill(x - 2, y - 1, int(font::length(s)) * font::kAdvance + 4, font::kLineHeight + 1, {0, 0, 0}, 170);
    text(x, y, s, c);
}

void Page::help(const std::string &keys) {
    fill(0, kScreenH - font::kLineHeight - 3, kScreenW, font::kLineHeight + 3, {0, 0, 0}, 200);
    text(4, kScreenH - font::kLineHeight - 1, keys + "   Tab: next page   Esc: launcher", {170, 190, 210});
}

void Page::finishScreenshot(gfx::Renderer3D *renderer, const Surface *overlay) {
    if (_shotFile.empty())
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
    std::filesystem::create_directories(_shotFile.parent_path(), ec);
    const std::vector<uint8_t> tga = image::encodeTga(img);
    std::ofstream out(_shotFile, std::ios::binary);
    out.write(reinterpret_cast<const char *>(tga.data()), std::streamsize(tga.size()));
    _ctx.setTitle((out ? "Saved " : "Couldn't save ") + _shotFile.string());
    _shotFile.clear();
}

int Page::step(uint32_t key, int index, int count) {
    if (count <= 0)
        return 0;
    int to = index;
    if (key == key::kLeft)
        to = index - 1;
    else if (key == key::kRight)
        to = index + 1;
    else if (key == key::kPageUp)
        to = index - 10;
    else if (key == key::kPageDown)
        to = index + 10;
    else if (key == key::kHome)
        to = 0;
    else if (key == key::kEnd)
        to = count - 1;
    else
        return 0;
    to = ((to % count) + count) % count;
    return to - index == 0 ? 0 : to - index;
}

const image::Image &Page::picture(const std::string &name) {
    const std::string key = normalizeArchiveName(name);
    auto it = _pictures.find(key);
    if (it != _pictures.end())
        return it->second;
    if (_pictures.size() > 64)
        _pictures.clear();
    image::Image &img = _pictures[key];
    std::vector<uint8_t> data;
    if (_ctx.files.read(key, data) || image::decode(data, img))
        img = image::Image();
    return img;
}

// --- Pictures ---------------------------------------------------------------------

PicturePage::PicturePage(ViewerContext &ctx) : Page(ctx) {
    _names = listFiles(ctx.files, {"locations/", "interface/", "inventory/", "elements/", "models/"},
                       {".jpg", ".png", ".tga", ".bmp"});
}

void PicturePage::activate() {
    begin2D();
    show();
}

void PicturePage::show() {
    clear(24, 24, 28);
    if (_names.empty())
        return;
    const std::string &name = _names[size_t(_index)];
    const image::Image &img = picture(name);
    if (img.w > 0) {
        // Scaled down (nearest) to fit, over a checkerboard that shows alpha.
        const int areaH = kScreenH - 2 * font::kLineHeight - 8;
        const float scale = std::min({1.0f, float(kScreenW) / float(img.w), float(areaH) / float(img.h)});
        const int w = std::max(1, int(float(img.w) * scale));
        const int h = std::max(1, int(float(img.h) * scale));
        const int x0 = (kScreenW - w) / 2;
        const int y0 = font::kLineHeight + 4 + (areaH - h) / 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const uint8_t *src =
                    img.pixel(std::min(img.w - 1, int(float(x) / scale)), std::min(img.h - 1, int(float(y) / scale)));
                const uint8_t check = ((x / 8 + y / 8) & 1) ? 90 : 60;
                uint8_t *dst = _ctx.screen.row(y0 + y) + (x0 + x) * 4;
                for (int c = 0; c < 3; c++)
                    dst[c] = uint8_t((src[c] * src[3] + check * (255 - src[3])) / 255);
            }
        label(4, 2,
              name + "  " + std::to_string(img.w) + "x" + std::to_string(img.h) + "  (" + std::to_string(_index + 1) +
                  "/" + std::to_string(_names.size()) + ")");
    } else {
        label(4, 2, name + ": can't be decoded", {255, 120, 120});
    }
    help("Left/Right: picture  PgUp/PgDn: 10  Home/End");
    present();
    _ctx.setTitle("Pictures: " + name);
}

void PicturePage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown)
        return;
    if (const int d = step(ev.key, _index, int(_names.size()))) {
        _index += d;
        show();
    }
}

// --- Scenes ---------------------------------------------------------------------

namespace {

// A scene's act variant ("Salon_01_06a.ac2" for act 6A): the base scene's
// name, or "" if it isn't one.
std::string baseSceneOf(const std::string &name) {
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
std::string fileNameOf(const std::string &path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string folderOf(const std::string &name) {
    const size_t slash = name.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : name.substr(0, slash);
}

// The rows named for the movie: the game starts its subtitles as the
// conversation of that name (0x42ae7d), so logos.txt, a copy of intro's,
// shows nothing.
std::vector<Subtitle> moviesOwn(std::vector<Subtitle> all, const std::string &movie) {
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&](const Subtitle &s) { return !s.name.empty() && fs::toLower(s.name) != movie; }),
              all.end());
    return all;
}

} // namespace

ScenePage::ScenePage(ViewerContext &ctx) : Page(ctx) {
    _names = listFiles(ctx.files, {"locations/"}, {".ac2"});
    _state.values["moving"] = 1;
    _runner = std::make_unique<ScriptRunner>(_state, *this);
    _dialogue = std::make_unique<DialogueManager>(_state, *this, *this);
    _dialogue->onLog = [this](const std::string &l) { logScript("    " + shorten(l, 150)); };
    _story = std::make_unique<Story>(_state, *_runner);
    loadOptions();
    _state.action = [this](const std::string &w) {
        if (_optionsMenu)
            return true;
        if (puzzleActive())
            return puzzleCondition(w);
        return slotCondition(w);
    };
    _state.canTalk = [this](const std::vector<std::string> &words) {
        loadDialogue();
        const TalkArgs t = parseTalkArgs(words, true);
        return _dialogue->canTalk(t.name, t.part, t.thread, t.nest, t.gen, t.number, t.plus, t.greet);
    };
    {
        std::vector<uint8_t> data;
        if (!ctx.files.read("text/acts.txt", data))
            _story->setActs(readActs(parseTable(decodeText(data))));
    }
    _story->onActTables = [this](const std::string &) { loadDialogue(); };
    _story->onLoadLocation = [this](const std::string &location, bool flush) {
        // Reluva reads scenes as they're shown: loading a location only
        // forgets, for a new place, how the scenes were left.
        logScript("  (location " + location + (flush ? ", a new place" : "") + ")");
        if (flush) {
            // A new place: the game drops the last one's scenes, and its
            // characters with them.
            _positions.clear();
            _npcPositions.clear();
            _keptCharacters.clear();
            _boxOpenedHere = false;
        }
        beginLoad(location, true);
    };
    _runner->onEvent = [this](const ScriptEvent &e) {
        std::string line;
        for (const std::string &w : e.words)
            line += (line.empty() ? "" : " ") + w;
        logScript("  " + line);
    };
}

ScenePage::~ScenePage() {
    releaseGpu();
}

void ScenePage::activate() {
    if (_ctx.sys.init3D(kScreenW, kScreenH, _renderer))
        _renderer = nullptr, begin2D();
    applyAntiAliasing();
    load();
}

void ScenePage::deactivate() {
    if (!_systemCursor)
        _ctx.sys.setMouseCursorVisible(_systemCursor = true);
    releaseGpu();
    if (_renderer)
        _renderer->setMultisampling(1); // the other pages draw without
    _renderer = nullptr;
}

void ScenePage::releaseGpu() {
    _characters.clear();
    _models.clear();
    _animations.clear();
    if (_renderer) {
        for (gfx::MeshId m : _frameMeshes)
            _renderer->destroyMesh(m);
        for (gfx::TextureId t : _frameTextures)
            _renderer->destroyTexture(t);
        for (PictureLayer &l : _layers)
            dropLayer(l);
        for (auto &[o, t] : _aloneTextures)
            if (t.texture)
                _renderer->destroyTexture(t.texture);
        for (auto &[name, t] : _pointTextures)
            if (t)
                _renderer->destroyTexture(t);
        if (_quad)
            _renderer->destroyMesh(_quad);
    }
    _frameMeshes.clear();
    _frameTextures.clear();
    _layers.clear();
    _aloneTextures.clear();
    _pointTextures.clear();
    _characterOrder.clear();
    _quad = 0;
}

void ScenePage::load() {
    _frame++;
    _selected = -1;
    _error.clear();
    _base.clear();
    _scene = Ac2Scene();
    _layersMade++; // its pictures' layers are made afresh
    _lightConditions.clear();
    _characters.clear();
    _loops.clear();
    _particles.clear();
    _scrolls.clear();
    if (!_playing)
        _state.act.clear();
    _grid = WalkGrid();
    _path.clear();
    _hover = {};
    _scriptObject = nullptr;
    _resumeAfter = nullptr;
    _sceneShown = true;
    _leadGoal = {};
    _slots.reset();
    _optionsMenu.reset();
    closePuzzle();
    _shownNow.clear();
    if (_names.empty())
        return draw();
    auto read = [&](const std::string &name, Ac2Scene &out) {
        std::vector<uint8_t> data;
        Error e = _ctx.files.read(name, data);
        if (!e)
            e = parseAc2(data, out);
        return e;
    };
    const std::string &name = _names[size_t(_index)];
    Ac2Scene variant;
    if (Error e = read(name, variant))
        _error = e.message;
    // An act's variant changes its base scene: shown over it, as the base
    // has the camera and the pictures.
    const std::string base = baseSceneOf(name);
    if (!base.empty() && _ctx.files.has(base) && !read(base, _scene)) {
        // Merged as 0x435990's mode 2 does: the base's camera and settings
        // stay (the variant's CAMR, SHOW, PUZZ... are read and dropped:
        // Calais.14-15_14.02_06a and _06b have one); music and events are
        // added; an object is added unless the scene has it already.
        _base = base;
        if (!_scene.records.empty() && _scene.records.back().tag == "DONE")
            _scene.records.pop_back();
        auto same = [](const Ac2Record &a, const Ac2Record &b) {
            // 0x42b680: the same tag, and with a file the same file and
            // place (the player by its file alone); without, a hotspot at
            // the same place with the same rectangle, a light of the same
            // type at the same place, anything else whatever it is.
            if (a.tag != b.tag)
                return false;
            const bool at = a.f("x") == b.f("x") && a.f("y") == b.f("y") && a.f("z") == b.f("z");
            if (!a.text("file").empty())
                return fs::toLower(a.text("file")) == fs::toLower(b.text("file")) && (at || a.tag == "PCXX");
            if (a.tag == "SPOT")
                return at && a.i("left") == b.i("left") && a.i("top") == b.i("top") && a.i("right") == b.i("right") &&
                       a.i("bottom") == b.i("bottom");
            if (a.tag == "LITE")
                return a.i("type") == b.i("type") && at;
            return true;
        };
        for (const Ac2Record &r : variant.records) {
            if (r.tag == "CAMR" || r.tag == "SHOW" || r.tag == "PUZZ" || r.tag == "FAKE" || r.tag == "OUTD" ||
                r.tag == "NVNT" || r.tag == "FOOT" || r.tag == "SCEN")
                continue;
            if (isAc2Object(r.tag) && std::any_of(_scene.records.begin(), _scene.records.end(),
                                                  [&](const Ac2Record &b) { return same(r, b); }))
                continue;
            _scene.records.push_back(r);
        }
        // The act it's for: "salon_01_06a.ac2" is act 06a's.
        if (!_playing)
            _state.act = name.substr(name.rfind('_') + 1, 3);
    } else {
        _scene = std::move(variant);
    }
    if (const Ac2Record *cam = _scene.camera())
        _grid.load(*cam);
    // This scene's name before its characters load: where they were kept
    // is looked up by it.
    _state.scene = fs::toLower(name.substr(name.find_last_of('/') + 1));
    {
        // Its folder is its location: locations/<location>/<scene>.ac2.
        const std::string lower = fs::toLower(name);
        const size_t from = lower.find('/') + 1;
        _location = _state.location = lower.substr(from, lower.find('/', from) - from);
    }
    // In a game at once; when browsing, once the page stays on the scene
    // (they take seconds to load).
    _charactersDue = _renderer && _showCharacters;
    if (_charactersDue && _playing) {
        _charactersDue = false;
        loadCharacters();
    }
    _charactersAt = _lastMs;
    loadLoops();
    for (const Ac2Record *o : _scene.objects())
        if (o->tag == "PART" || o->tag == "RAIN" || o->tag == "SNOW") {
            ParticleSystem &system =
                _particles.emplace(o, ParticleSystem(ParticleSettings::fromRecord(*o))).first->second;
            // SYST warmUp: run for 4.5 s in steps of 0.01 at load, so the
            // snow is already falling (0x42e68b, 0x42cbb0).
            if (const Ac2Record *syst = o->part("SYST"); syst && syst->i("warmUp") != 0)
                for (float t = 0; t < 4.5f; t += 0.01f)
                    system.update(0.01f);
        } else if (const Ac2Record *sprt = o->part("SPRT"); sprt && sprt->i("scroll") != 0) {
            _scrolls[o] = Scroll{o->f("x"), o->f("y"), 0};
        }
    // Entering the scene switches its objects on (0x4362b0, slot 0x48):
    // each shown or not by its condition now, whatever a script did on
    // an earlier visit.
    retestObjects();
    if (_playing) {
        enterScene();
    } else {
        // Browsing: the scene's puzzle works too (its scripts' talk and
        // tags as in a game).
        for (const Ac2Record &r : _scene.records)
            if (r.tag == "PUZZ") {
                const std::string kind = fs::toLower(r.text("value"));
                if (kind == "tornletter" || kind == "safe" || kind == "puzzlebox")
                    openPuzzle(kind);
            }
    }
    draw();
}

void ScenePage::enterScene() {
    // Its lights on or off by their conditions now (0x4362b0, 0x420ee0).
    _lightConditions = lightConditions(_scene, _state);
    playMusic();
    // The save and load screens (PUZZ save, load).
    for (const Ac2Record &r : _scene.records)
        if (r.tag == "PUZZ" && (fs::toLower(r.text("value")) == "save" || fs::toLower(r.text("value")) == "load"))
            openSlots(fs::toLower(r.text("value")) == "save");
        else if (r.tag == "PUZZ" && fs::toLower(r.text("value")) == "options")
            openOptions();
        else if (r.tag == "PUZZ")
            openPuzzle(fs::toLower(r.text("value")));
    // A saved game being restored runs no scene events (0x436331).
    if (_restoring)
        return;
    // The first event whose condition holds, queued (0x436a90).
    for (const Ac2Record &r : _scene.records)
        if (r.tag == "EVNT")
            if (const Ac2Value *v = r.field("events"))
                for (const auto &[condition, script] : v->pairs)
                    if (testCondition(condition, _state)) {
                        logScript("scene event: " + shorten(script, 120));
                        _runner->run(script);
                        goto queued;
                    }
queued:
    if (_restoring)
        return;
    // The visit, after the events: "!beenhere this" holds during the
    // first visit's.
    std::string &acts = _state.texts[_state.scene];
    if ((" " + acts + " ").find(" " + _state.act + " ") == std::string::npos)
        acts += (acts.empty() ? "" : " ") + _state.act;
}

void ScenePage::playMusic() {
    // Every MUSC entry whose condition holds plays, looping; what played
    // before and isn't among them stops (0x436900). Shared ambient loops
    // go on from scene to scene.
    std::set<std::string> want;
    for (const Ac2Record &r : _scene.records)
        if (r.tag == "MUSC")
            if (const Ac2Value *v = r.field("music"))
                for (const auto &[condition, file] : v->pairs)
                    if (testCondition(condition, _state))
                        want.insert(fs::toLower(file));
    for (auto it = _music.begin(); it != _music.end();) {
        if (!want.count(it->first)) {
            if (_ctx.mixer)
                _ctx.mixer->stop(it->second);
            it = _music.erase(it);
        } else {
            ++it;
        }
    }
    for (const std::string &file : want)
        playLooping(file, file.find("music") != std::string::npos);
}

void ScenePage::playLooping(const std::string &file, bool music) {
    if (_music.count(file))
        return;
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (!_ctx.mixer || file.empty())
        return;
    if (_ctx.files.read(file, data) || audio::decodeSound(data, *sound)) {
        logScript("    (no sound " + file + ")");
        return;
    }
    _music[file] = _ctx.mixer->play(sound, music ? audio::SoundType::kMusic : audio::SoundType::kEffects,
                                    audio::kMaxVolume, 0, true);
}

bool ScenePage::startMovie(ScriptRunner &runner, const ScriptEvent &e, bool wait) {
    const std::string name = fs::toLower(e.arg(1));
    auto flag = [&](const char *f) { return fs::toLower(e.arg(2)) == f || fs::toLower(e.arg(3)) == f; };
    // FMV\, then Movies\ (0x42ac70); none: the event ends at once.
    std::string path;
    for (const std::string &p : {"fmv/" + name + ".bik", "movies/" + name + ".bik", name + ".bik"})
        if (path.empty() && _ctx.files.has(p))
            path = p;
    std::vector<uint8_t> data;
    if (path.empty() || _ctx.files.read(path, data)) {
        logScript("    (no movie " + name + ")");
        return false;
    }
    // Unless "inscene", the scene is left for it and every sound stops
    // (0x436410, 0x425250); "silent" plays it without its own.
    if (!flag("inscene")) {
        _sceneShown = false;
        for (const auto &[file, handle] : _music)
            if (_ctx.mixer)
                _ctx.mixer->stop(handle);
        _music.clear();
    }
    auto player = std::make_unique<MoviePlayer>(_ctx.mixer);
    if (Error err = player->open(std::move(data), _lastMs,
                                 !flag("silent") && _ctx.mixer)) { // no sound to decode without a mixer
        logScript("    (" + name + ": " + err.message + ")");
        return false;
    }
    _movie = std::move(player);
    _movieName = name;
    // Its subtitles: Text\Movies\<name>.txt (the game plays them through
    // the dialogue in its movie mode).
    _movieSubtitles.clear();
    std::vector<uint8_t> text;
    if (!_ctx.files.read("text/movies/" + name + ".txt", text))
        _movieSubtitles = moviesOwn(parseSubtitles(decodeText(text)), name);
    // Starting them is starting a talk (0x42ae7d, not a dry run): the
    // inventory closes and what's held is let go.
    if (!_movieSubtitles.empty())
        talkStarting(name);
    _movieRunner = wait ? &runner : nullptr;
    return wait;
}

void ScenePage::endMovie() {
    _movie.reset();
    _movieSubtitles.clear();
    if (ScriptRunner *r = _movieRunner) {
        _movieRunner = nullptr;
        r->finished("movie");
    }
    draw();
}

std::vector<std::string> ScenePage::locationFiles(const std::string &location) const {
    // A file's suffix (after its last '_', when it has two) applies if it's
    // the act's, or night, snow or moving when those hold; a base with a
    // variant that applies is loaded with it (0x43e3d0..0x43e520).
    auto suffix = [](const std::string &stem) {
        const size_t first = stem.find('_');
        const size_t last = stem.rfind('_');
        return first != last && last != std::string::npos ? stem.substr(last + 1) : std::string();
    };
    auto value = [&](const char *name) {
        const auto it = _state.values.find(name);
        return it == _state.values.end() ? 0 : it->second;
    };
    auto applies = [&](const std::string &sfx) {
        return sfx == _state.act || (sfx == "night" && value("time")) || (sfx == "snow" && value("weather")) ||
               (sfx == "moving" && value("moving"));
    };
    std::vector<std::string> stems;
    std::vector<std::string> files;
    const std::string folder = "locations/" + location + "/";
    for (const std::string &n : _names) {
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
        for (const std::string &other : stems)
            if (other.size() > stems[i].size() + 1 && other.compare(0, stems[i].size() + 1, stems[i] + "_") == 0 &&
                applies(suffix(other)))
                variant = true;
        if (!variant)
            files.push_back(folder + stems[i] + ".ac2");
    }
    return files;
}

void ScenePage::beginLoad(const std::string &location, bool forAct) {
    // 0x422790: the loading screen when more than one file is to load,
    // faded in if the screen was faded out.
    const LocationLoad keep = _load;
    _load = LocationLoad();
    _load.scene = keep.scene, _load.verb = keep.verb, _load.entry = keep.entry, _load.runner = keep.runner;
    _load.walkIn = keep.walkIn;
    _load.restore = keep.state != 0 && keep.restore; // a load under way's, not one over
    _load.forAct = forAct;
    _load.location = location;
    _load.files = locationFiles(location);
    _load.state = 2;
    if (_load.files.size() > 1) {
        _load.screen = true;
        if (_fade >= 1) {
            _fadeTarget = 0;
            _load.fadedIn = true;
        }
    }
    draw();
}

void ScenePage::updateLoad() {
    LocationLoad &l = _load;
    if (l.state == 1) {
        // A scene in another place: once faded out.
        if (_fade >= 1) {
            if (l.restore) {
                // A saved game's location (states 7, 2, 3, 8).
                const SaveGame::Act &a = _restoring->act;
                ActRow row;
                row.act = _state.act;
                row.preLoad = a.preLoad, row.location = fs::toLower(a.location), row.postLoad = a.postLoad;
                row.completion = a.completion, row.event = a.event, row.music = a.music;
                row.time = a.timeOfDay, row.weather = a.weather, row.moving = a.moving;
                _story->restore(row, l.location);
                return;
            }
            if (_playing)
                _story->leavingScene();
            if (_story->sceneLocation(l.location)) {
                // A new place: the last one's scenes dropped (0x423430).
                logScript("  (location " + l.location + ", a new place)");
                _positions.clear();
                _npcPositions.clear();
                _keptCharacters.clear();
                _boxOpenedHere = false;
            }
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
            if (_drawing && !_ctx.files.read(l.files[l.next], data))
                parseAc2(data, scene);
            l.next++;
            l.wheel++;
            draw();
            return;
        }
        l.state = 3;
        if (l.fadedIn)
            _fadeTarget = 1;
        return;
    }
    if (l.state == 3) {
        if (_fade != _fadeTarget)
            return;
        l.screen = false;
        _location = _state.location = l.location; // its scenes are now the current location's
        if (l.forAct) {
            l.state = 0;
            _story->locationLoaded();
            if (l.restore && _restoring) {
                l.restore = false; // done: a later act's load isn't one
                finishRestore();
                if (ScriptRunner *r = l.runner) {
                    l.runner = nullptr;
                    r->finished(l.verb);
                }
            }
            return;
        }
        // The scene, faded in (states 6, 9).
        changeScene(l.scene, l.entry, l.walkIn);
        _fadeTarget = 0;
        l.state = 4;
        return;
    }
    if (l.state == 4 && _fade == _fadeTarget) {
        l.state = 0;
        if (ScriptRunner *r = l.runner) {
            l.runner = nullptr;
            r->finished(l.verb);
        }
    }
}

void ScenePage::newGame() {
    // As the game starts (0x4026e9): the main menu, whose event is
    // "reset"; its Play is "act", starting act 01A.
    _playing = true;
    _runner->clear();
    // The state afresh; the dialogue's test of parts told and the screens'
    // "action" test stay, the tables are read again.
    auto partTold = std::move(_state.partTold);
    auto action = std::move(_state.action);
    auto canTalk = std::move(_state.canTalk);
    auto onTest = std::move(_state.onTest);
    _state = GameState{};
    _state.partTold = std::move(partTold);
    _state.action = std::move(action);
    _state.canTalk = std::move(canTalk);
    _state.onTest = std::move(onTest);
    _state.values["master"] = 1; // the debug switch, off ([0x48b084] = 1)
    _dialogueAct = "-";
    _positions.clear();
    _scriptLog.clear();
    _fade = _fadeTarget = 0;
    _movie.reset();
    _movieRunner = nullptr;
    _soundRunner = nullptr;
    _interfaceOff = false;
    _letterbox = Letterbox();
    _ui = Interface();
    _sceneShown = true;
    _load = LocationLoad();
    _restoring.reset();
    _npcPositions.clear();
    _keptCharacters.clear();
    _boxOpenedHere = false;
    _gameScene.clear();
    _previousMenu.clear();
    _yesNo.reset();
    _slots.reset();
    _optionsMenu.reset();
    closePuzzle();
    logScript("new game");
    _runner->run("musica motoetheme, movie logos inscene, scene menu.main_01 1 0");
}

void ScenePage::drawParticles(const ParticleSystem &system, bool keepAlpha) {
    const ParticleSettings &s = system.settings();
    const Ac2Record *camr = _scene.camera();
    if (!camr && !s.screenSpace)
        return;
    const image::Image *img = &picture(s.texture);
    if (img->w <= 0) {
        // The texture isn't there: a soft white dot instead.
        if (_dot.w == 0) {
            _dot.w = _dot.h = 16;
            _dot.rgba.resize(16 * 16 * 4);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    const float dx = (float(x) - 7.5f) / 8;
                    const float dy = (float(y) - 7.5f) / 8;
                    uint8_t *p = _dot.pixel(x, y);
                    p[0] = p[1] = p[2] = 255;
                    p[3] = uint8_t(255 * std::clamp(1 - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f));
                }
        }
        img = &_dot;
    }
    const SceneCamera cam = camr ? SceneCamera::fromRecord(*camr) : SceneCamera();
    const gfx::Vec3 eye = cam.eye();
    std::vector<ParticleSystem::Point> points;
    system.points(points);
    for (const ParticleSystem::Point &p : points) {
        float x;
        float y;
        float distance;
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
        drawPointSprite(_ctx.screen, *img, x, y, size, p.rgba, !s.alphaBlend, keepAlpha);
    }
}

void ScenePage::drawPointSprites(const ParticleSystem &system) {
    // drawParticles' squares as quads for the GPU, one mesh for the frame:
    // the texture (nearest, as the game's point sprites sample it) times
    // each particle's colour, added or blended by its alpha.
    const ParticleSettings &s = system.settings();
    const Ac2Record *camr = _scene.camera();
    if (!camr && !s.screenSpace)
        return;
    const SceneCamera cam = camr ? SceneCamera::fromRecord(*camr) : SceneCamera();
    const gfx::Vec3 eye = cam.eye();
    std::vector<ParticleSystem::Point> points;
    system.points(points);
    std::vector<gfx::Vertex> verts;
    std::vector<uint32_t> indices;
    for (const ParticleSystem::Point &p : points) {
        float x;
        float y;
        float distance;
        if (s.screenSpace) {
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
        const float size = std::min(64.0f, distance > 0 ? s.size * float(kScreenH) / distance : 64.0f);
        if (size <= 0)
            continue;
        const float l = 2 * (x - size / 2) / kScreenW - 1;
        const float r = 2 * (x + size / 2) / kScreenW - 1;
        const float t = 1 - 2 * (y - size / 2) / kScreenH;
        const float b = 1 - 2 * (y + size / 2) / kScreenH;
        const float corners[4][4] = {{l, t, 0, 0}, {r, t, 1, 0}, {r, b, 1, 1}, {l, b, 0, 1}};
        const uint32_t base = uint32_t(verts.size());
        for (const auto &c : corners) {
            gfx::Vertex v{};
            v.pos[0] = c[0], v.pos[1] = c[1], v.pos[2] = 0.5f;
            v.uv[0] = c[2], v.uv[1] = c[3];
            std::copy(p.rgba, p.rgba + 4, v.color);
            verts.push_back(v);
        }
        for (uint32_t i : {0u, 2u, 1u, 0u, 3u, 2u})
            indices.push_back(base + i);
    }
    if (verts.empty())
        return;
    // Its texture, made once; the soft white dot if it isn't there.
    gfx::TextureId &tex = _pointTextures[s.texture];
    if (!tex) {
        image::Image img = picture(s.texture);
        if (img.w <= 0) {
            img.w = img.h = 16;
            img.rgba.assign(16 * 16 * 4, 255);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    const float dx = (float(x) - 7.5f) / 8;
                    const float dy = (float(y) - 7.5f) / 8;
                    img.rgba[size_t(y * 16 + x) * 4 + 3] =
                        uint8_t(255 * std::clamp(1 - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f));
                }
        }
        gfx::TextureParams params;
        params.clamp = true;
        params.mipmaps = false;
        params.nearest = true;
        _renderer->createTexture(img.w, img.h, img.rgba.data(), params, tex);
    }
    gfx::MeshId mesh = 0;
    if (_renderer->createMesh(verts, indices, mesh))
        return;
    _frameMeshes.push_back(mesh);
    gfx::DrawCall call;
    call.mesh = mesh;
    call.indexCount = uint32_t(indices.size());
    call.texture = tex;
    call.rgbSource = call.alphaSource = gfx::ColorSource::kVertex;
    // The pictures' brightness (applyBrightness's slope).
    const float bright = float(32 * brightnessLevel() + 97) / 257.0f;
    call.color = {bright, bright, bright, 1};
    call.state.src = s.alphaBlend ? gfx::Blend::kSrcAlpha : gfx::Blend::kOne;
    call.state.dst = s.alphaBlend ? gfx::Blend::kOneMinusSrcAlpha : gfx::Blend::kOne;
    call.state.depthTest = false;
    call.state.depthWrite = false;
    call.state.cull = gfx::Cull::kNone;
    _renderer->setView(gfx::identity(), {0, 0, 0});
    _renderer->draw(call);
}

void ScenePage::loadLoops() {
    if (!_drawing)
        return; // (only seen)
    for (const Ac2Record *o : _scene.objects()) {
        if (!hasExtension(o->text("file"), {".bik"}))
            continue;
        Loop loop;
        loop.movie = std::make_unique<bink::Movie>();
        std::vector<uint8_t> data;
        Error e = _ctx.files.read(o->text("file"), data);
        if (!e)
            e = loop.movie->open(std::move(data));
        if (e) {
            _error = o->text("file") + ": " + e.message;
            continue;
        }
        _loops.emplace(o, std::move(loop));
    }
    advanceLoops(0);
}

bool ScenePage::advanceLoops(float time) {
    bool changed = false;
    for (auto &[o, loop] : _loops) {
        // "Animated Effects" off: the flagged videos stay on their frame.
        if (frozen(*o) && !loop.frame.rgba.empty())
            continue;
        const bink::Movie &m = *loop.movie;
        const size_t count = m.frameCount();
        const double fps = m.rateDenominator() ? double(m.rateNumerator()) / m.rateDenominator() : 0;
        if (count == 0 || fps <= 0)
            continue;
        const size_t played = size_t(double(std::max(0.0f, time - loop.start)) * fps);
        // BNKX loop 0 (its default is 1): played once, then held on its
        // last frame (0x4061d8).
        const Ac2Record *bnkx = o->part("BNKX");
        const bool once = bnkx && bnkx->field("loop") && bnkx->i("loop") == 0;
        const size_t want = once ? std::min(played, count - 1) : played % count;
        if (want + 1 == loop.next && !loop.frame.rgba.empty())
            continue;
        // Frames build on the one before: from the start again when it
        // has gone round.
        if (want + 1 < loop.next || loop.next == 0) {
            loop.movie->rewind();
            loop.next = 0;
        }
        for (; loop.next <= want; loop.next++) {
            bool more = false;
            if (loop.movie->nextFrame(more) || !more)
                break;
        }
        loop.frame.w = m.width();
        loop.frame.h = m.height();
        // As the game's binkw32.dll converts its frames.
        loop.movie->toRgba(loop.frame.rgba, bink::Movie::Rgb::kBinkw32);
        changed = true;
    }
    return changed;
}

void ScenePage::loadCharacters() {
    const Ac2Record *camr = _scene.camera();
    if (!_renderer || !camr)
        return;
    bool any = false;
    for (const Ac2Record *o : _scene.objects())
        any |= o->tag == "NPCX" || o->tag == "PCXX";
    if (!any)
        return;
    // Loading them takes a few seconds each the first time (granny2.dll
    // decodes their textures): say so, in the viewer. Not in a game: the
    // game goes from scene to scene (a talk's close-ups) with no frame
    // between, and a black one would flash.
    if (!_playing) {
        std::fill(_ctx.screen.pixels.begin(), _ctx.screen.pixels.end(), 0);
        font::drawText(_ctx.screen, 8, 8, "Loading the characters...", {255, 230, 150}, 2);
        _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
        _renderer->endFrame(&_ctx.screen);
    }

    for (const Ac2Record *o : _scene.objects()) {
        if (o->tag != "NPCX" && o->tag != "PCXX")
            continue;
        const std::string file = normalizeArchiveName(o->text("file"));
        Character c;
        c.name = file;
        auto found = _models.find(file);
        if (found == _models.end()) {
            Gr2File gr2;
            Model model;
            Error e = openGr2(_ctx.files, file, gr2);
            if (!e)
                e = loadModel(gr2, model);
            if (e) {
                _error = file + ": " + e.message;
                found = _models.emplace(file, nullptr).first;
            } else {
                found = _models.emplace(file, std::make_shared<GpuModel>(*_renderer, std::move(model))).first;
            }
        }
        c.model = found->second;
        if (!c.model)
            continue;
        const std::string folder = folderOf(file);
        std::string who = file.substr(folder.size() + 1);
        who = who.substr(0, who.find("_model"));
        c.x = o->f("x"), c.y = o->f("y"), c.z = o->f("z"), c.rotY = o->f("rotY");
        c.rotX = o->f("rotX"), c.rotZ = o->f("rotZ");
        // Its scale (1 unset); -1 down or across is the first's (0x410389).
        auto scaled = [&](const char *name, float otherwise) {
            const float v = o->field(name) ? o->f(name) : otherwise;
            return v == -1 ? c.scale[0] : v;
        };
        c.scale[0] = o->field("spriteScaleX") ? o->f("spriteScaleX") : 1;
        c.scale[1] = scaled("spriteScaleY", 1);
        c.scale[2] = scaled("spriteScaleZ", 1);
        c.player = o->tag == "PCXX";
        c.who = fs::toLower(who);
        c.record = o;
        // A character following the player (FLLW, on its COND) arrives as
        // the player does (0x440070).
        const Ac2Record *fllw = o->part("FLLW");
        const Ac2Record *cond = o->part("COND");
        c.follower = !c.player && fllw && fllw->i("follow") != 0 &&
                     testCondition(cond ? cond->text("condition") : std::string(), _state);
        // Arriving by "scene <name> <n>": at its walk point n, else its
        // first; with -1 where they were when the scene was left
        // (0x440690).
        if ((c.player || c.follower) && _entryPoint >= 0) {
            if (const Ac2Record *w = o->part("WALK"); w && !w->parts.empty()) {
                const Ac2Record *at = &w->parts.front();
                for (const Ac2Record &pt : w->parts)
                    if (pt.i("id") == _entryPoint)
                        at = &pt;
                c.x = at->f("x"), c.z = at->f("z"), c.rotY = at->f("rotY");
            }
        } else if (c.player && _playing) {
            if (const auto kept = _positions.find(_state.scene); kept != _positions.end())
                c.x = kept->second[0], c.z = kept->second[1], c.rotY = kept->second[2];
        }
        if (!c.player)
            if (const auto kept = _npcPositions.find(_state.scene + "|" + fs::toLower(fileNameOf(file)));
                kept != _npcPositions.end()) {
                const SaveGame::Placement &p = kept->second;
                c.x = p.position[0], c.y = p.position[1], c.z = p.position[2], c.rotY = p.rotation[1];
            }
        // Its heading in the scene's file: one put back where it was left
        // keeps the home it had.
        c.home = normalizeDegrees(o->f("rotY") * 57.29578f);
        if (const Ac2Record *m = o->part("MODL"); m && m->field("shadow"))
            c.shadow = m->i("shadow") != 0;
        // Its animations: <who>_<name>.gr2 beside its model, loaded as
        // they're asked for.
        auto loader = [this, folder, who](const std::string &name) -> std::shared_ptr<const Animation> {
            const std::string path = folder + "/" + who + "_" + name + ".gr2";
            auto a = _animations.find(path);
            if (a == _animations.end()) {
                std::shared_ptr<Animation> anim;
                if (_ctx.files.has(path)) {
                    Gr2File gr2;
                    anim = std::make_shared<Animation>();
                    if (openGr2(_ctx.files, path, gr2) || awe::loadAnimation(gr2, *anim))
                        anim.reset();
                }
                a = _animations.emplace(path, anim).first;
            }
            return a->second;
        };
        c.actor = std::make_unique<Actor>(
            c.model->model(), o, loader, [this](const std::string &cond) { return testCondition(cond, _state); },
            unsigned(_characters.size() + 1));
        Actor *actor = c.actor.get();
        actor->onAnimDone = [this, actor](const std::string &name) {
            for (Character &ch : _characters)
                if (ch.actor.get() == actor && ch.waiting && ch.waitVerb == "anim" &&
                    (name == ch.waitAnim || name == ch.waitAnimAs)) {
                    ScriptRunner *r = ch.waiting;
                    ch.waiting = nullptr;
                    r->finished("anim");
                }
        };
        actor->onRotateDone = [this, actor]() {
            for (Character &ch : _characters)
                if (ch.actor.get() == actor && ch.waiting && ch.waitVerb == "rotate") {
                    ScriptRunner *r = ch.waiting;
                    ch.waiting = nullptr;
                    r->finished("rotate");
                }
        };
        actor->activate();
        // Walking in (0x440808): the player (and a follower) straight ahead
        // from the walk point, 100 units (a follower 50), unless the scene
        // was entered with its flag 0.
        if ((c.player || c.follower) && _entryPoint >= 0 && _entryWalk && startWalk(c)) {
            c.walkIn = c.follower ? 50.0f : 100.0f;
            logScript("    (" + c.who + " walks in)");
        }
        // Models MODL attaches at load, on their conditions (Poirot's hat,
        // Antoinette's coat; her snowshoes with "tag snowshoeson").
        if (const Ac2Record *m = o->part("MODL"))
            if (const Ac2Value *list = m->field("list"))
                for (const auto &[condition, path] : list->pairs) {
                    const std::string prop = normalizeArchiveName(path);
                    if (!testCondition(condition, _state))
                        continue;
                    if (auto pm = propModel(prop))
                        c.props.push_back({prop, pm});
                }
        if (o->tag == "NPCX") {
            c.routine = std::make_unique<ScriptRunner>(_state, *this);
            c.routine->setCharacter(c.who);
            queueRoutine(c);
        }
        // Back in a scene of the place: as it was left (the game kept it).
        if (const auto kept = _keptCharacters.find(_state.scene + "|" + fs::toLower(fileNameOf(file)));
            kept != _keptCharacters.end()) {
            const KeptCharacter &k = kept->second;
            c.paused = k.paused && c.routine;
            c.shadow = k.shadow;
            c.props.clear();
            for (const std::string &path : k.props)
                if (auto pm = propModel(path))
                    c.props.push_back({path, pm});
            if (!k.endless.empty())
                c.actor->play(k.endless, 0, 0);
        }
        placeCharacter(c);
        _characters.push_back(std::move(c));
    }
}

void ScenePage::draw() {
    if (!_drawing)
        return;
    const auto drawStart = std::chrono::steady_clock::now();
    struct Timed {
        std::chrono::steady_clock::time_point t;
        ~Timed() {
            g_perf.draw2d += FramePerf::ms(t);
            g_perf.draws++;
        }
    } timed{drawStart};
    const std::vector<const Ac2Record *> objects = _scene.objects();
    // Pictures and video loops back to front, by depth: the background at
    // -10000, what's behind its windows further still. A picture's d0,
    // when set, is its depth instead of z (the view outside a window: d0
    // -11000, z 0). Those whose condition doesn't hold now are left out
    // (the countryside going by: "moving >0").
    std::vector<const Ac2Record *> pictures;
    for (const Ac2Record *o : objects) {
        if (!objectShown(*o) || fogged(*o))
            continue;
        if (_particles.count(o)) {
            // "Particle Effects" off: none drawn (0x42d4be).
            if (_options.get(GameOptions::kRain))
                pictures.push_back(o);
        } else if ((hasExtension(o->text("file"), {".jpg", ".png", ".tga", ".bmp"}) || _loops.count(o)) &&
                   !puzzleDraws(*o)) {
            pictures.push_back(o);
        }
    }
    auto imageOf = [&](const Ac2Record *o) -> const image::Image & {
        const auto loop = _loops.find(o);
        return loop != _loops.end() ? loop->second.frame : picture(o->text("file"));
    };
    // Placed as the game's sprites are: scaled, turned about the top-left
    // corner, moved (docs/engines/awe/games/motoe/formats/ac2.md).
    auto place = [&](const Ac2Record *o, bool keepAlpha) {
        if (const auto ps = _particles.find(o); ps != _particles.end())
            return drawParticles(ps->second, keepAlpha);
        const image::Image &img = imageOf(o);
        if (img.w <= 0)
            return;
        SpriteTransform t;
        t.x = o->f("x"), t.y = o->f("y");
        if (const auto ov = _overrides.find(o); ov != _overrides.end()) {
            // Moved and turned by a puzzle: about its centre, tinted if
            // picked.
            const PictureOverride &p = ov->second;
            const float c = std::cos(p.roll);
            const float sn = std::sin(p.roll);
            t.roll = p.roll;
            t.x = p.x + p.cx - (p.cx * c - p.cy * sn);
            t.y = p.y + p.cy - (p.cx * sn + p.cy * c);
            if (p.tint) {
                image::Image tinted = img;
                for (size_t i = 2; i < tinted.rgba.size(); i += 4)
                    tinted.rgba[i] = uint8_t(tinted.rgba[i] * 3 / 4);
                return drawSprite(_ctx.screen, tinted, t, keepAlpha);
            }
            return drawSprite(_ctx.screen, img, t, keepAlpha);
        }
        if (_optionsMenu)
            // The options screen's slider thumbs, at their values.
            for (int i = 0; i < 3; i++)
                if (const Ac2Record *thumb = [&]() -> const Ac2Record * {
                        int n = i;
                        for (const Ac2Record *x : objects)
                            if (fs::toLower(fileNameOf(x->text("file"))) == "slider.jpg" && n-- == 0)
                                return x;
                        return nullptr;
                    }();
                    thumb == o)
                    t.x = float(thumbX(i));
        if (o->field("spriteScaleX"))
            t.scaleX = o->f("spriteScaleX"), t.scaleY = o->f("spriteScaleY");
        if (const Ac2Record *p = o->part(o->tag == "BINK" ? "BNKX" : "SPRT"))
            t.roll = p->f("roll");
        if (const auto sc = _scrolls.find(o); sc != _scrolls.end()) {
            // Scrolling: where it has got to, and again a width or height
            // behind, so the two cover the way (0x437db6).
            t.x = sc->second.x, t.y = sc->second.y;
            drawSprite(_ctx.screen, img, t, keepAlpha);
            const int way = o->part("SPRT")->i("scroll");
            t.x += way == 1 ? float(img.w) : way == 2 ? -float(img.w) : 0.0f;
            t.y += way == 3 ? float(img.h) : way == 4 ? -float(img.h) : 0.0f;
            return drawSprite(_ctx.screen, img, t, keepAlpha);
        }
        if (!t.plain())
            drawSprite(_ctx.screen, img, t, keepAlpha);
        else if (keepAlpha)
            image::blitOver(_ctx.screen, img, int(t.x), int(t.y));
        else
            image::blit(_ctx.screen, img, int(t.x), int(t.y));
    };
    auto depth = [&](const Ac2Record *o) {
        if (const auto ps = _particles.find(o); ps != _particles.end())
            return ps->second.settings().depth;
        if (const auto ov = _overrides.find(o); ov != _overrides.end())
            return ov->second.depth;
        return o->f("depth") != 0 ? o->f("depth") : o->f("z");
    };
    std::stable_sort(pictures.begin(), pictures.end(),
                     [&](const Ac2Record *a, const Ac2Record *b) { return depth(a) < depth(b); });
    // With 3D, the pictures go in layers between the characters: the game
    // draws its objects back to front by depth, a character's being its z
    // (0x435d90, 0x4104b0), the object list's order where they're equal.
    // Layer k is drawn after the k-th character in that order (0: the
    // background, behind them all); the labels go into the overlay.
    const bool layered = _renderer != nullptr;
    const auto layersStart = std::chrono::steady_clock::now();
    if (layered) {
        auto objectIndex = [&](const Ac2Record *r) {
            const auto it = std::find(objects.begin(), objects.end(), r);
            return size_t(it - objects.begin());
        };
        // Footprints are objects too, added after the scene's own.
        _characterOrder.clear();
        if (charactersShown())
            for (size_t i = 0; i < _characters.size(); i++) {
                _characterOrder.push_back({i, -1});
                for (size_t k = 0; k < _characters[i].footprints.size(); k++)
                    _characterOrder.push_back({i, int(k)});
            }
        auto slotZ = [&](const ModelSlot &s) {
            const Character &c = _characters[s.character];
            // A character's depth field when it isn't 0, else where it
            // stands (0x4104b0, every object's: Antoinette at 11000 in front
            // of the dining car's tables in 7A).
            if (s.footprint >= 0)
                return c.footprints[size_t(s.footprint)].z;
            const float depth = c.record ? c.record->f("depth") : 0.0f;
            return depth != 0 ? depth : c.z;
        };
        auto slotIndex = [&](const ModelSlot &s) {
            const Character &c = _characters[s.character];
            return s.footprint < 0 ? objectIndex(c.record) : objects.size() + c.footprints[size_t(s.footprint)].serial;
        };
        std::stable_sort(_characterOrder.begin(), _characterOrder.end(), [&](const ModelSlot &a, const ModelSlot &b) {
            const float za = slotZ(a);
            const float zb = slotZ(b);
            return za != zb ? za < zb : slotIndex(a) < slotIndex(b);
        });
        auto layerOf = [&](const Ac2Record *o) {
            const float d = depth(o);
            const size_t at = objectIndex(o);
            size_t n = 0;
            for (const ModelSlot &s : _characterOrder) {
                const float z = slotZ(s);
                n += z < d || (z == d && slotIndex(s) < at);
            }
            return n;
        };
        // A picture drawn after a character takes the shadows off under
        // it, where its alpha isn't 0 (0x437b00): unless it's a fog or
        // cloud layer (SPRT scroll set) or at depth 10000 or more. Particles
        // don't (0x42d470).
        auto resetsShadows = [&](const Ac2Record *o) {
            if (_particles.count(o) || depth(o) >= 10000)
                return false;
            const Ac2Record *sprite = o->part("SPRT");
            return !sprite || sprite->i("scroll") == 0;
        };
        const bool shadows = _options.get(GameOptions::kShadows);
        for (size_t k = _characterOrder.size() + 1; k < _layers.size(); k++)
            dropLayer(_layers[k]);
        _layers.resize(_characterOrder.size() + 1);
        std::vector<size_t> layerOfPicture;
        for (const Ac2Record *o : pictures)
            layerOfPicture.push_back(layerOf(o));
        // A picture that scrolls and a video loop are drawn by themselves,
        // a quad of their own, so the rest of their layer needn't be put
        // together again as they move (the clouds, the countryside going
        // by): unless moved by a puzzle.
        auto alone = [&](const Ac2Record *o) { return (_scrolls.count(o) || _loops.count(o)) && !_overrides.count(o); };
        gfx::TextureParams params;
        params.clamp = true;
        params.mipmaps = false;
        for (size_t k = 0; k < _layers.size(); k++) {
            PictureLayer &layer = _layers[k];
            std::vector<const Ac2Record *> in;
            bool allReset = true;
            bool anyAlone = false;
            // Particles are drawn by the GPU as the game's point sprites,
            // between the pictures put together, as they move every frame.
            auto separate = [&](const Ac2Record *o) { return alone(o) || _particles.count(o) != 0; };
            for (size_t p = 0; p < pictures.size(); p++)
                if (layerOfPicture[p] == k) {
                    in.push_back(pictures[p]);
                    allReset &= resetsShadows(pictures[p]);
                    anyAlone |= separate(pictures[p]);
                }
            // A character's breath, drawn just after it (0x425e48); none
            // with "Particle Effects" off.
            const ParticleSystem *breath = nullptr;
            if (k > 0 && _characterOrder[k - 1].footprint < 0 && _options.get(GameOptions::kRain))
                breath = _characters[_characterOrder[k - 1].character].breath.get();
            allReset &= breath == nullptr; // particles take no shadows off
            if (in.empty() && !breath && k > 0) {
                dropLayer(layer);
                continue;
            }
            // Where those drawn alone are now: as place() puts them.
            auto placeAlone = [&](PictureLayer::Part &part) {
                const Ac2Record *o = part.alone;
                SpriteTransform &t = part.at;
                t = SpriteTransform();
                t.x = o->f("x"), t.y = o->f("y");
                if (o->field("spriteScaleX"))
                    t.scaleX = o->f("spriteScaleX"), t.scaleY = o->f("spriteScaleY");
                if (const Ac2Record *p = o->part(o->tag == "BINK" ? "BNKX" : "SPRT"))
                    t.roll = p->f("roll");
                part.twice = false;
                if (const auto sc = _scrolls.find(o); sc != _scrolls.end()) {
                    // Where it has got to, and again a width or height
                    // behind, so the two cover the way (0x437db6).
                    t.x = sc->second.x, t.y = sc->second.y;
                    const int way = o->part("SPRT")->i("scroll");
                    part.at2 = t;
                    part.at2.x += way == 1 ? float(part.w) : way == 2 ? -float(part.w) : 0.0f;
                    part.at2.y += way == 3 ? float(part.h) : way == 4 ? -float(part.h) : 0.0f;
                    part.twice = true;
                }
            };
            // Put together again only when what's in it has changed: which
            // pictures; always with the options screen's sliders. Most draws (the mouse moving, a line said, a cloud
            // moving on) change none of them.
            std::string key;
            {
                auto put = [&key](const auto &v) { key.append(reinterpret_cast<const char *>(&v), sizeof v); };
                put(_layersMade), put(brightnessLevel()), put(shadows);
                put(breath);
                const bool remade = _optionsMenu != nullptr;
                for (const Ac2Record *o : in) {
                    put(o);
                    if (const auto ov = _overrides.find(o); ov != _overrides.end())
                        put(ov->second.x), put(ov->second.y), put(ov->second.roll), put(ov->second.cx),
                            put(ov->second.cy), put(ov->second.tint);
                    if (!alone(o)) {
                        if (const auto lp = _loops.find(o); lp != _loops.end())
                            put(lp->second.next);
                        if (const auto sc = _scrolls.find(o); sc != _scrolls.end())
                            put(sc->second.x), put(sc->second.y);
                    }
                }
                if (remade)
                    key.clear();
            }
            if (!key.empty() && key == layer.key) {
                for (PictureLayer::Part &part : layer.parts)
                    if (part.alone) {
                        aloneTexture(*part.alone, imageOf(part.alone));
                        placeAlone(part);
                    }
                continue;
            }
            dropLayer(layer);
            layer.key = std::move(key);
            auto takeResets = [&] {
                layer.resets.assign(size_t(kScreenW) * kScreenH, 0);
                for (size_t i = 0; i < layer.resets.size(); i++)
                    layer.resets[i] = _ctx.screen.pixels[i * 4 + 3] != 0;
            };
            const bool resets = shadows && k > 0;
            if (resets && (!allReset || anyAlone)) {
                // Those pictures alone first, for where they reset.
                std::fill(_ctx.screen.pixels.begin(), _ctx.screen.pixels.end(), 0);
                for (const Ac2Record *o : in)
                    if (resetsShadows(o))
                        place(o, true);
                takeResets();
            }
            // The rest in runs between those drawn alone, each put together
            // over the whole frame: the background's first opaque, the
            // others over what's behind.
            size_t next = 0;
            bool first = true;
            if (breath) {
                PictureLayer::Part part;
                part.particles = breath;
                layer.parts.push_back(part);
            }
            while (next < in.size()) {
                if (const auto ps = _particles.find(in[next]); ps != _particles.end()) {
                    PictureLayer::Part part;
                    part.particles = &ps->second;
                    layer.parts.push_back(part);
                    next++;
                    first = false;
                    continue;
                }
                if (alone(in[next])) {
                    PictureLayer::Part part;
                    part.alone = in[next++];
                    const image::Image &img = imageOf(part.alone);
                    part.w = img.w, part.h = img.h;
                    aloneTexture(*part.alone, img);
                    placeAlone(part);
                    layer.parts.push_back(part);
                    first = false;
                    continue;
                }
                const bool opaque = k == 0 && first;
                if (opaque)
                    clear(0, 0, 0);
                else
                    std::fill(_ctx.screen.pixels.begin(), _ctx.screen.pixels.end(), 0);
                for (; next < in.size() && !separate(in[next]); next++)
                    place(in[next], !opaque);
                if (resets && allReset && !anyAlone)
                    takeResets();
                applyBrightness();
                PictureLayer::Part part;
                _renderer->createTexture(kScreenW, kScreenH, _ctx.screen.pixels.data(), params, part.texture);
                layer.parts.push_back(part);
                first = false;
            }
            // Where it takes the shadows off, for the GPU's stencil: alpha
            // 255 there, else 0.
            if (!layer.resets.empty() && _renderer->hasStencil()) {
                std::vector<uint8_t> mask(layer.resets.size() * 4, 0);
                for (size_t i = 0; i < layer.resets.size(); i++)
                    mask[i * 4 + 3] = layer.resets[i] ? 255 : 0;
                gfx::TextureParams nearest = params;
                nearest.nearest = true;
                _renderer->createTexture(kScreenW, kScreenH, mask.data(), nearest, layer.resetMask);
            }
        }
        std::fill(_ctx.screen.pixels.begin(), _ctx.screen.pixels.end(), 0);
    } else {
        clear(0, 0, 0);
        for (const Ac2Record *o : pictures)
            place(o, false);
        applyBrightness();
    }
    g_perf.layers += FramePerf::ms(layersStart);
    if (_showGrid)
        drawGrid();
    if (_hotspots)
        for (const Ac2Record *o : objects) {
            const int l = o->i("left");
            const int t = o->i("top");
            const int r = o->i("right");
            const int b = o->i("bottom");
            if (o->tag != "SPOT" || r <= l || b <= t || !objectShown(*o))
                continue;
            // Its outline where it has one (listD), else its rectangle.
            const Ac2Value *outline = o->field("listD");
            if (outline && outline->numbers.size() >= 3) {
                const auto &pts = outline->numbers;
                for (size_t i = 0; i < pts.size(); i++)
                    line(int(pts[i].first), int(pts[i].second), int(pts[(i + 1) % pts.size()].first),
                         int(pts[(i + 1) % pts.size()].second), {255, 220, 60});
            } else {
                frame(l, t, r - l, b - t, {255, 220, 60});
            }
            const Ac2Record *hot = o->part("HOTS");
            std::string name = o->text("name");
            if (name.empty() && hot)
                name = hot->text("name");
            label(l + 2, t + 2, name.empty() ? shorten(o->text("script"), 60) : name, {255, 220, 60});
        }
    if (_selected >= 0 && size_t(_selected) < objects.size()) {
        const Ac2Record *o = objects[size_t(_selected)];
        const image::Image &img = imageOf(o);
        if (img.w > 0)
            frame(int(o->f("x")), int(o->f("y")), img.w, img.h, {90, 255, 120});
        // Its fields, as the dump shows them.
        int y = kScreenH - 2 * font::kLineHeight - 6;
        std::vector<std::string> lines;
        std::string text;
        for (const auto &[name, v] : o->fields) {
            const std::string s = v.str();
            if (s == "0" || s == "\"\"" || s == "[]")
                continue;
            const std::string item = name + "=" + shorten(s, 60) + "  ";
            if (font::length(text + item) * font::kAdvance > size_t(kScreenW - 8))
                lines.push_back(text), text.clear();
            text += item;
        }
        for (const Ac2Record &p : o->parts) {
            text += p.tag + "{";
            for (const auto &[name, v] : p.fields)
                if (v.str() != "0" && v.str() != "\"\"" && v.str() != "[]")
                    text += name + "=" + shorten(v.str(), 40) + " ";
            text += "} ";
        }
        lines.push_back(text);
        y -= int(lines.size()) * font::kLineHeight;
        for (const std::string &l : lines) {
            label(4, y, l, {150, 255, 170});
            y += font::kLineHeight;
        }
    }
    if (_info) {
        std::vector<std::string> lines;
        lines.push_back((_names.empty() ? std::string("No scenes") : _names[size_t(_index)]) + "  (" +
                        std::to_string(_index + 1) + "/" + std::to_string(_names.size()) + ")" +
                        (_base.empty() ? "" : "  over " + _base));
        if (!_error.empty())
            lines.push_back(_error);
        std::map<std::string, int> tags;
        for (const Ac2Record *o : objects)
            tags[o->tag]++;
        std::string counts = std::to_string(objects.size()) + " objects:";
        for (const auto &[t, n] : tags)
            counts += " " + std::to_string(n) + " " + t;
        lines.push_back(counts);
        if (!_grid.empty()) {
            std::string g = "walk grid: cells of " + std::to_string(_grid.cellSize());
            if (_hover.valid())
                g += ", under the mouse (" + std::to_string(_hover.a) + ", " + std::to_string(_hover.b) +
                     ") = " + std::to_string(_grid.at(_hover.a, _hover.b));
            if (!_path.empty())
                g += ", path of " + std::to_string(_path.size()) + " points";
            lines.push_back(g);
        }
        if (!_scriptLog.empty()) {
            std::string tags;
            for (const auto &[t, v] : _state.tags)
                tags += " " + t + "=" + std::to_string(v);
            std::string items;
            for (const GameState::Item &i : _state.items)
                items += " " + i.name;
            lines.push_back("script: tags" + (tags.empty() ? std::string(" none") : tags) + "; items" +
                            (items.empty() ? std::string(" none") : items));
            for (const std::string &l : _scriptLog)
                lines.push_back(l);
        }
        lines.push_back(std::string("train ") + (_state.values["moving"] ? "moving" : "stopped") + ", weather " +
                        (_state.values["weather"] ? "on" : "off") + (_state.values["time"] ? ", night" : "") +
                        (_state.act.empty() ? "" : ", act " + _state.act));
        if (const Ac2Record *cam = _scene.camera()) {
            char buf[200];
            std::snprintf(buf, sizeof buf,
                          "camera about (%g, %g, %g), turned (%g, %g, %g), back (%g, %g, %g), fov %.1f deg",
                          cam->f("x"), cam->f("y"), cam->f("z"), cam->f("pitch"), cam->f("yaw"), cam->f("roll"),
                          cam->f("offsetX"), cam->f("offsetY"), cam->f("offsetZ"), cam->f("fov") * 57.29578f);
            lines.push_back(buf);
        }
        for (const Ac2Record &r : _scene.records)
            if (r.tag == "MUSC" || r.tag == "EVNT")
                if (const Ac2Value *v = r.fields.empty() ? nullptr : &r.fields[0].second; v && !v->pairs.empty())
                    lines.push_back(r.tag + " " + shorten(v->str(), 150));
        for (const Ac2Record *o : objects)
            if (o->tag == "NPCX" || o->tag == "PCXX") {
                char buf[200];
                std::string anim;
                // Where it is now, else where the scene puts it.
                float x = o->f("x");
                float y = o->f("y");
                float z = o->f("z");
                float rotY = o->f("rotY");
                for (const Character &c : _characters)
                    if (c.record == o && c.actor) {
                        x = c.x, y = c.y, z = c.z, rotY = c.rotY;
                        for (const Animator::Slot &sl : c.actor->animator().slots())
                            if (sl.state != Animator::kStopped)
                                anim += (anim.empty() ? ", playing " : " ") + sl.name;
                    }
                std::snprintf(buf, sizeof buf, "%s %s at (%g, %g, %g), facing %g%s", o->tag.c_str(),
                              o->text("file").c_str(), x, y, z, rotY, anim.c_str());
                lines.push_back(buf);
            }
        int y = 2;
        for (const std::string &l : lines) {
            label(4, y, shorten(l, 168), l == _error ? Color{255, 120, 120} : Color{230, 230, 230});
            y += font::kLineHeight;
        }
    }
    // The bar and the inventory window, then a card held up (0x415227,
    // 0x418040): under the dialogue box.
    if (_drawing && !_load.screen && !_movie) {
        drawInterface();
        // The letterbox over the bar, under a card (0x416ff0, at
        // 0x4155ef): black, from just above the top and up from the
        // bottom.
        if (_letterbox.mode != 0) {
            const int h = int(_letterbox.height);
            fill(0, 0, kScreenW, h, Color{0, 0, 0}, 255);
            fill(0, kScreenH - h, kScreenW, h, Color{0, 0, 0}, 255);
        }
        drawCards();
    }
    if (_dialogue->boxShown() && _ctx.fonts) {
        DialogueBoxPictures pics;
        pics.window =
            &picture(_dialogue->window() == 1 ? "interface/dialoguewin02.png" : "interface/dialoguewin01.png");
        pics.bullet = &picture("interface/dialoguewin04.png");
        pics.bulletOn = &picture("interface/dialoguewin05.png");
        // fonts.txt's colours by their place: 6 choices, 7 the one under
        // the mouse, 8 Antoinette, 9 and 10 the others.
        static const char *const kColours[] = {
            "dialogue text when choosing", "highlighted dialogue text when choosing", "dialogue for main character",
            "dialogue text for second character in conversation", "dialogue text for third character in conversation"};
        const int c = std::clamp(_dialogue->colour() - 6, 0, 4);
        std::vector<std::string> choices;
        std::vector<const image::Image *> choicePictures;
        for (const auto &ch : _dialogue->choices()) {
            choices.push_back(ch.text);
            choicePictures.push_back(ch.picture.empty() ? nullptr : &picture(normalizeArchiveName(ch.picture)));
        }
        const DialogueLine *said = _dialogue->lineShown() ? _dialogue->line() : nullptr;
        // "Text Captions" off: no box or line unless choosing, or the radio
        // (0x43cf90).
        const bool radio = said && (said->name == "dia_barnabyham" || said->name == "hamradio");
        if (!_options.get(GameOptions::kCaptions) && !_dialogue->choosing() && !radio)
            said = nullptr;
        const auto areas = drawDialogueBox(_ctx.screen, *_ctx.fonts, pics, _dialogue->boxTop(),
                                           said ? said->text : std::string(), _ctx.fonts->colour(kColours[c]),
                                           _dialogue->choosing() ? choices : std::vector<std::string>(),
                                           _dialogue->highlighted(), choicePictures);
        std::vector<DialogueManager::ChoiceArea> rows;
        for (const auto &a : areas)
            rows.push_back({a.x, a.y, a.w, a.h, a.choice, a.picture});
        _dialogue->setChoiceAreas(std::move(rows));
    }
    if (_slots && !_load.screen && !_movie)
        drawSlots();
    if (_optionsMenu && !_load.screen && !_movie)
        drawOptions();
    if (_box && !_load.screen && !_movie)
        drawPuzzleBox();
    if (_messageTime > 0 && !_yesNo && !_load.screen && !_movie)
        drawMessage();
    if (_yesNo && _ctx.fonts)
        drawYesNo();
    if (_load.screen) {
        // The location's picture, the wheel turning at its foot (0x422ff0).
        const image::Image &back = picture("interface/loading/loading_" + [&] {
            const std::string &l = _load.location;
            const size_t dot = l.find('.');
            if (dot == std::string::npos)
                return l;
            return l.find("exterior") != std::string::npos || l.find("sirkeci") != std::string::npos ? l.substr(dot + 1)
                                                                                                     : l.substr(0, dot);
        }() + ".jpg");
        fill(0, 0, kScreenW, kScreenH, Color{0, 0, 0});
        image::blit(_ctx.screen, back.w ? back : picture("interface/loading/loading_generic.jpg"), 0, 0);
        char wheel[48];
        std::snprintf(wheel, sizeof wheel, "interface/loading/wheels%02d.png", _load.wheel % 7 + 1);
        const image::Image &w = picture(wheel);
        image::blit(_ctx.screen, w, (kScreenW - w.w) / 2, 550);
        if (_fade > 0)
            fill(0, 0, kScreenW, kScreenH, Color{0, 0, 0}, uint8_t(_fade * 255));
    } else if (_movie || !_sceneShown) {
        // The movie over everything, else black: no scene is shown.
        fill(0, 0, kScreenW, kScreenH, Color{0, 0, 0});
        if (_movie && _movie->frame().w) {
            const image::Image &f = _movie->frame();
            image::blit(_ctx.screen, f, (kScreenW - f.w) / 2, (kScreenH - f.h) / 2);
            if (_options.get(GameOptions::kCaptions))
                drawSubtitles(_movieSubtitles, _movie->seconds());
        }
    } else if (_fade > 0) {
        fill(0, 0, kScreenW, kScreenH, Color{0, 0, 0}, uint8_t(_fade * 255));
    }
    help(std::string("Left/Right: scene  Up/Down: object  H: hotspots  I (F9 in a game): info  M: moving  W: weather  "
                     "T: night  G: grid  N: new game  click: walk") +
         (_renderer ? "  C: characters" : "  (no 3D: no characters)"));
    // The game's cursor over everything; the system's hidden in a game
    // (0x413f1f), and while the game's shows.
    drawCursor();
    if (const bool system = !_playing && !cursorShown(); system != _systemCursor)
        _ctx.sys.setMouseCursorVisible(_systemCursor = system);
    if (!_renderer)
        present();
    _ctx.setTitle("Scenes: " + (_names.empty() ? std::string() : _names[size_t(_index)]));
}

void ScenePage::update(uint64_t nowMs) {
    struct Timed {
        std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
        ~Timed() {
            g_perf.total += FramePerf::ms(t);
            g_perf.frames++;
        }
    } timed;
    _frame++;
    // Drawn once, at the end, whatever asks for it on the way.
    bool redraw = false;
    if (_startMs == 0)
        _startMs = nowMs;
    const float time = float(nowMs - _startMs) / 1000.0f;
    // Particles move on by the time since the last frame (at most a tenth
    // of a second, so a pause doesn't empty them).
    const float dt = _lastMs ? std::min(0.1f, float(nowMs - _lastMs) / 1000.0f) : 0.0f;
    _lastMs = nowMs;
    _time = time;
    if (_movie) {
        // Over when its last frame's time has gone by (0x40c490).
        if (_movie->update(nowMs))
            redraw = true;
        if (_movie->done(nowMs))
            endMovie();
    }
    if (_load.state)
        updateLoad();
    if (_slots)
        _slots->sinceClick += dt;
    // The message's time and the caret's blink go on in every scene
    // (0x414400).
    if (_messageTime > 0) {
        _messageTime = std::max(0.0f, _messageTime - dt);
        if (_messageTime == 0)
            redraw = true;
    }
    if ((_caretTime -= dt) <= 0) {
        _caretTime = 0.33f;
        _caretOn = !_caretOn;
        if (_slots && _slots->save)
            redraw = true;
    }
    if (puzzleActive())
        puzzleUpdate(dt);
    if (_charactersDue && nowMs >= _charactersAt + 400) {
        _charactersDue = false;
        loadCharacters();
        redraw = true;
    }
    if (_playing)
        _story->update(dt);
    if (_letterbox.mode >= 2) {
        // 250 pixels a second, to 70 or to nothing (0x416f40).
        Letterbox &l = _letterbox;
        const bool in = l.mode == 2;
        l.height = in ? l.height + dt * 250 : l.height - dt * 250;
        if (in ? l.height >= 70 : l.height <= 0) {
            l.height = in ? 70.0f : 0.0f;
            l.mode = in ? 1 : 0;
            if (ScriptRunner *r = l.runner) {
                l.runner = nullptr;
                r->finished("letterbox");
            }
        }
        redraw = true;
    }
    if (_fade != _fadeTarget) {
        // 8 of 255 every 0.01 s (0x40c33b, 0x4158e0): about a third of a
        // second. The game takes at most a step a frame, so it's slower
        // where frames are: 0.27 to 0.47 s in a recording of it.
        const float step = dt * 8 / 255 / 0.01f;
        _fade = _fadeTarget > _fade ? std::min(_fadeTarget, _fade + step) : std::max(_fadeTarget, _fade - step);
        if (_fade == _fadeTarget && _fadeRunner) {
            ScriptRunner *r = _fadeRunner;
            _fadeRunner = nullptr;
            r->finished("fade");
        }
        redraw = true;
    }
    if (_soundRunner && !(_ctx.mixer && _ctx.mixer->isPlaying(_soundWaited))) {
        ScriptRunner *r = _soundRunner;
        _soundRunner = nullptr;
        r->finished("sound");
    }
    if (interfaceUpdate(dt) || (_ui.open && _ui.page == 0 && !_state.fresh.empty() && _caretTime == 0.33f))
        redraw = true;
    _runner->update(dt);
    // The clicked character's routine goes on once the main script is
    // over (0x409cab).
    if (_resumeAfter && _runner->idle()) {
        for (Character &c : _characters)
            if (c.record == _resumeAfter)
                c.paused = false;
        _resumeAfter = nullptr;
    }
    // Every frame (0x416d8f): a Povent left running after the talk is
    // still run, and the talk then told done.
    const bool talking = _dialogue->active();
    _dialogue->update(dt);
    if (talking)
        redraw = true;                             // the box moves, lines change
    bool changed = _drawing && advanceLoops(time); // nothing to decode for a tool not drawing
    for (Character &c : _characters) {
        // Its routine, unless paused; its animations and any turn.
        if (c.routine && !c.paused)
            c.routine->update(dt);
        float heading = c.rotY * 57.29578f;
        c.actor->setConversation(_dialogue->active());
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
            c.leg = -1, c.turningFirst = false;
            placeCharacter(c);
            continue;
        }
        stepWalk(c, dt);
        placeCharacter(c);
    }
    for (auto &[o, system] : _particles)
        if (_drawing && _options.get(GameOptions::kRain) && objectShown(*o)) {
            system.update(dt);
            changed = true;
        }
    changed |= scrollPictures(dt);
    updateHotspots();
    changed |= std::exchange(_cursorMoved, false);
    if (changed || redraw)
        draw();
    // A page not drawing (a tool's) makes no 3D frame either: its
    // characters still move, and are picked by their bones.
    if (!_renderer || !_drawing) {
        if (_drawing)
            Page::update(nowMs);
        return;
    }
    for (gfx::MeshId m : _frameMeshes)
        _renderer->destroyMesh(m);
    _frameMeshes.clear();
    // The pictures in their layers, each a quad over the whole frame,
    // between the characters (draw()).
    _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    if (!_quad) {
        std::vector<gfx::Vertex> v(4);
        const float corners[4][4] = {{-1, 1, 0, 0}, {1, 1, 1, 0}, {1, -1, 1, 1}, {-1, -1, 0, 1}};
        for (size_t i = 0; i < 4; i++) {
            v[i] = {};
            v[i].pos[0] = corners[i][0], v[i].pos[1] = corners[i][1], v[i].pos[2] = 0.5f;
            v[i].uv[0] = corners[i][2], v[i].uv[1] = corners[i][3];
            v[i].color[0] = v[i].color[1] = v[i].color[2] = v[i].color[3] = 255;
        }
        _renderer->createMesh(v, {0, 2, 1, 0, 3, 2}, _quad);
    }
    auto drawLayer = [&](size_t k) {
        if (k >= _layers.size() || !_quad)
            return;
        for (size_t i = 0; i < _layers[k].parts.size(); i++) {
            const PictureLayer::Part &part = _layers[k].parts[i];
            gfx::DrawCall call;
            call.mesh = _quad;
            call.indexCount = 6;
            if (k > 0 || i > 0) {
                call.state.src = gfx::Blend::kSrcAlpha;
                call.state.dst = gfx::Blend::kOneMinusSrcAlpha;
            }
            call.state.depthTest = false;
            call.state.depthWrite = false;
            call.state.cull = gfx::Cull::kNone;
            if (part.particles) {
                drawPointSprites(*part.particles);
                continue;
            }
            if (!part.alone) {
                call.texture = part.texture;
                _renderer->setView(gfx::identity(), {0, 0, 0});
                _renderer->draw(call);
                continue;
            }
            // The quad over the whole frame taken to where the picture's
            // corners go (twice for one that scrolls).
            const auto t = _aloneTextures.find(part.alone);
            if (t == _aloneTextures.end() || !t->second.texture)
                continue;
            call.texture = t->second.texture;
            for (int n = 0; n < (part.twice ? 2 : 1); n++) {
                const SpriteTransform &at = n ? part.at2 : part.at;
                float x0;
                float y0;
                float xu;
                float yu;
                float xv;
                float yv;
                at.apply(0, 0, x0, y0);
                at.apply(float(part.w), 0, xu, yu);
                at.apply(0, float(part.h), xv, yv);
                // The quad's (X, Y) is the picture's ((X + 1) / 2, (1 - Y) / 2)
                // of the way across and down; then pixels to clip space.
                const float sx = 2.0f / kScreenW;
                const float sy = -2.0f / kScreenH;
                gfx::Mat4 m = gfx::identity();
                m[0] = sx * (xu - x0) / 2, m[1] = sy * (yu - y0) / 2;
                m[4] = -sx * (xv - x0) / 2, m[5] = -sy * (yv - y0) / 2;
                m[12] = sx * (x0 + (xu - x0) / 2 + (xv - x0) / 2) - 1;
                m[13] = sy * (y0 + (yu - y0) / 2 + (yv - y0) / 2) + 1;
                _renderer->setView(m, {0, 0, 0});
                _renderer->draw(call);
            }
        }
    };
    drawLayer(0);
    for (gfx::TextureId t : _frameTextures)
        _renderer->destroyTexture(t);
    _frameTextures.clear();
    size_t layer = 1;
    const auto charactersStart = std::chrono::steady_clock::now();
    // The characters, through the scene's camera, in depth order.
    if (const Ac2Record *camr = _scene.camera(); camr && charactersShown()) {
        const SceneCamera cam = SceneCamera::fromRecord(*camr);
        // Their poses, and their props'.
        std::vector<Pose> poses(_characters.size());
        std::vector<std::vector<Pose>> propPoses(_characters.size());
        for (size_t i = 0; i < _characters.size(); i++) {
            const Character &c = _characters[i];
            c.actor->animator().pose(poses[i]);
            for (const auto &[name, prop] : c.props) {
                propPoses[i].emplace_back();
                c.actor->animator().poseFor(prop->model(), propPoses[i].back());
            }
        }
        // The shadows (docs/engines/awe/games/motoe/systems/shadows.md): the game's stencil,
        // on the GPU as the game's is; made on the CPU as the frame is
        // drawn where the device has no stencil buffer.
        const bool shadows = _options.get(GameOptions::kShadows);
        const bool gpuStencil = shadows && _renderer->hasStencil();
        if (shadows && !gpuStencil) {
            if (!_stencil)
                _stencil = std::make_unique<ShadowStencil>(kScreenW, kScreenH);
            _stencil->clear();
        }
        const gfx::Mat4 viewProjection = cam.viewProjection();
        _renderer->clearDepth();
        // Brightness: the game's gamma ramp is the whole screen's, the
        // characters' too (slope 32g + 97 over 257).
        const float bright = float(32 * brightnessLevel() + 97) / 257.0f;
        std::vector<ModelSlot> order = _characterOrder;
        size_t shownCharacters = 0;
        bool stale = false;
        for (const ModelSlot &s : order) {
            stale |= s.character >= _characters.size() ||
                     (s.footprint >= 0 && size_t(s.footprint) >= _characters[s.character].footprints.size());
            shownCharacters += s.footprint < 0;
        }
        if (stale || shownCharacters != _characters.size()) {
            order.clear();
            for (size_t i = 0; i < _characters.size(); i++)
                order.push_back({i, -1});
        }
        // Lit by the scene's lights, each with its own material, which its
        // props share (docs/engines/awe/games/motoe/systems/lighting.md).
        SceneLighting lighting = sceneLighting(_scene, _state, _lightConditions);
        {
            const gfx::Vec3 e = cam.eye();
            lighting.eye = {e[0], e[1], e[2]};
        }
        // Their skinning and lighting, the most of the work, all at once on
        // the machine's cores; drawn after in their order.
        std::vector<bool> skinnedNow(_characters.size(), false);
        {
            const auto skinStart = std::chrono::steady_clock::now();
            struct Job {
                GpuModel *model;
                const Pose *pose;
                const gfx::Mat4 *placement;
                Material material;
                SkinnedModel *out;
            };
            std::vector<Job> jobs;
            for (size_t i = 0; i < _characters.size(); i++) {
                Character &c = _characters[i];
                if (c.record && !objectShown(*c.record))
                    continue;
                skinnedNow[i] = true;
                const Material material = c.record ? materialOf(*c.record) : Material();
                jobs.push_back({c.model.get(), &poses[i], &c.placement, material, &c.skinned});
                c.propsSkinned.resize(c.props.size());
                for (size_t k = 0; k < c.props.size(); k++)
                    jobs.push_back(
                        {c.props[k].second.get(), &propPoses[i][k], &c.placement, material, &c.propsSkinned[k]});
            }
            parallelFor(jobs.size(), [&](size_t j) {
                const Job &job = jobs[j];
                job.model->skinAndLight(*job.pose, *job.placement, false, &lighting, job.material, *job.out);
            });
            // Onto the GPU, all before any is drawn: sent together.
            for (const Job &job : jobs)
                job.model->upload(*job.out);
            g_perf.skin += FramePerf::ms(skinStart);
        }
        for (const ModelSlot &slot : order) {
            const size_t i = slot.character;
            const Character &c = _characters[i];
            // A character is drawn, as any object, only while shown: not
            // hidden and its condition holding (0x425e00 asks slot 0x50
            // first; 0x40e900 tests the condition every frame). Greta
            // waits in the market until "tag gretafollow".
            const bool drawn = !c.record || objectShown(*c.record);
            if (slot.footprint >= 0) {
                _renderer->setView(viewProjection, cam.eye());
                drawFootprint(c.footprints[size_t(slot.footprint)], lighting, bright);
            } else if (shadows && !gpuStencil && drawn) {
                // 0x428836: its pixels' stencil 2 for the player, 0 for the
                // others.
                const uint8_t ref = c.player ? 2 : 0;
                stencilModel(viewProjection, *c.model, poses[i], c.placement, c.shadow, ref, *_stencil);
                for (size_t k = 0; k < c.props.size(); k++)
                    stencilModel(viewProjection, *c.props[k].second, propPoses[i][k], c.placement, c.shadow, ref,
                                 *_stencil);
            }
            if (slot.footprint < 0 && drawn) {
                _renderer->setView(viewProjection, cam.eye());
                const Material material = c.record ? materialOf(*c.record) : Material();
                // 0x428836: its pixels' stencil 2 for the player, 0 for the
                // others; each mesh's shadow first, if it casts one.
                StencilShadow stencil;
                stencil.scene = &_scene;
                stencil.cast = c.shadow;
                stencil.ref = c.player ? 2 : 0;
                const StencilShadow *withShadow = gpuStencil ? &stencil : nullptr;
                c.model->draw(poses[i], c.placement, false, _frameMeshes, bright, &lighting, material, withShadow,
                              skinnedNow[i] ? &c.skinned : nullptr);
                for (size_t k = 0; k < c.props.size(); k++)
                    c.props[k].second->draw(propPoses[i][k], c.placement, false, _frameMeshes, bright, &lighting,
                                            material, withShadow,
                                            skinnedNow[i] && k < c.propsSkinned.size() ? &c.propsSkinned[k] : nullptr);
            }
            // The pictures after it, taking the shadows off under them.
            if (layer < _layers.size()) {
                drawLayer(layer);
                if (gpuStencil)
                    stencilQuad(_layers[layer].resetMask, gfx::Renderer3D::StencilMode::kMark, 0, Color{0, 0, 0}, 0);
                else if (shadows)
                    _stencil->reset(_layers[layer].resets);
                layer++;
            }
        }
        if (gpuStencil) {
            // Where the stencil is 1, darkened (0x402ba0: black at alpha
            // 63); then the player washed toward white with distance, in
            // the snow (the camera's whiteoutStart and whiteoutEnd, 0x402e20).
            stencilQuad(0, gfx::Renderer3D::StencilMode::kInside, 1, Color{0, 0, 0}, 63);
            if (const int alpha = whiteOut(); alpha > 0)
                stencilQuad(0, gfx::Renderer3D::StencilMode::kInside, 2, Color{255, 255, 255}, uint8_t(alpha));
        } else if (shadows) {
            // Where the stencil is 1, darkened (0x402ba0: black at alpha
            // 63); then the player washed toward white with distance, in
            // the snow (the camera's whiteoutStart and whiteoutEnd, 0x402e20).
            if (!_shadowMask)
                _shadowMask = std::make_unique<CoverageMask>(kScreenW, kScreenH);
            _stencil->select(1, *_shadowMask);
            _renderer->setView(gfx::identity(), cam.eye());
            drawMask(*_shadowMask, Color{0, 0, 0}, 63);
            drawWhiteOut();
        }
    }
    g_perf.characters += FramePerf::ms(charactersStart);
    for (; layer < _layers.size(); layer++)
        drawLayer(layer);
    prepareScreenshot(_renderer);
    if (_grabDue)
        _renderer->captureNextFrame();
    const auto presentStart = std::chrono::steady_clock::now();
    _renderer->endFrame(&_ctx.screen);
    g_perf.present += FramePerf::ms(presentStart);
    g_perf.report(_names.empty() ? std::string() : _names[size_t(_index)]);
    if (_grabDue) {
        // The view for a save's thumbnail, the 2D over the 3D.
        image::Image img;
        img.w = kScreenW, img.h = kScreenH;
        if (_renderer->takeCapture(img.rgba)) {
            const auto &o = _ctx.screen.pixels;
            if (o.size() == img.rgba.size())
                for (size_t i = 0; i < img.rgba.size(); i += 4) {
                    const int a = o[i + 3];
                    for (size_t c = 0; c < 3; c++)
                        img.rgba[i + c] = uint8_t((o[i + c] * a + img.rgba[i + c] * (255 - a)) / 255);
                }
            _grab = std::move(img);
            _grabDue = false;
        }
    }
    finishScreenshot(_renderer, &_ctx.screen);
}

void ScenePage::stencilModel(const gfx::Mat4 &viewProjection, const GpuModel &model, const Pose &pose,
                             const gfx::Mat4 &placement, bool shadow, uint8_t ref, ShadowStencil &stencil) const {
    // 0x428500: each mesh with materials (those without are skipped,
    // shadow and all), its shadow first if the character casts one, then
    // its body. A shadow sets the stencil to 1 where it passes the depth
    // test, writing no depth; a body sets it to `ref`, and its depth.
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 3>> screen;
    std::vector<uint8_t> ok;
    auto toScreen = [&](const float p[3], std::array<float, 3> &out) {
        const gfx::Mat4 &m = viewProjection;
        float clip[4];
        for (size_t row = 0; row < 4; row++)
            clip[row] = m[row] * p[0] + m[4 + row] * p[1] + m[8 + row] * p[2] + m[12 + row];
        if (clip[3] <= 0)
            return uint8_t(0);
        out = {(clip[0] / clip[3] + 1) * 0.5f * float(kScreenW), (1 - clip[1] / clip[3]) * 0.5f * float(kScreenH),
               clip[2] / clip[3]};
        return uint8_t(1);
    };
    auto fill = [&](const ModelMesh &mesh, uint8_t value, bool writeDepth) {
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const uint32_t a = mesh.indices[i];
            const uint32_t b = mesh.indices[i + 1];
            const uint32_t c = mesh.indices[i + 2];
            if (a < ok.size() && b < ok.size() && c < ok.size() && ok[a] && ok[b] && ok[c])
                stencil.fillTriangle(screen[a].data(), screen[b].data(), screen[c].data(), value, writeDepth);
        }
    };
    const std::array<float, 3> light = shadowLight(_scene);
    for (const ModelMesh &mesh : model.model().meshes) {
        bool textured = false;
        for (const auto &g : mesh.groups)
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
            const std::array<float, 3> moved = shadowLight(_scene, origin);
            for (size_t v = 0; v < mesh.vertices.size(); v++) {
                const float *q = mesh.vertices[v].position;
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

void ScenePage::drawMask(const CoverageMask &mask, Color colour, uint8_t alpha) {
    // A quad over the mask's box, its texture the colour where covered.
    if (mask.empty())
        return;
    const int x0 = mask.minX();
    const int y0 = mask.minY();
    const int w = mask.maxX() - x0 + 1;
    const int h = mask.maxY() - y0 + 1;
    std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (mask.at(x0 + x, y0 + y)) {
                uint8_t *p = rgba.data() + (size_t(y) * size_t(w) + size_t(x)) * 4;
                p[0] = colour.r, p[1] = colour.g, p[2] = colour.b, p[3] = alpha;
            }
    gfx::TextureId tex = 0;
    gfx::TextureParams params;
    params.clamp = true;
    params.mipmaps = false;
    params.nearest = true;
    if (_renderer->createTexture(w, h, rgba.data(), params, tex))
        return;
    _frameTextures.push_back(tex);
    const float l = 2.0f * float(x0) / kScreenW - 1;
    const float r = 2.0f * float(x0 + w) / kScreenW - 1;
    const float t = 1 - 2.0f * float(y0) / kScreenH;
    const float b = 1 - 2.0f * float(y0 + h) / kScreenH;
    std::vector<gfx::Vertex> v(4);
    const float corners[4][4] = {{l, t, 0, 0}, {r, t, 1, 0}, {r, b, 1, 1}, {l, b, 0, 1}};
    for (size_t i = 0; i < 4; i++) {
        v[i] = {};
        v[i].pos[0] = corners[i][0], v[i].pos[1] = corners[i][1], v[i].pos[2] = 0.5f;
        v[i].uv[0] = corners[i][2], v[i].uv[1] = corners[i][3];
        v[i].color[0] = v[i].color[1] = v[i].color[2] = v[i].color[3] = 255;
    }
    gfx::MeshId mesh = 0;
    if (_renderer->createMesh(v, {0, 2, 1, 0, 3, 2}, mesh))
        return;
    _frameMeshes.push_back(mesh);
    gfx::DrawCall call;
    call.mesh = mesh;
    call.indexCount = 6;
    call.texture = tex;
    call.state.src = gfx::Blend::kSrcAlpha;
    call.state.dst = gfx::Blend::kOneMinusSrcAlpha;
    call.state.depthTest = false;
    call.state.depthWrite = false;
    call.state.cull = gfx::Cull::kNone;
    _renderer->draw(call);
}

int ScenePage::whiteOut() const {
    // 0x402e20: a white quad where the stencil is 2 (the player's pixels
    // still showing), more opaque the further it has walked past A, fully
    // at B.
    const Ac2Record *camr = _scene.camera();
    const float a = camr->f("whiteoutStart");
    const float b = camr->f("whiteoutEnd");
    if (a == b)
        return 0;
    for (const Character &c : _characters) {
        if (!c.player || c.z >= a)
            continue;
        return std::clamp(c.z < b ? 255 : int((a - c.z) / (a - b) * 255), 0, 255);
    }
    return 0;
}

void ScenePage::drawWhiteOut() {
    if (const int alpha = whiteOut(); alpha > 0) {
        _stencil->select(2, *_shadowMask);
        drawMask(*_shadowMask, Color{255, 255, 255}, uint8_t(alpha));
    }
}

void ScenePage::stencilQuad(gfx::TextureId mask, gfx::Renderer3D::StencilMode mode, uint8_t ref, Color colour,
                            uint8_t alpha) {
    // A quad over the frame. With a mask (Mark): the stencil set to `ref`
    // where the mask's alpha isn't 0, nothing drawn. Without (Inside):
    // `colour` at `alpha` where the stencil is `ref`.
    if (!_quad || (mode == gfx::Renderer3D::StencilMode::kMark && !mask))
        return;
    gfx::DrawCall call;
    call.mesh = _quad;
    call.indexCount = 6;
    call.texture = mask;
    call.color = {colour.r / 255.0f, colour.g / 255.0f, colour.b / 255.0f, mask ? 1.0f : alpha / 255.0f};
    call.state.src = mask ? gfx::Blend::kZero : gfx::Blend::kSrcAlpha;
    call.state.dst = mask ? gfx::Blend::kOne : gfx::Blend::kOneMinusSrcAlpha;
    call.state.alphaTest = mask ? gfx::AlphaTest::kGt0 : gfx::AlphaTest::kNone;
    call.state.depthTest = false;
    call.state.depthWrite = false;
    call.state.cull = gfx::Cull::kNone;
    _renderer->setView(gfx::identity(), {0, 0, 0});
    _renderer->setStencilMode(mode);
    _renderer->setStencilReference(ref);
    _renderer->draw(call);
    _renderer->setStencilMode(gfx::Renderer3D::StencilMode::kNone);
}

void ScenePage::fillPolygon(const std::vector<std::pair<float, float>> &points, Color c, uint8_t alpha) {
    if (points.size() < 3)
        return;
    float minX = 1e9f;
    float minY = 1e9f;
    float maxX = -1e9f;
    float maxY = -1e9f;
    for (const auto &[x, y] : points) {
        minX = std::min(minX, x), maxX = std::max(maxX, x);
        minY = std::min(minY, y), maxY = std::max(maxY, y);
    }
    const int x0 = std::max(0, int(std::floor(minX)));
    const int x1 = std::min(kScreenW - 1, int(std::ceil(maxX)));
    const int y0 = std::max(0, int(std::floor(minY)));
    const int y1 = std::min(kScreenH - 1, int(std::ceil(maxY)));
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            // Inside if on the same side of every edge.
            const float px = float(x) + 0.5f;
            const float py = float(y) + 0.5f;
            int sign = 0;
            bool inside = true;
            for (size_t i = 0; i < points.size() && inside; i++) {
                const auto &[ax, ay] = points[i];
                const auto &[bx, by] = points[(i + 1) % points.size()];
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
    const Ac2Record *camr = _scene.camera();
    if (!camr || _grid.empty())
        return;
    const SceneCamera cam = SceneCamera::fromRecord(*camr);
    const float size = float(_grid.cellSize());
    auto corner = [&](float x, float z, std::pair<float, float> &out) {
        const float p[3] = {x, 0, z};
        return cam.project(p, kScreenW, kScreenH, out.first, out.second);
    };
    // A cell's square on the floor, on the screen.
    auto square = [&](WalkGrid::Cell cell, std::vector<std::pair<float, float>> &pts) {
        const float x0 = float(cell.a - 32) * size;
        const float z1 = float(32 - cell.b) * size;
        pts.assign(4, {});
        return corner(x0, z1 - size, pts[0]) && corner(x0 + size, z1 - size, pts[1]) && corner(x0 + size, z1, pts[2]) &&
               corner(x0, z1, pts[3]);
    };
    std::vector<std::pair<float, float>> pts;
    for (int a = 0; a < WalkGrid::kSize; a++)
        for (int b = 0; b < WalkGrid::kSize; b++) {
            const uint8_t v = _grid.at(a, b);
            if (v == 0 || !square({a, b}, pts))
                continue;
            // 1 blocked, 2 blocked unless allowed, 3 blocked at first.
            const Color c = v == 1 ? Color{230, 50, 50} : v == 2 ? Color{240, 150, 40} : Color{240, 230, 60};
            fillPolygon(pts, c, 90);
        }
    if (_hover.valid() && square(_hover, pts))
        for (size_t i = 0; i < 4; i++)
            line(int(pts[i].first), int(pts[i].second), int(pts[(i + 1) % 4].first), int(pts[(i + 1) % 4].second),
                 _grid.blocked(_hover, true) ? Color{255, 80, 80} : Color{80, 255, 255});
    for (size_t i = 0; i + 1 < _path.size(); i++) {
        float x0;
        float z0;
        float x1;
        float z1;
        _grid.centre(_path[i], x0, z0);
        _grid.centre(_path[i + 1], x1, z1);
        std::pair<float, float> p0;
        std::pair<float, float> p1;
        if (corner(x0, z0, p0) && corner(x1, z1, p1))
            line(int(p0.first), int(p0.second), int(p1.first), int(p1.second), {90, 255, 120});
    }
}

void ScenePage::beginWalk(Character &c, bool run) {
    // With its path set: its first leg starts on the next step, turning on
    // the spot first if it isn't facing the way (0x442780 with 1). A
    // gesture or idle variation under way eases out over 0.3 s (0x441160
    // calls 0x4048f0), else it would hold the body still as it moved.
    if (c.actor)
        c.actor->stopGestures();
    c.running = run;
    c.leg = -1;
    c.turnFirst = true;
    c.turningFirst = false;
    c.walkIn = 0;
}

void ScenePage::stepWalk(Character &c, float dt) {
    constexpr float kTwoPi = 6.2831853f;
    constexpr float kDegrees = 57.29578f;
    auto headingTo = [&](const std::pair<float, float> &p) { return std::atan2(p.first - c.x, p.second - c.z); };
    if (c.leg < 0) {
        // A leg starts (0x442780): as long as the way to its point, if
        // that's 10 or more (a shorter one is passed over).
        while (!c.path.empty()) {
            c.leg = std::hypot(c.path.front().first - c.x, c.path.front().second - c.z);
            if (c.leg >= 10)
                break;
            c.path.erase(c.path.begin());
            c.leg = -1;
        }
        if (c.path.empty())
            return arrive(c);
        // The first turns on the spot, by its turning animation, if it's 2
        // degrees or more off (0x441c90, 0x441730, 0x441690); then walks or
        // runs. Later legs go on walking.
        if (std::exchange(c.turnFirst, false)) {
            if (c.actor->rotateTo(headingTo(c.path.front()) * kDegrees, c.rotY * kDegrees))
                c.turningFirst = true;
            else
                startWalk(c, c.running);
        }
    }
    if (c.turningFirst) {
        if (c.actor->rotating())
            return;
        c.turningFirst = false;
        startWalk(c, c.running);
    }
    // Forward along its heading, as the walk carries it (0x4422c0), the
    // leg shortened by as much; past its end, back by the overshoot
    // (0x425a70), and the next leg (0x442690).
    const float step = c.walkSpeed * dt;
    c.x += std::sin(c.rotY) * step, c.z += std::cos(c.rotY) * step;
    c.leg -= step;
    if (c.leg <= 0) {
        c.x += std::sin(c.rotY) * c.leg, c.z += std::cos(c.rotY) * c.leg;
        c.path.erase(c.path.begin());
        c.leg = -1;
        if (c.path.empty())
            arrive(c);
        return;
    }
    // Steering (0x442160): toward its point, or in the leg's last 100
    // units toward the next one (none on the last leg's), 100 degrees a
    // second (200 running), until within 2 degrees.
    const std::pair<float, float> *toward = c.leg > 100 ? &c.path.front() : c.path.size() > 1 ? &c.path[1] : nullptr;
    if (!toward)
        return;
    const float off = std::remainder(headingTo(*toward) - c.rotY, kTwoPi);
    if (std::abs(off) * kDegrees < 2)
        return;
    const float most = (c.running ? 200.0f : 100.0f) / kDegrees * dt;
    c.rotY = std::remainder(c.rotY + std::clamp(off, -most, most), kTwoPi);
}

void ScenePage::arrive(Character &c) {
    // There (0x442733): idle1 eased in over half a second, and a script
    // waiting on the walk goes on.
    c.leg = -1, c.turningFirst = false;
    c.actor->play("idle1", 0, 0.5f);
    if (c.waiting && (c.waitVerb == "walk" || c.waitVerb == "run")) {
        ScriptRunner *r = c.waiting;
        const std::string verb = c.waitVerb;
        c.waiting = nullptr;
        r->finished(verb);
    }
}

bool ScenePage::startWalk(Character &c, bool run) {
    if (!c.actor)
        return false;
    // The walk carries the body forward (the biped's root, Bip01, moves
    // the whole loop's distance): that is how fast it walks. The game's
    // characters move by that much, while the loop starts again from the
    // same place.
    const bool played = !c.actor->play(run ? "run" : "walk", 0, 0).empty();
    c.walkSpeed = 0;
    if (const Animator::Slot *s = c.actor->animator().base(); played && s && s->animation->duration > 0) {
        const Actor::Motion m = c.actor->motion(*s->animation);
        c.walkSpeed = std::sqrt(m.drift[0] * m.drift[0] + m.drift[1] * m.drift[1] + m.drift[2] * m.drift[2]) /
                      s->animation->duration;
    }
    // One with no walk goes 0.075 units every half millisecond, running
    // or not: 150 a second (0x4422d9).
    if (c.walkSpeed <= 1)
        c.walkSpeed = 150.0f;
    return true;
}

void ScenePage::follow(Character &f) {
    // 0x4400a0: while the player walks somewhere, a follower 250 units or
    // more away, and further from the goal than the player, walks there by
    // the path-finder, stopping 125 units short (0x442780).
    const Character *p = nullptr;
    for (const Character &c : _characters)
        if (c.player)
            p = &c;
    if (!p || p->path.empty() || !_leadGoal.valid())
        return;
    float gx;
    float gz;
    _grid.centre(_leadGoal, gx, gz);
    auto dist = [](float ax, float az, float bx, float bz) { return std::hypot(ax - bx, az - bz); };
    if (dist(p->x, p->z, f.x, f.z) < 250 || dist(p->x, p->z, gx, gz) >= dist(f.x, f.z, gx, gz))
        return;
    const std::vector<WalkGrid::Cell> keep = _path;
    if (walkCharacter(f, _leadGoal, false) && _grid.at(_leadGoal.a, _leadGoal.b) != 2) {
        // The last leg shortened; under 10 units it isn't walked.
        const float fx = f.path.size() > 1 ? f.path[f.path.size() - 2].first : f.x;
        const float fz = f.path.size() > 1 ? f.path[f.path.size() - 2].second : f.z;
        auto &last = f.path.back();
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
    _path = keep; // the overlay shows the player's
}

void ScenePage::placeCharacter(Character &c) const {
    c.placement = characterMatrix(c.x, c.y, c.z, c.rotX, c.rotY, c.rotZ, c.scale);
    const Animator::Slot *s = c.actor ? c.actor->animator().base() : nullptr;
    if (!s || s->animation->duration <= 0 ||
        !(s->name.find("walk") != std::string::npos || s->name.find("run") != std::string::npos ||
          s->name.find("rotate") != std::string::npos))
        return;
    // Back by how far the walk (or turn) has carried the body this loop,
    // as the character itself moves (or turns): by what its pose carries,
    // none when its mask leaves the root at rest, as the walks' and runs'
    // do (else the body stood still through each loop and then jumped).
    const Actor::Motion m = c.actor->motion(*s->animation);
    const float loop = std::fmod(s->clock, s->animation->duration) / s->animation->duration;
    gfx::Mat4 back = gfx::identity();
    const float a = -loop * m.carriedTurn / 57.29578f;
    back[0] = std::cos(a), back[1] = std::sin(a), back[4] = -std::sin(a), back[5] = std::cos(a);
    for (int k = 0; k < 3; k++)
        back[size_t(12 + k)] = -loop * m.carried[k];
    c.placement = gfx::multiply(c.placement, back);
}

bool ScenePage::scrollPictures(float dt) {
    // 0x4378b0: scrollSpeed / 20 pixels a second, in whole pixels; past
    // its own width or height (or the screen's edge) a picture goes back
    // by that much. 1 scrolls left, 2 right, 3 up, 4 down.
    bool moved = false;
    for (auto &[o, s] : _scrolls) {
        const image::Image &img = picture(o->text("file"));
        const int way = o->part("SPRT")->i("scroll");
        const float sum = s.part + dt * o->part("SPRT")->f("scrollSpeed") * 0.05f;
        s.part = sum;
        const int n = int(sum);
        if (n <= 0 || img.w <= 0 || img.h <= 0)
            continue;
        const float w = float(img.w);
        const float h = float(img.h);
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
    const Ac2Record *camr = _scene.camera();
    float x;
    float z;
    if (!camr || _grid.empty() ||
        !SceneCamera::fromRecord(*camr).floorAt(float(sx), float(sy), kScreenW, kScreenH, x, z))
        return;
    const WalkGrid::Cell goal = _grid.cellAt(x, z);
    // From the player's character (else the first character, else the
    // grid's centre: the path alone is shown).
    Character *who = nullptr;
    for (Character &c : _characters)
        if (c.player || !who)
            who = &c;
    // A player whose MODL clickToWalk is 0 stays put (0x416343).
    if (who && who->player && who->record)
        if (const Ac2Record *modl = who->record->part("MODL");
            modl && modl->field("clickToWalk") && modl->i("clickToWalk") == 0)
            return;
    WalkGrid::Cell start{32, 32};
    if (who)
        start = _grid.cellAt(who->x, who->z);
    else
        for (const Ac2Record *o : _scene.objects())
            if (o->tag == "PCXX")
                start = _grid.cellAt(o->f("x"), o->f("z"));
    _path = _grid.findPath(start, goal);
    if (who && who->player && _path.size() > 1)
        _leadGoal = goal;
    if (who && who->actor && _path.size() > 1) {
        who->path.clear();
        for (size_t i = 1; i < _path.size(); i++) {
            float px;
            float pz;
            _grid.centre(_path[i], px, pz);
            who->path.push_back({px, pz});
        }
        beginWalk(*who, false);
    }
    draw();
}

std::vector<std::string> ScenePage::objectKeys(const Ac2Record &o) {
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

void ScenePage::dropLayer(PictureLayer &layer) {
    for (const PictureLayer::Part &part : layer.parts)
        if (part.texture)
            _renderer->destroyTexture(part.texture);
    if (layer.resetMask)
        _renderer->destroyTexture(layer.resetMask);
    layer = PictureLayer();
}

void ScenePage::aloneTexture(const Ac2Record &o, const image::Image &img) {
    // Made again with the brightness, or a video loop's next frame.
    std::string key = std::to_string(brightnessLevel()) + " " + std::to_string(img.w) + "x" + std::to_string(img.h);
    if (const auto lp = _loops.find(&o); lp != _loops.end())
        key += " " + std::to_string(lp->second.next);
    AloneTexture &t = _aloneTextures[&o];
    if (t.texture && t.key == key)
        return;
    if (t.texture)
        _renderer->destroyTexture(t.texture);
    t.texture = 0;
    t.key = key;
    if (img.w <= 0 || img.h <= 0)
        return;
    std::vector<uint8_t> rgba = img.rgba;
    applyBrightness(rgba);
    gfx::TextureParams params;
    params.clamp = true;
    params.mipmaps = false;
    _renderer->createTexture(img.w, img.h, rgba.data(), params, t.texture);
}

std::string ScenePage::conditionOf(const Ac2Record &o) const {
    // A particle object's is its class's default where it has none
    // ("weather >0" for rain and snow).
    if (const auto ps = _particles.find(&o); ps != _particles.end())
        return ps->second.settings().condition;
    return o.text("condition");
}

void ScenePage::retestObjects() {
    // 0x40e900 on every object (0x436280, "update"; 0x4362b0, entering):
    // shown if its condition holds, hidden if not, whatever "hide" or
    // "show" made it.
    for (const Ac2Record *o : _scene.objects())
        _shownNow[o] = testCondition(conditionOf(*o), _state);
}

void ScenePage::showObject(const std::string &name, bool show) {
    // "hide" and "show" (0x40a240, 0x436660): the scene's object by that
    // name, until the scene is entered again or "update" tests the
    // conditions. A video starts from its first frame when shown.
    for (const Ac2Record *o : _scene.objects())
        for (const std::string &key : objectKeys(*o))
            if (key == name) {
                _shownNow[o] = show;
                if (const auto loop = _loops.find(o); show && loop != _loops.end())
                    loop->second.start = _time;
                break;
            }
}

namespace {

// Whether a screen point is in an object's outline (listD: hotspots and
// characters have one), by the crossings of a ray to the right; false if
// it has none.
bool inOutline(const Ac2Record &o, int sx, int sy) {
    const Ac2Value *outline = o.field("listD");
    if (!outline || outline->numbers.size() < 3)
        return false;
    bool inside = false;
    const auto &pts = outline->numbers;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const float xi = float(pts[i].first);
        const float yi = float(pts[i].second);
        const float xj = float(pts[j].first);
        const float yj = float(pts[j].second);
        if ((yi > float(sy)) != (yj > float(sy)) && float(sx) < (xj - xi) * (float(sy) - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}

} // namespace

const Ac2Record *ScenePage::hotspotAt(int sx, int sy) const {
    const Ac2Record *hit = nullptr;
    for (const Ac2Record *o : _scene.objects()) {
        if (o->tag != "SPOT" || !objectShown(*o))
            continue;
        const Ac2Value *outline = o->field("listD");
        const bool inside = outline && outline->numbers.size() >= 3
                                ? inOutline(*o, sx, sy)
                                : sx >= o->i("left") && sx < o->i("right") && sy >= o->i("top") && sy < o->i("bottom");
        if (inside)
            hit = o; // the last drawn is on top
    }
    return hit;
}

void ScenePage::runScript(const Ac2Record &object) {
    // The first conditional script whose condition holds, else its own.
    std::string script = object.text("script");
    if (const Ac2Value *alternatives = object.field("listB"))
        for (const auto &[condition, run] : alternatives->pairs)
            if (testCondition(condition, _state)) {
                script = run;
                break;
            }
    _scriptObject = &object;
    const Ac2Record *hot = object.part("HOTS");
    const std::string name = !object.text("name").empty() ? object.text("name") : hot ? hot->text("name") : "";
    logScript(name + ": " + shorten(script, 120));
    _runner->run(script);
}

ScenePage::Character *ScenePage::scriptCharacter(const ScriptRunner &runner) {
    // setchar's (0x40ae94: by name, as 0x4367b0 finds it), else the
    // player's: the first character marked the player (0x4365d0). A scene
    // whose Antoinette isn't (an NPCX close-up) has none, and the verbs
    // are over at once, as in the game.
    const std::string who = fs::toLower(runner.character());
    if (!who.empty())
        return characterNamed(who);
    for (Character &c : _characters)
        if (c.player)
            return &c;
    return nullptr;
}

ScenePage::Character *ScenePage::characterNamed(const std::string &who) {
    // 0x4367b0: the first whose model's file has "<name>_model" in it
    // (any case); for "poirot", failing that, "poirotbed_model" (Poirot in
    // his bed, in his compartment from 5C on).
    const std::string w = fs::toLower(who);
    auto byModel = [&](const std::string &model) -> Character * {
        for (Character &c : _characters)
            if (c.record && fs::toLower(c.record->text("file")).find(model) != std::string::npos)
                return &c;
        return nullptr;
    };
    if (Character *c = byModel(w + "_model"))
        return c;
    if (w == "poirot")
        if (Character *c = byModel("poirotbed_model"))
            return c;
    for (Character &c : _characters)
        if (c.who == w || fs::toLower(c.record ? c.record->text("name") : "") == w)
            return &c;
    return nullptr;
}

bool ScenePage::objectAt(const std::string &name, float &x, float &z) const {
    const std::string n = fs::toLower(name);
    if (const Character *c = const_cast<ScenePage *>(this)->characterNamed(n)) {
        x = c->x, z = c->z;
        return true;
    }
    for (const Ac2Record *o : _scene.objects())
        if (fs::toLower(o->text("name")) == n) {
            x = o->f("x"), z = o->f("z");
            return true;
        }
    return false;
}

void ScenePage::queueRoutine(Character &c) {
    // The first IDLE entry whose condition holds (0x44087d; "loop" queues
    // it again: 0x405940).
    if (!c.routine || !c.record)
        return;
    for (const Ac2Record &part : c.record->parts)
        if (const Ac2Value *idles = part.field("idles"))
            for (const auto &[condition, script] : idles->pairs)
                if (!script.empty() && testCondition(condition, _state)) {
                    c.routine->run(script);
                    return;
                }
}

bool ScenePage::walkCharacter(Character &c, WalkGrid::Cell goal, bool toPoint, bool run) {
    c.walkIn = 0; // a walk replaces walking in
    const WalkGrid::Cell start = _grid.cellAt(c.x, c.z);
    _path = _grid.findPath(start, goal, toPoint);
    if (_path.size() < 2 || !c.actor)
        return false;
    c.path.clear();
    for (size_t i = 1; i < _path.size(); i++) {
        float px;
        float pz;
        _grid.centre(_path[i], px, pz);
        c.path.push_back({px, pz});
    }
    beginWalk(c, run);
    return true;
}

bool ScenePage::changeScene(const std::string &name, int entry, bool walkIn) {
    // The scene's file by its name, the act's variant if there is one.
    const std::string want = fs::toLower(name) + ".ac2";
    const std::string variant = _state.act.empty() ? "" : fs::toLower(name) + "_" + _state.act + ".ac2";
    int found = -1;
    for (size_t i = 0; i < _names.size(); i++) {
        const std::string file = fs::toLower(_names[i].substr(_names[i].find_last_of('/') + 1));
        if (file == variant || (file == want && found < 0))
            found = int(i);
    }
    if (found < 0) {
        logScript("  (no scene " + name + ")");
        return false;
    }
    _dialogue->leaveScene(sceneName());
    // Leaving (0x423390): where the player was is kept (the game keeps
    // the scene itself), and the act's time, weather and moving apply.
    for (const Character &c : _characters) {
        if (c.player)
            _positions[_state.scene] = {c.x, c.z, c.rotY};
        else if (c.record)
            // The game keeps the scene, so its characters stay where they
            // are (and are saved there: POSI).
            _npcPositions[_state.scene + "|" + fs::toLower(fileNameOf(c.name))] = {{c.x, c.y, c.z}, {0, c.rotY, 0}};
        if (!c.record || !c.actor)
            continue;
        // And as scripts left them: paused (Klaus, for the cleaver talk,
        // through its close-ups), their shadow, what they hold, an
        // animation playing for ever ("anim sitidle 1 0").
        KeptCharacter &k = _keptCharacters[_state.scene + "|" + fs::toLower(fileNameOf(c.name))];
        k.paused = c.paused;
        k.shadow = c.shadow;
        k.props.clear();
        for (const auto &[path, model] : c.props)
            k.props.push_back(path);
        // (Not a walk, run or turn under way: its path isn't kept.)
        const Animator::Slot *base = c.actor->animator().base();
        const bool moving =
            base && (base->name.find("walk") != std::string::npos || base->name.find("run") != std::string::npos ||
                     base->name.find("rotate") != std::string::npos);
        k.endless =
            base && base->loops == 0 && !moving && base->name.compare(0, 4, "idle") != 0 ? base->name : std::string();
    }
    _state.lastScene = _state.scene;
    _story->leavingScene();
    _index = found;
    _entryPoint = entry;
    _entryWalk = walkIn;
    load();
    _entryPoint = -1;
    _entryWalk = false;
    return true;
}

void ScenePage::playSound(const std::string &name) {
    startSound(name, false);
}

audio::SoundHandle ScenePage::startSound(const std::string &name, bool loop) {
    // sound\<name>.wav: found by its file's name, wherever it is.
    std::string key = fs::toLower(name);
    if (const size_t slash = key.find_last_of("\\/"); slash != std::string::npos)
        key = key.substr(slash + 1);
    if (_soundFiles.empty())
        for (const std::string &f : listFiles(_ctx.files, {"sound/"}, {".wav", ".mp3"})) {
            const std::string file = f.substr(f.find_last_of('/') + 1);
            _soundFiles.emplace(fs::toLower(file.substr(0, file.rfind('.'))), f);
        }
    const auto it = _soundFiles.find(key);
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (!_ctx.mixer)
        return audio::kNoSound;
    if (it == _soundFiles.end() || _ctx.files.read(it->second, data) || audio::decodeSound(data, *sound)) {
        logScript("    (no sound " + key + ")");
        return audio::kNoSound;
    }
    const audio::SoundHandle h = _ctx.mixer->play(sound, audio::SoundType::kEffects, audio::kMaxVolume, 0, loop);
    // Kept for "stop", those still playing.
    for (auto s = _sounds.begin(); s != _sounds.end();)
        s = _ctx.mixer->isPlaying(s->second) ? std::next(s) : _sounds.erase(s);
    _sounds.emplace_back(key, h);
    return h;
}

void ScenePage::logScript(const std::string &line) {
    if (onLog)
        onLog(line);
    _scriptLog.push_back(line);
    while (_scriptLog.size() > 8)
        _scriptLog.pop_front();
}

// --- Character verbs ----------------------------------------------------------

std::shared_ptr<GpuModel> ScenePage::propModel(const std::string &file) {
    // Kept with the characters' models: loading one decodes its textures.
    if (file.empty() || !_renderer)
        return nullptr;
    auto found = _models.find(file);
    if (found == _models.end()) {
        Gr2File gr2;
        Model m;
        std::shared_ptr<GpuModel> gpu;
        if (_ctx.files.has(file) && !openGr2(_ctx.files, file, gr2) && !loadModel(gr2, m))
            gpu = std::make_shared<GpuModel>(*_renderer, std::move(m));
        found = _models.emplace(file, gpu).first;
    }
    return found->second;
}

bool ScenePage::startCharacterVerb(ScriptRunner &runner, Character &c, const ScriptEvent &e, bool wait, bool &handled) {
    const std::string v = e.verb();
    auto lower = [](const std::string &t) { return fs::toLower(t); };
    const std::string a1 = lower(e.arg(1));
    const std::string a2 = lower(e.arg(2));
    const std::string a3 = lower(e.arg(3));
    const float body = normalizeDegrees(c.rotY * 57.29578f);
    // The heading from the character to a point (0x425b10): 0 faces +Z.
    auto headingTo = [&](float x, float z) { return normalizeDegrees(std::atan2(x - c.x, z - c.z) * 57.29578f); };
    auto cellHeading = [&](const std::string &a, const std::string &b) {
        float x;
        float z;
        _grid.centre({std::atoi(a.c_str()), std::atoi(b.c_str())}, x, z);
        return headingTo(x, z);
    };
    auto upDown = [](const std::string &w) { return w == "up" ? 1 : w == "down" ? 2 : 0; };
    handled = true;
    if (v == "turn") {
        // The head (0x40b7a8); it never waits.
        if (a1 == "home" && a2 == "all") {
            for (Character &ch : _characters)
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
            float x;
            float z;
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
                if (const Ac2Record *w = c.record->part("WALK"))
                    for (const Ac2Record &pt : w->parts)
                        if (pt.i("id") == std::atoi(name.c_str() + 2)) {
                            heading = normalizeDegrees(pt.f("rotY") * 57.29578f), have = true;
                            if (back)
                                heading += heading <= 180 ? 180 : -180;
                        }
            } else if (name == "camera" && _scene.camera()) {
                // The camera's azimuth (0x441941: the scene's Granny camera
                // +0x68), never turned round.
                heading = normalizeDegrees(_scene.camera()->f("yaw") * 57.29578f), have = true;
            } else {
                float x;
                float z;
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
                                         [&](const auto &p) { return p.first.find(a1) != std::string::npos; }),
                          c.props.end());
            return false;
        }
        const std::string path = "models/objects/" + a1 + "_model.gr2";
        for (const auto &p : c.props)
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
    if (_dialogueAct == _state.act)
        return;
    _dialogueAct = _state.act;
    DialogueBook &book = _dialogue->book();
    book.clear();
    std::vector<std::string> files = {"text/dialogue/gen_antoinette.txt", "text/dialogue/gen_comments.txt",
                                      "text/dialogue/gen_characters.txt"};
    if (!_state.act.empty())
        files.push_back("text/dialogue/act" + _state.act + ".txt");
    else
        for (const std::string &f : listFiles(_ctx.files, {"text/dialogue/act"}, {".txt"}))
            files.push_back(f);
    for (const std::string &f : files) {
        // Each read once (they're read again with every act and saved game).
        auto it = _dialogueTables.find(f);
        if (it == _dialogueTables.end()) {
            std::vector<uint8_t> data;
            Table table;
            if (!_ctx.files.read(f, data))
                table = parseTable(decodeText(data));
            it = _dialogueTables.emplace(f, std::move(table)).first;
        }
        if (!it->second.headings.empty())
            book.add(it->second);
    }
}

bool ScenePage::startTalk(ScriptRunner &runner, const ScriptEvent &e) {
    loadDialogue();
    const TalkArgs t = parseTalkArgs({e.words.begin() + 1, e.words.end()}, runner.fromCharacter());
    if (_dialogue->talk(runner, t.name, t.part, t.thread, t.nest, t.gen, t.number, t.plus, t.greet))
        return true;
    return t.gen && _dialogue->noTalk(runner);
}

bool ScenePage::playVoice(const std::string &file) {
    stopVoice();
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (!_ctx.mixer)
        return false;
    if (_ctx.files.read(file, data) || audio::decodeSound(data, *sound)) {
        logScript("    (no voice " + file + ")");
        return false;
    }
    _voice = _ctx.mixer->play(sound, audio::SoundType::kSpeech);
    _voiceSound = sound;
    return _voice != audio::kNoSound;
}

bool ScenePage::voicePlaying() {
    return _ctx.mixer && _voice != audio::kNoSound && _ctx.mixer->isPlaying(_voice);
}

void ScenePage::stopVoice() {
    if (_ctx.mixer && _voice != audio::kNoSound)
        _ctx.mixer->stop(_voice);
    _voice = audio::kNoSound;
}

std::string ScenePage::readText(const std::string &file) {
    std::vector<uint8_t> data;
    return _ctx.files.read(file, data) ? std::string() : decodeText(data);
}

std::string ScenePage::sceneName() {
    if (_names.empty())
        return {};
    const std::string &n = _names[size_t(_index)];
    const std::string file = n.substr(n.find_last_of('/') + 1);
    return fs::toLower(file.substr(0, file.rfind('.')));
}

void ScenePage::turnHead(const std::string &speaker, const std::string &target) {
    // A line's Target (0x43bd30): "up"/"down" [w], a cell "a b [up|down]",
    // someone "[up|down]"; empty: home again.
    Character *c = characterNamed(speaker);
    if (!c && fs::toLower(speaker) == "poirot")
        c = characterNamed("poirotbed");
    if (!c)
        return;
    std::vector<std::string> w;
    for (const Command &cmd : parseCommands(target))
        for (const std::string &word : cmd.words)
            w.push_back(fs::toLower(word));
    if (w.empty() || w[0] == "0") {
        c->actor->headHome();
        return;
    }
    ScriptEvent e{{"turn"}};
    e.words.insert(e.words.end(), w.begin(), w.end());
    bool handled = false;
    startCharacterVerb(*_runner, *c, e, false, handled);
}

void ScenePage::lineStarted(const std::string &speaker) {
    if (Character *c = characterNamed(speaker))
        c->actor->lineStart();
}

void ScenePage::lineEnded(const std::string &speaker) {
    if (Character *c = characterNamed(speaker))
        c->actor->lineEnd();
}

void ScenePage::mouth(const std::string &speaker, bool on) {
    if (Character *c = characterNamed(speaker))
        c->actor->mouth(on);
}

int ScenePage::voiceLevel() {
    // How loud the voice is where it plays: its samples over a thirtieth
    // of a second, 0 when about silent.
    if (!_ctx.mixer || !_voiceSound || _voice == audio::kNoSound || _voiceSound->channels <= 0)
        return -1;
    const size_t at = size_t(_ctx.mixer->position(_voice));
    const size_t ch = size_t(_voiceSound->channels);
    const size_t frames = _voiceSound->frames();
    const size_t span = size_t(std::max(1, _voiceSound->rate / 30));
    double sum = 0;
    size_t n = 0;
    for (size_t f = at; f < std::min(frames, at + span); f++, n++)
        sum += std::abs(double(_voiceSound->samples[f * ch]));
    return n && sum / double(n) > 600 ? 1 : 0;
}

const Ac2Record *ScenePage::characterAt(int sx, int sy) const {
    const Ac2Record *camr = _scene.camera();
    if (!camr)
        return nullptr;
    const SceneCamera cam = SceneCamera::fromRecord(*camr);
    const Ac2Record *hit = nullptr;
    for (const Ac2Record *o : _scene.objects()) {
        if (o->tag != "NPCX" || o->text("script").empty() || !objectShown(*o))
            continue;
        // Its outline on the screen, if it has one.
        if (const Ac2Value *outline = o->field("listD"); outline && outline->numbers.size() >= 3) {
            if (inOutline(*o, sx, sy))
                hit = o;
            continue;
        }
        // Else where it stands: from its feet to a head's height, about a
        // third as wide.
        float x = o->f("x");
        float z = o->f("z");
        for (const Character &c : _characters)
            if (c.record == o)
                x = c.x, z = c.z;
        const float feet[3] = {x, o->f("y"), z};
        const float head[3] = {x, o->f("y") + 170, z};
        float fx;
        float fy;
        float hx;
        float hy;
        if (!cam.project(feet, kScreenW, kScreenH, fx, fy) || !cam.project(head, kScreenW, kScreenH, hx, hy))
            continue;
        const float h = fy - hy;
        const float half = h * 0.18f;
        if (float(sy) >= hy && float(sy) <= fy && std::abs(float(sx) - fx) <= half)
            hit = o;
    }
    return hit;
}

std::string ScenePage::scriptText(const std::string &name) {
    std::vector<uint8_t> data;
    if (_ctx.files.read("text/scripts/" + fs::toLower(name) + ".txt", data))
        return {};
    return decodeText(data);
}

bool ScenePage::start(ScriptRunner &runner, const ScriptEvent &e, bool wait) {
    const std::string &v = e.verb();
    auto lower = [](const std::string &t) { return fs::toLower(t); };
    {
        bool handled = false;
        Character *c = scriptCharacter(runner);
        if (v == "pause" || v == "resume" || v == "loop" || v == "attach" || v == "detach")
            c = v == "loop" ? nullptr : characterNamed(e.arg(1)) ? characterNamed(e.arg(1)) : c;
        if (v == "loop")
            for (Character &ch : _characters)
                if (ch.routine.get() == &runner)
                    c = &ch;
        if (c) {
            const bool waits = startCharacterVerb(runner, *c, e, wait, handled);
            if (handled)
                return waits;
        }
    }
    if (v == "walk" || v == "run") {
        Character *c = scriptCharacter(runner);
        if (!c || _grid.empty())
            return false;
        WalkGrid::Cell goal;
        bool toPoint = false;
        const std::string target = lower(e.arg(1));
        if (!target.empty() && e.arg(2).empty() && !std::isdigit(uint8_t(target[0]))) {
            // "walk sp3": the character's walk point 3; else an object.
            float x = 0;
            float z = 0;
            bool have = false;
            if (target.size() > 2 && target.compare(0, 2, "sp") == 0 && c->record) {
                if (const Ac2Record *w = c->record->part("WALK"))
                    for (const Ac2Record &pt : w->parts)
                        if (pt.i("id") == std::atoi(target.c_str() + 2))
                            x = pt.f("x"), z = pt.f("z"), have = true;
                toPoint = true;
            } else {
                for (const Character &other : _characters)
                    if (other.who == target)
                        x = other.x, z = other.z, have = true;
            }
            if (!have)
                return false;
            goal = _grid.cellAt(x, z);
        } else {
            goal = {std::atoi(e.arg(1).c_str()), std::atoi(e.arg(2).c_str())};
        }
        if (!walkCharacter(*c, goal, toPoint, v == "run"))
            return false;
        if (c->player)
            _leadGoal = goal;
        if (wait)
            c->waiting = &runner, c->waitVerb = v;
        return wait;
    }
    if (v == "scene" || v == "gotoscene") {
        // gotoscene: the scene "savescene" kept, its ".ac2" taken off.
        std::vector<std::string> words = e.words;
        if (v == "gotoscene") {
            words = {"scene"};
            for (const Command &c : parseCommands(_state.texts["savedscene"]))
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
        if (_playing && wait && name.find(_location) == std::string::npos && name.find("global") == std::string::npos) {
            _load = LocationLoad();
            _load.state = 1;
            _load.location = name.substr(0, name.find('_'));
            _load.scene = name, _load.verb = v, _load.entry = entry, _load.runner = &runner;
            _load.walkIn = walkIn;
            _fadeTarget = 1;
            return true;
        }
        changeScene(name, entry, walkIn);
        return false;
    }
    if (v == "act") {
        _story->startAct(lower(e.arg(1)));
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
        if (!_restoring)
            return false;
        _state.act = actName(_restoring->actMajor, _restoring->actMinor);
        loadDialogue();
        _load = LocationLoad();
        _load.state = 1;
        _load.restore = true;
        _load.location = fs::toLower(_restoring->location);
        // trailing spaces in the saved cells ("calais.hall ")
        while (!_load.location.empty() && _load.location.back() == ' ')
            _load.location.pop_back();
        _load.verb = v;
        _load.runner = wait ? &runner : nullptr;
        _fadeTarget = 1;
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
        if (_optionsMenu)
            optionsAction(lower(e.arg(1)));
        else if (puzzleActive())
            puzzleAction(lower(e.arg(1)));
        else if (_slots)
            slotAction(lower(e.arg(1)), lower(e.arg(2)));
        return false;
    }
    if (v == "yesno") {
        _yesNo = std::make_unique<YesNo>();
        _yesNo->text = interfaceText(lower(e.arg(1)));
        _yesNo->kind = std::atoi(e.arg(2).c_str());
        draw();
        return false;
    }
    if (v == "quit") {
        _quit = true;
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
        _fadeTarget = lower(e.arg(1)) == "out" ? 1.0f : 0.0f;
        if (_fade == _fadeTarget)
            return false;
        if (wait)
            _fadeRunner = &runner;
        return wait;
    }
    if (v == "sound") {
        // sound\<name>.wav, looping with "loop"; "sound" waits till it's
        // over (0x40bd45, 0x425400).
        const audio::SoundHandle h = startSound(e.arg(1), lower(e.arg(2)) == "loop");
        if (h == audio::kNoSound || !wait)
            return false;
        _soundRunner = &runner, _soundWaited = h;
        return true;
    }
    if (v == "stop") {
        // The first sound playing whose name has the word in it (0x4251c0).
        const std::string name = lower(e.arg(1));
        for (auto it = _sounds.begin(); it != _sounds.end(); ++it)
            if (it->first.find(name) != std::string::npos && _ctx.mixer && _ctx.mixer->isPlaying(it->second)) {
                _ctx.mixer->stop(it->second);
                _sounds.erase(it);
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
        _interfaceOff = lower(e.arg(1)) == "off" ? true : lower(e.arg(1)) == "on" ? false : _interfaceOff;
        return false;
    }
    if (v == "letterbox") {
        // 0x40a3db, 0x416ef0: "on" and "off" at once; "start" and "end"
        // slide the bars in from nothing or out to it, and wait till
        // they're there. (Nothing in the install uses it.)
        const std::string how = lower(e.arg(1));
        _letterbox.runner = nullptr;
        if (how == "on")
            _letterbox.mode = 1, _letterbox.height = 70;
        else if (how == "off")
            _letterbox.mode = 0;
        else if (how == "start" || how == "end") {
            _letterbox.mode = how == "start" ? 2 : 3;
            if (how == "start")
                _letterbox.height = 0;
            _letterbox.runner = &runner;
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
        if (name == "this" && _scriptObject)
            name = lower(!_scriptObject->text("name").empty() ? _scriptObject->text("name")
                                                              : normalizeArchiveName(_scriptObject->text("file")));
        showObject(name, v == "show");
        draw();
        return false;
    }
    if (v == "take") {
        // The item comes and the object goes (0x40b12f). "take this": the
        // hotspot's, its item named by its picture ("...\scegun.png").
        std::string item = lower(e.arg(1));
        if (item == "this" && _scriptObject) {
            const std::string file = lower(_scriptObject->text("file"));
            const size_t sce = file.find("\\sce");
            if (sce != std::string::npos)
                item = file.substr(sce + 4, file.find('.', sce) - sce - 4);
            showObject(lower(!_scriptObject->text("name").empty() ? _scriptObject->text("name")
                                                                  : normalizeArchiveName(_scriptObject->text("file"))),
                       false);
        } else {
            showObject(item, false);
        }
        if (item != "this")
            addItem(_state, item, 1); // just taken (0x40b1de)
        draw();
        return false;
    }
    if (v == "update") {
        // The scene's objects looked at again, for conditions now holding
        // (0x40b4be: 0x436280).
        retestObjects();
        draw();
        return false;
    }
    if (v == "talk")
        return startTalk(runner, e) && wait;
    // A character's verb with no character to do it: over at once. As in
    // the game when the scene has no player (Antoinette as an NPCX in the
    // table close-ups, 0x4365d0) or no one of that name (0x4367b0). A
    // line's speaker in another scene of the place (0x43bb60 looks there
    // too) isn't loaded here, so what the game would do to them off screen
    // is lost.
    for (const char *verb : {"turn", "look", "rotate", "anim", "animate", "pause", "resume", "loop", "attach", "detach",
                             "hideshadow", "showshadow"})
        if (v == verb) {
            const std::string &who = runner.character();
            logScript("    (" + v + ": no " + (who.empty() ? std::string("player") : who) + " here)");
            return false;
        }
    // The rest isn't done here yet: logged, and over.
    logScript("    (not done yet: " + v + ")");
    return false;
}

void ScenePage::line(int x0, int y0, int x1, int y1, Color c) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
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

void ScenePage::handleEvent(const Event &event) {
    _frame++;
    // In a game the mouse's place is kept within x 1004 and y 748, or 758
    // while a conversation is on (0x4014ce..0x401546); clicks and picks
    // use that place (0x416036).
    Event ev = event;
    if (_playing &&
        (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown || ev.type == EventType::kMouseUp)) {
        ev.mouseX = std::min(ev.mouseX, 1004);
        ev.mouseY = std::min(ev.mouseY, _dialogue->active() ? 758 : 748);
    }
    if (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown || ev.type == EventType::kMouseUp) {
        _lastMouseX = ev.mouseX, _lastMouseY = ev.mouseY;
        _cursorMoved |= cursorShown();
    }
    if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft && puzzleActive())
        puzzleRelease(ev.mouseX, ev.mouseY);
    if (_optionsMenu && _optionsMenu->dragging) {
        if (ev.type == EventType::kMouseMove)
            draw();
        else if (ev.type == EventType::kMouseUp)
            optionsRelease();
    }
    if (_yesNo) {
        // The yes/no box takes every click and key while it's up.
        if (ev.type == EventType::kMouseMove) {
            // The word under the mouse lights up, with mouseover2.wav as it
            // comes onto one (0x4325b0).
            const int at = yesNoAt(ev.mouseX, ev.mouseY);
            if (at != _yesNo->highlighted) {
                _yesNo->highlighted = at;
                if (at >= 0)
                    playSound("mouseover2");
                draw();
            }
        } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
            // select2.wav as one is clicked (0x43274b).
            if (const int at = yesNoAt(ev.mouseX, ev.mouseY); at >= 0) {
                playSound("select2");
                return answer(at == 0);
            }
        } else if (ev.type == EventType::kKeyDown && (ev.key == key::kReturn || ev.key == key::kEscape)) {
            answer(ev.key == key::kReturn);
        }
        return;
    }
    if (_movie) {
        // Escape skips it, unless it's the legal notice (0x409860); clicks
        // don't.
        if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && _movieName.find("legal") == std::string::npos)
            endMovie();
        return;
    }
    if (_messageTime > 0 &&
        (ev.type == EventType::kKeyDown ||
         (ev.type == EventType::kMouseDown && (ev.button == MouseButton::kLeft || ev.button == MouseButton::kRight)))) {
        // A key or a click (left or right) while the message shows only
        // puts it away (0x4164e0, 0x416000, 0x416450).
        _messageTime = 0;
        draw();
        return;
    }
    if (_dialogue->active() &&
        (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown || ev.type == EventType::kMouseWheel)) {
        if (ev.type == EventType::kMouseMove)
            _dialogue->mouseMove(ev.mouseX, ev.mouseY);
        else if (ev.type == EventType::kMouseWheel)
            _dialogue->wheel(ev.wheelY, _lastMouseX, _lastMouseY); // a notch is 120 (0x401431)
        else if (ev.button == MouseButton::kMiddle)
            _dialogue->middleClick(); // WM_MBUTTONDOWN (0x401557, 0x4163c0)
        else if (ev.button == MouseButton::kLeft)
            _dialogue->click(ev.mouseX, ev.mouseY);
        draw();
        return;
    }
    // The interface: the bar, the inventory, the item in hand
    // (scene_interface.cpp).
    if (interfaceEvent(ev))
        return;
    if (ev.type == EventType::kMouseMove && _showGrid) {
        float x;
        float z;
        WalkGrid::Cell cell;
        if (const Ac2Record *cam = _scene.camera();
            cam && SceneCamera::fromRecord(*cam).floorAt(float(ev.mouseX), float(ev.mouseY), kScreenW, kScreenH, x, z))
            cell = _grid.cellAt(x, z);
        if (!(cell == _hover)) {
            _hover = cell;
            draw();
        }
        return;
    }
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        // The game takes no clicks while a script runs, but during its
        // walks and turns and after an "unlock"; one taken drops what was
        // running (0x409760).
        if (_runner->locked())
            return;
        if (!_runner->idle() && !_slots && !puzzleActive())
            _runner->clear();
        if (_slots && !hotspotAt(ev.mouseX, ev.mouseY) && slotClick(ev.mouseX, ev.mouseY))
            return;
        // A puzzle sees the click first, then the hotspot under it runs
        // (0x4161c1).
        if (puzzleActive())
            puzzleClick(ev.mouseX, ev.mouseY);
        _runner->setFromCharacter(false);
        // The object the game's pick finds there (0x41613c); one with
        // nothing to run is walked to like the floor. An item in hand was
        // used by the interface (interfaceEvent) before this.
        const Ac2Record *picked = pickAllowed(ev.mouseY) ? pickAt(ev.mouseX, ev.mouseY) : nullptr;
        if (picked)
            hotspotPressed(*picked);
        if (picked && clickable(*picked)) {
            if (picked->tag == "NPCX") {
                // A character's own script: a talk then offers its generic
                // lines and goodbyes (0x4162a3).
                _runner->setFromCharacter(true);
                _resumeAfter = picked;
            }
            runScript(*picked);
        } else if (pickAllowed(ev.mouseY)) {
            // The floor only where the pick could be (0x416300).
            walkTo(ev.mouseX, ev.mouseY);
        }
        return;
    }
    if (ev.type != EventType::kKeyDown)
        return;
    if (_playing && gameKey(ev.key))
        return;
    if (const int d = step(ev.key, _index, int(_names.size()))) {
        _index += d;
        load();
        return;
    }
    const int n = int(_scene.objects().size());
    if (ev.key == key::kUp || ev.key == key::kDown) {
        if (n > 0)
            _selected = ev.key == key::kDown ? (_selected + 1 >= n ? -1 : _selected + 1)
                                             : (_selected < 0 ? n - 1 : _selected - 1);
        draw();
    } else if (ev.key == 'h') {
        _hotspots = !_hotspots;
        draw();
    } else if (ev.key == 'n') {
        newGame();
    } else if (ev.key == 'g') {
        _showGrid = !_showGrid;
        draw();
    } else if (ev.key == 'm' || ev.key == 'w' || ev.key == 't') {
        int &v = _state.values[ev.key == 'm' ? "moving" : ev.key == 'w' ? "weather" : "time"];
        v = !v;
        retestObjects(); // the viewer's own: shown at once
        draw();
    } else if (ev.key == 'i' || ev.key == key::kF1 + 8) {
        // (In a game I is the inventory's: F9.)
        _info = !_info;
        draw();
    } else if (ev.key == 'c' && _renderer) {
        _showCharacters = !_showCharacters;
        if (_showCharacters && _characters.empty())
            loadCharacters();
        draw();
    }
}

// --- Movies ---------------------------------------------------------------------

MoviePage::MoviePage(ViewerContext &ctx) : Page(ctx), _player(ctx.mixer) {
    _names = listFiles(ctx.files, {"movies/", "elements/", "locations/"}, {".bik"});
}

MoviePage::~MoviePage() {
    _player.stop();
}

void MoviePage::activate() {
    begin2D();
    open();
}

void MoviePage::deactivate() {
    _player.stop();
}

void MoviePage::open() {
    _player.stop();
    _subtitles.clear();
    _info.clear();
    clear(0, 0, 0);
    if (_names.empty())
        return;
    const std::string &name = _names[size_t(_index)];
    {
        // Its subtitles, if it has some: Text/Movies/<its name>.txt.
        std::string stem = name.substr(name.find_last_of('/') + 1);
        stem = stem.substr(0, stem.rfind('.'));
        std::vector<uint8_t> text;
        if (!_ctx.files.read("text/movies/" + fs::toLower(stem) + ".txt", text))
            _subtitles = moviesOwn(parseSubtitles(decodeText(text)), fs::toLower(stem));
    }
    std::vector<uint8_t> data;
    Error e = _ctx.files.read(name, data);
    if (!e)
        e = _player.open(std::move(data), _ctx.sys.getMillis());
    if (e) {
        _info = e.message;
        return;
    }
    const bink::Movie &movie = *_player.movie();
    char buf[160];
    std::snprintf(buf, sizeof buf, "%dx%d, %u frames at %.2f fps, revision %c", movie.width(), movie.height(),
                  movie.frameCount(), _player.fps(), movie.revision());
    _info = buf;
    if (_player.hasSound())
        _info += ", sound " + std::to_string(movie.audioRate()) + " Hz";
    if (!_player.error().empty())
        _info += ", " + _player.error();
    _ctx.setTitle("Movies: " + name);
}

void MoviePage::update(uint64_t nowMs) {
    _player.update(nowMs);
    // Video loops (no sound) start over.
    if (_player.loaded() && !_player.hasSound() && !_player.paused() && _player.done(nowMs)) {
        open();
        return;
    }
    clear(0, 0, 0);
    const image::Image &frame = _player.frame();
    if (frame.w)
        image::blit(_ctx.screen, frame, (kScreenW - frame.w) / 2, (kScreenH - frame.h) / 2);
    drawSubtitles(_subtitles, _player.seconds());
    label(4, 2,
          (_names.empty() ? std::string("No movies") : _names[size_t(_index)]) + "  (" + std::to_string(_index + 1) +
              "/" + std::to_string(_names.size()) + ")  " + _info +
              (_player.loaded()
                   ? "  frame " + std::to_string(_player.next()) + "/" + std::to_string(_player.movie()->frameCount())
                   : "") +
              (_subtitles.empty() ? "" : ", " + std::to_string(_subtitles.size()) + " subtitles"),
          {255, 230, 150});
    help("Left/Right: movie  Space: pause  Enter: restart");
    Page::update(nowMs);
}

void MoviePage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown)
        return;
    if (const int d = step(ev.key, _index, int(_names.size()))) {
        _index += d;
        open();
    } else if (ev.key == key::kReturn) {
        open();
    } else if (ev.key == key::kSpace && _player.loaded()) {
        _player.pause(!_player.paused(), _ctx.sys.getMillis());
    }
}

// --- Text ---------------------------------------------------------------------

TextPage::TextPage(ViewerContext &ctx) : Page(ctx) {
    _names = listFiles(ctx.files, {"text/"}, {".txt"});
}

void TextPage::activate() {
    begin2D();
    load();
}

void TextPage::load() {
    _scroll = 0;
    _lines.clear();
    if (_names.empty())
        return draw();
    std::vector<uint8_t> data;
    if (Error e = _ctx.files.read(_names[size_t(_index)], data)) {
        _lines.push_back(e.message);
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
            _lines.push_back(line.substr(0, cut));
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
          (_names.empty() ? std::string("No text files") : _names[size_t(_index)]) + "  (" +
              std::to_string(_index + 1) + "/" + std::to_string(_names.size()) + "), lines " +
              std::to_string(_scroll + 1) + "-" + std::to_string(std::min(int(_lines.size()), _scroll + rows)) +
              " of " + std::to_string(_lines.size()),
          {255, 230, 150});
    for (int i = 0; i < rows && size_t(_scroll + i) < _lines.size(); i++)
        text(4, font::kLineHeight + 6 + i * font::kLineHeight, _lines[size_t(_scroll + i)]);
    help("Left/Right: file  Up/Down, PgUp/PgDn (with Shift: file by 10): scroll");
    present();
    _ctx.setTitle("Text: " + (_names.empty() ? std::string() : _names[size_t(_index)]));
}

void TextPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown)
        return;
    const int rows = (kScreenH - 2 * font::kLineHeight - 8) / font::kLineHeight;
    const int maxScroll = std::max(0, int(_lines.size()) - rows);
    const bool shift = ev.mods & keymod::kShift;
    if (ev.key == key::kLeft || ev.key == key::kRight ||
        ((ev.key == key::kPageUp || ev.key == key::kPageDown) && shift)) {
        if (const int d = step(ev.key, _index, int(_names.size()))) {
            _index += d;
            load();
        }
    } else if (ev.key == key::kUp || ev.key == key::kDown || ev.key == key::kPageUp || ev.key == key::kPageDown ||
               ev.key == key::kHome || ev.key == key::kEnd) {
        const int by = ev.key == key::kUp         ? -1
                       : ev.key == key::kDown     ? 1
                       : ev.key == key::kPageUp   ? -rows
                       : ev.key == key::kPageDown ? rows
                       : ev.key == key::kHome     ? -int(_lines.size())
                                                  : int(_lines.size());
        _scroll = std::clamp(_scroll + by, 0, maxScroll);
        draw();
    }
}

// --- Sounds ---------------------------------------------------------------------

SoundPage::SoundPage(ViewerContext &ctx) : Page(ctx) {
    _names = listFiles(ctx.files, {"sound/", "models/"}, {".wav", ".mp3"});
}

void SoundPage::activate() {
    begin2D();
    draw();
}

void SoundPage::deactivate() {
    if (_ctx.mixer)
        _ctx.mixer->stopAll();
    _playing = audio::kNoSound;
}

void SoundPage::play() {
    _info.clear();
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (Error e = _ctx.files.read(_names[size_t(_index)], data)) {
        _info = e.message;
    } else if (Error e2 = audio::decodeSound(data, *sound)) {
        _info = e2.message;
    } else {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s, %d Hz, %d channel%s, %.2f s", audio::formatName(sound->format), sound->rate,
                      sound->channels, sound->channels == 1 ? "" : "s",
                      sound->rate ? double(sound->frames()) / sound->rate : 0.0);
        _info = buf;
        if (_ctx.mixer) {
            _ctx.mixer->stopAll();
            _playing = _ctx.mixer->play(sound, audio::SoundType::kEffects);
        } else {
            _info += " (no audio device)";
        }
    }
    draw();
}

void SoundPage::draw() {
    clear(18, 24, 22);
    const int rows = (kScreenH - 4 * font::kLineHeight - 8) / font::kLineHeight;
    const int first = std::clamp(_index - rows / 2, 0, std::max(0, int(_names.size()) - rows));
    label(4, 2, std::to_string(_names.size()) + " sounds" + (_info.empty() ? "" : ":  " + _info), {255, 230, 150});
    for (int i = 0; i < rows && first + i < int(_names.size()); i++) {
        const bool current = first + i == _index;
        const int y = 2 * font::kLineHeight + 4 + i * font::kLineHeight;
        if (current)
            fill(0, y - 1, kScreenW, font::kLineHeight, {60, 80, 70});
        text(8, y, _names[size_t(first + i)], current ? Color{255, 255, 255} : Color{190, 200, 195});
    }
    help("Up/Down (Left/Right): sound  PgUp/PgDn: 10  Space/Enter: play  S: stop");
    present();
    _ctx.setTitle("Sounds");
}

void SoundPage::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || _names.empty())
        return;
    const uint32_t k = ev.key == key::kUp ? key::kLeft : ev.key == key::kDown ? key::kRight : ev.key;
    if (const int d = step(k, _index, int(_names.size()))) {
        _index += d;
        _info.clear();
        draw();
    } else if (ev.key == key::kSpace || ev.key == key::kReturn) {
        play();
    } else if (ev.key == 's' && _ctx.mixer) {
        _ctx.mixer->stopAll();
    }
}

} // namespace reluva::awe
