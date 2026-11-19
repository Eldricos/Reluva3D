# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The Unreal engine: Unreal Engine 1 games, starting with Star Trek: Deep
# Space Nine - The Fallen (docs/engines/unreal/). See
# docs/ADDING_AN_ENGINE.md.
reluva_add_engine(unreal
    ACCESSOR unrealMetaEngine
    SOURCES
        audio/level_audio.cpp
        formats/actors.cpp
        formats/class_defaults.cpp
        formats/dialog_table.cpp
        formats/font.cpp
        formats/game_save.cpp
        formats/game_save_write.cpp
        formats/mesh.cpp
        formats/model.cpp
        formats/package.cpp
        formats/package_set.cpp
        formats/package_writer.cpp
        formats/properties.cpp
        formats/sound.cpp
        formats/texture.cpp
        game/collision.cpp
        game/level_play.cpp
        game/zones.cpp
        games.cpp
        render/level_view.cpp
        render/lighting.cpp
        render/mesh_render.cpp
        render/procedural.cpp
        render/view.cpp
        ui/movie.cpp
        unreal.cpp
        vm/script.cpp
        vm/script_text.cpp
        vm/vm.cpp
        vm/vm_anim.cpp
        vm/vm_attach.cpp
        vm/vm_canvas.cpp
        vm/vm_climbing.cpp
        vm/vm_console.cpp
        vm/vm_dialog.cpp
        vm/vm_documents.cpp
        vm/vm_effect_physics.cpp
        vm/vm_effects.cpp
        vm/vm_level.cpp
        vm/vm_natives_core.cpp
        vm/vm_paths.cpp
        vm/vm_physics.cpp
        vm/vm_save.cpp
        vm/vm_sound.cpp
        vm/vm_splines.cpp
        vm/vm_travel.cpp)

# Its data viewer (maps, meshes, textures, sounds, movies, packages), in
# reluva-viewer.
reluva_add_engine_viewers(unreal
    ACCESSOR unrealMetaViewer
    SOURCES
        pages/mesh_viewer.cpp
        pages/viewer.cpp
        pages/viewers.cpp)

# Its reluva-tool commands (reluva-tool help unreal lists them).
reluva_add_engine_tools(unreal
    SOURCES
        tools/commands.cpp
        tools/soft_renderer.cpp)

# Its tests (tests/engines/unreal/), in reluva-tests: the unit tests by
# topic, on packages built in the tests, and the engine test, without game
# files (a small install of its own; its record in data/engine-record.txt).
# See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/unreal)
reluva_add_engine_tests(unreal
    SOURCES
        ${tests}/unit/packages.cpp
        ${tests}/unit/textures.cpp
        ${tests}/unit/sounds.cpp
        ${tests}/unit/levels.cpp
        ${tests}/unit/meshes.cpp
        ${tests}/unit/script.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.packages unit.textures unit.sounds unit.levels unit.meshes unit.script engine)
