// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Which music and lines, and when, are the recording's:
// docs/engines/tpw/games/simcoaster/systems/front-end.md#the-music-and-the-speech.

#include "engines/tpw/ui/front_end_audio.h"

#include "engines/tpw/ui/front_end_overlay.h"

namespace reluva::tpw {

namespace {

constexpr const char *kSpeechFolder = "global/Speech";
constexpr const char *kSpeechCategory = "speech";
// How much music is kept queued ahead, in seconds: the longest piece and
// more, so a loop that doesn't call update() for a while (choosing a
// world) doesn't run it dry.
constexpr int kQueuedSeconds = 40;

} // namespace

Error FrontEndAudio::load(const SearchSet &files) {
    _files = &files;
    std::vector<uint8_t> effects;
    std::vector<uint8_t> banks;
    const std::string base = std::string(kMusicFolder) + "/cat_music";
    if (Error err = files.read(base + "SFX.map", effects))
        return err;
    if (Error err = files.read(base + "BANK.map", banks))
        return err;
    if (Error err = _music.parse(effects, banks))
        return Error::fail(base + ": " + err.message);
    if (_music.effects().empty() || _music.effects()[0].groups.empty() || _music.banks().empty())
        return Error::fail(base + ": not the front end's music");
    // The first piece's group by the value too: the music's groups all
    // have the same links.
    _group = follow(0);
    _musicBankFile = soundBankFile(kMusicFolder, _music.banks()[0]);
    if (Error err = _speech.load(files, kSpeechFolder, {kSpeechCategory}))
        return err;
    _speechLoaded = true;
    return Error::success();
}

uint16_t FrontEndAudio::roll() {
    _seed = _seed * 1103515245u + 12345u;
    return uint16_t(_seed >> 16);
}

void FrontEndAudio::setMixer(audio::Mixer *mixer) {
    _mixer = mixer;
}

size_t FrontEndAudio::follow(size_t group) const {
    const auto &groups = _music.effects()[0].groups;
    if (group >= groups.size())
        return group;
    for (const SoundMap::Link &link : groups[group].links)
        if (_value >= link.low && _value <= link.high && link.group < groups.size())
            return link.group;
    return group;
}

void FrontEndAudio::refill() {
    if (!_mixer || !_files || _music.effects().empty())
        return;
    if (!_musicBankRead) {
        _musicBankRead = true;
        std::vector<uint8_t> data;
        if (_files->read(_musicBankFile, data) || _musicBank.parse(std::move(data)))
            return;
    }
    const auto &groups = _music.effects()[0].groups;
    if (_group >= groups.size())
        return;
    // A piece that won't decode is passed over; a few tries, so a group of
    // nothing but those doesn't hold the loop.
    for (int tries = 0; tries < 8; ++tries) {
        if (_stream && _stream->queued() >= size_t(kQueuedSeconds * _stream->rate() * (_stream->stereo() ? 2 : 1)))
            return;
        if (_started)
            _group = follow(_group);
        const SoundMap::Sound *piece = SoundMap::pick(groups[_group], roll());
        audio::Sound sound;
        if (!piece || _musicBank.decode(piece->entry, sound) || sound.channels < 1 || sound.channels > 2)
            continue;
        // The recording's next piece starts as the map's length runs out,
        // which is a frame or so short of the decoded sound's.
        const size_t mapped = size_t(piece->millis) * size_t(sound.rate) / 1000 * size_t(sound.channels);
        if (mapped > 0 && mapped < sound.samples.size())
            sound.samples.resize(mapped);
        if (!_stream) {
            _stream = std::make_shared<audio::QueuingStream>(sound.rate, sound.channels == 2);
            _musicHandle = _mixer->play(_stream, audio::SoundType::kMusic);
        }
        // The stream's format is the first piece's; the music's are all
        // stereo at 22,050 Hz.
        if (sound.rate != _stream->rate() || (sound.channels == 2) != _stream->stereo())
            continue;
        _stream->queue(sound.samples);
        _piecesQueued.push_back(piece->entry);
        _started = true;
        tries = 0;
    }
}

void FrontEndAudio::update(uint32_t ms) {
    const auto &lines = FrontEndOverlay::advisorLines();
    while (!_speechOver && _nextLine < lines.size() && ms >= lines[_nextLine].says) {
        const FrontEndOverlay::AdvisorLine &line = lines[_nextLine++];
        // A line whose portrait has gone by (the path skipped) isn't said.
        if (ms >= line.shownTo)
            continue;
        if (_mixer && _line != audio::kNoSound)
            _mixer->stop(_line);
        _line =
            _mixer && _speechLoaded ? _speech.play(*_mixer, line.speech, audio::SoundType::kSpeech) : audio::kNoSound;
        _linesStarted.push_back(line.speech);
    }
    refill();
}

void FrontEndAudio::setMusicValue(int value, bool cut) {
    _value = value;
    if (!cut || _music.effects().empty() || follow(_group) == _group)
        return;
    _group = follow(_group);
    _started = false;
    if (_mixer && _musicHandle != audio::kNoSound)
        _mixer->stop(_musicHandle);
    _musicHandle = audio::kNoSound;
    _stream.reset();
    refill();
}

void FrontEndAudio::stopSpeech(bool forGood) {
    if (_mixer && _line != audio::kNoSound)
        _mixer->stop(_line);
    _line = audio::kNoSound;
    _speechOver = _speechOver || forGood;
}

void FrontEndAudio::stop() {
    stopSpeech(true);
    if (_mixer && _musicHandle != audio::kNoSound)
        _mixer->stop(_musicHandle);
    _musicHandle = audio::kNoSound;
    _stream.reset();
}

} // namespace reluva::tpw
