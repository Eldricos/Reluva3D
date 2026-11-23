// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Storm3D as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/storm3d/data/engine-record.txt.
//
//   reluva-tests storm3d.engine [--update]
//
// So far: its install is found as Star Trek: Armada, and the data viewer
// shows it page by page. Add steps as the engine learns to run its games
// (see tests/engines/idtech3/engine.cpp), then --update and review the
// record.

#include "audio/wav.h"
#include "common/headless_system.h"
#include "engines/registry.h"
#include "engines/viewer_registry.h"
#include "image/image.h"
#include "tests/engines/storm3d/sod_writer.h"

#include "tests/support/record.h"
#include "tests/support/recording_renderer.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using namespace reluva;

namespace {

using test::Hash;
using test::RecordingRenderer;

// The headless System with a player: keys pressed at given times, and the
// sound pulled as the clock moves. It keeps a checksum of every screen
// shown, of the sound, and each window title.
class PlayingSystem : public HeadlessSystem {
public:
    struct Input {
        uint64_t at;
        Event event;
    };
    std::vector<Input> inputs;
    Hash screens;
    Hash sound;
    int shown = 0;
    int64_t soundFrames = 0;
    int64_t soundPeak = 0;
    std::vector<std::string> titles;

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        screens.bytes(s.pixels);
        shown++;
    }
    void setWindowTitle(const std::string &t) override {
        HeadlessSystem::setWindowTitle(t);
        if (titles.empty() || titles.back() != t)
            titles.push_back(t);
    }
    bool pollEvent(Event &ev) override {
        if (_next < inputs.size() && inputs[_next].at <= getMillis()) {
            ev = inputs[_next++].event;
            return true;
        }
        return false;
    }
    void delayMillis(uint32_t ms) override {
        HeadlessSystem::delayMillis(ms);
        if (!_audio)
            return;
        _pending += int64_t(ms) * audioRate();
        const int frames = int(_pending / 1000);
        _pending %= 1000;
        std::vector<int16_t> buf(size_t(frames) * 2);
        _audio(buf.data(), frames);
        sound.bytes(buf.data(), buf.size() * 2);
        soundFrames += frames;
        for (int16_t v : buf)
            soundPeak = std::max<int64_t>(soundPeak, v < 0 ? -int64_t(v) : v);
    }
    // 3D through the recording renderer, for the Models page.
    RecordingRenderer renderer;
    Error init3D(int, int, gfx::Renderer3D *&out) override {
        out = &renderer;
        return Error::success();
    }
    Error startAudio(AudioCallback cb) override {
        _audio = std::move(cb);
        return Error::success();
    }
    void stopAudio() override { _audio = nullptr; }

private:
    size_t _next = 0;
    AudioCallback _audio;
    int64_t _pending = 0;
};

Event keyDown(uint32_t key) {
    Event e;
    e.type = EventType::kKeyDown;
    e.key = key;
    return e;
}

std::string joined(const std::vector<std::string> &lines) {
    std::string out;
    for (const std::string &l : lines)
        out += (out.empty() ? "" : " | ") + l;
    return out.empty() ? "(none)" : out;
}

void writeFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}

// A w x h TGA of a diagonal gradient, its alpha falling to the right.
std::vector<uint8_t> makeTga(int w, int h) {
    image::Image img;
    img.w = w;
    img.h = h;
    img.rgba.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t *p = img.pixel(x, y);
            p[0] = uint8_t(x * 255 / w);
            p[1] = uint8_t(y * 255 / h);
            p[2] = uint8_t((x + y) * 127 / (w + h));
            p[3] = uint8_t(255 - x * 255 / w);
        }
    return image::encodeTga(img, true);
}

// A quarter of a second of a 440 Hz tone at Armada's effects' rate,
// 22,050 Hz mono (the shared encoder writes 16-bit).
std::vector<uint8_t> makeWav() {
    audio::Sound tone;
    tone.rate = 22050;
    tone.channels = 1;
    tone.sourceBits = 8;
    for (int i = 0; i < 22050 / 4; ++i)
        tone.samples.push_back(int16_t(std::lround(8000 * std::sin(i * 2 * 3.14159265358979 * 440 / 22050))));
    return audio::encodePcmWav(tone);
}

// A sprite table as the game's are written: a sprite on Bbase.tga stepped
// through two keyframes, and one laid out in a row by the game.
const char *const kSprites = "sprite_table\r\n\r\n@reference=64\r\n@skip=(0,0,0)\r\n"
                             "@animation walk\r\noffset 2 1.0 step\r\n@keyframes\r\n0\t0\r\n16\t16\r\n"
                             "@animation strip\r\noffset 4 1.0 step\r\n@auto=row\r\n@keyframes\r\n"
                             "# Name\tFile\tU\tV\tW\tH\r\n"
                             "box\tBbase\t0\t0\t16\t16\t@anim=walk\r\n"
                             "strip\tBbase\t0\t32\t16\t16\t@anim=strip @origin=(8,8)\r\n";

// Armada's files by name, so an unknown release, a few textures, a model,
// a sprite table, a sound and a text file.
void makeInstall(const std::filesystem::path &dir) {
    for (const char *name : {"Armada.exe", "assets.zfs", "objtxt.zfs"})
        writeFile(dir / name, {'x'});
    writeFile(dir / "Textures/RGB/Bbase.tga", makeTga(64, 64));
    writeFile(dir / "Textures/RGB/Bbase.1", makeTga(32, 32));
    writeFile(dir / "Textures/Index8/Xspark01", makeTga(16, 16));
    writeFile(dir / "sounds/effects/tone.wav", makeWav());
    writeFile(dir / "SOD/square.sod", storm3d::testing::modelV18("Bbase"));
    const std::string sprites = kSprites;
    writeFile(dir / "Sprites/sprites.spr", {sprites.begin(), sprites.end()});
    std::string credits = "// Credits\r\n";
    for (int i = 1; i <= 80; ++i)
        credits += "\tLine " + std::to_string(i) + "\r\n";
    writeFile(dir / "Credits.txt", {credits.begin(), credits.end()});
}

void engineTest(const std::filesystem::path &tmp) {
    test::Record rec;
    const MetaEngine *engine = findEngine("storm3d");
    const MetaViewer *viewer = findViewer("storm3d");
    if (!engine || !viewer)
        return test::fail("storm3d or its viewer isn't registered");

    const std::filesystem::path dir = tmp / "install";
    makeInstall(dir);
    const std::vector<DetectedGame> found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    for (const DetectedGame &game : found)
        rec.add("detect.game", game.gameId + ": " + game.description);

    // The data viewer, its pages in turn. The archives are only names here,
    // so the viewer reads the install's folders without them.
    std::filesystem::remove(dir / "assets.zfs");
    std::filesystem::remove(dir / "objtxt.zfs");
    GameTarget target{"armada", "storm3d", "armada", "Star Trek: Armada", dir, tmp / "saves", {}};
    PlayingSystem sys;
    sys.inputs = {
        {100, keyDown(key::kRight)},     // RGB/Bbase.tga, after Index8's one picture
        {150, keyDown(key::kRight)},     // its smaller copy, Bbase.1
        {200, keyDown('a')},             // its alpha over a checkerboard
        {250, keyDown('f')},             // the next folder: back to the first
        {300, keyDown('i')},             // no status line
        {350, keyDown(key::kTab)},       // models: the square, turning
        {450, keyDown(key::kUp)},        // seen from higher
        {500, keyDown('+')},             // nearer
        {550, keyDown(key::kSpace)},     // no longer turning
        {600, keyDown(key::kTab)},       // sprites, back in 2D
        {650, keyDown(key::kDown)},      // the box's second keyframe
        {700, keyDown(key::kRight)},     // the strip
        {750, keyDown(key::kDown)},      // its second cell
        {800, keyDown(key::kTab)},       // sounds
        {850, keyDown(key::kSpace)},     // the tone played
        {1250, keyDown(key::kTab)},      // no movies: the text, the sprite table first
        {1300, keyDown('f')},            // the next kind: Credits.txt
        {1350, keyDown(key::kPageDown)}, // its next screen
        {1400, keyDown(key::kTab)},      // back to the images
        {1500, keyDown(key::kEscape)},   // to reluva-viewer's list
    };
    const Error err = viewer->create(sys, target)->run();
    rec.add("run.result", err ? "error: " + err.message : std::string("ok"));
    rec.add("run.size", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
    rec.add("run.screens", std::to_string(sys.shown) + " " + sys.screens.hex());
    rec.add("run.sound",
            std::to_string(sys.soundFrames) + " frames, peak " + std::to_string(sys.soundPeak) + " " + sys.sound.hex());
    rec.add("run.3d", std::to_string(sys.renderer.frames) + " frames, " + std::to_string(sys.renderer.drawCount) +
                          " draws " + sys.renderer.draws.hex());
    rec.add("run.3d.textures", std::to_string(sys.renderer.textureCount) + " " + sys.renderer.textures.hex());
    rec.add("run.3d.meshes", std::to_string(sys.renderer.meshCount) + " " + sys.renderer.meshes.hex());
    rec.add("run.titles", joined(sys.titles));
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/storm3d/data/engine-record.txt", "storm3d.engine",
              "tests/engines/storm3d/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("storm3d.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
