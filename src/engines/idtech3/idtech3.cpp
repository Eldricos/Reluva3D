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

#include <cstdio>
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
        // The Expansion Pack, unless this game's entry says to leave it out.
        const auto setting = target_.settings.find("expansion");
        const bool expansion = setting == target_.settings.end() || setting->second != "0";
        const std::vector<std::string> skip =
            expansion ? std::vector<std::string>{} : std::vector<std::string>{kExpansionPak};
        // The mod the entry plays (as the original's +set fs_game), if any.
        const auto modSetting = target_.settings.find("mod");
        std::string mod = modSetting == target_.settings.end() ? std::string() : modFolderName(modSetting->second, game_.baseDir);
        if (Error err = mountFiles(mod, skip, warnings))
            return err;

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

        ViewerContext ctx{sys_, screen_, files_, shaders_, game_, warnings, modSavePath(target_.savePath, mod),
                          audioOut ? &mixer : nullptr, {}, {}, {}, {}, {}, {}, {}, target_.path, false, {},
                          singlePlayerDir(target_.path, game_, expansion), mod, target_.savePath};
        // A pak just downloaded: in the search path at once, above the
        // rest, and the shader scripts read again (it may bring its own).
        int added = 0;
        ctx.addPak = [this, &added](const std::filesystem::path& path) {
            auto zip = std::make_unique<ZipArchive>();
            if (Error err = zip->open(path))
                return err;
            files_.add(std::move(zip), kDownloadPriority + ++added);
            shaders_.load(files_, nullptr);
            return Error::success();
        };
        startSettings(ctx);
        struct SaveConfig { // keeps settings changed while running, however it ends
            ViewerContext& ctx;
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
            const std::string next = modFolderName(ctx.cvars.string("fs_game"), game_.baseDir);
            saveConfig(ctx);
            warnings.clear();
            if (Error err = mountFiles(next, skip, warnings))
                return err;
            added = 0;
            ctx.gameDir = next;
            ctx.savePath = modSavePath(target_.savePath, next);
            ctx.cvars = CvarStore();
            ctx.bindings = KeyBindings();
            startSettings(ctx);
            resumePage = switchedFrom;
        }
    }

private:
    // The game's files for a mod (or none), and their shader scripts.
    // Damaged archives or scripts don't stop the viewer; the Files page
    // lists what went wrong.
    Error mountFiles(const std::string& mod, const std::vector<std::string>& skip, std::vector<std::string>& warnings) {
        if (Error err = mountGame(files_, target_.path, target_.savePath, game_.baseDir, mod, warnings, skip))
            return err;
        std::printf("Game files: %s%s\n", game_.baseDir.c_str(), mod.empty() ? "" : (" and the mod " + mod).c_str());
        shaders_.load(files_, &warnings);
        return Error::success();
    }

    // The settings for the files mounted: the mod's own, or, until it has
    // some, the game's; fs_game says which mod is played (set only from
    // the command line, as the original's, but for the menus' Mods screens).
    void startSettings(ViewerContext& ctx) {
        const std::filesystem::path own = ctx.savePath;
        if (!ctx.gameDir.empty() && !std::filesystem::exists(own / "reluva.cfg"))
            ctx.savePath = ctx.homePath;
        loadConfig(ctx);
        ctx.savePath = own;
        ctx.cvars.create("fs_game", ctx.gameDir, CvarStore::SystemInfo | CvarStore::Init);
        ctx.cvars.set("fs_game", ctx.gameDir, true);
    }

    // The pages, from `resume` if it's one of them (else the start), until
    // the player leaves (switchedFrom empty) or the game's files are to
    // start again (switchedFrom: the page that asked).
    Error runPages(ViewerContext& ctx, const std::string& resume, std::string& switchedFrom) {
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
        for (size_t i = 1; i < firstTool; ++i)
            if (!resume.empty() && pages[i]->name() == resume && pages[i]->count() > 0)
                page = i;
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
        // The page shown left, its renderer gone, before the pages go.
        auto leave = [&]() -> Error {
            const bool was3D = pages[page]->uses3D();
            pages[page]->deactivate();
            if (was3D)
                return sys_.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32);
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
            // vid_restart with a different fs_game: the files start again
            // (FS_ConditionalRestart in the original's).
            if (ctx.requestRestart) {
                ctx.requestRestart = false;
                if (modFolderName(ctx.cvars.string("fs_game"), game_.baseDir) != ctx.gameDir) {
                    switchedFrom = pages[page]->name();
                    return leave();
                }
            }
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

// How many paks and saved games an import brings.
std::string countNote(const ImportPlan& plan) {
    size_t paks = 0, saves = 0;
    for (const ImportPlan::File& f : plan.files) {
        const std::string to = fs::toLower(f.to);
        paks += to.size() > 4 && to.compare(to.size() - 4, 4, ".pk3") == 0;
        saves += to.find("/saves/") != std::string::npos;
    }
    return std::to_string(paks) + " pak file" + (paks == 1 ? "" : "s") +
           (saves ? ", " + std::to_string(saves) + " saved game" + (saves == 1 ? "" : "s") : std::string());
}

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

    std::vector<GameOption> options(const std::string& gameId, const std::filesystem::path& dir) const override {
        const GameInfo* game = findGame(gameId);
        if (!game)
            return {};
        std::vector<GameOption> out;
        // With the Expansion Pack there: whether to use it (an entry each,
        // the same folder added twice, gives both versions).
        if (describeRelease(dir, *game).expansion)
            out.push_back({"expansion", "Use the Expansion Pack",
                           "Its maps, Virtual Voyager and holodeck missions, and its menus' options.\n"
                           "Off: the game as patch 1.2 left it, for matches with players without it.",
                           true,
                           {}});
        // The mods beside the game's folder: which to play, as the
        // original's +set fs_game (the menus' Mods screens change it too).
        const std::vector<ModFolder> mods = listMods(dir, game->baseDir);
        if (!mods.empty()) {
            GameOption mod{"mod", "Mod",
                           "A mod in the game's folder to play: its files over the game's, its own saved games\n"
                           "and settings. The Mods screens in the game's menus change it while it runs.",
                           true,
                           {{"", "None"}}};
            for (const ModFolder& m : mods) {
                // Its description on one line, if it says more than its name.
                std::string text;
                for (const char c : m.description)
                    text += static_cast<unsigned char>(c) < 32 ? ' ' : c;
                while (!text.empty() && text.back() == ' ')
                    text.pop_back();
                mod.choices.emplace_back(m.name, text.empty() || text == m.name ? m.name : m.name + " - " + text);
            }
            out.push_back(std::move(mod));
        }
        return out;
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
                            plan.files.push_back({from, to, size, std::nullopt});
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
                    // Single player's code; the Expansion Pack's kept apart
                    // (expansion/), so the game can be run without it too.
                    for (const std::string& dll : {game.spGameDll, game.spUiDll})
                        if (!dll.empty())
                            if (const auto path = fs::findFile(root, dll))
                                add(*path, (isExpansionDll(*path) ? "expansion/" : "") + fs::toLower(dll));
                    plan.notes.push_back("Patch level " + release.patch +
                                         (release.patch == "1.0" ? " (as on the CD; online play needs 1.2)" : ""));
                    if (!release.singlePlayer && release.expansionSinglePlayer)
                        plan.notes.push_back("Single player only with the Expansion Pack (the 1.x code isn't there: "
                                             "add patch 1.2 to have it without)");
                    else if (!release.singlePlayer)
                        plan.notes.push_back("No single player: " + game.spGameDll + " and " + game.spUiDll +
                                             " weren't found beside " + base->filename().string());
                    else if (!release.singlePlayerPatch.empty())
                        plan.notes.push_back("Single player " + release.singlePlayerPatch);
                    plan.notes.push_back(countNote(plan));
                    // A patch beside it (eliteforcepatch1_2.exe in the
                    // folder picked) is applied as it's copied.
                    std::error_code ec;
                    for (std::filesystem::directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
                        std::string ignored;
                        if (it->is_regular_file(ec) && fs::toLower(it->path().extension().string()) == ".exe")
                            addToImport(plan, it->path(), ignored);
                    }
                    return {plan};
                }
            }
        }
        return {};
    }

    bool addToImport(ImportPlan& plan, const std::filesystem::path& file, std::string& message) const override {
        const GameInfo* game = findGame(plan.game.gameId);
        std::error_code ec;
        if (game && std::filesystem::is_directory(file, ec))
            return addExpansion(plan, *game, file, message);
        // A patch's installer: the files it holds that Reluva uses, told
        // by their signatures, in place of the plan's.
        const auto& wanted = game ? patchFiles(*game) : std::vector<PatchFile>{};
        if (wanted.empty()) {
            message = "Reluva doesn't know this game's patches";
            return false;
        }
        std::vector<WiseStream> streams;
        if (Error err = readWiseInstaller(file, streams)) {
            message = err.message;
            return false;
        }
        std::string patch;
        int found = 0;
        for (const WiseStream& s : streams)
            for (const PatchFile& f : wanted) {
                if (int64_t(s.size) != f.size || s.headMd5 != f.md5)
                    continue;
                std::erase_if(plan.files,
                              [&](const ImportPlan::File& p) { return fs::toLower(p.to) == fs::toLower(f.to); });
                plan.files.push_back({file, f.to, s.size, s});
                ++found;
                if (std::string(f.patch) > patch)
                    patch = f.patch;
            }
        if (patch.empty()) {
            message = file.filename().string() + " isn't a patch Reluva knows for " + game->name;
            return false;
        }
        // The plan now gives that patch level (and single player, which
        // the patch brings).
        const bool expansion = plan.game.description.find("Expansion Pack") != std::string::npos;
        if (const size_t at = plan.game.description.rfind(" ("); at != std::string::npos)
            plan.game.description = plan.game.description.substr(0, at);
        plan.game.description += " (" + patch + (expansion ? " + Expansion Pack" : "") + ")";
        std::erase_if(plan.notes, [](const std::string& n) {
            return n.rfind("Patch level", 0) == 0 || n.rfind("No single player", 0) == 0 ||
                   n.rfind("Single player", 0) == 0;
        });
        // (Its DLLs are single player's 1.x code, beside any Expansion
        // Pack's.)
        plan.notes.insert(plan.notes.begin(), "Patch " + patch + " applied from " + file.filename().string() + " (" +
                                                  std::to_string(found) + " files)");
        for (std::string& n : plan.notes)
            if (n.find(" pak file") != std::string::npos)
                n = countNote(plan);
        message.clear();
        return true;
    }

    // The Expansion Pack, from its CD (Setup/baseef: pak1 and pak2 as
    // patch 1.2's, pak3 its own; its single-player code in Setup/) or an
    // install it was added to.
    bool addExpansion(ImportPlan& plan, const GameInfo& game, const std::filesystem::path& folder,
                      std::string& message) const {
        std::vector<std::filesystem::path> roots{folder};
        if (const auto setup = fs::findDirectory(folder, "Setup"))
            roots.push_back(*setup);
        for (const std::filesystem::path& root : roots) {
            const auto base = fs::findDirectory(root, game.baseDir);
            const auto pak = base ? fs::findFile(*base, kExpansionPak) : std::nullopt;
            if (!pak || !isExpansionPak(*pak))
                continue;
            auto replace = [&](const std::filesystem::path& from, const std::string& to) {
                std::erase_if(plan.files,
                              [&](const ImportPlan::File& p) { return fs::toLower(p.to) == fs::toLower(to); });
                std::error_code e;
                plan.files.push_back({from, to, std::filesystem::file_size(from, e), std::nullopt});
            };
            bool patched = false;
            std::error_code e;
            for (std::filesystem::directory_iterator it(*base, e), end; !e && it != end; it.increment(e)) {
                const std::string name = fs::toLower(it->path().filename().string());
                if (it->is_regular_file(e) && (it->path().extension() == ".pk3" || name.ends_with(".pk3") ||
                                               name == "efq3.key" || name == "expefq3.key")) {
                    replace(it->path(), game.baseDir + "/" + it->path().filename().string());
                    patched |= name == "pak2.pk3";
                }
            }
            for (const std::string& dll : {game.spGameDll, game.spUiDll})
                if (const auto path = fs::findFile(root, dll); path && isExpansionDll(*path))
                    replace(*path, "expansion/" + fs::toLower(dll));
            // The description and notes: with it now.
            std::string summary;
            if (const size_t at = plan.game.description.rfind(" ("); at != std::string::npos) {
                summary = plan.game.description.substr(at + 2, plan.game.description.size() - at - 3);
                plan.game.description = plan.game.description.substr(0, at);
            }
            std::string patch = summary.substr(0, summary.find_first_of(" ,"));
            if (patched)
                patch = "1.2";
            plan.game.description += " (" + patch + " + Expansion Pack)";
            std::erase_if(plan.notes, [](const std::string& n) { return n.rfind("Patch level", 0) == 0; });
            plan.notes.insert(plan.notes.begin(), "Expansion Pack from " + folder.filename().string() +
                                                      (patched ? " (with patch 1.2's paks)" : ""));
            for (std::string& n : plan.notes)
                if (n.find(" pak file") != std::string::npos)
                    n = countNote(plan);
            message.clear();
            return true;
        }
        message = "the Expansion Pack isn't found in " + folder.filename().string();
        return false;
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
