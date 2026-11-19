# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# reluva_emulation: running the original games' own code. x86/: a 32-bit
# x86 interpreter and JIT with stand-ins for Windows, DirectX, Aureal A3D
# and the Visual C++ runtimes (docs/common/x86/); x86/glide/: the Glide
# stand-in served to programs as glide2x.dll and glide3x.dll.
add_library(reluva_emulation STATIC
    x86/a3d.cpp
    x86/com.cpp
    x86/cpu.cpp
    x86/cpu_cache.cpp
    x86/disasm.cpp
    x86/format.cpp
    x86/fpu.cpp
    x86/jit.cpp
    x86/mmx.cpp
    x86/msvcp60.cpp
    x86/msvcrt.cpp
    x86/pokes.cpp
    x86/win32.cpp
    x86/win32_files.cpp
    x86/win32_registry.cpp
    x86/win32_system.cpp
    x86/win32_threads.cpp
    x86/win32_user.cpp
    x86/directx/d3d7.cpp
    x86/directx/ddraw.cpp
    x86/directx/dinput.cpp
    x86/directx/interfaces.cpp
    x86/directx/raster.cpp
    x86/directx/surface.cpp
    x86/glide/glide2_x86.cpp
    x86/glide/glide3_x86.cpp
    x86/glide/glide_x86.cpp)
# graphics: the Glide stand-in's pipeline (src/graphics/glide/), which x86/glide/ serves to programs.
target_link_libraries(reluva_emulation PUBLIC reluva_common reluva_audio reluva_graphics PRIVATE reluva_warnings)
