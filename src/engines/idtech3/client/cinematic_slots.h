// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/cinematic.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva {
class SearchSet;
}

namespace reluva::idtech3 {

/**
 * The engine's cinematics a program runs itself (its CIN_* calls), as
 * jk2mp.exe keeps them for the menus (0x404940 on): up to 16 videos,
 * each moved on by the program's "run" and drawn where it last said.
 */
class CinematicSlots {
public:
    // CIN_PlayCinematic's bits.
    static constexpr int32_t kSystem = 1; // the engine's own cinematic, never shared
    static constexpr int32_t kLoop = 2;
    static constexpr int32_t kHold = 4; // the last frame kept at the end
    // What run and stop answer (e_status).
    static constexpr int32_t kIdle = 0;
    static constexpr int32_t kPlay = 1;
    static constexpr int32_t kEnded = 2; // FMV_EOF
    static constexpr int kSlots = 16;

    explicit CinematicSlots(const SearchSet &files) : _files(files) {}

    /**
     * CIN_PlayCinematic: `name` under video/ unless it has a folder, .roq
     * unless it has a type; the slot already playing it unless `bits`
     * has kSystem. -1 if it can't be read. `nowMs` is when it starts.
     */
    int32_t play(const std::string &name, int32_t x, int32_t y, int32_t w, int32_t h, int32_t bits, int32_t nowMs);
    /** CIN_StopCinematic: the slot let go. */
    int32_t stop(int32_t slot);
    /** CIN_RunCinematic: the frames due at `nowMs` decoded; its status. */
    int32_t run(int32_t slot, int32_t nowMs);
    /** CIN_SetExtents, in the program's screen. */
    void setExtents(int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h);

    /** What CIN_DrawCinematic draws: the slot's frame and where. */
    struct Frame {
        const std::vector<uint8_t> *rgba = nullptr; // cols x rows, rows top first
        int cols = 0;
        int rows = 0;
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
    };
    /** False if the slot has nothing to draw. */
    bool frame(int32_t slot, Frame &out) const;

    /** Every slot let go. */
    void clear();

private:
    struct Slot {
        std::string name; // empty: free
        std::vector<uint8_t> file;
        std::string extension;
        CinematicMovie movie;
        int32_t status = kEnded;
        int32_t start = 0; // ms, at its first frame
        bool loop = false;
        bool hold = false;
        std::array<int32_t, 4> extents{};
        std::vector<uint8_t> rgba; // the last frame decoded
    };
    bool valid(int32_t slot) const { return slot >= 0 && slot < kSlots && _slots[size_t(slot)].status != kEnded; }
    bool restart(Slot &s, int32_t nowMs);

    const SearchSet &_files;
    std::array<Slot, kSlots> _slots;
};

} // namespace reluva::idtech3
