// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/pages/resource_viewer.h"

#include "common/bytes.h"
#include "common/log.h"
#include "common/text.h"
#include "engines/stgenerations/audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::stgenerations {

namespace {

const int kNumStars = 400;
const double kPi = 3.14159265358979323846;
const char *const kHelp =
    "Up/Down, Enter: missions   Left/Right: images   PgUp/PgDn: archives   Tab: levels   Esc: quit";

} // namespace

ResourceViewer::ResourceViewer(Platform *system, Options options) : Generations(system, std::move(options)) {
}

Error ResourceViewer::start() {
    log::debug(1, "Controls: Left/Right = image, PgUp/PgDn = archive, Up/Down = sound, Space = replay sound,");
    log::debug(1,
               "          Home = starfield, click = test tone, F5/F7 = save/load, Alt+Enter = fullscreen, Esc = quit");
    log::debug(
        1,
        "          On a level: [ ] = floor plan, 3D view, textures; in 3D, arrows/WASD move, PgUp/PgDn look, L lighting, C collision");
    _status = kHelp;
    for (int i = 0; i < kNumStars; i++)
        _stars.push_back({(int32_t)_random.getRandomNumberRng(0, 2000) - 1000,
                          (int32_t)_random.getRandomNumberRng(0, 2000) - 1000,
                          (int32_t)_random.getRandomNumberRng(1, 1000)});
    return Error::success();
}

void ResourceViewer::keyDown(const KeyState &kbd) {
    switch (kbd.keycode) {
    case reluva::key::kEscape:
        // Out of a mission back to the list, and from anywhere else, out
        // of the viewer.
        if (_playingMission) {
            leaveMission(text::format("Left %s", _missions[_missionChoice].name.c_str()));
            break;
        }
        quitGame();
        break;
    case reluva::key::kReturn:
        if (isOnStarfield() && !_missions.empty())
            playMission(_missionChoice);
        break;
    case reluva::key::kRight:
        showImage(_imageIndex + 1);
        break;
    case reluva::key::kLeft:
        showImage(_imageIndex - 1);
        break;
    case reluva::key::kPageDown:
        jumpArchive(1);
        break;
    case reluva::key::kPageUp:
        jumpArchive(-1);
        break;
    case reluva::key::kTab:
        jumpLevel((kbd.flags & kKbdShift) ? -1 : 1);
        break;
    case reluva::key::kDown:
    case reluva::key::kUp: {
        int step = kbd.keycode == reluva::key::kDown ? 1 : -1;
        // On the starfield they pick a mission; elsewhere, a sound.
        if (isOnStarfield() && !_missions.empty()) {
            int count = (int)_missions.size();
            _missionChoice = ((_missionChoice + step) % count + count) % count;
            _dirty = true;
        } else {
            playSound(_soundIndex + step);
        }
        break;
    }
    case reluva::key::kSpace:
        playSound(std::max(_soundIndex, 0));
        break;
    case reluva::key::kHome:
        clearView();
        break;
    case (reluva::key::kF1 + 7): {
        // Play the movies in turn.
        static const char *const kMovies[] = {"MOVIE/MOV_01.AVI", "MOVIE/MOV_02.AVI"};
        playMovie(kMovies[_movieChoice++ % 2]);
        _dirty = true;
        break;
    }
    case ']':
        showLevelPage(_levelPage + 1);
        break;
    case '[':
        showLevelPage(_levelPage - 1);
        break;
    case (reluva::key::kF1 + 4): {
        Error err = saveGameState(0, "Quick save");
        if (err)
            log::warning("Save failed: %s", err.message.c_str());
        break;
    }
    case (reluva::key::kF1 + 6): {
        Error err = loadGameState(0);
        if (err)
            log::warning("Load failed: %s", err.message.c_str());
        break;
    }
    default:
        break;
    }
}

void ResourceViewer::click(int x, int /*y*/) {
    // Pitch rises from left to right across the screen.
    playBeep(220 + x * 2);
}

bool ResourceViewer::drawPage(uint32_t delta) {
    if (_level) {
        if (!_dirty) {
            _system->updateScreen();
            return false;
        }
        uint8_t background = _screen->getPalette().findBestColor(0, 0, 0);
        _screen->clear(background);
        if (_levelPage == kLevelPageMap) {
            Canvas map;
            map.create(Screen::kWidth, Screen::kHeight - kTextPanelHeight, Canvas::kClut8);
            map.fillRect(Rect(0, 0, map.w, map.h), background);
            drawLevelMap(*_level, map);
            _screen->drawImage(map, Screen::kHeight - kTextPanelHeight);
        } else {
            LevelFile::Texture texture;
            if (_level->loadTexture(_levelTextures[_levelPage - kLevelPageTextures], texture))
                _screen->drawImage(texture.surface, Screen::kHeight - kTextPanelHeight);
        }
    } else if (_spritesLoaded) {
        // Animate at about 8 frames per second, anchored bottom centre.
        _overlayTime += delta;
        if (_overlayTime >= 125) {
            _overlayTime = 0;
            _overlayFrame = (_overlayFrame + 1) % _sprites.getFrameCount();
        }
        const SpriteSet::Frame &f = _sprites.getFrame(_overlayFrame);
        Canvas frame;
        frame.create(_sprites.getMaxWidth(), _sprites.getMaxHeight(), Canvas::kClut8);
        uint8_t background = _screen->getPalette().findBestColor(0, 0, 0);
        frame.fillRect(Rect(0, 0, frame.w, frame.h), background);
        frame.transBlitFrom(f.surface, (frame.w - f.surface.w) / 2, frame.h - f.surface.h, SpriteSet::kTransparent);
        _screen->clear(background);
        _screen->drawImage(frame, Screen::kHeight - kTextPanelHeight);
    } else if (_overlayLoaded) {
        // Animate at about 12 frames per second.
        _overlayTime += delta;
        if (_overlayTime >= 80) {
            _overlayTime = 0;
            _overlayFrame = (_overlayFrame + 1) % (_overlay.getRowCount() / _overlayFrameHeight);
        }
        Canvas frame;
        frame.create(_overlay.getWidth(), _overlayFrameHeight, Canvas::kClut8);
        uint8_t background = _screen->getPalette().findBestColor(0, 0, 0);
        frame.fillRect(Rect(0, 0, frame.w, frame.h), background);
        _overlay.drawFrame(frame, _overlayFrame, _overlayFrameHeight, 0, 0);
        _screen->clear(background);
        _screen->drawImage(frame, Screen::kHeight - kTextPanelHeight);
    } else if (_imageLoaded) {
        // Static image: only redraw when something changed.
        if (!_dirty) {
            _system->updateScreen();
            return false;
        }
        _screen->clear(_screen->getPalette().findBestColor(0, 0, 0));
        _screen->drawImage(_image.getSurface(), Screen::kHeight - kTextPanelHeight);
    } else {
        _screen->clear(kColorBlack);
        drawStarfield(delta);
        Canvas &s = _screen->getSurface();
        s.frameRect(Rect(8, 8, Screen::kWidth - 8, Screen::kHeight - kTextPanelHeight - 8), kColorFrame);
        drawMissionList();
    }
    return true;
}

void ResourceViewer::missionOver() {
    std::string message = text::format("%s %s", _missions[_missionChoice].name.c_str(),
                                       _mission->isAccomplished() ? "accomplished" : "ended, objectives not met");
    // Codes left for the campaign (SET_MISSION_OUTCOME).
    for (int32_t code : _mission->getOutcomes())
        message += text::format(" [%d]", code);
    leaveMission(message);
}

void ResourceViewer::clearView() {
    _imageLoaded = false;
    _overlayLoaded = false;
    _spritesLoaded = false;
    Generations::clearView();
    _imageIndex = -1;
    _status = kHelp;
}

Error ResourceViewer::restorePlace(int imageIndex, int soundIndex) {
    _soundIndex = soundIndex;
    if (imageIndex >= 0)
        showImage(imageIndex);
    return Error::success();
}

bool ResourceViewer::isOnStarfield() const {
    return !_level && !_imageLoaded && !_overlayLoaded && !_spritesLoaded;
}

void ResourceViewer::drawMissionList() {
    if (!_font.isLoaded() || _missions.empty())
        return;
    const Palette &pal = _screen->getPalette();
    uint8_t remap[256];
    for (int i = 0; i < 256; i++) {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        if (_hasFontPalette)
            _fontPalette.getColor(i, r, g, b);
        else
            r = g = b = (uint8_t)i;
        remap[i] = pal.findBestColor(r, g, b);
    }
    Canvas &s = _screen->getSurface();
    int x = 40;
    int y = 32;
    _font.drawString(s, "Away missions (Up/Down, Enter):", x, y, remap);
    y += _font.getHeight() * 3 / 2;
    for (int i = 0; i < (int)_missions.size(); i++) {
        if (i == _missionChoice)
            s.frameRect(Rect(x - 4, y - 1, x + 160, y + _font.getHeight() + 1), kColorFrame);
        _font.drawString(s, _missions[i].name, x, y, remap);
        y += _font.getHeight() + 2;
    }
    if (!_missionMessage.empty())
        _font.drawString(s, _missionMessage, x, y + _font.getHeight(), remap);
}

void ResourceViewer::drawStarfield(uint32_t delta) {
    Canvas &s = _screen->getSurface();
    const int cx = Screen::kWidth / 2;
    const int cy = Screen::kHeight / 2;
    int32_t speed = (int32_t)std::max<uint32_t>(1, delta / 2);

    for (Star &star : _stars) {
        star.z -= speed;
        if (star.z <= 1) {
            star.x = (int32_t)_random.getRandomNumberRng(0, 2000) - 1000;
            star.y = (int32_t)_random.getRandomNumberRng(0, 2000) - 1000;
            star.z = 1000;
        }
        int sx = cx + star.x * 256 / star.z;
        int sy = cy + star.y * 256 / star.z;
        uint8_t color = (uint8_t)(kColorStarFirst + (kColorStarLast - kColorStarFirst) * (1000 - star.z) / 1000);
        s.setPixel(sx, sy, color);
        if (star.z < 300) {
            s.setPixel(sx + 1, sy, color);
            s.setPixel(sx, sy + 1, color);
            s.setPixel(sx + 1, sy + 1, color);
        }
    }
}

void ResourceViewer::showImage(int index) {
    const std::vector<ResourceRef> &images = _resources.getImages();
    if (images.empty())
        return;

    int count = (int)images.size();
    index = ((index % count) + count) % count;
    _imageIndex = index;
    const ResourceRef &ref = images[index];
    _imageLoaded = _overlayLoaded = _spritesLoaded = false;
    unloadLevel();

    if (text::hasSuffixIgnoreCase(ref.name, ".lv2")) {
        showLevel(ref);
    } else if (text::hasSuffixIgnoreCase(ref.name, ".ovr")) {
        showOverlay(ref);
    } else if (text::hasSuffixIgnoreCase(ref.name, ".gs2")) {
        showSprites(ref);
    } else {
        _imageLoaded = _resources.loadImage(ref, _image);
        if (_imageLoaded && _image.hasPalette()) {
            std::memcpy(_screen->getPalette().data(), _image.getPalette().data(),
                        size_t(_image.getPaletteColorCount()) * 3);
            _screen->applyPalette();
        } else {
            initPalette();
        }
        const Canvas &surf = _image.getSurface();
        _status = text::format("%s  %dx%d", ref.toString().c_str(), surf.w, surf.h);
        if (!_imageLoaded)
            _status += "  (could not decode)";
    }

    _status = text::format("%d/%d  ", index + 1, count) + _status;
    log::debug(1, "Image %s", _status.c_str());
    _dirty = true;
}

void ResourceViewer::showSprites(const ResourceRef &ref) {
    std::unique_ptr<SeekableReadStream> stream = _resources.open(ref);
    _spritesLoaded = stream && _sprites.load(*stream) && _sprites.getFrameCount() > 0;
    if (!_spritesLoaded) {
        initPalette();
        _status = ref.toString() + "  (could not decode)";
        return;
    }

    // Sprites carry no palette of their own: they are drawn with the 3D
    // level's, from the GAME.LV2 beside them. Failing that, borrow one
    // from an image in the same archive.
    Palette palette;
    size_t slash = ref.name.find_last_of("/\\");
    std::string folder = slash == std::string::npos ? "" : ref.name.substr(0, slash + 1);
    std::unique_ptr<SeekableReadStream> levelStream = _resources.open({ref.archive, folder + "GAME.LV2"});
    LevelFile level;
    if (levelStream && level.load(*levelStream)) {
        _screen->getPalette() = level.getPalette();
        _screen->applyPalette();
    } else if (_resources.loadArchivePalette(ref.archive, palette)) {
        _screen->getPalette() = palette;
        _screen->applyPalette();
    } else {
        initPalette();
    }

    int sequences = 0;
    for (const std::vector<uint8_t> &sequence : _sprites.getSequences())
        sequences += !sequence.empty();
    _overlayFrame = 0;
    _overlayTime = 0;
    _status = text::format("%s  %d frames up to %dx%d, %d sequences", ref.toString().c_str(), _sprites.getFrameCount(),
                           _sprites.getMaxWidth(), _sprites.getMaxHeight(), sequences);
}

void ResourceViewer::showOverlay(const ResourceRef &ref) {
    std::unique_ptr<SeekableReadStream> stream = _resources.open(ref);
    _overlayLoaded = stream && _overlay.load(*stream);
    if (!_overlayLoaded) {
        initPalette();
        _status = ref.toString() + "  (could not decode)";
        return;
    }

    // Overlays carry no palette; borrow one from an image in the same archive.
    Palette palette;
    if (_resources.loadArchivePalette(ref.archive, palette)) {
        _screen->getPalette() = palette;
        _screen->applyPalette();
    } else {
        initPalette();
    }

    // As the game cuts them: the held items (nu*.ovr) into 88-row frames,
    // the punches into frames as tall as the view, the rest whole.
    size_t slash = ref.name.find_last_of("/\\");
    std::string file = text::toLower(ref.name.substr(slash == std::string::npos ? 0 : slash + 1));
    int rows = _overlay.getRowCount();
    if (file.compare(0, 2, "nu") == 0 && rows % HeldItem::kFrameHeight == 0)
        _overlayFrameHeight = HeldItem::kFrameHeight;
    else if (rows > Overlay::kViewHeight && rows % Overlay::kViewHeight == 0)
        _overlayFrameHeight = Overlay::kViewHeight;
    else
        _overlayFrameHeight = std::max(1, rows);
    _overlayFrame = 0;
    _overlayTime = 0;
    _status = text::format("%s  %dx%d, %d frames", ref.toString().c_str(), _overlay.getWidth(), _overlayFrameHeight,
                           rows / _overlayFrameHeight);
}

void ResourceViewer::jumpArchive(int direction) {
    const std::vector<ResourceRef> &images = _resources.getImages();
    if (images.empty())
        return;

    // Index of the first image of each archive.
    std::vector<int> starts;
    for (int i = 0; i < (int)images.size(); i++) {
        if (i == 0 || images[i].archive != images[i - 1].archive)
            starts.push_back(i);
    }

    int current = std::max(_imageIndex, 0);
    int group = 0;
    while (group + 1 < (int)starts.size() && starts[group + 1] <= current)
        group++;
    int n = (int)starts.size();
    showImage(starts[((group + direction) % n + n) % n]);
}

void ResourceViewer::jumpLevel(int direction) {
    const std::vector<ResourceRef> &images = _resources.getImages();
    int count = (int)images.size();
    for (int step = 1; step <= count; step++) {
        int index = (((_imageIndex + direction * step) % count) + count) % count;
        if (text::hasSuffixIgnoreCase(images[index].name, ".lv2")) {
            showImage(index);
            return;
        }
    }
}

void ResourceViewer::playSound(int index) {
    const std::vector<ResourceRef> &sounds = _resources.getSounds();
    if (sounds.empty()) {
        playBeep(440);
        return;
    }

    int count = (int)sounds.size();
    index = ((index % count) + count) % count;
    _soundIndex = index;
    const ResourceRef &ref = sounds[index];

    std::unique_ptr<SoundStream> stream = _resources.loadSound(ref);
    if (!stream)
        return;

    bool music = text::matchGlob(ref.archive, "MUSIC.*");
    _subtitle = music ? std::string() : _resources.loadSubtitle(ref);
    std::string desc = text::format("Sound %d/%d: %s", index + 1, count, ref.toString().c_str());
    log::debug(1, "%s%s%s", desc.c_str(), _subtitle.empty() ? "" : " - ", _subtitle.c_str());
    if (_subtitle.empty())
        _subtitle = desc;

    _mixer->stopHandle(_soundHandle);
    _mixer->playStream(music ? Mixer::kMusicSoundType : Mixer::kSpeechSoundType, &_soundHandle, std::move(stream));
    _dirty = true;
}

void ResourceViewer::playBeep(int frequency) {
    // 150 ms sine with a short linear fade-out, generated at 22050 Hz.
    const int rate = 22050;
    const int length = rate * 150 / 1000;
    std::vector<uint8_t> data(length * 2);
    for (int i = 0; i < length; i++) {
        double env = i > length * 3 / 4 ? (double)(length - i) / (length / 4) : 1.0;
        int16_t sample = (int16_t)(sin(2.0 * kPi * frequency * i / rate) * 12000 * env);
        writeLE16(&data[i * 2], (uint16_t)sample);
    }
    _mixer->playStream(Mixer::kSFXSoundType, nullptr,
                       makeRawStream(std::move(data), rate, kRaw16Bits | kRawLittleEndian));
}

} // namespace reluva::stgenerations
