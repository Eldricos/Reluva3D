// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WINMM's MIDI out, as Microsoft documents it on a machine with no MIDI
// device: none to list or open, so no stream or header handle is ever
// valid. A program then plays its music some other way, or without it.

#include "emulation/x86/win32.h"

namespace reluva::x86 {

namespace {

constexpr uint32_t kBadDeviceId = 2;   // MMSYSERR_BADDEVICEID
constexpr uint32_t kInvalidHandle = 5; // MMSYSERR_INVALHANDLE

} // namespace

void Win32Process::addMidiStandIns() {
    const char *w = "WINMM.dll";
    stdcall(w, "midiOutGetNumDevs", 0, [](Cpu &) { return 0u; });
    stdcall(w, "midiOutGetDevCapsA", 3, [](Cpu &) { return kBadDeviceId; });
    stdcall(w, "midiStreamOpen", 6, [](Cpu &c) {
        if (c.valid(c.arg(0), 4))
            c.write32(c.arg(0), 0);
        return kBadDeviceId;
    });
    for (const auto &[name, args] : {std::pair{"midiStreamClose", 1},
                                     {"midiStreamOut", 3},
                                     {"midiStreamRestart", 1},
                                     {"midiStreamStop", 1},
                                     {"midiStreamProperty", 3},
                                     {"midiOutPrepareHeader", 3},
                                     {"midiOutUnprepareHeader", 3},
                                     {"midiOutSetVolume", 2},
                                     {"midiOutGetVolume", 2}})
        stdcall(w, name, args, [](Cpu &) { return kInvalidHandle; });
}

} // namespace reluva::x86
