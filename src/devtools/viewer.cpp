// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-viewer: the engines' data viewers, for working on the engines.
// Players get reluva alone, which has none of this.
//
//   reluva-viewer                 the launcher's list of games, each opened
//                                 in its engine's viewer
//   reluva-viewer <target>        a game added in the launcher, by its name
//   reluva-viewer <folder>        the game in a folder, without adding it
//   reluva-viewer testbed         the backend's test pattern (no game)
//   reluva-viewer --list          the engines that have a viewer

#include "backends/graphics/sdl3/sdl_compute.h"
#include "backends/platform/sdl3/sdl_system.h"
#include "common/config.h"
#include "common/log.h"
#include "common/settings.h"
#include "engines/registry.h"
#include "engines/viewer_registry.h"
#include "gui/launcher.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

using namespace reluva;

namespace {

// The launcher's folder, so both programs share the list of games.
std::filesystem::path dataDir() {
    std::filesystem::path dir = ".";
    if (char *pref = SDL_GetPrefPath("reluva", "reluva")) {
        dir = pref;
        SDL_free(pref);
    }
    return dir;
}

int listViewers() {
    for (const MetaViewer *v : allViewers()) {
        const MetaEngine *e = findEngine(v->engineId());
        std::printf("%-16s %s\n", v->engineId(), e ? e->name() : "");
    }
    return 0;
}

// What the command line names: a game added in the launcher, the testbed,
// or a folder holding a game (the first match, as `reluva --detect` lists
// them).
std::optional<GameTarget> targetFromArgument(const Config &config, const std::string &name) {
    if (name == "testbed")
        return GameTarget{"testbed", "testbed", "testbed", "Testbed", {}, {}, config.settingsFor("testbed")};
    if (config.hasSection(name) && name != Config::kGlobalSection && !Config::isEngineSection(name))
        return GameTarget{name,
                          config.get(name, "engine"),
                          config.get(name, "gameid"),
                          config.get(name, "description", name),
                          config.get(name, "path"),
                          {},
                          config.settingsFor(name)};
    std::error_code ec;
    if (std::filesystem::is_directory(name, ec))
        for (const DetectedGame &g : detectGames(name)) {
            // Not added in the launcher: its screenshots go in a folder named
            // after the engine and the game.
            const std::string section = "viewer-" + g.engineId + "-" + g.gameId;
            return GameTarget{
                section, g.engineId, g.gameId, g.description, name, {}, config.settingsFor(Config::kGlobalSection)};
        }
    return std::nullopt;
}

} // namespace

int main(int argc, char **argv) {
    reluva::gfx::setComputeFactory(reluva::makeSdlCompute);
    if (argc >= 2 && std::strcmp(argv[1], "--list") == 0)
        return listViewers();

    StderrLog log(dataDir() / "reluva-viewer.log");
    SDL_SetLogOutputFunction(
        [](void *, int, SDL_LogPriority, const char *message) { std::fprintf(stderr, "%s\n", message); }, nullptr);

    SdlSystem system;
    if (Error err = system.init()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Reluva viewer", err.message.c_str(), nullptr);
        SDL_Log("%s", err.message.c_str());
        return 1;
    }

    Config config;
    config.load(dataDir() / "reluva.ini");

    std::optional<GameTarget> direct;
    if (argc >= 2) {
        direct = targetFromArgument(config, argv[1]);
        if (!direct) {
            std::fprintf(stderr, "'%s' is neither a game added in the launcher nor a folder with a game\n", argv[1]);
            return 1;
        }
    }

    Launcher launcher(system, config, LauncherPurpose::kView);
    launcher.setLibraryDir(dataDir() / "library");
    while (auto target = direct ? direct : launcher.run()) {
        // The testbed is an engine of its own, with nothing to view.
        const MetaViewer *viewer = findViewer(target->engineId);
        const MetaEngine *testbed = target->engineId == "testbed" ? findEngine("testbed") : nullptr;
        if (!viewer && !testbed) {
            const std::string message =
                target->description + ": its engine (" + target->engineId + ") has no data viewer";
            if (direct) {
                std::fprintf(stderr, "%s\n", message.c_str());
                return 1;
            }
            launcher.setStatus(message);
            continue;
        }

        target->savePath = dataDir() / "games" / target->target;
        system.setDisplayOptions(settings::display(target->settings));
        SDL_Log("Viewing %s", target->description.c_str());
        auto engine = viewer ? viewer->create(system, *target) : testbed->create(system, *target);
        Error err = engine->run();
        engine.reset();

        if (direct) {
            if (err)
                std::fprintf(stderr, "%s\n", err.message.c_str());
            return err ? 1 : 0;
        }
        if (system.quitRequested())
            break;
        launcher.setStatus(err ? target->description + ": " + err.message : "Returned from " + target->description);
    }
    return 0;
}
