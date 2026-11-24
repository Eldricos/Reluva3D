# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Storm3D (docs/engines/storm3d/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(storm3d
    ACCESSOR storm3dMetaEngine
    SOURCES
        storm3d.cpp
        game_files.cpp
        formats/bzn.cpp
        formats/config_file.cpp
        formats/input_map.cpp
        formats/label_map.cpp
        formats/ldl.cpp
        formats/odf.cpp
        formats/sod.cpp
        formats/sprite_table.cpp
        formats/zfs_archive.cpp
        shell/full_screen_movie.cpp
        shell/main_menu.cpp
        shell/panel_movie.cpp
        shell/shell_draw.cpp
        shell/single_player.cpp)

# Its data viewer, for reluva-viewer: a page for each kind of file.
reluva_add_engine_viewers(storm3d
    ACCESSOR storm3dMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/image_page.cpp
        pages/map_page.cpp
        pages/model_page.cpp
        pages/movie_page.cpp
        pages/page.cpp
        pages/sod_meshes.cpp
        pages/sound_page.cpp
        pages/sprite_page.cpp
        pages/text_page.cpp)

# Its reluva-tool commands (reluva-tool help storm3d lists them).
reluva_add_engine_tools(storm3d
    SOURCES
        tools/archives.cpp
        tools/engine_shot.cpp
        tools/files.cpp
        tools/host.cpp
        tools/images.cpp
        tools/maps.cpp
        tools/media.cpp
        tools/models.cpp
        tools/survey.cpp
        tools/text.cpp
        tools/viewer_shot.cpp)

# Its tests (tests/engines/storm3d/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/storm3d)
reluva_add_engine_tests(storm3d
    SOURCES
        ${tests}/unit/bzn.cpp
        ${tests}/unit/config.cpp
        ${tests}/unit/detection.cpp
        ${tests}/unit/images.cpp
        ${tests}/unit/input_map.cpp
        ${tests}/unit/label_map.cpp
        ${tests}/unit/ldl.cpp
        ${tests}/unit/odf.cpp
        ${tests}/unit/shell.cpp
        ${tests}/unit/sod.cpp
        ${tests}/unit/spr.cpp
        ${tests}/unit/zfs.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.bzn unit.config unit.detection unit.images unit.input_map unit.label_map unit.ldl unit.odf unit.shell unit.sod unit.spr unit.zfs engine)

# Its engine test with the game's own files, when it has one (see
# docs/DEVELOPMENT.md, Engine tests):
# reluva_add_gamedata_test(storm3d
#     GAME <game id>
#     ENV RELUVA_<GAME>
#     SCRIPT ${tests}/gamedata.cmake
#     STEPS release ...)
