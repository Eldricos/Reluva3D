# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Need for Speed III: Hot Pursuit (docs/engines/nfs/): its files read, and
# the retail game run in the x86 emulator (host/), shown by the Play page
# (ui/). See the main README (Adding an engine) and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(nfs
    ACCESSOR nfsMetaEngine
    SOURCES
        nfs.cpp
        games.cpp
        folders.cpp
        game_files.cpp
        formats/viv.cpp
        formats/refpack.cpp
        formats/fsh.cpp
        formats/text_table.cpp
        formats/ffn.cpp
        formats/bnk.cpp
        formats/mus.cpp
        formats/mad.cpp
        formats/fce.cpp
        formats/frd.cpp
        formats/col.cpp
        formats/hrz.cpp
        render/particles.cpp
        render/thrash_gpu.cpp
        ui/page.cpp
        ui/play_page.cpp
        host/game_host.cpp
        host/directx.cpp
        host/force_effects.cpp
        host/native_sound.cpp
        host/sound3d.cpp
        host/winsock.cpp
        host/serial.cpp
        host/thrash.cpp
        host/thrash_native.cpp)

# Its data viewer, in reluva-viewer and never in the game: the files'
# pictures, models, tracks, sounds and text, beside the game's Play page.
reluva_add_engine_viewers(nfs
    ACCESSOR nfsMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/viewers.cpp
        pages/car_viewer.cpp
        pages/track_viewer.cpp
        pages/media_viewer.cpp)

# Its reluva-tool commands (reluva-tool help nfs lists them). WITH_SDL: the
# host command's GPU work (the Glide stand-in on the GPU) goes through the
# SDL backend, on a device of its own.
reluva_add_engine_tools(nfs WITH_SDL
    SOURCES
        tools/formats.cpp
        tools/host.cpp
        tools/pages.cpp)

# Its tests (tests/engines/nfs/), in reluva-tests: the unit tests by topic,
# and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/nfs)
reluva_add_engine_tests(nfs
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/formats.cpp
        ${tests}/unit/track.cpp
        ${tests}/unit/computer.cpp
        ${tests}/unit/host.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.formats unit.track unit.computer unit.host engine)

# Its engine test with the game's own files (tests/engines/nfs/gamedata.cmake):
# RELUVA_NFS3=<the game's install> ctest -L gamedata. See docs/DEVELOPMENT.md
# (Engine tests).
reluva_add_gamedata_test(nfs
    GAME nfs3
    ENV RELUVA_NFS3
    SCRIPT ${tests}/gamedata.cmake
    STEPS release survey formats host host_glide)
