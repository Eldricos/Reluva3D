// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island's unit tests: SI files (docs/engines/legoisland/formats/si.md),
// read from one built here, laid out as the CD's are.

#include "common/bytes.h"
#include "common/stream.h"
#include "engines/legoisland/formats/si.h"

#include "tests/support/test.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::legoisland;

namespace {

using Bytes = std::vector<uint8_t>;

constexpr uint32_t kBuffer = 512;

void put16(Bytes &b, uint16_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}

void put32(Bytes &b, uint32_t v) {
    put16(b, uint16_t(v));
    put16(b, uint16_t(v >> 16));
}

void putStr(Bytes &b, const std::string &s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}

void putTag(Bytes &b, const char *tag) {
    b.insert(b.end(), tag, tag + 4);
}

void putDouble(Bytes &b, double v) {
    uint8_t raw[8];
    std::memcpy(raw, &v, 8);
    b.insert(b.end(), raw, raw + 8);
}

// A chunk: its tag, size and payload, padded to an even size.
Bytes chunk(const char *tag, const Bytes &payload) {
    Bytes b;
    putTag(b, tag);
    put32(b, uint32_t(payload.size()));
    b.insert(b.end(), payload.begin(), payload.end());
    if (payload.size() & 1)
        b.push_back(0);
    return b;
}

Bytes list(const char *type, const Bytes &contents) {
    Bytes payload;
    putTag(payload, type);
    payload.insert(payload.end(), contents.begin(), contents.end());
    return chunk("LIST", payload);
}

// An MxOb record's fields up to its extra string.
Bytes record(uint16_t type, const std::string &presenter, const std::string &name, uint32_t id,
             const std::string &extra, double x = 0) {
    Bytes b;
    put16(b, type);
    putStr(b, presenter);
    put32(b, 0);
    putStr(b, name);
    put32(b, id);
    put32(b, 0x22);
    put32(b, 0);
    put32(b, uint32_t(-1));
    put32(b, 1);
    for (double v : {x, 2.0, 3.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0})
        putDouble(b, v);
    put16(b, uint16_t(extra.empty() ? 0 : extra.size() + 1));
    if (!extra.empty())
        putStr(b, extra);
    return b;
}

Bytes medium(Bytes rec, const std::string &file, uint32_t rate, const char *format, bool sound) {
    putStr(rec, file);
    put32(rec, 0);
    put32(rec, 0);
    put32(rec, rate);
    putTag(rec, format);
    put32(rec, 1);
    put32(rec, 0);
    if (sound)
        put32(rec, 79);
    return chunk("MxOb", rec);
}

Bytes mxch(uint16_t flags, uint32_t id, int32_t time, uint32_t remaining, const Bytes &data) {
    Bytes p;
    put16(p, flags);
    put32(p, id);
    put32(p, uint32_t(time));
    put32(p, remaining);
    p.insert(p.end(), data.begin(), data.end());
    return chunk("MxCh", p);
}

// The file's chunks laid out in buffers of kBuffer bytes, as the CD's are:
// a piece that would cross a buffer split in two, and filler where less than
// a chunk header is left.
class DataWriter {
public:
    explicit DataWriter(size_t start) : _pos(start) {}

    void add(uint16_t flags, uint32_t id, int32_t time, const Bytes &data) {
        size_t done = 0;
        do {
            fill();
            const size_t room = kBuffer - _pos % kBuffer - 8 - 14;
            const size_t n = std::min(room, data.size() - done);
            const bool split = n < data.size() - done || done > 0;
            const Bytes part(data.begin() + std::ptrdiff_t(done), data.begin() + std::ptrdiff_t(done + n));
            append(mxch(uint16_t(flags | (split ? SiChunk::kSplit : 0)), id, time, uint32_t(data.size() - done), part));
            done += n;
        } while (done < data.size());
    }
    void end(uint32_t id, int32_t time) {
        fill();
        append(mxch(SiChunk::kEnd, id, time, 0, {}));
    }
    // A pad chunk leaving `n` bytes (less than a header) in this buffer.
    void leave(size_t n) {
        size_t left = kBuffer - _pos % kBuffer;
        if (left < n + 8)
            left += kBuffer;
        append(chunk("pad ", Bytes(left - n - 8, 0)));
    }
    const Bytes &bytes() const { return _out; }

private:
    // The rest of a buffer too small for a chunk with data: a pad chunk,
    // or bytes of filler where even its header won't fit.
    void fill() {
        const size_t left = kBuffer - _pos % kBuffer;
        if (left >= 8 + 14 + 2)
            return;
        if (left >= 8)
            append(chunk("pad ", Bytes(left - 8, 0)));
        else
            append(Bytes(left, 0xAC));
    }
    void append(const Bytes &b) {
        _out.insert(_out.end(), b.begin(), b.end());
        _pos += b.size();
    }

    size_t _pos;
    Bytes _out;
};

struct BuiltSi {
    Bytes file;
    Bytes pixels;
    Bytes samples;
};

// A world with a still, a sound, a select of two FLCs; the still's pixels
// split across buffers.
BuiltSi buildSi() {
    BuiltSi built;
    for (int i = 0; i < 4 * 900; i++)
        built.pixels.push_back(uint8_t(i * 7));
    for (int i = 0; i < 300; i++)
        built.samples.push_back(uint8_t(i));

    Bytes selectPayload = record(uint16_t(SiType::kSelect), "", "Pick", 4, "");
    Bytes choices;
    putTag(choices, "MxCh");
    putStr(choices, "RANDOM_2");
    put32(choices, 2);
    putStr(choices, "0");
    putStr(choices, "1");
    for (uint32_t id : {5u, 6u}) {
        const Bytes flc = medium(record(uint16_t(SiType::kAnimation), "", "Flc" + std::to_string(id), id, ""),
                                 "q:\\a.flc", 10, " FLC", false);
        choices.insert(choices.end(), flc.begin(), flc.end());
    }
    selectPayload.insert(selectPayload.end(), {'L', 'I', 'S', 'T'});
    put32(selectPayload, uint32_t(choices.size()));
    selectPayload.insert(selectPayload.end(), choices.begin(), choices.end());
    const Bytes select = chunk("MxOb", selectPayload);

    Bytes kids;
    putTag(kids, "MxCh");
    put32(kids, 3);
    const Bytes still =
        medium(record(uint16_t(SiType::kStill), "", "Back", 1, "World:current", 145.0), "q:\\b.bmp", 1, " STL", false);
    const Bytes sound = medium(record(uint16_t(SiType::kSound), "", "Voice", 2, ""), "q:\\v.wav", 1, " WAV", true);
    kids.insert(kids.end(), still.begin(), still.end());
    kids.insert(kids.end(), sound.begin(), sound.end());
    kids.insert(kids.end(), select.begin(), select.end());
    Bytes rootPayload = record(uint16_t(SiType::kParallel), "LegoWorldPresenter", "_StartUp", 0, "Object:Test");
    rootPayload.insert(rootPayload.end(), {'L', 'I', 'S', 'T'});
    put32(rootPayload, uint32_t(kids.size()));
    rootPayload.insert(rootPayload.end(), kids.begin(), kids.end());
    const Bytes root = chunk("MxOb", rootPayload);

    Bytes hd;
    put16(hd, 2);
    put16(hd, 2);
    put32(hd, kBuffer);
    put32(hd, 4);
    Bytes of;
    put32(of, 7);
    put32(of, 0x50);
    put32(of, 0);
    // RIFF+OMNI (12), MxHd (20), MxOf (20), LIST+MxSt (12), MxSt (8), the
    // root, LIST+MxDa (12): where the chunks start.
    const size_t dataStart = 12 + 20 + 20 + 12 + 8 + root.size() + 12;

    Bytes bmpHeader;
    put32(bmpHeader, 40);
    put32(bmpHeader, 60);
    put32(bmpHeader, 60);
    put16(bmpHeader, 1);
    put16(bmpHeader, 8);
    for (int i = 0; i < 6; i++)
        put32(bmpHeader, 0);
    bmpHeader.resize(40 + 4 * 256, 0x11);
    Bytes wavHeader;
    for (uint16_t v : {1, 1})
        put16(wavHeader, v);
    put32(wavHeader, 11025);
    put32(wavHeader, 11025);
    put16(wavHeader, 1);
    put16(wavHeader, 8);
    put32(wavHeader, 302);
    put32(wavHeader, 44);

    DataWriter w(dataStart);
    w.add(0, 1, -1, bmpHeader);
    w.add(0, 2, -1, wavHeader);
    w.add(0, 1, 0, built.pixels);
    w.add(0, 2, 0, Bytes(built.samples.begin(), built.samples.begin() + 200));
    w.leave(4);
    w.add(0, 2, 1000, Bytes(built.samples.begin() + 200, built.samples.end()));
    w.end(2, 1100);
    w.end(1, 2000);
    w.end(0, 2000);

    Bytes st = root;
    st.insert(st.end(), {'L', 'I', 'S', 'T'});
    put32(st, uint32_t(4 + w.bytes().size()));
    st.insert(st.end(), {'M', 'x', 'D', 'a'});
    st.insert(st.end(), w.bytes().begin(), w.bytes().end());
    const Bytes body = list("MxSt", chunk("MxSt", st));
    Bytes omni = chunk("MxHd", hd);
    const Bytes ofChunk = chunk("MxOf", of);
    omni.insert(omni.end(), ofChunk.begin(), ofChunk.end());
    omni.insert(omni.end(), body.begin(), body.end());
    built.file = list("OMNI", omni);
    std::memcpy(built.file.data(), "RIFF", 4); // the outer chunk is a RIFF, laid out as a LIST
    return built;
}

void testObjects(const std::filesystem::path &tmp) {
    const BuiltSi built = buildSi();
    CHECK(writeFile(tmp / "test.si", built.file));
    SiFile si;
    const Error e = si.open(tmp / "test.si");
    CHECK(!e);
    if (e)
        return;
    CHECK(si.majorVersion() == 2 && si.minorVersion() == 2 && si.bufferSize() == kBuffer);
    CHECK(si.streamOffsets().size() == 2 && si.streamOffsets()[0] == 0x50);
    CHECK(si.objects().size() == 1);
    const SiObject &root = si.objects()[0];
    CHECK(root.name == "_StartUp" && root.presenter == "LegoWorldPresenter" && root.extra == "Object:Test");
    CHECK(root.children.size() == 3 && si.all().size() == 6);
    const SiObject *still = si.find(1);
    CHECK(still && still->format == "STL" && still->fileName == "q:\\b.bmp" && still->duration == -1);
    CHECK(still && still->location[0] == 145.0 && still->up[1] == 1.0 && still->extra == "World:current");
    const SiObject *sound = si.find(2);
    CHECK(sound && sound->format == "WAV" && sound->volume == 79);
    const SiObject *select = si.find(4);
    CHECK(select && select->variable == "RANDOM_2" && select->choices.size() == 2 && select->children.size() == 2);
    CHECK(si.find(6) && si.find(6)->format == "FLC" && si.find(6)->frameRate == 10);
}

void testPieces(const std::filesystem::path &tmp) {
    const BuiltSi built = buildSi();
    CHECK(writeFile(tmp / "test.si", built.file));
    SiFile si;
    if (si.open(tmp / "test.si"))
        return;
    // The pixels were split across buffers, with filler before some.
    int splits = 0;
    for (const SiChunk &c : si.chunks())
        splits += c.objectId == 1 && (c.flags & SiChunk::kSplit);
    CHECK(splits > 2);
    // ... and at least one buffer ends in bytes of filler, not a chunk.
    const uint8_t filler[] = {0xAC, 'M', 'x', 'C', 'h'};
    CHECK(std::search(built.file.begin(), built.file.end(), std::begin(filler), std::end(filler)) != built.file.end());
    std::vector<SiPiece> pieces;
    int32_t end = 0;
    CHECK(!si.readPieces(1, pieces, &end));
    CHECK(pieces.size() == 2 && pieces[0].time == -1 && pieces[1].data == built.pixels && end == 2000);

    std::vector<uint8_t> bmp;
    CHECK(!rebuildMediaFile(*si.find(1), pieces, bmp));
    CHECK(bmp.size() == 14 + 40 + 1024 + built.pixels.size() && bmp[0] == 'B' && readLE32(&bmp[10]) == 14 + 1064);

    CHECK(!si.readPieces(2, pieces));
    CHECK(pieces.size() == 3 && pieces[2].time == 1000);
    std::vector<uint8_t> wav;
    CHECK(!rebuildMediaFile(*si.find(2), pieces, wav));
    CHECK(wav.size() == 44 + built.samples.size() && readLE32(&wav[40]) == built.samples.size());
    CHECK(std::equal(built.samples.begin(), built.samples.end(), wav.begin() + 44));
}

// An FLC's frames lose the rectangles before them; its header gets its
// size and where its first two frames are.
void testFlc(const std::filesystem::path &) {
    SiObject flc;
    flc.format = "FLC";
    std::vector<SiPiece> pieces(3);
    pieces[0].data.assign(128, 0);
    for (int i = 1; i < 3; i++) {
        put32(pieces[i].data, 1);
        for (int k = 0; k < 4; k++)
            put32(pieces[i].data, 9);
        put32(pieces[i].data, 16);
        put16(pieces[i].data, 0xF1FA);
        pieces[i].data.resize(pieces[i].data.size() + 10, uint8_t(i));
    }
    std::vector<uint8_t> out;
    CHECK(!rebuildMediaFile(flc, pieces, out));
    CHECK(out.size() == 128 + 32 && readLE32(out.data()) == out.size());
    CHECK(readLE32(&out[80]) == 128 && readLE32(&out[84]) == 144 && readLE16(&out[132]) == 0xF1FA);
}

// An event: the header every one has, then its name and value.
void testEvent(const std::filesystem::path &) {
    std::vector<SiPiece> pieces(2);
    pieces[0].data = {1, 0, 0, 0, 2, 0xCD, 0xCD, 0xCD, 1, 0, 0, 0, 2, 0xCD, 0xCD, 0xCD};
    const std::string text = std::string("HIT_WALL_SOUND") + '\0' + "HitWall" + '\0';
    pieces[1].data.assign(text.begin(), text.end());
    SiEvent ev;
    CHECK(!parseEvent(pieces, ev));
    CHECK(ev.name == "HIT_WALL_SOUND" && ev.value == "HitWall");
    // Bytes after the value, a value without its NUL, and no data.
    pieces[1].data.push_back('x');
    CHECK(bool(parseEvent(pieces, ev)));
    pieces[1].data.resize(pieces[1].data.size() - 2);
    CHECK(bool(parseEvent(pieces, ev)));
    pieces.resize(1);
    CHECK(bool(parseEvent(pieces, ev)));
}

void testNotSi(const std::filesystem::path &tmp) {
    CHECK(writeFile(tmp / "x.si", Bytes{'R', 'I', 'F', 'F', 4, 0, 0, 0, 'W', 'A', 'V', 'E'}));
    SiFile si;
    CHECK(bool(si.open(tmp / "x.si")));
    CHECK(bool(si.open(tmp / "missing.si")));
}

} // namespace

static const reluva::test::Suite kSuite("legoisland.unit.si",
                                        {
                                            {"objects", [](const std::filesystem::path &t) { testObjects(t); }},
                                            {"pieces", [](const std::filesystem::path &t) { testPieces(t); }},
                                            {"flc", [](const std::filesystem::path &t) { testFlc(t); }},
                                            {"event", [](const std::filesystem::path &t) { testEvent(t); }},
                                            {"not an SI file", [](const std::filesystem::path &t) { testNotSi(t); }},
                                        });
