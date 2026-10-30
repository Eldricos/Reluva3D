// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Sounds, Music and Movies pages.

#include "engines/nfs/viewers.h"

#include "engines/nfs/page_draw.h"

#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace reluva::nfs {

namespace {

using namespace page;

constexpr Color kBarColor{70, 80, 110};
constexpr Color kFillColor{255, 200, 64};
constexpr Color kWaveColor{120, 200, 255};

std::string seconds(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f s", s);
    return buf;
}

const char* compressionName(int c) {
    switch (c) {
    case SoundHeader::kPcm16: return "PCM";
    case SoundHeader::kEaXa: return "EA-XA";
    case SoundHeader::kMicroTalk: return "MicroTalk";
    default: return "?";
    }
}

// A bar showing `done` of `total`.
void progress(Surface& s, int x, int y, int w, double done, double total) {
    fill(s, x, y, w, 6, kBarColor);
    if (total > 0)
        fill(s, x, y, static_cast<int>(w * std::clamp(done / total, 0.0, 1.0)), 6, kFillColor);
}

// A file's name without its extension, lower case: for finding the files
// that go with it.
std::string withoutExtension(const std::string& name) {
    const size_t dot = name.rfind('.');
    return fs::toLower(dot == std::string::npos ? name : name.substr(0, dot));
}

// The part of a path after its last '/'.
std::string baseName(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

const char* const kNoAudio = "No audio output: sounds can't be played";

} // namespace

// --- Sounds -------------------------------------------------------------------

SoundPage::SoundPage(const GameFiles& files, audio::Mixer* mixer) : files_(files), mixer_(mixer) {
    banks_ = files.withExtensions({".bnk"});
}

void SoundPage::deactivate() {
    if (mixer_ && playing_ != audio::kNoSound)
        mixer_->stop(playing_);
    playing_ = audio::kNoSound;
}

void SoundPage::load(int bank) {
    bank_ = std::clamp(bank, 0, static_cast<int>(banks_.size()) - 1);
    loaded_ = {};
    sounds_.clear();
    error_.clear();
    if (Error err = files_.read(banks_[bank_], data_))
        error_ = err.message;
    else if (Error err2 = parseSoundBank(data_, loaded_))
        error_ = err2.message;
    for (size_t i = 0; i < loaded_.slots.size(); ++i)
        if (loaded_.slots[i])
            sounds_.push_back(static_cast<int>(i));
    // A car's banks sit in its car.viv with its carp.txt: the engine's
    // revs, from idle ("engine minimum rpm") to "engine redline in rpm".
    minRpm_ = redline_ = rpm_ = 0;
    const FileRef& bankFile = banks_[bank_];
    if (!bankFile.inner.empty())
        for (const FileRef& f : files_.files())
            if (f.file == bankFile.file && fs::toLower(f.inner) == "carp.txt") {
                std::vector<uint8_t> carp;
                if (files_.read(f, carp))
                    break;
                const std::string text(carp.begin(), carp.end());
                auto value = [&](const std::string& name) {
                    const size_t at = text.find(name);
                    const size_t line = at == std::string::npos ? at : text.find('\n', at);
                    return line == std::string::npos ? 0 : std::atoi(text.c_str() + line + 1);
                };
                minRpm_ = value("engine minimum rpm");
                redline_ = value("engine redline in rpm");
                if (redline_ <= minRpm_)
                    minRpm_ = redline_ = 0;
                rpm_ = minRpm_;
                break;
            }
    sound_ = -1;
    select(0);
}

bool SoundPage::engine() const {
    return !sounds_.empty() && loaded_.slots[size_t(sounds_[size_t(sound_)])]->tags.count(0x0A) != 0;
}

// The key a sound is played at: 60 for a car's sounds, as the game plays
// its engine; elsewhere the sound's own root key (0x07), the key a caller
// would play isn't known.
static int keyFor(const SoundHeader& h, bool car) {
    auto root = h.tags.find(0x07);
    return car || root == h.tags.end() ? 60 : static_cast<int>(root->second);
}

double SoundPage::rate() const {
    if (!engine())
        return std::pow(2.0, semitones_ / 12.0);
    const SoundHeader& h = *loaded_.slots[size_t(sounds_[size_t(sound_)])];
    const int bend = redline_ ? engineBend(rpm_, redline_) : bend_;
    return std::pow(2.0, patchCents(h, keyFor(h, redline_ != 0), bend) / 1200.0);
}

std::string SoundPage::engineText() const {
    const SoundHeader& h = *loaded_.slots[size_t(sounds_[size_t(sound_)])];
    const int bend = redline_ ? engineBend(rpm_, redline_) : bend_;
    std::string out = redline_ ? "   " + std::to_string(rpm_) + " rpm (redline " + std::to_string(redline_) + "), bend "
                               : "   bend ";
    return out + std::to_string(bend) + ", " + std::to_string(patchCents(h, keyFor(h, redline_ != 0), bend)) +
           " cents";
}

void SoundPage::select(int sound) {
    decoded_.reset();
    soundError_.clear();
    if (sounds_.empty()) {
        sound_ = 0;
        return;
    }
    sound_ = std::clamp(sound, 0, static_cast<int>(sounds_.size()) - 1);
    auto decoded = std::make_shared<audio::Sound>();
    if (Error err = decodeBankSound(data_, *loaded_.slots[sounds_[sound_]], *decoded))
        soundError_ = err.message;
    else
        decoded_ = std::move(decoded);
    if (autoPlay_)
        play();
}

// A sound with a loop (engines, ambiences) plays it over and over until
// stopped, at the pitch chosen.
void SoundPage::play() {
    deactivate();
    if (mixer_ && decoded_) {
        playing_ = mixer_->play(decoded_, audio::SoundType::Effects, audio::kMaxVolume, 0, decoded_->loopEnd != 0);
        mixer_->setRate(playing_, rate());
    }
}

void SoundPage::handleKey(uint32_t k, uint16_t) {
    if (bank_ < 0)
        load(0);
    const int count = static_cast<int>(sounds_.size());
    switch (k) {
    case key::Up: select(sound_ - 1); break;
    case key::Down: select(sound_ + 1); break;
    case key::Home: select(0); break;
    case key::End: select(count - 1); break;
    case key::PageUp: load(bank_ - 1); break;
    case key::PageDown: load(bank_ + 1); break;
    case key::Space:
    case key::Return: play(); break;
    case 's': deactivate(); break;
    case 'p': autoPlay_ = !autoPlay_; break;
    case key::Left:
    case key::Right:
        // An engine's revs (250 at a time) or bend (4); else the pitch, a
        // semitone at a time.
        if (engine() && redline_)
            rpm_ = std::clamp(rpm_ + (k == key::Right ? 250 : -250), minRpm_, redline_);
        else if (engine())
            bend_ = std::clamp(bend_ + (k == key::Right ? 4 : -4), 0, 127);
        else
            semitones_ = std::clamp(semitones_ + (k == key::Right ? 1 : -1), -24, 24);
        if (mixer_ && playing_ != audio::kNoSound)
            mixer_->setRate(playing_, rate());
        break;
    default: break;
    }
}

bool SoundPage::tick(uint64_t) {
    // Redrawn while playing, for the position, and once when it ends.
    if (!mixer_ || playing_ == audio::kNoSound)
        return false;
    if (!mixer_->isPlaying(playing_))
        playing_ = audio::kNoSound;
    return true;
}

void SoundPage::draw(Surface& s) {
    if (bank_ < 0)
        load(0);
    frame(s, "Sounds  " + std::to_string(bank_ + 1) + "/" + std::to_string(banks_.size()) + "  " + banks_[bank_].name,
          "Up/Down: sound  PgUp/PgDn: bank  Space: play  S: stop  P: autoplay  Left/Right: pitch");
    int y = 8 + font::kLineHeight * 2;
    if (!error_.empty()) {
        text(s, kMargin, y, error_, kWarnColor);
        return;
    }
    text(s, kMargin, y,
         "Version " + std::to_string(loaded_.version) + ", " + std::to_string(loaded_.slots.size()) + " slots, " +
             std::to_string(sounds_.size()) + " sounds" + (autoPlay_ ? "   (plays when selected)" : "") +
             (engine() ? engineText() : semitones_ ? "   pitch " + std::string(semitones_ > 0 ? "+" : "") +
                                                          std::to_string(semitones_) + " semitones"
                                                    : std::string()),
         kLabelColor);
    y += font::kLineHeight;
    if (!mixer_) {
        text(s, kMargin, y, kNoAudio, kWarnColor);
    }
    y += font::kLineHeight + 4;
    text(s, kMargin, y, "Slot  Codec      Ch  Rate    Samples   Length  Loop", kLabelColor);
    y += font::kLineHeight;

    // The list, scrolled to keep the selection in view; a waveform below.
    constexpr int kWaveH = 100;
    const int rows = std::max(1, (s.h - y - kWaveH - font::kLineHeight * 3) / font::kLineHeight);
    const int count = static_cast<int>(sounds_.size());
    const int top = std::clamp(sound_ - rows / 2, 0, std::max(0, count - rows));
    for (int i = top; i < std::min(count, top + rows); ++i, y += font::kLineHeight) {
        const SoundHeader& h = *loaded_.slots[sounds_[i]];
        char line[160];
        std::snprintf(line, sizeof line, "%4d  %-9s  %2d  %5d  %8u  %7s  %s", sounds_[i], compressionName(h.compression),
                      h.channels, h.rate, h.samples, seconds(double(h.samples) / h.rate).c_str(),
                      h.loopEnd ? (std::to_string(h.loopStart) + "-" + std::to_string(h.loopEnd)).c_str() : "");
        const bool selected = i == sound_;
        if (selected)
            fill(s, kMargin - 4, y - 1, s.w - 2 * kMargin + 8, font::kLineHeight, kBarColor);
        text(s, kMargin, y, line, selected ? kTitleColor : kTextColor);
    }

    const int waveY = s.h - kWaveH - font::kLineHeight * 2 - 4, waveW = s.w - 2 * kMargin;
    if (!soundError_.empty()) {
        text(s, kMargin, waveY, soundError_, kWarnColor);
        return;
    }
    if (!decoded_ || decoded_->frames() == 0)
        return;
    // Each column: the first channel's lowest and highest sample there.
    const audio::Sound& snd = *decoded_;
    const size_t frames = snd.frames();
    fill(s, kMargin, waveY + kWaveH / 2, waveW, 1, kBarColor);
    for (int x = 0; x < waveW; ++x) {
        const size_t a = frames * x / waveW, b = std::max(a + 1, frames * (x + 1) / waveW);
        int lo = 0, hi = 0;
        for (size_t f = a; f < b && f < frames; ++f) {
            const int v = snd.samples[f * snd.channels];
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        const int top = waveY + kWaveH / 2 - hi * (kWaveH / 2) / 32768;
        const int bottom = waveY + kWaveH / 2 - lo * (kWaveH / 2) / 32768;
        fill(s, kMargin + x, top, 1, std::max(1, bottom - top), kWaveColor);
    }
    if (snd.loopEnd) { // the loop's ends
        const size_t end = std::min(snd.loopEnd, frames);
        fill(s, kMargin + static_cast<int>(waveW * snd.loopStart / frames), waveY, 1, kWaveH, kLabelColor);
        fill(s, kMargin + static_cast<int>(waveW * (end - 1) / frames), waveY, 1, kWaveH, kLabelColor);
    }
    if (mixer_ && playing_ != audio::kNoSound) {
        const int at = static_cast<int>(waveW * std::min(1.0, mixer_->position(playing_) / double(frames)));
        fill(s, kMargin + at, waveY, 1, kWaveH, kFillColor);
    }
}

// --- Music --------------------------------------------------------------------

MusicPage::MusicPage(const GameFiles& files, audio::Mixer* mixer) : files_(files), mixer_(mixer) {
    streams_ = files.withExtensions({".mus"});
}

void MusicPage::deactivate() { stop(); }

void MusicPage::stop() {
    if (mixer_ && playing_ != audio::kNoSound)
        mixer_->stop(playing_);
    playing_ = audio::kNoSound;
    current_.reset();
    queue_.reset();
    queuedSection_ = -1;
}

std::shared_ptr<audio::Sound> MusicPage::decodeSection(int section) {
    const MusicMap* m = activeMap();
    const int segment = m ? loaded_.segmentAt(m->offsets[section]) : section;
    auto sound = std::make_shared<audio::Sound>();
    if (Error err = loaded_.decode(segment, *sound)) {
        error_ = err.message;
        return nullptr;
    }
    return sound;
}

void MusicPage::load(int stream) {
    stop();
    stream_ = std::clamp(stream, 0, static_cast<int>(streams_.size()) - 1);
    loaded_ = {};
    map_ = linear_ = {};
    haveMap_ = haveLinear_ = false;
    error_.clear();
    mapError_.clear();
    section_ = -1;
    history_.clear();
    std::vector<uint8_t> data;
    if (Error err = files_.read(streams_[stream_], data)) {
        error_ = err.message;
        return;
    }
    if (Error err = loaded_.open(std::move(data))) {
        error_ = err.message;
        return;
    }
    // The maps beside it, by name; each must name this stream's segments.
    const std::string base = withoutExtension(streams_[stream_].name);
    for (const FileRef& ref : files_.withExtensions({".map", ".lin"})) {
        if (withoutExtension(ref.name) != base)
            continue;
        const bool linear = fs::toLower(ref.name).ends_with(".lin");
        MusicMap& m = linear ? linear_ : map_;
        Error err = files_.read(ref, data);
        if (!err)
            err = parseMusicMap(data, m);
        for (size_t i = 0; !err && i < m.offsets.size(); ++i)
            if (loaded_.segmentAt(m.offsets[i]) < 0)
                err = Error::fail("a section names no segment");
        if (err)
            mapError_ = baseName(ref.name) + ": " + err.message;
        else
            (linear ? haveLinear_ : haveMap_) = true;
    }
    useLinear_ = !haveMap_ && haveLinear_;
}

const MusicMap* MusicPage::activeMap() const {
    if (useLinear_ ? haveLinear_ : haveMap_)
        return useLinear_ ? &linear_ : &map_;
    return nullptr;
}

// Where the music starts: the map's start section (a race's intro), or
// the first.
int MusicPage::firstSection() const {
    if (const MusicMap* m = activeMap())
        if (m->start >= 0 && m->start < static_cast<int>(m->sections.size()))
            return m->start;
    return 0;
}

int MusicPage::following(int section) {
    if (const MusicMap* m = activeMap()) {
        // An event pending goes first, to its section for this part
        // (nfs3.exe 0x4e6be3); else the route for the intensity.
        if (pendingEvent_ >= 0) {
            const int to = m->event(section, pendingEvent_);
            pendingEvent_ = -1;
            if (to >= 0)
                return to;
        }
        return m->next(section, intensity_);
    }
    // No map: the segments in order.
    return section + 1 < static_cast<int>(loaded_.segments().size()) ? section + 1 : -1;
}

void MusicPage::start(int section) {
    stop();
    section_ = section;
    if (section < 0)
        return;
    history_.push_back(section);
    if (history_.size() > 64)
        history_.erase(history_.begin());
    current_ = decodeSection(section);
    if (!current_ || !mixer_)
        return;
    // Played as a stream of segments, the next queued behind it.
    queue_ = std::make_shared<audio::SoundQueue>();
    queue_->push(current_, section);
    queuedSection_ = section;
    if (const int next = following(section); next >= 0)
        if (auto sound = decodeSection(next)) {
            queue_->push(sound, next);
            queuedSection_ = next;
        }
    playing_ = mixer_->playStream(queue_, audio::SoundType::Music);
}

void MusicPage::handleKey(uint32_t k, uint16_t) {
    if (stream_ < 0)
        load(0);
    switch (k) {
    case key::Up: load(stream_ - 1); break;
    case key::Down: load(stream_ + 1); break;
    case key::Left: intensity_ = std::max(0, intensity_ - 10); break;
    case key::Right: intensity_ = std::min(100, intensity_ + 10); break;
    case key::Space:
        if (playing_ != audio::kNoSound || (!mixer_ && section_ >= 0))
            stop(), section_ = -1;
        else
            start(firstSection());
        break;
    case key::Home: start(firstSection()); break;
    case 'n': start(section_ < 0 ? firstSection() : following(section_)); break;
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        pendingEvent_ = static_cast<int>(k - '0');
        break;
    case 'l':
        if (haveMap_ && haveLinear_) {
            useLinear_ = !useLinear_;
            stop();
            section_ = -1;
            history_.clear();
        }
        break;
    default: break;
    }
}

bool MusicPage::tick(uint64_t nowMs) {
    if (!mixer_ || playing_ == audio::kNoSound)
        return false;
    // When the queue moves on to the next segment: that one is playing,
    // and the one after it is queued (decided now, by the intensity).
    if (queue_) {
        const int now = queue_->playing();
        if (now < 0) {
            stop();
            section_ = -1;
            return true;
        }
        if (now != section_) {
            section_ = now;
            history_.push_back(now);
            if (history_.size() > 64)
                history_.erase(history_.begin());
        }
        if (queue_->queued() < 2 && queuedSection_ >= 0) {
            const int next = following(queuedSection_);
            queuedSection_ = next;
            if (next >= 0)
                if (auto sound = decodeSection(next))
                    queue_->push(sound, next);
            return true;
        }
    }
    if (nowMs - lastDraw_ < 100)
        return false;
    lastDraw_ = nowMs;
    return true;
}

void MusicPage::draw(Surface& s) {
    if (stream_ < 0)
        load(0);
    frame(s, "Music  " + std::to_string(stream_ + 1) + "/" + std::to_string(streams_.size()) + "  " +
                 streams_[stream_].name,
          "Up/Down: stream  Space: play  Left/Right: intensity  N: next  0-9: event");
    int y = 8 + font::kLineHeight * 2;
    if (!error_.empty()) {
        text(s, kMargin, y, error_, kWarnColor);
        return;
    }
    const MusicMap* m = activeMap();
    std::string order = m ? std::string(useLinear_ ? "linear order (.lin)" : "Pathfinder map (.map)") +
                                ", " + std::to_string(m->sections.size()) + " sections, " +
                                std::to_string(m->events.size()) + " events"
                          : "no map: segments in order";
    if (m && firstSection() != 0)
        order += ", starting at section " + std::to_string(firstSection());
    if (haveMap_ && haveLinear_)
        order += "   L: the other order";
    text(s, kMargin, y, std::to_string(loaded_.segments().size()) + " segments; " + order, kTextColor);
    y += font::kLineHeight;
    if (!mapError_.empty()) {
        text(s, kMargin, y, mapError_, kWarnColor);
        y += font::kLineHeight;
    }
    if (!mixer_) {
        text(s, kMargin, y, kNoAudio, kWarnColor);
        y += font::kLineHeight;
    }
    y += font::kLineHeight;

    text(s, kMargin, y, "Intensity " + std::to_string(intensity_), kLabelColor);
    progress(s, kMargin + 15 * font::kAdvance, y + 4, 200, intensity_, 100);
    y += font::kLineHeight * 2;

    if (section_ < 0) {
        text(s, kMargin, y, "Stopped. Space plays from the start.", kLabelColor);
        return;
    }
    const int segment = m ? loaded_.segmentAt(m->offsets[section_]) : section_;
    const auto& header = loaded_.segments()[segment].header;
    const double length = double(header.samples) / header.rate;
    const double at = mixer_ && playing_ != audio::kNoSound ? mixer_->position(playing_) / header.rate : 0;
    text(s, kMargin, y,
         "Section " + std::to_string(section_) + " (segment " + std::to_string(segment) + ", " +
             compressionName(header.compression) + ")   " + seconds(at) + " / " + seconds(length),
         kTitleColor);
    y += font::kLineHeight;
    progress(s, kMargin, y + 2, s.w - 2 * kMargin, at, length);
    y += font::kLineHeight + 4;

    if (m) {
        const MusicMap::Section& sec = m->sections[section_];
        text(s, kMargin, y, "Routes from here (part " + std::to_string(sec.part) + "):", kLabelColor);
        y += font::kLineHeight;
        if (sec.routes.empty())
            text(s, kMargin + 2 * font::kAdvance, y, "none: the music ends here", kTextColor), y += font::kLineHeight;
        for (const auto& r : sec.routes) {
            const bool taken = intensity_ >= r.low && intensity_ <= r.high;
            char line[64];
            std::snprintf(line, sizeof line, "%3d-%3d  ->  section %d", r.low, r.high, r.next);
            text(s, kMargin + 2 * font::kAdvance, y, std::string(line) + (taken ? "   <- next" : ""),
                 taken ? kTitleColor : kTextColor);
            y += font::kLineHeight;
        }
        // This part's events: where each leads (the game sends 1 for a
        // pursuit and the track's zones, 4-9), and the one pending.
        std::string events = "Events:";
        for (int e = 0; e < 10; ++e)
            if (const int to = m->event(section_, e); to >= 0 && (e == 0 || to != 0))
                events += "  " + std::to_string(e) + ">" + std::to_string(to);
        if (pendingEvent_ >= 0)
            events += "   pending " + std::to_string(pendingEvent_);
        text(s, kMargin, y, events, pendingEvent_ >= 0 ? kTitleColor : kTextColor);
        y += font::kLineHeight;
    } else {
        const int next = following(section_);
        text(s, kMargin, y, next < 0 ? "Last segment" : "Next: segment " + std::to_string(next), kLabelColor);
        y += font::kLineHeight;
    }
    y += font::kLineHeight;
    // The sections played, latest last, as many as fit.
    text(s, kMargin, y, "Played:", kLabelColor);
    y += font::kLineHeight;
    std::string line;
    for (size_t i = 0; i < history_.size(); ++i) {
        const std::string item = (i ? " > " : "") + std::to_string(history_[i]);
        if ((font::length(line) + item.size()) * font::kAdvance > static_cast<size_t>(s.w - 2 * kMargin)) {
            text(s, kMargin, y, line, kTextColor);
            y += font::kLineHeight;
            line.clear();
        }
        line += item;
    }
    text(s, kMargin, y, line, kTextColor);
}

// --- Movies -------------------------------------------------------------------

MoviePage::MoviePage(const GameFiles& files, audio::Mixer* mixer) : files_(files), mixer_(mixer) {
    movies_ = files.withExtensions({".mad"});
}

double MoviePage::frameMs() const {
    const double ms = loaded_.frameDuration() / 65536.0;
    return ms > 1 ? ms : 1000.0 / 15;
}

void MoviePage::stopSound() {
    if (mixer_ && playing_ != audio::kNoSound)
        mixer_->stop(playing_);
    playing_ = audio::kNoSound;
}

void MoviePage::activate(System& sys) {
    nowMs_ = sys.getMillis();
    if (movie_ >= 0 && !paused_)
        seek(decoded_, nowMs_);
}

void MoviePage::deactivate() {
    stopSound();
    paused_ = true;
}

void MoviePage::load(int movie) {
    stopSound();
    movie_ = std::clamp(movie, 0, static_cast<int>(movies_.size()) - 1);
    loaded_ = {};
    audio_.reset();
    error_.clear();
    decoder_ = {};
    decoder_.setArithmetic(display_ ? MadDecoder::Arithmetic::Game : MadDecoder::Arithmetic::Reference);
    decoded_ = -1;
    picture_ = {};
    std::vector<uint8_t> data;
    if (Error err = files_.read(movies_[movie_], data)) {
        error_ = err.message;
        return;
    }
    if (Error err = loaded_.open(std::move(data))) {
        error_ = err.message;
        return;
    }
    if (loaded_.hasAudio()) {
        auto sound = std::make_shared<audio::Sound>();
        if (!loaded_.decodeAudio(*sound))
            audio_ = std::move(sound);
    }
    seek(0, nowMs_);
}

void MoviePage::showFrame(int frame) {
    const int last = static_cast<int>(loaded_.frames().size()) - 1;
    frame = std::clamp(frame, 0, last);
    if (frame == decoded_ && !picture_.pixels.empty())
        return;
    // Frames refer to the one before, back to a key frame: from there, or
    // on from the frame held.
    int from = frame;
    while (from > 0 && loaded_.frames()[from].kind != 'k')
        --from;
    if (decoded_ >= from && decoded_ < frame)
        from = decoded_ + 1;
    for (int i = from; i <= frame; ++i)
        if (Error err = loaded_.decodeFrame(decoder_, i)) {
            error_ = "frame " + std::to_string(i) + ": " + err.message;
            decoded_ = -1;
            return;
        }
    decoded_ = frame;
    if (display_)
        decoder_.toGameSurface(picture_, static_cast<MadDecoder::Display>(display_));
    else
        decoder_.toSurface(picture_);
}

void MoviePage::seek(int frame, uint64_t nowMs) {
    if (!error_.empty() || loaded_.frames().empty())
        return;
    stopSound();
    showFrame(frame);
    startFrame_ = std::max(decoded_, 0);
    startMs_ = nowMs;
    if (paused_ || !mixer_ || !audio_)
        return;
    // The sound from this frame on.
    auto rest = std::make_shared<audio::Sound>(*audio_);
    const size_t skip = std::min(rest->frames(), static_cast<size_t>(startFrame_ * frameMs() * audio_->rate / 1000));
    rest->samples.erase(rest->samples.begin(), rest->samples.begin() + skip * rest->channels);
    playing_ = mixer_->play(rest, audio::SoundType::Music);
}

void MoviePage::handleKey(uint32_t k, uint16_t) {
    if (movie_ < 0)
        load(0);
    const int step = static_cast<int>(std::lround(5000 / frameMs()));
    switch (k) {
    case key::Up: load(movie_ - 1); break;
    case key::Down: load(movie_ + 1); break;
    case key::Home: seek(0, nowMs_); break;
    case key::Left: seek(decoded_ - step, nowMs_); break;
    case key::Right: seek(decoded_ + step, nowMs_); break;
    case key::Space:
        paused_ = !paused_;
        if (paused_)
            stopSound();
        else
            seek(decoded_ + 1 >= static_cast<int>(loaded_.frames().size()) ? 0 : decoded_, nowMs_);
        break;
    case key::PageUp:
        paused_ = true;
        stopSound();
        showFrame(decoded_ - 1);
        break;
    case key::PageDown:
        paused_ = true;
        stopSound();
        showFrame(decoded_ + 1);
        break;
    case 'g': {
        // 3, 2, 1, then Reluva's own; the decoder starts again from the
        // first frame with the arithmetic to match.
        display_ = (display_ + 3) % 4;
        const int frame = decoded_;
        decoded_ = -1;
        decoder_ = {};
        decoder_.setArithmetic(display_ ? MadDecoder::Arithmetic::Game : MadDecoder::Arithmetic::Reference);
        showFrame(std::max(frame, 0));
        break;
    }
    default: break;
    }
}

bool MoviePage::tick(uint64_t nowMs) {
    nowMs_ = nowMs;
    if (movie_ < 0 || paused_ || !error_.empty() || loaded_.frames().empty())
        return false;
    // The sound is the clock while it plays; without it, the time.
    double elapsedMs;
    if (playing_ != audio::kNoSound) {
        if (!mixer_->isPlaying(playing_)) {
            playing_ = audio::kNoSound;
            elapsedMs = 1e12; // the sound ended: so has the movie
        } else {
            elapsedMs = mixer_->position(playing_) * 1000.0 / audio_->rate;
        }
    } else {
        elapsedMs = double(nowMs - startMs_);
    }
    const int last = static_cast<int>(loaded_.frames().size()) - 1;
    const int target = startFrame_ + static_cast<int>(elapsedMs / frameMs());
    if (target >= last) {
        showFrame(last);
        paused_ = true;
        stopSound();
        return true;
    }
    if (target == decoded_)
        return false;
    showFrame(target);
    return true;
}

void MoviePage::draw(Surface& s) {
    if (movie_ < 0)
        load(0);
    frame(s, "Movies  " + std::to_string(movie_ + 1) + "/" + std::to_string(movies_.size()) + "  " +
                 movies_[movie_].name,
          "Up/Down: movie  Space: pause  Left/Right: 5 s  PgUp/PgDn: frame  G: display");
    int y = 8 + font::kLineHeight * 2;
    if (!error_.empty()) {
        text(s, kMargin, y, error_, kWarnColor);
        return;
    }
    const int count = static_cast<int>(loaded_.frames().size());
    const double total = count * frameMs() / 1000;
    char info[160];
    static constexpr const char* kDisplays[] = {"Reluva's conversion", "game mode 1", "game mode 2",
                                                "game mode 3"};
    std::snprintf(info, sizeof info, "%dx%d  %.0f fps  frame %d/%d (%s)  %s / %s  %s%s  %s", loaded_.width(),
                  loaded_.height(), 1000 / frameMs(), decoded_ + 1, count,
                  decoded_ >= 0 && loaded_.frames()[decoded_].kind == 'k' ? "key" : "inter",
                  seconds(std::max(decoded_, 0) * frameMs() / 1000).c_str(), seconds(total).c_str(),
                  paused_ ? "paused" : "playing", mixer_ ? (audio_ ? "" : "  (no sound)") : "  (no audio output)",
                  kDisplays[display_]);
    text(s, kMargin, y, info, kTextColor);
    y += font::kLineHeight + 4;

    // The picture, as large as fits (nearest pixel), then the position.
    const int areaW = s.w - 2 * kMargin, areaH = s.h - y - font::kLineHeight * 2 - 12;
    if (picture_.w > 0 && picture_.h > 0) {
        const double scale = std::min(double(areaW) / picture_.w, double(areaH) / picture_.h);
        const int w = static_cast<int>(picture_.w * scale), h = static_cast<int>(picture_.h * scale);
        const int x0 = kMargin + (areaW - w) / 2;
        for (int dy = 0; dy < h; ++dy) {
            const uint8_t* src = picture_.row(std::min(picture_.h - 1, static_cast<int>(dy / scale)));
            uint8_t* dst = s.row(y + dy) + x0 * 4;
            for (int dx = 0; dx < w; ++dx)
                std::copy_n(src + std::min(picture_.w - 1, static_cast<int>(dx / scale)) * 4, 4, dst + dx * 4);
        }
        y += h + 6;
    }
    progress(s, kMargin, y, areaW, decoded_ + 1, count);
}

} // namespace reluva::nfs
