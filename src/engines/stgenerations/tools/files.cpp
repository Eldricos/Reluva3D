// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's Star Trek: Generations commands for its files, from
// STG-Engine's command line: what each file holds (probe, STG-Engine's
// --probe), files taken out of an archive (extract, --extract), a script's
// contents and disassembly, and every file with its detection signature
// (files, --dump-files).

#include "common/bytes.h"
#include "common/detection.h"
#include "common/fs.h"
#include "common/stream.h"
#include "common/text.h"
#include "common/zip.h"
#include "engines/stgenerations/formats/disasm.h"
#include "engines/stgenerations/formats/script.h"
#include "devtools/tool.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <system_error>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::stgenerations;

// How much of each file is read; enough for AVI headers.
const uint32_t kProbeReadSize = 64 * 1024;

std::string fourCC(uint32_t tagBE) {
    std::string s;
    for (int shift = 24; shift >= 0; shift -= 8) {
        char c = (char)((tagBE >> shift) & 0xFF);
        s += isprint((unsigned char)c) ? c : '.';
    }
    return s;
}

std::string fourCCLE(const uint8_t *p) {
    return fourCC(readBE32(p));
}

const char *waveFormatName(uint16_t tag) {
    switch (tag) {
    case 0x0001:
        return "PCM";
    case 0x0002:
        return "MS ADPCM";
    case 0x0011:
        return "IMA ADPCM";
    case 0x0055:
        return "MP3";
    default:
        return "unknown";
    }
}

void describeWaveFormat(const uint8_t *p, uint32_t size, std::string &out) {
    if (size < 16)
        return;
    uint16_t tag = readLE16(p);
    out += text::format("audio 0x%04x (%s), %u Hz, %u ch, %u bit", tag, waveFormatName(tag), readLE32(p + 4),
                        readLE16(p + 2), readLE16(p + 14));
}

// Walk the RIFF chunk tree of an AVI's header and describe its streams.
void describeAVI(const std::vector<uint8_t> &data, std::string &out) {
    std::string lastType;
    // Flat walk over chunks, descending into LISTs, within what was read.
    std::vector<std::pair<size_t, size_t>> stack = {{12, data.size()}};
    while (!stack.empty()) {
        size_t pos = stack.back().first;
        size_t end = stack.back().second;
        stack.pop_back();
        while (pos + 8 <= end) {
            uint32_t tag = readBE32(&data[pos]);
            uint32_t size = readLE32(&data[pos + 4]);
            size_t body = pos + 8;
            size_t next = body + size + (size & 1);
            if (tag == tagBE('L', 'I', 'S', 'T') && body + 4 <= data.size()) {
                uint32_t listType = readBE32(&data[body]);
                if (listType == tagBE('m', 'o', 'v', 'i'))
                    return; // frame data follows; headers are done
                stack.push_back({next, end});
                stack.push_back({body + 4, std::min(next, data.size())});
                break;
            }
            if (body + size > data.size())
                return;
            const uint8_t *p = &data[body];
            if (tag == tagBE('a', 'v', 'i', 'h') && size >= 40) {
                uint32_t usPerFrame = readLE32(p);
                out += text::format("\n    %ux%u, %u frames, %.2f fps, %u streams", readLE32(p + 32), readLE32(p + 36),
                                    readLE32(p + 16), usPerFrame ? 1000000.0 / usPerFrame : 0.0, readLE32(p + 24));
            } else if (tag == tagBE('s', 't', 'r', 'h') && size >= 36) {
                lastType = fourCCLE(p);
                out += text::format("\n    stream '%s' handler '%s', rate %u/%u, length %u", lastType.c_str(),
                                    fourCCLE(p + 4).c_str(), readLE32(p + 24), readLE32(p + 20), readLE32(p + 32));
            } else if (tag == tagBE('s', 't', 'r', 'f')) {
                out += "\n      format: ";
                if (lastType == "vids" && size >= 20) {
                    out += text::format("video '%s', %dx%d, %u bpp", fourCCLE(p + 16).c_str(), (int32_t)readLE32(p + 4),
                                        (int32_t)readLE32(p + 8), readLE16(p + 14));
                } else if (lastType == "auds") {
                    describeWaveFormat(p, size, out);
                } else {
                    out += text::format("%u bytes", size);
                }
            }
            pos = next;
        }
    }
}

bool looksLikeText(const std::vector<uint8_t> &data) {
    size_t n = std::min<size_t>(data.size(), 512);
    if (n == 0)
        return false;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = data[i];
        if (c < 9 || (c > 13 && c < 32 && c != 26))
            return false;
    }
    return true;
}

// Heuristic: does the file start with a count followed by ascending offsets?
std::string offsetTableHint(const std::vector<uint8_t> &data, int64_t fileSize) {
    for (size_t start : {(size_t)0, (size_t)4, (size_t)8}) {
        if (start + 4 > data.size())
            break;
        uint32_t count = readLE32(&data[start]);
        if (count < 2 || count > 100000)
            continue;
        // Try entry strides of 4, 8, 12 and 16 bytes with the offset first.
        for (size_t stride : {4, 8, 12, 16}) {
            size_t needed = std::min<size_t>(count, 16);
            if (start + 4 + needed * stride > data.size())
                continue;
            uint32_t prev = 0;
            bool ok = true;
            for (size_t i = 0; i < needed && ok; i++) {
                uint32_t off = readLE32(&data[start + 4 + i * stride]);
                ok = off > prev && (int64_t)off <= fileSize;
                prev = off;
            }
            if (ok)
                return text::format("\n  hint: dword at 0x%zx (%u) may be an entry count, followed by ascending "
                                    "offsets with a %zu byte stride",
                                    start, count, stride);
        }
    }
    return std::string();
}

void hexDump(const std::vector<uint8_t> &data, uint32_t bytes, std::string &out) {
    size_t n = std::min<size_t>(data.size(), bytes);
    for (size_t row = 0; row < n; row += 16) {
        out += text::format("\n  %08zx ", row);
        for (size_t i = 0; i < 16; i++) {
            if (row + i < n)
                out += text::format(" %02x", data[row + i]);
            else
                out += "   ";
            if (i == 7)
                out += " ";
        }
        out += "  |";
        for (size_t i = 0; i < 16 && row + i < n; i++)
            out += isprint(data[row + i]) ? (char)data[row + i] : '.';
        out += "|";
    }
    out += "\n  LE dwords:";
    for (size_t i = 0; i + 4 <= std::min<size_t>(n, 64); i += 4)
        out += text::format(" %u", readLE32(&data[i]));
}

// Identify a format from the leading bytes of a file or archive member.
void describeData(const std::vector<uint8_t> &data, int64_t fullSize, uint32_t dumpBytes, std::string &out) {
    if (data.empty()) {
        out += "empty";
        return;
    }

    auto has = [&](size_t offset, const char *magic) {
        size_t len = strlen(magic);
        return data.size() >= offset + len && memcmp(&data[offset], magic, len) == 0;
    };

    if (has(0, "RIFF") && data.size() >= 12) {
        std::string form = fourCCLE(&data[8]);
        out += "RIFF " + form;
        if (form == "AVI ") {
            describeAVI(data, out);
        } else if (form == "WAVE") {
            for (size_t pos = 12; pos + 8 <= data.size();) {
                uint32_t size = readLE32(&data[pos + 4]);
                if (readBE32(&data[pos]) == tagBE('f', 'm', 't', ' ') && pos + 8 + size <= data.size()) {
                    out += ", ";
                    describeWaveFormat(&data[pos + 8], size, out);
                    break;
                }
                pos += 8 + size + (size & 1);
            }
        }
        return;
    }
    if (has(0, "MZ")) {
        bool pe = false;
        if (data.size() >= 0x40) {
            uint32_t peOffset = readLE32(&data[0x3C]);
            pe = peOffset + 4 <= data.size() && memcmp(&data[peOffset], "PE\0\0", 4) == 0;
        }
        out += pe ? "Windows PE executable" : "DOS executable";
        return;
    }
    if (has(0, "BM") && data.size() >= 30) {
        out += text::format("BMP %dx%d, %u bpp", (int32_t)readLE32(&data[18]), (int32_t)readLE32(&data[22]),
                            readLE16(&data[28]));
        return;
    }
    if (data.size() >= 12 && (readLE16(&data[4]) == 0xAF11 || readLE16(&data[4]) == 0xAF12)) {
        out += text::format("FLIC animation %ux%u, %u frames", readLE16(&data[8]), readLE16(&data[10]),
                            readLE16(&data[6]));
        return;
    }
    if (has(0, "Creative Voice File")) {
        out += "Creative Voice (VOC)";
        return;
    }
    if (has(0, "FORM")) {
        out += "IFF FORM " + (data.size() >= 12 ? fourCCLE(&data[8]) : std::string());
        return;
    }
    if (has(0, "PK\x03\x04")) {
        out += "ZIP archive";
        return;
    }
    if (has(0, "\x13\x5d\x65\x8c")) {
        out += "InstallShield compressed archive";
        return;
    }
    if (looksLikeText(data)) {
        out += "text";
        // Short text files (INI files and the like) are shown in full.
        if (data.size() <= 1024) {
            for (const std::string &line : text::split(std::string(data.begin(), data.end()), '\n'))
                out += "\n  | " + text::trim(line);
        }
        return;
    }

    out += "unknown";
    out += offsetTableHint(data, fullSize);
    hexDump(data, dumpBytes, out);
}

std::string extensionOf(const std::string &name) {
    size_t dot = name.rfind('.');
    size_t slash = name.rfind('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return "(none)";
    return text::toUpper(name.substr(dot + 1));
}

const char *methodName(uint16_t method) {
    switch (method) {
    case 0:
        return "stored";
    case 8:
        return "deflated";
    default:
        return "other";
    }
}

// A game script (SGEN): its objects, states and resources; with `verbose`,
// its disassembly too.
bool describeScript(const std::vector<uint8_t> &data, bool verbose, std::string &out) {
    if (data.size() < 4 || std::memcmp(data.data(), "SGEN", 4) != 0)
        return false;
    MemoryReadStream stream(data);
    ScriptFile script;
    if (!script.load(stream)) {
        out = "SGEN script (could not be parsed)";
        return true;
    }
    size_t states = 0, blocks = 0;
    for (const ScriptObject &object : script.getObjects()) {
        states += object.states.size();
        for (const ScriptState &state : object.states)
            blocks += state.blocks.size();
    }
    out = text::format("SGEN script v%u: %d objects, %d states, %d event blocks, %d words of code", script.getVersion(),
                       (int)script.getObjects().size(), (int)states, (int)blocks, (int)script.getCode().size());
    out += "\n  resources:";
    for (const std::string &name : script.getResourceNames())
        out += " " + name;
    if (verbose)
        out += "\n" + disassembleScript(script);
    return true;
}

// Members larger than this are not decompressed just to sample them.
const uint32_t kMaxSampleSize = 32 * 1024 * 1024;

void describeZip(const std::filesystem::path &file, uint32_t dumpBytes, bool listAll, std::string &out) {
    auto zip = std::make_unique<ZipArchive>();
    if (zip->open(file)) {
        out += "ZIP archive (could not read the central directory)";
        return;
    }

    const std::vector<ZipArchive::Entry> &entries = zip->entries();
    uint64_t total = 0;
    std::map<std::string, int> methods;
    struct ExtInfo {
        int count = 0;
        uint64_t size = 0;
        const ZipArchive::Entry *sample = nullptr;
    };
    std::map<std::string, ExtInfo> extensions;
    for (const ZipArchive::Entry &entry : entries) {
        total += entry.size;
        methods[methodName(entry.method)]++;
        ExtInfo &info = extensions[extensionOf(entry.name)];
        info.count++;
        info.size += entry.size;
        if (!info.sample)
            info.sample = &entry;
    }

    out += text::format("ZIP archive, %d members, %llu bytes uncompressed", (int)entries.size(),
                        (unsigned long long)total);
    for (const auto &m : methods)
        out += text::format(", %d %s", m.second, m.first.c_str());

    for (const auto &ext : extensions) {
        const ExtInfo &info = ext.second;
        out += text::format("\n  .%-6s %5d files %12llu bytes   e.g. %s: ", ext.first.c_str(), info.count,
                            (unsigned long long)info.size, info.sample->name.c_str());
        if (info.sample->size > kMaxSampleSize) {
            out += "too large to sample";
            continue;
        }
        std::vector<uint8_t> data;
        if (zip->read(*info.sample, data)) {
            out += text::format("could not extract (%s)", methodName(info.sample->method));
            continue;
        }
        int64_t fullSize = (int64_t)data.size();
        if (data.size() > kProbeReadSize)
            data.resize(kProbeReadSize);
        std::string desc;
        describeData(data, fullSize, dumpBytes, desc);
        // Indent the sample's own detail lines under its extension line.
        size_t pos = 0;
        while ((pos = desc.find('\n', pos)) != std::string::npos) {
            desc.insert(pos + 1, "    ");
            pos += 5;
        }
        out += desc;
    }

    if (listAll) {
        out += "\n  Members:";
        for (const ZipArchive::Entry &entry : entries)
            out += text::format("\n    %-40s %10u  %s", entry.name.c_str(), entry.size, methodName(entry.method));
    }
}

std::string probeFile(const std::filesystem::path &file, const std::string &name, uint32_t dumpBytes,
                      bool listMembers) {
    std::error_code ec;
    const int64_t fileSize = int64_t(std::filesystem::file_size(file, ec));
    std::string out = text::format("%s (%lld bytes): ", name.c_str(), (long long)fileSize);
    std::vector<uint8_t> data;
    if (ec || !reluva::readFile(file, data))
        return out + "unreadable";

    // Scripts keep their resource list at the very end, so are read whole.
    std::string description;
    if (data.size() <= 4 * 1024 * 1024 && describeScript(data, listMembers, description))
        return out + description;
    if (data.size() > kProbeReadSize)
        data.resize(kProbeReadSize);
    if (data.size() >= 4 && std::memcmp(data.data(), "PK\x03\x04", 4) == 0)
        describeZip(file, dumpBytes, listMembers, out);
    else
        describeData(data, fileSize, dumpBytes, out);
    return out;
}

std::vector<std::filesystem::path> filesUnder(const std::filesystem::path &dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec))
            out.push_back(it->path());
    std::sort(out.begin(), out.end(), [&](const auto &a, const auto &b) {
        return fs::windowsNameLess(a.lexically_relative(dir).generic_string(),
                                   b.lexically_relative(dir).generic_string());
    });
    return out;
}

// A folder's files (or one file, with an archive's members), each with what
// it seems to hold.
int cmdProbe(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool stgenerations probe <file or folder> [bytes to dump]\n");
        return 2;
    }
    const std::filesystem::path path = argv[2];
    const long long bytes = argc > 3 ? tool::parseNumber(argv[3]) : 0;
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec)) {
        std::printf("%s\n", probeFile(path, path.filename().string(), bytes > 0 ? uint32_t(bytes) : 512, true).c_str());
        return 0;
    }
    if (!std::filesystem::is_directory(path, ec)) {
        std::fprintf(stderr, "%s: not a file or a folder\n", argv[2]);
        return 1;
    }
    for (const auto &file : filesUnder(path))
        std::printf("%s\n", probeFile(file, file.lexically_relative(path).generic_string(),
                                      bytes > 0 ? uint32_t(bytes) : 128, false)
                                .c_str());
    return 0;
}

// Files taken out of one of the game's archives (the .DAT files and
// MUSIC.1/2 are ZIPs).
int cmdExtract(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool stgenerations extract <archive> <output folder> [pattern]\n"
                             "  e.g. extract COMMON.DAT out 'FONTS/*'\n");
        return 2;
    }
    ZipArchive zip;
    if (Error err = zip.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    const std::string pattern = argc > 4 ? argv[4] : "*";
    int extracted = 0, failed = 0;
    for (const ZipArchive::Entry &entry : zip.entries()) {
        std::string name = entry.name;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.empty() || name.back() == '/' || !text::matchGlob(name, pattern))
            continue;
        // No way out of the folder.
        if (name.find("..") != std::string::npos || name[0] == '/') {
            std::fprintf(stderr, "skipped %s\n", entry.name.c_str());
            ++failed;
            continue;
        }
        std::vector<uint8_t> data;
        const std::filesystem::path target = std::filesystem::path(argv[3]) / name;
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (zip.read(entry, data) || !reluva::writeFile(target, data)) {
            std::fprintf(stderr, "could not extract %s\n", entry.name.c_str());
            ++failed;
            continue;
        }
        ++extracted;
    }
    std::printf("Extracted %d files%s\n", extracted, failed ? text::format(", %d failed", failed).c_str() : "");
    return failed ? 1 : 0;
}

// A script's contents, and its disassembly.
int cmdScript(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool stgenerations script <GAME.BIN>\n");
        return 2;
    }
    std::vector<uint8_t> data;
    if (!reluva::readFile(argv[2], data)) {
        std::fprintf(stderr, "%s: cannot read\n", argv[2]);
        return 1;
    }
    std::string out;
    if (!describeScript(data, true, out)) {
        std::fprintf(stderr, "%s: not a game script (SGEN)\n", argv[2]);
        return 1;
    }
    std::printf("%s\n", out.c_str());
    return 0;
}

// Every file in a game's folder, with its size and its detection signature
// (the MD5 of its start): for adding a release to kVariants.
int cmdFiles(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool stgenerations files <game folder>\n");
        return 2;
    }
    const std::filesystem::path root = argv[2];
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
        std::fprintf(stderr, "%s: not a folder\n", argv[2]);
        return 1;
    }
    for (const auto &file : filesUnder(root)) {
        const auto md5 = fs::md5File(file, kDetectionHashBytes);
        std::printf("%12llu  %s  %s\n", static_cast<unsigned long long>(std::filesystem::file_size(file, ec)),
                    md5 ? md5->c_str() : "(unreadable)                    ",
                    file.lexically_relative(root).generic_string().c_str());
    }
    return 0;
}

const tool::Commands commands(
    "stgenerations",
    {
        {"files", "<game folder>", "every file in a game's folder, with its size and detection signature", cmdFiles},
        {"probe", "<file or folder> [bytes]",
         "what each file holds: archives and their members, pictures, sounds, movies, scripts; a hex dump of the rest",
         cmdProbe},
        {"extract", "<archive> <folder> [pattern]", "files taken out of one of the game's archives (e.g. 'FONTS/*')",
         cmdExtract},
        {"script", "<GAME.BIN>", "a mission script's objects, states and resources, and its disassembly", cmdScript},
    });

} // namespace
