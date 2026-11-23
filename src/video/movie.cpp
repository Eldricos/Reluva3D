// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "video/movie.h"

#include "common/stream.h"
#include "video/avi.h"
#include "video/ea_movie.h"
#include "video/indeo5.h"
#include "video/tqi.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>

namespace reluva::video {

namespace {

// What the player needs of a movie, whatever its kind.
struct Source {
    int width = 0;
    int height = 0;
    double framesPerSecond = 0;
    size_t frameCount = 0;
    std::shared_ptr<const audio::Sound> sound;
    // Decodes the next frame (each builds on the last, so all of them, in
    // order); true when there's a new picture.
    std::function<bool()> next;
    std::function<const Surface &()> picture;
};

// An AVI's picture and its 16-bit PCM sound.
Error openAvi(std::vector<uint8_t> data, const std::string &name, Source &out) {
    auto avi = std::make_shared<avi::Decoder>();
    if (!avi->load(std::make_unique<MemoryReadStream>(std::move(data))))
        return Error::fail(name + ": not an AVI with video");
    const bool indeo = avi->videoCodec() == avi::fourcc("IV50") || avi->videoCodec() == avi::fourcc("iv50");
    if (!indeo && !avi->canDecode())
        return Error::fail(name + ": not Indeo 5 or Cinepak video");
    if (avi->width() <= 0 || avi->height() <= 0 || avi->frameMicros() == 0)
        return Error::fail(name + ": no picture size or rate");
    out.width = avi->width();
    out.height = avi->height();
    out.framesPerSecond = 1e6 / double(avi->frameMicros());
    out.frameCount = size_t(avi->frameCount());
    if (avi->hasAudio() && avi->audioBits() == 16 && avi->audioChannels() >= 1 && avi->audioChannels() <= 2) {
        // The PCM soundtrack, 16-bit little-endian, whole frames.
        auto track = std::make_shared<audio::Sound>();
        track->rate = avi->audioRate();
        track->channels = avi->audioChannels();
        track->format = 1;
        const std::vector<uint8_t> &bytes = avi->audio();
        track->samples.resize(bytes.size() / 2 / size_t(track->channels) * size_t(track->channels));
        std::memcpy(track->samples.data(), bytes.data(), track->samples.size() * 2);
        out.sound = std::move(track);
    }
    if (!indeo) {
        // Cinepak: the decoder keeps the picture.
        out.next = [avi] { return avi->decodeNextFrame(); };
        out.picture = [avi]() -> const Surface & { return avi->surface(); };
        return Error::success();
    }
    auto decoder = std::make_shared<Indeo5Decoder>();
    auto picture = std::make_shared<Surface>();
    picture->create(avi->width(), avi->height(), PixelFormat::kRGBA32);
    auto index = std::make_shared<int>(0);
    out.next = [avi, decoder, picture, index] {
        std::vector<uint8_t> frame;
        // None (a repeat), unreadable or bad: the picture stays, until the
        // next key frame mends it.
        if (!avi->readFrame((*index)++, frame) || frame.empty() || decoder->decode(frame.data(), frame.size()))
            return false;
        if (decoder->width() != picture->w || decoder->height() != picture->h)
            return false;
        decoder->rgba(picture->pixels.data(), picture->pitch);
        return true;
    };
    out.picture = [picture]() -> const Surface & { return *picture; };
    return Error::success();
}

// An EA movie's TQI pictures and its stream's sound, the segments joined.
Error openEa(std::vector<uint8_t> data, const std::string &name, Source &out) {
    auto movie = std::make_shared<EaMovie>();
    if (Error err = movie->open(std::move(data)))
        return Error::fail(name + ": " + err.message);
    auto decoder = std::make_shared<tqi::Decoder>();
    if (Error err = decoder->decode(movie->frame(0), movie->frameSize(0)))
        return Error::fail(name + ": " + err.message);
    out.width = decoder->width();
    out.height = decoder->height();
    out.framesPerSecond = EaMovie::kFramesPerSecond;
    out.frameCount = movie->frameCount();
    auto track = std::make_shared<audio::Sound>();
    for (size_t s = 0; s < movie->sound().segments().size(); ++s) {
        audio::Sound segment;
        if (movie->sound().decode(s, segment) ||
            (s > 0 && (segment.rate != track->rate || segment.channels != track->channels)))
            break; // the sound as far as it can be played
        track->rate = segment.rate;
        track->channels = segment.channels;
        track->format = segment.format;
        track->samples.insert(track->samples.end(), segment.samples.begin(), segment.samples.end());
    }
    if (!track->samples.empty())
        out.sound = std::move(track);
    auto index = std::make_shared<size_t>(0);
    out.next = [movie, decoder, index] {
        const size_t i = (*index)++;
        // The first frame is decoded already; a bad one keeps the last.
        return i == 0 || (i < movie->frameCount() && !decoder->decode(movie->frame(i), movie->frameSize(i)));
    };
    out.picture = [decoder]() -> const Surface & { return decoder->surface(); };
    return Error::success();
}

} // namespace

Error playMovie(System &sys, audio::Mixer *mixer, const std::filesystem::path &file, bool &quit,
                const MovieOptions &options) {
    quit = false;
    std::vector<uint8_t> data;
    if (!readFile(file, data))
        return Error::fail("can't open " + file.string());
    Source movie;
    const std::string name = file.filename().string();
    if (Error err =
            EaMovie::looksLike(data) ? openEa(std::move(data), name, movie) : openAvi(std::move(data), name, movie))
        return err;

    // Shown at its own size, or stretched to the size asked for.
    const int shownW = options.pictureWidth > 0 ? options.pictureWidth : movie.width;
    const int shownH = options.pictureHeight > 0 ? options.pictureHeight : movie.height;
    const int screenW = options.screenWidth > 0 ? options.screenWidth : shownW;
    const int screenH = options.screenHeight > 0 ? options.screenHeight : shownH;
    // The picture in the screen's middle, cut to it if it's larger.
    const int x0 = std::max(0, (screenW - shownW) / 2);
    const int y0 = std::max(0, (screenH - shownH) / 2);
    Surface screen;
    screen.create(screenW, screenH, PixelFormat::kRGBA32);
    if (Error err = sys.initGraphics(screenW, screenH, PixelFormat::kRGBA32))
        return err;
    sys.updateScreen(screen);
    auto show = [&](const Surface &from) {
        const int w = std::min(shownW, screenW - x0);
        for (int y = 0; y < shownH && y0 + y < screenH; ++y) {
            const uint8_t *src = from.row(std::min(from.h - 1, y * from.h / shownH));
            uint8_t *dst = screen.row(y0 + y) + size_t(x0) * 4;
            if (shownW == from.w)
                std::memcpy(dst, src, size_t(w) * 4);
            else
                for (int x = 0; x < w; ++x)
                    std::memcpy(dst + x * 4, src + (x * from.w / shownW) * 4, 4);
        }
        sys.updateScreen(screen);
    };

    // The picture keeps to the sound's position while it plays, else to
    // the clock.
    audio::SoundHandle sound = audio::kNoSound;
    std::shared_ptr<const audio::Sound> pcm = movie.sound;
    if (!pcm && options.soundtrack && options.soundtrack->rate > 0)
        pcm = options.soundtrack;
    if (mixer && pcm)
        sound = mixer->play(pcm, audio::SoundType::kMusic);
    const uint64_t start = sys.getMillis();
    auto stop = [&] {
        if (sound)
            mixer->stop(sound);
    };
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
        const size_t due = std::min(movie.frameCount, size_t(seconds * movie.framesPerSecond) + 1);
        // Every frame is decoded; only the latest is shown.
        bool shown = false;
        while (next < due) {
            ++next;
            shown = movie.next() || shown;
        }
        if (shown)
            show(movie.picture());
        const bool soundDone = !sound || !mixer->isPlaying(sound);
        if (next >= movie.frameCount && soundDone)
            return Error::success();
        sys.delayMillis(5);
    }
}

} // namespace reluva::video
