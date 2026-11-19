# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Star Trek: Generations' engine test with the game's own files: one step
# each time ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/stgenerations/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests).
#
# The game's folder comes from the environment variable named by GAME_ENV
# (RELUVA_STGENERATIONS): Reluva's import of the game (the installed game,
# its CDs in disc1/ and disc2/), or a folder holding the installed game and
# both CDs side by side. Without it the step is skipped.
#
# Each step runs reluva-tool's stgenerations commands on the game:
#   release   what the game is (detect), its parts, and its files (files)
#   archives  every archive's every file read (archives)
#   pictures  every bitmap, overlay, sprite set and font (pictures)
#   levels    every away mission's level, textures and script (levels)
#   sounds    every sound (sounds)
#   movies    every movie's frames and sound (movies)
#   front_end the game played: the title, the menu, a new game, its movie
#             skipped, the bridge, a save and loading it (play)
#   mission   the first away mission, walked into (play)
#   combat    the first space combat, its helm given an order (play)
# A command that fails fails the step. What each prints (paths taken out) is
# checksummed, with a summary line, into a record:
# tests/engines/stgenerations/data/gamedata/<release>/<step>.txt. The step
# passes when its record is the same as the committed one. Records so far:
# english (the 2 CD English release, made on Linux, from a copy with a few
# damaged files: docs/engines/stgenerations/TESTING.md). Make others with
# RELUVA_GAMEDATA_UPDATE=1, and check them.
#
# Environment: RELUVA_STGENERATIONS, the game's folder; and those of the
# shared helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("Reluva's import of the game, or the installed game and both CDs side by side")

# Where its parts are: an import holds them all; side by side, the CDs are
# given to the commands.
run_tool(parts stgenerations parts "${game_dir}")
set(install "${game_dir}")
set(cds "")
if(parts MATCHES "install: ([^\n]*)")
    if(NOT CMAKE_MATCH_1 STREQUAL ".")
        set(install "${game_dir}/${CMAKE_MATCH_1}")
    endif()
else()
    message(FATAL_ERROR "The installed game isn't found in ${game_dir}:\n${parts}")
endif()
if(NOT install STREQUAL game_dir)
    foreach(cd cd1 cd2)
        if(parts MATCHES "${cd}: ([^\n]*)")
            list(APPEND cds --${cd} "${game_dir}/${CMAKE_MATCH_1}")
        endif()
    endforeach()
endif()

# What the game is: the record's folder is named after its language.
run_tool(detected detect "${install}")
string(REGEX MATCH "stgenerations/stgenerations: ([^\n]*) \\(([a-z][a-z]), windows\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Star Trek: Generations isn't found in ${install}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
set(slug "english")
if(NOT CMAKE_MATCH_2 STREQUAL "en")
    set(slug "${CMAKE_MATCH_2}")
endif()

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
    string(REPLACE "${game_dir}" "<game>" shown "${parts}")
    string(REGEX REPLACE "\n$" "" shown "${shown}")
    string(REPLACE "\n" ", " shown "${shown}")
    set(record "${record}parts: ${shown}\n")
    record_run(files "" stgenerations files "${install}")
elseif(STEP STREQUAL "archives")
    record_run(archives "archives, " stgenerations archives "${install}" ${cds})
elseif(STEP STREQUAL "pictures")
    record_run(pictures "bitmaps" stgenerations pictures "${install}" ${cds})
elseif(STEP STREQUAL "levels")
    record_run(levels "levels read" stgenerations levels "${install}" ${cds})
elseif(STEP STREQUAL "sounds")
    record_run(sounds "sounds read" stgenerations sounds "${install}" ${cds})
elseif(STEP STREQUAL "movies")
    record_run(movies "movies read" stgenerations movies "${install}" ${cds})
elseif(STEP STREQUAL "front_end" OR STEP STREQUAL "mission" OR STEP STREQUAL "combat")
    set(saves "${OUTPUT}/${STEP}/saves")
    file(REMOVE_RECURSE "${saves}")
    set(play stgenerations play "${install}" ${cds} --saves "${saves}" --frames)
    if(STEP STREQUAL "front_end")
        # The title, the menu: New, Accept; the movies skipped to the bridge;
        # OPTIONS, Save, the first slot named, Accept; then that save loaded.
        list(APPEND play --until 45000 --click 7000:165,101 --click 8000:418,404
             --key 10500:space --key 14500:space --key 18500:space --key 22500:space
             --click 26000:575,446 --click 28000:165,71 --click 29000:280,73 --type 30000:Picard
             --click 32000:417,400 --click 35000:575,446 --click 36000:165,41 --click 37000:280,73
             --click 38000:417,400)
    elseif(STEP STREQUAL "mission")
        # Amargosa's observatory: the briefing, PROCEED, then a few steps in.
        list(APPEND play --story amargosa1play --until 25000 --key 1500:space --key 4000:space
             --click 7000:320,240 --key 13000:up --key 13300:up --key 13600:up --click 21000:320,200)
    else()
        # Space combat 1: the briefing, PROCEED, the helm's orders, phasers,
        # a torpedo.
        list(APPEND play --combat 1 --until 30000 --click 4000:320,440 --click 5000:540,445
             --click 6000:600,440 --key 10000:1 --key 15000:lshift --key 16000:return)
    endif()
    record_run(play "frames shown" ${play})
    file(GLOB saved "${saves}/*")
    list(LENGTH saved n)
    set(record "${record}files saved: ${n}\n")
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
