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
#include <cmath>
#include <cstdio>
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
// The visitors' sprites (esprites.wad), by generation: the children's, the
// parents', the grandparents'.
constexpr std::array<std::array<const char *, 4>, 3> kVisitorSprites{{
    {"esprites/Generic/Kids/SPR_KB", "esprites/Generic/Kids/SPR_GK", "esprites/Generic/Kids/SPR_BD",
     "esprites/Generic/Kids/SPR_GB"},
    {"esprites/Generic/Parents/SPR_FA", "esprites/Generic/Parents/SPR_MO"},
    {"esprites/Generic/Grandparents/SPR_GR", "esprites/Generic/Grandparents/SPR_GM"},
}};
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
// `angle` degrees.
void drawPlaced(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &eye, const LoadedModel &model,
                const Park::Cell &at, int angle, const soft3d::Vec &light) {
    const float half = Park::kCellUnits / 2;
    const float turn = float(angle) * kRadiansPerDegree;
    const float c = std::cos(turn);
    const float s = std::sin(turn);
    // A positive angle turns east (+x) towards south (+z), as the save's
    // path corners do. Column-major: x' = c x − s z, z' = s x + c z, about
    // the cell's middle.
    const float cx = float(at.x) * Park::kCellUnits + half;
    const float cz = float(at.y) * Park::kCellUnits + half;
    const gfx::Mat4 place{
        c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, cx - (c * half - s * half), kObjectLift, cz - (s * half + c * half), 1};
    std::vector<gfx::Mat4> transforms = meshTransforms(model, -1, 0);
    for (gfx::Mat4 &m : transforms)
        m = gfx::multiply(place, m);
    drawMeshes(rasterizer, eye, model, true, light, &transforms);
}

} // namespace

Error ParkScreen::load(const SearchSet &files, const Park &park) {
    if (Error err = _world.load(files, park.folder()))
        return err;
    _hud = false;
    _furnitureRead = !_furniture.load(files);
    loadObjects(files, park);
    _pathsRead = true;
    for (size_t i = 0; i < kPathTextures.size(); ++i)
        if (loadPicture(files, park.folder() + "/terrain/pathtex/" + kPathTextures[i] + ".fsh", _pathTextures[i]))
            _pathsRead = false;
    for (size_t generation = 0; generation < kVisitorSprites.size(); ++generation) {
        _visitorLooks[generation].clear();
        for (const char *name : kVisitorSprites[generation]) {
            LoadedSprite sprite;
            if (name && !sprite.load(files, name))
                _visitorLooks[generation].push_back(std::move(sprite));
        }
    }
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
        if (!model.load(files, name.substr(0, name.size() - 4) + ".MD2")) {
            _objectModels.emplace(id, std::move(model));
            wanted.erase(id);
        }
    }
    for (const auto &[piece, file] : kQueueModels) {
        LoadedModel model;
        if (!model.load(files, prefix + "queue/" + file))
            _queueModels.emplace(int(piece), std::move(model));
    }
}

void ParkScreen::drawObjects(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &eye, const Park &park) const {
    const soft3d::Vec light = _world.hasSky ? soft3d::normalize(toViewerWorld(_world.sky.light)) : defaultLight();
    for (const Park::PlacedObject &o : park.objects()) {
        const auto model = _objectModels.find(o.id);
        if (model != _objectModels.end())
            drawPlaced(rasterizer, eye, model->second, o.at, o.angle, light);
    }
    // The queue being laid drawn as it would be, in its own's place.
    for (const Park::Queue &queue : park.queues()) {
        const bool laying = _queuePreview && _queuePreview->entrance == queue.entrance;
        for (const Park::QueuePiece &q : Park::queuePieces(laying ? *_queuePreview : queue)) {
            const auto model = _queueModels.find(int(q.model));
            if (model != _queueModels.end())
                drawPlaced(rasterizer, eye, model->second, q.at, q.angle, light);
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

void ParkScreen::drawVisitors(Surface &screen, const soft3d::OrbitCamera &eye, const Park &park, float yaw) const {
    // Farthest first, each at the point it stands on.
    struct Seen {
        float depth;
        const Park::Visitor *visitor;
        soft3d::ScreenVertex at;
    };
    std::vector<Seen> seen;
    for (const Park::Visitor &v : park.visitorsAbout()) {
        const soft3d::ViewVertex view = eye.toView(toViewerWorld({v.x, 0, v.z}));
        if (view.z >= eye.nearZ)
            seen.push_back({view.z, &v, eye.project(view)});
    }
    std::sort(seen.begin(), seen.end(), [](const Seen &a, const Seen &b) { return a.depth > b.depth; });
    for (const Seen &s : seen) {
        const Park::Visitor &v = *s.visitor;
        const std::vector<LoadedSprite> &looks = _visitorLooks[size_t(std::clamp(v.generation, 0, 2))];
        if (looks.empty()) {
            mark(screen, eye, {v.x, 1, v.z}, 2, kVisitorColour);
            continue;
        }
        const LoadedSprite &look = looks[v.look % looks.size()];
        const bool walking = v.headingX != 0 || v.headingZ != 0;
        const auto &animation = look.set.animations()[walking ? kWalkAnimation : kStandAnimation];
        // The heading as the camera sees it: 0 away from it, a quarter
        // turn to its right.
        const float forward = v.headingX * std::sin(yaw) + v.headingZ * std::cos(yaw);
        const float right = v.headingX * std::cos(yaw) - v.headingZ * std::sin(yaw);
        const SpritePose pose = spritePose(animation, walking ? std::atan2(right, forward) : 3.14159265f,
                                           walking ? uint32_t(v.walked / kUnitsPerWalkFrame) : 0);
        drawSpriteFrame(screen, look.sheet, pose.frame, s.at.x, s.at.y, kSpritePixelUnits * eye.focal / s.depth,
                        pose.mirror);
    }
}

void ParkScreen::draw(Surface &screen, soft3d::Rasterizer &rasterizer, const Park &park, const Camera &camera) const {
    std::fill(screen.pixels.begin(), screen.pixels.end(), uint8_t(0));
    const WorldView view = viewOf(camera);
    drawWorld(screen, rasterizer, _world, view);
    const soft3d::OrbitCamera eye = worldCamera(_world, view, screen.w, screen.h);
    drawPaths(rasterizer, eye, park);
    drawObjects(rasterizer, eye, park);
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
    drawVisitors(screen, eye, park, camera.yaw);
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
    const ScreenRect field = ParkHud::dateField();
    if (!_furnitureRead)
        fillRect(screen, field, Color{0, 0, 0});
    const std::string date = dateText(park.day());
    const int w = gameTextWidth(_dateFont, _characters, date);
    drawGameText(screen, _dateFont, _characters, field.x + (field.w - w) / 2, field.y + 3, date, kHudText);
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
    for (;;) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return false;
            if (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown) {
                mouseX = ev.mouseX;
                mouseY = ev.mouseY;
            }
            if (screen.hud().handle(ev))
                continue;
            Park::Cell under;
            const bool onGround = screen.cellAt(camera, mouseX, mouseY, under);
            const bool laying = screen.hud().tool() == ParkHud::Tool::kPath;
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
        screen.hud().advance(uint32_t(now - last));
        last = now;

        if (screen.hud().takeChoice() == ParkHud::kObjectivesItem && showObjectives) {
            park.setPaused(true);
            const ParkScreen::Camera now = camera;
            const bool open = showObjectives([&](Surface &s) { screen.draw(s, rasterizer, park, now); });
            park.setPaused(false);
            last = sys.getMillis();
            if (!open)
                return false;
        }
        if (screen.hud().tool() != ParkHud::Tool::kPath) {
            pathStart.reset();
            queueLaying = -1;
        }
        Park::Cell under;
        std::vector<Park::Cell> preview;
        std::optional<Park::Queue> queuePreview;
        if (screen.hud().tool() == ParkHud::Tool::kPath && !screen.hud().covers(mouseX, mouseY) &&
            screen.cellAt(camera, mouseX, mouseY, under)) {
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
