// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/session.h"

#include "audio/mixer.h"
#include "common/settings.h"
#include "common/zip.h"
#include "engines/idtech3/client/menu_page.h"
#include "engines/idtech3/client/play_page.h"
#include "engines/idtech3/client/sp_play_page.h"
#include "engines/idtech3/filesystem.h"

#include <cstdio>

namespace reluva::idtech3 {

namespace {

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

const GameInfo &gameFor(const GameTarget &target) {
    const GameInfo *game = findGame(target.gameId);
    return game ? *game : games().front();
}

} // namespace

Session::Session(System &system, GameTarget target)
    : _sys(system), _target(std::move(target)), _game(gameFor(_target)) {
}

Error Session::run() {
    std::vector<std::string> warnings;
    // The Expansion Pack, unless this game's entry says to leave it out.
    const auto setting = _target.settings.find("expansion");
    const bool expansion = setting == _target.settings.end() || setting->second != "0";
    const std::vector<std::string> skip =
        expansion ? std::vector<std::string>{} : std::vector<std::string>{kExpansionPak};
    // The mod the entry plays (as the original's +set fs_game), if any.
    const auto modSetting = _target.settings.find("mod");
    std::string mod =
        modSetting == _target.settings.end() ? std::string() : modFolderName(modSetting->second, _game.baseDir);
    if (Error err = mountFiles(mod, skip, warnings))
        return err;

    if (Error err = _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
        return err;
    _screen.create(kScreenW, kScreenH, PixelFormat::kRGBA32);

    // One mixer for every page's sounds; without audio output, none.
    audio::Mixer mixer(_sys.audioRate());
    mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
    const bool audioOut = !audio::startMixerOutput(_sys, mixer);
    struct AudioOff { // stops the output before the mixer goes
        System &sys;
        ~AudioOff() { sys.stopAudio(); }
    } audioOff{_sys};

    PageContext ctx{_sys,
                    _screen,
                    _files,
                    _shaders,
                    _game,
                    warnings,
                    modSavePath(_target.savePath, mod),
                    audioOut ? &mixer : nullptr,
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    {},
                    _target.path,
                    false,
                    {},
                    singlePlayerDir(_target.path, _game, expansion),
                    mod,
                    _target.savePath};
    // A pak just downloaded: in the search path at once, above the
    // rest, and the shader scripts read again (it may bring its own).
    int added = 0;
    ctx.addPak = [this, &added](const std::filesystem::path &path) {
        auto zip = std::make_unique<ZipArchive>();
        if (Error err = zip->open(path))
            return err;
        _files.add(std::move(zip), kDownloadPriority + ++added);
        _shaders.load(_files, nullptr);
        return Error::success();
    };
    startSettings(ctx);
    struct SaveConfig { // keeps settings changed while running, however it ends
        PageContext &ctx;
        ~SaveConfig() { saveConfig(ctx); }
    } saveOnExit{ctx};

    // The pages, built again (from the start page, or the page that
    // asked) each time the game's files change: a mod chosen in the
    // menus.
    std::string resumePage;
    while (true) {
        std::string switchedFrom;
        if (Error err = runPages(ctx, resumePage, switchedFrom))
            return err;
        if (switchedFrom.empty())
            return Error::success();
        // fs_game changed and vid_restart run: the files mounted again
        // for the mod (or none), its own settings, and the menus over.
        const std::string next = modFolderName(ctx.cvars.string("fs_game"), _game.baseDir);
        saveConfig(ctx);
        warnings.clear();
        if (Error err = mountFiles(next, skip, warnings))
            return err;
        added = 0;
        ctx.gameDir = next;
        ctx.savePath = modSavePath(_target.savePath, next);
        ctx.cvars = CvarStore();
        ctx.bindings = KeyBindings();
        startSettings(ctx);
        resumePage = switchedFrom;
    }
}

void Session::addPages(PageContext & /*ctx*/, Pages & /*pages*/, std::vector<StartPage::Choice> & /*choices*/) {
}

std::optional<size_t> Session::pageKey(const Event & /*ev*/, const Pages & /*pages*/, size_t /*page*/) {
    return std::nullopt;
}

void Session::switched(size_t /*from*/, size_t /*to*/) {
}

// The game's files for a mod (or none), and their shader scripts.
// Damaged archives or scripts don't stop the game; they're kept in
// `warnings`.
Error Session::mountFiles(const std::string &mod, const std::vector<std::string> &skip,
                          std::vector<std::string> &warnings) {
    if (Error err = mountGame(_files, _target.path, _target.savePath, _game.baseDir, mod, warnings, skip))
        return err;
    std::printf("Game files: %s%s\n", _game.baseDir.c_str(), mod.empty() ? "" : (" and the mod " + mod).c_str());
    _shaders.load(_files, &warnings);
    return Error::success();
}

// The settings for the files mounted: the mod's own, or, until it has
// some, the game's; fs_game says which mod is played (set only from
// the command line, as the original's, but for the menus' Mods screens).
void Session::startSettings(PageContext &ctx) {
    const std::filesystem::path own = ctx.savePath;
    if (!ctx.gameDir.empty() && !std::filesystem::exists(own / "reluva.cfg"))
        ctx.savePath = ctx.homePath;
    loadConfig(ctx);
    ctx.savePath = own;
    ctx.cvars.create("fs_game", ctx.gameDir, CvarStore::kSystemInfo | CvarStore::kInit);
    ctx.cvars.set("fs_game", ctx.gameDir, true);
}

// The pages, from `resume` if it's one of the game's (else the start),
// until the player leaves (switchedFrom empty) or the game's files are to
// start again (switchedFrom: the page that asked).
Error Session::runPages(PageContext &ctx, const std::string &resume, std::string &switchedFrom) {
    // The pages: the start (what to play), then the game's own (single
    // player, Holomatch's menus and play).
    Pages pages;
    pages.push_back(nullptr); // the start, made once the rest are known
    pages.push_back(std::make_unique<SpPlayPage>(ctx));
    pages.push_back(std::make_unique<MenuPage>(ctx));
    pages.push_back(std::make_unique<PlayPage>(ctx));
    const size_t gamePages = pages.size();
    std::vector<StartPage::Choice> choices;
    if (pages[1]->count() > 0)
        choices.push_back({"Single Player", "The story, from the game's own menus", pages[1]->name()});
    if (pages[2]->count() > 0)
        choices.push_back({"Holomatch", "Multiplayer: bots, LAN and Internet servers", pages[2]->name()});
    addPages(ctx, pages, choices);
    if (choices.empty()) {
        const std::string folder = _target.path.string() + "/" + _game.baseDir;
        if (_files.list().empty())
            return Error::fail("No files found in " + folder);
        return Error::fail("Nothing to play in " + folder + ": neither single player (its game code, " +
                           _game.spGameDll + ", and its maps) nor Holomatch's menus (vm/ui.qvm) were found");
    }
    pages[0] = std::make_unique<StartPage>(ctx, std::move(choices));
    size_t page = 0;
    for (size_t i = 1; i < gamePages; ++i)
        if (!resume.empty() && pages[i]->name() == resume && pages[i]->count() > 0)
            page = i;
    pages[page]->activate();
    // Leaving a 3D page: its renderer goes, the framebuffer comes back.
    auto switchTo = [&](size_t next) -> Error {
        const bool was3D = pages[page]->uses3D();
        pages[page]->deactivate();
        if (was3D)
            if (Error err = _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
                return err;
        const size_t from = page;
        page = next;
        switched(from, page);
        pages[page]->activate();
        return Error::success();
    };
    // The page shown left, its renderer gone, before the pages go.
    auto leave = [&]() -> Error {
        const bool was3D = pages[page]->uses3D();
        pages[page]->deactivate();
        if (was3D)
            return _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32);
        return Error::success();
    };

    while (true) {
        Event ev;
        while (_sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return Error::success();
            const bool takenByPage = pages[page]->takesKey(ev);
            if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && !takenByPage) {
                // Back to the start; from there, to the launcher.
                if (page == 0)
                    return Error::success();
                if (Error err = switchTo(0))
                    return err;
                continue;
            }
            if (const std::optional<size_t> next = pageKey(ev, pages, page)) {
                if (Error err = switchTo(*next))
                    return err;
                continue;
            }
            pages[page]->handleEvent(ev);
        }
        // A page asking for another (the menus starting a level).
        if (!ctx.requestPage.empty()) {
            const std::string wanted = std::move(ctx.requestPage);
            ctx.requestPage.clear();
            for (size_t i = 0; i < pages.size(); ++i)
                if (wanted == pages[i]->name() && pages[i]->count() > 0 && i != page) {
                    if (Error err = switchTo(i))
                        return err;
                    break;
                }
        }
        pages[page]->update(_sys.getMillis());
        // vid_restart with a different fs_game: the files start again
        // (FS_ConditionalRestart in the original's).
        if (ctx.requestRestart) {
            ctx.requestRestart = false;
            if (modFolderName(ctx.cvars.string("fs_game"), _game.baseDir) != ctx.gameDir) {
                switchedFrom = pages[page]->name();
                return leave();
            }
        }
        _sys.updateScreen(_screen);
        // 3D pages are paced by presenting (vsync); the rest needn't spin.
        if (!pages[page]->uses3D())
            _sys.delayMillis(10);
    }
}

} // namespace reluva::idtech3
