# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The AWE engine: Agatha Christie: Murder on the Orient Express
# (docs/engines/awe/). See docs/ADDING_AN_ENGINE.md.
reluva_add_engine(awe
    ACCESSOR aweMetaEngine
    SOURCES
        awe.cpp
        formats/ac2.cpp
        formats/bink_dll.cpp
        formats/bink_texture.cpp
        formats/curves.cpp
        formats/gr2.cpp
        formats/granny_dll.cpp
        formats/model.cpp
        formats/oodle1.cpp
        formats/savegame.cpp
        formats/tables.cpp
        game/actor.cpp
        game/animator.cpp
        game/camera.cpp
        game/conditions.cpp
        game/dialogue.cpp
        game/game_paths.cpp
        game/picking.cpp
        game/scene_autoplay.cpp
        game/scene_cursor.cpp
        game/scene_interface.cpp
        game/scene_inventory.cpp
        game/scene_options.cpp
        game/scene_outdoors.cpp
        game/scene_page.cpp
        game/scene_puzzles.cpp
        game/scene_saves.cpp
        game/script.cpp
        game/story.cpp
        game/walk_grid.cpp
        render/lighting.cpp
        render/model_draw.cpp
        render/movie_player.cpp
        render/particles.cpp
        render/shadows.cpp
        render/sprite.cpp
        ui/dialogue_box.cpp
        ui/game_text.cpp
        ui/page.cpp)
# Characters are skinned and lit on every core (common/parallel.h).
find_package(Threads REQUIRED)
target_link_libraries(reluva_engine_awe PUBLIC Threads::Threads)
# The path-finder and the compressed curves match the game's arithmetic
# only without fused multiply-adds: off everywhere but Visual Studio (the
# top CMakeLists.txt), which doesn't fuse unless asked.

# Its data viewer (pages/), in reluva-viewer only: the game's scenes with
# the scene browser, models, movies, pictures, text and sounds.
reluva_add_engine_viewers(awe
    ACCESSOR aweMetaViewer
    SOURCES
        pages/model_viewer.cpp
        pages/viewers.cpp)

# Its reluva-tool commands (reluva-tool help awe lists them). They play the
# game in a window with -3d (the SDL backend's renderer), so the
# characters load and are picked by their bones.
reluva_add_engine_tools(awe
    WITH_SDL
    SOURCES
        tools/commands.cpp
        tools/check.cpp
        tools/path.cpp
        tools/play.cpp
        tools/scripts.cpp)

# Its tests (tests/engines/awe/), in reluva-tests: the unit tests by topic,
# on files built in the tests, and the engine test, without game files (a
# small install played through, its record in data/engine-record.txt). See
# docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/awe)
reluva_add_engine_tests(awe
    SOURCES
        ${tests}/unit/formats.cpp
        ${tests}/unit/scene.cpp
        ${tests}/unit/script.cpp
        ${tests}/unit/dialogue.cpp
        ${tests}/unit/characters.cpp
        ${tests}/unit/saves.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.formats unit.scene unit.script unit.dialogue unit.characters unit.saves engine)

# Its engine test with the game's own files (tests/engines/awe/gamedata.cmake):
# RELUVA_MOTOE=<the game's folder> ctest -L gamedata. See docs/DEVELOPMENT.md
# (Engine tests).
reluva_add_gamedata_test(awe
    GAME motoe
    ENV RELUVA_MOTOE
    SCRIPT ${tests}/gamedata.cmake
    STEPS release files scripts route-search route)
# The route's search can take more than the hour each step is given.
if(RELUVA_BUILD_TESTS)
    set_tests_properties(awe.gamedata.route-search PROPERTIES TIMEOUT 7200)
endif()
