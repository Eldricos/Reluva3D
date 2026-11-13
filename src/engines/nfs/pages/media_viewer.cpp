// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Sounds, Music and Movies pages.

#include "engines/nfs/pages/viewers.h"

#include "engines/nfs/pages/page_draw.h"

#include "graphics/font.h"
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

const char *compressionName(int c) {
    switch (c) {
    case SoundHeader::kPcm16:
        return "PCM";
    case SoundHeader::kEaXa:
        return "EA-XA";
    case SoundHeader::kMicroTalk:
        return "MicroTalk";
    default:
        return "?";
    }
}

// A bar showing `done` of `total`.
void progress(Surface &s, int x, int y, int w, double done, double total) {
    fill(s, x, y, w, 6, kBarColor);
    if (total > 0)
        fill(s, x, y, static_cast<int>(w * std::clamp(done / total, 0.0, 1.0)), 6, kFillColor);
}

// A file's name without its extension, lower case: for finding the files
// that go with it.
std::string withoutExtension(const std::string &name) {
    const size_t dot = name.rfind('.');
    return fs::toLower(dot == std::string::npos ? name : name.substr(0, dot));
}

// The part of a path after its last '/'.
std::string baseName(const std::string &path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

const char *const kNoAudio = "No audio output: sounds can't be played";

} // namespace

// --- Sounds -------------------------------------------------------------------

SoundPage::SoundPage(const GameFiles &files, audio::Mixer *mixer) : _files(files), _mixer(mixer) {
    _banks = files.withExtensions({".bnk"});
}

void SoundPage::deactivate() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void SoundPage::load(int bank) {
    _bank = std::clamp(bank, 0, static_cast<int>(_banks.size()) - 1);
    _loaded = {};
    _sounds.clear();
    _error.clear();
    if (Error err = _files.read(_banks[_bank], _data))
        _error = err.message;
    else if (Error err2 = parseSoundBank(_data, _loaded))
        _error = err2.message;
    for (size_t i = 0; i < _loaded.slots.size(); ++i)
        if (_loaded.slots[i])
            _sounds.push_back(static_cast<int>(i));
    // A car's banks sit in its car.viv with its carp.txt: the engine's
    // revs, from idle ("engine minimum rpm") to "engine redline in rpm".
    _minRpm = _redline = _rpm = 0;
    const FileRef &bankFile = _banks[_bank];
    if (!bankFile.inner.empty())
        for (const FileRef &f : _files.files())
            if (f.file == bankFile.file && fs::toLower(f.inner) == "carp.txt") {
                std::vector<uint8_t> carp;
                if (_files.read(f, carp))
                    break;
                const std::string text(carp.begin(), carp.end());
                auto value = [&](const std::string &name) {
                    const size_t at = text.find(name);
                    const size_t line = at == std::string::npos ? at : text.find('\n', at);
                    return line == std::string::npos ? 0 : std::atoi(text.c_str() + line + 1);
                };
                _minRpm = value("engine minimum rpm");
                _redline = value("engine redline in rpm");
                if (_redline <= _minRpm)
                    _minRpm = _redline = 0;
                _rpm = _minRpm;
                break;
            }
    _sound = -1;
    select(0);
}

bool SoundPage::engine() const {
    return !_sounds.empty() && _loaded.slots[size_t(_sounds[size_t(_sound)])]->tags.count(0x0A) != 0;
}

// The key a sound is played at: 60 for a car's sounds, as the game plays
// its engine; elsewhere the sound's own root key (0x07), the key a caller
// would play isn't known.
static int keyFor(const SoundHeader &h, bool car) {
    auto root = h.tags.find(0x07);
    return car || root == h.tags.end() ? 60 : static_cast<int>(root->second);
}

double SoundPage::rate() const {
    if (!engine())
        return std::pow(2.0, _semitones / 12.0);
    const SoundHeader &h = *_loaded.slots[size_t(_sounds[size_t(_sound)])];
    const int bend = _redline ? engineBend(_rpm, _redline) : _bend;
    return std::pow(2.0, patchCents(h, keyFor(h, _redline != 0), bend) / 1200.0);
}

std::string SoundPage::engineText() const {
    const SoundHeader &h = *_loaded.slots[size_t(_sounds[size_t(_sound)])];
    const int bend = _redline ? engineBend(_rpm, _redline) : _bend;
    std::string out =
        _redline ? "   " + std::to_string(_rpm) + " rpm (redline " + std::to_string(_redline) + "), bend " : "   bend ";
    return out + std::to_string(bend) + ", " + std::to_string(patchCents(h, keyFor(h, _redline != 0), bend)) + " cents";
}

void SoundPage::select(int sound) {
    _decoded.reset();
    _soundError.clear();
    if (_sounds.empty()) {
        _sound = 0;
        return;
    }
    _sound = std::clamp(sound, 0, static_cast<int>(_sounds.size()) - 1);
    auto decoded = std::make_shared<audio::Sound>();
    if (Error err = decodeBankSound(_data, *_loaded.slots[_sounds[_sound]], *decoded))
        _soundError = err.message;
    else
        _decoded = std::move(decoded);
    if (_autoPlay)
        play();
}

// A sound with a loop (engines, ambiences) plays it over and over until
// stopped, at the pitch chosen.
void SoundPage::play() {
    deactivate();
    if (_mixer && _decoded) {
        _playing = _mixer->play(_decoded, audio::SoundType::kEffects, audio::kMaxVolume, 0, _decoded->loopEnd != 0);
        _mixer->setRate(_playing, rate());
    }
}

void SoundPage::handleKey(uint32_t k, uint16_t) {
    if (_bank < 0)
        load(0);
    const int count = static_cast<int>(_sounds.size());
    switch (k) {
    case key::kUp:
        select(_sound - 1);
        break;
    case key::kDown:
        select(_sound + 1);
        break;
    case key::kHome:
        select(0);
        break;
    case key::kEnd:
        select(count - 1);
        break;
    case key::kPageUp:
        load(_bank - 1);
        break;
    case key::kPageDown:
        load(_bank + 1);
        break;
    case key::kSpace:
    case key::kReturn:
        play();
        break;
    case 's':
        deactivate();
        break;
    case 'p':
        _autoPlay = !_autoPlay;
        break;
    case key::kLeft:
    case key::kRight:
        // An engine's revs (250 at a time) or bend (4); else the pitch, a
        // semitone at a time.
        if (engine() && _redline)
            _rpm = std::clamp(_rpm + (k == key::kRight ? 250 : -250), _minRpm, _redline);
        else if (engine())
            _bend = std::clamp(_bend + (k == key::kRight ? 4 : -4), 0, 127);
        else
            _semitones = std::clamp(_semitones + (k == key::kRight ? 1 : -1), -24, 24);
        if (_mixer && _playing != audio::kNoSound)
            _mixer->setRate(_playing, rate());
        break;
    default:
        break;
    }
}

bool SoundPage::tick(uint64_t) {
    // Redrawn while playing, for the position, and once when it ends.
    if (!_mixer || _playing == audio::kNoSound)
        return false;
    if (!_mixer->isPlaying(_playing))
        _playing = audio::kNoSound;
    return true;
}

void SoundPage::draw(Surface &s) {
    if (_bank < 0)
        load(0);
    frame(s, "Sounds  " + std::to_string(_bank + 1) + "/" + std::to_string(_banks.size()) + "  " + _banks[_bank].name,
          "Up/Down: sound  PgUp/PgDn: bank  Space: play  S: stop  P: autoplay  Left/Right: pitch");
    int y = 8 + font::kLineHeight * 2;
    if (!_error.empty()) {
        text(s, kMargin, y, _error, kWarnColor);
        return;
    }
    text(s, kMargin, y,
         "Version " + std::to_string(_loaded.version) + ", " + std::to_string(_loaded.slots.size()) + " slots, " +
             std::to_string(_sounds.size()) + " sounds" + (_autoPlay ? "   (plays when selected)" : "") +
             (engine() ? engineText()
              : _semitones
                  ? "   pitch " + std::string(_semitones > 0 ? "+" : "") + std::to_string(_semitones) + " semitones"
                  : std::string()),
         kLabelColor);
    y += font::kLineHeight;
    if (!_mixer) {
        text(s, kMargin, y, kNoAudio, kWarnColor);
    }
    y += font::kLineHeight + 4;
    text(s, kMargin, y, "Slot  Codec      Ch  Rate    Samples   Length  Loop", kLabelColor);
    y += font::kLineHeight;

    // The list, scrolled to keep the selection in view; a waveform below.
    constexpr int kWaveH = 100;
    const int rows = std::max(1, (s.h - y - kWaveH - font::kLineHeight * 3) / font::kLineHeight);
    const int count = static_cast<int>(_sounds.size());
    const int top = std::clamp(_sound - rows / 2, 0, std::max(0, count - rows));
    for (int i = top; i < std::min(count, top + rows); ++i, y += font::kLineHeight) {
        const SoundHeader &h = *_loaded.slots[_sounds[i]];
        char line[160];
        std::snprintf(line, sizeof line, "%4d  %-9s  %2d  %5d  %8u  %7s  %s", _sounds[i],
                      compressionName(h.compression), h.channels, h.rate, h.samples,
                      seconds(double(h.samples) / h.rate).c_str(),
                      h.loopEnd ? (std::to_string(h.loopStart) + "-" + std::to_string(h.loopEnd)).c_str() : "");
        const bool selected = i == _sound;
        if (selected)
            fill(s, kMargin - 4, y - 1, s.w - 2 * kMargin + 8, font::kLineHeight, kBarColor);
        text(s, kMargin, y, line, selected ? kTitleColor : kTextColor);
    }

    const int waveY = s.h - kWaveH - font::kLineHeight * 2 - 4;
    const int waveW = s.w - 2 * kMargin;
    if (!_soundError.empty()) {
        text(s, kMargin, waveY, _soundError, kWarnColor);
        return;
    }
    if (!_decoded || _decoded->frames() == 0)
        return;
    // Each column: the first channel's lowest and highest sample there.
    const audio::Sound &snd = *_decoded;
    const size_t frames = snd.frames();
    fill(s, kMargin, waveY + kWaveH / 2, waveW, 1, kBarColor);
    for (int x = 0; x < waveW; ++x) {
        const size_t a = frames * x / waveW;
        const size_t b = std::max(a + 1, frames * (x + 1) / waveW);
        int lo = 0;
        int hi = 0;
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
    if (_mixer && _playing != audio::kNoSound) {
        const int at = static_cast<int>(waveW * std::min(1.0, _mixer->position(_playing) / double(frames)));
        fill(s, kMargin + at, waveY, 1, kWaveH, kFillColor);
    }
}

// --- Music --------------------------------------------------------------------

MusicPage::MusicPage(const GameFiles &files, audio::Mixer *mixer) : _files(files), _mixer(mixer) {
    _streams = files.withExtensions({".mus"});
}

void MusicPage::deactivate() {
    stop();
}

void MusicPage::stop() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
    _current.reset();
    _queue.reset();
    _queuedSection = -1;
}

std::shared_ptr<audio::Sound> MusicPage::decodeSection(int section) {
    const MusicMap *m = activeMap();
    const int segment = m ? _loaded.segmentAt(m->offsets[section]) : section;
    auto sound = std::make_shared<audio::Sound>();
    if (Error err = _loaded.decode(segment, *sound)) {
        _error = err.message;
        return nullptr;
    }
    return sound;
}

void MusicPage::load(int stream) {
    stop();
    _stream = std::clamp(stream, 0, static_cast<int>(_streams.size()) - 1);
    _loaded = {};
    _map = _linear = {};
    _haveMap = _haveLinear = false;
    _error.clear();
    _mapError.clear();
    _section = -1;
    _history.clear();
    std::vector<uint8_t> data;
    if (Error err = _files.read(_streams[_stream], data)) {
        _error = err.message;
        return;
    }
    if (Error err = _loaded.open(std::move(data))) {
        _error = err.message;
        return;
    }
    // The maps beside it, by name; each must name this stream's segments.
    const std::string base = withoutExtension(_streams[_stream].name);
    for (const FileRef &ref : _files.withExtensions({".map", ".lin"})) {
        if (withoutExtension(ref.name) != base)
            continue;
        const bool linear = fs::toLower(ref.name).ends_with(".lin");
        MusicMap &m = linear ? _linear : _map;
        Error err = _files.read(ref, data);
        if (!err)
            err = parseMusicMap(data, m);
        for (size_t i = 0; !err && i < m.offsets.size(); ++i)
            if (_loaded.segmentAt(m.offsets[i]) < 0)
                err = Error::fail("a section names no segment");
        if (err)
            _mapError = baseName(ref.name) + ": " + err.message;
        else
            (linear ? _haveLinear : _haveMap) = true;
    }
    _useLinear = !_haveMap && _haveLinear;
}

const MusicMap *MusicPage::activeMap() const {
    if (_useLinear ? _haveLinear : _haveMap)
        return _useLinear ? &_linear : &_map;
    return nullptr;
}

// Where the music starts: the map's start section (a race's intro), or
// the first.
int MusicPage::firstSection() const {
    if (const MusicMap *m = activeMap())
        if (m->start >= 0 && m->start < static_cast<int>(m->sections.size()))
            return m->start;
    return 0;
}

int MusicPage::following(int section) {
    if (const MusicMap *m = activeMap()) {
        // An event pending goes first, to its section for this part
        // (nfs3.exe 0x4e6be3); else the route for the intensity.
        if (_pendingEvent >= 0) {
            const int to = m->event(section, _pendingEvent);
            _pendingEvent = -1;
            if (to >= 0)
                return to;
        }
        return m->next(section, _intensity);
    }
    // No map: the segments in order.
    return section + 1 < static_cast<int>(_loaded.segments().size()) ? section + 1 : -1;
}

void MusicPage::start(int section) {
    stop();
    _section = section;
    if (section < 0)
        return;
    _history.push_back(section);
    if (_history.size() > 64)
        _history.erase(_history.begin());
    _current = decodeSection(section);
    if (!_current || !_mixer)
        return;
    // Played as a stream of segments, the next queued behind it.
    _queue = std::make_shared<audio::SoundQueue>();
    _queue->push(_current, section);
    _queuedSection = section;
    if (const int next = following(section); next >= 0)
        if (auto sound = decodeSection(next)) {
            _queue->push(sound, next);
            _queuedSection = next;
        }
    _playing = _mixer->playOutput(_queue, audio::SoundType::kMusic);
}

void MusicPage::handleKey(uint32_t k, uint16_t) {
    if (_stream < 0)
        load(0);
    switch (k) {
    case key::kUp:
        load(_stream - 1);
        break;
    case key::kDown:
        load(_stream + 1);
        break;
    case key::kLeft:
        _intensity = std::max(0, _intensity - 10);
        break;
    case key::kRight:
        _intensity = std::min(100, _intensity + 10);
        break;
    case key::kSpace:
        if (_playing != audio::kNoSound || (!_mixer && _section >= 0))
            stop(), _section = -1;
        else
            start(firstSection());
        break;
    case key::kHome:
        start(firstSection());
        break;
    case 'n':
        start(_section < 0 ? firstSection() : following(_section));
        break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        _pendingEvent = static_cast<int>(k - '0');
        break;
    case 'l':
        if (_haveMap && _haveLinear) {
            _useLinear = !_useLinear;
            stop();
            _section = -1;
            _history.clear();
        }
        break;
    default:
        break;
    }
}

bool MusicPage::tick(uint64_t nowMs) {
    if (!_mixer || _playing == audio::kNoSound)
        return false;
    // When the queue moves on to the next segment: that one is playing,
    // and the one after it is queued (decided now, by the intensity).
    if (_queue) {
        const int now = _queue->playing();
        if (now < 0) {
            stop();
            _section = -1;
            return true;
        }
        if (now != _section) {
            _section = now;
            _history.push_back(now);
            if (_history.size() > 64)
                _history.erase(_history.begin());
        }
        if (_queue->queued() < 2 && _queuedSection >= 0) {
            const int next = following(_queuedSection);
            _queuedSection = next;
            if (next >= 0)
                if (auto sound = decodeSection(next))
                    _queue->push(sound, next);
            return true;
        }
    }
    if (nowMs - _lastDraw < 100)
        return false;
    _lastDraw = nowMs;
    return true;
}

void MusicPage::draw(Surface &s) {
    if (_stream < 0)
        load(0);
    frame(s,
          "Music  " + std::to_string(_stream + 1) + "/" + std::to_string(_streams.size()) + "  " +
              _streams[_stream].name,
          "Up/Down: stream  Space: play  Left/Right: intensity  N: next  0-9: event");
    int y = 8 + font::kLineHeight * 2;
    if (!_error.empty()) {
        text(s, kMargin, y, _error, kWarnColor);
        return;
    }
    const MusicMap *m = activeMap();
    std::string order = m ? std::string(_useLinear ? "linear order (.lin)" : "Pathfinder map (.map)") + ", " +
                                std::to_string(m->sections.size()) + " sections, " + std::to_string(m->events.size()) +
                                " events"
                          : "no map: segments in order";
    if (m && firstSection() != 0)
        order += ", starting at section " + std::to_string(firstSection());
    if (_haveMap && _haveLinear)
        order += "   L: the other order";
    text(s, kMargin, y, std::to_string(_loaded.segments().size()) + " segments; " + order, kTextColor);
    y += font::kLineHeight;
    if (!_mapError.empty()) {
        text(s, kMargin, y, _mapError, kWarnColor);
        y += font::kLineHeight;
    }
    if (!_mixer) {
        text(s, kMargin, y, kNoAudio, kWarnColor);
        y += font::kLineHeight;
    }
    y += font::kLineHeight;

    text(s, kMargin, y, "Intensity " + std::to_string(_intensity), kLabelColor);
    progress(s, kMargin + 15 * font::kAdvance, y + 4, 200, _intensity, 100);
    y += font::kLineHeight * 2;

    if (_section < 0) {
        text(s, kMargin, y, "Stopped. Space plays from the start.", kLabelColor);
        return;
    }
    const int segment = m ? _loaded.segmentAt(m->offsets[_section]) : _section;
    const auto &header = _loaded.segments()[segment].header;
    const double length = double(header.samples) / header.rate;
    const double at = _mixer && _playing != audio::kNoSound ? _mixer->position(_playing) / header.rate : 0;
    text(s, kMargin, y,
         "Section " + std::to_string(_section) + " (segment " + std::to_string(segment) + ", " +
             compressionName(header.compression) + ")   " + seconds(at) + " / " + seconds(length),
         kTitleColor);
    y += font::kLineHeight;
    progress(s, kMargin, y + 2, s.w - 2 * kMargin, at, length);
    y += font::kLineHeight + 4;

    if (m) {
        const MusicMap::Section &sec = m->sections[_section];
        text(s, kMargin, y, "Routes from here (part " + std::to_string(sec.part) + "):", kLabelColor);
        y += font::kLineHeight;
        if (sec.routes.empty())
            text(s, kMargin + 2 * font::kAdvance, y, "none: the music ends here", kTextColor), y += font::kLineHeight;
        for (const auto &r : sec.routes) {
            const bool taken = _intensity >= r.low && _intensity <= r.high;
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
            if (const int to = m->event(_section, e); to >= 0 && (e == 0 || to != 0))
                events += "  " + std::to_string(e) + ">" + std::to_string(to);
        if (_pendingEvent >= 0)
            events += "   pending " + std::to_string(_pendingEvent);
        text(s, kMargin, y, events, _pendingEvent >= 0 ? kTitleColor : kTextColor);
        y += font::kLineHeight;
    } else {
        const int next = following(_section);
        text(s, kMargin, y, next < 0 ? "Last segment" : "Next: segment " + std::to_string(next), kLabelColor);
        y += font::kLineHeight;
    }
    y += font::kLineHeight;
    // The sections played, latest last, as many as fit.
    text(s, kMargin, y, "Played:", kLabelColor);
    y += font::kLineHeight;
    std::string line;
    for (size_t i = 0; i < _history.size(); ++i) {
        const std::string item = (i ? " > " : "") + std::to_string(_history[i]);
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

MoviePage::MoviePage(const GameFiles &files, audio::Mixer *mixer) : _files(files), _mixer(mixer) {
    _movies = files.withExtensions({".mad"});
}

double MoviePage::frameMs() const {
    const double ms = _loaded.frameDuration() / 65536.0;
    return ms > 1 ? ms : 1000.0 / 15;
}

void MoviePage::stopSound() {
    if (_mixer && _playing != audio::kNoSound)
        _mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void MoviePage::activate(System &sys) {
    _nowMs = sys.getMillis();
    if (_movie >= 0 && !_paused)
        seek(_decoded, _nowMs);
}

void MoviePage::deactivate() {
    stopSound();
    _paused = true;
}

void MoviePage::load(int movie) {
    stopSound();
    _movie = std::clamp(movie, 0, static_cast<int>(_movies.size()) - 1);
    _loaded = {};
    _audio.reset();
    _error.clear();
    _decoder = {};
    _decoder.setArithmetic(_display ? MadDecoder::Arithmetic::kGame : MadDecoder::Arithmetic::kReference);
    _decoded = -1;
    _picture = {};
    std::vector<uint8_t> data;
    if (Error err = _files.read(_movies[_movie], data)) {
        _error = err.message;
        return;
    }
    if (Error err = _loaded.open(std::move(data))) {
        _error = err.message;
        return;
    }
    if (_loaded.hasAudio()) {
        auto sound = std::make_shared<audio::Sound>();
        if (!_loaded.decodeAudio(*sound))
            _audio = std::move(sound);
    }
    seek(0, _nowMs);
}

void MoviePage::showFrame(int frame) {
    const int last = static_cast<int>(_loaded.frames().size()) - 1;
    frame = std::clamp(frame, 0, last);
    if (frame == _decoded && !_picture.pixels.empty())
        return;
    // Frames refer to the one before, back to a key frame: from there, or
    // on from the frame held.
    int from = frame;
    while (from > 0 && _loaded.frames()[from].kind != 'k')
        --from;
    if (_decoded >= from && _decoded < frame)
        from = _decoded + 1;
    for (int i = from; i <= frame; ++i)
        if (Error err = _loaded.decodeFrame(_decoder, i)) {
            _error = "frame " + std::to_string(i) + ": " + err.message;
            _decoded = -1;
            return;
        }
    _decoded = frame;
    if (_display)
        _decoder.toGameSurface(_picture, static_cast<MadDecoder::Display>(_display));
    else
        _decoder.toSurface(_picture);
}

void MoviePage::seek(int frame, uint64_t nowMs) {
    if (!_error.empty() || _loaded.frames().empty())
        return;
    stopSound();
    showFrame(frame);
    _startFrame = std::max(_decoded, 0);
    _startMs = nowMs;
    if (_paused || !_mixer || !_audio)
        return;
    // The sound from this frame on.
    auto rest = std::make_shared<audio::Sound>(*_audio);
    const size_t skip = std::min(rest->frames(), static_cast<size_t>(_startFrame * frameMs() * _audio->rate / 1000));
    rest->samples.erase(rest->samples.begin(), rest->samples.begin() + skip * rest->channels);
    _playing = _mixer->play(rest, audio::SoundType::kMusic);
}

void MoviePage::handleKey(uint32_t k, uint16_t) {
    if (_movie < 0)
        load(0);
    const int step = static_cast<int>(std::lround(5000 / frameMs()));
    switch (k) {
    case key::kUp:
        load(_movie - 1);
        break;
    case key::kDown:
        load(_movie + 1);
        break;
    case key::kHome:
        seek(0, _nowMs);
        break;
    case key::kLeft:
        seek(_decoded - step, _nowMs);
        break;
    case key::kRight:
        seek(_decoded + step, _nowMs);
        break;
    case key::kSpace:
        _paused = !_paused;
        if (_paused)
            stopSound();
        else
            seek(_decoded + 1 >= static_cast<int>(_loaded.frames().size()) ? 0 : _decoded, _nowMs);
        break;
    case key::kPageUp:
        _paused = true;
        stopSound();
        showFrame(_decoded - 1);
        break;
    case key::kPageDown:
        _paused = true;
        stopSound();
        showFrame(_decoded + 1);
        break;
    case 'g': {
        // 3, 2, 1, then Reluva's own; the decoder starts again from the
        // first frame with the arithmetic to match.
        _display = (_display + 3) % 4;
        const int frame = _decoded;
        _decoded = -1;
        _decoder = {};
        _decoder.setArithmetic(_display ? MadDecoder::Arithmetic::kGame : MadDecoder::Arithmetic::kReference);
        showFrame(std::max(frame, 0));
        break;
    }
    default:
        break;
    }
}

bool MoviePage::tick(uint64_t nowMs) {
    _nowMs = nowMs;
    if (_movie < 0 || _paused || !_error.empty() || _loaded.frames().empty())
        return false;
    // The sound is the clock while it plays; without it, the time.
    double elapsedMs;
    if (_playing != audio::kNoSound) {
        if (!_mixer->isPlaying(_playing)) {
            _playing = audio::kNoSound;
            elapsedMs = 1e12; // the sound ended: so has the movie
        } else {
            elapsedMs = _mixer->position(_playing) * 1000.0 / _audio->rate;
        }
    } else {
        elapsedMs = double(nowMs - _startMs);
    }
    const int last = static_cast<int>(_loaded.frames().size()) - 1;
    const int target = _startFrame + static_cast<int>(elapsedMs / frameMs());
    if (target >= last) {
        showFrame(last);
        _paused = true;
        stopSound();
        return true;
    }
    if (target == _decoded)
        return false;
    showFrame(target);
    return true;
}

void MoviePage::draw(Surface &s) {
    if (_movie < 0)
        load(0);
    frame(s,
          "Movies  " + std::to_string(_movie + 1) + "/" + std::to_string(_movies.size()) + "  " + _movies[_movie].name,
          "Up/Down: movie  Space: pause  Left/Right: 5 s  PgUp/PgDn: frame  G: display");
    int y = 8 + font::kLineHeight * 2;
    if (!_error.empty()) {
        text(s, kMargin, y, _error, kWarnColor);
        return;
    }
    const int count = static_cast<int>(_loaded.frames().size());
    const double total = count * frameMs() / 1000;
    char info[160];
    static constexpr const char *kDisplays[] = {"Reluva's conversion", "game mode 1", "game mode 2", "game mode 3"};
    std::snprintf(info, sizeof info, "%dx%d  %.0f fps  frame %d/%d (%s)  %s / %s  %s%s  %s", _loaded.width(),
                  _loaded.height(), 1000 / frameMs(), _decoded + 1, count,
                  _decoded >= 0 && _loaded.frames()[_decoded].kind == 'k' ? "key" : "inter",
                  seconds(std::max(_decoded, 0) * frameMs() / 1000).c_str(), seconds(total).c_str(),
                  _paused ? "paused" : "playing", _mixer ? (_audio ? "" : "  (no sound)") : "  (no audio output)",
                  kDisplays[_display]);
    text(s, kMargin, y, info, kTextColor);
    y += font::kLineHeight + 4;

    // The picture, as large as fits (nearest pixel), then the position.
    const int areaW = s.w - 2 * kMargin;
    const int areaH = s.h - y - font::kLineHeight * 2 - 12;
    if (_picture.w > 0 && _picture.h > 0) {
        const double scale = std::min(double(areaW) / _picture.w, double(areaH) / _picture.h);
        const int w = static_cast<int>(_picture.w * scale);
        const int h = static_cast<int>(_picture.h * scale);
        const int x0 = kMargin + (areaW - w) / 2;
        for (int dy = 0; dy < h; ++dy) {
            const uint8_t *src = _picture.row(std::min(_picture.h - 1, static_cast<int>(dy / scale)));
            uint8_t *dst = s.row(y + dy) + x0 * 4;
            for (int dx = 0; dx < w; ++dx)
                std::copy_n(src + std::min(_picture.w - 1, static_cast<int>(dx / scale)) * 4, 4, dst + dx * 4);
        }
        y += h + 6;
    }
    progress(s, kMargin, y, areaW, _decoded + 1, count);
}

} // namespace reluva::nfs
