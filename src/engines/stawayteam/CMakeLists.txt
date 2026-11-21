# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Star Trek: Away Team (docs/engines/stawayteam/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(stawayteam
    ACCESSOR stawayteamMetaEngine
    SOURCES
        stawayteam.cpp
        formats/binary_object.cpp
        formats/class_table.cpp
        formats/frame.cpp
        formats/object_schema.cpp
        ui/movies.cpp
        ui/main_menu.cpp
        ui/settings.cpp
        game/options.cpp
        ui/button.cpp
        ui/credits.cpp
        ui/difficulty.cpp
        ui/font.cpp
        ui/layout.cpp
        ui/widgets.cpp
        formats/rds_script.cpp
        formats/text_object.cpp)

# Its data viewer, in reluva-viewer (never in reluva): docs/DEVELOPMENT.md.
reluva_add_engine_viewers(stawayteam
    ACCESSOR stawayteamMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/animation.cpp
        pages/draw.cpp
        pages/file_list.cpp
        pages/font_viewer.cpp
        pages/model_viewer.cpp
        pages/object_viewer.cpp
        pages/picture_viewer.cpp
        pages/sequence_viewer.cpp
        pages/sound_viewer.cpp)

# Its reluva-tool commands (reluva-tool help stawayteam lists them).
reluva_add_engine_tools(stawayteam
    SOURCES
        tools/archive.cpp
        tools/files.cpp
        tools/images.cpp
        tools/objects.cpp
        tools/text.cpp)

# Its tests (tests/engines/stawayteam/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/stawayteam)
reluva_add_engine_tests(stawayteam
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/formats.cpp
        ${tests}/unit/menus.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.formats unit.menus engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(stawayteam
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
