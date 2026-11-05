// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The front end: the title, the options menu, the campaign between
// missions, the ending texts and the credits.

#include "engines/stgenerations/game/generations.h"

#include <algorithm>
#include <ctime>

#include "engines/stgenerations/debug.h"
#include "engines/stgenerations/platform.h"
#include "engines/stgenerations/formats/locations.h"

namespace reluva::stgenerations {

namespace {

const char* const kOptionsArchive = "OPTIONS.DAT";
// How long the title stays up (CIntroScreen, 0x48DD74).
const uint32_t kIntroMillis = 5000;
// How fast the ending texts and the credits roll, in pixels a second.
const int kScrollSpeed = 30;

int toMenuVolume(int volume) {
    return std::max(0, std::min((int)GameSettings::kMaxVolume,
                                (volume * GameSettings::kMaxVolume + Mixer::kMaxMixerVolume / 2) /
                                    Mixer::kMaxMixerVolume));
}

int toMixerVolume(int volume) {
    return volume * Mixer::kMaxMixerVolume / GameSettings::kMaxVolume;
}

} // namespace

bool Generations::startFrontEnd() {
    if (_options.viewer)
        return false;
    _menu.reset(new OptionsMenu(_resources));
    if (!_menu->load()) {
        log::warning("OPTIONS.DAT has no menu; starting the resource viewer");
        _menu.reset();
        return false;
    }
    std::unique_ptr<SeekableReadStream> script = _resources.open({ "STELLAR.DAT", "DESIGN/STATES.INI" });
    if (!script) {
        log::warning("STELLAR.DAT (on disc 1) wasn't found, so there's no story to play; starting the resource viewer");
        _menu.reset();
        return false;
    }
    std::vector<uint8_t> text = script->readAll();
    if (!_campaign.load(std::string(text.begin(), text.end()))) {
        log::warning("DESIGN/STATES.INI has no [Start]; starting the resource viewer");
        _menu.reset();
        return false;
    }
    _campaign.setRandom([this](int count) { return (int)_random.getRandomNumber(count - 1); });
    _bridge.reset(new Bridge(_resources, _mixer, _campaign, _random));
    if (_bridge->load()) {
        _bridge->reset();
        _campaign.setWorld(_bridge.get());
    } else {
        log::warning("STELLAR.DAT has no star map; the story goes on without the bridge");
        _bridge.reset();
    }
    _menu->setSoundCallback([this](int id) { playMenuSound(id); });
    _frontEnd = true;
    loadSettings();

    // For testing: straight into a space combat (--combat=n), or the story
    // from any section (--story=Amargosa2; --story=list names them).
    std::string story = _options.story;
    if (_options.combat > 0)
        story = text::format("SpaceCombat%dPlay", _options.combat);
    if (story == "list") {
        std::string names;
        for (const std::string& name : _campaign.getSectionNames())
            names += " " + name;
        log::debug(1, "The story's sections:%s", names.c_str());
    } else if (!story.empty()) {
        if (!_campaign.hasSection(story)) {
            log::warning("The story has no section [%s]; story=list names them", story.c_str());
        } else {
            startStoryAt(story);
            return true;
        }
    }

    // The original goes from the title straight into a new game. Here the
    // menu comes first, so a game can be loaded.
    showIntro();
    openMenu(false);
    return true;
}

void Generations::startStoryAt(const std::string& section) {
    // A new game's setup, then the section: the readings the scanner shows
    // (NewGame's SetFlag 1-4) and the Enterprise at the section's star, or
    // at Amargosa.
    if (_bridge)
        _bridge->reset();
    _campaign.start(section);
    for (int flag = 1; flag <= 4; flag++)
        _campaign.setFlag(flag, true);
    if (_bridge) {
        std::string sun = "Amargosa";
        for (size_t length = section.size(); length >= 4; length--) {
            if (_bridge->isSun(section.substr(0, length))) {
                sun = section.substr(0, length);
                break;
            }
        }
        _bridge->bridgeCommand("move", { "Enterprise", sun });
    }
    log::debug(1, "Starting the story at [%s]", section.c_str());
    runCampaign();
}

// The game's menu keeps its choices in the game's folder (kSettingsFile:
// key=value lines); the launcher's settings for the game are what they are
// until the menu changes them.
void Generations::loadSettings() {
    _storedSettings.clear();
    if (std::unique_ptr<SeekableReadStream> in = _system->openForLoading(kSettingsFile)) {
        while (!in->eos()) {
            const std::string line = text::trim(in->readLine());
            const size_t eq = line.find('=');
            if (eq != std::string::npos && line[0] != '#')
                _storedSettings[text::trim(line.substr(0, eq))] = text::trim(line.substr(eq + 1));
        }
    }
    auto number = [this](const char* key, int fallback) {
        auto it = _storedSettings.find(key);
        if (it == _storedSettings.end() || it->second.empty())
            return fallback;
        char* end = nullptr;
        const long v = std::strtol(it->second.c_str(), &end, 10);
        return *end ? fallback : int(v);
    };
    _settings.musicVolume = toMenuVolume(number("music_volume", _options.musicVolume));
    _settings.effectsVolume = toMenuVolume(number("sfx_volume", _options.effectsVolume));
    const int difficulty = number("difficulty", _options.difficulty);
    if (difficulty >= 0)
        _settings.difficulty = std::max(0, std::min(2, difficulty));
    const int subtitles = number("subtitles", _options.subtitles);
    if (subtitles >= 0)
        _settings.subtitles = subtitles != 0;
    const int largeWindow = number("large_window", _options.largeWindow);
    if (largeWindow >= 0)
        _settings.largeWindow = largeWindow != 0;
    const int lookspring = number("lookspring", _options.lookspring);
    if (lookspring >= 0)
        _settings.lookspring = lookspring != 0;
    syncSoundSettings();
}

void Generations::saveSettings() {
    _storedSettings["music_volume"] = std::to_string(toMixerVolume(_settings.musicVolume));
    _storedSettings["sfx_volume"] = std::to_string(toMixerVolume(_settings.effectsVolume));
    _storedSettings["speech_volume"] = std::to_string(toMixerVolume(_settings.effectsVolume));
    _storedSettings["difficulty"] = std::to_string(_settings.difficulty);
    _storedSettings["subtitles"] = _settings.subtitles ? "1" : "0";
    _storedSettings["large_window"] = _settings.largeWindow ? "1" : "0";
    _storedSettings["lookspring"] = _settings.lookspring ? "1" : "0";
    std::string out = "# Star Trek: Generations' own settings, as its menu sets them.\n";
    for (const auto& [key, value] : _storedSettings)
        out += key + "=" + value + "\n";
    if (!_system->saveFile(kSettingsFile, std::vector<uint8_t>(out.begin(), out.end())))
        log::warning("The settings couldn't be saved in %s", _system->saveDir().string().c_str());
    syncSoundSettings();
}

void Generations::syncSoundSettings() {
    if (!_mixer)
        return;
    auto stored = [this](const char* key, int fallback) {
        auto it = _storedSettings.find(key);
        return it == _storedSettings.end() ? fallback : std::atoi(it->second.c_str());
    };
    _mixer->setVolumeForSoundType(Mixer::kMusicSoundType, stored("music_volume", _options.musicVolume));
    _mixer->setVolumeForSoundType(Mixer::kSFXSoundType, stored("sfx_volume", _options.effectsVolume));
    _mixer->setVolumeForSoundType(Mixer::kSpeechSoundType, stored("speech_volume", _options.speechVolume));
    _mixer->setVolumeForSoundType(Mixer::kPlainSoundType, Mixer::kMaxMixerVolume);
}

void Generations::runCampaign() {
    while (_frontEnd && !shouldQuit()) {
        Campaign::Action action = _campaign.next();
        if (action.type == Campaign::Action::kBridge) {
            // The main loop runs the bridge, a command a frame.
            setBridgeMode(true);
            return;
        }
        // Anything else takes the screen from the bridge.
        if (action.type != Campaign::Action::kAutoSave)
            setBridgeMode(false);
        switch (action.type) {
        case Campaign::Action::kBridge:
            break;
        case Campaign::Action::kSpaceCombat:
            if (playSpaceCombat(action.number))
                return;
            break;
        case Campaign::Action::kIntro:
            showIntro();
            break;
        case Campaign::Action::kMovie:
            playMovie(text::format("MOVIE/MOV_%02d.AVI", action.number));
            break;
        case Campaign::Action::kScrollText:
            showScrollText(action.number);
            break;
        case Campaign::Action::kAutoSave:
            saveGameState(OptionsMenu::kAutoSaveSlot, "Automatic save");
            break;
        case Campaign::Action::kActionGame:
            if (startCampaignMission(action.number))
                return;
            // Not installed: the mission counts as lost.
            _campaign.missionEnded(false);
            break;
        case Campaign::Action::kEndGame:
            showCredits();
            quitGame();
            return;
        case Campaign::Action::kOptions:
        case Campaign::Action::kNone:
            // The game is over: the menu, with nothing to return to.
            openMenu(false);
            return;
        }
    }
}

void Generations::setBridgeMode(bool on) {
    if (!_bridge || on == _bridgeMode)
        return;
    _bridgeMode = on;
    if (on) {
        // The bridge is drawn in true colour, as the original's 16-bit
        // screen showed its many palettes.
        const Canvas::Format format = Canvas::kArgb32;
        _system->initSize(Screen::kWidth, Screen::kHeight, format);
        _bridgeSurface.create(Screen::kWidth, Screen::kHeight, format);
        _bridge->setSubtitles(_settings.subtitles);
        setCursor(false);
    } else {
        _bridge->hide();
        _system->initSize(Screen::kWidth, Screen::kHeight);
        _screen->applyPalette();
    }
    _dirty = true;
}

void Generations::updateBridge(uint32_t delta) {
    _bridge->update(delta);
    for (Bridge::Event event = _bridge->takeEvent(); event.type != Bridge::Event::kNone;
         event = _bridge->takeEvent()) {
        switch (event.type) {
        case Bridge::Event::kTransport:
            _campaign.transport();
            break;
        case Bridge::Event::kTactical:
            _campaign.startCombat(-1);
            break;
        case Bridge::Event::kCombat:
            _campaign.startCombat(event.fleet);
            break;
        case Bridge::Event::kOptions:
            openMenu(true);
            return;
        default:
            break;
        }
    }
    // One script command a frame while the bridge is up.
    runCampaign();
    if (!_bridgeMode || _menuOpen)
        return;
    _bridge->draw(_bridgeSurface);
    _system->copyRectToScreen(_bridgeSurface.getPixels(), _bridgeSurface.pitch, 0, 0, Screen::kWidth, Screen::kHeight);
    _system->updateScreen();
}

bool Generations::playSpaceCombat(int number) {
    _combat.reset(new SpaceCombat(_resources, _mixer, _random));
    if (!_combat->load(number)) {
        // COMBAT.DAT is on disc 1; without it the fight is taken as won.
        log::warning("Space combat %d can't be loaded (is COMBAT.DAT there?); taking it as won", number);
        _combat.reset();
        _campaign.combatEnded(Campaign::kCombatWon);
        return false;
    }
    _combat->setSubtitles(_settings.subtitles);
    _combat->setDifficulty(_settings.difficulty);
    setTrueColour(true);
    setCursor(false);
    return true;
}

void Generations::setTrueColour(bool on) {
    if (on) {
        const Canvas::Format format = Canvas::kArgb32;
        _system->initSize(Screen::kWidth, Screen::kHeight, format);
        _bridgeSurface.create(Screen::kWidth, Screen::kHeight, format);
    } else {
        _system->initSize(Screen::kWidth, Screen::kHeight);
        _screen->applyPalette();
    }
}

void Generations::updateCombat(uint32_t delta) {
    _combat->update(delta);
    if (_combat->takeOptionsRequest()) {
        openMenu(true);
        return;
    }
    if (_combat->isFinished()) {
        // 0x418F7A: won (fleet beaten), lost (warped out) or the Enterprise gone.
        SpaceCombat::Result result = _combat->getResult();
        _combat.reset();
        setTrueColour(false);
        _campaign.combatEnded(result == SpaceCombat::kWon    ? Campaign::kCombatWon
                              : result == SpaceCombat::kLost ? Campaign::kCombatLost
                                                             : Campaign::kEnterpriseDestroyed);
        runCampaign();
        return;
    }
    _combat->draw(_bridgeSurface);
    _system->copyRectToScreen(_bridgeSurface.getPixels(), _bridgeSurface.pitch, 0, 0, Screen::kWidth, Screen::kHeight);
    _system->updateScreen();
}

bool Generations::startCampaignMission(int type) {
    for (const Location& location : getLocations()) {
        if (location.type != type)
            continue;
        for (int i = 0; i < (int)_missions.size(); i++) {
            if (_missions[i].name == location.folder) {
                playMission(i);
                return _playingMission;
            }
        }
        log::warning("The away mission %s isn't installed", location.folder);
        return false;
    }
    log::warning("The campaign asked for away mission %d, which doesn't exist", type);
    return false;
}

void Generations::campaignMissionEnded() {
    bool accomplished = _mission && _mission->isAccomplished();
    leaveMission(text::format("%s %s", _missions[_missionChoice].name.c_str(),
                                accomplished ? "accomplished" : "ended, objectives not met"));
    // Any outcome code the mission set is a clue for the story (IfClue).
    bool clue = _mission && !_mission->getOutcomes().empty();
    _campaign.missionEnded(accomplished, clue);
    runCampaign();
}

void Generations::openMenu(bool canReturn) {
    if (!_menu)
        return;
    if (!_menuOpen)
        _gamePalette = _screen->getPalette();
    _menuOverBridge = _bridgeMode;
    if (_bridgeMode) {
        _system->initSize(Screen::kWidth, Screen::kHeight);
        _bridgeMode = false;
    }
    if (_combat)
        _system->initSize(Screen::kWidth, Screen::kHeight);
    _menuOpen = true;
    _menu->setSlots(listSaves());
    bool inMission = _playingMission && _mission;
    // Combat can't be saved: its state isn't kept. Nor can a mission that
    // hasn't started.
    _menu->open(_settings, canReturn, (inMission || _menuOverBridge) && !_combat && !_awaitingProceed,
                !inMission && !_combat);
    _screen->getPalette() = _menu->getPalette();
    _screen->applyPalette();
    setCursor(false);
    _dirty = true;
}

void Generations::closeMenu(bool resume) {
    _menuOpen = false;
    _system->setTextInput(false);
    _screen->getPalette() = _gamePalette;
    _screen->applyPalette();
    if (_menuOverBridge) {
        _menuOverBridge = false;
        if (resume)
            setBridgeMode(true);
        else if (_bridge)
            _bridge->hide();
    }
    if (_combat) {
        if (resume)
            setTrueColour(true);
        else
            _combat.reset();
    }
    _dirty = true;
}

void Generations::playMenuSound(int id) {
    ResourceRef ref = { kOptionsArchive, text::format("%c_%04d.WAV", id >= 2000 ? 'M' : 'X', id) };
    std::unique_ptr<SoundStream> stream = _resources.loadSound(ref);
    if (stream)
        _mixer->playStream(Mixer::kSFXSoundType, &_menuSound, std::move(stream));
}

void Generations::handleMenuEvent(const Event& event) {
    switch (event.type) {
    case kEventKeydown:
        _menu->keyDown(event.kbd);
        break;
    case kEventLbuttondown:
        _menu->mouseDown(event.mouse.x, event.mouse.y);
        break;
    case kEventMousemove:
        _menu->mouseMove(event.mouse.x, event.mouse.y);
        break;
    case kEventLbuttonup:
        _menu->mouseUp(event.mouse.x, event.mouse.y);
        break;
    default:
        break;
    }
    _dirty = true;
    // Typed characters reach the menu while a name is typed in.
    _system->setTextInput(_menu->isTyping());

    if (_menu->takeSettingsChanged()) {
        _settings = _menu->getSettings();
        saveSettings();
        if (_mission)
            _mission->setDifficulty(_settings.difficulty);
        if (_bridge)
            _bridge->setSubtitles(_settings.subtitles);
    }

    switch (_menu->takeResult()) {
    case OptionsMenu::kReturn:
        closeMenu();
        break;
    case OptionsMenu::kNewGame:
        closeMenu(false);
        if (_playingMission)
            leaveMission("New game");
        _carriedArray.clear();
        if (_bridge)
            _bridge->reset();
        _campaign.start("NewGame");
        runCampaign();
        break;
    case OptionsMenu::kLoad: {
        closeMenu(false);
        Error err = loadGameState(_menu->getSlot());
        if (err) {
            log::warning("%s", err.message.c_str());
            openMenu(_playingMission);
        }
        break;
    }
    case OptionsMenu::kSave: {
        Error err = saveGameState(_menu->getSlot(), _menu->getSaveName());
        if (err)
            log::warning("%s", err.message.c_str());
        closeMenu();
        break;
    }
    case OptionsMenu::kQuit:
        closeMenu(false);
        _mixer->stopAll();
        showCredits();
        quitGame();
        break;
    default:
        break;
    }
}

std::vector<OptionsMenu::Slot> Generations::listSaves() {
    std::vector<OptionsMenu::Slot> slots(OptionsMenu::kAutoSaveSlot + 1);
    for (int i = 0; i < (int)slots.size(); i++) {
        std::string description, date;
        if (readSaveHeader(i, description, date)) {
            slots[i].used = true;
            slots[i].name = description;
            slots[i].date = date;
        }
    }
    return slots;
}

bool Generations::waitForSkip() {
    bool skip = false;
    Event event;
    while (_eventMan->pollEvent(event)) {
        if (event.type == kEventLbuttondown || event.type == kEventRbuttondown ||
            event.type == kEventKeydown)
            skip = true;
    }
    return skip || shouldQuit();
}

bool Generations::showPicture(const ResourceRef& ref, BitmapDecoder& decoder) {
    if (!_resources.loadImage(ref, decoder) || !decoder.hasPalette())
        return false;
    _screen->getPalette() = decoder.getPalette();
    _screen->applyPalette();
    _screen->clear(0);
    _screen->drawCentered(decoder.getSurface());
    _screen->update();
    return true;
}

void Generations::showIntro() {
    BitmapDecoder decoder;
    if (!showPicture({ kOptionsArchive, "INTRO1.BMP" }, decoder))
        return;
    _system->showMouse(false);
    uint32_t start = _system->getMillis();
    while (_system->getMillis() - start < kIntroMillis && !waitForSkip()) {
        _screen->update();
        _system->delayMillis(10);
    }
    _system->showMouse(true);
}

bool Generations::playMusic(const std::string& name) {
    // The music is on the discs, in MUSIC.1 or MUSIC.2.
    for (const char* archive : { "MUSIC.1", "MUSIC.2" }) {
        ResourceRef ref = { archive, text::toUpper(name) };
        if (!_resources.hasResource(ref))
            continue;
        std::unique_ptr<SoundStream> stream = _resources.loadSound(ref);
        if (!stream)
            return false;
        _mixer->stopHandle(_musicHandle);
        _mixer->playStream(Mixer::kMusicSoundType, &_musicHandle, std::move(stream));
        return true;
    }
    log::warning("The music %s wasn't found", name.c_str());
    return false;
}

void Generations::rollText(const BitmapDecoder& background, const Font& font,
                         const std::vector<RollLine>& lines, const std::vector<std::string>& music) {
    _screen->getPalette() = background.getPalette();
    _screen->applyPalette();
    _system->showMouse(false);

    int height = 0;
    for (const RollLine& line : lines)
        height += line.gap + font.getHeight();
    size_t track = 0;
    bool playing = !music.empty() && playMusic(music[track++]);

    // The text rises from the bottom until it has all gone by, or the
    // music has ended.
    uint32_t start = _system->getMillis();
    while (!waitForSkip()) {
        int offset = (int)((_system->getMillis() - start) * kScrollSpeed / 1000);
        if (offset > height + Screen::kHeight)
            break;
        if (playing && !_mixer->isSoundHandleActive(_musicHandle)) {
            if (track >= music.size())
                break;
            playing = playMusic(music[track++]);
        }
        _screen->clear(0);
        _screen->drawCentered(background.getSurface());
        int y = Screen::kHeight - offset;
        for (const RollLine& line : lines) {
            y += line.gap;
            if (y > -font.getHeight() && y < Screen::kHeight) {
                int x = line.centred ? (Screen::kWidth - font.getStringWidth(line.text)) / 2 : 0;
                font.drawString(_screen->getSurface(), line.text, x, y);
            }
            y += font.getHeight();
        }
        _screen->update();
        _system->delayMillis(10);
    }
    _mixer->stopHandle(_musicHandle);
    _system->showMouse(true);
}

void Generations::showScrollText(int number) {
    // TEXTn.TXT: the background, the music, the font, then the text, its
    // lines centred by hand with spaces.
    std::unique_ptr<SeekableReadStream> stream =
        _resources.open({ kOptionsArchive, text::format("TEXT%d.TXT", number) });
    if (!stream)
        return;
    std::vector<std::string> header;
    std::vector<RollLine> lines;
    while (!stream->eos()) {
        std::string line = stream->readLine();
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (header.size() < 3)
            header.push_back(text::trim(line));
        else
            lines.push_back({ line, 0, false });
    }
    if (header.size() < 3)
        return;
    BitmapDecoder background;
    Font font;
    std::unique_ptr<SeekableReadStream> fontStream = _resources.open({ kOptionsArchive, header[2] });
    if (!_resources.loadImage({ kOptionsArchive, header[0] }, background) || !background.hasPalette() || !fontStream ||
        !font.load(*fontStream))
        return;
    rollText(background, font, lines, { header[1] });
}

bool Generations::loadCredits(std::vector<RollLine>& lines) {
    // The credits are a table in sgens.exe (0x5401CC): records of a heading,
    // a name and a style, 1 when more names follow, 3 a section's title.
    std::unique_ptr<SeekableReadStream> exe = _resources.open({ "", "SGENS.EXE" });
    if (!exe)
        return false;
    std::vector<uint8_t> data = exe->readAll();
    auto u16 = [&](size_t o) { return o + 2 <= data.size() ? (uint32_t)data[o] | (uint32_t)data[o + 1] << 8 : 0u; };
    auto u32 = [&](size_t o) { return o + 4 <= data.size() ? u16(o) | u16(o + 2) << 16 : 0u; };
    uint32_t pe = u32(0x3C);
    if (u32(pe) != 0x4550)
        return false;
    uint32_t sections = u16(pe + 6), base = u32(pe + 52);
    size_t table = pe + 24 + u16(pe + 20);
    // A virtual address to a file offset, or 0.
    auto offset = [&](uint32_t va) -> size_t {
        for (uint32_t i = 0; i < sections; i++) {
            size_t s = table + 40 * i;
            uint32_t start = base + u32(s + 12), size = std::max(u32(s + 8), u32(s + 16)), raw = u32(s + 20);
            if (va >= start && va - start < size && raw + (va - start) < data.size())
                return raw + (va - start);
        }
        return 0;
    };
    auto text = [&](uint32_t va, std::string& out) {
        size_t o = va ? offset(va) : 0;
        if (va && !o)
            return false;
        out.clear();
        while (o && o < data.size() && data[o] && out.size() < 200)
            out += (char)data[o++];
        return true;
    };

    size_t records = offset(0x5401CC);
    std::string heading, name;
    if (!records || !text(u32(records), heading) || heading != "Producer")
        return false;
    for (int i = 0; i < 1000; i++) {
        size_t r = records + 12 * i;
        uint32_t style = u32(r + 8);
        if (style > 3 || !text(u32(r), heading) || !text(u32(r + 4), name))
            break;
        if (style == 0) {
            lines.push_back({ "", 0, true });
            continue;
        }
        if (!heading.empty())
            lines.push_back({ heading, style == 3 ? 24 : 0, true });
        if (!name.empty())
            lines.push_back({ name, 0, true });
    }
    return !lines.empty();
}

void Generations::showCredits() {
    std::vector<RollLine> lines;
    if (!loadCredits(lines)) {
        log::warning("The credits are read from SGENS.EXE, which wasn't found in the game folder");
        return;
    }
    BitmapDecoder background;
    Font font;
    std::unique_ptr<SeekableReadStream> fontStream = _resources.open({ kOptionsArchive, "CREDITS.FNT" });
    if (!_resources.loadImage({ kOptionsArchive, "CREDITBK.BMP" }, background) || !background.hasPalette() ||
        !fontStream || !font.load(*fontStream))
        return;
    // The music the credits change through (0x540198).
    rollText(background, font, lines, { "CREDITS.WAV", "MUSI9022.WAV", "MUSI5003.WAV", "MUSI9024.WAV", "MUSI9020.WAV" });
}

bool Generations::readSaveHeader(int slot, std::string& description, std::string& date) {
    std::unique_ptr<SeekableReadStream> in = _system->openForLoading(getSaveFileName(slot));
    if (!in || in->readUint32BE() != kSaveMagic)
        return false;
    uint32_t version = in->readUint32LE();
    description = in->readFixedString(in->readUint16LE());
    date.clear();
    if (version >= kFirstDatedSaveVersion)
        date = in->readFixedString(in->readUint16LE());
    return !in->eos();
}

std::string Generations::saveDate() {
    std::time_t now = std::time(nullptr);
    char text[32];
    if (!std::strftime(text, sizeof text, "%m/%d %H:%M", std::localtime(&now)))
        return std::string();
    return text;
}

} // namespace reluva::stgenerations
