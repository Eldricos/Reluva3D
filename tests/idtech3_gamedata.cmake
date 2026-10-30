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
# Each step runs reluva-tool commands on the game, the way playing does:
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
# tests/data/idtech3/gamedata/<release>/<step>.txt. The step passes when
# its record is the same as the committed one, so any change in what the
# engine does with the real game is caught.
#
# Environment:
#   RELUVA_STVEF             the game's folder
#   RELUVA_GAMEDATA_UPDATE=1 write this run's records as the new ones
#   RELUVA_GAMEDATA_RECORDS  a folder of records of your own, instead of the
#                            committed ones
# Each command's full output is kept in the build, under gamedata/idtech3/
# <step>/, with the previous run's beside it (.previous.txt), to see what
# changed.

cmake_minimum_required(VERSION 3.24)

set(game_dir "$ENV{${GAME_ENV}}")
if(game_dir STREQUAL "")
    message("SKIPPED: set ${GAME_ENV} to the game's folder (the one holding baseEF) to run this step")
    return()
endif()
if(NOT IS_DIRECTORY "${game_dir}")
    message(FATAL_ERROR "${GAME_ENV} is \"${game_dir}\", which isn't a folder")
endif()
file(REAL_PATH "${game_dir}" game_dir)

# The base game's folder, in any case.
set(base "")
file(GLOB children LIST_DIRECTORIES true "${game_dir}/*")
foreach(child IN LISTS children)
    get_filename_component(leaf "${child}" NAME)
    string(TOLOWER "${leaf}" lower)
    if(lower STREQUAL "baseef" AND IS_DIRECTORY "${child}")
        set(base "${child}")
    endif()
endforeach()
if(base STREQUAL "")
    message(FATAL_ERROR "No baseEF folder in ${game_dir}")
endif()

# Runs reluva-tool with the arguments given; fails the step if it fails.
# The output, with the game's folder and timings taken out, in <var>.
function(run_tool var)
    execute_process(COMMAND "${TOOL}" ${ARGN}
        OUTPUT_VARIABLE out ERROR_VARIABLE out RESULT_VARIABLE rc TIMEOUT 1800)
    string(REPLACE "${game_dir}" "<game>" out "${out}")
    string(REGEX REPLACE " in [0-9]+(\\.[0-9]+)? s" "" out "${out}")
    if(NOT rc EQUAL 0)
        list(JOIN ARGN " " shown)
        string(REPLACE "${game_dir}" "<game>" shown "${shown}")
        string(LENGTH "${out}" length)
        if(length GREATER 4000)
            math(EXPR from "${length} - 4000")
            string(SUBSTRING "${out}" ${from} 4000 out)
        endif()
        message(FATAL_ERROR "reluva-tool ${shown} failed (${rc}):\n${out}")
    endif()
    set(${var} "${out}" PARENT_SCOPE)
endfunction()

# The step's record lines, kept in order.
set(record "")
# One run of the tool: <name> in the record, with a checksum of its output
# and the last line matching <summary> (a regular expression; "" for none).
function(record_run name summary)
    run_tool(out ${ARGN})
    set(dir "${OUTPUT}/${STEP}")
    file(MAKE_DIRECTORY "${dir}")
    if(EXISTS "${dir}/${name}.txt")
        file(RENAME "${dir}/${name}.txt" "${dir}/${name}.previous.txt")
    endif()
    file(WRITE "${dir}/${name}.txt" "${out}")
    string(SHA256 sum "${out}")
    string(SUBSTRING "${sum}" 0 16 sum)
    set(line "${name}: ${sum}")
    if(NOT summary STREQUAL "")
        # (A ";" would split the line: CMake's lists.)
        string(REPLACE ";" "," flat "${out}")
        string(REGEX MATCHALL "[^\n]*${summary}[^\n]*" found "${flat}")
        if(found)
            list(GET found -1 last)
            string(STRIP "${last}" last)
            string(APPEND line " | ${last}")
        endif()
    endif()
    set(record "${record}${line}\n" PARENT_SCOPE)
endfunction()

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
    record_run(bsp-check "maps: " bsp-check "${base}")
elseif(STEP STREQUAL "shaders")
    record_run(shader-check "" shader-check "${base}")
elseif(STEP STREQUAL "models")
    record_run(model-check "" model-check "${base}")
elseif(STEP STREQUAL "images")
    record_run(image-check "distinct missing images" image-check "${base}")
elseif(STEP STREQUAL "holomatch")
    record_run(hm_borg1-bots "start level" game-run "${base}" hm_borg1 -frames 100 -addbot "munro 3" -addbot "chell 2" -cgame)
    record_run(hm_voy1-walk "start level" game-run "${base}" hm_voy1 -frames 80 -walk forward -cgame)
    if(expansion)
        record_run(hm_raven-bots "start level" game-run "${base}" hm_raven -frames 80 -addbot "munro 3" -cgame)
    endif()
elseif(STEP STREQUAL "menus")
    # Single player's: past the first-run setup and the opening screen to
    # the main menu (the Expansion's has more on it).
    set(menu_keys -frames 40 -set ui_initialsetup 1 -key 5 ENTER -fixedclock)
    record_run(holomatch "" ui-run "${base}" -frames 30)
    if(NOT sp_ui STREQUAL "")
        record_run(single-player "" sp-menus "${base}" -dll "${sp_ui}" ${menu_keys} ${sp_skip})
    endif()
    if(NOT xp_ui STREQUAL "")
        record_run(expansion "" sp-menus "${base}" -dll "${xp_ui}" ${menu_keys})
    endif()
elseif(STEP STREQUAL "single_player")
    if(sp_dll STREQUAL "")
        message("SKIPPED: no single-player game code (efgamex86.dll) in ${game_dir}")
        return()
    endif()
    foreach(map borg1 voy1 stasis1)
        record_run(${map} "frames run" sp-run "${base}" ${map} -dll "${sp_dll}" -frames 100 -saveload -cgame -fixedclock
                   ${sp_skip})
    endforeach()
elseif(STEP STREQUAL "expansion")
    if(NOT expansion OR xp_dll STREQUAL "")
        message("SKIPPED: the game in ${game_dir} has no Expansion Pack")
        return()
    endif()
    foreach(map tour/deck01 _holodeck_garden _holodeck_proton)
        string(REPLACE "/" "-" name "${map}")
        record_run(${name} "frames run" sp-run "${base}" ${map} -dll "${xp_dll}" -frames 80 -saveload -fixedclock)
    endforeach()
elseif(STEP STREQUAL "cinematics")
    file(MAKE_DIRECTORY "${OUTPUT}/${STEP}")
    foreach(video intro eflogo)
        set(yuv "${OUTPUT}/${STEP}/${video}.yuv")
        set(pcm "${OUTPUT}/${STEP}/${video}.pcm")
        record_run(${video} "frames, " bink-decode "${base}" video/${video} -frames 60 -yuv "${yuv}" -pcm "${pcm}")
        file(SHA256 "${yuv}" yuv_sum)
        file(SHA256 "${pcm}" pcm_sum)
        string(SUBSTRING "${yuv_sum}" 0 16 yuv_sum)
        string(SUBSTRING "${pcm_sum}" 0 16 pcm_sum)
        string(APPEND record "${video}.pictures: ${yuv_sum}\n${video}.sound: ${pcm_sum}\n")
        file(REMOVE "${yuv}" "${pcm}")
    endforeach()
else()
    message(FATAL_ERROR "No step \"${STEP}\"")
endif()

# The record, against the one kept for this release.
set(records "${RECORDS}")
if(NOT "$ENV{RELUVA_GAMEDATA_RECORDS}" STREQUAL "")
    set(records "$ENV{RELUVA_GAMEDATA_RECORDS}")
endif()
set(record_file "${records}/${slug}/${STEP}.txt")
if("$ENV{RELUVA_GAMEDATA_UPDATE}" STREQUAL "1")
    file(MAKE_DIRECTORY "${records}/${slug}")
    file(WRITE "${record_file}" "${record}")
    message("Record written: ${record_file}\n${record}")
    return()
endif()
if(NOT EXISTS "${record_file}")
    message(FATAL_ERROR "No record for ${release}'s ${STEP} step (${record_file}). This run's:\n${record}\n"
                        "Check it, then keep it with RELUVA_GAMEDATA_UPDATE=1.")
endif()
file(READ "${record_file}" want)
if(want STREQUAL record)
    message("${release}, ${STEP}: as recorded\n${record}")
    return()
endif()
message(FATAL_ERROR "${release}, ${STEP}: different from the record (${record_file}).\n"
                    "Recorded:\n${want}\nThis run:\n${record}\n"
                    "Each command's output is in ${OUTPUT}/${STEP}/ with the previous run's beside it "
                    "(.previous.txt). If the change is meant, run again with RELUVA_GAMEDATA_UPDATE=1 and "
                    "review the records' diff.")
