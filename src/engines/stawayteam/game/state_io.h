// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stawayteam/formats/text_object.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace reluva::stawayteam {

/**
 * Writes or reads a game in progress as Reluva saves it: the same calls
 * either way, each value written as it's met or read back into it. A
 * pointer into one of the game's text objects (a map's, Rules.txt's, an
 * item's...) is saved as its tree's name and its place in it, counted
 * object by object from the top.
 */
class StateIo {
public:
    /** Finds a tree by the name it was saved under, loading it if need be; nullptr if there's none. */
    using Resolver = std::function<const TextObject *(const std::string &name)>;

    /** A writer. */
    StateIo();
    /** A reader of `data`, its trees found by `resolve`. */
    StateIo(std::vector<uint8_t> data, Resolver resolve);

    bool reading() const { return _reading; }
    /** Whether what was read ran short, or named what isn't there. */
    bool failed() const { return _failed; }
    /** What's been written. */
    const std::vector<uint8_t> &data() const { return _data; }
    /** Pointers written that weren't in any tree added: saved as none. */
    size_t missed() const { return _missed; }

    /** A tree the pointers written may point into, by its name. */
    void addRoot(const std::string &name, const TextObject *root);

    void io(bool &v);
    void io(std::string &v);
    void io(float &v);
    void io(double &v);
    void io(const TextObject *&v);
    template<class T>
        requires std::is_integral_v<T> || std::is_enum_v<T>
    void io(T &v) {
        int64_t n = 0;
        if (!_reading)
            n = int64_t(v);
        whole(n);
        if (_reading)
            v = T(n);
    }
    template<class A, class B>
    void io(std::pair<A, B> &v) {
        io(v.first);
        io(v.second);
    }
    template<class T>
    void io(std::vector<T> &v) {
        list(v, [&](T &each) { io(each); });
    }
    /** A list, each of its items by `each`. */
    template<class T, class F>
    void list(std::vector<T> &v, F each) {
        int64_t n = int64_t(v.size());
        whole(n);
        if (_reading) {
            if (n < 0 || n > int64_t(_data.size())) {
                _failed = true;
                n = 0;
            }
            v.assign(size_t(n), T{});
        }
        for (T &item : v)
            each(item);
    }
    /** A map, each of its entries by `each(key, value)`. */
    template<class K, class V, class F>
    void map(std::map<K, V> &m, F each) {
        int64_t n = int64_t(m.size());
        whole(n);
        if (!_reading) {
            for (auto &[key, value] : m) {
                K k = key;
                each(k, value);
            }
            return;
        }
        m.clear();
        for (int64_t i = 0; i < n && !_failed; ++i) {
            K k{};
            V value{};
            each(k, value);
            m.emplace(std::move(k), std::move(value));
        }
    }

private:
    void whole(int64_t &n);
    void bytes(void *p, size_t size);

    bool _reading = false;
    bool _failed = false;
    size_t _missed = 0;
    std::vector<uint8_t> _data;
    size_t _at = 0;
    // Writing: each object's tree and place; reading: each tree's objects
    // in order, as they're asked for.
    std::vector<std::string> _rootNames;
    std::unordered_map<const TextObject *, std::pair<size_t, uint32_t>> _places;
    Resolver _resolve;
    std::map<std::string, std::vector<const TextObject *>> _trees;
};

} // namespace reluva::stawayteam
