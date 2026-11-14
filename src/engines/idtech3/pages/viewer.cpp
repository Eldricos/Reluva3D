// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The id Tech 3 data viewer: the game's own pages (see client/page.h), and
// the developer tools, pages showing what the game's files hold (World,
// Models, Maps, Textures, Files), as a choice on the Start page. Once
// they're chosen, Ctrl+Tab steps through every page (Tab is the game's:
// its scores, its objectives); Esc goes back to the Start page, and Esc
// there leaves the viewer.
//
// World:    the map drawn by the GPU renderer. Left/Right pick a map; drag
//           to look, W/A/S/D to move, Space/C (or E/Q) up and down, Shift
//           to go faster, R back to the start point, I to show or hide
//           the info.
// Models:   every .md3, .mdr and Ghoul2 .glm; players (lower, upper and head
//           in one folder) put together. Left/Right step, PageUp/PageDown
//           jump 10; drag to turn, wheel or +/- to zoom, A to animate, ,/.
//           to step frames, [ and ] to step through a player's animations
//           (from its animation.cfg, legs and torso paired as the game plays
//           them; a Ghoul2 model's, from its skeleton's, on the whole of it),
//           T to place a player's torso on its legs' tag or as in its
//           file, R to reset the view, I to show or hide the info.
// Files:    Up/Down, PageUp/PageDown, Home/End scroll the search path's files.
// Textures: Left/Right step, PageUp/PageDown jump 10, Home/End.
// Text:     Raven's string packages (Jedi Outcast's) in the game's fonts:
//           Left/Right pick a package, Up/Down and PageUp/PageDown scroll,
//           L changes language, F font.
// Videos:   every video (Bink, RoQ) played with its sound: Left/Right pick
//           one, R plays it again.
// Maps:     Left/Right pick a map; drag (or arrow keys with Shift) to rotate,
//           wheel or +/- to zoom, W/A/S/D/Q/E to move, R to reset the view,
//           P to show or hide patches, I to show or hide the info.

#include "engines/idtech3/client/session.h"
#include "engines/idtech3/pages/file_viewer.h"
#include "engines/idtech3/pages/map_viewer.h"
#include "engines/idtech3/pages/model_viewer.h"
#include "engines/idtech3/pages/text_viewer.h"
#include "engines/idtech3/pages/texture_viewer.h"
#include "engines/idtech3/pages/video_viewer.h"
#include "engines/idtech3/pages/world_viewer.h"
#include "engines/viewer.h"

#include <memory>

namespace reluva {

namespace {

using namespace idtech3;

class ViewerSession : public Session {
public:
    using Session::Session;

protected:
    void addPages(PageContext &ctx, Pages &pages, std::vector<StartPage::Choice> &choices) override {
        _tools = false;
        _firstTool = pages.size();
        pages.push_back(std::make_unique<WorldViewer>(ctx));
        pages.push_back(std::make_unique<ModelViewer>(ctx));
        pages.push_back(std::make_unique<MapViewer>(ctx));
        pages.push_back(std::make_unique<TextureViewer>(ctx));
        pages.push_back(std::make_unique<TextViewer>(ctx));
        pages.push_back(std::make_unique<VideoViewer>(ctx));
        pages.push_back(std::make_unique<FileViewer>(ctx));
        for (size_t i = _firstTool; i < pages.size(); ++i)
            if (pages[i]->count() > 0) {
                choices.push_back({"Developer tools",
                                   "The game's maps, models, textures and files (Ctrl+Tab between them)",
                                   pages[i]->name()});
                break;
            }
    }

    std::optional<size_t> pageKey(const Event &ev, const Pages &pages, size_t page) override {
        if (!_tools || ev.type != EventType::kKeyDown || ev.key != key::kTab || !(ev.mods & keymod::kCtrl))
            return std::nullopt;
        size_t next = page;
        do
            next = next + 1 < pages.size() ? next + 1 : 1;
        while (pages[next]->count() == 0);
        return next;
    }

    // The tools are chosen on the Start page, and left by going back to it.
    void switched(size_t from, size_t to) override {
        if (from == 0 || to == 0)
            _tools = to >= _firstTool;
    }

private:
    size_t _firstTool = 0;
    bool _tools = false; // the developer tools chosen: Ctrl+Tab steps through every page
};

class IdTech3MetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "idtech3"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<ViewerSession>(system, target);
    }
};

} // namespace

const MetaViewer &idTech3MetaViewer() {
    static const IdTech3MetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
