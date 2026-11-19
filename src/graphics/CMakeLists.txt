# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_graphics: 2D drawing, palettes, fonts, cursors, and the 3D
# renderer interface (gfx3d.h) with the software one (soft3d/), GPU compute
# (gpu_compute.h), and the Glide stand-in's pipeline (glide/).
add_library(reluva_graphics STATIC
    cursor.cpp
    draw.cpp
    font.cpp
    gfx3d.cpp
    gpu_compute.cpp
    palette.cpp
    truetype.cpp
    glide/antialias.cpp
    glide/glide2.cpp
    glide/glide3.cpp
    glide/glide_utils.cpp
    glide/gpu_renderer.cpp
    glide/pipeline.cpp
    glide/pixel.cpp
    glide/raster_setup.cpp
    glide/software_renderer.cpp
    glide/textures.cpp)
target_link_libraries(reluva_graphics PUBLIC reluva_common PRIVATE reluva_warnings)
# stb_truetype (third_party/, see its README) as a system include.
target_include_directories(reluva_graphics SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb)
