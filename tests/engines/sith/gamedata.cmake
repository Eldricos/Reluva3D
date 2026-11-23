# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Jedi Knight's engine test with the game's own files: one step each time
# ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/sith/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and
# docs/engines/sith/games/jk/TESTING.md.
#
# The game's install comes from RELUVA_JK, with copies of both CDs where
# the engine finds them (in the install as cd1 and cd2, or beside it).
# Without the variable or a launcher entry the step is skipped.
#
# Each step runs reluva-tool on the game:
#   release  what the game is (detect), and the CDs and their music found
#            (sith folders)
#   formats  every file of the kinds Reluva reads in the archives decoded,
#            each kind as a checksum (sith formats -hash)
#   movies   every cut scene's frames, palettes, sound and subtitle numbers
#            decoded, each as a checksum (sith movies -all)
#   start    the retail game run in the emulator from a new state until it
#            waits at New Player: the files it opens, its registry writes
#            and its debug output (sith host -files)
#   level    the same with a player made and New Game clicked, cut scenes
#            skipped, into Nar Shaddaa and through it by the cheat that ends
#            a level: the cut scenes and levels reached, and every file
#            opened, up to the second level (sith host -skim)
# The game runs on the machine's clock, so the host steps record only what
# a run decides itself, cut off at a point it reaches on any machine fast
# enough to play the game: the clicks come at set seconds.
#
# A command that fails fails the step. The records, in
# tests/engines/sith/data/gamedata/<release>/<step>.txt, hold only counts,
# names of the game's files and checksums. Records so far: us-1.0 (the US
# retail CDs, made on Linux).
#
# Environment: RELUVA_JK, the install; and those of the shared helpers
# (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the game's install, its CDs in it or beside it")

get_filename_component(parent "${game_dir}" DIRECTORY)

# What the game is: the record's folder is named after the release.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "sith/jk: Jedi Knight: Dark Forces II \\(([^)]*)\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Jedi Knight isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
string(TOLOWER "${release}" slug)
string(REGEX REPLACE "[ ,]+" "-" slug "${slug}")

# The game run in the emulator from a new state, for `seconds`, with the
# tool's other arguments: the lines a run decides itself, up to the first
# file opened matching `until` ("" for all of them), kept as <name>.
function(record_host name seconds until)
    set(out "${OUTPUT}/${STEP}")
    file(REMOVE_RECURSE "${out}/write")
    file(MAKE_DIRECTORY "${out}/write")
    run_tool(log sith host "${game_dir}" -seconds ${seconds} -files -write "${out}/write" ${ARGN})
    file(REMOVE_RECURSE "${out}/write")
    if(EXISTS "${out}/${name}.txt")
        file(RENAME "${out}/${name}.txt" "${out}/${name}.previous.txt")
    endif()
    file(WRITE "${out}/${name}.txt" "${log}")
    # Not the seconds a level or cut scene came at, nor how far the
    # machine's speed took the run past `until`.
    string(REPLACE ";" "," flat "${log}")
    string(REGEX REPLACE "\n[0-9]+\\.[0-9] s: " "\n" flat "\n${flat}")
    string(REGEX MATCHALL "(debug output|registry written|cut scene|level|opened)[^\n]*" lines "${flat}")
    set(kept "")
    set(steps "")
    set(files 0)
    foreach(line IN LISTS lines)
        if(line MATCHES "^(cut scene|level) ")
            list(APPEND steps "${line}")
        elseif(line MATCHES "^opened: ")
            math(EXPR files "${files} + 1")
        endif()
        string(APPEND kept "${line}\n")
        if(NOT until STREQUAL "" AND line MATCHES "^opened: .*${until}$")
            break()
        endif()
    endforeach()
    string(SHA256 sum "${kept}")
    string(SUBSTRING "${sum}" 0 16 sum)
    set(record "${record}${name}: ${sum} | ${files} files opened\n")
    foreach(step IN LISTS steps)
        if(until STREQUAL "" OR NOT step MATCHES "${until}$")
            set(record "${record}${name}.reached: ${step}\n")
        endif()
    endforeach()
    set(record "${record}" PARENT_SCOPE)
endfunction()

# A new player, "kyle", made at New Player, Single Player and New Game
# clicked: at these seconds the game is waiting for each.
set(new_game "4:esc,12:k,12.3:y,12.6:l,12.9:e,14:click517x449,18:click318x187,22:click294x109")

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
    run_tool(folders sith folders "${game_dir}")
    string(REGEX MATCHALL "(CD [12]: [^\n]*\\(([^)]*)\\)|music: [^\n]*)" lines "${folders}")
    foreach(line IN LISTS lines)
        # Found, wherever: in the install or beside it.
        string(REGEX REPLACE "^(CD [12]): .*$" "\\1: found" line "${line}")
        set(record "${record}${line}\n")
    endforeach()
elseif(STEP STREQUAL "formats")
    record_run(formats "\\.wav: " sith formats "${game_dir}" -q -hash)
elseif(STEP STREQUAL "movies")
    record_run(movies "57A\\.SMK" sith movies "${game_dir}" -all)
elseif(STEP STREQUAL "start")
    record_host(start 30 "" -keys "4:esc")
elseif(STEP STREQUAL "level")
    record_host(level 75 "02narshadda\\.jkl" -skim -keys "${new_game}")
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
