// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "video/movie.h"
#include "video/avi.h"
#include "common/stream.h"
#include "video/indeo5.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace reluva::video {

Error playMovie(System &sys, audio::Mixer *mixer, const std::filesystem::path &file, bool &quit) {
    quit = false;
    std::vector<uint8_t> data;
    if (!readFile(file, data))
        return Error::fail("can't open " + file.string());
    avi::Decoder avi;
    if (!avi.load(std::make_unique<MemoryReadStream>(std::move(data))))
        return Error::fail(file.filename().string() + ": not an AVI with video");
    if (avi.videoCodec() != avi::fourcc("IV50") && avi.videoCodec() != avi::fourcc("iv50"))
        return Error::fail(file.filename().string() + ": not Indeo 5 video");
    if (avi.width() <= 0 || avi.height() <= 0 || avi.frameMicros() == 0)
        return Error::fail(file.filename().string() + ": no picture size or rate");
    const double framesPerSecond = 1e6 / double(avi.frameMicros());

    Surface screen;
    screen.create(avi.width(), avi.height(), PixelFormat::RGBA32);
    if (Error err = sys.initGraphics(avi.width(), avi.height(), PixelFormat::RGBA32))
        return err;
    sys.updateScreen(screen);

    // The picture keeps to the sound's position while it plays, else to
    // the clock.
    audio::SoundHandle sound = audio::kNoSound;
    std::shared_ptr<audio::Sound> pcm;
    if (mixer && avi.hasAudio() && avi.audioBits() == 16 && avi.audioChannels() >= 1 && avi.audioChannels() <= 2) {
        // The PCM soundtrack, 16-bit little-endian, whole frames.
        pcm = std::make_shared<audio::Sound>();
        pcm->rate = avi.audioRate();
        pcm->channels = avi.audioChannels();
        pcm->format = 1;
        const std::vector<uint8_t> &bytes = avi.audio();
        pcm->samples.resize(bytes.size() / 2 / size_t(pcm->channels) * size_t(pcm->channels));
        std::memcpy(pcm->samples.data(), bytes.data(), pcm->samples.size() * 2);
    }
    if (pcm)
        sound = mixer->play(pcm, audio::SoundType::Music);
    const uint64_t start = sys.getMillis();
    // What was already waiting (the launcher's click or key, still queued
    // as the movie starts) mustn't skip it: only a press from here on.
    {
        Event ev;
        while (sys.pollEvent(ev))
            if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher) {
                quit = true;
                if (sound)
                    mixer->stop(sound);
                return Error::success();
            }
    }
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
            // A press skips it, though not in its first half second (one
            // queued behind the start, or a double click's second).
            if (((ev.type == EventType::KeyDown && !ev.repeat) || ev.type == EventType::MouseDown) &&
                sys.getMillis() - start >= 500) {
                if (sound)
                    mixer->stop(sound);
                return Error::success();
            }
        }
        double seconds = double(sys.getMillis() - start) / 1000;
        if (sound && mixer->isPlaying(sound))
            seconds = mixer->position(sound) / pcm->rate;
        const size_t frameCount = size_t(avi.frameCount());
        const size_t due = std::min(frameCount, size_t(seconds * framesPerSecond) + 1);
        // Every frame is decoded (each builds on the last); only the
        // latest is shown.
        bool shown = false;
        std::vector<uint8_t> frame;
        while (next < due) {
            if (!avi.readFrame(int(next++), frame) || frame.empty())
                continue; // none (a repeat), or unreadable: the picture stays
            if (Error err = decoder.decode(frame.data(), frame.size()))
                continue; // a bad frame: the picture stays until the next key frame mends it
            shown = true;
        }
        if (shown && decoder.width() == avi.width() && decoder.height() == avi.height()) {
            decoder.rgba(screen.pixels.data(), screen.pitch);
            sys.updateScreen(screen);
        }
        const bool soundDone = !sound || !mixer->isPlaying(sound);
        if (next >= frameCount && soundDone)
            return Error::success();
        sys.delayMillis(5);
    }
}

} // namespace reluva::video
