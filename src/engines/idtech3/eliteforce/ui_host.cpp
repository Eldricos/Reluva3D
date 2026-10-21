// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/eliteforce/ui_host.h"

#include <cstring>
#include <string>

namespace reluva::idtech3::eliteforce {

namespace {

// Elite Force ui.qvm system calls. The evidence for each number is in
// docs/formats/idtech3/eliteforce-syscalls.md.
enum UiCall : int {
    Error_ = 0,
    Print = 1,
    Milliseconds = 2,
    CvarSet = 3,
    CvarVariableValue = 4,
    CvarVariableStringBuffer = 5,
    CvarSetValue = 6,
    CvarReset = 7,
    CvarCreate = 8,
    CvarInfoStringBuffer = 9,
    Argc = 10,
    Argv = 11,
    CmdExecuteText = 12,
    FsFOpenFile = 13,
    FsRead = 14,
    FsWrite = 15,
    FsFCloseFile = 16,
    FsGetFileList = 17,
    RRegisterModel = 18,
    RRegisterSkin = 19,
    RRegisterShaderNoMip = 20,
    RClearScene = 21,
    RAddRefEntityToScene = 22,
    RAddPolyToScene = 23,
    RAddLightToScene = 24,
    RRenderScene = 25,
    RSetColor = 26,
    RDrawStretchPic = 27,
    UpdateScreen = 28,
    SRegisterSound = 31,
    SStartLocalSound = 32,
    KeyKeynumToStringBuf = 33,
    KeyGetBindingBuf = 34,
    KeySetBinding = 35,
    KeyIsDown = 36,
    KeyGetOverstrikeMode = 37,
    KeySetOverstrikeMode = 38,
    KeyClearStates = 39,
    KeyGetCatcher = 40,
    KeySetCatcher = 41,
    GetClipboardData = 42,
    GetGlconfig = 43,
    GetClientState = 44,
    GetConfigString = 45,
    CvarRegister = 54,
    CvarUpdate = 55,
    MemoryRemaining = 56,
    GetCdKey = 57,
    SetCdKey = 58,
};

// glconfig_t as Quake III lays it out: four strings (renderer, vendor,
// version, extensions), then 17 words. Elite Force's layout is assumed to
// match until checked (see the syscall notes).
constexpr int32_t kGlconfigStrings = 1024 * 3 + 8192;
constexpr int32_t kGlconfigSize = kGlconfigStrings + 17 * 4;
// uiClientState_t: three words, then three 1024-byte strings.
constexpr int32_t kClientStateSize = 12 + 3 * 1024;

} // namespace

UiHost::UiHost(const SearchSet& files, CvarStore& cvars) : files_(files), cvars_(cvars), vmFiles_(files) {
    vm_.setSystemCall([this](Qvm& vm, int number, const int32_t* args) { return systemCall(vm, number, args); });
}

Error UiHost::load() {
    std::vector<uint8_t> data;
    if (Error err = files_.read("vm/ui.qvm", data))
        return Error::fail("vm/ui.qvm: " + err.message);
    if (Error err = vm_.load(data))
        return Error::fail("vm/ui.qvm: " + err.message);
    int32_t version = 0;
    if (Error err = call(GetApiVersion, {}, version))
        return err;
    if (version != kApiVersion)
        return Error::fail("vm/ui.qvm reports API version " + std::to_string(version) + ", not " +
                           std::to_string(kApiVersion));
    return Error::success();
}

Error UiHost::call(int32_t command, const std::vector<int32_t>& args, int32_t& result) {
    return vm_.call(command, args, result);
}

void UiHost::writeGlconfig(int32_t address) {
    if (!vm_.valid(address, kGlconfigSize)) {
        vm_.abort("GetGlconfig: the structure isn't inside the program's memory");
        return;
    }
    std::memset(&vm_.memory()[size_t(address)], 0, kGlconfigSize);
    vm_.writeString(address, "Reluva", 1024);
    vm_.writeString(address + 1024, "Reluva", 1024);
    vm_.writeString(address + 2048, "1.0", 1024);
    const int32_t words[17] = {
        2048,   // maxTextureSize
        2,      // numTextureUnits
        32, 24, 8, // colour, depth, stencil bits
        0, 0,   // driverType (ICD), hardwareType (generic)
        0,      // deviceSupportsGamma
        0, 1,   // textureCompression, textureEnvAddAvailable
        width_, height_,
        Qvm::fromFloat(float(width_) / float(height_)),
        60,     // displayFrequency
        0, 0, 0, // isFullscreen, stereoEnabled, smpActive
    };
    for (int i = 0; i < 17; ++i)
        vm_.writeInt(address + kGlconfigStrings + 4 * i, words[i]);
}

void UiHost::writeClientState(int32_t address) {
    if (!vm_.valid(address, kClientStateSize)) {
        vm_.abort("GetClientState: the structure isn't inside the program's memory");
        return;
    }
    std::memset(&vm_.memory()[size_t(address)], 0, kClientStateSize);
    vm_.writeInt(address, 1); // connState: disconnected
}

int32_t UiHost::systemCall(Qvm& vm, int number, const int32_t* a) {
    if (trace)
        trace(number, a);
    auto str = [&](int i, size_t max = 1024) { return vm.string(a[i], max); };
    auto f = [&](int i) { return Qvm::asFloat(a[i]); };
    int32_t result = 0;
    if (commonSystemCall(vm, number, a, result))
        return result;

    switch (number) {
    case Error_: vm.abort("ui.qvm: " + str(0)); return 0;
    case Print: {
        const std::string text = str(0, 4096);
        if (print)
            print(text);
        else
            log_ += text;
        return 0;
    }
    case Milliseconds: return time_;
    case CvarSet: cvars_.set(str(0, 256), str(1, 256)); return 0;
    case CvarVariableValue: return Qvm::fromFloat(cvars_.value(str(0, 256)));
    case CvarVariableStringBuffer: vm.writeString(a[1], cvars_.string(str(0, 256)), a[2]); return 0;
    case CvarSetValue: {
        // Quake III writes whole numbers without a decimal point.
        const float v = f(1);
        const std::string text = v == float(int32_t(v)) ? std::to_string(int32_t(v)) : std::to_string(v);
        cvars_.set(str(0, 256), text);
        return 0;
    }
    case CvarReset:
        if (const CvarStore::Cvar* cv = cvars_.find(str(0, 256)))
            cvars_.set(cv->name, cv->defaultValue);
        return 0;
    case CvarCreate: cvars_.create(str(0, 256), str(1, 256), a[2]); return 0;
    case CvarInfoStringBuffer: {
        std::string info;
        for (const CvarStore::Cvar& cv : cvars_.all())
            if (cv.flags & a[0])
                info += "\\" + cv.name + "\\" + cv.value;
        vm.writeString(a[1], info, a[2]);
        return 0;
    }
    case Argc: return 0;
    case Argv: vm.writeString(a[1], "", a[2]); return 0;
    case CmdExecuteText: commands_.push_back(str(1, 4096)); return 0;
    case FsFOpenFile: return vmFiles_.open(vm, a[0], a[1], a[2]);
    case FsRead: return vmFiles_.read(vm, a[0], a[1], a[2]);
    case FsFCloseFile: vmFiles_.close(a[0]); return 0;
    case FsGetFileList: return vmFiles_.list(vm, a[0], a[1], a[2], a[3]);
    case RRegisterModel: return models_.add(str(0, 256));
    case RRegisterSkin: return skins_.add(str(0, 256));
    case RRegisterShaderNoMip: return shaders_.add(str(0, 256));
    case RClearScene:
    case RAddRefEntityToScene:
    case RAddPolyToScene:
    case RAddLightToScene:
    case RRenderScene:
    case UpdateScreen:
        return 0; // 3D scenes in the menus: later
    case RSetColor:
        if (!a[0])
            color_ = {1, 1, 1, 1};
        else if (!vm.valid(a[0], 16))
            vm.abort("R_SetColor: colour outside the program's memory");
        else
            for (int i = 0; i < 4; ++i)
                color_[size_t(i)] = Qvm::asFloat(vm.readInt(a[0] + 4 * i));
        return 0;
    case RDrawStretchPic:
        pictures_.push_back({f(0), f(1), f(2), f(3), f(4), f(5), f(6), f(7), a[8], color_});
        return 0;
    case SRegisterSound: return sounds_.add(str(0, 256));
    case SStartLocalSound: return 0; // sound: later
    case KeyKeynumToStringBuf: {
        std::string name = a[0] > 32 && a[0] < 127 ? std::string(1, char(a[0])) : "#" + std::to_string(a[0]);
        vm.writeString(a[1], name, a[2]);
        return 0;
    }
    case KeyGetBindingBuf: vm.writeString(a[1], "", a[2]); return 0;
    case KeySetBinding: return 0;
    case KeyIsDown: return 0;
    case KeyGetOverstrikeMode: return overstrike_;
    case KeySetOverstrikeMode: overstrike_ = a[0] != 0; return 0;
    case KeyClearStates: return 0;
    case KeyGetCatcher: return catcher_;
    case KeySetCatcher: catcher_ = a[0]; return 0;
    case GetClipboardData: vm.writeString(a[0], "", a[1]); return 0;
    case GetGlconfig: writeGlconfig(a[0]); return 0;
    case GetClientState: writeClientState(a[0]); return 0;
    case GetConfigString: vm.writeString(a[1], "", a[2]); return 0;
    case CvarRegister: {
        const CvarStore::Cvar& cv = cvars_.create(str(1, 256), str(2, 256), a[3]);
        if (a[0])
            cvars_.writeVmCvar(vm, a[0], cv.handle);
        return 0;
    }
    case CvarUpdate: cvars_.updateVmCvar(vm, a[0]); return 0;
    case MemoryRemaining: return 8 << 20;
    case GetCdKey: vm.writeString(a[0], "", a[1]); return 0;
    case SetCdKey: return 0;
    default: ++unhandled_[number]; return 0;
    }
}

} // namespace reluva::idtech3::eliteforce
