// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>

namespace reluva::idtech3::jk2 {

// Multiplayer 1.03 and 1.04 number keys their own way, not Quake III's:
// jk2mp.exe 1.04's key names (0x4f6bbc, 16 bytes a key, by its number)
// put ENTER at 10, the arrows at 170 to 173, MOUSE1 at 141. Reluva keeps
// Quake III's numbers and changes them where they meet those menus.
// Characters (32 to 126) and typed text (1024 and up) keep theirs.

/** A Quake III key's number in 1.03 and 1.04 (0 where they have none). */
int32_t toPatchedKey(int32_t key);
/** And back (0 where Quake III has none). */
int32_t fromPatchedKey(int32_t key);

} // namespace reluva::idtech3::jk2
