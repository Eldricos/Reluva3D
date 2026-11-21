// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Star Trek: Away Team's data viewer, in reluva-viewer: the pictures in
// DATA.dat (picture_viewer.h has the keys). Esc leaves.

#include "common/fs.h"
#include "common/zip.h"
#include "engines/stawayteam/pages/picture_viewer.h"
#include "engines/viewer.h"

#include <utility>

namespace reluva {

namespace {

using stawayteam::PictureViewer;

class StawayteamViewer : public Engine {
public:
    StawayteamViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        const auto path = fs::findFile(_target.path, "DATA.dat");
        if (!path)
            return Error::fail("No DATA.dat in " + _target.path.string());
        if (Error err = _data.open(*path))
            return err;
        // The game's own screen size.
        constexpr int kWidth = 640;
        constexpr int kHeight = 480;
        if (Error err = _sys.initGraphics(kWidth, kHeight, PixelFormat::kRGBA32))
            return err;
        _screen.create(kWidth, kHeight, PixelFormat::kRGBA32);
        PictureViewer pictures(_sys, _screen, _data);
        if (pictures.count() == 0)
            return Error::fail("No pictures in " + path->string());
        pictures.activate();
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                if (ev.type != EventType::kKeyDown)
                    continue;
                if (ev.key == key::kEscape)
                    return Error::success();
                pictures.handleKey(ev.key);
            }
            _sys.updateScreen(_screen);
            _sys.delayMillis(10);
        }
    }

private:
    System &_sys;
    GameTarget _target;
    Surface _screen;
    ZipArchive _data;
};

class StawayteamMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "stawayteam"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<StawayteamViewer>(system, target);
    }
};

} // namespace

const MetaViewer &stawayteamMetaViewer() {
    static const StawayteamMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
