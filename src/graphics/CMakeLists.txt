# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_graphics: 2D drawing, palettes, fonts, cursors, and the 3D
# renderer interface (gfx3d.h) with the software one (soft3d/).
add_library(reluva_graphics STATIC
    cursor.cpp
    draw.cpp
    font.cpp
    gfx3d.cpp
    palette.cpp
    truetype.cpp)
target_link_libraries(reluva_graphics PUBLIC reluva_common PRIVATE reluva_warnings)
# stb_truetype (third_party/, see its README) as a system include.
target_include_directories(reluva_graphics SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb)
