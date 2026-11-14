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
#include "engines/idtech3/formats/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/client/scene_drawer.h"
#include "engines/idtech3/client/play_page.h"

#include "graphics/font.h"
#include "image/image.h"
#include "common/developer.h"
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
// (Elite Force's own), found with reluva-tool idtech3 game-run -buttons: holding
// either changes the same player state words (weapon time, ammunition,
// events). See docs/engines/idtech3/games/stvef/systems/syscalls.md.

} // namespace

PlayPage::PlayPage(PageContext &ctx) : Page(ctx) {
    if (ctx.game.code != GameCode::kEliteForce || !ctx.files.has("vm/qagame.qvm") || !ctx.files.has("vm/cgame.qvm"))
        return;
    for (const std::string &n : ctx.files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.rfind("maps/", 0) == 0 && key.size() > 4 && key.compare(key.size() - 4, 4, ".bsp") == 0)
            _names.push_back(key);
    }
    std::sort(_names.begin(), _names.end());
    // Holomatch maps first.
    for (size_t i = 0; i < _names.size(); ++i)
        if (_names[i].rfind("maps/hm_", 0) == 0) {
            _index = int(i);
            break;
        }
}

PlayPage::~PlayPage() {
    deactivate();
}

bool PlayPage::setUpGraphics() {
    const VideoSettings video = videoSettings(_ctx.cvars);
    _frameW = video.width;
    _frameH = video.height;
    _ctx.sys.setVideoMode(_frameW, _frameH, video.fullscreen);
    if (Error err = _ctx.sys.init3D(_frameW, _frameH, _renderer)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Play page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return false;
    }
    _overlay.create(_frameW, _frameH, PixelFormat::kRGBA32);
    _shaders = std::make_unique<ShaderCache>(*_renderer, _ctx.files, _ctx.shaders);
    _world = std::make_unique<WorldRenderer>(*_renderer, *_shaders);
    _models = std::make_unique<ModelCache>(*_renderer, *_shaders, _ctx.files);
    applyVideoSettings(video, *_shaders, _world.get(), _models.get());
    _scene = std::make_unique<SceneDrawer>(*_renderer, *_shaders, *_models, _ctx.game.refEntity);
    _menus.attach(_renderer, _frameW, _frameH);
    return true;
}

void PlayPage::restartVideo() {
    _videoRestart = false;
    if (!_renderer || !_cgame)
        return;
    if (_menuOpen)
        closeMenu();
    // What draws through the renderer goes first, then the renderer
    // starts again; the menus too, as the original's (closed).
    _menus.detach();
    _menus.reset();
    _cgame.reset();
    stopSounds();
    _scene.reset();
    _models.reset();
    _world.reset();
    _shaders.reset();
    if (!setUpGraphics())
        return;
    _ctx.sys.setRelativeMouse(true);
    // The level's graphics and the client game, where the match is now:
    // the server's latest configstrings, and for a server elsewhere the
    // commands it has sent so far as done.
    const std::string map = infoValue(matchConfigstring(0), "mapname");
    std::shared_ptr<Bsp> bsp;
    if (!loadLevel(normalizeArchiveName("maps/" + map + ".bsp"), bsp))
        return;
    if (_client) {
        if (Error err = startClientGame(_client->configstrings(), 0, int32_t(_client->serverCommands().size())))
            stop("The client game stopped: " + err.message);
        return;
    }
    if (!_game)
        return;
    _localConfigstrings = _game->configstrings();
    _localCommands.clear();
    _localCommandsSeen = _game->serverCommands().size();
    if (Error err = startClientGame(_game->configstrings(), 0, 0)) {
        stop("The client game stopped: " + err.message);
        return;
    }
    _clientTime = _game->levelTime();
    _cgame->newSnapshot(localSnapshot());
}

void PlayPage::activate() {
    JoystickInput::registerCvars(_cvars);
    if (!setUpGraphics())
        return;
    _menuOpen = false;
    _videoRestart = false;
    _shownMs = 0;
    _ctx.sys.setRelativeMouse(true);
    // A demo the menus asked to play.
    if (!_ctx.requestDemo.empty()) {
        const std::string name = _ctx.requestDemo;
        _ctx.requestDemo.clear();
        playDemo(name);
        return;
    }
    // A server the menus asked to join.
    if (!_ctx.requestConnect.empty()) {
        const std::string address = _ctx.requestConnect;
        _ctx.requestConnect.clear();
        connect(address);
        return;
    }
    // A level the menus asked for.
    if (!_ctx.requestMap.empty()) {
        const std::string wanted = normalizeArchiveName("maps/" + _ctx.requestMap + ".bsp");
        for (size_t i = 0; i < _names.size(); ++i)
            if (_names[i] == wanted)
                _index = int(i);
        _ctx.requestMap.clear();
    }
    start(_index);
}

void PlayPage::deactivate() {
    closeTyping();
    if (_cgame || _client || _game)
        _ctx.matchEnded = true;
    if (_menuOpen)
        closeMenu();
    // The in-game menu goes with the match (started afresh on the next).
    _menus.detach();
    _menus.reset();
    if (_renderer)
        _ctx.sys.setRelativeMouse(false);
    // The level stops with the page; it starts again when shown.
    if (_client)
        _client->disconnect();
    _client.reset();
    _server.reset();
    _cgame.reset();
    _game.reset();
    stopSounds();
    _scene.reset();
    _models.reset();
    _world.reset();
    _shaders.reset();
    _renderer = nullptr;
    _input.releaseAll();
}

void PlayPage::reset() {
    _error.clear();
    if (_client)
        _client->disconnect();
    _client.reset();
    _server.reset();
    _cgame.reset();
    _game.reset();
    stopSounds();
    if (_scene)
        _scene->reset();
    _models->clear();
    _world->clear();
    _shaders->clear();
    _input.yaw = _input.pitch = 0;
}

bool PlayPage::loadLevel(const std::string &path, std::shared_ptr<Bsp> &bsp) {
    bsp = std::make_shared<Bsp>();
    std::vector<uint8_t> data;
    Error err = _ctx.files.read(path, data);
    if (!err)
        err = parseBsp(data, *bsp);
    if (!err)
        err = _world->load(*bsp, _ctx.shaders);
    if (err) {
        stop("Couldn't load " + path + ": " + err.message);
        return false;
    }
    // The light grid lights models, with the worldspawn's cell size if it
    // gives one.
    _grid = LightGrid{};
    float cell[3] = {64, 64, 128};
    std::vector<Entity> entities;
    if (!parseEntities(bsp->entities, entities) && !entities.empty())
        entities[0].vector("gridsize", cell);
    _grid.build(*bsp, {cell[0], cell[1], cell[2]});
    return true;
}

Error PlayPage::startClientGame(const std::map<int32_t, std::string> &configstrings, int32_t serverMessage,
                                int32_t commandSequence) {
    _cgame = std::make_unique<eliteforce::CgameHost>(_ctx.files, _cvars);
    _cgame->print = [this](const std::string &text) { addPrint(text, true); };
    _scene->setHandles({&_cgame->models(), &_cgame->skins(), &_cgame->shaders()});
    _cgame->subdivisions = videoSettings(_cvars).subdivisions;
    _notify.clear();
    _printPartial[1].clear();
    if (_ctx.mixer) {
        _soundSystem = std::make_unique<SoundSystem>(*_ctx.mixer, _soundLibrary);
        _cgame->soundSystem = _soundSystem.get();
    }
    Error err = _cgame->load();
    if (!err) {
        _cgame->setConfigstrings(configstrings);
        if (_client)
            _cgame->setServerCommands(_client->serverCommands());
        else
            sendServerCommands();
        _loadingFailed = false;
        _cgame->updateScreen = [this] { drawLoadingScreen(); };
        err = _cgame->init(0, serverMessage, commandSequence);
        _cgame->updateScreen = nullptr;
    }
    return err;
}

void PlayPage::start(int index) {
    _index = index;
    reset();
    const std::string &path = _names[size_t(index)];
    std::string map = path.substr(5, path.size() - 9); // without maps/ and .bsp
    setTitle(map + " (" + std::to_string(index + 1) + "/" + std::to_string(_names.size()) + ")");
    std::shared_ptr<Bsp> bsp;
    if (!loadLevel(path, bsp))
        return;

    // The server: the level, and the player joining it. The holodeck
    // intro, which holds new players in place, is off until there's a
    // way to watch it.
    _cvars.set("g_holoIntro", "0");
    _game = std::make_unique<eliteforce::GameHost>(_ctx.files, _cvars);
    _game->print = [this](const std::string &text) { addPrint(text, false); };
    _printPartial[0].clear();
    closeTyping();
    Error err = _game->load();
    _game->reserveClient(0); // the player's, though bots may be added first
    if (!err)
        err = _game->startLevel(map, bsp, int32_t(_ctx.sys.getMillis()));
    std::string refused;
    if (!err)
        err = _game->connectClient(0, userinfo() + "\\ip\\localhost", refused);
    if (!err && !refused.empty())
        err = Error::fail("the game refused the player: " + refused);
    if (!err)
        err = _game->beginClient(0);
    // What the menus asked to run once the level started, as the
    // original's console runs it: the server game's console commands
    // (adding bots...), and the rest sent to the server as the player's
    // own (the team chosen for a team game: "team red").
    for (const std::string &command : _ctx.requestCommands) {
        if (err)
            break;
        bool handled = false;
        err = _game->serverCommand(command, &handled);
        if (!err && !handled)
            err = _game->clientCommand(0, command);
    }
    _ctx.requestCommands.clear();
    if (err) {
        stop("The server game stopped: " + err.message);
        return;
    }
    startHosting();
    _localConfigstrings = _game->configstrings();
    _localCommands.clear();
    _localCommandsSeen = 0;
    _localRestartsSeen = _game->restarts();

    // The player's client game, given what the server has set.
    if (Error e = startClientGame(_game->configstrings(), 0, 0)) {
        stop("The client game stopped: " + e.message);
        return;
    }
    _clientTime = _game->levelTime();
    _lastMs = 0;
    _cgame->newSnapshot(localSnapshot());
}

bool PlayPage::changeLevel(const std::string &map) {
    // As the original's map command on a server with players: the level
    // loaded, the game started afresh on it, the players and bots kept;
    // those on other machines are sent the new level's game state.
    const std::string path = normalizeArchiveName("maps/" + map + ".bsp");
    if (!_ctx.files.has(path)) {
        addPrint("Can't find map " + path + "\n", false);
        return true;
    }
    _cgame.reset();
    stopSounds();
    if (_scene)
        _scene->reset();
    _models->clear();
    _world->clear();
    _shaders->clear();
    std::shared_ptr<Bsp> bsp;
    if (!loadLevel(path, bsp))
        return false;
    for (size_t i = 0; i < _names.size(); ++i)
        if (_names[i] == path)
            _index = int(i);
    setTitle(map + " (" + std::to_string(_index + 1) + "/" + std::to_string(_names.size()) + ")");
    std::vector<int32_t> players;
    if (Error err = _game->changeLevel(map, bsp, int32_t(_ctx.sys.getMillis()), players)) {
        stop("The server game stopped: " + err.message);
        return false;
    }
    // The page's own player is in the level at once.
    if (std::find(players.begin(), players.end(), 0) != players.end())
        if (Error err = _game->beginClient(0)) {
            stop("The server game stopped: " + err.message);
            return false;
        }
    if (_server)
        _server->levelStarted();
    _localConfigstrings = _game->configstrings();
    _localCommands.clear();
    _localCommandsSeen = _game->serverCommands().size();
    _localRestartsSeen = _game->restarts();
    if (Error err = startClientGame(_game->configstrings(), 0, 0)) {
        stop("The client game stopped: " + err.message);
        return false;
    }
    _clientTime = _game->levelTime();
    _cgame->newSnapshot(localSnapshot());
    return false; // this frame's done
}

bool PlayPage::startDownloads() {
    // As the original's client: the paks the server lists as the level's
    // (sv_referencedPaks, by checksum, and their names) that it has none
    // of, but Raven's own, which are never downloaded; with
    // cl_allowDownload, downloaded; without it, only named.
    const auto &cs = _client->configstrings();
    const std::string info = cs.count(1) ? cs.at(1) : std::string();
    std::istringstream sums(infoValue(info, "sv_referencedPaks"));
    std::istringstream names(infoValue(info, "sv_referencedPakNames"));
    std::set<int32_t> ours;
    for (const Archive *a : _ctx.files.archives())
        if (const auto *pak = dynamic_cast<const ZipArchive *>(a))
            ours.insert(pakChecksum(*pak));
    _downloads.clear();
    std::string sum;
    std::string name;
    while (sums >> sum && names >> name) {
        const auto checksum = int32_t(std::strtoll(sum.c_str(), nullptr, 10));
        const std::string lower = fs::toLower(name);
        const bool raven =
            lower.size() == 10 && lower.rfind("baseef/pak", 0) == 0 && lower[9] >= '0' && lower[9] <= '3';
        if (ours.count(checksum) || raven)
            continue;
        _downloads.emplace_back(name + ".pk3", checksum);
    }
    if (_downloads.empty())
        return false;
    _cvars.create("cl_allowDownload", "0", CvarStore::kArchive);
    if (!_cvars.integer("cl_allowDownload")) {
        std::string missing;
        for (const auto &[file, checksum] : _downloads)
            missing += " " + file;
        addPrint("You're missing files the server uses:" + missing +
                     "\nYou might not be able to play. Set cl_allowDownload 1 to download them.\n",
                 true);
        _downloads.clear();
        return false;
    }
    addPrint("Downloading " + std::to_string(_downloads.size()) + " file(s) from the server\n", true);
    _client->download(_downloads.front().first);
    return true;
}

bool PlayPage::finishDownload(const std::string &name, const std::vector<uint8_t> &data) {
    // Kept in the save folder's game folder (the base game's, or the mod
    // played's), as the original keeps them in its home folder: a name with
    // no way out of it, checked against the checksum the server gave before
    // it's used.
    if (_downloads.empty() || _downloads.front().first != name)
        return true;
    const int32_t expected = _downloads.front().second;
    _downloads.erase(_downloads.begin());
    const std::filesystem::path relative(name);
    const std::string folder = fs::toLower(relative.parent_path().string());
    if (_ctx.homePath.empty() || relative.is_absolute() || name.find("..") != std::string::npos ||
        (folder != fs::toLower(_ctx.game.baseDir) && (_ctx.gameDir.empty() || folder != fs::toLower(_ctx.gameDir))) ||
        fs::toLower(relative.extension().string()) != ".pk3") {
        stop("Won't keep the download " + name);
        return false;
    }
    const std::filesystem::path path = _ctx.homePath / relative;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    {
        std::ofstream out(path.string() + ".tmp", std::ios::binary);
        out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
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
    if (!_ctx.addPak || _ctx.addPak(path)) {
        stop("Couldn't use " + name);
        return false;
    }
    _shaders->clear(); // the shader scripts were read again
    addPrint("Downloaded " + name + "\n", true);
    if (!_downloads.empty())
        _client->download(_downloads.front().first);
    else
        _client->addClientCommand("donedl"); // the game state again
    return true;
}

std::string PlayPage::userinfo() const {
    // The player's settings the menus keep (name, model...), as the
    // original sends them: every user-info setting.
    std::string info;
    for (const CvarStore::Cvar &cv : _cvars.all())
        if ((cv.flags & CvarStore::kUserInfo) && !cv.value.empty())
            setInfoValue(info, cv.name, cv.value);
    auto fallback = [&](const char *key, const char *value) {
        if (infoValue(info, key).empty())
            setInfoValue(info, key, value);
    };
    fallback("name", "Reluva");
    fallback("model", _ctx.game.playerModel.c_str());
    fallback("handicap", "100");
    fallback("snaps", "20");
    fallback("rate", "25000");
    fallback("sex", "male");
    fallback("color", "4");
    return info;
}

void PlayPage::startHosting() {
    // Others can join over the network: on the original's port (or the
    // next of its four, if taken), only with patch 1.2's game code,
    // whose layouts the protocol carries.
    if (_game->layout().version != 12) {
        addPrint("Not open to other players: network play needs Elite Force's patch 1.2 (pak1-pak3.pk3)\n", false);
        return;
    }
    std::unique_ptr<net::UdpSocket> socket;
    for (int p = 0; p < ServerBrowser::kServerPorts && !socket; ++p)
        if (_ctx.sys.openUdp(uint16_t(ServerBrowser::kServerPort + p), socket))
            socket.reset();
    if (!socket) {
        addPrint("Not open to other players: no network\n", false);
        return;
    }
    _server = std::make_unique<eliteforce::NetServer>(*_game, _cvars, std::move(socket));
    _server->print = [this](const std::string &text) { addPrint(text, false); };
    _server->resolveHost = [this](const std::string &host, uint32_t &ip) { return _ctx.sys.resolveHost(host, ip); };
    _server->reserve(0);
    _server->setFiles(&_ctx.files);
    _server->levelStarted();
    addPrint("Open to other players on port " + std::to_string(_server->port()) + "\n", false);
}

void PlayPage::connect(const std::string &address) {
    reset();
    setTitle("Connecting to " + address);
    const auto [host, port] = net::splitHostPort(address, ServerBrowser::kServerPort);
    uint32_t ip = 0;
    if (!_ctx.sys.resolveHost(host, ip)) {
        stop("Couldn't find " + host);
        return;
    }
    std::unique_ptr<net::UdpSocket> socket;
    if (Error err = _ctx.sys.openUdp(0, socket)) {
        stop("No network: " + err.message);
        return;
    }
    // The qport tells the server it's still us if a router changes our port.
    const auto qport = uint16_t(_ctx.sys.getMillis() * 2654435761u >> 16);
    _serverAddress = address;
    _joinStartMs = _ctx.sys.getMillis();
    _joinState = eliteforce::NetClient::State::kDisconnected;
    addPrint("Joining " + address + "\n", false);
    _client = std::make_unique<eliteforce::NetClient>(std::move(socket), net::Address{ip, port}, userinfo(), qport);
    _lastMs = 0;
}

void PlayPage::playDemo(const std::string &name) {
    reset();
    std::string path = normalizeArchiveName("demos/" + name);
    if (path.size() < 7 || path.compare(path.size() - 7, 7, ".efdemo") != 0)
        path += ".efdemo";
    std::vector<uint8_t> data;
    if (Error err = _ctx.files.read(path, data)) {
        stop("Couldn't open " + path);
        return;
    }
    setTitle("Demo " + name);
    _serverAddress.clear();
    _joinStartMs = _ctx.sys.getMillis();
    _joinState = eliteforce::NetClient::State::kDisconnected;
    _client = std::make_unique<eliteforce::NetClient>(std::move(data));
    _lastMs = 0;
}

bool PlayPage::playingDemo() const {
    return _client && _client->playingDemo();
}

void PlayPage::endDemo() {
    // No error to show; the menus go back to their main menu.
    stop("");
    _error.clear();
    _ctx.matchEnded = true;
}

void PlayPage::runRemote(uint64_t nowMs, int32_t dt) {
    // Playing on someone else's server: what it sends, a level loaded
    // when a game state comes, its snapshots for the client game, and the
    // player's commands back.
    _client->frame(nowMs);
    // How long each step of joining took, for the console.
    if (_client->state() != _joinState && !_client->playingDemo()) {
        _joinState = _client->state();
        static const char *const kSteps[] = {"",
                                             "disconnected",
                                             "",
                                             "asking for a challenge",
                                             "connecting",
                                             "waiting for the level",
                                             "",
                                             "loading the level",
                                             "level loaded, joining the game"};
        const auto st = size_t(_joinState);
        char line[128];
        std::snprintf(line, sizeof line, "%.2f s: %s\n", double(nowMs - _joinStartMs) / 1000,
                      st < 9 ? kSteps[st] : "?");
        addPrint(line, true); // on screen for a few seconds too
    }
    if (_client->demoFinished()) {
        // The end of a demo: back to the menus, as the original.
        endDemo();
        _ctx.requestPage = "Menu";
        return;
    }
    if (_client->state() == eliteforce::NetClient::State::kDisconnected) {
        // Back to the menus, which show why, as the original.
        const std::string why = _client->message().empty() ? "Server disconnected" : _client->message();
        _client.reset();
        stop(why);
        _cvars.create("com_errorMessage", "", 0);
        _cvars.set("com_errorMessage", why);
        _ctx.requestPage = "Menu";
        return;
    }
    // A pak finished downloading: kept, checked, and the next, or done.
    {
        std::string name;
        std::vector<uint8_t> data;
        if (_client->takeDownload(name, data) && !finishDownload(name, data))
            return;
    }
    if (_client->takeNewGamestate()) {
        // A server playing a mod (fs_game in its system info) other than
        // this one's: as the original's client, the mod taken up, the
        // game's files started again (the engine, at requestRestart) and
        // the server joined again.
        if (!_client->playingDemo()) {
            const auto &info = _client->configstrings();
            const std::string serverMod =
                modFolderName(info.count(1) ? infoValue(info.at(1), "fs_game") : std::string(), _ctx.game.baseDir);
            if (serverMod != _ctx.gameDir) {
                _cvars.set("fs_game", serverMod, true);
                _ctx.requestConnect = _serverAddress;
                _ctx.requestRestart = true;
                _client->disconnect();
                _client.reset();
                return;
            }
        }
        // Paks the server uses that this player hasn't: downloaded first
        // (the level loads when the server sends its game state again).
        if (!_client->playingDemo() && startDownloads())
            return;
        const auto &cs = _client->configstrings();
        const std::string map = cs.count(0) ? infoValue(cs.at(0), "mapname") : std::string();
        _cgame.reset();
        stopSounds();
        if (_scene)
            _scene->reset();
        _models->clear();
        _world->clear();
        _shaders->clear();
        if (!_client->playingDemo())
            setTitle(map + " on " + _serverAddress);
        std::shared_ptr<Bsp> bsp;
        if (!loadLevel(normalizeArchiveName("maps/" + map + ".bsp"), bsp))
            return;
        if (Error err = startClientGame(cs, _client->gamestateMessage(), _client->gamestateCommandSequence())) {
            stop("The client game stopped: " + err.message);
            return;
        }
        // The pure check, before the first move: which paks the client
        // game, the menus and the rest come from (a server that isn't
        // pure ignores it).
        const std::string paks = cs.count(1) ? infoValue(cs.at(1), "sv_paks") : std::string();
        if (const std::string cp = pureCommand(_ctx.files, _client->checksumFeed(), paks);
            !cp.empty() && !_client->playingDemo())
            _client->addClientCommand(cp);
        _client->setLoaded();
    }
    if (!_cgame)
        return;
    _cgame->setServerCommands(_client->serverCommands());
    for (eliteforce::GameHost::Snapshot &snap : _client->takeSnapshots()) {
        // The player's number (playerState_t's clientNum, byte 156), for
        // the in-game menu.
        if (snap.playerState.size() >= 160)
            std::memcpy(&_playerNumber, snap.playerState.data() + 156, 4);
        _cgame->newSnapshot(std::move(snap));
    }
    // Commands go out as soon as the level is loaded, as the original's
    // client sends them: the first puts the player in the game.
    if (_client->hasSnapshot())
        _clientTime = _client->serverTime();
    if (dt > 0 && !_client->playingDemo()) {
        eliteforce::GameHost::UserCommand cmd = buildCommand();
        cmd.serverTime = _client->hasSnapshot() ? _clientTime : 0;
        _client->addCommand(cmd);
        if (_client->hasSnapshot())
            _cgame->addUserCommand(cmd);
    }
}

void PlayPage::drawLoadingScreen() {
    // As the original: the client game's frame drawn now, in the middle of
    // its loading, which only draws what it's loading (the level's picture,
    // its name, the media so far); then shown.
    if (!_cgame || !_renderer || _loadingFailed)
        return;
    _cgame->clearPictures();
    _cgame->clearScene();
    int32_t ignored = 0;
    if (_cgame->call(eliteforce::CgameHost::kDrawActiveFrame, {_game ? _game->levelTime() : 0, 0, 0}, ignored)) {
        _loadingFailed = true; // it can't yet; the level goes on loading regardless
        return;
    }
    _renderer->setGamma(brightness(_ctx));
    _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    _scene->drawPictures(_cgame->pictures(), 0, _cgame->pictures().size(), 640, 480, 0);
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    _renderer->endFrame(&_overlay);
    _cgame->clearPictures();
    _cgame->clearScene();
}

void PlayPage::stop(const std::string &why) {
    _error = why;
    const std::string log = _cgame ? _cgame->log() : _game ? _game->log() : std::string();
    if (!log.empty()) {
        std::string last = log;
        while (!last.empty() && last.back() == '\n')
            last.pop_back();
        _error +=
            " (last printed: " + last.substr(last.rfind('\n') == std::string::npos ? 0 : last.rfind('\n') + 1) + ")";
    }
    if (_client)
        _client->disconnect();
    _client.reset();
    _server.reset();
    _cgame.reset();
    _game.reset();
    stopSounds();
}

void PlayPage::stopSounds() {
    // After the client game, which uses it; the mixer's volumes back as
    // the other pages expect them.
    _soundSystem.reset();
    if (_ctx.mixer) {
        _ctx.mixer->setTypeVolume(audio::SoundType::kEffects, audio::kMaxVolume);
        _ctx.mixer->setTypeVolume(audio::SoundType::kMusic, audio::kMaxVolume);
    }
}

void PlayPage::sendServerCommands() {
    // What the server would send the player over the network, in order:
    // the configstrings that changed (cs, bcs: the client game sets up a
    // player who joined by theirs), then the game's commands to them.
    for (std::string &change : eliteforce::configstringCommands(_localConfigstrings, _game->configstrings()))
        _localCommands.push_back(std::move(change));
    _localConfigstrings = _game->configstrings();
    const auto &commands = _game->serverCommands();
    for (; _localCommandsSeen < commands.size(); ++_localCommandsSeen)
        if (commands[_localCommandsSeen].first == 0 || commands[_localCommandsSeen].first == -1)
            _localCommands.push_back(commands[_localCommandsSeen].second);
    // A map_restart, which the client game starts its level over for.
    if (_game->restarts() != _localRestartsSeen) {
        _localRestartsSeen = _game->restarts();
        _localCommands.push_back("map_restart");
    }
    _cgame->setServerCommands(_localCommands);
}

eliteforce::GameHost::Snapshot PlayPage::localSnapshot() {
    eliteforce::GameHost::Snapshot snap = _game->snapshot(0);
    snap.serverCommandSequence = int32_t(_localCommands.size());
    return snap;
}

void PlayPage::consoleCommand(const std::string &text) {
    // As the original's client: commands it knows itself, then the client
    // game's, and anything else goes to the server, whose game answers
    // (an unknown command it prints as such).
    const std::vector<std::string> w = splitCommand(text);
    if (w.empty() || !_cgame)
        return;
    const std::string verb = fs::toLower(w[0]);
    if (verb == "centerview") {
        _input.pitch = 0;
        return;
    }
    if (verb == "vstr" && w.size() >= 2) {
        runBinding(_cvars.string(w[1]), 0, true);
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
        if (std::ifstream in(_ctx.savePath / name, std::ios::binary); !_ctx.savePath.empty() && in)
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        else if (!_ctx.files.read(name, data))
            text.assign(data.begin(), data.end());
        else {
            addPrint("couldn't exec " + name + "\n", false);
            return;
        }
        if (_execDepth >= 8)
            return; // a file running itself
        addPrint("execing " + name + "\n", false);
        ++_execDepth;
        for (const std::string &command : splitCommands(text)) {
            consoleCommand(command);
            if (!_cgame)
                break; // the match stopped
        }
        --_execDepth;
        return;
    }
    if (verb == "bind" || verb == "unbind" || verb == "unbindall") {
        _ctx.bindings.execute(text, _cvars);
        return;
    }
    if (verb == "togglemenu") {
        openMenu();
        return;
    }
    if (verb == "quit") {
        _ctx.requestPage = "Start";
        return;
    }
    if (verb == "vid_restart") {
        _videoRestart = true;       // before the next frame, not in the middle of this one
        _ctx.requestRestart = true; // and the game's files, if fs_game changed
        return;
    }
    if (verb == "cmd") {
        // The rest for the server as the player's own (the in-game
        // menu's "cmd team red").
        const size_t at = text.find_first_not_of(
            ' ', text.find_first_of(' ') == std::string::npos ? text.size() : text.find_first_of(' '));
        const std::string rest = at == std::string::npos ? std::string() : text.substr(at);
        if (rest.empty())
            return;
        if (_client)
            _client->addClientCommand(rest);
        else if (_game)
            if (Error err = _game->clientCommand(0, rest))
                stop("The server game stopped: " + err.message);
        return;
    }
    if (verb == "toggleconsole") {
        if (_typing == Typing::kConsole)
            closeTyping();
        else
            openTyping(Typing::kConsole);
        return;
    }
    if (verb == "messagemode" || verb == "messagemode2") {
        openTyping(verb == "messagemode" ? Typing::kSay : Typing::kSayTeam, -1, _bindingKeyChar);
        return;
    }
    if (verb == "messagemode3" || verb == "messagemode4") {
        // A word to the player in the crosshair, or the last to hit you,
        // as the client game says.
        int32_t client = -1;
        if (Error err = _cgame->call(verb == "messagemode3" ? 4 : 5, {}, client)) {
            stop("The client game stopped: " + err.message);
            return;
        }
        if (client >= 0)
            openTyping(Typing::kTell, client, _bindingKeyChar);
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
    if (verb == "reconnect" && !_serverAddress.empty()) {
        connect(_serverAddress);
        return;
    }
    if (verb == "disconnect") {
        // Back to the menus, as the original.
        if (_client)
            _client->disconnect();
        _ctx.requestPage = "Menu";
        return;
    }
    if (verb == "clear") {
        _consoleLines.clear();
        return;
    }
    if (verb == "screenshot" || verb == "screenshotjpeg") {
        // The next frame, kept when it's drawn (saveScreenshot()). As the
        // original: "silent" says nothing; another word names the file.
        // Both are written as TGA files.
        _screenshot = Screenshot{true, w.size() >= 2 && w[1] == "silent",
                                 w.size() >= 2 && w[1] != "silent" ? w[1] : std::string()};
        _renderer->captureNextFrame();
        return;
    }
    bool handled = false;
    if (Error err = _cgame->consoleCommand(text, handled)) {
        stop("The client game stopped: " + err.message);
        return;
    }
    if (handled)
        return;
    // Settings, after the client game's own commands as the original's
    // console: set (seta, sets, setu) name value, or a setting's name
    // alone (shows it) or with a value.
    if ((verb == "set" || verb == "seta" || verb == "sets" || verb == "setu") && w.size() >= 3) {
        const int32_t flags = verb == "seta"   ? CvarStore::kArchive
                              : verb == "sets" ? CvarStore::kServerInfo
                              : verb == "setu" ? CvarStore::kUserInfo
                                               : 0;
        _cvars.create(w[1], w[2], flags);
        setFromConsole(w[1], w[2]);
        return;
    }
    if (const CvarStore::Cvar *cv = _cvars.find(w[0])) {
        if (w.size() == 1)
            addPrint("\"" + cv->name + "\" is \"" + cv->value + "\"\n", false);
        else
            setFromConsole(w[0], w[1]);
        return;
    }
    // On someone else's server, the rest goes to it as the player's.
    if (_client) {
        _client->addClientCommand(text);
        return;
    }
    if (!_game)
        return;
    // On a server of one's own, its game's console commands (addbot,
    // kick...) next; the rest goes to the server as the player's.
    if (Error err = _game->serverCommand(text, &handled)) {
        stop("The server game stopped: " + err.message);
        return;
    }
    if (handled)
        return;
    if (Error err = _game->clientCommand(0, text))
        stop("The server game stopped: " + err.message);
}

void PlayPage::setFromConsole(const std::string &name, const std::string &value) {
    // As the original's console: a variable that only changes with a new
    // level (the game type, the Holomatch modes) says so.
    _cvars.set(name, value);
    if (_cvars.hasLatched(name))
        addPrint(name + " will be changed upon restarting.\n", false);
}

void PlayPage::openMenu() {
    // Outside a match, the menus' own page; a demo ends, as the
    // original's does on Esc.
    if (!_cgame || !_renderer || playingDemo()) {
        if (playingDemo())
            endDemo();
        _ctx.requestPage = "Menu";
        return;
    }
    closeTyping();
    // The menus see a connected player and the match's configstrings
    // (its game type decides what they offer).
    // Whether this machine runs the match's server: the in-game menu
    // offers the server's items (adding bots, restarting) by it.
    _cvars.create("sv_running", "0", CvarStore::kReadOnly);
    _cvars.set("sv_running", _game && !_client ? "1" : "0", true);
    _menus.configure = [this](eliteforce::UiHost &ui) {
        ui.clientState.connState = 8; // CA_ACTIVE
        ui.clientState.clientNum = _client ? _playerNumber : 0;
        ui.clientState.serverName = _client ? _serverAddress : "localhost";
        ui.configstring = [this](int32_t index) { return matchConfigstring(index); };
    };
    if (_menus.running())
        _menus.show(MenuLayer::kInGameMenu);
    else if (!_menus.start(MenuLayer::kInGameMenu)) {
        addPrint(_menus.error() + "\n", false);
        return;
    }
    _menuOpen = true;
    _menuCaught = _menus.open(); // they take the keys as they show the menu
    _input.releaseAll();
    _ctx.sys.setRelativeMouse(false);
    _ctx.sys.setMouseCursorVisible(false);
    _ctx.sys.setTextInput(true);
}

void PlayPage::closeMenu() {
    _menuOpen = false;
    _menuCaught = false;
    _input.releaseAll();
    _ctx.sys.setTextInput(false);
    _ctx.sys.setRelativeMouse(true);
    if (!_menus.error().empty()) {
        addPrint(_menus.error() + "\n", false);
        _menus.clearError();
    }
}

void PlayPage::runMenuCommands() {
    // What the in-game menu runs goes to the console, as the original's
    // menus' commands do ("cmd team red", "callvote ...", "disconnect").
    for (const std::string &line : _menus.takeCommands())
        for (const std::string &command : splitCommands(line)) {
            const std::vector<std::string> words = splitCommand(command);
            if (words.empty())
                continue;
            const std::string verb = fs::toLower(words[0]);
            if (verb == "wait")
                continue;
            _lastMenuCommand = command;
            _lastMenuCommandMs = _ctx.sys.getMillis();
            consoleCommand(command);
            if (!_ctx.requestPage.empty())
                return;
        }
}

std::string PlayPage::matchConfigstring(int32_t index) const {
    const std::map<int32_t, std::string> &all = _client ? _client->configstrings() : _localConfigstrings;
    const auto it = all.find(index);
    return it == all.end() ? std::string() : it->second;
}

void PlayPage::runBinding(const std::string &line, int32_t key, bool down) {
    _input.runBinding(line, key, down, [this](const std::string &text) { consoleCommand(text); });
}

bool PlayPage::takesKey(const Event &ev) const {
    // Esc goes back to the menus, as in the game; with Ctrl it leaves.
    // While typing, every key is the typing's (Tab, Esc too), but Ctrl+Esc.
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && (ev.mods & keymod::kCtrl))
        return false;
    // The in-game menu takes them all too.
    return _menuOpen || _typing != Typing::kNone || (ev.type == EventType::kKeyDown && ev.key == key::kEscape);
}

void PlayPage::openTyping(Typing t, int32_t client, uint32_t key) {
    // The keys held are let go, as the original's typing takes them.
    _input.releaseAll();
    _typing = t;
    _tellClient = client;
    _field.clear();
    _cursor = 0;
    _consoleScroll = 0;
    _historyPos = _history.size();
    _swallowChar = key;
    _ctx.sys.setTextInput(true);
}

void PlayPage::closeTyping() {
    if (_typing != Typing::kNone && _renderer)
        _ctx.sys.setTextInput(false);
    _typing = Typing::kNone;
    _field.clear();
    _cursor = 0;
}

void PlayPage::submitTyping() {
    std::string text = _field;
    const Typing t = _typing;
    _field.clear();
    _cursor = 0;
    if (t == Typing::kConsole) {
        // Lines starting / or \ are commands; others are said, as the
        // original's console does once connected.
        if (text.empty())
            return;
        std::replace(text.begin(), text.end(), '"', '\'');
        if (_history.empty() || _history.back() != text)
            _history.push_back(text);
        _historyPos = _history.size();
        _consoleLines.push_back("]" + text);
        _consoleScroll = 0;
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
    if (t == Typing::kSay)
        consoleCommand("say \"" + text + "\"");
    else if (t == Typing::kSayTeam)
        consoleCommand("say_team \"" + text + "\"");
    else if (t == Typing::kTell)
        consoleCommand("tell " + std::to_string(_tellClient) + " \"" + text + "\"");
}

void PlayPage::typingEvent(const Event &ev) {
    if (ev.type == EventType::kTextInput) {
        // Characters typed (Latin-1, as the original's), but the one of the
        // key that opened the typing, and the console key in the console.
        for (uint32_t cp : decodeUtf8(ev.text)) {
            if (cp == _swallowChar) {
                _swallowChar = 0;
                continue;
            }
            _swallowChar = 0;
            if (cp < 32 || cp == 127 || cp > 255 || _field.size() >= 255)
                continue;
            if (_typing == Typing::kConsole && (cp == '`' || cp == '~'))
                continue;
            _field.insert(_cursor++, 1, char(cp));
        }
        return;
    }
    if (ev.type != EventType::kKeyDown)
        return;
    _swallowChar = 0;
    switch (ev.key) {
    case key::kEscape:
        closeTyping();
        return;
    case key::kReturn:
    case key::kKpEnter:
        submitTyping();
        return;
    case key::kBackspace:
        if (_cursor > 0)
            _field.erase(--_cursor, 1);
        return;
    case key::kDelete:
        if (_cursor < _field.size())
            _field.erase(_cursor, 1);
        return;
    case key::kLeft:
        _cursor = _cursor > 0 ? _cursor - 1 : 0;
        return;
    case key::kRight:
        _cursor = std::min(_field.size(), _cursor + 1);
        return;
    case key::kHome:
        _cursor = 0;
        return;
    case key::kEnd:
        _cursor = _field.size();
        return;
    default:
        break;
    }
    if (_typing != Typing::kConsole)
        return;
    switch (ev.key) {
    case '`':
        closeTyping();
        return; // the console key closes it again
    case key::kUp:
    case key::kDown:
        // Earlier lines typed, back and forth.
        if (_history.empty())
            return;
        if (ev.key == key::kUp && _historyPos > 0)
            --_historyPos;
        else if (ev.key == key::kDown && _historyPos < _history.size())
            ++_historyPos;
        _field = _historyPos < _history.size() ? _history[_historyPos] : std::string();
        _cursor = _field.size();
        return;
    case key::kPageUp:
        _consoleScroll = std::min(_consoleScroll + 4, int(_consoleLines.size()));
        return;
    case key::kPageDown:
        _consoleScroll = std::max(_consoleScroll - 4, 0);
        return;
    default:
        return;
    }
}

void PlayPage::handleEvent(const Event &ev) {
    // The joystick, read once a frame (pollJoystick).
    if (ev.type == EventType::kJoyButtonDown || ev.type == EventType::kJoyButtonUp || ev.type == EventType::kJoyAxis ||
        ev.type == EventType::kJoyHat || ev.type == EventType::kJoyRemoved) {
        _joystick.event(ev);
        return;
    }
    if (_typing != Typing::kNone) {
        // Typing takes the keys and characters; the mouse still looks.
        if (ev.type == EventType::kMouseMove) {
            _input.addMouse(ev.moveX, ev.moveY);
        } else {
            typingEvent(ev);
        }
        return;
    }
    if (_menuOpen) {
        // Menus that never took the keys (their key catcher) can't close
        // themselves: Esc does.
        // A held Esc's repeats don't close what its press opened.
        if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && ev.repeat)
            return;
        if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && (!_menuCaught || (ev.mods & keymod::kShift))) {
            // (Shift+Esc always: back to the match, whatever they show.)
            closeMenu();
            return;
        }
        _menus.handleEvent(ev);
        runMenuCommands();
        return;
    }
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
        if (!ev.repeat)
            openMenu();
        return;
    }
    if (!_renderer || _names.empty())
        return;
    if (ev.type == EventType::kKeyDown && ev.key == '`' && !(ev.mods & keymod::kCtrl)) {
        // The console key, whatever it's bound to, as in the original.
        openTyping(Typing::kConsole, -1, '`');
        return;
    }
    switch (ev.type) {
    case EventType::kKeyDown:
    case EventType::kKeyUp: {
        const bool down = ev.type == EventType::kKeyDown;
        if (kDeveloperKeys && down && (ev.mods & keymod::kCtrl)) {
            // Reluva's own, for working on the engine: restart, another
            // map, the info.
            const int n = int(_names.size());
            if (ev.key == 'r') {
                start(_index);
                return;
            }
            if (ev.key == key::kPageUp || ev.key == key::kPageDown) {
                start((_index + (ev.key == key::kPageUp ? n - 1 : 1)) % n);
                return;
            }
            if (ev.key == 'i') {
                _showInfo = !_showInfo;
                return;
            }
        }
        if (down && ev.repeat)
            return;
        // The key's character, in case its command opens typing (so it
        // isn't typed as well).
        _bindingKeyChar = ev.key < 128 ? ev.key : 0;
        if (const int32_t k = toQ3Key(ev.key))
            runBinding(_ctx.bindings.get(k), k, down);
        _bindingKeyChar = 0;
        break;
    }
    case EventType::kMouseDown:
    case EventType::kMouseUp:
        if (const int32_t k = toQ3Button(ev.button))
            runBinding(_ctx.bindings.get(k), k, ev.type == EventType::kMouseDown);
        break;
    case EventType::kMouseWheel:
        if (ev.wheelY) {
            const int32_t k = ev.wheelY > 0 ? q3key::kWheelUp : q3key::kWheelDown;
            runBinding(_ctx.bindings.get(k), k, true);
            runBinding(_ctx.bindings.get(k), k, false);
        }
        break;
    case EventType::kMouseMove:
        // Turned into looking once a frame (runGame), as the settings say.
        _input.addMouse(ev.moveX, ev.moveY);
        break;
    default:
        break;
    }
}

void PlayPage::runGame(uint64_t nowMs) {
    if (!_client && (!_game || !_cgame))
        return;
    const int32_t dt = _lastMs ? int32_t(std::min<uint64_t>(nowMs - _lastMs, 100)) : 0;
    _lastMs = nowMs;
    // Looking, by the mouse and keys at the menus' Controls settings.
    _input.turn(_cvars, dt);
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point from) {
        return std::chrono::duration<float, std::milli>(Clock::now() - from).count();
    };
    if (_client) {
        const auto start = Clock::now();
        runRemote(nowMs, dt);
        _timing.server = ms(start);
        if (!_cgame || !_client || !_client->hasSnapshot())
            return;
    } else {
        if (!runLocal(nowMs, dt))
            return;
    }
    _cgame->clearScene();
    _cgame->clearPictures();
    if (_soundSystem)
        _soundSystem->setTime(nowMs);
    int32_t ignored = 0;
    const auto clientStart = Clock::now();
    // (Its third argument says a demo is playing: no prediction from the
    // player's commands, which there aren't.)
    if (Error err =
            _cgame->call(eliteforce::CgameHost::kDrawActiveFrame, {_clientTime, 0, playingDemo() ? 1 : 0}, ignored)) {
        stop("The client game stopped: " + err.message);
        return;
    }
    _timing.client = ms(clientStart);
    runClientGameCommands();
    if (!_cgame)
        return;
    if (_soundSystem) {
        // At the original's settings: effects (s_volume) 0.8 and music
        // (s_musicvolume) 0.25 unless the menus set them.
        auto setting = [&](const char *name, float fallback) {
            return _cvars.find(name) ? _cvars.value(name) : fallback;
        };
        _soundSystem->setVolumes(setting("s_volume", 0.8f), setting("s_musicvolume", 0.25f));
        _soundSystem->endFrame();
    }
}

void PlayPage::runClientGameCommands() {
    // What the client game asked for this frame (and in the console
    // commands it handled since): its commands for the server as the
    // player's ("score", asking for the scoreboard), sent as the
    // original's client sends them; and console commands, run as typed.
    for (const std::string &command : _cgame->takeClientCommands()) {
        if (_client) {
            if (!_client->playingDemo())
                _client->addClientCommand(command);
        } else if (_game) {
            if (Error err = _game->clientCommand(0, command)) {
                stop("The server game stopped: " + err.message);
                return;
            }
        }
    }
    for (const std::string &line : _cgame->takeConsoleCommands())
        for (const std::string &command : splitCommands(line)) {
            consoleCommand(command);
            if (!_cgame)
                return;
        }
}

eliteforce::GameHost::UserCommand PlayPage::buildCommand() {
    // The player's command for this moment, as a client sends one each
    // frame: where they look, how they move, what they press.
    eliteforce::GameHost::UserCommand cmd;
    const PlayerInput::Command c = _input.command(_cvars, _ctx.game);
    cmd.angles = c.angles;
    cmd.buttons = c.buttons;
    cmd.weapon = uint8_t(_cgame ? _cgame->weaponSelect() : 0);
    cmd.forward = c.forward;
    cmd.right = c.right;
    cmd.up = c.up;
    return cmd;
}

bool PlayPage::runLocal(uint64_t nowMs, int32_t dt) {
    if (dt > 0)
        _clientTime += dt;
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
    if (_server)
        if (Error err = _server->receive(nowMs)) {
            stop("The server game stopped: " + err.message);
            return false;
        }
    while (_game->levelTime() <= _clientTime && !_game->levelRequested()) {
        if (Error err = _game->runFrame()) {
            stop("The server game stopped: " + err.message);
            return false;
        }
        if (_server)
            if (Error err = _server->frame(nowMs)) {
                stop("The server game stopped: " + err.message);
                return false;
            }
        sendServerCommands();
        _cgame->newSnapshot(localSnapshot());
    }
    _timing.server = ms(serverStart);
    // A new level the game (the end of a match, a vote) or a player asked for.
    if (_game->levelRequested())
        return changeLevel(_game->takeRequestedMap());
    if (dt > 0) {
        // The game moves the player by their command at once, as the
        // original server does.
        eliteforce::GameHost::UserCommand cmd = buildCommand();
        cmd.serverTime = _clientTime;
        if (Error err = _game->clientThink(0, cmd)) {
            stop("The server game stopped: " + err.message);
            return false;
        }
        _cgame->addUserCommand(cmd);
    }
    // The last two seconds' frames, for the info line: each frame's length,
    // whether it sent a command, how many snapshots, and the player's event
    // sequence (player state word 108) as the server has it.
    _recentFrames.push_back({nowMs, dt, dt > 0, _game->snapshotsMade(), _game->clientInt(0, 108)});
    while (!_recentFrames.empty() && _recentFrames.front().ms + 2000 < nowMs)
        _recentFrames.pop_front();
    return true;
}

void PlayPage::pollJoystick() {
    // As the original reads it each frame: its keys (buttons, and axes
    // and hat taken as keys) run their bindings, or go to the menus over
    // the match; its analog axes move and turn the player.
    const JoystickInput::Frame f = _joystick.frame(_cvars);
    const bool playing = !_menuOpen && _typing == Typing::kNone;
    _input.setJoystick(playing ? f.x : 0, playing ? f.y : 0);
    for (const auto &[k, down] : f.keys) {
        if (_menuOpen) {
            _menus.key(k, down);
            runMenuCommands();
        } else if (_typing == Typing::kNone) {
            runBinding(_ctx.bindings.get(k), k, down);
        }
        if (!_renderer)
            return;
    }
}

void PlayPage::update(uint64_t nowMs) {
    if (_videoRestart)
        restartVideo();
    if (!_renderer)
        return;
    pollJoystick();
    if (!_renderer)
        return;
    if (!_shownMs)
        _shownMs = nowMs;
    runGame(nowMs);
    _renderer->setGamma(brightness(_ctx));
    const float time = float(_clientTime) / 1000.0f;

    // The client game's frame: each scene it rendered over the pictures
    // drawn before it, the rest of its pictures (the HUD) over them all.
    // The player's view is the first with the level in it.
    const auto drawStart = std::chrono::steady_clock::now();
    _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    bool haveView = false;
    if (_cgame) {
        const auto &scene = _cgame->scene();
        const auto &pictures = _cgame->pictures();
        const bool dynamicLights = !_cvars.find("r_dynamiclight") || _cvars.integer("r_dynamiclight") != 0;
        size_t drawn = 0;
        bool first = true;
        // r_drawentities 0: the level alone; r_drawEffects 0 (Reluva's):
        // without sprites, beams and marks.
        _scene->showModels = !_cvars.find("r_drawentities") || _cvars.integer("r_drawentities") != 0;
        _scene->showEffects =
            _scene->showModels && (!_cvars.find("r_drawEffects") || _cvars.integer("r_drawEffects") != 0);
        for (const auto &v : scene.views) {
            _scene->drawPictures(pictures, drawn, v.picturesBefore, 640, 480, time);
            drawn = std::max(drawn, v.picturesBefore);
            _scene->drawView(scene, v, 640, 480, time, first, _world.get(), &_grid, dynamicLights);
            first = false;
            if (!haveView && !(readInt(v.refdef, 76) & kNoWorldModel)) {
                haveView = true;
                _eye = _scene->eye();
                _shown = {_scene->placedCount(),   _scene->effectCount(), _scene->lightCount(),
                          _scene->otherEntities(), _scene->unloadable(),  _scene->effectSummary()};
            }
        }
        _scene->drawPictures(pictures, drawn, pictures.size(), 640, 480, time);
    }
    _timing.draw = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - drawStart).count();

    // The in-game menu over it all, until it closes itself.
    if (_menuOpen) {
        _menus.frame(nowMs);
        runMenuCommands();
        if (_menuOpen) {
            const bool caught = _menus.open();
            _menuCaught |= caught;
            if (!_menus.running() || (_menuCaught && !caught))
                closeMenu();
        }
    }

    drawOverlay(nowMs, haveView);
    _renderer->endFrame(&_overlay);
    if (_screenshot.wanted)
        saveScreenshot();
}

// The frame kept for the screenshot command, as the original's: in
// screenshots/ in the save folder, as the name given or the first of
// shot0000.tga to shot9999.tga not taken.
void PlayPage::saveScreenshot() {
    const Screenshot shot = _screenshot;
    _screenshot = {};
    std::vector<uint8_t> rgba;
    if (_ctx.savePath.empty() || !_renderer->takeCapture(rgba)) {
        addPrint("Couldn't take the screenshot\n", true);
        return;
    }
    const std::filesystem::path folder = _ctx.savePath / "screenshots";
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
    img.w = _renderer->width();
    img.h = _renderer->height();
    img.rgba = std::move(rgba);
    const std::vector<uint8_t> tga = image::encodeTga(img);
    std::ofstream out(folder / name, std::ios::binary);
    out.write(reinterpret_cast<const char *>(tga.data()), std::streamsize(tga.size()));
    if (!out)
        addPrint("Couldn't write screenshots/" + name + "\n", true);
    else if (!shot.silent)
        addPrint("Wrote screenshots/" + name + "\n", true);
}

void PlayPage::addPrint(const std::string &text, bool notify) {
    // Whole lines into the console (the last thousand); the client game's
    // also on screen, each 3 seconds (the original's con_notifytime), at
    // most the last 4.
    std::string &partial = _printPartial[notify ? 1 : 0];
    partial += text;
    size_t end;
    while ((end = partial.find('\n')) != std::string::npos) {
        const std::string line = partial.substr(0, end);
        partial.erase(0, end + 1);
        _consoleLines.push_back(line);
        while (_consoleLines.size() > 1000)
            _consoleLines.pop_front();
        if (_consoleScroll > 0)
            ++_consoleScroll;
        if (!notify)
            continue;
        _notify.emplace_back(_ctx.sys.getMillis(), line);
        while (_notify.size() > 4)
            _notify.pop_front();
    }
}

void PlayPage::drawColoured(int x, int y, const std::string &line) {
    // A line in the original's colours (^0-^7), with a shadow.
    static const Color kColors[8] = {{0, 0, 0},   {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                                     {0, 0, 255}, {0, 255, 255}, {255, 0, 255}, {255, 255, 255}};
    Color color{255, 255, 255};
    std::string run;
    auto flush = [&] {
        if (run.empty())
            return;
        font::drawText(_overlay, x + 1, y + 1, run, {0, 0, 0});
        font::drawText(_overlay, x, y, run, color);
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

void PlayPage::drawOverlay(uint64_t nowMs, bool haveView) {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    // (A line printed during this frame is stamped a little after nowMs.)
    while (!_notify.empty() && _notify.front().first + 3000 < nowMs)
        _notify.pop_front();
    const int lh = font::kLineHeight;
    const bool blink = (nowMs / 250) % 2 == 0;
    if (_typing == Typing::kConsole) {
        // The console over the top half: the lines printed, newest at the
        // bottom (PageUp/PageDown scroll), and what's being typed.
        const int height = _frameH / 2;
        for (int yy = 0; yy < height; ++yy) {
            uint8_t *p = _overlay.row(yy);
            for (int x = 0; x < _frameW; ++x) {
                p[x * 4 + 0] = 10, p[x * 4 + 1] = 14, p[x * 4 + 2] = 24;
                p[x * 4 + 3] = 215;
            }
        }
        for (int x = 0; x < _frameW; ++x) {
            uint8_t *p = _overlay.row(height) + x * 4;
            p[0] = 200, p[1] = 140, p[2] = 40, p[3] = 255;
        }
        int y = height - 2 * lh - 4;
        drawColoured(4, height - lh - 4,
                     "]" + _field.substr(0, _cursor) + (blink ? "_" : " ") + _field.substr(_cursor));
        for (int i = int(_consoleLines.size()) - 1 - _consoleScroll; i >= 0 && y >= 0; --i, y -= lh)
            drawColoured(4, y, _consoleLines[size_t(i)]);
    } else {
        // Joining a server: how far it has got, in the middle.
        if (_client && (!_cgame || !_client->hasSnapshot())) {
            static const char *const kStates[] = {"",           "Disconnected",          "", "Asking for a challenge",
                                                  "Connecting", "Waiting for the level", "", "Loading the level",
                                                  "Joining"};
            const auto st = size_t(_client->state());
            std::string status = (st < 9 ? kStates[st] : "") + std::string(" - ") + _serverAddress;
            if (_client->downloading())
                status =
                    "Downloading " + _client->downloadName() + ": " + std::to_string(_client->downloadCount() / 1024) +
                    " of " +
                    (_client->downloadSize() < 0 ? std::string("?") : std::to_string(_client->downloadSize() / 1024)) +
                    " KB";
            if (_client->state() <= eliteforce::NetClient::State::kChallenging)
                status += " (" + std::to_string(_client->connectPackets()) + ")";
            const int x = std::max(4, (_frameW - int(font::length(status)) * font::kAdvance) / 2);
            drawColoured(x, _frameH / 2 - lh, status);
            if (!_client->message().empty()) {
                const int mx = std::max(4, (_frameW - int(font::length(_client->message())) * font::kAdvance) / 2);
                drawColoured(mx, _frameH / 2 + lh, _client->message());
            }
        }
        // The notify lines at the top, then a chat line being typed.
        int notifyY = 4;
        for (const auto &[when, line] : _notify) {
            drawColoured(4, notifyY, line);
            notifyY += lh;
        }
        if (_typing != Typing::kNone) {
            const char *prompt = _typing == Typing::kSay       ? "Say: "
                                 : _typing == Typing::kSayTeam ? "Say Team: "
                                                               : "Tell: ";
            drawColoured(4, notifyY,
                         std::string("^7") + prompt + _field.substr(0, _cursor) + (blink ? "_" : " ") +
                             _field.substr(_cursor));
        }
    }
    std::vector<std::string> lines;
    if (!_error.empty()) {
        lines.push_back(_error);
        lines.push_back(kDeveloperKeys ? "Ctrl+R: start the level again, Ctrl+PageUp/PageDown: another map."
                                       : "Esc: the menus.");
    }
    if (_showInfo) {
        char line[256];
        if (_game) {
            std::snprintf(
                line, sizeof line,
                "time %.1f s, eye at %.0f %.0f %.0f%s; %zu models, %zu effects and %zu lights drawn, %d other entities not yet",
                float(_clientTime) / 1000.0f, _eye[0], _eye[1], _eye[2], haveView ? "" : " (no view yet)",
                _shown.models, _shown.effects, _shown.lights, _shown.other);
            lines.push_back(line);
            if (!_shown.effectShaders.empty())
                lines.push_back("effects: " + _shown.effectShaders);
            // What the player's commands carry, to see that keys arrive.
            std::string held;
            for (const auto &[name, keys] : _input.buttons())
                if (!keys.empty())
                    held += " +" + name;
            std::snprintf(line, sizeof line, "weapon chosen by the client game: %d; held:%s",
                          _cgame ? _cgame->weaponSelect() : 0, held.empty() ? " nothing" : held.c_str());
            lines.push_back(line);
            // Where each frame's time goes, smoothed, to find what's slow.
            std::snprintf(line, sizeof line,
                          "frame: server and bots %.1f ms, client game %.1f ms, drawing %.1f ms; %zu sounds playing",
                          _timing.server, _timing.client, _timing.draw,
                          _soundSystem ? _soundSystem->playingCount() : size_t(0));
            lines.push_back(line);
            if (_recentFrames.size() > 1) {
                const auto &first = _recentFrames.front();
                const auto &last = _recentFrames.back();
                int commands = 0;
                int longest = 0;
                for (const auto &f : _recentFrames) {
                    commands += f.command;
                    longest = std::max(longest, f.dt);
                }
                std::snprintf(line, sizeof line,
                              "last 2 s: %zu frames (%.1f ms each, the longest %d), %d commands, %d snapshots, %d "
                              "player events on the server",
                              _recentFrames.size(), float(last.ms - first.ms) / float(_recentFrames.size() - 1),
                              longest, commands, last.snapshots - first.snapshots, last.events - first.events);
                lines.push_back(line);
            }
            if (_soundSystem) {
                for (const std::string &r : _soundSystem->recent())
                    lines.push_back("sound: " + (r.size() > 140 ? r.substr(0, 140) + "..." : r));
                std::string loops = "looping:";
                for (const std::string &l : _soundSystem->loopingNow())
                    loops += " " + l;
                lines.push_back(loops.size() > 150 ? loops.substr(0, 150) + "..." : loops);
            }
            if (_soundSystem && !_soundSystem->missing().empty()) {
                // Their names, the voices' (which the game files lack, as the
                // original's) left out, then why the last one failed.
                std::string names;
                size_t voices = 0;
                for (const std::string &n : _soundSystem->missing()) {
                    if (n.rfind("sound/voice/", 0) == 0)
                        ++voices;
                    else
                        names += " " + n;
                }
                lines.push_back(std::to_string(_soundSystem->missing().size()) + " sounds couldn't be played (" +
                                std::to_string(voices) +
                                " voices):" + (names.size() > 120 ? names.substr(0, 120) + "..." : names));
                if (const auto &problems = _soundLibrary.problems(); !problems.empty())
                    lines.push_back("  last: " + (problems.back().size() > 150 ? problems.back().substr(0, 150) + "..."
                                                                               : problems.back()));
            }
            if (_world) {
                const WorldRenderer::Visibility v = _world->visibility();
                std::snprintf(line, sizeof line,
                              "visibility: cluster %d, %zu of %zu surfaces drawn (%zu past the vis data and doors)%s, "
                              "%d areas hidden",
                              v.cluster, v.visible, v.total, v.potentiallyVisible,
                              v.culling ? "" : " (all: no culling)", v.hiddenAreas);
                lines.push_back(line);
                std::snprintf(line, sizeof line, "mirror and portal views drawn so far: %d%s", _scene->portalViews(),
                              _renderer->hasStencil() ? "" : " (none: no stencil buffer)");
                lines.push_back(line);
            }
            if (_game) {
                std::snprintf(line, sizeof line, "%d areas; sight checks a closed door blocked: %d%s",
                              _game->areaCount(), _game->closedDoorBlocks(),
                              _cvars.integer("cm_noAreas") ? " (cm_noAreas 1: areas ignored)" : "");
                lines.push_back(line);
            }
            // Network play: who has joined this level, or the server played on.
            if (_server) {
                std::string players = "Open to others on port " + std::to_string(_server->port()) + ";";
                const auto joined = _server->players();
                if (joined.empty())
                    players += " no one has joined";
                for (const auto &[number, name] : joined)
                    players += " " + std::to_string(number) + ": " + name;
                lines.push_back(players);
            } else if (_client) {
                lines.push_back("Playing on " + _serverAddress + " (" + _client->server().toString() + ")");
            } else if (_game && _game->layout().version != 12) {
                lines.push_back("Not open to others: network play needs patch 1.2");
            }
            if (!_shown.unloadable.empty()) {
                std::string names = "Models not drawn (only .md3 and .mdr):";
                for (const std::string &n : _shown.unloadable)
                    names += " " + n;
                lines.push_back(names.size() > 150 ? names.substr(0, 150) + "..." : names);
            }
        }
        for (const auto *host : {"server", "client"}) {
            const std::map<int, int> *unhandled = nullptr;
            if (host[0] == 's' && _game)
                unhandled = &_game->unhandled();
            else if (host[0] == 'c' && _cgame)
                unhandled = &_cgame->unhandled();
            if (!unhandled || unhandled->empty())
                continue;
            std::string calls = std::string("The ") + host + " game's system calls not implemented yet:";
            for (const auto &[number, count] : *unhandled)
                calls += " " + std::to_string(number);
            lines.push_back(calls);
        }
        if (!_notice.empty())
            lines.push_back(_notice);
    }
    if (_menuOpen)
        lines.push_back(std::string("The game's in-game menu (menu ") + std::to_string(MenuLayer::kInGameMenu) +
                        "): " + (_menuCaught ? "it has the keys" : "it hasn't taken the keys") +
                        ". Shift+Esc: back to the match.");
    if (!_lastMenuCommand.empty() && nowMs - _lastMenuCommandMs < kHintMs)
        lines.push_back("The in-game menu ran: " + _lastMenuCommand);
    if (nowMs - _shownMs < kHintMs) {
        lines.push_back("Keys do what they're bound to (set in the menus' Controls). The mouse looks. ~: the console.");
        lines.push_back(kDeveloperKeys ? "Ctrl+PageUp/PageDown: another map, Ctrl+I: info, Ctrl+R: restart, Esc: the "
                                         "menus, Ctrl+Esc: launcher."
                                       : "Esc: the menus, Ctrl+Esc: launcher.");
    }
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = _frameH - int(lines.size() - i) * lh - 4;
        const int w = std::min(_frameW, int(font::length(lines[i])) * font::kAdvance + 8);
        for (int yy = y - 2; yy < y + lh; ++yy) {
            uint8_t *p = _overlay.row(yy);
            for (int x = 0; x < w; ++x)
                p[x * 4 + 3] = 170;
        }
        font::drawText(_overlay, 4, y, lines[i],
                       i == 0 && !_error.empty() ? Color{255, 120, 120} : Color{230, 230, 230});
    }
}

} // namespace reluva::idtech3
