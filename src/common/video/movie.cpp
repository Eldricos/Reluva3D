// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/video/movie.h"
#include "common/video/avi.h"
#include "common/video/indeo5.h"

#include <fstream>
#include <iterator>

namespace reluva::video {

Error playMovie(System& sys, audio::Mixer* mixer, const std::filesystem::path& file, bool& quit) {
    quit = false;
    std::vector<uint8_t> data;
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return Error::fail("can't open " + file.string());
        data.assign(std::istreambuf_iterator<char>(in), {});
    }
    AviFile avi;
    if (Error err = avi.parse(data))
        return err;
    if (avi.videoCodec != fourcc("IV50") && avi.videoCodec != fourcc("iv50"))
        return Error::fail(file.filename().string() + ": not Indeo 5 video");
    if (avi.width <= 0 || avi.height <= 0 || avi.framesPerSecond <= 0)
        return Error::fail(file.filename().string() + ": no picture size or rate");

    Surface screen;
    screen.create(avi.width, avi.height, PixelFormat::RGBA32);
    if (Error err = sys.initGraphics(avi.width, avi.height, PixelFormat::RGBA32))
        return err;
    sys.updateScreen(screen);

    // The picture keeps to the sound's position while it plays, else to
    // the clock.
    audio::SoundHandle sound = audio::kNoSound;
    std::shared_ptr<audio::Sound> pcm = mixer ? avi.sound(data) : nullptr;
    if (pcm)
        sound = mixer->play(pcm, audio::SoundType::Music);
    const uint64_t start = sys.getMillis();
    Indeo5Decoder decoder;
    size_t next = 0; // the next frame to decode
    while (true) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher) {
                quit = true;
                if (sound)
                    mixer->stop(sound);
                return Error::success();
            }
            if ((ev.type == EventType::KeyDown && !ev.repeat) || ev.type == EventType::MouseDown) {
                if (sound)
                    mixer->stop(sound);
                return Error::success();
            }
        }
        double seconds = double(sys.getMillis() - start) / 1000;
        if (sound && mixer->isPlaying(sound))
            seconds = mixer->position(sound) / pcm->rate;
        const size_t due = std::min(avi.frames.size(), size_t(seconds * avi.framesPerSecond) + 1);
        // Every frame is decoded (each builds on the last); only the
        // latest is shown.
        bool shown = false;
        while (next < due) {
            const AviFile::Chunk& c = avi.frames[next++];
            if (Error err = decoder.decode(data.data() + c.offset, c.size))
                continue; // a bad frame: the picture stays until the next key frame mends it
            shown = true;
        }
        if (shown && decoder.width() == avi.width && decoder.height() == avi.height) {
            decoder.rgba(screen.pixels.data(), screen.pitch);
            sys.updateScreen(screen);
        }
        const bool soundDone = !sound || !mixer->isPlaying(sound);
        if (next >= avi.frames.size() && soundDone)
            return Error::success();
        sys.delayMillis(5);
    }
}

} // namespace reluva::video
