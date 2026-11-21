// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// SI ("Interleaf") files: docs/engines/legoisland/formats/si.md.

#include "engines/legoisland/formats/si.h"

#include "common/bytes.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

namespace reluva::legoisland {

namespace {

constexpr uint32_t fourCc(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}

constexpr uint32_t kRiff = fourCc("RIFF");
constexpr uint32_t kList = fourCc("LIST");
constexpr uint32_t kOmni = fourCc("OMNI");
constexpr uint32_t kMxHd = fourCc("MxHd");
constexpr uint32_t kMxOf = fourCc("MxOf");
constexpr uint32_t kMxSt = fourCc("MxSt");
constexpr uint32_t kMxOb = fourCc("MxOb");
constexpr uint32_t kMxCh = fourCc("MxCh");

// A chunk's header: its tag, and the size of what follows (padded to an
// even size in the file, as RIFF's are).
constexpr uint32_t kChunkHeader = 8;
// MxCh's own fields after its header: flags, object id, time, remaining.
constexpr uint32_t kMxChFields = 14;
// The MxOb fields after the names, before the vectors: id, flags, start,
// duration, loops.
constexpr uint32_t kMxObFixed = 20;
// A medium's fields after its file name, before a sound's volume.
constexpr uint32_t kMediaTrailer = 24;

// Reads MxOb fields in order from a record, failing once, at the end, if
// any ran past it.
class RecordReader {
public:
    RecordReader(const uint8_t *data, size_t size) : _data(data), _size(size) {}

    bool fits(size_t n) const { return _pos + n <= _size; }
    size_t pos() const { return _pos; }
    bool failed() const { return _failed; }
    const uint8_t *here() const { return _data + _pos; }
    size_t left() const { return _size - _pos; }

    uint16_t u16() {
        if (!fits(2))
            return fail(), 0;
        const uint16_t v = readLE16(_data + _pos);
        _pos += 2;
        return v;
    }
    uint32_t u32() {
        if (!fits(4))
            return fail(), 0;
        const uint32_t v = readLE32(_data + _pos);
        _pos += 4;
        return v;
    }
    double f64() {
        if (!fits(8))
            return fail(), 0.0;
        double v = 0;
        std::memcpy(&v, _data + _pos, 8);
        _pos += 8;
        return v;
    }
    std::string cstr() {
        const void *end = std::memchr(_data + _pos, 0, _size - _pos);
        if (!end)
            return fail(), std::string();
        const size_t len = static_cast<const uint8_t *>(end) - (_data + _pos);
        std::string s(reinterpret_cast<const char *>(_data + _pos), len);
        _pos += len + 1;
        return s;
    }
    // A string in a field of `n` bytes, ending at its first NUL.
    std::string fixed(size_t n) {
        if (!fits(n))
            return fail(), std::string();
        const char *p = reinterpret_cast<const char *>(_data + _pos);
        _pos += n;
        return std::string(p, strnlen(p, n));
    }
    void skip(size_t n) {
        if (!fits(n))
            fail();
        else
            _pos += n;
    }

private:
    void fail() {
        _failed = true;
        _pos = _size;
    }

    const uint8_t *_data;
    size_t _size;
    size_t _pos = 0;
    bool _failed = false;
};

std::string formatName(uint32_t tag) {
    std::string s;
    for (int i = 0; i < 4; i++) {
        const char c = char(tag >> (8 * i));
        if (c != ' ' && c != 0)
            s += c;
    }
    return s;
}

Error parseObject(RecordReader &r, SiObject &out, int depth) {
    if (depth > 64)
        return Error::fail("objects nested too deeply");
    out.type = r.u16();
    out.presenter = r.cstr();
    r.u32(); // 0 in every record on the CD
    out.name = r.cstr();
    if (!r.fits(kMxObFixed))
        return Error::fail("record cut short");
    out.id = r.u32();
    out.flags = r.u32();
    out.startTime = int32_t(r.u32());
    out.duration = int32_t(r.u32());
    out.loops = r.u32();
    for (double &v : out.location)
        v = r.f64();
    for (double &v : out.direction)
        v = r.f64();
    for (double &v : out.up)
        v = r.f64();
    out.extra = r.fixed(r.u16());
    if (r.failed())
        return Error::fail("record cut short");
    if (r.left() == 0)
        return Error::success();

    // Children: a LIST of MxObs, a select's variable and choices first.
    if (r.fits(12) && readLE32(r.here()) == kList) {
        const uint32_t listSize = readLE32(r.here() + 4);
        if (readLE32(r.here() + 8) != kMxCh || !r.fits(kChunkHeader + size_t(listSize)))
            return Error::fail("bad list of children in " + out.name);
        const size_t end = r.pos() + kChunkHeader + listSize;
        r.skip(12);
        uint32_t count = 0;
        if (out.type == uint16_t(SiType::kSelect)) {
            out.variable = r.cstr();
            count = r.u32();
            for (uint32_t i = 0; i < count && !r.failed(); i++)
                out.choices.push_back(r.cstr());
        } else {
            count = r.u32();
        }
        for (uint32_t i = 0; i < count; i++) {
            if (!r.fits(kChunkHeader) || readLE32(r.here()) != kMxOb)
                return Error::fail("child " + std::to_string(i) + " of " + out.name + " isn't an MxOb");
            const uint32_t size = readLE32(r.here() + 4);
            r.skip(kChunkHeader);
            if (!r.fits(size))
                return Error::fail("child of " + out.name + " runs past its list");
            RecordReader child(r.here(), size);
            out.children.emplace_back();
            if (Error e = parseObject(child, out.children.back(), depth + 1))
                return e;
            r.skip(size + (size & 1));
        }
        if (r.failed() || r.pos() > end)
            return Error::fail("children of " + out.name + " run past their list");
        return Error::success();
    }

    // A medium: the file it was made from, then how it plays.
    out.fileName = r.cstr();
    r.u32(); // 0 in every record on the CD
    r.u32(); // likewise
    out.frameRate = r.u32();
    const uint32_t tag = r.u32();
    out.paletteManaged = r.u32();
    r.u32(); // 0 in most records; up to 54,100 in a few
    if (r.failed())
        return Error::fail("medium " + out.name + " cut short");
    out.format = formatName(tag);
    if (out.format == "WAV")
        out.volume = r.u32();
    if (r.failed())
        return Error::fail("sound " + out.name + " cut short");
    return Error::success();
}

void collect(const SiObject &o, std::vector<const SiObject *> &out) {
    out.push_back(&o);
    for (const SiObject &c : o.children)
        collect(c, out);
}

void put16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
}

void put32(std::vector<uint8_t> &v, uint32_t x) {
    put16(v, uint16_t(x));
    put16(v, uint16_t(x >> 16));
}

void set32(std::vector<uint8_t> &v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; i++)
        v[at + i] = uint8_t(x >> (8 * i));
}

} // namespace

Error parseSiObject(const uint8_t *data, size_t size, SiObject &out) {
    RecordReader r(data, size);
    out = SiObject();
    return parseObject(r, out, 0);
}

Error SiFile::open(const std::filesystem::path &path) {
    _path = path;
    _objects.clear();
    _chunks.clear();
    _streamOffsets.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("can't open " + path.string());
    in.seekg(0, std::ios::end);
    const uint64_t fileSize = uint64_t(in.tellg());
    in.seekg(0);

    uint8_t head[12];
    if (!in.read(reinterpret_cast<char *>(head), 12) || readLE32(head) != kRiff || readLE32(head + 8) != kOmni)
        return Error::fail(path.string() + " isn't an SI file (no RIFF OMNI header)");
    const uint64_t riffEnd = std::min<uint64_t>(fileSize, kChunkHeader + uint64_t(readLE32(head + 4)));

    // The chunks are walked in order, into lists and MxSt; records are read
    // whole, chunks' data skipped.
    struct Level {
        uint64_t end;
    };
    std::vector<Level> levels = {{riffEnd}};
    uint64_t pos = 12;
    bool haveHeader = false;
    while (!levels.empty()) {
        if (pos + kChunkHeader > levels.back().end) {
            pos = levels.back().end;
            levels.pop_back();
            continue;
        }
        // A chunk's header never straddles a buffer: with less than one
        // left in this buffer, what's there is filler and the next chunk
        // starts the next buffer.
        if (_bufferSize >= kChunkHeader) {
            const uint64_t boundary = (pos / _bufferSize + 1) * _bufferSize;
            if (pos + kChunkHeader > boundary && boundary < levels.back().end) {
                pos = boundary;
                continue;
            }
        }
        uint8_t h[kChunkHeader + 4];
        in.seekg(std::streamoff(pos));
        if (!in.read(reinterpret_cast<char *>(h), kChunkHeader))
            return Error::fail("read error at " + std::to_string(pos));
        const uint32_t tag = readLE32(h);
        const uint32_t size = readLE32(h + 4);
        const uint64_t body = pos + kChunkHeader;
        const uint64_t next = body + size + (size & 1);
        if (body + size > levels.back().end)
            return Error::fail("chunk at " + std::to_string(pos) + " runs past its container");

        if (tag == kRiff || tag == kList) {
            levels.push_back({body + size});
            pos = body + 4; // past the list's type
            continue;
        }
        if (tag == kMxSt) {
            levels.push_back({body + size});
            pos = body;
            continue;
        }
        std::vector<uint8_t> data;
        if (tag == kMxHd || tag == kMxOf || tag == kMxOb) {
            data.resize(size);
            if (!in.read(reinterpret_cast<char *>(data.data()), size))
                return Error::fail("read error at " + std::to_string(pos));
        }
        if (tag == kMxHd) {
            if (size < 12)
                return Error::fail("MxHd too short");
            _minor = readLE16(data.data());
            _major = readLE16(data.data() + 2);
            _bufferSize = readLE32(data.data() + 4);
            _bufferCount = readLE32(data.data() + 8);
            haveHeader = true;
        } else if (tag == kMxOf) {
            for (uint32_t at = 4; at + 4 <= size; at += 4)
                _streamOffsets.push_back(readLE32(data.data() + at));
        } else if (tag == kMxOb) {
            SiObject object;
            if (Error e = parseSiObject(data.data(), data.size(), object))
                return Error::fail("object at " + std::to_string(pos) + ": " + e.message);
            _objects.push_back(std::move(object));
        } else if (tag == kMxCh) {
            if (size < kMxChFields)
                return Error::fail("MxCh at " + std::to_string(pos) + " too short");
            uint8_t f[kMxChFields];
            if (!in.read(reinterpret_cast<char *>(f), kMxChFields))
                return Error::fail("read error at " + std::to_string(pos));
            SiChunk c;
            c.flags = readLE16(f);
            c.objectId = readLE32(f + 2);
            c.time = int32_t(readLE32(f + 6));
            c.remaining = readLE32(f + 10);
            c.dataOffset = body + kMxChFields;
            c.dataSize = size - kMxChFields;
            _chunks.push_back(c);
        }
        pos = next;
    }
    if (!haveHeader)
        return Error::fail(path.string() + " has no MxHd header");
    return Error::success();
}

const SiObject *SiFile::find(uint32_t id) const {
    for (const SiObject *o : all())
        if (o->id == id)
            return o;
    return nullptr;
}

std::vector<const SiObject *> SiFile::all() const {
    std::vector<const SiObject *> out;
    for (const SiObject &o : _objects)
        collect(o, out);
    return out;
}

Error SiFile::readPieces(uint32_t id, std::vector<SiPiece> &out, int32_t *endTime) const {
    out.clear();
    std::ifstream in(_path, std::ios::binary);
    if (!in)
        return Error::fail("can't open " + _path.string());
    // A split piece's first chunk counts all of it; the chunks after it,
    // of the same object, hold the rest.
    uint32_t wanted = 0;
    for (const SiChunk &c : _chunks) {
        if (c.objectId != id)
            continue;
        if (c.flags & SiChunk::kEnd) {
            if (endTime)
                *endTime = c.time;
            continue;
        }
        const bool continues = wanted > 0;
        if (!continues) {
            out.push_back({c.flags, c.time, {}});
            wanted = (c.flags & SiChunk::kSplit) ? std::max(c.remaining, c.dataSize) : c.dataSize;
        }
        std::vector<uint8_t> &data = out.back().data;
        const size_t at = data.size();
        data.resize(at + c.dataSize);
        in.seekg(std::streamoff(c.dataOffset));
        if (!in.read(reinterpret_cast<char *>(data.data() + at), c.dataSize))
            return Error::fail("read error at " + std::to_string(c.dataOffset));
        wanted = c.dataSize >= wanted ? 0 : wanted - c.dataSize;
    }
    if (wanted)
        return Error::fail("object " + std::to_string(id) + "'s last piece is cut short");
    return Error::success();
}

std::string mediaExtension(const SiObject &object) {
    if (object.format == "STL")
        return "bmp";
    if (object.format == "WAV")
        return "wav";
    if (object.format == "FLC")
        return "flc";
    if (object.format == "SMK")
        return "smk";
    return "";
}

Error parseEvent(const std::vector<SiPiece> &pieces, SiEvent &out) {
    out = SiEvent();
    // Its header: 01 00 00 00 02 and 3 bytes of fill, twice, in every one.
    if (pieces.size() != 2 || pieces[0].data.size() != 16)
        return Error::fail("not an event's header and data");
    const std::vector<uint8_t> &d = pieces[1].data;
    const auto nameEnd = std::find(d.begin(), d.end(), uint8_t(0));
    if (nameEnd == d.end())
        return Error::fail("an event's name without its end");
    const auto valueEnd = std::find(nameEnd + 1, d.end(), uint8_t(0));
    if (valueEnd == d.end() || valueEnd + 1 != d.end())
        return Error::fail("an event's value not ending its data");
    out.name.assign(d.begin(), nameEnd);
    out.value.assign(nameEnd + 1, valueEnd);
    return Error::success();
}

std::vector<uint8_t> objectData(const std::vector<SiPiece> &pieces) {
    std::vector<uint8_t> out;
    for (const SiPiece &p : pieces)
        out.insert(out.end(), p.data.begin(), p.data.end());
    if (!out.empty() && out.back() == 0)
        out.pop_back();
    return out;
}

std::string objectKind(const SiObject &object) {
    if (object.format != "OBJ")
        return "";
    const size_t dot = object.fileName.rfind('.');
    if (dot == std::string::npos)
        return "";
    std::string ext = object.fileName.substr(dot + 1);
    for (char &c : ext)
        c = char(std::tolower(uint8_t(c)));
    return ext == "ani" || ext == "mod" || ext == "tex" || ext == "gph" ? ext : "";
}

Error rebuildMediaFile(const SiObject &object, const std::vector<SiPiece> &pieces, std::vector<uint8_t> &out) {
    out.clear();
    if (pieces.empty())
        return Error::fail(object.name + " has no data");
    const std::vector<uint8_t> &header = pieces[0].data;

    if (object.format == "STL") {
        // A bitmap's header (BITMAPINFOHEADER and palette), then its pixels.
        if (header.size() < 40 || pieces.size() < 2)
            return Error::fail(object.name + ": no bitmap header or pixels");
        const uint32_t pixels = uint32_t(pieces[1].data.size());
        out = {'B', 'M'};
        put32(out, uint32_t(14 + header.size() + pixels));
        put32(out, 0);
        put32(out, uint32_t(14 + header.size()));
        out.insert(out.end(), header.begin(), header.end());
        out.insert(out.end(), pieces[1].data.begin(), pieces[1].data.end());
        return Error::success();
    }
    if (object.format == "WAV") {
        // WAVEFORMAT's 16 bytes, the data's size, then a second of sound a
        // piece.
        if (header.size() < 20)
            return Error::fail(object.name + ": no sound format");
        std::vector<uint8_t> samples;
        for (size_t i = 1; i < pieces.size(); i++)
            samples.insert(samples.end(), pieces[i].data.begin(), pieces[i].data.end());
        out = {'R', 'I', 'F', 'F'};
        put32(out, uint32_t(4 + 8 + 16 + 8 + samples.size()));
        out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
        put32(out, 16);
        out.insert(out.end(), header.begin(), header.begin() + 16);
        out.insert(out.end(), {'d', 'a', 't', 'a'});
        put32(out, uint32_t(samples.size()));
        out.insert(out.end(), samples.begin(), samples.end());
        return Error::success();
    }
    if (object.format == "FLC") {
        // The FLIC header, then each frame after its list of the rectangles
        // it changes, which a FLIC file doesn't have.
        constexpr size_t kFlicHeader = 128;
        if (header.size() < kFlicHeader)
            return Error::fail(object.name + ": no FLIC header");
        out = header;
        out.resize(kFlicHeader);
        std::vector<size_t> frameStarts;
        for (size_t i = 1; i < pieces.size(); i++) {
            const std::vector<uint8_t> &p = pieces[i].data;
            if (p.size() < 4)
                return Error::fail(object.name + ": frame " + std::to_string(i) + " too short");
            const uint64_t skip = 4 + 16 * uint64_t(readLE32(p.data()));
            if (skip > p.size())
                return Error::fail(object.name + ": frame " + std::to_string(i) + "'s rectangles run past it");
            frameStarts.push_back(out.size());
            out.insert(out.end(), p.begin() + std::ptrdiff_t(skip), p.end());
        }
        set32(out, 0, uint32_t(out.size()));
        // Where the first and second frames are, for a player looping back.
        if (!frameStarts.empty())
            set32(out, 80, uint32_t(frameStarts[0]));
        if (frameStarts.size() > 1)
            set32(out, 84, uint32_t(frameStarts[1]));
        return Error::success();
    }
    if (object.format == "SMK") {
        // The Smacker header and tables, then each frame: the file as it was.
        for (const SiPiece &p : pieces)
            out.insert(out.end(), p.data.begin(), p.data.end());
        return Error::success();
    }
    return Error::fail(object.name + ": " + object.format + " objects aren't a standard file");
}

} // namespace reluva::legoisland
