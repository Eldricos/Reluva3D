// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/audio/dls.h"
#include "common/audio/mixer.h"
#include "common/audio/synth.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace reluva::audio {

// DirectMusic's style-based music: segments (.sgt) whose command track
// asks a style (.sty) for patterns at groove levels, whose parts' notes,
// as music values, become MIDI notes over the chord, played by bands of
// DLS instruments. See docs/formats/directmusic.md.

constexpr int kDmPulsesPerQuarter = 768; // music time ticks per quarter note

struct DmTimeSig {
    int beatsPerMeasure = 4, beat = 4, gridsPerBeat = 4;
    int beatTicks() const { return beat > 0 ? kDmPulsesPerQuarter * 4 / beat : kDmPulsesPerQuarter / 64; }
    int measureTicks() const { return beatsPerMeasure * beatTicks(); }
    // A grid position to ticks (Microsoft's formula: whole beats, then
    // the grids left over).
    int gridTicks(int grid) const;
};

// A reference to another file (DMRF): its name and file name.
struct DmReference {
    std::string name, file;
};

struct DmInstrument {
    uint32_t patch = 0; // MSB << 16 | LSB << 8 | program, and bit 31 for drums
    int channel = 0;
    uint32_t flags = 0;
    int pan = 64, volume = 100, transpose = 0;
    DmReference dls;
};

struct DmBand {
    int time = 0;
    std::string name;
    std::vector<DmInstrument> instruments;
};

struct DmNote {
    int grid = 0;
    uint32_t variations = 0;
    int duration = 0, offset = 0;
    uint16_t musicValue = 0;
    int velocity = 100, timeRange = 0, durationRange = 0, velocityRange = 0;
    int playMode = 16; // 16: the part's
};

struct DmCurve {
    int grid = 0; // or, in a sequence track, ticks
    uint32_t variations = 0;
    int duration = 0, offset = 0;
    int startValue = 0, endValue = 0;
    int type = 0, shape = 0, cc = 0;
    int channel = 0; // in a sequence track
};

struct DmPart {
    std::array<uint8_t, 16> guid{};
    DmTimeSig timeSig;
    std::array<uint32_t, 32> variationChoices{};
    int measures = 1;
    int playMode = 0;
    std::vector<DmNote> notes;
    std::vector<DmCurve> curves;
    // The variations in use (any chord bit set).
    std::vector<int> variations() const;
};

struct DmPartRef {
    size_t part = 0; // into DmStyle::parts
    int channel = 0;
    int lockId = 0, subChordLevel = 0, randomVariation = 1;
};

// DMUS_EMBELLISHT bits.
enum : int { kDmFill = 1, kDmBreak = 2, kDmIntro = 4, kDmEnd = 8, kDmMotif = 16 };

struct DmPattern {
    std::string name;
    DmTimeSig timeSig;
    int grooveBottom = 1, grooveTop = 100, embellishment = 0, measures = 1;
    std::vector<DmPartRef> parts;
};

struct DmStyle {
    std::string name;
    DmTimeSig timeSig;
    double tempo = 120;
    std::vector<DmPart> parts;
    std::vector<DmPattern> patterns;
    std::vector<DmBand> bands;
};

// DMUS_COMMANDT: groove, fill, intro, break, end, end and intro.
struct DmCommand {
    int time = 0, measure = 0, beat = 0, command = 0, groove = 0, grooveRange = 0;
};

// A chord, as its first subchord: semitones above its root (bit 0 the
// root), and the scale's, from its root. Without a chord track:
// C major 7 over the C major scale (docs/formats/directmusic.md).
struct DmChord {
    int time = 0;
    uint32_t chordPattern = 0x891, scalePattern = 0xab5ab5;
    int chordRoot = 0, scaleRoot = 0;
};

struct DmSequenceEvent {
    int time = 0, duration = 0, channel = 0, status = 0, data1 = 0, data2 = 0;
};

struct DmSegment {
    std::string name;
    int repeats = 0, length = 0, playStart = 0, loopStart = 0, loopEnd = 0;
    DmReference style;
    std::vector<DmBand> bands;
    std::vector<DmCommand> commands;
    std::vector<std::pair<int, double>> tempos; // time, beats per minute
    std::vector<DmChord> chords;
    std::vector<DmSequenceEvent> sequence;
    std::vector<DmCurve> sequenceCurves;
};

Error readDmStyle(const std::vector<uint8_t>& data, DmStyle& out);
Error readDmSegment(const std::vector<uint8_t>& data, DmSegment& out);
Error readDmBand(const std::vector<uint8_t>& data, DmBand& out);

// A music value to a MIDI key against a chord in a play mode
// (DMUS_PLAYMODE flags); -1 if it isn't to be played.
int dmMusicToMidi(uint16_t musicValue, int playMode, const DmChord& chord);

// Plays segments: composes their styles' patterns as they go, into a DLS
// synthesizer, as a mixer stream. play() switches segment at the next
// measure. Thread-safe: read() runs on the audio thread.
class DmPerformance : public Stream {
public:
    // Reads a file the music names (a segment, style, band or DLS
    // collection, by its file name); false if there's none.
    using Loader = std::function<bool(const std::string& file, std::vector<uint8_t>& out)>;

    DmPerformance(int rate, Loader loader, uint32_t seed = 1);

    // Starts a segment (by file name), at the next measure of the one
    // playing (now if none). Reads its files first: call from one thread.
    Error play(const std::string& segmentFile);
    // Reads a segment's files ahead, so play() later has nothing to read.
    Error prepare(const std::string& segmentFile);
    void stop();
    bool playing() const;
    std::string current() const;
    void setVolume(float volume);

    void read(float* out, int frames) override;

private:
    struct Event {
        double tick = 0; // performance ticks
        int kind = 0;    // 0 note off, 1 note on, 2 controller, 3 pitch bend
        int channel = 0, data1 = 0, data2 = 0;
        float value = 0;
    };
    struct Playing {
        std::shared_ptr<const DmSegment> segment;
        std::shared_ptr<const DmStyle> style;
        double segmentTick = 0; // where in the segment
        int repeatsLeft = 0;
        // The pattern playing: which, from which segment measure, its
        // measures left.
        const DmPattern* pattern = nullptr;
        int measuresLeft = 0;
        int nextMeasure = 0; // the next measure (from the segment's start) to compose
        int groove = 1;
        size_t bandIndex = 0;
    };

    Error load(const std::string& file, bool queue);
    std::shared_ptr<const DmSegment> segment(const std::string& file);
    std::shared_ptr<const DmStyle> style(const std::string& file);
    const DlsCollection* collection(const std::string& file);
    void applyBand(const DmBand& band);
    void startPending();
    // Composes the measure starting at the segment tick, at the
    // performance tick.
    void composeMeasure(double performanceTick);
    void choosePattern(int measure);
    void addPattern(const DmPattern& pattern, int measureInPattern, double performanceTick);
    void addCurve(const DmCurve& c, double start, int channel);
    double tempoAt(double segmentTick) const;
    const DmChord& chordAt(double segmentTick) const;
    void dispatch(const Event& e);

    int rate_;
    Loader loader_;
    std::mt19937 random_;
    mutable std::mutex mutex_;
    DlsSynth synth_;
    float volume_ = 1;
    std::map<std::string, std::shared_ptr<const DmSegment>> segments_;
    std::map<std::string, std::shared_ptr<const DmStyle>> styles_;
    std::map<std::string, std::unique_ptr<DlsCollection>> collections_;
    std::unique_ptr<Playing> playing_;
    std::shared_ptr<const DmSegment> pending_;
    std::shared_ptr<const DmStyle> pendingStyle_;
    std::set<std::string> known_; // collections play() has read (the caller's thread's)
    double performanceTick_ = 0;
    double measureEnd_ = 0;                   // performance tick where the measure being played ends
    std::vector<Event> events_;               // kept sorted by tick
    std::map<size_t, int> variationState_;    // each pattern part's last variation (for the in-order modes)
    std::map<size_t, int> instanceVariation_; // and the one it plays now
    std::array<float, 64> bendRange_{};
    DmChord defaultChord_;
};

} // namespace reluva::audio
