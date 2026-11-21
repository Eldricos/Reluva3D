// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/pages/picture_viewer.h"

#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/pages/draw.h"

#include <cstdio>

namespace reluva::stawayteam {

PictureViewer::PictureViewer(System &sys, Surface &screen, const Archive &data)
    : _sys(sys), _screen(screen), _data(data),
      _files(data, [](const std::string &name) { return name.ends_with(".frm16"); }) {
}

void PictureViewer::handleKey(uint32_t key) {
    if (key == 'b')
        _backdrop = (_backdrop + 1) % 3;
    else if (key == 'z')
        _zoom = (_zoom + 1) % 5;
    else if (key == 'i')
        _info = !_info;
    else if (!_files.handleKey(key))
        return;
    show();
}

void PictureViewer::show() {
    clearPage(_screen);
    fill(_screen, 0, _screen.h - kStatusHeight, _screen.w, kStatusHeight, {40, 40, 48});
    if (_files.empty())
        return;
    std::string status;
    std::vector<uint8_t> data;
    Frame frame;
    if (Error err = _data.read(_files.current(), data)) {
        status = err.message;
    } else if (Error err2 = readFrame(data.data(), data.size(), frame)) {
        status = err2.message;
    } else {
        int zoom = _zoom;
        if (zoom == 0) {
            zoom = 4;
            while (zoom > 1 && (frame.width * zoom > _screen.w || frame.height * zoom > _screen.h - kStatusHeight))
                --zoom;
        }
        const image::Image shown = enlarged(frameImage(frame), zoom);
        const int x = (_screen.w - shown.w) / 2;
        const int y = (_screen.h - kStatusHeight - shown.h) / 2;
        drawBackdrop(_screen, x, y, shown.w, shown.h, _backdrop);
        image::blit(_screen, shown, x, y);
        size_t blended = 0;
        for (uint8_t a : frame.alpha)
            blended += a != 0 && a != 255;
        char buf[160];
        std::snprintf(buf, sizeof buf, "%dx%d, origin %d,%d, flags 0x%x, %zu blended pixels, x%d", frame.width,
                      frame.height, frame.originX, frame.originY, frame.flags, blended, zoom);
        status = buf;
    }
    // The status line goes over the bottom of the page; I hides it.
    if (_info)
        drawStatus(_screen, _files.position(), status);
    _sys.setWindowTitle("Reluva - Star Trek: Away Team - pictures (Left/Right, F: next folder, B: background, Z: "
                        "zoom, I: info, Tab: next page, Esc: leave)");
}

} // namespace reluva::stawayteam
