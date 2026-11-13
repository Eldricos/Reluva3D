# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# What every engine's game-files test script shares (tests/engines/<engine>/
# gamedata.cmake, run by ctest as `cmake -P` once per step, as registered by
# reluva_add_gamedata_test). The script includes this, then:
#
#   gamedata_begin("<what the folder holds>")   game_dir, or the step skipped
#   ... works out the release, then runs the step's commands:
#   record_run(<name> <summary> <engine> <command> <args>...)
#   gamedata_end("<release>" "<records folder>")  compares or writes the record
#
# Given by ctest (-D): TOOL (reluva-tool), STEP, ENGINE and GAME (the
# engine's and the game's ids, as the launcher stores them), GAME_ENV (the
# variable naming the game's folder), RECORDS (the committed records'
# folder) and OUTPUT (where each command's output is kept).
#
# The game's folder is GAME_ENV's; without it, the launcher's entry for the
# game in reluva.ini, if there's one.
#
# Environment:
#   RELUVA_GAMEDATA_UPDATE=1 write this run's records as the new ones
#   RELUVA_GAMEDATA_RECORDS  a folder of records of your own, instead of the
#                            committed ones
#   RELUVA_GAMEDATA_RECORDS_ROOT  the same for every engine at once: each
#                            engine's in a folder of it named after it
#   RELUVA_GAMEDATA_TARGET   the launcher's entry to use, by its name, where
#                            it has several for one game
#   RELUVA_CONFIG            the launcher's reluva.ini, if not where the
#                            launcher keeps it

cmake_minimum_required(VERSION 3.24)

# The step's record lines, kept in order.
set(record "")

# Sets game_dir from GAME_ENV, or skips the step (a macro, so its return()
# ends the script). `holds` says what the folder should be: "the one
# holding baseEF".
macro(gamedata_begin holds)
    set(game_dir "$ENV{${GAME_ENV}}")
    if(game_dir STREQUAL "")
        launcher_game_dir(game_dir skip_why)
        if(game_dir STREQUAL "")
            message("SKIPPED: set ${GAME_ENV} to the game's folder (${holds}) to run this step${skip_why}")
            return()
        endif()
    endif()
    if(NOT IS_DIRECTORY "${game_dir}")
        message(FATAL_ERROR "${GAME_ENV} is \"${game_dir}\", which isn't a folder")
    endif()
    file(REAL_PATH "${game_dir}" game_dir)
endmacro()

# Where the launcher keeps reluva.ini: SDL's preferences folder for
# "reluva" (src/base/main.cpp), unless RELUVA_CONFIG names another.
function(launcher_config var)
    if(NOT "$ENV{RELUVA_CONFIG}" STREQUAL "")
        set(${var} "$ENV{RELUVA_CONFIG}" PARENT_SCOPE)
    elseif(CMAKE_HOST_WIN32)
        set(${var} "$ENV{APPDATA}/reluva/reluva/reluva.ini" PARENT_SCOPE)
    elseif(CMAKE_HOST_APPLE)
        set(${var} "$ENV{HOME}/Library/Application Support/reluva/reluva/reluva.ini" PARENT_SCOPE)
    elseif(NOT "$ENV{XDG_DATA_HOME}" STREQUAL "")
        set(${var} "$ENV{XDG_DATA_HOME}/reluva/reluva/reluva.ini" PARENT_SCOPE)
    else()
        set(${var} "$ENV{HOME}/.local/share/reluva/reluva/reluva.ini" PARENT_SCOPE)
    endif()
endfunction()

# The folder of the launcher's entry for ENGINE's GAME, in <var>: the one
# entry there is, or RELUVA_GAMEDATA_TARGET's among several. "" if none can
# be picked, with why (a sentence to add to the skip message) in <why_var>.
function(launcher_game_dir var why_var)
    set(${var} "" PARENT_SCOPE)
    set(${why_var} ", or add the game in the launcher" PARENT_SCOPE)
    launcher_config(config)
    if(NOT EXISTS "${config}")
        return()
    endif()
    # [name], then its key=value lines; an entry is a section with these
    # three keys (src/gui/launcher.cpp, Launcher::addGame).
    file(STRINGS "${config}" lines ENCODING UTF-8)
    set(names "")
    set(section "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^\\[(.*)\\]$")
            set(section "${CMAKE_MATCH_1}")
            list(APPEND names "${section}")
        elseif(NOT section STREQUAL "" AND line MATCHES "^(engine|gameid|path)=(.*)$")
            set("entry_${CMAKE_MATCH_1}_${section}" "${CMAKE_MATCH_2}")
        endif()
    endforeach()
    set(found "")
    foreach(name IN LISTS names)
        if("${entry_engine_${name}}" STREQUAL "${ENGINE}" AND "${entry_gameid_${name}}" STREQUAL "${GAME}"
           AND NOT "${entry_path_${name}}" STREQUAL "")
            list(APPEND found "${name}")
        endif()
    endforeach()
    set(wanted "$ENV{RELUVA_GAMEDATA_TARGET}")
    if(NOT wanted STREQUAL "" AND wanted IN_LIST found)
        set(found "${wanted}")
    endif()
    list(LENGTH found count)
    if(count EQUAL 1)
        message("${GAME_ENV} isn't set: the launcher's ${found}, ${entry_path_${found}}")
        set(${var} "${entry_path_${found}}" PARENT_SCOPE)
    elseif(count GREATER 1)
        list(JOIN found ", " listed)
        set(${why_var} ", or pick one of the launcher's entries for it (${listed}) with RELUVA_GAMEDATA_TARGET"
            PARENT_SCOPE)
    endif()
endfunction()

# The folder in `dir` named `name` in any case, in <var> ("" if none).
function(find_folder var dir name)
    string(TOLOWER "${name}" want)
    set(found "")
    file(GLOB children LIST_DIRECTORIES true "${dir}/*")
    foreach(child IN LISTS children)
        get_filename_component(leaf "${child}" NAME)
        string(TOLOWER "${leaf}" lower)
        if(lower STREQUAL want AND IS_DIRECTORY "${child}")
            set(found "${child}")
        endif()
    endforeach()
    set(${var} "${found}" PARENT_SCOPE)
endfunction()

# Runs reluva-tool with the arguments given; fails the step if it fails.
# The output, with the game's folder, the build's output folder and timings
# taken out, in <var>.
function(run_tool var)
    execute_process(COMMAND "${TOOL}" ${ARGN}
        OUTPUT_VARIABLE out ERROR_VARIABLE out RESULT_VARIABLE rc TIMEOUT 1800)
    string(REPLACE "${game_dir}" "<game>" out "${out}")
    if(NOT "${OUTPUT}" STREQUAL "")
        string(REPLACE "${OUTPUT}" "<output>" out "${out}")
    endif()
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

# One run of the tool: <name> in the record, with a checksum of its output
# and the last line matching <summary> (a regular expression; "" for none).
# The output is kept in OUTPUT/<step>/<name>.txt, the previous run's beside
# it (.previous.txt).
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

# A file a command wrote (a video's decoded pictures, say): <name> in the
# record with its checksum.
function(record_file name path)
    file(SHA256 "${path}" sum)
    string(SUBSTRING "${sum}" 0 16 sum)
    set(record "${record}${name}: ${sum}\n" PARENT_SCOPE)
endfunction()

# The record, against the one kept in <records folder> (named after the
# release, one per release) under RECORDS; or, with RELUVA_GAMEDATA_UPDATE,
# written there. `release` is how messages name it.
macro(gamedata_end release folder)
    set(records "${RECORDS}")
    if(NOT "$ENV{RELUVA_GAMEDATA_RECORDS}" STREQUAL "")
        set(records "$ENV{RELUVA_GAMEDATA_RECORDS}")
    elseif(NOT "$ENV{RELUVA_GAMEDATA_RECORDS_ROOT}" STREQUAL "")
        set(records "$ENV{RELUVA_GAMEDATA_RECORDS_ROOT}/${ENGINE}")
    endif()
    set(record_file "${records}/${folder}/${STEP}.txt")
    if("$ENV{RELUVA_GAMEDATA_UPDATE}" STREQUAL "1")
        file(MAKE_DIRECTORY "${records}/${folder}")
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
endmacro()
