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
