# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva: the program. Starts the SDL3 backend and the launcher, and runs
# the game picked.
add_executable(reluva main.cpp)
target_link_libraries(reluva PRIVATE reluva_gui reluva_engines reluva_backend_sdl3 reluva_backend_sockets imgui
                                     SDL3::SDL3 reluva_warnings)
