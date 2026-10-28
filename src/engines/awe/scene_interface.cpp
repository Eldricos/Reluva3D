// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's interface, as motoe.exe draws it over a game's scenes
// (docs/formats/awe/interface.md): the bar along the top (0x417290), the
// inventory window and its six pages (0x4190d0...), the item in hand
// (0x415650), and the passport or fingerprint card a script holds up
// (0x417ba0).

#include "engines/awe/viewers.h"

#include "common/fs.h"
#include "engines/awe/tables.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

namespace {

// The bar's sections' right ends (0x48d650): the bag, the five cars, the
// hat; and the cars' words in a location's name (0x417760).
constexpr int kBarEnds[7] = {75, 250, 422, 595, 771, 950, 1024};
constexpr const char* kCars[5] = {"baggage", "restaurant", "salon", "calais", "athens"};

// The window's top; its pages' pictures (0x41af10, 0x41a110, 0x4182f0,
// 0x41e5e0, 0x41cea0, 0x41d6d0).
constexpr int kTop = 10;
constexpr const char* kPages[6] = {"windowinventory", "windowexamine",   "windowcombine",
                                   "windowscrapbook", "windowpassports", "windowfingerprints"};

// The scrapbook's sections, as contents.txt lists them (0x41ec60).
constexpr const char* kSections[9] = {"contents", "clippings", "instructions", "suspects", "timetable",
                                      "maps",     "docs",      "barnaby",      "challenge"};
// The suspects' passports and fingerprint cards, by their place in
// suspects.txt (0x48f268, 0x48f2b4).
constexpr const char* kSuspectPasses[] = {
    "passcount",   "passcount",    "passarbuthnot", "passdebenham", "passdragomiroff", "passfoscarelli", "passhardman",
    "passhubbard", "passmacqueen", "passmasterman", "passohlsson",  "passschmidt",     "passmichel",     "passconstantine",
    "passklaus",   "passlucien",   "passmatteo",    "passengineer", "passfireman"};
constexpr const char* kSuspectPrints[] = {
    "printcount",   "printcountess", "printarbuthnot", "printdebenham", "printdragomiroff", "printfoscarelli",
    "printhardman", "printhubbard",  "printmacqueen",  "printmasterman", "printohlsson",    "printschmidt",
    "printmichel",  "printconstantine", "printklaus",  "printlucien",   "printmatteo",     "printengineer",
    "printfireman"};

// The window's pages' places (the floats after their vtables).
constexpr int kArrowBack[4] = {46, 29, 76, 44}, kArrowOn[4] = {435, 29, 464, 44}; // 0x41c980
constexpr int kBlipLeft = 78, kBlipSpan = 342;                                       // 0x482814, 0x482810

bool inside(int x, int y, int l, int t, int r, int b) { return x >= l && x < r && y >= t && y < b; }

// A picture over the screen, faded by `alpha`, between columns clipL and
// clipR; the screen's alpha kept (it may be an overlay on the 3D).
void blend(Surface& dst, const image::Image& img, int x, int y, int alpha, int clipL = 0, int clipR = kScreenW) {
    if (img.w <= 0 || alpha <= 0)
        return;
    const int x0 = std::max({0, clipL - x}), x1 = std::min({img.w, clipR - x, dst.w - x});
    for (int sy = std::max(0, -y); sy < img.h && y + sy < dst.h; sy++) {
        uint8_t* row = dst.row(y + sy);
        for (int sx = x0; sx < x1; sx++) {
            const uint8_t* s = img.pixel(sx, sy);
            uint8_t* d = row + (x + sx) * 4;
            const int a = s[3] * std::min(alpha, 255) / 255, da = d[3] * (255 - a) / 255, out = a + da;
            if (out == 0)
                continue;
            for (int c = 0; c < 3; c++)
                d[c] = uint8_t((s[c] * a + d[c] * da) / out);
            d[3] = uint8_t(out);
        }
    }
}

void darken(Surface& dst, int alpha) {
    for (int y = 0; y < dst.h; y++) {
        uint8_t* row = dst.row(y);
        for (int x = 0; x < dst.w; x++) {
            uint8_t* d = row + x * 4;
            for (int c = 0; c < 3; c++)
                d[c] = uint8_t(d[c] * (255 - alpha) / 255);
            d[3] = uint8_t(alpha + d[3] * (255 - alpha) / 255);
        }
    }
}

std::string unquoted(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return s;
}

// A cell, or its translation in the next column if that has one (not "0").
std::string translated(const Table& t, size_t r, int col) {
    const std::string tr = unquoted(t.cell(r, col + 1));
    return tr.empty() || tr == "0" ? unquoted(t.cell(r, col)) : tr;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

} // namespace

// --- When it's there ------------------------------------------------------------

bool ScenePage::interfaceOn() const {
    // 0x4143c0: a scene with SHOW on (none: on), "interface on".
    if (!playing_ || interfaceOff_ || movie_ || !sceneShown_ || load_.screen || load_.state)
        return false;
    for (const Ac2Record& r : scene_.records)
        if (r.tag == "SHOW")
            return r.i("value") != 0;
    return true;
}

bool ScenePage::canAct() const {
    // 0x415fa0: no fade, nothing loading, no locked script running, no
    // dialogue box, no message, no scrapbook picture full screen.
    return playing_ && fade_ == fadeTarget_ && !load_.state && !movie_ && !(runner_->locked() && !runner_->idle()) &&
           !dialogue_->boxShown() && messageTime_ <= 0 && ui_.zoom.empty();
}

// --- Items ------------------------------------------------------------------------

ScenePage::ItemInfo ScenePage::itemInfo(const std::string& item) const {
    // Text\itemlist.txt (0x420a70): Item Name, In-game Item Description,
    // Translation, Examine Event, Examine Description, Translation,
    // Flipped Description, Translation; "0" is none.
    ItemInfo info;
    const Table& t = itemTable("itemlist.txt");
    for (size_t r = 0; r < t.rows.size(); r++)
        if (fs::toLower(unquoted(t.cell(r, 0))) == item) {
            auto none = [](std::string s) { return s == "0" ? std::string() : s; };
            info.description = none(translated(t, r, 1));
            info.event = none(unquoted(t.cell(r, 3)));
            info.examine = none(translated(t, r, 4));
            info.flipped = none(translated(t, r, 6));
            break;
        }
    return info;
}

std::string ScenePage::itemPicture(const std::string& item, const char* kind) const {
    // inventory\items\<item>\<kind><item>: "inv" a .png or else a .jpg;
    // "exa" and "ex2" the inv picture's path with \inv changed (0x41a1c0),
    // "" if there's none; "cur" a .png, "fol" a .jpg (0x41bc10).
    const std::string folder = "inventory/items/" + item + "/";
    const std::string kindS = kind;
    if (kindS == "cur" || kindS == "fol") {
        const std::string f = folder + kindS + item + (kindS == "cur" ? ".png" : ".jpg");
        return ctx_.files.has(f) ? f : std::string();
    }
    std::string inv = folder + "inv" + item + ".png";
    if (!ctx_.files.has(inv))
        inv = folder + "inv" + item + ".jpg";
    if (kindS == "inv")
        return inv;
    const std::string f = folder + kindS + item + inv.substr(inv.size() - 4);
    return ctx_.files.has(f) ? f : std::string();
}

int ScenePage::printRow(const std::string& item) const {
    // A fingerprint card with an Examine Description (whose print it
    // matches) is one lifted from a thing: the second row (0x41bd40).
    return itemInfo(item).examine.size() > 1 ? 1 : 0;
}

std::vector<std::string> ScenePage::bagItems() {
    // In the bag, just taken or in hand, by slot (0x41b7f0).
    std::vector<std::pair<int, std::string>> slots;
    for (const GameState::Item& i : state_.items)
        if (i.state >= 1 && i.state <= 3)
            slots.emplace_back(i.slot, i.name);
    std::stable_sort(slots.begin(), slots.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (auto& [slot, name] : slots)
        out.push_back(name);
    return out;
}

std::string ScenePage::itemInSlot(int slot) {
    // The item in the bag (or just taken) in that slot (0x41b790).
    for (const GameState::Item& i : state_.items)
        if ((i.state == 1 || i.state == 2) && i.slot == slot)
            return i.name;
    return {};
}

int ScenePage::itemPages() {
    // One empty page after the last used, 2 to 10 (0x41b6c0).
    int last = 0;
    for (const GameState::Item& i : state_.items)
        if ((i.state == 1 || i.state == 2) && i.slot / 16 != 10)
            last = std::max(last, i.slot / 16);
    return std::clamp(last + 2, 2, 10);
}

void ScenePage::holdItem(const std::string& item, int slot) {
    // In hand (0x41c000): the one in that slot, else the first in the bag;
    // the item held before goes back.
    releaseHeld();
    GameState::Item* in = nullptr;
    for (GameState::Item& i : state_.items)
        if (i.name == item && (i.state == 1 || i.state == 2) && (slot < 0 || i.slot == slot)) {
            in = &i;
            break;
        }
    if (!in)
        return;
    state_.held = item;
    in->state = 3;
}

void ScenePage::releaseHeld() {
    // Back in the bag (0x41c030).
    for (GameState::Item& i : state_.items)
        if (i.state == 3)
            i.state = 2;
    state_.held.clear();
}

void ScenePage::endFresh() {
    // The item just taken stops blinking: in the bag (0x41ba80).
    for (GameState::Item& i : state_.items)
        if (i.state == 1)
            i.state = 2;
    state_.fresh.clear();
    ui_.freshItem.clear();
}

void ScenePage::playItemSound(const std::string& item) {
    // sound\items\<item>.wav, or default.wav; none for passports and
    // fingerprint cards (0x419c10).
    if (startsWith(item, "pass") || startsWith(item, "print"))
        return;
    playSound(ctx_.files.has("sound/items/" + item + ".wav") ? item : "default");
}

void ScenePage::runFromInventory(const std::string& script, bool fromInventory) {
    // 0x419f20: the player stops, what was running is dropped, and the
    // script runs; its talks leave the window open if it's the window's.
    for (Character& c : characters_)
        if (c.player)
            c.path.clear();
    ui_.fromInventory = fromInventory;
    runner_->clear();
    runner_->run(script);
}

void ScenePage::talkStarting(const std::string& name) {
    // 0x43a670, once the conversation is found: the window closes unless
    // a script of its own started the talk; then, unless the name has
    // "gen_" or "com_" in it (0x43e750: anywhere, any case), the item in
    // hand goes back and the one just taken stops blinking.
    if (ui_.open && !ui_.fromInventory)
        closeInventory();
    ui_.fromInventory = false;
    const std::string n = fs::toLower(name);
    if (n.find("gen_") == std::string::npos && n.find("com_") == std::string::npos) {
        releaseHeld();
        endFresh();
    }
}

// --- The window -------------------------------------------------------------------

void ScenePage::toggleInventory() {
    // 0x4194d0. Closed with an item in hand: only the item put back.
    if (ui_.open) {
        closeInventory();
        return;
    }
    if (!state_.held.empty()) {
        releaseHeld();
        return;
    }
    ui_.open = true;
    ui_.page = 0;
    ui_.fade = 1;
    ui_.alpha = 0;
    ui_.fromInventory = false;
    ui_.hover = pageButton(lastMouseX_, lastMouseY_);
    showPage(0);
}

void ScenePage::closeInventory() {
    if (!ui_.open || ui_.fade == 2)
        return;
    endFresh();
    ui_.fade = 2;
    ui_.alpha = 255;
    ui_.dragging = 0;
}

void ScenePage::showPage(int page) {
    // Each page's activation.
    ui_.page = page;
    if (page == 1) {
        // Examine (0x41a4f0): the item in hand, else the one before if it's
        // still there, else the first.
        const std::vector<std::string> items = bagItems();
        if (!state_.held.empty())
            ui_.examined = state_.held;
        else if (std::find(items.begin(), items.end(), ui_.examined) == items.end())
            ui_.examined = items.empty() ? std::string() : items.front();
        ui_.back = false;
    } else if (page == 2) {
        ui_.message.clear(); // 0x418a30
    } else if (page == 4) {
        // Passports (0x41d0b0): 12 a page, up to 3 pages.
        int n = 0;
        for (const GameState::Item& i : state_.items)
            n += i.state == 5;
        if (n > 12)
            ui_.passPages = std::max(ui_.passPages, 2);
        if (n > 24)
            ui_.passPages = 3;
    } else if (page == 5) {
        ui_.printAt[0] = ui_.printAt[1] = 0; // 0x41d6c0
    }
}

bool ScenePage::interfaceUpdate(float dt) {
    bool changed = false;
    const int mx = lastMouseX_, my = lastMouseY_;
    // The window's fade (0x419990): 1000 alpha a second.
    if (ui_.fade == 1 || ui_.fade == 3) {
        ui_.alpha += dt * 1000;
        if (ui_.alpha >= 255)
            ui_.alpha = 255, ui_.fade = 0;
        changed = true;
    } else if (ui_.fade == 2) {
        ui_.alpha -= dt * 1000;
        if (ui_.alpha <= 0)
            ui_.alpha = 255, ui_.fade = 0, ui_.open = false;
        changed = true;
    }
    // The bar (0x417120).
    if (ui_.open && ui_.alpha >= 255) {
        if (ui_.barAlpha != 0 || ui_.barShown)
            changed = true;
        ui_.barAlpha = 0;
        ui_.barShown = false;
    } else if (!ui_.open && canAct() && runner_->idle() && interfaceOn()) {
        const float before = ui_.barAlpha;
        if (my > 60) {
            if (ui_.barShown || ui_.barAlpha > 0) {
                ui_.barAlpha -= dt * 1000;
                if (ui_.barAlpha <= 0)
                    ui_.barAlpha = 0, ui_.barShown = false;
            }
        } else if (my < 5 || (ui_.barAlpha < 255 && ui_.barAlpha > 0)) {
            if (!ui_.barShown || ui_.barAlpha < 255) {
                ui_.barAlpha += dt * 1000;
                if (ui_.barAlpha >= 255)
                    ui_.barAlpha = 255, ui_.barShown = true;
            }
        }
        changed |= before != ui_.barAlpha;
    }
    if (ui_.open) {
        // The item just taken blinks for 5 s of the items page shown
        // (0x41b0d0).
        if (state_.fresh != ui_.freshItem) {
            ui_.freshItem = state_.fresh;
            ui_.freshTime = 5;
        }
        if (ui_.page == 0 && !ui_.freshItem.empty() && interfaceOn()) {
            ui_.freshTime -= dt;
            if (ui_.freshTime <= 0)
                endFresh();
            changed = true;
        }
        // The button under the mouse (0x419a29): mouseover2.wav as it
        // changes.
        if (ui_.zoom.empty()) {
            const int b = pageButton(mx, my);
            if (b != ui_.hover) {
                ui_.hover = b;
                if (b != -1)
                    playSound("mouseover2");
                changed = true;
            }
        }
    }
    // A card held up (0x417e10, 0x415850): the screen darkens to half, 8
    // every 0.01 s; the pictures fade 8 every 0.025 s (0x40eda0).
    if (ui_.card) {
        auto finish = [&](const std::string& verb) {
            if (ScriptRunner* r = ui_.cardRunner) {
                ui_.cardRunner = nullptr;
                r->finished(verb);
            }
        };
        auto fadeStep = [&](float& alpha, int& mode, float& clock) {
            if (!mode)
                return;
            for (clock += dt; clock >= 0.025f; clock -= 0.025f) {
                alpha += mode == 2 ? 8 : -8;
                if (alpha >= 255 || alpha <= 0) {
                    alpha = std::clamp(alpha, 0.0f, 255.0f);
                    mode = 0;
                    clock = 0;
                    break;
                }
            }
        };
        fadeStep(ui_.cardAlpha, ui_.cardFade, ui_.cardClock);
        fadeStep(ui_.printAlpha, ui_.printFade, ui_.printClock);
        if (ui_.card == 1 || ui_.card == 5) {
            for (ui_.dimClock += dt; ui_.dimClock >= 0.01f; ui_.dimClock -= 0.01f) {
                ui_.dim += ui_.card == 1 ? 8 : -8;
                if (ui_.card == 1 && ui_.dim >= 128) {
                    // The card fades in; a passport's event is done.
                    ui_.dim = 128;
                    ui_.card = 2;
                    ui_.cardFade = 2;
                    ui_.cardAlpha = 0;
                    ui_.cardClock = 0;
                    if (ui_.cardKind == 1) {
                        ui_.card = 3;
                        finish(ui_.cardVerb);
                        playSound("passport");
                    }
                    break;
                }
                if (ui_.card == 5 && ui_.dim <= 0) {
                    // Done: the "end" event over.
                    ui_.dim = 0;
                    ui_.card = 0;
                    finish(ui_.cardVerb);
                    break;
                }
            }
        } else if (ui_.card == 2 && ui_.cardAlpha >= 255) {
            // The fingerprint card in: the print fades in over it.
            ui_.card = 3;
            finish(ui_.cardVerb);
            playSound("fingerprint");
            ui_.printFade = 2;
            ui_.printAlpha = 0;
            ui_.printClock = 0;
        } else if (ui_.card == 4 && ui_.cardAlpha <= 0) {
            ui_.card = 5;
            ui_.dimClock = 0;
        }
        changed = true;
    }
    return changed;
}

bool ScenePage::startCard(ScriptRunner& runner, const ScriptEvent& e) {
    const std::string v = fs::toLower(e.verb());
    if (v == "startpass" || v == "startprint") {
        // 0x417ba0: 80 higher with the dialogue box up.
        ui_.card = 1;
        ui_.cardKind = v == "startpass" ? 1 : 2;
        ui_.cardWho = fs::toLower(e.arg(1));
        ui_.cardRaise = dialogue_->boxShown() ? 160 : 0;
        ui_.dim = ui_.dimClock = 0;
        ui_.cardAlpha = ui_.printAlpha = 0;
        ui_.cardFade = ui_.printFade = 0;
        ui_.cardRunner = &runner;
        ui_.cardVerb = v;
        return true;
    }
    // endpass, endprint (0x418090): the pictures fade out.
    if (ui_.card != 3)
        return false;
    ui_.card = 4;
    ui_.cardAlpha = 255;
    ui_.cardFade = 1;
    ui_.cardClock = 0;
    if (ui_.cardKind == 2) {
        ui_.printAlpha = 255;
        ui_.printFade = 1;
        ui_.printClock = 0;
    }
    ui_.cardRunner = &runner;
    ui_.cardVerb = v;
    return true;
}

// --- Text -------------------------------------------------------------------------

int ScenePage::textHeight(int font, int width, const std::string& text, int* lines) {
    if (!ctx_.fonts || font < 0 || size_t(font) >= ctx_.fonts->styles().size())
        return 0;
    const TextStyle& s = ctx_.fonts->styles()[size_t(font)];
    if (!s.font)
        return 0;
    const int lh = s.font->lineHeight(s.pixels);
    const int n = s.font->width(text, s.pixels) <= width ? 1 : int(s.font->wrap(text, s.pixels, width).size());
    if (lines)
        *lines = n;
    return n * lh;
}

int ScenePage::uiText(int font, int colour, int l, int t, int r, int b, int flags, const std::string& text,
                      int alpha) {
    if (!ctx_.fonts || text.empty() || font < 0 || size_t(font) >= ctx_.fonts->styles().size() || alpha <= 0)
        return 0;
    const TextStyle& s = ctx_.fonts->styles()[size_t(font)];
    if (!s.font)
        return 0;
    const Color c = ctx_.fonts->colourAt(size_t(colour));
    const int lh = s.font->lineHeight(s.pixels);
    std::vector<std::string> lines;
    if (s.font->width(text, s.pixels) <= r - l)
        lines.push_back(text);
    else
        lines = s.font->wrap(text, s.pixels, r - l);
    const int h = int(lines.size()) * lh;
    int y = flags & 8 ? b - h : flags & 4 ? t + (b - t - h) / 2 : t;
    for (const std::string& line : lines) {
        const int w = s.font->width(line, s.pixels);
        const int x = flags & 2 ? r - w : flags & 1 ? l + (r - l - w) / 2 : l;
        ctx_.fonts->draw(ctx_.screen, s, x, y, line, c, uint8_t(std::min(alpha, 255)));
        if (flags & 0x1000)
            for (int px = x; px < x + w; px++)
                if (px >= 0 && px < kScreenW && y + lh / 2 >= 0 && y + lh / 2 < kScreenH) {
                    uint8_t* d = ctx_.screen.row(y + lh / 2) + px * 4;
                    d[0] = uint8_t((c.r * alpha + d[0] * (255 - alpha)) / 255);
                    d[1] = uint8_t((c.g * alpha + d[1] * (255 - alpha)) / 255);
                    d[2] = uint8_t((c.b * alpha + d[2] * (255 - alpha)) / 255);
                    d[3] = std::max(d[3], uint8_t(alpha));
                }
        y += lh;
    }
    return h;
}

// --- Input ------------------------------------------------------------------------

int ScenePage::pageButton(int x, int y, int* bx, int* by) {
    // The tabs (0x41cc90), then the page's own button (7: examine's eye,
    // combine's gears, 8: the fingerprint cards' magnifier).
    const int wx = (kScreenW - std::max(1, picture("inventory/interface/windowblank.png").w)) / 2;
    for (int i = 0, ty = kTop + 43; i < 7; i++, ty += 78)
        if (inside(x, y, wx + 511, ty, wx + 578, ty + 60)) {
            if (bx)
                *bx = wx + 511, *by = ty;
            return i;
        }
    int button = -1, l = 0, t = 0, r = 0, b = 0;
    if (ui_.page == 1)
        button = 7, l = 220, t = 411, r = 292, b = 476;
    else if (ui_.page == 2)
        button = 7, l = 221, t = 240, r = 292, b = 303;
    else if (ui_.page == 5)
        button = 8, l = 223, t = 230, r = 286, b = 287;
    if (button < 0 || !inside(x, y, wx + l, kTop + t, wx + r, kTop + b))
        return -1;
    if (bx)
        *bx = wx + l, *by = kTop + t;
    return button;
}

bool ScenePage::blipClick(int x, int y, int row, int count, int& at) {
    // The line's arrows (select1.wav, round the ends) and the blip's line
    // (a drag starts: mouseover1.wav). Row 2 is the fingerprint cards'
    // second line, 449 lower.
    const int wx = (kScreenW - picture("inventory/interface/windowblank.png").w) / 2;
    const int dy = kTop + (row == 2 ? 449 : 0);
    if (count <= 1)
        return false;
    if (inside(x, y, wx + kArrowBack[0], dy + kArrowBack[1], wx + kArrowBack[2], dy + kArrowBack[3])) {
        at = at > 0 ? at - 1 : count - 1;
        playSound("select1");
        return true;
    }
    if (inside(x, y, wx + kArrowOn[0], dy + kArrowOn[1], wx + kArrowOn[2], dy + kArrowOn[3])) {
        at = at + 1 < count ? at + 1 : 0;
        playSound("select1");
        return true;
    }
    if (x > wx + kArrowBack[2] && x < wx + kArrowOn[0] && y >= dy + kArrowBack[1] && y <= dy + kArrowBack[3]) {
        ui_.dragging = row;
        playSound("mouseover1");
        return true;
    }
    return false;
}

bool ScenePage::interfaceEvent(const Event& ev) {
    const int x = ev.mouseX, y = ev.mouseY;
    if (ev.type == EventType::MouseMove) {
        if (ui_.dragging) {
            // The blip follows the mouse to the nearest place (0x41afd0).
            const int wx = (kScreenW - picture("inventory/interface/windowblank.png").w) / 2;
            const int bw = picture("inventory/interface/blip.png").w;
            auto nearest = [&](int count) {
                if (count <= 1)
                    return 0;
                const float step = float(kBlipSpan) / float(count - 1), at = float(x - bw / 2 - wx - kBlipLeft);
                int i = 0;
                while (i < count - 1 && at > float(i) * step + step * 0.5f)
                    i++;
                return i;
            };
            if (ui_.page == 0)
                state_.itemPage = nearest(itemPages());
            else if (ui_.page == 1) {
                const std::vector<std::string> items = bagItems();
                const int i = nearest(int(items.size()));
                if (i < int(items.size()) && items[size_t(i)] != ui_.examined) {
                    ui_.examined = items[size_t(i)];
                    ui_.back = false;
                    if (!state_.held.empty())
                        holdItem(ui_.examined);
                }
            } else if (ui_.page == 4) {
                const int p = nearest(ui_.passPages);
                if (p != ui_.passPage)
                    ui_.passPage = p, ui_.passAt = p * 12;
            } else if (ui_.page == 5) {
                int n[2] = {0, 0};
                for (const GameState::Item& i : state_.items)
                    if (i.state == 6)
                        n[printRow(i.name)]++;
                ui_.printAt[ui_.dragging - 1] = nearest(n[ui_.dragging - 1]);
            }
        }
        if (ui_.open || ui_.barShown || ui_.barAlpha > 0 || !state_.held.empty())
            draw();
        return false;
    }
    if (ev.type == EventType::MouseUp && ev.button == MouseButton::Left) {
        // The blip let go (0x419d10).
        if (!ui_.dragging)
            return false;
        ui_.dragging = 0;
        draw();
        return true;
    }
    if (ev.type == EventType::KeyDown && (ev.key == 'i' || ev.key == 'I') && playing_ && !(ev.mods & keymod::Shift)) {
        // I (0x416806): the window, with the interface there and no
        // dialogue box.
        if (interfaceOn() && !dialogue_->boxShown())
            toggleInventory();
        draw();
        return true;
    }
    if (ev.type != EventType::MouseDown || !playing_)
        return false;
    if (!ui_.zoom.empty() && (ev.button == MouseButton::Left || ev.button == MouseButton::Right)) {
        // A scrapbook picture full screen: any click puts it away (0x41fa70).
        ui_.zoom.clear();
        draw();
        return true;
    }
    if (ev.button == MouseButton::Right) {
        // 0x416450: the window opened or closed, or the item in hand put
        // back.
        if (!canAct())
            return false;
        toggleInventory();
        draw();
        return true;
    }
    if (ev.button != MouseButton::Left || !canAct())
        return false;
    const bool on = interfaceOn();
    // The window (0x419d30): its page, its tabs; any click on it is its.
    if (on && ui_.open) {
        const image::Image& frame = picture("inventory/interface/windowblank.png");
        const int wx = (kScreenW - frame.w) / 2;
        const bool onFrame = inside(x, y, wx, kTop, wx + frame.w, kTop + frame.h) &&
                             frame.pixel(x - wx, y - kTop)[3] != 0;
        if (ui_.fade != 0 && onFrame)
            return true;
        if (pageClick(x, y)) {
            draw();
            return true;
        }
        const int b = pageButton(x, y);
        if (b >= 0 && b < 7) {
            playSound("select2");
            if (b == 6)
                closeInventory();
            else if (b != ui_.page) {
                endFresh();
                ui_.fromPage = ui_.page;
                showPage(b);
                ui_.fade = 3;
                ui_.alpha = 0;
            }
            draw();
            return true;
        }
        if (onFrame)
            return true;
    }
    // The scene's part of the screen: below the bar (0x417580).
    const bool inScene = (!ui_.barShown && ui_.barAlpha == 0) ? y >= 5 : y >= 60;
    // An item in hand: used on what's under it (0x419f10).
    if (!state_.held.empty()) {
        // What the game's pick finds there (0x41613c).
        const Ac2Record* target = inScene && pickAllowed(y) ? pickAt(x, y) : nullptr;
        if (target) {
            runner_->setFromCharacter(target->tag == "NPCX");
            if (target->tag == "NPCX")
                resumeAfter_ = target;
            useItem(state_.held, *target);
        } else {
            logScript("use " + state_.held + ": (nothing there)");
            runFromInventory("talk gen_cantuse 1", true);
        }
        return true;
    }
    // The bar (0x4177f0); select1.wav.
    // Above the scene's part, nothing in the scene is clicked.
    const int section = on ? barSection(x, y) : -1;
    if (section < 0)
        return on && !inScene;
    if (section == 0 || section == 6 || carOn(section)) {
        if (section >= 1 && section <= 5 && inCar(section))
            return false;
        if (section == 0) {
            toggleInventory();
        } else if (section == 6) {
            // The hat: the in-game menu, as P (0x417080).
            releaseHeld();
            endFresh();
            gameKey('p');
        } else {
            runner_->clear();
            runner_->run("fade out, scene " + carScene(section) + " 0, fade in");
        }
    }
    playSound("select1");
    draw();
    return true;
}

int ScenePage::barSection(int x, int y) {
    // 0x4175c0: fully shown, on an opaque pixel of the bar's picture; the
    // first section whose end is past x.
    if (!ui_.barShown || ui_.barAlpha < 255)
        return -1;
    const image::Image& bar = picture("interface/topmenubar01.png");
    if (x < 0 || y < 0 || x >= bar.w || y >= bar.h || bar.pixel(x, y)[3] == 0)
        return -1;
    int section = 0;
    while (section < 6 && x >= kBarEnds[section])
        section++;
    return section;
}

bool ScenePage::carOn(int car) {
    // Where travel is allowed at all (0x417630), and to each car
    // (0x4176a0).
    auto test = [&](const char* c) { return testCondition(c, state_); };
    if (car < 1 || car > 5)
        return true;
    if (test("act <04a") || test("act 08a") || test("location exterior.forest | location exterior.train") ||
        test("scene calais.9_ashes.puzzle") || test("act 04a, location baggage, !tag michellockbaggage"))
        return false;
    switch (car) {
    case 1:
        return test("act >05c, act <08a, tag baggageunlocked") ||
               test("act 04a, !tag michellockbaggage, tag seenmichel04a, tag micheladmonish");
    case 2:
        return test("act 04a, tag micheladmonish") || test("act >05b, act <08a");
    case 3:
        return test("act 04a") || test("act >05b, act <08a");
    case 5:
        return test("act >05c, act <08a, tag athensunlocked");
    }
    return true;
}

std::string ScenePage::carScene(int car) const {
    // Where the bar's car goes (0x4177f0): its first scene from the front
    // or its last from the back, with the walk point.
    const bool fromFront = inCar(1) || inCar(2) || inCar(3);
    switch (car) {
    case 1:
        return "baggage_01 1";
    case 2:
        return inCar(1) ? "restaurant_01 1" : "restaurant_06 2";
    case 3:
        return inCar(1) || inCar(2) ? "salon_01 1" : "salon_06 2";
    case 4:
        return inCar(5) && !testCondition("tag athensunlocked", state_) ? "calais.10-11_10.02 4"
               : fromFront                                             ? "calais.hall_01 1"
                                                                       : "calais.hall_06 2";
    default:
        return fromFront || inCar(4) ? "athens.hall_01 1" : "athens.hall_06 2";
    }
}

bool ScenePage::inCar(int car) const {
    // 0x417760: the location's name has the car's word in it.
    return car >= 1 && car <= 5 && location_.find(kCars[car - 1]) != std::string::npos;
}

bool ScenePage::pageClick(int x, int y) {
    const int wx = (kScreenW - picture("inventory/interface/windowblank.png").w) / 2;
    switch (ui_.page) {
    case 0: {
        // The items (0x41b150).
        int page = state_.itemPage;
        if (blipClick(x, y, 1, itemPages(), page)) {
            state_.itemPage = page;
            return true;
        }
        for (int box = 0; box < 16; box++) {
            const int bx = wx + 38 + 112 * (box % 4), by = kTop + 54 + 112 * (box / 4);
            if (!inside(x, y, bx, by, bx + 96, by + 96))
                continue;
            const int slot = state_.itemPage * 16 + box;
            const std::string there = itemInSlot(slot);
            if (state_.held.empty()) {
                if (there.empty())
                    return false;
                playItemSound(there);
                endFresh();
                ui_.hovered.clear();
                holdItem(there, slot);
                return true;
            }
            // In hand: put in an empty box, or swapped with the one there
            // (which is then in hand).
            const std::string held = state_.held;
            GameState::Item* inHand = state_.findItem(held, {3});
            if (there.empty()) {
                playItemSound(held);
                if (inHand)
                    inHand->slot = slot;
                releaseHeld();
                return true;
            }
            const int heldSlot = inHand ? inHand->slot : slot;
            for (GameState::Item& i : state_.items)
                if ((i.state == 1 || i.state == 2) && i.slot == slot)
                    i.slot = heldSlot;
            if (inHand)
                inHand->slot = slot;
            releaseHeld();
            endFresh();
            playItemSound(there);
            holdItem(there, heldSlot);
            return false;
        }
        return false;
    }
    case 1: {
        // Examine (0x41a860).
        const std::vector<std::string> items = bagItems();
        int at = int(std::find(items.begin(), items.end(), ui_.examined) - items.begin());
        if (at < int(items.size()) && blipClick(x, y, 1, int(items.size()), at)) {
            if (ui_.dragging)
                return true;
            ui_.examined = items[size_t(at)];
            ui_.back = false;
            if (!state_.held.empty())
                holdItem(ui_.examined);
            return true;
        }
        if (ui_.examined.empty())
            return false;
        if (inside(x, y, wx + 220, kTop + 411, wx + 292, kTop + 476)) {
            // The eye: the back, if it has one; its Examine Event.
            const std::string other = itemPicture(ui_.examined, ui_.back ? "exa" : "ex2");
            if (!ui_.back && other.empty())
                return true;
            playSound("select1");
            ui_.back = !ui_.back;
            const ItemInfo info = itemInfo(ui_.examined);
            if (ui_.back && info.event.size() > 1) {
                playSound("discovery");
                runFromInventory(info.event, true);
            }
            return true;
        }
        if (state_.held.empty() && inside(x, y, wx + 40, kTop + 57, wx + 470, kTop + 375)) {
            playItemSound(ui_.examined);
            holdItem(ui_.examined);
            return true;
        }
        return false;
    }
    case 2: {
        // Combine (0x418660): the gears, the boxes and their Xs.
        if (pageButton(x, y) == 7) {
            combine();
            return true;
        }
        for (int i = 0; i < 5; i++) {
            const int l = i < 4 ? 56 : 360, t = i < 4 ? 56 + 112 * i : 224, r = i < 4 ? 151 : 454,
                      b = i < 4 ? 151 + 112 * i : 319;
            if (!inside(x, y, wx + l, kTop + t, wx + r, kTop + b))
                continue;
            std::string& box = ui_.boxes[i];
            if (!box.empty() && x >= wx + r - 21 && y <= kTop + t + 21) {
                playSound("select2");
                box.clear();
            } else if (!state_.held.empty()) {
                playItemSound(state_.held);
                box = state_.held;
                releaseHeld();
            } else if (!box.empty() && state_.itemAvailable(box)) {
                playItemSound(box);
                holdItem(box);
            }
            return true;
        }
        return false;
    }
    case 3: {
        // The scrapbook (0x41e940).
        std::string clicked;
        drawScrapbook(255, false, x, y, &clicked);
        return !clicked.empty();
    }
    case 4: {
        // Passports (0x41d2a0): a passport on the page, the arrows, the blip.
        std::vector<std::string> passes;
        for (const GameState::Item& i : state_.items)
            if (i.state == 5)
                passes.push_back(i.name);
        for (int c = 0; c < 12; c++) {
            const int px = wx + 45 + 72 * (c % 6), py = kTop + 61 + 70 * (c / 6);
            const int n = ui_.passPage * 12 + c;
            if (inside(x, y, px, py, px + 62, py + 62) && n < int(passes.size())) {
                playItemSound(passes[size_t(n)]);
                ui_.passAt = n;
                return true;
            }
        }
        int page = ui_.passPage;
        if (blipClick(x, y, 1, ui_.passPages, page)) {
            if (page != ui_.passPage)
                ui_.passPage = page, ui_.passAt = page * 12;
            return true;
        }
        return false;
    }
    case 5: {
        // Fingerprint cards (0x41de80): each row's arrows and blip, then
        // the magnifier.
        int n[2] = {0, 0};
        for (const GameState::Item& i : state_.items)
            if (i.state == 6)
                n[printRow(i.name)]++;
        if (blipClick(x, y, 1, n[0], ui_.printAt[0]) || blipClick(x, y, 2, n[1], ui_.printAt[1]))
            return true;
        if (n[0] && n[1] && inside(x, y, wx + 225, kTop + 232, wx + 286, kTop + 287))
            comparePrints();
        return false;
    }
    }
    return false;
}

// --- Combining and comparing ----------------------------------------------------

bool ScenePage::combine() {
    // The gears (0x418a50): two or more items on the left combined, or the
    // one on the right taken apart.
    int sources = 0;
    for (int i = 0; i < 4; i++)
        sources += !ui_.boxes[i].empty();
    const std::string result = ui_.boxes[4];
    const Table& t = itemTable("combinelist.txt");
    auto names = [](const std::string& list) {
        std::vector<std::string> out;
        for (const Command& c : parseCommands(unquoted(list)))
            if (!c.words.empty())
                out.push_back(fs::toLower(c.words[0]));
        return out;
    };
    auto finish = [&](size_t r, const std::string& list, int first, const char* otherwise) {
        // The boxes' items used up (0x418dc0), the new ones given, just
        // taken, into the boxes from `first` (not fingerprint cards:
        // 0x418df0), the Event run.
        for (std::string& box : ui_.boxes) {
            if (GameState::Item* i = box.empty() ? nullptr : state_.findItem(box, {1, 2, 3}))
                i->state = 4;
            box.clear();
        }
        int box = first;
        for (const std::string& item : names(list)) {
            addItem(state_, item, 1);
            if (box < 5 && item.find("print") == std::string::npos)
                ui_.boxes[box++] = item;
        }
        const std::string event = unquoted(t.cell(r, 5));
        logScript(std::string(first ? "combine: " : "decombine: ") + event);
        runFromInventory(event.empty() || event == "0" ? otherwise : event, true);
    };
    if (sources == 0) {
        if (result.empty())
            return false;
        playSound("combine");
        // The row whose Result is the item, that can be taken apart
        // (0x418be0).
        for (size_t r = 0; r < t.rows.size(); r++)
            if (fs::toLower(unquoted(t.cell(r, 1))) == result && std::atoi(unquoted(t.cell(r, 2)).c_str()) != 0 &&
                testCondition(unquoted(t.cell(r, 4)), state_)) {
                finish(r, t.cell(r, 0), 0, "talk gen_decombine");
                return true;
            }
        ui_.message = interfaceText("combine3");
        runFromInventory("talk gen_cantdecombine", true);
        return false;
    }
    playSound("combine");
    if (!result.empty()) {
        ui_.message = interfaceText("combine1");
        return false;
    }
    if (sources < 2)
        return false;
    // The row whose Parts are exactly the boxes' items, that can be
    // combined (0x418af0, 0x418cf0).
    for (size_t r = 0; r < t.rows.size(); r++) {
        const std::vector<std::string> parts = names(t.cell(r, 0));
        bool all = int(parts.size()) == sources;
        for (const std::string& p : parts)
            all = all && std::find(ui_.boxes, ui_.boxes + 4, p) != ui_.boxes + 4;
        if (all && std::atoi(unquoted(t.cell(r, 3)).c_str()) != 0 && testCondition(unquoted(t.cell(r, 4)), state_)) {
            finish(r, t.cell(r, 1), 4, "talk gen_combine");
            return true;
        }
    }
    ui_.message = interfaceText("combine2");
    std::vector<std::string> have;
    for (int i = 0; i < 4; i++)
        if (!ui_.boxes[i].empty())
            have.push_back(ui_.boxes[i]);
    std::sort(have.begin(), have.end());
    runFromInventory(have == std::vector<std::string>{"icetongs", "paperscrap"} ? "talk gen_icetongs, tag triedicetongs"
                                                                                : "talk gen_cantcombine",
                     true);
    return false;
}

void ScenePage::comparePrints() {
    // The magnifier (0x41e2e0): the second row's card matches the first's
    // if its Flipped Description names it; the tag "<first>=<second>".
    std::vector<std::string> rows[2];
    for (const GameState::Item& i : state_.items)
        if (i.state == 6)
            rows[printRow(i.name)].push_back(i.name);
    if (rows[0].empty() || rows[1].empty())
        return;
    const std::string a = rows[0][size_t(std::min(ui_.printAt[0], int(rows[0].size()) - 1))];
    const std::string b = rows[1][size_t(std::min(ui_.printAt[1], int(rows[1].size()) - 1))];
    playItemSound(a);
    if (fs::toLower(itemInfo(b).flipped) == a) {
        state_.tags[a + "=" + b] = 0;
        runFromInventory("talk gen_fingerprint 6", true);
    } else {
        runFromInventory("talk gen_fingerprint 7", true);
    }
}

// --- Drawing ----------------------------------------------------------------------

void ScenePage::drawInterface() {
    if (!interfaceOn())
        return;
    // The bar (0x417290): no brighter than the window fading in lets it be.
    int barAlpha = int(ui_.barAlpha);
    if (ui_.open && ui_.barShown && ui_.alpha < 255)
        barAlpha = std::min(barAlpha, 255 - int(ui_.alpha));
    if (barAlpha > 0) {
        const image::Image& dark = picture("interface/topmenubar01.png");
        const image::Image& lit = picture("interface/topmenubar02.png");
        const int hover = barSection(lastMouseX_, lastMouseY_);
        for (int s = 0, left = 0; s < 7; left = kBarEnds[s++]) {
            // Lit: the bag or the hat under the mouse, a car that can be
            // travelled to or the one the player is in.
            const bool on = s == 0 || s == 6 ? hover == s : carOn(s) || inCar(s);
            blend(ctx_.screen, on ? lit : dark, 0, 0, barAlpha, left, kBarEnds[s]);
            if (hover == s && s >= 1 && s <= 5 && on) {
                uiText(6, 11, left, 45, kBarEnds[s], 60, 1, interfaceText("map" + std::to_string(s)), barAlpha);
                if (inCar(s))
                    uiText(6, 12, left, 10, kBarEnds[s], 50, 5, interfaceText("map6"), barAlpha);
            }
        }
    }
    // The window (0x419a90): the frame, the page (cross-faded from the one
    // before), the highlight on the button under the mouse.
    if (!ui_.open)
        return;
    const image::Image& frame = picture("inventory/interface/windowblank.png");
    const int wx = (kScreenW - frame.w) / 2, a = int(ui_.alpha);
    if (ui_.fade == 3) {
        blend(ctx_.screen, frame, wx, kTop, 255);
        drawPage(ui_.fromPage, 255 - a);
    } else {
        blend(ctx_.screen, frame, wx, kTop, a);
    }
    drawPage(ui_.page, a);
    if (ui_.zoom.empty()) {
        int bx = 0, by = 0;
        const int b = pageButton(lastMouseX_, lastMouseY_, &bx, &by);
        if (b == 7)
            blend(ctx_.screen, picture("inventory/interface/largehighlight.png"), bx, by, a);
        else if (b >= 0)
            blend(ctx_.screen, picture("inventory/interface/smallhighlight.png"), bx + 1, by + 1, a);
    }
    if (!ui_.zoom.empty()) {
        // A scrapbook picture full screen, over the scene darkened (0xc0).
        darken(ctx_.screen, 0xc0);
        const image::Image& z = picture(ui_.zoom);
        blend(ctx_.screen, z, (kScreenW - z.w) / 2, (kScreenH - z.h) / 2, 255);
    }
}

void ScenePage::drawBlip(int row, int count, int at, int alpha) {
    // 0x41cb50: along the line, or under the mouse while dragged.
    const image::Image& blip = picture("inventory/interface/blip.png");
    const int wx = (kScreenW - picture("inventory/interface/windowblank.png").w) / 2;
    int x = wx + kBlipLeft + (count > 1 ? at * kBlipSpan / (count - 1) : 0);
    if (ui_.dragging == row)
        x = std::clamp(lastMouseX_ - blip.w / 2, wx + kBlipLeft, wx + kBlipLeft + kBlipSpan);
    blend(ctx_.screen, blip, x, kTop + (row == 1 ? 29 : 478), alpha);
}

void ScenePage::drawPage(int page, int alpha) {
    if (page < 0 || page > 5 || alpha <= 0)
        return;
    const image::Image& win = picture(std::string("inventory/interface/") + kPages[page] + ".png");
    const int wx = (kScreenW - win.w) / 2;
    blend(ctx_.screen, win, wx, kTop, alpha);
    // The description bar (0x41cae0).
    auto bar = [&](const std::string& text) { uiText(6, 6, wx + 45, kTop + 520, wx + 465, kTop + 568, 5, text, alpha); };
    switch (page) {
    case 0: {
        // The items (0x41b080, 0x41bab0): one just taken blinks.
        drawBlip(1, itemPages(), state_.itemPage, alpha);
        std::string hovered;
        for (const GameState::Item& i : state_.items) {
            if ((i.state != 1 && i.state != 2) || i.slot / 16 != state_.itemPage)
                continue;
            const int box = i.slot % 16, bx = wx + 38 + 112 * (box % 4), by = kTop + 54 + 112 * (box / 4);
            if (state_.held.empty() && inside(lastMouseX_, lastMouseY_, bx, by, bx + 96, by + 96))
                hovered = i.name;
            if (i.state == 1 && !caretOn_)
                continue;
            blend(ctx_.screen, picture(itemPicture(i.name, "inv")), bx + 5, by + 5, alpha);
        }
        if (!hovered.empty())
            bar(itemInfo(hovered).description);
        break;
    }
    case 1: {
        // Examine (0x41a680): the close-up, or its back, and its text.
        if (ui_.examined.empty())
            break;
        const std::vector<std::string> items = bagItems();
        const int at = int(std::find(items.begin(), items.end(), ui_.examined) - items.begin());
        drawBlip(1, int(items.size()), at, alpha);
        std::string pic = itemPicture(ui_.examined, ui_.back ? "ex2" : "exa");
        if (pic.empty())
            pic = itemPicture(ui_.examined, "inv");
        const image::Image& img = picture(pic);
        blend(ctx_.screen, img, wx + 40 + (432 - img.w) / 2, kTop + 56 + (320 - img.h) / 2, alpha);
        const ItemInfo info = itemInfo(ui_.examined);
        bar(ui_.back && !info.flipped.empty() ? info.flipped : !info.examine.empty() ? info.examine : info.description);
        break;
    }
    case 2: {
        // Combine (0x4183f0): the boxes, their Xs; the text of the item
        // under the mouse, else the message.
        const image::Image& x = picture("inventory/interface/x.jpg");
        std::string hovered;
        for (int i = 0; i < 5; i++) {
            const int l = wx + (i < 4 ? 56 : 360), t = kTop + (i < 4 ? 56 + 112 * i : 224), r = wx + (i < 4 ? 151 : 454),
                      b = kTop + (i < 4 ? 151 + 112 * i : 319);
            if (ui_.boxes[i].empty())
                continue;
            if (state_.held.empty() && inside(lastMouseX_, lastMouseY_, l, t, r, b))
                hovered = ui_.boxes[i];
            blend(ctx_.screen, picture(itemPicture(ui_.boxes[i], "inv")), l, t, alpha);
            blend(ctx_.screen, x, i < 4 ? r - 21 : r - 20, i < 4 ? t + 1 : t + 2, alpha);
        }
        if (!hovered.empty()) {
            ui_.message.clear();
            bar(itemInfo(hovered).description);
        } else {
            bar(ui_.message);
        }
        break;
    }
    case 3:
        drawScrapbook(alpha, true);
        break;
    case 4: {
        // Passports (0x41d120): the one shown above, the page's twelve
        // below.
        std::vector<std::string> passes;
        for (const GameState::Item& i : state_.items)
            if (i.state == 5)
                passes.push_back(i.name);
        drawBlip(1, ui_.passPages, ui_.passPage, alpha);
        if (ui_.passAt < int(passes.size())) {
            const std::string& p = passes[size_t(ui_.passAt)];
            const image::Image& img = picture(itemPicture(p, "exa"));
            blend(ctx_.screen, img, wx + 40 + (432 - img.w) / 2, kTop + 216 + (273 - img.h) / 2, alpha);
            bar(itemInfo(p).examine);
        }
        for (int c = 0; c < 12; c++) {
            const int n = ui_.passPage * 12 + c;
            if (n >= int(passes.size()))
                break;
            blend(ctx_.screen, picture(itemPicture(passes[size_t(n)], "fol")), wx + 45 + 72 * (c % 6),
                  kTop + 61 + 70 * (c / 6), alpha);
        }
        break;
    }
    case 5: {
        // Fingerprint cards (0x41d7c0): each row's card and its
        // neighbours, with their names.
        std::vector<std::string> rows[2];
        for (const GameState::Item& i : state_.items)
            if (i.state == 6)
                rows[printRow(i.name)].push_back(i.name);
        for (int r = 0; r < 2; r++) {
            const int n = int(rows[r].size()), at = std::min(ui_.printAt[r], std::max(0, n - 1)),
                      y = kTop + (r == 0 ? 56 : 320);
            auto card = [&](int i, int x) {
                const std::string& item = rows[r][size_t(i)];
                blend(ctx_.screen, picture(itemPicture(item, "inv")), x, y, alpha);
                uiText(12, 14, x + 5, y, x + 120, y + 136, 9, itemInfo(item).description, alpha);
            };
            if (n == 0)
                continue;
            card(at, wx + 191);
            if (n > 1) {
                if (at > 0 || n > 2)
                    card(at > 0 ? at - 1 : n - 1, wx + 40);
                if (at < n - 1 || n > 2)
                    card(at < n - 1 ? at + 1 : 0, wx + 342);
            }
        }
        if (!rows[0].empty() && !rows[1].empty()) {
            // A lower card matched: what it matches.
            const std::string& b = rows[1][size_t(std::min(ui_.printAt[1], int(rows[1].size()) - 1))];
            for (const auto& [tag, v] : state_.tags)
                if (tag.size() > b.size() + 1 && tag.compare(tag.size() - b.size() - 1, std::string::npos, "=" + b) == 0) {
                    bar(itemInfo(b).examine);
                    break;
                }
        }
        drawBlip(1, int(rows[0].size()), ui_.printAt[0], alpha);
        drawBlip(2, int(rows[1].size()), ui_.printAt[1], alpha);
        break;
    }
    }
}

void ScenePage::drawCards() {
    // 0x418040: the screen darkened, the card, the print over it.
    if (!ui_.card)
        return;
    if (ui_.dim > 0)
        darken(ctx_.screen, int(ui_.dim));
    auto show = [&](const std::string& file, float alpha) {
        const image::Image& img = picture(file);
        blend(ctx_.screen, img, (kScreenW - img.w) / 2, (kScreenH - img.h - ui_.cardRaise) / 2, int(alpha));
    };
    if (ui_.card >= 2) {
        show(ui_.cardKind == 1 ? "inventory/items/pass" + ui_.cardWho + "/exapass" + ui_.cardWho + ".png"
                               : "inventory/interface/card.png",
             ui_.cardAlpha);
        if (ui_.cardKind == 2)
            show("inventory/interface/print.png", ui_.printAlpha);
    }
}

// --- The scrapbook ----------------------------------------------------------------

std::vector<ScenePage::ScrapRow> ScenePage::scrapRows() {
    // Text\Scrapbook\<section>.txt (0x41ec60): Sub-Heading (its
    // translation after it), Condition, Entry, a second Entry; the
    // instructions' first column is their condition and their third their
    // tick.
    std::vector<ScrapRow> out;
    const std::string file = std::string("scrapbook/") + kSections[std::clamp(ui_.section, 0, 8)] + ".txt";
    const Table& t = itemTable(file.c_str());
    for (size_t r = 0; r < t.rows.size(); r++) {
        ScrapRow row;
        row.heading = ui_.section == 2 ? unquoted(t.cell(r, 0)) : translated(t, r, 0);
        row.condition = unquoted(t.cell(r, ui_.section == 2 ? 0 : 2));
        if (ui_.section == 2)
            row.tick = unquoted(t.cell(r, 2));
        row.entry = translated(t, r, 3);
        row.entry2 = translated(t, r, 5);
        if (!row.heading.empty() || !row.entry.empty())
            out.push_back(std::move(row));
    }
    return out;
}

void ScenePage::scrapbookBack() {
    // 0x41f140: a page back, else up a level: an entry to its section's
    // list, or to the contents.
    int& at = ui_.at[ui_.level];
    if (at > 0) {
        at--;
        return;
    }
    if (ui_.section == 0)
        return;
    const int s = ui_.section;
    if (ui_.level == 2 && s != 2 && s != 4 && s != 7 && s != 8) {
        ui_.level = 1;
        return;
    }
    ui_.section = ui_.level = 0;
    ui_.at[0] = 0;
}

int ScenePage::drawScrapbook(int alpha, bool drawIt, int clickX, int clickY, std::string* clicked) {
    // The page shown (0x41e860) or clicked (0x41e940). Text from (x + 50,
    // y + 60), font 4 (journal text), colour 13, rows 5 apart, a new page
    // where a row would end past y 525.
    const int wx = (kScreenW - picture("inventory/interface/windowscrapbook.png").w) / 2, left = wx + 50,
              top = kTop + 60, right = wx + 460;
    const std::vector<ScrapRow> rows = scrapRows();
    auto holds = [&](const std::string& c) { return testCondition(c, state_); };
    const int shownPage = ui_.at[ui_.level];
    int page = 0, y = top;
    auto place = [&](int h) {
        if (y + h >= 525)
            y = top, page++;
    };
    auto lineSize = [&](const std::string& text, int& w, int& h) {
        w = h = 0;
        if (ctx_.fonts && ctx_.fonts->styles().size() > 4 && ctx_.fonts->styles()[4].font) {
            const TextStyle& s = ctx_.fonts->styles()[4];
            w = s.font->width(text, s.pixels);
            h = s.font->lineHeight(s.pixels);
        }
    };
    const bool click = clickX >= 0;
    std::string hit;
    int hitRow = -1;
    if (ui_.level == 0) {
        // The contents: the sections shown.
        for (size_t i = 0; i < rows.size(); i++) {
            if (!holds(rows[i].condition))
                continue;
            int w, h;
            lineSize(rows[i].heading, w, h);
            place(h);
            if (page == shownPage) {
                if (drawIt)
                    uiText(4, 13, left, y, left + 2000, y + h, 0, rows[i].heading, alpha);
                if (click && inside(clickX, clickY, left, y, left + w, y + h))
                    hitRow = int(i);
            }
            y += h + 5;
        }
    } else if (ui_.level == 1) {
        // A section's list: each Sub-Heading once; for the suspects, the
        // passport and fingerprint card icons (0x41f450).
        std::vector<std::string> seen;
        int suspect = 0;
        for (const ScrapRow& r : rows) {
            if (!holds(r.condition) || std::find(seen.begin(), seen.end(), r.heading) != seen.end())
                continue;
            seen.push_back(r.heading);
            int w, h;
            lineSize(r.heading, w, h);
            place(h);
            if (page == shownPage) {
                if (drawIt) {
                    uiText(4, 13, left, y, left + 2000, y + h, 0, r.heading, alpha);
                    if (ui_.section == 3 && suspect < int(std::size(kSuspectPasses))) {
                        const image::Image& pass = picture("interface/passicon.png");
                        const image::Image& print = picture("interface/printicon.png");
                        if (state_.itemState(kSuspectPasses[suspect]))
                            blend(ctx_.screen, pass, left + 225, y + (h - pass.h) / 2, alpha);
                        if (state_.itemState(kSuspectPrints[suspect]))
                            blend(ctx_.screen, print, left + 250, y + (h - print.h) / 2, alpha);
                    }
                }
                if (click && inside(clickX, clickY, left, y, left + w, y + h))
                    hit = r.heading;
            }
            suspect++;
            y += h + 5;
        }
    } else if (ui_.section == 1) {
        // A clipping on the newspaper (0x41f8d0, 0x41fbc0): the date, the
        // headline, the sub-headline, the photograph, its caption.
        const image::Image& paper = picture("inventory/scrapbook/newspaperbase.png");
        const int nx = wx + 41, ny = kTop + 56;
        std::vector<std::string> parts;
        for (const ScrapRow& r : rows)
            if (r.heading == ui_.entry && parts.size() < 5)
                parts.push_back(r.entry);
        parts.resize(5);
        if (drawIt) {
            blend(ctx_.screen, paper, nx, ny, alpha);
            blend(ctx_.screen, picture("inventory/scrapbook/" + fs::toLower(parts[3])), nx + 40, ny + 110, alpha);
            uiText(11, 14, nx + 40, ny + 40, nx + 392, ny + 56, 6, parts[0], alpha);
            int lines = 0;
            textHeight(9, 372, parts[1], &lines);
            uiText(lines > 2 ? 10 : 9, 14, nx + 30, ny + 56, nx + 402, ny + 112, 5, parts[1], alpha);
            uiText(10, 14, nx + 192, ny + 110, nx + 392, ny + 192, 5, parts[2], alpha);
            textHeight(11, 136, parts[4], &lines);
            uiText(11, 14, nx + 40, ny + 264, nx + 176, ny + 304, lines == 1 ? 5 : 4, parts[4], alpha);
        }
    } else {
        // An entry's rows: the instructions (struck through once done),
        // a suspect's facts, the timetable, Barnaby's, the challenge's
        // (0x420280, 0x41fff0, 0x41fdc0, 0x420440).
        int column = 0;
        if (ui_.section == 3) {
            for (const ScrapRow& r : rows)
                if (r.heading == ui_.entry) {
                    int w, h;
                    lineSize(r.entry, w, h);
                    column = std::max(column, w + 25);
                }
        } else if (ui_.section == 4) {
            for (const ScrapRow& r : rows)
                if (holds(r.condition)) {
                    int w, h;
                    lineSize(r.heading, w, h);
                    column = 2 * w;
                    break;
                }
        }
        for (const ScrapRow& r : rows) {
            if (!holds(r.condition) || (ui_.section == 3 && r.heading != ui_.entry))
                continue;
            const std::string& text = ui_.section == 3 ? r.entry2 : r.entry;
            const int h = textHeight(4, right - left - column, text);
            place(h);
            if (page == shownPage && drawIt) {
                if (ui_.section == 3 || ui_.section == 4) {
                    int w, lh;
                    lineSize(ui_.section == 3 ? r.entry : r.heading, w, lh);
                    uiText(4, 13, left, y, left + 2000, y + lh, 0, ui_.section == 3 ? r.entry : r.heading, alpha);
                }
                const int colour = (ui_.section == 7 || ui_.section == 8) && r.heading == "Antoinette" ? 12 : 13;
                const int flags = ui_.section == 2 && holds(r.tick) ? 0x1000 : 0;
                uiText(4, colour, left + column, y, right, y + 100, flags, text, alpha);
            }
            y += h + 5;
        }
    }
    const int pages = page + 1;
    ui_.pages[ui_.level] = pages;
    if (ui_.at[ui_.level] >= pages)
        ui_.at[ui_.level] = pages - 1;
    // The arrows: back while there's a page or a level to go back to,
    // on while there's a page after (0x41f100, 0x41f0b0).
    const bool back = ui_.section > 0 || ui_.at[ui_.level] > 0, on = ui_.at[ui_.level] < pages - 1;
    const image::Image& backArrow = picture("inventory/interface/scraparrowbackward.png");
    const image::Image& onArrow = picture("inventory/interface/scraparrowforward.png");
    const int ax = wx + 41, ox = wx + 440, ay = kTop + 520;
    if (drawIt) {
        if (back)
            blend(ctx_.screen, backArrow, ax, ay, alpha);
        if (on)
            blend(ctx_.screen, onArrow, ox, ay, alpha);
    }
    if (!click)
        return pages;
    std::string done;
    if (hitRow >= 0 && hitRow + 1 <= 8) {
        // A section opened (select1.wav): the instructions, the timetable,
        // Barnaby's and the challenge straight to their entries.
        playSound("select1");
        ui_.section = hitRow + 1;
        const int s = ui_.section;
        ui_.level = s == 2 || s == 4 || s == 7 || s == 8 ? 2 : 1;
        ui_.at[ui_.level] = 0;
        done = "section";
    } else if (!hit.empty()) {
        // An entry opened; a map's or a document's picture full screen
        // (0x41fa90).
        playSound("select1");
        ui_.entry = hit;
        if (ui_.section == 5 || ui_.section == 6) {
            for (const ScrapRow& r : rows)
                if (r.heading == hit) {
                    ui_.zoom = "inventory/scrapbook/" + fs::toLower(r.entry);
                    break;
                }
        } else {
            ui_.level = 2;
            ui_.at[2] = 0;
        }
        done = "entry";
    } else if (back && inside(clickX, clickY, ax, ay, ax + backArrow.w, ay + backArrow.h)) {
        playSound("mouseover1");
        scrapbookBack();
        done = "back";
    } else if (on && inside(clickX, clickY, ox, ay, ox + onArrow.w, ay + onArrow.h)) {
        playSound("mouseover1");
        ui_.at[ui_.level]++;
        done = "on";
    }
    if (clicked)
        *clicked = done;
    return pages;
}

} // namespace reluva::awe
