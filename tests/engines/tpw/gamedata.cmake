# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The Theme Park World engine's test with SimCoaster's own files: one step
# each time ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/tpw/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and
# docs/engines/tpw/games/simcoaster/TESTING.md.
#
# The game's folder (SimCoaster installed, or imported from its CD: the one
# holding Game.exe and Data) comes from the environment variable named by
# GAME_ENV (RELUVA_SIMCOASTER). Without it the step is skipped.
#
# Each step runs reluva-tool's commands on the game:
#   release  what the game is (detect)
#   data     every file of its Data folder read, every archive's
#            unpacked, with a checksum of them all (tpw data-check)
#   settings every settings file (.sam) read (tpw sam-check)
# A command that fails fails the step. What each prints is checksummed,
# with a summary line, into a record:
# tests/engines/tpw/data/gamedata/<release>/<step>.txt. The step passes when
# its record is the same as the committed one. Records so far, made on
# Linux: us (the US release, installed by its setup or imported from its CD,
# which come to the same files); make others with RELUVA_GAMEDATA_UPDATE=1.
#
# Environment: RELUVA_SIMCOASTER, the game's folder; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the one holding Game.exe and Data")

run_tool(detected detect "${game_dir}")
string(REGEX MATCH "tpw/simcoaster: ([^\n]*)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "SimCoaster isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
if(release MATCHES "^SimCoaster \\(US\\)")
    set(slug "us")
else()
    set(slug "unknown-release")
endif()

if(STEP STREQUAL "release")
    set(record "release: ${release}\n")
elseif(STEP STREQUAL "data")
    record_run(data "files read" tpw data-check "${game_dir}")
elseif(STEP STREQUAL "settings")
    record_run(settings "settings files read" tpw sam-check "${game_dir}")
else()
    message(FATAL_ERROR "No step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
