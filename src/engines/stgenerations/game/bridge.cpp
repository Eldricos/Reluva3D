// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engines/stgenerations/audio.h"
#include "engines/stgenerations/debug.h"
#include "engines/stgenerations/formats/bitmap.h"
#include "common/serializer.h"
#include "engines/stgenerations/formats/overlay.h"

namespace reluva::stgenerations {

namespace {

// Not M_PI: MSVC only has it with _USE_MATH_DEFINES.
constexpr double kPi = 3.14159265358979323846;

const char *const kArchive = "STELLAR.DAT";
const int kMapHeight = 362;
const int kPanelTop = 362;
// The text and scanner display's corner (GUI (0,6)).
const int kScannerX = 0, kScannerY = 368;
const Rect kTextArea(100, 368, 253, 464);
const Rect kStatusLine(0, 466, 640, 480);
const Rect kMapArea(0, 0, 640, kMapHeight);
// The stardate's digits (GUI (485,35)).
const int kStardateX = 485, kStardateY = 397;
const uint32_t kZoomMillis = 2000;
const uint32_t kStardateStep = 500;
const uint32_t kScanFrameMillis = 60;
const int kScanFrames = 29;
const double kNewGameStardate = 48623.4;
// Fleet slots around a star (0x46E96B), in map units, sector and system view.
const int kSlotOffsets[2][4][2] = {{{-15, -10}, {-15, 10}, {25, -10}, {25, 10}},
                                   {{-40, -10}, {-40, 25}, {40, -10}, {40, 25}}};

std::string lower(const std::string &s) {
    return text::toLower(s);
}

std::string join(const std::vector<std::string> &args, size_t from) {
    std::string text;
    for (size_t i = from; i < args.size(); i++)
        text += (text.empty() ? "" : " ") + args[i];
    return text;
}

int toInt(const std::vector<std::string> &args, size_t i) {
    return i < args.size() ? std::atoi(args[i].c_str()) : 0;
}

std::string arg(const std::vector<std::string> &args, size_t i) {
    return i < args.size() ? args[i] : std::string();
}

} // namespace

struct Bridge::Planet {
    std::string name;
    std::string texture, scan;
    int size = 16;
    double distance = 40, period = 13, pitch = 0, intersect = 0, angle = 0;
    struct Moon {
        std::string texture;
        int size;
        double distance, period, angle;
    };
    std::vector<Moon> moons;
};

struct Bridge::Sun {
    std::string name;
    double x = 0, y = 0, z = 0;
    int size = 64;
    std::string texture, destruct, destroyed;
    int textureFrames = 6, destructFrames = 12, destroyedFrames = 6, fps = 10;
    std::vector<Planet> planets;
    // Time into the destruction (Destroy or a simulation), in ms.
    uint32_t destructTime = 0;
};

struct Bridge::Fleet {
    std::string name;
    // BOP, CHO, ENT, VOR or WAR: the FLEET\ pictures.
    std::string kind;
    int ships = 1;
    // 0 Green, 1 Yellow, 2 Red.
    int alert = 0;
    int location = -1;
    // 0x4687B1's states, as the report numbers them.
    int state = 0;
    int frame = 0;
    uint32_t timer = 0;
    int destination = -1;
    double arrival = 0;
};

struct Bridge::Figure {
    struct Sequence {
        std::string graphic;
        int x, y, frames;
        std::vector<std::pair<int, int>> steps; // frame (1-based), ms
    };
    std::vector<Sequence> sequences;
    int current = 0;
    int step = 0;
    uint32_t timer = 0;
    bool playing = false;
    uint32_t wait = 5000;
};

struct Bridge::Button {
    enum Id { kSetCourse, kContext, kOptions, kView, kSimulate, kScan, kUp, kDown };
    Id id;
    Rect rect;
    const char *file;
    int frames;
    bool hasDim;
    int state = 0;
    bool enabled = true;
    bool flash = false;
};

Bridge::Bridge(const ResourceManager &resources, Mixer *mixer, Campaign &campaign, RandomSource &random)
    : _resources(resources), _mixer(mixer), _campaign(campaign), _random(random) {
    // The panel's buttons (GUI loader 0x47347C), at y 362 on.
    auto add = [this](Button::Id id, int l, int t, int r, int b, const char *file, int frames, bool hasDim) {
        Button button;
        button.id = id;
        button.rect = Rect(l, t + kPanelTop, r, b + kPanelTop);
        button.file = file;
        button.frames = frames;
        button.hasDim = hasDim;
        _buttons.push_back(button);
    };
    add(Button::kSetCourse, 536, 0, 640, 27, "GUI\\SETCOURS.BMP", 2, false);
    add(Button::kContext, 536, 36, 640, 63, "GUI\\TRANSACT.BMP", 7, true);
    add(Button::kOptions, 536, 73, 640, 100, "GUI\\OPTIONS.BMP", 2, false);
    add(Button::kView, 292, 0, 412, 27, "GUI\\VIEW.BMP", 4, false);
    add(Button::kSimulate, 292, 36, 412, 63, "GUI\\SIMULATE.BMP", 5, true);
    add(Button::kScan, 292, 73, 412, 100, "GUI\\SCAN.BMP", 4, false);
    add(Button::kUp, 258, 27, 283, 45, "GUI\\UPTEXT.BMP", 3, true);
    add(Button::kDown, 258, 46, 283, 64, "GUI\\DNTEXT.BMP", 3, true);
    _camera[0] = 319500;
    _camera[1] = 180500;
    _camera[2] = 1000;
    for (int i = 0; i < 3; i++)
        _cameraTarget[i] = _camera[i], _cameraStep[i] = 0;
}

Bridge::~Bridge() {
    hide();
}

// Data

std::unique_ptr<SeekableReadStream> Bridge::open(const std::string &path) const {
    return _resources.open({kArchive, path});
}

const RgbImage *Bridge::image(const std::string &path, int frames, int key) {
    std::string name = lower(path) + text::format("#%d", frames);
    auto it = _images.find(name);
    if (it != _images.end())
        return it->second.get();
    std::unique_ptr<RgbImage> result;
    BitmapDecoder decoder;
    std::unique_ptr<SeekableReadStream> stream = open(path);
    if (stream && decoder.loadStream(*stream) && decoder.hasPalette()) {
        result.reset(new RgbImage());
        if (!result->load(decoder, frames, key))
            result.reset();
    } else {
        log::warning("Bridge: can't load %s", path.c_str());
    }
    const RgbImage *found = result.get();
    _images[name] = std::move(result);
    return found;
}

const RgbImage *Bridge::overlay(const std::string &path, int frameHeight, const std::string &palette) {
    std::string name = lower(path);
    auto it = _images.find(name);
    if (it != _images.end())
        return it->second.get();
    std::unique_ptr<RgbImage> result;
    BitmapDecoder decoder;
    Overlay rle;
    std::unique_ptr<SeekableReadStream> stream = open(path);
    std::unique_ptr<SeekableReadStream> paletteStream = open(palette);
    if (stream && paletteStream && rle.load(*stream) && decoder.loadStream(*paletteStream) && decoder.hasPalette()) {
        result.reset(new RgbImage());
        result->load(rle, decoder.getPalette(), frameHeight > 0 ? frameHeight : rle.getRowCount());
    } else {
        log::warning("Bridge: can't load %s", path.c_str());
    }
    const RgbImage *found = result.get();
    _images[name] = std::move(result);
    return found;
}

bool Bridge::load() {
    std::unique_ptr<SeekableReadStream> ini = open("DESIGN/UNIVERSE.INI");
    if (!ini)
        return false;
    std::vector<uint8_t> text = ini->readAll();
    _ini.load(std::string(text.begin(), text.end()));

    std::unique_ptr<SeekableReadStream> font = open("FONTS/STELLAR.FNT");
    if (!font || !_font.load(*font))
        log::warning("Bridge: no FONTS/STELLAR.FNT");
    // The font's colours are the panel's.
    BitmapDecoder panel;
    std::unique_ptr<SeekableReadStream> panelStream = open("GUI/GUI-BACK.BMP");
    if (!panelStream || !panel.loadStream(*panelStream) || !panel.hasPalette())
        return false;
    _fontPalette = panel.getPalette();

    _suns.clear();
    for (int i = 1; i <= _ini.getInt("Universe", "NumSuns", kSuns) && i <= kSuns; i++) {
        Sun sun;
        sun.name = _ini.get("Universe", text::format("Sun%d", i));
        const std::string &s = sun.name;
        std::vector<std::string> location = text::split(_ini.get(s, "Location", "0,0,0"), ',');
        sun.x = location.size() > 0 ? std::atof(location[0].c_str()) : 0;
        sun.y = location.size() > 1 ? std::atof(location[1].c_str()) : 0;
        sun.z = location.size() > 2 ? std::atof(location[2].c_str()) : 0;
        sun.texture = _ini.get(s, "TextureGraphic");
        sun.textureFrames = _ini.getInt(s, "TextureFrames", 6);
        sun.fps = std::max(1, _ini.getInt(s, "TextureFPS", 10));
        sun.destruct = _ini.get(s, "DestructGraphic");
        sun.destructFrames = _ini.getInt(s, "DestructFrames", 12);
        sun.destroyed = _ini.get(s, "DestroyedGraphic");
        sun.destroyedFrames = _ini.getInt(s, "DestroyedFrames", 6);
        // Antilios has no Size: the texture's width (0x464716).
        sun.size = _ini.getInt(s, "Size", 64);
        double pitch = _ini.getDouble(s, "OrbitPitch");
        double intersect = _ini.getDouble(s, "OrbitIntersect");
        for (int p = 1; p <= _ini.getInt(s, "NumPlanets"); p++) {
            Planet planet;
            planet.name = s + text::format("%d", p);
            const std::string &n = planet.name;
            planet.texture = _ini.get(n, "TextureGraphic");
            planet.scan = _ini.get(n, "ScanGraphic");
            planet.size = _ini.getInt(n, "Size", 16);
            planet.distance = _ini.getDouble(n, "Distance", 40 + 20 * (p - 1));
            planet.period = _ini.getDouble(n, "Speed", planet.distance / 3);
            planet.pitch = _ini.getDouble(n, "OrbitPitch", pitch);
            planet.intersect = _ini.getDouble(n, "OrbitIntersect", intersect);
            for (int m = 1; m <= _ini.getInt(n, "NumMoons"); m++) {
                std::string moon = n + text::format("Moon%d", m);
                planet.moons.push_back({_ini.get(moon, "TextureGraphic"), _ini.getInt(moon, "Size", 8),
                                        (double)(planet.size / 2 + 6 * m), 4.0 + 2 * m, 0});
            }
            sun.planets.push_back(planet);
        }
        _suns.push_back(sun);
    }

    _figures.clear();
    for (const char *who : {"Data", "Picard"}) {
        Figure figure;
        int count = _ini.getInt("PicardDataAnimation", text::format("Num%sSequences", who), 0);
        for (int i = 1; i <= count; i++) {
            std::string s = text::format("%s%d", who, i);
            Figure::Sequence sequence;
            sequence.graphic = _ini.get(s, "Graphic");
            std::vector<std::string> location = text::split(_ini.get(s, "Location", "0,0"), ',');
            sequence.x = std::atoi(location[0].c_str());
            sequence.y = location.size() > 1 ? std::atoi(location[1].c_str()) : 0;
            sequence.frames = std::max(1, _ini.getInt(s, "NumFrames", 1));
            for (int k = 1; k <= _ini.getInt(s, "NumSequences"); k++) {
                std::vector<std::string> step = text::split(_ini.get(s, text::format("Sequence%d", k)), ',');
                if (step.size() == 2)
                    sequence.steps.push_back({std::atoi(step[0].c_str()), std::atoi(step[1].c_str())});
            }
            figure.sequences.push_back(sequence);
        }
        _figures.push_back(figure);
    }
    return !_suns.empty();
}

void Bridge::reset() {
    // SetText and Alert changes go with the old game.
    std::unique_ptr<SeekableReadStream> ini = open("DESIGN/UNIVERSE.INI");
    if (ini) {
        std::vector<uint8_t> text = ini->readAll();
        _ini.load(std::string(text.begin(), text.end()));
    }
    _stardate = _targetStardate = kNewGameStardate;
    _flags.clear();
    _soran.clear();
    _clueWave = "Sound\\Voices\\Comp0210.wav";
    _selected.clear();
    _nexusGraphic = _nexusOldGraphic = 1;
    _nexusShown = false;
    _nexusState = 1;
    _longScans = 0;
    _nexusSpoken = false;
    _simEnabled = false;
    _controls = true;
    _contextMode = kContextIdle;
    _panelMode = kPanelEmpty;
    _scanObject.clear();
    _textLines.clear();
    _pendingCombat = 0;
    _events.clear();
    _mapMode = kSectorView;
    _system = -1;
    _zoomLeft = 0;
    _camera[0] = 319500;
    _camera[1] = 180500;
    _camera[2] = 1000;

    // The objects' readings (0x4644ED: only on a new game).
    auto readings = [this](const std::string &section) {
        std::set<int> &f = flags(section);
        if (_ini.getBool(section, "HasTrilithium"))
            f.insert(2);
        if (_ini.getBool(section, "HasEnergy"))
            f.insert(3);
        if (_ini.getBool(section, "HasLife"))
            f.insert(4);
    };
    for (Sun &sun : _suns) {
        readings(sun.name);
        sun.destructTime = 0;
        for (Planet &planet : sun.planets) {
            readings(planet.name);
            planet.angle = _random.getRandomNumber(359) * kPi / 180;
            for (Planet::Moon &moon : planet.moons)
                moon.angle = _random.getRandomNumber(359) * kPi / 180;
        }
    }
    readings("SpaceStation");

    _fleets.clear();
    _fleets.resize(kFleets + 1);
    for (int i = 0; i <= kFleets; i++) {
        Fleet &fleet = _fleets[i];
        fleet.name = i == 0 ? std::string("Enterprise") : text::format("Fleet%d", i);
        std::string type = lower(_ini.get(fleet.name, "Type", i == 0 ? "Galaxy" : "Klingon"));
        fleet.kind = type == "galaxy"     ? "ENT"
                     : type == "klingon2" ? "VOR"
                     : type == "romulan"  ? "WAR"
                     : type == "chodak"   ? "CHO"
                                          : "BOP";
        fleet.ships = std::max(1, _ini.getInt(fleet.name, "NumShips", 1));
        std::string alert = lower(_ini.get(fleet.name, "Alert", "Green"));
        fleet.alert = alert == "red" ? 2 : alert == "yellow" ? 1 : 0;
        readings(fleet.name);
        if (_ini.getBool(fleet.name, "Cloaked"))
            flags(fleet.name).insert(6);
    }
}

int Bridge::findSun(const std::string &name) const {
    for (int i = 0; i < (int)_suns.size(); i++) {
        if (text::equalsIgnoreCase(_suns[i].name, name))
            return i;
    }
    return -1;
}

int Bridge::findFleet(const std::string &name) const {
    if (text::equalsIgnoreCase(name, "Enterprise"))
        return 0;
    if (name.size() > 5 && text::equalsIgnoreCase(name.substr(0, 5), "fleet")) {
        int n = std::atoi(name.c_str() + 5);
        if (n >= 1 && n <= kFleets)
            return n;
    }
    return -1;
}

std::set<int> &Bridge::flags(const std::string &object) {
    return _flags[lower(object)];
}

const std::set<int> *Bridge::findFlags(const std::string &object) const {
    auto it = _flags.find(lower(object));
    return it == _flags.end() ? nullptr : &it->second;
}

bool Bridge::getObjectFlag(const std::string &object, int flag) const {
    const std::set<int> *f = findFlags(object);
    return f && f->count(flag);
}

int Bridge::sunOf(const std::string &object) const {
    if (object.empty())
        return -1;
    int fleet = findFleet(object);
    if (fleet >= 0)
        return fleet < (int)_fleets.size() ? _fleets[fleet].location : -1;
    if (text::equalsIgnoreCase(object, "SpaceStation"))
        return findSun("Amargosa");
    for (int i = 0; i < (int)_suns.size(); i++) {
        const std::string &name = _suns[i].name;
        if (object.size() >= name.size() && text::equalsIgnoreCase(object.substr(0, name.size()), name))
            return i;
    }
    return -1;
}

bool Bridge::isEnterpriseAt(const std::string &sun) const {
    int i = findSun(sun);
    return i >= 0 && !_fleets.empty() && _fleets[0].location == i;
}

bool Bridge::isWarping() const {
    if (_fleets.empty())
        return false;
    int state = _fleets[0].state;
    return state >= 2 && state <= 7;
}

bool Bridge::isEnterpriseIdle() const {
    return !_fleets.empty() && _fleets[0].location >= 0 && (_fleets[0].state == 8 || _fleets[0].state == 0);
}

// Commands

void Bridge::bridgeCommand(const std::string &command, const std::vector<std::string> &args) {
    // Moving things about and setting flags happen with or without the
    // bridge up; everything else brings it up (0x4185FA).
    bool quiet = command == "move" || command == "hidefleet" || command == "setfleetflag" ||
                 command == "setobjectflag" || command == "settext" || command == "fleetalert" ||
                 command == "ontransport" || command == "ontactical" || command == "enablesim" ||
                 command == "disablesim" || command == "clueWave";
    if (!quiet && !_shown) {
        _shown = true;
        startAmbience();
        updateButtons();
    }

    if (command == "showstellar") {
        // Only brings the bridge up.
    } else if (command == "select") {
        select(arg(args, 0));
    } else if (command == "zoomin") {
        zoomIn(findSun(arg(args, 0)));
    } else if (command == "zoomout") {
        int sun = findSun(arg(args, 0));
        if (sun < 0 || sun != _system)
            zoomOut();
    } else if (command == "move") {
        std::string what = lower(arg(args, 0));
        if (what == "enterprise")
            moveEnterprise(findSun(arg(args, 1)));
        else if (what == "soran")
            moveSoran(arg(args, 1));
        else if (what == "fleet")
            moveFleet(toInt(args, 1), findSun(arg(args, 2)));
    } else if (command == "warpfleetin") {
        warpFleetIn(toInt(args, 0), findSun(arg(args, 1)), arg(args, 2));
    } else if (command == "warpfleetaway") {
        warpFleetAway(toInt(args, 0), arg(args, 1));
    } else if (command == "hidefleet") {
        hideFleet(toInt(args, 0));
    } else if (command == "warpaway") {
        warpAway(findSun(arg(args, 0)));
    } else if (command == "destroy") {
        destroySun(findSun(arg(args, 0)));
    } else if (command == "setfleetflag") {
        int n = toInt(args, 0);
        if (n >= 0 && n <= kFleets) {
            std::set<int> &f = flags(_fleets[n].name);
            if (text::equalsIgnoreCase(arg(args, 2), "true"))
                f.insert(toInt(args, 1));
            else
                f.erase(toInt(args, 1));
        }
    } else if (command == "setobjectflag") {
        std::set<int> &f = flags(arg(args, 0));
        if (text::equalsIgnoreCase(arg(args, 2), "true"))
            f.insert(toInt(args, 1));
        else
            f.erase(toInt(args, 1));
    } else if (command == "settext") {
        setText(arg(args, 0), arg(args, 1), join(args, 2));
    } else if (command == "fleetalert") {
        setFleetAlert(toInt(args, 0), arg(args, 1));
    } else if (command == "queaudio") {
        queue(arg(args, 0), 3);
    } else if (command == "quemusic") {
        // Music only goes in an empty queue (0x4790F9).
        if (_queue.empty())
            queue(arg(args, 0), 2);
    } else if (command == "disablecontrols" || command == "enablecontrols") {
        setControls(command == "enablecontrols");
    } else if (command == "disablesim" || command == "enablesim") {
        _simEnabled = command == "enablesim";
        updateButtons();
    } else if (command == "scanner") {
        std::string mode = lower(join(args, 0));
        int panel = -1;
        if (mode.compare(0, 3, "fed") == 0)
            panel = kPanelFederation;
        else if (mode.compare(0, 6, "female") == 0)
            panel = mode.find("listen") != std::string::npos ? kPanelFemaleListening : kPanelFemaleTalking;
        else if (mode.compare(0, 4, "male") == 0)
            panel = mode.find("listen") != std::string::npos ? kPanelMaleListening : kPanelMaleTalking;
        if (panel >= 0) {
            if (_panelMode < kPanelFederation)
                _panelBeforeComm = _panelMode;
            _panelMode = panel;
        } else if (mode == "normal") {
            _panelMode = _panelBeforeComm;
        }
    } else if (command == "cluewave") {
        _clueWave = arg(args, 0);
        updateButtons();
    } else if (command == "shownexus") {
        _nexusShown = true;
        pickNexus(true);
        if (_nexusState == 1 && _nexusWipe >= 640) {
            _nexusWipe = 0;
            _nexusState = 3;
        }
    } else if (command == "ontransport" || command == "ontactical") {
        _contextMode = command == "ontransport" ? kContextTransport : kContextTactical;
        updateButtons();
    } else if (command == "fadeout" || command == "figit") {
        // Not used by the game's script.
    } else if (command != "cluewave") {
        log::debug(1, "Bridge: %s isn't known", command.c_str());
    }
}

void Bridge::moveEnterprise(int sun) {
    if (_fleets.empty())
        return;
    Fleet &e = _fleets[0];
    e.location = sun;
    e.state = sun >= 0 ? 8 : 0;
    e.frame = 8;
    flags(e.name).erase(6);
    flags(e.name).erase(14);
}

void Bridge::moveFleet(int n, int sun) {
    if (n < 1 || n > kFleets)
        return;
    Fleet &f = _fleets[n];
    f.location = sun;
    f.state = sun >= 0 ? 8 : 0;
    f.frame = 8;
    flags(f.name).erase(6);
}

void Bridge::moveSoran(const std::string &object) {
    if (!_soran.empty())
        flags(_soran).erase(1);
    _soran = object;
    if (!_soran.empty())
        flags(_soran).insert(1);
    updateButtons();
}

void Bridge::warpFleetIn(int n, int sun, const std::string &sound) {
    if (n < 1 || n > kFleets || sun < 0)
        return;
    Fleet &f = _fleets[n];
    // A fleet beaten in combat doesn't come back.
    if (getObjectFlag(f.name, 8) || f.location == sun)
        return;
    if (!sound.empty())
        queue(sound, 3);
    f.location = sun;
    f.state = 1;
    f.frame = 0;
    flags(f.name).insert(6);
}

void Bridge::warpFleetAway(int n, const std::string &sound) {
    if (n < 1 || n > kFleets)
        return;
    Fleet &f = _fleets[n];
    if (getObjectFlag(f.name, 6) || f.location < 0)
        return;
    if (!sound.empty())
        queue(sound, 3);
    f.state = 4;
}

void Bridge::hideFleet(int n) {
    if (n < 1 || n > kFleets)
        return;
    _fleets[n].location = -1;
    _fleets[n].state = 0;
    flags(_fleets[n].name).insert(6);
}

void Bridge::warpAway(int sun) {
    if (sun < 0 || _fleets[0].location != sun)
        return;
    // Somewhere else, not destroyed and with no fleet waiting to attack.
    std::vector<int> choices;
    for (int i = 0; i < (int)_suns.size(); i++) {
        if (i == sun || getObjectFlag(_suns[i].name, 8))
            continue;
        bool hostile = false;
        for (int n = 1; n <= kFleets; n++)
            hostile |= _fleets[n].location == i && _fleets[n].alert == 2;
        if (!hostile)
            choices.push_back(i);
    }
    if (!choices.empty())
        setCourse(choices[_random.getRandomNumber((uint32_t)choices.size() - 1)], true);
}

void Bridge::destroySun(int sun) {
    if (sun < 0)
        return;
    flags(_suns[sun].name).insert(8);
    flags(_suns[sun].name).insert(5);
    _suns[sun].destructTime = 0;
    playEffect("Sound\\SoundFX\\SimDest.wav");
    pickNexus(true);
    updateButtons();
}

void Bridge::setText(const std::string &kind, const std::string &object, const std::string &text) {
    std::string k = lower(kind);
    const char *key = k == "dead"    ? "DeadScanText"
                      : k == "long"  ? "LongScan"
                      : k == "short" ? "ShortScan"
                                     : "SoranScan";
    _ini.set(object, key, text);
}

void Bridge::setFleetAlert(int n, const std::string &level) {
    if (n < 0 || n > kFleets)
        return;
    std::string l = lower(level);
    _fleets[n].alert = l == "red" ? 2 : l == "yellow" ? 1 : 0;
    _ini.set(_fleets[n].name, "Alert", level);
}

void Bridge::setControls(bool enabled) {
    _controls = enabled;
    updateButtons();
}

void Bridge::select(const std::string &object) {
    _selected = object;
    updateButtons();
}

void Bridge::setCourse(int sun, bool fromScript) {
    Fleet &e = _fleets[0];
    if (sun < 0)
        return;
    flags(_suns[sun].name).insert(5);
    if (!fromScript)
        playEffect("Sound\\SoundFX\\Beep3.wav");
    std::string sound = _ini.get(_suns[sun].name, "GotoSound");
    if (!sound.empty())
        queue(sound, 3);
    e.destination = sun;
    flags(e.name).insert(14);
    e.state = 4;
    // The context button keeps its mode while travelling, as the original's:
    // only OnTransport and OnTactical set it (0x418349 and 0x4182FC, through
    // 0x474149); the button is off meanwhile only because the controls are.
    _busy = true;
    setControls(false);
}

void Bridge::zoomIn(int sun) {
    if (sun < 0 || (_mapMode == kSystemView && _system == sun))
        return;
    _system = sun;
    double x;
    double y;
    double z;
    sunPosition(sun, x, y, z);
    double minZ = _ini.getDouble("Display", "MinZ", 200);
    _cameraTarget[0] = x;
    _cameraTarget[1] = y;
    _cameraTarget[2] = z + minZ;
    for (int i = 0; i < 3; i++)
        _cameraStep[i] = (_cameraTarget[i] - _camera[i]) / kZoomMillis;
    _zoomLeft = kZoomMillis;
    _mapMode = kZoomingIn;
    playEffect("Sound\\SoundFX\\Beep16.wav");
}

void Bridge::zoomOut() {
    if (_mapMode == kSectorView)
        return;
    double maxZ = _ini.getDouble("Display", "MaxZ", 1000);
    _cameraTarget[0] = 639 * maxZ / 2;
    _cameraTarget[1] = 361 * maxZ / 2;
    _cameraTarget[2] = maxZ;
    for (int i = 0; i < 3; i++)
        _cameraStep[i] = (_cameraTarget[i] - _camera[i]) / kZoomMillis;
    _zoomLeft = kZoomMillis;
    _mapMode = kZoomingOut;
    playEffect("Sound\\SoundFX\\Beep20.wav");
}

// Sound

void Bridge::queue(const std::string &path, int type) {
    if (path.empty())
        return;
    _queue.push_back({path, type});
    _idleMusic = 0;
    // A voice fades out music playing (0x478C7C).
    if (type == 3 && _queuePlaying && _queue.front().type == 2 && _queueLeft == 0)
        _queueLeft = 1000;
}

void Bridge::playEffect(const std::string &path) {
    if (!_mixer)
        return;
    std::unique_ptr<SoundStream> stream = _resources.loadSound({kArchive, path});
    if (stream)
        _mixer->playStream(Mixer::kSFXSoundType, &_effect, std::move(stream));
}

void Bridge::startAmbience() {
    if (!_mixer || _mixer->isSoundHandleActive(_ambience))
        return;
    std::unique_ptr<SoundStream> stream = _resources.loadSound({kArchive, "Sound\\SoundFX\\Stellar.wav"});
    if (stream)
        _mixer->playStream(Mixer::kSFXSoundType, &_ambience, makeLooping(std::move(stream), 0));
}

void Bridge::updateQueue(uint32_t ms) {
    if (_queuePlaying) {
        bool done = !_mixer || !_mixer->isSoundHandleActive(_queueHandle);
        if (_queueLeft > 0) {
            // Fading the music out for a voice.
            _queueLeft = ms >= _queueLeft ? 0 : _queueLeft - ms;
            if (_mixer)
                _mixer->setChannelVolume(_queueHandle, (uint8_t)(255 * _queueLeft / 1000));
            if (_queueLeft == 0) {
                if (_mixer)
                    _mixer->stopHandle(_queueHandle);
                done = true;
            }
        }
        if (!done)
            return;
        _queuePlaying = false;
        _subtitle.clear();
        if (!_queue.empty())
            _queue.pop_front();
    }
    if (_queue.empty()) {
        // Every minute with nothing queued, now and then some music.
        _idleMusic += ms;
        if (_idleMusic >= 60000) {
            _idleMusic = 0;
            _idleToggle = !_idleToggle;
            if (_idleToggle)
                queue(text::format("Loop%d.wav", 1 + (int)_random.getRandomNumber(3)), 2);
        }
        return;
    }

    const QueueItem &item = _queue.front();
    // Music is in MUSIC.1/2 unless a path says otherwise.
    ResourceRef ref = {kArchive, item.path};
    if (item.type == 2 && item.path.find('\\') == std::string::npos) {
        ref.archive.clear();
        for (const char *archive : {"MUSIC.1", "MUSIC.2"}) {
            if (_resources.hasResource({archive, text::toUpper(item.path)})) {
                ref = {archive, text::toUpper(item.path)};
                break;
            }
        }
    }
    std::unique_ptr<SoundStream> stream;
    if (!ref.archive.empty() && _resources.hasResource(ref))
        stream = _resources.loadSound(ref);
    if (!stream || !_mixer) {
        if (!stream)
            log::warning("Bridge: no sound %s", item.path.c_str());
        _queue.pop_front();
        return;
    }
    _mixer->playStream(item.type == 2 ? Mixer::kMusicSoundType : Mixer::kSpeechSoundType, &_queueHandle,
                       std::move(stream));
    _queuePlaying = true;
    _queueLeft = 0;
    _subtitle = item.type == 3 ? _resources.loadSubtitle(ref) : std::string();
    if (!_subtitle.empty())
        log::debug(1, "%s", _subtitle.c_str());
}

void Bridge::hide() {
    if (_mixer) {
        _mixer->stopHandle(_queueHandle);
        _mixer->stopHandle(_ambience);
        _mixer->stopHandle(_effect);
    }
    _queue.clear();
    _queuePlaying = false;
    _subtitle.clear();
    _shown = false;
}

// Simulation

void Bridge::update(uint32_t ms) {
    if (!_shown)
        return;
    ms = std::min<uint32_t>(ms, 200);
    _time += ms;
    updateQueue(ms);

    // The stardate climbs a tenth every half second (0x47267A).
    _stardateLit = _stardateLit > ms ? _stardateLit - ms : 0;
    if (_targetStardate > _stardate + 1e-6) {
        _stardateTimer += ms;
        if (_stardateTimer >= kStardateStep) {
            _stardateTimer = 0;
            _stardate = std::min(_targetStardate, _stardate + 0.1);
            _stardateLit = 380;
            playEffect("Sound\\SoundFX\\TimeBeep.wav");
        }
    } else {
        _targetStardate = _stardate;
        _stardateTimer = 0;
    }

    updateFleets(ms);
    updateMap(ms);
    updateAnimations(ms);

    if (_panelMode == kPanelScanning) {
        _scanTime += ms;
        if (_scanTime >= kScanFrames * kScanFrameMillis)
            finishScan();
    }
    if (_pendingCombat && _queue.empty()) {
        Event event;
        event.type = Event::kCombat;
        event.fleet = _pendingCombat;
        _pendingCombat = 0;
        _events.push_back(event);
    }
    updateButtons();
}

void Bridge::updateFleets(uint32_t ms) {
    for (int i = 0; i < (int)_fleets.size(); i++) {
        Fleet &f = _fleets[i];
        f.timer += ms;
        switch (f.state) {
        case 1: // waiting for the queue, then warping in
            if (_queue.empty())
                f.state = 2;
            break;
        case 2:
            flags(f.name).erase(6);
            playEffect(i == 0 ? "Sound\\SoundFX\\EWarpIn.wav" : "Sound\\SoundFX\\Warp.wav");
            f.state = 3;
            f.timer = 0;
            f.frame = 0;
            break;
        case 3:
            f.frame = (int)(f.timer / 100);
            if (f.frame >= 9) {
                f.frame = 8;
                f.state = 8;
                f.timer = 0;
                if (i == 0)
                    arrive();
            }
            break;
        case 4: // waiting for the queue, then warping out
            if (_queue.empty())
                f.state = 5;
            break;
        case 5: {
            const char *sound = f.kind == "ENT"   ? "EWarpOut"
                                : f.kind == "WAR" ? "RWarpOut"
                                : f.kind == "CHO" ? "CWarpOut"
                                                  : "KWarpOut";
            playEffect(text::format("Sound\\SoundFX\\%s.wav", sound));
            f.state = 6;
            f.timer = 0;
            f.frame = 0;
            break;
        }
        case 6:
            f.frame = (int)(f.timer / 100);
            if (f.frame >= 17) {
                f.location = -1;
                f.timer = 0;
                if (i == 0) {
                    // In transit: a stardate, and there (0x469D86).
                    f.state = 7;
                    f.arrival = _stardate + 0.9;
                    addTime(1.0);
                } else {
                    flags(f.name).insert(6);
                    f.state = 0;
                }
            }
            break;
        case 7:
            if (_stardate >= f.arrival - 1e-6) {
                f.location = f.destination;
                f.state = 2;
            }
            break;
        case 8:
            // Idle: the last two frames, in turn.
            f.frame = (f.timer / 800) % 2 ? 7 : 8;
            break;
        default:
            break;
        }
    }
}

void Bridge::arrive() {
    Fleet &e = _fleets[0];
    flags(e.name).erase(14);
    _busy = false;
    setControls(true);
    // A fleet on red alert attacks at once.
    for (int n = 1; n <= kFleets; n++) {
        const Fleet &f = _fleets[n];
        if (f.location != e.location || f.alert != 2 || getObjectFlag(f.name, 8) || getObjectFlag(f.name, 6))
            continue;
        queue("Sound\\SoundFX\\CommBoop.wav", 1);
        bool riker = _random.getRandomNumber(1) == 0;
        queue(riker ? text::format("Sound\\Voices\\Rike%04d.wav", 238 + (int)_random.getRandomNumber(8))
                    : text::format("Sound\\Voices\\Worf%04d.wav", 124 + (int)_random.getRandomNumber(8)),
              3);
        setControls(false);
        _pendingCombat = n;
        break;
    }
}

void Bridge::updateMap(uint32_t ms) {
    if (_zoomLeft > 0) {
        uint32_t step = std::min(ms, _zoomLeft);
        for (int i = 0; i < 3; i++)
            _camera[i] += _cameraStep[i] * step;
        _zoomLeft -= step;
        if (_zoomLeft == 0) {
            for (int i = 0; i < 3; i++)
                _camera[i] = _cameraTarget[i];
            _mapMode = _mapMode == kZoomingIn ? kSystemView : kSectorView;
        }
    }
    for (Sun &sun : _suns) {
        if (getObjectFlag(sun.name, 8) || getObjectFlag(sun.name, 7))
            sun.destructTime += ms;
        else
            sun.destructTime = 0;
        for (Planet &planet : sun.planets) {
            planet.angle += ms * 2 * kPi / (1000 * std::max(1.0, planet.period));
            for (Planet::Moon &moon : planet.moons)
                moon.angle += ms * 2 * kPi / (1000 * moon.period);
        }
    }

    // The Nexus wipes its new path in.
    if (_nexusState != 1) {
        _nexusWipe += std::max(1, std::min(32, (int)ms * 640 / 1000));
        if (_nexusWipe >= 640) {
            if (_nexusState == 2) {
                _nexusState = 3;
                _nexusWipe = 0;
                playEffect("Sound\\SoundFX\\Nexus2.wav");
            } else {
                _nexusState = 1;
                _nexusWipe = 640;
            }
        }
    } else if (_nexusShown && _nexusGraphic != 1 && !_nexusSpoken && _queue.empty()) {
        // The first time the path moves (0x46950C).
        queue("Sound\\Voices\\Data0127.wav", 3);
        queue("Sound\\Voices\\Pica0138.wav", 3);
        _campaign.setFlag(12, true);
        _nexusSpoken = true;
    }
}

void Bridge::updateAnimations(uint32_t ms) {
    // Data and Picard: a sequence, then 5 to 15 seconds still (0x46EFB7).
    for (Figure &figure : _figures) {
        if (figure.sequences.empty())
            continue;
        figure.timer += ms;
        if (!figure.playing) {
            if (figure.timer >= figure.wait) {
                int count = (int)figure.sequences.size();
                int next = (int)_random.getRandomNumber(count - 1);
                if (count > 1 && next == figure.current)
                    next = (next + 1) % count;
                figure.current = next;
                figure.step = 0;
                figure.timer = 0;
                figure.playing = true;
            }
            continue;
        }
        const Figure::Sequence &sequence = figure.sequences[figure.current];
        while (figure.step < (int)sequence.steps.size() &&
               figure.timer >= (uint32_t)sequence.steps[figure.step].second) {
            figure.timer -= sequence.steps[figure.step].second;
            if (figure.step + 1 >= (int)sequence.steps.size()) {
                figure.playing = false;
                figure.wait = 5000 + _random.getRandomNumber(10000);
                figure.timer = 0;
                break;
            }
            figure.step++;
        }
    }
    // The comm portraits talk and blink.
    _commTime += ms;
    if (_commTime >= 120) {
        _commTime = 0;
        bool talking = _panelMode == kPanelMaleTalking || _panelMode == kPanelFemaleTalking;
        _mouth = talking ? (int)_random.getRandomNumber(4) : 0;
        _eyes = _random.getRandomNumber(20) == 0 ? 1 : 0;
    }
}

void Bridge::pickNexus(bool speak) {
    if (_suns.size() < 10)
        return;
    // sunGone(i): sun i of UNIVERSE.INI destroyed or being simulated.
    auto gone = [this](int i) {
        const std::string &name = _suns[i - 1].name;
        return getObjectFlag(name, 8) || getObjectFlag(name, 7);
    };
    int pair = (gone(1) ? 1 : 0) + (gone(4) ? 1 : 0) + (gone(6) ? 1 : 0);
    int graphic;
    if (gone(5) || gone(9) || gone(7))
        graphic = 8;
    else if (gone(3) || gone(8))
        graphic = 7;
    else if (_campaign.getFlag(13) && gone(10))
        graphic = 5;
    else if (_campaign.getFlag(8) && gone(10))
        graphic = 4;
    else if (gone(10) && pair >= 2)
        graphic = 5;
    else if (pair >= 2)
        graphic = 3;
    else if (gone(1))
        graphic = 2;
    else if (gone(2))
        graphic = 6;
    else
        graphic = 1;

    bool crosses = graphic == 4 || graphic == 5;
    if (speak && crosses && !_campaign.getFlag(10)) {
        queue("Sound\\Voices\\Comp0162.wav", 3);
        queue(text::format("Sound\\SoundFX\\SSoran%d.wav", 1 + (int)_random.getRandomNumber(4)), 2);
    }
    _campaign.setFlag(10, crosses);
    if (graphic != _nexusGraphic) {
        if (speak && _nexusSpoken)
            queue("Sound\\Voices\\Comp0197.wav", 3);
        _nexusOldGraphic = _nexusGraphic;
        _nexusGraphic = graphic;
        if (_nexusShown) {
            _nexusState = 2;
            _nexusWipe = 0;
            playEffect("Sound\\SoundFX\\Nexus1.wav");
        }
    }
}

void Bridge::startScan(const std::string &object, bool shortRange) {
    if (object.empty()) {
        queue("Sound\\Voices\\Data0211.wav", 3);
        return;
    }
    _scanObject = object;
    _scanShort = shortRange;
    _scanTime = 0;
    _panelMode = kPanelScanning;
    _textLines.clear();
    playEffect("Sound\\SoundFX\\Scan.wav");
    if (!shortRange && ++_longScans % 5 == 0)
        queue("Sound\\Voices\\Data0114.wav", 3);
}

void Bridge::finishScan() {
    _panelMode = kPanelResult;
    addTime(0.1);
    flags(_scanObject).insert(5);

    // The text (0x4643E1): a destroyed star's, Soran's, or the range's.
    std::string section = _scanObject;
    int fleet = findFleet(section);
    std::string text;
    if (findSun(section) >= 0 && getObjectFlag(section, 8))
        text = _ini.get(section, "DeadScanText");
    else if (!_scanShort || (fleet > 0 && _fleets[fleet].alert == 2))
        text = _ini.get(section, "LongScan");
    else if (getObjectFlag(section, 1) && !_ini.get(section, "SoranScan").empty())
        text = _ini.get(section, "SoranScan");
    else
        text = _ini.get(section, "ShortScan");
    _textLines.clear();
    for (const std::string &paragraph : text::split(text, '~')) {
        for (const std::string &line : wrapText(paragraph, kTextArea.width()))
            _textLines.push_back(line);
    }
    _textScroll = 0;

    if (_scanShort && getObjectFlag(_scanObject, 1)) {
        _campaign.setFlag(11, true);
        queue(text::format("Sound\\Voices\\Data%04d.wav", 217 + (int)_random.getRandomNumber(2)), 3);
        playEffect("Sound\\SoundFX\\Enhance.wav");
    }
    updateButtons();
}

void Bridge::simulate(int sun) {
    if (sun < 0)
        return;
    const std::string &name = _suns[sun].name;
    flags(name).insert(5);
    if (getObjectFlag(name, 7)) {
        queue("Sound\\Voices\\Comp0159.wav", 3);
        flags(name).erase(7);
        playEffect("Sound\\SoundFX\\Boop1.wav");
    } else {
        queue(_ini.get(name, "SimDestructionSound"), 3);
        flags(name).insert(7);
        playEffect("Sound\\SoundFX\\Beep14.wav");
    }
    addTime(0.3);
    pickNexus(true);
}

int Bridge::contextState() const {
    // 0x474175: what the context button offers; -1 nothing.
    if (!_controls || _fleets.empty())
        return -1;
    int here = _fleets[0].location;
    if (!_scanObject.empty() && _scanShort && _panelMode == kPanelResult && here >= 0 && sunOf(_scanObject) == here &&
        isEnterpriseIdle()) {
        if (getObjectFlag(_scanObject, 1)) {
            if (_contextMode == kContextTransport)
                return 1;
            if (_contextMode == kContextTactical)
                return 0;
        } else if (findFleet(_scanObject) > 0) {
            return 0;
        }
    }
    return _clueWave.empty() ? -1 : 2;
}

void Bridge::pressContext() {
    switch (contextState()) {
    case 1: {
        setControls(false);
        Event event;
        event.type = Event::kTransport;
        _events.push_back(event);
        break;
    }
    case 0: {
        int fleet = findFleet(_scanObject);
        if (getObjectFlag(_scanObject, 1) || fleet <= 0) {
            Event event;
            event.type = Event::kTactical;
            _events.push_back(event);
        } else {
            setControls(false);
            queue("Sound\\Voices\\Pica0289.wav", 3);
            _pendingCombat = fleet;
        }
        break;
    }
    case 2:
        queue(_clueWave, 3);
        break;
    default:
        break;
    }
}

void Bridge::updateButtons() {
    int here = _fleets.empty() ? -1 : _fleets[0].location;
    int selectedSun = sunOf(_selected);
    bool system = _mapMode == kSystemView || _mapMode == kZoomingIn;
    bool on = _controls && !_busy;
    for (Button &b : _buttons) {
        b.enabled = on;
        b.flash = false;
        switch (b.id) {
        case Button::kView:
            b.state = system && !(selectedSun >= 0 && selectedSun != _system) ? 1 : 0;
            break;
        case Button::kScan:
            b.state = selectedSun >= 0 && selectedSun == here ? 0 : 1;
            break;
        case Button::kSimulate: {
            int sun = findSun(_selected);
            bool simulated = sun >= 0 && getObjectFlag(_suns[sun].name, 7);
            b.state = simulated ? 0 : 1;
            b.enabled = on && _simEnabled && sun >= 0 && !getObjectFlag(_suns[sun].name, 8);
            break;
        }
        case Button::kContext: {
            int state = contextState();
            b.enabled = state >= 0;
            b.state = std::max(0, state);
            b.flash = state == 0 || state == 1;
            break;
        }
        case Button::kOptions:
            b.enabled = !_busy;
            break;
        case Button::kUp:
            b.enabled = _textScroll > 0;
            break;
        case Button::kDown:
            b.enabled = _textScroll + 6 < (int)_textLines.size();
            break;
        default:
            break;
        }
    }
}

// Input

void Bridge::mouseMove(int x, int y) {
    _mouseX = x;
    _mouseY = y;
}

void Bridge::mouseDown(int x, int y) {
    _pressed = -1;
    for (int i = 0; i < (int)_buttons.size(); i++) {
        if (_buttons[i].rect.contains(x, y) && _buttons[i].enabled)
            _pressed = i;
    }
    if (_pressed >= 0 || y >= kMapHeight || !_controls || _busy || _panelMode == kPanelScanning)
        return;
    // A click on the map selects what's under it.
    std::string object = hit(x, y);
    if (!object.empty())
        select(object);
}

void Bridge::mouseUp(int x, int y) {
    int pressed = _pressed;
    _pressed = -1;
    if (pressed < 0 || !_buttons[pressed].rect.contains(x, y) || !_buttons[pressed].enabled)
        return;
    Button &b = _buttons[pressed];
    int here = _fleets[0].location;
    switch (b.id) {
    case Button::kSetCourse: {
        int sun = sunOf(_selected);
        if (sun < 0)
            queue("Sound\\Voices\\Data0211.wav", 3);
        else if (sun == here)
            queue("Sound\\Voices\\Data0210.wav", 3);
        else
            setCourse(sun, false);
        break;
    }
    case Button::kContext:
        pressContext();
        break;
    case Button::kOptions: {
        Event event;
        event.type = Event::kOptions;
        _events.push_back(event);
        break;
    }
    case Button::kView:
        if (b.state == 1)
            zoomOut();
        else if (sunOf(_selected) >= 0)
            zoomIn(sunOf(_selected));
        addTime(0.1);
        break;
    case Button::kSimulate:
        simulate(findSun(_selected));
        break;
    case Button::kScan:
        startScan(_selected, b.state == 0);
        break;
    case Button::kUp:
        _textScroll = std::max(0, _textScroll - 1);
        break;
    case Button::kDown:
        _textScroll++;
        break;
    }
    updateButtons();
}

void Bridge::keyDown(const KeyState &key) {
    switch (key.keycode) {
    case reluva::key::kSpace:
        // Skip what's playing (0x479AD7).
        if (_queuePlaying && _mixer)
            _mixer->stopHandle(_queueHandle);
        break;
    case reluva::key::kUp:
        _textScroll = std::max(0, _textScroll - 1);
        break;
    case reluva::key::kDown:
        if (_textScroll + 6 < (int)_textLines.size())
            _textScroll++;
        break;
    case reluva::key::kPageUp:
        _textScroll = std::max(0, _textScroll - 6);
        break;
    case reluva::key::kPageDown:
        _textScroll = std::max(0, std::min((int)_textLines.size() - 6, _textScroll + 6));
        break;
    case reluva::key::kEscape:
        if (!_busy) {
            Event event;
            event.type = Event::kOptions;
            _events.push_back(event);
        }
        break;
    default:
        break;
    }
    updateButtons();
}

Bridge::Event Bridge::takeEvent() {
    if (_events.empty())
        return Event();
    Event event = _events.front();
    _events.pop_front();
    return event;
}

// Drawing

void Bridge::sunPosition(int sun, double &x, double &y, double &z) const {
    // 0x465A03: Location is a screen position in the sector view.
    double maxZ = _ini.getDouble("Display", "MaxZ", 1000);
    double cx = _ini.getDouble("Display", "CenterX", 316);
    double cy = _ini.getDouble("Display", "CenterY", 120);
    const Sun &s = _suns[sun];
    x = (s.x + 320 - cx) * maxZ;
    y = (s.y + 180 - cy) * maxZ;
    z = s.z * 10;
}

Bridge::Projected Bridge::project(double x, double y, double z, int size) const {
    // 0x46CD96.
    double cx = _ini.getDouble("Display", "CenterX", 316);
    double cy = _ini.getDouble("Display", "CenterY", 120);
    double magnify = _ini.getDouble("Display", "SizeMagnification", 200);
    Projected p;
    double d = std::fabs(z - _camera[2]);
    if (d < 1) {
        p.visible = false;
        p.x = p.y = p.r = 0;
        return p;
    }
    p.x = (int)std::lround(cx + (x - _camera[0]) / d);
    p.y = (int)std::lround(cy + (y - _camera[1]) / d);
    p.r = std::max(1, std::min(63, (int)std::lround(size / 2.0 * magnify / d)));
    p.visible = z < _camera[2];
    return p;
}

void Bridge::planetPosition(const Planet &planet, int sun, double &x, double &y, double &z) const {
    // A tilted circle round the sun, spread by DistanceMagnification
    // (0x464F54).
    double minZ = _ini.getDouble("Display", "MinZ", 200);
    double maxZ = _ini.getDouble("Display", "MaxZ", 1000);
    double magnify = _ini.getDouble("Display", "DistanceMagnification", 400);
    double s = (_camera[2] - minZ) / (maxZ - minZ) + 0.8;
    double px = planet.distance * std::cos(planet.angle);
    double py = planet.distance * std::sin(planet.angle);
    double pz = 0;
    double pitch = planet.pitch * kPi / 180;
    double cross = planet.intersect * kPi / 180;
    double ry = py * std::cos(pitch);
    double rz = py * std::sin(pitch);
    py = ry;
    pz = rz;
    double rx = px * std::cos(cross) - py * std::sin(cross);
    ry = px * std::sin(cross) + py * std::cos(cross);
    sunPosition(sun, x, y, z);
    x += rx * magnify / s;
    y += ry * magnify / s;
    z += pz * magnify / s / 1000;
}

int Bridge::fleetSlot(int n) const {
    int location = _fleets[n].location;
    int slot = 0;
    for (int i = 0; i < n; i++) {
        if (_fleets[i].location == location && _fleets[i].state != 0)
            slot++;
    }
    return slot % 4;
}

void Bridge::fleetPosition(int n, double &x, double &y, double &z) const {
    double maxZ = _ini.getDouble("Display", "MaxZ", 1000);
    sunPosition(_fleets[n].location, x, y, z);
    bool system = _mapMode == kSystemView;
    const int *offset = kSlotOffsets[system ? 1 : 0][fleetSlot(n)];
    x += offset[0] * maxZ;
    y += offset[1] * maxZ;
}

std::string Bridge::hit(int x, int y) const {
    std::string best;
    int bestDistance = 1 << 30;
    auto consider = [&](const std::string &name, const Projected &p, int radius) {
        if (!p.visible)
            return;
        int dx = x - p.x;
        int dy = y - p.y;
        int d = dx * dx + dy * dy;
        if (d <= radius * radius && d < bestDistance) {
            bestDistance = d;
            best = name;
        }
    };
    bool system = _mapMode == kSystemView;
    for (int i = 0; i < (int)_suns.size(); i++) {
        double sx;
        double sy;
        double sz;
        sunPosition(i, sx, sy, sz);
        Projected p = project(sx, sy, sz, _suns[i].size);
        consider(_suns[i].name, p, std::max(p.r, 12));
        if (system && i == _system) {
            for (const Planet &planet : _suns[i].planets) {
                double px;
                double py;
                double pz;
                planetPosition(planet, i, px, py, pz);
                Projected pp = project(px, py, pz, planet.size);
                consider(planet.name, pp, std::max(pp.r, 8));
            }
            if (i == findSun("Amargosa") && _ini.hasSection("SpaceStation")) {
                double maxZ = _ini.getDouble("Display", "MaxZ", 1000);
                Projected ps = project(sx + 40 * maxZ, sy + 30 * maxZ, sz, 32);
                consider("SpaceStation", ps, 16);
            }
        }
    }
    for (int n = 0; n < (int)_fleets.size(); n++) {
        const Fleet &f = _fleets[n];
        if (f.location < 0 || (n > 0 && getObjectFlag(f.name, 6)) || f.state == 0 || f.state == 7)
            continue;
        if (system && f.location != _system)
            continue;
        double fx;
        double fy;
        double fz;
        fleetPosition(n, fx, fy, fz);
        Projected p = project(fx, fy, fz, 16);
        consider(f.name, p, system ? 40 : 18);
    }
    return best;
}

void Bridge::drawText(Canvas &screen, const std::string &text, int x, int y, const Rect *clip) {
    uint32_t palette[256];
    for (int i = 0; i < 256; i++) {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        _fontPalette.getColor(i, r, g, b);
        palette[i] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
    }
    _font.drawStringRgb(screen, text, x, y, palette, clip);
}

std::vector<std::string> Bridge::wrapText(const std::string &text, int width) const {
    return _font.wordWrap(text::trim(text), width);
}

void Bridge::drawDisc(Canvas &screen, const RgbImage &texture, int frame, int cx, int cy, int r, double light) const {
    // A planet: its texture wrapped round a sphere, lit from the sun's side.
    int tw = texture.getWidth();
    int th = texture.getFrameHeight();
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= kMapHeight)
            continue;
        for (int dx = -r; dx <= r; dx++) {
            int x = cx + dx;
            if (x < 0 || x >= screen.w || dx * dx + dy * dy > r * r)
                continue;
            double nx = (double)dx / r;
            double ny = (double)dy / r;
            double nz = std::sqrt(std::max(0.0, 1 - nx * nx - ny * ny));
            double u = 0.5 + std::atan2(nx, nz) / (2 * kPi) + _time / 60000.0;
            double v = 0.5 + std::asin(ny) / kPi;
            int tx = ((int)(u * tw) % tw + tw) % tw;
            int ty = std::max(0, std::min(th - 1, (int)(v * th)));
            uint32_t c = texture.getPixel(frame, tx, ty);
            if (!(c >> 24))
                continue;
            double shade = 0.25 + 0.75 * std::max(0.0, nx * std::cos(light) + nz * 0.5 - ny * std::sin(light) * 0.3);
            shade = std::min(1.0, shade);
            uint32_t rr = (uint32_t)(((c >> 16) & 0xFF) * shade);
            uint32_t gg = (uint32_t)(((c >> 8) & 0xFF) * shade);
            uint32_t bb = (uint32_t)((c & 0xFF) * shade);
            *(uint32_t *)screen.getBasePtr(x, y) = 0xFF000000u | rr << 16 | gg << 8 | bb;
        }
    }
}

void Bridge::drawMap(Canvas &screen) {
    if (const RgbImage *background = image("View\\Backgrnd.bmp", 1, RgbImage::kOpaque))
        background->draw(screen, 0, 0, 0, &kMapArea);

    // The Nexus, wiping in or out.
    if (_nexusShown) {
        auto drawNexus = [&](int graphic, int left, int right) {
            std::string path = _ini.get("Nexus", text::format("Graphic%d", graphic));
            std::vector<std::string> at =
                text::split(_ini.get("Nexus", text::format("Location%d", graphic), "0,0"), ',');
            const RgbImage *nexus = overlay(path, 0, "Nexus\\Nexus.bmp");
            if (!nexus)
                return;
            Rect clip(left, 0, right, kMapHeight);
            nexus->draw(screen, 0, std::atoi(at[0].c_str()), at.size() > 1 ? std::atoi(at[1].c_str()) : 0, &clip);
        };
        if (_nexusState == 2)
            drawNexus(_nexusOldGraphic, _nexusWipe, 640);
        else
            drawNexus(_nexusGraphic, 0, _nexusState == 3 ? _nexusWipe : 640);
    }

    bool system = _mapMode == kSystemView || (_mapMode == kZoomingIn && _zoomLeft < kZoomMillis / 2) ||
                  (_mapMode == kZoomingOut && _zoomLeft > kZoomMillis / 2);
    const RgbImage *names = image("View\\SysNames.bmp", 10);
    for (int i = 0; i < (int)_suns.size(); i++) {
        const Sun &sun = _suns[i];
        double x;
        double y;
        double z;
        sunPosition(i, x, y, z);
        Projected p = project(x, y, z, sun.size);
        if (!p.visible)
            continue;
        bool dead = getObjectFlag(sun.name, 8) || getObjectFlag(sun.name, 7);
        const RgbImage *texture = nullptr;
        int frame = 0;
        if (dead && (int)(sun.destructTime * sun.fps / 1000) < sun.destructFrames) {
            texture = image(sun.destruct, sun.destructFrames);
            frame = (int)(sun.destructTime * sun.fps / 1000);
        } else if (dead) {
            texture = image(sun.destroyed, sun.destroyedFrames);
            frame = (int)(_time * sun.fps / 1000) % std::max(1, sun.destroyedFrames);
        } else {
            texture = image(sun.texture, sun.textureFrames);
            frame = (int)(_time * sun.fps / 1000) % std::max(1, sun.textureFrames);
        }
        if (texture)
            texture->drawScaled(screen, frame, p.x - p.r, p.y - p.r, 2 * p.r, 2 * p.r, &kMapArea);
        // The names under the stars in the sector view, once known.
        if (!system && names && getObjectFlag(sun.name, 5))
            names->draw(screen, 9 - i, p.x - names->getWidth() / 2, p.y + p.r + 2, &kMapArea);

        if (system && i == _system) {
            for (const Planet &planet : sun.planets) {
                double px;
                double py;
                double pz;
                planetPosition(planet, i, px, py, pz);
                Projected pp = project(px, py, pz, planet.size);
                const RgbImage *tex = image(planet.texture, 1, RgbImage::kOpaque);
                if (pp.visible && tex)
                    drawDisc(screen, *tex, 0, pp.x, pp.y, pp.r, std::atan2(p.y - pp.y, p.x - pp.x));
                for (const Planet::Moon &moon : planet.moons) {
                    int mx = pp.x + (int)(std::cos(moon.angle) * (pp.r + moon.distance / 2));
                    int my = pp.y + (int)(std::sin(moon.angle) * (pp.r + moon.distance / 2) * 0.4);
                    const RgbImage *mt = image(moon.texture, 1, RgbImage::kOpaque);
                    if (mt)
                        drawDisc(screen, *mt, 0, mx, my, std::max(1, moon.size / 4), 0);
                }
            }
            if (i == findSun("Amargosa") && _ini.hasSection("SpaceStation")) {
                double maxZ = _ini.getDouble("Display", "MaxZ", 1000);
                Projected ps = project(x + 40 * maxZ, y + 30 * maxZ, z, _ini.getInt("SpaceStation", "Size", 126));
                const RgbImage *station = image(_ini.get("SpaceStation", "TextureGraphic"), 2);
                if (station)
                    station->drawScaled(screen, (_time / 500) % 2, ps.x - ps.r, ps.y - ps.r, 2 * ps.r, 2 * ps.r,
                                        &kMapArea);
            }
        }
    }

    // The fleets, the Enterprise among them.
    for (int n = 0; n < (int)_fleets.size(); n++) {
        const Fleet &f = _fleets[n];
        if (f.location < 0 || f.state == 0 || f.state == 7 || f.state == 1)
            continue;
        if (n > 0 && getObjectFlag(f.name, 6) && f.state != 2 && f.state != 3)
            continue;
        if (system && f.location != _system)
            continue;
        double x;
        double y;
        double z;
        fleetPosition(n, x, y, z);
        Projected p = project(x, y, z, 16);
        bool out = f.state == 5 || f.state == 6;
        int size = system ? 128 : 64;
        std::string path = text::format("Fleet\\%s%s%s.rle", f.kind.c_str(), system ? "L" : "S", out ? "OUT" : "IN");
        const RgbImage *sprite = overlay(path, size, "Fleet\\EntSIn.bmp");
        if (sprite)
            sprite->draw(screen, f.frame, p.x - size / 2, p.y - size / 2, &kMapArea);
    }

    // The selection's ring.
    if (!_selected.empty()) {
        int sun = findSun(_selected);
        int fleet = findFleet(_selected);
        Projected p = {0, 0, 0, false};
        if (sun >= 0) {
            double x;
            double y;
            double z;
            sunPosition(sun, x, y, z);
            p = project(x, y, z, _suns[sun].size);
            p.r = std::max(p.r + 6, 12);
        } else if (fleet >= 0 && fleet < (int)_fleets.size() && _fleets[fleet].location >= 0) {
            double x;
            double y;
            double z;
            fleetPosition(fleet, x, y, z);
            p = project(x, y, z, 16);
            p.r = system ? 40 : 18;
        } else if (system && sunOf(_selected) == _system) {
            for (const Planet &planet : _suns[_system].planets) {
                if (!text::equalsIgnoreCase(planet.name, _selected))
                    continue;
                double x;
                double y;
                double z;
                planetPosition(planet, _system, x, y, z);
                p = project(x, y, z, planet.size);
                p.r += 6;
            }
            if (text::equalsIgnoreCase(_selected, "SpaceStation")) {
                double x;
                double y;
                double z;
                double maxZ = _ini.getDouble("Display", "MaxZ", 1000);
                sunPosition(_system, x, y, z);
                p = project(x + 40 * maxZ, y + 30 * maxZ, z, 32);
                p.r = 24;
            }
        }
        const RgbImage *ring = image(_panelMode == kPanelScanning ? "View\\CurScan.bmp" : "View\\CurIdle.bmp", 2);
        if (p.visible && ring && (system || sun >= 0 || fleet >= 0))
            ring->drawScaled(screen, (_time / 400) % 2, p.x - p.r, p.y - p.r, 2 * p.r, 2 * p.r, &kMapArea);
    }

    // The bridge rail, with Data and Picard (0x46E76F).
    if (const RgbImage *equip = image("View\\Equip.bmp"))
        equip->draw(screen, 0, 155, 196, &kMapArea);
    for (const Figure &figure : _figures) {
        if (figure.sequences.empty())
            continue;
        const Figure::Sequence &sequence = figure.sequences[figure.current];
        int frame =
            sequence.steps.empty() ? 1 : sequence.steps[std::min(figure.step, (int)sequence.steps.size() - 1)].first;
        if (!figure.playing && !sequence.steps.empty())
            frame = sequence.steps.back().first;
        if (const RgbImage *img = image(sequence.graphic, sequence.frames))
            img->draw(screen, frame - 1, sequence.x, sequence.y, &kMapArea);
    }
}

void Bridge::drawPanel(Canvas &screen) {
    if (const RgbImage *back = image("GUI\\GUI-Back.bmp"))
        back->draw(screen, 0, 0, kPanelTop);

    // The buttons: frame = dim + 2 * state + lit (0x46F9CA).
    bool flashOn = (_time / 125) % 2 == 0;
    for (int i = 0; i < (int)_buttons.size(); i++) {
        const Button &b = _buttons[i];
        const RgbImage *img = image(b.file, b.frames);
        if (!img)
            continue;
        int frame;
        if (!b.enabled && b.hasDim)
            frame = 0;
        else
            frame = (b.hasDim ? 1 : 0) + 2 * b.state + ((i == _pressed || (b.flash && flashOn)) && b.enabled ? 1 : 0);
        if (b.id == Button::kUp || b.id == Button::kDown)
            frame = !b.enabled ? 0 : i == _pressed ? 2 : 1;
        img->draw(screen, frame, b.rect.left, b.rect.top);
    }

    // The stardate (Numerals.bmp: 0123456789. six pixels apiece).
    if (const RgbImage *digits = image("GUI\\Numerals.bmp", 2)) {
        // Eight digits fill the field the panel leaves for them.
        std::string text = text::format("%.2f", _stardate);
        int x = kStardateX;
        for (char c : text) {
            int glyph = c == '.' ? 10 : c - '0';
            if (glyph < 0 || glyph > 10)
                continue;
            Rect clip(x, kStardateY, x + 6, kStardateY + digits->getFrameHeight());
            digits->draw(screen, _stardateLit ? 1 : 0, x - glyph * 6, kStardateY, &clip);
            x += 6;
        }
    }

    // The scanner display.
    const Rect scanner(kScannerX, kScannerY, kScannerX + 256, kScannerY + 96);
    switch (_panelMode) {
    case kPanelScanning:
        if (const RgbImage *scan = image("GUI\\SC-Activ.bmp", kScanFrames))
            scan->draw(screen, std::min(kScanFrames - 1, (int)(_scanTime / kScanFrameMillis)), kScannerX, kScannerY,
                       &scanner);
        break;
    case kPanelResult: {
        std::string graphic;
        int frames = 1;
        int fleet = findFleet(_scanObject);
        if (findSun(_scanObject) >= 0 && getObjectFlag(_scanObject, 8)) {
            graphic = _ini.get(_scanObject, "DeadScanGraphic");
        } else if (fleet > 0 && !_ini.has(_scanObject, "ScanGraphic")) {
            graphic = text::format("GUI\\%s%d.bmp", _fleets[fleet].kind.c_str(), _fleets[fleet].ships);
        } else {
            graphic = _ini.get(_scanObject, "ScanGraphic");
            frames = std::max(1, _ini.getInt(_scanObject, "ScanFrames", 1));
        }
        if (const RgbImage *img = image(graphic, frames, RgbImage::kOpaque))
            img->draw(screen, (int)(_time / 500) % frames, kScannerX, kScannerY, &scanner);
        // The readings found (scanner icons, 4 frames each).
        int frame = (int)(_time / 150) % 4;
        if (_scanShort && getObjectFlag(_scanObject, 1) && _campaign.getFlag(1))
            if (const RgbImage *i = image("GUI\\SC-Life.bmp", 4))
                i->draw(screen, frame, 12, kPanelTop + 91);
        if (getObjectFlag(_scanObject, 3) && _campaign.getFlag(3))
            if (const RgbImage *i = image("GUI\\SC-Energ.bmp", 4))
                i->draw(screen, frame, 36, kPanelTop + 91);
        if (_scanShort && getObjectFlag(_scanObject, 2) && _campaign.getFlag(2))
            if (const RgbImage *i = image("GUI\\SC-Trili.bmp", 4))
                i->draw(screen, frame, 60, kPanelTop + 91);
        break;
    }
    case kPanelFederation:
        if (const RgbImage *img = image("GUI\\SCFeder.bmp"))
            img->draw(screen, 0, kScannerX + 63, kScannerY);
        break;
    case kPanelMaleTalking:
    case kPanelMaleListening:
        if (const RgbImage *img = image("GUI\\SCMale.bmp"))
            img->draw(screen, 0, kScannerX, kScannerY + 1);
        if (const RgbImage *img = image("GUI\\SCMEyes.bmp", 2))
            img->draw(screen, _eyes, kScannerX + 111, kScannerY + 24);
        if (const RgbImage *img = image("GUI\\SCMMouth.bmp", 5))
            img->draw(screen, _mouth, kScannerX + 117, kScannerY + 41);
        break;
    case kPanelFemaleTalking:
    case kPanelFemaleListening:
        if (const RgbImage *img = image("GUI\\SCFemale.bmp"))
            img->draw(screen, 0, kScannerX, kScannerY + 1);
        if (const RgbImage *img = image("GUI\\SCFEyes.bmp", 2))
            img->draw(screen, _eyes, kScannerX + 128, kScannerY + 30);
        if (const RgbImage *img = image("GUI\\SCFMouth.bmp", 5))
            img->draw(screen, _mouth, kScannerX + 121, kScannerY + 44);
        break;
    default:
        break;
    }
    if (_panelMode == kPanelResult) {
        int y = kTextArea.top + 2;
        for (int i = _textScroll; i < (int)_textLines.size() && y + _font.getHeight() <= kTextArea.bottom; i++) {
            drawText(screen, _textLines[i], kTextArea.left, y, &kTextArea);
            y += _font.getHeight();
        }
    }

    // Subtitles along the bottom.
    if (_subtitles && !_subtitle.empty()) {
        std::vector<std::string> lines = wrapText(_subtitle, kStatusLine.width() - 8);
        if (!lines.empty())
            drawText(screen, lines[0], (640 - _font.getStringWidth(lines[0])) / 2, kStatusLine.top, &kStatusLine);
    }
}

void Bridge::draw(Canvas &screen) {
    drawMap(screen);
    drawPanel(screen);
}

// Saving

void Bridge::sync(Serializer &s) {
    s.syncAsDouble(_stardate);
    s.syncAsDouble(_targetStardate);
    uint32_t count = (uint32_t)_flags.size();
    s.syncAsUint32(count);
    if (s.isLoading()) {
        _flags.clear();
        if (count > 10000) {
            s.fail();
            return;
        }
        for (uint32_t i = 0; i < count && !s.err(); i++) {
            std::string name;
            s.syncString(name);
            std::vector<int> list;
            s.syncVector(list, [](Serializer &ser, int &v) { ser.syncAsInt(v); });
            _flags[name] = std::set<int>(list.begin(), list.end());
        }
    } else {
        for (auto &entry : _flags) {
            std::string name = entry.first;
            s.syncString(name);
            std::vector<int> list(entry.second.begin(), entry.second.end());
            s.syncVector(list, [](Serializer &ser, int &v) { ser.syncAsInt(v); });
        }
    }
    s.syncString(_soran);
    s.syncString(_clueWave);
    s.syncString(_selected);
    s.syncAsInt(_nexusGraphic);
    s.syncAsBool(_nexusShown);
    s.syncAsInt(_longScans);
    s.syncAsBool(_nexusSpoken);
    s.syncAsBool(_simEnabled);
    s.syncAsBool(_controls);
    s.syncAsInt(_contextMode);
    s.syncCount((uint32_t)_fleets.size());
    for (Fleet &f : _fleets) {
        s.syncAsInt(f.location);
        s.syncAsInt(f.alert);
        s.syncAsInt(f.state);
        s.syncAsInt(f.destination);
    }
    _ini.syncChanges(s);
}

void Bridge::afterLoad() {
    // Fleets on their way somewhere are gone; the Enterprise gets there.
    for (int n = 0; n < (int)_fleets.size(); n++) {
        Fleet &f = _fleets[n];
        if (n == 0 && f.state >= 2 && f.state <= 7) {
            f.location = f.destination;
            f.state = 8;
            flags(f.name).erase(14);
        } else if (f.state >= 1 && f.state <= 6) {
            f.location = -1;
            f.state = 0;
            flags(f.name).insert(6);
        }
        f.frame = 8;
    }
    _nexusOldGraphic = _nexusGraphic;
    _nexusState = 1;
    _nexusWipe = 640;
    _campaign.setFlag(11, false);
    _busy = false;
    _panelMode = kPanelEmpty;
    _scanObject.clear();
    _textLines.clear();
    _pendingCombat = 0;
    _events.clear();
    _mapMode = kSectorView;
    _zoomLeft = 0;
    double maxZ = _ini.getDouble("Display", "MaxZ", 1000);
    _camera[0] = 639 * maxZ / 2;
    _camera[1] = 361 * maxZ / 2;
    _camera[2] = maxZ;
    updateButtons();
}

} // namespace reluva::stgenerations
