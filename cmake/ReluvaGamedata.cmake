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
# Given by ctest (-D): TOOL (reluva-tool), STEP, GAME_ENV (the variable
# naming the game's folder), RECORDS (the committed records' folder) and
# OUTPUT (where each command's output is kept).
#
# Environment:
#   RELUVA_GAMEDATA_UPDATE=1 write this run's records as the new ones
#   RELUVA_GAMEDATA_RECORDS  a folder of records of your own, instead of the
#                            committed ones

cmake_minimum_required(VERSION 3.24)

# The step's record lines, kept in order.
set(record "")

# Sets game_dir from GAME_ENV, or skips the step (a macro, so its return()
# ends the script). `holds` says what the folder should be: "the one
# holding baseEF".
macro(gamedata_begin holds)
    set(game_dir "$ENV{${GAME_ENV}}")
    if(game_dir STREQUAL "")
        message("SKIPPED: set ${GAME_ENV} to the game's folder (${holds}) to run this step")
        return()
    endif()
    if(NOT IS_DIRECTORY "${game_dir}")
        message(FATAL_ERROR "${GAME_ENV} is \"${game_dir}\", which isn't a folder")
    endif()
    file(REAL_PATH "${game_dir}" game_dir)
endmacro()

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
