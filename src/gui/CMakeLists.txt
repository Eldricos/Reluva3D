# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_gui: the launcher (Dear ImGui): the games list, adding games,
# settings, and each engine's pages.
add_library(reluva_gui STATIC launcher.cpp)
target_link_libraries(reluva_gui PUBLIC reluva_engines imgui PRIVATE reluva_warnings)
