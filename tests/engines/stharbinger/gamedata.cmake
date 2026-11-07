# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Harbinger's engine test with the game's own files: one step each time
# ctest runs this script (cmake -P), as registered by reluva_add_gamedata_test
# in src/engines/stharbinger/CMakeLists.txt. See docs/DEVELOPMENT.md (Engine
# tests).
#
# The game's folder comes from the environment variable named by GAME_ENV
# (RELUVA_STHARBINGER): anything the launcher takes, a CD, both CDs side by
# side, an install, or Reluva's import of them. Without it the step is
# skipped.
#
# Each step runs reluva-tool's stharbinger commands on the game:
#   release        what the game is (detect), and its parts and files (files)
#   info           its scenes with the files each finds, dialogues, credits
#   scripts        every scene and dialogue script listed (script, dialogue)
#   conversations  every conversation played through three times, replies
#                  picked at random (conversations)
# A command that fails fails the step. What each prints (paths taken out) is
# checksummed, with a summary line, into a record:
# tests/engines/stharbinger/data/gamedata/<release>/<step>.txt. The step
# passes when its record is the same as the committed one. Records so far:
# both-discs (the English release, made on Linux); make others with
# RELUVA_GAMEDATA_UPDATE=1, and check them (docs/engines/stharbinger/TESTING.md).
#
# Environment: RELUVA_STHARBINGER, the game's folder; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("a CD, both CDs side by side, or an install")

# What the game is: the record's folder is named after it ("both-discs", or
# "disc-1" when the folder holds only disc 1).
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "stharbinger/ds9harbinger: ([^\n]*) \\([a-z][a-z], dos\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Harbinger isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
set(slug "both-discs")
if(release MATCHES "\\(disc ([12])\\)")
    set(slug "disc-${CMAKE_MATCH_1}")
endif()

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
    record_run(files "" stharbinger files "${game_dir}")
elseif(STEP STREQUAL "info")
    record_run(info "lines of credits" stharbinger info "${game_dir}")
elseif(STEP STREQUAL "scripts")
    set(out "${OUTPUT}/${STEP}/listings")
    file(REMOVE_RECURSE "${out}")
    record_run(script "symbol arguments named" stharbinger script "${game_dir}" "${out}")
    record_file(summary "${out}/summary.txt")
    record_run(dialogue "conversations" stharbinger dialogue "${game_dir}" "${out}/dialogue")
    record_file(dialogue.txt "${out}/dialogue/dialogue.txt")
elseif(STEP STREQUAL "conversations")
    record_run(conversations "conversations played" stharbinger conversations "${game_dir}" 3)
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
