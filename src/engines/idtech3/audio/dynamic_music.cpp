// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/audio/dynamic_music.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace reluva::idtech3 {

namespace {

// Names are kept to 63 letters (64-byte buffers in jk2sp.exe).
constexpr size_t kNameMax = 63;
// How many `uses` are followed (0x42bed8).
constexpr int kUsesMax = 10;
// Explore and action pieces have at most four exits (tracks 4-7, 8-11).
constexpr size_t kExitsMax = 4;
// How near an exit time must be to where the piece has got to (0x4c8cf8).
constexpr float kExitWindow = 0.3f;
// A piece hands over, or starts again, this long before its end (0x4c8478).
constexpr double kHandOverSeconds = 2.0;
// Every fade's length (0x4c8474).
constexpr uint64_t kFadeMs = 1000;

bool same(std::string_view a, std::string_view b) {
    return fs::toLower(std::string(a)) == fs::toLower(std::string(b));
}

// dms.dat's text as groups ("name { ... }") of pairs ("key value", the
// rest of the line its value) and groups.
struct Group {
    std::string name;
    std::vector<std::pair<std::string, std::string>> pairs;
    std::vector<Group> groups;

    const Group *group(std::string_view n) const {
        for (const Group &g : groups)
            if (same(g.name, n))
                return &g;
        return nullptr;
    }
};

std::string trim(const std::string &s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

// Reads the lines from `at` into `g` up to its closing brace; false if the
// braces don't match.
bool parseGroup(const std::vector<std::string> &lines, size_t &at, Group &g, bool top) {
    while (at < lines.size()) {
        const std::string line = lines[at++];
        if (line.empty())
            continue;
        if (line == "}")
            return !top;
        if (line == "{")
            return false;
        const bool opens = at < lines.size() && lines[at] == "{";
        if (opens) {
            ++at;
            Group child;
            child.name = line;
            if (!parseGroup(lines, at, child, false))
                return false;
            g.groups.push_back(std::move(child));
            continue;
        }
        const size_t gap = line.find_first_of(" \t");
        if (gap == std::string::npos)
            g.pairs.emplace_back(line, std::string());
        else
            g.pairs.emplace_back(line.substr(0, gap), trim(line.substr(gap)));
    }
    return top;
}

std::string clip(std::string s) {
    if (s.size() > kNameMax)
        s.resize(kNameMax);
    return s;
}

// A musicfiles entry (0x42ca40): its entry markers, exits and times.
Error readPiece(const Group &musicFiles, const std::string &name, bool transitions, MusicPiece &out) {
    const Group *g = musicFiles.group(name);
    if (!g)
        return Error::fail("Unable to find musicfiles entry \"" + name + "\"");
    out = {};
    out.name = name;
    if (const Group *entry = g->group("entry"))
        for (const auto &[key, value] : entry->pairs)
            out.markers.emplace_back(clip(key), std::strtof(value.c_str(), nullptr));
    std::sort(out.markers.begin(), out.markers.end(),
              [](const auto &a, const auto &b) { return fs::toLower(a.first) < fs::toLower(b.first); });
    bool anyNext = false;
    for (const Group &exit : g->groups) {
        if (exit.name != "exit")
            continue;
        MusicPiece::Exit e;
        const int index = int(out.exits.size());
        for (const auto &[key, value] : exit.pairs) {
            if (key == "nextfile")
                e.nextFile = clip(value);
            else if (key == "nextmark")
                e.nextMark = clip(value);
            else if (key.compare(0, 4, "time") == 0)
                out.times.emplace_back(std::strtof(value.c_str(), nullptr), index);
        }
        anyNext = anyNext || !e.nextFile.empty();
        out.exits.push_back(e);
    }
    if (transitions) {
        if (out.exits.size() > kExitsMax)
            return Error::fail("\"" + name + "\" has > 4 transitions defined!");
        if (!g->group("entry"))
            return Error::fail("Unable to find subgroup \"entry\" in group \"" + name + "\"");
        if (!anyNext)
            return Error::fail("Unable to find subgroup \"exit\" in group \"" + name + "\"");
    } else if (!out.exits.empty()) {
        // Printed but allowed (0x4dcd24); its exits are never taken.
        out.exits.clear();
        out.times.clear();
    }
    std::sort(out.times.begin(), out.times.end());
    return {};
}

} // namespace

std::optional<float> MusicPiece::marker(std::string_view name) const {
    for (const auto &[n, at] : markers)
        if (same(n, name))
            return at;
    return std::nullopt;
}

std::string MusicSet::path(int id) const {
    auto in = [](const std::string &folder, const std::string &file) {
        return "music/" + folder + "/" + file + ".mp3";
    };
    switch (id) {
    case DynamicMusic::kExplore:
        return explore ? in(folder, explore->name) : std::string();
    case DynamicMusic::kAction:
        return action ? in(folder, action->name) : std::string();
    case DynamicMusic::kBoss:
        return boss ? in(bossFolder, boss->name) : std::string();
    case DynamicMusic::kDeath:
        return "music/death_music.mp3";
    default:
        break;
    }
    const bool fromAction = id >= DynamicMusic::kActionTransition && id < DynamicMusic::kExploreTransition;
    const std::optional<MusicPiece> &piece = fromAction ? action : explore;
    const int exit = id - (fromAction ? DynamicMusic::kActionTransition : DynamicMusic::kExploreTransition);
    if (id < DynamicMusic::kActionTransition || id >= DynamicMusic::kTracks || !piece || exit < 0 ||
        size_t(exit) >= piece->exits.size())
        return {};
    return in(folder, piece->exits[size_t(exit)].nextFile);
}

Error findMusicSet(std::string_view dms, std::string_view name, const std::function<bool(const std::string &)> &exists,
                   MusicSet &out) {
    // As jk2sp.exe's reader (0x42bcb0, 0x42bd40): the name's last part, in
    // lower case; its levelmusic entry, through `uses`; its pieces from
    // musicfiles; each file checked.
    std::string want(name);
    if (const size_t slash = want.find_last_of('/'); slash != std::string::npos)
        want = want.substr(slash + 1);
    want = fs::toLower(clip(want));
    std::vector<std::string> lines;
    {
        std::istringstream in{std::string(dms)};
        std::string line;
        while (std::getline(in, line))
            lines.push_back(trim(line));
    }
    Group root;
    size_t at = 0;
    if (!parseGroup(lines, at, root, true))
        return Error::fail("Error using GP to parse file");
    const Group *musicFiles = root.group("musicfiles");
    const Group *levelMusic = root.group("levelmusic");
    if (!musicFiles || !levelMusic)
        return Error::fail(std::string("Unable to find subgroup \"") + (musicFiles ? "levelmusic" : "musicfiles") +
                           "\"");
    out = {};
    out.name = want;
    std::string level = want;
    const Group *entry = nullptr;
    for (int hop = 0;; ++hop) {
        entry = hop <= kUsesMax ? levelMusic->group(level) : nullptr;
        if (!entry)
            return Error::fail("Unable to find entry for \"" + level + "\" in \"ext_data/dms.dat\"");
        std::string uses;
        for (const auto &[key, value] : entry->pairs)
            if (same(key, "uses"))
                uses = clip(value);
        if (uses.empty())
            break;
        level = uses;
    }
    out.folder = level;
    out.bossFolder = level;
    std::string exploreName;
    std::string actionName;
    std::string bossName;
    for (const auto &[key, value] : entry->pairs) {
        // "placeholder" stands for no piece (0x42bf6b).
        if (same(value, "placeholder"))
            continue;
        if (same(key, "explore")) {
            exploreName = clip(value);
        } else if (same(key, "action")) {
            actionName = clip(value);
        } else if (same(key, "boss")) {
            bossName = clip(value);
        } else if (same(key, "useboss")) {
            // Another level's boss piece, from its folder (0x42c038).
            const Group *other = levelMusic->group(value);
            if (!other)
                return Error::fail("Unable to find 'useboss' entry \"" + value + "\"");
            std::string theirs;
            for (const auto &[k, v] : other->pairs)
                if (same(k, "boss"))
                    theirs = clip(v);
            if (theirs.empty())
                return Error::fail("'useboss' \"" + value + "\" has no \"boss\" entry!");
            bossName = theirs;
            out.bossFolder = clip(value);
        }
    }
    auto read = [&](const std::string &n, bool transitions, std::optional<MusicPiece> &piece) -> Error {
        if (n.empty())
            return {};
        piece.emplace();
        return readPiece(*musicFiles, n, transitions, *piece);
    };
    if (Error e = read(exploreName, true, out.explore))
        return e;
    if (Error e = read(actionName, true, out.action))
        return e;
    if (Error e = read(bossName, false, out.boss))
        return e;
    out.death.name = "death_music";
    // Every file there; each exit's mark one of the explore piece's.
    for (int id = DynamicMusic::kExplore; id <= DynamicMusic::kDeath; ++id) {
        const std::string p = out.path(id);
        if (!p.empty() && !exists(p))
            return Error::fail("Music file \"" + p + "\" not found!");
    }
    for (const std::optional<MusicPiece> *piece : {&out.explore, &out.action}) {
        if (!*piece)
            continue;
        for (const MusicPiece::Exit &e : (*piece)->exits) {
            if (!e.nextFile.empty() && !exists("music/" + out.folder + "/" + e.nextFile + ".mp3"))
                return Error::fail("Transition file \"" + e.nextFile + "\" (entry \"" + (*piece)->name +
                                   "\" ) not found!");
            if (e.nextMark.empty())
                continue;
            if (!out.explore)
                return Error::fail("Unable to find explore piece to match \"" + e.nextMark + "\"");
            if (!out.explore->marker(e.nextMark))
                return Error::fail("Unable to find entry point \"" + e.nextMark + "\" in description for \"" +
                                   out.explore->name + "\"");
        }
    }
    if (!out.explore || !out.action)
        return Error::fail("Dynamic music did not have both 'action' and 'explore' versions");
    return {};
}

std::string DynamicMusic::trackName(int id) {
    switch (id) {
    case kExplore:
        return "explore";
    case kAction:
        return "action";
    case kBoss:
        return "boss";
    case kDeath:
        return "death";
    case kSilence:
        return "silence";
    default:
        break;
    }
    if (id >= kActionTransition && id < kExploreTransition)
        return "action_tr" + std::to_string(id - kActionTransition);
    if (id >= kExploreTransition && id < kTracks)
        return "explore_tr" + std::to_string(id - kExploreTransition);
    return "?";
}

bool DynamicMusic::start(const MusicSet &set, const std::array<std::shared_ptr<const audio::Sound>, kTracks> &sounds,
                         uint64_t nowMs) {
    // As S_StartBackgroundTrack's dynamic path (0x427468 on): explore
    // fading in from its start over a second.
    stop();
    if (!sounds[kExplore] || !sounds[kAction])
        return false;
    _set = set;
    _sounds = sounds;
    _positions = {};
    _on = true;
    _pending = false;
    _requested = _current = kExplore;
    _playing = {};
    _playing.handle = _mixer.play(_sounds[kExplore], audio::SoundType::kMusic, 0);
    _playing.startMs = nowMs;
    _playing.target = 255;
    return true;
}

void DynamicMusic::stop() {
    for (Fade *f : {&_playing, &_fading})
        if (f->handle != audio::kNoSound) {
            _mixer.stop(f->handle);
            f->handle = audio::kNoSound;
        }
    _on = false;
    _sounds = {};
}

void DynamicMusic::request(std::string_view state) {
    // Configstring 907, read every frame (0x428290).
    const std::string s = fs::toLower(std::string(state));
    auto starts = [&](std::string_view word) { return s.compare(0, word.size(), word) == 0; };
    if (starts("silence"))
        _requested = kSilence;
    else if (starts("action"))
        _requested = kAction;
    else if (starts("boss"))
        _requested = loaded(kBoss) ? kBoss : _current;
    else if (starts("death"))
        _requested = loaded(kDeath) ? kDeath : _current;
    else
        _requested = kExplore;
}

double DynamicMusic::seconds(int id) const {
    if (!loaded(id) || _playing.handle == audio::kNoSound)
        return 0;
    const auto &sound = _sounds[size_t(id)];
    if (!_mixer.isPlaying(_playing.handle))
        return double(sound->frames()) / sound->rate;
    return _mixer.position(_playing.handle) / sound->rate;
}

double DynamicMusic::remaining(int id) const {
    if (!loaded(id))
        return 0;
    const auto &sound = _sounds[size_t(id)];
    return double(sound->frames()) / sound->rate - seconds(id);
}

void DynamicMusic::switchTo(int id, bool instant, uint64_t nowMs) {
    // As 0x427e50: the track playing fades out over a second, kept where it
    // got to; the new one plays on from where it was left, fading in, or at
    // once at full volume.
    if (_fading.handle != audio::kNoSound)
        _mixer.stop(_fading.handle);
    if (loaded(_current) && _playing.handle != audio::kNoSound)
        _positions[size_t(_current)] = seconds(_current);
    _fading = _playing;
    _fading.startMs = nowMs;
    _fading.target = 0;
    _playing = {};
    _current = id;
    if (!loaded(id))
        return; // silence, or a piece the set hasn't
    const auto &sound = _sounds[size_t(id)];
    _playing.handle = _mixer.play(sound, audio::SoundType::kMusic, instant ? audio::kMaxVolume : 0);
    _mixer.seek(_playing.handle, _positions[size_t(id)] * sound->rate);
    _playing.startMs = nowMs;
    _playing.target = 255;
    _playing.volume = instant ? 255 : 0;
}

bool DynamicMusic::exitPoint(int state, int &track, float &entry) const {
    // As 0x42d730: the time points either side of where the piece has got
    // to, the earlier first, the one within 0.3 s taken. Leaving explore
    // goes through explore_trN to the next state's start; leaving action
    // through action_trN to explore's marker named by that exit, or
    // through action_tr0 to its start if it names none.
    const std::optional<MusicPiece> &piece = state == kAction ? _set.action : _set.explore;
    if (!piece || (state != kAction && state != kExplore))
        return false;
    const auto now = float(seconds(state));
    const auto &times = piece->times;
    const auto lower = std::lower_bound(times.begin(), times.end(), now,
                                        [](const std::pair<float, int> &t, float v) { return t.first < v; });
    const auto upper =
        std::upper_bound(lower, times.end(), now, [](float v, const std::pair<float, int> &t) { return v < t.first; });
    const auto from = lower == times.begin() ? lower : lower - 1;
    const auto to = upper == times.end() ? upper : upper + 1;
    for (auto it = from; it != to; ++it) {
        if (std::fabs(it->first - now) > kExitWindow)
            continue;
        const int exit = it->second;
        if (state == kExplore) {
            track = kExploreTransition + exit;
            entry = 0;
            return true;
        }
        const std::string &mark = piece->exits[size_t(exit)].nextMark;
        if (mark.empty()) {
            track = kActionTransition;
            entry = 0;
            return true;
        }
        const std::optional<float> at = _set.explore ? _set.explore->marker(mark) : std::nullopt;
        if (!at)
            return false;
        track = kActionTransition + exit;
        entry = *at;
        return true;
    }
    return false;
}

float DynamicMusic::randomMarker(int state) {
    // As 0x42d930: a random entry marker, by name order, not the one taken
    // last time if there's another.
    const std::optional<MusicPiece> &piece = state == kAction ? _set.action : _set.explore;
    if (!piece || piece->markers.empty())
        return 0;
    const auto n = int(piece->markers.size());
    ++_markerCount;
    int index = int((_random() + uint32_t(_markerCount)) % uint32_t(n));
    if (index == _lastMarker && n > 1)
        index = (index + 1) % n;
    _lastMarker = index;
    return piece->markers[size_t(index)].second;
}

void DynamicMusic::setVolume(Fade &f, uint64_t nowMs) {
    // As 0x427f10: a linear second's fade to the target, its gain then
    // smoothed, half the old and half the new each frame.
    int volume = f.volume;
    if (volume != f.target) {
        const uint64_t elapsed = nowMs - f.startMs;
        if (elapsed <= kFadeMs) {
            const int part = int(double(elapsed) * 0.001 * 255);
            volume = f.target == 0 ? 255 - part : part;
        } else {
            volume = f.target;
        }
        f.volume = volume;
    }
    f.smoothed = (f.smoothed + float(volume) / 255) * 0.5f;
    if (f.handle != audio::kNoSound)
        _mixer.setVolume(f.handle, int(f.smoothed * float(audio::kMaxVolume)));
}

void DynamicMusic::update(uint64_t nowMs) {
    if (!_on)
        return;
    // Whether the state asked for may cut in now (0x42d6e0): death always,
    // and nothing cuts death; boss always, and only silence cuts boss;
    // action always; explore and silence not during a transition.
    const int want = _requested;
    const int now = _current;
    const bool transition = now >= kActionTransition && now < kTracks;
    bool may = false;
    if (want == now)
        may = false;
    else if (want == kDeath)
        may = true;
    else if (now == kDeath)
        may = false;
    else if (want == kBoss)
        may = true;
    else if (now == kBoss)
        may = want == kSilence;
    else if (want == kAction)
        may = true;
    else
        may = !transition;
    if (may) {
        // The change, as 0x4283a9 makes it.
        int track = 0;
        float entry = 0;
        switch (want) {
        case kExplore:
            if (now == kAction) {
                if (exitPoint(kAction, track, entry)) {
                    _positions[size_t(track)] = 0;
                    switchTo(track, false, nowMs);
                    _pending = true;
                    _next = kExplore;
                    _entry = entry;
                }
            } else if (now == kSilence) {
                _positions[kExplore] = 0;
                switchTo(kExplore, false, nowMs);
            } else {
                switchTo(kSilence, false, nowMs);
            }
            break;
        case kAction:
            if (now == kSilence) {
                _positions[kAction] = 0;
                switchTo(kAction, false, nowMs);
            } else {
                _positions[kAction] = randomMarker(kAction);
                switchTo(kAction, true, nowMs);
            }
            break;
        case kBoss:
            switchTo(kBoss, false, nowMs);
            break;
        case kDeath:
            switchTo(kDeath, true, nowMs);
            break;
        case kSilence:
            if (now == kExplore || now == kAction) {
                if (exitPoint(now, track, entry)) {
                    _positions[size_t(track)] = 0;
                    switchTo(track, false, nowMs);
                    _pending = true;
                    _next = kSilence;
                    _entry = 0;
                }
            } else {
                switchTo(kSilence, false, nowMs);
            }
            break;
        default:
            break;
        }
    }

    // Under two seconds left: a transition hands over to the state it
    // leads to, at its entry point; any other piece starts again from its
    // start, crossfading, but death, which plays once.
    if (_current != kSilence && loaded(_current) && remaining(_current) < kHandOverSeconds) {
        if (_pending) {
            _pending = false;
            if (loaded(_next))
                _positions[size_t(_next)] = _entry;
            switchTo(_next, false, nowMs);
        } else {
            // (Made silence first so the switch doesn't keep the old
            // place: the piece starts again from 0.)
            const int id = _current;
            _current = kSilence;
            _positions[size_t(id)] = 0;
            switchTo(id, false, nowMs);
            if (id == kDeath)
                _playing.target = 0;
        }
    }
    setVolume(_playing, nowMs);
    if (_fading.handle != audio::kNoSound) {
        setVolume(_fading, nowMs);
        if (nowMs - _fading.startMs > kFadeMs) {
            _mixer.stop(_fading.handle);
            _fading.handle = audio::kNoSound;
        }
    }
}

} // namespace reluva::idtech3
