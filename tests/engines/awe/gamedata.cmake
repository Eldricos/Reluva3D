# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The AWE engine's test with the game's own files: one step each time ctest
# runs this script (cmake -P), as registered by reluva_add_gamedata_test in
# src/engines/awe/CMakeLists.txt. See docs/DEVELOPMENT.md (Engine tests) and
# docs/engines/awe/games/motoe/TESTING.md.
#
# The game's folder (GAME motoe: Murder on the Orient Express installed,
# the one holding motoe.exe; GAME attwn: And Then There Were None's, with
# ac.exe) comes from the environment variable named by GAME_ENV
# (RELUVA_MOTOE, RELUVA_ATTWN). Without it the step is skipped.
#
# Each step runs reluva-tool's awe commands on the game:
#   release  what the game is (detect)
#   files    every Granny file, scene, text, movie and sound read, the
#            cameras and compressed curves compared with granny2.dll's
#            (files-check)
#   scripts  every script's commands counted, and the verbs Reluva doesn't
#            know (script-check)
#   route-search  a way from a new game to the credits (game-run -solve),
#            searched for the first time and kept in the build
#            (gamedata/<name>/route.txt), as it names the game's files; its
#            checksum is in the record. Orient Express's search takes up to
#            an hour or so, so it has a step of its own, with a longer time;
#            And Then There Were None's takes hours, over several runs of
#            the step, each going on from where the last stopped.
#   route    the whole game by that route, four ways: by the mouse with
#            sound, by the hand alone, saved and loaded after every action,
#            and wandering off it (game-check -quick).
#   endings  And Then There Were None's four codas: the route played back
#            as it is, and changed to let Lombard die in 9A (its five
#            minutes waited out before his rescue), Vera in 10A (another
#            of her room's hotspots clicked for the marble chunk's use),
#            and both (game-run -play), each to the credits, with the coda
#            its movie names.
#   import   the game imported from its two CDs (import, from the folder
#            RELUVA_MOTOE_CDS or RELUVA_ATTWN_CDS names), each file
#            compared with the installed game's.
# A command that fails fails the step. What each prints (paths and timings
# taken out) is checksummed, with a summary line, into a record:
# tests/engines/awe/data/gamedata/<release>/<step>.txt. The step passes when
# its record is the same as the committed one. Records so far: english-1.0
# (Orient Express) and attwn-english (made on Linux); make others with
# RELUVA_GAMEDATA_UPDATE=1, and check them (each game's TESTING.md).
#
# Environment: RELUVA_MOTOE or RELUVA_ATTWN, the game's folder;
# RELUVA_MOTOE_CDS or RELUVA_ATTWN_CDS, the folder of its CDs (the import step); and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
if(GAME STREQUAL "attwn")
    gamedata_begin("the one holding ac.exe")
else()
    gamedata_begin("the one holding motoe.exe")
endif()

# What the game is: the record's folder is named after it.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "awe/${GAME}: ([^\n]*)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "The game (${GAME}) isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
if(GAME STREQUAL "attwn" AND release MATCHES "\\(en, windows\\)$")
    set(slug "attwn-english")
elseif(release MATCHES "\\(en, windows\\)$")
    set(slug "english-1.0")
elseif(GAME STREQUAL "attwn")
    set(slug "attwn-unknown-release")
else()
    set(slug "unknown-release")
endif()

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
elseif(STEP STREQUAL "files")
    record_run(files "Everything read|failed" awe files-check "${game_dir}" -j 4)
elseif(STEP STREQUAL "scripts")
    record_run(scripts "commands of" awe script-check "${game_dir}")
elseif(STEP STREQUAL "route-search")
    set(route "${OUTPUT}/route.txt")
    if(NOT EXISTS "${route}")
        # (Not run_tool: a search can take longer than its half hour.) A
        # search the time ran out on keeps its way so far in route.txt.part
        # and goes on from it the next time (And Then There Were None's
        # takes hours).
        file(MAKE_DIRECTORY "${OUTPUT}")
        set(from "")
        if(EXISTS "${route}.part")
            set(from -from "${route}.part")
        endif()
        execute_process(COMMAND "${TOOL}" awe game-run "${game_dir}" -solve "${route}.part" ${from} -mouse -characters
            OUTPUT_VARIABLE out ERROR_VARIABLE out RESULT_VARIABLE rc TIMEOUT 7000)
        if(rc MATCHES "timeout")
            message(FATAL_ERROR "The search isn't done yet: its way so far is in ${route}.part; run the step again to go on from it")
        elseif(NOT rc EQUAL 0)
            message(FATAL_ERROR "The search for a route failed (${rc}):\n${out}")
        endif()
        file(RENAME "${route}.part" "${route}")
    endif()
    record_file(route.txt "${route}")
elseif(STEP STREQUAL "route")
    set(route "${OUTPUT}/route.txt")
    if(NOT EXISTS "${route}")
        message(FATAL_ERROR "No route: the route-search step makes it (ctest -R gamedata.route-search)")
    endif()
    record_run(route "all passed|FAILED" awe game-check "${game_dir}" "${route}" -quick)
elseif(STEP STREQUAL "endings")
    if(NOT GAME STREQUAL "attwn")
        message(FATAL_ERROR "Only And Then There Were None has endings to choose")
    endif()
    set(route "${OUTPUT}/route.txt")
    if(NOT EXISTS "${route}")
        message(FATAL_ERROR "No route: the route-search step makes it (ctest -R gamedata.route-search)")
    endif()
    file(READ "${route}" both)
    # Lombard's rescue in 9A (its listB: "timer >300" lets him die) and
    # Vera's in 10A (the act's other hotspots over her room's: the second
    # of the name, in front).
    set(rescue "click|easternpaths03.ac1|SPOT@557,444|0|\n")
    set(marble "use|verasroom01.ac1|unknown_model.gr2|0||marblechunk\n")
    string(FIND "${both}" "${rescue}" at1)
    string(FIND "${both}" "${marble}" at2)
    if(at1 EQUAL -1 OR at2 EQUAL -1)
        message(FATAL_ERROR "The route hasn't the rescues the endings change (${route})")
    endif()
    string(REPLACE "${rescue}" "wait|easternpaths03.ac1|301||\n${rescue}" lombardDies "${both}")
    string(REPLACE "${marble}" "click|verasroom01.ac1|SPOT@10,239|1|\n" veraDies "${both}")
    string(REPLACE "${marble}" "click|verasroom01.ac1|SPOT@10,239|1|\n" neither "${lombardDies}")
    file(WRITE "${OUTPUT}/ending-vera.txt" "${lombardDies}")
    file(WRITE "${OUTPUT}/ending-lombard.txt" "${veraDies}")
    file(WRITE "${OUTPUT}/ending-neither.txt" "${neither}")
    foreach(ending both lombard vera neither)
        set(file "${OUTPUT}/ending-${ending}.txt")
        if(ending STREQUAL "both")
            set(file "${route}")
        endif()
        run_tool(out awe game-run "${game_dir}" -play "${file}" -v)
        string(REGEX MATCH "\\(coda: [^)]*\\)" coda "${out}")
        string(REGEX MATCH "[0-9]+ of [0-9]+ actions[^\n]*" summary "${out}")
        string(REGEX REPLACE "; [0-9.]+ minutes of game time" "" summary "${summary}")
        set(record "${record}${ending} alive: ${coda} ${summary}\n")
    endforeach()
elseif(STEP STREQUAL "import")
    # From the CDs (RELUVA_MOTOE_CDS or RELUVA_ATTWN_CDS: a folder holding
    # both, each in a folder of its own), the disc with data1.hdr planned
    # and the other added, as the launcher's "Add other disc..." does.
    set(cds "$ENV{${GAME_ENV}_CDS}")
    if(cds STREQUAL "")
        message("SKIPPED: set ${GAME_ENV}_CDS to a folder holding the game's two CDs to run this step")
        return()
    endif()
    file(REAL_PATH "${cds}" cds)
    file(GLOB discs LIST_DIRECTORIES true "${cds}/*")
    set(first "")
    set(others "")
    foreach(disc IN LISTS discs)
        file(GLOB header "${disc}/[Dd][Aa][Tt][Aa]1.[Hh][Dd][Rr]")
        if(header AND first STREQUAL "")
            set(first "${disc}")
        elseif(IS_DIRECTORY "${disc}")
            list(APPEND others -patch "${disc}")
        endif()
    endforeach()
    if(first STREQUAL "")
        message(FATAL_ERROR "No CD with data1.hdr in ${cds}")
    endif()
    set(into "${OUTPUT}/import")
    file(REMOVE_RECURSE "${into}")
    run_tool(out import "${first}" "${into}" ${others})
    string(REPLACE "${cds}" "<cds>" out "${out}")
    string(REGEX MATCH "[0-9]+ files, [0-9.]+ MB" size "${out}")
    string(SHA256 sum "${out}")
    string(SUBSTRING "${sum}" 0 16 sum)
    set(record "${record}import: ${sum} | ${size}\n")
    # Each file as the game's own setup installed it, its name matched
    # ignoring case (a copy of the install may differ, as Windows doesn't
    # mind).
    file(GLOB_RECURSE installed RELATIVE "${game_dir}" "${game_dir}/*")
    foreach(name IN LISTS installed)
        string(TOLOWER "${name}" lower)
        set("installed:${lower}" "${name}")
    endforeach()
    file(GLOB_RECURSE imported RELATIVE "${into}" "${into}/*")
    list(LENGTH imported count)
    set(same 0)
    foreach(name IN LISTS imported)
        string(TOLOWER "${name}" lower)
        set(key "installed:${lower}")
        if(DEFINED "${key}")
            file(SHA256 "${into}/${name}" a)
            file(SHA256 "${game_dir}/${${key}}" b)
            if(a STREQUAL b)
                math(EXPR same "${same} + 1")
            else()
                set(record "${record}different from the install: ${name}\n")
            endif()
        else()
            set(record "${record}not in the install: ${name}\n")
        endif()
    endforeach()
    set(record "${record}the same as the install: ${same} of ${count} files\n")
    file(REMOVE_RECURSE "${into}")
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
