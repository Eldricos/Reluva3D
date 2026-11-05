# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Star Trek: Deep Space Nine - Harbinger (docs/engines/stharbinger/), ported
# from STDS9H. See the main README (Adding an engine) and
# docs/ADDING_AN_ENGINE.md.
reluva_add_engine(stharbinger
    ACCESSOR stharbingerMetaEngine
    SOURCES
        stharbinger.cpp
        detection.cpp
        harbinger.cpp
        platform.cpp
        formats/archive.cpp
        formats/audio.cpp
        formats/dialogue.cpp
        formats/font.cpp
        formats/graphics.cpp
        formats/resources.cpp
        formats/script.cpp
        game/arcade.cpp
        game/game.cpp
        game/game_anim.cpp
        game/game_dialogue.cpp
        game/game_extras.cpp
        game/game_menu.cpp
        game/game_save.cpp
        game/game_script.cpp
        game/game_video.cpp
        pages/viewer.cpp)

# Its reluva-tool commands (reluva-tool help stharbinger lists them).
reluva_add_engine_tools(stharbinger
    SOURCES
        tools/files.cpp)

# Its tests (tests/engines/stharbinger/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/stharbinger)
reluva_add_engine_tests(stharbinger
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/formats.cpp
        ${tests}/unit/game.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.formats unit.game engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(stharbinger
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
