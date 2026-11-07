// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/log.h"

#include <string>

namespace reluva::stgenerations {

// Debug output by topic, as STG-Engine's --debugflags gave it: a channel
// ("resources", "scripts", "sound") prints its messages up to the debug
// level (log::setDebugLevel) once it's turned on. The engine's developer
// settings turn them on.
void enableDebugChannel(const std::string& name);
bool isDebugChannelEnabled(const char* name);
void debugC(int level, const char* channel, const char* fmt, ...) RELUVA_PRINTF(3, 4);

} // namespace reluva::stgenerations
