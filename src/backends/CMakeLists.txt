# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The backends: the System interface (common/system.h) made real for a
# platform. Only the programs link them; engines never do.

# networking/sockets: UDP over the system's sockets (common/net.h).
add_library(reluva_backend_sockets STATIC networking/sockets/socket_net.cpp)
target_link_libraries(reluva_backend_sockets PUBLIC reluva_common PRIVATE reluva_warnings)
if(WIN32)
    target_link_libraries(reluva_backend_sockets PUBLIC ws2_32 iphlpapi)
endif()

# platform/sdl3 and graphics/sdl3: a window, input and sound through SDL3,
# and the 3D renderer on SDL's GPU API. The SDL system finds network
# addresses through the sockets backend.
add_library(reluva_backend_sdl3 STATIC
    platform/sdl3/sdl_system.cpp
    graphics/sdl3/sdl_gpu.cpp)
target_link_libraries(reluva_backend_sdl3 PUBLIC reluva_common reluva_graphics reluva_backend_sockets SDL3::SDL3
                                          PRIVATE reluva_warnings)
