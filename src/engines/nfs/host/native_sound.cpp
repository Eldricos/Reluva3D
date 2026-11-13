// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The game's software sound mixer (iSND, linked into nfs3.exe) run
// natively: its hand-written x87 loops took about a quarter of a race's
// instructions (docs/engines/nfs/games/nfs3/PLAN.md, Native sound mixer). Each is worked
// out from the retail code and replaced only where its bytes are exactly
// those (GameHost::replaceRoutine). They compute as the interpreter does
// the x87's (doubles, rounded to float where the code stores one), in the
// same order, so the sound is the same to the bit; `nfs native-check`
// runs each against its original on random inputs to make sure.
//
// Each routine is cdecl (arguments on the stack after the return address)
// and keeps every register as it was (PUSHA/POPA), but the conversion to
// 16 bits, which leaves EAX, ECX and EDX as its loop does.

#include "engines/nfs/host/game_host.h"

#include <bit>
#include <cstring>

namespace reluva::nfs {

namespace {

using x86::Cpu;

float loadFloat(Cpu &c, uint32_t at) {
    return std::bit_cast<float>(c.read32(at));
}
void storeFloat(Cpu &c, uint32_t at, double v) {
    c.write32(at, std::bit_cast<uint32_t>(float(v)));
}
uint32_t arg(Cpu &c, int n) {
    return c.read32(c.reg[Cpu::ESP] + 4 + 4 * uint32_t(n));
}

// The resamplers' state: a source position (whole samples, *posAt) and its
// fraction (16.16, *fracAt), stepped by stepInt.stepFrac each sample.
// Between two samples a and b, the result is a*(1-t) + b*t, t the
// fraction's top 16 bits times a scale (1/65536). The code takes those bits
// with FILD from a scratch dword two bytes into the fraction's copy, so
// its top half is the word after it, read as it is.
struct Resampler {
    uint32_t scratch, oneAt, scaleAt; // the routine's globals
    uint32_t frameBytes;              // 8 stereo, 4 mono
};

// resample(count, src, dst, posAt, fracAt, stepInt, stepFrac): float
// samples, stereo (two floats a frame) or mono.
uint32_t resample(Cpu &c, const Resampler &r, bool stereo) {
    const int32_t count = int32_t(arg(c, 0));
    const uint32_t src = arg(c, 1), posAt = arg(c, 3), fracAt = arg(c, 4), stepInt = arg(c, 5), stepFrac = arg(c, 6);
    uint32_t dst = arg(c, 2), pos = c.read32(posAt), frac = c.read32(fracAt);
    const uint32_t high = uint32_t(c.read16(r.scratch + 4)) << 16;
    const double one = loadFloat(c, r.oneAt), scale = loadFloat(c, r.scaleAt);
    auto step = [&] {
        const uint64_t sum = uint64_t(frac) + stepFrac;
        frac = uint32_t(sum);
        pos += stepInt + uint32_t(sum >> 32);
    };
    if (stereo) {
        // At least one frame, as the code always makes its first.
        int32_t left = count;
        do {
            const double t = double(int32_t(high | frac >> 16)) * scale, rest = one - t;
            const uint32_t at = src + pos * 8;
            const double a0 = loadFloat(c, at), b0 = loadFloat(c, at + 4), a1 = loadFloat(c, at + 8),
                         b1 = loadFloat(c, at + 12);
            storeFloat(c, dst, a1 * t + a0 * rest);
            storeFloat(c, dst + 4, b0 * rest + b1 * t);
            dst += 8;
            step();
        } while (--left > 0);
    } else {
        for (int32_t i = 0; i < count; ++i) {
            const double t = double(int32_t(high | frac >> 16)) * scale, rest = one - t;
            const uint32_t at = src + pos * 4;
            storeFloat(c, dst, t * double(loadFloat(c, at + 4)) + rest * double(loadFloat(c, at)));
            dst += 4;
            step();
        }
    }
    c.write32(r.scratch, frac);
    c.write32(fracAt, frac);
    c.write32(posAt, pos);
    return 0;
}

// mix(gains, count, src, dst): float samples added into a stereo float
// mix, each side the source's (mono: the one sample; stereo: its side)
// times that side's gain (gains[0] left, gains[1] right), or as it is
// (full volume), or not at all (silent).
enum class Side { Off, Full, Scaled };
// What a call added, for GameHost::logVoices: the energy to L+R and L-R,
// and whether its two gains have opposite signs.
struct Added {
    double c = 0, s = 0;
    bool rear = false;
};
uint32_t mix(Cpu &c, bool stereoSource, Side left, Side right, Added *added = nullptr) {
    const uint32_t gains = arg(c, 0), src = arg(c, 2), dst = arg(c, 3);
    const int32_t count = int32_t(arg(c, 1));
    const double gainLeft = loadFloat(c, gains), gainRight = loadFloat(c, gains + 4);
    const uint32_t stride = stereoSource ? 8 : 4, rightOffset = stereoSource ? 4 : 0;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t from = src + uint32_t(i) * stride, to = dst + uint32_t(i) * 8;
        double l = 0, r = 0;
        if (left != Side::Off) {
            const double s = loadFloat(c, from);
            l = left == Side::Scaled ? s * gainLeft : s;
            storeFloat(c, to, l + double(loadFloat(c, to)));
        }
        if (right != Side::Off) {
            const double s = loadFloat(c, from + rightOffset);
            r = right == Side::Scaled ? s * gainRight : s;
            storeFloat(c, to + 4, r + double(loadFloat(c, to + 4)));
        }
        if (added) {
            added->c += (l + r) * (l + r);
            added->s += (l - r) * (l - r);
        }
    }
    if (added)
        added->rear = (left == Side::Scaled && gainLeft < 0) != (right == Side::Scaled && gainRight < 0) &&
                      left != Side::Off && right != Side::Off;
    return 0;
}

// toInt16(frames, src, dst): the stereo float mix as 16-bit samples. Each
// float has 1.5 * 2^23 (a double at magicAt) added and is stored back over
// itself, its low mantissa bits then the sample rounded to the nearest;
// past 16 bits it's clipped to 32767 or -32768.
uint32_t toInt16(Cpu &c, uint32_t magicAt) {
    uint64_t magicBits = c.read32(magicAt) | uint64_t(c.read32(magicAt + 4)) << 32;
    const double magic = std::bit_cast<double>(magicBits);
    uint32_t p = arg(c, 1), dst = arg(c, 2);
    const uint32_t end = arg(c, 0) * 8 + p;
    uint32_t low = p; // ECX as the loop leaves it
    while (p < end) {
        const float sum = float(double(loadFloat(c, p)) + magic);
        const uint32_t bits = std::bit_cast<uint32_t>(sum);
        c.write32(p, bits);
        low = bits & 0xfffff;
        p += 4;
        uint16_t sample;
        if (low <= 0x7fff || low >= 0xf8000)
            sample = uint16_t(low);
        else
            sample = low >= 0x80000 ? 0x8000 : 0x7fff;
        c.write16(dst, sample);
        dst += 2;
    }
    c.reg[Cpu::EAX] = p;
    c.reg[Cpu::ECX] = low;
    c.reg[Cpu::EDX] = dst;
    return 0;
}

} // namespace

void GameHost::addNativeSound() {
    // The resamplers (float, linear between samples): stereo and mono.
    static const Resampler stereo{0x56b138, 0x56b130, 0x56b134, 8}, mono{0x56b148, 0x56b140, 0x56b144, 4};
    replaceRoutine(0x527330, 142, 0x3c2833f2244228b1ull, "nfs3.exe resample stereo",
                   [](Cpu &c) { return resample(c, stereo, true); });
    replaceRoutine(0x5273c0, 239, 0x274025e0e91db35full, "nfs3.exe resample mono",
                   [](Cpu &c) { return resample(c, mono, false); });

    // The mixers, one for each pair of levels (0, 127 or between), for mono
    // sources and for stereo ones; each set chosen as a voice's levels
    // change (0x520a5e, 0x520e1e). Those for silence on both sides do nothing
    // already.
    struct Mixer {
        uint32_t address, size;
        uint64_t hash;
        const char *name;
        bool stereoSource;
        Side left, right;
    };
    static const Mixer mixers[] = {
        {0x520aee, 144, 0x5379f527a623ac77ull, "nfs3.exe mix mono", false, Side::Scaled, Side::Scaled},
        {0x520b7e, 111, 0x694a4624bd0854e9ull, "nfs3.exe mix mono full", false, Side::Full, Side::Full},
        {0x520bed, 124, 0xef4603cd37825d83ull, "nfs3.exe mix mono left", false, Side::Scaled, Side::Off},
        {0x520c69, 125, 0x4ddd4f39984ea84full, "nfs3.exe mix mono right", false, Side::Off, Side::Scaled},
        {0x520ce6, 133, 0x42dea07e5a23af60ull, "nfs3.exe mix mono, right full", false, Side::Scaled, Side::Full},
        {0x520d6b, 134, 0x1aa08d1458d01e13ull, "nfs3.exe mix mono, left full", false, Side::Full, Side::Scaled},
        {0x520eae, 146, 0x508f42791fb53867ull, "nfs3.exe mix stereo", true, Side::Scaled, Side::Scaled},
        {0x520f40, 113, 0x09c53eab834a798dull, "nfs3.exe mix stereo full", true, Side::Full, Side::Full},
        {0x520fb1, 124, 0x2a4f0816567258afull, "nfs3.exe mix stereo left", true, Side::Scaled, Side::Off},
        {0x52102d, 127, 0x3131c83dfd33bbb3ull, "nfs3.exe mix stereo right", true, Side::Off, Side::Scaled},
        {0x5210ac, 135, 0xc7c1113b379b9734ull, "nfs3.exe mix stereo, right full", true, Side::Scaled, Side::Full},
        {0x521133, 136, 0x6a5f84d912e41e83ull, "nfs3.exe mix stereo, left full", true, Side::Full, Side::Scaled},
    };
    for (const Mixer &m : mixers) {
        const bool stereoSource = m.stereoSource;
        const Side left = m.left, right = m.right;
        replaceRoutine(m.address, m.size, m.hash, m.name, [this, stereoSource, left, right](Cpu &c) {
            if (!voiceLog_)
                return mix(c, stereoSource, left, right);
            Added added;
            const uint32_t pop = mix(c, stereoSource, left, right, &added);
            voiceMixed(added.rear, added.c, added.s, 1);
            return pop;
        });
    }

    // The mix to 16 bits.
    replaceRoutine(0x521d00, 115, 0x25d2ab7032489a2full, "nfs3.exe mix to 16 bits",
                   [](Cpu &c) { return toInt16(c, 0x55188c); });
}

void GameHost::logVoices(std::vector<VoiceEnergy> *to, uint32_t windowMs) {
    voiceLog_ = to;
    voiceWindowMs_ = windowMs ? windowMs : 100;
    if (to && options_.mmx && !mmxVoicesHooked_)
        hookMmxMixers();
}

// With MMX the game mixes with its integer routines, in the interpreter
// (bnk.md, The MMX path), so the native mixers above never run. For
// -voices the two 16-bit mixers (0x51f7ad mono, 0x51f897 stereo; the
// 8-bit ones play no retail sound) get a native function at their entry
// that measures what the call will add and then does the routine's first
// four instructions (push ebp; mov ebp, esp; pushad; mov esi, [ebp+8]),
// whose bytes it replaces, and goes on at the fifth: the mix itself is
// still the game's. Put in only when asked for, so a run without -voices
// runs as before.
//
// mix(levels, count, src, dst): levels as words L x 256 (mono: four of
// the left's, then four of the right's; stereo: left, right, left,
// right), src 16-bit samples (count of them, or count frames), dst the
// stereo 16-bit bus, which the mix down doubles. What a frame adds is
// taken as pmulhw gives it, (L x 256 x s) >> 16, doubled, so in the same
// units as the float path's; the bus's saturation is left out.
void GameHost::hookMmxMixers() {
    static const uint8_t prologue[7] = {0x55, 0x8b, 0xec, 0x60, 0x8b, 0x75, 0x08};
    struct Mixer {
        uint32_t address;
        const char *name;
        bool stereoSource;
    };
    static const Mixer mixers[] = {{0x51f7ad, "nfs3.exe MMX mix mono (voices)", false},
                                   {0x51f897, "nfs3.exe MMX mix stereo (voices)", true}};
    for (const Mixer &m : mixers) {
        bool same = true;
        for (uint32_t i = 0; i < sizeof prologue; ++i)
            same = same && cpu_->read8(m.address + i) == prologue[i];
        if (!same)
            continue;
        const uint32_t address = m.address;
        const bool stereoSource = m.stereoSource;
        const uint32_t native = cpu_->addNative(m.name, [this, address, stereoSource](Cpu &c) {
            if (voiceLog_) {
                const uint32_t levels = arg(c, 0), src = arg(c, 2);
                const int32_t count = int32_t(arg(c, 1));
                const int32_t left = int16_t(c.read16(levels)),
                              right = int16_t(c.read16(levels + (stereoSource ? 2 : 8)));
                Added added;
                for (int32_t i = 0; i < count; ++i) {
                    const int32_t sl = int16_t(c.read16(src + uint32_t(i) * (stereoSource ? 4 : 2)));
                    const int32_t sr = stereoSource ? int16_t(c.read16(src + uint32_t(i) * 4 + 2)) : sl;
                    const double l = 2.0 * double((left * sl) >> 16), r = 2.0 * double((right * sr) >> 16);
                    added.c += (l + r) * (l + r);
                    added.s += (l - r) * (l - r);
                }
                added.rear = left != 0 && right != 0 && (left < 0) != (right < 0);
                voiceMixed(added.rear, added.c, added.s, 1);
            }
            // The routine's first instructions, then on from the fifth.
            auto push = [&c](uint32_t v) {
                c.reg[Cpu::ESP] -= 4;
                c.write32(c.reg[Cpu::ESP], v);
            };
            push(c.reg[Cpu::EBP]);
            c.reg[Cpu::EBP] = c.reg[Cpu::ESP];
            const uint32_t esp = c.reg[Cpu::ESP];
            for (int r : {Cpu::EAX, Cpu::ECX, Cpu::EDX, Cpu::EBX})
                push(c.reg[r]);
            push(esp);
            for (int r : {Cpu::EBP, Cpu::ESI, Cpu::EDI})
                push(c.reg[r]);
            c.reg[Cpu::ESI] = c.read32(c.reg[Cpu::EBP] + 8);
            push(address + sizeof prologue); // where the native's return goes
            return 0u;
        });
        cpu_->write8(address, 0xe9); // JMP rel32
        cpu_->write32(address + 1, native - (address + 5));
        cpu_->write8(address + 5, 0x90);
        cpu_->write8(address + 6, 0x90);
        mmxVoicesHooked_ = true;
    }
}

void GameHost::voiceMixed(bool rear, double c, double s, uint32_t calls) {
    const uint64_t window = process_->now() / voiceWindowMs_ * voiceWindowMs_;
    if (voiceLog_->empty() || voiceLog_->back().ms != window)
        voiceLog_->push_back({window});
    VoiceEnergy &e = voiceLog_->back();
    e.calls += calls;
    if (rear) {
        e.rearCalls += calls;
        e.rearC += c;
        e.rearS += s;
    } else {
        e.frontC += c;
        e.frontS += s;
    }
}

} // namespace reluva::nfs
