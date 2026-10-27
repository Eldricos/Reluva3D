// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's saved games: the game's save format (savegame.h), its
// menus (Menu.InGame, Menu.Save, Menu.Load) and their save and load
// screens, quick saves, and restoring a save. See
// docs/formats/awe/saves.md.

#include "engines/awe/viewers.h"

#include "common/fs.h"
#include "common/text.h"
#include "engines/awe/dialogue_box.h"
#include "engines/awe/story.h"
#include "engines/awe/tables.h"

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
constexpr int kRowLeft = 703, kRowRight = 957, kRowHeight = 190;

std::string fileNameOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool readAll(const std::filesystem::path& file, std::vector<uint8_t>& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

bool writeAll(const std::filesystem::path& file, const std::vector<uint8_t>& data) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    return bool(out);
}

// A slot's files.
std::string slotName(int slot) { return "slot" + std::to_string(slot); }

} // namespace

std::filesystem::path ScenePage::savesFolder() const {
    // Reluva's own folder for the game, where the game's Saved.Games is
    // kept (the files are the game's: either can read the other's).
    return ctx_.savePath.empty() ? std::filesystem::path("Saved.Games") : ctx_.savePath / "Saved.Games";
}

std::string ScenePage::interfaceText(const std::string& tag) {
    if (interfaceTexts_.empty()) {
        std::vector<uint8_t> data;
        if (!ctx_.files.read("text/interface.txt", data)) {
            const Table t = parseTable(decodeText(data));
            const int text = t.column("Text"), translation = t.column("Translation");
            for (size_t r = 0; r < t.rows.size(); r++) {
                const std::string& tr = t.cell(r, translation);
                interfaceTexts_[fs::toLower(t.cell(r, 0))] = tr.empty() || tr == "0" ? t.cell(r, text) : tr;
            }
        }
        interfaceTexts_[""] = "";
    }
    const auto it = interfaceTexts_.find(fs::toLower(tag));
    return it == interfaceTexts_.end() ? tag : it->second;
}

SaveGame ScenePage::captureSave(const std::string& description) const {
    // 0x411230: what the game writes, from Reluva's state. Saved from a
    // menu, the game scene the menus return to is the one saved.
    SaveGame s;
    s.description = description;
    const std::time_t now = std::time(nullptr);
    if (const std::tm* tm = std::localtime(&now)) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%02d:%02d", tm->tm_hour, tm->tm_min);
        s.time = buf;
        std::snprintf(buf, sizeof buf, "%02d-%02d-%02d", tm->tm_mon + 1, tm->tm_mday, (tm->tm_year - 100) % 100);
        s.date = buf;
    }
    auto value = [&](const char* n) {
        const auto it = state_.values.find(n);
        return it == state_.values.end() ? 0 : it->second;
    };
    s.timeOfDay = value("time"), s.weather = value("weather"), s.moving = value("moving");
    // The scene: its path as the game writes it ("Locations\Salon\Salon_01_06a.ac2").
    const bool inMenu = state_.scene.rfind("menu.", 0) == 0;
    std::string path = inMenu ? gameScene_ : (names_.empty() ? std::string() : names_[size_t(index_)]);
    std::replace(path.begin(), path.end(), '/', '\\');
    s.scene = path;
    std::string file = fs::toLower(fileNameOf(path));
    {
        const std::string lower = fs::toLower(path);
        const size_t from = lower.find('\\') + 1;
        s.location = lower.substr(from, lower.find('\\', from) - from);
    }
    int major = 1, minor = 1;
    if (parseActName(state_.act, major, minor))
        s.actMajor = major, s.actMinor = minor;
    if (const ActRow* row = story_->row()) {
        s.act.preLoad = row->preLoad, s.act.location = row->location, s.act.postLoad = row->postLoad;
        s.act.completion = row->completion, s.act.event = row->event;
        s.act.music = "sound\\music\\" + row->music;
        s.act.weather = row->weather, s.act.timeOfDay = row->time, s.act.moving = row->moving;
    }
    // The player where it stands (or stood, saved from a menu); the
    // characters where they are, by scene and model.
    std::map<std::string, SaveGame::Placement> npcs = npcPositions_;
    if (!inMenu) {
        for (const Character& c : characters_) {
            SaveGame::Placement p{{c.x, c.y, c.z}, {0, c.rotY, 0}};
            if (c.player)
                s.player = p;
            else if (c.record)
                npcs[state_.scene + "|" + fs::toLower(fileNameOf(c.name))] = p;
        }
    } else if (const auto kept = positions_.find(file); kept != positions_.end()) {
        s.player = SaveGame::Placement{{kept->second[0], 0, kept->second[1]}, {0, kept->second[2], 0}};
    }
    for (const auto& [key, p] : npcs) {
        const size_t bar = key.find('|');
        s.positions.push_back({key.substr(0, bar), key.substr(bar + 1), p});
    }
    for (const auto& [name, v] : state_.tags)
        s.tags.emplace_back(name, v);
    for (const auto& [name, v] : state_.timers)
        s.timers.emplace_back(name, v);
    for (const auto& [name, v] : state_.texts)
        s.texts.emplace_back(name, v);
    for (const auto& [key, progress] : state_.talks) {
        // talkKey: "name|act|part|thread".
        SaveGame::Talk k;
        size_t a = key.find('|'), b = key.find('|', a + 1), c = key.find('|', b + 1);
        k.name = key.substr(0, a);
        k.act = key.substr(a + 1, b - a - 1);
        k.part = std::atoi(key.c_str() + b + 1);
        k.thread = std::atoi(key.c_str() + c + 1);
        k.status = progress.status;
        k.nest = progress.nest;
        s.talks.push_back(std::move(k));
    }
    int slot = 0;
    for (const std::string& item : state_.items) {
        s.items.push_back({item, 2, slot, slot / 16});
        slot++;
    }
    return s;
}

bool ScenePage::preload(const std::filesystem::path& file, bool fromGame) {
    // 0x411c60: nothing changes if the file can't be read.
    std::vector<uint8_t> data;
    if (fromGame ? bool(ctx_.files.read(file.generic_string(), data)) : !readAll(file, data)) {
        logScript("    (no saved game " + file.generic_string() + ")");
        return false;
    }
    auto save = std::make_unique<SaveGame>();
    if (Error e = readSaveGame(data, *save)) {
        logScript("    (" + file.generic_string() + ": " + e.message + ")");
        return false;
    }
    // The story state afresh (as "reset"), then the save's lists.
    state_.tags.clear();
    state_.texts.clear();
    state_.timers.clear();
    state_.items.clear();
    state_.talks.clear();
    positions_.clear();
    npcPositions_.clear();
    hiddenNames_.clear();
    for (const auto& [name, v] : save->tags)
        state_.tags[fs::toLower(name)] = v;
    for (const auto& [name, v] : save->timers)
        state_.timers.emplace_back(fs::toLower(name), v);
    for (const auto& [name, v] : save->texts)
        state_.texts[fs::toLower(name)] = v;
    for (const SaveGame::Talk& k : save->talks)
        state_.talks[talkKey(k.name, k.act, k.part, k.thread)] = TalkProgress{k.status, k.nest};
    for (const SaveGame::Item& i : save->items)
        if (i.state != 4) // 4: taken away, kept for "restore"
            state_.items.insert(fs::toLower(i.name));
    for (const SaveGame::Position& p : save->positions)
        npcPositions_[fs::toLower(p.scene) + "|" + fs::toLower(p.character)] = p.at;
    restoring_ = std::move(save);
    logScript("  (saved game \"" + restoring_->description + "\": act " +
              actName(restoring_->actMajor, restoring_->actMinor) + ", " + fileNameOf(restoring_->scene) + ")");
    return true;
}

void ScenePage::finishRestore() {
    // 0x422da0: the saved globals, the saved scene at walk point -1 with
    // the player where it was.
    SaveGame& s = *restoring_;
    state_.values["time"] = s.timeOfDay;
    state_.values["weather"] = s.weather;
    state_.values["moving"] = s.moving;
    std::string scene = fs::toLower(fileNameOf(s.scene));
    if (s.player)
        positions_[scene] = {s.player->position[0], s.player->position[2], s.player->rotation[1]};
    gameScene_.clear();
    previousMenu_.clear();
    const std::string stem = scene.size() > 4 ? scene.substr(0, scene.size() - 4) : scene;
    // Its events don't run (it's still restoring as it's entered).
    changeScene(stem, -1);
    restoring_.reset();
    dialogueAct_ = "-";
    loadDialogue();
}

void ScenePage::openMenu(const std::string& name) {
    // 0x423c00: from the game, the scene to come back to; from a menu, that
    // menu.
    const bool inMenu = state_.scene.rfind("menu.", 0) == 0;
    if (inMenu) {
        previousMenu_ = state_.scene.substr(0, state_.scene.size() - 4);
    } else {
        previousMenu_.clear();
        if (!names_.empty())
            gameScene_ = names_[size_t(index_)];
    }
    changeScene("menu." + name + "_01", 1);
}

void ScenePage::previous(bool toScene) {
    // "previous scene": back to the game; "previous": to the menu before,
    // if any (0x40a5d9).
    if (!toScene && !previousMenu_.empty()) {
        const std::string menu = previousMenu_;
        previousMenu_.clear();
        changeScene(menu, 1);
        return;
    }
    if (gameScene_.empty())
        return;
    const std::string file = fs::toLower(fileNameOf(gameScene_));
    changeScene(file.substr(0, file.size() - 4), -1);
}

bool ScenePage::quickSave() {
    const SaveGame s = captureSave("");
    if (!writeAll(savesFolder() / "quick.ac2", writeSaveGame(s)))
        return false;
    logScript("  (" + interfaceText("ingamemenu2") + ": quick.ac2)");
    return true;
}

bool ScenePage::gameKey(uint32_t key) {
    // 0x4164e0. In the save or load screen: Enter is OK, Escape Cancel,
    // the rest the typed name.
    const bool inMenu = state_.scene.rfind("menu.", 0) == 0;
    auto runSpot = [&](const char* name) {
        for (const Ac2Record* o : scene_.objects())
            if (o->tag == "SPOT")
                if (const Ac2Record* h = o->part("HOTS"); h && fs::toLower(h->text("name")) == name) {
                    if (runner_->idle())
                        runScript(*o);
                    return true;
                }
        return false;
    };
    if (optionsMenu_) {
        // Escape is Cancel; OK's key (VK_ENTER) matches nothing, so Enter
        // does nothing here (0x436139).
        if (key == key::Escape)
            runSpot("cancelover");
        return true;
    }
    if (slots_) {
        if (key == key::Return)
            return runSpot("okover");
        if (key == key::Escape)
            return runSpot("cancelover");
        slotKey(key);
        draw();
        return true;
    }
    if (key == key::Escape) {
        if (location_ == "menu.main") {
            runner_->run("yesno quit2 0");
        } else if (inMenu) {
            // The in-game menu's Resume.
            if (!runSpot("resumeover"))
                runner_->run("fade out, previous scene, fade in");
        } else {
            key = 'p';
        }
    }
    if ((key == 'p' || key == 's' || key == 'l') && !inMenu && location_.find("menu") == std::string::npos) {
        // 0x417080: the view kept for a thumbnail, then the menu.
        if (renderer_)
            grabDue_ = true;
        else
            grab_ = image::Image{kScreenW, kScreenH, ctx_.screen.pixels};
        runner_->clear();
        for (Character& c : characters_)
            if (c.player)
                c.path.clear();
        runner_->run(std::string("fade out, menu ") + (key == 'p' ? "ingame" : key == 's' ? "save" : "load") +
                     ", fade in");
        return true;
    }
    if (key == key::F1 + 1) {
        // F2: a quick save, out of the menus.
        if (location_ != "menu.main" && !inMenu)
            quickSave();
        return true;
    }
    if (key == key::F1 + 2) {
        // F3: the quick save, loaded.
        if (std::filesystem::exists(savesFolder() / "quick.ac2")) {
            runner_->clear();
            runner_->run("fade out, preload quick, load, fade in");
        }
        return true;
    }
    return key == key::Escape;
}

// --- The save and load screens ---------------------------------------------------

void ScenePage::openSlots(bool save) {
    slots_ = std::make_unique<SlotMenu>();
    SlotMenu& m = *slots_;
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
    SlotMenu& m = *slots_;
    for (int r = 0; r < 3; r++) {
        SlotMenu::Row& row = m.rows[r];
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

bool ScenePage::slotCondition(const std::string& word) const {
    // 0x435290 save, 0x424a50 load.
    if (!slots_)
        return false;
    const SlotMenu& m = *slots_;
    const int last = m.save ? m.count : m.count - 1;
    if (!m.save && m.count == 0)
        return false;
    if (word == "up")
        return m.page > 0;
    if (word == "down")
        return m.page + 2 < last;
    return !m.save; // load: "action load"
}

void ScenePage::slotAction(const std::string& word, const std::string& second) {
    SlotMenu& m = *slots_;
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
            runner_->clear();
            yesNo_ = std::make_unique<YesNo>();
            yesNo_->text = interfaceText("over");
            yesNo_->kind = 2;
            return;
        }
        writeAll(file, writeSaveGame(captureSave(m.name)));
        if (grab_.w > 0) {
            // The thumbnail: the view as the menu was opened, 256x192.
            image::Image t;
            t.w = 256, t.h = 192;
            t.rgba.resize(size_t(t.w) * t.h * 4);
            const int fx = grab_.w / t.w, fy = grab_.h / t.h;
            for (int y = 0; y < t.h; y++)
                for (int x = 0; x < t.w; x++)
                    for (int c = 0; c < 3; c++) {
                        int sum = 0;
                        for (int v = 0; v < fy; v++)
                            for (int u = 0; u < fx; u++)
                                sum += grab_.pixel(x * fx + u, y * fy + v)[c];
                        t.pixel(x, y)[c] = uint8_t(sum / (fx * fy));
                    }
            writeAll(savesFolder() / (slotName(slot) + ".bmp"), encodeBmp32(t.w, t.h, t.rgba));
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
    SlotMenu& m = *slots_;
    for (int r = 0; r < 3; r++) {
        if (x < kRowLeft || x >= kRowRight || y < kRowTop[r] || y >= kRowTop[r] + kRowHeight)
            continue;
        const int slot = m.page + r;
        if (m.save ? slot > m.count : slot >= m.count)
            return true;
        if (r == m.row && m.lastRow == r && m.sinceClick < 0.33f) {
            // A double click is OK.
            gameKey(key::Return);
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
    SlotMenu& m = *slots_;
    if (!m.save)
        return;
    if (key == key::Backspace || key == key::Delete) {
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
    const TextStyle* style = ctx_.fonts ? ctx_.fonts->style("save, load, and options screens") : nullptr;
    if (style && style->font && style->font->width(m.name, style->pixels) > 125)
        m.name.pop_back();
}

void ScenePage::drawSlots() {
    // 0x434b90, 0x4245e0: three rows: the thumbnail (dimmed unless picked),
    // the name in the middle, the date and time at the foot.
    const SlotMenu& m = *slots_;
    const TextStyle* style = ctx_.fonts ? ctx_.fonts->style("save, load, and options screens") : nullptr;
    const Color picked = ctx_.fonts ? ctx_.fonts->colour("save, load, and options selected (argb format)",
                                                         {255, 220, 130})
                                    : Color{255, 220, 130};
    const Color other = ctx_.fonts ? ctx_.fonts->colour("save, load, and options grayed out", {230, 170, 75})
                                   : Color{230, 170, 75};
    // The glow on the picked row.
    if (m.save || m.count > 0) {
        const image::Image& glow = picture(m.save ? "locations/menu.save/cutouts/glow.tga"
                                                  : "locations/menu.load/cutouts/glow.tga");
        if (glow.w)
            image::blit(ctx_.screen, glow, kRowLeft, kRowTop[m.row]);
    }
    for (int r = 0; r < 3; r++) {
        const SlotMenu::Row& row = m.rows[r];
        const int top = kRowTop[r];
        const bool on = r == m.row;
        if (row.thumbnail.w) {
            image::Image t = row.thumbnail;
            if (!on)
                for (size_t i = 3; i < t.rgba.size(); i += 4)
                    t.rgba[i] = 92;
            image::blit(ctx_.screen, t, 704, top + 1);
        }
        if (!style || !style->font)
            continue;
        const int h = style->font->lineHeight(style->pixels);
        std::string main;
        Color colour = other;
        if (m.save && on) {
            main = m.name + "|";
            colour = picked;
        } else if (!row.exists) {
            main = interfaceText("empty");
        } else {
            main = row.description;
            colour = on && !m.save ? picked : other;
        }
        const int w = style->font->width(main, style->pixels);
        ctx_.fonts->draw(ctx_.screen, *style, 624 - w / 2, top + (kRowHeight - h) / 2, main, colour);
        if (row.exists) {
            ctx_.fonts->draw(ctx_.screen, *style, 708, top + kRowHeight - h, row.date, on ? picked : other);
            ctx_.fonts->draw(ctx_.screen, *style, 952 - style->font->width(row.time, style->pixels),
                             top + kRowHeight - h, row.time, on ? picked : other);
        }
    }
}

void ScenePage::answer(bool yes) {
    // 0x432cb1: 0 quits the program, 1 the game, 2 overwrites the save.
    const int kind = yesNo_->kind;
    yesNo_.reset();
    draw();
    if (!yes)
        return;
    runner_->clear();
    if (kind == 0)
        runner_->run("fade out, quit");
    else if (kind == 1)
        runner_->run("fade out, reset, scene menu.main_01, fade in");
    else
        runner_->run("action save confirmed, fade out, previous, fade in");
}

} // namespace reluva::awe
