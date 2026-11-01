// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/video.h"

#include <algorithm>
#include <cstring>
#include <deque>

#ifdef RELUVA_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

namespace reluva::video {

#ifdef RELUVA_HAVE_FFMPEG

namespace {

// The file's bytes, read through FFmpeg's I/O callbacks.
struct Memory {
    std::vector<uint8_t> data;
    size_t at = 0;
};

int readMemory(void* opaque, uint8_t* buf, int size) {
    Memory& m = *static_cast<Memory*>(opaque);
    const size_t n = std::min(size_t(size), m.data.size() - m.at);
    if (n == 0)
        return AVERROR_EOF;
    std::memcpy(buf, m.data.data() + m.at, n);
    m.at += n;
    return int(n);
}

int64_t seekMemory(void* opaque, int64_t offset, int whence) {
    Memory& m = *static_cast<Memory*>(opaque);
    if (whence == AVSEEK_SIZE)
        return int64_t(m.data.size());
    int64_t to = offset;
    if ((whence & 3) == SEEK_CUR)
        to += int64_t(m.at);
    else if ((whence & 3) == SEEK_END)
        to += int64_t(m.data.size());
    if (to < 0 || to > int64_t(m.data.size()))
        return -1;
    m.at = size_t(to);
    return to;
}

} // namespace

struct Movie::Impl {
    Memory memory;
    AVIOContext* io = nullptr;
    AVFormatContext* format = nullptr;
    AVCodecContext* video = nullptr;
    SwsContext* scale = nullptr;
    AVFrame* frame = nullptr;
    int stream = -1;
    double timeBase = 0, frameTime = 1.0 / 15;
    std::deque<AVPacket*> packets; // the video's, demuxed at the start
    bool flushed = false;
    double shown = -1; // the time of the picture in `picture`
    image::Image picture;
    audio::Sound sound;

    ~Impl() {
        for (AVPacket* p : packets)
            av_packet_free(&p);
        sws_freeContext(scale);
        av_frame_free(&frame);
        avcodec_free_context(&video);
        avformat_close_input(&format);
        if (io)
            av_freep(&io->buffer);
        avio_context_free(&io);
    }

    // The next picture into `picture`; false at the end.
    bool decodeNext() {
        for (;;) {
            const int got = avcodec_receive_frame(video, frame);
            if (got == 0) {
                toRgba();
                const int64_t pts = frame->best_effort_timestamp;
                shown = pts == AV_NOPTS_VALUE ? shown + frameTime : double(pts) * timeBase;
                return true;
            }
            if (got != AVERROR(EAGAIN))
                return false; // the end, or broken
            if (!packets.empty()) {
                AVPacket* p = packets.front();
                packets.pop_front();
                avcodec_send_packet(video, p);
                av_packet_free(&p);
            } else if (!flushed) {
                flushed = true;
                avcodec_send_packet(video, nullptr);
            } else {
                return false;
            }
        }
    }

    void toRgba() {
        scale = sws_getCachedContext(scale, frame->width, frame->height, AVPixelFormat(frame->format), frame->width,
                                     frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scale)
            return;
        picture.w = frame->width;
        picture.h = frame->height;
        picture.rgba.resize(size_t(picture.w) * picture.h * 4);
        uint8_t* dst[4] = {picture.rgba.data(), nullptr, nullptr, nullptr};
        int stride[4] = {picture.w * 4, 0, 0, 0};
        sws_scale(scale, frame->data, frame->linesize, 0, frame->height, dst, stride);
    }

    // The whole soundtrack at once, to 16-bit samples at its own rate.
    void decodeSound(int audioStream, std::vector<AVPacket*>& audioPackets) {
        const AVCodecParameters* par = format->streams[audioStream]->codecpar;
        const AVCodec* codec = avcodec_find_decoder(par->codec_id);
        AVCodecContext* ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
        SwrContext* swr = nullptr;
        AVFrame* f = av_frame_alloc();
        if (ctx && avcodec_parameters_to_context(ctx, par) >= 0 && avcodec_open2(ctx, codec, nullptr) >= 0) {
            const int channels = std::clamp(ctx->ch_layout.nb_channels, 1, 2);
            AVChannelLayout out;
            av_channel_layout_default(&out, channels);
            if (swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_S16, ctx->sample_rate, &ctx->ch_layout, ctx->sample_fmt,
                                    ctx->sample_rate, 0, nullptr) >= 0 &&
                swr_init(swr) >= 0) {
                sound.rate = ctx->sample_rate;
                sound.channels = channels;
                sound.format = 1;
                auto drain = [&] {
                    while (avcodec_receive_frame(ctx, f) == 0) {
                        const size_t at = sound.samples.size();
                        const int room = swr_get_out_samples(swr, f->nb_samples);
                        sound.samples.resize(at + size_t(std::max(0, room)) * size_t(channels));
                        uint8_t* dst = reinterpret_cast<uint8_t*>(sound.samples.data() + at);
                        const int n =
                            swr_convert(swr, &dst, room, const_cast<const uint8_t**>(f->extended_data), f->nb_samples);
                        sound.samples.resize(at + size_t(std::max(0, n)) * size_t(channels));
                    }
                };
                for (AVPacket* p : audioPackets) {
                    avcodec_send_packet(ctx, p);
                    drain();
                }
                avcodec_send_packet(ctx, nullptr);
                drain();
            }
            av_channel_layout_uninit(&out);
        }
        for (AVPacket*& p : audioPackets)
            av_packet_free(&p);
        swr_free(&swr);
        av_frame_free(&f);
        avcodec_free_context(&ctx);
    }
};

bool Movie::supported() { return true; }

std::unique_ptr<Movie> Movie::open(std::vector<uint8_t> data, std::string& error) {
    auto impl = std::make_unique<Impl>();
    impl->memory.data = std::move(data);
    constexpr int kBuffer = 1 << 16;
    auto* buffer = static_cast<uint8_t*>(av_malloc(kBuffer));
    impl->io =
        buffer ? avio_alloc_context(buffer, kBuffer, 0, &impl->memory, readMemory, nullptr, seekMemory) : nullptr;
    if (!impl->io) {
        av_free(buffer);
        error = "out of memory";
        return nullptr;
    }
    impl->format = avformat_alloc_context();
    impl->format->pb = impl->io;
    if (avformat_open_input(&impl->format, nullptr, nullptr, nullptr) < 0) {
        error = "not a movie FFmpeg reads"; // avformat_open_input freed the context
        return nullptr;
    }
    avformat_find_stream_info(impl->format, nullptr);
    impl->stream = av_find_best_stream(impl->format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audioStream = av_find_best_stream(impl->format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (impl->stream < 0) {
        error = "no pictures";
        return nullptr;
    }
    AVStream* vs = impl->format->streams[impl->stream];
    const AVCodec* codec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!codec) {
        error = std::string("no decoder for ") + avcodec_get_name(vs->codecpar->codec_id);
        return nullptr;
    }
    impl->video = avcodec_alloc_context3(codec);
    if (!impl->video || avcodec_parameters_to_context(impl->video, vs->codecpar) < 0 ||
        avcodec_open2(impl->video, codec, nullptr) < 0) {
        error = "can't open the decoder";
        return nullptr;
    }
    impl->timeBase = av_q2d(vs->time_base);
    if (vs->avg_frame_rate.num > 0)
        impl->frameTime = 1 / av_q2d(vs->avg_frame_rate);
    impl->frame = av_frame_alloc();

    // Read the whole file: the pictures' packets kept for later, the sound
    // decoded now.
    std::vector<AVPacket*> audioPackets;
    for (;;) {
        AVPacket* p = av_packet_alloc();
        if (av_read_frame(impl->format, p) < 0) {
            av_packet_free(&p);
            break;
        }
        if (p->stream_index == impl->stream)
            impl->packets.push_back(p);
        else if (p->stream_index == audioStream)
            audioPackets.push_back(p);
        else
            av_packet_free(&p);
    }
    if (audioStream >= 0)
        impl->decodeSound(audioStream, audioPackets);
    return std::unique_ptr<Movie>(new Movie(std::move(impl)));
}

int Movie::width() const { return impl_->video->width; }
int Movie::height() const { return impl_->video->height; }

double Movie::duration() const {
    return impl_->format->duration > 0 ? double(impl_->format->duration) / AV_TIME_BASE : 0;
}

bool Movie::frameAt(double seconds, image::Image& out) {
    if (impl_->shown < 0 && !impl_->decodeNext())
        return false;
    // On to the last picture due by now.
    while (impl_->shown + impl_->frameTime <= seconds)
        if (!impl_->decodeNext())
            return false;
    out = impl_->picture;
    return true;
}

#else

struct Movie::Impl {
    audio::Sound sound;
};

bool Movie::supported() { return false; }

std::unique_ptr<Movie> Movie::open(std::vector<uint8_t>, std::string& error) {
    error = "Reluva was built without FFmpeg";
    return nullptr;
}

int Movie::width() const { return 0; }
int Movie::height() const { return 0; }
double Movie::duration() const { return 0; }
bool Movie::frameAt(double, image::Image&) { return false; }

#endif

Movie::Movie(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Movie::~Movie() = default;

const audio::Sound& Movie::sound() const { return impl_->sound; }

} // namespace reluva::video
