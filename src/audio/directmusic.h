// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/dls.h"
#include "audio/mixer.h"
#include "audio/synth.h"
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
// DLS instruments. See docs/common/audio/directmusic.md.

/** Music time's ticks per quarter note. */
constexpr int kDmPulsesPerQuarter = 768;

/** A time signature, and the grid notes are placed on. */
struct DmTimeSig {
    int beatsPerMeasure = 4;
    int beat = 4; // the note a beat is (4: a quarter)
    int gridsPerBeat = 4;
    /** A beat's and a measure's ticks; a beat of 0 counts as a 256th note. */
    ///@{
    int beatTicks() const { return beat > 0 ? kDmPulsesPerQuarter * 4 / beat : kDmPulsesPerQuarter / 64; }
    int measureTicks() const { return beatsPerMeasure * beatTicks(); }
    ///@}
    /**
     * A grid position to ticks (Microsoft's formula: whole beats, then
     * the grids left over).
     */
    int gridTicks(int grid) const;
};

/** A reference to another file (DMRF): its name and file name. */
struct DmReference {
    std::string name;
    std::string file;
};

/** One instrument of a band: the DLS patch a channel plays, and its controls. */
struct DmInstrument {
    uint32_t patch = 0; // MSB << 16 | LSB << 8 | program, and bit 31 for drums
    int channel = 0;
    uint32_t flags = 0;
    int pan = 64;
    int volume = 100;
    int transpose = 0;
    DmReference dls;
};

/** A band: the instruments to set, at a time in the segment (ticks). */
struct DmBand {
    int time = 0;
    std::string name;
    std::vector<DmInstrument> instruments;
};

/** A note of a style's part, as a music value on the part's grid. */
struct DmNote {
    int grid = 0;
    uint32_t variations = 0;
    int duration = 0;
    int offset = 0;
    uint16_t musicValue = 0;
    int velocity = 100;
    int timeRange = 0;
    int durationRange = 0;
    int velocityRange = 0;
    int playMode = 16; // 16: the part's
};

/** A controller or pitch bend curve, of a style's part or a sequence track. */
struct DmCurve {
    int grid = 0; // or, in a sequence track, ticks
    uint32_t variations = 0;
    int duration = 0;
    int offset = 0;
    int startValue = 0;
    int endValue = 0;
    int type = 0;
    int shape = 0;
    int cc = 0;
    int channel = 0; // in a sequence track
};

/** A style's part: its notes and curves, in up to 32 variations. */
struct DmPart {
    std::array<uint8_t, 16> guid{};
    DmTimeSig timeSig;
    std::array<uint32_t, 32> variationChoices{};
    int measures = 1;
    int playMode = 0;
    std::vector<DmNote> notes;
    std::vector<DmCurve> curves;
    /** The variations in use (any chord bit set), in order. */
    std::vector<int> variations() const;
};

/** A pattern's use of a part: on which channel, and how its variation is chosen. */
struct DmPartRef {
    size_t part = 0; // into DmStyle::parts
    int channel = 0;
    int lockId = 0;
    int subChordLevel = 0;
    int randomVariation = 1;
};

/** DMUS_EMBELLISHT bits. */
enum : int { kDmFill = 1, kDmBreak = 2, kDmIntro = 4, kDmEnd = 8, kDmMotif = 16 };

/** A style's pattern: the parts it plays, at which groove levels and for what. */
struct DmPattern {
    std::string name;
    DmTimeSig timeSig;
    int grooveBottom = 1;
    int grooveTop = 100;
    int embellishment = 0;
    int measures = 1;
    std::vector<DmPartRef> parts;
};

/** A style (.sty) as readDmStyle reads it. */
struct DmStyle {
    std::string name;
    DmTimeSig timeSig;
    double tempo = 120;
    std::vector<DmPart> parts;
    std::vector<DmPattern> patterns;
    std::vector<DmBand> bands;
};

/** A command track's entry (DMUS_COMMANDT): groove, fill, intro, break, end, end and intro. */
struct DmCommand {
    int time = 0;
    int measure = 0;
    int beat = 0;
    int command = 0;
    int groove = 0;
    int grooveRange = 0;
};

/**
 * A chord, as its first subchord: semitones above its root (bit 0 the
 * root), and the scale's, from its root. Without a chord track:
 * C major 7 over the C major scale (docs/common/audio/directmusic.md).
 */
struct DmChord {
    int time = 0;
    uint32_t chordPattern = 0x891;
    uint32_t scalePattern = 0xab5ab5;
    int chordRoot = 0;
    int scaleRoot = 0;
};

/** A sequence track's MIDI event; a note's duration in ticks. */
struct DmSequenceEvent {
    int time = 0;
    int duration = 0;
    int channel = 0;
    int status = 0;
    int data1 = 0;
    int data2 = 0;
};

/** A segment (.sgt) as readDmSegment reads it: times in ticks. */
struct DmSegment {
    std::string name;
    int repeats = 0;
    int length = 0;
    int playStart = 0;
    int loopStart = 0;
    int loopEnd = 0;
    DmReference style;
    std::vector<DmBand> bands;
    std::vector<DmCommand> commands;
    std::vector<std::pair<int, double>> tempos; // time, beats per minute
    std::vector<DmChord> chords;
    std::vector<DmSequenceEvent> sequence;
    std::vector<DmCurve> sequenceCurves;
};

/**
 * A style, segment or band file (a RIFF form of DMST, DMSG or DMBD).
 * @return An error if it isn't one, or (a segment) has no tracks.
 */
///@{
Error readDmStyle(const std::vector<uint8_t> &data, DmStyle &out);
Error readDmSegment(const std::vector<uint8_t> &data, DmSegment &out);
Error readDmBand(const std::vector<uint8_t> &data, DmBand &out);
///@}

/**
 * A music value to a MIDI key against a chord in a play mode
 * (DMUS_PLAYMODE flags), moved by octaves into 0-127; -1 if it isn't to
 * be played.
 */
int dmMusicToMidi(uint16_t musicValue, int playMode, const DmChord &chord);

/**
 * Plays segments: composes their styles' patterns as they go, into a DLS
 * synthesizer, as a mixer stream (Mixer::playOutput). play() switches
 * segment at the next measure. Thread-safe: read() runs on the audio
 * thread.
 */
class DmPerformance : public OutputStream {
public:
    /**
     * Reads a file the music names (a segment, style, band or DLS
     * collection, by its file name); false if there's none.
     */
    using Loader = std::function<bool(const std::string &file, std::vector<uint8_t> &out)>;

    /**
     * A performance at `rate` frames a second (the mixer's output rate),
     * its random choices seeded with `seed`.
     */
    DmPerformance(int rate, Loader loader, uint32_t seed = 1);

    /**
     * Starts a segment (by file name), at the next measure of the one
     * playing (now if none). Reads its files first: call from one thread.
     * @return An error if the segment or its style can't be read; a DLS
     *         collection that can't is left silent.
     */
    Error play(const std::string &segmentFile);
    /** Reads a segment's files ahead, so play() later has nothing to read. */
    Error prepare(const std::string &segmentFile);
    /**
     * A style's motif (a pattern marked as one) over the segment playing,
     * from now, with a band for its instruments (empty: none). Reads its
     * files first: call from the same thread as play(). Fails if no
     * segment is playing.
     */
    Error playMotif(const std::string &styleFile, const std::string &motif, const std::string &bandFile);
    /** Stops the music, and a segment waiting to start; notes fade by their release. */
    void stop();
    /** Whether a segment is playing or waiting to start. */
    bool playing() const;
    /** The name of the segment playing ("" if none). */
    std::string current() const;
    /** The music's volume: 1 as composed, 0 silent (below 0 counts as 0). */
    void setVolume(float volume);

    /** Adds `frames` stereo frames at the performance's rate into `out` (interleaved, full scale +-1). */
    void render(float *out, int frames);
    /** The mixer's read: `frames` frames rendered and put into 16 bits (the performance runs at the output's rate). */
    void read(int16_t *out, int frames, int rate) override;

private:
    struct Event {
        double tick = 0; // performance ticks
        int kind = 0;    // 0 note off, 1 note on, 2 controller, 3 pitch bend
        int channel = 0;
        int data1 = 0;
        int data2 = 0;
        float value = 0;
    };
    struct Playing {
        std::shared_ptr<const DmSegment> segment;
        std::shared_ptr<const DmStyle> style;
        double segmentTick = 0; // where in the segment
        int repeatsLeft = 0;
        // The pattern playing: which, from which segment measure, its
        // measures left.
        const DmPattern *pattern = nullptr;
        int measuresLeft = 0;
        int nextMeasure = 0; // the next measure (from the segment's start) to compose
        int groove = 1;
        size_t bandIndex = 0;
    };

    Error load(const std::string &file, bool queue);
    std::shared_ptr<const DmSegment> segment(const std::string &file);
    std::shared_ptr<const DmStyle> style(const std::string &file);
    const DlsCollection *collection(const std::string &file);
    void applyBand(const DmBand &band);
    void startPending();
    // Composes the measure starting at the segment tick, at the
    // performance tick.
    void composeMeasure(double performanceTick);
    void choosePattern(int measure);
    void addPattern(const DmStyle &style, const DmPattern &pattern, int measureInPattern, double performanceTick);
    void addCurve(const DmCurve &c, double start, int channel);
    double tempoAt(double segmentTick) const;
    const DmChord &chordAt(double segmentTick) const;
    void dispatch(const Event &e);

    int _rate;
    Loader _loader;
    std::mt19937 _random;
    mutable std::mutex _mutex;
    DlsSynth _synth;
    float _volume = 1;
    std::map<std::string, std::shared_ptr<const DmSegment>> _segments;
    std::map<std::string, std::shared_ptr<const DmStyle>> _styles;
    std::map<std::string, std::unique_ptr<DlsCollection>> _collections;
    std::unique_ptr<Playing> _playing;
    std::shared_ptr<const DmSegment> _pending;
    std::shared_ptr<const DmStyle> _pendingStyle;
    std::set<std::string> _known; // collections play() has read (the caller's thread's)
    double _performanceTick = 0;
    double _measureEnd = 0;                   // performance tick where the measure being played ends
    std::vector<Event> _events;               // kept sorted by tick
    std::map<size_t, int> _variationState;    // each pattern part's last variation (for the in-order modes)
    std::map<size_t, int> _instanceVariation; // and the one it plays now
    std::array<float, 64> _bendRange{};
    DmChord _defaultChord;
};

} // namespace reluva::audio
