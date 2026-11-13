// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

//   reluva-tests nfs.unit.host

#include "tests/engines/nfs/fixtures.h"

using namespace reluva::nfs::testing;

namespace {


// THRASH's fog as voodoo2a.dll has Glide draw it (thrash.md, Fog).
void testThrashFog() {
    CHECK(glideFog(0, 5) == 0);
    CHECK(std::fabs(glideFog(52428.8f, 5) - 1) < 1e-4f);
    CHECK(std::fabs(glideFog(1, 5) - (1 - std::exp(-0.2f))) < 1e-4f);
    CHECK(glideFog(100, 500) < glideFog(1000, 500));
    CHECK(glideFog(1000, 0) == 0);

    // Triangles over pixel (10, 10), black, at the w given; red fog of 5.
    x86::Cpu cpu(1u << 21);
    const uint32_t target = 0x10000, verts = 0x1000;
    NativeThrash t(cpu);
    t.setTarget(target, 640 * 2, 640, 480);
    auto put = [&](int i, float x, float y, float w) {
        const float f[4] = {x, y, 0.5f, 1 / w};
        std::memcpy(cpu.memory().data() + verts + 32 * i, f, 16);
        const uint32_t black = 0xff000000u;
        std::memcpy(cpu.memory().data() + verts + 32 * i + 16, &black, 4);
    };
    auto draw = [&](float w) {
        put(0, 0, 0, w);
        put(1, 40, 0, w);
        put(2, 0, 40, w);
        t.triangle(verts, verts + 32, verts + 64);
        uint16_t p;
        std::memcpy(&p, cpu.memory().data() + target + 10 * 1280 + 10 * 2, 2);
        return p;
    };
    t.setState(0xf, 0xffff0000u);
    t.setState(0xe, 5);
    CHECK(draw(50000) == 0xf800);  // all fog
    const uint16_t near = draw(1); // 18% of it, by entry 0 of the table
    CHECK((near >> 11) >= 4 && (near >> 11) <= 6 && (near & 0x7ff) == 0);
    t.setState(0x15, 0);
    CHECK(draw(50000) == 0); // off
    t.setState(0x15, 4);
    CHECK(draw(50000) == 0xf800); // on again, with the table it had
    t.setState(0xe, 0);
    CHECK(draw(50000) == 0);

    // Dithered to 5:6:5 as Glide does (state 5, on from the start): a
    // 4x4 block of grey 100 averages 100 in red (12.15 of 31 steps), where
    // truncation gives 12 steps (98.7) everywhere; state 5 = 0 truncates.
    {
        auto grey = [&](uint32_t colour) {
            for (int i = 0; i < 3; ++i)
                std::memcpy(cpu.memory().data() + verts + 32 * i + 16, &colour, 4);
            t.triangle(verts, verts + 32, verts + 64);
            float sum = 0;
            int distinct = 0, last = -1;
            for (int y = 4; y < 8; ++y)
                for (int x = 4; x < 8; ++x) {
                    uint16_t p;
                    std::memcpy(&p, cpu.memory().data() + target + y * 1280 + x * 2, 2);
                    sum += float(p >> 11);
                    distinct += int(p >> 11) != last && last >= 0 ? 1 : 0;
                    last = int(p >> 11);
                }
            return std::make_pair(sum / 16, distinct);
        };
        put(0, 0, 0, 1);
        put(1, 40, 0, 1);
        put(2, 0, 40, 1);
        const auto dithered = grey(0xff646464u);
        CHECK(std::fabs(dithered.first - 100.0f * 31 / 255) < 0.07f && dithered.second > 0);
        t.setState(0x5, 0);
        const auto plain = grey(0xff646464u);
        CHECK(plain.first == 12 && plain.second == 0);
        t.setState(0x5, 1);
    }

    // Recorded for the GPU, a batch carries its fog.
    t.setState(0xe, 250);
    t.setRecording(true);
    draw(100);
    const ThrashFrame frame = t.takeFrame();
    CHECK(frame.batches.size() == 1 && frame.batches[0].fog == 250 && frame.batches[0].fogColour == 0xff0000);
}


// THRASH drawn as d3da.dll's Direct3D device draws (thrash.md, d3da's
// device defaults): bilinear filtering, state 7 picking point sampling or
// bilinear, no dithering whatever state 5 says.
void testThrashDirect3D() {
    x86::Cpu cpu(1u << 21);
    const uint32_t target = 0x10000, verts = 0x1000, texels = 0x2000;
    // A 2x2 5:6:5 checker: black, white / white, black.
    const uint16_t checker[4] = {0x0000, 0xffff, 0xffff, 0x0000};
    std::memcpy(cpu.memory().data() + texels, checker, sizeof checker);
    // A 40x40 quad from (0, 0), white, the texture once across it.
    auto corner = [&](int i, float x, float y) {
        const float f[4] = {x, y, 0.5f, 1};
        std::memcpy(cpu.memory().data() + verts + 32 * i, f, 16);
        const uint32_t white = 0xffffffffu;
        std::memcpy(cpu.memory().data() + verts + 32 * i + 16, &white, 4);
        const float uv[2] = {x / 40, y / 40};
        std::memcpy(cpu.memory().data() + verts + 32 * i + 24, uv, 8);
    };
    corner(0, 0, 0);
    corner(1, 40, 0);
    corner(2, 40, 40);
    corner(3, 0, 40);
    auto red = [&](NativeThrash &t, int x, int y) {
        t.quad(verts, verts + 32, verts + 64, verts + 96);
        uint16_t p;
        std::memcpy(&p, cpu.memory().data() + target + y * 1280 + x * 2, 2);
        return int(p >> 11);
    };
    auto setUp = [&](NativeThrash &t) {
        t.setTarget(target, 640 * 2, 640, 480);
        t.talloc(7, 2, 2, 4);
        t.tupdate(7, texels, 0);
        t.setState(0x1, 7);
        t.setState(0xd, 0); // clamped
    };
    {
        // The 3dfx look: bilinear, as voodoo2a.dll asks for itself after
        // setvideomode (thrash.md, Texture filtering); the game's state 7
        // changes nothing (voodoo2a's own dithering mixes the grey).
        NativeThrash t(cpu);
        setUp(t);
        CHECK(!t.direct3D());
        const int grey = red(t, 20, 20);
        CHECK(grey >= 14 && grey <= 16);
        t.setState(0x7, 0);
        CHECK(red(t, 20, 20) == grey);
    }
    {
        // Held point-sampled (texturefilter=point): state 7 still changes nothing.
        NativeThrash t(cpu);
        t.setPointSampled(true);
        t.setDirect3D(false);
        setUp(t);
        CHECK(red(t, 20, 20) == 0 && red(t, 10, 20) == 31);
        t.setState(0x7, 1);
        CHECK(red(t, 20, 20) == 0);
    }
    {
        NativeThrash t(cpu);
        t.setDirect3D(true);
        setUp(t);
        // At the texture's centre the four texels mix to grey, about half
        // (127 of 255: 15 of 31 truncated, no dither); by the clamped edges
        // only one texel is reached, (0, 0) at pixel (9, 9), (1, 0) at (30, 9).
        CHECK(red(t, 20, 20) == 15);
        CHECK(red(t, 9, 9) == 0 && red(t, 30, 9) == 31);
        // State 7: 0 point-sampled (d3da's TEXTUREMAG/MIN NEAREST), 1 bilinear.
        t.setState(0x7, 0);
        CHECK(red(t, 20, 20) == 0);
        t.setState(0x7, 2); // not one d3da knows: unchanged
        CHECK(red(t, 20, 20) == 0);
        t.setState(0x7, 1);
        CHECK(red(t, 20, 20) == 15);
        // No dithering, state 5 or not: a flat grey stays one value.
        t.setState(0x1, 0);
        t.setState(0x5, 1);
        for (int i = 0; i < 4; ++i) {
            const uint32_t grey = 0xff646464u;
            std::memcpy(cpu.memory().data() + verts + 32 * i + 16, &grey, 4);
        }
        int first = -1;
        bool flat = true;
        for (int y = 4; y < 8; ++y)
            for (int x = 4; x < 8; ++x) {
                const int r = red(t, x, y);
                flat = flat && (first < 0 || r == first);
                first = r;
            }
        CHECK(flat && first == 12);
        // Recorded for the GPU, a batch carries its filter.
        t.setState(0x1, 7);
        t.setRecording(true);
        t.quad(verts, verts + 32, verts + 64, verts + 96);
        t.setState(0x7, 0);
        t.quad(verts, verts + 32, verts + 64, verts + 96);
        const ThrashFrame frame = t.takeFrame();
        CHECK(frame.batches.size() == 2 && frame.batches[0].filter == 1 && frame.batches[1].filter == 0);
    }
    {
        // Without it, batches leave the filter to the page.
        NativeThrash t(cpu);
        setUp(t);
        t.setRecording(true);
        t.quad(verts, verts + 32, verts + 64, verts + 96);
        const ThrashFrame frame = t.takeFrame();
        CHECK(frame.batches.size() == 1 && frame.batches[0].filter == -1);
    }
}


// THRASH's blend modes in the software rasterizer, as the GPU's drawing
// (thrash_gpu.cpp) and softtria do them (thrash.md, Blend modes): while
// blending is on (state 0xa) state 0x68's mode applies to opaque pixels
// too, so an added pixel of alpha 255 is added in full; with blending off,
// or in mode 0, an opaque pixel replaces what's there.
void testThrashBlend() {
    x86::Cpu cpu(1u << 21);
    const uint32_t target = 0x10000, verts = 0x1000;
    NativeThrash t(cpu);
    t.setTarget(target, 640 * 2, 640, 480);
    t.setState(0x5, 0); // no dither: exact 5:6:5 steps
    // A triangle over pixel (10, 10) in one colour; the pixel's red, of 31.
    auto draw = [&](uint32_t argb) {
        const float at[3][2] = {{0, 0}, {40, 0}, {0, 40}};
        for (int i = 0; i < 3; ++i) {
            const float f[4] = {at[i][0], at[i][1], 0.5f, 1};
            std::memcpy(cpu.memory().data() + verts + 32 * i, f, 16);
            std::memcpy(cpu.memory().data() + verts + 32 * i + 16, &argb, 4);
        }
        t.triangle(verts, verts + 32, verts + 64);
        uint16_t p;
        std::memcpy(&p, cpu.memory().data() + target + 10 * 1280 + 10 * 2, 2);
        return int(p >> 11);
    };
    auto base = [&] {
        t.setState(0xa, 0);
        return draw(0xff404040u); // red 64: 8 of 31 (64 >> 3), read back as 65
    };
    CHECK(base() == 8);
    // Added, opaque: 65 + 64 = 129, 16 of 31 (the old drawing replaced it: 8).
    t.setState(0xa, 2);
    t.setState(0x68, 1);
    CHECK(draw(0xff404040u) == 16);
    // Added, alpha 128: 65 + 32 = 97, 12 of 31.
    CHECK(base() == 8);
    t.setState(0xa, 2);
    CHECK(draw(0x80404040u) == 12);
    // Mode 2 (zero, inverse source alpha) at alpha 255: black.
    CHECK(base() == 8);
    t.setState(0xa, 2);
    t.setState(0x68, 2);
    CHECK(draw(0xff404040u) == 0);
    // Blending off: an opaque pixel replaces, whatever 0x68 says.
    CHECK(base() == 8);
    t.setState(0x68, 1);
    CHECK(draw(0xffc0c0c0u) == 24);
    // Mode 0, on: opaque replaces, translucent mixes.
    t.setState(0xa, 2);
    t.setState(0x68, 0);
    CHECK(draw(0xff404040u) == 8);
}



// DirectInput effects as the game would set them, in a little memory of
// their own: DIEFFECT read, and what playing effects add up to.
void testForceEffects() {
    std::map<uint32_t, uint32_t> memory;
    auto read32 = [&](uint32_t a) { return memory.count(a) ? memory[a] : 0u; };
    // A DIEFFECT at 0x100 (56 bytes): object offsets, cartesian, half a
    // second, gain 8000, on X alone, coming from -X; its DICONSTANTFORCE at
    // 0x200 and an envelope at 0x300.
    auto effect = [&](uint32_t flags, uint32_t axes, uint32_t typeBytes) {
        memory[0x100] = 56;
        memory[0x104] = flags;
        memory[0x108] = 500000;
        memory[0x110] = 8000;
        memory[0x11c] = axes;
        memory[0x120] = 0x180; // rgdwAxes
        memory[0x124] = 0x190; // rglDirection
        memory[0x128] = 0;
        memory[0x12c] = typeBytes;
        memory[0x130] = 0x200;
        memory[0x134] = 0;
    };
    effect(0x2 | 0x10, 1, 4);
    memory[0x180] = 0;            // X, by offset
    memory[0x190] = uint32_t(-1); // from -X
    memory[0x200] = 5000;         // magnitude
    DiEffect push;
    CHECK(DiEffect::kindOf(0x13541c20, push.kind) && push.kind == DiEffect::Constant);
    CHECK(!DiEffect::kindOf(0x6f1d2b60, push.kind));
    push.set(read32, 0x100, 0x3ff);
    CHECK(push.durationUs == 500000 && push.gain == 8000 && push.xAxis == 0 && push.magnitude == 5000);
    CHECK(push.alongX() == -1.0f);
    // Not playing: nothing. Playing, with the device at half gain:
    // 5000/10000 x 0.8 x 0.5, from -X.
    CHECK(sumForces({&push}, 10000, 1000).constant == 0);
    push.playing = true;
    push.startedMs = 1000;
    GamepadForce f = sumForces({&push}, 5000, 1200);
    CHECK(std::fabs(f.constant + 0.2f) < 1e-4f);
    // Done after its half second (one iteration); for ever with infinite.
    CHECK(sumForces({&push}, 10000, 1500).constant == 0);
    push.iterations = DiEffect::kInfinite;
    CHECK(sumForces({&push}, 10000, 9000).constant != 0);
    push.iterations = 1;

    // An envelope: attack from 0 over 0.1 s, so halfway through it half.
    memory[0x128] = 0x300;
    memory[0x300] = 20;
    memory[0x304] = 0;
    memory[0x308] = 100000;
    push.set(read32, 0x100, 0x80); // DIEP_ENVELOPE alone
    CHECK(push.envelope && push.attackUs == 100000);
    f = sumForces({&push}, 10000, 1050);
    CHECK(std::fabs(f.constant + 0.2f) < 1e-3f); // -(5000/2)/10000 x 0.8

    // Two axes, polar, from the east (90 degrees): all of it along +X.
    effect(0x2 | 0x20, 2, 4);
    memory[0x180] = 0;
    memory[0x184] = 4;
    memory[0x190] = 9000;
    DiEffect polar;
    polar.set(read32, 0x100, 0x3ff);
    CHECK(polar.axisCount == 2 && std::fabs(polar.alongX() - 1.0f) < 1e-4f);
    // On Y alone: none of it along X.
    effect(0x2 | 0x10, 1, 4);
    memory[0x180] = 4;
    DiEffect onY;
    onY.set(read32, 0x100, 0x3ff);
    CHECK(onY.xAxis == -1 && onY.alongX() == 0);

    // A sine (DIPERIODIC: magnitude, offset, phase, period in us) and a
    // spring (DICONDITION), as the game makes them for the engine and the
    // wheel's centring.
    effect(0x2 | 0x10, 1, 16);
    memory[0x180] = 0;
    memory[0x190] = 1;
    memory[0x200] = 3000;
    memory[0x204] = 0;
    memory[0x20c] = 83000;
    DiEffect sine;
    sine.kind = DiEffect::Sine;
    sine.set(read32, 0x100, 0x3ff);
    sine.playing = true;
    sine.durationUs = DiEffect::kInfinite;
    effect(0x2 | 0x10, 1, 24);
    memory[0x200] = 1000; // offset
    memory[0x204] = 7000; // + coefficient
    memory[0x208] = 7000; // - coefficient
    memory[0x20c] = 9500; // + saturation
    memory[0x210] = 9500;
    DiEffect spring;
    spring.kind = DiEffect::Spring;
    spring.set(read32, 0x100, 0x3ff);
    spring.playing = true;
    spring.durationUs = DiEffect::kInfinite;
    f = sumForces({&sine, &spring}, 10000, 5000);
    CHECK(std::fabs(f.vibration - 0.24f) < 1e-4f && f.vibrationPeriodMs == 83 && !f.vibrationSquare);
    CHECK(std::fabs(f.spring - 0.56f) < 1e-4f && std::fabs(f.springCentre - 0.1f) < 1e-4f &&
          std::fabs(f.springSaturation - 0.95f) < 1e-4f);
    CHECK(f.constant == 0 && f.damper == 0);
    // SetParameters with only the type-specific part keeps the rest.
    memory[0x204] = 2000;
    memory[0x208] = 2000;
    memory[0x108] = 1;
    spring.set(read32, 0x100, 0x100);
    CHECK(spring.durationUs == DiEffect::kInfinite && spring.positiveCoefficient == 2000);
}


// DirectSound 3D's model (sound3d.h): distance, pan, cone and Doppler.
void testSound3d() {
    auto near = [](double a, double b) { return std::fabs(a - b) < 1e-4; };
    Ds3dListener listener;
    Ds3dSource s;
    // Ahead, within the minimum distance: as it is.
    s.position[2] = 0.5f;
    Ds3dResult r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 1) && near(r.doppler, 1));
    // Ahead at 2 and 4 times the minimum distance: -6 and -12 dB or so;
    // twice the rolloff, faster; past the maximum distance, no quieter.
    s.position[2] = 2;
    CHECK(near(spatialize(listener, s).left, 0.5));
    s.position[2] = 4;
    CHECK(near(spatialize(listener, s).left, 0.25));
    listener.rolloffFactor = 2;
    CHECK(near(spatialize(listener, s).left, 1.0 / 7));
    listener.rolloffFactor = 1;
    s.maxDistance = 2;
    CHECK(near(spatialize(listener, s).left, 0.5));
    s.maxDistance = 1e9f;
    // Straight to the right: the left speaker 20 dB down; to the left, the
    // other way round; 30 degrees right, the left down by 10 dB.
    s.position[0] = 1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.right, 1) && near(r.left, 0.1));
    s.position[0] = -1;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 0.1));
    s.position[0] = 0.5f;
    s.position[2] = std::sqrt(0.75f);
    r = spatialize(listener, s);
    CHECK(near(r.right, 1) && near(r.left, std::pow(10.0, -0.5)));
    // The listener turned to face +x: a source at +x is ahead; one at +z is
    // now to its left.
    listener.front[0] = 1;
    listener.front[2] = 0;
    s.position[0] = 1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 1));
    s.position[0] = 0;
    s.position[2] = 1;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 0.1));
    // Straight behind (-x for this listener): both speakers 3 dB down and
    // muffled to 4 kHz; beside, neither.
    s.position[0] = -1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.behind, 1) && near(r.left, std::pow(10.0, -0.15)) && near(r.right, r.left));
    CHECK(near(rearCutoff(r.behind), 4000) && near(rearCutoff(0), 20000) && near(rearGain(0), 1));
    CHECK(near(lowPassCoefficient(4000, 44100), 1 - std::exp(-2 * 3.14159265358979 * 4000 / 44100)));
    // Head-relative: the position is the listener's own, whichever way it
    // faces; disabled: as it is, wherever.
    s.mode = 1;
    s.position[0] = 1;
    s.position[2] = 0;
    r = spatialize(listener, s);
    CHECK(near(r.right, 1) && near(r.left, 0.1));
    s.mode = 2;
    s.position[0] = 100;
    r = spatialize(listener, s);
    CHECK(near(r.left, 1) && near(r.right, 1));
    s = Ds3dSource();
    listener = Ds3dListener();
    // A cone pointing away (-z) from a listener at the origin, the source
    // ahead of it at z 1: outside, so -20 dB.
    s.position[2] = 1;
    s.insideCone = 90;
    s.outsideCone = 180;
    s.coneOrientation[2] = 1;
    s.coneOutsideVolume = -2000;
    CHECK(near(spatialize(listener, s).left, 0.1));
    s.coneOrientation[2] = -1; // pointing at the listener: inside
    CHECK(near(spatialize(listener, s).left, 1));
    s = Ds3dSource();
    // Doppler: a source 10 units ahead coming at the listener at a tenth of
    // the speed of sound sounds higher, 1 / 0.9; going away, lower.
    s.position[2] = 10;
    s.velocity[2] = -34.33f;
    CHECK(near(spatialize(listener, s).doppler, 1 / 0.9));
    s.velocity[2] = 34.33f;
    CHECK(near(spatialize(listener, s).doppler, 1 / 1.1));
    listener.dopplerFactor = 0;
    CHECK(near(spatialize(listener, s).doppler, 1));
}

} // namespace

static const reluva::test::Suite kSuite("nfs.unit.host", {
    {"force effects", [](const stdfs::path &) { testForceEffects(); }},
    {"sound3d", [](const stdfs::path &) { testSound3d(); }},
    {"thrash fog", [](const stdfs::path &) { testThrashFog(); }},
    {"thrash direct3d", [](const stdfs::path &) { testThrashDirect3D(); }},
    {"thrash blend", [](const stdfs::path &) { testThrashBlend(); }},
});
