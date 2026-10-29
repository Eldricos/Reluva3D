// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/sp_ui_host.h"

#include "engines/idtech3/eliteforce/game_info.h"

#include "common/x86/format.h"
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
    Init = 0,
    Shutdown = 1,
    KeyEvent = 2,
    MouseEvent = 3,
    Refresh = 4,
    IsFullscreen = 5,
    SetActiveMenu = 6,
    ConsoleCommand = 7,
    DrawLoadingScreen = 8, // (server, info)
    DrawConnectText = 9,   // (server, info)
    ExportCount = 12,
};

// The engine's functions in the table Init is handed.
enum Import : int {
    Printf = 0,
    Error_ = 1,
    CvarSet = 2,
    CvarVariableValue = 3,
    CvarVariableStringBuffer = 4,
    CvarSetValue = 5,
    CvarReset = 6,
    CvarCreate = 7,
    CvarInfoStringBuffer = 8,
    Argc = 9,
    Argv = 10,
    CmdExecuteText = 11,
    CmdTokenizeString = 12,
    FsFOpenFile = 13,
    FsRead = 14,
    FsWrite = 15,
    FsFCloseFile = 16,
    FsGetFileList = 17,
    FsReadFile = 18,
    FsFreeFile = 19,
    RegisterModel = 20,
    RegisterSkin = 21,
    RegisterShader = 22,
    RegisterShaderNoMip = 23,
    ClearScene = 24,
    AddRefEntityToScene = 25,
    AddPolyToScene = 26,
    AddLightToScene = 27,
    RenderScene = 28,
    SetColor = 29,
    DrawStretchPic = 30,
    Empty = 31,
    UpdateScreen = 32,
    ScreenShot = 33,
    LerpTag = 34,
    StartLocalSound = 35,
    RegisterSound = 36,
    StartLocalLoopingSound = 37,
    DrawStretchRaw = 38,
    SavePicture = 39,
    SaveComment = 40,
    SaveReadable = 41,
    AllowedToSave = 42,
    SetSaveComment = 43,
    ScreenPixels = 44,
    KeynumToStringBuf = 45,
    GetBindingBuf = 46,
    SetBinding = 47,
    IsDown = 48,
    GetOverstrikeMode = 49,
    SetOverstrikeMode = 50,
    ClearStates = 51,
    GetCatcher = 52,
    SetCatcher = 53,
    GetClipboardData = 54,
    GetGlconfig = 55,
    ConnectionState = 56,
    GetConfigString = 57,
    Milliseconds = 58,
    ImportCount = 59,
};

constexpr uint32_t kScratch = 4096;
constexpr uint32_t kCommentSize = 128;

uint32_t floatBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

std::string lower(std::string s) {
    for (char& c : s)
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

SpUiHost::SpUiHost(const SearchSet& files, CvarStore& cvars) : files_(files), cvars_(cvars), openFiles_(files) {}

SpUiHost::~SpUiHost() = default;

void SpUiHost::import(int slot, std::function<uint32_t(x86::Cpu&)> fn) {
    const uint32_t address =
        cpu_->addNative("ui engine[" + std::to_string(slot) + "]", [fn](x86::Cpu& c) {
            c.reg[x86::Cpu::EAX] = fn(c);
            return 0u;
        });
    cpu_->write32(imports_ + 4 * uint32_t(slot), address);
}

uint32_t SpUiHost::text(const std::string& s) {
    // Text handed to the menus' functions: kept in a scratch block (they
    // copy what they keep).
    cpu_->writeString(scratch_, s, kScratch);
    return scratch_;
}

float SpUiHost::argFloat(x86::Cpu& c, int n) const {
    float f;
    const uint32_t bits = c.arg(n);
    std::memcpy(&f, &bits, 4);
    return f;
}

Vec3f SpUiHost::readVec(uint32_t address) const {
    Vec3f v{};
    for (int i = 0; i < 3; ++i) {
        const uint32_t bits = cpu_->read32(address + 4 * uint32_t(i));
        std::memcpy(&v[size_t(i)], &bits, 4);
    }
    return v;
}

const Md3Model* SpUiHost::md3(int32_t handle) {
    auto [it, added] = md3s_.try_emplace(handle);
    if (added) {
        const std::string& name = models_.name(handle);
        std::vector<uint8_t> data;
        auto m = std::make_shared<Md3Model>();
        if (!name.empty() && !files_.read(name, data) && !parseModel(data, *m))
            it->second = std::move(m);
    }
    return it->second.get();
}

Error SpUiHost::load(const std::vector<uint8_t>& dll) {
    cpu_ = std::make_unique<x86::Cpu>(kMemory);
    process_ = std::make_unique<x86::Win32Process>(*cpu_, kExeName);
    process_->output = [this](const std::string& s) {
        if (print)
            print(s);
    };
    if (Error e = process_->load("efuix86.dll", dll, module_))
        return e;
    if (Error e = process_->attach(*module_))
        return e;
    imports_ = process_->memory().alloc(4 * ImportCount);
    scratch_ = process_->memory().alloc(kScratch);
    scratch2_ = process_->memory().alloc(kScratch);
    comment_ = process_->memory().alloc(kCommentSize);
    capture_ = process_->memory().alloc(kShotSize);
    addImports();
    const uint32_t getUiApi = module_->exportAddress("GetUIAPI");
    if (!getUiApi)
        return Error::fail("efuix86.dll has no GetUIAPI");
    uint32_t result = 0;
    if (Error e = cpu_->call(getUiApi, {}, result))
        return Error::fail("GetUIAPI: " + e.message);
    if (!cpu_->valid(result, 4 * ExportCount))
        return Error::fail("GetUIAPI gave no table");
    exports_ = result;
    if (Error e = callUi(Init, {uint32_t(kApiVersion), imports_}))
        return Error::fail("The menus' Init: " + e.message);
    return Error::success();
}

Error SpUiHost::callUi(int index, const std::vector<uint32_t>& args, uint32_t* result) {
    uint32_t r = 0;
    if (Error e = cpu_->call(cpu_->read32(exports_ + 4 * uint32_t(index)), args, r))
        return e;
    if (result)
        *result = r;
    return Error::success();
}

void SpUiHost::setCapture(const std::vector<uint8_t>& rgba) {
    captured_ = rgba.size() == kShotSize;
    if (captured_)
        std::memcpy(cpu_->memory().data() + capture_, rgba.data(), kShotSize);
    else
        std::memset(cpu_->memory().data() + capture_, 0, kShotSize);
}

std::vector<uint8_t> SpUiHost::capture() const {
    if (!captured_)
        return std::vector<uint8_t>(kShotSize, 0);
    const uint8_t* p = cpu_->memory().data() + capture_;
    return std::vector<uint8_t>(p, p + kShotSize);
}

Error SpUiHost::keyEvent(int32_t key) { return callUi(KeyEvent, {uint32_t(key)}); }

Error SpUiHost::mouseEvent(int32_t dx, int32_t dy) { return callUi(MouseEvent, {uint32_t(dx), uint32_t(dy)}); }

Error SpUiHost::refresh(int32_t time) {
    time_ = time;
    pictures_.clear();
    raw_.clear();
    scene_ = Scene{};
    color_ = {1, 1, 1, 1};
    return callUi(Refresh, {uint32_t(time)});
}

bool SpUiHost::fullscreen() {
    // (0, &out): out nonzero when they cover the screen.
    cpu_->write32(scratch_, 0);
    if (callUi(IsFullscreen, {0, scratch_}))
        return false;
    return cpu_->read32(scratch_) != 0;
}

Error SpUiHost::drawLoadingScreen(const std::string& server, const std::string& info) {
    pictures_.clear();
    raw_.clear();
    scene_ = Scene{};
    color_ = {1, 1, 1, 1};
    cpu_->writeString(scratch2_, info, kScratch);
    return callUi(DrawLoadingScreen, {text(server), scratch2_});
}

Error SpUiHost::drawConnectText(const std::string& server, const std::string& info) {
    pictures_.clear();
    raw_.clear();
    scene_ = Scene{};
    color_ = {1, 1, 1, 1};
    cpu_->writeString(scratch2_, info, kScratch);
    return callUi(DrawConnectText, {text(server), scratch2_});
}

Error SpUiHost::setActiveMenu(const std::string& name) {
    return callUi(SetActiveMenu, {name.empty() ? 0u : text(name)});
}

Error SpUiHost::consoleCommand(const std::string& line, bool* handled) {
    args_ = splitCommand(line);
    uint32_t r = 0;
    const Error e = callUi(ConsoleCommand, {}, &r);
    if (handled)
        *handled = r != 0;
    return e;
}

uint32_t SpUiHost::fileList(const std::string& folder, const std::string& extension, uint32_t buffer,
                            uint32_t size) {
    // The names (without the folder) of the files directly in `folder`
    // ending in `extension` (with or without its dot), NUL-separated.
    std::string dir = lower(folder);
    while (!dir.empty() && (dir.back() == '/' || dir.back() == '\\'))
        dir.pop_back();
    std::string ext = lower(extension);
    if (!ext.empty() && ext[0] != '.')
        ext = "." + ext;
    std::vector<std::string> names;
    for (const std::string& n : files_.list()) {
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
            const auto& entry = *it;
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
        std::erase_if(names, [this](const std::string& n) {
            const size_t dot = n.rfind('.');
            return !listSave(dot == std::string::npos ? n : n.substr(0, dot));
        });
    uint32_t used = 0, count = 0;
    for (const std::string& n : names) {
        if (used + n.size() + 1 > size)
            break;
        cpu_->writeString(buffer + used, n, uint32_t(n.size()) + 1);
        used += uint32_t(n.size()) + 1;
        ++count;
    }
    return count;
}

void SpUiHost::addImports() {
    auto unhandled = [this](int slot) {
        import(slot, [this, slot](x86::Cpu&) {
            ++unhandled_[slot];
            return 0u;
        });
    };
    for (int i = 0; i < ImportCount; ++i)
        unhandled(i);

    import(Printf, [this](x86::Cpu& c) {
        // (format, ...), as printf.
        if (print)
            print(x86::formatPrintf(c, c.arg(0), c.reg[x86::Cpu::ESP] + 8));
        return 0u;
    });
    import(Error_, [](x86::Cpu& c) {
        // (level, format, ...)
        c.abort("efuix86.dll: " + x86::formatPrintf(c, c.arg(1), c.reg[x86::Cpu::ESP] + 12));
        return 0u;
    });

    // Cvars.
    import(CvarSet, [this](x86::Cpu& c) {
        cvars_.set(str(c, 0, 256), str(c, 1, 4096), true); // as the original's: read-only ones too
        return 0u;
    });
    import(CvarVariableValue, [this](x86::Cpu& c) {
        c.fpuPush(cvars_.value(str(c, 0, 256)));
        return 0u;
    });
    import(CvarVariableStringBuffer, [this](x86::Cpu& c) {
        c.writeString(c.arg(1), cvars_.string(str(c, 0, 256)), c.arg(2));
        return 0u;
    });
    import(CvarSetValue, [this](x86::Cpu& c) {
        cvars_.set(str(c, 0, 256), numberText(argFloat(c, 1)), true);
        return 0u;
    });
    import(CvarReset, [this](x86::Cpu& c) {
        const std::string name = str(c, 0, 256);
        if (const CvarStore::Cvar* cv = cvars_.find(name))
            cvars_.set(name, std::string(cv->defaultValue), true);
        return 0u;
    });
    import(CvarCreate, [this](x86::Cpu& c) {
        cvars_.create(str(c, 0, 256), str(c, 1, 4096), int32_t(c.arg(2)));
        return 0u;
    });
    import(CvarInfoStringBuffer, [this](x86::Cpu& c) {
        std::string info;
        for (const CvarStore::Cvar& cv : cvars_.all())
            if (cv.flags & int32_t(c.arg(0)))
                info += "\\" + cv.name + "\\" + cv.value;
        c.writeString(c.arg(1), info, c.arg(2));
        return 0u;
    });

    // Commands.
    import(Argc, [this](x86::Cpu&) { return uint32_t(args_.size()); });
    import(Argv, [this](x86::Cpu& c) {
        const uint32_t n = c.arg(0);
        c.writeString(c.arg(1), n < args_.size() ? args_[n] : std::string(), c.arg(2));
        return 0u;
    });
    import(CmdExecuteText, [this](x86::Cpu& c) {
        // (when, text): run by the page between frames.
        for (const std::string& line : splitCommands(str(c, 1, 4096)))
            if (!line.empty())
                commands_.push_back(line);
        return 0u;
    });
    import(CmdTokenizeString, [this](x86::Cpu& c) {
        args_ = splitCommand(str(c, 0, 4096));
        return 0u;
    });

    // Files: read from the game's; writes kept only while open.
    import(FsFOpenFile, [this](x86::Cpu& c) -> uint32_t {
        // (path, &handle, mode: 0 read, 1 write, 2 append) -> length
        const uint32_t handleAt = c.arg(1);
        uint32_t handle = 0;
        const int32_t length = openFiles_.open(str(c, 0, 256), int32_t(c.arg(2)), handleAt != 0, true, handle);
        if (handleAt)
            c.write32(handleAt, handle);
        return uint32_t(length);
    });
    import(FsRead, [this](x86::Cpu& c) -> uint32_t {
        // (buffer, length, handle) -> bytes read
        if (!c.valid(c.arg(0), c.arg(1)))
            return 0;
        return uint32_t(openFiles_.read(c.arg(2), c.memory().data() + c.arg(0), c.arg(1)));
    });
    import(FsWrite, [this](x86::Cpu& c) -> uint32_t {
        if (!c.valid(c.arg(0), c.arg(1)))
            return 0;
        return uint32_t(openFiles_.write(c.arg(2), c.memory().data() + c.arg(0), c.arg(1)));
    });
    import(FsFCloseFile, [this](x86::Cpu& c) {
        openFiles_.close(c.arg(0));
        return 0u;
    });
    import(FsGetFileList, [this](x86::Cpu& c) -> uint32_t {
        // (folder, extension, buffer, size) -> count. "$modlist": no mods.
        const std::string folder = str(c, 0, 256);
        if (folder == "$modlist")
            return 0;
        return fileList(folder, str(c, 1, 64), c.arg(2), c.arg(3));
    });
    import(FsReadFile, [this](x86::Cpu& c) -> uint32_t {
        // (name, &buffer or 0) -> length, -1 if there's no such file
        std::vector<uint8_t> data;
        if (files_.read(str(c, 0, 256), data))
            return 0xffffffffu;
        if (c.arg(1)) {
            const uint32_t a = process_->memory().alloc(uint32_t(data.size()) + 1);
            if (a) {
                std::memcpy(c.memory().data() + a, data.data(), data.size());
                c.write8(a + uint32_t(data.size()), 0);
            }
            c.write32(c.arg(1), a);
        }
        return uint32_t(data.size());
    });
    import(FsFreeFile, [this](x86::Cpu& c) {
        if (c.arg(0))
            process_->memory().free(c.arg(0));
        return 0u;
    });

    // The renderer: what's registered and drawn is recorded.
    import(RegisterModel, [this](x86::Cpu& c) { return uint32_t(models_.add(str(c, 0, 256))); });
    import(RegisterSkin, [this](x86::Cpu& c) { return uint32_t(skins_.add(str(c, 0, 256))); });
    // 0 for a shader that can't be found, as the original's renderer.
    auto registerShader = [this](x86::Cpu& c) {
        const std::string name = str(c, 0, 256);
        return shaderExists && !shaderExists(name) ? 0u : uint32_t(shaders_.add(name));
    };
    import(RegisterShader, registerShader);
    import(RegisterShaderNoMip, registerShader);
    import(ClearScene, [this](x86::Cpu&) {
        scene_.clear();
        return 0u;
    });
    import(AddRefEntityToScene, [this](x86::Cpu& c) {
        if (c.valid(c.arg(0), SpGameHost::kRefEntitySize))
            scene_.addEntity(c.memory().data() + c.arg(0), SpGameHost::kRefEntitySize, 0);
        return 0u;
    });
    import(AddPolyToScene, [this](x86::Cpu& c) {
        const uint32_t bytes = std::min<uint32_t>(c.arg(1), 256) * 24;
        scene_.addPoly(int32_t(c.arg(0)), int32_t(c.arg(1)),
                       bytes && c.valid(c.arg(2), bytes) ? c.memory().data() + c.arg(2) : nullptr);
        return 0u;
    });
    import(AddLightToScene, [this](x86::Cpu& c) {
        scene_.addLight(readVec(c.arg(0)), argFloat(c, 1), argFloat(c, 2), argFloat(c, 3), argFloat(c, 4));
        return 0u;
    });
    import(RenderScene, [this](x86::Cpu& c) {
        scene_.render(c.valid(c.arg(0), kRefdefSize) ? c.memory().data() + c.arg(0) : nullptr, pictures_.size());
        return 0u;
    });
    import(SetColor, [this](x86::Cpu& c) {
        if (!c.arg(0))
            color_ = {1, 1, 1, 1};
        else
            for (int i = 0; i < 4; ++i) {
                const uint32_t bits = c.read32(c.arg(0) + 4 * uint32_t(i));
                std::memcpy(&color_[size_t(i)], &bits, 4);
            }
        return 0u;
    });
    import(DrawStretchPic, [this](x86::Cpu& c) {
        pictures_.push_back({argFloat(c, 0), argFloat(c, 1), argFloat(c, 2), argFloat(c, 3), argFloat(c, 4),
                             argFloat(c, 5), argFloat(c, 6), argFloat(c, 7), int32_t(c.arg(8)), color_});
        return 0u;
    });
    import(Empty, [](x86::Cpu&) { return 0u; });
    import(UpdateScreen, [](x86::Cpu&) { return 0u; });
    import(LerpTag, [this](x86::Cpu& c) -> uint32_t {
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
    import(StartLocalSound, [this](x86::Cpu& c) {
        if (startLocalSound)
            startLocalSound(sounds_.name(int32_t(c.arg(0))), int(c.arg(1)));
        return 0u;
    });
    import(RegisterSound, [this](x86::Cpu& c) { return uint32_t(sounds_.add(str(c, 0, 256))); });
    import(StartLocalLoopingSound, [](x86::Cpu&) { return 0u; });
    import(DrawStretchRaw, [this](x86::Cpu& c) {
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
            const uint8_t* p = c.memory().data() + c.arg(6);
            img.rgba.assign(p, p + bytes);
            raw_.push_back(std::move(img));
        }
        return 0u;
    });

    // Saved games, by name (no folder or extension), as the load and save
    // menus ask: a picture, a comment, whether one can be read.
    auto openSave = [this](x86::Cpu& c, int n, SaveReader& reader) {
        std::vector<uint8_t> file;
        if (!readSave || !readSave(str(c, n, 256), file))
            return false;
        reader = SaveReader(std::move(file));
        return true;
    };
    import(SavePicture, [this, openSave](x86::Cpu& c) -> uint32_t {
        // (name, buffer of 256 x 256 x 4)
        SaveReader r;
        std::vector<uint8_t> comment, shot;
        if (!openSave(c, 0, r) || r.next(chunkId("COMM"), comment) || r.next(chunkId("SHOT"), shot, kShotSize) ||
            !c.valid(c.arg(1), kShotSize))
            return 0;
        std::memcpy(c.memory().data() + c.arg(1), shot.data(), kShotSize);
        return 1;
    });
    import(SaveComment, [this, openSave](x86::Cpu& c) -> uint32_t {
        // (name) -> its comment (128 bytes), or 0
        SaveReader r;
        std::vector<uint8_t> comment;
        if (!openSave(c, 0, r) || r.next(chunkId("COMM"), comment, kCommentSize))
            return 0;
        std::memcpy(c.memory().data() + comment_, comment.data(), kCommentSize);
        return comment_;
    });
    import(SaveReadable, [this, openSave](x86::Cpu& c) -> uint32_t {
        SaveReader r;
        std::vector<uint8_t> data;
        return openSave(c, 0, r) && !r.next(chunkId("COMM"), data, kCommentSize) &&
                       !r.next(chunkId("SHOT"), data, kShotSize)
                   ? 1
                   : 0;
    });
    import(SetSaveComment, [this](x86::Cpu& c) {
        saveComment = str(c, 0, kCommentSize);
        return 0u;
    });
    import(ScreenShot, [](x86::Cpu&) {
        // The original's captures only in a game with no menu open, so
        // never from here; the page captures as the in-game menu opens.
        return 0u;
    });
    import(ScreenPixels, [this](x86::Cpu& c) -> uint32_t {
        // (&captured) -> the last capture, 256 x 256 x 4
        if (c.arg(0))
            c.write32(c.arg(0), captured_ ? 1 : 0);
        return capture_;
    });

    import(AllowedToSave, [this](x86::Cpu& c) -> uint32_t {
        // (flag): with no game, the flag itself (so SetActiveMenu, which
        // asks with 1 before opening any menu, goes ahead); with one, the
        // game's say (its GameAllowedToSaveHere), as the original's engine.
        const uint32_t flag = c.arg(0);
        return askGame ? askGame(flag) : flag;
    });

    // Keys.
    import(KeynumToStringBuf, [](x86::Cpu& c) {
        c.writeString(c.arg(1), KeyBindings::keyName(int32_t(c.arg(0))), c.arg(2));
        return 0u;
    });
    import(GetBindingBuf, [this](x86::Cpu& c) {
        const int32_t k = int32_t(c.arg(0));
        c.writeString(c.arg(1), bindings && k >= 0 && k < q3key::Count ? bindings->get(k) : std::string(), c.arg(2));
        return 0u;
    });
    import(SetBinding, [this](x86::Cpu& c) {
        const int32_t k = int32_t(c.arg(0));
        if (bindings && k >= 0 && k < q3key::Count)
            bindings->set(k, str(c, 1, 1024));
        return 0u;
    });
    import(IsDown, [this](x86::Cpu& c) -> uint32_t { return keysDown.count(int32_t(c.arg(0))) ? 1 : 0; });
    import(GetOverstrikeMode, [this](x86::Cpu&) -> uint32_t { return overstrike_ ? 1 : 0; });
    import(SetOverstrikeMode, [this](x86::Cpu& c) {
        overstrike_ = c.arg(0) != 0;
        return 0u;
    });
    import(ClearStates, [this](x86::Cpu&) {
        keysDown.clear();
        return 0u;
    });
    import(GetCatcher, [this](x86::Cpu&) { return uint32_t(catcher_); });
    import(SetCatcher, [this](x86::Cpu& c) {
        catcher_ = int32_t(c.arg(0));
        return 0u;
    });
    import(GetClipboardData, [](x86::Cpu& c) {
        c.writeString(c.arg(0), "", c.arg(1));
        return 0u;
    });

    import(GetGlconfig, [](x86::Cpu& c) {
        SpGameHost::writeGlconfigTo(c, c.arg(0));
        return 0u;
    });
    import(ConnectionState, [this](x86::Cpu&) { return uint32_t(connectionState); });
    import(GetConfigString, [this](x86::Cpu& c) -> uint32_t {
        std::string s;
        const bool have = configString && configString(int32_t(c.arg(0)), s);
        c.writeString(c.arg(1), have ? s : std::string(), c.arg(2));
        return have ? 1 : 0;
    });
    import(Milliseconds, [this](x86::Cpu&) { return uint32_t(time_); });
}

} // namespace reluva::idtech3::eliteforce
