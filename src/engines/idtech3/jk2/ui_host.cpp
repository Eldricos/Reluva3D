// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/jk2/ui_host.h"

#include "engines/idtech3/formats/md3.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace reluva::idtech3::jk2 {

namespace {

// Jedi Outcast ui.qvm system calls. The evidence for each number is in
// docs/engines/idtech3/games/jk2/systems/syscalls.md.
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
    kRRegisterShaderNoMip = 20,
    kRClearScene = 21,
    kRAddRefEntityToScene = 22,
    kRAddLightToScene = 24,
    kRRenderScene = 25,
    kRSetColor = 26,
    kRDrawStretchPic = 27,
    kUpdateScreen = 28,
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
    kLanGetPingQueueCount = 46,
    kLanClearPing = 47,
    kLanGetPing = 48,
    kLanGetPingInfo = 49,
    kCvarRegister = 50,
    kCvarUpdate = 51,
    kMemoryRemaining = 52,
    kRRegisterFont = 56,
    kRFontStrLenPixels = 57,
    kRFontStrLenChars = 58,
    kRFontStrHeightPixels = 59,
    kRFontDrawString = 60,
    kAnyLanguageReadCharFromString = 61,
    kRModelBounds = 62,
    kPcAddGlobalDefine = 63,
    kPcLoadSource = 64,
    kPcFreeSource = 65,
    kPcReadToken = 66,
    kPcSourceFileAndLine = 67,
    kPcLoadGlobalDefines = 68,
    kPcRemoveAllGlobalDefines = 69,
    kSStopBackgroundTrack = 70,
    kSStartBackgroundTrack = 71,
    kRealTime = 72,
    kLanGetServerCount = 73,
    kLanGetServerAddressString = 74,
    kLanGetServerInfo = 75,
    kLanMarkServerVisible = 76,
    kLanUpdateVisiblePings = 77,
    kLanResetPings = 78,
    kLanLoadCachedServers = 79,
    kLanSaveCachedServers = 80,
    kLanAddServer = 81,
    kLanRemoveServer = 82,
    kCinPlayCinematic = 83,
    kCinStopCinematic = 84,
    kCinRunCinematic = 85,
    kCinDrawCinematic = 86,
    kCinSetExtents = 87,
    kRRemapShader = 88,
    kLanServerStatus = 89,
    kLanGetServerPing = 90,
    kLanServerIsVisible = 91,
    kLanCompareServers = 92,
    kSpRegister = 200,
    kSpGetStringTextString = 201,
};

// A font handle's style bits, over its number.
constexpr int32_t kStyleDropShadow = int32_t(0x80000000);
constexpr int32_t kStyleBlink = 0x40000000;
constexpr int32_t kFontMask = 0x00ffffff;

// glconfig_t: renderer, vendor and version strings of 1024 bytes, an
// 8192-byte extensions string, then 17 words.
constexpr int32_t kGlconfigWords = 3 * 1024 + 8192;
constexpr int32_t kGlconfigSize = kGlconfigWords + 17 * 4;
// uiClientState_t: three words, then three 1024-byte strings.
constexpr int32_t kClientStateSize = 12 + 3 * 1024;
// pc_token_t: type, subtype, the number as int and float, then the text.
constexpr int32_t kTokenText = 16;
constexpr int32_t kTokenTextSize = 1024;

// Quake III's colour codes, ^0 to ^7.
constexpr float kCodeColours[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
                                      {0, 0, 1}, {0, 1, 1}, {1, 0, 1}, {1, 1, 1}};

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
    int32_t version = 0;
    if (Error err = call(kGetApiVersion, {}, version))
        return err;
    if (version != kApiVersion)
        return Error::fail("vm/ui.qvm reports API version " + std::to_string(version) + ", not " +
                           std::to_string(kApiVersion));
    return Error::success();
}

Error UiHost::call(int32_t command, const std::vector<int32_t> &args, int32_t &result) {
    return _vm.call(command, args, result);
}

Error UiHost::refresh(int32_t time) {
    setTime(time);
    int32_t fullscreen = 0;
    if (Error err = call(kIsFullscreen, {}, fullscreen))
        return err;
    // Disconnected, the main menu opens if nothing covers the screen, as
    // the original engine reopens it.
    if (!fullscreen && clientState.connState <= 1)
        if (Error err = call(kSetActiveMenu, {1}))
            return err;
    return call(kRefresh, {time});
}

bool UiHost::isCursor(const std::string &shader) const {
    const std::string name = normalizeArchiveName(shader);
    const size_t slash = name.find_last_of('/');
    return name.compare(slash == std::string::npos ? 0 : slash + 1, 6, "cursor") == 0;
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

void UiHost::addPicture(float x, float y, float w, float h, float s1, float t1, float s2, float t2, int32_t shader,
                        const std::array<float, 4> &color) {
    // From 640x480 to the frame's pixels.
    const float sx = float(_width) / 640;
    const float sy = float(_height) / 480;
    _pictures.push_back({x * sx, y * sy, w * sx, h * sy, s1, t1, s2, t2, shader, color});
}

void UiHost::writeGlconfig(int32_t address) {
    if (!_vm.checkWrite(address, kGlconfigSize))
        return;
    std::memset(&_vm.memory()[size_t(address)], 0, kGlconfigSize);
    _vm.writeString(address, "Reluva", 1024);
    _vm.writeString(address + 1024, "Reluva", 1024);
    _vm.writeString(address + 2048, "1.0", 1024);
    // The screen the menus see is 640x480, the original renderer's 2D
    // space, so their own scaling (vidWidth / 640) is 1.
    const int32_t words[17] = {
        2048, 2,      // maxTextureSize, maxActiveTextures
        32,   24,  8, // colour, depth, stencil bits
        0,    0,   1,
        1,    1, // deviceSupportsGamma, textureCompression, envAdd, anisotropic, clampToEdge
        640,  480, Qvm::fromFloat(640.0f / 480.0f),
        60,           // displayFrequency
        0,    0,   0, // isFullscreen, stereoEnabled, smpActive
    };
    for (int i = 0; i < 17; ++i)
        _vm.writeInt(address + kGlconfigWords + 4 * i, words[i]);
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

int32_t UiHost::registerFont(const std::string &name) {
    const std::string key = fs::toLower(name);
    if (const auto it = _fontByName.find(key); it != _fontByName.end())
        return it->second;
    std::vector<uint8_t> data;
    LoadedFont f;
    int32_t handle = 0;
    if (!_files.read("fonts/" + name + ".fontdat", data) && !raven::parseFontdat(data, key, f.font)) {
        f.shader = _shaders.add("fonts/" + name);
        _fonts.push_back(std::move(f));
        handle = int32_t(_fonts.size());
    }
    return _fontByName[key] = handle;
}

const raven::Font *UiHost::font(int32_t handle) const {
    const int32_t i = (handle & kFontMask) - 1;
    return i >= 0 && i < int32_t(_fonts.size()) ? &_fonts[size_t(i)].font : nullptr;
}

void UiHost::drawString(int32_t x, int32_t y, const std::string &text, const std::array<float, 4> &color,
                        int32_t handle, int32_t maxChars, float scale, bool shadow) {
    const raven::Font *f = font(handle);
    if (!f)
        return;
    // Blinking text is off every other 128 ms.
    if ((handle & kStyleBlink) && ((_time >> 7) & 1))
        return;
    const int32_t shader = _fonts[size_t((handle & kFontMask) - 1)].shader;
    if (handle & kStyleDropShadow) {
        const int offset = int(std::floor(float(f->pointSize) * scale * 0.075f + 0.5f));
        drawString(x + offset, y + offset, text, {0.15f, 0.15f, 0.15f, 1}, handle & kFontMask, maxChars, scale, true);
    }
    for (const raven::Font::Piece &p : f->layout(text, x, y, scale, -1, maxChars)) {
        std::array<float, 4> c = color;
        // The shadow is grey throughout: the original takes colour codes out of its text.
        if (p.colour >= 0 && !shadow)
            c = {kCodeColours[p.colour][0], kCodeColours[p.colour][1], kCodeColours[p.colour][2], 1};
        addPicture(p.x, p.y, p.w, p.h, p.s, p.t, p.s2, p.t2, shader, c);
    }
}

int32_t UiHost::readToken(int32_t handle, int32_t address) {
    const auto it = _sources.find(handle);
    if (it == _sources.end() || !_vm.checkWrite(address, kTokenText + kTokenTextSize))
        return 0;
    botlib::BotToken t;
    if (!it->second->next(t))
        return 0;
    _lastToken[handle] = t;
    // pc_token_t's types: string 1, literal 2, number 3, name 4,
    // punctuation 5; a number's subtype says integer (0x1000, with
    // decimal 0x8) or float (0x800).
    int32_t type = 4;
    int32_t subtype = 0;
    switch (t.type) {
    case botlib::BotToken::Type::kString:
        type = 1;
        break;
    case botlib::BotToken::Type::kLiteral:
        type = 2;
        break;
    case botlib::BotToken::Type::kNumber:
        type = 3;
        subtype = t.isFloat ? 0x800 : 0x1008;
        break;
    case botlib::BotToken::Type::kName:
        type = 4;
        break;
    case botlib::BotToken::Type::kPunctuation:
        type = 5;
        break;
    }
    _vm.writeInt(address, type);
    _vm.writeInt(address + 4, subtype);
    _vm.writeInt(address + 8, int32_t(t.number));
    _vm.writeInt(address + 12, Qvm::fromFloat(float(t.number)));
    _vm.writeString(address + kTokenText, t.text, kTokenTextSize);
    return 1;
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
    case kCvarSet:
        _cvars.set(str(0, 256), str(1, 256), true);
        return 0;
    case kCvarVariableValue:
        return Qvm::fromFloat(_cvars.value(str(0, 256)));
    case kCvarVariableStringBuffer:
        vm.writeString(a[1], _cvars.string(str(0, 256)), a[2]);
        return 0;
    case kCvarSetValue: {
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
    case kCmdExecuteText:
        _commands.push_back(str(1, 4096));
        return 0;
    case kFsFOpenFile:
        return _vmFiles.open(vm, a[0], a[1], a[2]);
    case kFsRead:
        return _vmFiles.read(vm, a[0], a[1], a[2]);
    case kFsWrite:
        return 0; // nothing the menus write is kept yet
    case kFsFCloseFile:
        _vmFiles.close(a[0]);
        return 0;
    case kFsGetFileList:
        return _vmFiles.list(vm, a[0], a[1], a[2], a[3]);
    case kRRegisterModel:
        return _models.add(str(0, 256));
    case kRRegisterShaderNoMip:
        return _shaders.add(str(0, 256));
    case kRClearScene:
        _scene.clear();
        return 0;
    case kRAddRefEntityToScene:
        _scene.addEntity(vm, a[0], 0);
        return 0;
    case kRAddLightToScene:
        _scene.addLight(vm, a[0], f(1), f(2), f(3), f(4));
        return 0;
    case kRRenderScene: { // (refdef_t *): its rectangle from 640x480 to the frame
        std::array<uint8_t, 112> refdef{};
        if (!vm.valid(a[0], int32_t(refdef.size())))
            return 0;
        std::memcpy(refdef.data(), &vm.memory()[size_t(a[0])], refdef.size());
        const float scale[4] = {float(_width) / 640, float(_height) / 480, float(_width) / 640, float(_height) / 480};
        for (size_t i = 0; i < 4; ++i) {
            int32_t v = 0;
            std::memcpy(&v, &refdef[i * 4], 4);
            v = int32_t(std::lround(float(v) * scale[i]));
            std::memcpy(&refdef[i * 4], &v, 4);
        }
        _scene.render(refdef.data(), _pictures.size());
        return 0;
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
        if (vm.checkWrite(a[1], 12) && vm.checkWrite(a[2], 12))
            for (int i = 0; i < 3; ++i) {
                vm.writeInt(a[1] + 4 * i, Qvm::fromFloat(it->second.first[size_t(i)]));
                vm.writeInt(a[2] + 4 * i, Qvm::fromFloat(it->second.second[size_t(i)]));
            }
        return 0;
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
        addPicture(f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), a[8], _color);
        return 0;
    case kUpdateScreen:
        return 0; // drawn each frame anyway
    case kSRegisterSound:
        return _sounds.add(str(0, 256));
    case kSStartLocalSound:
        if (startLocalSound && !_sounds.name(a[0]).empty())
            startLocalSound(_sounds.name(a[0]), a[1]);
        return 0;
    case kKeyKeynumToStringBuf:
        vm.writeString(a[1], KeyBindings::keyName(a[0]), a[2]);
        return 0;
    case kKeyGetBindingBuf:
        vm.writeString(a[1], bindings ? bindings->get(a[0]) : std::string(), a[2]);
        return 0;
    case kKeySetBinding:
        if (bindings)
            bindings->set(a[0], str(1, 1024));
        return 0;
    case kKeyIsDown:
    case kKeyClearStates:
        return 0;
    case kKeyGetOverstrikeMode:
        return _overstrike;
    case kKeySetOverstrikeMode:
        _overstrike = a[0] != 0;
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
    case kGetConfigString: {
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
    case kRRegisterFont:
        return registerFont(str(0, 64));
    case kRFontStrLenPixels: {
        const raven::Font *ft = font(a[1]);
        return ft ? ft->textWidth(str(0, 4096), f(2)) : 0;
    }
    case kRFontStrLenChars: {
        // Colour codes and line breaks don't count.
        const std::string text = str(0, 4096);
        int32_t n = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '^')
                ++i;
            else if (text[i] != '\n' && text[i] != '\r')
                ++n;
        }
        return n;
    }
    case kRFontStrHeightPixels: {
        const raven::Font *ft = font(a[0]);
        return ft ? int32_t(std::floor(float(ft->pointSize) * f(1) + 0.5f)) : 0;
    }
    case kRFontDrawString: { // (x, y, text, rgba, font, letters at most or -1, scale)
        std::array<float, 4> c{1, 1, 1, 1};
        if (a[3] && vm.valid(a[3], 16))
            for (int i = 0; i < 4; ++i)
                c[size_t(i)] = Qvm::asFloat(vm.readInt(a[3] + 4 * i));
        drawString(a[0], a[1], str(2, 4096), c, a[4], a[5], f(6));
        return 0;
    }
    case kAnyLanguageReadCharFromString: {
        // (char **): the character there, the pointer moved past it; one
        // byte each in the languages Jedi Outcast ships.
        if (!vm.valid(a[0], 4))
            return 0;
        const int32_t at = vm.readInt(a[0]);
        if (!vm.valid(at, 1))
            return 0;
        const auto c = int32_t(uint8_t(vm.memory()[size_t(at)]));
        vm.writeInt(a[0], at + 1);
        return c;
    }
    case kPcAddGlobalDefine:
        _globalDefines.push_back(str(0, 1024));
        return 1;
    case kPcLoadGlobalDefines: {
        // A file of #define lines, each made a global define.
        std::vector<uint8_t> data;
        if (_files.read(str(0, 256), data))
            return 0;
        const std::string text(data.begin(), data.end());
        size_t at = 0;
        while (at < text.size()) {
            size_t end = text.find('\n', at);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            const size_t d = line.find("#define");
            if (d == std::string::npos)
                continue;
            line = line.substr(d + 7);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            const size_t start = line.find_first_not_of(" \t");
            if (start != std::string::npos)
                _globalDefines.push_back(line.substr(start));
        }
        return 1;
    }
    case kPcRemoveAllGlobalDefines:
        _globalDefines.clear();
        return 0;
    case kPcLoadSource: {
        auto loader = [this](const std::string &path, std::string &text) {
            std::vector<uint8_t> data;
            if (_files.read(path, data))
                return false;
            text.assign(data.begin(), data.end());
            return true;
        };
        auto script = std::make_unique<botlib::BotScript>(loader, _globalDefines);
        script->allowAtNames();
        if (!script->open(str(0, 256)))
            return 0;
        const int32_t handle = _nextSource++;
        _sources[handle] = std::move(script);
        return handle;
    }
    case kPcFreeSource:
        _sources.erase(a[0]);
        _lastToken.erase(a[0]);
        return 1;
    case kPcReadToken:
        return readToken(a[0], a[1]);
    case kPcSourceFileAndLine: {
        const auto it = _lastToken.find(a[0]);
        if (it == _lastToken.end())
            return 0;
        vm.writeString(a[1], it->second.file, 128);
        if (vm.checkWrite(a[2], 4))
            vm.writeInt(a[2], it->second.line);
        return 1;
    }
    case kSStopBackgroundTrack:
        if (backgroundTrack)
            backgroundTrack({}, {});
        return 0;
    case kSStartBackgroundTrack:
        if (backgroundTrack)
            backgroundTrack(str(0, 256), str(1, 256));
        return 0;
    case kRealTime: { // (qtime_t *): seconds, minutes, hours, day, month, years since 1900, weekday, day of the year, DST
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        const int32_t fields[9] = {local.tm_sec,  local.tm_min,  local.tm_hour, local.tm_mday, local.tm_mon,
                                   local.tm_year, local.tm_wday, local.tm_yday, local.tm_isdst};
        if (a[0] && vm.checkWrite(a[0], 36))
            for (int i = 0; i < 9; ++i)
                vm.writeInt(a[0] + 4 * i, fields[i]);
        return int32_t(now);
    }
    // The server lists: none yet (phase 6 of the plan).
    case kLanGetPingQueueCount:
    case kLanGetServerCount:
    case kLanGetServerPing:
    case kLanServerIsVisible:
    case kLanUpdateVisiblePings:
    case kLanCompareServers:
    case kLanAddServer:
    case kLanServerStatus:
        return 0;
    case kLanClearPing:
    case kLanMarkServerVisible:
    case kLanResetPings:
    case kLanLoadCachedServers:
    case kLanSaveCachedServers:
    case kLanRemoveServer:
        return 0;
    case kLanGetPing:
        vm.writeString(a[1], "", a[2]);
        if (vm.checkWrite(a[3], 4))
            vm.writeInt(a[3], 0);
        return 0;
    case kLanGetPingInfo:
        vm.writeString(a[1], "", a[2]);
        return 0;
    case kLanGetServerAddressString:
    case kLanGetServerInfo:
        vm.writeString(a[2], "", a[3]);
        return 0;
    // Cinematics in the menus: none yet; -1 is "couldn't play it".
    case kCinPlayCinematic:
        return -1;
    case kCinStopCinematic:
    case kCinRunCinematic:
    case kCinDrawCinematic:
    case kCinSetExtents:
    case kRRemapShader:
        ++_unhandled[number];
        return 0;
    case kSpRegister: {
        // Every package is read at once; whether this one was.
        if (!_stringsLoaded) {
            _strings.load(_files);
            _stringsLoaded = true;
        }
        const std::string name = fs::toLower(str(0, 256));
        for (const raven::StringPackage &p : _strings.packages())
            if (fs::toLower(p.reference) == name)
                return 1;
        return 0;
    }
    case kSpGetStringTextString: { // (reference, buffer, size) -> whether there's such text
        if (!_stringsLoaded) {
            _strings.load(_files);
            _stringsLoaded = true;
        }
        const int language = std::clamp(_cvars.integer("sp_language"), 0, 2);
        const std::string *text = _strings.find(str(0, 256), raven::StringTable::Language(language));
        vm.writeString(a[1], text ? *text : std::string(), a[2]);
        return text ? 1 : 0;
    }
    default:
        ++_unhandled[number];
        return 0;
    }
}

} // namespace reluva::idtech3::jk2
