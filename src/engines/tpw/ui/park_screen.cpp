// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The HUD's places and the camera's start are measured on the recording:
// docs/engines/tpw/games/simcoaster/systems/park.md.

#include "engines/tpw/ui/park_screen.h"

#include "engines/tpw/formats/sam.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/textures.h"
#include "engines/tpw/ui/opening.h"
#include "engines/tpw/ui/text.h"
#include "engines/tpw/ui/ui_model.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace reluva::tpw {

namespace {

constexpr int kWidth = kOpeningWidth;
constexpr int kHeight = kOpeningHeight;
constexpr uint32_t kFrameMillis = 40;

// The HUD at 640 × 480: the money at the top left, the date in its box at
// the top right.
// The money right of the thermometer, its first digit at x 108 as the
// recording's.
constexpr int kCashX = 65;
constexpr int kCashY = 5;
constexpr Color kHudText{255, 255, 255};
// The money's shadow, down and right of it.
constexpr Color kHudShadow{0, 0, 0};
constexpr int kShadow = 2;

// The camera's start: over the entrance, looking into the park from the
// road, as the recording's first view after the briefing; how far it may
// go in and out.
constexpr float kStartYaw = 0;
constexpr float kStartPitch = 0.45f;
constexpr float kStartDistance = 200;
constexpr float kStartInside = 10; // units past the entrance it looks at
constexpr float kNearest = 60;
constexpr float kFarthest = 1600;

// How fast the keys move it: units a second at the start's distance,
// radians a second, and a factor a second.
constexpr float kPanPerSecond = 240;
constexpr float kTurnPerSecond = 1.6f;
constexpr float kZoomPerSecond = 2.5f;
constexpr float kWheelZoom = 1.15f;

// The fixed items' markers, by kind, until their models are placed.
constexpr Color kEntranceColour{255, 220, 0};
constexpr Color kBoothColour{255, 80, 40};
constexpr Color kBusStopColour{80, 160, 255};
// The visitors' sprites (esprites.wad), whole and their heads alone, by
// the settings' numbering of people (Park::Visitor::person), as their
// comments describe them: the boy, the girl with pigtails, the girl with
// a cap, the boy with a cap; the fat bloke, the tall woman; the old
// biddy, the old bloke.
constexpr std::array<const char *, Park::kPeople> kPersonSprites{
    "esprites/Generic/Kids/SPR_BD",
    "esprites/Generic/Kids/SPR_GB",
    "esprites/Generic/Kids/SPR_GK",
    "esprites/Generic/Kids/SPR_KB",
    "esprites/Generic/Parents/SPR_FA",
    "esprites/Generic/Parents/SPR_MO",
    nullptr,
    nullptr,
    "esprites/Generic/Grandparents/SPR_GM",
    "esprites/Generic/Grandparents/SPR_GR",
};
constexpr std::array<const char *, Park::kPeople> kPersonHeads{
    "esprites/Generic/Kidsheads/Spr_bd",
    "esprites/Generic/Kidsheads/Spr_gb",
    "esprites/Generic/Kidsheads/SPR_GK",
    "esprites/Generic/Kidsheads/SPR_KB",
    "esprites/Generic/Parentsheads/SPR_FA",
    "esprites/Generic/Parentsheads/SPR_MO",
    nullptr,
    nullptr,
    "esprites/Generic/Grandparentsheads/SPR_GM",
    "esprites/Generic/Grandparentsheads/SPR_GR",
};
// What else the people carry and leave about.
constexpr const char *kLitterSprite = "esprites/Generic/Litter/Spr_li";
constexpr const char *kThoughtSprite = "esprites/Generic/Thoughts/Spr_tb";
constexpr const char *kMoreThoughtSprite = "esprites/Generic/Thoughts/Spr_tc";
constexpr const char *kUmbrellaSprite = "esprites/Generic/Umbrellas/Spr_um";
constexpr const char *kBalloonSprite = "esprites/Generic/Balloons/Spr_bl";
constexpr const char *kParticleSprite = "esprites/Generic/Particles/Spr_pa";
// Each thought's bubble, by Park::Thought: its frame of Spr_tb, or of
// Spr_tc past kThoughtFrames (matched by what they show: the 21 visitors'
// bubbles, blue, for the 21 thoughts of THOUGHTS.str).
constexpr int kThoughtFrames = 16;
constexpr std::array<int, 22> kThoughtBubbles{-1, 8, 9, 10, 14, 11, 12, 15, 0,  1,  2,
                                              3,  7, 4, 6,  5,  13, 16, 17, 18, 19, 20};
// A stink bomb's cloud: the particles' white puff growing (animation 3).
constexpr size_t kStinkAnimation = 3;
// The umbrellas' size against their sprite (GenericUmbrellaModifiers.ScalingFactor),
// and how far the stick comes below the height the settings give (guessed).
constexpr float kUmbrellaScale = 1.25f;
constexpr float kUmbrellaBelow = 4.25f;
// A head sprite's tilts: from straight above, by 30 degrees, to from below;
// its 8 headings, the first facing the viewer.
constexpr int kHeadTilts = 7;
constexpr int kHeadHeadings = 8;
// Weather on the screen: so many raindrops, so long, falling so fast and
// slanting so (WeatherEffects.MaxRaindrops in a storm, half in rain);
// snowflakes; a lightning flash so often, so long (all guessed but the most).
constexpr int kStormDrops = 600;
constexpr int kDropLength = 18;
constexpr float kDropPixelsPerSecond = 700;
constexpr float kDropSlant = 0.15f;
constexpr int kSnowflakes = 220;
constexpr float kSnowPixelsPerSecond = 60;
constexpr int kFlakeSize = 10;
constexpr uint64_t kLightningEveryMillis = 7000;
constexpr uint64_t kLightningMillis = 250;
constexpr Color kRainColour{170, 190, 230};
constexpr int kRainAlpha = 170;
constexpr size_t kWalkAnimation = 1;
constexpr size_t kStandAnimation = 0;
// A walking frame each unit walked: a step cycle of 8 units, about one
// and a half of an adult's heights (guessed).
constexpr float kUnitsPerWalkFrame = 1;
// A pixel of a frame, in the world's units: the recording's visitors stand
// about half a path's cell tall, and the settings hold an umbrella over a
// child at 5.7 units (Standard.sam's PersonSpriteUmbrellaModifiers), over
// sprites 50 to 60 pixels tall.
constexpr float kSpritePixelUnits = 0.1f;
constexpr Color kBlockedColour{230, 40, 40}; // a cell the path being laid can't cross
// An attraction's footprint being placed, see-through blue, as the
// recording's at 2:55.
constexpr Color kPlacingColour{40, 110, 230};
constexpr int kPlacingAlpha = 110;
// What it costs, under the money: the recording's at 2:55, yellow, smaller.
constexpr int kPriceY = 57;
constexpr int kPriceRightIn = 4; // its right edge, left of the money's
constexpr int kPriceDrawn = 48;  // the money font's rows drawn, before halving
constexpr Color kPriceColour{255, 222, 0};
// The queue's models, by the pieces they draw.
const std::array<std::pair<Park::QueueModel, const char *>, 7> kQueueModels{{
    {Park::QueueModel::kStraight, "questra.MD2"},
    {Park::QueueModel::kBend, "quebnd1.MD2"},
    {Park::QueueModel::kBendOther, "quebnd2.MD2"},
    {Park::QueueModel::kJoin, "queend.MD2"},
    {Park::QueueModel::kJoinBendSouth, "quebin1.MD2"},
    {Park::QueueModel::kJoinBendEast, "quebin2.MD2"},
    {Park::QueueModel::kDeadEnd, "quedead.MD2"},
}};
constexpr float kRadiansPerDegree = 3.14159265f / 180;
// How far path lies over the ground, in units, and objects' models (their
// bases flat at 0, as the ground) over the path.
constexpr float kPathLift = 0.05f;
constexpr float kObjectLift = 0.1f;
// How far the scripts of the objects there from the start are run on at
// once, past their building (guessed: none's building takes so long).
constexpr uint32_t kSettledMillis = 20000;
// How long an attraction without a script keeps a visitor, as the shops'
// scripts do (`WAIT 3000`).
constexpr uint64_t kUnscriptedUseMillis = 3000;
// The helpers riders' heads are put at, numbered from 1 by their seats:
// "Head01" on most rides, "SpriteHead01" on the robot.
constexpr std::string_view kHeadHelper = "head";
constexpr std::string_view kSpritePrefix = "sprite";
// A bouncy ride's bouncers are at its "Sprite 01" helpers, in the order
// they got on, the sprite's middle there: the helpers stand about this
// far over the cushion they bounce on, and its animation moves them.
constexpr float kSpriteMiddle = 2.5f;
constexpr Color kVisitorColour{255, 255, 255}; // a visitor whose sprite isn't read

void fillRect(Surface &s, const ScreenRect &r, Color c) {
    for (int y = std::max(0, r.y); y < std::min(s.h, r.y + r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(s.w, r.x + r.w); ++x) {
            uint8_t *p = s.row(y) + 4 * x;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
}

// A square at a point of the world, `units` across.
void mark(Surface &screen, const soft3d::OrbitCamera &camera, const gfx::Vec3 &at, float units, Color c) {
    const soft3d::ViewVertex v = camera.toView(toViewerWorld(at));
    if (v.z < camera.nearZ)
        return;
    const soft3d::ScreenVertex s = camera.project(v);
    const int size = std::max(1, int(units * camera.focal / v.z / 2));
    fillRect(screen, {int(s.x) - size, int(s.y) - size, 2 * size, 2 * size}, c);
}

void mark(Surface &screen, const soft3d::OrbitCamera &camera, const Park::Cell &cell, Color c) {
    mark(screen, camera, Park::cellCentre(cell), 4, c);
}

WorldView viewOf(const ParkScreen::Camera &camera) {
    WorldView view;
    view.aimed = true;
    view.target = camera.target;
    view.yaw = camera.yaw;
    view.pitch = camera.pitch;
    view.distance = camera.distance;
    view.objects = false;
    return view;
}

// A model laid from a cell's corner, turned about that cell's middle by
// `angle` degrees, posed by an animation at a frame (−1 for none).
// A model's place in the park: laid from its first cell's corner, turned
// about that cell's middle.
gfx::Mat4 placeOf(const Park::Cell &at, int angle) {
    const float half = Park::kCellUnits / 2;
    const float turn = float(angle) * kRadiansPerDegree;
    const float c = std::cos(turn);
    const float s = std::sin(turn);
    // A positive angle turns east (+x) towards south (+z), as the save's
    // path corners do. Column-major: x' = c x − s z, z' = s x + c z, about
    // the cell's middle.
    const float cx = float(at.x) * Park::kCellUnits + half;
    const float cz = float(at.y) * Park::kCellUnits + half;
    return {c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, cx - (c * half - s * half), kObjectLift, cz - (s * half + c * half),
            1};
}

void drawPlaced(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &eye, const LoadedModel &model,
                const Park::Cell &at, int angle, const soft3d::Vec &light, int animation = -1, float frame = 0) {
    const gfx::Mat4 place = placeOf(at, angle);
    std::vector<gfx::Mat4> transforms = meshTransforms(model, animation, frame);
    for (gfx::Mat4 &m : transforms)
        m = gfx::multiply(place, m);
    const std::vector<std::vector<gfx::Vec3>> positions = meshPositions(model, animation, frame);
    drawMeshes(rasterizer, eye, model, true, light, &transforms, &positions);
}

} // namespace

Error ParkScreen::load(const SearchSet &files, const Park &park) {
    if (Error err = _world.load(files, park.folder()))
        return err;
    _hud = false;
    _furnitureRead = !_furniture.load(files);
    _files = &files;
    // The attractions and their names first: the objects' signs want them.
    _attractions.load(files, park.folder());
    _buyRead = !_buy.load(files);
    loadObjects(files, park);
    _runs.clear();
    _ranTo = park.millis();
    _unscripted.clear();
    _prebuilt = park.objects().size();
    _pathsRead = true;
    for (size_t i = 0; i < kPathTextures.size(); ++i)
        if (loadPicture(files, park.folder() + "/terrain/pathtex/" + kPathTextures[i] + ".fsh", _pathTextures[i]))
            _pathsRead = false;
    for (size_t person = 0; person < Park::kPeople; ++person) {
        _personLooks[person].reset();
        _personHeads[person].reset();
        LoadedSprite sprite;
        if (kPersonSprites[person] && !sprite.load(files, kPersonSprites[person]))
            _personLooks[person] = std::move(sprite);
        LoadedSprite head;
        if (kPersonHeads[person] && !head.load(files, kPersonHeads[person]))
            _personHeads[person] = std::move(head);
    }
    for (auto [sprite, name] :
         {std::pair{&_litterSprite, kLitterSprite}, std::pair{&_thoughtSprite, kThoughtSprite},
          std::pair{&_moreThoughtSprite, kMoreThoughtSprite}, std::pair{&_umbrellaSprite, kUmbrellaSprite},
          std::pair{&_balloonSprite, kBalloonSprite}, std::pair{&_particleSprite, kParticleSprite}}) {
        sprite->reset();
        LoadedSprite loaded;
        if (!loaded.load(files, name))
            *sprite = std::move(loaded);
    }
    for (auto [picture, name] :
         {std::pair{&_snow, "generic/weather/snow.tga"}, std::pair{&_lightning, "generic/weather/lightning.tga"}})
        if (loadPicture(files, name, *picture))
            *picture = {};
    // Where each person holds their umbrella (PersonSpriteUmbrellaModifiers[n].Y).
    SamFile general;
    std::vector<uint8_t> settings;
    _umbrellaHeights.fill(5.7f);
    if (!files.read("levels/Standard.sam", settings) && !general.parse(settings))
        for (size_t person = 0; person < Park::kPeople; ++person)
            _umbrellaHeights[person] = float(general.number(
                "PersonSpriteUmbrellaModifiers[" + std::to_string(person) + "].Y", _umbrellaHeights[person]));
    const std::string language = languageOf(files);
    if (language.empty())
        return Error::success();
    const std::string folder = "Language/" + language + "/";
    std::vector<uint8_t> data;
    _hud = !files.read(folder + "MBToUni.dat", data) && !_characters.parse(data) &&
           !readGameFont(files, folder, "CASHMED", _cashFont) && !readGameFont(files, folder, "DATESMALL", _dateFont) &&

           !readStringTable(files, folder, "MONTHS", _characters, _months);
    return Error::success();
}

bool ParkScreen::cellAt(const Camera &camera, int x, int y, Park::Cell &out) const {
    const soft3d::OrbitCamera eye = worldCamera(_world, viewOf(camera), kWidth, kHeight);
    // The world's axes in the viewer's, to meet the ground's plane.
    const soft3d::Vec origin = toViewerWorld({0, 0, 0});
    const soft3d::Vec alongX = toViewerWorld({1, 0, 0}) - origin;
    const soft3d::Vec alongZ = toViewerWorld({0, 0, 1}) - origin;
    const soft3d::Vec upward = toViewerWorld({0, 1, 0}) - origin;
    const soft3d::Vec ray = eye.forward + eye.right * ((float(x) + 0.5f - eye.halfW) / eye.focal) +
                            eye.up * ((eye.halfH - float(y) - 0.5f) / eye.focal);
    const float rise = soft3d::dot(ray, upward);
    if (rise >= 0)
        return false;
    const soft3d::Vec hit = eye.eye + ray * (soft3d::dot(origin - eye.eye, upward) / rise);
    const float wx = soft3d::dot(hit - origin, alongX) / soft3d::dot(alongX, alongX);
    const float wz = soft3d::dot(hit - origin, alongZ) / soft3d::dot(alongZ, alongZ);
    out = {int(std::floor(wx / Park::kCellUnits)), int(std::floor(wz / Park::kCellUnits))};
    return true;
}

// The objects' models, by their numbers: each object's folder of the
// world's (`rides/robot/`) has its settings, `robot.sam`, its number their
// `Info.Id`, and its model, `robot.MD2`. Only those standing in the park
// are read.
void ParkScreen::loadObjects(const SearchSet &files, const Park &park) {
    _objectModels.clear();
    _queueModels.clear();
    std::set<int> wanted;
    for (const Park::PlacedObject &o : park.objects())
        wanted.insert(o.id);
    const std::string prefix = park.folder() + "/";
    for (const std::string &name : files.list()) {
        if (wanted.empty())
            break;
        if (name.size() < prefix.size() + 4 || name.compare(0, prefix.size(), prefix) != 0 ||
            name.compare(name.size() - 4, 4, ".sam") != 0)
            continue;
        // <kind>/<object>/<object>.sam
        const std::string rest = name.substr(prefix.size(), name.size() - prefix.size() - 4);
        const size_t a = rest.find('/');
        const size_t b = a == std::string::npos ? a : rest.find('/', a + 1);
        if (b == std::string::npos || rest.find('/', b + 1) != std::string::npos ||
            rest.compare(a + 1, b - a - 1, rest, b + 1, std::string::npos) != 0)
            continue;
        std::vector<uint8_t> data;
        SamFile settings;
        if (files.read(name, data) || settings.parse(data))
            continue;
        const int id = int(settings.number("Info.Id", -1));
        if (!wanted.count(id))
            continue;
        LoadedModel model;
        const std::string path = name.substr(0, name.size() - 4) + ".MD2";
        if (!model.load(files, path)) {
            letterSign(id, path, model);
            _objectModels.emplace(id, std::move(model));
            _objectPaths[id] = path;
            wanted.erase(id);
        }
    }
    for (const auto &[piece, file] : kQueueModels) {
        LoadedModel model;
        if (!model.load(files, prefix + "queue/" + file))
            _queueModels.emplace(int(piece), std::move(model));
    }
}

void ParkScreen::letterSign(int id, const std::string &path, LoadedModel &model) const {
    const Attractions::Attraction *a = _attractions.find(id);
    if (_files && a && _buyRead)
        _signs.letter(*_files, path, _buy.names().lines(*a), model);
}

// One placed since the park opened is read as it's first wanted.
const LoadedModel *ParkScreen::objectModel(int id) const {
    auto model = _objectModels.find(id);
    if (model == _objectModels.end() && _files && !_missingModels.count(id)) {
        const Attractions::Attraction *a = _attractions.find(id);
        LoadedModel loaded;
        if (a && !a->model.empty() && !loaded.load(*_files, a->model)) {
            letterSign(id, a->model, loaded);
            model = _objectModels.emplace(id, std::move(loaded)).first;
            _objectPaths[id] = a->model;
        } else {
            _missingModels.insert(id);
        }
    }
    return model == _objectModels.end() ? nullptr : &model->second;
}

// Each object's script, beside its model, run on the stand-in for the park
// without its guests, the park's visitors not riding yet: it builds itself
// (its animation 0), then plays what it plays while it waits. Those there
// as the park opens are run on past their building.
void ParkScreen::advance(Park &park) {
    const uint32_t ms = uint32_t(park.millis() - std::min(park.millis(), _ranTo));
    _ranTo = park.millis();
    for (size_t i = _runs.size(); i < park.objects().size(); ++i) {
        std::unique_ptr<RideRun> run;
        const int id = park.objects()[i].id;
        if (objectModel(id) && _files) {
            run = std::make_unique<RideRun>();
            if (run->start(*_files, _objectPaths[id], false))
                run.reset();
            else if (i < _prebuilt)
                run->advance(kSettledMillis);
        }
        _runs.push_back(std::move(run));
    }
    // Each ride's script takes the visitor waiting at its entrance when
    // it will, and lets its riders off.
    for (size_t i = 0; i < _runs.size(); ++i) {
        RideRun *run = _runs[i].get();
        const int queue = park.queueOf(i);
        if (!run) {
            // Without a script: each visitor in turn, a few seconds.
            if (queue < 0)
                continue;
            std::vector<std::pair<int, uint64_t>> &users = _unscripted[queue];
            if (users.empty())
                if (const int waiting = park.waitingAt(queue); waiting && park.board(queue, waiting))
                    users.emplace_back(waiting, park.millis() + kUnscriptedUseMillis);
            while (!users.empty() && users.front().second <= park.millis()) {
                park.letOff(queue, users.front().first);
                users.erase(users.begin());
            }
            continue;
        }
        SimulatedHost *host = run->host();
        if (queue >= 0 && host && !host->hasOffered()) {
            if (const int waiting = park.waitingAt(queue))
                host->offerGuest(waiting);
        }
        run->advance(ms);
        if (queue < 0 || !host)
            continue;
        for (int id : host->takeBoarded())
            park.board(queue, id);
        for (int id : host->takeLetOff())
            park.letOff(queue, id);
    }
}

void ParkScreen::drawObjects(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &eye, const Park &park) const {
    const soft3d::Vec light = _world.hasSky ? soft3d::normalize(toViewerWorld(_world.sky.light)) : defaultLight();
    for (size_t i = 0; i < park.objects().size(); ++i) {
        const Park::PlacedObject &o = park.objects()[i];
        const LoadedModel *model = objectModel(o.id);
        if (!model)
            continue;
        ModelView pose;
        if (i < _runs.size() && _runs[i])
            _runs[i]->pose(*model, pose);
        drawPlaced(rasterizer, eye, *model, o.at, o.angle, light, pose.animation, pose.frame);
    }
    // The queue being laid drawn as it would be, in its own's place.
    for (const Park::Queue &queue : park.queues()) {
        if (queue.kiosk)
            continue;
        const bool laying = _queuePreview && _queuePreview->entrance == queue.entrance;
        for (const Park::QueuePiece &q : Park::queuePieces(laying ? *_queuePreview : queue)) {
            const auto model = _queueModels.find(int(q.model));
            if (model != _queueModels.end())
                drawPlaced(rasterizer, eye, model->second, q.at, q.angle, light);
        }
    }
}

// Each cell a quad on the ground, filled over what's drawn, see-through.
void ParkScreen::drawPlacement(Surface &screen, const soft3d::OrbitCamera &eye) const {
    if (!_placement)
        return;
    const Color c = _placementFits ? kPlacingColour : kBlockedColour;
    for (const Park::Cell &cell : _placement->cells) {
        std::array<soft3d::ScreenVertex, 4> corner;
        bool seen = true;
        const std::array<std::array<int, 2>, 4> at{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
        for (size_t i = 0; i < 4; ++i) {
            const soft3d::ViewVertex v =
                eye.toView(toViewerWorld({float(cell.x + at[i][0]) * Park::kCellUnits, kObjectLift,
                                          float(cell.y + at[i][1]) * Park::kCellUnits}));
            seen = seen && v.z >= eye.nearZ;
            if (seen)
                corner[i] = eye.project(v);
        }
        if (!seen)
            continue;
        float top = corner[0].y;
        float bottom = corner[0].y;
        for (const auto &p : corner) {
            top = std::min(top, p.y);
            bottom = std::max(bottom, p.y);
        }
        for (int y = std::max(0, int(std::ceil(top))); y < std::min(screen.h, int(std::ceil(bottom))); ++y) {
            // The quad's span on this row, between its edges crossing it.
            float left = 1e9f;
            float right = -1e9f;
            for (size_t i = 0; i < 4; ++i) {
                const auto &a = corner[i];
                const auto &b = corner[(i + 1) % 4];
                if ((a.y <= float(y) && b.y > float(y)) || (b.y <= float(y) && a.y > float(y))) {
                    const float x = a.x + (float(y) - a.y) / (b.y - a.y) * (b.x - a.x);
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            }
            for (int x = std::max(0, int(std::ceil(left))); x < std::min(screen.w, int(std::ceil(right))); ++x) {
                uint8_t *p = screen.row(y) + x * 4;
                p[0] = uint8_t((p[0] * (255 - kPlacingAlpha) + c.r * kPlacingAlpha) / 255);
                p[1] = uint8_t((p[1] * (255 - kPlacingAlpha) + c.g * kPlacingAlpha) / 255);
                p[2] = uint8_t((p[2] * (255 - kPlacingAlpha) + c.b * kPlacingAlpha) / 255);
            }
        }
    }
}

bool ParkScreen::cellOnScreen(const Camera &camera, const Park::Cell &cell, int &x, int &y) const {
    const soft3d::OrbitCamera eye = worldCamera(_world, viewOf(camera), kWidth, kHeight);
    const soft3d::ViewVertex v = eye.toView(toViewerWorld(Park::cellCentre(cell)));
    if (v.z < eye.nearZ)
        return false;
    const soft3d::ScreenVertex s = eye.project(v);
    x = int(s.x);
    y = int(s.y);
    return true;
}

// Path is drawn as the ground's cells are, flat, a little over the ground
// so as not to fight it for the nearer.
void ParkScreen::drawPaths(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &eye, const Park &park) const {
    if (!_pathsRead)
        return;
    const soft3d::Vec light = _world.hasSky ? soft3d::normalize(toViewerWorld(_world.sky.light)) : defaultLight();
    const int w = park.groundWidth();
    const int h = park.groundHeight();
    std::vector<uint8_t> laid(size_t(w) * size_t(h), 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            laid[size_t(y) * size_t(w) + size_t(x)] = park.isPath({x, y});
    for (const Park::Cell &c : _preview)
        if (park.ground(c) != Park::Ground::kOutside)
            laid[size_t(c.y) * size_t(w) + size_t(c.x)] = 1;
    auto pathAt = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < w && y < h && laid[size_t(y) * size_t(w) + size_t(x)];
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!pathAt(x, y))
                continue;
            uint8_t around = 0;
            const std::array<std::pair<std::array<int, 2>, uint8_t>, 8> sides{{
                {{0, -1}, kPathN},
                {{1, 0}, kPathE},
                {{0, 1}, kPathS},
                {{-1, 0}, kPathW},
                {{1, -1}, kPathNE},
                {{1, 1}, kPathSE},
                {{-1, 1}, kPathSW},
                {{-1, -1}, kPathNW},
            }};
            for (const auto &[d, bit] : sides)
                if (pathAt(x + d[0], y + d[1]))
                    around |= bit;
            const PathTile tile = pathTileFor(around, uint32_t(x * 73856093) ^ uint32_t(y * 19349663));
            // A corner of the cell, and the texture's point drawn there, the
            // texture turned so many quarters clockwise; u along its columns,
            // v up its rows, as the rasterizer has them.
            auto corner = [&](int cx, int cy) {
                float u = float(cx);
                float v = float(cy);
                for (int t = 0; t < tile.turns; ++t) {
                    const float was = u;
                    u = v;
                    v = 1 - was;
                }
                return std::pair{
                    toViewerWorld({float(x + cx) * Park::kCellUnits, kPathLift, float(y + cy) * Park::kCellUnits}),
                    std::array<float, 2>{u, 1 - v}};
            };
            const auto [a, ua] = corner(0, 0);
            const auto [b, ub] = corner(1, 0);
            const auto [d, ud] = corner(1, 1);
            const auto [e, ue] = corner(0, 1);
            const image::Image *texture = &_pathTextures[tile.texture];
            drawTexturedTriangle(rasterizer, eye, {a, b, d}, {ua, ub, ud}, texture, light);
            drawTexturedTriangle(rasterizer, eye, {a, d, e}, {ua, ud, ue}, texture, light);
        }
}

ParkScreen::Camera ParkScreen::startCamera(const Park &park) const {
    const auto &entrance = park.fixedItems().entrance;
    const gfx::Vec3 a = Park::cellCentre(entrance[0]);
    const gfx::Vec3 b = Park::cellCentre(entrance[1]);
    return {{(a[0] + b[0]) / 2, 0, (a[2] + b[2]) / 2 + kStartInside}, kStartYaw, kStartPitch, kStartDistance};
}

std::string ParkScreen::dateText(uint32_t day) const {
    const Park::Date d = Park::dateOf(day);
    const std::string month =
        _months.size() > size_t(d.month) ? _months.text(size_t(d.month)) : std::to_string(d.month + 1);
    char text[48];
    std::snprintf(text, sizeof text, "%d.%s.%02d", d.day, month.c_str(), d.year % 100);
    return text;
}

// Where each object's riders are, the model posed as it plays: in their
// seats as its script shows them (ADDHEAD), at its head helpers; or
// bouncing (BOUNCE), at its sprite helpers.
std::vector<ParkScreen::Rider> ParkScreen::riders(const Park &park) const {
    std::vector<Rider> out;
    for (size_t i = 0; i < _runs.size() && i < park.objects().size(); ++i) {
        const RideRun *run = _runs[i].get();
        if (!run || !run->host() || (run->host()->heads().empty() && run->host()->bouncers().empty()))
            continue;
        const Park::PlacedObject &o = park.objects()[i];
        const LoadedModel *model = objectModel(o.id);
        if (!model)
            continue;
        ModelView pose;
        run->pose(*model, pose);
        const std::vector<gfx::Mat4> nodes = nodeTransforms(*model, pose.animation, pose.frame);
        const std::vector<int> &heads = run->host()->heads();
        const std::vector<int> bouncers = run->host()->bouncers();
        for (size_t n = 0; n < model->model.nodes().size() && n < nodes.size(); ++n) {
            std::string name;
            for (char c : model->model.nodes()[n].name)
                if (c != ' ')
                    name.push_back(char(std::tolower(static_cast<unsigned char>(c))));
            // "SpriteHead07" and "Head07" a seat; "Sprite 07" a bouncer's place.
            std::string rest = name;
            const bool sprite = rest.compare(0, kSpritePrefix.size(), kSpritePrefix.data(), kSpritePrefix.size()) == 0;
            if (sprite)
                rest = rest.substr(kSpritePrefix.size());
            const bool seat = rest.compare(0, kHeadHelper.size(), kHeadHelper.data(), kHeadHelper.size()) == 0;
            if (seat)
                rest = rest.substr(kHeadHelper.size());
            if ((!seat && !sprite) || rest.empty() || rest.find_first_not_of("0123456789") != std::string::npos)
                continue;
            const size_t place = size_t(std::atoi(rest.c_str()));
            const std::vector<int> &who = seat ? heads : bouncers;
            if (place < 1 || place > who.size() || who[place - 1] == 0)
                continue;
            const gfx::Mat4 placed = gfx::multiply(placeOf(o.at, o.angle), nodes[n]);
            const gfx::Vec3 at = gfx::transformPoint(placed, {0, 0, 0});
            // Facing the model's front, as turned with its seat (guessed).
            const gfx::Vec3 ahead = gfx::transformPoint(placed, {0, 0, -1});
            const float fx = ahead[0] - at[0];
            const float fz = ahead[2] - at[2];
            const float length = std::max(1e-6f, std::sqrt(fx * fx + fz * fz));
            out.push_back(
                {who[place - 1], {at[0], seat ? at[1] : at[1] - kSpriteMiddle, at[2]}, seat, fx / length, fz / length});
        }
    }
    return out;
}

void ParkScreen::drawVisitors(Surface &screen, const soft3d::OrbitCamera &eye, const Park &park,
                              const Camera &camera) const {
    const float yaw = camera.yaw;
    // A frame of a sheet at a point of the world, farthest drawn first.
    struct Seen {
        float depth;
        const SpriteSheet *sheet;
        size_t frame;
        soft3d::ScreenVertex at;
        bool mirror;
        float scale;
    };
    std::vector<Seen> seen;
    auto see = [&](const LoadedSprite &sprite, size_t frame, const gfx::Vec3 &at, bool mirror, float size = 1,
                   float nearer = 0) {
        if (frame >= sprite.sheet.frames().size())
            return;
        const soft3d::ViewVertex view = eye.toView(toViewerWorld(at));
        if (view.z >= eye.nearZ)
            seen.push_back({view.z - nearer, &sprite.sheet, frame, eye.project(view), mirror, size});
    };
    // The way someone faces as the camera sees it: 0 away from it, a quarter turn to its right.
    auto turned = [&](float hx, float hz) {
        const float forward = hx * std::sin(yaw) + hz * std::cos(yaw);
        const float right = hx * std::cos(yaw) - hz * std::sin(yaw);
        return std::atan2(right, forward);
    };

    // What's on the ground, and stink bombs going off.
    if (_litterSprite)
        for (const Park::Litter &l : park.litter())
            see(*_litterSprite, size_t(l.frame), {l.x, 0, l.z}, false, 1, -1);
    if (_particleSprite)
        for (const Park::StinkBomb &b : park.stinkBombs()) {
            const auto &puff = _particleSprite->set.animations()[kStinkAnimation];
            const uint64_t span = std::max<uint64_t>(1, b.until - b.since);
            const int frame = int((park.millis() - b.since) * uint64_t(std::max(1, puff.frames)) / span);
            see(*_particleSprite, size_t(puff.first + std::min(frame, std::max(0, puff.frames - 1))), {b.x, 1, b.z},
                false, 2);
        }

    const std::vector<Rider> seated = riders(park);
    for (const Park::Visitor &v : park.visitorsAbout()) {
        const size_t person = size_t(std::clamp(v.person, 0, int(Park::kPeople) - 1));
        gfx::Vec3 feet{v.x, 0, v.z};
        bool shown = v.doing != Park::Doing::kRiding;
        if (!shown) {
            const auto rider = std::find_if(seated.begin(), seated.end(), [&](const Rider &r) { return r.id == v.id; });
            if (rider != seated.end() && rider->seated) {
                // Seated: their head alone, at the seat, by how the camera
                // looks down on it and the way they face.
                if (_personHeads[person]) {
                    const LoadedSprite &head = *_personHeads[person];
                    const float down = std::clamp(camera.pitch, 0.0f, 1.5707964f);
                    const int tilt =
                        std::clamp(kHeadTilts / 2 - int(std::lround(down / 0.5235988f)), 0, kHeadTilts - 1);
                    // Facing the viewer at the first heading, round to the right.
                    const float away = turned(rider->headingX, rider->headingZ);
                    const int step = int(std::lround((3.14159265f - away) / (3.14159265f / 4)));
                    const int heading = ((step % kHeadHeadings) + kHeadHeadings) % kHeadHeadings;
                    see(head, size_t(head.set.animations()[0].first + tilt * kHeadHeadings + heading), rider->at,
                        false);
                }
                continue;
            }
            if (rider != seated.end()) {
                feet = rider->at;
                shown = true;
            } else {
                const Park::Queue &q = park.queues()[size_t(v.queue)];
                const Attractions::Attraction *a =
                    q.object >= 0 ? _attractions.find(park.objects()[size_t(q.object)].id) : nullptr;
                shown = a && a->handlesSprite;
            }
        }
        if (!shown)
            continue;
        if (!_personLooks[person]) {
            mark(screen, eye, {v.x, 1, v.z}, 2, kVisitorColour);
            continue;
        }
        const LoadedSprite &look = *_personLooks[person];
        const bool walking = v.doing != Park::Doing::kRiding && (v.headingX != 0 || v.headingZ != 0);
        const auto &animation = look.set.animations()[walking ? kWalkAnimation : kStandAnimation];
        const SpritePose pose = spritePose(animation, walking ? turned(v.headingX, v.headingZ) : 3.14159265f,
                                           walking ? uint32_t(v.walked / kUnitsPerWalkFrame) : 0);
        see(look, pose.frame, feet, pose.mirror);
        // What they carry over them, just nearer than they are, and their thought.
        if (v.appearance == 3 && _umbrellaSprite)
            see(*_umbrellaSprite, size_t(v.id) % _umbrellaSprite->sheet.frames().size(),
                {feet[0], feet[1] + _umbrellaHeights[person] - kUmbrellaBelow, feet[2]}, false, kUmbrellaScale, 0.1f);
        if (v.appearance == 1 && _balloonSprite)
            see(*_balloonSprite, size_t(v.id) % _balloonSprite->sheet.frames().size(), feet, false, 1, 0.1f);
        const size_t bubble = size_t(v.thought);
        if (v.thoughtUntil > park.millis() && bubble < kThoughtBubbles.size() && kThoughtBubbles[bubble] >= 0) {
            const int frame = kThoughtBubbles[bubble];
            const std::optional<LoadedSprite> &sheet = frame < kThoughtFrames ? _thoughtSprite : _moreThoughtSprite;
            if (sheet)
                see(*sheet, size_t(frame < kThoughtFrames ? frame : frame - kThoughtFrames), feet, false, 1, 0.2f);
        }
    }
    std::sort(seen.begin(), seen.end(), [](const Seen &a, const Seen &b) { return a.depth > b.depth; });
    for (const Seen &s : seen)
        drawSpriteFrame(screen, *s.sheet, s.frame, s.at.x, s.at.y, s.scale * kSpritePixelUnits * eye.focal / s.depth,
                        s.mirror);
}

// The weather over the park, on the screen: rain falling in streaks, a
// storm's more, with lightning now and then; snow drifting down. Each
// drop and flake by its number, so the same at the same time.
void ParkScreen::drawWeather(Surface &screen, const Park &park) const {
    const Park::Weather weather = park.weather();
    const float seconds = float(park.millis() % 1000000) / 1000;
    auto hash = [](uint32_t n) {
        n = (n ^ 61u) ^ (n >> 16);
        n *= 9u;
        n ^= n >> 4;
        n *= 0x27d4eb2du;
        return n ^ (n >> 15);
    };
    auto blend = [&](int x, int y, Color c, int alpha) {
        if (x < 0 || y < 0 || x >= screen.w || y >= screen.h)
            return;
        uint8_t *p = screen.row(y) + size_t(x) * 4;
        p[0] = uint8_t((c.r * alpha + p[0] * (255 - alpha)) / 255);
        p[1] = uint8_t((c.g * alpha + p[1] * (255 - alpha)) / 255);
        p[2] = uint8_t((c.b * alpha + p[2] * (255 - alpha)) / 255);
    };
    if (weather == Park::Weather::kRain || weather == Park::Weather::kStorm) {
        const int drops = weather == Park::Weather::kStorm ? kStormDrops : kStormDrops / 2;
        const int fall = screen.h + kDropLength;
        for (int i = 0; i < drops; ++i) {
            const uint32_t h = hash(uint32_t(i));
            const float speed = kDropPixelsPerSecond * (0.8f + float(h % 40) / 100);
            const int y = int(float(h >> 8) + seconds * speed) % fall - kDropLength;
            const int x = int(h % uint32_t(screen.w + screen.h)) - int(float(y) * kDropSlant);
            for (int k = 0; k < kDropLength; ++k)
                blend(x - int(float(k) * kDropSlant), y + k, kRainColour, kRainAlpha * k / kDropLength);
        }
    }
    if (weather == Park::Weather::kStorm && _lightning.w > 0) {
        const uint64_t at = park.millis() % kLightningEveryMillis;
        if (at < kLightningMillis) {
            // The bolt down the screen, at a place of its own each time, the sky lit.
            const int x = int(hash(uint32_t(park.millis() / kLightningEveryMillis)) % uint32_t(screen.w));
            for (int y = 0; y < screen.h; ++y)
                for (int dx = 0; dx < _lightning.w / 2; ++dx) {
                    const uint8_t *t = _lightning.pixel(_lightning.w / 4 + dx,
                                                        std::min(_lightning.h - 1, y * _lightning.h / screen.h));
                    blend(x + dx - _lightning.w / 4, y, Color{t[0], t[1], t[2]}, t[3] * t[0] / 255);
                }
            for (int y = 0; y < screen.h; ++y)
                for (int x2 = 0; x2 < screen.w; ++x2)
                    blend(x2, y, Color{255, 255, 255}, 60);
        }
    }
    if (weather == Park::Weather::kSnow && _snow.w > 0) {
        const int half = _snow.w / 2;
        for (int i = 0; i < kSnowflakes; ++i) {
            const uint32_t h = hash(uint32_t(i) + 7919u);
            const int size = kFlakeSize / 2 + int(h % uint32_t(kFlakeSize));
            const int y =
                int(float(h >> 8) + seconds * kSnowPixelsPerSecond * (0.6f + float(h % 50) / 100)) % (screen.h + size) -
                size;
            const int x = int(h % uint32_t(screen.w)) + int(8 * std::sin(seconds + float(i)));
            // One of the four flakes, scaled to its size.
            const int qx = int(h >> 3 & 1u) * half;
            const int qy = int(h >> 4 & 1u) * half;
            for (int fy = 0; fy < size; ++fy)
                for (int fx = 0; fx < size; ++fx) {
                    const uint8_t *t = _snow.pixel(qx + fx * half / size, qy + fy * half / size);
                    blend(x + fx, y + fy, Color{t[0], t[1], t[2]}, t[3]);
                }
        }
    }
}

void ParkScreen::draw(Surface &screen, soft3d::Rasterizer &rasterizer, const Park &park, const Camera &camera) const {
    std::fill(screen.pixels.begin(), screen.pixels.end(), uint8_t(0));
    const WorldView view = viewOf(camera);
    drawWorld(screen, rasterizer, _world, view);
    const soft3d::OrbitCamera eye = worldCamera(_world, view, screen.w, screen.h);
    drawPaths(rasterizer, eye, park);
    drawObjects(rasterizer, eye, park);
    drawPlacement(screen, eye);
    for (const Park::Cell &c : _preview)
        if (park.ground(c) == Park::Ground::kOutside)
            mark(screen, eye, c, kBlockedColour);
    const Park::FixedItems &fixed = park.fixedItems();
    for (const Park::Cell &c : fixed.entrance)
        mark(screen, eye, c, kEntranceColour);
    for (const Park::Cell &c : fixed.ticketBooths)
        mark(screen, eye, c, kBoothColour);
    for (const Park::Cell &c : fixed.busStops)
        mark(screen, eye, c, kBusStopColour);
    drawVisitors(screen, eye, park, camera);
    drawWeather(screen, park);
    if (_furnitureRead) {
        ParkHud::View hudView;
        hudView.canZoomIn = camera.distance > kNearest;
        hudView.canZoomOut = camera.distance < kFarthest;
        _furniture.draw(screen, hudView);
    }
    if (!_hud)
        return;
    // The money font's "$" is the game's currency sign, drawn like a "§".
    const std::string cash = "$ " + std::to_string(park.cash());
    drawGameText(screen, _cashFont, _characters, kCashX + kShadow, kCashY + kShadow, cash, kHudShadow);
    drawGameText(screen, _cashFont, _characters, kCashX, kCashY, cash, kHudText);
    if (_placementPrice > 0) {
        // The price of what's being placed, under the money, its right
        // edge by the money's.
        // In the money's font at half its size: the recording's is half as
        // wide as the same text in it (guessed: no font of the game's is
        // that size).
        const std::string price = "$ " + std::to_string(_placementPrice);
        const int right = kCashX + gameTextWidth(_cashFont, _characters, cash);
        const int wide = gameTextWidth(_cashFont, _characters, price);
        Surface text;
        text.create(wide + 2, kPriceDrawn, PixelFormat::kRGBA32);
        drawGameText(text, _cashFont, _characters, 0, 0, price, kPriceColour);
        const int x0 = right - kPriceRightIn - wide / 2;
        for (int y = 0; y < kPriceDrawn / 2 && kPriceY + y < screen.h; ++y)
            for (int x = 0; x < wide / 2 && x0 + x < screen.w; ++x) {
                int sum[3] = {0, 0, 0};
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const uint8_t *q = text.row(2 * y + dy) + (2 * x + dx) * 4;
                        for (int i = 0; i < 3; ++i)
                            sum[i] += q[i];
                    }
                // Yellow on black: how bright it is, how much it covers.
                const int alpha = std::min(255, std::max({sum[0], sum[1], sum[2]}) / 4 * 255 / kPriceColour.r);
                if (alpha == 0 || x0 + x < 0)
                    continue;
                uint8_t *p = screen.row(kPriceY + y) + (x0 + x) * 4;
                const uint8_t c[3] = {kPriceColour.r, kPriceColour.g, kPriceColour.b};
                for (int i = 0; i < 3; ++i)
                    p[i] = uint8_t((p[i] * (255 - alpha) + c[i] * alpha) / 255);
            }
    }
    const ScreenRect field = ParkHud::dateField();
    if (!_furnitureRead)
        fillRect(screen, field, Color{0, 0, 0});
    const std::string date = dateText(park.day());
    const int w = gameTextWidth(_dateFont, _characters, date);
    drawGameText(screen, _dateFont, _characters, field.x + (field.w - w) / 2, field.y + 3, date, kHudText);
    if (_buyRead)
        _buy.draw(screen);
}

bool playPark(System &sys, ParkScreen &screen, Park &park, const ObjectivesShower &showObjectives) {
    Surface surface;
    surface.create(kWidth, kHeight, PixelFormat::kRGBA32);
    soft3d::Rasterizer rasterizer;
    ParkScreen::Camera camera = screen.startCamera(park);
    std::set<uint32_t> held;
    uint64_t last = sys.getMillis();
    // The path tool's length being laid: from its start to the cell under
    // the mouse; or the queue it's laying ("Edit line"), on from its end.
    std::optional<Park::Cell> pathStart;
    int queueLaying = -1;
    auto queueEnd = [&](int index) {
        const Park::Queue &q = park.queues()[size_t(index)];
        return q.cells.empty() ? q.entrance : q.cells.back();
    };
    int mouseX = -1;
    int mouseY = -1;
    // The attraction being placed, bought in the Buy window, and its turn.
    const Attractions::Attraction *placing = nullptr;
    int placingAngle = 0;
    for (;;) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return false;
            if (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown) {
                mouseX = ev.mouseX;
                mouseY = ev.mouseY;
            }
            // The Buy window takes the mouse, and Esc, while it's open.
            BuyWindow &buy = screen.buyWindow();
            if (buy.isOpen()) {
                if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                    buy.close();
                } else if (buy.handle(ev) == BuyWindow::Result::kBuy) {
                    placing = buy.chosen();
                    placingAngle = 0;
                    pathStart.reset();
                    queueLaying = -1;
                }
                if (ev.type != EventType::kKeyUp)
                    continue;
            }
            if (screen.hud().handle(ev))
                continue;
            Park::Cell under;
            const bool onGround = screen.cellAt(camera, mouseX, mouseY, under);
            if (placing) {
                // "Rotate using < and >, click to build" (UIHELPTEXT 146).
                if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && onGround) {
                    const int queue =
                        park.place(placing->id, placing->price, placing->shape, placing->hasQueue, under, placingAngle);
                    if (queue != Park::kNotPlaced) {
                        placing = nullptr;
                        // A ride's queue is laid at once, from its entrance ("Edit line").
                        queueLaying = queue;
                    }
                    continue;
                }
                if ((ev.type == EventType::kMouseDown && ev.button == MouseButton::kRight) ||
                    (ev.type == EventType::kKeyDown && ev.key == key::kEscape)) {
                    placing = nullptr;
                    continue;
                }
                if (ev.type == EventType::kKeyDown &&
                    (ev.key == ',' || ev.key == '<' || ev.key == '.' || ev.key == '>')) {
                    placingAngle = (placingAngle + (ev.key == ',' || ev.key == '<' ? 270 : 90)) % 360;
                    continue;
                }
            }
            const bool laying = screen.hud().tool() == ParkHud::Tool::kPath || queueLaying >= 0;
            if (ev.type == EventType::kMouseDown && laying) {
                if (ev.button == MouseButton::kRight) {
                    pathStart.reset();
                    queueLaying = -1;
                } else if (ev.button == MouseButton::kLeft && onGround && queueLaying >= 0) {
                    // A click on its end finishes it; any other lays a length
                    // on, which finishes it if it meets path.
                    const Park::Cell end = queueEnd(queueLaying);
                    if (under == end) {
                        queueLaying = -1;
                    } else {
                        park.layQueue(queueLaying, Park::pathRun(end, under));
                        if (park.queues()[size_t(queueLaying)].atPath)
                            queueLaying = -1;
                    }
                } else if (ev.button == MouseButton::kLeft && onGround) {
                    if (!pathStart) {
                        // On an attraction's entrance, or a queue's loose end,
                        // it lays queue.
                        queueLaying = park.queueFrom(under);
                        if (queueLaying < 0 && park.ground(under) != Park::Ground::kOutside)
                            pathStart = under;
                    } else {
                        // A click onto path already laid finishes; any
                        // other lays the length and goes on from its end.
                        const bool ontoPath = park.isPath(under) && !(under == *pathStart);
                        const std::vector<Park::Cell> run = Park::pathRun(*pathStart, under);
                        park.buildPath(run);
                        pathStart.reset();
                        if (!ontoPath)
                            pathStart = run.back();
                    }
                }
                continue;
            }
            if (ev.type == EventType::kKeyDown) {
                if (ev.key == key::kEscape && (pathStart || queueLaying >= 0)) {
                    pathStart.reset();
                    queueLaying = -1;
                    continue;
                }
                if (ev.key == key::kEscape)
                    return true;
                if (ev.key == key::kHome)
                    camera = screen.startCamera(park);
                held.insert(ev.key);
            } else if (ev.type == EventType::kKeyUp) {
                held.erase(ev.key);
            } else if (ev.type == EventType::kMouseWheel && ev.wheelY) {
                camera.distance *= ev.wheelY > 0 ? 1 / kWheelZoom : kWheelZoom;
            }
        }
        const uint64_t now = sys.getMillis();
        const float seconds = float(now - last) / 1000;
        park.advance(uint32_t(now - last));
        screen.advance(park);
        screen.hud().advance(uint32_t(now - last));
        screen.buyWindow().advance(uint32_t(now - last));
        last = now;

        const uint32_t choice = screen.hud().takeChoice();
        if (choice == ParkHud::kBuildAttractionItem)
            screen.buyWindow().open(screen.attractions(), Attractions::Kind::kRide, park);
        if (choice == ParkHud::kObjectivesItem && showObjectives) {
            park.setPaused(true);
            const ParkScreen::Camera now = camera;
            const bool open = showObjectives([&](Surface &s) { screen.draw(s, rasterizer, park, now); });
            park.setPaused(false);
            last = sys.getMillis();
            if (!open)
                return false;
        }
        if (screen.hud().tool() != ParkHud::Tool::kPath)
            pathStart.reset();
        Park::Cell under;
        std::optional<Park::Footprint> footprint;
        bool fits = false;
        if (placing && !screen.hud().covers(mouseX, mouseY) && screen.cellAt(camera, mouseX, mouseY, under)) {
            footprint = Park::footprint(placing->shape, under, placingAngle);
            fits = park.canPlace(*footprint) && park.cash() >= placing->price;
        }
        screen.setPlacement(std::move(footprint), fits, placing ? placing->price : 0);
        std::vector<Park::Cell> preview;
        std::optional<Park::Queue> queuePreview;
        if (!placing && (screen.hud().tool() == ParkHud::Tool::kPath || queueLaying >= 0) &&
            !screen.hud().covers(mouseX, mouseY) && screen.cellAt(camera, mouseX, mouseY, under)) {
            if (queueLaying >= 0)
                queuePreview = park.queueWith(queueLaying, Park::pathRun(queueEnd(queueLaying), under));
            else
                preview = pathStart ? Park::pathRun(*pathStart, under) : std::vector<Park::Cell>{under};
        }
        screen.setPathPreview(std::move(preview));
        screen.setQueuePreview(std::move(queuePreview));

        // The arrows move along the ground as the camera faces; farther
        // out, faster.
        const float pan = kPanPerSecond * seconds * camera.distance / kStartDistance;
        const float forward = (held.count(key::kUp) ? 1.0f : 0.0f) - (held.count(key::kDown) ? 1.0f : 0.0f);
        const float sideways = (held.count(key::kRight) ? 1.0f : 0.0f) - (held.count(key::kLeft) ? 1.0f : 0.0f);
        camera.target[0] += pan * (forward * std::sin(camera.yaw) + sideways * std::cos(camera.yaw));
        camera.target[2] += pan * (forward * std::cos(camera.yaw) - sideways * std::sin(camera.yaw));
        // The compass's buttons as Q, E, Page Up and Page Down, while held.
        const ParkHud &hud = screen.hud();
        const bool turnLeft = held.count('q') || hud.held(ParkHud::CompassButton::kTurnLeft);
        const bool turnRight = held.count('e') || hud.held(ParkHud::CompassButton::kTurnRight);
        camera.yaw += kTurnPerSecond * seconds * ((turnRight ? 1.0f : 0.0f) - (turnLeft ? 1.0f : 0.0f));
        if (held.count(key::kPageUp) || hud.held(ParkHud::CompassButton::kZoomIn))
            camera.distance /= std::pow(kZoomPerSecond, seconds);
        if (held.count(key::kPageDown) || hud.held(ParkHud::CompassButton::kZoomOut))
            camera.distance *= std::pow(kZoomPerSecond, seconds);
        camera.distance = std::clamp(camera.distance, kNearest, kFarthest);

        screen.draw(surface, rasterizer, park, camera);
        sys.updateScreen(surface);
        sys.delayMillis(kFrameMillis);
    }
}

} // namespace reluva::tpw
