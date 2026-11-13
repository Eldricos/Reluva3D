// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/viewer.h"

#include <string_view>
#include <vector>

namespace reluva {

/** Every engine viewer built (reluva_viewers, which reluva doesn't link). */
const std::vector<const MetaViewer *> &allViewers();

/** The viewer for the engine `engineId`, or nullptr if it has none. */
const MetaViewer *findViewer(std::string_view engineId);

} // namespace reluva
