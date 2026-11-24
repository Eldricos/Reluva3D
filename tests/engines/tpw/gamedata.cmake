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
#   text     every string table (.str) read in the language's character
#            set, with a checksum of every string (tpw str-check)
#   fonts    every font (.bf4) read, with a checksum of every glyph
#            (tpw bf4-check)
#   pictures every picture decoded, .fsh banks, .tga, .png and .jpg, with
#            a checksum of them all (tpw picture-check)
#   movies   every movie (.tgq) decoded, its frames and its sound, with a
#            checksum of them all (tpw movie-check)
#   opening  the opening run headless: the screens shown, when it ended
#            (tpw opening-run)
#   frontend the front end run headless: its camera's path skipped 5
#            seconds in, then a player named in the Personnel File and
#            made, "Enter Game", and a world on the globe; the screens
#            shown, the world chosen, with a checksum (tpw front-end-run)
#   loading  the loading screen put together, with a checksum of it
#            (tpw loading-screen)
#   sounds   every sound bank (.sdt) decoded, with a checksum of every
#            sound (tpw sdt-check)
#   soundmaps every sound category's maps read, each sound found in its
#            bank and its length checked, with a checksum (tpw sfx-check)
#   models   every model (.MD2) read, with a checksum of every mesh
#            (tpw md2-check)
#   animations every model animation (.MD2, flag 0x20) read and set
#            against its model (tpw anim-check)
#   sprites  every sprite sheet (.TPC) and animation set (.ESP) of
#            esprites.wad read, with a checksum (tpw sprite-check)
#   heights  every height map (.hmp) read, with a checksum of them all
#            (tpw hmp-check)
#   terrain  every world's ground (.lnd), attribute map (base.map) and
#            objects (scape.omp) read, with a checksum of them all (tpw
#            terrain-check)
#   saves    the prebuilt park (PreBuilt/prebuilt.TPWS) read: its cells,
#            path and world, with a checksum of its state (tpw tpws-check)
#   scripts  every ride and shop script (.RSE) read, with a checksum of
#            their disassembly (tpw rse-check)
#   park     a new park in Land of Invention played headless: its
#            briefing, path laid, a ride bought and placed with its
#            queue, the Park menu's Objectives, the camera
#            moved by keys and the compass, a tool chosen, a menu, a
#            visitor, then Esc; the screens shown, the date, money,
#            visitors, path, briefings and tool, with a checksum (tpw
#            park-run); and the world's signs lettered, their fonts and
#            lines, with a checksum of them drawn (tpw sign-shot)
#   viewer   the data viewer, headless: every page, a few keys on each,
#            then Esc; a checksum of every screen shown, and its titles
#            (tpw viewer-run)
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
elseif(STEP STREQUAL "text")
    record_run(text "string tables read" tpw str-check "${game_dir}")
elseif(STEP STREQUAL "fonts")
    record_run(fonts "fonts read" tpw bf4-check "${game_dir}")
elseif(STEP STREQUAL "pictures")
    record_run(pictures "pictures decoded" tpw picture-check "${game_dir}")
elseif(STEP STREQUAL "movies")
    record_run(movies "movies decoded" tpw movie-check "${game_dir}")
elseif(STEP STREQUAL "opening")
    record_run(opening "screens shown" tpw opening-run "${game_dir}")
elseif(STEP STREQUAL "frontend")
    record_run(frontend "screens shown" tpw front-end-run "${game_dir}")
elseif(STEP STREQUAL "loading")
    record_run(loading "md5" tpw loading-screen "${game_dir}")
elseif(STEP STREQUAL "sounds")
    record_run(sounds "sounds decoded" tpw sdt-check "${game_dir}")
elseif(STEP STREQUAL "soundmaps")
    record_run(soundmaps "categories," tpw sfx-check "${game_dir}")
elseif(STEP STREQUAL "models")
    record_run(models "meshes," tpw md2-check "${game_dir}")
elseif(STEP STREQUAL "animations")
    record_run(animations "animations" tpw anim-check "${game_dir}")
elseif(STEP STREQUAL "sprites")
    record_run(sprites "sheets," tpw sprite-check "${game_dir}")
elseif(STEP STREQUAL "heights")
    record_run(heights "height maps," tpw hmp-check "${game_dir}")
elseif(STEP STREQUAL "terrain")
    record_run(terrain "grounds," tpw terrain-check "${game_dir}")
elseif(STEP STREQUAL "saves")
    record_run(saves "cells" tpw tpws-check "${game_dir}")
elseif(STEP STREQUAL "scripts")
    record_run(scripts "scripts," tpw rse-check "${game_dir}")
elseif(STEP STREQUAL "park")
    record_run(park "screens shown" tpw park-run "${game_dir}" levels/science)
    file(MAKE_DIRECTORY "${OUTPUT}/park")
    record_run(signs "signs lettered" tpw sign-shot "${game_dir}" levels/science "${OUTPUT}/park/signs.png")
    record_file(signs.png "${OUTPUT}/park/signs.png")
elseif(STEP STREQUAL "viewer")
    record_run(viewer "left by:" tpw viewer-run "${game_dir}")
else()
    message(FATAL_ERROR "No step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
