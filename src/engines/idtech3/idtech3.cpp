// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 engine: games built on the Quake III Arena engine, starting
// with Star Trek: Voyager - Elite Force.
//
// Sources: the file formats are documented in docs/formats/idtech3/, from
// the game's own data and from the Quake III Arena source code id Software
// released under the GPL (version 2 or later, so usable in this GPLv3
// project). See CONTRIBUTING.md: never use leaked or unlicensed code.
//
// For now the engine is a data viewer (see pages/page.h for the keys): it
// mounts the game's search path and shows its files, textures and maps.
// Ctrl+Tab switches pages (Tab itself belongs to the game: its scores, its
// objectives); Esc goes back to the launcher (on the Menu page Ctrl+Esc,
// as the menus take Esc).

#include "common/audio/mixer.h"
#include "common/fs.h"
#include "common/zip.h"
#include "engines/engine.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/pages/file_viewer.h"
#include "engines/idtech3/pages/map_viewer.h"
#include "engines/idtech3/pages/menu_viewer.h"
#include "engines/idtech3/pages/model_viewer.h"
#include "engines/idtech3/pages/play_viewer.h"
#include "engines/idtech3/pages/sp_play_viewer.h"
#include "engines/idtech3/pages/start_page.h"
#include "engines/idtech3/pages/texture_viewer.h"
#include "engines/idtech3/pages/world_viewer.h"

#include <memory>

namespace reluva {

namespace {

using namespace idtech3;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

class IdTech3Engine : public Engine {
public:
    IdTech3Engine(System& system, GameTarget target, const GameInfo& game)
        : sys_(system), target_(std::move(target)), game_(game) {}

    Error run() override {
        std::vector<std::string> warnings;
        if (Error err = mountGameDir(files_, target_.path, game_.baseDir, 0, warnings))
            return err;
        // Paks downloaded from servers, in the save folder's game folder:
        // above the install's, as the original's home folder is.
        if (!target_.savePath.empty() && fs::findDirectory(target_.savePath, game_.baseDir))
            mountGameDir(files_, target_.savePath, game_.baseDir, 1, warnings);
        // Damaged archives or scripts don't stop the viewer; the Files
        // page lists what went wrong.
        shaders_.load(files_, &warnings);

        if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32))
            return err;
        screen_.create(kScreenW, kScreenH, PixelFormat::RGBA32);

        // One mixer for every page's sounds; without audio output, none.
        audio::Mixer mixer(sys_.audioRate());
        const bool audioOut = !audio::startMixerOutput(sys_, mixer);
        struct AudioOff { // stops the output before the mixer goes
            System& sys;
            ~AudioOff() { sys.stopAudio(); }
        } audioOff{sys_};

        ViewerContext ctx{sys_, screen_, files_, shaders_, game_, warnings, target_.savePath,
                          audioOut ? &mixer : nullptr, {}, {}, {}, {}, {}, {}, {}, target_.path, false, {}};
        // A pak just downloaded: in the search path at once, above the
        // rest, and the shader scripts read again (it may bring its own).
        int added = 0;
        ctx.addPak = [this, &added](const std::filesystem::path& path) {
            auto zip = std::make_unique<ZipArchive>();
            if (Error err = zip->open(path))
                return err;
            files_.add(std::move(zip), 2 * kPrioritiesPerGameDir + ++added);
            shaders_.load(files_, nullptr);
            return Error::success();
        };
        loadConfig(ctx);
        struct SaveConfig { // keeps settings changed while running, however it ends
            ViewerContext& ctx;
            ~SaveConfig() { saveConfig(ctx); }
        } saveOnExit{ctx};
        // The pages: the start (what to play), the game's own (single
        // player, Holomatch's menus and play), and the developer tools,
        // which Ctrl+Tab steps through once chosen.
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(nullptr); // the start, made once the rest are known
        pages.push_back(std::make_unique<SpPlayViewer>(ctx));
        pages.push_back(std::make_unique<MenuViewer>(ctx));
        pages.push_back(std::make_unique<PlayViewer>(ctx));
        const size_t firstTool = pages.size();
        pages.push_back(std::make_unique<WorldViewer>(ctx));
        pages.push_back(std::make_unique<ModelViewer>(ctx));
        pages.push_back(std::make_unique<MapViewer>(ctx));
        pages.push_back(std::make_unique<TextureViewer>(ctx));
        pages.push_back(std::make_unique<FileViewer>(ctx));
        std::vector<StartPage::Choice> choices;
        if (pages[1]->count() > 0)
            choices.push_back({"Single Player", "The story, from the game's own menus", pages[1]->name()});
        if (pages[2]->count() > 0)
            choices.push_back({"Holomatch", "Multiplayer: bots, LAN and Internet servers", pages[2]->name()});
        for (size_t i = firstTool; i < pages.size(); ++i)
            if (pages[i]->count() > 0) {
                choices.push_back({"Developer tools",
                                   "The game's maps, models, textures and files (Ctrl+Tab between them)",
                                   pages[i]->name()});
                break;
            }
        if (choices.empty())
            return Error::fail("No files found in " + target_.path.string() + "/" + game_.baseDir);
        pages[0] = std::make_unique<StartPage>(ctx, std::move(choices));
        size_t page = 0;
        bool tools = false; // the developer tools chosen: Ctrl+Tab steps through every page
        pages[page]->activate();
        // Leaving a 3D page: its renderer goes, the framebuffer comes back.
        auto switchTo = [&](size_t next) -> Error {
            const bool was3D = pages[page]->uses3D();
            pages[page]->deactivate();
            if (was3D)
                if (Error err = sys_.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32))
                    return err;
            page = next;
            pages[page]->activate();
            return Error::success();
        };

        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher)
                    return Error::success();
                const bool pageKey = pages[page]->takesKey(ev);
                if (ev.type == EventType::KeyDown && ev.key == key::Escape && !pageKey) {
                    // Back to the start; from there, to the launcher.
                    if (page == 0)
                        return Error::success();
                    tools = false;
                    if (Error err = switchTo(0))
                        return err;
                    continue;
                }
                if (tools && ev.type == EventType::KeyDown && ev.key == key::Tab && (ev.mods & keymod::Ctrl)) {
                    size_t next = page;
                    do
                        next = next + 1 < pages.size() ? next + 1 : 1;
                    while (pages[next]->count() == 0);
                    if (Error err = switchTo(next))
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
                        if (page == 0 || i == 0)
                            tools = i >= firstTool; // chosen at the start, or back to it
                        if (Error err = switchTo(i))
                            return err;
                        break;
                    }
            }
            pages[page]->update(sys_.getMillis());
            sys_.updateScreen(screen_);
            // 3D pages are paced by presenting (vsync); the rest needn't spin.
            if (!pages[page]->uses3D())
                sys_.delayMillis(10);
        }
    }

private:
    System& sys_;
    GameTarget target_;
    const GameInfo& game_;
    SearchSet files_;
    ShaderLibrary shaders_;
    Surface screen_;
};

class IdTech3MetaEngine : public MetaEngine {
public:
    const char* id() const override { return "idtech3"; }
    const char* name() const override { return "id Tech 3"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        auto found = detectFromTable(id(), detectionTable(), dir);
        if (found.empty())
            found = detectFromTable(id(), fallbackTable(), dir);
        // With the patch level and what's missing: "... (1.2)".
        for (DetectedGame& g : found)
            if (const GameInfo* game = findGame(g.gameId))
                g.description += " (" + describeRelease(dir, *game).summary + ")";
        return found;
    }

    std::vector<ImportPlan> planImport(const std::filesystem::path& source) const override {
        // The game's folder (baseEF) under the folder picked: an install
        // (its folder), the CD (SETUP/BaseEF), or the game folder itself;
        // the first that holds the game. What Reluva needs of it: the
        // game folder's paks, the players' saved games and recordings,
        // the CD key files; beside it, single player's game code.
        for (const GameInfo& game : games()) {
            std::vector<std::filesystem::path> roots{source};
            if (const auto setup = fs::findDirectory(source, "SETUP"))
                roots.push_back(*setup);
            if (fs::toLower(source.filename().string()) == fs::toLower(game.baseDir))
                roots.push_back(source.parent_path());
            for (const std::filesystem::path& root : roots) {
                const auto found = detect(root);
                const auto base = fs::findDirectory(root, game.baseDir);
                if (!base)
                    continue;
                for (const DetectedGame& g : found) {
                    if (g.gameId != game.gameId)
                        continue;
                    ImportPlan plan;
                    plan.game = g;
                    auto add = [&](const std::filesystem::path& from, const std::string& to) {
                        std::error_code ec;
                        const auto size = std::filesystem::file_size(from, ec);
                        if (!ec)
                            plan.files.push_back({from, to, size});
                    };
                    // Files in a folder with one of `extensions` (lower case).
                    auto addAll = [&](const std::filesystem::path& dir, const std::string& to,
                                      std::initializer_list<const char*> extensions) {
                        std::error_code ec;
                        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                            if (!it->is_regular_file(ec))
                                continue;
                            const std::string ext = fs::toLower(it->path().extension().string());
                            for (const char* e : extensions)
                                if (ext == e)
                                    add(it->path(), to + it->path().filename().string());
                        }
                    };
                    const std::string baseTo = game.baseDir + "/";
                    addAll(*base, baseTo, {".pk3", ".key"});
                    if (const auto saves = fs::findDirectory(*base, "saves"))
                        addAll(*saves, baseTo + "saves/", {".sav"});
                    if (const auto demos = fs::findDirectory(*base, "demos"))
                        addAll(*demos, baseTo + "demos/", {".efdemo", ".dm_24"});
                    const Release release = describeRelease(root, game);
                    for (const std::string& dll : {game.spGameDll, game.spUiDll})
                        if (!dll.empty())
                            if (const auto path = fs::findFile(root, dll))
                                add(*path, fs::toLower(dll));
                    plan.notes.push_back("Patch level " + release.patch +
                                         (release.patch == "1.0" ? " (as on the CD; online play needs 1.2)" : ""));
                    if (!release.singlePlayer)
                        plan.notes.push_back("No single player: " + game.spGameDll + " and " + game.spUiDll +
                                             " weren't found beside " + base->filename().string());
                    else if (!release.singlePlayerPatch.empty())
                        plan.notes.push_back("Single player " + release.singlePlayerPatch);
                    size_t paks = 0, saves = 0;
                    for (const ImportPlan::File& f : plan.files) {
                        paks += fs::toLower(f.to).size() > 4 && fs::toLower(f.to).substr(f.to.size() - 4) == ".pk3";
                        saves += f.to.find("/saves/") != std::string::npos;
                    }
                    plan.notes.push_back(std::to_string(paks) + " pak file" + (paks == 1 ? "" : "s") +
                                         (saves ? ", " + std::to_string(saves) + " saved game" + (saves == 1 ? "" : "s")
                                                : std::string()));
                    return {plan};
                }
            }
        }
        return {};
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        const GameInfo* game = findGame(target.gameId);
        if (!game)
            game = &games().front();
        return std::make_unique<IdTech3Engine>(system, target, *game);
    }
};

} // namespace

const MetaEngine& idTech3MetaEngine() {
    static const IdTech3MetaEngine instance;
    return instance;
}

} // namespace reluva
