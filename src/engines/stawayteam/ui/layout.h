// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stawayteam/formats/text_object.h"

#include <string>

namespace reluva::stawayteam {

/** A menu's definition in Rules.txt (`Custom Rules`, `Menu`, by its `Menu Name`); nullptr if there's none. */
const TextObject *menuDefinition(const TextObject &rules, const std::string &name);

/** A point as Rules.txt writes it ("155,109") into x and y; (0, 0) if `object` (may be nullptr) hasn't got it. */
void readPoint(const TextObject *object, const std::string &key, int &x, int &y);

/** A field's value, empty if `object` (may be nullptr) hasn't got it. */
std::string readText(const TextObject *object, const std::string &key);

} // namespace reluva::stawayteam
