// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/viewer_registry.h"

namespace reluva {

// Each engine with a viewer exposes one accessor. The list of those built
// is generated from engines.cfg (cmake/ReluvaEngines.cmake).
#define RELUVA_VIEWER(accessor) const MetaViewer &accessor();
#include "engines/viewer_list.inc"
#undef RELUVA_VIEWER

const std::vector<const MetaViewer *> &allViewers() {
    static const std::vector<const MetaViewer *> kViewers = {
#define RELUVA_VIEWER(accessor) &accessor(),
#include "engines/viewer_list.inc"
#undef RELUVA_VIEWER
    };
    return kViewers;
}

const MetaViewer *findViewer(std::string_view engineId) {
    for (const MetaViewer *v : allViewers())
        if (engineId == v->engineId())
            return v;
    return nullptr;
}

} // namespace reluva
