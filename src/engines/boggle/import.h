// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/detection.h"

namespace reluva::boggle {
// Current Classic dependencies plus the release's identification files.
// Identification is the caller's responsibility; this builds a copy plan only.
Error classicImportPlan(const std::filesystem::path& root, const DetectedGame& game,
                        const std::vector<FileSignature>& identity, ImportPlan& out);
} // namespace reluva::boggle
