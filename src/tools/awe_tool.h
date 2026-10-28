// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/gfx3d.h"
#include "common/system.h"

#include <cstdint>
#include <deque>

// reluva-tool's AWE engine commands: the command's exit code, or -1 if
// argv[1] isn't one of them.
int aweToolCommand(int argc, char** argv);
// reluva-tool awe-check: every Granny file, scene and text file of an
// install read, failures reported (awe_check.cpp).
int aweCheckCommand(int argc, char** argv);
// Their usage lines.
const char* aweToolUsage();

namespace reluva::awe {

// A renderer that keeps nothing and draws nothing: the scenes' characters
// load (and run their routines) without a window.
class NullRenderer : public gfx::Renderer3D {
public:
    Error createTexture(int, int, const uint8_t*, const gfx::TextureParams&, gfx::TextureId& out) override {
        out = gfx::TextureId(++next_);
        return Error::success();
    }
    void destroyTexture(gfx::TextureId) override {}
    Error createMesh(const std::vector<gfx::Vertex>&, const std::vector<uint32_t>&, gfx::MeshId& out) override {
        out = gfx::MeshId(++next_);
        return Error::success();
    }
    Error createIndexedMesh(gfx::MeshId, const std::vector<uint32_t>&, gfx::MeshId& out) override {
        out = gfx::MeshId(++next_);
        return Error::success();
    }
    Error setMeshIndices(gfx::MeshId, const std::vector<uint32_t>&) override { return Error::success(); }
    void destroyMesh(gfx::MeshId) override {}
    int width() const override { return 1024; }
    int height() const override { return 768; }
    std::string deviceName() const override { return "none"; }
    void beginFrame(const gfx::Mat4&, const gfx::Vec3&, Color) override {}
    void draw(const gfx::DrawCall&) override {}
    void setView(const gfx::Mat4&, const gfx::Vec3&) override {}
    void setDynamicLights(const std::vector<gfx::DynamicLight>&) override {}
    void setViewport(int, int, int, int) override {}
    void setDepthRange(float) override {}
    void clearDepth() override {}
    bool hasStencil() const override { return false; }
    void setStencilMode(StencilMode) override {}
    void setMirrored(bool) override {}
    void setGamma(float) override {}
    void endFrame(const Surface*) override {}
    void captureNextFrame() override {}
    bool takeCapture(std::vector<uint8_t>&) override { return false; }

private:
    uint32_t next_ = 0;
};

// A System with no window: the last frame shown is kept, events come from
// a queue.
class HeadlessSystem : public System {
public:
    Surface last;
    std::deque<Event> events;
    Error initGraphics(int, int, PixelFormat) override { return Error::success(); }
    void setPalette(const Color*, int, int) override {}
    void updateScreen(const Surface& s) override { last = s; }
    void setWindowTitle(const std::string&) override {}
    bool pollEvent(Event& ev) override {
        if (events.empty())
            return false;
        ev = events.front();
        events.pop_front();
        return true;
    }
    uint64_t getMillis() const override { return now; }
    uint64_t now = 0;
    void delayMillis(uint32_t) override {}
    Error startAudio(AudioCallback) override { return Error::fail("no audio"); }
    void stopAudio() override {}
    int audioRate() const override { return 44100; }
    // 3D from another System (a window's), if given; else, with
    // `characters`, a renderer that draws nothing.
    System* threeD = nullptr;
    bool characters = false;
    NullRenderer none;
    Error init3D(int width, int height, gfx::Renderer3D*& out) override {
        if (!threeD && characters) {
            out = &none;
            return Error::success();
        }
        if (!threeD) {
            out = nullptr;
            return Error::fail("no 3D");
        }
        return threeD->init3D(width, height, out);
    }
};

// reluva-tool awe-path: the game's path between two cells beside Reluva's
// (awe_path.cpp).
int aweCommandPath(int argc, char** argv);
// reluva-tool awe-scripts: every script command of an install, by verb
// (awe_scripts.cpp).
int aweCommandScripts(int argc, char** argv);

// reluva-tool awe-acts -solve and -play: a playthrough searched for, and
// one played back (awe_play.cpp).
int aweCommandSolve(int argc, char** argv);
} // namespace reluva::awe
