// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/script.h"
#include "common/bytes.h"

#include "engines/stgenerations/debug.h"
#include "engines/stgenerations/formats/opcodes.h"

namespace reluva::stgenerations {

bool ScriptFile::load(SeekableReadStream& stream) {
    _code.clear();
    _resourceNames.clear();

    if (stream.readUint32BE() != tagBE('S', 'G', 'E', 'N')) {
        log::warning("ScriptFile: missing SGEN header");
        return false;
    }
    _version = stream.readUint16LE();
    stream.readUint16LE();
    uint32_t size = stream.readUint32LE();
    uint32_t codeSize = stream.readUint32LE();
    uint32_t sectionSizes[kSectionCount];
    uint64_t total = 40 + (uint64_t)codeSize;
    for (uint32_t& sectionSize : sectionSizes) {
        sectionSize = stream.readUint32LE();
        total += sectionSize;
    }

    // AMARGOS2's GAME.BIN has 72 bytes of padding after what its header
    // counts; anything shorter than the header says is damaged.
    if ((int64_t)size + 12 > stream.size() || total > (uint64_t)size + 12 || (codeSize & 1)) {
        log::warning("ScriptFile: header sizes do not match a %lld byte file", (long long)stream.size());
        return false;
    }

    _code.resize(codeSize / 2);
    for (uint16_t& word : _code)
        word = stream.readUint16LE();
    for (int i = 0; i < kSectionCount; i++) {
        _sections[i].resize(sectionSizes[i]);
        stream.read(_sections[i].data(), sectionSizes[i]);
    }

    std::vector<uint8_t> names = stream.readAll();
    std::string name;
    for (uint8_t c : names) {
        if (c) {
            name += (char)c;
        } else if (!name.empty()) {
            _resourceNames.push_back(name);
            name.clear();
        }
    }
    if (!name.empty())
        _resourceNames.push_back(name);
    return parseProgram();
}

bool ScriptFile::parseProgram() {
    _objects.clear();
    const size_t size = _code.size();
    size_t p = 0;
    auto word = [&](size_t i) -> uint16_t { return i < size ? _code[i] : 0x1000; };

    while (word(p) != kEndOfProgram) {
        if (p + 2 > size) {
            log::warning("ScriptFile: program runs past the end of the code");
            return false;
        }
        ScriptObject object;
        object.type = word(p);
        object.id = word(p + 1);
        p += 2;

        while (true) {
            uint16_t header = word(p);
            if ((header & 0xF000) != kStateMarker) {
                log::warning("ScriptFile: expected a state at word %u, found %04x", (unsigned)p, header);
                return false;
            }
            ScriptState state;
            state.id = header & 0x0FFF;
            p++;

            while (word(p) != kEndOfState) {
                ScriptBlock block;
                block.event = word(p);
                block.nextState = (int16_t)word(p + 1);
                block.codeStart = (uint32_t)(p + 2);
                p += 2;
                while (word(p) != OP_END) {
                    p += getInstructionLength(word(p));
                    if (p >= size) {
                        log::warning("ScriptFile: block at word %u has no end", block.codeStart);
                        return false;
                    }
                }
                p++;
                state.blocks.push_back(block);
            }
            p++;
            object.states.push_back(std::move(state));

            if (word(p) == kEndOfObject) {
                p++;
                break;
            }
        }
        _objects.push_back(std::move(object));
    }

    if (p + 1 != size) {
        log::warning("ScriptFile: program ends at word %u of %u", (unsigned)p + 1, (unsigned)size);
        return false;
    }
    return true;
}

} // namespace reluva::stgenerations
