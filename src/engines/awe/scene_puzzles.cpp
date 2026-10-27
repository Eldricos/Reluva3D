// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's puzzle screens (a scene's PUZZ record): the torn letter
// (0x43ec00), the baggage car's safe (0x434390) and the puzzle box
// (0x433290). Each sees a click before the hotspot under it, every left
// button release, the scene's "action" verbs and conditions. See
// docs/formats/awe/puzzles.md.

#include "engines/awe/viewers.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace reluva::awe {

namespace {

constexpr float kDegree = 0.0174532924f;

// The C library's rand() (MSVC's), as the game shuffles with it.
int msvcRand(unsigned& seed) {
    seed = seed * 214013u + 2531011u;
    return int((seed >> 16) & 0x7fff);
}

bool isPicture(const std::string& file) {
    const std::string f = fs::toLower(file);
    for (const char* e : {".jpg", ".png", ".tga"})
        if (f.size() > 4 && f.compare(f.size() - 4, 4, e) == 0)
            return true;
    return false;
}

// A tag's value, 0 if it isn't set.
int tagOr0(const GameState& state, const std::string& name) {
    const auto it = state.tags.find(name);
    return it == state.tags.end() ? 0 : it->second;
}

// Whether a point is inside a polygon of (x, y) pairs.
bool insidePolygon(const std::vector<std::pair<uint32_t, uint32_t>>& pts, float x, float y) {
    bool inside = false;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const float xi = float(int32_t(pts[i].first)), yi = float(int32_t(pts[i].second));
        const float xj = float(int32_t(pts[j].first)), yj = float(int32_t(pts[j].second));
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}

// Torn letter: where pieces snap (0x43f210) and where they're checked
// (0x43f3b0: one more in x).
constexpr int kSnapX[4] = {554, 680, 805, 931}, kSnapY[5] = {82, 208, 334, 460, 586};
constexpr int kColEdge[3] = {617, 742, 868}, kRowEdge[4] = {145, 271, 397, 523};

// Puzzle box: where its panels are drawn, its pieces' arrows.
constexpr int kBoxX = 89, kBoxY = 130;
const char* const kEndArrows[4] = {"arrowendl", "arrowendr", "arrowendu", "arrowendd"};
const char* const kSideArrows[4] = {nullptr, nullptr, "arrowsideu", "arrowsided"};
const char* const kLidArrows[4] = {"arrowtopf", "arrowtopb", nullptr, nullptr};

} // namespace

const Ac2Record* ScenePage::puzzleObject(const std::string& file, int n) const {
    // The n-th object whose file is that (0x436660 by its file's name).
    for (const Ac2Record* o : scene_.objects()) {
        std::string f = fs::toLower(o->text("file"));
        f = f.substr(f.find_last_of('\\') + 1);
        if (f == file && n-- == 0)
            return o;
    }
    return nullptr;
}

bool ScenePage::hitPicture(const Ac2Record* o, float x, float y, int px, int py) const {
    // Inside its picture placed at (px, py), where it isn't transparent
    // (0x437f70).
    if (!o)
        return false;
    const image::Image& img = const_cast<ScenePage*>(this)->picture(o->text("file"));
    const int u = int(std::floor(x)) - px, v = int(std::floor(y)) - py;
    return img.w > 0 && u >= 0 && v >= 0 && u < img.w && v < img.h && img.pixel(u, v)[3] != 0;
}

void ScenePage::openPuzzle(const std::string& kind) {
    closePuzzle();
    unsigned& seed = puzzleSeed_;
    if (kind == "tornletter") {
        // 0x43ec00: the 20 pieces; their places and turns kept as text tags
        // ("paper01.png" = "x y z turn"); the first time, shuffled among
        // the authored places, each turned 0, 90, 180 or 270 degrees.
        torn_ = std::make_unique<TornLetter>();
        TornLetter& t = *torn_;
        struct Place {
            float x, y, z;
            bool used;
        };
        std::vector<Place> places;
        for (int i = 0; i < 20; i++) {
            char name[16];
            std::snprintf(name, sizeof name, "paper%02d.png", i + 1);
            const Ac2Record* o = puzzleObject(name);
            t.pieces.push_back(o);
            places.push_back({o ? o->f("x") : 0, o ? o->f("y") : 0, o ? o->f("z") : 0, false});
            if (o)
                t.topz = std::max(t.topz, o->f("z"));
        }
        for (int i = 0; i < 20; i++) {
            const Ac2Record* o = t.pieces[size_t(i)];
            if (!o)
                continue;
            PictureOverride p;
            const image::Image& img = picture(o->text("file"));
            p.cx = float(img.w / 2), p.cy = float(img.h / 2);
            const std::string key = fs::toLower(o->text("file").substr(o->text("file").find_last_of('\\') + 1));
            const auto kept = state_.texts.find(key);
            if (kept != state_.texts.end()) {
                std::sscanf(kept->second.c_str(), "%f %f %f %f", &p.x, &p.y, &p.depth, &p.roll);
            } else {
                int r;
                do
                    r = msvcRand(seed) % 20;
                while (places[size_t(r)].used || !t.pieces[size_t(r)]);
                places[size_t(r)].used = true;
                p.x = places[size_t(r)].x, p.y = places[size_t(r)].y, p.depth = places[size_t(r)].z;
                p.roll = float(msvcRand(seed) % 4) * 90 * kDegree;
            }
            overrides_[o] = p;
            saveTornPiece(i);
        }
        if (state_.tags.count("solved_tornletter")) {
            // Solved: the whole letter, the pieces gone.
            for (const Ac2Record* o : t.pieces)
                if (o)
                    showObject(objectKeys(*o).back(), false);
            showObject("paperfull", true);
            t.solved = true;
        } else if (const auto top = state_.texts.find("topz"); top != state_.texts.end()) {
            t.topz = float(std::atof(top->second.c_str()));
        }
    } else if (kind == "safe") {
        // 0x434390: the dial, at 0 every visit (the game reads back tags it
        // never writes).
        safe_ = std::make_unique<Safe>();
        safe_->dial = puzzleObject("baggage_safe.dial.png");
        if (safe_->dial) {
            PictureOverride p;
            const image::Image& img = picture(safe_->dial->text("file"));
            p.x = safe_->dial->f("x"), p.y = safe_->dial->f("y");
            p.depth = safe_->dial->f("depth") != 0 ? safe_->dial->f("depth") : safe_->dial->f("z");
            p.cx = float(img.w / 2), p.cy = float(img.h / 2);
            overrides_[safe_->dial] = p;
        }
    } else if (kind == "puzzlebox") {
        // 0x433290: the box's state from its tags.
        box_ = std::make_unique<PuzzleBox>();
        PuzzleBox& b = *box_;
        b.step = tagOr0(state_, "puzzlebox_moveindex");
        b.state[1] = tagOr0(state_, "puzzlebox_endupper");
        b.state[2] = tagOr0(state_, "puzzlebox_endlower");
        b.state[3] = tagOr0(state_, "puzzlebox_side");
        b.state[4] = tagOr0(state_, "puzzlebox_top");
        b.selected = tagOr0(state_, "puzzlebox_arrowpiece");
        b.misses = tagOr0(state_, "puzzlebox_misses");
    }
}

void ScenePage::closePuzzle() {
    torn_.reset();
    safe_.reset();
    box_.reset();
    overrides_.clear();
}

bool ScenePage::puzzleCondition(const std::string& word) const {
    if (torn_)
        // 0x43efc0: the arrows act on a piece picked.
        return !torn_->solved && (word == "left" || word == "right") && torn_->selected >= 0;
    if (safe_)
        // 0x4345f0: "open" once the three numbers are in.
        return word == "open" ? safe_->entered == 3 : true;
    return false; // the puzzle box has none
}

bool ScenePage::puzzleDraws(const Ac2Record& o) const {
    // The puzzle box draws the opening lid and the locket itself, after
    // its panels (0x4336b0).
    if (!box_)
        return false;
    const std::string file = fs::toLower(o.text("file"));
    return file.find("topopen") != std::string::npos || file.find("exalocket") != std::string::npos;
}

// --- Torn letter ---------------------------------------------------------------------

void ScenePage::saveTornPiece(int i) {
    const Ac2Record* o = torn_->pieces[size_t(i)];
    const PictureOverride& p = overrides_[o];
    char buf[96];
    std::snprintf(buf, sizeof buf, "%g %g %g %g", p.x, p.y, p.depth, p.roll);
    state_.texts[fs::toLower(o->text("file").substr(o->text("file").find_last_of('\\') + 1))] = buf;
    std::snprintf(buf, sizeof buf, "%g", torn_->topz);
    state_.texts["topz"] = buf;
}

void ScenePage::snapTornPiece(int i) {
    // 0x43f210: dropped inside the frame, to the nearest cell; else kept
    // on the screen.
    PictureOverride& p = overrides_[torn_->pieces[size_t(i)]];
    const int cx = int(p.x + p.cx), cy = int(p.y + p.cy);
    if (cx >= 491 && cx < 992 && cy >= 18 && cy < 648) {
        int col = 0, row = 0;
        for (int e : kColEdge)
            col += cx > e;
        for (int e : kRowEdge)
            row += cy > e;
        p.x = float(kSnapX[col]) - p.cx;
        p.y = float(kSnapY[row]) - p.cy;
        return;
    }
    if (cx < 0)
        p.x -= float(cx);
    if (cx > 1024)
        p.x -= float(cx - 1024);
    if (cy < 0)
        p.y -= float(cy);
    if (cy > 700)
        p.y -= float(cy - 700);
}

bool ScenePage::checkTornLetter() {
    // 0x43f3b0: each piece within 5 pixels of its cell (one right of where
    // they snap) and within 15 degrees of upright.
    TornLetter& t = *torn_;
    for (int i = 0; i < 20; i++) {
        const Ac2Record* o = t.pieces[size_t(i)];
        if (!o)
            return false;
        const PictureOverride& p = overrides_[o];
        if (std::abs(int(p.x + p.cx) - (kSnapX[i % 4] + 1)) > 5 || std::abs(int(p.y + p.cy) - kSnapY[i / 4]) > 5)
            return false;
        int a = int(int16_t(int(p.roll * 57.2957764f))) % 360;
        if (a < 0)
            a += 360;
        if (a > 15 && a < 345)
            return false;
    }
    t.solved = true;
    if (t.selected >= 0)
        overrides_[t.pieces[size_t(t.selected)]].tint = false;
    t.selected = -1;
    runner_->run("fade in paperfull, tag solved_tornletter, talk gen_tornpaper 2");
    for (const Ac2Record* o : t.pieces)
        state_.texts.erase(fs::toLower(o->text("file").substr(o->text("file").find_last_of('\\') + 1)));
    state_.texts.erase("topz");
    return true;
}

// --- Safe ------------------------------------------------------------------------------

void ScenePage::turnDial(int direction) {
    // A number a step (3.6 degrees): right counts down, left up.
    Safe& s = *safe_;
    PictureOverride& p = overrides_[s.dial];
    if (direction == 1)
        p.roll += 3.6f * kDegree, s.number--;
    else
        p.roll -= 3.6f * kDegree, s.number++;
    s.number = (s.number + 100) % 100;
    playSound("1safeclick");
}

void ScenePage::writeSafeTags() {
    // Written but never read back (0x434750); kept for saved games.
    state_.tags["safecombo_dial"] = safe_->number;
    state_.tags["safecombo_index"] = safe_->entered;
    state_.tags["safecombo_dir"] = safe_->expected;
}

// --- Puzzle box -----------------------------------------------------------------------

std::string ScenePage::boxEndPicture() const {
    // 0x433f80: the near end as its two halves stand.
    const int u = box_->state[1], l = box_->state[2];
    if (u == 4 || l == 4)
        return "endtlbrdown.png";
    static const struct {
        int u, l;
        const char* file;
    } kEnds[] = {{0, 0, "end.png"},   {1, 0, "endtl.png"}, {0, 2, "endbr.png"},  {1, 2, "endtlbr.png"},
                 {2, 0, "endtr.png"}, {0, 1, "endbl.png"}, {2, 1, "endtrbl.png"}};
    for (const auto& e : kEnds)
        if (e.u == u && e.l == l)
            return e.file;
    return {};
}

std::string ScenePage::boxSidePicture() const {
    const int s = box_->state[3];
    return s == 0 ? "side.png" : s == 3 ? "sideup01.png" : s == 7 ? "sideup02.png" : std::string();
}

int ScenePage::boxArrowAt(int x, int y) const {
    // The selected piece's first arrow under the point (0x433ba0); the end
    // arrows moved up for the upper half, down for the lower.
    const int p = box_->selected;
    const char* const* names = p == 1 || p == 2 ? kEndArrows : p == 3 ? kSideArrows : p == 4 ? kLidArrows : nullptr;
    if (!names)
        return 0;
    for (int a = 0; a < 4; a++) {
        if (!names[a])
            continue;
        const Ac2Record* o = puzzleObject(std::string(names[a]) + ".png");
        if (!o)
            continue;
        const int dy = p == 1 ? -50 : p == 2 ? 50 : 0;
        if (hitPicture(o, float(x), float(y), int(o->f("x")), int(o->f("y")) + dy))
            return a + 1;
    }
    return 0;
}

bool ScenePage::boxMovePossible(int piece, int arrow) const {
    // 0x4338e0: an end half can't slide further the way it has gone.
    if (piece >= 3)
        return true;
    const int s = box_->state[piece];
    return !(s == 1 && arrow == 1) && !(s == 2 && arrow == 2);
}

bool ScenePage::boxMoveCorrect(int piece, int arrow) const {
    // 0x433950: the solution, step by step (pairs either way round).
    const PuzzleBox& b = *box_;
    const int u = b.state[1], l = b.state[2];
    switch (b.step) {
    case 0:
    case 1:
        return (piece == 1 && arrow == 2 && u == 0) || (piece == 2 && arrow == 1 && l == 0);
    case 2:
    case 5:
        return piece == 3 && arrow == 3;
    case 3:
    case 4:
        return (piece == 1 && arrow == 1 && u == 2) || (piece == 2 && arrow == 2 && l == 1);
    case 6:
    case 7:
        return (piece == 1 && arrow == 1 && u == 0) || (piece == 2 && arrow == 2 && l == 0);
    case 8:
        return (piece == 1 || piece == 2) && arrow == 4;
    case 9:
        return piece == 4 && arrow == 1;
    default:
        return false;
    }
}

void ScenePage::writeBoxTags() {
    const PuzzleBox& b = *box_;
    state_.tags["puzzlebox_moveindex"] = b.step;
    state_.tags["puzzlebox_endupper"] = b.state[1];
    state_.tags["puzzlebox_endlower"] = b.state[2];
    state_.tags["puzzlebox_side"] = b.state[3];
    state_.tags["puzzlebox_top"] = b.state[4];
    state_.tags["puzzlebox_arrowpiece"] = b.selected;
    state_.tags["puzzlebox_misses"] = b.misses;
}

void ScenePage::drawPuzzleBox() {
    // 0x4336b0, after the scene: the lid, the side, the end, each with the
    // picked piece's arrows (and the one under the mouse lit if it may
    // move); solved, the opening lid with the side over it, then the
    // locket.
    const PuzzleBox& b = *box_;
    auto at = [&](const std::string& file, int x, int y) {
        if (const Ac2Record* o = puzzleObject(file))
            image::blit(ctx_.screen, picture(o->text("file")), x, y);
    };
    auto arrows = [&](int piece, const char* const* names) {
        if (b.selected != piece)
            return;
        const int dy = piece == 1 ? -50 : piece == 2 ? 50 : 0;
        const int over = boxArrowAt(lastMouseX_, lastMouseY_);
        for (int a = 0; a < 4; a++) {
            if (!names[a])
                continue;
            const Ac2Record* o = puzzleObject(std::string(names[a]) + ".png");
            if (!o)
                continue;
            const int x = int(o->f("x")), y = int(o->f("y")) + dy;
            image::blit(ctx_.screen, picture(o->text("file")), x, y);
            if (over == a + 1 && boxMovePossible(piece, a + 1))
                at(std::string(names[a]) + "o.png", x, y);
        }
    };
    if (!b.solved) {
        if (b.state[4] == 0)
            at("top.png", kBoxX, kBoxY);
        arrows(4, kLidArrows);
    }
    at(boxSidePicture(), kBoxX, kBoxY);
    arrows(3, kSideArrows);
    at(boxEndPicture(), kBoxX, kBoxY);
    arrows(b.selected == 2 ? 2 : 1, kEndArrows);
    if (!b.solved)
        return;
    // The lid opening, the side in front of it within (440, 0) 583 x 436.
    for (const Ac2Record* o : scene_.objects()) {
        const std::string file = fs::toLower(o->text("file"));
        if (file.find("topopen") == std::string::npos || !shown(*o) || hidden(*o))
            continue;
        if (const auto loop = loops_.find(o); loop != loops_.end() && loop->second.frame.w)
            image::blit(ctx_.screen, loop->second.frame, int(o->f("x")), int(o->f("y")));
        if (const Ac2Record* side = puzzleObject(boxSidePicture())) {
            const image::Image& img = picture(side->text("file"));
            image::Image clip;
            clip.w = 583, clip.h = 436;
            clip.rgba.assign(size_t(clip.w) * clip.h * 4, 0);
            for (int y = 0; y < clip.h; y++)
                for (int x = 0; x < clip.w; x++) {
                    const int u = 440 + x - kBoxX, v = y - kBoxY;
                    if (u >= 0 && v >= 0 && u < img.w && v < img.h)
                        std::copy_n(img.pixel(u, v), 4, clip.pixel(x, y));
                }
            image::blit(ctx_.screen, clip, 440, 0);
        }
    }
    for (const Ac2Record* o : scene_.objects())
        if (fs::toLower(o->text("file")).find("exalocket") != std::string::npos && shown(*o) && !hidden(*o))
            image::blit(ctx_.screen, picture(o->text("file")), int(o->f("x")), int(o->f("y")));
}

// --- Input -------------------------------------------------------------------------------

void ScenePage::puzzleClick(int x, int y) {
    if (torn_ && !torn_->solved) {
        // 0x43ed00: the front-most picture with something under the point,
        // a hotspot first; a piece is picked up (tinted, brought forward).
        TornLetter& t = *torn_;
        if (hotspotAt(x, y))
            return;
        const Ac2Record* best = tornPick(x, y);
        if (!best || fs::toLower(best->text("file")).find("paper") == std::string::npos)
            return;
        const auto it = std::find(t.pieces.begin(), t.pieces.end(), best);
        if (it == t.pieces.end())
            return;
        if (t.selected >= 0)
            overrides_[t.pieces[size_t(t.selected)]].tint = false;
        t.selected = int(it - t.pieces.begin());
        PictureOverride& p = overrides_[best];
        p.tint = true;
        t.dragging = true;
        if (p.depth < t.topz)
            p.depth = ++t.topz;
        playSound("mouseover2");
        t.dragX = x, t.dragY = y;
        draw();
        return;
    }
    if (box_ && !box_->solved) {
        // 0x4337b0.
        PuzzleBox& b = *box_;
        if (b.selected == 0) {
            b.selected = boxPieceAt(x, y);
            writeBoxTags();
            draw();
            return;
        }
        const int piece = b.selected, arrow = boxArrowAt(x, y);
        if (arrow == 0) {
            b.selected = 0;
        } else if (!boxMovePossible(piece, arrow)) {
            return;
        } else if (boxMoveCorrect(piece, arrow)) {
            playSound("slidepuzzle" + std::to_string(msvcRand(puzzleSeed_) % 3 + 1));
            if (b.step == 9 && piece == 4 && arrow == 1) {
                // Open (3.7).
                b.solved = true;
                playSound("puzzlelid");
                runner_->run("show topopen, wait 1.9, hide topopen, talk gen_puzzle 6, tag puzzlebox_solved, fade in "
                             "exalocket, talk gen_takeobject 1, fade out exalocket, take locket");
            } else {
                int& s = b.state[piece];
                if (s == 1 && arrow == 2)
                    s = 0;
                else if (s == 2 && arrow == 1)
                    s = 0;
                else if (s == 3 && arrow == 3)
                    s = 7;
                else
                    s = arrow;
                b.step++;
                b.misses = 0;
            }
            b.selected = 0;
        } else {
            // A wrong move: the third in a row starts again.
            if (b.misses == 2) {
                b = PuzzleBox();
                runner_->run("talk gen_puzzle 3");
            } else {
                b.misses++;
                runner_->run("talk gen_puzzle 4");
            }
            b.selected = 0;
        }
        writeBoxTags();
        draw();
    }
}

const Ac2Record* ScenePage::tornPick(int x, int y) const {
    // 0x43ed00: the front-most picture with something under the point.
    const Ac2Record* best = nullptr;
    float bestDepth = -1e9f;
    for (const Ac2Record* o : scene_.objects()) {
        if (hidden(*o) || !(testCondition(o->text("condition"), state_) || shown(*o)) ||
            !isPicture(o->text("file")))
            continue;
        const image::Image& img = const_cast<ScenePage*>(this)->picture(o->text("file"));
        if (img.w <= 0)
            continue;
        float d, ox = o->f("x"), oy = o->f("y"), roll = 0, cx = 0, cy = 0;
        if (const auto ov = overrides_.find(o); ov != overrides_.end()) {
            d = ov->second.depth, ox = ov->second.x, oy = ov->second.y;
            roll = ov->second.roll, cx = ov->second.cx, cy = ov->second.cy;
        } else {
            d = o->f("depth") != 0 ? o->f("depth") : o->f("z");
        }
        if (d <= bestDepth)
            continue;
        // The unrotated box, then the pixel turned back about the centre.
        const float px = float(x), py = float(y);
        if (px < ox || py < oy || px >= ox + float(img.w) || py >= oy + float(img.h))
            continue;
        if (o->i("alpha") != 0) {
            const float dx = px - (ox + cx), dy = py - (oy + cy);
            const float c = std::cos(roll), s = std::sin(roll);
            const int u = int(std::floor(dx * c + dy * s + cx)), v = int(std::floor(-dx * s + dy * c + cy));
            if (u < 0 || v < 0 || u >= img.w || v >= img.h || img.pixel(u, v)[3] == 0)
                continue;
        }
        best = o, bestDepth = d;
    }
    return best;
}

int ScenePage::boxPieceAt(int x, int y) const {
    // A piece of the box (0x4337b0): the end (its upper half inside the
    // "endtop" outline), the side, the lid; 0 none.
    if (hitPicture(puzzleObject(boxEndPicture()), float(x), float(y), kBoxX, kBoxY)) {
        for (const Ac2Record* o : scene_.objects())
            if (o->tag == "SPOT" && fs::toLower(o->text("name")) == "endtop")
                if (const Ac2Value* poly = o->field("listD");
                    poly && poly->numbers.size() >= 3 && insidePolygon(poly->numbers, float(x), float(y)))
                    return 1;
        return 2;
    }
    if (hitPicture(puzzleObject(boxSidePicture()), float(x), float(y), kBoxX, kBoxY))
        return 3;
    if (hitPicture(puzzleObject("top.png"), float(x), float(y), kBoxX, kBoxY))
        return 4;
    return 0;
}

void ScenePage::puzzleRelease(int x, int y) {
    (void)x, (void)y;
    if (torn_ && torn_->selected >= 0 && torn_->dragging) {
        // 0x43ee40: dropped, snapped, kept, checked.
        torn_->dragging = false;
        snapTornPiece(torn_->selected);
        saveTornPiece(torn_->selected);
        playSound("mouseover2");
        checkTornLetter();
        draw();
    }
    if (safe_) {
        // 0x434620: a number counts if the dial was turned the way expected
        // and stopped on it.
        Safe& s = *safe_;
        static constexpr int kCombination[3] = {6, 6, 6};
        if (s.spinning == s.expected && s.entered < 3 && s.number == kCombination[s.entered]) {
            s.entered++;
            if (s.entered == 3) {
                runner_->run("sound safeclick, talk gen_safe 2");
                s.spinning = 0;
                writeSafeTags();
                return;
            }
            s.expected = s.expected == 2 ? 1 : 2;
        }
        s.spinning = 0;
        writeSafeTags();
    }
}

void ScenePage::puzzleUpdate(float seconds) {
    if (torn_ && torn_->dragging && torn_->selected >= 0) {
        // The piece follows the mouse (0x43ecc0).
        PictureOverride& p = overrides_[torn_->pieces[size_t(torn_->selected)]];
        if (lastMouseX_ != torn_->dragX || lastMouseY_ != torn_->dragY) {
            p.x += float(lastMouseX_ - torn_->dragX);
            p.y += float(lastMouseY_ - torn_->dragY);
            torn_->dragX = lastMouseX_, torn_->dragY = lastMouseY_;
            draw();
        }
    }
    if (safe_ && safe_->spinning > 0) {
        // Held: on after a quarter second, faster each step, to about 30
        // numbers a second (0x434690).
        Safe& s = *safe_;
        s.timer += seconds;
        while (s.timer >= s.interval) {
            s.timer -= s.interval;
            turnDial(s.spinning);
            if (s.interval > 0.035f)
                s.interval *= 0.75f;
        }
        draw();
    }
}

void ScenePage::puzzleAction(const std::string& word) {
    if (torn_ && torn_->selected >= 0) {
        // 0x43eeb0: the picked piece turned a quarter, about its centre.
        PictureOverride& p = overrides_[torn_->pieces[size_t(torn_->selected)]];
        float deg = p.roll / kDegree + (word == "left" ? 90.0f : -90.0f);
        if (deg >= 360)
            deg -= 360;
        if (deg < 0)
            deg += 360;
        p.roll = deg * kDegree;
        saveTornPiece(torn_->selected);
        playSound(msvcRand(puzzleSeed_) & 1 ? "paperpuzzle1" : "paperpuzzle2");
        checkTornLetter();
        draw();
        return;
    }
    if (safe_ && safe_->dial) {
        // 0x434500: right counts down, left up; turning the other way once
        // a number is in starts again.
        Safe& s = *safe_;
        const int dir = word == "right" ? 1 : 2;
        s.spinning = dir;
        turnDial(dir);
        if ((s.spinning != s.expected || s.entered == 3) && s.entered > 0) {
            runner_->run("talk gen_safe 1");
            s.spinning = 0, s.entered = 0, s.expected = 2, s.number = 0;
            overrides_[s.dial].roll = 0;
        }
        s.timer = 0, s.interval = 0.25f;
        writeSafeTags();
        draw();
    }
}

// --- For tools: the puzzle solved as a player would ------------------------------------

std::string ScenePage::puzzleOpen() const {
    return torn_ ? "tornletter" : safe_ ? "safe" : box_ ? "puzzlebox" : "";
}

bool ScenePage::puzzleSolved() const {
    return (torn_ && torn_->solved) || (safe_ && safe_->entered == 3) || (box_ && box_->solved);
}

bool ScenePage::puzzleStep() {
    // The mouse pressed and let go where the puzzle's own checks say: a
    // point on what's to be picked, with no hotspot over it.
    auto mouse = [&](EventType type, int x, int y) {
        Event ev;
        ev.type = type;
        ev.mouseX = x, ev.mouseY = y;
        ev.button = type == EventType::MouseMove ? MouseButton::None : MouseButton::Left;
        ev.clicks = type == EventType::MouseDown ? 1 : 0;
        handleEvent(ev);
    };
    auto free = [&](int x, int y) {
        const Ac2Record* o = pickAllowed(y) ? pickAt(x, y) : nullptr;
        return !hotspotAt(x, y) && !(o && clickable(*o));
    };
    // The first point (every 3 pixels) that passes `test`.
    auto find = [&](const std::function<bool(int, int)>& test, int& fx, int& fy) {
        for (int y = 1; y < kScreenH; y += 3)
            for (int x = 1; x < kScreenW; x += 3)
                if (test(x, y) && free(x, y)) {
                    fx = x, fy = y;
                    return true;
                }
        return false;
    };
    // A hotspot's script run, as its click runs it (the letter's and the
    // safe's arrows: "action left").
    auto arrow = [&](const std::string& word) {
        for (const Ac2Record* o : scene_.objects()) {
            if (o->tag != "SPOT" || hidden(*o))
                continue;
            bool match = fs::toLower(o->text("script")) == "action " + word;
            if (const Ac2Value* alternatives = o->field("listB"))
                for (const auto& [condition, run] : alternatives->pairs)
                    match |= fs::toLower(run) == "action " + word;
            if (match) {
                runScript(*o);
                runner_->update(0);
                return true;
            }
        }
        return false;
    };
    int x = 0, y = 0;
    if (box_) {
        // The move the solution asks for next.
        PuzzleBox& b = *box_;
        if (b.solved || !runner_->idle())
            return false;
        int piece = 0, dir = 0;
        for (int p = 1; p <= 4 && !piece; p++)
            for (int a = 1; a <= 4 && !piece; a++)
                if (boxMovePossible(p, a) && boxMoveCorrect(p, a))
                    piece = p, dir = a;
        if (!piece)
            return false;
        if (b.selected != piece) {
            // Picked (another one dropped first, by a click off its arrows).
            if (!find([&](int px, int py) { return boxPieceAt(px, py) == piece && boxArrowAt(px, py) == 0; }, x, y))
                return false;
        } else if (!find([&](int px, int py) { return boxArrowAt(px, py) == dir; }, x, y)) {
            return false;
        }
        mouse(EventType::MouseDown, x, y);
        mouse(EventType::MouseUp, x, y);
        return true;
    }
    if (torn_) {
        // Each piece in turn: picked, turned upright, dragged to its cell.
        TornLetter& t = *torn_;
        if (t.solved || !runner_->idle())
            return false;
        for (int i = 0; i < int(t.pieces.size()); i++) {
            const Ac2Record* o = t.pieces[size_t(i)];
            if (!o)
                return false;
            const PictureOverride p = overrides_[o];
            const int tx = kSnapX[i % 4] + 1, ty = kSnapY[i / 4];
            const bool placed = std::abs(int(p.x + p.cx) - tx) <= 5 && std::abs(int(p.y + p.cy) - ty) <= 5;
            int a = int(int16_t(int(p.roll * 57.2957764f))) % 360;
            if (a < 0)
                a += 360;
            const bool upright = a <= 15 || a >= 345;
            if (placed && upright)
                continue;
            const bool found = find([&](int px, int py) { return tornPick(px, py) == o; }, x, y);
            if (t.selected != i || !upright) {
                if (t.selected != i) {
                    if (!found)
                        return false;
                    mouse(EventType::MouseDown, x, y);
                    mouse(EventType::MouseUp, x, y);
                    return true;
                }
                return arrow("left");
            }
            if (!found)
                return false;
            const int dx = tx - int(p.x + p.cx), dy = ty - int(p.y + p.cy);
            mouse(EventType::MouseDown, x, y);
            mouse(EventType::MouseMove, x + dx, y + dy);
            puzzleUpdate(0);
            mouse(EventType::MouseUp, x + dx, y + dy);
            return true;
        }
        return false;
    }
    if (safe_) {
        // 6 left, a turn right back to 6, a turn left back to 6: a press
        // and a let go a number.
        Safe& s = *safe_;
        if (s.entered == 3 || !runner_->idle())
            return false;
        if (!arrow(s.expected == 2 ? "left" : "right"))
            return false;
        mouse(EventType::MouseUp, lastMouseX_, lastMouseY_);
        return true;
    }
    return false;
}

} // namespace reluva::awe
