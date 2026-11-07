# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Elite Force's engine test with the game's own files: one step each time
# ctest runs this script (cmake -P), as registered by reluva_add_gamedata_test
# in src/engines/idtech3/CMakeLists.txt. See docs/DEVELOPMENT.md (Engine
# tests).
#
# The game's folder (the one holding baseEF) comes from the environment
# variable named by GAME_ENV (RELUVA_STVEF). Without it the step is skipped.
#
# Each step runs reluva-tool's idtech3 commands on the game, the way playing
# does:
#   release        what the game is (1.0, 1.2, with the Expansion Pack)
#   maps           every map read (bsp-check)
#   shaders        every shader script read (shader-check)
#   models         every model and skin read (model-check)
#   images         the images every map's shaders name (image-check)
#   holomatch      Holomatch's own game code on its maps: server, bots,
#                  client game (game-run)
#   menus          Holomatch's menus and single player's (ui-run, sp-menus)
#   single_player  single player's game code on its maps, saved and loaded
#                  again (sp-run)
#   expansion      the Expansion Pack's single player: Virtual Voyager and a
#                  holodeck mission, saved and loaded (sp-run)
#   cinematics     the videos decoded (bink-decode)
# A command that fails fails the step. What each prints (paths and timings
# taken out) is checksummed, with a summary line, into a record:
# tests/engines/idtech3/data/gamedata/<release>/<step>.txt. The step passes when
# its record is the same as the committed one, so any change in what the
# engine does with the real game is caught.
#
# Environment: RELUVA_STVEF, the game's folder; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1 writes this
# run's records as the new ones, RELUVA_GAMEDATA_RECORDS names a folder of
# records of your own. Each command's full output is kept in the build,
# under gamedata/idtech3/<step>/, with the previous run's beside it
# (.previous.txt), to see what changed.

include("${HELPERS}")
gamedata_begin("the one holding baseEF")

find_folder(base "${game_dir}" baseEF)
if(base STREQUAL "")
    message(FATAL_ERROR "No baseEF folder in ${game_dir}")
endif()

# What the game is: the record's folder is named after it.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "\\(([^()]*)\\) \\([a-z][a-z], [a-z]+\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Elite Force isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
string(TOLOWER "${release}" slug)
string(REGEX REPLACE "[^a-z0-9.]+" "-" slug "${slug}")
string(REGEX REPLACE "^-|-$" "" slug "${slug}")
set(expansion OFF)
if(release MATCHES "Expansion")
    set(expansion ON)
endif()

# Single player's game code: 1.x's beside the game folder, the Expansion
# Pack's in expansion/ (an import keeps both) or beside it (its install).
set(sp_dll "")
set(xp_dll "")
foreach(name efgamex86.dll EFGAMEX86.DLL)
    if(EXISTS "${game_dir}/${name}")
        set(sp_dll "${game_dir}/${name}")
    endif()
    if(EXISTS "${game_dir}/expansion/${name}")
        set(xp_dll "${game_dir}/expansion/${name}")
    endif()
endforeach()
if(expansion AND xp_dll STREQUAL "")
    set(xp_dll "${sp_dll}")
endif()
# As the launcher pairs them: 1.x's game code without the Expansion Pack's
# pak (its setting off), the Expansion's with it. An install made with the
# Expansion has only its code, so single player there is the Expansion's.
set(sp_skip "")
set(sp_ui "")
set(xp_ui "")
foreach(name efuix86.dll EFUIX86.DLL)
    if(EXISTS "${game_dir}/${name}")
        set(sp_ui "${game_dir}/${name}")
    endif()
    if(EXISTS "${game_dir}/expansion/${name}")
        set(xp_ui "${game_dir}/expansion/${name}")
    endif()
endforeach()
if(expansion AND NOT xp_dll STREQUAL sp_dll)
    set(sp_skip -skippak pak3.pk3)
endif()
# Installs differ in single player's code as well: records of their own.
if(expansion AND xp_dll STREQUAL sp_dll)
    string(APPEND slug "-expansion-code-only")
elseif(sp_dll STREQUAL "")
    string(APPEND slug "-no-single-player")
endif()

if(STEP STREQUAL "release")
    string(STRIP "${detected}" detected)
    set(record "release: ${detected}\n")
elseif(STEP STREQUAL "maps")
    record_run(bsp-check "maps: " idtech3 bsp-check "${base}")
elseif(STEP STREQUAL "shaders")
    record_run(shader-check "" idtech3 shader-check "${base}")
elseif(STEP STREQUAL "models")
    record_run(model-check "" idtech3 model-check "${base}")
elseif(STEP STREQUAL "images")
    record_run(image-check "distinct missing images" idtech3 image-check "${base}")
elseif(STEP STREQUAL "holomatch")
    record_run(hm_borg1-bots "start level" idtech3 game-run "${base}" hm_borg1 -frames 100 -addbot "munro 3" -addbot "chell 2" -cgame)
    record_run(hm_voy1-walk "start level" idtech3 game-run "${base}" hm_voy1 -frames 80 -walk forward -cgame)
    if(expansion)
        record_run(hm_raven-bots "start level" idtech3 game-run "${base}" hm_raven -frames 80 -addbot "munro 3" -cgame)
    endif()
elseif(STEP STREQUAL "menus")
    # Single player's: past the first-run setup and the opening screen to
    # the main menu (the Expansion's has more on it).
    set(menu_keys -frames 40 -set ui_initialsetup 1 -key 5 ENTER -fixedclock)
    record_run(holomatch "" idtech3 ui-run "${base}" -frames 30)
    if(NOT sp_ui STREQUAL "")
        record_run(single-player "" idtech3 sp-menus "${base}" -dll "${sp_ui}" ${menu_keys} ${sp_skip})
    endif()
    if(NOT xp_ui STREQUAL "")
        record_run(expansion "" idtech3 sp-menus "${base}" -dll "${xp_ui}" ${menu_keys})
    endif()
elseif(STEP STREQUAL "single_player")
    if(sp_dll STREQUAL "")
        message("SKIPPED: no single-player game code (efgamex86.dll) in ${game_dir}")
        return()
    endif()
    foreach(map borg1 voy1 stasis1)
        record_run(${map} "frames run" idtech3 sp-run "${base}" ${map} -dll "${sp_dll}" -frames 100 -saveload -cgame -fixedclock
                   ${sp_skip})
    endforeach()
elseif(STEP STREQUAL "expansion")
    if(NOT expansion OR xp_dll STREQUAL "")
        message("SKIPPED: the game in ${game_dir} has no Expansion Pack")
        return()
    endif()
    foreach(map tour/deck01 _holodeck_garden _holodeck_proton)
        string(REPLACE "/" "-" name "${map}")
        record_run(${name} "frames run" idtech3 sp-run "${base}" ${map} -dll "${xp_dll}" -frames 80 -saveload -fixedclock)
    endforeach()
elseif(STEP STREQUAL "cinematics")
    file(MAKE_DIRECTORY "${OUTPUT}/${STEP}")
    foreach(video intro eflogo)
        set(yuv "${OUTPUT}/${STEP}/${video}.yuv")
        set(pcm "${OUTPUT}/${STEP}/${video}.pcm")
        record_run(${video} "frames, " idtech3 bink-decode "${base}" video/${video} -frames 60 -yuv "${yuv}" -pcm "${pcm}")
        record_file(${video}.pictures "${yuv}")
        record_file(${video}.sound "${pcm}")
        file(REMOVE "${yuv}" "${pcm}")
    endforeach()
else()
    message(FATAL_ERROR "No step \"${STEP}\"")
endif()

gamedata_end("${release}" "${slug}")
