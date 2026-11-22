// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace reluva::storm3d {

/** The length of the .ldl files' key, which repeats through each file. */
constexpr size_t kLdlKeySize = 400;

/**
 * Reads the key .ldl files are XORed with from the player's `Armada.exe`,
 * where it's part of the program's data. Only the English 1.0 executable's
 * place for it is known; another executable fails. See
 * docs/engines/storm3d/games/armada/formats/ldl.md.
 */
Error readLdlKey(const std::filesystem::path &exe, std::vector<uint8_t> &key);

/** Undoes an .ldl file's encoding: each byte XORed with the key's byte at its offset modulo 400. */
void decodeLdl(std::vector<uint8_t> &data, const std::vector<uint8_t> &key);

} // namespace reluva::storm3d
