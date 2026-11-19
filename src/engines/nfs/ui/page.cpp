// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/ui/page.h"

#include "common/settings.h"

#include <string>

namespace reluva::nfs {

Error runPages(System &sys, const GameTarget &target, const GameInfo &game, const MakePages &makePages, bool browse) {
    const GameFolders folders = findFolders(target.path, target.settings, game.executable);
    if (trackFolders(folders.install).empty())
        return Error::fail("No tracks (GameData/Tracks/trkNNN) found in " + folders.install.string());

    if (Error err = sys.initGraphics(kScreenWidth, kScreenHeight, PixelFormat::kRGBA32))
        return err;
    Surface screen;
    screen.create(kScreenWidth, kScreenHeight, PixelFormat::kRGBA32);

    // One mixer for the pages' sounds; without audio output, none. The
    // pages go before it. surround= in reluva.ini sends it through the
    // Pro Logic decoder model, for the game's Dolby Surround mode.
    audio::Mixer mixer(sys.audioRate());
    const auto surround = target.settings.find("surround");
    const bool audioOut = !audio::startMixerOutput(
        sys, mixer, audio::surroundFromSetting(surround == target.settings.end() ? std::string() : surround->second));
    mixer.setMasterVolume(settings::volume(target.settings, settings::kMasterVolume));
    struct AudioOff { // stops the output before the mixer goes
        System &sys;
        ~AudioOff() { sys.stopAudio(); }
    } audioOff{sys};
    auto pages = makePages(folders, audioOut ? &mixer : nullptr);
    if (pages.empty())
        return Error::fail(std::string("Nothing to show for ") + game.name);

    size_t page = 0;
    bool dirty = true;
    auto title = [&] {
        sys.setWindowTitle(std::string("Reluva - ") + game.name +
                           (browse ? std::string(" - ") + pages[page]->name() : std::string()));
    };
    auto show = [&](size_t next) -> Error {
        // Leaving a 3D page: its renderer goes, the framebuffer comes back.
        const bool was3D = pages[page]->uses3D();
        pages[page]->deactivate();
        if (was3D)
            if (Error err = sys.initGraphics(kScreenWidth, kScreenHeight, PixelFormat::kRGBA32))
                return err;
        page = next;
        title();
        pages[page]->activate(sys);
        dirty = true;
        return Error::success();
    };
    title();
    pages[page]->activate(sys);

    while (true) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) {
                pages[page]->deactivate();
                return Error::success();
            }
            if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && !pages[page]->capturesEscape()) {
                pages[page]->deactivate();
                return Error::success();
            }
            if (browse && ev.type == EventType::kKeyDown && ev.key == key::kTab) {
                const size_t step = (ev.mods & keymod::kShift) ? pages.size() - 1 : 1;
                if (Error err = show((page + step) % pages.size()))
                    return err;
                continue;
            }
            pages[page]->handleEvent(ev);
            dirty = true;
        }
        if (pages[page]->finished()) {
            pages[page]->deactivate();
            return Error::success();
        }
        if (!browse)
            if (Error err = pages[page]->failure()) {
                pages[page]->deactivate();
                return err;
            }
        if (pages[page]->uses3D()) {
            // Paced by presenting (vsync).
            pages[page]->update(sys.getMillis());
            continue;
        }
        if (pages[page]->tick(sys.getMillis()))
            dirty = true;
        if (dirty) {
            pages[page]->draw(screen);
            dirty = false;
        }
        sys.updateScreen(screen);
        sys.delayMillis(10);
    }
}

} // namespace reluva::nfs
