// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Written from the formats' public descriptions: the ProTracker MOD layout,
// Scream Tracker 3's technical notes, FastTracker 2's XM document, Impulse
// Tracker's ITTECH and the public write-ups of IT 2.14/2.15 sample
// compression. Every format is turned into one form (tracker.h) with the
// player's own effect codes below, which one player runs.

#include "audio/tracker.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::audio {

namespace {

constexpr size_t kMaxSampleFrames = size_t(1) << 24;
constexpr size_t kMaxTotalFrames = size_t(1) << 26;
constexpr int kMaxRows = 256, kMaxChannels = 64;
constexpr double kAmigaClock = 14317056.0; // period (x4 Amiga units) to Hz

// The player's effects; each format's letters map onto these.
enum Fx : uint8_t {
    kFxNone,
    kFxArpeggio,
    kFxPortaUp,
    kFxPortaDown,
    kFxTonePorta,
    kFxVibrato,
    kFxTonePortaVol,
    kFxVibratoVol,
    kFxTremolo,
    kFxPan,
    kFxOffset,
    kFxVolSlide,
    kFxJump,
    kFxVolume,
    kFxBreak,
    kFxSpeed,
    kFxTempo,
    kFxSpeedTempo,
    kFxGlobalVol,
    kFxGlobalVolSlide,
    kFxRetrig,
    kFxFinePortaUp,
    kFxFinePortaDown,
    kFxXFinePortaUp,
    kFxXFinePortaDown,
    kFxFineVolUp,
    kFxFineVolDown,
    kFxPatLoop,
    kFxNoteCut,
    kFxNoteDelay,
    kFxPatDelay,
    kFxKeyOff,
    kFxPanSlide,
    kFxChanVol,
    kFxChanVolSlide,
    kFxFineVibrato,
    kFxVibWave,
    kFxTremWave,
    kFxCount
};

// Volume column commands.
enum Vc : uint8_t {
    kVcNone,
    kVcVolume,
    kVcSlideUp,
    kVcSlideDown,
    kVcFineUp,
    kVcFineDown,
    kVcVibSpeed,
    kVcVibDepth,
    kVcPan,
    kVcPanLeft,
    kVcPanRight,
    kVcTonePorta,
    kVcPortaUp,
    kVcPortaDown
};

// Bounds-checked little/big-endian reads: past the end reads as 0.
struct Reader {
    const std::vector<uint8_t> &d;
    size_t size() const { return d.size(); }
    bool has(size_t at, size_t n) const { return at <= d.size() && n <= d.size() - at; }
    unsigned u8(size_t at) const { return at < d.size() ? d[at] : 0; }
    unsigned le16(size_t at) const { return u8(at) | u8(at + 1) << 8; }
    uint32_t le32(size_t at) const { return le16(at) | uint32_t(le16(at + 2)) << 16; }
    unsigned be16(size_t at) const { return u8(at) << 8 | u8(at + 1); }
    bool tag(size_t at, const char *s) const {
        const size_t n = std::strlen(s);
        return has(at, n) && std::memcmp(d.data() + at, s, n) == 0;
    }
    std::string text(size_t at, size_t n) const {
        std::string s;
        for (size_t i = 0; i < n && u8(at + i); ++i)
            s += char(u8(at + i));
        return s;
    }
};

// Reads up to `frames` PCM frames at `at`, as far as the file (and the
// decoding budget) goes: 8 or 16-bit, signed or not, plain or delta-coded.
std::vector<int16_t> readPcm(const Reader &r, size_t at, size_t frames, bool is16, bool isSigned, bool delta,
                             size_t &budget) {
    const size_t width = is16 ? 2 : 1;
    const size_t avail = at < r.size() ? (r.size() - at) / width : 0;
    frames = std::min({frames, avail, kMaxSampleFrames, budget});
    budget -= frames;
    std::vector<int16_t> out(frames);
    unsigned acc = 0;
    for (size_t i = 0; i < frames; ++i) {
        unsigned v = is16 ? r.le16(at + 2 * i) : r.u8(at + i);
        if (delta)
            v = acc = (acc + v) & (is16 ? 0xFFFFu : 0xFFu);
        if (!isSigned)
            v ^= is16 ? 0x8000u : 0x80u;
        out[i] = is16 ? int16_t(uint16_t(v)) : int16_t(int8_t(uint8_t(v)) * 256);
    }
    return out;
}

// IT 2.14 compressed samples (2.15: twice delta-coded): blocks of a 16-bit
// byte count then a little-endian bit stream of 0x8000 (8-bit) or 0x4000
// (16-bit) deltas, whose bit width changes in-band.
std::vector<int16_t> decompressIt(const Reader &r, size_t at, size_t frames, bool is16, bool it215, size_t &budget) {
    frames = std::min({frames, kMaxSampleFrames, budget});
    std::vector<int16_t> out;
    const int top = is16 ? 17 : 9, bits = is16 ? 16 : 8;
    while (out.size() < frames && r.has(at, 2)) {
        size_t bytes = r.le16(at);
        at += 2;
        bytes = std::min(bytes, r.size() - at);
        const uint8_t *p = r.d.data() + at;
        size_t bit = 0;
        at += bytes;
        auto read = [&](int n, uint32_t &v) {
            if (bit + size_t(n) > bytes * 8)
                return false;
            v = 0;
            for (int i = 0; i < n; ++i, ++bit)
                v |= uint32_t(p[bit >> 3] >> (bit & 7) & 1) << i;
            return true;
        };
        size_t count = std::min(frames - out.size(), size_t(is16 ? 0x4000 : 0x8000));
        int width = top;
        int32_t d1 = 0, d2 = 0;
        while (count) {
            uint32_t v;
            if (!read(width, v))
                break;
            if (width < 7) {
                if (v == 1u << (width - 1)) { // width change: the new width follows
                    if (!read(is16 ? 4 : 3, v))
                        break;
                    const int w = int(v) + 1;
                    width = w < width ? w : w + 1;
                    continue;
                }
            } else if (width < top) {
                const uint32_t border = ((is16 ? 0xFFFFu : 0xFFu) >> (top - width)) - (is16 ? 8 : 4);
                if (v > border && v <= border + (is16 ? 16 : 8)) {
                    const int w = int(v - border);
                    width = w < width ? w : w + 1;
                    continue;
                }
            } else if (v & (1u << (top - 1))) {
                width = int((v + 1) & 0xFF);
                if (width < 1 || width > top)
                    break;
                continue;
            }
            int32_t s;
            if (width < bits)
                s = int32_t(v << (32 - width)) >> (32 - width);
            else
                s = is16 ? int16_t(uint16_t(v)) : int8_t(uint8_t(v));
            if (is16) {
                d1 = int16_t(uint16_t(d1 + s));
                d2 = int16_t(uint16_t(d2 + d1));
                out.push_back(int16_t(it215 ? d2 : d1));
            } else {
                d1 = int8_t(uint8_t(d1 + s));
                d2 = int8_t(uint8_t(d2 + d1));
                out.push_back(int16_t((it215 ? d2 : d1) * 256));
            }
            --count;
        }
        out.resize(out.size() + count, 0); // a block cut short: silence for the rest
    }
    budget -= out.size();
    return out;
}

void fixLoops(TrackerSample &s) {
    const size_t n = s.data.size();
    s.loopEnd = std::min(s.loopEnd, n);
    s.sustainEnd = std::min(s.sustainEnd, n);
    if (s.loopStart >= s.loopEnd)
        s.loop = false;
    if (s.sustainStart >= s.sustainEnd)
        s.sustain = false;
}

void fixEnvelope(TrackerEnvelope &e) {
    const int n = int(e.points.size());
    if (n == 0)
        e.enabled = false;
    if (e.loopStart > e.loopEnd || e.loopEnd >= n)
        e.loop = false;
    if (e.sustainStart > e.sustainEnd || e.sustainEnd >= n)
        e.sustain = false;
}

TrackerPattern emptyPattern(int rows, int channels) {
    TrackerPattern p;
    p.rows = rows;
    p.cells.resize(size_t(rows) * size_t(channels));
    return p;
}

// MOD and XM effects (XM adds G..X).
void modEffect(unsigned fx, unsigned p, bool xm, TrackerCell &c) {
    uint8_t e = kFxNone;
    const unsigned x = p >> 4, y = p & 15;
    switch (fx) {
    case 0x0:
        e = p ? kFxArpeggio : kFxNone;
        break;
    case 0x1:
        e = kFxPortaUp;
        break;
    case 0x2:
        e = kFxPortaDown;
        break;
    case 0x3:
        e = kFxTonePorta;
        break;
    case 0x4:
        e = kFxVibrato;
        break;
    case 0x5:
        e = kFxTonePortaVol;
        break;
    case 0x6:
        e = kFxVibratoVol;
        break;
    case 0x7:
        e = kFxTremolo;
        break;
    case 0x8:
        e = kFxPan;
        break;
    case 0x9:
        e = kFxOffset;
        break;
    case 0xA:
        e = kFxVolSlide;
        break;
    case 0xB:
        e = kFxJump;
        break;
    case 0xC:
        e = kFxVolume, p = std::min(p, 64u);
        break;
    case 0xD:
        e = kFxBreak, p = x * 10 + y;
        break;
    case 0xE:
        p = y;
        switch (x) {
        case 0x1:
            e = kFxFinePortaUp;
            break;
        case 0x2:
            e = kFxFinePortaDown;
            break;
        case 0x4:
            e = kFxVibWave;
            break;
        case 0x6:
            e = kFxPatLoop;
            break;
        case 0x7:
            e = kFxTremWave;
            break;
        case 0x8:
            e = kFxPan, p = y * 17;
            break;
        case 0x9:
            e = kFxRetrig;
            break;
        case 0xA:
            e = kFxFineVolUp;
            break;
        case 0xB:
            e = kFxFineVolDown;
            break;
        case 0xC:
            e = kFxNoteCut;
            break;
        case 0xD:
            e = kFxNoteDelay;
            break;
        case 0xE:
            e = kFxPatDelay;
            break;
        default:
            break;
        }
        break;
    case 0xF:
        e = kFxSpeedTempo;
        break;
    case 16:
        e = xm ? kFxGlobalVol : kFxNone, p = std::min(p, 64u) * 2;
        break; // G
    case 17:
        e = xm ? kFxGlobalVolSlide : kFxNone;
        break; // H
    case 20:
        e = xm ? kFxKeyOff : kFxNone;
        break; // K
    case 25:
        e = xm ? kFxPanSlide : kFxNone;
        break; // P
    case 27:
        e = xm ? kFxRetrig : kFxNone;
        break; // R
    case 33:   // X1y / X2y: extra fine portamento
        e = !xm ? kFxNone : x == 1 ? kFxXFinePortaUp : x == 2 ? kFxXFinePortaDown : kFxNone, p = y;
        break;
    default:
        break;
    }
    c.effect = e;
    c.param = e == kFxNone ? 0 : uint8_t(p);
}

// S3M and IT effects, by letter (1 = A).
void s3mEffect(unsigned cmd, unsigned p, bool it, TrackerCell &c) {
    uint8_t e = kFxNone;
    const unsigned x = p >> 4, y = p & 15;
    switch (cmd) {
    case 1:
        e = kFxSpeed;
        break;
    case 2:
        e = kFxJump;
        break;
    case 3:
        e = kFxBreak, p = it ? p : x * 10 + y;
        break;
    case 4:
        e = kFxVolSlide;
        break;
    case 5:
        e = kFxPortaDown;
        break;
    case 6:
        e = kFxPortaUp;
        break;
    case 7:
        e = kFxTonePorta;
        break;
    case 8:
        e = kFxVibrato;
        break;
    case 10:
        e = kFxArpeggio;
        break;
    case 11:
        e = kFxVibratoVol;
        break;
    case 12:
        e = kFxTonePortaVol;
        break;
    case 13:
        e = it ? kFxChanVol : kFxNone, p = std::min(p, 64u);
        break;
    case 14:
        e = it ? kFxChanVolSlide : kFxNone;
        break;
    case 15:
        e = kFxOffset;
        break;
    case 16:
        e = it ? kFxPanSlide : kFxNone;
        break;
    case 17:
        e = kFxRetrig;
        break;
    case 18:
        e = kFxTremolo;
        break;
    case 19:
        p = y;
        switch (x) {
        case 0x3:
            e = kFxVibWave;
            break;
        case 0x4:
            e = kFxTremWave;
            break;
        case 0x8:
            e = kFxPan, p = y * 17;
            break;
        case 0xB:
            e = kFxPatLoop;
            break;
        case 0xC:
            e = kFxNoteCut;
            break;
        case 0xD:
            e = kFxNoteDelay;
            break;
        case 0xE:
            e = kFxPatDelay;
            break;
        default:
            break;
        }
        break;
    case 20:
        e = kFxTempo;
        break;
    case 21:
        e = kFxFineVibrato;
        break;
    case 22:
        e = kFxGlobalVol, p = it ? std::min(p, 128u) : std::min(p, 64u) * 2;
        break;
    case 23:
        e = it ? kFxGlobalVolSlide : kFxNone;
        break;
    case 24:
        e = kFxPan, p = it ? p : p == 0x80 ? 255 : p > 0x80 ? 128 : p * 2;
        break;
    default:
        break;
    }
    c.effect = e;
    c.param = e == kFxNone ? 0 : uint8_t(p);
}

void xmVolume(unsigned v, TrackerCell &c) {
    const unsigned x = v & 15;
    uint8_t cmd = kVcNone, p = uint8_t(x);
    if (v >= 0x10 && v <= 0x50)
        cmd = kVcVolume, p = uint8_t(v - 0x10);
    else
        switch (v >> 4) {
        case 0x6:
            cmd = kVcSlideDown;
            break;
        case 0x7:
            cmd = kVcSlideUp;
            break;
        case 0x8:
            cmd = kVcFineDown;
            break;
        case 0x9:
            cmd = kVcFineUp;
            break;
        case 0xA:
            cmd = kVcVibSpeed;
            break;
        case 0xB:
            cmd = kVcVibDepth;
            break;
        case 0xC:
            cmd = kVcPan, p = uint8_t(x * 17);
            break;
        case 0xD:
            cmd = kVcPanLeft;
            break;
        case 0xE:
            cmd = kVcPanRight;
            break;
        case 0xF:
            cmd = kVcTonePorta, p = uint8_t(x << 4);
            break;
        default:
            break;
        }
    c.volCmd = cmd;
    c.volParam = cmd == kVcNone ? 0 : p;
}

void itVolume(unsigned v, TrackerCell &c) {
    static const uint8_t kPorta[10] = {0, 1, 4, 8, 16, 32, 64, 96, 128, 255};
    uint8_t cmd = kVcNone;
    unsigned p = 0;
    if (v <= 64)
        cmd = kVcVolume, p = v;
    else if (v <= 74)
        cmd = kVcFineUp, p = v - 65;
    else if (v <= 84)
        cmd = kVcFineDown, p = v - 75;
    else if (v <= 94)
        cmd = kVcSlideUp, p = v - 85;
    else if (v <= 104)
        cmd = kVcSlideDown, p = v - 95;
    else if (v <= 114)
        cmd = kVcPortaDown, p = (v - 105) * 4;
    else if (v <= 124)
        cmd = kVcPortaUp, p = (v - 115) * 4;
    else if (v >= 128 && v <= 192)
        cmd = kVcPan, p = std::min((v - 128) * 4, 255u);
    else if (v >= 193 && v <= 202)
        cmd = kVcTonePorta, p = kPorta[v - 193];
    else if (v >= 203 && v <= 212)
        cmd = kVcVibDepth, p = v - 203;
    c.volCmd = cmd;
    c.volParam = uint8_t(p);
}

// --- MOD ---------------------------------------------------------------------

int modChannels(const Reader &r) {
    if (!r.has(1080, 4))
        return 0;
    const std::string s = r.text(1080, 4);
    auto digit = [&](size_t i) { return s.size() > i && s[i] >= '0' && s[i] <= '9'; };
    if (s == "M.K." || s == "M!K!" || s == "M&K!" || s == "FLT4" || s == "4CHN")
        return 4;
    if (s == "FLT8" || s == "CD81" || s == "OKTA" || s == "OCTA")
        return 8;
    if (digit(0) && s.substr(1) == "CHN")
        return s[0] - '0';
    if (digit(0) && digit(1) && s.substr(2) == "CH")
        return (s[0] - '0') * 10 + s[1] - '0';
    if (s.substr(0, 3) == "TDZ" && digit(3))
        return s[3] - '0';
    return 0;
}

Error loadMod(const Reader &r, TrackerModule &m) {
    const int channels = modChannels(r);
    if (channels < 1 || channels > 32)
        return Error::fail("not a tracker module");
    m.format = TrackerFormat::kMOD;
    m.title = r.text(0, 20);
    m.channels = channels;
    const unsigned songLength = r.u8(950);
    if (songLength < 1 || songLength > 128)
        return Error::fail("MOD with a song of " + std::to_string(songLength) + " orders");
    m.restartOrder = int(r.u8(951)) < int(songLength) ? int(r.u8(951)) : 0;
    unsigned patterns = 0;
    for (unsigned i = 0; i < 128; ++i)
        patterns = std::max(patterns, r.u8(952 + i) + 1);
    for (unsigned i = 0; i < songLength; ++i)
        m.orders.push_back(uint8_t(r.u8(952 + i)));
    const size_t patternBytes = size_t(64) * size_t(channels) * 4;
    if (!r.has(1084, patterns * patternBytes))
        return Error::fail("MOD cut short in its patterns");
    for (unsigned p = 0; p < patterns; ++p) {
        TrackerPattern pat = emptyPattern(64, channels);
        for (size_t i = 0; i < pat.cells.size(); ++i) {
            const size_t at = 1084 + p * patternBytes + i * 4;
            TrackerCell &c = pat.cells[i];
            const unsigned b0 = r.u8(at), b2 = r.u8(at + 2);
            const unsigned period = (b0 & 15) << 8 | r.u8(at + 1);
            c.instrument = uint8_t((b0 & 0xF0) | b2 >> 4);
            if (period)
                c.note = uint8_t(std::clamp(61 + int(std::lround(12 * std::log2(428.0 / period))), 1, 120));
            modEffect(b2 & 15, r.u8(at + 3), false, c);
        }
        m.patterns.push_back(std::move(pat));
    }
    size_t at = 1084 + patterns * patternBytes, budget = kMaxTotalFrames;
    for (int i = 0; i < 31; ++i) {
        const size_t h = 20 + size_t(i) * 30;
        TrackerSample s;
        const size_t length = size_t(r.be16(h + 22)) * 2;
        const int finetune = int(r.u8(h + 24) & 7) - int(r.u8(h + 24) & 8);
        s.c5speed = 8363.0 * std::exp2(finetune / 96.0);
        s.volume = int(std::min(r.u8(h + 25), 64u));
        s.loopStart = size_t(r.be16(h + 26)) * 2;
        const size_t loopLength = size_t(r.be16(h + 28)) * 2;
        s.loopEnd = s.loopStart + loopLength;
        s.loop = loopLength > 2;
        s.data = readPcm(r, at, length, false, true, false, budget);
        at += length;
        fixLoops(s);
        m.samples.push_back(std::move(s));
    }
    for (int c = 0; c < channels; ++c) // Amiga: channels left, right, right, left
        m.channelPan.push_back((c & 3) == 0 || (c & 3) == 3 ? 0 : 255);
    m.channelVolume.assign(size_t(channels), 64);
    return Error::success();
}

// --- S3M ---------------------------------------------------------------------

Error loadS3m(const Reader &r, TrackerModule &m) {
    if (!r.has(0, 0x60))
        return Error::fail("S3M header cut short");
    const unsigned orders = r.le16(0x20), samples = r.le16(0x22), patterns = r.le16(0x24);
    const unsigned format = r.le16(0x2A);
    if (orders > 256 || samples > 256 || patterns > 256)
        return Error::fail("S3M with too many orders, samples or patterns");
    if (!r.has(0x60, orders + 2 * (samples + patterns)))
        return Error::fail("S3M cut short in its order list");
    m.format = TrackerFormat::kS3M;
    m.title = r.text(0, 28);
    m.s3mEffects = m.effectMemory = m.orderMarkers = true;
    m.speed = r.u8(0x31) ? int(r.u8(0x31)) : 6;
    m.tempo = r.u8(0x32) >= 32 ? int(r.u8(0x32)) : 125;
    m.globalVolume = int(std::min(r.u8(0x30), 64u)) * 2;
    const bool stereo = r.u8(0x33) & 0x80;
    m.mixVolume = std::clamp(int(r.u8(0x33) & 0x7F), 16, 127);
    for (unsigned i = 0; i < orders; ++i)
        m.orders.push_back(uint8_t(r.u8(0x60 + i)));
    const size_t samplePtrs = 0x60 + orders, patternPtrs = samplePtrs + 2 * samples;
    const size_t panTable = patternPtrs + 2 * patterns;

    // Channels: 0-7 left, 8-15 right, others off; the pan table may refine.
    std::vector<bool> enabled(32);
    for (int c = 0; c < 32; ++c) {
        const unsigned s = r.u8(0x40 + size_t(c));
        enabled[size_t(c)] = s < 16;
        if (s < 16)
            m.channels = c + 1;
    }
    if (m.channels == 0)
        return Error::fail("S3M without channels");
    for (int c = 0; c < m.channels; ++c) {
        int pan = r.u8(0x40 + size_t(c)) < 8 ? 3 * 17 : 12 * 17;
        if (r.u8(0x35) == 252 && (r.u8(panTable + size_t(c)) & 0x20))
            pan = int(r.u8(panTable + size_t(c)) & 15) * 17;
        m.channelPan.push_back(uint8_t(stereo ? pan : 128));
    }
    m.channelVolume.assign(size_t(m.channels), 64);

    size_t budget = kMaxTotalFrames;
    for (unsigned i = 0; i < samples; ++i) {
        const size_t h = size_t(r.le16(samplePtrs + 2 * i)) * 16;
        TrackerSample s;
        if (r.has(h, 0x50) && r.u8(h) == 1 && r.u8(h + 0x1E) == 0) {
            const size_t at = (size_t(r.u8(h + 13)) << 16 | r.le16(h + 14)) * 16;
            const unsigned flags = r.u8(h + 0x1F);
            s.loop = flags & 1;
            s.loopStart = r.le32(h + 0x14);
            s.loopEnd = r.le32(h + 0x18);
            s.volume = int(std::min(r.u8(h + 0x1C), 64u));
            s.c5speed = r.le32(h + 0x20) ? double(r.le32(h + 0x20)) : 8363.0;
            s.data = readPcm(r, at, r.le32(h + 0x10), flags & 4, format == 1, false, budget);
            fixLoops(s);
        }
        m.samples.push_back(std::move(s));
    }

    for (unsigned i = 0; i < patterns; ++i) {
        TrackerPattern pat = emptyPattern(64, m.channels);
        size_t at = size_t(r.le16(patternPtrs + 2 * i)) * 16;
        if (at) {
            at += 2; // packed length, which the rows end anyway
            for (int row = 0; row < 64 && at < r.size();) {
                const unsigned what = r.u8(at++);
                if (!what) {
                    ++row;
                    continue;
                }
                const int c = int(what & 31);
                TrackerCell cell;
                if (what & 32) {
                    const unsigned note = r.u8(at), ins = r.u8(at + 1);
                    at += 2;
                    if (note == 254)
                        cell.note = kNoteCut;
                    else if (note < 0x90 && (note & 15) < 12)
                        cell.note = uint8_t((note >> 4) * 12 + (note & 15) + 12 + 1);
                    cell.instrument = uint8_t(ins);
                }
                if (what & 64) {
                    const unsigned v = r.u8(at++);
                    if (v <= 64)
                        cell.volCmd = kVcVolume, cell.volParam = uint8_t(v);
                }
                if (what & 128) {
                    s3mEffect(r.u8(at), r.u8(at + 1), false, cell);
                    at += 2;
                }
                if (c < m.channels && enabled[size_t(c)])
                    pat.cells[size_t(row) * size_t(m.channels) + size_t(c)] = cell;
            }
        }
        m.patterns.push_back(std::move(pat));
    }
    return Error::success();
}

// --- XM ----------------------------------------------------------------------

void readXmEnvelope(const Reader &r, size_t points, unsigned count, unsigned sustain, unsigned loopStart,
                    unsigned loopEnd, unsigned type, TrackerEnvelope &e) {
    for (unsigned i = 0; i < std::min(count, 12u); ++i)
        e.points.push_back({int(r.le16(points + 4 * i)), int(std::min(r.le16(points + 4 * i + 2), 64u))});
    e.enabled = type & 1;
    e.sustain = type & 2;
    e.loop = type & 4;
    e.sustainStart = e.sustainEnd = int(sustain);
    e.loopStart = int(loopStart);
    e.loopEnd = int(loopEnd);
    fixEnvelope(e);
}

Error loadXm(const Reader &r, TrackerModule &m) {
    if (!r.has(0, 80 + 256))
        return Error::fail("XM header cut short");
    const unsigned songLength = r.le16(64), channels = r.le16(68), patterns = r.le16(70);
    const unsigned instruments = r.le16(72);
    if (channels < 1 || channels > unsigned(kMaxChannels) || songLength > 256 || patterns > 256 || instruments > 256)
        return Error::fail("XM with an impossible header");
    m.format = TrackerFormat::kXM;
    m.title = r.text(17, 20);
    m.channels = int(channels);
    m.effectMemory = true;
    m.linearSlides = r.le16(74) & 1;
    m.restartOrder = r.le16(66) < songLength ? int(r.le16(66)) : 0;
    m.speed = r.le16(76) ? int(std::min(r.le16(76), 255u)) : 6;
    m.tempo = r.le16(78) >= 32 ? int(std::min(r.le16(78), 255u)) : 125;
    for (unsigned i = 0; i < songLength; ++i)
        m.orders.push_back(uint8_t(r.u8(80 + i)));
    m.channelPan.assign(channels, 128);
    m.channelVolume.assign(channels, 64);

    size_t at = 60 + size_t(r.le32(60));
    for (unsigned i = 0; i < patterns; ++i) {
        if (!r.has(at, 9))
            return Error::fail("XM cut short in pattern " + std::to_string(i));
        const unsigned rows = r.le16(at + 5);
        const size_t packed = r.le16(at + 7);
        at += r.le32(at);
        if (rows < 1 || rows > unsigned(kMaxRows) || !r.has(at, packed))
            return Error::fail("XM pattern " + std::to_string(i) + " is broken");
        TrackerPattern pat = emptyPattern(int(rows), m.channels);
        size_t q = at;
        const size_t end = at + packed;
        for (size_t k = 0; packed && k < pat.cells.size() && q < end; ++k) {
            TrackerCell &c = pat.cells[k];
            unsigned flags = r.u8(q);
            if (flags & 0x80)
                ++q;
            else
                flags = 0x1F;
            const unsigned note = flags & 1 ? r.u8(q++) : 0;
            c.instrument = uint8_t(flags & 2 ? r.u8(q++) : 0);
            const unsigned vol = flags & 4 ? r.u8(q++) : 0;
            const unsigned fx = flags & 8 ? r.u8(q++) : 0;
            const unsigned param = flags & 16 ? r.u8(q++) : 0;
            c.note = note >= 1 && note <= 96 ? uint8_t(note + 12) : note == 97 ? kNoteOff : 0;
            xmVolume(vol, c);
            modEffect(fx, param, true, c);
        }
        m.patterns.push_back(std::move(pat));
        at = end;
    }

    size_t budget = kMaxTotalFrames;
    for (unsigned i = 0; i < instruments; ++i) {
        if (!r.has(at, 29))
            return Error::fail("XM cut short in instrument " + std::to_string(i + 1));
        const size_t h = at;
        const unsigned count = r.le16(h + 27);
        TrackerInstrument ins;
        for (size_t n = 0; n < 120; ++n)
            ins.note[n] = uint8_t(n);
        at = h + r.le32(h);
        if (count == 0) {
            m.instruments.push_back(std::move(ins));
            continue;
        }
        if (count > 256 || !r.has(h, 241))
            return Error::fail("XM instrument " + std::to_string(i + 1) + " is broken");
        const size_t headerSize = r.le32(h + 29) ? r.le32(h + 29) : 40;
        const size_t base = m.samples.size() + 1;
        for (size_t n = 0; n < 96; ++n)
            if (r.u8(h + 33 + n) < count)
                ins.sample[n + 12] = uint8_t(std::min<size_t>(base + r.u8(h + 33 + n), 255));
        readXmEnvelope(r, h + 129, r.u8(h + 225), r.u8(h + 227), r.u8(h + 228), r.u8(h + 229), r.u8(h + 233),
                       ins.volumeEnvelope);
        readXmEnvelope(r, h + 177, r.u8(h + 226), r.u8(h + 230), r.u8(h + 231), r.u8(h + 232), r.u8(h + 234),
                       ins.panEnvelope);
        ins.fadeout = int(r.le16(h + 239)) * 2;
        size_t data = at + count * headerSize;
        if (!r.has(at, count * headerSize))
            return Error::fail("XM cut short in instrument " + std::to_string(i + 1));
        for (unsigned s = 0; s < count; ++s) {
            const size_t sh = at + s * headerSize;
            const size_t bytes = r.le32(sh);
            const unsigned type = r.u8(sh + 14);
            const bool is16 = type & 16;
            TrackerSample smp;
            smp.loopStart = r.le32(sh + 4) / (is16 ? 2 : 1);
            smp.loopEnd = smp.loopStart + r.le32(sh + 8) / (is16 ? 2 : 1);
            smp.loop = (type & 3) != 0;
            smp.pingPong = (type & 3) == 2;
            smp.volume = int(std::min(r.u8(sh + 12), 64u));
            smp.c5speed = 8363.0 * std::exp2(int8_t(uint8_t(r.u8(sh + 13))) / (128.0 * 12));
            smp.pan = int(r.u8(sh + 15));
            smp.relativeNote = int8_t(uint8_t(r.u8(sh + 16)));
            smp.data = readPcm(r, data, bytes / (is16 ? 2 : 1), is16, true, true, budget);
            data += bytes;
            fixLoops(smp);
            m.samples.push_back(std::move(smp));
        }
        at = data;
        m.instruments.push_back(std::move(ins));
    }
    if (m.samples.size() > 255)
        return Error::fail("XM with too many samples");
    return Error::success();
}

// --- IT ----------------------------------------------------------------------

void readItEnvelope(const Reader &r, size_t at, bool pan, TrackerEnvelope &e) {
    const unsigned flags = r.u8(at), count = std::min(r.u8(at + 1), 25u);
    for (unsigned i = 0; i < count; ++i) {
        const int v = int8_t(uint8_t(r.u8(at + 6 + 3 * i)));
        e.points.push_back({int(r.le16(at + 7 + 3 * i)), std::clamp(pan ? v + 32 : v, 0, 64)});
    }
    e.enabled = flags & 1;
    e.loop = flags & 2;
    e.sustain = flags & 4;
    e.loopStart = int(r.u8(at + 2));
    e.loopEnd = int(r.u8(at + 3));
    e.sustainStart = int(r.u8(at + 4));
    e.sustainEnd = int(r.u8(at + 5));
    fixEnvelope(e);
}

TrackerInstrument readItInstrument(const Reader &r, size_t h, bool oldFormat) {
    TrackerInstrument ins;
    for (size_t n = 0; n < 120; ++n) {
        const unsigned note = r.u8(h + 0x40 + 2 * n);
        ins.note[n] = uint8_t(note < 120 ? note : n);
        ins.sample[n] = uint8_t(r.u8(h + 0x41 + 2 * n));
    }
    if (oldFormat) {
        ins.fadeout = int(r.le16(h + 0x18)) * 128;
        TrackerEnvelope &e = ins.volumeEnvelope;
        const unsigned flags = r.u8(h + 0x11);
        for (unsigned i = 0; i < 25 && r.u8(h + 0x1F8 + 2 * i) != 0xFF; ++i)
            e.points.push_back({int(r.u8(h + 0x1F8 + 2 * i)), int(std::min(r.u8(h + 0x1F9 + 2 * i), 64u))});
        e.enabled = flags & 1;
        e.loop = flags & 2;
        e.sustain = flags & 4;
        e.loopStart = int(r.u8(h + 0x12));
        e.loopEnd = int(r.u8(h + 0x13));
        e.sustainStart = int(r.u8(h + 0x14));
        e.sustainEnd = int(r.u8(h + 0x15));
        fixEnvelope(e);
        return ins;
    }
    ins.fadeout = int(r.le16(h + 0x14)) * 64;
    ins.globalVolume = int(std::min(r.u8(h + 0x18), 128u));
    if (!(r.u8(h + 0x19) & 0x80))
        ins.pan = int(std::min(r.u8(h + 0x19) * 4, 255u));
    readItEnvelope(r, h + 0x130, false, ins.volumeEnvelope);
    readItEnvelope(r, h + 0x182, true, ins.panEnvelope);
    return ins;
}

Error loadIt(const Reader &r, TrackerModule &m) {
    if (!r.has(0, 0xC0))
        return Error::fail("IT header cut short");
    const unsigned orders = r.le16(0x20), instruments = r.le16(0x22), samples = r.le16(0x24);
    const unsigned patterns = r.le16(0x26), compatible = r.le16(0x2A), flags = r.le16(0x2C);
    if (orders > 256 || instruments > 255 || samples > 255 || patterns > 256)
        return Error::fail("IT with too many orders, instruments, samples or patterns");
    const size_t insPtrs = 0xC0 + orders, smpPtrs = insPtrs + 4 * instruments, patPtrs = smpPtrs + 4 * samples;
    if (!r.has(0xC0, orders + 4 * (instruments + samples + patterns)))
        return Error::fail("IT cut short in its order list");
    m.format = TrackerFormat::kIT;
    m.title = r.text(4, 26);
    m.s3mEffects = m.effectMemory = m.orderMarkers = true;
    m.linearSlides = flags & 8;
    m.globalVolume = int(std::min(r.u8(0x30), 128u));
    m.mixVolume = int(std::min(r.u8(0x31), 128u));
    m.speed = r.u8(0x32) ? int(r.u8(0x32)) : 6;
    m.tempo = r.u8(0x33) >= 32 ? int(r.u8(0x33)) : 125;
    for (unsigned i = 0; i < orders; ++i)
        m.orders.push_back(uint8_t(r.u8(0xC0 + i)));

    if (flags & 4)
        for (unsigned i = 0; i < instruments; ++i) {
            const size_t h = r.le32(insPtrs + 4 * i);
            m.instruments.push_back(r.tag(h, "IMPI") ? readItInstrument(r, h, compatible < 0x200)
                                                     : TrackerInstrument{});
        }

    size_t budget = kMaxTotalFrames;
    for (unsigned i = 0; i < samples; ++i) {
        const size_t h = r.le32(smpPtrs + 4 * i);
        TrackerSample s;
        if (r.has(h, 0x50) && r.tag(h, "IMPS")) {
            const unsigned f = r.u8(h + 0x12), cvt = r.u8(h + 0x2E);
            s.globalVolume = int(std::min(r.u8(h + 0x11), 64u));
            s.volume = int(std::min(r.u8(h + 0x13), 64u));
            if (r.u8(h + 0x2F) & 0x80)
                s.pan = int(std::min((r.u8(h + 0x2F) & 0x7F) * 4, 255u));
            s.loop = f & 0x10;
            s.sustain = f & 0x20;
            s.pingPong = f & 0x40;
            s.sustainPingPong = f & 0x80;
            s.loopStart = r.le32(h + 0x34);
            s.loopEnd = r.le32(h + 0x38);
            s.c5speed = r.le32(h + 0x3C) ? double(r.le32(h + 0x3C)) : 8363.0;
            s.sustainStart = r.le32(h + 0x40);
            s.sustainEnd = r.le32(h + 0x44);
            const size_t length = r.le32(h + 0x30), at = r.le32(h + 0x48);
            if (f & 1)
                s.data = f & 8 ? decompressIt(r, at, length, f & 2, cvt & 4, budget)
                               : readPcm(r, at, length, f & 2, cvt & 1, false, budget);
            fixLoops(s);
        }
        m.samples.push_back(std::move(s));
    }

    // Patterns, read for all 64 channels, then cut to those used.
    int used = 0;
    std::vector<TrackerPattern> full;
    for (unsigned i = 0; i < patterns; ++i) {
        const size_t h = r.le32(patPtrs + 4 * i);
        if (!h || !r.has(h, 8)) {
            full.push_back(emptyPattern(64, kMaxChannels));
            continue;
        }
        const int rows = int(r.le16(h + 2));
        if (rows < 1 || rows > kMaxRows)
            return Error::fail("IT pattern " + std::to_string(i) + " has " + std::to_string(rows) + " rows");
        TrackerPattern pat = emptyPattern(rows, kMaxChannels);
        uint8_t mask[kMaxChannels] = {};
        TrackerCell last[kMaxChannels];
        unsigned lastVol[kMaxChannels] = {}, lastFx[kMaxChannels] = {}, lastParam[kMaxChannels] = {};
        size_t q = h + 8;
        const size_t end = std::min(r.size(), q + r.le16(h));
        for (int row = 0; row < rows && q < end;) {
            const unsigned what = r.u8(q++);
            if (!what) {
                ++row;
                continue;
            }
            const int c = int((what - 1) & 63);
            if (what & 0x80)
                mask[c] = uint8_t(r.u8(q++));
            const unsigned mk = mask[c];
            TrackerCell &cell = pat.cells[size_t(row) * kMaxChannels + size_t(c)];
            TrackerCell &prev = last[c];
            if (mk & 1) {
                const unsigned n = r.u8(q++);
                prev.note = uint8_t(n < 120 ? n + 1 : n == 255 ? kNoteOff : n == 254 ? kNoteCut : kNoteFade);
            }
            if (mk & 2)
                prev.instrument = uint8_t(r.u8(q++));
            if (mk & 4)
                lastVol[c] = r.u8(q++) + 1;
            if (mk & 8) {
                lastFx[c] = r.u8(q);
                lastParam[c] = r.u8(q + 1);
                q += 2;
            }
            if (mk & 0x11)
                cell.note = prev.note;
            if (mk & 0x22)
                cell.instrument = prev.instrument;
            if (mk & 0x44 && lastVol[c])
                itVolume(lastVol[c] - 1, cell);
            if (mk & 0x88)
                s3mEffect(lastFx[c], lastParam[c], true, cell);
            used = std::max(used, c + 1);
        }
        full.push_back(std::move(pat));
    }
    m.channels = std::max(used, 1);
    for (TrackerPattern &f : full) {
        TrackerPattern pat = emptyPattern(f.rows, m.channels);
        for (int row = 0; row < f.rows; ++row)
            for (int c = 0; c < m.channels; ++c)
                if (!(r.u8(0x40 + size_t(c)) & 0x80)) // a disabled channel stays empty
                    pat.cells[size_t(row * m.channels + c)] = f.cells[size_t(row * kMaxChannels + c)];
        m.patterns.push_back(std::move(pat));
    }
    for (int c = 0; c < m.channels; ++c) {
        const unsigned p = r.u8(0x40 + size_t(c)) & 0x7F;
        m.channelPan.push_back(uint8_t(!(flags & 1) || p > 64 ? 128 : std::min(p * 4, 255u)));
        m.channelVolume.push_back(uint8_t(std::min(r.u8(0x80 + size_t(c)), 64u)));
    }
    return Error::success();
}

// --- The player --------------------------------------------------------------

int waveform(int type, int pos) {
    pos &= 63;
    switch (type & 3) {
    case 1:
        return std::max(255 - pos * 8, -255); // ramp down
    case 2:
        return pos < 32 ? 255 : -255; // square
    default:
        return int(std::lround(255 * std::sin(pos * 3.14159265358979 / 32))); // sine (and "random")
    }
}

int envelopeValue(const TrackerEnvelope &e, int pos) {
    const auto &p = e.points;
    if (pos <= p.front().tick)
        return p.front().value;
    for (size_t i = 1; i < p.size(); ++i)
        if (pos <= p[i].tick) {
            const int t0 = p[i - 1].tick, t1 = p[i].tick;
            if (t1 <= t0)
                return p[i].value;
            return p[i - 1].value + (p[i].value - p[i - 1].value) * (pos - t0) / (t1 - t0);
        }
    return p.back().value;
}

void envelopeAdvance(const TrackerEnvelope &e, int &pos, bool keyOn) {
    ++pos;
    const auto &p = e.points;
    if (e.sustain && keyOn) {
        if (pos > p[size_t(e.sustainEnd)].tick)
            pos = p[size_t(e.sustainStart)].tick;
    } else if (e.loop && pos > p[size_t(e.loopEnd)].tick) {
        pos = p[size_t(e.loopStart)].tick;
    }
    pos = std::min(pos, p.back().tick + 1);
}

class Player {
public:
    Player(const TrackerModule &m, int rate);
    Error render(int startOrder, double maxSeconds, Sound &out);

private:
    struct Channel {
        const TrackerSample *smp = nullptr;
        const TrackerInstrument *ins = nullptr;
        TrackerCell cell;
        int param = 0; // the effect's, after memory
        int lastIns = 0, lastNote = 60;
        bool playing = false, backward = false, keyOn = true, fading = false;
        double pos = 0, period = 0, target = 0;
        int volume = 64, chanVol = 64, pan = 128, fade = 65536;
        int volEnvPos = 0, panEnvPos = 0;
        uint8_t mem[kFxCount] = {};
        int portaSpeed = 0;
        int vibPos = 0, vibSpeed = 0, vibDepth = 0, vibWave = 0;
        int tremPos = 0, tremSpeed = 0, tremDepth = 0, tremWave = 0;
        int vibDelta = 0, tremDelta = 0, arp = 0;
        int retrigCount = 0, loopRow = 0, loopCount = 0, delayTick = 0;
    };

    const TrackerPattern &pattern(int order) const {
        const size_t p = _m.orders[size_t(order)];
        return p < _m.patterns.size() ? _m.patterns[p] : _empty;
    }
    int playable(int order) const;
    void enterOrder(int order, int row);
    void nextRow();
    void startRow();
    void trigger(Channel &ch);
    void keyOff(Channel &ch);
    void rowEffect(Channel &ch);
    void tickEffect(Channel &ch);
    int memory(Channel &ch, uint8_t fx, int p);
    int slide(int p, bool tick0) const;
    void portamento(Channel &ch, double delta) const;
    void tonePorta(Channel &ch) const;
    void vibrato(Channel &ch, bool fine) const;
    void tremolo(Channel &ch) const;
    double periodOf(int note, const TrackerSample &s) const;
    const TrackerSample *resolve(int instrument, int &note, const TrackerInstrument *&ins) const;
    void mixChannel(Channel &ch, std::vector<double> &mix, size_t frames);

    const TrackerModule &_m;
    const int _rate;
    TrackerPattern _empty;
    std::vector<Channel> _ch;
    int _speed, _tempo, _globalVolume;
    int _order = 0, _row = 0, _tick = 0, _patDelay = 0, _jumpOrder = -1, _breakRow = -1, _loopJump = -1;
    bool _done = false;
    size_t _frames = 0, _loopFrame = 0;
    std::vector<std::vector<int64_t>> _visited; // per order and row: the frame it first played at
    double _gain;
};

Player::Player(const TrackerModule &m, int rate)
    : _m(m), _rate(rate), _empty(emptyPattern(64, m.channels)), _ch(size_t(m.channels)), _speed(m.speed),
      _tempo(m.tempo), _globalVolume(m.globalVolume), _visited(m.orders.size()) {
    for (size_t c = 0; c < _ch.size(); ++c) {
        _ch[c].pan = c < m.channelPan.size() ? m.channelPan[c] : 128;
        _ch[c].chanVol = c < m.channelVolume.size() ? m.channelVolume[c] : 64;
    }
    _gain = 0.5 * std::min(1.0, std::sqrt(4.0 / std::max(m.channels, 1))) * m.mixVolume / 64.0;
}

int Player::playable(int order) const {
    for (; order >= 0 && size_t(order) < _m.orders.size(); ++order) {
        const uint8_t p = _m.orders[size_t(order)];
        if (_m.orderMarkers && p == 255)
            return -1;
        if (!(_m.orderMarkers && p == 254))
            return order;
    }
    return -1;
}

// Moves to an order and row; back to one already played ends the render.
void Player::enterOrder(int order, int row) {
    int o = playable(order);
    if (o < 0)
        o = playable(_m.restartOrder);
    if (o < 0)
        o = playable(0);
    if (o < 0) {
        _done = true;
        return;
    }
    if (row >= pattern(o).rows)
        row = 0;
    const auto &v = _visited[size_t(o)];
    if (size_t(row) < v.size() && v[size_t(row)] >= 0) {
        _done = true;
        _loopFrame = size_t(v[size_t(row)]);
        return;
    }
    _order = o;
    _row = row;
}

void Player::nextRow() {
    _tick = 0;
    if (_loopJump >= 0) { // a pattern loop stays in this pattern
        _row = _loopJump;
        return;
    }
    if (_jumpOrder >= 0 || _breakRow >= 0)
        return enterOrder(_jumpOrder >= 0 ? _jumpOrder : _order + 1, std::max(_breakRow, 0));
    if (_row + 1 < pattern(_order).rows)
        ++_row;
    else
        enterOrder(_order + 1, 0);
}

int Player::memory(Channel &ch, uint8_t fx, int p) {
    int slot = fx;
    bool uses = _m.effectMemory;
    switch (fx) {
    case kFxOffset:
        uses = true;
        break;
    case kFxPortaDown:
        slot = _m.s3mEffects ? int(kFxPortaUp) : int(fx);
        break;
    case kFxTonePortaVol:
    case kFxVibratoVol:
        slot = kFxVolSlide;
        break;
    case kFxArpeggio:
        uses = _m.s3mEffects;
        break;
    case kFxPortaUp:
    case kFxVolSlide:
    case kFxGlobalVolSlide:
    case kFxChanVolSlide:
    case kFxPanSlide:
    case kFxRetrig:
    case kFxFinePortaUp:
    case kFxFinePortaDown:
    case kFxXFinePortaUp:
    case kFxXFinePortaDown:
    case kFxFineVolUp:
    case kFxFineVolDown:
        break;
    default:
        uses = false;
        break;
    }
    if (!uses)
        return p;
    if (p)
        ch.mem[slot] = uint8_t(p);
    return ch.mem[slot];
}

// A slide's change this tick: S3M/IT's Dxy rules (DxF/DFy fine, on the first
// tick only), else MOD/XM's (x up, else y down, after the first tick).
int Player::slide(int p, bool tick0) const {
    const int hi = p >> 4, lo = p & 15;
    if (!_m.s3mEffects)
        return tick0 ? 0 : hi ? hi : -lo;
    if (lo == 15 && hi)
        return tick0 ? hi : 0;
    if (hi == 15 && lo)
        return tick0 ? -lo : 0;
    if (tick0)
        return 0;
    return lo == 0 ? hi : hi == 0 ? -lo : 0;
}

double Player::periodOf(int note, const TrackerSample &s) const {
    if (_m.linearSlides)
        return 7680.0 - note * 64.0;
    const double c5 = s.c5speed > 0 ? s.c5speed : 8363.0;
    return kAmigaClock / (c5 * std::exp2((note - 60) / 12.0));
}

void Player::portamento(Channel &ch, double delta) const {
    ch.period =
        _m.linearSlides ? std::clamp(ch.period + delta, -1536.0, 9216.0) : std::clamp(ch.period + delta, 28.0, 1e6);
}

void Player::tonePorta(Channel &ch) const {
    const double step = 4.0 * ch.portaSpeed;
    ch.period = ch.period < ch.target ? std::min(ch.period + step, ch.target) : std::max(ch.period - step, ch.target);
}

void Player::vibrato(Channel &ch, bool fine) const {
    ch.vibDelta = waveform(ch.vibWave, ch.vibPos) * ch.vibDepth / (fine ? 128 : 32);
    ch.vibPos = (ch.vibPos + ch.vibSpeed) & 63;
}

void Player::tremolo(Channel &ch) const {
    ch.tremDelta = waveform(ch.tremWave, ch.tremPos) * ch.tremDepth / 64;
    ch.tremPos = (ch.tremPos + ch.tremSpeed) & 63;
}

const TrackerSample *Player::resolve(int instrument, int &note, const TrackerInstrument *&ins) const {
    ins = nullptr;
    size_t s = size_t(instrument);
    if (instrument < 1)
        return nullptr;
    if (!_m.instruments.empty()) {
        if (s > _m.instruments.size())
            return nullptr;
        ins = &_m.instruments[s - 1];
        s = ins->sample[size_t(note)];
        note = ins->note[size_t(note)];
    }
    if (s < 1 || s > _m.samples.size())
        return nullptr;
    const TrackerSample *smp = &_m.samples[s - 1];
    note = std::clamp(note + smp->relativeNote, 0, 119);
    return smp;
}

void Player::keyOff(Channel &ch) {
    ch.keyOn = false;
    if (!ch.ins) { // plain samples: only a sustain loop outlives the key
        if (!(ch.smp && ch.smp->sustain))
            ch.playing = false;
        return;
    }
    const TrackerEnvelope &e = ch.ins->volumeEnvelope;
    if (_m.format == TrackerFormat::kXM && !e.enabled)
        ch.volume = 0;
    else if (!(_m.format == TrackerFormat::kIT && e.enabled && !e.loop)) // IT fades at such an envelope's end
        ch.fading = true;
}

// The note, instrument and volume column, on the row's first tick or its
// note delay's.
void Player::trigger(Channel &ch) {
    const TrackerCell &c = ch.cell;
    const bool porta = c.effect == kFxTonePorta || c.effect == kFxTonePortaVol || c.volCmd == kVcTonePorta;
    if (c.instrument)
        ch.lastIns = c.instrument;
    const TrackerSample *smp = nullptr;
    const TrackerInstrument *ins = nullptr;
    if (c.note >= 1 && c.note <= 120) {
        ch.lastNote = c.note - 1;
        int note = ch.lastNote;
        smp = resolve(ch.lastIns, note, ins);
        if (smp && porta && ch.playing && ch.smp) {
            ch.target = periodOf(note, *ch.smp);
        } else if (smp) {
            ch.smp = smp;
            ch.ins = ins;
            ch.period = ch.target = periodOf(note, *smp);
            ch.pos = 0;
            ch.playing = ch.keyOn = true;
            ch.backward = ch.fading = false;
            ch.fade = 65536;
            ch.volEnvPos = ch.panEnvPos = 0;
            ch.retrigCount = 0;
            if (!(ch.vibWave & 4))
                ch.vibPos = 0;
            if (!(ch.tremWave & 4))
                ch.tremPos = 0;
            if (c.effect == kFxOffset) {
                ch.pos = ch.param * 256.0;
                if (ch.pos >= double(smp->data.size())) {
                    ch.pos = smp->loop ? double(smp->loopStart) : 0;
                    ch.playing = smp->loop;
                }
            }
        }
    } else if (c.instrument) {
        int note = ch.lastNote;
        smp = resolve(c.instrument, note, ins);
        if (ins && ch.ins == ins && ch.smp)
            smp = ch.smp;
        if (smp && _m.format == TrackerFormat::kXM) { // XM: the instrument alone restarts its envelopes
            ch.keyOn = true;
            ch.fading = false;
            ch.fade = 65536;
            ch.volEnvPos = ch.panEnvPos = 0;
        }
    }
    if (c.instrument && smp) {
        ch.volume = smp->volume;
        if (ins && ins->pan >= 0)
            ch.pan = ins->pan;
        if (smp->pan >= 0)
            ch.pan = smp->pan;
    }
    if (c.note == kNoteOff)
        keyOff(ch);
    else if (c.note == kNoteCut)
        ch.playing = false;
    else if (c.note == kNoteFade)
        ch.ins ? void(ch.fading = true) : void(ch.playing = false);

    const int p = c.volParam;
    switch (c.volCmd) {
    case kVcVolume:
        ch.volume = std::min(p, 64);
        break;
    case kVcPan:
        ch.pan = p;
        break;
    case kVcFineUp:
        ch.volume = std::min(ch.volume + p, 64);
        break;
    case kVcFineDown:
        ch.volume = std::max(ch.volume - p, 0);
        break;
    case kVcVibSpeed:
        ch.vibSpeed = p ? p : ch.vibSpeed;
        break;
    case kVcVibDepth:
        ch.vibDepth = p ? p : ch.vibDepth;
        break;
    case kVcTonePorta:
        ch.portaSpeed = p ? p : ch.portaSpeed;
        break;
    default:
        break;
    }
}

// Effects on a row's first tick.
void Player::rowEffect(Channel &ch) {
    const int p = ch.param, x = p >> 4, y = p & 15;
    switch (ch.cell.effect) {
    case kFxSpeed:
        _speed = p ? p : _speed;
        break;
    case kFxTempo:
        _tempo = p >= 32 ? p : _tempo;
        break;
    case kFxSpeedTempo:
        if (p && p < 32)
            _speed = p;
        else if (p >= 32)
            _tempo = p;
        break;
    case kFxJump:
        _jumpOrder = p;
        break;
    case kFxBreak:
        _breakRow = p;
        break;
    case kFxVolume:
        ch.volume = std::min(p, 64);
        break;
    case kFxGlobalVol:
        _globalVolume = std::min(p, 128);
        break;
    case kFxPan:
        ch.pan = p;
        break;
    case kFxChanVol:
        ch.chanVol = std::min(p, 64);
        break;
    case kFxFinePortaUp:
        portamento(ch, -4.0 * p);
        break;
    case kFxFinePortaDown:
        portamento(ch, 4.0 * p);
        break;
    case kFxXFinePortaUp:
        portamento(ch, -p);
        break;
    case kFxXFinePortaDown:
        portamento(ch, p);
        break;
    case kFxPortaUp:
    case kFxPortaDown:
        if (_m.s3mEffects && p >= 0xE0) // FFx fine, FEx extra fine
            portamento(ch, (ch.cell.effect == kFxPortaUp ? -1.0 : 1.0) * (p >= 0xF0 ? 4 * y : y));
        break;
    case kFxFineVolUp:
        ch.volume = std::min(ch.volume + p, 64);
        break;
    case kFxFineVolDown:
        ch.volume = std::max(ch.volume - p, 0);
        break;
    case kFxVolSlide:
    case kFxTonePortaVol:
    case kFxVibratoVol:
        ch.volume = std::clamp(ch.volume + slide(p, true), 0, 64);
        break;
    case kFxGlobalVolSlide:
        _globalVolume = std::clamp(_globalVolume + slide(p, true), 0, 128);
        break;
    case kFxChanVolSlide:
        ch.chanVol = std::clamp(ch.chanVol + slide(p, true), 0, 64);
        break;
    case kFxPanSlide:
        ch.pan = std::clamp(ch.pan - 4 * slide(p, true), 0, 255);
        break;
    case kFxTonePorta:
        ch.portaSpeed = p ? p : ch.portaSpeed;
        break;
    case kFxVibrato:
    case kFxFineVibrato:
        ch.vibSpeed = x ? x : ch.vibSpeed;
        ch.vibDepth = y ? y : ch.vibDepth;
        break;
    case kFxTremolo:
        ch.tremSpeed = x ? x : ch.tremSpeed;
        ch.tremDepth = y ? y : ch.tremDepth;
        break;
    case kFxVibWave:
        ch.vibWave = p;
        break;
    case kFxTremWave:
        ch.tremWave = p;
        break;
    case kFxPatLoop:
        if (p == 0)
            ch.loopRow = _row;
        else if (ch.loopCount == 0)
            ch.loopCount = p, _loopJump = ch.loopRow;
        else if (--ch.loopCount > 0)
            _loopJump = ch.loopRow;
        break;
    case kFxNoteCut:
        if (p == 0)
            ch.volume = 0;
        break;
    case kFxPatDelay:
        _patDelay = _patDelay ? _patDelay : p;
        break;
    case kFxKeyOff:
        if (p == 0)
            keyOff(ch);
        break;
    default:
        break;
    }
}

// Effects on the ticks after the first.
void Player::tickEffect(Channel &ch) {
    const int p = ch.param, t = _tick % _speed;
    switch (ch.cell.effect) {
    case kFxArpeggio:
        ch.arp = t % 3 == 0 ? 0 : t % 3 == 1 ? p >> 4 : p & 15;
        break;
    case kFxPortaUp:
        if (!_m.s3mEffects || p < 0xE0)
            portamento(ch, -4.0 * p);
        break;
    case kFxPortaDown:
        if (!_m.s3mEffects || p < 0xE0)
            portamento(ch, 4.0 * p);
        break;
    case kFxTonePorta:
        tonePorta(ch);
        break;
    case kFxTonePortaVol:
        tonePorta(ch);
        ch.volume = std::clamp(ch.volume + slide(p, false), 0, 64);
        break;
    case kFxVibrato:
        vibrato(ch, false);
        break;
    case kFxFineVibrato:
        vibrato(ch, true);
        break;
    case kFxVibratoVol:
        vibrato(ch, false);
        ch.volume = std::clamp(ch.volume + slide(p, false), 0, 64);
        break;
    case kFxTremolo:
        tremolo(ch);
        break;
    case kFxVolSlide:
        ch.volume = std::clamp(ch.volume + slide(p, false), 0, 64);
        break;
    case kFxGlobalVolSlide:
        _globalVolume = std::clamp(_globalVolume + slide(p, false) * (_m.format == TrackerFormat::kXM ? 2 : 1), 0, 128);
        break;
    case kFxChanVolSlide:
        ch.chanVol = std::clamp(ch.chanVol + slide(p, false), 0, 64);
        break;
    case kFxPanSlide: // XM: x right, y left; IT: x left, y right
        ch.pan = std::clamp(ch.pan + (_m.s3mEffects ? -4 : 1) * slide(p, false), 0, 255);
        break;
    case kFxTempo:
        if (_m.format == TrackerFormat::kIT && p < 0x20) // T0x / T1x: tempo slide
            _tempo = std::clamp(_tempo + (p >> 4 ? 1 : -1) * (p & 15), 32, 255);
        break;
    case kFxRetrig:
        if ((p & 15) && ++ch.retrigCount >= (p & 15)) {
            ch.retrigCount = 0;
            ch.pos = 0;
            ch.backward = false;
            ch.playing = ch.smp != nullptr;
            static const int kAdd[16] = {0, -1, -2, -4, -8, -16, 0, 0, 0, 1, 2, 4, 8, 16, 0, 0};
            const int x = p >> 4;
            int v = ch.volume + kAdd[x];
            if (x == 6)
                v = v * 2 / 3;
            else if (x == 7)
                v /= 2;
            else if (x == 14)
                v = v * 3 / 2;
            else if (x == 15)
                v *= 2;
            ch.volume = std::clamp(v, 0, 64);
        }
        break;
    case kFxNoteCut:
        if (t == p)
            ch.volume = 0;
        break;
    case kFxNoteDelay:
        if (t == ch.delayTick && _tick < _speed)
            trigger(ch);
        break;
    case kFxKeyOff:
        if (t == p)
            keyOff(ch);
        break;
    default:
        break;
    }
    const int v = ch.cell.volParam;
    switch (ch.cell.volCmd) {
    case kVcSlideUp:
        ch.volume = std::min(ch.volume + v, 64);
        break;
    case kVcSlideDown:
        ch.volume = std::max(ch.volume - v, 0);
        break;
    case kVcPanLeft:
        ch.pan = std::max(ch.pan - v, 0);
        break;
    case kVcPanRight:
        ch.pan = std::min(ch.pan + v, 255);
        break;
    case kVcPortaUp:
        portamento(ch, -4.0 * v);
        break;
    case kVcPortaDown:
        portamento(ch, 4.0 * v);
        break;
    case kVcVibDepth:
        if (ch.cell.effect != kFxVibrato && ch.cell.effect != kFxVibratoVol)
            vibrato(ch, false);
        break;
    case kVcTonePorta:
        if (ch.cell.effect != kFxTonePorta && ch.cell.effect != kFxTonePortaVol)
            tonePorta(ch);
        break;
    default:
        break;
    }
}

void Player::startRow() {
    _patDelay = 0;
    _jumpOrder = _breakRow = _loopJump = -1;
    const TrackerPattern &pat = pattern(_order);
    if (_row >= pat.rows)
        _row = 0;
    auto &v = _visited[size_t(_order)];
    if (v.size() < size_t(pat.rows))
        v.resize(size_t(pat.rows), -1);
    if (v[size_t(_row)] < 0)
        v[size_t(_row)] = int64_t(_frames);
    for (size_t c = 0; c < _ch.size(); ++c) {
        Channel &ch = _ch[c];
        const size_t i = size_t(_row) * _ch.size() + c;
        ch.cell = i < pat.cells.size() ? pat.cells[i] : TrackerCell{};
        ch.param = memory(ch, ch.cell.effect, ch.cell.param);
        ch.delayTick = ch.cell.effect == kFxNoteDelay ? ch.param : 0;
        if (!ch.delayTick)
            trigger(ch);
        rowEffect(ch);
    }
}

// Mixes a channel's tick: envelopes, fadeout, volumes, pan and pitch, then
// its sample resampled with linear interpolation.
void Player::mixChannel(Channel &ch, std::vector<double> &mix, size_t frames) {
    if (!ch.playing || !ch.smp || ch.smp->data.empty())
        return;
    const TrackerSample &s = *ch.smp;
    int env = 64, panEnv = 32;
    if (ch.ins) {
        const TrackerEnvelope &ve = ch.ins->volumeEnvelope;
        if (ve.enabled) {
            env = envelopeValue(ve, ch.volEnvPos);
            const bool ended = ch.volEnvPos > ve.points.back().tick;
            envelopeAdvance(ve, ch.volEnvPos, ch.keyOn);
            if (ended && env == 0) {
                ch.playing = false;
                return;
            }
            if (ended && _m.format == TrackerFormat::kIT && !ch.keyOn)
                ch.fading = true;
        }
        const TrackerEnvelope &pe = ch.ins->panEnvelope;
        if (pe.enabled) {
            panEnv = envelopeValue(pe, ch.panEnvPos);
            envelopeAdvance(pe, ch.panEnvPos, ch.keyOn);
        }
        if (ch.fading) {
            ch.fade = std::max(ch.fade - ch.ins->fadeout, 0);
            if (ch.fade == 0) {
                ch.playing = false;
                return;
            }
        }
    }
    double vol = std::clamp(ch.volume + ch.tremDelta, 0, 64) / 64.0 * ch.chanVol / 64.0 * s.globalVolume / 64.0 * env /
                 64.0 * ch.fade / 65536.0 * _globalVolume / 128.0 * _gain;
    if (ch.ins)
        vol *= ch.ins->globalVolume / 128.0;
    const int pan = std::clamp(ch.pan + (panEnv - 32) * (128 - std::abs(ch.pan - 128)) / 32, 0, 255);
    const double right = vol * std::min(1.0, pan / 128.0), left = vol * std::min(1.0, (255 - pan) / 127.0);

    const double c5 = s.c5speed > 0 ? s.c5speed : 8363.0, period = ch.period + ch.vibDelta;
    double freq = _m.linearSlides ? c5 * std::exp2((3840.0 - period) / 768.0) : kAmigaClock / std::max(period, 1.0);
    freq *= std::exp2(ch.arp / 12.0);
    const double step = std::min(freq / _rate, 1e4);

    const auto &d = s.data;
    const bool sustained = s.sustain && ch.keyOn, loop = sustained || s.loop;
    const size_t ls = sustained ? s.sustainStart : s.loopStart, le = sustained ? s.sustainEnd : s.loopEnd;
    const bool ping = sustained ? s.sustainPingPong : s.pingPong;
    const size_t end = loop ? le : d.size();
    if (ch.backward && !(loop && ping))
        ch.backward = false;
    for (size_t i = 0; i < frames; ++i) {
        if (loop && !ch.backward && ch.pos >= double(le)) // e.g. a sustain loop let go past its end
            ch.pos = double(ls) + std::fmod(ch.pos - double(ls), double(le - ls));
        const size_t at = std::min(size_t(ch.pos), end - 1);
        const double frac = ch.pos - double(at);
        const int a = d[at];
        const int b = at + 1 < end ? d[at + 1] : loop && !ping ? d[ls] : a;
        const double v = a + (b - a) * std::clamp(frac, 0.0, 1.0);
        mix[2 * i] += v * left;
        mix[2 * i + 1] += v * right;
        if (!ch.backward) {
            ch.pos += step;
            if (loop && ch.pos >= double(le)) {
                if (ping) {
                    ch.pos = std::max(2.0 * double(le) - ch.pos, double(ls));
                    ch.backward = true;
                } else {
                    ch.pos = double(ls) + std::fmod(ch.pos - double(ls), double(le - ls));
                }
            } else if (!loop && ch.pos >= double(d.size())) {
                ch.playing = false;
                return;
            }
        } else {
            ch.pos -= step;
            if (ch.pos < double(ls)) {
                ch.pos = std::min(2.0 * double(ls) - ch.pos, double(le) - 1e-6);
                ch.backward = false;
            }
        }
    }
}

Error Player::render(int startOrder, double maxSeconds, Sound &out) {
    out = Sound{};
    out.rate = _rate;
    out.channels = 2;
    const int first = playable(startOrder);
    if (first < 0 || _m.channels < 1)
        return Error::fail("nothing to play from order " + std::to_string(startOrder));
    _order = first;
    const size_t maxFrames = size_t(std::clamp(maxSeconds, 0.0, 3600.0) * _rate);
    std::vector<double> mix;
    double tickFrames = 0;
    while (!_done && _frames < maxFrames) {
        for (Channel &ch : _ch)
            ch.vibDelta = ch.tremDelta = ch.arp = 0;
        if (_tick == 0)
            startRow();
        else
            for (Channel &ch : _ch)
                tickEffect(ch);
        tickFrames += _rate * 2.5 / _tempo; // samples per tick = rate * 5 / (2 * tempo)
        const size_t n = std::min(size_t(tickFrames), maxFrames - _frames);
        tickFrames -= std::floor(tickFrames);
        mix.assign(n * 2, 0.0);
        for (Channel &ch : _ch)
            mixChannel(ch, mix, n);
        for (double v : mix)
            out.samples.push_back(int16_t(std::clamp(std::lround(v), -32768L, 32767L)));
        _frames += n;
        if (++_tick >= _speed * (1 + _patDelay))
            nextRow();
    }
    out.loopStart = _done ? _loopFrame : 0;
    out.loopEnd = out.frames();
    return Error::success();
}

} // namespace

Error loadTrackerModule(const std::vector<uint8_t> &data, TrackerModule &out) {
    out = TrackerModule{};
    const Reader r{data};
    Error err;
    if (r.tag(0, "IMPM"))
        err = loadIt(r, out);
    else if (r.tag(0, "Extended Module:"))
        err = loadXm(r, out);
    else if (r.tag(0x2C, "SCRM"))
        err = loadS3m(r, out);
    else
        err = loadMod(r, out);
    if (!err && out.orders.empty())
        err = Error::fail("tracker module without orders");
    if (err)
        out = TrackerModule{};
    return err;
}

std::string trackerFormat(const TrackerModule &m) {
    switch (m.format) {
    case TrackerFormat::kS3M:
        return "S3M";
    case TrackerFormat::kXM:
        return "XM";
    case TrackerFormat::kIT:
        return "IT";
    default:
        return "MOD";
    }
}

Error renderTrackerModule(const TrackerModule &m, int rate, int startOrder, double maxSeconds, Sound &out) {
    if (rate < 1000 || rate > 384000)
        return Error::fail("can't render at " + std::to_string(rate) + " Hz");
    return Player(m, rate).render(startOrder, maxSeconds, out);
}

} // namespace reluva::audio
