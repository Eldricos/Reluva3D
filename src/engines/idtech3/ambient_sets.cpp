// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// How sets are read and played follows the GPL Jedi Knight engine's
// ambient sound sets (OpenJK), which Elite Force's engine shares (the same
// keywords, defaults and messages); this is new code written from that
// description.

#include "engines/idtech3/ambient_sets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace reluva::idtech3 {

namespace {

constexpr int kMaxVolume = 255;
constexpr int kMaxSubWaves = 8;
constexpr int64_t kCrossFadeMs = 1000;
// When a set last played a sound, before it ever has: long enough ago that
// its first comes at once, as the original's (whose clock starts with the
// program, not the level).
constexpr int64_t kNever = std::numeric_limits<int64_t>::min() / 2;

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool same(std::string_view a, std::string_view b) { return lower(a) == lower(b); }

std::vector<std::string> words(std::string_view line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        size_t j = i;
        while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j])))
            ++j;
        if (j > i)
            out.emplace_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

int toInt(const std::vector<std::string>& w, size_t i) {
    return i < w.size() ? std::atoi(w[i].c_str()) : 0;
}

const char* const kKindWords[] = {"generalSet", "localSet", "bmodelSet"};

bool isKindWord(std::string_view word) {
    for (const char* k : kKindWords)
        if (same(word, k))
            return true;
    return false;
}

} // namespace

void AmbientSets::addPrecacheEntry(std::string_view name) {
    if (same(name, "#clear")) {
        precache_.clear();
        current_ = old_ = -1;
    } else {
        precache_.insert(lower(name));
    }
}

void AmbientSets::clear() {
    sets_.clear();
    byName_.clear();
    parsed_ = false;
    current_ = old_ = -1;
    currentLast_ = oldLast_ = kNever;
}

bool AmbientSets::parse(std::string_view text, std::vector<std::string>& warnings) {
    clear();
    std::vector<std::vector<std::string>> lines;
    for (size_t at = 0; at <= text.size();) {
        size_t end = text.find_first_of("\r\n", at);
        if (end == std::string_view::npos)
            end = text.size();
        lines.push_back(words(text.substr(at, end - at)));
        at = end + 1;
    }

    // The header: "type ambientSet" says what the file is (amsdir, outdir
    // and basedir, the tools' folders, are ignored).
    for (const auto& w : lines)
        if (!w.empty() && same(w[0], "type")) {
            if (w.size() < 2 || !same(w[1], "ambientSet")) {
                warnings.push_back("set type \"" + (w.size() > 1 ? w[1] : std::string()) +
                                   "\" is not a valid set type");
                return false;
            }
            break;
        }

    // Each kind in turn, through the whole file: "<kind> <name>", then its
    // keywords a line each up to the first line that isn't one.
    for (int k = 0; k < 3; ++k) {
        const Kind kind = Kind(k);
        for (size_t i = 0; i < lines.size();) {
            const auto& head = lines[i];
            if (head.empty() || !same(head[0], kKindWords[k]) || head.size() < 2 ||
                !precache_.count(lower(head[1]))) {
                ++i;
                continue;
            }
            Set set;
            set.name = head[1];
            set.kind = kind;
            set.id = int(sets_.size());
            for (++i; i < lines.size(); ++i) {
                const auto& w = lines[i];
                if (w.empty())
                    continue;
                const std::string key = lower(w[0]);
                if (key == "subwaves") {
                    for (size_t n = 2; n < w.size(); ++n) {
                        if (set.subWaves.size() >= kMaxSubWaves) {
                            warnings.push_back("too many subwaves on set \"" + set.name + "\"");
                            break;
                        }
                        set.subWaves.push_back("sound/" + (w.size() > 1 ? w[1] : std::string()) + "/" + w[n] + ".wav");
                    }
                } else if (key == "timebetweenwaves" && kind != Kind::BModel) {
                    set.timeStart = toInt(w, 1);
                    set.timeEnd = toInt(w, 2);
                    if (set.timeStart > set.timeEnd)
                        std::swap(set.timeStart, set.timeEnd);
                } else if (key == "loopedwave" && kind != Kind::BModel) {
                    if (w.size() > 1)
                        set.loopedWave = "sound/" + w[1] + ".wav";
                } else if (key == "volrange" && kind != Kind::BModel) {
                    set.volumeMin = toInt(w, 1);
                    set.volumeMax = toInt(w, 2);
                    if (set.volumeMin > set.volumeMax)
                        std::swap(set.volumeMin, set.volumeMax);
                } else if (key == "radius" && kind == Kind::Local) {
                    set.radius = toInt(w, 1);
                } else {
                    // The next set, a comment, or a mistake.
                    if (!isKindWord(w[0]) && w[0][0] != ';')
                        warnings.push_back("unknown ambient set keyword \"" + w[0] + "\"");
                    break;
                }
            }
            byName_[lower(set.name)] = sets_.size();
            sets_.push_back(std::move(set));
        }
    }
    for (const std::string& name : precache_)
        if (!byName_.count(name))
            warnings.push_back("unable to find ambient set \"" + name + "\"");
    parsed_ = true;
    return true;
}

const AmbientSets::Set* AmbientSets::find(std::string_view name) const {
    const auto it = byName_.find(lower(name));
    return it == byName_.end() ? nullptr : &sets_[it->second];
}

AmbientSets::Set* AmbientSets::findSet(std::string_view name) {
    const auto it = byName_.find(lower(name));
    return it == byName_.end() ? nullptr : &sets_[it->second];
}

int AmbientSets::pick(int low, int high) {
    if (high <= low)
        return low;
    return std::uniform_int_distribution<int>(low, high)(random_);
}

void AmbientSets::fade(int64_t nowMs) {
    // The new set comes up over a second, the one before goes down.
    if (current_ < 0 || current_ >= int(sets_.size()))
        return;
    Set& current = sets_[size_t(current_)];
    if (current.masterVolume < kMaxVolume)
        current.masterVolume = int(float(nowMs - current.fadeStart) / float(kCrossFadeMs) * kMaxVolume);
    current.masterVolume = std::min(current.masterVolume, kMaxVolume);
    if (old_ < 0 || old_ >= int(sets_.size()))
        return;
    Set& old = sets_[size_t(old_)];
    if (old.masterVolume > 0)
        old.masterVolume = kMaxVolume - int(float(nowMs - old.fadeStart) / float(kCrossFadeMs) * kMaxVolume);
    if (old.masterVolume <= 0) {
        old.masterVolume = 0;
        old_ = -1;
    }
}

void AmbientSets::playGeneral(const Set& set, const Vec3f& origin, int64_t& lastMs, int64_t nowMs,
                              std::vector<Sound>& out) {
    // Heard everywhere: at the view. Each set's loop is kept apart (its
    // own entity number) so two can cross-fade.
    const int entity = -2 - set.id;
    if (!set.loopedWave.empty())
        out.push_back({true, entity, origin, float(set.masterVolume) / kMaxVolume, set.loopedWave});
    if (nowMs - lastMs < int64_t(pick(set.timeStart, set.timeEnd)) * 1000)
        return;
    lastMs = nowMs;
    const float scale = float(set.masterVolume) / kMaxVolume;
    const int volume = std::min(pick(int(scale * float(set.volumeMin)), int(scale * float(set.volumeMax))),
                                set.masterVolume);
    if (!set.subWaves.empty())
        out.push_back({false, 0, origin, float(volume) / kMaxVolume,
                       set.subWaves[size_t(pick(0, int(set.subWaves.size()) - 1))]});
}

std::vector<AmbientSets::Sound> AmbientSets::updateGeneral(std::string_view name, const Vec3f& origin,
                                                           int64_t nowMs) {
    std::vector<Sound> out;
    Set* set = findSet(name);
    if (!set)
        return out;
    if (set->id != current_) {
        old_ = current_;
        current_ = set->id;
        set->masterVolume = 0;
        set->fadeStart = nowMs;
        if (old_ >= 0 && old_ < int(sets_.size())) {
            sets_[size_t(old_)].masterVolume = kMaxVolume;
            sets_[size_t(old_)].fadeStart = nowMs;
        }
    }
    fade(nowMs);
    playGeneral(*set, origin, currentLast_, nowMs, out);
    if (old_ >= 0 && old_ < int(sets_.size()))
        playGeneral(sets_[size_t(old_)], origin, oldLast_, nowMs, out);
    return out;
}

std::vector<AmbientSets::Sound> AmbientSets::addLocal(std::string_view name, const Vec3f& listener,
                                                      const Vec3f& origin, int entity, int64_t& lastMs,
                                                      int64_t nowMs) {
    std::vector<Sound> out;
    const Set* set = find(name);
    if (!set) {
        lastMs = nowMs;
        return out;
    }
    // Full within half its radius, then fading to nothing at its edge.
    const float dx = origin[0] - listener[0], dy = origin[1] - listener[1], dz = origin[2] - listener[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float half = float(set->radius) * 0.5f;
    const float scale = dist < half ? 1.0f : (float(set->radius) - dist) / half;
    const int volume = scale > 1 || scale < 0 || !(half > 0) ? 0 : int(float(set->masterVolume) * scale);
    if (!set->loopedWave.empty())
        out.push_back({true, entity, origin, float(volume) / kMaxVolume, set->loopedWave});
    if (nowMs - lastMs < int64_t(pick(set->timeStart, set->timeEnd)) * 1000)
        return out;
    lastMs = nowMs;
    const float volScale = float(volume) / kMaxVolume;
    const int played = pick(int(volScale * float(set->volumeMin)), int(volScale * float(set->volumeMax)));
    if (!set->subWaves.empty())
        out.push_back({false, entity, origin, float(std::min(played, kMaxVolume)) / kMaxVolume,
                       set->subWaves[size_t(pick(0, int(set->subWaves.size()) - 1))]});
    return out;
}

std::string AmbientSets::bmodelSound(std::string_view name, int stage) const {
    const Set* set = find(name);
    if (!set || stage < 0 || stage >= int(set->subWaves.size()))
        return {};
    return set->subWaves[size_t(stage)];
}

} // namespace reluva::idtech3
