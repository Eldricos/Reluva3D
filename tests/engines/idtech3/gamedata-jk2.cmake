# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Jedi Outcast's engine test with the game's own files, as gamedata.cmake is
# Elite Force's: one step each time ctest runs this script, as registered by
# reluva_add_gamedata_test in src/engines/idtech3/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests).
#
# The game's folder (the one holding base: an install's GameData, the CD's
# GAMEDATA/GameData, or an import) comes from RELUVA_JK2. Without it the
# step is skipped.
#
# The steps are named for the game (jk2-...), so their outputs in the build
# don't meet Elite Force's:
#   jk2-release   what the game is
#   jk2-maps      every map read (bsp-check)
#   jk2-shaders   every shader script read (shader-check)
#   jk2-models    every MD3 model and skin read (model-check)
#   jk2-ghoul2    every Ghoul2 mesh and skeleton read, each mesh against
#                 its skeleton (ghoul2-check)
#   jk2-images    the images every map's shaders name (image-check)
#   jk2-videos    two RoQ videos decoded, their pictures and sound recorded
#                 (roq-decode)
# Records: tests/engines/idtech3/data/gamedata/jk2-<release>/<step>.txt.

include("${HELPERS}")
gamedata_begin("the one holding base")

find_folder(base "${game_dir}" base)
if(base STREQUAL "")
    message(FATAL_ERROR "No base folder in ${game_dir}")
endif()

run_tool(detected detect "${game_dir}")
string(REGEX MATCH "idtech3/jk2: [^\n]*\\(([^()]*)\\) \\([a-z][a-z], [a-z]+\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Jedi Outcast isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
string(TOLOWER "jk2-${release}" slug)
string(REGEX REPLACE "[^a-z0-9.]+" "-" slug "${slug}")
string(REGEX REPLACE "^-|-$" "" slug "${slug}")

if(STEP STREQUAL "jk2-release")
    string(STRIP "${detected}" detected)
    set(record "release: ${detected}\n")
elseif(STEP STREQUAL "jk2-maps")
    record_run(bsp-check "maps: " idtech3 bsp-check "${base}")
elseif(STEP STREQUAL "jk2-shaders")
    record_run(shader-check "" idtech3 shader-check "${base}")
elseif(STEP STREQUAL "jk2-models")
    record_run(model-check "" idtech3 model-check "${base}")
elseif(STEP STREQUAL "jk2-ghoul2")
    record_run(ghoul2-check "meshes, " idtech3 ghoul2-check "${base}")
elseif(STEP STREQUAL "jk2-images")
    record_run(image-check "distinct missing images" idtech3 image-check "${base}")
elseif(STEP STREQUAL "jk2-videos")
    file(MAKE_DIRECTORY "${OUTPUT}/${STEP}")
    foreach(video openinglogos jk0101)
        set(yuv "${OUTPUT}/${STEP}/${video}.yuv")
        set(pcm "${OUTPUT}/${STEP}/${video}.pcm")
        record_run(${video} "frames, " idtech3 roq-decode "${base}" video/${video} -frames 60 -yuv "${yuv}" -pcm "${pcm}")
        record_file(${video}.pictures "${yuv}")
        record_file(${video}.sound "${pcm}")
        file(REMOVE "${yuv}" "${pcm}")
    endforeach()
else()
    message(FATAL_ERROR "No step \"${STEP}\"")
endif()

gamedata_end("${release}" "${slug}")
