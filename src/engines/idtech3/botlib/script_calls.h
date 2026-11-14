// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/vm/qvm.h"

#include "common/archive.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::idtech3::botlib {

/**
 * The script calls (PC_*) Team Arena's engines give their menus and client
 * games for reading menu scripts through the bot library's script reader:
 * sources by handle, tokens written as pc_token_t (type, subtype, the
 * number as int and float, then 1024 bytes of text), global defines added
 * to every source opened after. Names may start with '@' (Raven's string
 * references).
 */
class ScriptCalls {
public:
    // In the order both of Jedi Outcast's modules number them.
    enum Call {
        kAddGlobalDefine,       // (text) -> 1
        kLoadSource,            // (file) -> handle, 0 if it can't be read
        kFreeSource,            // (handle) -> 1
        kReadToken,             // (handle, pc_token_t*) -> 0 at the end
        kSourceFileAndLine,     // (handle, file buffer, &line) -> whether known
        kLoadGlobalDefines,     // (file of #define lines) -> whether read
        kRemoveAllGlobalDefines // ()
    };

    explicit ScriptCalls(const SearchSet &files) : _files(files) {}
    int32_t call(Qvm &vm, Call which, const int32_t *args);

private:
    int32_t readToken(Qvm &vm, int32_t handle, int32_t address);

    const SearchSet &_files;
    std::map<int32_t, std::unique_ptr<BotScript>> _sources;
    std::map<int32_t, BotToken> _lastToken; // each source's, for SourceFileAndLine
    int32_t _nextSource = 1;
    std::vector<std::string> _globalDefines;
};

} // namespace reluva::idtech3::botlib
