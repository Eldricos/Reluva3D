// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// WINMM's multimedia file I/O (mmio), as Microsoft documents it, for
// reading RIFF files (WAVE sounds): from a file, or from memory (the 'MEM '
// I/O procedure), with the buffer programs read straight from
// (mmioGetInfo, mmioAdvance, mmioSetInfo). Files are only read.

#include "emulation/x86/win32.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace reluva::x86 {

namespace {

constexpr uint32_t kBufferSize = 8192; // MMIO_DEFAULTBUFFER
// mmioOpenA's flags.
constexpr uint32_t kAccessMask = 0x3; // MMIO_READ 0, MMIO_WRITE 1, MMIO_READWRITE 2
constexpr uint32_t kCreate = 0x1000;
constexpr uint32_t kParse = 0x100;
constexpr uint32_t kDelete = 0x200;
constexpr uint32_t kExist = 0x4000;
// mmioDescend's.
constexpr uint32_t kFindChunk = 0x10;
constexpr uint32_t kFindRiff = 0x20;
constexpr uint32_t kFindList = 0x40;
// Errors.
constexpr uint32_t kErrFileNotFound = 257;
constexpr uint32_t kErrCannotOpen = 259;
constexpr uint32_t kErrCannotRead = 261;
constexpr uint32_t kErrChunkNotFound = 265;
constexpr uint32_t kErrUnbuffered = 266;
constexpr uint32_t kErrInvalidParam = 11; // MMSYSERR_INVALPARAM

constexpr uint32_t fourcc(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
           uint32_t(uint8_t(s[3])) << 24;
}
constexpr uint32_t kRiff = fourcc("RIFF");
constexpr uint32_t kList = fourcc("LIST");
constexpr uint32_t kMemory = fourcc("MEM ");

// MMIOINFO's fields.
enum : uint32_t {
    kInfoErrorRet = 12,
    kInfoBufferSize = 20,
    kInfoBuffer = 24,
    kInfoNext = 28,
    kInfoEndRead = 32,
    kInfoEndWrite = 36,
    kInfoBufOffset = 40,
    kInfoDiskOffset = 44,
    kInfoHandle = 68,
    kInfoSize = 72,
};

} // namespace

uint32_t Win32Process::MmioFile::read(Cpu &c, uint32_t at, uint32_t bytes, uint8_t *to) const {
    if (at >= size)
        return 0;
    bytes = std::min(bytes, size - at);
    std::memmove(to, inMemory ? c.memory().data() + memory + at : data.data() + at, bytes);
    return bytes;
}

void Win32Process::addMmioStandIns() {
    const char *w = "WINMM.dll";
    auto file = [this](uint32_t handle) -> MmioFile * {
        auto it = _mmioFiles.find(handle);
        return it == _mmioFiles.end() ? nullptr : &it->second;
    };
    // The buffer holding the file's bytes from `at` on: a memory file's is
    // its memory, whole.
    auto fill = [](Cpu &c, MmioFile &f, uint32_t at) {
        if (f.inMemory) {
            f.bufferOffset = 0;
            f.buffered = f.size;
            return;
        }
        f.bufferOffset = at;
        f.buffered = f.read(c, at, kBufferSize, c.memory().data() + f.buffer);
    };
    auto writeInfo = [](Cpu &c, const MmioFile &f, uint32_t handle, uint32_t info) {
        std::memset(c.memory().data() + info, 0, kInfoSize);
        c.write32(info + kInfoBufferSize, f.inMemory ? f.size : kBufferSize);
        c.write32(info + kInfoBuffer, f.buffer);
        c.write32(info + kInfoNext, f.buffer + (f.position - f.bufferOffset));
        c.write32(info + kInfoEndRead, f.buffer + f.buffered);
        c.write32(info + kInfoEndWrite, f.buffer + (f.inMemory ? f.size : kBufferSize));
        c.write32(info + kInfoBufOffset, f.bufferOffset);
        c.write32(info + kInfoDiskOffset, f.bufferOffset + f.buffered);
        c.write32(info + kInfoHandle, handle);
    };

    // mmioOpenA(name, MMIOINFO*, flags): a file of the drives, or with
    // MMIOINFO's I/O procedure 'MEM ' (+4), its buffer (+24, +20 bytes).
    stdcall(w, "mmioOpenA", 3, [this, fill](Cpu &c) {
        const uint32_t info = c.arg(1);
        const uint32_t flags = c.arg(2);
        auto fail = [&](uint32_t error) {
            if (info && c.valid(info, kInfoSize))
                c.write32(info + kInfoErrorRet, error);
            return 0u;
        };
        if ((flags & kAccessMask) || (flags & (kCreate | kParse | kDelete | kExist)))
            return fail(kErrCannotOpen); // only reading is stood in for
        MmioFile f;
        if (info && c.valid(info, kInfoSize) && c.read32(info + 4) == kMemory) {
            f.inMemory = true;
            f.memory = f.buffer = c.read32(info + kInfoBuffer);
            f.size = c.read32(info + kInfoBufferSize);
            if (!c.valid(f.memory, f.size))
                return fail(kErrInvalidParam);
        } else {
            if (!c.arg(0))
                return fail(kErrInvalidParam);
            const std::string name = fullPath(c.string(c.arg(0)));
            const auto path = mapPath ? mapPath(name, false) : std::nullopt;
            std::ifstream in;
            if (path)
                in.open(*path, std::ios::binary);
            const bool found = path && in;
            std::vector<uint8_t> bytes;
            if (found)
                bytes.assign(std::istreambuf_iterator<char>(in), {});
            if (fileOpened)
                fileOpened(name, found);
            if (!found)
                return fail(kErrFileNotFound);
            f.size = uint32_t(std::min<size_t>(bytes.size(), 0xffffffffu));
            f.data = std::move(bytes);
            f.buffer = memory().alloc(kBufferSize);
            if (!f.buffer)
                return fail(kErrCannotOpen);
        }
        fill(c, f, 0);
        const uint32_t handle = _nextMmioFile;
        _nextMmioFile += 4;
        _mmioFiles[handle] = std::move(f);
        if (info && c.valid(info, kInfoSize)) {
            c.write32(info + kInfoErrorRet, 0);
            c.write32(info + kInfoHandle, handle);
        }
        return handle;
    });
    stdcall(w, "mmioClose", 2, [this](Cpu &c) {
        auto it = _mmioFiles.find(c.arg(0));
        if (it == _mmioFiles.end())
            return kErrInvalidParam;
        if (!it->second.inMemory)
            memory().free(it->second.buffer);
        _mmioFiles.erase(it);
        return 0u;
    });
    // mmioRead(handle, to, bytes): the bytes read, -1 if it fails.
    stdcall(w, "mmioRead", 3, [file](Cpu &c) {
        MmioFile *f = file(c.arg(0));
        const uint32_t bytes = c.arg(2);
        if (!f || !c.valid(c.arg(1), bytes))
            return 0xffffffffu;
        const uint32_t n = f->read(c, f->position, bytes, c.memory().data() + c.arg(1));
        f->position += n;
        return n;
    });
    // mmioSeek(handle, offset, SEEK_SET 0, SEEK_CUR 1 or SEEK_END 2): the new
    // position, -1 if it fails.
    stdcall(w, "mmioSeek", 3, [file](Cpu &c) {
        MmioFile *f = file(c.arg(0));
        if (!f || c.arg(2) > 2)
            return 0xffffffffu;
        const int64_t from = c.arg(2) == 0 ? 0 : c.arg(2) == 1 ? f->position : f->size;
        const int64_t at = from + int32_t(c.arg(1));
        if (at < 0)
            return 0xffffffffu;
        f->position = uint32_t(at);
        return f->position;
    });
    // mmioGetInfo(handle, MMIOINFO*, flags): the buffer, as far as it's read,
    // from the current position.
    stdcall(w, "mmioGetInfo", 3, [file, fill, writeInfo](Cpu &c) {
        MmioFile *f = file(c.arg(0));
        if (!f || !c.valid(c.arg(1), kInfoSize))
            return kErrInvalidParam;
        if (f->position < f->bufferOffset || f->position > f->bufferOffset + f->buffered)
            fill(c, *f, f->position);
        writeInfo(c, *f, c.arg(0), c.arg(1));
        return 0u;
    });
    // mmioSetInfo(handle, MMIOINFO*, flags): where the program has read to.
    stdcall(w, "mmioSetInfo", 3, [file](Cpu &c) {
        MmioFile *f = file(c.arg(0));
        if (!f || !c.valid(c.arg(1), kInfoSize))
            return kErrInvalidParam;
        const uint32_t next = c.read32(c.arg(1) + kInfoNext);
        if (next >= f->buffer && next <= f->buffer + f->buffered)
            f->position = f->bufferOffset + (next - f->buffer);
        return 0u;
    });
    // mmioAdvance(handle, MMIOINFO*, MMIO_READ 0): the buffer refilled from
    // where the program has read to; at the end, empty.
    stdcall(w, "mmioAdvance", 3, [file, fill, writeInfo](Cpu &c) {
        MmioFile *f = file(c.arg(0));
        if (!f)
            return kErrInvalidParam;
        if (c.arg(2) & kAccessMask)
            return kErrCannotRead; // writing isn't stood in for
        const uint32_t info = c.arg(1);
        if (info && c.valid(info, kInfoSize)) {
            const uint32_t next = c.read32(info + kInfoNext);
            if (next >= f->buffer && next <= f->buffer + f->buffered)
                f->position = f->bufferOffset + (next - f->buffer);
        }
        if (f->inMemory) {
            if (info && c.valid(info, kInfoSize))
                writeInfo(c, *f, c.arg(0), info);
            return f->position >= f->size ? kErrCannotRead : 0u;
        }
        fill(c, *f, f->position);
        if (info && c.valid(info, kInfoSize))
            writeInfo(c, *f, c.arg(0), info);
        return f->buffer ? 0u : kErrUnbuffered;
    });
    // mmioDescend(handle, MMCKINFO*, parent MMCKINFO*, flags): into the next
    // chunk, or the first after it that's sought (MMIO_FINDCHUNK by its id,
    // MMIO_FINDRIFF and MMIO_FINDLIST by their form type) within the parent.
    // MMCKINFO: id, size, form type, data offset, flags.
    stdcall(w, "mmioDescend", 4, [file](Cpu &c) {
        MmioFile *f = file(c.arg(0));
        const uint32_t ck = c.arg(1);
        const uint32_t parent = c.arg(2);
        const uint32_t flags = c.arg(3);
        if (!f || !c.valid(ck, 20) || (parent && !c.valid(parent, 20)))
            return kErrInvalidParam;
        const uint32_t wantId = flags & kFindRiff ? kRiff : flags & kFindList ? kList : c.read32(ck);
        const uint32_t wantType = c.read32(ck + 8);
        const bool seeking = flags & (kFindChunk | kFindRiff | kFindList);
        const uint64_t end = parent ? uint64_t(c.read32(parent + 12)) + c.read32(parent + 4) : f->size;
        uint32_t at = f->position;
        while (uint64_t(at) + 8 <= end) {
            uint8_t header[12]{};
            if (f->read(c, at, 12, header) < 8)
                break;
            uint32_t id;
            uint32_t size;
            uint32_t type = 0;
            std::memcpy(&id, header, 4);
            std::memcpy(&size, header + 4, 4);
            const bool form = id == kRiff || id == kList;
            if (form)
                std::memcpy(&type, header + 8, 4);
            if (!seeking || (id == wantId && (!(flags & (kFindRiff | kFindList)) || type == wantType))) {
                c.write32(ck, id);
                c.write32(ck + 4, size);
                c.write32(ck + 8, type);
                c.write32(ck + 12, at + 8);
                c.write32(ck + 16, 0);
                f->position = at + (form ? 12 : 8);
                return 0u;
            }
            at += 8 + size + (size & 1);
        }
        return kErrChunkNotFound;
    });
    // mmioAscend(handle, MMCKINFO*, flags): past the chunk's end, padded to
    // a word.
    stdcall(w, "mmioAscend", 3, [file](Cpu &c) {
        MmioFile *f = file(c.arg(0));
        if (!f || !c.valid(c.arg(1), 20))
            return kErrInvalidParam;
        const uint32_t size = c.read32(c.arg(1) + 4);
        f->position = c.read32(c.arg(1) + 12) + size + (size & 1);
        return 0u;
    });
}

} // namespace reluva::x86
