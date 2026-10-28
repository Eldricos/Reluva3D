// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/audio/directmusic.h"

#include "common/riff.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::audio {

namespace {

// DMUS_PLAYMODE flags.
enum : int { kKeyRoot = 1, kChordRoot = 2, kScaleIntervals = 4, kChordIntervals = 8, kPartsMode = 16 };
// DMUS_COMMANDT types.
enum : int { kGroove = 0, kCommandFill = 1, kCommandIntro = 2, kCommandBreak = 3, kCommandEnd = 4, kEndAndIntro = 5 };
// Curve types and shapes as the game's files use them
// (docs/formats/directmusic.md).
enum : int { kCurvePitchBend = 3, kCurveController = 4 };
enum : int { kShapeLinear = 0, kShapeInstant = 1, kShapeExp = 2, kShapeLog = 3, kShapeSine = 4 };

constexpr int kCurveStep = 24;       // ticks between a curve's steps (a 32nd note)
constexpr float kMasterGain = 0.35f; // the synthesizer's whole mix, so a busy band stays under full scale

// A record array chunk: a DWORD record size, then the records.
template <class F>
void records(const RiffChunk& c, size_t smallest, F each) {
    const size_t size = c.u32(0);
    if (size < smallest)
        return;
    for (size_t at = 4; at + size <= c.size; at += size)
        each(at, size);
}

DmTimeSig timeSig(const RiffChunk& c, size_t at) {
    DmTimeSig t;
    t.beatsPerMeasure = std::max(1, int(c.u8(at)));
    t.beat = c.u8(at + 1);
    t.gridsPerBeat = std::max(1, int(c.u16(at + 2)));
    return t;
}

std::string unfoName(const RiffChunk& c) {
    if (const RiffChunk* unfo = c.findList("UNFO"))
        if (const RiffChunk* unam = unfo->find("UNAM"))
            return unam->text16();
    return {};
}

DmReference reference(const RiffChunk& dmrf) {
    DmReference r;
    if (const RiffChunk* name = dmrf.find("name"))
        r.name = name->text16();
    if (const RiffChunk* file = dmrf.find("file"))
        r.file = file->text16();
    return r;
}

void readBandForm(const RiffChunk& form, DmBand& out) {
    out.name = unfoName(form);
    const RiffChunk* lbil = form.findList("lbil");
    if (!lbil)
        return;
    for (const RiffChunk* lbin : lbil->lists("lbin")) {
        const RiffChunk* bins = lbin->find("bins");
        if (!bins || bins->size < 36)
            continue;
        DmInstrument i;
        i.patch = bins->u32(0);
        i.channel = int(bins->u32(24));
        i.flags = bins->u32(28);
        i.pan = bins->u8(32);
        i.volume = bins->u8(33);
        i.transpose = bins->s16(34);
        if (const RiffChunk* dmrf = lbin->findList("DMRF"))
            i.dls = reference(*dmrf);
        out.instruments.push_back(i);
    }
}

// Microsoft's StoredRangeToActualRange: a stored randomisation range
// (0-255) in ticks.
int actualRange(int stored) {
    if (stored <= 190)
        return stored;
    if (stored <= 212)
        return (stored - 190) * 5 + 190;
    if (stored <= 232)
        return (stored - 212) * 10 + 300;
    return (stored - 232) * 50 + 500;
}

float curveShape(int shape, float x) {
    switch (shape) {
    case kShapeInstant: return 1;
    case kShapeExp: return x * x;
    case kShapeLog: return std::sqrt(x);
    case kShapeSine: return 0.5f - 0.5f * std::cos(3.14159265f * x);
    default: return x;
    }
}

} // namespace

int DmTimeSig::gridTicks(int grid) const {
    const int b = beatTicks();
    return (grid / gridsPerBeat) * b + (grid % gridsPerBeat) * (b / gridsPerBeat);
}

std::vector<int> DmPart::variations() const {
    std::vector<int> out;
    for (int v = 0; v < 32; ++v)
        if (variationChoices[size_t(v)] & 0x1fffffff)
            out.push_back(v);
    return out;
}

Error readDmBand(const std::vector<uint8_t>& data, DmBand& out) {
    out = DmBand{};
    RiffChunk top;
    if (Error err = readRiff(data, top))
        return err;
    if (top.type != "DMBD")
        return Error::fail("not a DirectMusic band");
    readBandForm(top, out);
    return Error::success();
}

Error readDmStyle(const std::vector<uint8_t>& data, DmStyle& out) {
    out = DmStyle{};
    RiffChunk top;
    if (Error err = readRiff(data, top))
        return err;
    if (top.type != "DMST")
        return Error::fail("not a DirectMusic style");
    out.name = unfoName(top);
    if (const RiffChunk* styh = top.find("styh"); styh && styh->size >= 12) {
        out.timeSig = timeSig(*styh, 0);
        double tempo = 0;
        std::memcpy(&tempo, styh->data + styh->size - 8, 8); // after the time signature, packed or not
        if (tempo > 1 && tempo < 1000)
            out.tempo = tempo;
    }
    std::map<std::array<uint8_t, 16>, size_t> partOf;
    for (const RiffChunk* list : top.lists("part")) {
        const RiffChunk* prth = list->find("prth");
        if (!prth || prth->size < 151)
            continue;
        DmPart p;
        p.timeSig = timeSig(*prth, 0);
        for (size_t v = 0; v < 32; ++v)
            p.variationChoices[v] = prth->u32(4 + 4 * v);
        std::memcpy(p.guid.data(), prth->data + 132, 16);
        p.measures = std::max(1, int(prth->u16(148)));
        p.playMode = prth->u8(150);
        if (const RiffChunk* note = list->find("note"))
            records(*note, 22, [&](size_t at, size_t) {
                DmNote n;
                n.grid = note->s32(at);
                n.variations = note->u32(at + 4);
                n.duration = note->s32(at + 8);
                n.offset = note->s16(at + 12);
                n.musicValue = note->u16(at + 14);
                n.velocity = note->u8(at + 16);
                n.timeRange = note->u8(at + 17);
                n.durationRange = note->u8(at + 18);
                n.velocityRange = note->u8(at + 19);
                n.playMode = note->u8(at + 21);
                p.notes.push_back(n);
            });
        if (const RiffChunk* crve = list->find("crve"))
            records(*crve, 28, [&](size_t at, size_t) {
                DmCurve c;
                c.grid = crve->s32(at);
                c.variations = crve->u32(at + 4);
                c.duration = crve->s32(at + 8);
                c.offset = crve->s16(at + 16);
                c.startValue = crve->s16(at + 18);
                c.endValue = crve->s16(at + 20);
                c.type = crve->u8(at + 24);
                c.shape = crve->u8(at + 25);
                c.cc = crve->u8(at + 26);
                p.curves.push_back(c);
            });
        partOf[p.guid] = out.parts.size();
        out.parts.push_back(std::move(p));
    }
    for (const RiffChunk* list : top.lists("pttn")) {
        const RiffChunk* ptnh = list->find("ptnh");
        if (!ptnh || ptnh->size < 10)
            continue;
        DmPattern p;
        p.name = unfoName(*list);
        p.timeSig = timeSig(*ptnh, 0);
        p.grooveBottom = ptnh->u8(4);
        p.grooveTop = ptnh->u8(5);
        p.embellishment = ptnh->u16(6);
        p.measures = std::max(1, int(ptnh->u16(8)));
        for (const RiffChunk* pref : list->lists("pref")) {
            const RiffChunk* prfc = pref->find("prfc");
            if (!prfc || prfc->size < 22)
                continue;
            std::array<uint8_t, 16> guid;
            std::memcpy(guid.data(), prfc->data, 16);
            const auto part = partOf.find(guid);
            if (part == partOf.end())
                continue;
            DmPartRef r;
            r.part = part->second;
            // DirectX 7's 22 bytes give the channel as the logical part;
            // later ones add a performance channel.
            r.channel = prfc->size >= 28 ? int(prfc->u32(24)) : prfc->u16(16);
            r.lockId = prfc->u8(18);
            r.subChordLevel = prfc->u8(19);
            r.randomVariation = prfc->u8(21);
            p.parts.push_back(r);
        }
        out.patterns.push_back(std::move(p));
    }
    for (const RiffChunk& c : top.children)
        if (c.id == "RIFF" && c.type == "DMBD") {
            DmBand b;
            readBandForm(c, b);
            out.bands.push_back(std::move(b));
        }
    return Error::success();
}

Error readDmSegment(const std::vector<uint8_t>& data, DmSegment& out) {
    out = DmSegment{};
    RiffChunk top;
    if (Error err = readRiff(data, top))
        return err;
    if (top.type != "DMSG")
        return Error::fail("not a DirectMusic segment");
    out.name = unfoName(top);
    if (const RiffChunk* segh = top.find("segh"); segh && segh->size >= 20) {
        out.repeats = int(segh->u32(0));
        out.length = segh->s32(4);
        out.playStart = segh->s32(8);
        out.loopStart = segh->s32(12);
        out.loopEnd = segh->s32(16);
    }
    const RiffChunk* trkl = top.findList("trkl");
    if (!trkl)
        return Error::fail("no tracks");
    for (const RiffChunk& track : trkl->children) {
        if (track.type != "DMTK")
            continue;
        for (const RiffChunk& c : track.children) {
            if (c.id == "cmnd") {
                records(c, 10, [&](size_t at, size_t) {
                    DmCommand m;
                    m.time = c.s32(at);
                    m.measure = c.u16(at + 4);
                    m.beat = c.u8(at + 6);
                    m.command = c.u8(at + 7);
                    m.groove = c.u8(at + 8);
                    m.grooveRange = c.u8(at + 9);
                    out.commands.push_back(m);
                });
            } else if (c.id == "tetr") {
                records(c, 12, [&](size_t at, size_t size) {
                    double bpm = 0;
                    std::memcpy(&bpm, c.data + at + size - 8, 8);
                    if (bpm > 1 && bpm < 1000)
                        out.tempos.push_back({c.s32(at), bpm});
                });
            } else if (c.isList() && c.type == "sttr") {
                for (const RiffChunk* strf : c.lists("strf"))
                    if (const RiffChunk* dmrf = strf->findList("DMRF"); dmrf && out.style.file.empty())
                        out.style = reference(*dmrf);
            } else if (c.id == "RIFF" && c.type == "DMBT") {
                if (const RiffChunk* lbdl = c.findList("lbdl"))
                    for (const RiffChunk* lbnd : lbdl->lists("lbnd")) {
                        DmBand b;
                        if (const RiffChunk* h = lbnd->find("bdih"))
                            b.time = h->s32(0);
                        else if (const RiffChunk* h2 = lbnd->find("bd2h"))
                            b.time = h2->s32(0);
                        for (const RiffChunk& f : lbnd->children)
                            if (f.id == "RIFF" && f.type == "DMBD")
                                readBandForm(f, b);
                        out.bands.push_back(std::move(b));
                    }
            } else if (c.isList() && c.type == "cord") {
                const RiffChunk* crdh = c.find("crdh");
                const uint32_t key = crdh ? crdh->u32(0) : 0;
                for (const RiffChunk& crdb : c.children) {
                    if (crdb.id != "crdb")
                        continue;
                    const uint32_t chordSize = crdb.u32(0);
                    const size_t sub = 4 + chordSize; // the subchords' count, then their size
                    if (crdb.u32(sub) == 0)
                        continue;
                    const size_t first = sub + 8;
                    DmChord ch;
                    ch.time = crdb.s32(4 + 32); // after the chord's 16-character name
                    ch.chordPattern = crdb.u32(first);
                    ch.scalePattern = crdb.u32(first + 4);
                    ch.chordRoot = crdb.u8(first + 16);
                    ch.scaleRoot = crdb.u8(first + 17);
                    if (!ch.scalePattern)
                        ch.scalePattern = key & 0xffffff;
                    out.chords.push_back(ch);
                }
            } else if (c.id == "seqt") {
                // Chunks inside a chunk (as Microsoft notes): evtl, curl.
                size_t at = 0;
                while (at + 8 <= c.size) {
                    const std::string id(reinterpret_cast<const char*>(c.data + at), 4);
                    const uint32_t size = c.u32(at + 4);
                    RiffChunk sub;
                    sub.id = id;
                    sub.data = c.data + at + 8;
                    sub.size = std::min<size_t>(size, c.size - at - 8);
                    if (id == "evtl")
                        records(sub, 17, [&](size_t r, size_t) {
                            DmSequenceEvent e;
                            e.time = sub.s32(r) + sub.s16(r + 12);
                            e.duration = sub.s32(r + 4);
                            e.channel = int(sub.u32(r + 8));
                            e.status = sub.u8(r + 14);
                            e.data1 = sub.u8(r + 15);
                            e.data2 = sub.u8(r + 16);
                            out.sequence.push_back(e);
                        });
                    else if (id == "curl")
                        records(sub, 28, [&](size_t r, size_t) {
                            DmCurve cv;
                            cv.grid = sub.s32(r) + sub.s16(r + 16);
                            cv.duration = sub.s32(r + 4);
                            cv.channel = int(sub.u32(r + 12));
                            cv.startValue = sub.s16(r + 18);
                            cv.endValue = sub.s16(r + 20);
                            cv.type = sub.u8(r + 24);
                            cv.shape = sub.u8(r + 25);
                            cv.cc = sub.u8(r + 26);
                            out.sequenceCurves.push_back(cv);
                        });
                    at += 8 + size + (size & 1);
                }
            }
        }
    }
    std::sort(out.commands.begin(), out.commands.end(),
              [](const DmCommand& a, const DmCommand& b) { return a.time < b.time; });
    std::sort(out.bands.begin(), out.bands.end(), [](const DmBand& a, const DmBand& b) { return a.time < b.time; });
    return Error::success();
}

int dmMusicToMidi(uint16_t value, int mode, const DmChord& chord) {
    int key = 0;
    if (!(mode & (kScaleIntervals | kChordIntervals))) {
        // Fixed: the value is the key, over the chord's or key's root.
        key = value;
        if (mode & kChordRoot)
            key += chord.chordRoot % 12;
        else if (mode & kKeyRoot)
            key += chord.scaleRoot % 12;
    } else {
        int octave = value >> 12, chordPos = value >> 8 & 15, scalePos = value >> 4 & 15, accidental = value & 15;
        if (octave > 14)
            octave -= 16; // -2 and -1
        if (accidental > 7)
            accidental -= 16;
        const int root = (mode & kChordRoot) ? chord.chordRoot : chord.scaleRoot;
        // The scale's notes as semitones above the chord's root, over
        // three octaves.
        std::vector<int> scale;
        for (int i = 0; i < 48; ++i) {
            const int pitch = ((i - (root - chord.scaleRoot)) % 24 + 24) % 24; // into the two-octave pattern
            if (chord.scalePattern >> (pitch % 24) & 1)
                scale.push_back(i);
        }
        std::vector<int> tones;
        for (int i = 0; i < 24; ++i)
            if (chord.chordPattern >> i & 1)
                tones.push_back(i);
        int base;
        if ((mode & kChordIntervals) && chordPos < int(tones.size())) {
            base = tones[size_t(chordPos)];
        } else if (mode & kScaleIntervals) {
            // Melodic (or always-play past the chord's top): chord
            // positions as every other note of the scale.
            const size_t degree = size_t(chordPos) * 2;
            base = degree < scale.size() ? scale[degree] : 12 * int(degree / 7);
        } else {
            return -1; // past the chord's top in normal chord mode
        }
        // Up the scale from there.
        for (int s = 0; s < scalePos; ++s) {
            const auto next = std::upper_bound(scale.begin(), scale.end(), base);
            base = next != scale.end() ? *next : base + 2;
        }
        key = octave * 12 + root + base + accidental;
    }
    while (key < 0)
        key += 12;
    while (key > 127)
        key -= 12;
    return key;
}

DmPerformance::DmPerformance(int rate, Loader loader, uint32_t seed)
    : rate_(rate), loader_(std::move(loader)), random_(seed), synth_(rate) {
    bendRange_.fill(2);
}

std::shared_ptr<const DmSegment> DmPerformance::segment(const std::string& file) {
    if (auto it = segments_.find(file); it != segments_.end())
        return it->second;
    std::vector<uint8_t> data;
    auto s = std::make_shared<DmSegment>();
    if (!loader_(file, data) || readDmSegment(data, *s))
        s.reset();
    segments_[file] = s;
    return s;
}

std::shared_ptr<const DmStyle> DmPerformance::style(const std::string& file) {
    if (auto it = styles_.find(file); it != styles_.end())
        return it->second;
    std::vector<uint8_t> data;
    auto s = std::make_shared<DmStyle>();
    if (!loader_(file, data) || readDmStyle(data, *s))
        s.reset();
    styles_[file] = s;
    return s;
}

const DlsCollection* DmPerformance::collection(const std::string& file) {
    // Loaded by play(): the audio thread only looks.
    const auto it = collections_.find(file);
    return it != collections_.end() ? it->second.get() : nullptr;
}

void DmPerformance::applyBand(const DmBand& band) {
    for (const DmInstrument& i : band.instruments) {
        if (i.channel < 0 || i.channel >= 64)
            continue;
        const DlsCollection* c = i.dls.file.empty() ? nullptr : collection(i.dls.file);
        const uint32_t bank = ((i.patch >> 16) & 0x7f) << 8 | ((i.patch >> 8) & 0x7f) | (i.patch & 0x80000000u);
        const DlsInstrument* inst = c ? c->find(bank, i.patch & 0x7f) : nullptr;
        if (!inst)
            continue; // a General MIDI instrument: none to play here
        synth_.setInstrument(i.channel, c, inst);
        synth_.setVolume(i.channel, i.volume);
        synth_.setPan(i.channel, i.pan);
        synth_.setTranspose(i.channel, i.transpose);
    }
}

Error DmPerformance::play(const std::string& file) { return load(file, true); }

Error DmPerformance::prepare(const std::string& file) { return load(file, false); }

Error DmPerformance::load(const std::string& file, bool queue) {
    // Everything it needs is read here, on the caller's thread: the
    // segment, its style and the DLS collections its bands name.
    auto s = segment(file);
    if (!s)
        return Error::fail("can't read segment " + file);
    std::shared_ptr<const DmStyle> st;
    if (!s->style.file.empty() && !(st = style(s->style.file)))
        return Error::fail("can't read style " + s->style.file);
    std::vector<std::string> wanted;
    for (const DmBand& b : s->bands)
        for (const DmInstrument& i : b.instruments)
            wanted.push_back(i.dls.file);
    if (st)
        for (const DmBand& b : st->bands)
            for (const DmInstrument& i : b.instruments)
                wanted.push_back(i.dls.file);
    std::map<std::string, std::unique_ptr<DlsCollection>> loaded;
    for (const std::string& f : wanted) {
        if (f.empty() || loaded.count(f) || known_.count(f))
            continue;
        std::vector<uint8_t> data;
        auto c = std::make_unique<DlsCollection>();
        if (!loader_(f, data) || readDls(data, *c))
            c.reset();
        loaded[f] = std::move(c);
        known_.insert(f);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [f, c] : loaded)
        collections_[f] = std::move(c);
    if (queue) {
        pending_ = s;
        pendingStyle_ = st;
    }
    return Error::success();
}

void DmPerformance::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    playing_.reset();
    pending_.reset();
    pendingStyle_.reset();
    events_.clear();
    synth_.releaseAll();
}

bool DmPerformance::playing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playing_ || pending_;
}

std::string DmPerformance::current() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return playing_ ? playing_->segment->name : std::string();
}

void DmPerformance::setVolume(float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    volume_ = std::max(0.0f, volume);
}

void DmPerformance::startPending() {
    // Whatever was playing lets go; the new segment starts here.
    events_.clear();
    synth_.releaseAll();
    playing_ = std::make_unique<Playing>();
    playing_->segment = std::move(pending_);
    pending_.reset();
    playing_->style = std::move(pendingStyle_);
    playing_->segmentTick = playing_->segment->playStart;
    playing_->repeatsLeft = playing_->segment->repeats;
    if (playing_->segment->bands.empty() && playing_->style && !playing_->style->bands.empty())
        applyBand(playing_->style->bands.front());
    variationState_.clear();
    instanceVariation_.clear();
    measureEnd_ = performanceTick_;
}

double DmPerformance::tempoAt(double tick) const {
    double tempo = playing_ && playing_->style ? playing_->style->tempo : 120;
    if (playing_)
        for (const auto& [time, bpm] : playing_->segment->tempos)
            if (time <= tick)
                tempo = bpm;
    return tempo;
}

const DmChord& DmPerformance::chordAt(double tick) const {
    const DmChord* c = &defaultChord_;
    if (playing_)
        for (const DmChord& ch : playing_->segment->chords)
            if (ch.time <= tick)
                c = &ch;
    return *c;
}

void DmPerformance::choosePattern(int measure) {
    Playing& p = *playing_;
    if (!p.style)
        return;
    // A command at this measure: its groove level (randomised within its
    // range), and any embellishment it asks for.
    const DmCommand* command = nullptr;
    for (const DmCommand& c : p.segment->commands)
        if (c.measure == measure)
            command = &c;
    int wanted = 0;
    if (command) {
        if (command->groove > 0) {
            const int range = command->grooveRange & ~1;
            p.groove = std::clamp(command->groove +
                                      (range ? std::uniform_int_distribution<int>(-range / 2, range / 2)(random_) : 0),
                                  1, 100);
        }
        switch (command->command) {
        case kCommandFill: wanted = kDmFill; break;
        case kCommandIntro: wanted = kDmIntro; break;
        case kCommandBreak: wanted = kDmBreak; break;
        case kCommandEnd: wanted = kDmEnd; break;
        case kEndAndIntro: wanted = kDmEnd | kDmIntro; break;
        default: break;
        }
    }
    const bool fits = p.pattern && p.groove >= p.pattern->grooveBottom && p.groove <= p.pattern->grooveTop;
    if (p.pattern && p.measuresLeft > 0 && !wanted && fits)
        return; // the pattern plays on
    // Candidates: of the kind asked for (else plain ones), at the groove
    // level (else those nearest it).
    std::vector<const DmPattern*> best;
    int bestDistance = 1 << 30;
    for (int pass = 0; pass < 2 && best.empty(); ++pass)
        for (const DmPattern& pt : p.style->patterns) {
            if (pt.embellishment & kDmMotif)
                continue;
            if (pass == 0 ? !(wanted ? (pt.embellishment & wanted) : pt.embellishment == 0) : pt.embellishment != 0)
                continue;
            const int distance =
                p.groove < pt.grooveBottom ? pt.grooveBottom - p.groove : std::max(0, p.groove - pt.grooveTop);
            if (distance < bestDistance) {
                best.clear();
                bestDistance = distance;
            }
            if (distance == bestDistance)
                best.push_back(&pt);
        }
    if (best.empty())
        return;
    p.pattern = best[std::uniform_int_distribution<size_t>(0, best.size() - 1)(random_)];
    p.measuresLeft = p.pattern->measures;
}

void DmPerformance::addCurve(const DmCurve& c, double start, int channel) {
    auto valueAt = [&](float x) {
        return float(c.startValue) + float(c.endValue - c.startValue) * curveShape(c.shape, std::clamp(x, 0.0f, 1.0f));
    };
    const int steps = c.shape == kShapeInstant || c.duration <= 0 ? 0 : std::max(1, c.duration / kCurveStep);
    for (int s = 0; s <= steps; ++s) {
        Event e;
        e.tick = start + (steps ? double(c.duration) * s / steps : 0);
        e.channel = channel;
        e.value = steps ? valueAt(float(s) / float(steps)) : float(c.endValue);
        if (c.type == kCurvePitchBend) {
            e.kind = 3;
        } else if (c.type == kCurveController) {
            e.kind = 2;
            e.data1 = c.cc;
        } else {
            continue;
        }
        events_.push_back(e);
    }
}

void DmPerformance::addPattern(const DmPattern& pattern, int measureInPattern, double start) {
    Playing& p = *playing_;
    // Each part's variation, chosen as the pattern starts; parts with a
    // lock id share theirs.
    std::map<int, int> locked;
    const size_t patternIndex = size_t(&pattern - p.style->patterns.data());
    for (size_t r = 0; r < pattern.parts.size(); ++r) {
        const DmPartRef& ref = pattern.parts[r];
        const DmPart& part = p.style->parts[ref.part];
        const size_t key = patternIndex << 8 | r;
        if (measureInPattern == 0 || !instanceVariation_.count(key)) {
            std::vector<int> choices = part.variations();
            if (choices.empty())
                choices.push_back(0);
            int chosen;
            if (ref.lockId && locked.count(ref.lockId)) {
                chosen = locked[ref.lockId];
            } else {
                const auto last = variationState_.find(key);
                const int previous = last != variationState_.end() ? last->second : -1;
                const auto pick = [&] {
                    return choices[std::uniform_int_distribution<size_t>(0, choices.size() - 1)(random_)];
                };
                switch (ref.randomVariation) {
                case 0:   // in order
                case 2: { // in order from a random start
                    auto at = std::find(choices.begin(), choices.end(), previous);
                    if (at == choices.end())
                        chosen = ref.randomVariation == 0 ? choices.front() : pick();
                    else
                        chosen = ++at == choices.end() ? choices.front() : *at;
                    break;
                }
                default: // at random; the no-repeat modes never the last one again
                    chosen = pick();
                    if (ref.randomVariation >= 3 && choices.size() > 1 && chosen == previous) {
                        const size_t i = size_t(std::find(choices.begin(), choices.end(), chosen) - choices.begin());
                        chosen = choices[(i + 1) % choices.size()];
                    }
                    break;
                }
                if (ref.lockId)
                    locked[ref.lockId] = chosen;
            }
            variationState_[key] = chosen;
            instanceVariation_[key] = chosen;
        }
        const uint32_t mask = uint32_t(1) << instanceVariation_[key];
        const int playMode = part.playMode;
        const int measure = measureInPattern % std::max(1, part.measures);
        const int measureTicks = part.timeSig.measureTicks();
        for (const DmNote& n : part.notes) {
            if (!(n.variations & mask))
                continue;
            const int at = part.timeSig.gridTicks(n.grid);
            if (at / measureTicks != measure)
                continue;
            double tick = start + double(at - measure * measureTicks + n.offset);
            if (n.timeRange) {
                const int r = actualRange(n.timeRange) / 2;
                tick += std::uniform_int_distribution<int>(-r, r)(random_);
            }
            int duration = n.duration;
            if (n.durationRange) {
                const int r = actualRange(n.durationRange) / 2;
                duration += std::uniform_int_distribution<int>(-r, r)(random_);
            }
            int velocity = n.velocity;
            if (n.velocityRange)
                velocity += std::uniform_int_distribution<int>(-n.velocityRange / 2, n.velocityRange / 2)(random_);
            const int mode = n.playMode == kPartsMode ? playMode : n.playMode;
            const int key =
                dmMusicToMidi(n.musicValue, mode, chordAt(p.segmentTick + double(at - measure * measureTicks)));
            if (key < 0)
                continue;
            tick = std::max(tick, performanceTick_);
            events_.push_back({tick, 1, ref.channel, key, std::clamp(velocity, 1, 127), 0});
            events_.push_back({tick + std::max(1, duration), 0, ref.channel, key, 0, 0});
        }
        for (const DmCurve& c : part.curves) {
            if (!(c.variations & mask))
                continue;
            const int at = part.timeSig.gridTicks(c.grid);
            if (at / measureTicks != measure)
                continue;
            addCurve(c, std::max(performanceTick_, start + double(at - measure * measureTicks + c.offset)),
                     ref.channel);
        }
    }
}

void DmPerformance::composeMeasure(double start) {
    Playing& p = *playing_;
    const DmSegment& s = *p.segment;
    const int measureTicks = p.style ? p.style->timeSig.measureTicks() : DmTimeSig{}.measureTicks();
    // Looping: back from the loop's end while repeats are left.
    const int loopEnd = s.loopEnd > s.loopStart ? s.loopEnd : s.length;
    if (p.segmentTick >= loopEnd && p.repeatsLeft > 0 && loopEnd > s.loopStart) {
        --p.repeatsLeft;
        p.segmentTick = s.loopStart;
        p.measuresLeft = 0;
    }
    if (p.segmentTick >= s.length) {
        playing_.reset(); // finished
        return;
    }
    const double from = p.segmentTick, to = from + measureTicks;
    // Bands due.
    while (p.bandIndex < s.bands.size() && s.bands[p.bandIndex].time < to)
        applyBand(s.bands[p.bandIndex++]);
    const int measure = int(from) / measureTicks;
    choosePattern(measure);
    if (p.pattern) {
        addPattern(*p.pattern, p.pattern->measures - p.measuresLeft, start);
        --p.measuresLeft;
    }
    // A sequence track's own notes and curves in this measure.
    for (const DmSequenceEvent& e : s.sequence) {
        if (e.time < from || e.time >= to)
            continue;
        const double tick = start + (e.time - from);
        if ((e.status & 0xf0) == 0x90 && e.data2 > 0) {
            events_.push_back({tick, 1, e.channel, e.data1, e.data2, 0});
            events_.push_back({tick + std::max(1, e.duration), 0, e.channel, e.data1, 0, 0});
        } else if ((e.status & 0xf0) == 0xb0) {
            events_.push_back({tick, 2, e.channel, e.data1, 0, float(e.data2)});
        }
    }
    for (const DmCurve& c : s.sequenceCurves)
        if (c.grid >= from && c.grid < to)
            addCurve(c, start + (c.grid - from), c.channel);
    std::stable_sort(events_.begin(), events_.end(), [](const Event& a, const Event& b) { return a.tick < b.tick; });
    p.segmentTick = to;
    measureEnd_ = start + measureTicks;
}

void DmPerformance::dispatch(const Event& e) {
    switch (e.kind) {
    case 0: synth_.noteOff(e.channel, e.data1); break;
    case 1: synth_.noteOn(e.channel, e.data1, e.data2); break;
    case 2: {
        const int v = std::clamp(int(std::lround(e.value)), 0, 127);
        switch (e.data1) {
        case 1: synth_.setModWheel(e.channel, v); break;
        case 7: synth_.setVolume(e.channel, v); break;
        case 10: synth_.setPan(e.channel, v); break;
        case 11: synth_.setExpression(e.channel, v); break;
        case 64: synth_.setSustain(e.channel, v >= 64); break;
        default: break;
        }
        break;
    }
    case 3:
        synth_.setPitchBend(e.channel, (e.value - 8192) / 8192 * bendRange_[size_t(e.channel) % bendRange_.size()]);
        break;
    default: break;
    }
}

void DmPerformance::read(float* out, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    int done = 0;
    while (done < frames) {
        if (!playing_ && pending_)
            startPending();
        if (playing_ && performanceTick_ + 1e-6 >= measureEnd_) {
            if (pending_)
                startPending();
            composeMeasure(measureEnd_);
        }
        size_t used = 0;
        while (used < events_.size() && events_[used].tick <= performanceTick_ + 1e-6)
            dispatch(events_[used++]);
        events_.erase(events_.begin(), events_.begin() + std::ptrdiff_t(used));
        int n = frames - done;
        double ticksPerFrame = 0;
        if (playing_) {
            ticksPerFrame = tempoAt(playing_->segmentTick) / 60.0 * kDmPulsesPerQuarter / rate_;
            double next = measureEnd_;
            if (!events_.empty())
                next = std::min(next, events_.front().tick);
            n = std::clamp(int(std::ceil((next - performanceTick_) / ticksPerFrame - 1e-9)), 1, n);
        } else if (!events_.empty()) {
            // Note-offs left after the music stopped.
            for (const Event& e : events_)
                if (e.kind == 0)
                    dispatch(e);
            events_.clear();
        }
        synth_.render(out + 2 * done, size_t(n), volume_ * kMasterGain);
        performanceTick_ += n * ticksPerFrame;
        done += n;
    }
}

} // namespace reluva::audio
