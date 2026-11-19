// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Options menus and the main menu's dialogs: tune/widget.csv's Main
// Options Menu, Audio, Graphics and Control Options, Control Custom,
// AboutMenu Menu and the dialogs, on the game's jpg/ backgrounds with its
// texture/ buttons. Which picture is which, and what each control does,
// is Reluva's reading of them; see docs/engines/age/games/mm2/systems/menus.md.

#include "engines/age/ui/game_menus.h"

#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

// Where the dialogs go (tune/menu.csv's places for dialogs of their
// size): the 300 x 225 ones, and the 400 x 76 message boxes.
constexpr int kSmallX = 170;
constexpr int kSmallY = 127;
constexpr int kBoxX = 120;
constexpr int kBoxY = 202;
constexpr int kDialogY = 75;
// the replay list's (in jpg/rpl_bk's box)
constexpr int kReplayRow = 14;
constexpr int kReplayRows = 11;
// The bars' grid on the option screens' backgrounds, within their
// widget's box (measured on jpg/aud_bk, gfx_bk, ctrl_bk).
constexpr int kBarInset = 23;
constexpr int kBarWidth = 137;
// Customize Controls' list: its rows and columns (on jpg/cuss_bk's box).
constexpr int kListRow = 18;
constexpr int kKeyColumn = 160;
constexpr int kSecondColumn = 230;
constexpr int kListShown = 19; // controls shown at once from y 62 (the list scrolls)
// The About screen's credits scroll by at this many pixels a second.
constexpr float kCreditsSpeed = 30;

void fill(Surface &o, int x, int y, int w, int h, Color c) {
    for (int yy = std::max(0, y); yy < std::min(o.h, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(o.w, x + w); ++xx) {
            uint8_t *p = o.row(yy) + xx * 4;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
}

// A band of a tall picture (rows from `top`, wrapping round), drawn at
// x, y.
void drawBand(Surface &o, const image::Image &im, int top, int x, int y, int w, int h) {
    if (im.w <= 0 || im.h <= 0)
        return;
    image::Image band;
    band.w = std::min(w, im.w);
    band.h = h;
    band.rgba.resize(size_t(band.w) * size_t(h) * 4);
    for (int r = 0; r < h; ++r) {
        const int src = ((top + r) % im.h + im.h) % im.h;
        std::copy_n(im.pixel(0, src), size_t(band.w) * 4, band.rgba.begin() + std::ptrdiff_t(size_t(r) * band.w * 4));
    }
    drawPicture(o, band, x, y);
}

} // namespace

GameMenus::GameMenus(System &sys, const SearchSet &files, const GameStrings &strings, audio::Mixer *mixer,
                     std::filesystem::path savePath)
    : _sys(sys), _files(files), _strings(strings), _mixer(mixer), _savePath(std::move(savePath)), _pictures(files) {
    std::vector<uint8_t> data;
    if (!files.read("tune/widget.csv", data))
        _ok = !parseWidgets(std::string(data.begin(), data.end()), _widgets) && _pictures.jpg("opt_bk");
    // Change Race Options' time and weather: the picture's two boxes
    // (jpg/Renv_dlg; widget.csv's icons are placed for another layout).
    _widgets.push_back({"Race Environment", "TIME", 150, 61, 125, 26, ""});
    _widgets.push_back({"Race Environment", "WEATHER", 150, 95, 125, 26, ""});
    // A name used again in a menu (Create a New Driver's two "checkbox",
    // the records' "popup_checkbox"es) is numbered: "checkbox#2"...
    for (size_t i = 0; i < _widgets.size(); ++i) {
        int seen = 1;
        for (size_t j = 0; j < i; ++j)
            if (_widgets[j].menu == _widgets[i].menu &&
                _widgets[j].name.substr(0, _widgets[j].name.find('#')) == _widgets[i].name)
                ++seen;
        if (seen > 1)
            _widgets[i].name += "#" + std::to_string(seen);
    }
}

std::string GameMenus::menuName() const {
    switch (_screen) {
    case Screen::kOptions:
        return "Main Options Menu";
    case Screen::kAudio:
        return "Audio Options";
    case Screen::kGraphics:
        return "Graphics Options";
    case Screen::kControl:
        return "Control Options";
    case Screen::kCustomize:
        return "Control Custom";
    case Screen::kAbout:
        return "AboutMenu Menu";
    case Screen::kDefaults:
    case Screen::kDuplicateKey:
        return "odef_dlg";
    case Screen::kQuit:
    case Screen::kReplayAsk:
        return "quit_dlg";
    case Screen::kReplayList:
        return "Replay Selection";
    case Screen::kReplayName:
        return "Replay Description";
    case Screen::kNone:
        break;
    default:
        return driverMenuName();
    }
    return {};
}

int GameMenus::originX() const {
    if (driverScreen(_screen)) {
        int x = 0;
        int y = 0;
        driverOrigin(x, y);
        return x;
    }
    switch (_screen) {
    case Screen::kDefaults:
    case Screen::kDuplicateKey:
        return kSmallX;
    case Screen::kQuit:
    case Screen::kReplayAsk:
    case Screen::kReplayList:
    case Screen::kReplayName:
        return kBoxX;
    default:
        return 0;
    }
}

int GameMenus::originY() const {
    if (driverScreen(_screen)) {
        int x = 0;
        int y = 0;
        driverOrigin(x, y);
        return y;
    }
    switch (_screen) {
    case Screen::kDefaults:
    case Screen::kDuplicateKey:
        return kSmallY;
    case Screen::kQuit:
    case Screen::kReplayAsk:
        return kBoxY;
    case Screen::kReplayList:
    case Screen::kReplayName:
        return kDialogY;
    default:
        return 0;
    }
}

bool GameMenus::overlay(Screen s) const {
    return s == Screen::kQuit || s == Screen::kReplayAsk || s == Screen::kReplayList || s == Screen::kReplayName ||
           driverScreen(s);
}

bool GameMenus::typing() const {
    return _waitingForKey || _screen == Screen::kReplayName || (_screen == Screen::kNewDriver && _focus == 0);
}

void GameMenus::nameReplay(const std::string &name) {
    _name = name.substr(0, 40);
    _under = Screen::kNone;
    _screen = Screen::kNone;
    go(Screen::kReplayName);
}

void GameMenus::askToSaveReplay(const std::string &name) {
    _name = name.substr(0, 40);
    _under = Screen::kNone;
    _screen = Screen::kNone;
    go(Screen::kReplayAsk);
}

const Widget *GameMenus::widget(const std::string &name) const {
    const std::string menu = menuName();
    for (const Widget &w : _widgets)
        if (w.menu == menu && w.name == name)
            return &w;
    return nullptr;
}

std::vector<GameMenus::Item> GameMenus::items() const {
    auto button = [](const std::string &w, const std::string &pic, bool enabled = true) {
        return Item{Kind::kButton, w, pic, 4, enabled};
    };
    auto check = [](const std::string &w, bool enabled = true) { return Item{Kind::kCheck, w, w, 5, enabled}; };
    auto bar = [](const std::string &w, bool enabled = true) { return Item{Kind::kBar, w, {}, 0, enabled}; };
    auto choice = [](const std::string &w, bool enabled = true) { return Item{Kind::kChoice, w, {}, 0, enabled}; };
    const std::vector<Item> bottom = {button("opt_def", "opt_def"), button("opt_can", "opt_can"),
                                      button("opt_dn", "opt_done")};
    std::vector<Item> list;
    switch (_screen) {
    case Screen::kOptions:
        return {button("opt_aud", "opt_aud"), button("opt_ctl", "opt_ctl"), button("opt_gfx", "opt_gfx"),
                button("opt_abt", "opt_abt"), button("mnav_prev", "mnav_prv")};
    case Screen::kAudio:
        // The sound card's device, stereo and quality are the system's:
        // shown, not chosen.
        list = {check("aud_fx"),
                check("aud_com"),
                check("aud_musc"),
                check("aud_amb"),
                choice("DEVICE", false),
                choice("STEREO", false),
                choice("SOUND QUALITY", false),
                bar("WAVE VOLUME"),
                bar("CD VOLUME"),
                bar("BALANCE")};
        break;
    case Screen::kGraphics:
        // The renderer isn't Reluva's to change.
        list = {check("gfx_sky"),  check("gfx_rflx"),         check("gfx_peds"),       check("gfx_port"),
                choice("DISPLAY"), choice("RENDERER", false), choice("RESOLUTION"),    bar("far clip"),
                bar("lighting"),   choice("TEXTURE QUALITY"), choice("OBJECT DETAIL"), choice("CLOUD SHADOWS")};
        break;
    case Screen::kControl:
        // The keyboard only: no joystick to calibrate, no force feedback.
        list = {check("ctrl_aut"),
                check("ctrl_pov"),
                check("ctrl_fbk"),
                choice("CONTROLLERS", false),
                bar("Steering Sensitivity"),
                bar("Joystick Dead Zone", false),
                button("ctrl_cal", "ctrl_cal", false),
                bar("Force-Feedback Collision Intensity", false),
                bar("Force-Feedback Road Force Intensity", false),
                button("ctrl_cus", "ctrl_cus")};
        break;
    case Screen::kCustomize:
        list = {Item{Kind::kList, "CW Array", {}, 0, true}};
        break;
    case Screen::kAbout:
        return {Item{Kind::kLabel, "Credits", {}, 0, false}, button("options_done", "opt_done")};
    case Screen::kDefaults:
        return {button("dlg_ok", "dlg_ok"), button("dlg_can", "dlg_can")};
    case Screen::kDuplicateKey:
        return {button("dlg_ok", "dlg_ok")};
    case Screen::kQuit:
        return {button("popup_ok", "dlg_ok"), button("popup_cancel", "dlg_can")};
    case Screen::kReplayAsk:
        return {button("popup_ok", "dlg_yes"), button("popup_cancel", "dlg_no")};
    case Screen::kReplayList:
        return {Item{Kind::kList, "CITY", {}, 0, !_replays.empty()}, button("rpl_load", "RPL_LOAD", !_replays.empty()),
                button("rpl_edit", "rpl_edit", !_replays.empty()), button("rpl_del", "RPL_DEL", !_replays.empty()),
                button("dlg_cancel", "dlg_can")};
    case Screen::kReplayName:
        return {Item{Kind::kText, "Description", {}, 0, true}, button("dlg_cancel", "dlg_can"),
                button("dlg_done", "dlg_done")};
    case Screen::kNone:
        return {};
    default:
        return driverItems();
    }
    list.insert(list.end(), bottom.begin(), bottom.end());
    return list;
}

void GameMenus::open(Screen s) {
    _edit = currentOptions();
    _entered = _edit;
    _under = Screen::kNone;
    _openedMs = _nowMs;
    go(s);
}

void GameMenus::go(Screen s) {
    if (s == Screen::kDeleteDriver && _roster && _roster->drivers().size() <= 1)
        s = Screen::kLastDriver; // "You cannot delete your only driver"
    // The game's alert for the dialogs that say no.
    if (s == Screen::kLastDriver || s == Screen::kLocked || s == Screen::kDuplicateDriver || s == Screen::kDuplicateKey)
        _voice = "uialert";
    if (s == Screen::kNewDriver && _screen != Screen::kDuplicateDriver) {
        _newName.clear();
        _newPro = false;
    }
    if (s == Screen::kRaceOptions && _front)
        _conditions = _front->choice().conditions;
    if (s == Screen::kCitySelection && _front)
        _cityRow = size_t(std::max(0, _front->choice().city));
    if (s == Screen::kDriverStats || s == Screen::kRaceRecords) {
        _screen = s;
        _tableRow = 0;
        buildTable();
    }
    if (s == Screen::kReplayList) {
        _replays = listReplays(_savePath);
        _replayRow = std::min(_replayRow, _replays.empty() ? 0 : _replays.size() - 1);
    }
    if (s == Screen::kDefaults || s == Screen::kDuplicateKey || s == Screen::kQuit || s == Screen::kReplayName ||
        s == Screen::kDuplicateDriver) {
        if (_screen != Screen::kDefaults && _screen != Screen::kDuplicateKey && _screen != Screen::kQuit &&
            _screen != Screen::kReplayName && _screen != Screen::kDuplicateDriver)
            _under = _screen;
    } else if (s != Screen::kNone && s != Screen::kOptions && _screen == Screen::kOptions)
        _entered = _edit; // what Cancel on the screen goes back to
    if (s == Screen::kAbout)
        _openedMs = _nowMs;
    _screen = s;
    _focus = 0;
    _waitingForKey = false;
    const auto list = items();
    while (size_t(_focus) < list.size() && (!list[size_t(_focus)].enabled || list[size_t(_focus)].kind == Kind::kLabel))
        ++_focus;
    if (size_t(_focus) >= list.size())
        _focus = 0;
    _sys.setTextInput(typing());
}

bool GameMenus::checked(const std::string &w) const {
    if (driverScreen(_screen))
        return driverChecked(w);
    const GameOptions &o = _edit;
    if (w == "aud_fx")
        return o.soundFx;
    if (w == "aud_com")
        return o.commentary;
    if (w == "aud_musc")
        return o.music;
    if (w == "aud_amb")
        return o.ambient;
    if (w == "gfx_sky")
        return o.texturedSky;
    if (w == "gfx_peds")
        return o.pedestrians;
    if (w == "gfx_rflx")
        return o.vehicleReflections;
    if (w == "gfx_port")
        return o.smartRendering;
    if (w == "ctrl_aut")
        return o.autoReverse;
    if (w == "ctrl_pov")
        return o.startInCar;
    if (w == "ctrl_fbk")
        return o.forceFeedback;
    return false;
}

void GameMenus::toggle(const std::string &w) {
    if (driverScreen(_screen)) {
        driverToggle(w);
        return;
    }
    GameOptions &o = _edit;
    if (w == "aud_fx")
        o.soundFx = !o.soundFx;
    else if (w == "aud_com")
        o.commentary = !o.commentary;
    else if (w == "aud_musc" || w == "aud_amb") {
        // "Check either Play Music or City Sounds" (jpg/aud_tmus): one or
        // the other, at Music/City Volume.
        bool &mine = w == "aud_musc" ? o.music : o.ambient;
        mine = !mine;
        if (mine)
            (w == "aud_musc" ? o.ambient : o.music) = false;
    } else if (w == "gfx_sky")
        o.texturedSky = !o.texturedSky;
    else if (w == "gfx_peds")
        o.pedestrians = !o.pedestrians;
    else if (w == "gfx_rflx")
        o.vehicleReflections = !o.vehicleReflections;
    else if (w == "gfx_port")
        o.smartRendering = !o.smartRendering;
    else if (w == "ctrl_fbk")
        o.forceFeedback = !o.forceFeedback;
    else if (w == "ctrl_aut")
        o.autoReverse = !o.autoReverse;
    else if (w == "ctrl_pov")
        o.startInCar = !o.startInCar;
    applySoundOptions(_edit, _mixer); // heard at once
}

float GameMenus::share(const std::string &w) const {
    if (_screen == Screen::kRaceOptions)
        return raceShare(w);
    const GameOptions &o = _edit;
    if (w == "WAVE VOLUME")
        return o.effectsVolume;
    if (w == "CD VOLUME")
        return o.musicVolume;
    if (w == "BALANCE")
        return (o.balance + 1) / 2;
    if (w == "far clip")
        return o.visibility;
    if (w == "lighting")
        return o.lightingQuality;
    if (w == "Steering Sensitivity")
        return o.steering;
    return 0;
}

void GameMenus::setShare(const std::string &w, float v) {
    if (_screen == Screen::kRaceOptions) {
        setRaceShare(w, v);
        return;
    }
    v = std::clamp(std::round(v * 20) / 20, 0.0f, 1.0f);
    GameOptions &o = _edit;
    if (w == "WAVE VOLUME")
        o.effectsVolume = v;
    else if (w == "CD VOLUME")
        o.musicVolume = v;
    else if (w == "BALANCE")
        o.balance = v * 2 - 1;
    else if (w == "far clip")
        o.visibility = v;
    else if (w == "lighting")
        o.lightingQuality = v;
    else if (w == "Steering Sensitivity")
        o.steering = v;
    applySoundOptions(_edit, _mixer);
}

std::string GameMenus::choiceText(const std::string &w) const {
    static const uint32_t kDetail[4] = {574, 575, 576, 577}; // Low, Medium, High, Very High
    static const char *const kDetailNames[4] = {"Low", "Medium", "High", "Very High"};
    if (w == "DEVICE")
        return "Default output";
    if (w == "STEREO")
        return _strings.get(327, "Stereo");
    if (w == "SOUND QUALITY")
        return _strings.get(576, "High");
    if (w == "DISPLAY")
        return _edit.fullScreen ? "Full Screen" : "Window";
    if (w == "RENDERER")
        return "Reluva 3D";
    if (w == "RESOLUTION") {
        const Resolution &r =
            resolutions()[size_t(std::clamp<int>(_edit.resolution, 0, int(resolutions().size()) - 1))];
        return std::to_string(r.w) + " x " + std::to_string(r.h);
    }
    if (w == "TEXTURE QUALITY")
        return _strings.get(kDetail[_edit.textureQuality & 3], kDetailNames[_edit.textureQuality & 3]);
    if (w == "OBJECT DETAIL")
        return _strings.get(kDetail[_edit.objectDetail & 3], kDetailNames[_edit.objectDetail & 3]);
    if (w == "CLOUD SHADOWS") {
        static const char *const kShadows[3] = {"None", "Low", "High"};
        const int level = std::clamp(_edit.cloudShadows, 0, 2);
        return _strings.get(uint32_t(660 + level), kShadows[level]);
    }
    if (w == "CONTROLLERS")
        return _strings.get(581, "Keyboard");
    return {};
}

void GameMenus::step(const Item &item, int dir) {
    const std::string &w = item.widget;
    if (item.kind == Kind::kBar && _screen == Screen::kRaceOptions)
        setShare(w, share(w) + (w == "COP DENSITY" ? 0.25f : 0.1f) * float(dir)); // a tenth; a police car
    else if (item.kind == Kind::kBar)
        setShare(w, share(w) + 0.05f * float(dir));
    else if (_screen == Screen::kRaceOptions && (w == "TIME" || w == "WEATHER"))
        stepCondition(w, dir);
    else if (w == "DISPLAY")
        _edit.fullScreen = !_edit.fullScreen;
    else if (w == "RESOLUTION")
        _edit.resolution = (_edit.resolution + dir + int(resolutions().size())) % int(resolutions().size());
    else if (w == "OBJECT DETAIL")
        _edit.objectDetail = std::clamp(_edit.objectDetail + dir, 0, 3);
    else if (w == "TEXTURE QUALITY")
        _edit.textureQuality = std::clamp(_edit.textureQuality + dir, 0, 3);
    else if (w == "CLOUD SHADOWS")
        _edit.cloudShadows = std::clamp(_edit.cloudShadows + dir, 0, 2);
    else if (item.kind == Kind::kList)
        _column = dir > 0 ? 1 : 0;
}

std::string GameMenus::description(const std::string &w) const {
    static const std::pair<const char *, const char *> kPictures[] = {
        {"opt_aud", "opt_taud"},
        {"opt_ctl", "opt_tctl"},
        {"opt_gfx", "opt_tgfx"},
        {"opt_abt", "opt_tabt"},
        {"mnav_prev", "opt_tbck"},
        {"aud_fx", "aud_tfx"},
        {"aud_com", "aud_tcom"},
        {"aud_musc", "aud_tmus"},
        {"aud_amb", "aud_tcty"},
        {"CD VOLUME", "aud_tmus"},
        {"gfx_sky", "gfx_tsky"},
        {"gfx_rflx", "gfx_tfl"},
        {"gfx_peds", "gfx_tped"},
        {"RENDERER", "gfx_tren"},
        {"RESOLUTION", "gfx_tres"},
        {"far clip", "gfx_tfp"},
        {"lighting", "gfx_tlq"},
        {"TEXTURE QUALITY", "gfx_ttq"},
        {"OBJECT DETAIL", "gfx_tobj"},
        {"ctrl_aut", "ctl_trev"},
        {"ctrl_pov", "ctl_tpov"},
        {"ctrl_fbk", "ctl_tffb"},
        {"CONTROLLERS", "ctl_tcon"},
        {"Steering Sensitivity", "ctl_tss"},
        {"Joystick Dead Zone", "ctl_dd"},
        {"ctrl_cal", "ctl_tcal"},
        {"Force-Feedback Collision Intensity", "ctl_titn"},
        {"Force-Feedback Road Force Intensity", "ctl_trf"},
        {"ctrl_cus", "ctl_tcus"},
        {"opt_can", "opt_tbck"},
    };
    if (w == "opt_def")
        return _screen == Screen::kAudio ? "opt_tadf" : "opt_tdef";
    for (const auto &[name, picture] : kPictures)
        if (w == name)
            return picture;
    return {};
}

GameMenus::Action GameMenus::done() {
    const GameOptions before = currentOptions();
    setCurrentOptions(_edit);
    applySoundOptions(_edit, _mixer);
    if (before.resolution != _edit.resolution || before.fullScreen != _edit.fullScreen) {
        const Resolution &r = resolutions()[size_t(_edit.resolution)];
        _sys.setVideoMode(r.w, r.h, _edit.fullScreen);
    }
    _edit.save(_savePath);
    _entered = _edit;
    go(Screen::kOptions);
    return Action::kNone;
}

GameMenus::Action GameMenus::cancel() {
    _edit = _entered;
    applySoundOptions(currentOptions(), _mixer);
    go(Screen::kOptions);
    return Action::kNone;
}

GameMenus::Action GameMenus::activate(const Item &item) {
    if (!item.enabled)
        return Action::kNone;
    if (driverScreen(_screen))
        return driverActivate(item);
    const std::string &w = item.widget;
    if (item.kind == Kind::kCheck) {
        toggle(w);
        return Action::kNone;
    }
    if (item.kind == Kind::kList && _screen == Screen::kReplayList)
        return activate(Item{Kind::kButton, "rpl_load", {}, 4, true});
    if (item.kind == Kind::kText)
        return activate(Item{Kind::kButton, "dlg_done", {}, 4, true});
    if (item.kind == Kind::kList) {
        _waitingForKey = true;
        _sys.setTextInput(false);
        return Action::kNone;
    }
    if (w == "opt_aud")
        go(Screen::kAudio);
    else if (w == "opt_gfx")
        go(Screen::kGraphics);
    else if (w == "opt_ctl")
        go(Screen::kControl);
    else if (w == "opt_abt")
        go(Screen::kAbout);
    else if (w == "ctrl_cus") {
        _row = _column = _listTop = 0;
        go(Screen::kCustomize);
    } else if (w == "mnav_prev") {
        _screen = Screen::kNone;
        return Action::kClosed;
    } else if (w == "options_done")
        go(Screen::kOptions);
    else if (w == "opt_dn")
        return done();
    else if (w == "opt_can")
        return cancel();
    else if (w == "opt_def")
        go(Screen::kDefaults);
    else if (w == "dlg_ok" && _screen == Screen::kDefaults) {
        // "Return all ... to their original, default settings."
        if (_under == Screen::kAudio)
            _edit.resetAudio();
        else if (_under == Screen::kGraphics)
            _edit.resetGraphics();
        else
            _edit.resetControls();
        applySoundOptions(_edit, _mixer);
        go(_under);
    } else if (w == "dlg_can" || (w == "dlg_ok" && _screen == Screen::kDuplicateKey))
        go(_under);
    else if (w == "popup_ok" && _screen == Screen::kReplayAsk) {
        _under = Screen::kNone;
        go(Screen::kReplayName); // save as
    } else if (w == "popup_ok") {
        _screen = Screen::kNone;
        return Action::kQuit;
    } else if (w == "popup_cancel") {
        _screen = Screen::kNone;
        return Action::kClosed;
    } else if (w == "rpl_load" && _replayRow < _replays.size()) {
        _replayFile = _replays[_replayRow].file;
        _screen = Screen::kNone;
        return Action::kLoadReplay;
    } else if (w == "rpl_edit" && _replayRow < _replays.size()) {
        _name = _replays[_replayRow].name;
        go(Screen::kReplayName);
    } else if (w == "rpl_del" && _replayRow < _replays.size()) {
        deleteReplay(_replays[_replayRow]);
        go(Screen::kReplayList);
    } else if (w == "dlg_cancel" && _screen == Screen::kReplayList) {
        _screen = Screen::kNone;
        return Action::kClosed;
    } else if (w == "dlg_cancel" && _screen == Screen::kReplayName) {
        if (_under == Screen::kReplayList)
            go(Screen::kReplayList);
        else {
            _screen = Screen::kNone;
            return Action::kClosed;
        }
    } else if (w == "dlg_done" && _screen == Screen::kReplayName && !_name.empty()) {
        if (_under == Screen::kReplayList) {
            if (_replayRow < _replays.size())
                renameReplay(_savePath, _replays[_replayRow], _name);
            go(Screen::kReplayList);
        } else {
            _screen = Screen::kNone;
            _sys.setTextInput(false);
            return Action::kSaveReplay;
        }
    }
    return Action::kNone;
}

bool GameMenus::hit(const Item &item, int x, int y, int &w, int &h) const {
    const Widget *wd = widget(item.widget);
    if (!wd)
        return false;
    w = wd->w;
    h = wd->h;
    if (item.kind == Kind::kButton || item.kind == Kind::kCheck)
        if (const image::Image *im = _pictures.texture(item.picture)) {
            w = im->w;
            h = im->h / std::max(1, item.states);
        }
    if (item.kind == Kind::kList)
        h = kListRow * std::min(kControls, kListShown);
    const int x0 = wd->x + originX();
    const int y0 = wd->y + originY();
    return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
}

GameMenus::Action GameMenus::handle(const Event &ev) {
    if (_screen == Screen::kNone)
        return Action::kNone;
    const auto list = items();
    if (list.empty())
        return Action::kNone;
    _focus = std::clamp(_focus, 0, int(list.size()) - 1);
    if (ev.type == EventType::kMouseMove && !_waitingForKey) {
        // Pointed at: focused, as the game's are.
        for (size_t i = 0; i < list.size(); ++i) {
            int w = 0;
            int h = 0;
            if (list[i].enabled && list[i].kind != Kind::kLabel && hit(list[i], ev.mouseX, ev.mouseY, w, h)) {
                _focus = int(i);
                break;
            }
        }
        return Action::kNone;
    }
    const Item &item = list[size_t(_focus)];
    // Customize Controls, waiting for the key to give a control.
    if (_waitingForKey) {
        if (ev.type != EventType::kKeyDown)
            return Action::kNone;
        _waitingForKey = false;
        if (ev.key == key::kEscape)
            return Action::kNone;
        const Control c = Control(_row);
        uint32_t &slot = _edit.keys[size_t(_row)][size_t(_column)];
        if (ev.key == key::kBackspace || ev.key == key::kDelete) {
            slot = 0;
            return Action::kNone;
        }
        if (_edit.otherWith(ev.key, c) != Control::kCount || _edit.keys[size_t(_row)][size_t(1 - _column)] == ev.key) {
            go(Screen::kDuplicateKey); // "Duplicate key or button, please make a different selection"
            return Action::kNone;
        }
        slot = ev.key;
        return Action::kNone;
    }
    if (_screen == Screen::kNewDriver && _focus == 0) {
        if (ev.type == EventType::kTextInput) {
            for (const char *c = ev.text; *c; ++c)
                if (static_cast<unsigned char>(*c) >= 32 && _newName.size() < 20)
                    _newName.push_back(*c);
            return Action::kNone;
        }
        if (ev.type == EventType::kKeyDown && ev.key == key::kBackspace) {
            if (!_newName.empty())
                _newName.pop_back();
            return Action::kNone;
        }
        if (ev.type == EventType::kKeyDown && ev.key == key::kSpace)
            return Action::kNone;
    }
    if (_screen == Screen::kReplayName) {
        if (ev.type == EventType::kTextInput) {
            for (const char *c = ev.text; *c; ++c)
                if (static_cast<unsigned char>(*c) >= 32 && _name.size() < 40)
                    _name.push_back(*c);
            return Action::kNone;
        }
        if (ev.type == EventType::kKeyDown && ev.key == key::kBackspace) {
            if (!_name.empty())
                _name.pop_back();
            return Action::kNone;
        }
        if (ev.type == EventType::kKeyDown && ev.key == key::kSpace)
            return Action::kNone; // typed, as text
    }
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        for (size_t i = 0; i < list.size(); ++i) {
            int w = 0;
            int h = 0;
            if (!list[i].enabled || !hit(list[i], ev.mouseX, ev.mouseY, w, h))
                continue;
            _focus = int(i);
            const Widget *wd = widget(list[i].widget);
            switch (list[i].kind) {
            case Kind::kBar:
                setShare(list[i].widget, float(ev.mouseX - (wd->x + originX() + barInset())) / kBarWidth);
                return Action::kNone;
            case Kind::kChoice:
                if (_screen == Screen::kRaceOptions) { // the icon clicked
                    const bool time = list[i].widget == "TIME";
                    int &v = time ? _conditions.timeOfDay : _conditions.weather;
                    v = std::clamp((ev.mouseX - (wd->x + originX() + 4)) / 31, 0, 3);
                    sayCondition(time);
                    return Action::kNone;
                }
                step(list[i], ev.mouseX < wd->x + originX() + wd->w / 2 ? -1 : 1);
                return Action::kNone;
            case Kind::kList:
                _row = std::clamp(_listTop + (ev.mouseY - (wd->y + originY())) / kListRow, 0, kControls - 1);
                _column = ev.mouseX >= wd->x + kSecondColumn ? 1 : 0;
                return activate(list[i]);
            default:
                return activate(list[i]);
            }
        }
        return Action::kNone;
    }
    if (ev.type != EventType::kKeyDown)
        return Action::kNone;
    switch (ev.key) {
    case key::kUp:
    case key::kDown: {
        const int dir = ev.key == key::kDown ? 1 : -1;
        if (item.kind == Kind::kList && _screen == Screen::kCitySelection) {
            if (_cities && int(_cityRow) + dir >= 0 && size_t(int(_cityRow) + dir) < _cities->size()) {
                _cityRow = size_t(int(_cityRow) + dir);
                break;
            }
        } else if (item.kind == Kind::kList && (_screen == Screen::kDriverStats || _screen == Screen::kRaceRecords)) {
            if (int(_tableRow) + dir >= 0 && size_t(int(_tableRow) + dir) + 1 < _table.size()) {
                _tableRow = size_t(int(_tableRow) + dir);
                break;
            }
        } else if (item.kind == Kind::kList && _screen == Screen::kReplayList) {
            if (int(_replayRow) + dir >= 0 && size_t(int(_replayRow) + dir) < _replays.size()) {
                _replayRow = size_t(int(_replayRow) + dir);
                break;
            }
        } else if (item.kind == Kind::kList && _row + dir >= 0 && _row + dir < kControls) {
            _row += dir;
            _listTop = std::clamp(_listTop, _row - kListShown + 1, _row);
            break;
        }
        for (size_t tries = 0; tries < list.size(); ++tries) {
            _focus = (_focus + dir + int(list.size())) % int(list.size());
            const Item &next = list[size_t(_focus)];
            if (next.enabled && next.kind != Kind::kLabel)
                break;
        }
        break;
    }
    case key::kLeft:
    case key::kRight:
        if (item.enabled && item.kind != Kind::kButton && item.kind != Kind::kCheck)
            step(item, ev.key == key::kRight ? 1 : -1);
        break;
    case key::kReturn:
    case key::kKpEnter:
    case key::kSpace:
        return activate(item);
    case key::kEscape:
        switch (_screen) {
        case Screen::kOptions:
        case Screen::kQuit:
        case Screen::kReplayAsk:
        case Screen::kReplayList:
            _screen = Screen::kNone;
            return Action::kClosed;
        case Screen::kReplayName:
            return activate(Item{Kind::kButton, "dlg_cancel", {}, 4, true});
        case Screen::kDuplicateDriver:
            go(Screen::kNewDriver);
            break;
        case Screen::kNewDriver:
        case Screen::kDeleteDriver:
        case Screen::kLastDriver:
        case Screen::kDriverStats:
        case Screen::kRaceRecords:
        case Screen::kRaceOptions:
        case Screen::kCitySelection:
        case Screen::kLocked:
            _screen = Screen::kNone;
            _sys.setTextInput(false);
            return Action::kClosed;
        case Screen::kAbout:
            go(Screen::kOptions);
            break;
        case Screen::kDefaults:
        case Screen::kDuplicateKey:
            go(_under);
            break;
        case Screen::kCustomize:
            go(Screen::kControl);
            break;
        default:
            return cancel();
        }
        break;
    default:
        break;
    }
    return Action::kNone;
}

void GameMenus::text(Surface &o, const std::string &name, const std::string &s, Color c) const {
    if (const Widget *w = widget(name))
        font::drawText(o, w->x + originX() + 6,
                       w->y + originY() + std::max(0, (std::min(w->h, 24) - font::kLineHeight) / 2), s, c);
}

void GameMenus::drawItems(Surface &o, const std::vector<Item> &list, int focus) const {
    for (size_t i = 0; i < list.size(); ++i) {
        const Item &item = list[i];
        const Widget *w = widget(item.widget);
        if (!w)
            continue;
        const bool focused = int(i) == focus;
        const int x = w->x + originX();
        const int y = w->y + originY();
        const Color words = !item.enabled ? Color{130, 130, 150}
                            : focused     ? Color{255, 255, 120}
                                          : Color{255, 230, 140};
        switch (item.kind) {
        case Kind::kButton:
            if (const image::Image *im = _pictures.texture(item.picture))
                drawButtonState(o, *im, x, y, item.states, !item.enabled ? item.states - 1 : focused ? 1 : 0);
            break;
        case Kind::kCheck:
            // Its five states: clear, clear and pointed at, ticked, ticked
            // and pointed at, not available.
            if (const image::Image *im = _pictures.texture(item.picture))
                drawButtonState(o, *im, x, y, 5,
                                !item.enabled ? 4 : (checked(item.widget) ? 2 : 0) + (focused ? 1 : 0));
            break;
        case Kind::kBar: {
            const int lit = int(std::lround(share(item.widget) * kBarWidth));
            // (Change Race Options' rows are 15 high on a grid twice that.)
            const int inset = barInset();
            const int top = _screen == Screen::kRaceOptions ? -4 : w->h > 20 ? 6 : 2;
            if (item.widget == "BALANCE")
                fill(o, x + inset + std::min(lit, kBarWidth - 4), y + top, 4, w->h - 2 * top, {255, 200, 40});
            else if (item.enabled)
                fill(o, x + inset, y + top, lit, w->h - 2 * top, {255, 200, 40});
            if (focused)
                font::drawText(o, x + 2, y + 8, ">", {255, 255, 120});
            break;
        }
        case Kind::kChoice:
            if (_screen == Screen::kRaceOptions) {
                drawConditionIcons(o, item.widget, focused);
                break;
            }
            text(o, item.widget, choiceText(item.widget), words);
            if (focused)
                font::drawText(o, x - 10, y + 4, ">", {255, 255, 120});
            break;
        case Kind::kText:
            if (_screen == Screen::kNewDriver) {
                text(o, item.widget, _newName + (focused && (_nowMs / 400) % 2 ? "_" : ""), {255, 255, 255});
                break;
            }
            // The end of it, as much as the box holds.
            text(o, item.widget,
                 (_name.size() > 36 ? _name.substr(_name.size() - 36) : _name) + ((_nowMs / 400) % 2 ? "_" : ""),
                 {255, 255, 255});
            break;
        case Kind::kList:
        case Kind::kLabel:
            break;
        }
    }
}

void GameMenus::drawScreenWords(Surface &o, uint64_t nowMs) const {
    if (_screen == Screen::kCustomize) {
        // Each control, its two keys; the chosen one waiting for a key.
        const Widget *w = widget("CW Array");
        if (!w)
            return;
        font::drawText(o, 432, 140, "Enter: change it", {255, 255, 255});
        font::drawText(o, 432, 154, "Left/Right: which key", {255, 255, 255});
        font::drawText(o, 432, 168, "Backspace: none", {255, 255, 255});
        font::drawText(o, 432, 182, "Esc: as it was", {255, 255, 255});
        for (int i = _listTop; i < std::min(kControls, _listTop + kListShown); ++i) {
            const int y = w->y + (i - _listTop) * kListRow + 4;
            const bool row = i == _row;
            if (row)
                fill(o, w->x - 6, y - 3, 290, kListRow - 2, {40, 40, 110});
            font::drawText(o, w->x, y, _strings.get(controlStringId(Control(i)), controlFallbackName(Control(i))),
                           row ? Color{255, 255, 120} : Color{230, 230, 230});
            for (int k = 0; k < 2; ++k) {
                const bool waiting = row && _waitingForKey && k == _column;
                std::string name =
                    waiting ? ((nowMs / 300) % 2 ? "Press a key" : "") : keyName(_edit.keys[size_t(i)][size_t(k)]);
                font::drawText(o, w->x + (k ? kSecondColumn : kKeyColumn) - 30, y, name,
                               row && k == _column ? Color{120, 255, 120} : Color{200, 200, 255});
            }
        }
    } else if (_screen == Screen::kAbout) {
        // The credits (jpg/credits, 215 x 4,582) scrolling through their box.
        if (const Widget *w = widget("Credits"))
            if (const image::Image *credits = _pictures.jpg("credits")) {
                const int top = int(float(nowMs - _openedMs) / 1000 * kCreditsSpeed) - w->h;
                drawBand(o, *credits, top, w->x, w->y, w->w, w->h);
            }
        text(o, "PID Label", "None: this is Reluva", {255, 255, 255});
    } else if (_screen == Screen::kDuplicateKey) {
        // ctrl_dlg says it all.
    }
}

void GameMenus::drawReplayList(Surface &o) const {
    // The replays saved: their names, city and length; the chosen one lit.
    const Widget *w = widget("CITY");
    if (!w)
        return;
    const int x = w->x + originX() - 8;
    const int y0 = w->y + originY() + 10;
    if (_replays.empty())
        font::drawText(o, x + 4, y0, "No replays saved yet", {200, 200, 200});
    const size_t first = _replayRow >= size_t(kReplayRows) ? _replayRow - size_t(kReplayRows) + 1 : 0;
    for (size_t i = first; i < _replays.size() && i < first + size_t(kReplayRows); ++i) {
        const ReplayEntry &r = _replays[i];
        const int y = y0 + int(i - first) * kReplayRow;
        if (i == _replayRow)
            fill(o, x, y - 2, 258, kReplayRow, {50, 50, 130});
        char line[96];
        std::snprintf(line, sizeof line, "%-24.24s %-6s %d:%02d", r.name.c_str(), r.city.c_str(), int(r.duration) / 60,
                      int(r.duration) % 60);
        font::drawText(o, x + 4, y, line, i == _replayRow ? Color{255, 255, 120} : Color{230, 230, 230});
    }
}

void GameMenus::draw(Surface &o, uint64_t nowMs) {
    _nowMs = nowMs;
    if (_screen == Screen::kNone)
        return;
    if (overlay(_screen)) {
        // Dialogs over whatever's beneath: the rename over the list.
        const Screen was = _screen;
        const int keep = _focus;
        if (was == Screen::kReplayName && _under == Screen::kReplayList) {
            _screen = Screen::kReplayList;
            _focus = -1;
            if (const image::Image *d = _pictures.jpg("rpl_bk"))
                drawPicture(o, *d, originX(), originY());
            drawItems(o, items(), -1);
            drawReplayList(o);
            _screen = was;
            _focus = keep;
        }
        if (driverScreen(was)) {
            // The new driver's form under the duplicate name's warning.
            if (was == Screen::kDuplicateDriver) {
                _screen = Screen::kNewDriver;
                _focus = -1;
                if (const image::Image *d = _pictures.jpg(driverPicture(Screen::kNewDriver)))
                    drawPicture(o, *d, originX(), originY());
                drawItems(o, items(), -1);
                _screen = was;
                _focus = keep;
            }
            if (const image::Image *d = _pictures.jpg(driverPicture(was)))
                drawPicture(o, *d, originX(), originY());
            if (was == Screen::kDriverStats || was == Screen::kRaceRecords)
                drawTable(o);
            if (was == Screen::kCitySelection && _cities)
                for (size_t i = 0; i < _cities->size(); ++i)
                    font::drawText(o, originX() + 80, originY() + 98 + int(i) * 16, (*_cities)[i].name,
                                   i == _cityRow ? Color{255, 255, 120} : Color{220, 220, 255});
            drawItems(o, items(), _focus);
            return;
        }
        const char *picture = was == Screen::kReplayList   ? "rpl_bk"
                              : was == Screen::kReplayName ? "REDIT_BK"
                              : was == Screen::kReplayAsk  ? "msg_dlg"
                                                           : "quit_dlg";
        if (const image::Image *d = _pictures.jpg(picture))
            drawPicture(o, *d, originX(), originY());
        if (was == Screen::kReplayAsk) {
            // "REPLAY SAVE MENU: Do you want to save this replay?" (the
            // string lacks its D).
            std::string ask = _strings.get(503, "Do you want to save this replay?");
            if (ask.rfind("o ", 0) == 0)
                ask = "D" + ask;
            font::drawText(o, originX() + 80, originY() + 14, ask, {255, 255, 255});
        }
        if (was == Screen::kReplayList)
            drawReplayList(o);
        drawItems(o, items(), _focus);
        return;
    }
    const bool dialog = _screen == Screen::kDefaults || _screen == Screen::kDuplicateKey || _screen == Screen::kQuit;
    const Screen was = _screen;
    const int keepFocus = _focus;
    auto background = [](Screen s) -> const char * {
        switch (s) {
        case Screen::kOptions:
            return "opt_bk";
        case Screen::kAudio:
            return "aud_bk";
        case Screen::kGraphics:
            return "gfx_bk";
        case Screen::kControl:
            return "ctrl_bk";
        case Screen::kCustomize:
            return "cuss_bk";
        case Screen::kAbout:
            return "about_bk";
        default:
            return "main_bk";
        }
    };
    // The screen, or the one a dialog is over (without its focus).
    const Screen base = dialog ? (_under == Screen::kNone ? Screen::kOptions : _under) : _screen;
    if (_screen == Screen::kQuit && _under == Screen::kNone) {
        // Over whatever menu asked (drawn already).
    } else {
        std::fill(o.pixels.begin(), o.pixels.end(), 0);
        if (const image::Image *bg = _pictures.jpg(background(base)))
            drawPicture(o, *bg, 0, 0, o.w, o.h);
        _screen = base;
        _focus = dialog ? -1 : keepFocus;
        drawItems(o, items(), _focus);
        drawScreenWords(o, nowMs);
        // The focused control's description, bottom left.
        if (!dialog) {
            const auto list = items();
            if (size_t(_focus) < list.size())
                if (const image::Image *d = _pictures.jpg(description(list[size_t(_focus)].widget)))
                    drawPicture(o, *d, 40, 396);
        }
        _screen = was;
        _focus = keepFocus;
    }
    if (dialog) {
        const char *picture = _screen == Screen::kDefaults       ? "odef_dlg"
                              : _screen == Screen::kDuplicateKey ? "ctrl_dlg"
                                                                 : "quit_dlg";
        if (const image::Image *d = _pictures.jpg(picture))
            drawPicture(o, *d, originX(), originY());
        drawItems(o, items(), _focus);
    }
    font::drawText(o, 4, o.h - 12, "Up/Down, Left/Right, Enter, Esc", {200, 200, 200});
}

} // namespace reluva::age
