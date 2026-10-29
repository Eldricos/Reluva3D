// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Options menus and the main menu's dialogs: tune/widget.csv's Main
// Options Menu, Audio, Graphics and Control Options, Control Custom,
// AboutMenu Menu and the dialogs, on the game's jpg/ backgrounds with its
// texture/ buttons. Which picture is which, and what each control does,
// is Reluva's reading of them; see docs/engines/age-menus.md.

#include "engines/age/game_menus.h"

#include "common/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::age {

namespace {

// Where the dialogs go (tune/menu.csv's places for dialogs of their
// size): the 300 x 225 ones, and the 400 x 76 message boxes.
constexpr int kSmallX = 170, kSmallY = 127, kBoxX = 120, kBoxY = 202, kDialogY = 75;
constexpr int kReplayRow = 14, kReplayRows = 11; // the replay list's (in jpg/rpl_bk's box)
// The bars' grid on the option screens' backgrounds, within their
// widget's box (measured on jpg/aud_bk, gfx_bk, ctrl_bk).
constexpr int kBarInset = 23, kBarWidth = 137;
// Customize Controls' list: its rows and columns (on jpg/cuss_bk's box).
constexpr int kListRow = 20, kKeyColumn = 160, kSecondColumn = 230;
// The About screen's credits scroll by at this many pixels a second.
constexpr float kCreditsSpeed = 30;

void fill(Surface& o, int x, int y, int w, int h, Color c) {
    for (int yy = std::max(0, y); yy < std::min(o.h, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min(o.w, x + w); ++xx) {
            uint8_t* p = o.row(yy) + xx * 4;
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
}

// A band of a tall picture (rows from `top`, wrapping round), drawn at
// x, y.
void drawBand(Surface& o, const image::Image& im, int top, int x, int y, int w, int h) {
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

GameMenus::GameMenus(System& sys, const SearchSet& files, const GameStrings& strings, audio::Mixer* mixer,
                     std::filesystem::path savePath)
    : sys_(sys), files_(files), strings_(strings), mixer_(mixer), savePath_(std::move(savePath)), pictures_(files) {
    std::vector<uint8_t> data;
    if (!files.read("tune/widget.csv", data))
        ok_ = !parseWidgets(std::string(data.begin(), data.end()), widgets_) && pictures_.jpg("opt_bk");
}

std::string GameMenus::menuName() const {
    switch (screen_) {
    case Screen::Options:
        return "Main Options Menu";
    case Screen::Audio:
        return "Audio Options";
    case Screen::Graphics:
        return "Graphics Options";
    case Screen::Control:
        return "Control Options";
    case Screen::Customize:
        return "Control Custom";
    case Screen::About:
        return "AboutMenu Menu";
    case Screen::Defaults:
    case Screen::DuplicateKey:
        return "odef_dlg";
    case Screen::Quit:
    case Screen::ReplayAsk:
        return "quit_dlg";
    case Screen::ReplayList:
        return "Replay Selection";
    case Screen::ReplayName:
        return "Replay Description";
    case Screen::None:
        break;
    }
    return {};
}

int GameMenus::originX() const {
    switch (screen_) {
    case Screen::Defaults:
    case Screen::DuplicateKey:
        return kSmallX;
    case Screen::Quit:
    case Screen::ReplayAsk:
    case Screen::ReplayList:
    case Screen::ReplayName:
        return kBoxX;
    default:
        return 0;
    }
}

int GameMenus::originY() const {
    switch (screen_) {
    case Screen::Defaults:
    case Screen::DuplicateKey:
        return kSmallY;
    case Screen::Quit:
    case Screen::ReplayAsk:
        return kBoxY;
    case Screen::ReplayList:
    case Screen::ReplayName:
        return kDialogY;
    default:
        return 0;
    }
}

bool GameMenus::overlay(Screen s) const {
    return s == Screen::Quit || s == Screen::ReplayAsk || s == Screen::ReplayList || s == Screen::ReplayName;
}

bool GameMenus::typing() const { return waitingForKey_ || screen_ == Screen::ReplayName; }

void GameMenus::nameReplay(const std::string& name) {
    name_ = name.substr(0, 40);
    under_ = Screen::None;
    screen_ = Screen::None;
    go(Screen::ReplayName);
}

void GameMenus::askToSaveReplay(const std::string& name) {
    name_ = name.substr(0, 40);
    under_ = Screen::None;
    screen_ = Screen::None;
    go(Screen::ReplayAsk);
}

const Widget* GameMenus::widget(const std::string& name) const {
    const std::string menu = menuName();
    for (const Widget& w : widgets_)
        if (w.menu == menu && w.name == name)
            return &w;
    return nullptr;
}

std::vector<GameMenus::Item> GameMenus::items() const {
    auto button = [](const std::string& w, const std::string& pic, bool enabled = true) {
        return Item{Kind::Button, w, pic, 4, enabled};
    };
    auto check = [](const std::string& w, bool enabled = true) { return Item{Kind::Check, w, w, 5, enabled}; };
    auto bar = [](const std::string& w, bool enabled = true) { return Item{Kind::Bar, w, {}, 0, enabled}; };
    auto choice = [](const std::string& w, bool enabled = true) { return Item{Kind::Choice, w, {}, 0, enabled}; };
    const std::vector<Item> bottom = {button("opt_def", "opt_def"), button("opt_can", "opt_can"),
                                      button("opt_dn", "opt_done")};
    std::vector<Item> list;
    switch (screen_) {
    case Screen::Options:
        return {button("opt_aud", "opt_aud"), button("opt_ctl", "opt_ctl"), button("opt_gfx", "opt_gfx"),
                button("opt_abt", "opt_abt"), button("mnav_prev", "mnav_prv")};
    case Screen::Audio:
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
    case Screen::Graphics:
        // Reflections, the renderer, texture quality and cloud shadows
        // aren't Reluva's to change.
        list = {check("gfx_sky"),        check("gfx_rflx", false),
                check("gfx_peds"),       check("gfx_port"),
                choice("DISPLAY"),       choice("RENDERER", false),
                choice("RESOLUTION"),    bar("far clip"),
                bar("lighting"),         choice("TEXTURE QUALITY", false),
                choice("OBJECT DETAIL"), choice("CLOUD SHADOWS", false)};
        break;
    case Screen::Control:
        // The keyboard only: no joystick to calibrate, no force feedback.
        list = {check("ctrl_aut"),
                check("ctrl_pov"),
                check("ctrl_fbk", false),
                choice("CONTROLLERS", false),
                bar("Steering Sensitivity"),
                bar("Joystick Dead Zone", false),
                button("ctrl_cal", "ctrl_cal", false),
                bar("Force-Feedback Collision Intensity", false),
                bar("Force-Feedback Road Force Intensity", false),
                button("ctrl_cus", "ctrl_cus")};
        break;
    case Screen::Customize:
        list = {Item{Kind::List, "CW Array", {}, 0, true}};
        break;
    case Screen::About:
        return {Item{Kind::Label, "Credits", {}, 0, false}, button("options_done", "opt_done")};
    case Screen::Defaults:
        return {button("dlg_ok", "dlg_ok"), button("dlg_can", "dlg_can")};
    case Screen::DuplicateKey:
        return {button("dlg_ok", "dlg_ok")};
    case Screen::Quit:
        return {button("popup_ok", "dlg_ok"), button("popup_cancel", "dlg_can")};
    case Screen::ReplayAsk:
        return {button("popup_ok", "dlg_yes"), button("popup_cancel", "dlg_no")};
    case Screen::ReplayList:
        return {Item{Kind::List, "CITY", {}, 0, !replays_.empty()}, button("rpl_load", "RPL_LOAD", !replays_.empty()),
                button("rpl_edit", "rpl_edit", !replays_.empty()), button("rpl_del", "RPL_DEL", !replays_.empty()),
                button("dlg_cancel", "dlg_can")};
    case Screen::ReplayName:
        return {Item{Kind::Text, "Description", {}, 0, true}, button("dlg_cancel", "dlg_can"),
                button("dlg_done", "dlg_done")};
    case Screen::None:
        return {};
    }
    list.insert(list.end(), bottom.begin(), bottom.end());
    return list;
}

void GameMenus::open(Screen s) {
    edit_ = currentOptions();
    entered_ = edit_;
    under_ = Screen::None;
    openedMs_ = nowMs_;
    go(s);
}

void GameMenus::go(Screen s) {
    if (s == Screen::ReplayList) {
        replays_ = listReplays(savePath_);
        replayRow_ = std::min(replayRow_, replays_.empty() ? 0 : replays_.size() - 1);
    }
    if (s == Screen::Defaults || s == Screen::DuplicateKey || s == Screen::Quit || s == Screen::ReplayName) {
        if (screen_ != Screen::Defaults && screen_ != Screen::DuplicateKey && screen_ != Screen::Quit &&
            screen_ != Screen::ReplayName)
            under_ = screen_;
    } else if (s != Screen::None && s != Screen::Options && screen_ == Screen::Options)
        entered_ = edit_; // what Cancel on the screen goes back to
    if (s == Screen::About)
        openedMs_ = nowMs_;
    screen_ = s;
    focus_ = 0;
    waitingForKey_ = false;
    const auto list = items();
    while (size_t(focus_) < list.size() && (!list[size_t(focus_)].enabled || list[size_t(focus_)].kind == Kind::Label))
        ++focus_;
    if (size_t(focus_) >= list.size())
        focus_ = 0;
    sys_.setTextInput(typing());
}

bool GameMenus::checked(const std::string& w) const {
    const GameOptions& o = edit_;
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
    if (w == "gfx_port")
        return o.smartRendering;
    if (w == "ctrl_aut")
        return o.autoReverse;
    if (w == "ctrl_pov")
        return o.startInCar;
    return false;
}

void GameMenus::toggle(const std::string& w) {
    GameOptions& o = edit_;
    if (w == "aud_fx")
        o.soundFx = !o.soundFx;
    else if (w == "aud_com")
        o.commentary = !o.commentary;
    else if (w == "aud_musc" || w == "aud_amb") {
        // "Check either Play Music or City Sounds" (jpg/aud_tmus): one or
        // the other, at Music/City Volume.
        bool& mine = w == "aud_musc" ? o.music : o.ambient;
        mine = !mine;
        if (mine)
            (w == "aud_musc" ? o.ambient : o.music) = false;
    } else if (w == "gfx_sky")
        o.texturedSky = !o.texturedSky;
    else if (w == "gfx_peds")
        o.pedestrians = !o.pedestrians;
    else if (w == "gfx_port")
        o.smartRendering = !o.smartRendering;
    else if (w == "ctrl_aut")
        o.autoReverse = !o.autoReverse;
    else if (w == "ctrl_pov")
        o.startInCar = !o.startInCar;
    applySoundOptions(edit_, mixer_); // heard at once
}

float GameMenus::share(const std::string& w) const {
    const GameOptions& o = edit_;
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

void GameMenus::setShare(const std::string& w, float v) {
    v = std::clamp(std::round(v * 20) / 20, 0.0f, 1.0f);
    GameOptions& o = edit_;
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
    applySoundOptions(edit_, mixer_);
}

std::string GameMenus::choiceText(const std::string& w) const {
    static const uint32_t kDetail[4] = {574, 575, 576, 577}; // Low, Medium, High, Very High
    static const char* const kDetailNames[4] = {"Low", "Medium", "High", "Very High"};
    if (w == "DEVICE")
        return "Default output";
    if (w == "STEREO")
        return strings_.get(327, "Stereo");
    if (w == "SOUND QUALITY")
        return strings_.get(576, "High");
    if (w == "DISPLAY")
        return edit_.fullScreen ? "Full Screen" : "Window";
    if (w == "RENDERER")
        return "Reluva 3D";
    if (w == "RESOLUTION") {
        const Resolution& r =
            resolutions()[size_t(std::clamp<int>(edit_.resolution, 0, int(resolutions().size()) - 1))];
        return std::to_string(r.w) + " x " + std::to_string(r.h);
    }
    if (w == "TEXTURE QUALITY")
        return strings_.get(576, "High");
    if (w == "OBJECT DETAIL")
        return strings_.get(kDetail[edit_.objectDetail & 3], kDetailNames[edit_.objectDetail & 3]);
    if (w == "CLOUD SHADOWS")
        return strings_.get(660, "None");
    if (w == "CONTROLLERS")
        return strings_.get(581, "Keyboard");
    return {};
}

void GameMenus::step(const Item& item, int dir) {
    const std::string& w = item.widget;
    if (item.kind == Kind::Bar)
        setShare(w, share(w) + 0.05f * float(dir));
    else if (w == "DISPLAY")
        edit_.fullScreen = !edit_.fullScreen;
    else if (w == "RESOLUTION")
        edit_.resolution = (edit_.resolution + dir + int(resolutions().size())) % int(resolutions().size());
    else if (w == "OBJECT DETAIL")
        edit_.objectDetail = std::clamp(edit_.objectDetail + dir, 0, 3);
    else if (item.kind == Kind::List)
        column_ = dir > 0 ? 1 : 0;
}

std::string GameMenus::description(const std::string& w) const {
    static const std::pair<const char*, const char*> kPictures[] = {
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
        return screen_ == Screen::Audio ? "opt_tadf" : "opt_tdef";
    for (const auto& [name, picture] : kPictures)
        if (w == name)
            return picture;
    return {};
}

GameMenus::Action GameMenus::done() {
    const GameOptions before = currentOptions();
    setCurrentOptions(edit_);
    applySoundOptions(edit_, mixer_);
    if (before.resolution != edit_.resolution || before.fullScreen != edit_.fullScreen) {
        const Resolution& r = resolutions()[size_t(edit_.resolution)];
        sys_.setVideoMode(r.w, r.h, edit_.fullScreen);
    }
    edit_.save(savePath_);
    entered_ = edit_;
    go(Screen::Options);
    return Action::None;
}

GameMenus::Action GameMenus::cancel() {
    edit_ = entered_;
    applySoundOptions(currentOptions(), mixer_);
    go(Screen::Options);
    return Action::None;
}

GameMenus::Action GameMenus::activate(const Item& item) {
    if (!item.enabled)
        return Action::None;
    const std::string& w = item.widget;
    if (item.kind == Kind::Check) {
        toggle(w);
        return Action::None;
    }
    if (item.kind == Kind::List && screen_ == Screen::ReplayList)
        return activate(Item{Kind::Button, "rpl_load", {}, 4, true});
    if (item.kind == Kind::Text)
        return activate(Item{Kind::Button, "dlg_done", {}, 4, true});
    if (item.kind == Kind::List) {
        waitingForKey_ = true;
        sys_.setTextInput(false);
        return Action::None;
    }
    if (w == "opt_aud")
        go(Screen::Audio);
    else if (w == "opt_gfx")
        go(Screen::Graphics);
    else if (w == "opt_ctl")
        go(Screen::Control);
    else if (w == "opt_abt")
        go(Screen::About);
    else if (w == "ctrl_cus") {
        row_ = column_ = 0;
        go(Screen::Customize);
    } else if (w == "mnav_prev") {
        screen_ = Screen::None;
        return Action::Closed;
    } else if (w == "options_done")
        go(Screen::Options);
    else if (w == "opt_dn")
        return done();
    else if (w == "opt_can")
        return cancel();
    else if (w == "opt_def")
        go(Screen::Defaults);
    else if (w == "dlg_ok" && screen_ == Screen::Defaults) {
        // "Return all ... to their original, default settings."
        if (under_ == Screen::Audio)
            edit_.resetAudio();
        else if (under_ == Screen::Graphics)
            edit_.resetGraphics();
        else
            edit_.resetControls();
        applySoundOptions(edit_, mixer_);
        go(under_);
    } else if (w == "dlg_can" || (w == "dlg_ok" && screen_ == Screen::DuplicateKey))
        go(under_);
    else if (w == "popup_ok" && screen_ == Screen::ReplayAsk) {
        under_ = Screen::None;
        go(Screen::ReplayName); // save as
    } else if (w == "popup_ok") {
        screen_ = Screen::None;
        return Action::Quit;
    } else if (w == "popup_cancel") {
        screen_ = Screen::None;
        return Action::Closed;
    } else if (w == "rpl_load" && replayRow_ < replays_.size()) {
        replayFile_ = replays_[replayRow_].file;
        screen_ = Screen::None;
        return Action::LoadReplay;
    } else if (w == "rpl_edit" && replayRow_ < replays_.size()) {
        name_ = replays_[replayRow_].name;
        go(Screen::ReplayName);
    } else if (w == "rpl_del" && replayRow_ < replays_.size()) {
        deleteReplay(replays_[replayRow_]);
        go(Screen::ReplayList);
    } else if (w == "dlg_cancel" && screen_ == Screen::ReplayList) {
        screen_ = Screen::None;
        return Action::Closed;
    } else if (w == "dlg_cancel" && screen_ == Screen::ReplayName) {
        if (under_ == Screen::ReplayList)
            go(Screen::ReplayList);
        else {
            screen_ = Screen::None;
            return Action::Closed;
        }
    } else if (w == "dlg_done" && screen_ == Screen::ReplayName && !name_.empty()) {
        if (under_ == Screen::ReplayList) {
            if (replayRow_ < replays_.size())
                renameReplay(savePath_, replays_[replayRow_], name_);
            go(Screen::ReplayList);
        } else {
            screen_ = Screen::None;
            sys_.setTextInput(false);
            return Action::SaveReplay;
        }
    }
    return Action::None;
}

bool GameMenus::hit(const Item& item, int x, int y, int& w, int& h) const {
    const Widget* wd = widget(item.widget);
    if (!wd)
        return false;
    w = wd->w;
    h = wd->h;
    if (item.kind == Kind::Button || item.kind == Kind::Check)
        if (const image::Image* im = pictures_.texture(item.picture)) {
            w = im->w;
            h = im->h / std::max(1, item.states);
        }
    if (item.kind == Kind::List)
        h = kListRow * kControls;
    const int x0 = wd->x + originX(), y0 = wd->y + originY();
    return x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
}

GameMenus::Action GameMenus::handle(const Event& ev) {
    if (screen_ == Screen::None)
        return Action::None;
    const auto list = items();
    if (list.empty())
        return Action::None;
    focus_ = std::clamp(focus_, 0, int(list.size()) - 1);
    const Item& item = list[size_t(focus_)];
    // Customize Controls, waiting for the key to give a control.
    if (waitingForKey_) {
        if (ev.type != EventType::KeyDown)
            return Action::None;
        waitingForKey_ = false;
        if (ev.key == key::Escape)
            return Action::None;
        const Control c = Control(row_);
        uint32_t& slot = edit_.keys[size_t(row_)][size_t(column_)];
        if (ev.key == key::Backspace || ev.key == key::Delete) {
            slot = 0;
            return Action::None;
        }
        if (edit_.otherWith(ev.key, c) != Control::Count || edit_.keys[size_t(row_)][size_t(1 - column_)] == ev.key) {
            go(Screen::DuplicateKey); // "Duplicate key or button, please make a different selection"
            return Action::None;
        }
        slot = ev.key;
        return Action::None;
    }
    if (screen_ == Screen::ReplayName) {
        if (ev.type == EventType::TextInput) {
            for (const char* c = ev.text; *c; ++c)
                if (static_cast<unsigned char>(*c) >= 32 && name_.size() < 40)
                    name_.push_back(*c);
            return Action::None;
        }
        if (ev.type == EventType::KeyDown && ev.key == key::Backspace) {
            if (!name_.empty())
                name_.pop_back();
            return Action::None;
        }
        if (ev.type == EventType::KeyDown && ev.key == key::Space)
            return Action::None; // typed, as text
    }
    if (ev.type == EventType::MouseDown && ev.button == MouseButton::Left) {
        for (size_t i = 0; i < list.size(); ++i) {
            int w = 0, h = 0;
            if (!list[i].enabled || !hit(list[i], ev.mouseX, ev.mouseY, w, h))
                continue;
            focus_ = int(i);
            const Widget* wd = widget(list[i].widget);
            switch (list[i].kind) {
            case Kind::Bar:
                setShare(list[i].widget, float(ev.mouseX - (wd->x + originX() + kBarInset)) / kBarWidth);
                return Action::None;
            case Kind::Choice:
                step(list[i], ev.mouseX < wd->x + originX() + wd->w / 2 ? -1 : 1);
                return Action::None;
            case Kind::List:
                row_ = std::clamp((ev.mouseY - (wd->y + originY())) / kListRow, 0, kControls - 1);
                column_ = ev.mouseX >= wd->x + kSecondColumn ? 1 : 0;
                return activate(list[i]);
            default:
                return activate(list[i]);
            }
        }
        return Action::None;
    }
    if (ev.type != EventType::KeyDown)
        return Action::None;
    switch (ev.key) {
    case key::Up:
    case key::Down: {
        const int dir = ev.key == key::Down ? 1 : -1;
        if (item.kind == Kind::List && screen_ == Screen::ReplayList) {
            if (int(replayRow_) + dir >= 0 && size_t(int(replayRow_) + dir) < replays_.size()) {
                replayRow_ = size_t(int(replayRow_) + dir);
                break;
            }
        } else if (item.kind == Kind::List && row_ + dir >= 0 && row_ + dir < kControls) {
            row_ += dir;
            break;
        }
        for (size_t tries = 0; tries < list.size(); ++tries) {
            focus_ = (focus_ + dir + int(list.size())) % int(list.size());
            const Item& next = list[size_t(focus_)];
            if (next.enabled && next.kind != Kind::Label)
                break;
        }
        break;
    }
    case key::Left:
    case key::Right:
        if (item.enabled && item.kind != Kind::Button && item.kind != Kind::Check)
            step(item, ev.key == key::Right ? 1 : -1);
        break;
    case key::Return:
    case key::KpEnter:
    case key::Space:
        return activate(item);
    case key::Escape:
        switch (screen_) {
        case Screen::Options:
        case Screen::Quit:
        case Screen::ReplayAsk:
        case Screen::ReplayList:
            screen_ = Screen::None;
            return Action::Closed;
        case Screen::ReplayName:
            return activate(Item{Kind::Button, "dlg_cancel", {}, 4, true});
        case Screen::About:
            go(Screen::Options);
            break;
        case Screen::Defaults:
        case Screen::DuplicateKey:
            go(under_);
            break;
        case Screen::Customize:
            go(Screen::Control);
            break;
        default:
            return cancel();
        }
        break;
    default:
        break;
    }
    return Action::None;
}

void GameMenus::text(Surface& o, const std::string& name, const std::string& s, Color c) const {
    if (const Widget* w = widget(name))
        font::drawText(o, w->x + originX() + 6,
                       w->y + originY() + std::max(0, (std::min(w->h, 24) - font::kLineHeight) / 2), s, c);
}

void GameMenus::drawItems(Surface& o, const std::vector<Item>& list, int focus) const {
    for (size_t i = 0; i < list.size(); ++i) {
        const Item& item = list[i];
        const Widget* w = widget(item.widget);
        if (!w)
            continue;
        const bool focused = int(i) == focus;
        const int x = w->x + originX(), y = w->y + originY();
        const Color words = !item.enabled ? Color{130, 130, 150}
                            : focused     ? Color{255, 255, 120}
                                          : Color{255, 230, 140};
        switch (item.kind) {
        case Kind::Button:
            if (const image::Image* im = pictures_.texture(item.picture))
                drawButtonState(o, *im, x, y, item.states, !item.enabled ? item.states - 1 : focused ? 1 : 0);
            break;
        case Kind::Check:
            // Its five states: clear, clear and pointed at, ticked, ticked
            // and pointed at, not available.
            if (const image::Image* im = pictures_.texture(item.picture))
                drawButtonState(o, *im, x, y, 5,
                                !item.enabled ? 4 : (checked(item.widget) ? 2 : 0) + (focused ? 1 : 0));
            break;
        case Kind::Bar: {
            const int lit = int(std::lround(share(item.widget) * kBarWidth));
            if (item.widget == "BALANCE")
                fill(o, x + kBarInset + std::min(lit, kBarWidth - 4), y + 6, 4, w->h - 12, {255, 200, 40});
            else if (item.enabled)
                fill(o, x + kBarInset, y + 6, lit, w->h - 12, {255, 200, 40});
            if (focused)
                font::drawText(o, x + 2, y + 8, ">", {255, 255, 120});
            break;
        }
        case Kind::Choice:
            text(o, item.widget, choiceText(item.widget), words);
            if (focused)
                font::drawText(o, x - 10, y + 4, ">", {255, 255, 120});
            break;
        case Kind::Text:
            // The end of it, as much as the box holds.
            text(o, item.widget,
                 (name_.size() > 36 ? name_.substr(name_.size() - 36) : name_) + ((nowMs_ / 400) % 2 ? "_" : ""),
                 {255, 255, 255});
            break;
        case Kind::List:
        case Kind::Label:
            break;
        }
    }
}

void GameMenus::drawScreenWords(Surface& o, uint64_t nowMs) const {
    if (screen_ == Screen::Customize) {
        // Each control, its two keys; the chosen one waiting for a key.
        const Widget* w = widget("CW Array");
        if (!w)
            return;
        font::drawText(o, 432, 140, "Enter: change it", {255, 255, 255});
        font::drawText(o, 432, 154, "Left/Right: which key", {255, 255, 255});
        font::drawText(o, 432, 168, "Backspace: none", {255, 255, 255});
        font::drawText(o, 432, 182, "Esc: as it was", {255, 255, 255});
        for (int i = 0; i < kControls; ++i) {
            const int y = w->y + i * kListRow + 4;
            const bool row = i == row_;
            if (row)
                fill(o, w->x - 6, y - 3, 290, kListRow - 2, {40, 40, 110});
            font::drawText(o, w->x, y, strings_.get(controlStringId(Control(i)), controlFallbackName(Control(i))),
                           row ? Color{255, 255, 120} : Color{230, 230, 230});
            for (int k = 0; k < 2; ++k) {
                const bool waiting = row && waitingForKey_ && k == column_;
                std::string name =
                    waiting ? ((nowMs / 300) % 2 ? "Press a key" : "") : keyName(edit_.keys[size_t(i)][size_t(k)]);
                font::drawText(o, w->x + (k ? kSecondColumn : kKeyColumn) - 30, y, name,
                               row && k == column_ ? Color{120, 255, 120} : Color{200, 200, 255});
            }
        }
    } else if (screen_ == Screen::About) {
        // The credits (jpg/credits, 215 x 4,582) scrolling through their box.
        if (const Widget* w = widget("Credits"))
            if (const image::Image* credits = pictures_.jpg("credits")) {
                const int top = int(float(nowMs - openedMs_) / 1000 * kCreditsSpeed) - w->h;
                drawBand(o, *credits, top, w->x, w->y, w->w, w->h);
            }
        text(o, "PID Label", "None: this is Reluva", {255, 255, 255});
    } else if (screen_ == Screen::DuplicateKey) {
        // ctrl_dlg says it all.
    }
}

void GameMenus::drawReplayList(Surface& o) const {
    // The replays saved: their names, city and length; the chosen one lit.
    const Widget* w = widget("CITY");
    if (!w)
        return;
    const int x = w->x + originX() - 8, y0 = w->y + originY() + 10;
    if (replays_.empty())
        font::drawText(o, x + 4, y0, "No replays saved yet", {200, 200, 200});
    const size_t first = replayRow_ >= size_t(kReplayRows) ? replayRow_ - size_t(kReplayRows) + 1 : 0;
    for (size_t i = first; i < replays_.size() && i < first + size_t(kReplayRows); ++i) {
        const ReplayEntry& r = replays_[i];
        const int y = y0 + int(i - first) * kReplayRow;
        if (i == replayRow_)
            fill(o, x, y - 2, 258, kReplayRow, {50, 50, 130});
        char line[96];
        std::snprintf(line, sizeof line, "%-24.24s %-6s %d:%02d", r.name.c_str(), r.city.c_str(), int(r.duration) / 60,
                      int(r.duration) % 60);
        font::drawText(o, x + 4, y, line, i == replayRow_ ? Color{255, 255, 120} : Color{230, 230, 230});
    }
}

void GameMenus::draw(Surface& o, uint64_t nowMs) {
    nowMs_ = nowMs;
    if (screen_ == Screen::None)
        return;
    if (overlay(screen_)) {
        // Dialogs over whatever's beneath: the rename over the list.
        const Screen was = screen_;
        const int keep = focus_;
        if (was == Screen::ReplayName && under_ == Screen::ReplayList) {
            screen_ = Screen::ReplayList;
            focus_ = -1;
            if (const image::Image* d = pictures_.jpg("rpl_bk"))
                drawPicture(o, *d, originX(), originY());
            drawItems(o, items(), -1);
            drawReplayList(o);
            screen_ = was;
            focus_ = keep;
        }
        const char* picture = was == Screen::ReplayList   ? "rpl_bk"
                              : was == Screen::ReplayName ? "REDIT_BK"
                              : was == Screen::ReplayAsk  ? "msg_dlg"
                                                          : "quit_dlg";
        if (const image::Image* d = pictures_.jpg(picture))
            drawPicture(o, *d, originX(), originY());
        if (was == Screen::ReplayAsk) {
            // "REPLAY SAVE MENU: Do you want to save this replay?" (the
            // string lacks its D).
            std::string ask = strings_.get(503, "Do you want to save this replay?");
            if (ask.rfind("o ", 0) == 0)
                ask = "D" + ask;
            font::drawText(o, originX() + 80, originY() + 14, ask, {255, 255, 255});
        }
        if (was == Screen::ReplayList)
            drawReplayList(o);
        drawItems(o, items(), focus_);
        return;
    }
    const bool dialog = screen_ == Screen::Defaults || screen_ == Screen::DuplicateKey || screen_ == Screen::Quit;
    const Screen was = screen_;
    const int keepFocus = focus_;
    auto background = [](Screen s) -> const char* {
        switch (s) {
        case Screen::Options:
            return "opt_bk";
        case Screen::Audio:
            return "aud_bk";
        case Screen::Graphics:
            return "gfx_bk";
        case Screen::Control:
            return "ctrl_bk";
        case Screen::Customize:
            return "cuss_bk";
        case Screen::About:
            return "about_bk";
        default:
            return "main_bk";
        }
    };
    // The screen, or the one a dialog is over (without its focus).
    const Screen base = dialog ? (under_ == Screen::None ? Screen::Options : under_) : screen_;
    if (screen_ == Screen::Quit && under_ == Screen::None) {
        // Over whatever menu asked (drawn already).
    } else {
        std::fill(o.pixels.begin(), o.pixels.end(), 0);
        if (const image::Image* bg = pictures_.jpg(background(base)))
            drawPicture(o, *bg, 0, 0, o.w, o.h);
        screen_ = base;
        focus_ = dialog ? -1 : keepFocus;
        drawItems(o, items(), focus_);
        drawScreenWords(o, nowMs);
        // The focused control's description, bottom left.
        if (!dialog) {
            const auto list = items();
            if (size_t(focus_) < list.size())
                if (const image::Image* d = pictures_.jpg(description(list[size_t(focus_)].widget)))
                    drawPicture(o, *d, 40, 396);
        }
        screen_ = was;
        focus_ = keepFocus;
    }
    if (dialog) {
        const char* picture = screen_ == Screen::Defaults       ? "odef_dlg"
                              : screen_ == Screen::DuplicateKey ? "ctrl_dlg"
                                                                : "quit_dlg";
        if (const image::Image* d = pictures_.jpg(picture))
            drawPicture(o, *d, originX(), originY());
        drawItems(o, items(), focus_);
    }
    font::drawText(o, 4, o.h - 12, "Up/Down, Left/Right, Enter, Esc", {200, 200, 200});
}

} // namespace reluva::age
