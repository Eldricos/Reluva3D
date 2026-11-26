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
constexpr float kGroundPitch = 0.08f; // the camcorder's: a visitor's eye height at the nearest
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
// The menus' items acted on, UITEXT's: the Staff and Park menus', and the
// right-click menus' of an attraction, a plant, a member of staff and the
// gates.
constexpr uint32_t kTrainStaffItem = 828;
constexpr uint32_t kListStaffItem = 829;
constexpr uint32_t kLocateStaffItem = 830;
constexpr uint32_t kStaffFinanceItem = 831;
constexpr uint32_t kParkGatesItem = 847;
constexpr uint32_t kAttractionFinanceItem = 836;
constexpr uint32_t kListAttractionsItem = 835;
constexpr uint32_t kListVisitorsItem = 838;
constexpr uint32_t kLocateVisitorsItem = 839;
constexpr uint32_t kVisitorStatisticsItem = 840;
constexpr uint32_t kParkStatisticsItem = 846;
constexpr uint32_t kParkAwardsItem = 850;
constexpr uint32_t kParkMapItem = 852;
constexpr uint32_t kOverviewMapItem = 853;
constexpr uint32_t kVisitorOpinionItem = 854;
constexpr uint32_t kChallengesItem = 849;
constexpr uint32_t kFinancialStatisticsItem = 842;
constexpr uint32_t kLoansItem = 843;
constexpr uint32_t kSharePortfolioItem = 844;
constexpr uint32_t kCallEngineerItem = 524;
constexpr uint32_t kCancelEngineerItem = 525;
constexpr uint32_t kEditLineItem = 526;
constexpr uint32_t kOpenItem = 530;
constexpr uint32_t kCloseItem = 531;
constexpr uint32_t kCallGardenerItem = 583;
constexpr uint32_t kCancelGardenerItem = 584;
constexpr uint32_t kEntryPriceItem = 597;
constexpr uint32_t kOpenParkItem = 599;
constexpr uint32_t kCloseParkItem = 600;
constexpr uint32_t kSetPatrolItem = 603;
constexpr uint32_t kClearPatrolItem = 605;
constexpr uint32_t kTrainItem = 606;
constexpr uint32_t kFireItem = 608;
constexpr uint32_t kBuyAnotherItem = 514;
constexpr uint32_t kDeleteItem = 516;
constexpr uint32_t kBuyUpgradeItem = 529;
constexpr uint32_t kCancelUpgradeItem = 127;
constexpr uint32_t kRideItem = 532;
constexpr uint32_t kFinanceSummaryItem = 533;
constexpr uint32_t kEditNameItem = 534;
constexpr uint32_t kKioskOpenItem = 547;
constexpr uint32_t kKioskCloseItem = 548;
constexpr uint32_t kKioskFinanceItem = 549;
constexpr uint32_t kKioskNameItem = 550;
constexpr uint32_t kViewObjectivesItem = 591;
constexpr uint32_t kProceedItem = 592;
// What a solution refused says (UITEXT): "You need to research a
// solution.", "You currently don't have all the staff of the required
// type or ability", "GOLDEN TICKETS NEEDED:".
constexpr uint32_t kNeedSolutionText = 103;
constexpr uint32_t kNeedStaffText = 92;
constexpr uint32_t kNeedTicketsText = 85;
// What an upgrade refused says, by Park::Upgrading: "fully upgraded" (125),
// "hired some engineers and trained them" (11), "You need to research a
// solution" (103, guessed for an upgrade not researched); none for want of
// money.
constexpr uint32_t kFullyUpgradedText = 125;
constexpr uint32_t kNeedEngineersText = 11;
constexpr uint32_t kNeedResearchText = 103;
// A message's time over the park (guessed).
constexpr uint64_t kNoticeMillis = 6000;
constexpr ScreenRect kNoticeBox{120, 40, 400, 44};
// A member of staff is picked within this of their feet, in units.
constexpr float kStaffPickHalfWidth = 1.5f;
constexpr float kStaffPickHeight = 5;
// A visitor in a costume (Park::Visitor::appearance), drawn by the world's.
constexpr int kCostumed = 2;
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
// A janitor sweeping up: the handyman's fifth animation, a frame this often.
constexpr size_t kSweepAnimation = 4;
constexpr uint64_t kSweepFrameMillis = 200;
constexpr size_t kStrikeAnimation = 5;
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

// A message over the park, broken to fit its frame.
void drawNotice(Surface &screen, const WindowKit &kit, const std::string &text) {
    kit.frame(screen, kNoticeBox);
    kit.paragraph(screen, kNoticeBox.x + 8, kNoticeBox.y + 8, kNoticeBox.w - 16, text);
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
    _hireRead = !_hire.load(files, park.folder());
    _kitRead = !_kit.load(files);
    _parkMap.load(files, park.folder());
    loadObjects(files, park);
    _runs.clear();
    _runIds.clear();
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
    // The world's costume, whole and its head (esprites/Science/Costumes/SPR_GK).
    _costume.reset();
    _costumeHead.reset();
    const std::string world = park.folder().substr(park.folder().rfind('/') + 1);
    for (const std::string &name : files.list()) {
        std::string low = name;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        for (auto [sprite, part] : {std::pair{&_costume, "/costumes/"}, std::pair{&_costumeHead, "/costumeheads/"}}) {
            const std::string prefix = "esprites/" + world + part;
            if (sprite->has_value() || low.compare(0, prefix.size(), prefix) != 0 || low.size() < 4 ||
                low.compare(low.size() - 4, 4, ".esp") != 0)
                continue;
            LoadedSprite loaded;
            if (!loaded.load(files, name.substr(0, name.size() - 4)))
                *sprite = std::move(loaded);
        }
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
    for (const auto &[object, queue] : park.takeRemovals())
        removed(object, queue);
    auto runOf = [&](size_t i) {
        std::unique_ptr<RideRun> run;
        const int id = park.objects()[i].id;
        if (objectModel(id) && _files) {
            run = std::make_unique<RideRun>();
            if (run->start(*_files, _objectPaths[id], false))
                run.reset();
            else if (i < _prebuilt)
                run->advance(kSettledMillis);
        }
        return run;
    };
    // An object changed for another (an obstacle cleared) runs the other's script.
    for (size_t i = 0; i < _runs.size() && i < park.objects().size(); ++i)
        if (_runIds[i] != park.objects()[i].id) {
            _runIds[i] = park.objects()[i].id;
            _runs[i] = runOf(i);
        }
    for (size_t i = _runs.size(); i < park.objects().size(); ++i) {
        _runs.push_back(runOf(i));
        _runIds.push_back(park.objects()[i].id);
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
        // What the park tells the script: broken, or closed (its variables by their names).
        if (ScriptMachine *machine = run->machine(); machine && i < park.objects().size()) {
            const Park::PlacedObject &o = park.objects()[i];
            const auto &names = machine->script().variables();
            for (size_t v = 0; v < names.size(); ++v) {
                if (names[v] == "VAR_BREAKSTAT")
                    machine->setVariable(v, o.broken ? 1 : 0);
                if (names[v] == "VAR_RIDECLOSED")
                    machine->setVariable(v, o.open ? 0 : 1);
                if (names[v] == "VAR_BROKEN" && !o.broken)
                    machine->setVariable(v, 0);
                // Upgraded, its level's; before, the script's host gives its own settings'.
                if (const Attractions::Attraction::Level *level = park.levelOf(i); level && o.level > 0) {
                    if (names[v] == "VAR_CAPACITY")
                        machine->setVariable(v, level->capacity);
                    if (names[v] == "VAR_DURATION")
                        machine->setVariable(v, level->duration);
                }
            }
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
            laid[size_t(y) * size_t(w) + size_t(x)] = park.pathLaid({x, y});
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

// How the game's view from a ride is placed isn't worked out: close in
// over its middle, as the camera faced.
ParkScreen::Camera ParkScreen::rideCamera(const Park &park, size_t object, const Camera &from) const {
    if (object >= park.objects().size())
        return from;
    const Park::PlacedObject &o = park.objects()[object];
    const Attractions::Attraction *a = park.attractions().find(o.id);
    const std::vector<Park::Cell> cells = a ? Park::footprint(a->shape, o.at, o.angle).cells : std::vector{o.at};
    gfx::Vec3 middle{};
    for (const Park::Cell &c : cells) {
        const gfx::Vec3 at = Park::cellCentre(c);
        middle[0] += at[0] / float(cells.size());
        middle[2] += at[2] / float(cells.size());
    }
    return {middle, from.yaw, from.pitch, kNearest};
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

void ParkScreen::drawVisitors(Surface &screen, const soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &eye,
                              const Park &park, const Camera &camera) const {
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
                const std::optional<LoadedSprite> &headSprite =
                    v.appearance == kCostumed && _costumeHead ? _costumeHead : _personHeads[person];
                if (headSprite) {
                    const LoadedSprite &head = *headSprite;
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
        const std::optional<LoadedSprite> &looks =
            v.appearance == kCostumed && _costume ? _costume : _personLooks[person];
        if (!looks) {
            mark(screen, eye, {v.x, 1, v.z}, 2, kVisitorColour);
            continue;
        }
        const LoadedSprite &look = *looks;
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
    // The staff, by their kind's sprite: walking, standing, a janitor
    // sweeping (its fifth animation, stooping with pan and brush).
    for (const Park::StaffMember &m : park.staff()) {
        if (m.doing == Park::StaffDoing::kResting || m.doing == Park::StaffDoing::kTraining)
            continue;
        const LoadedSprite *look = _hire.spriteOf(m.kind);
        if (!look) {
            mark(screen, eye, {m.x, 1, m.z}, 2, kVisitorColour);
            continue;
        }
        const bool walking = !m.working && (m.headingX != 0 || m.headingZ != 0);
        // On strike, once there, they hold up a sign: their sixth animation, where they have it.
        const bool picketing =
            m.doing == Park::StaffDoing::kStriking && !walking && look->set.animations()[kStrikeAnimation].frames > 0;
        const size_t slot = picketing   ? kStrikeAnimation
                            : m.working ? kSweepAnimation
                            : walking   ? kWalkAnimation
                                        : kStandAnimation;
        const auto &animation = look->set.animations()[slot];
        const SpritePose pose =
            spritePose(animation, walking || m.working ? turned(m.headingX, m.headingZ) : 3.14159265f,
                       m.working || picketing ? uint32_t(park.millis() / kSweepFrameMillis)
                       : walking              ? uint32_t(m.walked / kUnitsPerWalkFrame)
                                              : 0);
        see(*look, pose.frame, {m.x, 0, m.z}, pose.mirror);
    }
    std::sort(seen.begin(), seen.end(), [](const Seen &a, const Seen &b) { return a.depth > b.depth; });
    for (const Seen &s : seen)
        // Hidden where the models are nearer.
        drawSpriteFrame(screen, *s.sheet, s.frame, s.at.x, s.at.y, s.scale * kSpritePixelUnits * eye.focal / s.depth,
                        s.mirror, rasterizer, s.at.invZ);
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
    drawVisitors(screen, rasterizer, eye, park, camera);
    drawWeather(screen, park);
    if (_furnitureRead) {
        ParkHud::View hudView;
        hudView.canZoomIn = camera.distance > kNearest;
        hudView.canZoomOut = camera.distance < kFarthest;
        hudView.unread = int(std::count_if(_inbox.begin(), _inbox.end(),
                                           [](const MessageCenterWindow::Received &m) { return !m.read; }));
        _furniture.draw(screen, hudView);
    }
    if (!_hud)
        return;
    // The money font's "$" is the game's currency sign, drawn like a "§".
    const std::string cash = (park.cash() < 0 ? "-$ " : "$ ") + std::to_string(std::abs(park.cash()));
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
    if (_hireRead)
        _hire.draw(screen);
    if (_kitRead) {
        _attractionWindow.draw(screen, _kit, park);
        _gateWindow.draw(screen, _kit, park);
        _staffList.draw(screen, _kit, park, _hire);
        _trainingWindow.draw(screen, _kit, park, _hire);
        _staffFinance.draw(screen, _kit, park);
        _staffWindow.draw(screen, _kit, park, _hire);
        _researchLab.draw(screen, _kit, park, _hire, _buy.names());
        _projectWindow.draw(screen, _kit, park, _buy.names());
        _assignWindow.draw(screen, _kit, park, _hire, _buy.names());
        _financeSummary.draw(screen, _kit, park);
        _renameWindow.draw(screen, _kit);
        _financeStats.draw(screen, _kit, park);
        _loansWindow.draw(screen, _kit, park);
        _portfolio.draw(screen, _kit, park);
        _attractionFinance.draw(screen, _kit, park);
        _visitorStats.draw(screen, _kit, park);
        _visitorList.draw(screen, _kit, park);
        _visitorWindow.draw(screen, _kit, park);
        _attractionList.draw(screen, _kit, park, _buy.names());
        _parkStats.draw(screen, _kit, park);
        _awards.draw(screen, _kit, park);
        _activateRide.draw(screen, _kit, park, _hire);
        _parkMap.draw(screen, _kit, park,
                      {int(std::floor(camera.target[0] / Park::kCellUnits)),
                       int(std::floor(camera.target[2] / Park::kCellUnits))});
        _overviewMap.draw(screen, _kit);
        _messageCenter.draw(screen, _kit, _inbox, _playerName, [&](uint32_t day) { return dateText(day); });
        _messageWindow.draw(screen, _kit);
        _wageDemand.draw(screen, _kit, park, _hire);
        _popup.draw(screen, _kit);
        if (_notice && park.millis() < _noticeUntil)
            drawNotice(screen, _kit, _kit.ui(_notice));
        _gameMenu.draw(screen, _kit);
    }
}

std::vector<OverviewMapWindow::World> ParkScreen::worlds(const Park &park) const {
    // Land of Invention, Polar Zone, Arabian Nights, as the globe has them.
    constexpr std::array<int, 3> kGlobeOrder{1, 2, 0};
    std::vector<OverviewMapWindow::World> out;
    const std::vector<int> &opened = park.themesOpened();
    for (const int theme : kGlobeOrder)
        out.push_back({theme,
                       theme == park.theme() || _worldOpen[size_t(theme)] ||
                           std::find(opened.begin(), opened.end(), theme) != opened.end(),
                       park.challengesWonIn(theme), _worldChallenges[size_t(theme)]});
    return out;
}

bool ParkScreen::windowOpen() const {
    return _popup.isOpen() || _attractionWindow.isOpen() || _gateWindow.isOpen() || _staffWindow.isOpen() ||
           _staffList.isOpen() || _trainingWindow.isOpen() || _staffFinance.isOpen() || _wageDemand.isOpen() ||
           _researchLab.isOpen() || _projectWindow.isOpen() || _assignWindow.isOpen() || _financeSummary.isOpen() ||
           _renameWindow.isOpen() || _financeStats.isOpen() || _loansWindow.isOpen() || _portfolio.isOpen() ||
           _attractionFinance.isOpen() || _messageWindow.isOpen() || _visitorStats.isOpen() ||
           _visitorWindow.isOpen() || _visitorList.isOpen() || _attractionList.isOpen() || _parkStats.isOpen() ||
           _awards.isOpen() || _messageCenter.isOpen() || _gameMenu.isOpen() || _parkMap.isOpen() ||
           _overviewMap.isOpen() || _activateRide.isOpen();
}

// Every message goes to the Message Center, unread; the president's
// letters on an objective done, a promotion and the game's end show
// over the park too (guessed: the recording's Message Center has an
// "Objective Complete!" in it).
void ParkScreen::takeMessages(Park &park) {
    constexpr std::array<uint32_t, 4> kShown{168, 163, 165, 169};
    for (Park::Message &m : park.takeMessages()) {
        const bool shown = std::find(kShown.begin(), kShown.end(), m.title) != kShown.end();
        _inbox.push_back({m, park.day(), shown});
        if (shown)
            _messages.push_back(std::move(m));
    }
    if (!_messageWindow.isOpen() && !_messages.empty()) {
        _messageWindow.open(std::move(_messages.front()));
        _messages.pop_front();
    }
}

void ParkScreen::notify(const Park &park, uint32_t text) {
    _notice = text;
    _noticeUntil = park.millis() + kNoticeMillis;
}

// The scripts and the unscripted rides' users go by the objects' and queues' places.
void ParkScreen::removed(size_t object, int queue) {
    if (object < _runs.size()) {
        _runs.erase(_runs.begin() + long(object));
        _runIds.erase(_runIds.begin() + long(object));
    }
    if (object < _prebuilt)
        --_prebuilt;
    if (queue < 0)
        return;
    std::map<int, std::vector<std::pair<int, uint64_t>>> users;
    for (auto &[q, on] : _unscripted)
        if (q != queue)
            users.emplace(q > queue ? q - 1 : q, std::move(on));
    _unscripted = std::move(users);
}

int ParkScreen::objectAt(const Park &park, const Park::Cell &cell) const {
    for (size_t i = 0; i < park.objects().size(); ++i) {
        const Park::PlacedObject &o = park.objects()[i];
        const Attractions::Attraction *a = park.attractions().find(o.id);
        const std::vector<Park::Cell> cells =
            a ? Park::footprint(a->shape, o.at, o.angle).cells : std::vector<Park::Cell>{o.at};
        if (std::find(cells.begin(), cells.end(), cell) != cells.end())
            return int(i);
    }
    return -1;
}

// Within a sprite's half-width of where their feet are drawn, and up to
// their height above (guessed: about 5 units tall, 3 across).
// As staff are picked, but for those in the park walking or queueing.
int ParkScreen::visitorAt(const Park &park, const Camera &camera, int x, int y) const {
    const soft3d::OrbitCamera eye = worldCamera(_world, viewOf(camera), kWidth, kHeight);
    for (const Park::Visitor &v : park.visitorsAbout()) {
        if (!v.inPark || v.doing == Park::Doing::kRiding)
            continue;
        const soft3d::ViewVertex view = eye.toView(toViewerWorld({v.x, 0, v.z}));
        if (view.z < eye.nearZ)
            continue;
        const soft3d::ScreenVertex at = eye.project(view);
        const float scale = eye.focal / view.z;
        if (std::fabs(float(x) - at.x) <= kStaffPickHalfWidth * scale && float(y) <= at.y &&
            float(y) >= at.y - kStaffPickHeight * scale)
            return v.id;
    }
    return 0;
}

// Low over the ground, as the recording's camcorder looks at 11:50 (the
// camera's lowest tilt, its nearest).
ParkScreen::Camera ParkScreen::groundCamera(const Park::Cell &cell, const Camera &from) const {
    return {Park::cellCentre(cell), from.yaw, kGroundPitch, kNearest};
}

int ParkScreen::staffAt(const Park &park, const Camera &camera, int x, int y) const {
    const soft3d::OrbitCamera eye = worldCamera(_world, viewOf(camera), kWidth, kHeight);
    for (const Park::StaffMember &m : park.staff()) {
        if (m.doing == Park::StaffDoing::kResting || m.doing == Park::StaffDoing::kTraining)
            continue;
        const soft3d::ViewVertex view = eye.toView(toViewerWorld({m.x, 0, m.z}));
        if (view.z < eye.nearZ)
            continue;
        const soft3d::ScreenVertex at = eye.project(view);
        const float scale = eye.focal / view.z;
        if (std::fabs(float(x) - at.x) <= kStaffPickHalfWidth * scale && float(y) <= at.y &&
            float(y) >= at.y - kStaffPickHeight * scale)
            return m.id;
    }
    return 0;
}

std::string ParkScreen::nameOf(const Park &park, size_t object) const {
    if (object < park.objects().size() && !park.objects()[object].name.empty())
        return park.objects()[object].name;
    const Attractions::Attraction *a =
        object < park.objects().size() ? park.attractions().find(park.objects()[object].id) : nullptr;
    return a && _buyRead ? _buy.names().shown(*a) : a ? a->name : std::string();
}

const LoadedModel *ParkScreen::modelOf(const Park &park, size_t object) const {
    return object < park.objects().size() ? objectModel(park.objects()[object].id) : nullptr;
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
    // What the right-click menu is of: an object, a member of staff, or the gates.
    int menuObject = -1;
    int menuStaff = 0;
    // A patrol area being set: whose, and its first corner once clicked.
    int patrolling = 0;
    std::optional<Park::Cell> patrolCorner;
    size_t located = 0;        // Locate Staff's last
    size_t locatedVisitor = 0; // Locate Visitors' last
    bool wantObjectives = false;
    int shownMission = park.mission().number; // the mission whose briefing was last shown
    auto onGates = [&](const Park::Cell &c) {
        const auto &gates = park.fixedItems().entrance;
        return std::find(gates.begin(), gates.end(), c) != gates.end();
    };
    // A right-click menu's items, by what it's of (UITEXT's).
    auto objectItems = [&](size_t object) {
        std::vector<uint32_t> items;
        const Park::PlacedObject &o = park.objects()[object];
        const Attractions::Attraction *a = park.attractions().find(o.id);
        // An obstacle: the objectives, and, if it's the mission's, its solution.
        if (a && a->obstacle) {
            items.push_back(kViewObjectivesItem);
            if (o.id == park.mission().obstacle && park.mission().needsSolution())
                items.push_back(kProceedItem);
            return items;
        }
        if (a && a->requiresWatering)
            items.push_back(o.gardener ? kCancelGardenerItem : kCallGardenerItem);
        const int queue = park.queueOf(object);
        const bool ride = queue >= 0 && !park.queues()[size_t(queue)].kiosk;
        if (ride) {
            items.push_back(o.engineer ? kCancelEngineerItem : kCallEngineerItem);
            items.push_back(kEditLineItem);
            if (park.levelOf(object, o.level + 1))
                items.push_back(o.upgrading > 0 ? kCancelUpgradeItem : kBuyUpgradeItem);
            items.push_back(o.open ? kCloseItem : kOpenItem);
            items.push_back(kRideItem);
            items.push_back(kFinanceSummaryItem);
            items.push_back(kEditNameItem);
        } else if (queue >= 0) {
            items.push_back(o.open ? kKioskCloseItem : kKioskOpenItem);
            items.push_back(kKioskFinanceItem);
            items.push_back(kKioskNameItem);
        }
        if (a && a->price > 0 && a->available)
            items.push_back(kBuyAnotherItem);
        items.push_back(kDeleteItem);
        return items;
    };
    for (;;) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return false;
            if (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown) {
                mouseX = ev.mouseX;
                mouseY = ev.mouseY;
            }
            // The smaller windows and the right-click menu take the mouse, and
            // Esc, while open: the uppermost first.
            if (screen.windowOpen()) {
                if (screen.gameMenu().isOpen()) {
                    if (screen.gameMenu().handle(ev) == GameMenu::Choice::kQuit)
                        return true;
                } else if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                    screen.visitorStats().close();
                    screen.visitorWindow().close();
                    screen.visitorList().close();
                    screen.attractionList().close();
                    screen.parkStats().close();
                    screen.awards().close();
                    screen.parkMap().close();
                    screen.overviewMap().close();
                    screen.activateRide().close();
                    screen.messageCenter().close();
                    screen.popup().close();
                    screen.attractionWindow().close();
                    screen.gateWindow().close();
                    screen.staffWindow().close();
                    screen.staffList().close();
                    screen.trainingWindow().close();
                    screen.staffFinance().close();
                    screen.researchLab().close();
                    screen.projectWindow().close();
                    screen.assignWindow().close();
                    screen.financeSummary().close();
                    screen.financeStats().close();
                    screen.loansWindow().close();
                    screen.portfolio().close();
                    screen.attractionFinance().close();
                    screen.messageWindow().close();
                    if (screen.renameWindow().isOpen()) {
                        screen.renameWindow().close();
                        sys.setTextInput(false);
                    }
                } else if (screen.messageWindow().isOpen()) {
                    screen.messageWindow().handle(ev);
                } else if (screen.messageCenter().isOpen()) {
                    screen.messageCenter().handle(ev, screen.inbox());
                } else if (screen.visitorWindow().isOpen()) {
                    // Its left button takes the camera to them.
                    if (screen.visitorWindow().handle(ev, park) == WindowResult::kChose)
                        for (const Park::Visitor &v : park.visitorsAbout())
                            if (v.id == screen.visitorWindow().visitor())
                                camera.target = {v.x, 0, v.z};
                } else if (screen.visitorList().isOpen()) {
                    if (screen.visitorList().handle(ev, park) == WindowResult::kChose)
                        screen.visitorWindow().open(screen.visitorList().chosen());
                } else if (screen.visitorStats().isOpen()) {
                    screen.visitorStats().handle(ev);
                } else if (screen.attractionList().isOpen()) {
                    if (screen.attractionList().handle(ev, park) == WindowResult::kChose) {
                        const size_t object = size_t(screen.attractionList().chosen());
                        if (park.queueOf(object) >= 0)
                            screen.attractionWindow().open(park, object, screen.nameOf(park, object),
                                                           screen.modelOf(park, object));
                        else
                            camera.target = Park::cellCentre(park.objects()[object].at);
                    }
                } else if (screen.parkStats().isOpen()) {
                    screen.parkStats().handle(ev);
                } else if (screen.awards().isOpen()) {
                    screen.awards().handle(ev);
                } else if (screen.activateRide().isOpen()) {
                    screen.activateRide().handle(ev, park);
                } else if (screen.overviewMap().isOpen()) {
                    // Another world chosen: the park's left for it.
                    if (screen.overviewMap().handle(ev) == WindowResult::kChose) {
                        screen.setTravel(screen.overviewMap().chosen());
                        return true;
                    }
                } else if (screen.parkMap().isOpen()) {
                    // A click on the map takes the camera there; the map stays.
                    if (screen.parkMap().handle(ev) == WindowResult::kChose)
                        camera.target = Park::cellCentre(screen.parkMap().target());
                } else if (screen.loansWindow().isOpen()) {
                    screen.loansWindow().handle(ev, park);
                } else if (screen.financeStats().isOpen()) {
                    screen.financeStats().handle(ev);
                } else if (screen.portfolio().isOpen()) {
                    screen.portfolio().handle(ev);
                } else if (screen.attractionFinance().isOpen()) {
                    screen.attractionFinance().handle(ev, park);
                } else if (screen.renameWindow().isOpen()) {
                    if (screen.renameWindow().handle(ev) == WindowResult::kChose) {
                        park.rename(screen.renameWindow().object(), screen.renameWindow().typed());
                        sys.setTextInput(false);
                    }
                } else if (screen.wageDemand().isOpen()) {
                    screen.wageDemand().handle(ev, park);
                } else if (screen.popup().isOpen()) {
                    if (screen.popup().handle(ev) == WindowResult::kChose) {
                        const uint32_t item = screen.popup().chosen();
                        if (menuObject >= 0) {
                            const size_t object = size_t(menuObject);
                            if (item == kCallEngineerItem)
                                park.callEngineer(object);
                            else if (item == kCancelEngineerItem)
                                park.cancelEngineer(object);
                            else if (item == kCallGardenerItem)
                                park.callGardener(object);
                            else if (item == kEditLineItem)
                                queueLaying = park.queueOf(object);
                            else if (item == kOpenItem || item == kCloseItem || item == kKioskOpenItem ||
                                     item == kKioskCloseItem)
                                park.setOpen(object, item == kOpenItem || item == kKioskOpenItem);
                            else if (item == kBuyUpgradeItem)
                                switch (park.upgrade(object)) {
                                case Park::Upgrading::kFullyUpgraded:
                                    screen.notify(park, kFullyUpgradedText);
                                    break;
                                case Park::Upgrading::kNeedsEngineer:
                                    screen.notify(park, kNeedEngineersText);
                                    break;
                                case Park::Upgrading::kNotResearched:
                                    screen.notify(park, kNeedResearchText);
                                    break;
                                default:
                                    break;
                                }
                            else if (item == kCancelUpgradeItem)
                                park.cancelUpgrade(object);
                            else if (item == kViewObjectivesItem)
                                wantObjectives = true;
                            else if (item == kProceedItem)
                                switch (park.proceedWithSolution()) {
                                case Park::Solution::kNotResearched:
                                    screen.notify(park, kNeedSolutionText);
                                    break;
                                case Park::Solution::kNeedsStaff:
                                    screen.notify(park, kNeedStaffText);
                                    break;
                                case Park::Solution::kNeedsTickets:
                                    screen.notify(park, kNeedTicketsText);
                                    break;
                                default:
                                    break;
                                }
                            else if (item == kRideItem)
                                camera = screen.rideCamera(park, object, camera);
                            else if (item == kFinanceSummaryItem || item == kKioskFinanceItem)
                                screen.financeSummary().open(object, screen.nameOf(park, object));
                            else if (item == kEditNameItem || item == kKioskNameItem) {
                                screen.renameWindow().open(object, screen.nameOf(park, object));
                                sys.setTextInput(true);
                            } else if (item == kBuyAnotherItem) {
                                placing = park.attractions().find(park.objects()[object].id);
                                placingAngle = park.objects()[object].angle;
                            } else if (item == kDeleteItem) {
                                const int queue = park.queueOf(object);
                                park.remove(object);
                                if (queueLaying == queue)
                                    queueLaying = -1;
                                else if (queueLaying > queue && queue >= 0)
                                    --queueLaying;
                            }
                        } else if (menuStaff) {
                            if (item == kSetPatrolItem) {
                                patrolling = menuStaff;
                                patrolCorner.reset();
                            } else if (item == kClearPatrolItem) {
                                park.clearPatrol(menuStaff);
                            } else if (item == kTrainItem) {
                                park.sendForTraining(menuStaff);
                            } else if (item == kFireItem) {
                                park.fire(menuStaff);
                            }
                        } else if (item == kEntryPriceItem) {
                            screen.gateWindow().open();
                        } else if (item == kOpenParkItem || item == kCloseParkItem) {
                            park.setParkOpen(item == kOpenParkItem);
                        }
                    }
                } else if (screen.assignWindow().isOpen()) {
                    screen.assignWindow().handle(ev, park);
                } else if (screen.projectWindow().isOpen()) {
                    // A project chosen, scientists are assigned to it.
                    if (screen.projectWindow().handle(ev, park) == WindowResult::kChose)
                        screen.assignWindow().open(screen.researchLab().slot());
                } else if (screen.researchLab().isOpen()) {
                    switch (screen.researchLab().handle(ev, park)) {
                    case ResearchLabWindow::Wants::kChoose:
                        screen.projectWindow().open(screen.researchLab().slot());
                        break;
                    case ResearchLabWindow::Wants::kAssign:
                        screen.assignWindow().open(screen.researchLab().slot());
                        break;
                    default:
                        break;
                    }
                } else if (screen.financeSummary().isOpen()) {
                    screen.financeSummary().handle(ev);
                } else if (screen.staffWindow().isOpen()) {
                    screen.staffWindow().handle(ev, park);
                } else if (screen.staffList().isOpen()) {
                    if (screen.staffList().handle(ev, park) == WindowResult::kChose)
                        screen.staffWindow().open(screen.staffList().chosen());
                } else if (screen.trainingWindow().isOpen()) {
                    screen.trainingWindow().handle(ev);
                } else if (screen.staffFinance().isOpen()) {
                    screen.staffFinance().handle(ev, park);
                } else if (screen.attractionWindow().isOpen()) {
                    screen.attractionWindow().handle(ev, park);
                } else if (screen.gateWindow().isOpen()) {
                    screen.gateWindow().handle(ev, park);
                }
                if (ev.type != EventType::kKeyUp)
                    continue;
            }
            // The Hire Staff window likewise; its tick hires, and it stays open.
            HireWindow &hiring = screen.hireWindow();
            if (hiring.isOpen()) {
                if (ev.type == EventType::kKeyDown && ev.key == key::kEscape)
                    hiring.close();
                else if (hiring.handle(ev) == HireWindow::Result::kHire)
                    park.hire(hiring.kind(), hiring.chosen());
                if (ev.type != EventType::kKeyUp)
                    continue;
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
            // A patrol area: two corners clicked on the ground; a right click or Esc drops it.
            if (patrolling) {
                if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && onGround) {
                    if (!patrolCorner) {
                        patrolCorner = under;
                    } else {
                        park.setPatrol(patrolling, *patrolCorner, under);
                        patrolling = 0;
                    }
                    continue;
                }
                if ((ev.type == EventType::kMouseDown && ev.button == MouseButton::kRight) ||
                    (ev.type == EventType::kKeyDown && ev.key == key::kEscape)) {
                    patrolling = 0;
                    continue;
                }
            }
            // The bulldozer ("Delete mode") takes up what's clicked: an
            // attraction, sold for its scrap value, or a cell of path; not
            // an obstacle. The camcorder looks from the ground where it's
            // clicked.
            const ParkHud::Tool tool = screen.hud().tool();
            if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && onGround && queueLaying < 0 &&
                (tool == ParkHud::Tool::kBulldozer || tool == ParkHud::Tool::kCamera)) {
                if (tool == ParkHud::Tool::kCamera) {
                    camera = screen.groundCamera(under, camera);
                    continue;
                }
                const int object = screen.objectAt(park, under);
                const Attractions::Attraction *a =
                    object >= 0 ? park.attractions().find(park.objects()[size_t(object)].id) : nullptr;
                if (object >= 0 && !(a && a->obstacle))
                    park.remove(size_t(object));
                else if (object < 0)
                    park.removePath(under);
                continue;
            }
            // With the pointer, a click on an attraction, a member of staff or
            // the gates opens their window; a right click, their menu.
            if (ev.type == EventType::kMouseDown && screen.hud().tool() == ParkHud::Tool::kPointer && queueLaying < 0 &&
                (ev.button == MouseButton::kLeft || ev.button == MouseButton::kRight)) {
                const bool left = ev.button == MouseButton::kLeft;
                const int staff = screen.staffAt(park, camera, mouseX, mouseY);
                const int object = onGround ? screen.objectAt(park, under) : -1;
                menuObject = -1;
                menuStaff = 0;
                // A visitor's window, clicking them.
                if (const int visitor = left && !staff ? screen.visitorAt(park, camera, mouseX, mouseY) : 0) {
                    screen.visitorWindow().open(visitor);
                    continue;
                }
                if (staff) {
                    if (left) {
                        screen.staffWindow().open(staff);
                    } else {
                        menuStaff = staff;
                        const Park::StaffMember *m = park.staffMember(staff);
                        screen.popup().open(
                            m ? screen.hireWindow().nameOf(m->kind, m->name) : std::string(),
                            {m && m->patrolling ? kClearPatrolItem : kSetPatrolItem, kTrainItem, kFireItem}, mouseX,
                            mouseY);
                    }
                    continue;
                }
                // A special ride waiting to be activated: its window, by
                // either click (guessed).
                if (object >= 0 && park.awaitsActivation(size_t(object))) {
                    screen.activateRide().open(size_t(object), screen.nameOf(park, size_t(object)));
                    continue;
                }
                if (object >= 0) {
                    const std::vector<uint32_t> items = objectItems(size_t(object));
                    if (left && park.queueOf(size_t(object)) >= 0) {
                        screen.attractionWindow().open(park, size_t(object), screen.nameOf(park, size_t(object)),
                                                       screen.modelOf(park, size_t(object)));
                    } else if (!left && !items.empty()) {
                        menuObject = object;
                        const Park::PlacedObject &o = park.objects()[size_t(object)];
                        const Attractions::Attraction *a = park.attractions().find(o.id);
                        screen.popup().open(screen.nameOf(park, size_t(object)), items, mouseX, mouseY,
                                            {{3, std::to_string(o.level + 1)},
                                             {25, std::to_string(a ? a->price : 0)},
                                             {27, std::to_string(park.scrapValue(size_t(object)))}});
                    }
                    if (left || !items.empty())
                        continue;
                }
                if (onGround && onGates(under)) {
                    if (left)
                        screen.gateWindow().open();
                    else
                        screen.popup().open(screen.kit().ui(kParkGatesItem),
                                            {kEntryPriceItem, park.parkOpen() ? kCloseParkItem : kOpenParkItem}, mouseX,
                                            mouseY);
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
                // Esc opens the game's menu, which leaves the game.
                if (ev.key == key::kEscape) {
                    screen.gameMenu().open();
                    continue;
                }
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
        screen.hireWindow().advance(uint32_t(now - last));
        screen.attractionWindow().advance(uint32_t(now - last));
        screen.staffWindow().advance(uint32_t(now - last));
        last = now;

        const uint32_t choice = screen.hud().takeChoice();
        if (choice == ParkHud::kBuildAttractionItem)
            screen.buyWindow().open(park.attractions(), Attractions::Kind::kRide, park);
        if (choice == ParkHud::kHireStaffItem)
            screen.hireWindow().open(park, Park::StaffKind::kJanitor);
        if (choice == kListStaffItem)
            screen.staffList().open(Park::StaffKind::kJanitor);
        if (choice == kTrainStaffItem)
            screen.trainingWindow().open();
        if (choice == kStaffFinanceItem)
            screen.staffFinance().open();
        if (choice == kParkGatesItem)
            screen.gateWindow().open();
        if (choice == ParkHud::kResearchLabMenuItem)
            screen.researchLab().open();
        if (choice == kFinancialStatisticsItem)
            screen.financeStats().open();
        if (choice == kLoansItem)
            screen.loansWindow().open();
        if (choice == kSharePortfolioItem)
            screen.portfolio().open(screen.playerName(), screen.playerRank());
        if (choice == kAttractionFinanceItem)
            screen.attractionFinance().open();
        // The park's messages, one at a time; once the bankruptcy's letter
        // is put away, the park's over.
        screen.takeMessages(park);
        if ((park.bankrupt() || park.fired()) && !screen.messagesWaiting())
            return true;
        screen.hud().setResearchLab(park.hasResearchLab());
        if (choice == kCloseParkItem)
            park.setParkOpen(!park.parkOpen());
        // Locate Staff: the camera to each in turn.
        if (choice == kListAttractionsItem)
            screen.attractionList().open();
        if (choice == kVisitorStatisticsItem)
            screen.visitorStats().open();
        if (choice == kListVisitorsItem)
            screen.visitorList().open();
        if (choice == kParkStatisticsItem)
            screen.parkStats().open();
        if (choice == kParkMapItem)
            screen.parkMap().open();
        if (choice == kOverviewMapItem)
            screen.overviewMap().open(screen.worlds(park), park.theme());
        if (choice == kVisitorOpinionItem)
            screen.parkMap().open(ParkMapWindow::Mode::kOpinion);
        if (choice == kParkAwardsItem)
            screen.awards().open(screen.kit().worldName(park.theme()));
        if (choice == ParkHud::kMessageCenterItem) {
            screen.messageCenter().open();
            screen.markRead();
        }
        // Locate Visitors: the camera to each in the park in turn.
        if (choice == kLocateVisitorsItem) {
            std::vector<const Park::Visitor *> in;
            for (const Park::Visitor &v : park.visitorsAbout())
                if (v.inPark)
                    in.push_back(&v);
            if (!in.empty()) {
                locatedVisitor = (locatedVisitor + 1) % in.size();
                camera.target = {in[locatedVisitor]->x, 0, in[locatedVisitor]->z};
            }
        }
        // The game's menu stops the park's clock while it's open.
        park.setPaused(screen.gameMenu().isOpen());
        if (choice == kLocateStaffItem && !park.staff().empty()) {
            located = (located + 1) % park.staff().size();
            const Park::StaffMember &m = park.staff()[located];
            camera.target = {m.x, 0, m.z};
        }
        // A wage demand comes up of itself.
        if (const Park::WageDemand *demand = park.wageDemand(); demand && !screen.wageDemand().isOpen())
            screen.wageDemand().open(demand->raise);
        // A new mission's briefing shows of itself, once the messages are read.
        if (park.mission().number != shownMission && !screen.messagesWaiting()) {
            shownMission = park.mission().number;
            wantObjectives = wantObjectives || shownMission >= 0;
        }
        const bool challenges = choice == kChallengesItem;
        if ((choice == ParkHud::kObjectivesItem || challenges || std::exchange(wantObjectives, false)) &&
            showObjectives) {
            park.setPaused(true);
            const ParkScreen::Camera now = camera;
            const bool open = showObjectives([&](Surface &s) { screen.draw(s, rasterizer, park, now); }, challenges);
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
        // A patrol area being set shows as the footprint does, from its
        // first corner to the cell under the mouse.
        if (patrolling && patrolCorner && screen.cellAt(camera, mouseX, mouseY, under)) {
            Park::Footprint area;
            for (int y = std::min(patrolCorner->y, under.y); y <= std::max(patrolCorner->y, under.y); ++y)
                for (int x = std::min(patrolCorner->x, under.x); x <= std::max(patrolCorner->x, under.x); ++x)
                    area.cells.push_back({x, y});
            footprint = std::move(area);
            fits = true;
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
