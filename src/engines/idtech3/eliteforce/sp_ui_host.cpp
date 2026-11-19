// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/eliteforce/sp_ui_host.h"

#include "engines/idtech3/eliteforce/game_info.h"

#include "emulation/x86/format.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/raven/savegame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace reluva::idtech3::eliteforce {

namespace {

// The menus' functions, by their place in the table GetUIAPI returns.
enum Export : int {
    kInit = 0,
    kShutdown = 1,
    kKeyEvent = 2,
    kMouseEvent = 3,
    kRefresh = 4,
    kIsFullscreen = 5,
    kSetActiveMenu = 6,
    kConsoleCommand = 7,
    kDrawLoadingScreen = 8, // (server, info)
    kDrawConnectText = 9,   // (server, info)
    kExportCount = 12,
};

// The engine's functions in the table Init is handed.
enum Import : int {
    kPrintf = 0,
    kError = 1,
    kCvarSet = 2,
    kCvarVariableValue = 3,
    kCvarVariableStringBuffer = 4,
    kCvarSetValue = 5,
    kCvarReset = 6,
    kCvarCreate = 7,
    kCvarInfoStringBuffer = 8,
    kArgc = 9,
    kArgv = 10,
    kCmdExecuteText = 11,
    kCmdTokenizeString = 12,
    kFsFOpenFile = 13,
    kFsRead = 14,
    kFsWrite = 15,
    kFsFCloseFile = 16,
    kFsGetFileList = 17,
    kFsReadFile = 18,
    kFsFreeFile = 19,
    kRegisterModel = 20,
    kRegisterSkin = 21,
    kRegisterShader = 22,
    kRegisterShaderNoMip = 23,
    kClearScene = 24,
    kAddRefEntityToScene = 25,
    kAddPolyToScene = 26,
    kAddLightToScene = 27,
    kRenderScene = 28,
    kSetColor = 29,
    kDrawStretchPic = 30,
    kEmpty = 31,
    kUpdateScreen = 32,
    kScreenShot = 33,
    kLerpTag = 34,
    kStartLocalSound = 35,
    kRegisterSound = 36,
    kStartLocalLoopingSound = 37,
    kDrawStretchRaw = 38,
    kSavePicture = 39,
    kSaveComment = 40,
    kSaveReadable = 41,
    kAllowedToSave = 42,
    kSetSaveComment = 43,
    kScreenPixels = 44,
    kKeynumToStringBuf = 45,
    kGetBindingBuf = 46,
    kSetBinding = 47,
    kIsDown = 48,
    kGetOverstrikeMode = 49,
    kSetOverstrikeMode = 50,
    kClearStates = 51,
    kGetCatcher = 52,
    kSetCatcher = 53,
    kGetClipboardData = 54,
    kGetGlconfig = 55,
    kConnectionState = 56,
    kGetConfigString = 57,
    kMilliseconds = 58,
    kImportCount = 59,
};

constexpr uint32_t kScratch = 4096;
constexpr uint32_t kCommentSize = 128;

uint32_t floatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A cvar's text for a number, as the original's Cvar_SetValue writes it.
std::string numberText(float v) {
    char buf[64];
    if (v == float(int32_t(v)))
        std::snprintf(buf, sizeof buf, "%d", int32_t(v));
    else
        std::snprintf(buf, sizeof buf, "%f", double(v));
    return buf;
}

} // namespace

SpUiHost::SpUiHost(const SearchSet &files, CvarStore &cvars) : _files(files), _cvars(cvars), _openFiles(files) {
}

SpUiHost::~SpUiHost() = default;

void SpUiHost::import(int slot, const std::function<uint32_t(x86::Cpu &)> &fn) {
    const uint32_t address = _cpu->addNative("ui engine[" + std::to_string(slot) + "]", [fn](x86::Cpu &c) {
        c.reg[x86::Cpu::EAX] = fn(c);
        return 0u;
    });
    _cpu->write32(_imports + 4 * uint32_t(slot), address);
}

uint32_t SpUiHost::text(const std::string &s) {
    // Text handed to the menus' functions: kept in a scratch block (they
    // copy what they keep).
    _cpu->writeString(_scratch, s, kScratch);
    return _scratch;
}

float SpUiHost::argFloat(x86::Cpu &c, int n) const {
    float f;
    const uint32_t bits = c.arg(n);
    std::memcpy(&f, &bits, 4);
    return f;
}

Vec3f SpUiHost::readVec(uint32_t address) const {
    Vec3f v{};
    for (int i = 0; i < 3; ++i) {
        const uint32_t bits = _cpu->read32(address + 4 * uint32_t(i));
        std::memcpy(&v[size_t(i)], &bits, 4);
    }
    return v;
}

const Md3Model *SpUiHost::md3(int32_t handle) {
    auto [it, added] = _md3s.try_emplace(handle);
    if (added) {
        const std::string &name = _models.name(handle);
        std::vector<uint8_t> data;
        auto m = std::make_shared<Md3Model>();
        if (!name.empty() && !_files.read(name, data) && !parseModel(data, *m))
            it->second = std::move(m);
    }
    return it->second.get();
}

Error SpUiHost::load(const std::vector<uint8_t> &dll) {
    _cpu = std::make_unique<x86::Cpu>(kMemory);
    _process = std::make_unique<x86::Win32Process>(*_cpu, kExeName);
    _process->output = [this](const std::string &s) {
        if (print)
            print(s);
    };
    if (clock)
        _process->milliseconds = clock;
    if (Error e = _process->load("efuix86.dll", dll, _module))
        return e;
    if (Error e = _process->attach(*_module))
        return e;
    _imports = _process->memory().alloc(4 * kImportCount);
    _scratch = _process->memory().alloc(kScratch);
    _scratch2 = _process->memory().alloc(kScratch);
    _comment = _process->memory().alloc(kCommentSize);
    _capture = _process->memory().alloc(kShotSize);
    addImports();
    const uint32_t getUiApi = _module->exportAddress("GetUIAPI");
    if (!getUiApi)
        return Error::fail("efuix86.dll has no GetUIAPI");
    uint32_t result = 0;
    if (Error e = _cpu->call(getUiApi, {}, result))
        return Error::fail("GetUIAPI: " + e.message);
    if (!_cpu->valid(result, 4 * kExportCount))
        return Error::fail("GetUIAPI gave no table");
    _exports = result;
    if (Error e = callUi(kInit, {uint32_t(kApiVersion), _imports}))
        return Error::fail("The menus' Init: " + e.message);
    return Error::success();
}

Error SpUiHost::callUi(int index, const std::vector<uint32_t> &args, uint32_t *result) {
    uint32_t r = 0;
    if (Error e = _cpu->call(_cpu->read32(_exports + 4 * uint32_t(index)), args, r))
        return e;
    if (result)
        *result = r;
    return Error::success();
}

void SpUiHost::setCapture(const std::vector<uint8_t> &rgba) {
    _captured = rgba.size() == kShotSize;
    if (_captured)
        std::memcpy(_cpu->memory().data() + _capture, rgba.data(), kShotSize);
    else
        std::memset(_cpu->memory().data() + _capture, 0, kShotSize);
}

std::vector<uint8_t> SpUiHost::capture() const {
    if (!_captured)
        return std::vector<uint8_t>(kShotSize, 0);
    const uint8_t *p = _cpu->memory().data() + _capture;
    return std::vector<uint8_t>(p, p + kShotSize);
}

Error SpUiHost::keyEvent(int32_t key) {
    return callUi(kKeyEvent, {uint32_t(key)});
}

Error SpUiHost::mouseEvent(int32_t dx, int32_t dy) {
    return callUi(kMouseEvent, {uint32_t(dx), uint32_t(dy)});
}

Error SpUiHost::refresh(int32_t time) {
    _time = time;
    _pictures.clear();
    _raw.clear();
    _scene = Scene{};
    _color = {1, 1, 1, 1};
    return callUi(kRefresh, {uint32_t(time)});
}

bool SpUiHost::fullscreen() {
    // (0, &out): out nonzero when they cover the screen.
    _cpu->write32(_scratch, 0);
    if (callUi(kIsFullscreen, {0, _scratch}))
        return false;
    return _cpu->read32(_scratch) != 0;
}

Error SpUiHost::drawLoadingScreen(const std::string &server, const std::string &info) {
    _pictures.clear();
    _raw.clear();
    _scene = Scene{};
    _color = {1, 1, 1, 1};
    _cpu->writeString(_scratch2, info, kScratch);
    return callUi(kDrawLoadingScreen, {text(server), _scratch2});
}

Error SpUiHost::drawConnectText(const std::string &server, const std::string &info) {
    _pictures.clear();
    _raw.clear();
    _scene = Scene{};
    _color = {1, 1, 1, 1};
    _cpu->writeString(_scratch2, info, kScratch);
    return callUi(kDrawConnectText, {text(server), _scratch2});
}

Error SpUiHost::setActiveMenu(const std::string &name) {
    return callUi(kSetActiveMenu, {name.empty() ? 0u : text(name)});
}

Error SpUiHost::consoleCommand(const std::string &line, bool *handled) {
    _args = splitCommand(line);
    uint32_t r = 0;
    const Error e = callUi(kConsoleCommand, {}, &r);
    if (handled)
        *handled = r != 0;
    return e;
}

uint32_t SpUiHost::fileList(const std::string &folder, const std::string &extension, uint32_t buffer, uint32_t size) {
    // The names (without the folder) of the files directly in `folder`
    // ending in `extension` (with or without its dot), NUL-separated.
    std::string dir = lower(folder);
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    std::string ext = lower(extension);
    if (!ext.empty() && ext[0] != '.')
        ext = "." + ext;
    std::vector<std::string> names;
    for (const std::string &n : _files.list()) {
        const std::string key = normalizeArchiveName(n);
        if (key.size() <= dir.size() + 1 || key.compare(0, dir.size(), dir) != 0 || key[dir.size()] != '/')
            continue;
        const std::string rest = key.substr(dir.size() + 1);
        if (rest.find('/') != std::string::npos)
            continue;
        if (!ext.empty() && (rest.size() < ext.size() || rest.compare(rest.size() - ext.size(), ext.size(), ext) != 0))
            continue;
        names.push_back(rest);
    }
    if (dir == "saves" && !saveDir.empty()) {
        // Saved games written here too, besides those in the game's folder.
        // (Iterated without exceptions, and the names as UTF-8: a name the
        // system's code page can't hold would throw from string().)
        std::error_code ec;
        for (std::filesystem::directory_iterator it(saveDir, ec), end; !ec && it != end; it.increment(ec)) {
            const auto &entry = *it;
            const std::u8string u8 = entry.path().filename().u8string();
            const std::string n = lower(std::string(u8.begin(), u8.end()));
            if (entry.is_regular_file(ec) &&
                (ext.empty() || (n.size() >= ext.size() && n.compare(n.size() - ext.size(), ext.size(), ext) == 0)))
                names.push_back(n);
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    if (dir == "saves" && listSave)
        std::erase_if(names, [this](const std::string &n) {
            const size_t dot = n.rfind('.');
            return !listSave(dot == std::string::npos ? n : n.substr(0, dot));
        });
    uint32_t used = 0;
    uint32_t count = 0;
    for (const std::string &n : names) {
        if (used + n.size() + 1 > size)
            break;
        _cpu->writeString(buffer + used, n, uint32_t(n.size()) + 1);
        used += uint32_t(n.size()) + 1;
        ++count;
    }
    return count;
}

void SpUiHost::addImports() {
    auto unhandled = [this](int slot) {
        import(slot, [this, slot](x86::Cpu &) {
            ++_unhandled[slot];
            return 0u;
        });
    };
    for (int i = 0; i < kImportCount; ++i)
        unhandled(i);

    import(kPrintf, [this](x86::Cpu &c) {
        // (format, ...), as printf.
        if (print)
            print(x86::formatPrintf(c, c.arg(0), c.reg[x86::Cpu::ESP] + 8));
        return 0u;
    });
    import(kError, [](x86::Cpu &c) {
        // (level, format, ...)
        c.abort("efuix86.dll: " + x86::formatPrintf(c, c.arg(1), c.reg[x86::Cpu::ESP] + 12));
        return 0u;
    });

    // Cvars.
    import(kCvarSet, [this](x86::Cpu &c) {
        _cvars.set(str(c, 0, 256), str(c, 1, 4096), true); // as the original's: read-only ones too
        return 0u;
    });
    import(kCvarVariableValue, [this](x86::Cpu &c) {
        c.fpuPush(_cvars.value(str(c, 0, 256)));
        return 0u;
    });
    import(kCvarVariableStringBuffer, [this](x86::Cpu &c) {
        c.writeString(c.arg(1), _cvars.string(str(c, 0, 256)), c.arg(2));
        return 0u;
    });
    import(kCvarSetValue, [this](x86::Cpu &c) {
        _cvars.set(str(c, 0, 256), numberText(argFloat(c, 1)), true);
        return 0u;
    });
    import(kCvarReset, [this](x86::Cpu &c) {
        const std::string name = str(c, 0, 256);
        if (const CvarStore::Cvar *cv = _cvars.find(name))
            _cvars.set(name, std::string(cv->defaultValue), true);
        return 0u;
    });
    import(kCvarCreate, [this](x86::Cpu &c) {
        _cvars.create(str(c, 0, 256), str(c, 1, 4096), int32_t(c.arg(2)));
        return 0u;
    });
    import(kCvarInfoStringBuffer, [this](x86::Cpu &c) {
        std::string info;
        for (const CvarStore::Cvar &cv : _cvars.all())
            if (cv.flags & int32_t(c.arg(0)))
                info += "\\" + cv.name + "\\" + cv.value;
        c.writeString(c.arg(1), info, c.arg(2));
        return 0u;
    });

    // Commands.
    import(kArgc, [this](x86::Cpu &) { return uint32_t(_args.size()); });
    import(kArgv, [this](x86::Cpu &c) {
        const uint32_t n = c.arg(0);
        c.writeString(c.arg(1), n < _args.size() ? _args[n] : std::string(), c.arg(2));
        return 0u;
    });
    import(kCmdExecuteText, [this](x86::Cpu &c) {
        // (when, text): run by the page between frames.
        for (const std::string &line : splitCommands(str(c, 1, 4096)))
            if (!line.empty())
                _commands.push_back(line);
        return 0u;
    });
    import(kCmdTokenizeString, [this](x86::Cpu &c) {
        _args = splitCommand(str(c, 0, 4096));
        return 0u;
    });

    // Files: read from the game's; writes kept only while open.
    import(kFsFOpenFile, [this](x86::Cpu &c) -> uint32_t {
        // (path, &handle, mode: 0 read, 1 write, 2 append) -> length
        const uint32_t handleAt = c.arg(1);
        uint32_t handle = 0;
        const int32_t length = _openFiles.open(str(c, 0, 256), int32_t(c.arg(2)), handleAt != 0, true, handle);
        if (handleAt)
            c.write32(handleAt, handle);
        return uint32_t(length);
    });
    import(kFsRead, [this](x86::Cpu &c) -> uint32_t {
        // (buffer, length, handle) -> bytes read
        if (!c.valid(c.arg(0), c.arg(1)))
            return 0;
        return uint32_t(_openFiles.read(c.arg(2), c.memory().data() + c.arg(0), c.arg(1)));
    });
    import(kFsWrite, [this](x86::Cpu &c) -> uint32_t {
        if (!c.valid(c.arg(0), c.arg(1)))
            return 0;
        return uint32_t(_openFiles.write(c.arg(2), c.memory().data() + c.arg(0), c.arg(1)));
    });
    import(kFsFCloseFile, [this](x86::Cpu &c) {
        _openFiles.close(c.arg(0));
        return 0u;
    });
    import(kFsGetFileList, [this](x86::Cpu &c) -> uint32_t {
        // (folder, extension, buffer, size) -> count. "$modlist": the
        // mods' folders and descriptions, as the engine lists them.
        const std::string folder = str(c, 0, 256);
        if (folder == "$modlist") {
            const uint32_t buffer = c.arg(2);
            const uint32_t size = c.arg(3);
            if (int32_t(size) <= 0 || !c.valid(buffer, size))
                return 0;
            std::vector<uint8_t> list;
            const uint32_t count = packModList(
                modInstall.empty() ? std::vector<ModFolder>{} : listMods(modInstall, modBaseDir), size, list);
            std::memcpy(c.memory().data() + buffer, list.data(), list.size());
            return count;
        }
        return fileList(folder, str(c, 1, 64), c.arg(2), c.arg(3));
    });
    import(kFsReadFile, [this](x86::Cpu &c) -> uint32_t {
        // (name, &buffer or 0) -> length, -1 if there's no such file
        std::vector<uint8_t> data;
        if (_files.read(str(c, 0, 256), data))
            return 0xffffffffu;
        if (c.arg(1)) {
            const uint32_t a = _process->memory().alloc(uint32_t(data.size()) + 1);
            if (a) {
                std::memcpy(c.memory().data() + a, data.data(), data.size());
                c.write8(a + uint32_t(data.size()), 0);
            }
            c.write32(c.arg(1), a);
        }
        return uint32_t(data.size());
    });
    import(kFsFreeFile, [this](x86::Cpu &c) {
        if (c.arg(0))
            _process->memory().free(c.arg(0));
        return 0u;
    });

    // The renderer: what's registered and drawn is recorded.
    import(kRegisterModel, [this](x86::Cpu &c) { return uint32_t(_models.add(str(c, 0, 256))); });
    import(kRegisterSkin, [this](x86::Cpu &c) { return uint32_t(_skins.add(str(c, 0, 256))); });
    // 0 for a shader that can't be found, as the original's renderer.
    auto registerShader = [this](x86::Cpu &c) {
        const std::string name = str(c, 0, 256);
        return shaderExists && !shaderExists(name) ? 0u : uint32_t(_shaders.add(name));
    };
    import(kRegisterShader, registerShader);
    import(kRegisterShaderNoMip, registerShader);
    import(kClearScene, [this](x86::Cpu &) {
        _scene.clear();
        return 0u;
    });
    import(kAddRefEntityToScene, [this](x86::Cpu &c) {
        if (c.valid(c.arg(0), SpGameHost::kRefEntitySize))
            _scene.addEntity(c.memory().data() + c.arg(0), SpGameHost::kRefEntitySize, 0);
        return 0u;
    });
    import(kAddPolyToScene, [this](x86::Cpu &c) {
        const uint32_t bytes = std::min<uint32_t>(c.arg(1), 256) * 24;
        _scene.addPoly(int32_t(c.arg(0)), int32_t(c.arg(1)),
                       bytes && c.valid(c.arg(2), bytes) ? c.memory().data() + c.arg(2) : nullptr);
        return 0u;
    });
    import(kAddLightToScene, [this](x86::Cpu &c) {
        _scene.addLight(readVec(c.arg(0)), argFloat(c, 1), argFloat(c, 2), argFloat(c, 3), argFloat(c, 4));
        return 0u;
    });
    import(kRenderScene, [this](x86::Cpu &c) {
        _scene.render(c.valid(c.arg(0), kRefdefSize) ? c.memory().data() + c.arg(0) : nullptr, _pictures.size());
        return 0u;
    });
    import(kSetColor, [this](x86::Cpu &c) {
        if (!c.arg(0))
            _color = {1, 1, 1, 1};
        else
            for (int i = 0; i < 4; ++i) {
                const uint32_t bits = c.read32(c.arg(0) + 4 * uint32_t(i));
                std::memcpy(&_color[size_t(i)], &bits, 4);
            }
        return 0u;
    });
    import(kDrawStretchPic, [this](x86::Cpu &c) {
        _pictures.push_back({argFloat(c, 0), argFloat(c, 1), argFloat(c, 2), argFloat(c, 3), argFloat(c, 4),
                             argFloat(c, 5), argFloat(c, 6), argFloat(c, 7), int32_t(c.arg(8)), _color});
        return 0u;
    });
    import(kEmpty, [](x86::Cpu &) { return 0u; });
    import(kUpdateScreen, [](x86::Cpu &) { return 0u; });
    import(kLerpTag, [this](x86::Cpu &c) -> uint32_t {
        // (orientation out, model, start frame, end frame, frac, tag) -> found
        std::array<float, 12> o{};
        const bool found =
            lerpTag(md3(int32_t(c.arg(1))), int32_t(c.arg(2)), int32_t(c.arg(3)), argFloat(c, 4), str(c, 5, 64), o);
        if (c.valid(c.arg(0), 48))
            for (int i = 0; i < 12; ++i)
                c.write32(c.arg(0) + 4 * uint32_t(i), floatBits(o[size_t(i)]));
        return found ? 1 : 0;
    });

    // Sound.
    import(kStartLocalSound, [this](x86::Cpu &c) {
        if (startLocalSound)
            startLocalSound(_sounds.name(int32_t(c.arg(0))), int(c.arg(1)));
        return 0u;
    });
    import(kRegisterSound, [this](x86::Cpu &c) { return uint32_t(_sounds.add(str(c, 0, 256))); });
    import(kStartLocalLoopingSound, [](x86::Cpu &) { return 0u; });
    import(kDrawStretchRaw, [this](x86::Cpu &c) {
        // (x, y, w, h in screen pixels, cols, rows, RGBA data, dirty)
        RawImage img;
        img.x = float(int32_t(c.arg(0)));
        img.y = float(int32_t(c.arg(1)));
        img.w = float(int32_t(c.arg(2)));
        img.h = float(int32_t(c.arg(3)));
        img.cols = int(c.arg(4));
        img.rows = int(c.arg(5));
        const uint32_t bytes = uint32_t(std::max(img.cols, 0)) * uint32_t(std::max(img.rows, 0)) * 4;
        if (img.cols > 0 && img.rows > 0 && img.cols <= 1024 && img.rows <= 1024 && c.valid(c.arg(6), bytes)) {
            const uint8_t *p = c.memory().data() + c.arg(6);
            img.rgba.assign(p, p + bytes);
            _raw.push_back(std::move(img));
        }
        return 0u;
    });

    // Saved games, by name (no folder or extension), as the load and save
    // menus ask: a picture, a comment, whether one can be read.
    auto openSave = [this](x86::Cpu &c, int n, SaveReader &reader) {
        std::vector<uint8_t> file;
        if (!readSave || !readSave(str(c, n, 256), file))
            return false;
        reader = SaveReader(std::move(file));
        return true;
    };
    import(kSavePicture, [this, openSave](x86::Cpu &c) -> uint32_t {
        // (name, buffer of 256 x 256 x 4)
        SaveReader r;
        std::vector<uint8_t> comment;
        std::vector<uint8_t> shot;
        if (!openSave(c, 0, r) || r.next(chunkId("COMM"), comment) || r.next(chunkId("SHOT"), shot, kShotSize) ||
            !c.valid(c.arg(1), kShotSize))
            return 0;
        std::memcpy(c.memory().data() + c.arg(1), shot.data(), kShotSize);
        return 1;
    });
    import(kSaveComment, [this, openSave](x86::Cpu &c) -> uint32_t {
        // (name) -> its comment (128 bytes), or 0
        SaveReader r;
        std::vector<uint8_t> comment;
        if (!openSave(c, 0, r) || r.next(chunkId("COMM"), comment, kCommentSize))
            return 0;
        std::memcpy(c.memory().data() + _comment, comment.data(), kCommentSize);
        return _comment;
    });
    import(kSaveReadable, [this, openSave](x86::Cpu &c) -> uint32_t {
        SaveReader r;
        std::vector<uint8_t> data;
        return openSave(c, 0, r) && !r.next(chunkId("COMM"), data, kCommentSize) &&
                       !r.next(chunkId("SHOT"), data, kShotSize)
                   ? 1
                   : 0;
    });
    import(kSetSaveComment, [this](x86::Cpu &c) {
        saveComment = str(c, 0, kCommentSize);
        return 0u;
    });
    import(kScreenShot, [](x86::Cpu &) {
        // The original's captures only in a game with no menu open, so
        // never from here; the page captures as the in-game menu opens.
        return 0u;
    });
    import(kScreenPixels, [this](x86::Cpu &c) -> uint32_t {
        // (&captured) -> the last capture, 256 x 256 x 4
        if (c.arg(0))
            c.write32(c.arg(0), _captured ? 1 : 0);
        return _capture;
    });

    import(kAllowedToSave, [this](x86::Cpu &c) -> uint32_t {
        // (flag): with no game, the flag itself (so SetActiveMenu, which
        // asks with 1 before opening any menu, goes ahead); with one, the
        // game's say (its GameAllowedToSaveHere), as the original's engine.
        const uint32_t flag = c.arg(0);
        return askGame ? askGame(flag) : flag;
    });

    // Keys.
    import(kKeynumToStringBuf, [](x86::Cpu &c) {
        c.writeString(c.arg(1), KeyBindings::keyName(int32_t(c.arg(0))), c.arg(2));
        return 0u;
    });
    import(kGetBindingBuf, [this](x86::Cpu &c) {
        const int32_t k = int32_t(c.arg(0));
        c.writeString(c.arg(1), bindings && k >= 0 && k < q3key::kCount ? bindings->get(k) : std::string(), c.arg(2));
        return 0u;
    });
    import(kSetBinding, [this](x86::Cpu &c) {
        const int32_t k = int32_t(c.arg(0));
        if (bindings && k >= 0 && k < q3key::kCount)
            bindings->set(k, str(c, 1, 1024));
        return 0u;
    });
    import(kIsDown, [this](x86::Cpu &c) -> uint32_t { return keysDown.count(int32_t(c.arg(0))) ? 1 : 0; });
    import(kGetOverstrikeMode, [this](x86::Cpu &) -> uint32_t { return _overstrike ? 1 : 0; });
    import(kSetOverstrikeMode, [this](x86::Cpu &c) {
        _overstrike = c.arg(0) != 0;
        return 0u;
    });
    import(kClearStates, [this](x86::Cpu &) {
        keysDown.clear();
        return 0u;
    });
    import(kGetCatcher, [this](x86::Cpu &) { return uint32_t(_catcher); });
    import(kSetCatcher, [this](x86::Cpu &c) {
        _catcher = int32_t(c.arg(0));
        return 0u;
    });
    import(kGetClipboardData, [](x86::Cpu &c) {
        c.writeString(c.arg(0), "", c.arg(1));
        return 0u;
    });

    import(kGetGlconfig, [](x86::Cpu &c) {
        SpGameHost::writeGlconfigTo(c, c.arg(0));
        return 0u;
    });
    import(kConnectionState, [this](x86::Cpu &) { return uint32_t(connectionState); });
    import(kGetConfigString, [this](x86::Cpu &c) -> uint32_t {
        std::string s;
        const bool have = configString && configString(int32_t(c.arg(0)), s);
        c.writeString(c.arg(1), have ? s : std::string(), c.arg(2));
        return have ? 1 : 0;
    });
    import(kMilliseconds, [this](x86::Cpu &) { return uint32_t(_time); });
}

} // namespace reluva::idtech3::eliteforce
