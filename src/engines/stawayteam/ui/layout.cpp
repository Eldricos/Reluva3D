// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/layout.h"

#include <cstdlib>

namespace reluva::stawayteam {

const TextObject *menuDefinition(const TextObject &rules, const std::string &name) {
    const TextObject *custom = rules.object("Custom Rules");
    const TextObject *menus = custom ? custom->object("Menu") : nullptr;
    if (!menus)
        return nullptr;
    for (const TextField *item : menus->items()) {
        const std::string *menuName = item->object ? item->object->value("Menu Name") : nullptr;
        if (menuName && *menuName == name)
            return item->object->object("Menu Definition");
    }
    return nullptr;
}

void readPoint(const TextObject *object, const std::string &key, int &x, int &y) {
    x = y = 0;
    const std::string *p = object ? object->value(key) : nullptr;
    if (!p)
        return;
    const size_t comma = p->find(',');
    x = std::atoi(p->c_str());
    y = comma == std::string::npos ? 0 : std::atoi(p->c_str() + comma + 1);
}

std::string readText(const TextObject *object, const std::string &key) {
    const std::string *value = object ? object->value(key) : nullptr;
    return value ? *value : std::string();
}

} // namespace reluva::stawayteam
