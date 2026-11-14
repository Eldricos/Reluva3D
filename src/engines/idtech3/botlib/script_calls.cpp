// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/botlib/script_calls.h"

namespace reluva::idtech3::botlib {

namespace {

// pc_token_t's text, after type, subtype and the number as int and float.
constexpr int32_t kTokenText = 16;
constexpr int32_t kTokenTextSize = 1024;

} // namespace

int32_t ScriptCalls::readToken(Qvm &vm, int32_t handle, int32_t address) {
    const auto it = _sources.find(handle);
    if (it == _sources.end() || !vm.checkWrite(address, kTokenText + kTokenTextSize))
        return 0;
    BotToken t;
    if (!it->second->next(t))
        return 0;
    _lastToken[handle] = t;
    // pc_token_t's types: string 1, literal 2, number 3, name 4,
    // punctuation 5; a number's subtype says integer (0x1000, with
    // decimal 0x8) or float (0x800).
    int32_t type = 4;
    int32_t subtype = 0;
    switch (t.type) {
    case BotToken::Type::kString:
        type = 1;
        break;
    case BotToken::Type::kLiteral:
        type = 2;
        break;
    case BotToken::Type::kNumber:
        type = 3;
        subtype = t.isFloat ? 0x800 : 0x1008;
        break;
    case BotToken::Type::kName:
        type = 4;
        break;
    case BotToken::Type::kPunctuation:
        type = 5;
        break;
    }
    vm.writeInt(address, type);
    vm.writeInt(address + 4, subtype);
    vm.writeInt(address + 8, int32_t(t.number));
    vm.writeInt(address + 12, Qvm::fromFloat(float(t.number)));
    vm.writeString(address + kTokenText, t.text, kTokenTextSize);
    return 1;
}

int32_t ScriptCalls::call(Qvm &vm, Call which, const int32_t *a) {
    auto str = [&](int i, size_t max) { return vm.string(a[i], max); };
    switch (which) {
    case kAddGlobalDefine:
        _globalDefines.push_back(str(0, 1024));
        return 1;
    case kLoadGlobalDefines: {
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
    case kRemoveAllGlobalDefines:
        _globalDefines.clear();
        return 0;
    case kLoadSource: {
        auto loader = [this](const std::string &path, std::string &text) {
            std::vector<uint8_t> data;
            if (_files.read(path, data))
                return false;
            text.assign(data.begin(), data.end());
            return true;
        };
        auto script = std::make_unique<BotScript>(loader, _globalDefines);
        script->allowAtNames();
        if (!script->open(str(0, 256)))
            return 0;
        const int32_t handle = _nextSource++;
        _sources[handle] = std::move(script);
        return handle;
    }
    case kFreeSource:
        _sources.erase(a[0]);
        _lastToken.erase(a[0]);
        return 1;
    case kReadToken:
        return readToken(vm, a[0], a[1]);
    case kSourceFileAndLine: {
        const auto it = _lastToken.find(a[0]);
        if (it == _lastToken.end())
            return 0;
        vm.writeString(a[1], it->second.file, 128);
        if (vm.checkWrite(a[2], 4))
            vm.writeInt(a[2], it->second.line);
        return 1;
    }
    }
    return 0;
}

} // namespace reluva::idtech3::botlib
