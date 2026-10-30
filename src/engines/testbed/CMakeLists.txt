# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The testbed: a built-in test pattern, no game. Always built (the launcher
# and `reluva testbed` use it).
reluva_add_engine(testbed
    ACCESSOR testbedMetaEngine
    SOURCES
        testbed.cpp)
