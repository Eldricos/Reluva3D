// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Play page: a Holomatch level run by Elite Force's own game code, as
// a listen server would. The server game (vm/qagame.qvm) runs the level
// with one player, whose commands come from the keyboard and mouse; the
// client game (vm/cgame.qvm) gets a snapshot after each server frame and
// says what to draw: the level from the player's eye, then the HUD.
// Entities the client game adds as models are drawn with their skins,
// lit by the light grid; sprites, quads and lines (beams) with their
// shaders and colours. Other kinds aren't drawn yet.

#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/client/scene_drawer.h"
#include "engines/idtech3/pages/play_viewer.h"

#include "common/font.h"
#include "common/image.h"
#include "common/zip.h"
#include "common/fs.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <set>
#include <filesystem>
#include <fstream>

namespace reluva::idtech3 {

namespace {

constexpr uint64_t kHintMs = 8000;
constexpr float kPi = 3.14159265358979f;
// The command's button bits: attack 1 (Quake III's) and alt attack 32
// (Elite Force's own), found with reluva-tool game-run -buttons: holding
// either changes the same player state words (weapon time, ammunition,
// events). See eliteforce-syscalls.md.

} // namespace

PlayViewer::PlayViewer(ViewerContext& ctx) : Page(ctx) {
    if (ctx.game.code != GameCode::EliteForce || !ctx.files.has("vm/qagame.qvm") || !ctx.files.has("vm/cgame.qvm"))
        return;
    for (const std::string& n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".bsp") == 0)
            names_.push_back(key);
    }
    std::sort(names_.begin(), names_.end());
    // Holomatch maps first.
    for (size_t i = 0; i < names_.size(); ++i)
        if (names_[i].rfind("maps/hm_", 0) == 0) {
            index_ = int(i);
            break;
        }
}

PlayViewer::~PlayViewer() { deactivate(); }

bool PlayViewer::setUpGraphics() {
    const VideoSettings video = videoSettings(ctx_.cvars);
    frameW_ = video.width;
    frameH_ = video.height;
    ctx_.sys.setVideoMode(frameW_, frameH_, video.fullscreen);
    if (Error err = ctx_.sys.init3D(frameW_, frameH_, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Play page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return false;
    }
    overlay_.create(frameW_, frameH_, PixelFormat::RGBA32);
    shaders_ = std::make_unique<ShaderCache>(*renderer_, ctx_.files, ctx_.shaders);
    world_ = std::make_unique<WorldRenderer>(*renderer_, *shaders_);
    models_ = std::make_unique<ModelCache>(*renderer_, *shaders_, ctx_.files);
    applyVideoSettings(video, *shaders_, world_.get(), models_.get());
    scene_ = std::make_unique<SceneDrawer>(*renderer_, *shaders_, *models_, ctx_.game.refEntity);
    menus_.attach(renderer_, frameW_, frameH_);
    return true;
}

void PlayViewer::restartVideo() {
    videoRestart_ = false;
    if (!renderer_ || !cgame_)
        return;
    if (menuOpen_)
        closeMenu();
    // What draws through the renderer goes first, then the renderer
    // starts again; the menus too, as the original's (closed).
    menus_.detach();
    menus_.reset();
    cgame_.reset();
    stopSounds();
    scene_.reset();
    models_.reset();
    world_.reset();
    shaders_.reset();
    if (!setUpGraphics())
        return;
    ctx_.sys.setRelativeMouse(true);
    // The level's graphics and the client game, where the match is now:
    // the server's latest configstrings, and for a server elsewhere the
    // commands it has sent so far as done.
    const std::string map = infoValue(matchConfigstring(0), "mapname");
    std::shared_ptr<Bsp> bsp;
    if (!loadLevel(normalizeArchiveName("maps/" + map + ".bsp"), bsp))
        return;
    if (client_) {
        if (Error err = startClientGame(client_->configstrings(), 0, int32_t(client_->serverCommands().size())))
            stop("The client game stopped: " + err.message);
        return;
    }
    if (!game_)
        return;
    localConfigstrings_ = game_->configstrings();
    localCommands_.clear();
    localCommandsSeen_ = game_->serverCommands().size();
    if (Error err = startClientGame(game_->configstrings(), 0, 0)) {
        stop("The client game stopped: " + err.message);
        return;
    }
    clientTime_ = game_->levelTime();
    cgame_->newSnapshot(localSnapshot());
}

void PlayViewer::activate() {
    JoystickInput::registerCvars(cvars_);
    if (!setUpGraphics())
        return;
    menuOpen_ = false;
    videoRestart_ = false;
    shownMs_ = 0;
    ctx_.sys.setRelativeMouse(true);
    // A demo the menus asked to play.
    if (!ctx_.requestDemo.empty()) {
        const std::string name = ctx_.requestDemo;
        ctx_.requestDemo.clear();
        playDemo(name);
        return;
    }
    // A server the menus asked to join.
    if (!ctx_.requestConnect.empty()) {
        const std::string address = ctx_.requestConnect;
        ctx_.requestConnect.clear();
        connect(address);
        return;
    }
    // A level the menus asked for.
    if (!ctx_.requestMap.empty()) {
        const std::string wanted = normalizeArchiveName("maps/" + ctx_.requestMap + ".bsp");
        for (size_t i = 0; i < names_.size(); ++i)
            if (names_[i] == wanted)
                index_ = int(i);
        ctx_.requestMap.clear();
    }
    start(index_);
}

void PlayViewer::deactivate() {
    closeTyping();
    if (cgame_ || client_ || game_)
        ctx_.matchEnded = true;
    if (menuOpen_)
        closeMenu();
    // The in-game menu goes with the match (started afresh on the next).
    menus_.detach();
    menus_.reset();
    if (renderer_)
        ctx_.sys.setRelativeMouse(false);
    // The level stops with the page; it starts again when shown.
    if (client_)
        client_->disconnect();
    client_.reset();
    server_.reset();
    cgame_.reset();
    game_.reset();
    stopSounds();
    scene_.reset();
    models_.reset();
    world_.reset();
    shaders_.reset();
    renderer_ = nullptr;
    input_.releaseAll();
}

void PlayViewer::reset() {
    error_.clear();
    if (client_)
        client_->disconnect();
    client_.reset();
    server_.reset();
    cgame_.reset();
    game_.reset();
    stopSounds();
    if (scene_)
        scene_->reset();
    models_->clear();
    world_->clear();
    shaders_->clear();
    input_.yaw = input_.pitch = 0;
}

bool PlayViewer::loadLevel(const std::string& path, std::shared_ptr<Bsp>& bsp) {
    bsp = std::make_shared<Bsp>();
    std::vector<uint8_t> data;
    Error err = ctx_.files.read(path, data);
    if (!err)
        err = parseBsp(data, *bsp);
    if (!err)
        err = world_->load(*bsp, ctx_.shaders);
    if (err) {
        stop("Couldn't load " + path + ": " + err.message);
        return false;
    }
    // The light grid lights models, with the worldspawn's cell size if it
    // gives one.
    grid_ = LightGrid{};
    float cell[3] = {64, 64, 128};
    std::vector<Entity> entities;
    if (!parseEntities(bsp->entities, entities) && !entities.empty())
        entities[0].vector("gridsize", cell);
    grid_.build(*bsp, {cell[0], cell[1], cell[2]});
    return true;
}

Error PlayViewer::startClientGame(const std::map<int32_t, std::string>& configstrings, int32_t serverMessage,
                                  int32_t commandSequence) {
    cgame_ = std::make_unique<eliteforce::CgameHost>(ctx_.files, cvars_);
    cgame_->print = [this](const std::string& text) { addPrint(text, true); };
    scene_->setHandles({&cgame_->models(), &cgame_->skins(), &cgame_->shaders()});
    cgame_->subdivisions = videoSettings(cvars_).subdivisions;
    notify_.clear();
    printPartial_[1].clear();
    if (ctx_.mixer) {
        soundSystem_ = std::make_unique<SoundSystem>(*ctx_.mixer, soundLibrary_);
        cgame_->soundSystem = soundSystem_.get();
    }
    Error err = cgame_->load();
    if (!err) {
        cgame_->setConfigstrings(configstrings);
        if (client_)
            cgame_->setServerCommands(client_->serverCommands());
        else
            sendServerCommands();
        loadingFailed_ = false;
        cgame_->updateScreen = [this] { drawLoadingScreen(); };
        err = cgame_->init(0, serverMessage, commandSequence);
        cgame_->updateScreen = nullptr;
    }
    return err;
}

void PlayViewer::start(int index) {
    index_ = index;
    reset();
    const std::string& path = names_[size_t(index)];
    std::string map = path.substr(5, path.size() - 9); // without maps/ and .bsp
    setTitle(map + " (" + std::to_string(index + 1) + "/" + std::to_string(names_.size()) + ")");
    std::shared_ptr<Bsp> bsp;
    if (!loadLevel(path, bsp))
        return;

    // The server: the level, and the player joining it. The holodeck
    // intro, which holds new players in place, is off until there's a
    // way to watch it.
    cvars_.set("g_holoIntro", "0");
    game_ = std::make_unique<eliteforce::GameHost>(ctx_.files, cvars_);
    game_->print = [this](const std::string& text) { addPrint(text, false); };
    printPartial_[0].clear();
    closeTyping();
    Error err = game_->load();
    game_->reserveClient(0); // the player's, though bots may be added first
    if (!err)
        err = game_->startLevel(map, bsp, int32_t(ctx_.sys.getMillis()));
    std::string refused;
    if (!err)
        err = game_->connectClient(0, userinfo() + "\\ip\\localhost", refused);
    if (!err && !refused.empty())
        err = Error::fail("the game refused the player: " + refused);
    if (!err)
        err = game_->beginClient(0);
    // What the menus asked to run once the level started, as the
    // original's console runs it: the server game's console commands
    // (adding bots...), and the rest sent to the server as the player's
    // own (the team chosen for a team game: "team red").
    for (const std::string& command : ctx_.requestCommands) {
        if (err)
            break;
        bool handled = false;
        err = game_->serverCommand(command, &handled);
        if (!err && !handled)
            err = game_->clientCommand(0, command);
    }
    ctx_.requestCommands.clear();
    if (err) {
        stop("The server game stopped: " + err.message);
        return;
    }
    startHosting();
    localConfigstrings_ = game_->configstrings();
    localCommands_.clear();
    localCommandsSeen_ = 0;
    localRestartsSeen_ = game_->restarts();

    // The player's client game, given what the server has set.
    if (Error e = startClientGame(game_->configstrings(), 0, 0)) {
        stop("The client game stopped: " + e.message);
        return;
    }
    clientTime_ = game_->levelTime();
    lastMs_ = 0;
    cgame_->newSnapshot(localSnapshot());
}

bool PlayViewer::changeLevel(const std::string& map) {
    // As the original's map command on a server with players: the level
    // loaded, the game started afresh on it, the players and bots kept;
    // those on other machines are sent the new level's game state.
    const std::string path = normalizeArchiveName("maps/" + map + ".bsp");
    if (!ctx_.files.has(path)) {
        addPrint("Can't find map " + path + "\n", false);
        return true;
    }
    cgame_.reset();
    stopSounds();
    if (scene_)
        scene_->reset();
    models_->clear();
    world_->clear();
    shaders_->clear();
    std::shared_ptr<Bsp> bsp;
    if (!loadLevel(path, bsp))
        return false;
    for (size_t i = 0; i < names_.size(); ++i)
        if (names_[i] == path)
            index_ = int(i);
    setTitle(map + " (" + std::to_string(index_ + 1) + "/" + std::to_string(names_.size()) + ")");
    std::vector<int32_t> players;
    if (Error err = game_->changeLevel(map, bsp, int32_t(ctx_.sys.getMillis()), players)) {
        stop("The server game stopped: " + err.message);
        return false;
    }
    // The page's own player is in the level at once.
    if (std::find(players.begin(), players.end(), 0) != players.end())
        if (Error err = game_->beginClient(0)) {
            stop("The server game stopped: " + err.message);
            return false;
        }
    if (server_)
        server_->levelStarted();
    localConfigstrings_ = game_->configstrings();
    localCommands_.clear();
    localCommandsSeen_ = game_->serverCommands().size();
    localRestartsSeen_ = game_->restarts();
    if (Error err = startClientGame(game_->configstrings(), 0, 0)) {
        stop("The client game stopped: " + err.message);
        return false;
    }
    clientTime_ = game_->levelTime();
    cgame_->newSnapshot(localSnapshot());
    return false; // this frame's done
}

bool PlayViewer::startDownloads() {
    // As the original's client: the paks the server lists as the level's
    // (sv_referencedPaks, by checksum, and their names) that it has none
    // of, but Raven's own, which are never downloaded; with
    // cl_allowDownload, downloaded; without it, only named.
    const auto& cs = client_->configstrings();
    const std::string info = cs.count(1) ? cs.at(1) : std::string();
    std::istringstream sums(infoValue(info, "sv_referencedPaks")), names(infoValue(info, "sv_referencedPakNames"));
    std::set<int32_t> ours;
    for (const Archive* a : ctx_.files.archives())
        if (const auto* pak = dynamic_cast<const ZipArchive*>(a))
            ours.insert(pakChecksum(*pak));
    downloads_.clear();
    std::string sum, name;
    while (sums >> sum && names >> name) {
        const auto checksum = int32_t(std::strtoll(sum.c_str(), nullptr, 10));
        const std::string lower = fs::toLower(name);
        const bool raven = lower.size() == 10 && lower.rfind("baseef/pak", 0) == 0 && lower[9] >= '0' && lower[9] <= '3';
        if (ours.count(checksum) || raven)
            continue;
        downloads_.emplace_back(name + ".pk3", checksum);
    }
    if (downloads_.empty())
        return false;
    cvars_.create("cl_allowDownload", "0", CvarStore::Archive);
    if (!cvars_.integer("cl_allowDownload")) {
        std::string missing;
        for (const auto& [file, checksum] : downloads_)
            missing += " " + file;
        addPrint("You're missing files the server uses:" + missing +
                     "\nYou might not be able to play. Set cl_allowDownload 1 to download them.\n",
                 true);
        downloads_.clear();
        return false;
    }
    addPrint("Downloading " + std::to_string(downloads_.size()) + " file(s) from the server\n", true);
    client_->download(downloads_.front().first);
    return true;
}

bool PlayViewer::finishDownload(const std::string& name, const std::vector<uint8_t>& data) {
    // Kept in the save folder's game folder (the base game's, or the mod
    // played's), as the original keeps them in its home folder: a name with
    // no way out of it, checked against the checksum the server gave before
    // it's used.
    if (downloads_.empty() || downloads_.front().first != name)
        return true;
    const int32_t expected = downloads_.front().second;
    downloads_.erase(downloads_.begin());
    const std::filesystem::path relative(name);
    const std::string folder = fs::toLower(relative.parent_path().string());
    if (ctx_.homePath.empty() || relative.is_absolute() || name.find("..") != std::string::npos ||
        (folder != fs::toLower(ctx_.game.baseDir) && (ctx_.gameDir.empty() || folder != fs::toLower(ctx_.gameDir))) ||
        fs::toLower(relative.extension().string()) != ".pk3") {
        stop("Won't keep the download " + name);
        return false;
    }
    const std::filesystem::path path = ctx_.homePath / relative;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    {
        std::ofstream out(path.string() + ".tmp", std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!out) {
            stop("Couldn't write " + path.string());
            return false;
        }
    }
    std::filesystem::rename(path.string() + ".tmp", path, ec);
    ZipArchive check;
    if (ec || check.open(path) || pakChecksum(check) != expected) {
        std::filesystem::remove(path, ec);
        stop("Incorrect checksum for file: " + name);
        return false;
    }
    if (!ctx_.addPak || ctx_.addPak(path)) {
        stop("Couldn't use " + name);
        return false;
    }
    shaders_->clear(); // the shader scripts were read again
    addPrint("Downloaded " + name + "\n", true);
    if (!downloads_.empty())
        client_->download(downloads_.front().first);
    else
        client_->addClientCommand("donedl"); // the game state again
    return true;
}

std::string PlayViewer::userinfo() const {
    // The player's settings the menus keep (name, model...), as the
    // original sends them: every user-info setting.
    std::string info;
    for (const CvarStore::Cvar& cv : cvars_.all())
        if ((cv.flags & CvarStore::UserInfo) && !cv.value.empty())
            setInfoValue(info, cv.name, cv.value);
    auto fallback = [&](const char* key, const char* value) {
        if (infoValue(info, key).empty())
            setInfoValue(info, key, value);
    };
    fallback("name", "Reluva");
    fallback("model", ctx_.game.playerModel.c_str());
    fallback("handicap", "100");
    fallback("snaps", "20");
    fallback("rate", "25000");
    fallback("sex", "male");
    fallback("color", "4");
    return info;
}

void PlayViewer::startHosting() {
    // Others can join over the network: on the original's port (or the
    // next of its four, if taken), only with patch 1.2's game code,
    // whose layouts the protocol carries.
    if (game_->layout().version != 12) {
        addPrint("Not open to other players: network play needs Elite Force's patch 1.2 (pak1-pak3.pk3)\n", false);
        return;
    }
    std::unique_ptr<net::UdpSocket> socket;
    for (int p = 0; p < ServerBrowser::kServerPorts && !socket; ++p)
        if (ctx_.sys.openUdp(uint16_t(ServerBrowser::kServerPort + p), socket))
            socket.reset();
    if (!socket) {
        addPrint("Not open to other players: no network\n", false);
        return;
    }
    server_ = std::make_unique<eliteforce::NetServer>(*game_, cvars_, std::move(socket));
    server_->print = [this](const std::string& text) { addPrint(text, false); };
    server_->resolveHost = [this](const std::string& host, uint32_t& ip) { return ctx_.sys.resolveHost(host, ip); };
    server_->reserve(0);
    server_->setFiles(&ctx_.files);
    server_->levelStarted();
    addPrint("Open to other players on port " + std::to_string(server_->port()) + "\n", false);
}

void PlayViewer::connect(const std::string& address) {
    reset();
    setTitle("Connecting to " + address);
    const auto [host, port] = net::splitHostPort(address, ServerBrowser::kServerPort);
    uint32_t ip = 0;
    if (!ctx_.sys.resolveHost(host, ip)) {
        stop("Couldn't find " + host);
        return;
    }
    std::unique_ptr<net::UdpSocket> socket;
    if (Error err = ctx_.sys.openUdp(0, socket)) {
        stop("No network: " + err.message);
        return;
    }
    // The qport tells the server it's still us if a router changes our port.
    const auto qport = uint16_t(ctx_.sys.getMillis() * 2654435761u >> 16);
    serverAddress_ = address;
    joinStartMs_ = ctx_.sys.getMillis();
    joinState_ = eliteforce::NetClient::State::Disconnected;
    addPrint("Joining " + address + "\n", false);
    client_ = std::make_unique<eliteforce::NetClient>(std::move(socket), net::Address{ip, port}, userinfo(), qport);
    lastMs_ = 0;
}

void PlayViewer::playDemo(const std::string& name) {
    reset();
    std::string path = normalizeArchiveName("demos/" + name);
    if (path.size() < 7 || path.compare(path.size() - 7, 7, ".efdemo") != 0)
        path += ".efdemo";
    std::vector<uint8_t> data;
    if (Error err = ctx_.files.read(path, data)) {
        stop("Couldn't open " + path);
        return;
    }
    setTitle("Demo " + name);
    serverAddress_.clear();
    joinStartMs_ = ctx_.sys.getMillis();
    joinState_ = eliteforce::NetClient::State::Disconnected;
    client_ = std::make_unique<eliteforce::NetClient>(std::move(data));
    lastMs_ = 0;
}

bool PlayViewer::playingDemo() const { return client_ && client_->playingDemo(); }

void PlayViewer::endDemo() {
    // No error to show; the menus go back to their main menu.
    stop("");
    error_.clear();
    ctx_.matchEnded = true;
}

void PlayViewer::runRemote(uint64_t nowMs, int32_t dt) {
    // Playing on someone else's server: what it sends, a level loaded
    // when a game state comes, its snapshots for the client game, and the
    // player's commands back.
    client_->frame(nowMs);
    // How long each step of joining took, for the console.
    if (client_->state() != joinState_ && !client_->playingDemo()) {
        joinState_ = client_->state();
        static const char* const kSteps[] = {"", "disconnected", "", "asking for a challenge", "connecting",
                                             "waiting for the level", "", "loading the level", "level loaded, joining the game"};
        const auto st = size_t(joinState_);
        char line[128];
        std::snprintf(line, sizeof line, "%.2f s: %s\n", double(nowMs - joinStartMs_) / 1000,
                      st < 9 ? kSteps[st] : "?");
        addPrint(line, true); // on screen for a few seconds too
    }
    if (client_->demoFinished()) {
        // The end of a demo: back to the menus, as the original.
        endDemo();
        ctx_.requestPage = "Menu";
        return;
    }
    if (client_->state() == eliteforce::NetClient::State::Disconnected) {
        // Back to the menus, which show why, as the original.
        const std::string why = client_->message().empty() ? "Server disconnected" : client_->message();
        client_.reset();
        stop(why);
        cvars_.create("com_errorMessage", "", 0);
        cvars_.set("com_errorMessage", why);
        ctx_.requestPage = "Menu";
        return;
    }
    // A pak finished downloading: kept, checked, and the next, or done.
    {
        std::string name;
        std::vector<uint8_t> data;
        if (client_->takeDownload(name, data) && !finishDownload(name, data))
            return;
    }
    if (client_->takeNewGamestate()) {
        // A server playing a mod (fs_game in its system info) other than
        // this one's: as the original's client, the mod taken up, the
        // game's files started again (the engine, at requestRestart) and
        // the server joined again.
        if (!client_->playingDemo()) {
            const auto& info = client_->configstrings();
            const std::string serverMod =
                modFolderName(info.count(1) ? infoValue(info.at(1), "fs_game") : std::string(), ctx_.game.baseDir);
            if (serverMod != ctx_.gameDir) {
                cvars_.set("fs_game", serverMod, true);
                ctx_.requestConnect = serverAddress_;
                ctx_.requestRestart = true;
                client_->disconnect();
                client_.reset();
                return;
            }
        }
        // Paks the server uses that this player hasn't: downloaded first
        // (the level loads when the server sends its game state again).
        if (!client_->playingDemo() && startDownloads())
            return;
        const auto& cs = client_->configstrings();
        const std::string map = cs.count(0) ? infoValue(cs.at(0), "mapname") : std::string();
        cgame_.reset();
        stopSounds();
        if (scene_)
            scene_->reset();
        models_->clear();
        world_->clear();
        shaders_->clear();
        if (!client_->playingDemo())
            setTitle(map + " on " + serverAddress_);
        std::shared_ptr<Bsp> bsp;
        if (!loadLevel(normalizeArchiveName("maps/" + map + ".bsp"), bsp))
            return;
        if (Error err = startClientGame(cs, client_->gamestateMessage(), client_->gamestateCommandSequence())) {
            stop("The client game stopped: " + err.message);
            return;
        }
        // The pure check, before the first move: which paks the client
        // game, the menus and the rest come from (a server that isn't
        // pure ignores it).
        const std::string paks = cs.count(1) ? infoValue(cs.at(1), "sv_paks") : std::string();
        if (const std::string cp = pureCommand(ctx_.files, client_->checksumFeed(), paks);
            !cp.empty() && !client_->playingDemo())
            client_->addClientCommand(cp);
        client_->setLoaded();
    }
    if (!cgame_)
        return;
    cgame_->setServerCommands(client_->serverCommands());
    for (eliteforce::GameHost::Snapshot& snap : client_->takeSnapshots()) {
        // The player's number (playerState_t's clientNum, byte 156), for
        // the in-game menu.
        if (snap.playerState.size() >= 160)
            std::memcpy(&playerNumber_, snap.playerState.data() + 156, 4);
        cgame_->newSnapshot(std::move(snap));
    }
    // Commands go out as soon as the level is loaded, as the original's
    // client sends them: the first puts the player in the game.
    if (client_->hasSnapshot())
        clientTime_ = client_->serverTime();
    if (dt > 0 && !client_->playingDemo()) {
        eliteforce::GameHost::UserCommand cmd = buildCommand();
        cmd.serverTime = client_->hasSnapshot() ? clientTime_ : 0;
        client_->addCommand(cmd);
        if (client_->hasSnapshot())
            cgame_->addUserCommand(cmd);
    }
}

void PlayViewer::drawLoadingScreen() {
    // As the original: the client game's frame drawn now, in the middle of
    // its loading, which only draws what it's loading (the level's picture,
    // its name, the media so far); then shown.
    if (!cgame_ || !renderer_ || loadingFailed_)
        return;
    cgame_->clearPictures();
    cgame_->clearScene();
    int32_t ignored = 0;
    if (cgame_->call(eliteforce::CgameHost::DrawActiveFrame, {game_ ? game_->levelTime() : 0, 0, 0}, ignored)) {
        loadingFailed_ = true; // it can't yet; the level goes on loading regardless
        return;
    }
    renderer_->setGamma(brightness(ctx_));
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    scene_->drawPictures(cgame_->pictures(), 0, cgame_->pictures().size(), 640, 480, 0);
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    renderer_->endFrame(&overlay_);
    cgame_->clearPictures();
    cgame_->clearScene();
}

void PlayViewer::stop(const std::string& why) {
    error_ = why;
    const std::string log = cgame_ ? cgame_->log() : game_ ? game_->log() : std::string();
    if (!log.empty()) {
        std::string last = log;
        while (!last.empty() && last.back() == '\n')
            last.pop_back();
        error_ += " (last printed: " + last.substr(last.rfind('\n') == std::string::npos ? 0 : last.rfind('\n') + 1) + ")";
    }
    if (client_)
        client_->disconnect();
    client_.reset();
    server_.reset();
    cgame_.reset();
    game_.reset();
    stopSounds();
}

void PlayViewer::stopSounds() {
    // After the client game, which uses it; the mixer's volumes back as
    // the other pages expect them.
    soundSystem_.reset();
    if (ctx_.mixer) {
        ctx_.mixer->setTypeVolume(audio::SoundType::Effects, audio::kMaxVolume);
        ctx_.mixer->setTypeVolume(audio::SoundType::Music, audio::kMaxVolume);
    }
}

void PlayViewer::sendServerCommands() {
    // What the server would send the player over the network, in order:
    // the configstrings that changed (cs, bcs: the client game sets up a
    // player who joined by theirs), then the game's commands to them.
    for (std::string& change : eliteforce::configstringCommands(localConfigstrings_, game_->configstrings()))
        localCommands_.push_back(std::move(change));
    localConfigstrings_ = game_->configstrings();
    const auto& commands = game_->serverCommands();
    for (; localCommandsSeen_ < commands.size(); ++localCommandsSeen_)
        if (commands[localCommandsSeen_].first == 0 || commands[localCommandsSeen_].first == -1)
            localCommands_.push_back(commands[localCommandsSeen_].second);
    // A map_restart, which the client game starts its level over for.
    if (game_->restarts() != localRestartsSeen_) {
        localRestartsSeen_ = game_->restarts();
        localCommands_.push_back("map_restart");
    }
    cgame_->setServerCommands(localCommands_);
}

eliteforce::GameHost::Snapshot PlayViewer::localSnapshot() {
    eliteforce::GameHost::Snapshot snap = game_->snapshot(0);
    snap.serverCommandSequence = int32_t(localCommands_.size());
    return snap;
}

void PlayViewer::consoleCommand(const std::string& text) {
    // As the original's client: commands it knows itself, then the client
    // game's, and anything else goes to the server, whose game answers
    // (an unknown command it prints as such).
    const std::vector<std::string> w = splitCommand(text);
    if (w.empty() || !cgame_)
        return;
    const std::string verb = fs::toLower(w[0]);
    if (verb == "centerview") {
        input_.pitch = 0;
        return;
    }
    if (verb == "vstr" && w.size() >= 2) {
        runBinding(cvars_.string(w[1]), 0, true);
        return;
    }
    if (verb == "exec") {
        // A config file's commands, each as typed (the Holomatch modes'
        // assimilation.cfg...): the player's own in the save folder
        // first, then the game's; .cfg added when no extension is given.
        if (w.size() < 2) {
            addPrint("exec <filename> : execute a script file\n", false);
            return;
        }
        std::string name = w[1];
        if (name.find('.') == std::string::npos)
            name += ".cfg";
        std::string text;
        std::vector<uint8_t> data;
        if (std::ifstream in(ctx_.savePath / name, std::ios::binary); !ctx_.savePath.empty() && in)
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        else if (!ctx_.files.read(name, data))
            text.assign(data.begin(), data.end());
        else {
            addPrint("couldn't exec " + name + "\n", false);
            return;
        }
        if (execDepth_ >= 8)
            return; // a file running itself
        addPrint("execing " + name + "\n", false);
        ++execDepth_;
        for (const std::string& command : splitCommands(text)) {
            consoleCommand(command);
            if (!cgame_)
                break; // the match stopped
        }
        --execDepth_;
        return;
    }
    if (verb == "bind" || verb == "unbind" || verb == "unbindall") {
        ctx_.bindings.execute(text, cvars_);
        return;
    }
    if (verb == "togglemenu") {
        openMenu();
        return;
    }
    if (verb == "quit") {
        ctx_.requestPage = "Start";
        return;
    }
    if (verb == "vid_restart") {
        videoRestart_ = true; // before the next frame, not in the middle of this one
        ctx_.requestRestart = true; // and the game's files, if fs_game changed
        return;
    }
    if (verb == "cmd") {
        // The rest for the server as the player's own (the in-game
        // menu's "cmd team red").
        const size_t at = text.find_first_not_of(' ', text.find_first_of(' ') == std::string::npos
                                                          ? text.size()
                                                          : text.find_first_of(' '));
        const std::string rest = at == std::string::npos ? std::string() : text.substr(at);
        if (rest.empty())
            return;
        if (client_)
            client_->addClientCommand(rest);
        else if (game_)
            if (Error err = game_->clientCommand(0, rest))
                stop("The server game stopped: " + err.message);
        return;
    }
    if (verb == "toggleconsole") {
        if (typing_ == Typing::Console)
            closeTyping();
        else
            openTyping(Typing::Console);
        return;
    }
    if (verb == "messagemode" || verb == "messagemode2") {
        openTyping(verb == "messagemode" ? Typing::Say : Typing::SayTeam, -1, bindingKeyChar_);
        return;
    }
    if (verb == "messagemode3" || verb == "messagemode4") {
        // A word to the player in the crosshair, or the last to hit you,
        // as the client game says.
        int32_t client = -1;
        if (Error err = cgame_->call(verb == "messagemode3" ? 4 : 5, {}, client)) {
            stop("The client game stopped: " + err.message);
            return;
        }
        if (client >= 0)
            openTyping(Typing::Tell, client, bindingKeyChar_);
        return;
    }
    if (verb == "connect" && w.size() >= 2) {
        connect(w[1]);
        return;
    }
    if (verb == "demo" && w.size() >= 2) {
        playDemo(w[1]);
        return;
    }
    if (verb == "reconnect" && !serverAddress_.empty()) {
        connect(serverAddress_);
        return;
    }
    if (verb == "disconnect") {
        // Back to the menus, as the original.
        if (client_)
            client_->disconnect();
        ctx_.requestPage = "Menu";
        return;
    }
    if (verb == "clear") {
        consoleLines_.clear();
        return;
    }
    if (verb == "screenshot" || verb == "screenshotjpeg") {
        // The next frame, kept when it's drawn (saveScreenshot()). As the
        // original: "silent" says nothing; another word names the file.
        // Both are written as TGA files.
        screenshot_ = Screenshot{true, w.size() >= 2 && w[1] == "silent",
                                 w.size() >= 2 && w[1] != "silent" ? w[1] : std::string()};
        renderer_->captureNextFrame();
        return;
    }
    bool handled = false;
    if (Error err = cgame_->consoleCommand(text, handled)) {
        stop("The client game stopped: " + err.message);
        return;
    }
    if (handled)
        return;
    // Settings, after the client game's own commands as the original's
    // console: set (seta, sets, setu) name value, or a setting's name
    // alone (shows it) or with a value.
    if ((verb == "set" || verb == "seta" || verb == "sets" || verb == "setu") && w.size() >= 3) {
        const int32_t flags = verb == "seta"   ? CvarStore::Archive
                              : verb == "sets" ? CvarStore::ServerInfo
                              : verb == "setu" ? CvarStore::UserInfo
                                               : 0;
        cvars_.create(w[1], w[2], flags);
        setFromConsole(w[1], w[2]);
        return;
    }
    if (const CvarStore::Cvar* cv = cvars_.find(w[0])) {
        if (w.size() == 1)
            addPrint("\"" + cv->name + "\" is \"" + cv->value + "\"\n", false);
        else
            setFromConsole(w[0], w[1]);
        return;
    }
    // On someone else's server, the rest goes to it as the player's.
    if (client_) {
        client_->addClientCommand(text);
        return;
    }
    if (!game_)
        return;
    // On a server of one's own, its game's console commands (addbot,
    // kick...) next; the rest goes to the server as the player's.
    if (Error err = game_->serverCommand(text, &handled)) {
        stop("The server game stopped: " + err.message);
        return;
    }
    if (handled)
        return;
    if (Error err = game_->clientCommand(0, text))
        stop("The server game stopped: " + err.message);
}

void PlayViewer::setFromConsole(const std::string& name, const std::string& value) {
    // As the original's console: a variable that only changes with a new
    // level (the game type, the Holomatch modes) says so.
    cvars_.set(name, value);
    if (cvars_.hasLatched(name))
        addPrint(name + " will be changed upon restarting.\n", false);
}

void PlayViewer::openMenu() {
    // Outside a match, the menus' own page; a demo ends, as the
    // original's does on Esc.
    if (!cgame_ || !renderer_ || playingDemo()) {
        if (playingDemo())
            endDemo();
        ctx_.requestPage = "Menu";
        return;
    }
    closeTyping();
    // The menus see a connected player and the match's configstrings
    // (its game type decides what they offer).
    // Whether this machine runs the match's server: the in-game menu
    // offers the server's items (adding bots, restarting) by it.
    cvars_.create("sv_running", "0", CvarStore::ReadOnly);
    cvars_.set("sv_running", game_ && !client_ ? "1" : "0", true);
    menus_.configure = [this](eliteforce::UiHost& ui) {
        ui.clientState.connState = 8; // CA_ACTIVE
        ui.clientState.clientNum = client_ ? playerNumber_ : 0;
        ui.clientState.serverName = client_ ? serverAddress_ : "localhost";
        ui.configstring = [this](int32_t index) { return matchConfigstring(index); };
    };
    if (menus_.running())
        menus_.show(MenuLayer::kInGameMenu);
    else if (!menus_.start(MenuLayer::kInGameMenu)) {
        addPrint(menus_.error() + "\n", false);
        return;
    }
    menuOpen_ = true;
    menuCaught_ = menus_.open(); // they take the keys as they show the menu
    input_.releaseAll();
    ctx_.sys.setRelativeMouse(false);
    ctx_.sys.setMouseCursorVisible(false);
    ctx_.sys.setTextInput(true);
}

void PlayViewer::closeMenu() {
    menuOpen_ = false;
    menuCaught_ = false;
    input_.releaseAll();
    ctx_.sys.setTextInput(false);
    ctx_.sys.setRelativeMouse(true);
    if (!menus_.error().empty()) {
        addPrint(menus_.error() + "\n", false);
        menus_.clearError();
    }
}

void PlayViewer::runMenuCommands() {
    // What the in-game menu runs goes to the console, as the original's
    // menus' commands do ("cmd team red", "callvote ...", "disconnect").
    for (const std::string& line : menus_.takeCommands())
        for (const std::string& command : splitCommands(line)) {
            const std::vector<std::string> words = splitCommand(command);
            if (words.empty())
                continue;
            const std::string verb = fs::toLower(words[0]);
            if (verb == "wait")
                continue;
            lastMenuCommand_ = command;
            lastMenuCommandMs_ = ctx_.sys.getMillis();
            consoleCommand(command);
            if (!ctx_.requestPage.empty())
                return;
        }
}

std::string PlayViewer::matchConfigstring(int32_t index) const {
    const std::map<int32_t, std::string>& all = client_ ? client_->configstrings() : localConfigstrings_;
    const auto it = all.find(index);
    return it == all.end() ? std::string() : it->second;
}

void PlayViewer::runBinding(const std::string& line, int32_t key, bool down) {
    input_.runBinding(line, key, down, [this](const std::string& text) { consoleCommand(text); });
}

bool PlayViewer::takesKey(const Event& ev) const {
    // Esc goes back to the menus, as in the game; with Ctrl it leaves.
    // While typing, every key is the typing's (Tab, Esc too), but Ctrl+Esc.
    if (ev.type == EventType::KeyDown && ev.key == key::Escape && (ev.mods & keymod::Ctrl))
        return false;
    // The in-game menu takes them all too.
    return menuOpen_ || typing_ != Typing::None || (ev.type == EventType::KeyDown && ev.key == key::Escape);
}

void PlayViewer::openTyping(Typing t, int32_t client, uint32_t key) {
    // The keys held are let go, as the original's typing takes them.
    input_.releaseAll();
    typing_ = t;
    tellClient_ = client;
    field_.clear();
    cursor_ = 0;
    consoleScroll_ = 0;
    historyPos_ = history_.size();
    swallowChar_ = key;
    ctx_.sys.setTextInput(true);
}

void PlayViewer::closeTyping() {
    if (typing_ != Typing::None && renderer_)
        ctx_.sys.setTextInput(false);
    typing_ = Typing::None;
    field_.clear();
    cursor_ = 0;
}

void PlayViewer::submitTyping() {
    std::string text = field_;
    const Typing t = typing_;
    field_.clear();
    cursor_ = 0;
    if (t == Typing::Console) {
        // Lines starting / or \ are commands; others are said, as the
        // original's console does once connected.
        if (text.empty())
            return;
        std::replace(text.begin(), text.end(), '"', '\'');
        if (history_.empty() || history_.back() != text)
            history_.push_back(text);
        historyPos_ = history_.size();
        consoleLines_.push_back("]" + text);
        consoleScroll_ = 0;
        if (text[0] == '/' || text[0] == '\\')
            consoleCommand(text.substr(1));
        else
            consoleCommand("say \"" + text + "\"");
        return;
    }
    closeTyping();
    if (text.empty())
        return;
    // Quotes would end the line early: they become apostrophes.
    std::replace(text.begin(), text.end(), '"', '\'');
    if (t == Typing::Say)
        consoleCommand("say \"" + text + "\"");
    else if (t == Typing::SayTeam)
        consoleCommand("say_team \"" + text + "\"");
    else if (t == Typing::Tell)
        consoleCommand("tell " + std::to_string(tellClient_) + " \"" + text + "\"");
}

void PlayViewer::typingEvent(const Event& ev) {
    if (ev.type == EventType::TextInput) {
        // Characters typed (Latin-1, as the original's), but the one of the
        // key that opened the typing, and the console key in the console.
        for (uint32_t cp : decodeUtf8(ev.text)) {
            if (cp == swallowChar_) {
                swallowChar_ = 0;
                continue;
            }
            swallowChar_ = 0;
            if (cp < 32 || cp == 127 || cp > 255 || field_.size() >= 255)
                continue;
            if (typing_ == Typing::Console && (cp == '`' || cp == '~'))
                continue;
            field_.insert(cursor_++, 1, char(cp));
        }
        return;
    }
    if (ev.type != EventType::KeyDown)
        return;
    swallowChar_ = 0;
    switch (ev.key) {
    case key::Escape: closeTyping(); return;
    case key::Return:
    case key::KpEnter: submitTyping(); return;
    case key::Backspace:
        if (cursor_ > 0)
            field_.erase(--cursor_, 1);
        return;
    case key::Delete:
        if (cursor_ < field_.size())
            field_.erase(cursor_, 1);
        return;
    case key::Left: cursor_ = cursor_ > 0 ? cursor_ - 1 : 0; return;
    case key::Right: cursor_ = std::min(field_.size(), cursor_ + 1); return;
    case key::Home: cursor_ = 0; return;
    case key::End: cursor_ = field_.size(); return;
    default: break;
    }
    if (typing_ != Typing::Console)
        return;
    switch (ev.key) {
    case '`': closeTyping(); return; // the console key closes it again
    case key::Up:
    case key::Down:
        // Earlier lines typed, back and forth.
        if (history_.empty())
            return;
        if (ev.key == key::Up && historyPos_ > 0)
            --historyPos_;
        else if (ev.key == key::Down && historyPos_ < history_.size())
            ++historyPos_;
        field_ = historyPos_ < history_.size() ? history_[historyPos_] : std::string();
        cursor_ = field_.size();
        return;
    case key::PageUp: consoleScroll_ = std::min(consoleScroll_ + 4, int(consoleLines_.size())); return;
    case key::PageDown: consoleScroll_ = std::max(consoleScroll_ - 4, 0); return;
    default: return;
    }
}

void PlayViewer::handleEvent(const Event& ev) {
    // The joystick, read once a frame (pollJoystick).
    if (ev.type == EventType::JoyButtonDown || ev.type == EventType::JoyButtonUp || ev.type == EventType::JoyAxis ||
        ev.type == EventType::JoyHat || ev.type == EventType::JoyRemoved) {
        joystick_.event(ev);
        return;
    }
    if (typing_ != Typing::None) {
        // Typing takes the keys and characters; the mouse still looks.
        if (ev.type == EventType::MouseMove) {
            input_.addMouse(ev.moveX, ev.moveY);
        } else {
            typingEvent(ev);
        }
        return;
    }
    if (menuOpen_) {
        // Menus that never took the keys (their key catcher) can't close
        // themselves: Esc does.
        // A held Esc's repeats don't close what its press opened.
        if (ev.type == EventType::KeyDown && ev.key == key::Escape && ev.repeat)
            return;
        if (ev.type == EventType::KeyDown && ev.key == key::Escape && (!menuCaught_ || (ev.mods & keymod::Shift))) {
            // (Shift+Esc always: back to the match, whatever they show.)
            closeMenu();
            return;
        }
        menus_.handleEvent(ev);
        runMenuCommands();
        return;
    }
    if (ev.type == EventType::KeyDown && ev.key == key::Escape) {
        if (!ev.repeat)
            openMenu();
        return;
    }
    if (!renderer_ || names_.empty())
        return;
    if (ev.type == EventType::KeyDown && ev.key == '`' && !(ev.mods & keymod::Ctrl)) {
        // The console key, whatever it's bound to, as in the original.
        openTyping(Typing::Console, -1, '`');
        return;
    }
    switch (ev.type) {
    case EventType::KeyDown:
    case EventType::KeyUp: {
        const bool down = ev.type == EventType::KeyDown;
        if (down && (ev.mods & keymod::Ctrl)) {
            // Reluva's own: restart, another map, the info.
            const int n = int(names_.size());
            if (ev.key == 'r') {
                start(index_);
                return;
            }
            if (ev.key == key::PageUp || ev.key == key::PageDown) {
                start((index_ + (ev.key == key::PageUp ? n - 1 : 1)) % n);
                return;
            }
            if (ev.key == 'i') {
                showInfo_ = !showInfo_;
                return;
            }
        }
        if (down && ev.repeat)
            return;
        // The key's character, in case its command opens typing (so it
        // isn't typed as well).
        bindingKeyChar_ = ev.key < 128 ? ev.key : 0;
        if (const int32_t k = toQ3Key(ev.key))
            runBinding(ctx_.bindings.get(k), k, down);
        bindingKeyChar_ = 0;
        break;
    }
    case EventType::MouseDown:
    case EventType::MouseUp:
        if (const int32_t k = toQ3Button(ev.button))
            runBinding(ctx_.bindings.get(k), k, ev.type == EventType::MouseDown);
        break;
    case EventType::MouseWheel:
        if (ev.wheelY) {
            const int32_t k = ev.wheelY > 0 ? q3key::WheelUp : q3key::WheelDown;
            runBinding(ctx_.bindings.get(k), k, true);
            runBinding(ctx_.bindings.get(k), k, false);
        }
        break;
    case EventType::MouseMove:
        // Turned into looking once a frame (runGame), as the settings say.
        input_.addMouse(ev.moveX, ev.moveY);
        break;
    default: break;
    }
}

void PlayViewer::runGame(uint64_t nowMs) {
    if (!client_ && (!game_ || !cgame_))
        return;
    const int32_t dt = lastMs_ ? int32_t(std::min<uint64_t>(nowMs - lastMs_, 100)) : 0;
    lastMs_ = nowMs;
    // Looking, by the mouse and keys at the menus' Controls settings.
    input_.turn(cvars_, dt);
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point from) {
        return std::chrono::duration<float, std::milli>(Clock::now() - from).count();
    };
    if (client_) {
        const auto start = Clock::now();
        runRemote(nowMs, dt);
        timing_.server = ms(start);
        if (!cgame_ || !client_ || !client_->hasSnapshot())
            return;
    } else {
        if (!runLocal(nowMs, dt))
            return;
    }
    cgame_->clearScene();
    cgame_->clearPictures();
    if (soundSystem_)
        soundSystem_->setTime(nowMs);
    int32_t ignored = 0;
    const auto clientStart = Clock::now();
    // (Its third argument says a demo is playing: no prediction from the
    // player's commands, which there aren't.)
    if (Error err = cgame_->call(eliteforce::CgameHost::DrawActiveFrame, {clientTime_, 0, playingDemo() ? 1 : 0},
                                 ignored)) {
        stop("The client game stopped: " + err.message);
        return;
    }
    timing_.client = ms(clientStart);
    runClientGameCommands();
    if (!cgame_)
        return;
    if (soundSystem_) {
        // At the original's settings: effects (s_volume) 0.8 and music
        // (s_musicvolume) 0.25 unless the menus set them.
        auto setting = [&](const char* name, float fallback) {
            return cvars_.find(name) ? cvars_.value(name) : fallback;
        };
        soundSystem_->setVolumes(setting("s_volume", 0.8f), setting("s_musicvolume", 0.25f));
        soundSystem_->endFrame();
    }
}

void PlayViewer::runClientGameCommands() {
    // What the client game asked for this frame (and in the console
    // commands it handled since): its commands for the server as the
    // player's ("score", asking for the scoreboard), sent as the
    // original's client sends them; and console commands, run as typed.
    for (const std::string& command : cgame_->takeClientCommands()) {
        if (client_) {
            if (!client_->playingDemo())
                client_->addClientCommand(command);
        } else if (game_) {
            if (Error err = game_->clientCommand(0, command)) {
                stop("The server game stopped: " + err.message);
                return;
            }
        }
    }
    for (const std::string& line : cgame_->takeConsoleCommands())
        for (const std::string& command : splitCommands(line)) {
            consoleCommand(command);
            if (!cgame_)
                return;
        }
}

eliteforce::GameHost::UserCommand PlayViewer::buildCommand() {
    // The player's command for this moment, as a client sends one each
    // frame: where they look, how they move, what they press.
    eliteforce::GameHost::UserCommand cmd;
    const PlayerInput::Command c = input_.command(cvars_, ctx_.game);
    cmd.angles = c.angles;
    cmd.buttons = c.buttons;
    cmd.weapon = uint8_t(cgame_ ? cgame_->weaponSelect() : 0);
    cmd.forward = c.forward;
    cmd.right = c.right;
    cmd.up = c.up;
    return cmd;
}

bool PlayViewer::runLocal(uint64_t nowMs, int32_t dt) {
    if (dt > 0)
        clientTime_ += dt;
    // The server first, kept a frame ahead of the player's time, so the
    // client game always has the snapshot after it to move things towards;
    // then the player's command, stamped with a time the server has
    // reached, as the original's client stamps commands with the
    // snapshots' time. Given first, a command was up to a frame ahead of
    // the server, and with long frames (50 ms and more) the player's
    // weapon stopped firing and their sounds stopped (game-run -events).
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point from) {
        return std::chrono::duration<float, std::milli>(Clock::now() - from).count();
    };
    const auto serverStart = Clock::now();
    // Players on other machines: what they sent first, then, after each
    // of the game's frames, what they're sent.
    if (server_)
        if (Error err = server_->receive(nowMs)) {
            stop("The server game stopped: " + err.message);
            return false;
        }
    while (game_->levelTime() <= clientTime_ && !game_->levelRequested()) {
        if (Error err = game_->runFrame()) {
            stop("The server game stopped: " + err.message);
            return false;
        }
        if (server_)
            if (Error err = server_->frame(nowMs)) {
                stop("The server game stopped: " + err.message);
                return false;
            }
        sendServerCommands();
        cgame_->newSnapshot(localSnapshot());
    }
    timing_.server = ms(serverStart);
    // A new level the game (the end of a match, a vote) or a player asked for.
    if (game_->levelRequested())
        return changeLevel(game_->takeRequestedMap());
    if (dt > 0) {
        // The game moves the player by their command at once, as the
        // original server does.
        eliteforce::GameHost::UserCommand cmd = buildCommand();
        cmd.serverTime = clientTime_;
        if (Error err = game_->clientThink(0, cmd)) {
            stop("The server game stopped: " + err.message);
            return false;
        }
        cgame_->addUserCommand(cmd);
    }
    // The last two seconds' frames, for the info line: each frame's length,
    // whether it sent a command, how many snapshots, and the player's event
    // sequence (player state word 108) as the server has it.
    recentFrames_.push_back({nowMs, dt, dt > 0, game_->snapshotsMade(), game_->clientInt(0, 108)});
    while (!recentFrames_.empty() && recentFrames_.front().ms + 2000 < nowMs)
        recentFrames_.pop_front();
    return true;
}

void PlayViewer::pollJoystick() {
    // As the original reads it each frame: its keys (buttons, and axes
    // and hat taken as keys) run their bindings, or go to the menus over
    // the match; its analog axes move and turn the player.
    const JoystickInput::Frame f = joystick_.frame(cvars_);
    const bool playing = !menuOpen_ && typing_ == Typing::None;
    input_.setJoystick(playing ? f.x : 0, playing ? f.y : 0);
    for (const auto& [k, down] : f.keys) {
        if (menuOpen_) {
            menus_.key(k, down);
            runMenuCommands();
        } else if (typing_ == Typing::None) {
            runBinding(ctx_.bindings.get(k), k, down);
        }
        if (!renderer_)
            return;
    }
}

void PlayViewer::update(uint64_t nowMs) {
    if (videoRestart_)
        restartVideo();
    if (!renderer_)
        return;
    pollJoystick();
    if (!renderer_)
        return;
    if (!shownMs_)
        shownMs_ = nowMs;
    runGame(nowMs);
    renderer_->setGamma(brightness(ctx_));
    const float time = float(clientTime_) / 1000.0f;

    // The client game's frame: each scene it rendered over the pictures
    // drawn before it, the rest of its pictures (the HUD) over them all.
    // The player's view is the first with the level in it.
    const auto drawStart = std::chrono::steady_clock::now();
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    bool haveView = false;
    if (cgame_) {
        const auto& scene = cgame_->scene();
        const auto& pictures = cgame_->pictures();
        const bool dynamicLights = !cvars_.find("r_dynamiclight") || cvars_.integer("r_dynamiclight") != 0;
        size_t drawn = 0;
        bool first = true;
        // r_drawentities 0: the level alone; r_drawEffects 0 (Reluva's):
        // without sprites, beams and marks.
        scene_->showModels = !cvars_.find("r_drawentities") || cvars_.integer("r_drawentities") != 0;
        scene_->showEffects = scene_->showModels && (!cvars_.find("r_drawEffects") || cvars_.integer("r_drawEffects") != 0);
        for (const auto& v : scene.views) {
            scene_->drawPictures(pictures, drawn, v.picturesBefore, 640, 480, time);
            drawn = std::max(drawn, v.picturesBefore);
            scene_->drawView(scene, v, 640, 480, time, first, world_.get(), &grid_, dynamicLights);
            first = false;
            if (!haveView && !(readInt(v.refdef, 76) & kNoWorldModel)) {
                haveView = true;
                eye_ = scene_->eye();
                shown_ = {scene_->placedCount(), scene_->effectCount(), scene_->lightCount(),
                          scene_->otherEntities(), scene_->unloadable(), scene_->effectSummary()};
            }
        }
        scene_->drawPictures(pictures, drawn, pictures.size(), 640, 480, time);
    }
    timing_.draw = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - drawStart).count();

    // The in-game menu over it all, until it closes itself.
    if (menuOpen_) {
        menus_.frame(nowMs);
        runMenuCommands();
        if (menuOpen_) {
            const bool caught = menus_.open();
            menuCaught_ |= caught;
            if (!menus_.running() || (menuCaught_ && !caught))
                closeMenu();
        }
    }

    drawOverlay(nowMs, haveView);
    renderer_->endFrame(&overlay_);
    if (screenshot_.wanted)
        saveScreenshot();
}

// The frame kept for the screenshot command, as the original's: in
// screenshots/ in the save folder, as the name given or the first of
// shot0000.tga to shot9999.tga not taken.
void PlayViewer::saveScreenshot() {
    const Screenshot shot = screenshot_;
    screenshot_ = {};
    std::vector<uint8_t> rgba;
    if (ctx_.savePath.empty() || !renderer_->takeCapture(rgba)) {
        addPrint("Couldn't take the screenshot\n", true);
        return;
    }
    const std::filesystem::path folder = ctx_.savePath / "screenshots";
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    std::string name = shot.name;
    if (name.empty())
        for (int i = 0; i <= 9999; ++i) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "shot%04d.tga", i);
            if (!std::filesystem::exists(folder / buf, ec)) {
                name = buf;
                break;
            }
        }
    else if (std::filesystem::path(name).extension() != ".tga")
        name += ".tga";
    if (name.empty() || std::filesystem::path(name).filename().string() != name) {
        addPrint("Couldn't take the screenshot: no name free\n", true);
        return;
    }
    image::Image img;
    img.w = renderer_->width();
    img.h = renderer_->height();
    img.rgba = std::move(rgba);
    const std::vector<uint8_t> tga = image::encodeTga(img);
    std::ofstream out(folder / name, std::ios::binary);
    out.write(reinterpret_cast<const char*>(tga.data()), std::streamsize(tga.size()));
    if (!out)
        addPrint("Couldn't write screenshots/" + name + "\n", true);
    else if (!shot.silent)
        addPrint("Wrote screenshots/" + name + "\n", true);
}

void PlayViewer::addPrint(const std::string& text, bool notify) {
    // Whole lines into the console (the last thousand); the client game's
    // also on screen, each 3 seconds (the original's con_notifytime), at
    // most the last 4.
    std::string& partial = printPartial_[notify ? 1 : 0];
    partial += text;
    size_t end;
    while ((end = partial.find('\n')) != std::string::npos) {
        const std::string line = partial.substr(0, end);
        partial.erase(0, end + 1);
        consoleLines_.push_back(line);
        while (consoleLines_.size() > 1000)
            consoleLines_.pop_front();
        if (consoleScroll_ > 0)
            ++consoleScroll_;
        if (!notify)
            continue;
        notify_.emplace_back(ctx_.sys.getMillis(), line);
        while (notify_.size() > 4)
            notify_.pop_front();
    }
}

void PlayViewer::drawColoured(int x, int y, const std::string& line) {
    // A line in the original's colours (^0-^7), with a shadow.
    static const Color kColors[8] = {{0, 0, 0},     {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                                     {0, 0, 255},   {0, 255, 255}, {255, 0, 255}, {255, 255, 255}};
    Color color{255, 255, 255};
    std::string run;
    auto flush = [&] {
        if (run.empty())
            return;
        font::drawText(overlay_, x + 1, y + 1, run, {0, 0, 0});
        font::drawText(overlay_, x, y, run, color);
        x += int(font::length(run)) * font::kAdvance;
        run.clear();
    };
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '^' && i + 1 < line.size() && line[i + 1] != '^') {
            flush();
            color = kColors[(line[i + 1] - '0') & 7];
            ++i;
            continue;
        }
        run += line[i];
    }
    flush();
}

void PlayViewer::drawOverlay(uint64_t nowMs, bool haveView) {
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    // (A line printed during this frame is stamped a little after nowMs.)
    while (!notify_.empty() && notify_.front().first + 3000 < nowMs)
        notify_.pop_front();
    const int lh = font::kLineHeight;
    const bool blink = (nowMs / 250) % 2 == 0;
    if (typing_ == Typing::Console) {
        // The console over the top half: the lines printed, newest at the
        // bottom (PageUp/PageDown scroll), and what's being typed.
        const int height = frameH_ / 2;
        for (int yy = 0; yy < height; ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int x = 0; x < frameW_; ++x) {
                p[x * 4 + 0] = 10, p[x * 4 + 1] = 14, p[x * 4 + 2] = 24;
                p[x * 4 + 3] = 215;
            }
        }
        for (int x = 0; x < frameW_; ++x) {
            uint8_t* p = overlay_.row(height) + x * 4;
            p[0] = 200, p[1] = 140, p[2] = 40, p[3] = 255;
        }
        int y = height - 2 * lh - 4;
        drawColoured(4, height - lh - 4, "]" + field_.substr(0, cursor_) + (blink ? "_" : " ") + field_.substr(cursor_));
        for (int i = int(consoleLines_.size()) - 1 - consoleScroll_; i >= 0 && y >= 0; --i, y -= lh)
            drawColoured(4, y, consoleLines_[size_t(i)]);
    } else {
        // Joining a server: how far it has got, in the middle.
        if (client_ && (!cgame_ || !client_->hasSnapshot())) {
            static const char* const kStates[] = {"", "Disconnected", "", "Asking for a challenge", "Connecting",
                                                  "Waiting for the level", "", "Loading the level", "Joining"};
            const auto st = size_t(client_->state());
            std::string status = (st < 9 ? kStates[st] : "") + std::string(" - ") + serverAddress_;
            if (client_->downloading())
                status = "Downloading " + client_->downloadName() + ": " +
                         std::to_string(client_->downloadCount() / 1024) + " of " +
                         (client_->downloadSize() < 0 ? std::string("?")
                                                      : std::to_string(client_->downloadSize() / 1024)) +
                         " KB";
            if (client_->state() <= eliteforce::NetClient::State::Challenging)
                status += " (" + std::to_string(client_->connectPackets()) + ")";
            const int x = std::max(4, (frameW_ - int(font::length(status)) * font::kAdvance) / 2);
            drawColoured(x, frameH_ / 2 - lh, status);
            if (!client_->message().empty()) {
                const int mx = std::max(4, (frameW_ - int(font::length(client_->message())) * font::kAdvance) / 2);
                drawColoured(mx, frameH_ / 2 + lh, client_->message());
            }
        }
        // The notify lines at the top, then a chat line being typed.
        int notifyY = 4;
        for (const auto& [when, line] : notify_) {
            drawColoured(4, notifyY, line);
            notifyY += lh;
        }
        if (typing_ != Typing::None) {
            const char* prompt = typing_ == Typing::Say ? "Say: " : typing_ == Typing::SayTeam ? "Say Team: " : "Tell: ";
            drawColoured(4, notifyY, std::string("^7") + prompt + field_.substr(0, cursor_) + (blink ? "_" : " ") +
                                         field_.substr(cursor_));
        }
    }
    std::vector<std::string> lines;
    if (!error_.empty()) {
        lines.push_back(error_);
        lines.push_back("Ctrl+R: start the level again, PageUp/PageDown: another map.");
    }
    if (showInfo_) {
        char line[256];
        if (game_) {
            std::snprintf(line, sizeof line, "time %.1f s, eye at %.0f %.0f %.0f%s; %zu models, %zu effects and %zu lights drawn, %d other entities not yet",
                          float(clientTime_) / 1000.0f, eye_[0], eye_[1], eye_[2],
                          haveView ? "" : " (no view yet)", shown_.models, shown_.effects, shown_.lights,
                          shown_.other);
            lines.push_back(line);
            if (!shown_.effectShaders.empty())
                lines.push_back("effects: " + shown_.effectShaders);
            // What the player's commands carry, to see that keys arrive.
            std::string held;
            for (const auto& [name, keys] : input_.buttons())
                if (!keys.empty())
                    held += " +" + name;
            std::snprintf(line, sizeof line, "weapon chosen by the client game: %d; held:%s",
                          cgame_ ? cgame_->weaponSelect() : 0, held.empty() ? " nothing" : held.c_str());
            lines.push_back(line);
            // Where each frame's time goes, smoothed, to find what's slow.
            std::snprintf(line, sizeof line,
                          "frame: server and bots %.1f ms, client game %.1f ms, drawing %.1f ms; %zu sounds playing",
                          timing_.server, timing_.client, timing_.draw,
                          soundSystem_ ? soundSystem_->playingCount() : size_t(0));
            lines.push_back(line);
            if (recentFrames_.size() > 1) {
                const auto& first = recentFrames_.front();
                const auto& last = recentFrames_.back();
                int commands = 0, longest = 0;
                for (const auto& f : recentFrames_) {
                    commands += f.command;
                    longest = std::max(longest, f.dt);
                }
                std::snprintf(line, sizeof line,
                              "last 2 s: %zu frames (%.1f ms each, the longest %d), %d commands, %d snapshots, %d "
                              "player events on the server",
                              recentFrames_.size(), float(last.ms - first.ms) / float(recentFrames_.size() - 1), longest,
                              commands, last.snapshots - first.snapshots, last.events - first.events);
                lines.push_back(line);
            }
            if (soundSystem_) {
                for (const std::string& r : soundSystem_->recent())
                    lines.push_back("sound: " + (r.size() > 140 ? r.substr(0, 140) + "..." : r));
                std::string loops = "looping:";
                for (const std::string& l : soundSystem_->loopingNow())
                    loops += " " + l;
                lines.push_back(loops.size() > 150 ? loops.substr(0, 150) + "..." : loops);
            }
            if (soundSystem_ && !soundSystem_->missing().empty()) {
                // Their names, the voices' (which the game files lack, as the
                // original's) left out, then why the last one failed.
                std::string names;
                size_t voices = 0;
                for (const std::string& n : soundSystem_->missing()) {
                    if (n.rfind("sound/voice/", 0) == 0)
                        ++voices;
                    else
                        names += " " + n;
                }
                lines.push_back(std::to_string(soundSystem_->missing().size()) + " sounds couldn't be played (" +
                                std::to_string(voices) + " voices):" + (names.size() > 120 ? names.substr(0, 120) + "..." : names));
                if (const auto& problems = soundLibrary_.problems(); !problems.empty())
                    lines.push_back("  last: " + (problems.back().size() > 150 ? problems.back().substr(0, 150) + "..."
                                                                               : problems.back()));
            }
            if (world_) {
                const WorldRenderer::Visibility v = world_->visibility();
                std::snprintf(line, sizeof line,
                              "visibility: cluster %d, %zu of %zu surfaces drawn (%zu past the vis data and doors)%s, "
                              "%d areas hidden",
                              v.cluster, v.visible, v.total, v.potentiallyVisible,
                              v.culling ? "" : " (all: no culling)", v.hiddenAreas);
                lines.push_back(line);
                std::snprintf(line, sizeof line, "mirror and portal views drawn so far: %d%s", scene_->portalViews(),
                              renderer_->hasStencil() ? "" : " (none: no stencil buffer)");
                lines.push_back(line);
            }
            if (game_) {
                std::snprintf(line, sizeof line, "%d areas; sight checks a closed door blocked: %d%s",
                              game_->areaCount(), game_->closedDoorBlocks(),
                              cvars_.integer("cm_noAreas") ? " (cm_noAreas 1: areas ignored)" : "");
                lines.push_back(line);
            }
            // Network play: who has joined this level, or the server played on.
            if (server_) {
                std::string players = "Open to others on port " + std::to_string(server_->port()) + ";";
                const auto joined = server_->players();
                if (joined.empty())
                    players += " no one has joined";
                for (const auto& [number, name] : joined)
                    players += " " + std::to_string(number) + ": " + name;
                lines.push_back(players);
            } else if (client_) {
                lines.push_back("Playing on " + serverAddress_ + " (" + client_->server().toString() + ")");
            } else if (game_ && game_->layout().version != 12) {
                lines.push_back("Not open to others: network play needs patch 1.2");
            }
            if (!shown_.unloadable.empty()) {
                std::string names = "Models not drawn (only .md3 and .mdr):";
                for (const std::string& n : shown_.unloadable)
                    names += " " + n;
                lines.push_back(names.size() > 150 ? names.substr(0, 150) + "..." : names);
            }
        }
        for (const auto* host : {"server", "client"}) {
            const std::map<int, int>* unhandled = nullptr;
            if (host[0] == 's' && game_)
                unhandled = &game_->unhandled();
            else if (host[0] == 'c' && cgame_)
                unhandled = &cgame_->unhandled();
            if (!unhandled || unhandled->empty())
                continue;
            std::string calls = std::string("The ") + host + " game's system calls not implemented yet:";
            for (const auto& [number, count] : *unhandled)
                calls += " " + std::to_string(number);
            lines.push_back(calls);
        }
        if (!notice_.empty())
            lines.push_back(notice_);
    }
    if (menuOpen_)
        lines.push_back(std::string("The game's in-game menu (menu ") + std::to_string(MenuLayer::kInGameMenu) + "): " +
                        (menuCaught_ ? "it has the keys" : "it hasn't taken the keys") +
                        ". Shift+Esc: back to the match.");
    if (!lastMenuCommand_.empty() && nowMs - lastMenuCommandMs_ < kHintMs)
        lines.push_back("The in-game menu ran: " + lastMenuCommand_);
    if (nowMs - shownMs_ < kHintMs) {
        lines.push_back("Keys do what they're bound to (set in the menus' Controls). The mouse looks. ~: the console.");
        lines.push_back("Ctrl+PageUp/PageDown: another map, Ctrl+I: info, Ctrl+R: restart, Esc: the menus, Ctrl+Esc: launcher.");
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = frameH_ - int(lines.size() - i) * lh - 4;
        const int w = std::min(frameW_, int(font::length(lines[i])) * font::kAdvance + 8);
        for (int yy = y - 2; yy < y + lh; ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int x = 0; x < w; ++x)
                p[x * 4 + 3] = 170;
        }
        font::drawText(overlay_, 4, y, lines[i], i == 0 && !error_.empty() ? Color{255, 120, 120} : Color{230, 230, 230});
    }
}

} // namespace reluva::idtech3
