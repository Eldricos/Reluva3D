// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/winhelp.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>

namespace reluva::winhelp {

namespace {

constexpr uint32_t kMagic = 0x00035F3F;
constexpr uint16_t kTreeMagic = 0x293B, kSystemMagic = 0x036C;
constexpr size_t kFileHeader = 9, kTreeHeader = 38, kLinkHeader = 21, kBlockHeader = 12;
constexpr uint32_t kDecompressedBlock = 0x4000; // a compressed topic block, unpacked
enum : uint8_t { kTopicHeader = 0x02, kDisplay = 0x20, kTable = 0x23 };

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); }

// A run of bytes read from the front, every read checked.
struct Reader {
    const uint8_t* p;
    const uint8_t* end;
    bool bad = false;
    bool has(size_t n) {
        if (size_t(end - p) < n)
            bad = true;
        return !bad;
    }
    uint8_t u8() { return has(1) ? *p++ : 0; }
    uint16_t u16() {
        if (!has(2))
            return 0;
        p += 2;
        return le16(p - 2);
    }
    uint32_t u32() {
        if (!has(4))
            return 0;
        p += 4;
        return le32(p - 4);
    }
    void skip(size_t n) {
        if (has(n))
            p += n;
    }
    bool atEnd() const { return p >= end; }
    // The compressed numbers: the lowest bit says the long form.
    uint32_t word() { return has(1) && (*p & 1) ? u16() >> 1 : u8() >> 1u; }
    int32_t shortInt() { return has(1) && (*p & 1) ? int32_t(u16() >> 1) - 0x4000 : int32_t(u8() >> 1) - 0x40; }
    uint32_t longWord() { return has(1) && (*p & 1) ? u32() >> 1 : u16() >> 1u; }
    int32_t longInt() { return has(1) && (*p & 1) ? int32_t(u32() >> 1) - 0x40000000 : int32_t(u16() >> 1) - 0x4000; }
};

// The file's own little file system: its directory, a B+ tree of names.
struct HelpFile {
    const std::vector<uint8_t>& data;
    std::map<std::string, std::pair<size_t, size_t>> files; // name: start and size of its contents

    bool body(size_t at, size_t& start, size_t& size) const {
        if (at + kFileHeader > data.size())
            return false;
        const uint32_t used = le32(&data[at + 4]);
        start = at + kFileHeader;
        size = used;
        return start + size <= data.size();
    }
    bool get(const std::string& name, const uint8_t*& p, size_t& size) const {
        auto it = files.find(name);
        if (it == files.end())
            return false;
        p = data.data() + it->second.first;
        size = it->second.second;
        return true;
    }
};

// Each leaf of a B+ tree in turn: f(entries, count, end).
template <typename F>
bool forEachLeaf(const uint8_t* tree, size_t size, F&& f) {
    if (size < kTreeHeader || le16(tree) != kTreeMagic)
        return false;
    const uint16_t pageSize = le16(tree + 4);
    const int16_t root = int16_t(le16(tree + 26));
    const int16_t levels = int16_t(le16(tree + 32));
    const uint8_t* pages = tree + kTreeHeader;
    const size_t pageCount = pageSize ? (size - kTreeHeader) / pageSize : 0;
    auto page = [&](int16_t n) -> const uint8_t* {
        return n >= 0 && size_t(n) < pageCount ? pages + size_t(n) * pageSize : nullptr;
    };
    // Down the leftmost branch (an index page's first child is its
    // "previous page"), then along the leaves.
    int16_t n = root;
    for (int16_t level = 1; level < levels; ++level) {
        const uint8_t* p = page(n);
        if (!p)
            return false;
        n = int16_t(le16(p + 4));
    }
    for (size_t guard = 0; n != -1 && guard <= pageCount; ++guard) {
        const uint8_t* p = page(n);
        if (!p)
            return false;
        f(p + 8, int16_t(le16(p + 2)), p + pageSize);
        n = int16_t(le16(p + 6));
    }
    return true;
}

// The phrases of Hall compression (|PhrIndex, |PhrImage).
struct Phrases {
    std::vector<std::string> list;

    Error load(const HelpFile& help) {
        const uint8_t *index = nullptr, *image = nullptr;
        size_t indexSize = 0, imageSize = 0;
        if (!help.get("|PhrIndex", index, indexSize) || !help.get("|PhrImage", image, imageSize))
            return help.files.count("|Phrases") ? Error::fail("this help's phrases (|Phrases) aren't read")
                                                : Error::success(); // none: the text isn't compressed
        if (indexSize < 28)
            return Error::fail("|PhrIndex is too short");
        const uint32_t entries = le32(index + 4);
        const uint32_t unpacked = le32(index + 12), packed = le32(index + 16);
        const int bits = le16(index + 24) & 0xF;
        // The phrases' lengths, in a stream of bits read from 32-bit words,
        // lowest bit first.
        Reader r{index + 28, index + indexSize};
        uint32_t mask = 0, value = 0;
        auto bit = [&] {
            mask <<= 1;
            if (!mask) {
                value = r.u32();
                mask = 1;
            }
            return (value & mask) != 0;
        };
        std::vector<uint32_t> offsets{0};
        for (uint32_t i = 0; i < entries && !r.bad; ++i) {
            uint32_t n = 1;
            while (bit() && !r.bad)
                n += 1u << bits;
            for (int b = 0; b < std::max(bits, 1) && b < 5; ++b)
                if (bit())
                    n += 1u << b;
            offsets.push_back(offsets.back() + n);
        }
        if (r.bad)
            return Error::fail("|PhrIndex runs out");
        std::vector<uint8_t> text =
            unpacked == packed ? std::vector<uint8_t>(image, image + imageSize) : decompressLz77(image, imageSize);
        if (offsets.back() > text.size())
            return Error::fail("|PhrImage is shorter than |PhrIndex says");
        for (uint32_t i = 0; i < entries; ++i)
            list.emplace_back(text.begin() + offsets[i], text.begin() + offsets[i + 1]);
        return Error::success();
    }

    std::string expand(const uint8_t* p, size_t n) const {
        std::string out;
        auto phrase = [&](size_t i) {
            if (i < list.size())
                out += list[i];
        };
        for (size_t i = 0; i < n;) {
            const uint8_t c = p[i++];
            if ((c & 1) == 0) {
                phrase(c / 2);
            } else if ((c & 3) == 1) {
                if (i < n)
                    phrase(128 + size_t(c / 4) * 256 + p[i++]);
            } else if ((c & 7) == 3) {
                const size_t count = std::min<size_t>(c / 8 + 1, n - i);
                out.append(reinterpret_cast<const char*>(p + i), count);
                i += count;
            } else if ((c & 15) == 7) {
                out.append(c / 16 + 1, ' ');
            } else {
                out.append(c / 16 + 1, '\0');
            }
        }
        return out;
    }
};

// Windows' own characters as plain ASCII.
std::string ascii(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
        case 0x91:
        case 0x92:
            out += '\'';
            break;
        case 0x93:
        case 0x94:
            out += '"';
            break;
        case 0x95:
            out += '*';
            break;
        case 0x96:
        case 0x97:
            out += '-';
            break;
        case 0x85:
            out += "...";
            break;
        case 0x99:
            out += "(TM)";
            break;
        case 0xA0:
            out += ' ';
            break;
        case 0xB7:
            out += '*';
            break; // a list's bullet (in the Symbol font)
        case 0xA9:
            out += "(C)";
            break;
        case 0xAE:
            out += "(R)";
            break;
        default:
            out += c < 0x80 ? char(c) : '?';
        }
    }
    return out;
}

// A paragraph's layout, skipped.
void skipParagraph(Reader& r) {
    r.skip(4);
    const uint16_t bits = r.u16();
    if (bits & 0x0001)
        r.longWord();
    for (uint16_t m : {0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040})
        if (bits & m)
            r.shortInt();
    if (bits & 0x0100)
        r.skip(3); // a border
    if (bits & 0x0200) {
        const int32_t tabs = r.shortInt();
        for (int32_t i = 0; i < tabs && !r.bad; ++i)
            if (r.word() & 0x4000)
                r.word();
    }
}

// A text record's words: LinkData2's strings, each followed by the next of
// LinkData1's commands (a paragraph's end, a tab, a picture...).
std::string recordText(uint8_t type, const uint8_t* ld1, size_t n1, const std::string& ld2,
                       const std::function<int(uint32_t)>& linkTarget) {
    Reader r{ld1, ld1 + n1};
    r.longWord(); // the topic's size so far
    r.word();     // and this record's text's
    if (type == kTable) {
        const uint8_t columns = r.u8();
        const uint8_t tableType = r.u8();
        if (tableType == 0 || tableType == 2)
            r.skip(2); // its least width
        r.skip(size_t(columns) * 4);
    }
    std::string out;
    bool inLink = false;
    size_t at = 0; // in ld2
    auto nextString = [&] {
        if (at > ld2.size())
            return;
        const size_t end = std::min(ld2.find('\0', at), ld2.size());
        out += ld2.substr(at, end - at);
        at = end + 1;
    };
    while (!r.bad) {
        if (type == kTable) {
            // Each cell: its column (-1 ends the row), three bytes not
            // known, then a paragraph as any other.
            const int16_t column = int16_t(r.u16());
            if (column == -1 || r.bad)
                break;
            r.skip(3);
            if (column > 0)
                out += '\t';
        }
        skipParagraph(r);
        for (bool more = true; more && !r.bad;) {
            nextString();
            if (r.atEnd())
                return out;
            const uint8_t c = r.u8();
            switch (c) {
            case 0xFF:
                more = false;
                break;
            case 0x20:
                r.skip(4);
                break;
            case 0x21:
                r.skip(2);
                break;
            case 0x80:
                r.skip(2);
                break; // a font
            case 0x81:
                out += '\n';
                break;
            case 0x82:
                out += type == kTable ? " " : "\n\n";
                break;
            case 0x83:
                out += '\t';
                break;
            case 0x86:
            case 0x87:
            case 0x88: { // a picture, or a window in the text
                // Kind 0x03 a picture, 0x22 one with hot spots (their
                // count first); its data a type (0: from the file's |bm
                // pictures) and the picture's number. Kind 0x05 a window
                // a program draws in.
                const uint8_t kind = r.u8();
                const int32_t size = r.longInt();
                if (kind == 0x22)
                    r.word();
                if (size < 0) {
                    r.bad = true;
                } else if ((kind == 0x03 || kind == 0x22) && size >= 4) {
                    const uint16_t from = r.u16(), number = r.u16();
                    r.skip(size_t(size) - 4);
                    if (from == 0)
                        out += "\n" + std::string(1, kPictureMark) + std::to_string(number) + kPictureMark + "\n";
                } else {
                    r.skip(size_t(size));
                }
                break;
            }
            case 0x89: // a hot spot's end
                if (inLink)
                    out += kLinkEnd;
                inLink = false;
                break;
            case 0x8B:
                out += ' ';
                break;
            case 0x8C:
                out += '-';
                break;
            case 0xC8:
            case 0xCC:
                r.skip(r.u16());
                break; // a macro
            case 0xE0:
            case 0xE1:
                r.skip(4); // Windows 3.0's
                break;
            case 0xE2:
            case 0xE3:
            case 0xE6:
            case 0xE7: { // a jump or a pop-up: the hash of the topic's context name
                const int target = linkTarget(r.u32());
                if (target >= 0 && !inLink) {
                    out += std::string(1, kLinkMark) + std::to_string(target) + kLinkMark;
                    inLink = true;
                }
                break;
            }
            case 0xEA:
            case 0xEB:
            case 0xEE:
            case 0xEF:
                r.skip(r.u16());
                break; // to another file
            default:
                r.bad = true;
                break; // not known: the rest is lost
            }
        }
        if (type != kTable)
            break;
    }
    if (inLink)
        out += kLinkEnd;
    return type == kTable ? out + "\n\n" : out;
}

// Tidied: lines without trailing spaces, at most one blank line between.
std::string tidy(const std::string& s) {
    std::string out;
    size_t blank = 0;
    for (size_t at = 0; at <= s.size();) {
        size_t end = s.find('\n', at);
        if (end == std::string::npos)
            end = s.size();
        std::string line = s.substr(at, end - at);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line.empty()) {
            ++blank;
        } else {
            if (!out.empty())
                out += blank ? "\n\n" : "\n";
            out += line;
            blank = 0;
        }
        at = end + 1;
    }
    return out;
}

} // namespace

std::vector<uint8_t> decompressLz77(const uint8_t* data, size_t size) {
    // Eight items after each flag byte, lowest bit first: a 0 a byte as it
    // is, a 1 a 16-bit word: (length - 3) in its top 4 bits, (distance - 1)
    // back in the rest.
    std::vector<uint8_t> out;
    for (size_t i = 0; i < size;) {
        const uint8_t flags = data[i++];
        for (int b = 0; b < 8 && i < size; ++b) {
            if (flags & (1 << b)) {
                if (i + 2 > size)
                    return out;
                const uint16_t w = le16(data + i);
                i += 2;
                const size_t length = (w >> 12) + 3, distance = (w & 0x0FFF) + 1u;
                if (distance > out.size())
                    return out;
                for (size_t k = 0; k < length; ++k)
                    out.push_back(out[out.size() - distance]);
            } else {
                out.push_back(data[i++]);
            }
        }
    }
    return out;
}

Error openHelp(const std::vector<uint8_t>& file, HelpFile& help) {
    if (file.size() < 16 || le32(file.data()) != kMagic)
        return Error::fail("not a Windows Help file");
    size_t dirStart = 0, dirSize = 0;
    if (!help.body(le32(&file[4]), dirStart, dirSize))
        return Error::fail("its directory is past its end");
    const bool listed = forEachLeaf(file.data() + dirStart, dirSize, [&](const uint8_t* p, int n, const uint8_t* end) {
        for (int i = 0; i < n && p < end; ++i) {
            const uint8_t* zero = static_cast<const uint8_t*>(std::memchr(p, 0, size_t(end - p)));
            if (!zero || end - zero < 5)
                return;
            const std::string name(reinterpret_cast<const char*>(p), size_t(zero - p));
            size_t start = 0, size = 0;
            if (help.body(le32(zero + 1), start, size))
                help.files[name] = {start, size};
            p = zero + 5;
        }
    });
    if (!listed)
        return Error::fail("its directory can't be read");
    return Error::success();
}

Error readHelp(const std::vector<uint8_t>& file, std::vector<Topic>& out) {
    out.clear();
    HelpFile help{file, {}};
    if (Error err = openHelp(file, help))
        return err;

    // The links: a jump names its topic by the hash of a context name;
    // |CONTEXT gives the hash's topic offset (a B+ tree of a 32-bit hash
    // and offset), |TTLBTREE each topic's offset in order (an offset and
    // the topic's title).
    std::map<uint32_t, uint32_t> contexts;
    std::vector<uint32_t> topicOffsets;
    if (const uint8_t* tree = nullptr; true) {
        size_t size = 0;
        if (help.get("|CONTEXT", tree, size))
            forEachLeaf(tree, size, [&](const uint8_t* p, int n, const uint8_t* end) {
                for (int i = 0; i < n && end - p >= 8; ++i, p += 8)
                    contexts[le32(p)] = le32(p + 4);
            });
        if (help.get("|TTLBTREE", tree, size))
            forEachLeaf(tree, size, [&](const uint8_t* p, int n, const uint8_t* end) {
                for (int i = 0; i < n && end - p >= 5; ++i) {
                    topicOffsets.push_back(le32(p));
                    const uint8_t* zero = static_cast<const uint8_t*>(std::memchr(p + 4, 0, size_t(end - p - 4)));
                    if (!zero)
                        return;
                    p = zero + 1;
                }
            });
    }
    // The topic a hash names: the last to start at or before its offset.
    const auto linkTarget = [&](uint32_t hash) {
        const auto it = contexts.find(hash);
        if (it == contexts.end())
            return -1;
        const auto after = std::upper_bound(topicOffsets.begin(), topicOffsets.end(), it->second);
        return after == topicOffsets.begin() ? -1 : int(after - topicOffsets.begin()) - 1;
    };

    // |SYSTEM: the version, and whether the topics are compressed.
    const uint8_t* system = nullptr;
    size_t systemSize = 0;
    if (!help.get("|SYSTEM", system, systemSize) || systemSize < 12 || le16(system) != kSystemMagic)
        return Error::fail("no |SYSTEM");
    const uint16_t minor = le16(system + 2), flags = le16(system + 10);
    if (minor <= 16)
        return Error::fail("Windows 3.0 help files aren't read");
    const bool compressed = flags == 4 || flags == 8;
    const size_t blockSize = flags == 8 ? 2048 : 4096;
    Phrases phrases;
    if (Error err = phrases.load(help))
        return err;

    // |TOPIC: blocks, each a header and (compressed) records. A record's
    // place is its block's number times 0x4000 plus its offset in the
    // unpacked block, header counted.
    const uint8_t* topic = nullptr;
    size_t topicSize = 0;
    if (!help.get("|TOPIC", topic, topicSize))
        return Error::fail("no |TOPIC");
    std::vector<uint8_t> stream;
    std::vector<size_t> starts;
    for (size_t at = 0; at + kBlockHeader <= topicSize; at += blockSize) {
        const size_t n = std::min(blockSize, topicSize - at) - kBlockHeader;
        starts.push_back(stream.size());
        if (compressed) {
            const std::vector<uint8_t> unpacked = decompressLz77(topic + at + kBlockHeader, n);
            stream.insert(stream.end(), unpacked.begin(), unpacked.end());
        } else {
            stream.insert(stream.end(), topic + at + kBlockHeader, topic + at + kBlockHeader + n);
        }
    }
    const uint32_t perBlock = compressed ? kDecompressedBlock : uint32_t(blockSize);
    auto offsetOf = [&](uint32_t pos, size_t& at) {
        const size_t block = pos / perBlock, offset = pos % perBlock;
        if (block >= starts.size() || offset < kBlockHeader)
            return false;
        at = starts[block] + offset - kBlockHeader;
        return at + kLinkHeader <= stream.size();
    };

    int32_t scrollFrom = -1; // the topic's records before this are its non-scrolling top
    uint32_t pos = le32(topic + 4);
    for (size_t guard = 0; pos != 0xFFFFFFFF && guard < 1000000; ++guard) {
        size_t at = 0;
        if (!offsetOf(pos, at))
            break;
        const uint8_t* link = stream.data() + at;
        const uint32_t size = le32(link), length2 = le32(link + 4), next = le32(link + 12), length1 = le32(link + 16);
        const uint8_t type = link[20];
        if (length1 < kLinkHeader || length1 > size || at + size > stream.size())
            break;
        const uint8_t* ld1 = link + kLinkHeader;
        const size_t n1 = length1 - kLinkHeader;
        const uint8_t* raw2 = link + length1;
        const size_t n2 = size - length1;
        const std::string ld2 = length2 > n2 && !phrases.list.empty()
                                    ? phrases.expand(raw2, n2)
                                    : std::string(reinterpret_cast<const char*>(raw2), std::min<size_t>(n2, length2));
        if (type == kTopicHeader) {
            Topic t;
            t.title = ascii(ld2.substr(0, ld2.find('\0')));
            out.push_back(std::move(t));
            const int32_t nonScroll = n1 >= 24 ? int32_t(le32(ld1 + 16)) : -1;
            scrollFrom = nonScroll != -1 && n1 >= 24 ? int32_t(le32(ld1 + 20)) : -1;
        } else if ((type == kDisplay || type == kTable) && !out.empty()) {
            if (scrollFrom == -1 || int64_t(pos) >= scrollFrom)
                out.back().text += recordText(type, ld1, n1, ld2, linkTarget);
        }
        if (next <= pos)
            break;
        pos = next;
    }
    for (Topic& t : out)
        t.text = tidy(ascii(t.text));
    return out.empty() ? Error::fail("no topics") : Error::success();
}

std::string plainText(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == kPictureMark || c == kLinkMark) { // past the number and its closing mark
            const size_t close = text.find(c, i + 1);
            i = close == std::string::npos ? text.size() : close;
        } else if (c != kLinkEnd) {
            out += c;
        }
    }
    return out;
}

std::vector<uint8_t> unpackRuns(const uint8_t* data, size_t size) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < size;) {
        const uint8_t n = data[i++];
        if (n & 0x80) {
            const size_t count = std::min<size_t>(n & 0x7F, size - i);
            out.insert(out.end(), data + i, data + i + count);
            i += count;
        } else if (i < size) {
            out.insert(out.end(), n, data[i++]);
        }
    }
    return out;
}

Error readPicture(const std::vector<uint8_t>& file, int number, Picture& out) {
    out = Picture{};
    HelpFile help{file, {}};
    if (Error err = openHelp(file, help))
        return err;
    const uint8_t* data = nullptr;
    size_t size = 0;
    const std::string name = "|bm" + std::to_string(number);
    if (!help.get(name, data, size) || size < 8)
        return Error::fail("no " + name);
    // The file: magic ("lP" or "lp"), how many pictures, where each starts;
    // the first is read.
    if (le16(data) != 0x506C && le16(data) != 0x706C)
        return Error::fail(name + " isn't a picture file");
    const uint8_t* picture = data + le32(data + 4);
    if (picture + 2 > data + size)
        return Error::fail(name + " is short");
    // Each: its type (5 a device's bitmap, 6 a DIB, 8 a metafile, not
    // read), its packing (0 none, 1 runs, 2 LZ77, 3 both), then compressed
    // numbers: resolution, planes and bits a pixel, size, colours, the
    // packed data's size, the hot spots' size; then where the data and hot
    // spots are (from the picture's start); a DIB's colours follow.
    const uint8_t type = picture[0], packing = picture[1];
    if (type != 5 && type != 6)
        return Error::fail(name + ": a kind of picture that isn't read");
    Reader r{picture + 2, data + size};
    r.longWord();
    r.longWord();
    const uint32_t planes = r.word(), bits = r.word();
    const uint32_t width = r.longWord(), height = r.longWord(), colorsUsed = r.longWord();
    r.longWord(); // colours important
    const uint32_t packedSize = r.longWord();
    r.longWord(); // hot spots' size
    const uint32_t packedAt = r.u32();
    r.u32(); // the hot spots'
    if (r.bad || planes != 1 || width == 0 || height == 0 || width > 8192 || height > 8192 ||
        (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32))
        return Error::fail(name + ": not a picture as read");
    std::vector<uint32_t> palette;
    if (type == 6 && bits <= 8) {
        const uint32_t colors = colorsUsed ? colorsUsed : 1u << bits;
        for (uint32_t i = 0; i < colors && i < 256 && r.has(4); ++i) {
            const uint32_t bgrx = r.u32();
            palette.push_back(bgrx);
        }
    } else if (bits <= 8) { // a device's bitmap: black and white
        for (uint32_t i = 0; i < (1u << bits); ++i)
            palette.push_back(i ? 0xFFFFFF : 0);
    }
    if (picture + packedAt + packedSize > data + size)
        return Error::fail(name + ": its data is past its end");
    const uint8_t* packed = picture + packedAt;
    std::vector<uint8_t> pixels;
    switch (packing) {
    case 0:
        pixels.assign(packed, packed + packedSize);
        break;
    case 1:
        pixels = unpackRuns(packed, packedSize);
        break;
    case 2:
        pixels = decompressLz77(packed, packedSize);
        break;
    case 3: {
        const std::vector<uint8_t> once = decompressLz77(packed, packedSize);
        pixels = unpackRuns(once.data(), once.size());
        break;
    }
    default:
        return Error::fail(name + ": a packing that isn't read");
    }
    // Rows from the bottom, each to a whole 4 bytes (a DIB) or 2 (a
    // device's bitmap).
    const size_t rowBytes = type == 6 ? (width * bits + 31) / 32 * 4 : (width * bits + 15) / 16 * 2;
    if (pixels.size() < rowBytes * height)
        return Error::fail(name + ": its data is short");
    out.width = int(width);
    out.height = int(height);
    out.rgba.resize(size_t(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* row = pixels.data() + (height - 1 - y) * rowBytes;
        uint8_t* to = out.rgba.data() + size_t(y) * width * 4;
        for (uint32_t x = 0; x < width; ++x, to += 4) {
            uint32_t bgr = 0;
            if (bits == 24 || bits == 32) {
                const uint8_t* p = row + x * (bits / 8);
                bgr = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16;
            } else {
                const uint32_t bit = x * bits;
                const uint32_t index = (row[bit / 8] >> (8 - bits - bit % 8)) & ((1u << bits) - 1);
                bgr = index < palette.size() ? palette[index] : 0;
            }
            to[0] = uint8_t(bgr >> 16);
            to[1] = uint8_t(bgr >> 8);
            to[2] = uint8_t(bgr);
            to[3] = 255;
        }
    }
    return Error::success();
}

} // namespace reluva::winhelp
