// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The NetImmerse engine as a whole, without the game's files: the basic
// engine test, run by ctest and CI. It makes a small LEGO Alpha Team
// install of its own (.pac archives holding two .FIN models, a texture, a
// .DDF font with its page, a .DDS sound, a puzzle and the locale's text,
// built as docs/engines/netimmerse/formats/ describes them), checks that
// the engine recognises an install, and then, as there's no LEGOATeam.exe
// in it, runs the engine's data viewer on it as the launcher would: every
// page in turn, stepping through what each shows and playing the sound,
// then Esc back to the launcher. What happens goes into a record (counts,
// the window's titles, and checksums of the screens and the sound),
// compared with tests/engines/netimmerse/data/engine-record.txt.
//
// The game itself (its program in the x86 emulator) needs the game's own
// files: that's the game-files test, tests/engines/netimmerse/gamedata.cmake.
//
//   reluva-tests netimmerse.engine [--update]

#include "common/gfx3d.h"
#include "common/headless_system.h"
#include "engines/registry.h"
#include "tests/engines/netimmerse/fixtures.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::netimmerse::testing;
using test::Hash;

namespace {

// A 3D renderer that draws nothing but keeps checksums of what it's given:
// the textures, the meshes, and each frame's draw calls. Positions and
// matrices go in rounded to a thousandth, so the last bit's differences
// between compilers' maths don't change the record.
class RecordingRenderer : public gfx::Renderer3D {
public:
    Hash textures, meshes, draws;
    int textureCount = 0, meshCount = 0, frames = 0, drawCount = 0;

    Error createTexture(int width, int height, const uint8_t* rgba, const gfx::TextureParams& params,
                        gfx::TextureId& out) override {
        textures.i32(width).i32(height).bytes(rgba, size_t(width) * size_t(height) * 4).i32(params.clamp);
        ++textureCount;
        out = gfx::TextureId(++next_);
        return Error::success();
    }
    void destroyTexture(gfx::TextureId) override {}
    Error createMesh(const std::vector<gfx::Vertex>& vertices, const std::vector<uint32_t>& indices,
                     gfx::MeshId& out) override {
        mesh(vertices);
        meshes.bytes(indices.data(), indices.size() * 4);
        ++meshCount;
        out = gfx::MeshId(++next_);
        return Error::success();
    }
    Error createIndexedMesh(gfx::MeshId, const std::vector<uint32_t>& indices, gfx::MeshId& out) override {
        meshes.bytes(indices.data(), indices.size() * 4);
        out = gfx::MeshId(++next_);
        return Error::success();
    }
    Error setMeshIndices(gfx::MeshId, const std::vector<uint32_t>& indices) override {
        meshes.bytes(indices.data(), indices.size() * 4);
        return Error::success();
    }
    Error setMeshVertices(gfx::MeshId, const std::vector<gfx::Vertex>& vertices) override {
        mesh(vertices);
        return Error::success();
    }
    void destroyMesh(gfx::MeshId) override {}
    int width() const override { return 640; }
    int height() const override { return 480; }
    std::string deviceName() const override { return "recording"; }
    void beginFrame(const gfx::Mat4& viewProj, const gfx::Vec3&, Color clear) override {
        matrix(viewProj);
        draws.i32(clear.r).i32(clear.g).i32(clear.b);
        ++frames;
    }
    void draw(const gfx::DrawCall& call) override {
        draws.i32(int32_t(call.mesh)).i32(int32_t(call.firstIndex)).i32(int32_t(call.indexCount));
        draws.i32(call.vertexOffset).i32(int32_t(call.texture));
        matrix(call.model);
        ++drawCount;
    }
    void setView(const gfx::Mat4& viewProj, const gfx::Vec3&) override { matrix(viewProj); }
    void setDynamicLights(const std::vector<gfx::DynamicLight>& lights) override { draws.i32(int32_t(lights.size())); }
    void setViewport(int, int, int, int) override {}
    void setDepthRange(float) override {}
    void clearDepth() override {}
    bool hasStencil() const override { return false; }
    void setStencilMode(StencilMode) override {}
    void setMirrored(bool) override {}
    void setGamma(float) override {}
    void endFrame(const Surface* overlay) override {
        if (overlay)
            draws.bytes(overlay->pixels);
    }
    void captureNextFrame() override {}
    bool takeCapture(std::vector<uint8_t>&) override { return false; }

private:
    uint32_t next_ = 0;
    static int32_t rounded(float v) { return int32_t(std::lround(double(v) * 1000)); }
    void matrix(const gfx::Mat4& m) {
        for (float v : m)
            draws.i32(rounded(v));
    }
    void mesh(const std::vector<gfx::Vertex>& vertices) {
        for (const gfx::Vertex& v : vertices) {
            for (float f : v.pos)
                meshes.i32(rounded(f));
            for (float f : v.uv)
                meshes.i32(rounded(f));
            meshes.bytes(v.color, 4);
        }
    }
};

// The headless System with a player: keys pressed at given times, and the
// sound pulled as the clock moves (22050 Hz). It keeps a checksum of every
// screen shown and of the sound.
class PlayingSystem : public HeadlessSystem {
public:
    struct Input {
        uint64_t at;
        Event event;
    };
    std::vector<Input> inputs;
    Hash screens, sound;
    int shown = 0;
    int64_t soundFrames = 0, soundPeak = 0;
    std::vector<std::string> titles;

    void updateScreen(const Surface& s) override {
        HeadlessSystem::updateScreen(s);
        screens.bytes(s.pixels);
        shown++;
    }
    void setWindowTitle(const std::string& t) override {
        HeadlessSystem::setWindowTitle(t);
        if (titles.empty() || titles.back() != t)
            titles.push_back(t);
    }
    bool pollEvent(Event& ev) override {
        if (next_ < inputs.size() && inputs[next_].at <= getMillis()) {
            ev = inputs[next_++].event;
            return true;
        }
        return false;
    }
    void delayMillis(uint32_t ms) override {
        HeadlessSystem::delayMillis(ms);
        if (!audio_)
            return;
        pending_ += int64_t(ms) * audioRate();
        const int frames = int(pending_ / 1000);
        pending_ %= 1000;
        std::vector<int16_t> buf(size_t(frames) * 2);
        audio_(buf.data(), frames);
        sound.bytes(buf.data(), buf.size() * 2);
        soundFrames += frames;
        for (int16_t v : buf)
            soundPeak = std::max<int64_t>(soundPeak, v < 0 ? -int64_t(v) : v);
    }
    // 3D through the recording renderer, for the model page.
    RecordingRenderer renderer;
    Error init3D(int, int, gfx::Renderer3D*& out) override {
        out = &renderer;
        return Error::success();
    }
    Error startAudio(AudioCallback cb) override {
        audio_ = std::move(cb);
        return Error::success();
    }
    void stopAudio() override { audio_ = nullptr; }

private:
    size_t next_ = 0;
    AudioCallback audio_;
    int64_t pending_ = 0;
};

Event keyDown(int key) {
    Event e;
    e.type = EventType::KeyDown;
    e.key = key;
    return e;
}

// An uncompressed 32-bit TGA, top row first, its pixels from (x, y).
std::vector<uint8_t> tga(int width, int height, uint32_t (*pixel)(int x, int y)) {
    std::vector<uint8_t> d = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    d.insert(d.end(), {uint8_t(width), uint8_t(width >> 8), uint8_t(height), uint8_t(height >> 8), 32, 0x28});
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const uint32_t argb = pixel(x, y);
            d.insert(d.end(), {uint8_t(argb), uint8_t(argb >> 8), uint8_t(argb >> 16), uint8_t(argb >> 24)});
        }
    return d;
}

// A .DDS sound: a WAVEFORMATEX (PCM, 22,050 Hz mono, 16 bits), then a
// tenth of a second of a square wave.
std::vector<uint8_t> dds() {
    std::vector<uint8_t> d = {1, 0, 1, 0};
    put32(d, 22050);
    put32(d, 22050 * 2);
    d.insert(d.end(), {2, 0, 16, 0, 0, 0});
    for (int i = 0; i < 2205; ++i) {
        const int16_t s = i / 50 % 2 ? 8000 : -8000;
        d.push_back(uint8_t(s));
        d.push_back(uint8_t(uint16_t(s) >> 8));
    }
    return d;
}

PacFile file(const std::string& name, const std::vector<uint8_t>& data) { return {name, asString(data), kFileTime, 0}; }
PacFile file(const std::string& name, const std::string& text) { return {name, text, kFileTime, 0}; }

// The install: the archives the game has, each with a little of what it
// holds (docs/engines/netimmerse/formats/install.md), and no program.
void makeInstall(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    writeFile(dir / "Fin.pac", buildPac({file("THING.FIN", buildFin()), file("U0001.FIN", buildAnimatedFin())}));
    writeFile(dir / "Map.pac", buildPac({file("Brick.tga", tga(8, 8, [](int x, int y) {
                                              return (x + y) % 2 ? 0xffc04020u : 0xff2040c0u;
                                          }))}));
    writeFile(dir / "Etc.pac",
              buildPac({file("ARIAL.DDF", ddfFile(0)),
                        file("ARIAL_0.TGA", tga(4, 4, [](int x, int y) {
                                 return x < 2 && y < 2 ? 0xffffffffu : (x == 3 && y == 3 ? 0xffffffffu : 0u);
                             })),
                        file("M0010.PUZ", "PUZVER 1\r\nNEARFAR 20 10000\r\n"),
                        file("EMPTY.TXT", "")}));
    writeFile(dir / "Sound.pac", buildPac({file("BEEP.DDS", dds())}));
    writeFile(dir / "Locale_en.pac",
              buildPac({file("Locale_en.txt", "####\r\n# Assorted UI Strings\r\n####\r\n\r\n==MenuExitButton==\r\n"
                                              "Exit Game\r\n\r\n==Brief==\r\n\r\nLine one\r\n\r\nLine two\r\n")}));
}

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines)
        out += (out.empty() ? "" : " | ") + l;
    return out.empty() ? "(none)" : out;
}

void engineTest(const std::filesystem::path& tmp) {
    test::Record rec;
    const auto finish = [&] {
        rec.check(RELUVA_SOURCE_DIR "/tests/engines/netimmerse/data/engine-record.txt", "netimmerse.engine",
                  "tests/engines/netimmerse/engine.cpp");
    };
    const MetaEngine* engine = findEngine("netimmerse");
    if (!engine)
        return test::fail("netimmerse isn't registered");

    // Recognised: by its program and its behaviours' archive, as a release
    // not in the table (only the English retail CD's archive is).
    const std::filesystem::path install = tmp / "LEGO Alpha Team";
    makeInstall(install);
    rec.add("detect.no_program", int64_t(engine->detect(install).size()));
    const std::filesystem::path withProgram = tmp / "with program";
    std::filesystem::create_directories(withProgram);
    writeFile(withProgram / "LEGOATeam.exe", {'M', 'Z'});
    writeFile(withProgram / "Bhvr.pac", buildPac({}));
    const auto found = engine->detect(withProgram);
    rec.add("detect.program", found.empty() ? "(none)" : found[0].gameId + " " + found[0].description);

    // The data viewer, page by page: archives, models, textures, fonts,
    // sounds, text.
    GameTarget target{"legoalphateam", "netimmerse", "legoalphateam", "LEGO Alpha Team", install, tmp / "saves", {}};
    PlayingSystem sys;
    sys.inputs = {
        {100, keyDown(key::Tab)},    // models
        {150, keyDown('h')},         // its hidden parts shown
        {200, keyDown(key::Right)},  // the next model
        {250, keyDown(' ')},         // its animation paused
        {300, keyDown(key::Tab)},    // textures
        {400, keyDown(key::Tab)},    // fonts
        {450, keyDown('p')},         // its pages
        {500, keyDown(key::Tab)},    // sounds
        {550, keyDown(key::Space)},  // played
        {800, keyDown(key::Tab)},    // text
        {850, keyDown(key::Right)},  // the next file
        {900, keyDown(key::Down)},   // scrolled
        {1000, keyDown(key::Tab)},   // back to the archives
        {1100, keyDown(key::Escape)} // to the launcher
    };
    auto game = engine->create(sys, target);
    const Error err = game->run();
    rec.add("run.result", err ? "error: " + err.message : std::string("ok"));
    rec.add("run.size", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
    rec.add("run.screens", std::to_string(sys.shown) + " " + sys.screens.hex());
    rec.add("run.sound", std::to_string(sys.soundFrames) + " frames, peak " + std::to_string(sys.soundPeak) + " " +
                             sys.sound.hex());
    rec.add("run.3d", std::to_string(sys.renderer.frames) + " frames, " + std::to_string(sys.renderer.drawCount) +
                          " draws " + sys.renderer.draws.hex());
    rec.add("run.3d.textures", std::to_string(sys.renderer.textureCount) + " " + sys.renderer.textures.hex());
    rec.add("run.3d.meshes", std::to_string(sys.renderer.meshCount) + " " + sys.renderer.meshes.hex());
    rec.add("run.titles", joined(sys.titles));
    rec.add("run.saves", int64_t(std::filesystem::exists(tmp / "saves")));
    finish();
}

const test::Suite suite("netimmerse.engine", {
    {"the data viewer on an install of its own", [](const std::filesystem::path& tmp) { engineTest(tmp); }},
});

} // namespace
