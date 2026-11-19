// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Set for every engine by the top CMakeLists.txt (reluva_shared), from the
// RELUVA_DEVELOPER_KEYS option.
#ifndef RELUVA_DEVELOPER_KEYS
#error "RELUVA_DEVELOPER_KEYS isn't set: link reluva_shared"
#endif

namespace reluva {

/**
 * Whether the games' debug keys and the launcher's "Developer:" settings
 * are built in: on for working on the engines, off in release builds for
 * players (cmake -DRELUVA_DEVELOPER_KEYS=OFF).
 *
 * A constant rather than #if, so the code behind it is compiled, and
 * checked, either way.
 */
inline constexpr bool kDeveloperKeys = RELUVA_DEVELOPER_KEYS != 0;

} // namespace reluva
