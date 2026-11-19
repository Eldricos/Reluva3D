// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/formats/dialogue.h"

#include "common/bytes.h"
#include "common/log.h"
#include "common/text.h"

namespace reluva::stharbinger {

namespace {

const char *const kDialogueNames[kNumDialogues] = {
    "COM010",   "COM020",   "COM02X",   "COM02Y",   "COM030",   "COM050",   "COM051",   "COM060",   "COM070",
    "COM080",   "COM090",   "DAX000",   "DAX010",   "DAX011",   "DAX020",   "DAX021",   "DAX030",   "DAX05X",
    "DAX060",   "DAX070",   "HAVIK010", "HAVIK020", "JAN010",   "JAN020",   "JAN030",   "KARLOGS",  "KARPUZ01",
    "KEEL010",  "KIRA010",  "KIRA050",  "KIRA060",  "KIRA070",  "ODO010",   "ODO020",   "ODO030",   "ODO050",
    "ODO051",   "ODO052",   "ODO060",   "ODO080",   "QUARK010", "QUARK011", "QUARK020", "QUARK060", "QUARK070",
    "QUARK080", "QUARK090", "QUARKX1",  "RASMS010", "RASMS020", "SCYTH020", "SET010",   "SIS010",   "SIS020",
    "SIS030",   "SIS070",   "SYR010",   "VOINVIS1", "VOINVIS2", "VOINVIS3", "VOKMSG01", "VOODO010", "VOODO020",
    "WEY010",   "XMIDARC5", "XMIDARC9", "XODO010",  "XOQ010",   "XQD010",   "XSDK030",  "YAR010",   "YAR020",
};

bool readTree(const uint8_t *data, uint32_t size, uint32_t &pos, std::vector<DialogueGroup> &out, std::string *error) {
    if (pos >= size)
        return false;
    int groups = data[pos++];
    int done = 0;
    while (done < groups) {
        if (pos >= size)
            return false;
        int count = data[pos++];
        DialogueGroup group;
        for (int i = 0; i < count; i++) {
            if (pos + 4 > size)
                return false;
            DialogueItem item;
            item.line = readLE16(data + pos);
            item.choice = readLE16(data + pos + 2);
            pos += 4;
            if (!readDirectiveList(data, size, pos, item.script, error) || pos >= size)
                return false;
            int subs = data[pos++];
            for (int j = 0; j < subs; j++) {
                if (pos + 4 > size)
                    return false;
                DialogueItem::SubItem sub;
                sub.a = readLE16(data + pos);
                sub.b = readLE16(data + pos + 2);
                pos += 4;
                if (!readDirectiveList(data, size, pos, sub.script, error))
                    return false;
                item.subItems.push_back(std::move(sub));
            }
            group.push_back(std::move(item));
        }
        out.push_back(std::move(group));
        if (count)
            done++;
    }
    return true;
}

} // namespace

const char *getDialogueName(int index) {
    return index >= 0 && index < kNumDialogues ? kDialogueNames[index] : "";
}

int findDialogue(const std::string &name) {
    for (int i = 0; i < kNumDialogues; i++)
        if (text::equalsIgnoreCase(name, kDialogueNames[i]))
            return i;
    return -1;
}

bool readDialogue(const uint8_t *data, uint32_t size, Dialogue &out, std::string *error) {
    uint32_t pos = 0;
    std::string err;
    bool ok = readDirectiveList(data, size, pos, out.listA, &err) &&
              readDirectiveList(data, size, pos, out.listB, &err) && readTree(data, size, pos, out.npcLines, &err) &&
              readTree(data, size, pos, out.choices, &err);
    if (!ok && error)
        *error = err.empty() ? std::string("dialogue record truncated") : err;
    return ok;
}

bool readPhonemes(const uint8_t *data, uint32_t size, Phonemes &out) {
    out = Phonemes();
    if (size < 4)
        return false;
    out.unitMillis = uint32_t(uint64_t(readLE32(data)) * 100000 / 2205000);
    uint32_t pos = 4;
    while (pos < size && data[pos] != 0xFF) {
        if (pos + 2 > size)
            return false;
        Phonemes::Node node;
        node.code = data[pos];
        node.duration = data[pos + 1];
        pos += 2;
        while (pos < size && data[pos]) // the word, unused
            pos++;
        pos++;
        out.nodes.push_back(node);
    }
    return !out.nodes.empty();
}

uint32_t Phonemes::getNodeMillis(size_t i) const {
    uint32_t ms = (unitMillis * nodes[i].duration) >> 6;
    return ms ? ms : unitMillis;
}

std::vector<std::string> splitStrings(const uint8_t *data, uint32_t size, char separator) {
    std::vector<std::string> out;
    std::string cur;
    for (uint32_t i = 0; i < size; i++) {
        char c = char(data[i]);
        if (c == separator) {
            out.push_back(separator == '\n' ? text::trim(cur) : cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty() && text::trim(cur) != "\x1a")
        out.push_back(separator == '\n' ? text::trim(cur) : cur);
    return out;
}

std::string Dialogue::getMenuLabel(int line) const {
    if (line < 0 || line >= int(text.size()))
        return std::string();
    const std::string &t = text[line];
    size_t start = t.find_first_not_of(' ');
    if (start == std::string::npos || t[start] != '[')
        return std::string();
    size_t end = t.find(']', start);
    return end == std::string::npos ? std::string() : t.substr(start + 1, end - start - 1);
}

std::string Dialogue::getSpokenText(int line) const {
    if (line < 0 || line >= int(text.size()))
        return std::string();
    std::string out;
    const std::string &t = text[line];
    size_t i = 0;
    size_t start = t.find_first_not_of(' ');
    if (start != std::string::npos && t[start] == '[') {
        size_t end = t.find(']', start);
        if (end != std::string::npos)
            i = end + 1;
    }
    int depth = 0;
    for (; i < t.size(); i++) {
        if (t[i] == '{')
            depth++;
        else if (t[i] == '}' && depth)
            depth--;
        else if (!depth)
            out += t[i];
    }
    return text::trim(out);
}

} // namespace reluva::stharbinger
