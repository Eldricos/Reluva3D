# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Sith (docs/engines/sith/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(sith
    ACCESSOR sithMetaEngine
    SOURCES
        sith.cpp
        cut_scene.cpp
        folders.cpp
        game_files.cpp
        games.cpp
        import.cpp
        host/game_process.cpp
        host/player.cpp
        render/draw3d.cpp
        render/scene_gpu.cpp
        formats/bitmap.cpp
        formats/colormap.cpp
        formats/episode.cpp
        formats/font.cpp
        formats/gob.cpp
        formats/keyframe.cpp
        formats/level.cpp
        formats/material.cpp
        formats/model.cpp
        formats/puppet.cpp
        formats/sprite.cpp
        formats/sound_class.cpp
        formats/strings.cpp
        formats/text.cpp)

# Its data viewer, for reluva-viewer only: reluva, which players get,
# doesn't link it. See docs/DEVELOPMENT.md (The data viewer).
reluva_add_engine_viewers(sith
    ACCESSOR sithMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/level_pages.cpp
        pages/model_pages.cpp
        pages/movie_pages.cpp
        pages/picture_pages.cpp
        pages/sound_pages.cpp
        pages/sprite_pages.cpp
        pages/text_pages.cpp)

# Its reluva-tool commands (reluva-tool help sith lists them). WITH_SDL:
# the host command's -gpu draws the game's scenes on the GPU, through the
# SDL backend.
reluva_add_engine_tools(sith WITH_SDL
    SOURCES
        tools/files.cpp
        tools/formats.cpp
        tools/host.cpp
        tools/movies.cpp
        tools/pages.cpp)

# Its tests (tests/engines/sith/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/sith)
reluva_add_engine_tests(sith
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/folders.cpp
        ${tests}/unit/formats.cpp
        ${tests}/unit/gob.cpp
        ${tests}/unit/import.cpp
        ${tests}/unit/levels.cpp
        ${tests}/unit/models.cpp
        ${tests}/unit/render.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.folders unit.formats unit.gob unit.import unit.levels unit.models unit.render engine)

# Its engine test with the game's own files (see docs/DEVELOPMENT.md,
# Engine tests, and tests/engines/sith/gamedata.cmake).
reluva_add_gamedata_test(sith
    GAME jk
    ENV RELUVA_JK
    SCRIPT ${tests}/gamedata.cmake
    STEPS release formats movies start level wide wide1610 wide219)
reluva_add_gamedata_test(sith
    NAME sith.mots
    GAME mots
    ENV RELUVA_MOTS
    SCRIPT ${tests}/gamedata_mots.cmake
    STEPS release formats movies start level wide wide1610 wide219)
