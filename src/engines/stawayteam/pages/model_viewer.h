// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/pages/animation.h"
#include "engines/stawayteam/pages/file_list.h"
#include "engines/stawayteam/pages/page.h"

namespace reluva::stawayteam {

/**
 * The data viewer's page of models: every .mdl16 in DATA.dat, one of its
 * rotation sequences at a time, facing one way. The file keys of FileList;
 * Up/Down choose the sequence, , and . turn it, Space pauses, [ and ] step
 * a frame, B changes the background, Z the zoom.
 */
class ModelViewer : public Page {
public:
    /** `sys`, `screen` and `data` must outlive it. */
    ModelViewer(System &sys, Surface &screen, const Archive &data);

    size_t count() const override { return _files.size(); }
    void activate() override;
    void handleKey(uint32_t key) override;
    void update(uint64_t nowMs) override;

private:
    void load();
    void choose();
    void draw();

    System &_sys;
    Surface &_screen;
    const Archive &_data;
    FileList _files;
    BinaryObject _model;
    std::string _error;
    std::vector<size_t> _sequences; // the model's sequences that it has
    size_t _sequence = 0;           // which of them is shown
    int _rotations = 1;
    int _rotation = 0;
    Animation _animation;
    int _backdrop = 1;
    int _zoom = 0; // 0: as large as fits, up to 4 times
};

} // namespace reluva::stawayteam
