// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <functional>
#include <memory>

namespace reluva::audio {
class Mixer;
}

namespace reluva::x86 {

/**
 * Movies through MCI: Video for Windows' movie window (MSVFW32.dll's
 * MCIWndRegisterClass, the MCIWndClass window and its messages) and
 * WINMM.dll's mciSendCommandA for AVI files (docs/common/x86/mci.md),
 * written from the Windows SDK's documentation. A movie's frames are
 * decoded by Reluva (src/video/avi.h: Cinepak) and drawn into the screen's
 * pixels (Gdi::screen) where its window is, as its time comes; its sound
 * goes to the mixer. When it ends, MM_MCINOTIFY tells the window the
 * program asked to be told.
 */
class MciVideo {
public:
    /** Stands in for the movie window and MCI in `process`. */
    explicit MciVideo(Win32Process &process);
    ~MciVideo();

    // Where a movie's sound goes; none, and movies play in silence.
    audio::Mixer *mixer = nullptr;
    // Called after a frame is drawn onto the screen, for the engine to
    // show it; from the movie's thread, which takes turns with the
    // program's.
    std::function<void()> frameDrawn;

    /** Movies opened so far, and whether one is playing. */
    ///@{
    int opened() const;
    bool playing() const;
    ///@}

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
