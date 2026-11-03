// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/dictionary.h"
#include <algorithm>
#include <bit>

namespace reluva::boggle {
namespace {
constexpr uint32_t alphabet = 0x07ffffff;
constexpr size_t maxWord = 64, maxEntries = 2000000, maxText = 64 * 1024 * 1024;
class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& data) : data_(data) {}
    Error read(std::vector<std::string>& words) {
        if (data_.size() > 32 * 1024 * 1024 || !node({}, words) || at_ != data_.size())
            return Error::fail("Boggle dictionary: invalid/truncated tree, suffix list or size limit");
        return Error::success();
    }
private:
    bool u32(uint32_t& n) {
        if (data_.size() - at_ < 4) return false;
        n = uint32_t(data_[at_]) | uint32_t(data_[at_ + 1]) << 8 |
            uint32_t(data_[at_ + 2]) << 16 | uint32_t(data_[at_ + 3]) << 24;
        at_ += 4; return true;
    }
    static char letter(unsigned bit) { return bit == 26 ? char(0xd1) : char('A' + bit); }
    bool emit(const std::string& prefix, std::string_view suffix, std::vector<std::string>& words) {
        const size_t n = prefix.size() + suffix.size();
        if (!n || n > maxWord || words.size() >= maxEntries || n > maxText - textBytes_) return false;
        textBytes_ += n;
        words.push_back(prefix + std::string(suffix));
        return true;
    }
    bool node(const std::string& prefix, std::vector<std::string>& words) {
        if (prefix.size() >= maxWord || ++nodes_ > 1000000) return false;
        uint32_t terminal = 0, continuation = 0;
        if (!u32(terminal) || !u32(continuation) || (terminal & ~(alphabet | 0x80000000u)) ||
            (continuation & ~alphabet)) return false;
        for (unsigned bit = 0; bit < 27; ++bit)
            if (terminal & (1u << bit)) {
                const char c = letter(bit);
                if (!emit(prefix, std::string_view(&c, 1), words)) return false;
            }
        if (!continuation) return true;
        if (terminal & 0x80000000u) {
            if (size_t(std::popcount(continuation)) > (data_.size() - at_) / 8) return false;
            for (unsigned bit = 0; bit < 27; ++bit)
                if ((continuation & (1u << bit)) && !node(prefix + letter(bit), words)) return false;
            return true;
        }
        uint32_t size = 0;
        if (!u32(size) || size < 3 || size > data_.size() - at_) return false;
        const size_t end = at_ + size;
        size_t suffixes = 0;
        while (at_ < end && data_[at_]) {
            const size_t begin = at_;
            while (at_ < end && data_[at_]) {
                const auto c = data_[at_++];
                if (!(c >= 'A' && c <= 'Z') && c != 0xd1 && c != 0xf1) return false;
                if (at_ - begin > maxWord - prefix.size()) return false;
            }
            if (at_ == end || at_ - begin < 2 ||
                !emit(prefix, std::string_view(reinterpret_cast<const char*>(data_.data() + begin), at_ - begin), words)) return false;
            ++at_; ++suffixes;
        }
        if (!suffixes || at_ + 1 != end || data_[at_] != 0) return false;
        at_ = end;
        return true;
    }
    const std::vector<uint8_t>& data_;
    size_t at_ = 0, nodes_ = 0, textBytes_ = 0;
};
}
Error Dictionary::read(const std::vector<uint8_t>& bytes) {
    words_.clear(); entries_ = 0;
    std::vector<std::string> words;
    if (auto err = Reader(bytes).read(words)) return err;
    entries_ = words.size();
    std::sort(words.begin(), words.end());
    words.erase(std::unique(words.begin(), words.end()), words.end());
    words_ = std::move(words);
    return Error::success();
}
bool Dictionary::contains(std::string_view word) const {
    return !word.empty() && std::binary_search(words_.begin(), words_.end(), word);
}
bool Dictionary::hasPrefix(std::string_view prefix) const {
    const auto i = std::lower_bound(words_.begin(), words_.end(), prefix);
    return i != words_.end() && i->starts_with(prefix);
}
} // namespace reluva::boggle
