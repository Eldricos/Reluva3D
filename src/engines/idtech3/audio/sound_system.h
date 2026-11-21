// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/collision.h"
#include "engines/idtech3/audio/dynamic_music.h"
#include "engines/idtech3/audio/sounds.h"

#include "audio/mixer.h"

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

// The sounds of a level, as the original's sound system plays what the
// client game asks for (see docs/engines/idtech3/README.md): sounds placed in the
// world or on entities, heard from the listener's position, louder near
// and panned to the side they're on; looping sounds the client game
// re-adds each frame; the music.
class SoundSystem {
public:
    SoundSystem(audio::Mixer &mixer, SoundLibrary &sounds) : _mixer(mixer), _sounds(sounds) {}
    ~SoundSystem() { stopAll(); }
    SoundSystem(const SoundSystem &) = delete;
    SoundSystem &operator=(const SoundSystem &) = delete;

    // A new sound never cuts one already playing, on any channel; the same
    // sound the entity started under 50 ms ago (at the same spot, if it
    // was given one) is a repeat and skipped, as the original's. (ioq3 also caps each entity at 5 of one sound at
    // once, which silenced rapid fire here, so that's left out.) Channel 0
    // is the original's "any channel".
    static constexpr int kAutoChannel = 0;
    // The time now, for telling repeats; each frame before the client game.
    void setTime(uint64_t ms) { _nowMs = ms; }

    // A sound at `origin`, or (without one) following entity `entity`.
    void startSound(const std::optional<Vec3f> &origin, int entity, int channel, const std::string &name);
    // A sound heard as it is, wherever the listener is (the announcer,
    // the menus' sounds).
    void startLocalSound(const std::string &name, int channel);
    // Looping sounds: those added between clearLoopingSounds() and
    // endFrame() play; the rest stop.
    void clearLoopingSounds();
    void addLoopingSound(int entity, const Vec3f &origin, const std::string &name);
    // Jedi Outcast's: the entity's looping sounds stop now, and its sound
    // on `channel` (1 and up) is silenced.
    void stopLoopingSound(int entity);
    void muteSound(int entity, int channel);
    void updateEntityPosition(int entity, const Vec3f &origin);
    // Ambient set sounds (AmbientSets): at a volume of their own (0 to 1)
    // rather than faded by distance, though still panned to where they
    // are. Loops play while added each frame (until endFrame()).
    void addAmbientLoop(int entity, const Vec3f &origin, float volume, const std::string &name);
    void startAmbientSound(int entity, const Vec3f &origin, float volume, const std::string &name);
    // Where the listener is (entity `entity`) and which way it faces:
    // forward, left, up.
    void respatialize(int entity, const Vec3f &origin, const std::array<Vec3f, 3> &axes);
    // The music: `intro` once, then `loop` round and round (either may be
    // empty); names without an extension are tried as .mp3 and .wav.
    void startBackgroundTrack(const std::string &intro, const std::string &loop);
    void stopBackgroundTrack();
    // Jedi Outcast's dynamic music: dms.dat's text (empty: none), the
    // level's name (the set played when the music asked for is ""), and
    // s_allowDynamicMusic.
    void setMusicData(std::string dms) { _dms = std::move(dms); }
    void setMapName(const std::string &map) { _mapName = map; }
    void setAllowDynamicMusic(bool allow) { _allowDynamic = allow; }
    // s_debugdynamic: what the dynamic music prints (DynamicMusic::debug).
    void setDebugDynamic(int level) { _dynamic.debug = level; }
    // s_khz (11, 22 or 44): the original's output rate, by which its music
    // is decoded ahead (DynamicMusic::decodeLead).
    void setOutputKhz(int khz);
    // developer: the music's developer prints too.
    void setDeveloper(bool on) { _developer = on; }
    // s_info's text (S_SoundInfo_f, 0x422fe0), what Reluva has of it.
    std::string info() const;
    // The s_dynamic command (0x426c80): its words after the command's.
    void dynamicCommand(const std::vector<std::string> &args);
    // Its S_StartBackgroundTrack (0x427350): a file, if `intro` names one
    // with a path, as above; else the dynamic set it names (or the map's).
    void startMusic(const std::string &intro, const std::string &loop);
    // The same again, as after a cinematic (0x427310).
    void restartMusic();
    // The state the game asks for, configstring 907; nullptr while unset.
    void setMusicState(const std::string *state);
    const DynamicMusic &dynamicMusic() const { return _dynamic; }
    // Where the music's messages go (the console).
    std::function<void(const std::string &)> print;
    // Each frame, after the client game's: positions and volumes brought
    // up to date, looping sounds not re-added stopped, music moved on.
    void endFrame();
    // The effects' and music's volumes, 0 to 1.
    void setVolumes(float effects, float music);
    void stopAll();
    // Every sound but the music (S_StopSounds).
    void stopSounds();
    size_t playingCount() const { return _playing.size(); }
    // Sounds asked for that couldn't be found or read.
    const std::set<std::string> &missing() const { return _missing; }
    // The sounds asked for in the last two seconds, by name and entity
    // (the latest six): how many times, and what became of the last.
    std::vector<std::string> recent() const;
    // The looping sounds playing now.
    std::vector<std::string> loopingNow() const;

    // How loud (0..1) and where (-1 left .. 1 right) a sound at `origin`
    // is heard from the listener: full volume within 80 units, then
    // fading by 0.0008 a unit; panned by how far to the listener's right
    // it is.
    std::pair<float, float> spatialize(const Vec3f &origin) const;

private:
    void say(const std::string &text) const; // to print, if set
    struct Playing {
        audio::SoundHandle handle = audio::kNoSound;
        int entity = -1;
        int channel = 0;
        std::optional<Vec3f> origin; // fixed; else the entity's
        bool local = false;
        std::string name;
        uint64_t startMs = 0;
        std::optional<float> volume; // ambient: its own volume
    };
    // Whether `entity` may start `name` now (see kAutoChannel).
    bool allowed(int entity, bool local, const std::string &name, const std::optional<Vec3f> &origin) const;
    std::shared_ptr<const audio::Sound> find(const std::string &name);
    // Sets the sound's volume and pan for where it is; the volume.
    // An ambient sound's own volume replaces the one for its distance.
    float place(audio::SoundHandle handle, const std::optional<Vec3f> &origin, int entity,
                std::optional<float> volume = std::nullopt);

    audio::Mixer &_mixer;
    SoundLibrary &_sounds;
    std::vector<Playing> _playing;
    std::map<int, Vec3f> _entities;
    // Looping sounds by (entity, name): playing, and added this frame.
    std::map<std::pair<int, std::string>, audio::SoundHandle> _loops;
    std::map<std::pair<int, std::string>, Vec3f> _loopsAdded;
    // Ambient loops, the same way, with their volumes.
    std::map<std::pair<int, std::string>, audio::SoundHandle> _ambientLoops;
    std::map<std::pair<int, std::string>, std::pair<Vec3f, float>> _ambientAdded;
    void note(const std::string &name, int entity, std::string line);
    struct Start {
        uint64_t ms = 0;
        std::string name;
        int entity = 0;
        std::string line;
    };
    std::deque<Start> _starts;
    std::set<std::string> _missing;
    int _listener = -1;
    uint64_t _nowMs = 0;
    Vec3f _listenerOrigin{};
    std::array<Vec3f, 3> _listenerAxes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    audio::SoundHandle _music = audio::kNoSound;
    std::shared_ptr<const audio::Sound> _musicLoop;
    bool _musicIntro = false;
    // Jedi Outcast's: the names last asked for, and what plays for them.
    std::string _dms;
    std::string _mapName;
    bool _allowDynamic = true;
    bool _developer = false;
    std::string _musicName;
    std::string _musicLoopName;
    bool _silenced = false; // configstring 907 "silence": a plain track muted
    DynamicMusic _dynamic{_mixer};
};

} // namespace reluva::idtech3
