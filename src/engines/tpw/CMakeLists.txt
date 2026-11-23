# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Theme Park World (docs/engines/tpw/). See the main README (Adding an engine)
# and docs/ADDING_AN_ENGINE.md.
reluva_add_engine(tpw
    ACCESSOR tpwMetaEngine
    SOURCES
        tpw.cpp
        games.cpp
        game_files.cpp
        textures.cpp
        ui/model_view.cpp
        ui/opening.cpp
        ui/world_view.cpp
        formats/attribute_map.cpp
        formats/bf4.cpp
        formats/hmp.cpp
        formats/lnd.cpp
        formats/md2.cpp
        formats/omp.cpp
        formats/sam.cpp
        formats/sdt.cpp
        formats/str.cpp
        formats/wad.cpp)

# Its data viewer (pictures, fonts, text, settings, sounds, movies, files),
# in reluva-viewer.
reluva_add_engine_viewers(tpw
    ACCESSOR tpwMetaViewer
    SOURCES
        pages/viewer.cpp
        pages/media_pages.cpp
        pages/model_page.cpp
        pages/world_page.cpp
        pages/page.cpp
        pages/picture_pages.cpp
        pages/text_pages.cpp)

# Its reluva-tool commands (reluva-tool help tpw lists them).
reluva_add_engine_tools(tpw
    SOURCES
        tools/files.cpp
        tools/models.cpp
        tools/movies.cpp
        tools/pictures.cpp
        tools/sounds.cpp
        tools/terrain.cpp
        tools/viewer.cpp
        tools/text.cpp
        tools/wad.cpp)

# Its tests (tests/engines/tpw/), in reluva-tests: the unit tests by
# topic, and the engine test, without game files (its record in
# data/engine-record.txt). See docs/DEVELOPMENT.md (Tests).
set(tests ${PROJECT_SOURCE_DIR}/tests/engines/tpw)
reluva_add_engine_tests(tpw
    SOURCES
        ${tests}/unit/detection.cpp
        ${tests}/unit/import.cpp
        ${tests}/unit/formats.cpp
        ${tests}/unit/ui.cpp
        ${tests}/engine.cpp
    TOPICS
        unit.detection unit.import unit.formats unit.ui engine)

# Its engine test with SimCoaster's own files (see docs/DEVELOPMENT.md,
# Engine tests): RELUVA_SIMCOASTER=<the game's folder>.
reluva_add_gamedata_test(tpw
    GAME simcoaster
    ENV RELUVA_SIMCOASTER
    SCRIPT ${tests}/gamedata.cmake
    STEPS release data settings text fonts pictures movies opening sounds models heights terrain viewer)
