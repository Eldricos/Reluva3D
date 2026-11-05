// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/sound.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>

#include "engines/stgenerations/audio.h"
#include "engines/stgenerations/debug.h"
#include "common/bytes.h"

namespace reluva::stgenerations {

namespace {

const char* const kDebugChannel = "sound";
const double kFixed = 65536.0;
const double kPi = 3.14159265358979323846;
const uint32_t kCueStart = 0x12345678;
const uint32_t kCueEnd = 0x87654321;
const int kSoundSection = 3;
const int kCueSection = 2;
// Volumes the original treats as inaudible for positional sounds.
const int kAudibleVolume = 10;
// Speaker prefixes of sound file names (table at 0x53FDA8).
const char* const kPrefixes[] = {
    "MISC", "PICA", "TROI", "GEOR", "RIKE", "DATA", "WORF", "CRUS", "KIRK", "ROMU", "KLIN",
    "SORA", "CHOD", "HANU", "EXTR", "KRAU", "LURS", "BETO", "MUSI", "TEMP", "COMP"
};
const uint32_t kPrefixMusic = 18;
const uint32_t kKeyCommon = 0x1000000;

// The original's quick 3D distance: the largest axis plus a quarter of the others.
double quickDistance(double dx, double dy, double dz) {
    dx = std::fabs(dx);
    dy = std::fabs(dy);
    dz = std::fabs(dz);
    double m = std::max(dx, std::max(dy, dz));
    return m + (dx + dy + dz - m) / 4;
}

uint8_t mixerVolume(int volume) {
    return (uint8_t)(std::max(0, std::min(64, volume)) * Mixer::kMaxChannelVolume / 64);
}

} // namespace

struct MissionSound::Voice {
    uint32_t serial = 0;
    int sound = -1;
    uint16_t requester = 0;
    // Cue members still to play after this one (indices, or -ms delays).
    std::vector<int32_t> chain;
    SoundHandle handle;
    bool started = false;
    // Waiting before a repeat.
    bool waiting = false;
    uint32_t startAt = 0;
    // Current volume, 0-64, and a fade towards fadeTarget at fadeRate per ms.
    double volume = 64;
    double fadeTarget = -1;
    double fadeRate = 0;
    bool stopAfterFade = false;
    int playsLeft = 1;
    int priority = 0;
    uint32_t order = 0;
    // Watchdog: stopped if still playing after this.
    uint32_t deadline = 0;
    uint32_t length = 0;
};

MissionSound::MissionSound(const ScriptFile& script, Mixer* mixer, const FileOpener& opener,
                           const ObjectLocator& locator)
    : _mixer(mixer), _opener(opener), _locator(locator) {
    // Section 3: 56-byte entries, ended by a key of -1.
    const std::vector<uint8_t>& sounds = script.getSection(kSoundSection);
    for (size_t p = 0; p + 56 <= sounds.size(); p += 56) {
        const uint8_t* r = &sounds[p];
        if ((int32_t)readLE32(r) == -1)
            break;
        Definition d;
        d.key = readLE32(r);
        d.volume = (int32_t)readLE32(r + 0x04);
        d.priority = (int32_t)readLE32(r + 0x08);
        d.positionMode = (int32_t)readLE32(r + 0x0C);
        d.object = (uint16_t)(readLE32(r + 0x10) & 0x1FFF);
        d.x = (int32_t)readLE32(r + 0x14) / kFixed;
        d.y = (int32_t)readLE32(r + 0x18) / kFixed;
        d.z = (int32_t)readLE32(r + 0x1C) / kFixed;
        d.radius = (int32_t)readLE32(r + 0x20) / kFixed;
        // The loader zeroes the delay; scripts and cues set it.
        d.delay = 0;
        d.repeats = (int32_t)readLE32(r + 0x28);
        d.minGap = readLE32(r + 0x2C);
        d.maxGap = readLE32(r + 0x30);
        d.flags = readLE32(r + 0x34);
        _sounds.push_back(d);
    }

    // Section 2: cues, each between two magic numbers.
    const std::vector<uint8_t>& cues = script.getSection(kCueSection);
    size_t p = 0;
    while (p + 16 <= cues.size() && readLE32(&cues[p]) == kCueStart) {
        Cue cue;
        cue.number = (uint16_t)readLE32(&cues[p + 4]);
        cue.flags = readLE32(&cues[p + 8]);
        p += 16;
        while (p + 4 <= cues.size() && readLE32(&cues[p]) != kCueEnd && p + 8 <= cues.size()) {
            cue.members.push_back((int32_t)readLE32(&cues[p]));
            p += 8;
        }
        p += 4;
        cue.used.assign(cue.members.size(), false);
        _cues.push_back(cue);
    }
    debugC(1, kDebugChannel, "%d sounds, %d cues", (int)_sounds.size(), (int)_cues.size());
}

MissionSound::~MissionSound() {
    stopAll();
}

std::string MissionSound::getFileName(uint32_t key, const char* extension, Location& where) {
    uint32_t prefix = (key >> 16) & 0xFF;
    where = (key & kKeyCommon) ? kLocationCommon : prefix == kPrefixMusic ? kLocationMusic : kLocationMission;
    const char* name = prefix < sizeof(kPrefixes) / sizeof(kPrefixes[0]) ? kPrefixes[prefix] : "MISC";
    return text::format("%s%04u.%s", name, key & 0xFFFF, extension);
}

std::string MissionSound::cleanSubtitle(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            char code = text[i + 1];
            if (code == '\\') {
                out += '\\';
                i++;
                continue;
            }
            if (code == 'T' || code == 'W' || code == 'B' || code == 't' || code == 'w' || code == 'b') {
                size_t end = text.find('\\', i + 2);
                if (end != std::string::npos) {
                    i = end;
                    continue;
                }
            }
        }
        if (text[i] == '\r')
            continue;
        out += text[i] == '\n' ? ' ' : text[i];
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

uint32_t MissionSound::random(uint32_t n) {
    // The original's generator (0x546488).
    _seed = _seed * 189 + 2749;
    return (uint32_t)(((uint64_t)_seed * n) >> 32);
}

int MissionSound::definitionOf(uint32_t reference) const {
    int sound = (int)(reference & 0xFFFF);
    return sound < (int)_sounds.size() ? sound : -1;
}

std::vector<int> MissionSound::findVoices(uint32_t reference) const {
    std::vector<int> found;
    uint16_t high = (uint16_t)(reference >> 16);
    for (size_t i = 0; i < _voices.size(); i++) {
        const Voice& v = *_voices[i];
        if (high == 0 || high == 0xFFFF ? v.sound == (int)(reference & 0xFFFF) : v.serial == (uint32_t)high - 1)
            found.push_back((int)i);
    }
    return found;
}

int MissionSound::startVoice(int sound, uint16_t requester, std::vector<int32_t> chain, uint32_t extraDelay) {
    if (sound < 0 || sound >= (int)_sounds.size())
        return -1;
    const Definition& d = _sounds[sound];
    std::unique_ptr<Voice> voice(new Voice());
    voice->serial = _nextSerial++;
    voice->sound = sound;
    voice->requester = requester;
    voice->chain = std::move(chain);
    voice->startAt = _time + d.delay + extraDelay;
    voice->volume = (d.flags & kFlagFadeIn) ? 0 : d.volume;
    if (d.flags & kFlagFadeIn) {
        voice->fadeTarget = d.volume;
        voice->fadeRate = d.delay ? (double)d.volume / d.delay : 64;
    }
    voice->priority = d.priority + ((d.flags & kFlagHighPriority) ? 4 : 0);
    voice->playsLeft = d.repeats == kForever ? kForever : std::max(1, (int)d.repeats);
    voice->order = voice->serial;
    int handle = (int)voice->serial + 1;
    _voices.push_back(std::move(voice));
    return handle;
}

int MissionSound::play(int sound, uint16_t requester) {
    return startVoice(sound, requester, {}, 0);
}

int MissionSound::playSound(int sound, int volume) {
    if (sound < 0 || sound >= (int)_sounds.size())
        return -1;
    // The original stores the volume in the entry for good.
    _sounds[sound].volume = std::max(0, std::min(64, volume));
    return play(sound);
}

int MissionSound::playDelayed(int sound, uint32_t delay) {
    if (sound < 0 || sound >= (int)_sounds.size())
        return -1;
    _sounds[sound].delay = delay;
    return play(sound);
}

int MissionSound::playFadeIn(uint32_t reference, uint32_t ms) {
    int sound = definitionOf(reference);
    int handle = play(sound);
    if (handle > 0) {
        Voice& v = *_voices.back();
        v.fadeTarget = _sounds[sound].volume;
        v.volume = 0;
        v.fadeRate = ms ? v.fadeTarget / ms : 64;
    }
    return handle;
}

void MissionSound::playCue(int number, uint16_t requester) {
    auto it = std::find_if(_cues.begin(), _cues.end(), [number](const Cue& c) { return c.number == number; });
    if (it == _cues.end())
        return;
    Cue& cue = *it;
    std::vector<int32_t> picked;
    size_t n = cue.members.size();

    if (cue.flags & 0x20) {
        // The whole sequence, or only the first time with flag 1.
        if (!((cue.flags & 1) && n && cue.used[0]))
            picked = cue.members;
        if (n)
            cue.used[0] = true;
    } else {
        size_t first = 0, last = n;
        if (cue.flags & 0x10) {
            if (n)
                picked.push_back(cue.members[0]);
            first = 1;
        } else if (cue.flags & 4) {
            if (!cue.started && n) {
                picked.push_back(cue.members[0]);
                cue.started = true;
                first = n; // nothing more
            } else {
                first = 1;
            }
        }
        bool hasFinal = (cue.flags & 8) && last > first;
        if (hasFinal)
            last--;
        if (first < last || hasFinal) {
            std::vector<size_t> candidates;
            for (size_t i = first; i < last; i++) {
                if (!((cue.flags & 1) || hasFinal) || !cue.used[i])
                    candidates.push_back(i);
            }
            if (!candidates.empty()) {
                size_t pick = candidates[(cue.flags & 2) ? random((uint32_t)candidates.size()) : 0];
                cue.used[pick] = true;
                picked.push_back(cue.members[pick]);
            } else if (hasFinal && !cue.used[last]) {
                cue.used[last] = true;
                picked.push_back(cue.members[last]);
            }
        }
    }

    // Leading delays add up before the first sound; each sound carries the
    // delays before it, and a trailing delay is dropped.
    uint32_t delay = 0;
    size_t i = 0;
    while (i < picked.size() && picked[i] < 0)
        delay += (uint32_t)-picked[i++];
    if (i >= picked.size()) {
        // Nothing to play: tell the requester straight away (at the next update).
        std::unique_ptr<Voice> voice(new Voice());
        voice->serial = _nextSerial++;
        voice->requester = requester;
        voice->started = true;
        voice->length = 0;
        _voices.push_back(std::move(voice));
        return;
    }
    int sound = picked[i];
    if (sound >= 0 && sound < (int)_sounds.size())
        _sounds[sound].repeats = 0;
    startVoice(sound, requester, std::vector<int32_t>(picked.begin() + i + 1, picked.end()), delay);
}

void MissionSound::stop(uint32_t reference) {
    std::vector<int> found = findVoices(reference);
    for (auto it = found.rbegin(); it != found.rend(); ++it) {
        Voice& v = *_voices[*it];
        if (_mixer)
            _mixer->stopHandle(v.handle);
        v.chain.clear();
        v.playsLeft = 0;
        v.started = true;
        v.waiting = false;
        v.length = 0;
        v.deadline = _time;
    }
}

void MissionSound::stopOne(uint32_t reference) {
    if ((reference >> 16) != 0 && (reference >> 16) != 0xFFFF)
        stop(reference);
}

bool MissionSound::fadeOut(uint32_t reference, uint32_t ms) {
    std::vector<int> found = findVoices(reference);
    if (found.empty()) {
        if ((reference >> 16) != 0 || play(definitionOf(reference)) < 0)
            return false;
        found.push_back((int)_voices.size() - 1);
    }
    for (int index : found) {
        Voice& v = *_voices[index];
        v.fadeTarget = 0;
        v.fadeRate = ms ? std::max(v.volume, 1.0) / ms : 64;
        v.stopAfterFade = true;
    }
    return true;
}

void MissionSound::setVolume(uint32_t reference, int volume) {
    volume = std::max(0, std::min(64, volume));
    if ((reference >> 16) == 0) {
        int sound = definitionOf(reference);
        if (sound >= 0)
            _sounds[sound].volume = volume;
        return;
    }
    for (int index : findVoices(reference))
        _voices[index]->volume = volume;
}

void MissionSound::setLoop(int sound, int32_t repeats, uint32_t minGap, uint32_t maxGap) {
    if (sound < 0 || sound >= (int)_sounds.size())
        return;
    _sounds[sound].repeats = repeats;
    _sounds[sound].minGap = minGap;
    _sounds[sound].maxGap = maxGap;
}

void MissionSound::setPriority(uint32_t reference, int priority) {
    int sound = definitionOf(reference);
    if (sound >= 0 && (reference >> 16) == 0)
        _sounds[sound].priority = priority;
    for (int index : findVoices(reference))
        _voices[index]->priority = priority;
}

void MissionSound::setPosition(uint32_t reference, double x, double y, double z) {
    int sound = definitionOf(reference);
    if (sound < 0)
        return;
    _sounds[sound].x = x;
    _sounds[sound].y = y;
    _sounds[sound].z = z;
}

void MissionSound::setMuted(uint32_t reference, bool muted) {
    int sound = definitionOf(reference);
    if (sound < 0)
        return;
    if (muted)
        _sounds[sound].flags |= kFlagMuted;
    else
        _sounds[sound].flags &= ~kFlagMuted;
}

void MissionSound::setObject(uint32_t reference, uint16_t object) {
    int sound = definitionOf(reference);
    if (sound >= 0)
        _sounds[sound].object = (uint16_t)(object & 0x1FFF);
}

void MissionSound::setRadius(uint32_t reference, double radius) {
    int sound = definitionOf(reference);
    if (sound >= 0)
        _sounds[sound].radius = radius;
}

bool MissionSound::placeVoice(Voice& voice, const Listener& listener, int& volume, int8_t& balance) const {
    const Definition& d = _sounds[voice.sound];
    volume = (d.flags & kFlagMuted) ? 0 : (int)std::lround(voice.volume);
    balance = (d.flags & kFlagLeft) ? -127 : (d.flags & kFlagRight) ? 127 : 0;
    if (d.positionMode == kPositionNone)
        return true;

    double x = d.x, y = d.y, z = d.z;
    if (d.positionMode != kPositionFixed && (!_locator || !_locator(d.positionMode, d.object, x, y, z)))
        return true;
    // 0x41E583: fade linearly to nothing at the radius, and pan by the
    // direction from the player, less so up close.
    double dx = x - listener.x, dy = y - listener.y;
    double distance = quickDistance(dx, dy, z - listener.z);
    if (distance >= d.radius || volume <= kAudibleVolume) {
        volume = 0;
        return false;
    }
    volume = kAudibleVolume + (int)((volume - kAudibleVolume) * (d.radius - distance) / d.radius);
    double flat = std::sqrt(dx * dx + dy * dy);
    if (flat > 0) {
        // The side the sound is on: positive to the player's right.
        double side = (dx * std::sin(listener.yaw) - dy * std::cos(listener.yaw)) / flat;
        balance = (int8_t)std::lround(side * 127 * std::min(1.0, distance / d.radius));
    }
    return true;
}

void MissionSound::stealChannel() {
    // Eight channels: a new sound takes the lowest priority, oldest one.
    int playing = 0, victim = -1;
    for (size_t i = 0; i < _voices.size(); i++) {
        const Voice& v = *_voices[i];
        if (!v.started || v.waiting || !_mixer || !_mixer->isSoundHandleActive(v.handle))
            continue;
        playing++;
        if (victim < 0 || v.priority < _voices[victim]->priority ||
            (v.priority == _voices[victim]->priority && v.order < _voices[victim]->order))
            victim = (int)i;
    }
    if (playing >= kChannels && victim >= 0) {
        _mixer->stopHandle(_voices[victim]->handle);
        _voices[victim]->playsLeft = 0;
    }
}

void MissionSound::beginPlayback(Voice& voice, const Listener& listener) {
    const Definition& d = _sounds[voice.sound];
    voice.started = true;
    voice.waiting = false;
    int volume;
    int8_t balance;
    bool audible = placeVoice(voice, listener, volume, balance);
    // A one-off positional sound out of earshot is dropped (0x41E481).
    if (!audible && d.repeats != kForever) {
        voice.playsLeft = 0;
        voice.length = 0;
        voice.deadline = _time;
        return;
    }

    Location where;
    std::string name = getFileName(d.key, "WAV", where);
    std::unique_ptr<SeekableReadStream> file = _opener ? _opener(name, where) : nullptr;
    std::unique_ptr<SoundStream> stream = file ? makeWavStream(*file) : nullptr;
    if (!stream) {
        debugC(1, kDebugChannel, "sound %d: %s not found", voice.sound, name.c_str());
        voice.playsLeft = 0;
        voice.length = 0;
        voice.deadline = _time;
        return;
    }

    WavInfo info;
    file->seek(0);
    uint32_t length = 0;
    if (readWavInfo(*file, info) && info.sampleRate && info.channels && info.bitsPerSample)
        length = (uint32_t)((uint64_t)info.dataSize * 1000 / ((uint64_t)info.sampleRate * info.channels * info.bitsPerSample / 8));
    voice.length = length;

    // Seamless loops play as a looping stream; loops with a gap restart.
    std::unique_ptr<audio::Stream> playing;
    bool seamless = voice.playsLeft > 1 && d.minGap == 0;
    if (seamless) {
        playing = makeLooping(std::move(stream), voice.playsLeft == kForever ? 0 : voice.playsLeft);
        voice.playsLeft = 1;
        voice.deadline = UINT32_MAX;
    } else {
        playing = std::move(stream);
        voice.deadline = _time + length + std::max<uint32_t>(length, 3000);
    }

    if (_mixer) {
        stealChannel();
        Mixer::SoundType type = where == kLocationMusic ? Mixer::kMusicSoundType
                                       : (d.flags & kFlagSubtitle) ? Mixer::kSpeechSoundType
                                                                     : Mixer::kSFXSoundType;
        _mixer->playStream(type, &voice.handle, std::move(playing), -1, mixerVolume(volume), balance);
    }
    debugC(2, kDebugChannel, "sound %d: %s", voice.sound, name.c_str());

    if (d.flags & kFlagSubtitle) {
        std::unique_ptr<SeekableReadStream> text = _opener ? _opener(getFileName(d.key, "TXT", where), where) : nullptr;
        if (text) {
            std::string raw(text->size(), '\0');
            text->read(&raw[0], (uint32_t)raw.size());
            _subtitle = cleanSubtitle(raw);
            _subtitleDuration = std::max<uint32_t>(length, 2000);
            _hasSubtitle = !_subtitle.empty();
        }
    }
}

void MissionSound::finishVoice(size_t index, std::vector<Finished>& finished) {
    std::unique_ptr<Voice> voice = std::move(_voices[index]);
    _voices.erase(_voices.begin() + index);

    // Carry on down a cue's chain: delays add up before the next sound.
    uint32_t delay = 0;
    size_t i = 0;
    while (i < voice->chain.size() && voice->chain[i] < 0)
        delay += (uint32_t)-voice->chain[i++];
    if (i < voice->chain.size()) {
        int sound = voice->chain[i];
        if (sound >= 0 && sound < (int)_sounds.size())
            _sounds[sound].repeats = 0;
        startVoice(sound, voice->requester, std::vector<int32_t>(voice->chain.begin() + i + 1, voice->chain.end()), delay);
        return;
    }
    if (voice->requester)
        finished.push_back({ voice->requester });
}

void MissionSound::update(uint32_t ms, const Listener& listener, std::vector<Finished>& finished) {
    _time += ms;
    _listener = listener;
    for (size_t i = 0; i < _voices.size();) {
        Voice& v = *_voices[i];
        if (!v.started || v.waiting) {
            if (_time >= v.startAt) {
                if (v.sound < 0) {
                    v.started = true;
                } else {
                    beginPlayback(v, listener);
                }
            }
            if (!v.started || v.waiting) {
                i++;
                continue;
            }
        }

        // Fades.
        if (v.fadeTarget >= 0) {
            double step = v.fadeRate * ms;
            if (std::fabs(v.fadeTarget - v.volume) <= step) {
                v.volume = v.fadeTarget;
                v.fadeTarget = -1;
                if (v.stopAfterFade && v.volume <= 0) {
                    if (_mixer)
                        _mixer->stopHandle(v.handle);
                    v.playsLeft = 0;
                    v.chain.clear();
                }
            } else {
                v.volume += v.fadeTarget > v.volume ? step : -step;
            }
        }

        bool active = v.sound >= 0 && _mixer && _mixer->isSoundHandleActive(v.handle);
        if (active && _time > v.deadline) {
            _mixer->stopHandle(v.handle);
            active = false;
        }
        if (active) {
            int volume;
            int8_t balance;
            placeVoice(v, listener, volume, balance);
            _mixer->setChannelVolume(v.handle, mixerVolume(volume));
            _mixer->setChannelBalance(v.handle, balance);
            i++;
            continue;
        }

        // Ended: repeat after a gap, or finish.
        if (v.sound >= 0 && v.playsLeft != 0 && (v.playsLeft == kForever || --v.playsLeft > 0)) {
            const Definition& d = _sounds[v.sound];
            uint32_t gap = d.maxGap > d.minGap ? d.minGap + random(d.maxGap - d.minGap + 1) : d.minGap;
            v.waiting = true;
            v.startAt = _time + gap;
            i++;
            continue;
        }
        finishVoice(i, finished);
    }
}

bool MissionSound::takeSubtitle(std::string& text, uint32_t& durationMs) {
    if (!_hasSubtitle)
        return false;
    text = _subtitle;
    durationMs = _subtitleDuration;
    _hasSubtitle = false;
    return true;
}

void MissionSound::pauseAll(bool paused) {
    if (!_mixer)
        return;
    for (const std::unique_ptr<Voice>& v : _voices) {
        if (v->started)
            _mixer->pauseHandle(v->handle, paused);
    }
}

void MissionSound::stopAll() {
    for (const std::unique_ptr<Voice>& v : _voices) {
        if (_mixer && v->started)
            _mixer->stopHandle(v->handle);
    }
    _voices.clear();
}

} // namespace reluva::stgenerations
