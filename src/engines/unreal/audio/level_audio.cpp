// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/audio/level_audio.h"

#include "engines/unreal/formats/sound.h"

#include <algorithm>
#include <cmath>

namespace reluva::unreal {

namespace {

constexpr float kMusicFade = 3.0f; // seconds music takes to fade in or out (the driver's)

int volumeOf(float v) {
    return std::clamp(int(std::lround(v * audio::kMaxVolume)), 0, audio::kMaxVolume);
}

float dot(const Vec3f &a, const Vec3f &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Miles' reverb filter's rooms (its EAX environments, by ZoneRoomType):
// how much of it is wet, its time in seconds, its damping (0-1 of a
// quarter of the output rate).
struct RoomPreset {
    float mix, seconds, damp;
};
constexpr RoomPreset kRooms[26] = {
    {0, 1.493f, .5f},       {.25f, .1f, 0},         {.417f, .4f, .666f},   {.653f, 1.499f, .166f},
    {.208f, .478f, 0},      {.5f, 2.309f, .888f},   {.403f, 4.279f, .5f},  {.5f, 3.961f, .5f},
    {.5f, 2.886f, .888f},   {.361f, 7.284f, .332f}, {.5f, 10, .3f},        {.153f, .259f, .999f},
    {.361f, 1.493f, 0},     {.444f, 2.697f, .638f}, {.25f, 1.752f, .776f}, {.111f, 3.145f, .472f},
    {.111f, 2.767f, .224f}, {.194f, 7.841f, .472f}, {1, 1.499f, .5f},      {.097f, 2.767f, .224f},
    {.208f, 1.652f, .999f}, {.652f, 2.886f, .25f},  {1, 1.499f, 0},        {.875f, 8.392f, .999f},
    {.139f, 10, .666f},     {.486f, 7.563f, .806f},
};

// The room's reverb on a sound: the zone's level of the room's own mix
// (a room with none, all of it), at the driver's 22,050 Hz for the
// damping's cutoff.
audio::Mixer::Reverb reverbOf(const LevelPlay::Room &room) {
    audio::Mixer::Reverb r;
    if (room.type <= 0 || room.type >= 26 || room.level <= 0)
        return r;
    const RoomPreset &p = kRooms[room.type];
    constexpr float kQuarter = 22050.0f / 4;
    r.mix = std::clamp(room.level * (p.mix <= 0.0001f ? 1 : p.mix), 0.0f, 1.0f);
    r.seconds = p.seconds;
    r.dampHz = std::clamp(kQuarter * p.damp, 200.0f, kQuarter);
    return r;
}

} // namespace

// Where a sound is as the driver hands it to its 3D provider: in the
// listener's axes (x right, y up, z ahead), x the other way with
// ReverseStereo, z made positive without UseSurround (all ahead), x and
// y nothing without UseStereo.
Vec3f LevelAudio::listenerSpace(const Vec3f &at) const {
    const Vec3f d{at[0] - _listener[0], at[1] - _listener[1], at[2] - _listener[2]};
    Vec3f p{dot(d, _right) * (_reverse ? -1.0f : 1.0f), dot(d, _up), dot(d, _forward)};
    if (!_surround)
        p[2] = std::fabs(p[2]);
    if (!_stereo)
        p[0] = p[1] = 0;
    return p;
}

// The 3D provider's side for a positional sound (Miles' fast 2D
// positional): x over the whole distance (height pulls it to the middle
// too), through its table of 127 - acos(...) x 127 / pi, as Miles' pan
// 0..127; then Miles' pan law, the near side full and the far side
// 2 x pan / 128 up to the middle. As the mixer's pan.
int LevelAudio::positionalPan(const Vec3f &at) const {
    const Vec3f p = listenerSpace(at);
    const float len = std::sqrt(dot(p, p));
    int pan = 64;
    if (len * 0.01905f > 0.0001f) {
        const float d = std::clamp(p[0] / len, -1.0f, 1.0f);
        const int i = int(std::lround((d + 1) * 1024));
        const double a = std::acos(double(i) / 1024 - 1);
        pan = std::clamp(127 - int(std::ceil(a * 127 / 3.14159265358979)), 0, 127);
    }
    auto t = [](int i) { return i <= 62 ? 2 * i : 128; };
    const float left = float(t(127 - pan)) / 128;
    const float right = float(t(pan)) / 128;
    if (right < 1)
        return -int(std::lround((1 - right) * audio::kMaxPan));
    if (left < 1)
        return int(std::lround((1 - left) * audio::kMaxPan));
    return 0;
}

// The 3D provider's: a sound behind the listener at 71% (only with
// UseSurround: without it the driver puts every sound ahead).
float LevelAudio::behind(const Vec3f &at) const {
    return listenerSpace(at)[2] < 0 ? 0.71f : 1.0f;
}

// Doppler, as the driver has it for a sound it mixes flat: 1 - the
// sound's speed toward the listener / DopplerSpeed, within 0.5 to 2.
// For a positional one its 3D provider's (Miles' fast 2D positional):
// the whole speed (not along the line), a sound of 0.355 / 5.715e-6
// units a second (about 62,100), lower moving away, higher toward,
// unclamped; away or toward from where it's handed the sound (ahead,
// without UseSurround) and its velocity in the listener's axes.
float LevelAudio::doppler(const Vec3f &at, const Vec3f &velocity, bool flat) const {
    const Vec3f d{at[0] - _listener[0], at[1] - _listener[1], at[2] - _listener[2]};
    const float speed2 = dot(velocity, velocity);
    if (speed2 <= 0)
        return 1;
    if (flat) {
        const float len = std::sqrt(dot(d, d));
        if (len <= 0 || _dopplerSpeed <= 0)
            return 1;
        return std::clamp(1 - dot(d, velocity) / len / _dopplerSpeed, 0.5f, 2.0f);
    }
    const Vec3f p = listenerSpace(at);
    const Vec3f v{dot(velocity, _right), dot(velocity, _up), dot(velocity, _forward)};
    const Vec3f after{p[0] + v[0], p[1] + v[1], p[2] + v[2]};
    const float s = std::sqrt(speed2) * 5.715e-6f * (dot(after, after) > dot(p, p) ? 1.0f : -1.0f);
    return 0.355f / std::max(0.355f + s, 0.01f);
}

// How long the provider keeps a sound's reverb going once its data has
// run out: it streams it in 32 KB buffers (of the sound as 16-bit
// stereo, 8-bit for an 8-bit one), padding with silence until its sixth
// refill: about five buffers.
float LevelAudio::reverbTail(const audio::Sound &s) {
    const int bytes = s.sourceBits == 8 ? 1 : 2;
    return s.rate > 0 ? 5 * 32768.0f / (float(s.rate) * 2 * float(bytes)) : 0;
}

LevelAudio::LevelAudio(audio::Mixer &mixer, const PackageSet &packages) : _mixer(mixer), _packages(packages) {
}

LevelAudio::~LevelAudio() {
    reset();
}

std::vector<std::string> LevelAudio::describe() const {
    std::vector<std::string> out;
    auto name = [](const Package *p, int e) { return p ? p->name() + "." + p->path(e + 1) : std::string("?"); };
    auto line = [&](const char *kind, audio::SoundHandle h, const std::string &what) {
        if (_mixer.isPlaying(h))
            out.push_back(std::string(kind) + " " + what);
    };
    if (_music)
        line("music", _music,
             _musicSound && _musicSound->package ? name(_musicSound->package, _musicSound->exportIndex) : "?");
    for (const auto &[actor, a] : _ambient)
        line(a.music ? "ambient music" : "ambient", a.handle, name(a.sound.first, a.sound.second));
    for (const Playing &p : _playing)
        line(p.everywhere ? "voice" : "sound", p.handle, name(p.sound.package, p.sound.exportIndex));
    out.push_back("room " + std::to_string(_room.type) + " at " + std::to_string(_room.level) + " (reverb mix " +
                  std::to_string(_reverb.mix) + ")");
    std::string last = std::to_string(_started) + " started; last:";
    for (const std::string &r : _recent)
        last += " " + r;
    out.push_back(last);
    return out;
}

void LevelAudio::reset() {
    for (const Playing &p : _playing)
        _mixer.stop(p.handle);
    for (const auto &[actor, a] : _ambient)
        _mixer.stop(a.handle);
    for (const Fade &f : _fading)
        _mixer.stop(f.handle);
    _mixer.stop(_music);
    _playing.clear();
    _ambient.clear();
    _fading.clear();
    _music = audio::kNoSound;
    _musicSound = nullptr;
}

std::shared_ptr<const audio::Sound> LevelAudio::load(const ObjectRef &ref) {
    if (!ref)
        return nullptr;
    const auto key = std::make_pair(ref.package, ref.exportIndex);
    auto it = _sounds.find(key);
    if (it == _sounds.end()) {
        auto s = std::make_shared<audio::Sound>();
        std::shared_ptr<const audio::Sound> loaded;
        if (!loadSound(_packages, *ref.package, ref.exportIndex, *s) && s->frames() > 0)
            loaded = std::move(s);
        it = _sounds.emplace(key, std::move(loaded)).first;
    }
    return it->second;
}

// The audio driver's: (1 - distance / radius) x volume in hundredths,
// at most 100 of its 127.
float LevelAudio::loudness(const Vec3f &at, float radius, float volume) const {
    const Vec3f d{at[0] - _listener[0], at[1] - _listener[1], at[2] - _listener[2]};
    const float distance = std::sqrt(dot(d, d));
    const float near = radius > 0 ? std::clamp(1 - distance / radius, 0.0f, 1.0f) : 0;
    return float(std::clamp(int(near * std::clamp(volume, 0.0f, 2.0f) * 100), 0, 100)) / 127;
}

// The driver's side: the angle off straight ahead (one behind as if in
// front), less within a tenth of the radius, as 0..127 about 63.
int LevelAudio::panOf(const Vec3f &at, float radius) const {
    if (!_stereo)
        return 0;
    const Vec3f d{at[0] - _listener[0], at[1] - _listener[1], at[2] - _listener[2]};
    float x = dot(d, _right);
    if (_reverse)
        x = -x;
    float angle = std::atan2(x, std::fabs(dot(d, _forward)));
    const float distance = std::sqrt(dot(d, d));
    if (radius > 0 && distance < radius * 0.1f)
        angle *= distance / (radius * 0.1f);
    const int pan = std::clamp(int(angle * 111.125f / 3.14159265f + 63), 0, 127);
    return std::clamp((pan - 64) * audio::kMaxPan / 63, -audio::kMaxPan, audio::kMaxPan);
}

float LevelAudio::priority(const Vec3f &at, float radius, float volume) const {
    const Vec3f d{at[0] - _listener[0], at[1] - _listener[1], at[2] - _listener[2]};
    const float distance = std::sqrt(dot(d, d));
    return volume * std::max(radius > 0 ? 1 - distance / radius : 0.0f, 0.0001f);
}

bool LevelAudio::makeRoom(float wanted) {
    const size_t used = _playing.size() + _ambient.size() + _fading.size() + (_music ? 1 : 0);
    if (used < size_t(_channels))
        return true;
    // The least important one-off or ambient sound, if no more important.
    float least = wanted;
    Playing *lowest = nullptr;
    VmObject *lowestAmbient = nullptr;
    for (Playing &p : _playing)
        if (const float q = p.everywhere ? p.volume : priority(p.at, p.radius, p.volume); q <= least)
            least = q, lowest = &p, lowestAmbient = nullptr;
    for (auto &[actor, a] : _ambient)
        if (const float q = a.music ? a.volume : priority(a.at, a.radius, a.volume); q <= least)
            least = q, lowest = nullptr, lowestAmbient = actor;
    if (lowest) {
        _mixer.stop(lowest->handle);
        _playing.erase(_playing.begin() + (lowest - _playing.data()));
        return true;
    }
    if (lowestAmbient) {
        _mixer.stop(_ambient[lowestAmbient].handle);
        _ambient.erase(lowestAmbient);
        return true;
    }
    return false;
}

// A stream: its hundredths, twice, times MusicVolume / 255, of 127.
float LevelAudio::streamLoudness(float volume) const {
    const float v = float(std::min(100, int(std::max(volume, 0.0f) * 100)));
    return std::min(1.0f, v * 2 * _musicSetting / 127);
}

void LevelAudio::update(LevelPlay &play, const Vec3f &listener, const Rotator &facing, float seconds) {
    _listener = listener;
    const float yaw = float(facing.yaw) * 6.2831853f / 65536;
    const float pitch = float(facing.pitch) * 6.2831853f / 65536;
    _right = {-std::sin(yaw), std::cos(yaw), 0};
    _forward = {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    _up = {-std::sin(pitch) * std::cos(yaw), -std::sin(pitch) * std::sin(yaw), std::cos(pitch)};
    auto flag = [&](const char *key, bool otherwise) {
        const std::string v = play.audioSetting(key);
        return v.empty() ? otherwise : (v == "1" || v == "True" || v == "true" || v == "TRUE");
    };
    _stereo = flag("UseStereo", true);
    _reverse = flag("ReverseStereo", false);
    _surround = flag("UseSurround", false);
    {
        const std::string v = play.audioSetting("DopplerSpeed");
        _dopplerSpeed = v.empty() ? 35000 : std::strtof(v.c_str(), nullptr);
    }
    _viewerVelocity = play.viewerVelocity();
    // The listener's zone's reverb (with UseReverb), on every positional
    // sound; when the room changes, all of them.
    _room = flag("UseReverb", true) ? play.roomAt(listener) : LevelPlay::Room{};
    _reverb = reverbOf(_room);
    // A sound's own velocity, the listener's taken off; a voice's none.
    auto relative = [&](VmObject *actor, bool speech) {
        Vec3f v{};
        if (!actor || speech || !play.velocityOf(actor, v))
            return Vec3f{};
        return Vec3f{v[0] - _viewerVelocity[0], v[1] - _viewerVelocity[1], v[2] - _viewerVelocity[2]};
    };
    {
        const std::string v = play.audioSetting("Channels");
        _channels = v.empty() ? 16 : std::max(1, std::atoi(v.c_str()));
    }
    // The volumes: each kind's setting (0-255) under the master's. A
    // stream's own is part of its loudness (streamLoudness).
    const float master = std::clamp(play.masterVolume(), 0.0f, 255.0f) / 255;
    auto setting = [&](const char *key) {
        const std::string v = play.audioSetting(key);
        return std::clamp(v.empty() ? 255.0f : std::strtof(v.c_str(), nullptr), 0.0f, 255.0f) / 255;
    };
    _musicSetting = setting("MusicVolume");
    _mixer.setTypeVolume(audio::SoundType::kEffects, volumeOf(master * setting("SFXVolume")));
    _mixer.setTypeVolume(audio::SoundType::kMusic, volumeOf(master));
    _mixer.setTypeVolume(audio::SoundType::kSpeech, volumeOf(master * setting("SpeechVolume")));

    if (play.takeKillSounds()) {
        for (const Playing &p : _playing)
            _mixer.stop(p.handle);
        _playing.clear();
    }
    // Those stopped (a line cut off: its speaker's talk slot).
    for (const auto &[actor, slot] : play.takeSoundStops())
        for (Playing &p : _playing)
            if (p.actor == actor && p.slot == slot)
                _mixer.stop(p.handle);
    _playing.erase(
        std::remove_if(_playing.begin(), _playing.end(), [&](const Playing &p) { return !_mixer.isPlaying(p.handle); }),
        _playing.end());

    // What's played: one at a time in each of an actor's slots (but the
    // none slot). One played with bNoOverride keeps its slot; otherwise a
    // new one takes it.
    for (const ScriptLevel::SoundRequest &r : play.takeSounds()) {
        Playing p;
        p.actor = r.actor;
        p.slot = r.slot;
        p.radius = r.radius;
        p.volume = std::clamp(r.volume, 0.0f, 2.0f);
        p.everywhere = !r.actor;
        p.noOverride = r.noOverride;
        // A sound filtered (LowPass, HighPass: the driver's filters, each
        // in its one place, so a high-pass takes a low-pass's) is mixed
        // flat; the rest positionally.
        p.filtered = r.lowPass != 0 || r.highPass != 0;
        p.speech = r.speech;
        p.pitch = r.pitch;
        p.obstruction = r.obstruction;
        p.sound = r.sound;
        if (r.actor && !play.locationOf(r.actor, p.at))
            continue;
        if (r.slot != 0) {
            auto same = std::find_if(_playing.begin(), _playing.end(),
                                     [&](const Playing &q) { return q.actor == r.actor && q.slot == r.slot; });
            if (same != _playing.end()) {
                if (same->noOverride)
                    continue;
                _mixer.stop(same->handle);
                _playing.erase(same);
            }
        }
        auto sound = load(r.sound);
        if (!sound || !makeRoom(p.everywhere ? p.volume : priority(p.at, p.radius, p.volume)))
            continue;
        const float loud =
            p.everywhere ? float(std::min(100, int(p.volume * 100))) / 127 : loudness(p.at, p.radius, p.volume);
        p.handle = _mixer.play(sound, r.speech ? audio::SoundType::kSpeech : audio::SoundType::kEffects, volumeOf(loud),
                               p.everywhere ? 0
                               : p.filtered ? panOf(p.at, p.radius)
                                            : positionalPan(p.at),
                               r.loop);
        p.tail = r.loop ? 0 : reverbTail(*sound);
        if (p.handle == audio::kNoSound)
            continue;
        if (r.pitch != 1)
            _mixer.setRate(p.handle, r.pitch);
        if (r.highPass != 0)
            _mixer.setFilter(p.handle, audio::Mixer::Filter::kHighPass, r.highPass);
        else if (r.lowPass != 0)
            _mixer.setFilter(p.handle, audio::Mixer::Filter::kLowPass, r.lowPass);
        if (r.startSeconds > 0)
            _mixer.seek(p.handle, double(r.startSeconds) * sound->rate);
        _playing.push_back(p);
        ++_started;
        _recent.push_back(r.sound.package->name() + "." + r.sound.package->path(r.sound.exportIndex + 1));
        if (_recent.size() > 12)
            _recent.erase(_recent.begin());
    }
    // Those playing follow their actors (one gone is heard where it was),
    // quietened by their ObstructionFactor when the level's walls stand
    // between them and the listener. A positional one: in the room's
    // reverb, its side, Doppler and 71% from behind (with UseSurround) the
    // 3D provider's; a flat one's side and Doppler the driver's.
    for (Playing &p : _playing) {
        if (!p.filtered) {
            audio::Mixer::Reverb rv = _reverb;
            rv.tail = p.tail;
            _mixer.setReverb(p.handle, rv);
        }
        if (p.everywhere)
            continue;
        if (p.actor && !play.locationOf(p.actor, p.at))
            p.actor = nullptr;
        float loud = loudness(p.at, p.radius, p.volume);
        if (p.obstruction > 0 && loud > 0 && play.wallBetween(_listener, p.at))
            loud *= 1 - p.obstruction;
        if (!p.filtered)
            loud *= behind(p.at);
        _mixer.setVolume(p.handle, volumeOf(loud));
        _mixer.setPan(p.handle, p.filtered ? panOf(p.at, p.radius) : positionalPan(p.at));
        _mixer.setRate(p.handle, p.pitch * doppler(p.at, relative(p.actor, p.speech), p.filtered));
    }

    // The ambient sounds: started when the listener comes within their
    // radius, stopped when it leaves (and started afresh on coming back),
    // or when the actor's sound changes. One named MUSIC is heard alike
    // anywhere in range, fades in and out over 3 seconds, and isn't started
    // while other music plays.
    play.ambientSounds(_ambientNow);
    std::map<VmObject *, const LevelPlay::Ambient *> now;
    for (const LevelPlay::Ambient &a : _ambientNow)
        now[a.actor] = &a;
    auto inRange = [&](const Vec3f &at, float radius) {
        const Vec3f d{at[0] - _listener[0], at[1] - _listener[1], at[2] - _listener[2]};
        return dot(d, d) < radius * radius;
    };
    for (auto it = _ambient.begin(); it != _ambient.end();) {
        Ambient &am = it->second;
        auto found = now.find(it->first);
        const bool keep = found != now.end() && found->second->sound.package == am.sound.first &&
                          found->second->sound.exportIndex == am.sound.second &&
                          inRange(found->second->at, found->second->radius) && _mixer.isPlaying(am.handle);
        if (!keep && am.music && am.fade > 0 && _mixer.isPlaying(am.handle)) {
            am.fadeRate = -1 / kMusicFade; // faded out first
            ++it;
        } else if (!keep) {
            _mixer.stop(am.handle);
            it = _ambient.erase(it);
        } else {
            if (am.music && am.fadeRate < 0)
                am.fadeRate = 1 / kMusicFade;
            ++it;
        }
    }
    const bool musicOn = _music != audio::kNoSound || !_fading.empty();
    for (const LevelPlay::Ambient &a : _ambientNow) {
        auto it = _ambient.find(a.actor);
        if (it == _ambient.end()) {
            if (!inRange(a.at, a.radius) || (a.music && musicOn))
                continue;
            auto sound = load(a.sound);
            if (!sound || !makeRoom(a.music ? a.volume : priority(a.at, a.radius, a.volume)))
                continue;
            Ambient am;
            am.sound = {a.sound.package, a.sound.exportIndex};
            am.music = a.music;
            am.streamed = a.streamed;
            if (a.music)
                am.fade = 0, am.fadeRate = 1 / kMusicFade;
            am.handle =
                _mixer.play(sound, a.streamed ? audio::SoundType::kMusic : audio::SoundType::kEffects, 0, 0, true);
            if (am.handle == audio::kNoSound)
                continue;
            it = _ambient.emplace(a.actor, am).first;
        }
        Ambient &am = it->second;
        am.volume = a.volume;
        am.radius = a.radius;
        am.at = a.at;
        if (am.fadeRate != 0)
            am.fade = std::clamp(am.fade + am.fadeRate * seconds, 0.0f, 1.0f);
        float loud = am.music
                         ? (am.streamed ? streamLoudness(a.volume) : float(std::min(100, int(a.volume * 100))) / 127)
                         : (am.streamed ? streamLoudness(loudness(a.at, a.radius, a.volume) * 127 / 100)
                                        : loudness(a.at, a.radius, a.volume));
        loud *= am.fade;
        // An ambient sound is positional (not music, not streamed): the
        // room's reverb, quieter from behind, and Doppler.
        const bool positional = !am.music && !am.streamed;
        float doppled = 1;
        if (positional) {
            loud *= behind(a.at);
            doppled = doppler(a.at, relative(a.actor, false), false);
        }
        _mixer.setReverb(am.handle, positional ? _reverb : audio::Mixer::Reverb{});
        _mixer.setVolume(am.handle, volumeOf(loud));
        _mixer.setPan(am.handle, am.music ? 0 : positional ? positionalPan(a.at) : panOf(a.at, a.radius));
        _mixer.setRate(am.handle, a.pitch * doppled);
    }
    // Ambient music faded out, or whose actor is gone.
    for (auto it = _ambient.begin(); it != _ambient.end();) {
        Ambient &am = it->second;
        if (am.music && now.find(it->first) == now.end() && am.fadeRate < 0) {
            am.fade = std::max(0.0f, am.fade + am.fadeRate * seconds);
            _mixer.setVolume(am.handle, volumeOf(am.fade * (am.streamed ? streamLoudness(am.volume) : am.volume)));
        }
        if (am.music && am.fade <= 0 && am.fadeRate < 0) {
            _mixer.stop(am.handle);
            it = _ambient.erase(it);
        } else {
            ++it;
        }
    }

    // The music: whatever the player's CurrentMusic names. Played while it
    // plays already, it goes on; another starts at once, at full volume,
    // while the old one fades out over 3 seconds, as does one whose
    // CurrentMusic is cleared. Once it ends, CurrentMusic is None again
    // (the filler music waits for that). With CurrentMusic set and nothing
    // playing (a game loaded), it starts at full volume.
    VmObject *wanted = play.currentMusic();
    const ScriptLevel::Music &m = play.music();
    if (wanted != _musicSound) {
        if (_music != audio::kNoSound)
            _fading.push_back({_music, _musicStreamed ? streamLoudness(_musicVolume) : _musicVolume, kMusicFade});
        _music = audio::kNoSound;
        _musicSound = wanted;
        if (wanted && wanted->package && wanted->exportIndex >= 0)
            if (auto sound = load({wanted->package, wanted->exportIndex})) {
                // Ambient music gives way.
                for (auto &[actor, am] : _ambient)
                    if (am.music)
                        am.fadeRate = -1 / kMusicFade;
                _musicStreamed = play.streamed(wanted);
                _musicVolume = m.serial ? m.volume : 2.0f;
                const float loud =
                    _musicStreamed ? streamLoudness(_musicVolume) : float(std::min(100, int(_musicVolume * 100))) / 127;
                _music = _mixer.play(sound, _musicStreamed ? audio::SoundType::kMusic : audio::SoundType::kEffects,
                                     volumeOf(loud));
                if (m.startSeconds > 0)
                    _mixer.seek(_music, double(m.startSeconds) * sound->rate);
            }
    } else if (_music != audio::kNoSound && !_mixer.isPlaying(_music)) {
        _music = audio::kNoSound;
        play.musicEnded(_musicSound);
        _musicSound = nullptr;
    } else if (_music != audio::kNoSound) {
        _mixer.setVolume(_music, volumeOf(_musicStreamed ? streamLoudness(_musicVolume)
                                                         : float(std::min(100, int(_musicVolume * 100))) / 127));
    }
    for (Fade &f : _fading) {
        f.left -= seconds;
        _mixer.setVolume(f.handle, volumeOf(f.volume * std::max(0.0f, f.left) / kMusicFade));
        if (f.left <= 0)
            _mixer.stop(f.handle);
    }
    _fading.erase(std::remove_if(_fading.begin(), _fading.end(), [](const Fade &f) { return f.left <= 0; }),
                  _fading.end());
}

} // namespace reluva::unreal
