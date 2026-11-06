# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The AWE engine: Agatha Christie: Murder on the Orient Express
# (docs/engines/awe/). See docs/ADDING_AN_ENGINE.md.
reluva_add_engine(awe
    ACCESSOR aweMetaEngine
    SOURCES
        ac2.cpp
        actor.cpp
        animator.cpp
        awe.cpp
        bink_dll.cpp
        bink_texture.cpp
        camera.cpp
        conditions.cpp
        curves.cpp
        dialogue.cpp
        dialogue_box.cpp
        game_paths.cpp
        game_text.cpp
        gr2.cpp
        granny_dll.cpp
        lighting.cpp
        model.cpp
        model_draw.cpp
        model_viewer.cpp
        movie_player.cpp
        oodle1.cpp
        particles.cpp
        picking.cpp
        savegame.cpp
        scene_autoplay.cpp
        scene_cursor.cpp
        scene_interface.cpp
        scene_inventory.cpp
        scene_options.cpp
        scene_outdoors.cpp
        scene_puzzles.cpp
        scene_saves.cpp
        script.cpp
        shadows.cpp
        sprite.cpp
        story.cpp
        tables.cpp
        viewers.cpp
        walk_grid.cpp)
# Characters are skinned and lit on every core (common/parallel.h).
find_package(Threads REQUIRED)
target_link_libraries(reluva_engine_awe PUBLIC Threads::Threads)
# The path-finder and the compressed curves match the game's arithmetic
# only without fused multiply-adds: off everywhere but Visual Studio (the
# top CMakeLists.txt), which doesn't fuse unless asked.

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
# on files built in the tests. See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/awe)
reluva_add_engine_tests(awe
    SOURCES
        ${tests}/unit/formats.cpp
        ${tests}/unit/scene.cpp
        ${tests}/unit/script.cpp
        ${tests}/unit/dialogue.cpp
        ${tests}/unit/characters.cpp
        ${tests}/unit/saves.cpp
    TOPICS
        unit.formats unit.scene unit.script unit.dialogue unit.characters unit.saves)
