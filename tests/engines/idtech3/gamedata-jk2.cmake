# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Jedi Outcast's engine test with the game's own files, as gamedata.cmake is
# Elite Force's: one step each time ctest runs this script, as registered by
# reluva_add_gamedata_test in src/engines/idtech3/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests).
#
# The game's folder (the one holding base: an install's GameData, the CD's
# GAMEDATA/GameData, or an import) comes from RELUVA_JK2. Without it the
# step is skipped.
#
# The steps are named for the game (jk2-...), so their outputs in the build
# don't meet Elite Force's:
#   jk2-release   what the game is
#   jk2-maps      every map read (bsp-check)
#   jk2-shaders   every shader script read (shader-check)
#   jk2-models    every MD3 model and skin read (model-check)
#   jk2-ghoul2    every Ghoul2 mesh and skeleton read, each mesh against
#                 its skeleton (ghoul2-check)
#   jk2-images    the images every map's shaders name (image-check)
#   jk2-effects   every effect file read and registered, with the shaders,
#                 models and sounds it names (efx-check)
#   jk2-roffs     every recorded path read: its frames and where it ends up
#                 (roff-check)
#   jk2-videos    two RoQ videos decoded, their pictures and sound recorded
#                 (roq-decode)
#   jk2-game      the multiplayer game module: ffa_bespin with the player
#                 joining and running forward, then with two bots, then
#                 with the client game drawing the player's view and its
#                 effects as it fires (game-run;
#                 g_maxForceRank 7, as the player's default force powers
#                 are rank 7 and the force menu isn't there to lower them)
#   jk2-single-player  the single-player game DLL on kejim_post: its Init,
#                 the player joining and 100 server frames, the client game
#                 (its HUD menus read) drawing each, on a clock that moves
#                 with the level's so the run repeats (sp-run)
#   jk2-sp-tour   kejim_post and kejim_base played through with cheats once
#                 their openings are over: every trigger touched, fighting
#                 between them with every direct-fire weapon, to the level
#                 change; Jan spared (her death fails the mission). Each
#                 then the next level started with the player carried over
#                 (-next). About seven minutes.
#   jk2-sp-artus  the same for artus_mine (its way out by a written route,
#                 data/routes/artus_mine.txt) and artus_detention. About
#                 seven minutes.
#   jk2-menus     the multiplayer menus: the main menu, Setup, Create Server
#                 by way of Play, and Join Server (ui-run, clicking in the
#                 menus' 640 x 480, with the engine's server settings)
# Records: tests/engines/idtech3/data/gamedata/jk2-<release>/<step>.txt.

include("${HELPERS}")
gamedata_begin("the one holding base")

find_folder(base "${game_dir}" base)
if(base STREQUAL "")
    message(FATAL_ERROR "No base folder in ${game_dir}")
endif()

run_tool(detected detect "${game_dir}")
string(REGEX MATCH "idtech3/jk2: [^\n]*\\(([^()]*)\\) \\([a-z][a-z], [a-z]+\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Jedi Outcast isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
string(TOLOWER "jk2-${release}" slug)
string(REGEX REPLACE "[^a-z0-9.]+" "-" slug "${slug}")
string(REGEX REPLACE "^-|-$" "" slug "${slug}")

if(STEP STREQUAL "jk2-release")
    string(STRIP "${detected}" detected)
    set(record "release: ${detected}\n")
elseif(STEP STREQUAL "jk2-maps")
    record_run(bsp-check "maps: " idtech3 bsp-check "${base}")
elseif(STEP STREQUAL "jk2-shaders")
    record_run(shader-check "" idtech3 shader-check "${base}")
elseif(STEP STREQUAL "jk2-effects")
    record_run(efx-check "" idtech3 efx-check "${base}")
elseif(STEP STREQUAL "jk2-roffs")
    record_run(roff-check "" idtech3 roff-check "${base}")
elseif(STEP STREQUAL "jk2-models")
    record_run(model-check "" idtech3 model-check "${base}")
elseif(STEP STREQUAL "jk2-ghoul2")
    record_run(ghoul2-check "meshes, " idtech3 ghoul2-check "${base}")
elseif(STEP STREQUAL "jk2-images")
    record_run(image-check "distinct missing images" idtech3 image-check "${base}")
elseif(STEP STREQUAL "jk2-videos")
    file(MAKE_DIRECTORY "${OUTPUT}/${STEP}")
    foreach(video openinglogos jk0101)
        set(yuv "${OUTPUT}/${STEP}/${video}.yuv")
        set(pcm "${OUTPUT}/${STEP}/${video}.pcm")
        record_run(${video} "frames, " idtech3 roq-decode "${base}" video/${video} -frames 60 -yuv "${yuv}" -pcm "${pcm}")
        record_file(${video}.pictures "${yuv}")
        record_file(${video}.sound "${pcm}")
        file(REMOVE "${yuv}" "${pcm}")
    endforeach()
elseif(STEP STREQUAL "jk2-game")
    set(join -set g_maxForceRank 7 -clientcmd "team free")
    record_run(ffa_bespin-walk "start level" idtech3 game-run "${base}" ffa_bespin -frames 40 ${join} -walk forward)
    record_run(ffa_bespin-bots "start level" idtech3 game-run "${base}" ffa_bespin -frames 100 ${join}
               -addbot "Tavion 3" -addbot "Desann 3")
    record_run(ffa_bespin-cgame "start level" idtech3 game-run "${base}" ffa_bespin -frames 10 ${join} -cgame -fire)
    record_run(ffa_bespin-net "game code" idtech3 net-run "${base}" ffa_bespin -loopback 120 -set sv_pure 0
               -set g_maxForceRank 7 -clientcmd "team free")
    record_run(ffa_bespin-mapchange "game code" idtech3 net-run "${base}" ffa_bespin -loopback 160 -set sv_pure 0
               -set g_maxForceRank 7 -clientcmd "team free" -mapchange ffa_deathstar)
elseif(STEP STREQUAL "jk2-single-player")
    record_run(kejim_post "frames run" idtech3 sp-run "${base}" kejim_post -frames 100 -fixedclock -cgame -saveload)
elseif(STEP STREQUAL "jk2-sp-tour")
    record_run(kejim_post "ran 100 frames" idtech3 sp-run "${base}" kejim_post -tour 100000 -spare jan
               -spare cinematic1_jan -spare cinematic1_kyle -fixedclock -set helpUsObi 1 -next)
    record_run(kejim_base "ran 100 frames" idtech3 sp-run "${base}" kejim_base -tour 5000 -spare cinematic3
               -fixedclock -set helpUsObi 1 -next)
elseif(STEP STREQUAL "jk2-sp-artus")
    record_run(artus_mine "ran 100 frames" idtech3 sp-run "${base}" artus_mine
               -replay "${CMAKE_CURRENT_LIST_DIR}/data/routes/artus_mine.txt" -fixedclock -set helpUsObi 1 -next)
    record_run(artus_detention "ran 100 frames" idtech3 sp-run "${base}" artus_detention -tour 16000 -fixedclock
               -set helpUsObi 1 -next)
elseif(STEP STREQUAL "jk2-menus")
    record_run(main-menu "" idtech3 ui-run "${base}" -servercvars -frames 30)
    record_run(setup "" idtech3 ui-run "${base}" -servercvars -frames 10 -click 570 125)
    record_run(create-server "" idtech3 ui-run "${base}" -servercvars -frames 10 -click 179 126 -click 373 284)
    record_run(join-server "" idtech3 ui-run "${base}" -servercvars -frames 10 -click 179 126 -click 373 224)
else()
    message(FATAL_ERROR "No step \"${STEP}\"")
endif()

gamedata_end("${release}" "${slug}")
