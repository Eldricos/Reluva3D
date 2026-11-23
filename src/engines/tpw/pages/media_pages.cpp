// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's pages for the game's sounds and movies, and its files.

#include "common/events.h"
#include "common/fs.h"
#include "common/text.h"
#include "engines/tpw/pages/viewers.h"
#include "graphics/font.h"
#include "video/movie.h"

#include <algorithm>

namespace reluva::tpw {

namespace {

constexpr Color kBackground = {24, 28, 36};
constexpr Color kLabel = {230, 230, 230};
constexpr Color kDim = {150, 160, 175};
constexpr Color kPicked = {60, 80, 120};
constexpr Color kPlaying = {120, 220, 140};
constexpr Color kError = {255, 120, 110};
constexpr int kTop = 24;
constexpr int kLine = font::kLineHeight;

// The list's first row to show, keeping `picked` near the middle.
size_t firstRow(size_t picked, size_t count, size_t rows) {
    if (count <= rows || picked < rows / 2)
        return 0;
    return std::min(picked - rows / 2, count - rows);
}

} // namespace

SoundPage::SoundPage(ViewerContext &context) : Page(context) {
    _names = _context.namesEndingIn({".sdt"});
    if (!_names.empty())
        selected();
}

std::string SoundPage::current() const {
    if (_names.empty())
        return {};
    const auto &entries = _bank.entries();
    return _names[_index] + (_sound < entries.size() ? " " + entries[_sound].name : "");
}

void SoundPage::selected() {
    stop();
    _error.clear();
    _sound = 0;
    std::vector<uint8_t> data;
    _bank = {};
    if (Error err = _context.files.read(_names[_index], data))
        _error = err.message;
    else if (Error parseErr = _bank.parse(std::move(data)))
        _error = parseErr.message;
}

void SoundPage::play() {
    stop();
    _playError.clear();
    auto sound = std::make_shared<audio::Sound>();
    if (Error err = _bank.decode(_sound, *sound)) {
        _playError = err.message;
        return;
    }
    if (!_context.mixer) {
        _playError = "no audio device: not played";
        return;
    }
    _playingSound = sound;
    _playing = _context.mixer->play(sound, audio::SoundType::kEffects);
}

void SoundPage::stop() {
    if (_playing && _context.mixer)
        _context.mixer->stop(_playing);
    _playing = audio::kNoSound;
    _playingSound.reset();
}

void SoundPage::handleKey(uint32_t key) {
    const size_t n = _bank.entries().size();
    if (key == key::kSpace || key == key::kReturn) {
        if (_playing)
            stop();
        else
            play();
        return;
    }
    size_t next = _sound;
    if (key == key::kUp && _sound > 0)
        next = _sound - 1;
    else if (key == key::kDown && _sound + 1 < n)
        next = _sound + 1;
    else if (key == key::kPageUp)
        next = _sound >= 20 ? _sound - 20 : 0;
    else if (key == key::kPageDown && n > 0)
        next = std::min(n - 1, _sound + 20);
    else if (key == key::kLeft || key == key::kRight || key == key::kHome || key == key::kEnd) {
        Page::handleKey(key);
        return;
    }
    if (next != _sound) {
        stop();
        _playError.clear();
        _sound = next;
    }
}

bool SoundPage::tick(uint64_t nowMs) {
    if (!_playing)
        return false;
    if (_context.mixer && !_context.mixer->isPlaying(_playing)) {
        stop();
        return true;
    }
    if (nowMs - _lastDraw < 100)
        return false;
    _lastDraw = nowMs;
    return true;
}

void SoundPage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    const auto &entries = _bank.entries();
    drawLabel(screen, 4, 4,
              text::format("%zu/%zu  %s  %zu sounds", _index + 1, _names.size(),
                           _names.empty() ? "" : _names[_index].c_str(), entries.size()),
              kLabel);
    if (!_error.empty()) {
        drawLabel(screen, 4, kTop + 4, _error, kError);
        return;
    }
    const size_t rows = size_t((screen.h - kTop - 2 * kLine) / kLine);
    const size_t first = firstRow(_sound, entries.size(), rows);
    for (size_t i = first; i < entries.size() && i < first + rows; ++i) {
        const SoundBank::Entry &e = entries[i];
        const int y = kTop + int(i - first) * kLine;
        if (i == _sound)
            fillRect(screen, 0, y - 1, screen.w, kLine, kPicked);
        std::string line = text::format("%4zu  %-16s", i, e.name.c_str());
        if (!e.readable)
            line += "  (no header)";
        else if (e.mpeg)
            line += text::format("  MPEG %s %d Hz, %.2f s", e.channels == 2 ? "stereo" : "mono", e.rate,
                                 double(e.pcmBytes) / 2 / e.channels / std::max(1, e.rate));
        else
            line += text::format("  PCM %d Hz, %.2f s", e.rate, double(e.size) / 2 / std::max(1, e.rate));
        drawLabel(screen, 4, y, line, i == _sound && _playing ? kPlaying : e.readable ? kLabel : kDim);
    }
    std::string status = "Space: play";
    if (_playing && _context.mixer && _playingSound)
        status = text::format("playing %.1f / %.1f s (Space: stop)",
                              _context.mixer->position(_playing) / _playingSound->rate,
                              double(_playingSound->samples.size()) / _playingSound->channels / _playingSound->rate);
    if (!_playError.empty())
        status = _playError;
    drawLabel(screen, 4, screen.h - kLine - 2, status, _playError.empty() ? kDim : kError);
}

MoviePage::MoviePage(ViewerContext &context) : Page(context) {
    for (const std::string &name : _context.namesEndingIn({".tgq"}))
        if (const auto data = fs::findDirectory(_context.install, "Data"))
            if (const auto path = fs::findFile(*data, name))
                _movies.push_back(*path);
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(_context.install, ec))
        if (fs::toLower(entry.path().extension().string()) == ".avi")
            _movies.push_back(entry.path());
}

std::string MoviePage::current() const {
    return _movies.empty() ? std::string() : _movies[_index].lexically_relative(_context.install).generic_string();
}

void MoviePage::handleKey(uint32_t key) {
    if (key != key::kSpace && key != key::kReturn) {
        Page::handleKey(key);
        return;
    }
    // The logo's 320x480 pictures fill the screen, as the game shows them;
    // the training video plays at its own size in the middle.
    video::MovieOptions options;
    options.screenWidth = kViewerWidth;
    options.screenHeight = kViewerHeight;
    if (fs::toLower(_movies[_index].extension().string()) == ".tgq") {
        options.pictureWidth = kViewerWidth;
        options.pictureHeight = kViewerHeight;
    }
    bool quit = false;
    _error.clear();
    if (Error err = video::playMovie(_context.sys, _context.mixer, _movies[_index], quit, options))
        _error = err.message;
}

void MoviePage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    drawLabel(screen, 4, 4, text::format("%zu/%zu  %s", _index + 1, _movies.size(), current().c_str()), kLabel);
    for (size_t i = 0; i < _movies.size(); ++i) {
        const int y = kTop + int(i) * kLine;
        if (i == _index)
            fillRect(screen, 0, y - 1, screen.w, kLine, kPicked);
        drawLabel(screen, 4, y, _movies[i].lexically_relative(_context.install).generic_string(), kLabel);
    }
    drawLabel(screen, 4, screen.h - kLine - 2, _error.empty() ? "Space: play (a key or a click stops it)" : _error,
              _error.empty() ? kDim : kError);
}

FilePage::FilePage(ViewerContext &context) : Page(context) {
    _names = _context.files.list();
    if (!_names.empty())
        selected();
}

std::string FilePage::current() const {
    return _names.empty() ? std::string() : _names[_index];
}

void FilePage::selected() {
    _data.clear();
    // Where it is, from the game's folder: an archive, or Data for a loose file.
    const Archive *from = _context.files.find(_names[_index]);
    _where = from ? std::filesystem::path(from->describe()).lexically_relative(_context.install).generic_string() : "";
    if (Error err = _context.files.read(_names[_index], _data))
        _where = err.message;
}

void FilePage::handleKey(uint32_t key) {
    if (key == key::kUp)
        select(long(_index) - 1);
    else if (key == key::kDown)
        select(long(_index) + 1);
    else if (key == key::kPageUp)
        select(long(_index) - 20);
    else if (key == key::kPageDown)
        select(long(_index) + 20);
    else
        Page::handleKey(key);
}

void FilePage::draw(Surface &screen) {
    fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
    drawLabel(screen, 4, 4, text::format("%zu/%zu files", _index + 1, _names.size()), kLabel);
    // The list on the top half, the picked file's start below.
    const int split = screen.h / 2;
    const size_t rows = size_t((split - kTop) / kLine);
    const size_t first = firstRow(_index, _names.size(), rows);
    for (size_t i = first; i < _names.size() && i < first + rows; ++i) {
        const int y = kTop + int(i - first) * kLine;
        if (i == _index)
            fillRect(screen, 0, y - 1, screen.w, kLine, kPicked);
        drawLabel(screen, 4, y, _names[i], kLabel);
    }
    fillRect(screen, 0, split, screen.w, 1, kDim);
    drawLabel(screen, 4, split + 4, text::format("%zu bytes, in %s", _data.size(), _where.c_str()), kDim);
    // Text if its start is all printable, else bytes.
    const size_t probe = std::min<size_t>(_data.size(), 512);
    bool printable = probe > 0;
    for (size_t i = 0; i < probe && printable; ++i)
        printable = _data[i] >= 32 || _data[i] == '\n' || _data[i] == '\r' || _data[i] == '\t';
    int y = split + 4 + 2 * kLine;
    if (printable) {
        std::string all(_data.begin(), _data.begin() + long(std::min<size_t>(_data.size(), 4096)));
        for (std::string line : text::split(all, '\n')) {
            if (y + kLine > screen.h)
                break;
            line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
            std::replace(line.begin(), line.end(), '\t', ' ');
            drawLabel(screen, 4, y, line, kLabel);
            y += kLine;
        }
        return;
    }
    for (size_t at = 0; at < _data.size() && y + kLine <= screen.h; at += 16, y += kLine) {
        std::string line = text::format("%06zx ", at);
        std::string chars;
        for (size_t i = at; i < at + 16; ++i) {
            line += i < _data.size() ? text::format(" %02x", _data[i]) : "   ";
            chars += i < _data.size() ? char(_data[i] >= 32 && _data[i] < 127 ? _data[i] : '.') : ' ';
        }
        drawLabel(screen, 4, y, line + "  " + chars, kLabel);
    }
}

} // namespace reluva::tpw
