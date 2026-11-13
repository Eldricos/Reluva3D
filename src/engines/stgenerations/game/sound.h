// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "engines/stgenerations/audio.h"
#include "common/text.h"
#include "common/stream.h"
#include "engines/stgenerations/formats/script.h"

namespace reluva::stgenerations {

// A mission's sounds, after the original's sound manager (object at
// 0x53FE00).
//
// Everything a 3D level plays comes from its script's sound table
// (GAME.BIN section 3): speech, effects, ambience and music alike. Scripts
// play entries by index or through cues (section 2), which pick and chain
// several. Entries can follow a position, an actor or a billboard: they
// then fade with distance and pan with the direction from the player.
//
// File names come from each entry's key: a speaker prefix and a number,
// e.g. RIKE0107.WAV, with its subtitle in RIKE0107.TXT.
class MissionSound {
public:
    enum Flags {
        kFlagSubtitle = 0x1,
        kFlagStream = 0x4,
        kFlagLeft = 0x8,
        kFlagRight = 0x10,
        kFlagHighPriority = 0x20,
        kFlagFadeIn = 0x40,
        kFlagFadeOut = 0x80,
        kFlagMuted = 0x100
    };

    enum PositionMode { kPositionNone = 0, kPositionFixed = 1, kPositionBillboard = 2, kPositionActor = 3 };

    // A sound table entry (56 bytes).
    struct Definition {
        // Bits 0-15 number, 16-23 prefix (see getFileName()), bit 24 COMMON.
        uint32_t key;
        // 0-64.
        int volume;
        int priority;
        int positionMode;
        // The billboard or actor followed.
        uint16_t object;
        double x, y, z;
        // Silent at this distance and beyond.
        double radius;
        // Before it starts, or for fades, in ms.
        uint32_t delay;
        // Times played: 0 or 1 once, kForever forever.
        int32_t repeats;
        // Pause between repeats in ms, chosen at random in the range; 0 loops seamlessly.
        uint32_t minGap, maxGap;
        uint32_t flags;
    };

    static const int32_t kForever = 0x7FFFFFFF;

    // Where an entry's file lives.
    enum Location {
        kLocationMission, // <mission>/sounds/
        kLocationCommon,  // COMMON.DAT
        kLocationMusic    // the MUSIC.1 / MUSIC.2 archives
    };

    // Opens a sound's .WAV or .TXT by file name.
    typedef std::function<std::unique_ptr<SeekableReadStream>(const std::string &name, Location where)> FileOpener;
    // Where a billboard or actor is, for sounds that follow one.
    typedef std::function<bool(int mode, uint16_t object, double &x, double &y, double &z)> ObjectLocator;

    struct Listener {
        double x, y, z;
        // Direction of view; 0 looks along +x, pi/2 along +y.
        double yaw;
    };

    // Something for the scripts: an object to send event 46 to.
    struct Finished {
        uint16_t object;
    };

    static const int kChannels = 8;
    static const uint16_t kEventFinished = 46;

    MissionSound(const ScriptFile &script, Mixer *mixer, const FileOpener &opener, const ObjectLocator &locator);
    ~MissionSound();

    int getSoundCount() const { return (int)_sounds.size(); }
    const Definition &getDefinition(int sound) const { return _sounds[sound]; }
    int getCueCount() const { return (int)_cues.size(); }

    // The file name of an entry, e.g. "RIKE0107.WAV", and where it is.
    static std::string getFileName(uint32_t key, const char *extension, Location &where);

    // The scripts' sound natives. A reference is an index with high word 0,
    // an index with high word 0xFFFF for all its playing copies, or (handle
    // << 16) for the playing copy a play call returned handle for.

    // PLAY_SOUND: set the entry's volume and play it. Returns a handle (> 0), or -1.
    int playSound(int sound, int volume);
    // Play an entry as it is. Returns a handle (> 0), or -1.
    int play(int sound, uint16_t requester = 0);
    // PLAY_SOUND_DELAYED: set the entry's start delay and play it.
    int playDelayed(int sound, uint32_t delay);
    // PLAY_SOUND_FADE_IN: play, fading in over ms.
    int playFadeIn(uint32_t reference, uint32_t ms);
    // PLAY_SOUND_CUE: pick from a cue and play; requester gets event 46 at the end.
    void playCue(int cue, uint16_t requester);
    void stop(uint32_t reference);
    // Stop only the copy a handle names (STOP_ONE_SOUND).
    void stopOne(uint32_t reference);
    // Fade to silence over ms, then stop. Like the original, a sound that
    // isn't playing is started first. Returns false if there is no such sound.
    bool fadeOut(uint32_t reference, uint32_t ms);
    void setVolume(uint32_t reference, int volume);
    void setLoop(int sound, int32_t repeats, uint32_t minGap, uint32_t maxGap);
    void setPriority(uint32_t reference, int priority);
    void setPosition(uint32_t reference, double x, double y, double z);
    void setMuted(uint32_t reference, bool muted);
    void setObject(uint32_t reference, uint16_t object);
    void setRadius(uint32_t reference, double radius);

    // Advance by ms: start delayed sounds, run fades, repeats and cue
    // chains, and place positional sounds around the listener. Objects
    // whose sounds finished are added to finished.
    void update(uint32_t ms, const Listener &listener, std::vector<Finished> &finished);

    // A subtitle to show, if one started since the last call, and for how long.
    bool takeSubtitle(std::string &text, uint32_t &durationMs);

    // Strip the subtitle files' escapes (\T<colour>\, \W<delay>\, \B..\, \\).
    static std::string cleanSubtitle(const std::string &text);

    void stopAll();
    // Pause or resume everything playing, e.g. while the level isn't shown.
    void pauseAll(bool paused);

private:
    struct Cue {
        uint16_t number;
        uint32_t flags;
        std::vector<int32_t> members;
        std::vector<bool> used;
        bool started = false;
    };

    struct Voice;

    int startVoice(int sound, uint16_t requester, std::vector<int32_t> chain, uint32_t extraDelay);
    void beginPlayback(Voice &voice, const Listener &listener);
    void finishVoice(size_t index, std::vector<Finished> &finished);
    std::vector<int> findVoices(uint32_t reference) const;
    int definitionOf(uint32_t reference) const;
    bool placeVoice(Voice &voice, const Listener &listener, int &volume, int8_t &balance) const;
    void stealChannel();
    uint32_t random(uint32_t n);

    Mixer *_mixer;
    FileOpener _opener;
    ObjectLocator _locator;
    std::vector<Definition> _sounds;
    std::vector<Cue> _cues;
    std::vector<std::unique_ptr<Voice>> _voices;
    uint32_t _nextSerial = 1;
    uint32_t _time = 0;
    uint32_t _seed = 0x5347;
    Listener _listener = {0, 0, 0, 0};
    std::string _subtitle;
    uint32_t _subtitleDuration = 0;
    bool _hasSubtitle = false;
};

} // namespace reluva::stgenerations
