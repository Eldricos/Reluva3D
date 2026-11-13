// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's interface, as motoe.exe draws it over a game's scenes
// (docs/engines/awe/games/motoe/systems/interface.md): the bar along the top (0x417290), the
// inventory window and its six pages (0x4190d0...), the item in hand
// (0x415650), and the passport or fingerprint card a script holds up
// (0x417ba0).

#include "engines/awe/pages/viewers.h"

#include "common/fs.h"
#include "engines/awe/formats/tables.h"

#include <algorithm>
#include <cmath>

namespace reluva::awe {

namespace {

// The letters in UTF-8 text (the game counts its UTF-16 characters).
size_t letters(const std::string &s) {
    size_t n = 0;
    for (unsigned char c : s)
        n += (c & 0xc0) != 0x80;
    return n;
}

// The bar's sections' right ends (0x48d650): the bag, the five cars, the
// hat; and the cars' words in a location's name (0x417760).
constexpr int kBarEnds[7] = {75, 250, 422, 595, 771, 950, 1024};
constexpr const char *kCars[5] = {"baggage", "restaurant", "salon", "calais", "athens"};

// The window's top; its pages' pictures (0x41af10, 0x41a110, 0x4182f0,
// 0x41e5e0, 0x41cea0, 0x41d6d0).
constexpr int kTop = 10;
constexpr const char *kPages[6] = {"windowinventory", "windowexamine",   "windowcombine",
                                   "windowscrapbook", "windowpassports", "windowfingerprints"};

// The scrapbook's sections, as contents.txt lists them (0x41ec60).
constexpr const char *kSections[9] = {"contents", "clippings", "instructions", "suspects", "timetable",
                                      "maps",     "docs",      "barnaby",      "challenge"};
// The suspects' passports and fingerprint cards, by their place in
// suspects.txt (0x48f268, 0x48f2b4).
constexpr const char *kSuspectPasses[] = {"passcount",       "passcount",       "passarbuthnot", "passdebenham",
                                          "passdragomiroff", "passfoscarelli",  "passhardman",   "passhubbard",
                                          "passmacqueen",    "passmasterman",   "passohlsson",   "passschmidt",
                                          "passmichel",      "passconstantine", "passklaus",     "passlucien",
                                          "passmatteo",      "passengineer",    "passfireman"};
constexpr const char *kSuspectPrints[] = {"printcount",       "printcountess",    "printarbuthnot", "printdebenham",
                                          "printdragomiroff", "printfoscarelli",  "printhardman",   "printhubbard",
                                          "printmacqueen",    "printmasterman",   "printohlsson",   "printschmidt",
                                          "printmichel",      "printconstantine", "printklaus",     "printlucien",
                                          "printmatteo",      "printengineer",    "printfireman"};

// The window's pages' places (the floats after their vtables).
constexpr int kArrowBack[4] = {46, 29, 76, 44}, kArrowOn[4] = {435, 29, 464, 44}; // 0x41c980
constexpr int kBlipLeft = 78, kBlipSpan = 342;                                    // 0x482814, 0x482810

bool inside(int x, int y, int l, int t, int r, int b) {
    return x >= l && x < r && y >= t && y < b;
}

// A picture over the screen, faded by `alpha`, between columns clipL and
// clipR; the screen's alpha kept (it may be an overlay on the 3D).
void blend(Surface &dst, const image::Image &img, int x, int y, int alpha, int clipL = 0, int clipR = kScreenW) {
    if (img.w <= 0 || alpha <= 0)
        return;
    const int x0 = std::max({0, clipL - x});
    const int x1 = std::min({img.w, clipR - x, dst.w - x});
    for (int sy = std::max(0, -y); sy < img.h && y + sy < dst.h; sy++) {
        uint8_t *row = dst.row(y + sy);
        for (int sx = x0; sx < x1; sx++) {
            const uint8_t *s = img.pixel(sx, sy);
            uint8_t *d = row + (x + sx) * 4;
            const int a = s[3] * std::min(alpha, 255) / 255;
            const int da = d[3] * (255 - a) / 255;
            const int out = a + da;
            if (out == 0)
                continue;
            for (int c = 0; c < 3; c++)
                d[c] = uint8_t((s[c] * a + d[c] * da) / out);
            d[3] = uint8_t(out);
        }
    }
}

void darken(Surface &dst, int alpha) {
    for (int y = 0; y < dst.h; y++) {
        uint8_t *row = dst.row(y);
        for (int x = 0; x < dst.w; x++) {
            uint8_t *d = row + x * 4;
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
std::string translated(const Table &t, size_t r, int col) {
    const std::string tr = unquoted(t.cell(r, col + 1));
    return tr.empty() || tr == "0" ? unquoted(t.cell(r, col)) : tr;
}

bool startsWith(const std::string &s, const char *p) {
    return s.rfind(p, 0) == 0;
}

} // namespace

// --- When it's there ------------------------------------------------------------

bool ScenePage::interfaceOn() const {
    // 0x4143c0: a scene with SHOW on (none: on), "interface on".
    if (!_playing || _interfaceOff || _movie || !_sceneShown || _load.screen || _load.state)
        return false;
    for (const Ac2Record &r : _scene.records)
        if (r.tag == "SHOW")
            return r.i("value") != 0;
    return true;
}

bool ScenePage::canAct() const {
    // 0x415fa0: no fade, nothing loading, no locked script running, no
    // dialogue box, no message, no scrapbook picture full screen.
    return _playing && _fade == _fadeTarget && !_load.state && !_movie && !(_runner->locked() && !_runner->idle()) &&
           !_dialogue->boxShown() && _messageTime <= 0 && _ui.zoom.empty();
}

// --- Items ------------------------------------------------------------------------

ScenePage::ItemInfo ScenePage::itemInfo(const std::string &item) const {
    // Text\itemlist.txt (0x420a70): Item Name, In-game Item Description,
    // Translation, Examine Event, Examine Description, Translation,
    // Flipped Description, Translation; "0" is none.
    ItemInfo info;
    const Table &t = itemTable("itemlist.txt");
    for (size_t r = 0; r < t.rows.size(); r++)
        if (fs::toLower(unquoted(t.cell(r, 0))) == item) {
            auto none = [](const std::string &s) { return s == "0" ? std::string() : s; };
            info.description = none(translated(t, r, 1));
            info.event = none(unquoted(t.cell(r, 3)));
            info.examine = none(translated(t, r, 4));
            info.flipped = none(translated(t, r, 6));
            break;
        }
    return info;
}

std::string ScenePage::itemPicture(const std::string &item, const char *kind) const {
    // inventory\items\<item>\<kind><item>: "inv" a .png or else a .jpg;
    // "exa" and "ex2" the inv picture's path with \inv changed (0x41a1c0),
    // "" if there's none; "cur" a .png, "fol" a .jpg (0x41bc10).
    const std::string folder = "inventory/items/" + item + "/";
    const std::string kindS = kind;
    if (kindS == "cur" || kindS == "fol") {
        const std::string f = folder + kindS + item + (kindS == "cur" ? ".png" : ".jpg");
        return _ctx.files.has(f) ? f : std::string();
    }
    std::string inv = folder + "inv" + item + ".png";
    if (!_ctx.files.has(inv))
        inv = folder + "inv" + item + ".jpg";
    if (kindS == "inv")
        return inv;
    const std::string f = folder + kindS + item + inv.substr(inv.size() - 4);
    return _ctx.files.has(f) ? f : std::string();
}

int ScenePage::printRow(const std::string &item) const {
    // A fingerprint card with an Examine Description (whose print it
    // matches) is one lifted from a thing: the second row (0x41bd40).
    return itemInfo(item).examine.size() > 1 ? 1 : 0;
}

std::vector<std::string> ScenePage::bagItems() {
    // In the bag, just taken or in hand, by slot (0x41b7f0).
    std::vector<std::pair<int, std::string>> slots;
    for (const GameState::Item &i : _state.items)
        if (i.state >= 1 && i.state <= 3)
            slots.emplace_back(i.slot, i.name);
    std::stable_sort(slots.begin(), slots.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (auto &[slot, name] : slots)
        out.push_back(name);
    return out;
}

int ScenePage::examinedAt(const std::vector<std::string> &items) const {
    // The examined item's place among them: the game keeps the item itself
    // and steps to the one before or after it (0x41b920), so of several of
    // a name (the water glasses) each is reached in turn.
    if (_ui.examinedAt >= 0 && _ui.examinedAt < int(items.size()) && items[size_t(_ui.examinedAt)] == _ui.examined)
        return _ui.examinedAt;
    return int(std::find(items.begin(), items.end(), _ui.examined) - items.begin());
}

std::string ScenePage::itemInSlot(int slot) {
    // The item in the bag (or just taken) in that slot (0x41b790).
    for (const GameState::Item &i : _state.items)
        if ((i.state == 1 || i.state == 2) && i.slot == slot)
            return i.name;
    return {};
}

int ScenePage::itemPages() {
    // One empty page after the last used, 2 to 10 (0x41b6c0).
    int last = 0;
    for (const GameState::Item &i : _state.items)
        if ((i.state == 1 || i.state == 2) && i.slot / 16 != 10)
            last = std::max(last, i.slot / 16);
    return std::clamp(last + 2, 2, 10);
}

void ScenePage::holdItem(const std::string &item, int slot) {
    // In hand (0x41c000): the one in that slot, else the first in the bag;
    // the item held before goes back.
    releaseHeld();
    GameState::Item *in = nullptr;
    for (GameState::Item &i : _state.items)
        if (i.name == item && (i.state == 1 || i.state == 2) && (slot < 0 || i.slot == slot)) {
            in = &i;
            break;
        }
    if (!in)
        return;
    _state.held = item;
    in->state = 3;
}

void ScenePage::releaseHeld() {
    // Back in the bag (0x41c030).
    for (GameState::Item &i : _state.items)
        if (i.state == 3)
            i.state = 2;
    _state.held.clear();
}

void ScenePage::endFresh() {
    // The item just taken stops blinking: in the bag (0x41ba80).
    for (GameState::Item &i : _state.items)
        if (i.state == 1)
            i.state = 2;
    _state.fresh.clear();
    _ui.freshItem.clear();
}

void ScenePage::playItemSound(const std::string &item) {
    // sound\items\<item>.wav, or default.wav; none for passports and
    // fingerprint cards (0x419c10).
    if (startsWith(item, "pass") || startsWith(item, "print"))
        return;
    playSound(_ctx.files.has("sound/items/" + item + ".wav") ? item : "default");
}

void ScenePage::runFromInventory(const std::string &script, bool fromInventory) {
    // 0x419f20: the player stops, what was running is dropped, and the
    // script runs; its talks leave the window open if it's the window's.
    for (Character &c : _characters)
        if (c.player)
            c.path.clear();
    _ui.fromInventory = fromInventory;
    _runner->clear();
    _runner->run(script);
}

void ScenePage::talkStarting(const std::string &name) {
    // 0x43a670, once the conversation is found: the window closes unless
    // a script of its own started the talk; then, unless the name has
    // "gen_" or "com_" in it (0x43e750: anywhere, any case), the item in
    // hand goes back and the one just taken stops blinking.
    if (_ui.open && !_ui.fromInventory)
        closeInventory();
    _ui.fromInventory = false;
    const std::string n = fs::toLower(name);
    if (n.find("gen_") == std::string::npos && n.find("com_") == std::string::npos) {
        releaseHeld();
        endFresh();
    }
}

// --- The window -------------------------------------------------------------------

void ScenePage::toggleInventory() {
    // 0x4194d0. Closed with an item in hand: only the item put back.
    if (_ui.open) {
        closeInventory();
        return;
    }
    if (!_state.held.empty()) {
        releaseHeld();
        return;
    }
    _ui.open = true;
    _ui.page = 0;
    _ui.fade = 1;
    _ui.alpha = 0;
    _ui.fromInventory = false;
    _ui.hover = pageButton(_lastMouseX, _lastMouseY);
    showPage(0);
}

void ScenePage::closeInventory() {
    if (!_ui.open || _ui.fade == 2)
        return;
    endFresh();
    _ui.fade = 2;
    _ui.alpha = 255;
    _ui.dragging = 0;
}

void ScenePage::showPage(int page) {
    // Each page's activation.
    _ui.page = page;
    if (page == 1) {
        // Examine (0x41a4f0): the item in hand, else the one before if it's
        // still there, else the first.
        const std::vector<std::string> items = bagItems();
        if (!_state.held.empty())
            _ui.examined = _state.held;
        else if (std::find(items.begin(), items.end(), _ui.examined) == items.end())
            _ui.examined = items.empty() ? std::string() : items.front();
        _ui.back = false;
    } else if (page == 2) {
        _ui.message.clear(); // 0x418a30
    } else if (page == 4) {
        // Passports (0x41d0b0): 12 a page, up to 3 pages.
        int n = 0;
        for (const GameState::Item &i : _state.items)
            n += i.state == 5;
        if (n > 12)
            _ui.passPages = std::max(_ui.passPages, 2);
        if (n > 24)
            _ui.passPages = 3;
    } else if (page == 5) {
        _ui.printAt[0] = _ui.printAt[1] = 0; // 0x41d6c0
    }
}

bool ScenePage::interfaceUpdate(float dt) {
    bool changed = false;
    const int mx = _lastMouseX;
    const int my = _lastMouseY;
    // The window's fade (0x419990): 1000 alpha a second.
    if (_ui.fade == 1 || _ui.fade == 3) {
        _ui.alpha += dt * 1000;
        if (_ui.alpha >= 255)
            _ui.alpha = 255, _ui.fade = 0;
        changed = true;
    } else if (_ui.fade == 2) {
        _ui.alpha -= dt * 1000;
        if (_ui.alpha <= 0)
            _ui.alpha = 255, _ui.fade = 0, _ui.open = false;
        changed = true;
    }
    // The bar (0x417120).
    if (_ui.open && _ui.alpha >= 255) {
        if (_ui.barAlpha != 0 || _ui.barShown)
            changed = true;
        _ui.barAlpha = 0;
        _ui.barShown = false;
    } else if (!_ui.open && canAct() && _runner->idle() && interfaceOn()) {
        const float before = _ui.barAlpha;
        if (my > 60) {
            if (_ui.barShown || _ui.barAlpha > 0) {
                _ui.barAlpha -= dt * 1000;
                if (_ui.barAlpha <= 0)
                    _ui.barAlpha = 0, _ui.barShown = false;
            }
        } else if (my < 5 || (_ui.barAlpha < 255 && _ui.barAlpha > 0)) {
            if (!_ui.barShown || _ui.barAlpha < 255) {
                _ui.barAlpha += dt * 1000;
                if (_ui.barAlpha >= 255)
                    _ui.barAlpha = 255, _ui.barShown = true;
            }
        }
        changed |= before != _ui.barAlpha;
    }
    if (_ui.open) {
        // The item just taken blinks for 5 s of the items page shown
        // (0x41b0d0).
        if (_state.fresh != _ui.freshItem) {
            _ui.freshItem = _state.fresh;
            _ui.freshTime = 5;
        }
        if (_ui.page == 0 && !_ui.freshItem.empty() && interfaceOn()) {
            _ui.freshTime -= dt;
            if (_ui.freshTime <= 0)
                endFresh();
            changed = true;
        }
        // The button under the mouse (0x419a29): mouseover2.wav as it
        // changes.
        if (_ui.zoom.empty()) {
            const int b = pageButton(mx, my);
            if (b != _ui.hover) {
                _ui.hover = b;
                if (b != -1)
                    playSound("mouseover2");
                changed = true;
            }
        }
    }
    // A card held up (0x417e10, 0x415850): the screen darkens to half, 8
    // every 0.01 s; the pictures fade 8 every 0.025 s (0x40eda0).
    if (_ui.card) {
        auto finish = [&](const std::string &verb) {
            if (ScriptRunner *r = _ui.cardRunner) {
                _ui.cardRunner = nullptr;
                r->finished(verb);
            }
        };
        auto fadeStep = [&](float &alpha, int &mode, float &clock) {
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
        fadeStep(_ui.cardAlpha, _ui.cardFade, _ui.cardClock);
        fadeStep(_ui.printAlpha, _ui.printFade, _ui.printClock);
        if (_ui.card == 1 || _ui.card == 5) {
            for (_ui.dimClock += dt; _ui.dimClock >= 0.01f; _ui.dimClock -= 0.01f) {
                _ui.dim += _ui.card == 1 ? 8 : -8;
                if (_ui.card == 1 && _ui.dim >= 128) {
                    // The card fades in; a passport's event is done.
                    _ui.dim = 128;
                    _ui.card = 2;
                    _ui.cardFade = 2;
                    _ui.cardAlpha = 0;
                    _ui.cardClock = 0;
                    if (_ui.cardKind == 1) {
                        _ui.card = 3;
                        finish(_ui.cardVerb);
                        playSound("passport");
                    }
                    break;
                }
                if (_ui.card == 5 && _ui.dim <= 0) {
                    // Done: the "end" event over.
                    _ui.dim = 0;
                    _ui.card = 0;
                    finish(_ui.cardVerb);
                    break;
                }
            }
        } else if (_ui.card == 2 && _ui.cardAlpha >= 255) {
            // The fingerprint card in: the print fades in over it.
            _ui.card = 3;
            finish(_ui.cardVerb);
            playSound("fingerprint");
            _ui.printFade = 2;
            _ui.printAlpha = 0;
            _ui.printClock = 0;
        } else if (_ui.card == 4 && _ui.cardAlpha <= 0) {
            _ui.card = 5;
            _ui.dimClock = 0;
        }
        changed = true;
    }
    return changed;
}

bool ScenePage::startCard(ScriptRunner &runner, const ScriptEvent &e) {
    const std::string v = fs::toLower(e.verb());
    if (v == "startpass" || v == "startprint") {
        // 0x417ba0: 80 higher with the dialogue box up.
        _ui.card = 1;
        _ui.cardKind = v == "startpass" ? 1 : 2;
        _ui.cardWho = fs::toLower(e.arg(1));
        _ui.cardRaise = _dialogue->boxShown() ? 160 : 0;
        _ui.dim = _ui.dimClock = 0;
        _ui.cardAlpha = _ui.printAlpha = 0;
        _ui.cardFade = _ui.printFade = 0;
        _ui.cardRunner = &runner;
        _ui.cardVerb = v;
        return true;
    }
    // endpass, endprint (0x418090): the pictures fade out.
    if (_ui.card != 3)
        return false;
    _ui.card = 4;
    _ui.cardAlpha = 255;
    _ui.cardFade = 1;
    _ui.cardClock = 0;
    if (_ui.cardKind == 2) {
        _ui.printAlpha = 255;
        _ui.printFade = 1;
        _ui.printClock = 0;
    }
    _ui.cardRunner = &runner;
    _ui.cardVerb = v;
    return true;
}

// --- Text -------------------------------------------------------------------------

int ScenePage::textHeight(int font, int width, const std::string &text, int *lines) {
    if (!_ctx.fonts || font < 0 || size_t(font) >= _ctx.fonts->styles().size())
        return 0;
    const TextStyle &s = _ctx.fonts->styles()[size_t(font)];
    if (!s.font)
        return 0;
    const int lh = s.font->lineHeight(s.pixels);
    const int n = s.font->width(text, s.pixels) <= width ? 1 : int(gameRows(*s.font, s.pixels, text, width).size());
    if (lines)
        *lines = n;
    return n * lh;
}

int ScenePage::uiText(int font, int colour, int l, int t, int r, int b, int flags, const std::string &text, int alpha) {
    if (!_ctx.fonts || text.empty() || font < 0 || size_t(font) >= _ctx.fonts->styles().size() || alpha <= 0)
        return 0;
    const TextStyle &s = _ctx.fonts->styles()[size_t(font)];
    if (!s.font)
        return 0;
    const Color c = _ctx.fonts->colourAt(size_t(colour));
    const int lh = s.font->lineHeight(s.pixels);
    // A text that fits is one row as it is; a wider one is broken into
    // the game's rows, the last drawn only if it has more than one letter
    // (0x414b34), though it is counted in the height (0x414c30).
    const bool wrapped = s.font->width(text, s.pixels) > r - l;
    const std::vector<std::string> lines =
        wrapped ? gameRows(*s.font, s.pixels, text, r - l) : std::vector<std::string>{text};
    const int h = int(lines.size()) * lh;
    int y = flags & 8 ? b - h : flags & 4 ? t + (b - t - h) / 2 : t;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string &line = lines[i];
        if (i + 1 == lines.size() && wrapped && letters(line) <= 1)
            break;
        const int w = s.font->width(line, s.pixels);
        const int x = flags & 2 ? r - w : flags & 1 ? l + (r - l - w) / 2 : l;
        _ctx.fonts->draw(_ctx.screen, s, x, y, line, c, uint8_t(std::min(alpha, 255)));
        if (flags & 0x1000)
            for (int px = x; px < x + w; px++)
                if (px >= 0 && px < kScreenW && y + lh / 2 >= 0 && y + lh / 2 < kScreenH) {
                    uint8_t *d = _ctx.screen.row(y + lh / 2) + px * 4;
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

int ScenePage::pageButton(int x, int y, int *bx, int *by) {
    // The tabs (0x41cc90), then the page's own button (7: examine's eye,
    // combine's gears, 8: the fingerprint cards' magnifier).
    const int wx = (kScreenW - std::max(1, picture("inventory/interface/windowblank.png").w)) / 2;
    for (int i = 0, ty = kTop + 43; i < 7; i++, ty += 78)
        if (inside(x, y, wx + 511, ty, wx + 578, ty + 60)) {
            if (bx)
                *bx = wx + 511, *by = ty;
            return i;
        }
    int button = -1;
    int l = 0;
    int t = 0;
    int r = 0;
    int b = 0;
    if (_ui.page == 1)
        button = 7, l = 220, t = 411, r = 292, b = 476;
    else if (_ui.page == 2)
        button = 7, l = 221, t = 240, r = 292, b = 303;
    else if (_ui.page == 5)
        button = 8, l = 223, t = 230, r = 286, b = 287;
    if (button < 0 || !inside(x, y, wx + l, kTop + t, wx + r, kTop + b))
        return -1;
    if (bx)
        *bx = wx + l, *by = kTop + t;
    return button;
}

bool ScenePage::blipClick(int x, int y, int row, int count, int &at) {
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
        _ui.dragging = row;
        playSound("mouseover1");
        return true;
    }
    return false;
}

bool ScenePage::interfaceEvent(const Event &ev) {
    const int x = ev.mouseX;
    const int y = ev.mouseY;
    if (ev.type == EventType::kMouseMove) {
        if (_ui.dragging) {
            // The blip follows the mouse to the nearest place (0x41afd0).
            const int wx = (kScreenW - picture("inventory/interface/windowblank.png").w) / 2;
            const int bw = picture("inventory/interface/blip.png").w;
            auto nearest = [&](int count) {
                if (count <= 1)
                    return 0;
                const float step = float(kBlipSpan) / float(count - 1);
                const float at = float(x - bw / 2 - wx - kBlipLeft);
                int i = 0;
                while (i < count - 1 && at > float(i) * step + step * 0.5f)
                    i++;
                return i;
            };
            if (_ui.page == 0)
                _state.itemPage = nearest(itemPages());
            else if (_ui.page == 1) {
                const std::vector<std::string> items = bagItems();
                const int i = nearest(int(items.size()));
                if (i < int(items.size()) && i != examinedAt(items)) {
                    _ui.examined = items[size_t(i)];
                    _ui.examinedAt = i;
                    _ui.back = false;
                    if (!_state.held.empty())
                        holdItem(_ui.examined);
                }
            } else if (_ui.page == 4) {
                const int p = nearest(_ui.passPages);
                if (p != _ui.passPage)
                    _ui.passPage = p, _ui.passAt = p * 12;
            } else if (_ui.page == 5) {
                int n[2] = {0, 0};
                for (const GameState::Item &i : _state.items)
                    if (i.state == 6)
                        n[printRow(i.name)]++;
                _ui.printAt[_ui.dragging - 1] = nearest(n[_ui.dragging - 1]);
            }
        }
        if (_ui.open || _ui.barShown || _ui.barAlpha > 0 || !_state.held.empty())
            draw();
        return false;
    }
    if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        // The blip let go (0x419d10).
        if (!_ui.dragging)
            return false;
        _ui.dragging = 0;
        draw();
        return true;
    }
    if (ev.type == EventType::kKeyDown && (ev.key == 'i' || ev.key == 'I') && _playing && !(ev.mods & keymod::kShift)) {
        // I (0x416806): the window, with the interface there and no
        // dialogue box.
        if (interfaceOn() && !_dialogue->boxShown())
            toggleInventory();
        draw();
        return true;
    }
    if (ev.type != EventType::kMouseDown || !_playing)
        return false;
    if (!_ui.zoom.empty() && (ev.button == MouseButton::kLeft || ev.button == MouseButton::kRight)) {
        // A scrapbook picture full screen: any click puts it away (0x41fa70).
        _ui.zoom.clear();
        draw();
        return true;
    }
    if (ev.button == MouseButton::kRight) {
        // 0x416450: the window opened or closed, or the item in hand put
        // back.
        if (!canAct())
            return false;
        toggleInventory();
        draw();
        return true;
    }
    if (ev.button != MouseButton::kLeft || !canAct())
        return false;
    const bool on = interfaceOn();
    // The window (0x419d30): its page, its tabs; any click on it is its.
    if (on && _ui.open) {
        const image::Image &frame = picture("inventory/interface/windowblank.png");
        const int wx = (kScreenW - frame.w) / 2;
        const bool onFrame =
            inside(x, y, wx, kTop, wx + frame.w, kTop + frame.h) && frame.pixel(x - wx, y - kTop)[3] != 0;
        if (_ui.fade != 0 && onFrame)
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
            else if (b != _ui.page) {
                endFresh();
                _ui.fromPage = _ui.page;
                showPage(b);
                _ui.fade = 3;
                _ui.alpha = 0;
            }
            draw();
            return true;
        }
        if (onFrame)
            return true;
    }
    // The scene's part of the screen: below the bar (0x417580).
    const bool inScene = (!_ui.barShown && _ui.barAlpha == 0) ? y >= 5 : y >= 60;
    // An item in hand: used on what's under it (0x419f10).
    if (!_state.held.empty()) {
        // What the game's pick finds there (0x41613c).
        const Ac2Record *target = inScene && pickAllowed(y) ? pickAt(x, y) : nullptr;
        if (target) {
            _runner->setFromCharacter(target->tag == "NPCX");
            if (target->tag == "NPCX")
                _resumeAfter = target;
            useItem(_state.held, *target);
        } else {
            logScript("use " + _state.held + ": (nothing there)");
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
            _runner->clear();
            _runner->run("fade out, scene " + carScene(section) + " 0, fade in");
        }
    }
    playSound("select1");
    draw();
    return true;
}

int ScenePage::barSection(int x, int y) {
    // 0x4175c0: fully shown, on an opaque pixel of the bar's picture; the
    // first section whose end is past x.
    if (!_ui.barShown || _ui.barAlpha < 255)
        return -1;
    const image::Image &bar = picture("interface/topmenubar01.png");
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
    auto test = [&](const char *c) { return testCondition(c, _state); };
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
        return inCar(5) && !testCondition("tag athensunlocked", _state) ? "calais.10-11_10.02 4"
               : fromFront                                              ? "calais.hall_01 1"
                                                                        : "calais.hall_06 2";
    default:
        return fromFront || inCar(4) ? "athens.hall_01 1" : "athens.hall_06 2";
    }
}

bool ScenePage::inCar(int car) const {
    // 0x417760: the location's name has the car's word in it.
    return car >= 1 && car <= 5 && _location.find(kCars[car - 1]) != std::string::npos;
}

bool ScenePage::pageClick(int x, int y) {
    const int wx = (kScreenW - picture("inventory/interface/windowblank.png").w) / 2;
    switch (_ui.page) {
    case 0: {
        // The items (0x41b150).
        int page = _state.itemPage;
        if (blipClick(x, y, 1, itemPages(), page)) {
            _state.itemPage = page;
            return true;
        }
        for (int box = 0; box < 16; box++) {
            const int bx = wx + 38 + 112 * (box % 4);
            const int by = kTop + 54 + 112 * (box / 4);
            if (!inside(x, y, bx, by, bx + 96, by + 96))
                continue;
            const int slot = _state.itemPage * 16 + box;
            const std::string there = itemInSlot(slot);
            if (_state.held.empty()) {
                if (there.empty())
                    return false;
                playItemSound(there);
                endFresh();
                _ui.hovered.clear();
                holdItem(there, slot);
                return true;
            }
            // In hand: put in an empty box, or swapped with the one there
            // (which is then in hand).
            const std::string held = _state.held;
            GameState::Item *inHand = _state.findItem(held, {3});
            if (there.empty()) {
                playItemSound(held);
                if (inHand)
                    inHand->slot = slot;
                releaseHeld();
                return true;
            }
            const int heldSlot = inHand ? inHand->slot : slot;
            for (GameState::Item &i : _state.items)
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
        int at = examinedAt(items);
        if (at < int(items.size()) && blipClick(x, y, 1, int(items.size()), at)) {
            if (_ui.dragging)
                return true;
            _ui.examined = items[size_t(at)];
            _ui.examinedAt = at;
            _ui.back = false;
            if (!_state.held.empty())
                holdItem(_ui.examined);
            return true;
        }
        if (_ui.examined.empty())
            return false;
        if (inside(x, y, wx + 220, kTop + 411, wx + 292, kTop + 476)) {
            // The eye: the back, if it has one; its Examine Event.
            const std::string other = itemPicture(_ui.examined, _ui.back ? "exa" : "ex2");
            if (!_ui.back && other.empty())
                return true;
            playSound("select1");
            _ui.back = !_ui.back;
            const ItemInfo info = itemInfo(_ui.examined);
            if (_ui.back && info.event.size() > 1) {
                playSound("discovery");
                runFromInventory(info.event, true);
            }
            return true;
        }
        if (_state.held.empty() && inside(x, y, wx + 40, kTop + 57, wx + 470, kTop + 375)) {
            playItemSound(_ui.examined);
            holdItem(_ui.examined);
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
            const int l = i < 4 ? 56 : 360;
            const int t = i < 4 ? 56 + 112 * i : 224;
            const int r = i < 4 ? 151 : 454;
            const int b = i < 4 ? 151 + 112 * i : 319;
            if (!inside(x, y, wx + l, kTop + t, wx + r, kTop + b))
                continue;
            std::string &box = _ui.boxes[i];
            if (!box.empty() && x >= wx + r - 21 && y <= kTop + t + 21) {
                playSound("select2");
                box.clear();
            } else if (!_state.held.empty()) {
                playItemSound(_state.held);
                box = _state.held;
                releaseHeld();
            } else if (!box.empty() && _state.itemAvailable(box)) {
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
        for (const GameState::Item &i : _state.items)
            if (i.state == 5)
                passes.push_back(i.name);
        for (int c = 0; c < 12; c++) {
            const int px = wx + 45 + 72 * (c % 6);
            const int py = kTop + 61 + 70 * (c / 6);
            const int n = _ui.passPage * 12 + c;
            if (inside(x, y, px, py, px + 62, py + 62) && n < int(passes.size())) {
                playItemSound(passes[size_t(n)]);
                _ui.passAt = n;
                return true;
            }
        }
        int page = _ui.passPage;
        if (blipClick(x, y, 1, _ui.passPages, page)) {
            if (page != _ui.passPage)
                _ui.passPage = page, _ui.passAt = page * 12;
            return true;
        }
        return false;
    }
    case 5: {
        // Fingerprint cards (0x41de80): each row's arrows and blip, then
        // the magnifier.
        int n[2] = {0, 0};
        for (const GameState::Item &i : _state.items)
            if (i.state == 6)
                n[printRow(i.name)]++;
        if (blipClick(x, y, 1, n[0], _ui.printAt[0]) || blipClick(x, y, 2, n[1], _ui.printAt[1]))
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
        sources += !_ui.boxes[i].empty();
    const std::string result = _ui.boxes[4];
    const Table &t = itemTable("combinelist.txt");
    auto names = [](const std::string &list) {
        std::vector<std::string> out;
        for (const Command &c : parseCommands(unquoted(list)))
            if (!c.words.empty())
                out.push_back(fs::toLower(c.words[0]));
        return out;
    };
    auto finish = [&](size_t r, const std::string &list, int first, const char *otherwise) {
        // The boxes' items used up (0x418dc0), the new ones given, just
        // taken, into the boxes from `first` (not fingerprint cards:
        // 0x418df0), the Event run.
        for (std::string &box : _ui.boxes) {
            if (GameState::Item *i = box.empty() ? nullptr : _state.findItem(box, {1, 2, 3}))
                i->state = 4;
            box.clear();
        }
        int box = first;
        for (const std::string &item : names(list)) {
            addItem(_state, item, 1);
            if (box < 5 && item.find("print") == std::string::npos)
                _ui.boxes[box++] = item;
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
                testCondition(unquoted(t.cell(r, 4)), _state)) {
                finish(r, t.cell(r, 0), 0, "talk gen_decombine");
                return true;
            }
        _ui.message = interfaceText("combine3");
        runFromInventory("talk gen_cantdecombine", true);
        return false;
    }
    playSound("combine");
    if (!result.empty()) {
        _ui.message = interfaceText("combine1");
        return false;
    }
    if (sources < 2)
        return false;
    // The row whose Parts are exactly the boxes' items, that can be
    // combined (0x418af0, 0x418cf0).
    for (size_t r = 0; r < t.rows.size(); r++) {
        const std::vector<std::string> parts = names(t.cell(r, 0));
        bool all = int(parts.size()) == sources;
        for (const std::string &p : parts)
            all = all && std::find(_ui.boxes, _ui.boxes + 4, p) != _ui.boxes + 4;
        if (all && std::atoi(unquoted(t.cell(r, 3)).c_str()) != 0 && testCondition(unquoted(t.cell(r, 4)), _state)) {
            finish(r, t.cell(r, 1), 4, "talk gen_combine");
            return true;
        }
    }
    _ui.message = interfaceText("combine2");
    std::vector<std::string> have;
    for (int i = 0; i < 4; i++)
        if (!_ui.boxes[i].empty())
            have.push_back(_ui.boxes[i]);
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
    for (const GameState::Item &i : _state.items)
        if (i.state == 6)
            rows[printRow(i.name)].push_back(i.name);
    if (rows[0].empty() || rows[1].empty())
        return;
    const std::string a = rows[0][size_t(std::min(_ui.printAt[0], int(rows[0].size()) - 1))];
    const std::string b = rows[1][size_t(std::min(_ui.printAt[1], int(rows[1].size()) - 1))];
    playItemSound(a);
    if (fs::toLower(itemInfo(b).flipped) == a) {
        _state.tags[a + "=" + b] = 0;
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
    int barAlpha = int(_ui.barAlpha);
    if (_ui.open && _ui.barShown && _ui.alpha < 255)
        barAlpha = std::min(barAlpha, 255 - int(_ui.alpha));
    if (barAlpha > 0) {
        const image::Image &dark = picture("interface/topmenubar01.png");
        const image::Image &lit = picture("interface/topmenubar02.png");
        const int hover = barSection(_lastMouseX, _lastMouseY);
        for (int s = 0, left = 0; s < 7; left = kBarEnds[s++]) {
            // Lit: the bag or the hat under the mouse, a car that can be
            // travelled to or the one the player is in.
            const bool on = s == 0 || s == 6 ? hover == s : carOn(s) || inCar(s);
            blend(_ctx.screen, on ? lit : dark, 0, 0, barAlpha, left, kBarEnds[s]);
            if (hover == s && s >= 1 && s <= 5 && on) {
                uiText(6, 11, left, 45, kBarEnds[s], 60, 1, interfaceText("map" + std::to_string(s)), barAlpha);
                if (inCar(s))
                    uiText(6, 12, left, 10, kBarEnds[s], 50, 5, interfaceText("map6"), barAlpha);
            }
        }
    }
    // The window (0x419a90): the frame, the page (cross-faded from the one
    // before), the highlight on the button under the mouse.
    if (!_ui.open)
        return;
    const image::Image &frame = picture("inventory/interface/windowblank.png");
    const int wx = (kScreenW - frame.w) / 2;
    const int a = int(_ui.alpha);
    if (_ui.fade == 3) {
        blend(_ctx.screen, frame, wx, kTop, 255);
        drawPage(_ui.fromPage, 255 - a);
    } else {
        blend(_ctx.screen, frame, wx, kTop, a);
    }
    drawPage(_ui.page, a);
    if (_ui.zoom.empty()) {
        int bx = 0;
        int by = 0;
        const int b = pageButton(_lastMouseX, _lastMouseY, &bx, &by);
        if (b == 7)
            blend(_ctx.screen, picture("inventory/interface/largehighlight.png"), bx, by, a);
        else if (b >= 0)
            blend(_ctx.screen, picture("inventory/interface/smallhighlight.png"), bx + 1, by + 1, a);
    }
    if (!_ui.zoom.empty()) {
        // A scrapbook picture full screen, over the scene darkened (0xc0).
        darken(_ctx.screen, 0xc0);
        const image::Image &z = picture(_ui.zoom);
        blend(_ctx.screen, z, (kScreenW - z.w) / 2, (kScreenH - z.h) / 2, 255);
    }
}

void ScenePage::drawBlip(int row, int count, int at, int alpha) {
    // 0x41cb50: along the line, or under the mouse while dragged.
    const image::Image &blip = picture("inventory/interface/blip.png");
    const int wx = (kScreenW - picture("inventory/interface/windowblank.png").w) / 2;
    int x = wx + kBlipLeft + (count > 1 ? at * kBlipSpan / (count - 1) : 0);
    if (_ui.dragging == row)
        x = std::clamp(_lastMouseX - blip.w / 2, wx + kBlipLeft, wx + kBlipLeft + kBlipSpan);
    blend(_ctx.screen, blip, x, kTop + (row == 1 ? 29 : 478), alpha);
}

void ScenePage::drawPage(int page, int alpha) {
    if (page < 0 || page > 5 || alpha <= 0)
        return;
    const image::Image &win = picture(std::string("inventory/interface/") + kPages[page] + ".png");
    const int wx = (kScreenW - win.w) / 2;
    blend(_ctx.screen, win, wx, kTop, alpha);
    // The description bar (0x41cae0).
    auto bar = [&](const std::string &text) {
        uiText(6, 6, wx + 45, kTop + 520, wx + 465, kTop + 568, 5, text, alpha);
    };
    switch (page) {
    case 0: {
        // The items (0x41b080, 0x41bab0): one just taken blinks.
        drawBlip(1, itemPages(), _state.itemPage, alpha);
        std::string hovered;
        for (const GameState::Item &i : _state.items) {
            if ((i.state != 1 && i.state != 2) || i.slot / 16 != _state.itemPage)
                continue;
            const int box = i.slot % 16;
            const int bx = wx + 38 + 112 * (box % 4);
            const int by = kTop + 54 + 112 * (box / 4);
            if (_state.held.empty() && inside(_lastMouseX, _lastMouseY, bx, by, bx + 96, by + 96))
                hovered = i.name;
            if (i.state == 1 && !_caretOn)
                continue;
            blend(_ctx.screen, picture(itemPicture(i.name, "inv")), bx + 5, by + 5, alpha);
        }
        if (!hovered.empty())
            bar(itemInfo(hovered).description);
        break;
    }
    case 1: {
        // Examine (0x41a680): the close-up, or its back, and its text.
        if (_ui.examined.empty())
            break;
        const std::vector<std::string> items = bagItems();
        const int at = examinedAt(items);
        drawBlip(1, int(items.size()), at, alpha);
        std::string pic = itemPicture(_ui.examined, _ui.back ? "ex2" : "exa");
        if (pic.empty())
            pic = itemPicture(_ui.examined, "inv");
        const image::Image &img = picture(pic);
        blend(_ctx.screen, img, wx + 40 + (432 - img.w) / 2, kTop + 56 + (320 - img.h) / 2, alpha);
        const ItemInfo info = itemInfo(_ui.examined);
        bar(_ui.back && !info.flipped.empty() ? info.flipped : !info.examine.empty() ? info.examine : info.description);
        break;
    }
    case 2: {
        // Combine (0x4183f0): the boxes, their Xs; the text of the item
        // under the mouse, else the message.
        const image::Image &x = picture("inventory/interface/x.jpg");
        std::string hovered;
        for (int i = 0; i < 5; i++) {
            const int l = wx + (i < 4 ? 56 : 360);
            const int t = kTop + (i < 4 ? 56 + 112 * i : 224);
            const int r = wx + (i < 4 ? 151 : 454);
            const int b = kTop + (i < 4 ? 151 + 112 * i : 319);
            if (_ui.boxes[i].empty())
                continue;
            if (_state.held.empty() && inside(_lastMouseX, _lastMouseY, l, t, r, b))
                hovered = _ui.boxes[i];
            blend(_ctx.screen, picture(itemPicture(_ui.boxes[i], "inv")), l, t, alpha);
            blend(_ctx.screen, x, i < 4 ? r - 21 : r - 20, i < 4 ? t + 1 : t + 2, alpha);
        }
        if (!hovered.empty()) {
            _ui.message.clear();
            bar(itemInfo(hovered).description);
        } else {
            bar(_ui.message);
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
        for (const GameState::Item &i : _state.items)
            if (i.state == 5)
                passes.push_back(i.name);
        drawBlip(1, _ui.passPages, _ui.passPage, alpha);
        if (_ui.passAt < int(passes.size())) {
            const std::string &p = passes[size_t(_ui.passAt)];
            const image::Image &img = picture(itemPicture(p, "exa"));
            blend(_ctx.screen, img, wx + 40 + (432 - img.w) / 2, kTop + 216 + (273 - img.h) / 2, alpha);
            bar(itemInfo(p).examine);
        }
        for (int c = 0; c < 12; c++) {
            const int n = _ui.passPage * 12 + c;
            if (n >= int(passes.size()))
                break;
            blend(_ctx.screen, picture(itemPicture(passes[size_t(n)], "fol")), wx + 45 + 72 * (c % 6),
                  kTop + 61 + 70 * (c / 6), alpha);
        }
        break;
    }
    case 5: {
        // Fingerprint cards (0x41d7c0): each row's card and its
        // neighbours, with their names.
        std::vector<std::string> rows[2];
        for (const GameState::Item &i : _state.items)
            if (i.state == 6)
                rows[printRow(i.name)].push_back(i.name);
        for (int r = 0; r < 2; r++) {
            const int n = int(rows[r].size());
            const int at = std::min(_ui.printAt[r], std::max(0, n - 1));
            const int y = kTop + (r == 0 ? 56 : 320);
            auto card = [&](int i, int x) {
                const std::string &item = rows[r][size_t(i)];
                blend(_ctx.screen, picture(itemPicture(item, "inv")), x, y, alpha);
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
            const std::string &b = rows[1][size_t(std::min(_ui.printAt[1], int(rows[1].size()) - 1))];
            for (const auto &[tag, v] : _state.tags)
                if (tag.size() > b.size() + 1 &&
                    tag.compare(tag.size() - b.size() - 1, std::string::npos, "=" + b) == 0) {
                    bar(itemInfo(b).examine);
                    break;
                }
        }
        drawBlip(1, int(rows[0].size()), _ui.printAt[0], alpha);
        drawBlip(2, int(rows[1].size()), _ui.printAt[1], alpha);
        break;
    }
    }
}

void ScenePage::drawCards() {
    // 0x418040: the screen darkened, the card, the print over it.
    if (!_ui.card)
        return;
    if (_ui.dim > 0)
        darken(_ctx.screen, int(_ui.dim));
    auto show = [&](const std::string &file, float alpha) {
        const image::Image &img = picture(file);
        blend(_ctx.screen, img, (kScreenW - img.w) / 2, (kScreenH - img.h - _ui.cardRaise) / 2, int(alpha));
    };
    if (_ui.card >= 2) {
        show(_ui.cardKind == 1 ? "inventory/items/pass" + _ui.cardWho + "/exapass" + _ui.cardWho + ".png"
                               : "inventory/interface/card.png",
             _ui.cardAlpha);
        if (_ui.cardKind == 2)
            show("inventory/interface/print.png", _ui.printAlpha);
    }
}

// --- The scrapbook ----------------------------------------------------------------

std::vector<ScenePage::ScrapRow> ScenePage::scrapRows() {
    // Text\Scrapbook\<section>.txt (0x41ec60): Sub-Heading (its
    // translation after it), Condition, Entry, a second Entry; the
    // instructions' first column is their condition and their third their
    // tick.
    std::vector<ScrapRow> out;
    const std::string file = std::string("scrapbook/") + kSections[std::clamp(_ui.section, 0, 8)] + ".txt";
    const Table &t = itemTable(file.c_str());
    for (size_t r = 0; r < t.rows.size(); r++) {
        ScrapRow row;
        row.heading = _ui.section == 2 ? unquoted(t.cell(r, 0)) : translated(t, r, 0);
        row.condition = unquoted(t.cell(r, _ui.section == 2 ? 0 : 2));
        if (_ui.section == 2)
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
    int &at = _ui.at[_ui.level];
    if (at > 0) {
        at--;
        return;
    }
    if (_ui.section == 0)
        return;
    const int s = _ui.section;
    if (_ui.level == 2 && s != 2 && s != 4 && s != 7 && s != 8) {
        _ui.level = 1;
        return;
    }
    _ui.section = _ui.level = 0;
    _ui.at[0] = 0;
}

int ScenePage::drawScrapbook(int alpha, bool drawIt, int clickX, int clickY, std::string *clicked) {
    // The page shown (0x41e860) or clicked (0x41e940). Text from (x + 50,
    // y + 60), font 4 (journal text), colour 13, rows 5 apart, a new page
    // where a row would end past y 525.
    const int wx = (kScreenW - picture("inventory/interface/windowscrapbook.png").w) / 2;
    const int left = wx + 50;
    const int top = kTop + 60;
    const int right = wx + 460;
    const std::vector<ScrapRow> rows = scrapRows();
    auto holds = [&](const std::string &c) { return testCondition(c, _state); };
    const int shownPage = _ui.at[_ui.level];
    int page = 0;
    int y = top;
    auto place = [&](int h) {
        if (y + h >= 525)
            y = top, page++;
    };
    auto lineSize = [&](const std::string &text, int &w, int &h) {
        w = h = 0;
        if (_ctx.fonts && _ctx.fonts->styles().size() > 4 && _ctx.fonts->styles()[4].font) {
            const TextStyle &s = _ctx.fonts->styles()[4];
            w = s.font->width(text, s.pixels);
            h = s.font->lineHeight(s.pixels);
        }
    };
    const bool click = clickX >= 0;
    std::string hit;
    int hitRow = -1;
    if (_ui.level == 0) {
        // The contents: the sections shown.
        for (size_t i = 0; i < rows.size(); i++) {
            if (!holds(rows[i].condition))
                continue;
            int w;
            int h;
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
    } else if (_ui.level == 1) {
        // A section's list: each Sub-Heading once; for the suspects, the
        // passport and fingerprint card icons (0x41f450).
        std::vector<std::string> seen;
        int suspect = 0;
        for (const ScrapRow &r : rows) {
            if (!holds(r.condition) || std::find(seen.begin(), seen.end(), r.heading) != seen.end())
                continue;
            seen.push_back(r.heading);
            int w;
            int h;
            lineSize(r.heading, w, h);
            place(h);
            if (page == shownPage) {
                if (drawIt) {
                    uiText(4, 13, left, y, left + 2000, y + h, 0, r.heading, alpha);
                    if (_ui.section == 3 && suspect < int(std::size(kSuspectPasses))) {
                        const image::Image &pass = picture("interface/passicon.png");
                        const image::Image &print = picture("interface/printicon.png");
                        if (_state.itemState(kSuspectPasses[suspect]))
                            blend(_ctx.screen, pass, left + 225, y + (h - pass.h) / 2, alpha);
                        if (_state.itemState(kSuspectPrints[suspect]))
                            blend(_ctx.screen, print, left + 250, y + (h - print.h) / 2, alpha);
                    }
                }
                if (click && inside(clickX, clickY, left, y, left + w, y + h))
                    hit = r.heading;
            }
            suspect++;
            y += h + 5;
        }
    } else if (_ui.section == 1) {
        // A clipping on the newspaper (0x41f8d0, 0x41fbc0): the date, the
        // headline, the sub-headline, the photograph, its caption.
        const image::Image &paper = picture("inventory/scrapbook/newspaperbase.png");
        const int nx = wx + 41;
        const int ny = kTop + 56;
        std::vector<std::string> parts;
        for (const ScrapRow &r : rows)
            if (r.heading == _ui.entry && parts.size() < 5)
                parts.push_back(r.entry);
        parts.resize(5);
        if (drawIt) {
            blend(_ctx.screen, paper, nx, ny, alpha);
            blend(_ctx.screen, picture("inventory/scrapbook/" + fs::toLower(parts[3])), nx + 40, ny + 110, alpha);
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
        if (_ui.section == 3) {
            for (const ScrapRow &r : rows)
                if (r.heading == _ui.entry) {
                    int w;
                    int h;
                    lineSize(r.entry, w, h);
                    column = std::max(column, w + 25);
                }
        } else if (_ui.section == 4) {
            for (const ScrapRow &r : rows)
                if (holds(r.condition)) {
                    int w;
                    int h;
                    lineSize(r.heading, w, h);
                    column = 2 * w;
                    break;
                }
        }
        for (const ScrapRow &r : rows) {
            if (!holds(r.condition) || (_ui.section == 3 && r.heading != _ui.entry))
                continue;
            const std::string &text = _ui.section == 3 ? r.entry2 : r.entry;
            const int h = textHeight(4, right - left - column, text);
            place(h);
            if (page == shownPage && drawIt) {
                if (_ui.section == 3 || _ui.section == 4) {
                    int w;
                    int lh;
                    lineSize(_ui.section == 3 ? r.entry : r.heading, w, lh);
                    uiText(4, 13, left, y, left + 2000, y + lh, 0, _ui.section == 3 ? r.entry : r.heading, alpha);
                }
                const int colour = (_ui.section == 7 || _ui.section == 8) && r.heading == "Antoinette" ? 12 : 13;
                const int flags = _ui.section == 2 && holds(r.tick) ? 0x1000 : 0;
                uiText(4, colour, left + column, y, right, y + 100, flags, text, alpha);
            }
            y += h + 5;
        }
    }
    const int pages = page + 1;
    _ui.pages[_ui.level] = pages;
    if (_ui.at[_ui.level] >= pages)
        _ui.at[_ui.level] = pages - 1;
    // The arrows: back while there's a page or a level to go back to,
    // on while there's a page after (0x41f100, 0x41f0b0).
    const bool back = _ui.section > 0 || _ui.at[_ui.level] > 0;
    const bool on = _ui.at[_ui.level] < pages - 1;
    const image::Image &backArrow = picture("inventory/interface/scraparrowbackward.png");
    const image::Image &onArrow = picture("inventory/interface/scraparrowforward.png");
    const int ax = wx + 41;
    const int ox = wx + 440;
    const int ay = kTop + 520;
    if (drawIt) {
        if (back)
            blend(_ctx.screen, backArrow, ax, ay, alpha);
        if (on)
            blend(_ctx.screen, onArrow, ox, ay, alpha);
    }
    if (!click)
        return pages;
    std::string done;
    if (hitRow >= 0 && hitRow + 1 <= 8) {
        // A section opened (select1.wav): the instructions, the timetable,
        // Barnaby's and the challenge straight to their entries.
        playSound("select1");
        _ui.section = hitRow + 1;
        const int s = _ui.section;
        _ui.level = s == 2 || s == 4 || s == 7 || s == 8 ? 2 : 1;
        _ui.at[_ui.level] = 0;
        done = "section";
    } else if (!hit.empty()) {
        // An entry opened; a map's or a document's picture full screen
        // (0x41fa90).
        playSound("select1");
        _ui.entry = hit;
        if (_ui.section == 5 || _ui.section == 6) {
            for (const ScrapRow &r : rows)
                if (r.heading == hit) {
                    _ui.zoom = "inventory/scrapbook/" + fs::toLower(r.entry);
                    break;
                }
        } else {
            _ui.level = 2;
            _ui.at[2] = 0;
        }
        done = "entry";
    } else if (back && inside(clickX, clickY, ax, ay, ax + backArrow.w, ay + backArrow.h)) {
        playSound("mouseover1");
        scrapbookBack();
        done = "back";
    } else if (on && inside(clickX, clickY, ox, ay, ox + onArrow.w, ay + onArrow.h)) {
        playSound("mouseover1");
        _ui.at[_ui.level]++;
        done = "on";
    }
    if (clicked)
        *clicked = done;
    return pages;
}

// --- A player's hand ---------------------------------------------------------------
// The inputs a player makes, one at a time, each decided from the screen as
// it is (the window's fades and the bar's let the next wait). The places
// are the ones the clicks above test.

ScenePage::HandInput ScenePage::handStep(HandGoal &goal) {
    using In = HandInput;
    auto click = [](int x, int y) { return In{In::Kind::kClick, x, y, 0}; };
    auto rightClick = [](int x, int y) { return In{In::Kind::kRightClick, x, y, 0}; };
    auto none = [] { return In{}; };
    if (goal.done || goal.failed || !canAct())
        return none();
    const int wx = (kScreenW - picture("inventory/interface/windowblank.png").w) / 2;
    const bool windowSettled = _ui.open && _ui.fade == 0 && _ui.alpha >= 255;
    if (_ui.fade != 0)
        return none(); // the window fading in or out
    auto tab = [&](int i) { return click(wx + 544, kTop + 43 + 78 * i + 30); };
    auto arrowOn = [&](int row) {
        return click(wx + (kArrowOn[0] + kArrowOn[2]) / 2,
                     kTop + (row == 2 ? 449 : 0) + (kArrowOn[1] + kArrowOn[3]) / 2);
    };
    // The window open, with nothing in hand (a right-click: an item in hand
    // is put back first, 0x416450).
    auto openWindow = [&]() -> In {
        if (!_state.held.empty() && !_ui.open)
            return rightClick(_lastMouseX, _lastMouseY);
        return rightClick(_lastMouseX, _lastMouseY);
    };
    // An item taken up from the items page: its page turned to, its box
    // clicked.
    auto takeUpFromPage = [&](const std::string &item) -> In {
        if (_ui.page != 0)
            return tab(0);
        int slot = -1;
        for (const GameState::Item &i : _state.items)
            if (i.name == item && (i.state == 1 || i.state == 2)) {
                slot = i.slot;
                break;
            }
        if (slot < 0) {
            goal.failed = true;
            return none();
        }
        if (_state.itemPage != slot / 16)
            return arrowOn(1);
        const int box = slot % 16;
        return click(wx + 38 + 112 * (box % 4) + 48, kTop + 54 + 112 * (box / 4) + 48);
    };
    switch (goal.kind) {
    case HandGoal::Kind::kClick: {
        // Nothing in hand and the window away, then the click where the
        // game's pick finds it.
        if (_ui.open || !_state.held.empty())
            return rightClick(_lastMouseX, _lastMouseY);
        int x;
        int y;
        if (!pointFor(goal.target, x, y)) {
            goal.failed = true;
            return none();
        }
        goal.done = true;
        return click(x, y);
    }
    case HandGoal::Kind::kUse: {
        const std::string &item = goal.items.at(0);
        if (_state.held != item) {
            if (!_ui.open)
                return openWindow();
            if (!windowSettled)
                return none();
            if (!_state.held.empty())
                return takeUpFromPage(_state.held); // put back in its own box
            return takeUpFromPage(item);
        }
        if (_ui.open)
            return rightClick(_lastMouseX, _lastMouseY); // put away, the item kept in hand
        int x;
        int y;
        if (!pointFor(goal.target, x, y, item)) {
            goal.failed = true;
            return none();
        }
        goal.done = true;
        return click(x, y);
    }
    case HandGoal::Kind::kTravel: {
        if (_ui.open || !_state.held.empty())
            return rightClick(_lastMouseX, _lastMouseY);
        const int x = (kBarEnds[goal.car - 1] + kBarEnds[goal.car]) / 2;
        if (!_ui.barShown || _ui.barAlpha < 255)
            return _lastMouseY < 5 ? none() : In{In::Kind::kMove, x, 2, 0}; // the bar comes down
        // A point on the bar's picture there (its clicks need one).
        const image::Image &bar = picture("interface/topmenubar01.png");
        for (int y = 30; y < bar.h; y++)
            if (bar.pixel(x, y)[3] != 0) {
                goal.done = true;
                return click(x, y);
            }
        goal.failed = true;
        return none();
    }
    case HandGoal::Kind::kCombine: {
        if (!_ui.open)
            return openWindow();
        if (!windowSettled)
            return none();
        // The parts in the four boxes, in any of them; anything else there,
        // and the last result's box, emptied by their Xs (the gears won't
        // turn with the result's box full: 0x418af0).
        std::vector<std::string> missing = goal.items;
        int stray = -1;
        int empty = -1;
        for (int i = 0; i < 4; i++) {
            const auto it = std::find(missing.begin(), missing.end(), _ui.boxes[i]);
            if (_ui.boxes[i].empty())
                empty = empty < 0 ? i : empty;
            else if (it != missing.end())
                missing.erase(it);
            else if (stray < 0)
                stray = i;
        }
        auto xOf = [&](int i) {
            return i < 4 ? click(wx + 151 - 10, kTop + 56 + 112 * i + 10) : click(wx + 454 - 10, kTop + 224 + 10);
        };
        if (stray >= 0 || !_ui.boxes[4].empty()) {
            if (_ui.page != 2)
                return _state.held.empty() ? tab(2) : takeUpFromPage(_state.held);
            return xOf(stray >= 0 ? stray : 4);
        }
        if (!missing.empty()) {
            const std::string &want = missing.front();
            if (_state.held != want) {
                if (!_state.held.empty())
                    return _ui.page == 0 ? takeUpFromPage(_state.held) : tab(0); // put back in its box
                return takeUpFromPage(want);
            }
            if (_ui.page != 2)
                return tab(2);
            if (empty < 0) {
                goal.failed = true;
                return none();
            }
            return click(wx + (56 + 151) / 2, kTop + 56 + 112 * empty + 47);
        }
        if (!_state.held.empty())
            return _ui.page == 0 ? takeUpFromPage(_state.held) : tab(0);
        if (_ui.page != 2)
            return tab(2);
        goal.done = true;
        return click(wx + 256, kTop + 271); // the gears
    }
    case HandGoal::Kind::kFlip: {
        const std::string &item = goal.items.at(0);
        if (!_state.held.empty() && !_ui.open)
            return rightClick(_lastMouseX, _lastMouseY);
        if (!_ui.open)
            return openWindow();
        if (!windowSettled)
            return none();
        if (_ui.page != 1)
            return tab(1);
        if (_ui.examined != item) {
            const std::vector<std::string> items = bagItems();
            if (std::find(items.begin(), items.end(), item) == items.end()) {
                goal.failed = true;
                return none();
            }
            return arrowOn(1);
        }
        if (_ui.back) {
            goal.done = true; // already turned over
            return none();
        }
        goal.done = true;
        return click(wx + 256, kTop + 443); // the eye
    }
    case HandGoal::Kind::kCompare: {
        if (!_ui.open)
            return openWindow();
        if (!windowSettled)
            return none();
        if (_ui.page != 5)
            return tab(5);
        std::vector<std::string> rows[2];
        for (const GameState::Item &i : _state.items)
            if (i.state == 6)
                rows[printRow(i.name)].push_back(i.name);
        for (int r = 0; r < 2; r++) {
            const auto at = std::find(rows[r].begin(), rows[r].end(), goal.items.at(size_t(r)));
            if (at == rows[r].end()) {
                goal.failed = true;
                return none();
            }
            if (_ui.printAt[r] != int(at - rows[r].begin()))
                return arrowOn(r + 1);
        }
        goal.done = true;
        return click(wx + 254, kTop + 258); // the magnifier
    }
    }
    return none();
}

std::string ScenePage::handState() const {
    std::string boxes;
    for (const std::string &b : _ui.boxes)
        boxes += (boxes.empty() ? "" : ",") + b;
    std::string items;
    for (const GameState::Item &i : _state.items)
        if (i.state == 1 || i.state == 2 || i.state == 3)
            items += (items.empty() ? "" : " ") + i.name + ":" + std::to_string(i.state) + "@" + std::to_string(i.slot);
    return std::string("window ") + (_ui.open ? "open" : "shut") + " page " + std::to_string(_ui.page) + " fade " +
           std::to_string(_ui.fade) + " itempage " + std::to_string(_state.itemPage) + " held [" + _state.held +
           "] examined [" + _ui.examined + "] boxes [" + boxes + "] canact " + (canAct() ? "1" : "0") + " bag {" +
           items + "}";
}

ScenePage::HandInput ScenePage::handWhileRunning() {
    using In = HandInput;
    auto drawNow = [&] {
        // The places a click needs (the box's rows) are worked out as it's
        // drawn.
        const bool was = std::exchange(_drawing, true);
        draw();
        _drawing = was;
    };
    if (_movie)
        return _movieName.find("legal") == std::string::npos ? In{In::Kind::kKey, 0, 0, key::kEscape} : In{};
    if (_yesNo) {
        if (!_yesNo->drawn)
            drawNow();
        if (!_yesNo->drawn)
            return In{};
        return In{In::Kind::kClick, _yesNo->x[0] + _yesNo->w[0] / 2, 394 + _yesNo->h[0] / 2, 0};
    }
    if (!_dialogue->active() || !_dialogue->ready())
        return In{};
    if (_dialogue->choosing()) {
        // The pick as autoplay's, then its row clicked.
        const int n = int(_dialogue->choices().size());
        int pick = -1;
        if (chooser) {
            std::vector<std::string> texts;
            for (const DialogueManager::Choice &c : _dialogue->choices())
                texts.push_back(c.text);
            pick = chooser(texts);
            if (pick >= n)
                pick = -1;
        }
        const bool picked = pick >= 0;
        for (int i = 0; i < n && !picked; i++)
            if (pick < 0 || _autoChoices[_dialogue->choices()[size_t(i)].text] <
                                _autoChoices[_dialogue->choices()[size_t(pick)].text])
                pick = i;
        if (pick < 0)
            return In{};
        drawNow();
        for (const DialogueManager::ChoiceArea &a : _dialogue->choiceAreas())
            if (a.choice == pick && !a.picture) {
                _autoChoices[_dialogue->choices()[size_t(pick)].text]++;
                return In{In::Kind::kClick, 200, a.y + a.h / 2, 0};
            }
        return In{};
    }
    if (cutLines && _dialogue->lineSkippable())
        return In{In::Kind::kClick, 512, 700, 0};
    return In{};
}

} // namespace reluva::awe
