// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/audio/dls.h"

#include "common/riff.h"

#include <algorithm>

namespace reluva::audio {

namespace {

// wsmp: its size, unity note, fine tune, gain, options, loops (each its
// size, type, start and length).
DlsSampleInfo readSampleInfo(const RiffChunk& c) {
    DlsSampleInfo s;
    const uint32_t size = c.u32(0);
    s.unityNote = c.u16(4);
    s.fineTune = c.s16(6);
    s.gain = c.s32(8);
    if (c.u32(16) > 0) {
        s.looped = true;
        s.loopStart = c.u32(size + 8);
        s.loopLength = c.u32(size + 12);
    }
    return s;
}

// art1: its size, how many connections, then each (source, control,
// destination, transform, scale).
void readArticulation(const RiffChunk* lart, std::vector<DlsConnection>& out) {
    if (!lart)
        return;
    for (const RiffChunk& art : lart->children) {
        if (art.id != "art1" && art.id != "art2")
            continue;
        const uint32_t size = art.u32(0), count = art.u32(4);
        for (uint32_t i = 0; i < count && size + 12 * (i + 1) <= art.size; ++i) {
            const size_t at = size + 12 * i;
            out.push_back({art.u16(at), art.u16(at + 2), art.u16(at + 4), art.u16(at + 6), art.s32(at + 8)});
        }
    }
}

std::string infoName(const RiffChunk& c) {
    if (const RiffChunk* info = c.findList("INFO"))
        if (const RiffChunk* nam = info->find("INAM")) {
            std::string s(reinterpret_cast<const char*>(nam->data), nam->size);
            return s.substr(0, s.find('\0'));
        }
    return {};
}

} // namespace

const DlsInstrument* DlsCollection::find(uint32_t bank, uint32_t program) const {
    for (const DlsInstrument& i : instruments)
        if (i.bank == bank && i.program == program)
            return &i;
    return nullptr;
}

Error readDls(const std::vector<uint8_t>& data, DlsCollection& out) {
    out = DlsCollection{};
    RiffChunk top;
    if (Error err = readRiff(data, top))
        return err;
    if (top.type != "DLS ")
        return Error::fail("not a DLS collection");
    out.name = infoName(top);

    // The waves, and the pool table's offsets to them (from the wave
    // pool's data).
    const RiffChunk* pool = top.findList("wvpl");
    if (!pool)
        return Error::fail("no wave pool");
    std::vector<size_t> offsets;
    for (const RiffChunk& w : pool->children) {
        if (!w.isList() || (w.type != "wave" && w.type != "WAVE"))
            continue;
        offsets.push_back(size_t(w.data - 12 - pool->data));
        DlsWave wave;
        wave.name = infoName(w);
        const RiffChunk* fmt = w.find("fmt ");
        const RiffChunk* body = w.find("data");
        if (!fmt || !body || fmt->u16(0) != 1)
            return Error::fail("wave " + std::to_string(out.waves.size()) + ": not PCM");
        const int channels = std::max(1, int(fmt->u16(2))), bits = fmt->u16(14);
        wave.rate = int(fmt->u32(4));
        const size_t frames = body->size / size_t(channels * (bits / 8));
        wave.samples.resize(frames);
        for (size_t f = 0; f < frames; ++f) {
            // The first channel of each frame.
            const size_t at = f * size_t(channels * (bits / 8));
            wave.samples[f] = bits == 8 ? int16_t((int(body->u8(at)) - 128) << 8) : body->s16(at);
        }
        if (const RiffChunk* wsmp = w.find("wsmp"))
            wave.info = readSampleInfo(*wsmp);
        out.waves.push_back(std::move(wave));
    }
    std::vector<uint32_t> cues; // pool table: wave offsets, by cue
    if (const RiffChunk* ptbl = top.find("ptbl")) {
        const uint32_t size = ptbl->u32(0), count = ptbl->u32(4);
        for (uint32_t i = 0; i < count; ++i)
            cues.push_back(ptbl->u32(size + 4 * i));
    }
    auto waveForCue = [&](uint32_t cue) -> uint32_t {
        if (cue < cues.size())
            for (size_t w = 0; w < offsets.size(); ++w)
                if (offsets[w] == cues[cue])
                    return uint32_t(w);
        return cue; // no table: in order
    };

    const RiffChunk* lins = top.findList("lins");
    if (!lins)
        return Error::fail("no instruments");
    for (const RiffChunk* ins : lins->lists("ins ")) {
        DlsInstrument inst;
        inst.name = infoName(*ins);
        if (const RiffChunk* insh = ins->find("insh")) {
            inst.bank = insh->u32(4);
            inst.program = insh->u32(8);
        }
        readArticulation(ins->findList("lart"), inst.articulation);
        if (const RiffChunk* lrgn = ins->findList("lrgn"))
            for (const RiffChunk& rgn : lrgn->children) {
                if (!rgn.isList() || (rgn.type != "rgn " && rgn.type != "rgn2"))
                    continue;
                DlsRegion r;
                if (const RiffChunk* rgnh = rgn.find("rgnh")) {
                    r.keyLow = rgnh->u16(0);
                    r.keyHigh = rgnh->u16(2);
                    r.velocityLow = rgnh->u16(4);
                    r.velocityHigh = rgnh->u16(6);
                    r.options = rgnh->u16(8);
                    r.keyGroup = rgnh->u16(10);
                }
                if (const RiffChunk* wlnk = rgn.find("wlnk"))
                    r.wave = waveForCue(wlnk->u32(8));
                if (r.wave >= out.waves.size())
                    continue;
                if (const RiffChunk* wsmp = rgn.find("wsmp")) {
                    r.hasInfo = true;
                    r.info = readSampleInfo(*wsmp);
                }
                readArticulation(rgn.findList("lart"), r.articulation);
                inst.regions.push_back(std::move(r));
            }
        out.instruments.push_back(std::move(inst));
    }
    return Error::success();
}

} // namespace reluva::audio
