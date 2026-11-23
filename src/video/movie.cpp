// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "video/movie.h"
#include "video/avi.h"
#include "video/bink.h"
#include "common/stream.h"
#include "video/indeo5.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>

namespace reluva::video {

namespace {

// A movie's picture shown and its sound played until both end or a key or
// click skips it. `decodeTo(due)` decodes frames up to (not including)
// `due` and shows the latest, if any came; the picture follows the sound
// while it plays, else the clock.
Error runMovie(System &sys, audio::Mixer *mixer, const std::shared_ptr<const audio::Sound> &pcm, size_t frameCount,
               double framesPerSecond, bool &quit, const std::function<size_t(size_t due)> &decodeTo) {
    audio::SoundHandle sound = audio::kNoSound;
    if (mixer && pcm)
        sound = mixer->play(pcm, audio::SoundType::kMusic);
    auto stop = [&] {
        if (sound)
            mixer->stop(sound);
    };
    const uint64_t start = sys.getMillis();
    // What was already waiting (the launcher's click or key, still queued
    // as the movie starts) mustn't skip it: only a press from here on.
    {
        Event ev;
        while (sys.pollEvent(ev))
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) {
                quit = true;
                stop();
                return Error::success();
            }
    }
    size_t next = 0; // the next frame to decode
    while (true) {
        Event ev;
        while (sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) {
                quit = true;
                stop();
                return Error::success();
            }
            // A press skips it, though not in its first half second (one
            // queued behind the start, or a double click's second).
            if (((ev.type == EventType::kKeyDown && !ev.repeat) || ev.type == EventType::kMouseDown) &&
                sys.getMillis() - start >= 500) {
                stop();
                return Error::success();
            }
        }
        double seconds = double(sys.getMillis() - start) / 1000;
        if (sound && mixer->isPlaying(sound))
            seconds = mixer->position(sound) / pcm->rate;
        const size_t due = std::min(frameCount, size_t(seconds * framesPerSecond) + 1);
        if (next < due)
            next = decodeTo(due);
        const bool soundDone = !sound || !mixer->isPlaying(sound);
        if (next >= frameCount && soundDone)
            return Error::success();
        sys.delayMillis(5);
    }
}

// The screen a movie plays on, the picture in its middle on black.
struct Screen {
    System &sys;
    Surface screen;
    int x0 = 0;
    int y0 = 0;
    Error open(int width, int height, const MovieOptions &options) {
        const int w = options.screenWidth > 0 ? options.screenWidth : width;
        const int h = options.screenHeight > 0 ? options.screenHeight : height;
        // The picture in the screen's middle, cut to it if it's larger.
        x0 = std::max(0, (w - width) / 2);
        y0 = std::max(0, (h - height) / 2);
        screen.create(w, h, PixelFormat::kRGBA32);
        if (Error err = sys.initGraphics(w, h, PixelFormat::kRGBA32))
            return err;
        sys.updateScreen(screen);
        return Error::success();
    }
    void show(const uint8_t *rgba, int width, int height, size_t pitch) {
        const int w = std::min(width, screen.w - x0);
        for (int y = 0; y < height && y0 + y < screen.h; ++y)
            std::memcpy(screen.row(y0 + y) + size_t(x0) * 4, rgba + size_t(y) * pitch, size_t(w) * 4);
        sys.updateScreen(screen);
    }
};

Error playBink(System &sys, audio::Mixer *mixer, std::vector<uint8_t> data, const std::string &name, bool &quit,
               const MovieOptions &options) {
    bink::Movie movie;
    if (Error err = movie.open(std::move(data)))
        return Error::fail(name + ": " + err.message);
    if (movie.width() <= 0 || movie.height() <= 0 || movie.rateNumerator() == 0 || movie.rateDenominator() == 0)
        return Error::fail(name + ": no picture size or rate");
    Screen screen{sys, {}};
    if (Error err = screen.open(movie.width(), movie.height(), options))
        return err;
    // Its first audio track whole, ahead of the frames, as the AVI path has
    // its soundtrack.
    std::shared_ptr<const audio::Sound> pcm;
    if (mixer && movie.audioRate() > 0) {
        auto track = std::make_shared<audio::Sound>();
        track->rate = movie.audioRate();
        track->channels = movie.audioChannels();
        track->format = 1;
        if (!movie.decodeAllAudio(track->samples) && !track->samples.empty())
            pcm = std::move(track);
    }
    if (!pcm && mixer && options.soundtrack && options.soundtrack->rate > 0)
        pcm = options.soundtrack;
    std::vector<uint8_t> rgba;
    size_t next = 0;
    return runMovie(sys, mixer, pcm, movie.frameCount(),
                    double(movie.rateNumerator()) / double(movie.rateDenominator()), quit, [&](size_t due) {
                        // Every frame is decoded (each builds on the last);
                        // only the latest is shown.
                        bool more = true;
                        while (next < due && more)
                            if (movie.nextFrame(more) || !more)
                                break;
                            else
                                ++next;
                        if (next < due) // a bad frame or the end: nothing more to show
                            return size_t(movie.frameCount());
                        movie.toRgba(rgba);
                        screen.show(rgba.data(), movie.width(), movie.height(), size_t(movie.width()) * 4);
                        return next;
                    });
}

Error playAvi(System &sys, audio::Mixer *mixer, std::vector<uint8_t> data, const std::string &name, bool &quit,
              const MovieOptions &options) {
    avi::Decoder avi;
    if (!avi.load(std::make_unique<MemoryReadStream>(std::move(data))))
        return Error::fail(name + ": not an AVI with video");
    const bool indeo = avi.videoCodec() == avi::fourcc("IV50") || avi.videoCodec() == avi::fourcc("iv50");
    if (!indeo && !avi.canDecode())
        return Error::fail(name + ": not Indeo 5 or Cinepak video");
    if (avi.width() <= 0 || avi.height() <= 0 || avi.frameMicros() == 0)
        return Error::fail(name + ": no picture size or rate");
    Screen screen{sys, {}};
    if (Error err = screen.open(avi.width(), avi.height(), options))
        return err;
    Surface picture;
    picture.create(avi.width(), avi.height(), PixelFormat::kRGBA32);
    std::shared_ptr<const audio::Sound> pcm;
    if (mixer && avi.hasAudio() && avi.audioBits() == 16 && avi.audioChannels() >= 1 && avi.audioChannels() <= 2) {
        // The PCM soundtrack, 16-bit little-endian, whole frames.
        auto track = std::make_shared<audio::Sound>();
        track->rate = avi.audioRate();
        track->channels = avi.audioChannels();
        track->format = 1;
        const std::vector<uint8_t> &bytes = avi.audio();
        track->samples.resize(bytes.size() / 2 / size_t(track->channels) * size_t(track->channels));
        std::memcpy(track->samples.data(), bytes.data(), track->samples.size() * 2);
        pcm = std::move(track);
    } else if (mixer && options.soundtrack && options.soundtrack->rate > 0) {
        pcm = options.soundtrack;
    }
    Indeo5Decoder decoder;
    size_t next = 0;
    return runMovie(sys, mixer, pcm, size_t(avi.frameCount()), 1e6 / double(avi.frameMicros()), quit, [&](size_t due) {
        // Every frame is decoded (each builds on the last);
        // only the latest is shown.
        bool shown = false;
        std::vector<uint8_t> frame;
        while (next < due) {
            if (!indeo) {
                ++next;
                // Cinepak: the decoder keeps the picture
                shown = avi.decodeNextFrame() || shown;
                continue;
            }
            if (!avi.readFrame(int(next++), frame) || frame.empty())
                continue; // none (a repeat), or unreadable: the picture stays
            if (Error err = decoder.decode(frame.data(), frame.size()))
                continue; // a bad frame: the picture stays until the next key frame mends it
            shown = true;
        }
        if (shown && !indeo) {
            const Surface &s = avi.surface();
            screen.show(s.pixels.data(), s.w, s.h, s.pitch);
        } else if (shown && decoder.width() == avi.width() && decoder.height() == avi.height()) {
            decoder.rgba(picture.pixels.data(), picture.pitch);
            screen.show(picture.pixels.data(), picture.w, picture.h, picture.pitch);
        }
        return next;
    });
}

} // namespace

Error playMovie(System &sys, audio::Mixer *mixer, const std::filesystem::path &file, bool &quit,
                const MovieOptions &options) {
    quit = false;
    std::vector<uint8_t> data;
    if (!readFile(file, data))
        return Error::fail("can't open " + file.string());
    return playMovie(sys, mixer, std::move(data), file.filename().string(), quit, options);
}

Error playMovie(System &sys, audio::Mixer *mixer, std::vector<uint8_t> data, const std::string &name, bool &quit,
                const MovieOptions &options) {
    quit = false;
    if (data.size() >= 3 && data[0] == 'B' && data[1] == 'I' && data[2] == 'K')
        return playBink(sys, mixer, std::move(data), name, quit, options);
    return playAvi(sys, mixer, std::move(data), name, quit, options);
}

} // namespace reluva::video
