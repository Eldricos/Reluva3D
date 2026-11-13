// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/ui_host.h"

#include "engines/idtech3/eliteforce/game_info.h"

#include "engines/idtech3/formats/md3.h"

#include "common/fs.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace reluva::idtech3::eliteforce {

namespace {

// Elite Force ui.qvm system calls. The evidence for each number is in
// docs/engines/idtech3/games/stvef/systems/syscalls.md.
enum UiCall : int {
    kError = 0,
    kPrint = 1,
    kMilliseconds = 2,
    kCvarSet = 3,
    kCvarVariableValue = 4,
    kCvarVariableStringBuffer = 5,
    kCvarSetValue = 6,
    kCvarReset = 7,
    kCvarCreate = 8,
    kCvarInfoStringBuffer = 9,
    kArgc = 10,
    kArgv = 11,
    kCmdExecuteText = 12,
    kFsFOpenFile = 13,
    kFsRead = 14,
    kFsWrite = 15,
    kFsFCloseFile = 16,
    kFsGetFileList = 17,
    kRRegisterModel = 18,
    kRRegisterSkin = 19,
    kRRegisterShaderNoMip = 20,
    kRClearScene = 21,
    kRAddRefEntityToScene = 22,
    kRAddPolyToScene = 23,
    kRAddLightToScene = 24,
    kRRenderScene = 25,
    kRSetColor = 26,
    kRDrawStretchPic = 27,
    kUpdateScreen = 28,
    kCmLerpTag = 29, // Quake III's number; (orientation_t*, model, startFrame, endFrame, frac, tagName)
    kSRegisterSound = 31,
    kSStartLocalSound = 32,
    kKeyKeynumToStringBuf = 33,
    kKeyGetBindingBuf = 34,
    kKeySetBinding = 35,
    kKeyIsDown = 36,
    kKeyGetOverstrikeMode = 37,
    kKeySetOverstrikeMode = 38,
    kKeyClearStates = 39,
    kKeyGetCatcher = 40,
    kKeySetCatcher = 41,
    kGetClipboardData = 42,
    kGetGlconfig = 43,
    kGetClientState = 44,
    kGetConfigString = 45,
    kLanGetLocalServerCount = 46,
    kLanGetLocalServerAddressString = 47, // (n, buffer, size)
    kLanGetGlobalServerCount = 48,
    kLanGetGlobalServerAddressString = 49,
    kLanGetPingQueueCount = 50,
    kLanClearPing = 51,   // (n)
    kLanGetPing = 52,     // (n, buffer, size, &time)
    kLanGetPingInfo = 53, // (n, buffer, size)
    kCvarRegister = 54,
    kCvarUpdate = 55,
    kMemoryRemaining = 56,
    kSetCdKey = 57,     // (key) -> whether it's taken; seen on the CD key screen's Accept
    kRModelBounds = 58, // (model, mins[3], maxs[3]); seen after registering the phaser model
};

// glconfig_t as Elite Force's menus use it (qvm-refs): renderer, vendor
// and version strings of 1024 bytes and a 2048-byte extensions string,
// then a word of Elite Force's own and Quake III's 17 words, to byte 5192.
// Quake III's extensions string is 8192 bytes.
constexpr int32_t kGlconfigStrings = 1024 * 3 + 2048;
constexpr int32_t kGlconfigSize = 5192;
// uiClientState_t: three words, then three 1024-byte strings.
constexpr int32_t kClientStateSize = 12 + 3 * 1024;

} // namespace

UiHost::UiHost(const SearchSet &files, CvarStore &cvars) : _files(files), _cvars(cvars), _vmFiles(files) {
    _vm.setSystemCall([this](Qvm &vm, int number, const int32_t *args) { return systemCall(vm, number, args); });
}

Error UiHost::load() {
    std::vector<uint8_t> data;
    if (Error err = _files.read("vm/ui.qvm", data))
        return Error::fail("vm/ui.qvm: " + err.message);
    if (Error err = _vm.load(data))
        return Error::fail("vm/ui.qvm: " + err.message);
    // The Expansion Pack's menus: patch 1.2's show the Holomatch modes'
    // buttons and the settings that go with them when ui_cdkeychecked2 is
    // 1, which the original's engine sets when baseEF/expefq3.key (the
    // Expansion Pack's CD key) is there and isn't the game's own
    // (efq3.key). Reluva shows them anyway unless reluva_modeButtons is 0:
    // the modes are in the 1.2 game code, which is all they need.
    {
        std::vector<uint8_t> expansion;
        std::vector<uint8_t> base;
        const bool hasExpansionKey = !_files.read("expefq3.key", expansion) && !expansion.empty() &&
                                     (_files.read("efq3.key", base) || base != expansion);
        _cvars.create("reluva_modeButtons", "1", CvarStore::kArchive);
        _cvars.create("ui_cdkeychecked2", "0", CvarStore::kReadOnly);
        _cvars.set("ui_cdkeychecked2", hasExpansionKey || _cvars.integer("reluva_modeButtons") ? "1" : "0", true);
    }
    // The engine's settings the server list reads: its protocol, and the
    // master servers.
    _cvars.create("protocol", "24", CvarStore::kServerInfo | CvarStore::kReadOnly);
    masterServerNames(_cvars, masterServers());
    int32_t version = 0;
    if (Error err = call(kGetApiVersion, {}, version))
        return err;
    if (version != kApiVersion)
        return Error::fail("vm/ui.qvm reports API version " + std::to_string(version) + ", not " +
                           std::to_string(kApiVersion));
    return Error::success();
}

Error UiHost::call(int32_t command, const std::vector<int32_t> &args, int32_t &result) {
    Error err = _vm.call(command, args, result);
    // The menus ask for the CD key until ui_cdkeychecked is set, which
    // they do once one is accepted. With a key kept from an earlier run,
    // that has happened.
    if (!err && command == kInit && cdKeyLooksValid(storedCdKey()))
        _cvars.set("ui_cdkeychecked", "1", true);
    return err;
}

Error UiHost::refresh(int32_t time) {
    setTime(time);
    int32_t fullscreen = 0;
    if (Error err = call(kIsFullscreen, {}, fullscreen))
        return err;
    // Disconnected, the menus always show something covering the screen:
    // the main menu, if nothing else, as the original's engine reopens it
    // (only then: over a match, the in-game menu doesn't cover it).
    if (!fullscreen && clientState.connState <= 1 && !_mainMenuNotFullscreen) {
        if (Error err = call(kSetActiveMenu, {1})) // the main menu
            return err;
        // It should cover the screen now; if it doesn't, opening it every
        // frame would only keep resetting it.
        if (Error err = call(kIsFullscreen, {}, fullscreen))
            return err;
        if (!fullscreen) {
            _mainMenuNotFullscreen = true;
            _log += "Reluva: the main menu doesn't report covering the screen; not reopening it\n";
        }
    }
    return call(kRefresh, {time});
}

bool UiHost::cdKeyLooksValid(const std::string &key) {
    // The menus' own rule: up to 22 characters, 18 of them letters or
    // digits (the rest, say, dashes).
    if (key.size() > 22)
        return false;
    int alphanumeric = 0;
    for (char c : key)
        alphanumeric += (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    return alphanumeric == 18;
}

const std::string &UiHost::storedCdKey() {
    if (!_cdKeyRead) {
        // Kept by Reluva, not where the original keeps it.
        _cdKeyRead = true;
        if (!_keyFile.empty()) {
            std::ifstream in(_keyFile);
            std::getline(in, _cdKey);
        }
    }
    return _cdKey;
}

void UiHost::writeGlconfig(int32_t address) {
    if (!_vm.checkWrite(address, kGlconfigSize))
        return;
    std::memset(&_vm.memory()[size_t(address)], 0, kGlconfigSize);
    _vm.writeString(address, "Reluva", 1024);
    _vm.writeString(address + 1024, "Reluva", 1024);
    _vm.writeString(address + 2048, "1.0", 1024);
    // Quake III's 17 words, in its order, from glconfigWords_.
    const int32_t words[17] = {
        2048,               // maxTextureSize
        2,                  // numTextureUnits
        32,     24,      8, // colour, depth, stencil bits
        0,      0,          // driverType (ICD), hardwareType (generic)
        0,                  // deviceSupportsGamma
        0,      1,          // textureCompression, textureEnvAddAvailable
        _width, _height, Qvm::fromFloat(float(_width) / float(_height)),
        60,                 // displayFrequency
        0,      0,       0, // isFullscreen, stereoEnabled, smpActive
    };
    for (int i = 0; i < 17; ++i)
        if (_glconfigWords + 4 * i + 4 <= kGlconfigSize)
            _vm.writeInt(address + _glconfigWords + 4 * i, words[i]);
}

void UiHost::writeClientState(int32_t address) {
    if (!_vm.checkWrite(address, kClientStateSize))
        return;
    std::memset(&_vm.memory()[size_t(address)], 0, kClientStateSize);
    _vm.writeInt(address, clientState.connState);
    _vm.writeInt(address + 4, clientState.connectPacketCount);
    _vm.writeInt(address + 8, clientState.clientNum);
    _vm.writeString(address + 12, clientState.serverName, 1024);
    _vm.writeString(address + 12 + 1024, clientState.updateInfo, 1024);
    _vm.writeString(address + 12 + 2048, clientState.message, 1024);
}

bool UiHost::browserCommand(const std::string &line) {
    // The server list's own commands, run at once as the original runs
    // them (EXEC_NOW), so the menus see their pings straight away.
    if (!browser)
        return false;
    const std::vector<std::string> words = splitCommand(line);
    if (words.empty())
        return false;
    const std::string verb = fs::toLower(words[0]);
    if (verb == "localservers") {
        browser->localServers(now);
        return true;
    }
    if (verb == "globalservers") {
        // globalservers <master> <protocol> [keywords]: Elite Force numbers
        // the masters from 0 (sv_master1); Raven's being gone, 0 asks
        // every master set (sv_master2 is the community's).
        const int master = words.size() > 1 ? std::atoi(words[1].c_str()) : 0;
        const int protocol = words.size() > 2 && std::atoi(words[2].c_str()) ? std::atoi(words[2].c_str()) : 24;
        std::string keywords;
        for (size_t i = 3; i < words.size(); ++i)
            keywords += (keywords.empty() ? "" : " ") + words[i];
        const std::vector<std::string> names = masterServerNames(_cvars, masterServers());
        std::vector<net::Address> masters;
        for (size_t i = 0; i < names.size(); ++i) {
            if (master > 0 && _cvars.string("sv_master" + std::to_string(master + 1)) != names[i])
                continue;
            const auto [host, port] = net::splitHostPort(names[i], ServerBrowser::kMasterPort);
            uint32_t ip = 0;
            if (resolveHost && resolveHost(host, ip))
                masters.push_back({ip, port});
        }
        browser->globalServers(masters, protocol, keywords);
        return true;
    }
    if (verb == "ping" && words.size() >= 2) {
        const auto [host, port] = net::splitHostPort(words.back(), ServerBrowser::kServerPort);
        uint32_t ip = 0;
        if (resolveHost && resolveHost(host, ip))
            browser->ping({ip, port}, now);
        return true;
    }
    return false;
}

std::vector<std::string> UiHost::recentCalls() const {
    std::vector<std::string> out;
    for (size_t i = 0; i < _recent.size(); ++i) {
        const Recent &r = _recent[(_recentNext + i) % _recent.size()];
        if (r.number < 0)
            continue;
        std::string line = "system call " + std::to_string(r.number) + " (" + std::to_string(r.args[0]) + ", " +
                           std::to_string(r.args[1]) + ", " + std::to_string(r.args[2]) + ", " +
                           std::to_string(r.args[3]) + ")";
        if (!r.text.empty())
            line += " \"" + r.text + "\"";
        out.push_back(std::move(line));
    }
    return out;
}

int32_t UiHost::systemCall(Qvm &vm, int number, const int32_t *a) {
    if (trace)
        trace(number, a);
    {
        Recent &r = _recent[_recentNext];
        _recentNext = (_recentNext + 1) % _recent.size();
        r.number = number;
        std::copy(a, a + 4, r.args);
        r.text.clear();
        if (number != kSetCdKey)
            for (int i = 0; i < 2 && r.text.empty(); ++i) {
                if (a[i] <= 0 || !vm.valid(a[i], 1))
                    continue;
                std::string t = vm.string(a[i], 60);
                bool printable = t.size() >= 2;
                for (char c : t)
                    printable = printable && c >= 32 && c < 127;
                if (printable)
                    r.text = std::move(t);
            }
    }
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    auto f = [&](int i) { return Qvm::asFloat(a[i]); };
    int32_t result = 0;
    if (commonSystemCall(vm, number, a, result))
        return result;

    switch (number) {
    case kError:
        vm.abort("ui.qvm: " + str(0));
        return 0;
    case kPrint: {
        const std::string text = str(0, 4096);
        if (print)
            print(text);
        else
            _log += text;
        return 0;
    }
    case kMilliseconds:
        return _time;
    // Set at once, as the original engine's Cvar_Set (forced: a latched
    // variable too; only what's typed at the console waits for a level).
    case kCvarSet:
        _cvars.set(str(0, 256), str(1, 256), true);
        return 0;
    case kCvarVariableValue:
        return Qvm::fromFloat(_cvars.value(str(0, 256)));
    case kCvarVariableStringBuffer:
        vm.writeString(a[1], _cvars.string(str(0, 256)), a[2]);
        return 0;
    case kCvarSetValue: {
        // Quake III writes whole numbers without a decimal point.
        const float v = f(1);
        const std::string text = v == float(int32_t(v)) ? std::to_string(int32_t(v)) : std::to_string(v);
        _cvars.set(str(0, 256), text, true);
        return 0;
    }
    case kCvarReset:
        if (const CvarStore::Cvar *cv = _cvars.find(str(0, 256)))
            _cvars.set(cv->name, cv->defaultValue);
        return 0;
    case kCvarCreate:
        _cvars.create(str(0, 256), str(1, 256), a[2]);
        return 0;
    case kCvarInfoStringBuffer: {
        std::string info;
        for (const CvarStore::Cvar &cv : _cvars.all())
            if (cv.flags & a[0])
                info += "\\" + cv.name + "\\" + cv.value;
        vm.writeString(a[1], info, a[2]);
        return 0;
    }
    case kArgc:
        return 0;
    case kArgv:
        vm.writeString(a[1], "", a[2]);
        return 0;
    case kCmdExecuteText: {
        const std::string text = str(1, 4096);
        bool handled = false;
        for (const std::string &command : splitCommands(text))
            handled = browserCommand(command) || handled;
        if (!handled)
            _commands.push_back(text);
        return 0;
    }
    case kFsFOpenFile:
        return _vmFiles.open(vm, a[0], a[1], a[2]);
    case kFsRead:
        return _vmFiles.read(vm, a[0], a[1], a[2]);
    case kFsFCloseFile:
        _vmFiles.close(a[0]);
        return 0;
    case kFsGetFileList:
        return _vmFiles.list(vm, a[0], a[1], a[2], a[3]);
    case kRRegisterModel:
        return _models.add(str(0, 256));
    case kRRegisterSkin:
        return _skins.add(str(0, 256));
    case kRRegisterShaderNoMip:
        return _shaders.add(str(0, 256));
    case kRClearScene:
        _scene.clear();
        return 0;
    case kRAddRefEntityToScene:
        _scene.addEntity(vm, a[0], 0);
        return 0;
    case kRAddPolyToScene:
        _scene.addPoly(vm, a[0], a[1], a[2]);
        return 0;           // (shader, numVerts, verts)
    case kRAddLightToScene: // (origin, intensity, r, g, b)
        _scene.addLight(vm, a[0], Qvm::asFloat(a[1]), Qvm::asFloat(a[2]), Qvm::asFloat(a[3]), Qvm::asFloat(a[4]));
        return 0;
    case kRRenderScene:
        _scene.render(vm, a[0], _pictures.size());
        return 0;
    case kUpdateScreen:
        return 0; // drawn each frame anyway
    case kCmLerpTag: {
        auto [it, added] = _md3s.try_emplace(a[1]);
        if (added) {
            std::vector<uint8_t> data;
            auto md3 = std::make_shared<Md3Model>();
            const std::string &name = _models.name(a[1]);
            if (!name.empty() && !_files.read(name, data) && !parseModel(data, *md3))
                it->second = std::move(md3);
        }
        return writeLerpTag(vm, a[0], it->second.get(), a[2], a[3], Qvm::asFloat(a[4]), str(5, 64));
    }
    case kRSetColor:
        if (!a[0])
            _color = {1, 1, 1, 1};
        else if (!vm.valid(a[0], 16))
            vm.abort("R_SetColor: colour outside the program's memory");
        else
            for (int i = 0; i < 4; ++i)
                _color[size_t(i)] = Qvm::asFloat(vm.readInt(a[0] + 4 * i));
        return 0;
    case kRDrawStretchPic:
        _pictures.push_back({f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), a[8], _color});
        return 0;
    case kSRegisterSound:
        return _sounds.add(str(0, 256));
    case kSStartLocalSound: // (sound, channel); the menus use channel 6
        if (startLocalSound && !_sounds.name(a[0]).empty())
            startLocalSound(_sounds.name(a[0]), a[1]);
        return 0;
    case kKeyKeynumToStringBuf:
        vm.writeString(a[1], KeyBindings::keyName(a[0]), a[2]);
        return 0;
    case kKeyGetBindingBuf:
        vm.writeString(a[1], bindings ? bindings->get(a[0]) : std::string(), a[2]);
        return 0;
    case kKeySetBinding: // (key, command): the Controls menu
        if (bindings)
            bindings->set(a[0], str(1, 1024));
        return 0;
    case kKeyIsDown:
        return 0;
    case kKeyGetOverstrikeMode:
        return _overstrike;
    case kKeySetOverstrikeMode:
        _overstrike = a[0] != 0;
        return 0;
    case kKeyClearStates:
        return 0;
    case kKeyGetCatcher:
        return _catcher;
    case kKeySetCatcher:
        _catcher = a[0];
        return 0;
    case kGetClipboardData:
        vm.writeString(a[0], "", a[1]);
        return 0;
    case kGetGlconfig:
        writeGlconfig(a[0]);
        return 0;
    case kGetClientState:
        writeClientState(a[0]);
        return 0;
    case kLanGetLocalServerCount:
        return browser ? browser->localServerCount() : 0;
    case kLanGetLocalServerAddressString:
        vm.writeString(a[1], browser ? browser->localServerAddress(a[0]) : std::string(), a[2]);
        return 0;
    case kLanGetGlobalServerCount:
        return browser ? browser->globalServerCount() : 0;
    case kLanGetGlobalServerAddressString:
        vm.writeString(a[1], browser ? browser->globalServerAddress(a[0]) : std::string(), a[2]);
        return 0;
    case kLanGetPingQueueCount:
        return browser ? browser->pingQueueCount() : 0;
    case kLanClearPing:
        if (browser)
            browser->clearPing(a[0]);
        return 0;
    case kLanGetPing: {
        std::string address;
        int time = 0;
        if (browser)
            browser->getPing(a[0], now, _cvars.integer("cl_maxPing") ? _cvars.integer("cl_maxPing") : 800, address,
                             time);
        vm.writeString(a[1], address, a[2]);
        if (vm.checkWrite(a[3], 4))
            vm.writeInt(a[3], time);
        return 0;
    }
    case kLanGetPingInfo:
        vm.writeString(a[1], browser ? browser->pingInfo(a[0]) : std::string(), a[2]);
        return 0;
    case kGetConfigString: { // (index, buffer, size) -> whether there's one
        const std::string text = configstring ? configstring(a[0]) : std::string();
        vm.writeString(a[1], text, a[2]);
        return configstring ? 1 : 0;
    }
    case kCvarRegister: {
        const CvarStore::Cvar &cv = _cvars.create(str(1, 256), str(2, 256), a[3]);
        if (a[0])
            _cvars.writeVmCvar(vm, a[0], cv.handle);
        return 0;
    }
    case kCvarUpdate:
        _cvars.updateVmCvar(vm, a[0]);
        return 0;
    case kMemoryRemaining:
        return 8 << 20;
    case kSetCdKey: {
        // The CD key screen's Accept, after the menus' own check: whether
        // the engine takes the key. The original engine also checks it
        // against its own rules for the online service, which is gone;
        // Reluva applies the menus' rule again and keeps the key.
        const std::string key = str(0, 64);
        if (!cdKeyLooksValid(key))
            return 0;
        _cdKey = key;
        _cdKeyRead = true;
        if (!_keyFile.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(_keyFile.parent_path(), ec);
            std::ofstream(_keyFile) << _cdKey << '\n';
        }
        return 1;
    }
    case kRModelBounds: {
        // The first frame's box, as the original renderer gives; zeros for
        // a model it can't read.
        auto [it, added] = _modelBounds.try_emplace(a[0]);
        if (added) {
            std::vector<uint8_t> data;
            Md3Model md3;
            const std::string &name = _models.name(a[0]);
            if (!name.empty() && !_files.read(name, data) && !parseModel(data, md3) && !md3.frames.empty())
                it->second = {md3.frames[0].mins, md3.frames[0].maxs};
        }
        for (int i = 0; i < 3; ++i) {
            vm.writeInt(a[1] + 4 * i, Qvm::fromFloat(it->second.first[size_t(i)]));
            vm.writeInt(a[2] + 4 * i, Qvm::fromFloat(it->second.second[size_t(i)]));
        }
        return 0;
    }
    default:
        ++_unhandled[number];
        return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
