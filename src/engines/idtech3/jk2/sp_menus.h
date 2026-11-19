// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/sp_menu_host.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/jk2/font_set.h"
#include "engines/idtech3/jk2/menu_defs.h"
#include "engines/idtech3/raven/strings.h"

#include "common/archive.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace reluva::idtech3::jk2 {

/**
 * Jedi Outcast's single-player menus, which its engine runs itself from
 * the menu scripts in the paks (`ui/menus.txt` and the files it names):
 * the menus and their items drawn, their scripts run, keys and the mouse
 * handled (docs/engines/idtech3/games/jk2/systems/sp-menus.md).
 */
class SpMenus : public SpMenuHost {
public:
    // What jk2sp.exe numbers its windows' styles, items' kinds and text
    // alignment by (their names' tables from 0x4eabc4).
    enum Style : int32_t { kStyleEmpty, kStyleFilled, kStyleGradient, kStyleShader, kStyleTeamColor, kStyleCinematic };
    enum Type : int32_t {
        kText,
        kButton,
        kRadioButton,
        kCheckBox,
        kEditField,
        kCombo,
        kListBox,
        kModel,
        kOwnerDraw,
        kNumericField,
        kSlider,
        kYesNo,
        kMulti,
        kBind,
    };
    enum Align : int32_t { kLeft, kCenter, kRight };

    SpMenus(const SearchSet &files, CvarStore &cvars);
    ~SpMenus() override;

    /** Reads `ui/menus.txt`, the menus it names and their assets. */
    Error load();
    /** The menus read, by name in the order read. */
    std::vector<std::string> menuNames() const;
    /** The names of the menus open, in the order read. */
    std::vector<std::string> openMenus() const;
    /** The item with the focus in the menu with the focus; "" if none. */
    std::string focusedItem() const;
    /** Where the cursor is, in the 640 x 480 screen. */
    std::array<float, 2> cursor() const { return {_cursorX, _cursorY}; }

    Error keyEvent(int32_t key) override;
    Error mouseEvent(int32_t dx, int32_t dy) override;
    Error refresh(int32_t time) override;
    bool fullscreen() override;
    Error drawLoadingScreen(const std::string &server, const std::string &info) override;
    Error drawConnectText(const std::string &server, const std::string &info) override;
    Error setActiveMenu(const std::string &name) override;
    Error consoleCommand(const std::string &text, bool *handled) override;
    void setCapture(const std::vector<uint8_t> &rgba) override;
    std::vector<uint8_t> capture() const override;

private:
    using Color = std::array<float, 4>;
    using Rect = std::array<float, 4>; // x, y, width, height
    using Tokens = std::vector<std::string>;

    // What menus and items share.
    struct Window {
        std::string name;
        std::string group;
        Rect rect{};
        int32_t style = kStyleEmpty;
        int32_t border = 0;
        float borderSize = 1;
        Color foreColor{1, 1, 1, 1};
        Color backColor{0, 0, 0, 0};
        Color borderColor{0, 0, 0, 0};
        bool foreColorSet = false;
        int32_t background = 0; // shader handle
        bool visible = false;
        bool decoration = false;
    };
    struct Item : Window {
        int32_t type = kText;
        std::string text;
        std::string descText;
        int32_t textAlign = kLeft;
        float textAlignX = 0;
        float textAlignY = 0;
        float textScale = 0.55f;
        int32_t textStyle = 0;
        int32_t font = 0;
        std::string cvar;
        // A multi's choices: text and value (a number's as written).
        std::vector<std::pair<std::string, std::string>> choices;
        bool numberChoices = false;
        // A slider's: its default, least and most.
        float sliderDefault = 0;
        float sliderMin = 0;
        float sliderMax = 1;
        Tokens action;
        Tokens mouseEnter;
        Tokens mouseExit;
        Tokens onFocus;
        Tokens leaveFocus;
        // Shown, hidden, enabled or disabled as a setting's value is or
        // isn't among these.
        std::string cvarTest;
        Tokens showCvar;
        Tokens hideCvar;
        Tokens enableCvar;
        Tokens disableCvar;
        bool mouseOver = false;
        bool focus = false;
    };
    struct Menu : Window {
        bool fullscreen = false;
        Color focusColor{1, 1, 1, 1};
        Color disableColor{0.5f, 0.5f, 0.5f, 1};
        float descX = 0;
        float descY = 0;
        float descScale = 1;
        Color descColor{1, 1, 1, 1};
        int32_t descAlign = kLeft;
        Tokens onOpen;
        Tokens onClose;
        Tokens onEsc;
        std::vector<Item> items;
        bool focus = false;
    };

    void readAssets(const MenuEntries &entries);
    void readMenu(const MenuDef &def);
    Item readItem(const MenuItemDef &def);
    void readWindow(const MenuEntries &entries, Window &w);
    std::string text(std::string_view text) const;

    Menu *findMenu(std::string_view name);
    Menu *focusedMenu();
    void open(std::string_view name);
    void close(Menu &m);
    void closeAll();
    void runScript(Menu &menu, const Tokens &script);
    bool runUiScript(Menu &menu, const Tokens &script, size_t &at);
    void setFocus(Menu &menu, Item &item);
    bool shown(const Item &item) const;
    bool enabled(const Item &item) const;
    bool canFocus(const Item &item) const;
    void mouseMove(Menu &menu);
    void moveFocus(Menu &menu, int step);
    void itemKey(Menu &menu, Item &item, int32_t key);
    void startGame();

    void drawMenu(const Menu &menu);
    void drawItem(const Menu &menu, const Item &item);
    void drawWindow(const Window &w);
    void drawRect(const Rect &r, const Color &c, int32_t shader);
    void drawText(float x, float y, float scale, const Color &c, const std::string &s, int32_t style, int32_t font);
    int32_t fontHandle(int32_t font) const;
    float textWidth(const std::string &s, float scale, int32_t font) const;
    std::string itemValue(const Item &item) const;

    const SearchSet &_files;
    CvarStore &_cvars;
    raven::StringTable _strings;
    std::unique_ptr<FontSet> _fonts;
    std::vector<Menu> _menus;
    std::vector<size_t> _openStack; // menus that had the focus, by index
    // From assetGlobalDef.
    std::array<int32_t, 3> _fontHandles{}; // small, medium, big
    int32_t _cursorShader = 0;
    int32_t _white = 0;
    int32_t _sliderBar = 0;
    int32_t _sliderThumb = 0;
    std::string _focusSound;
    float _cursorX = 320;
    float _cursorY = 240;
    int32_t _time = 0;
    std::vector<uint8_t> _capture;
};

} // namespace reluva::idtech3::jk2
