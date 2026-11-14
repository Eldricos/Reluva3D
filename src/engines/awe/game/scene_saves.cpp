// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's saved games: the game's save format (savegame.h), its
// menus (Menu.InGame, Menu.Save, Menu.Load) and their save and load
// screens, quick saves, and restoring a save. See
// docs/engines/awe/games/motoe/systems/saves.md.

#include "engines/awe/game/scene_page.h"

#include "common/fs.h"
#include "common/text.h"
#include "engines/awe/ui/dialogue_box.h"
#include "engines/awe/game/story.h"
#include "engines/awe/formats/tables.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace reluva::awe {

namespace {

// The rows' tops on the save and load screens (0x490b98), and each row's
// box: x 703..957, 190 high.
constexpr int kRowTop[3] = {63, 287, 511};
constexpr int kRowLeft = 703;
constexpr int kRowRight = 957;
constexpr int kRowHeight = 190;

std::string fileNameOf(const std::string &path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool readAll(const std::filesystem::path &file, std::vector<uint8_t> &out) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

bool writeAll(const std::filesystem::path &file, const std::vector<uint8_t> &data) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
    return bool(out);
}

// A slot's files.
std::string slotName(int slot) {
    return "slot" + std::to_string(slot);
}

} // namespace

std::filesystem::path ScenePage::savesFolder() const {
    // Reluva's own folder for the game, where the game's Saved.Games is
    // kept (the files are the game's: either can read the other's).
    return _ctx.savePath.empty() ? std::filesystem::path("Saved.Games") : _ctx.savePath / "Saved.Games";
}

std::string ScenePage::interfaceText(const std::string &tag) {
    if (_interfaceTexts.empty()) {
        std::vector<uint8_t> data;
        if (!_ctx.files.read("text/interface.txt", data)) {
            const Table t = parseTable(decodeText(data));
            const int text = t.column("Text");
            const int translation = t.column("Translation");
            for (size_t r = 0; r < t.rows.size(); r++) {
                const std::string &tr = t.cell(r, translation);
                _interfaceTexts[fs::toLower(t.cell(r, 0))] = tr.empty() || tr == "0" ? t.cell(r, text) : tr;
            }
        }
        _interfaceTexts[""] = "";
    }
    const auto it = _interfaceTexts.find(fs::toLower(tag));
    return it == _interfaceTexts.end() ? tag : it->second;
}

SaveGame ScenePage::captureSave(const std::string &description) const {
    // 0x411230: what the game writes, from Reluva's state. Saved from a
    // menu, the game scene the menus return to is the one saved.
    SaveGame s;
    s.description = description;
    const std::time_t now = std::time(nullptr);
    if (const std::tm *tm = std::localtime(&now)) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%02d:%02d", tm->tm_hour, tm->tm_min);
        s.time = buf;
        std::snprintf(buf, sizeof buf, "%02d-%02d-%02d", tm->tm_mon + 1, tm->tm_mday, (tm->tm_year - 100) % 100);
        s.date = buf;
    }
    auto value = [&](const char *n) {
        const auto it = _state.values.find(n);
        return it == _state.values.end() ? 0 : it->second;
    };
    s.timeOfDay = value("time"), s.weather = value("weather"), s.moving = value("moving");
    // The scene: its path as the game writes it ("Locations\Salon\Salon_01_06a.ac2").
    const bool inMenu = _state.scene.rfind("menu.", 0) == 0;
    std::string path = inMenu ? _gameScene : (_names.empty() ? std::string() : _names[size_t(_index)]);
    std::replace(path.begin(), path.end(), '/', '\\');
    s.scene = path;
    std::string file = fs::toLower(fileNameOf(path));
    {
        const std::string lower = fs::toLower(path);
        const size_t from = lower.find('\\') + 1;
        s.location = lower.substr(from, lower.find('\\', from) - from);
    }
    int major = 1;
    int minor = 1;
    if (parseActName(_state.act, major, minor))
        s.actMajor = major, s.actMinor = minor;
    if (const ActRow *row = _story->row()) {
        s.act.preLoad = row->preLoad, s.act.location = row->location, s.act.postLoad = row->postLoad;
        s.act.completion = row->completion, s.act.event = row->event;
        s.act.music = "sound\\music\\" + row->music;
        s.act.weather = row->weather, s.act.timeOfDay = row->time, s.act.moving = row->moving;
    }
    // The player where it stands (or stood, saved from a menu); the
    // characters where they are, by scene and model.
    std::map<std::string, SaveGame::Placement> npcs = _npcPositions;
    if (!inMenu) {
        for (const Character &c : _characters) {
            SaveGame::Placement p{{c.x, c.y, c.z}, {0, c.rotY, 0}};
            if (c.player)
                s.player = p;
            else if (c.record)
                npcs[_state.scene + "|" + fs::toLower(fileNameOf(c.name))] = p;
        }
    } else if (const auto kept = _positions.find(file); kept != _positions.end()) {
        s.player = SaveGame::Placement{{kept->second[0], 0, kept->second[1]}, {0, kept->second[2], 0}};
    }
    for (const auto &[key, p] : npcs) {
        const size_t bar = key.find('|');
        s.positions.push_back({key.substr(0, bar), key.substr(bar + 1), p});
    }
    for (const auto &[name, v] : _state.tags)
        s.tags.emplace_back(name, v);
    for (const auto &[name, v] : _state.timers)
        s.timers.emplace_back(name, v);
    for (const auto &[name, v] : _state.texts)
        s.texts.emplace_back(name, v);
    for (const auto &[key, progress] : _state.talks) {
        // talkKey: "name|act|part|thread".
        SaveGame::Talk k;
        size_t a = key.find('|');
        size_t b = key.find('|', a + 1);
        size_t c = key.find('|', b + 1);
        k.name = key.substr(0, a);
        k.act = key.substr(a + 1, b - a - 1);
        k.part = std::atoi(key.c_str() + b + 1);
        k.thread = std::atoi(key.c_str() + c + 1);
        k.status = progress.status;
        k.nest = progress.nest;
        s.talks.push_back(std::move(k));
    }
    // Each item with its slot and page (0x41c730): passports at 0, 0,
    // fingerprint cards on page 0 in their row's slot.
    s.itemPage = _state.itemPage;
    for (const GameState::Item &i : _state.items) {
        const int slot = i.state == 6 ? printRow(i.name) : i.state == 5 ? 0 : i.slot;
        s.items.push_back({i.name, i.state, slot, i.state == 5 || i.state == 6 ? 0 : slot / 16});
    }
    return s;
}

bool ScenePage::preload(const std::filesystem::path &file, bool fromGame) {
    // 0x411c60: nothing changes if the file can't be read.
    std::vector<uint8_t> data;
    if (fromGame ? bool(_ctx.files.read(file.generic_string(), data)) : !readAll(file, data)) {
        logScript("    (no saved game " + file.generic_string() + ")");
        return false;
    }
    return preloadData(data, file.generic_string());
}

bool ScenePage::preloadData(const std::vector<uint8_t> &data, const std::string &what) {
    auto save = std::make_unique<SaveGame>();
    if (Error e = readSaveGame(data, *save)) {
        logScript("    (" + what + ": " + e.message + ")");
        return false;
    }
    // The story state afresh (as "reset"), then the save's lists.
    _state.tags.clear();
    _state.texts.clear();
    _state.timers.clear();
    _state.items.clear();
    _state.talks.clear();
    _positions.clear();
    _npcPositions.clear();
    _keptCharacters.clear();
    _boxOpenedHere = false;
    for (const auto &[name, v] : save->tags)
        _state.tags[fs::toLower(name)] = v;
    for (const auto &[name, v] : save->timers)
        _state.timers.emplace_back(fs::toLower(name), v);
    for (const auto &[name, v] : save->texts)
        _state.texts[fs::toLower(name)] = v;
    for (const SaveGame::Talk &k : save->talks)
        _state.talks[talkKey(k.name, k.act, k.part, k.thread)] = TalkProgress{k.status, k.nest};
    _state.held.clear();
    for (const SaveGame::Item &i : save->items)
        // Each as saved, in its slot (one in hand back in the bag).
        addItem(_state, i.name, i.state == 3 ? 2 : i.state, i.state == 5 || i.state == 6 ? -1 : i.slot);
    _state.itemPage = std::max(0, save->itemPage);
    _state.fresh.clear();
    for (const SaveGame::Position &p : save->positions)
        _npcPositions[fs::toLower(p.scene) + "|" + fs::toLower(p.character)] = p.at;
    _restoring = std::move(save);
    logScript("  (saved game \"" + _restoring->description + "\": act " +
              actName(_restoring->actMajor, _restoring->actMinor) + ", " + fileNameOf(_restoring->scene) + ")");
    return true;
}

void ScenePage::finishRestore() {
    // 0x422da0: the saved globals, the saved scene at walk point -1 with
    // the player where it was.
    SaveGame &s = *_restoring;
    _state.values["time"] = s.timeOfDay;
    _state.values["weather"] = s.weather;
    _state.values["moving"] = s.moving;
    std::string scene = fs::toLower(fileNameOf(s.scene));
    if (s.player)
        _positions[scene] = {s.player->position[0], s.player->position[2], s.player->rotation[1]};
    _gameScene.clear();
    _previousMenu.clear();
    const std::string stem = scene.size() > 4 ? scene.substr(0, scene.size() - 4) : scene;
    // Its events don't run (it's still restoring as it's entered).
    changeScene(stem, -1);
    _restoring.reset();
    _dialogueAct = "-";
    loadDialogue();
}

void ScenePage::openMenu(const std::string &name) {
    // 0x423c00: from the game, the scene to come back to; from a menu, that
    // menu.
    const bool inMenu = _state.scene.rfind("menu.", 0) == 0;
    if (inMenu) {
        _previousMenu = _state.scene.substr(0, _state.scene.size() - 4);
    } else {
        _previousMenu.clear();
        if (!_names.empty())
            _gameScene = _names[size_t(_index)];
    }
    changeScene("menu." + name + "_01", 1);
}

void ScenePage::previous(bool toScene) {
    // "previous scene": back to the game; "previous": to the menu before,
    // if any (0x40a5d9).
    if (!toScene && !_previousMenu.empty()) {
        const std::string menu = _previousMenu;
        _previousMenu.clear();
        changeScene(menu, 1);
        return;
    }
    if (_gameScene.empty())
        return;
    const std::string file = fs::toLower(fileNameOf(_gameScene));
    changeScene(file.substr(0, file.size() - 4), -1);
}

bool ScenePage::quickSave() {
    const SaveGame s = captureSave("");
    if (!writeAll(savesFolder() / "quick.ac2", writeSaveGame(s)))
        return false;
    // 0x4168c0: the message for 1.5 s; its text, once set, stays.
    _messageTime = 1.5f;
    if (_message.empty())
        _message = interfaceText("ingamemenu2");
    logScript("  (" + _message + ": quick.ac2)");
    draw();
    return true;
}

void ScenePage::drawMessage() {
    // 0x415511: the placard centred, its foot at y 766, the text in the
    // middle of it 2 pixels down, in the "location descriptions" font and
    // the "navigation text" colour (font 0, colour 3).
    const image::Image &placard = picture("interface/placard.tga");
    const int top = 766 - placard.h;
    if (placard.w)
        image::blitOver(_ctx.screen, placard, (kScreenW - placard.w) / 2, top);
    const TextStyle *style = _ctx.fonts ? _ctx.fonts->style("location descriptions") : nullptr;
    if (!style || !style->font)
        return;
    const int w = style->font->width(_message, style->pixels);
    const int h = style->font->lineHeight(style->pixels);
    _ctx.fonts->draw(_ctx.screen, *style, (kScreenW - w) / 2, (placard.h - h) / 2 + top + 2, _message,
                     _ctx.fonts->colour("navigation text", {230, 170, 75}));
}

bool ScenePage::gameKey(uint32_t key) {
    // 0x4164e0. Escape and Enter first press the hotspot that has them
    // for its key (0x436110): the menus' Resume, OK and Cancel (the
    // options screen's OK has VK_ENTER, which matches nothing, so Enter
    // does nothing there). In the save or load screen the rest are the
    // typed name.
    const bool inMenu = _state.scene.rfind("menu.", 0) == 0;
    if (hotspotKey(key))
        return true;
    if (_optionsMenu)
        return true;
    if (_slots) {
        slotKey(key);
        draw();
        return true;
    }
    if (key == key::kEscape) {
        if (_location == "menu.main") {
            _runner->run("yesno quit2 0");
        } else if (inMenu) {
            // A menu without a hotspot for it: back to the game.
            _runner->run("fade out, previous scene, fade in");
        } else {
            key = 'p';
        }
    }
    if ((key == 'p' || key == 's' || key == 'l') && !inMenu && _location.find("menu") == std::string::npos) {
        // 0x417080: the item in hand put back, the view kept for a
        // thumbnail, then the menu.
        releaseHeld();
        endFresh();
        if (_renderer)
            _grabDue = true;
        else
            _grab = image::Image{kScreenW, kScreenH, _ctx.screen.pixels};
        _runner->clear();
        for (Character &c : _characters)
            if (c.player)
                c.path.clear();
        _runner->run(std::string("fade out, menu ") +
                     (key == 'p'   ? "ingame"
                      : key == 's' ? "save"
                                   : "load") +
                     ", fade in");
        return true;
    }
    if (key == key::kF1 + 1) {
        // F2: a quick save, out of the menus.
        if (_location != "menu.main" && !inMenu)
            quickSave();
        return true;
    }
    if (key == key::kF1 + 2) {
        // F3: the quick save, loaded.
        if (std::filesystem::exists(savesFolder() / "quick.ac2")) {
            _runner->clear();
            _runner->run("fade out, preload quick, load, fade in");
        }
        return true;
    }
    return key == key::kEscape;
}

// --- The save and load screens ---------------------------------------------------

void ScenePage::openSlots(bool save) {
    _slots = std::make_unique<SlotMenu>();
    SlotMenu &m = *_slots;
    m.save = save;
    // Slots run from 0 with no gaps.
    while (std::filesystem::exists(savesFolder() / (slotName(m.count) + ".ac2")))
        m.count++;
    // Save: on a new slot; load: on the newest.
    const int at = save ? m.count : std::max(0, m.count - 1);
    m.page = at - at % 3;
    m.row = at % 3;
    refreshSlots();
}

void ScenePage::refreshSlots() {
    SlotMenu &m = *_slots;
    for (int r = 0; r < 3; r++) {
        SlotMenu::Row &row = m.rows[r];
        row = SlotMenu::Row();
        const int slot = m.page + r;
        std::vector<uint8_t> data;
        if (slot < m.count && readAll(savesFolder() / (slotName(slot) + ".ac2"), data) &&
            !readSaveHeader(data, row.description, row.date, row.time)) {
            row.exists = true;
            std::vector<uint8_t> bmp;
            if (readAll(savesFolder() / (slotName(slot) + ".bmp"), bmp))
                image::decode(bmp, row.thumbnail);
        }
    }
    // The name: the picked slot's, or none for a new one.
    if (m.save)
        m.name = m.rows[m.row].exists ? m.rows[m.row].description : std::string();
}

bool ScenePage::slotCondition(const std::string &word) const {
    // 0x435290 save, 0x424a50 load.
    if (!_slots)
        return false;
    const SlotMenu &m = *_slots;
    const int last = m.save ? m.count : m.count - 1;
    if (!m.save && m.count == 0)
        return false;
    if (word == "up")
        return m.page > 0;
    if (word == "down")
        return m.page + 2 < last;
    return !m.save; // load: "action load"
}

void ScenePage::slotAction(const std::string &word, const std::string &second) {
    SlotMenu &m = *_slots;
    const int last = m.save ? m.count : m.count - 1;
    if (word == "up" && m.page > 0) {
        m.page -= 3, m.row = 2;
    } else if (word == "down") {
        m.page += 3;
        m.row = 0;
        if (m.page < last) {
            m.row = 1;
            if (m.page + 1 < last)
                m.row = 2;
        }
    } else if (word == "save" && m.save) {
        const int slot = std::min(m.page + m.row, m.count);
        const std::filesystem::path file = savesFolder() / (slotName(slot) + ".ac2");
        if (second != "confirmed" && std::filesystem::exists(file)) {
            // Over an old one: "Overwrite?", the rest of OK's script dropped.
            _runner->clear();
            _yesNo = std::make_unique<YesNo>();
            _yesNo->text = interfaceText("over");
            _yesNo->kind = 2;
            return;
        }
        writeAll(file, writeSaveGame(captureSave(m.name)));
        if (_grab.w > 0) {
            // The thumbnail: the view as the menu was opened, 256x192.
            image::Image t;
            t.w = 256, t.h = 192;
            t.rgba.resize(size_t(t.w) * t.h * 4);
            const int fx = _grab.w / t.w;
            const int fy = _grab.h / t.h;
            for (int y = 0; y < t.h; y++)
                for (int x = 0; x < t.w; x++)
                    for (int c = 0; c < 3; c++) {
                        int sum = 0;
                        for (int v = 0; v < fy; v++)
                            for (int u = 0; u < fx; u++)
                                sum += _grab.pixel(x * fx + u, y * fy + v)[c];
                        t.pixel(x, y)[c] = uint8_t(sum / (fx * fy));
                    }
            writeAll(savesFolder() / (slotName(slot) + ".bmp"), encodeBmp24(t.w, t.h, t.rgba));
        }
        logScript("  (saved " + slotName(slot) + ": \"" + m.name + "\")");
        return;
    } else if (word == "load" && !m.save) {
        preload(savesFolder() / (slotName(m.page + m.row) + ".ac2"), false);
        return;
    } else {
        return;
    }
    refreshSlots();
}

bool ScenePage::slotClick(int x, int y) {
    SlotMenu &m = *_slots;
    for (int r = 0; r < 3; r++) {
        if (x < kRowLeft || x >= kRowRight || y < kRowTop[r] || y >= kRowTop[r] + kRowHeight)
            continue;
        const int slot = m.page + r;
        if (m.save ? slot > m.count : slot >= m.count)
            return true;
        if (r == m.row && m.lastRow == r && m.sinceClick < 0.33f) {
            // A double click is OK.
            gameKey(key::kReturn);
            return true;
        }
        if (r != m.row) {
            m.row = r;
            if (m.save)
                m.name = m.rows[r].exists ? m.rows[r].description : std::string();
        }
        m.lastRow = r;
        m.sinceClick = 0;
        draw();
        return true;
    }
    return false;
}

void ScenePage::slotKey(uint32_t key) {
    // 0x434ee0: A-Z (as capitals), digits and space, up to 31 and 125
    // pixels; Backspace or Delete take the last off.
    SlotMenu &m = *_slots;
    if (!m.save)
        return;
    if (key == key::kBackspace || key == key::kDelete) {
        if (!m.name.empty())
            m.name.pop_back();
        return;
    }
    char c = 0;
    if (key < 128 && std::isalpha(int(key)))
        c = char(std::toupper(int(key)));
    else if (key < 128 && (std::isdigit(int(key)) || key == ' '))
        c = char(key);
    if (!c || m.name.size() >= 31)
        return;
    m.name += c;
    const TextStyle *style = _ctx.fonts ? _ctx.fonts->style("save, load, and options screens") : nullptr;
    if (style && style->font && style->font->width(m.name, style->pixels) > 125)
        m.name.pop_back();
}

void ScenePage::drawSlots() {
    // 0x434b90, 0x4245e0: three rows: the thumbnail (dimmed unless picked),
    // the name in the middle, the date and time at the foot.
    const SlotMenu &m = *_slots;
    const TextStyle *style = _ctx.fonts ? _ctx.fonts->style("save, load, and options screens") : nullptr;
    const Color picked = _ctx.fonts
                             ? _ctx.fonts->colour("save, load, and options selected (argb format)", {255, 220, 130})
                             : Color{255, 220, 130};
    const Color other =
        _ctx.fonts ? _ctx.fonts->colour("save, load, and options grayed out", {230, 170, 75}) : Color{230, 170, 75};
    // The glow on the picked row.
    if (m.save || m.count > 0) {
        const image::Image &glow =
            picture(m.save ? "locations/menu.save/cutouts/glow.tga" : "locations/menu.load/cutouts/glow.tga");
        if (glow.w)
            image::blitOver(_ctx.screen, glow, kRowLeft, kRowTop[m.row]);
    }
    for (int r = 0; r < 3; r++) {
        const SlotMenu::Row &row = m.rows[r];
        const int top = kRowTop[r];
        const bool on = r == m.row;
        if (row.thumbnail.w) {
            image::Image t = row.thumbnail;
            if (!on)
                for (size_t i = 3; i < t.rgba.size(); i += 4)
                    t.rgba[i] = 92;
            image::blitOver(_ctx.screen, t, 704, top + 1);
        }
        if (!style || !style->font)
            continue;
        const int h = style->font->lineHeight(style->pixels);
        std::string main;
        Color colour = other;
        if (m.save && on) {
            main = m.name;
            colour = picked;
        } else if (!row.exists) {
            main = interfaceText("empty");
        } else {
            main = row.description;
            colour = on && !m.save ? picked : other;
        }
        const int w = style->font->width(main, style->pixels);
        _ctx.fonts->draw(_ctx.screen, *style, 624 - w / 2, top + (kRowHeight - h) / 2, main, colour);
        // The caret just after the name, while the blink has it on
        // (0x434c91).
        if (m.save && on && _caretOn)
            _ctx.fonts->draw(_ctx.screen, *style, 624 + w - w / 2, top + (kRowHeight - h) / 2, "|", colour);
        if (row.exists) {
            _ctx.fonts->draw(_ctx.screen, *style, 708, top + kRowHeight - h, row.date, on ? picked : other);
            _ctx.fonts->draw(_ctx.screen, *style, 952 - style->font->width(row.time, style->pixels),
                             top + kRowHeight - h, row.time, on ? picked : other);
        }
    }
}

void ScenePage::drawYesNo() {
    // 0x432630, 0x432d60: interface\mainmenuwin.tga centred on the
    // screen; the question in the popup font (font 8) and the popup text
    // colour (11), centred, its foot at y 364; Yes and No below at y 394,
    // in the highlighted colour (12) when the mouse is over them. With m
    // the wider of the two, Yes is centred in m pixels ending at x 482,
    // No in m pixels from x 542.
    const image::Image &box = picture("interface/mainmenuwin.tga");
    if (box.w)
        image::blitOver(_ctx.screen, box, (kScreenW - box.w) / 2, (kScreenH - box.h) / 2);
    const TextStyle *style = _ctx.fonts->style("popup dialogs");
    if (!style || !style->font)
        return;
    const Color plain = _ctx.fonts->colour("popup menu text", {255, 230, 230});
    const Color lit = _ctx.fonts->colour("highlighted popup menu text", {255, 255, 255});
    const int lineH = style->font->lineHeight(style->pixels);
    const int qw = style->font->width(_yesNo->text, style->pixels);
    _ctx.fonts->draw(_ctx.screen, *style, (kScreenW - qw) / 2, 364 - lineH, _yesNo->text, plain);
    const std::string words[2] = {interfaceText("yes"), interfaceText("no")};
    for (int i = 0; i < 2; i++)
        _yesNo->w[i] = style->font->width(words[i], style->pixels), _yesNo->h[i] = lineH;
    const int m = std::max(_yesNo->w[0], _yesNo->w[1]);
    _yesNo->x[0] = (m - _yesNo->w[0]) / 2 - m + 482;
    _yesNo->x[1] = (m - _yesNo->w[1]) / 2 + 542;
    _yesNo->drawn = true;
    for (int i = 0; i < 2; i++)
        _ctx.fonts->draw(_ctx.screen, *style, _yesNo->x[i], 394, words[i], _yesNo->highlighted == i ? lit : plain);
}

int ScenePage::yesNoAt(int x, int y) const {
    // Each word's own box, edges included, from y 394 (0x4330c0).
    if (!_yesNo || !_yesNo->drawn)
        return -1;
    for (int i = 0; i < 2; i++)
        if (x >= _yesNo->x[i] && x <= _yesNo->x[i] + _yesNo->w[i] && y >= 394 && y <= 394 + _yesNo->h[i])
            return i;
    return -1;
}

void ScenePage::answer(bool yes) {
    // 0x432cb1: 0 quits the program, 1 the game, 2 overwrites the save.
    const int kind = _yesNo->kind;
    _yesNo.reset();
    draw();
    if (!yes)
        return;
    _runner->clear();
    if (kind == 0)
        _runner->run("fade out, quit");
    else if (kind == 1)
        _runner->run("fade out, reset, scene menu.main_01, fade in");
    else
        _runner->run("action save confirmed, fade out, previous, fade in");
}

} // namespace reluva::awe
