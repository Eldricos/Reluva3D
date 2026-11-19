// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::botf {

// The game's data tables in stbof.res (docs/engines/botf/formats/tables.md): fixed-size
// records, some with a count or a header in front, many with a description
// in a companion file of zero-terminated strings. Each table is described by
// a TableLayout; fields not worked out yet aren't listed, and show as bytes.

enum class FieldType {
    kText, // zero-terminated within `size` bytes (what follows the zero is leftover)
    kU8,   // `size` of them
    kU16,  // `size` of them
    kS16,  // `size` of them, signed
    kU32,  // `size` of them
    kS32,  // `size` of them, signed
    kF32,
    kF64,
};

/** One field of a table's records. */
struct Field {
    const char *name;
    uint32_t offset;
    FieldType type;
    uint32_t size = 1; // Text: bytes; U8, U16, S16, U32, S32: how many
};

/** How a table's file is laid out: its count, header, records and fields. */
struct TableLayout {
    enum class Count { kU16AtStart, kU32AtStart, kFileSize };

    const char *file;         // "race.rst"
    const char *descriptions; // the companion file of descriptions, or nullptr
    Count count;
    uint32_t headerSize; // bytes before the first record
    uint32_t recordSize;
    std::vector<Field> fields;
    int nameField = 0;         // index into fields, or -1 for none
    int descriptionField = -1; // a U32 offset into the descriptions, or -1
    /**
     * Names for records that hold none: a fixed list, or a list of
     * fixed-size strings at an offset in the header (the .wtf tables).
     */
    std::vector<const char *> recordNames = {};
    uint32_t headerNamesAt = 0;
    uint32_t headerNameSize = 0;
};

/** Every table worked out so far. */
const std::vector<TableLayout> &tableLayouts();

/** A table read with its layout: its records, their fields' values and descriptions. */
class DataTable {
public:
    /** `descriptions` may be empty when the table has none or it's missing. */
    Error load(const TableLayout &layout, std::vector<uint8_t> data, std::vector<uint8_t> descriptions = {});

    const TableLayout &layout() const { return *_layout; }
    size_t count() const { return _count; }
    const uint8_t *record(size_t i) const { return _data.data() + _layout->headerSize + i * _layout->recordSize; }
    /**
     * A field's value as text: strings as they are (Windows-1252 decoded),
     * numbers in decimal, U8 arrays space-separated.
     */
    std::string value(size_t record, const Field &field) const;
    std::string name(size_t record) const;
    std::string description(size_t record) const; // empty if none
    /** Which of a record's bytes no field covers, as [begin, end) ranges. */
    std::vector<std::pair<uint32_t, uint32_t>> unknownRanges() const;

private:
    const TableLayout *_layout = nullptr;
    std::vector<uint8_t> _data;
    std::vector<uint8_t> _descriptions;
    size_t _count = 0;
};

} // namespace reluva::botf
