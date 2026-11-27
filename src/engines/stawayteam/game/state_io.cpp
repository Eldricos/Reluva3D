// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/state_io.h"

#include <cstring>

namespace reluva::stawayteam {

namespace {

// A tree's objects, each before those in its fields, in its fields' order.
void collect(const TextObject *o, std::vector<const TextObject *> &out) {
    out.push_back(o);
    for (const TextField &f : o->fields)
        if (f.object)
            collect(f.object.get(), out);
}

} // namespace

StateIo::StateIo() = default;

StateIo::StateIo(std::vector<uint8_t> data, Resolver resolve)
    : _reading(true), _data(std::move(data)), _resolve(std::move(resolve)) {
}

void StateIo::addRoot(const std::string &name, const TextObject *root) {
    if (!root)
        return;
    std::vector<const TextObject *> all;
    collect(root, all);
    const size_t tree = _rootNames.size();
    _rootNames.push_back(name);
    for (size_t i = 0; i < all.size(); ++i)
        _places.emplace(all[i], std::make_pair(tree, uint32_t(i)));
}

void StateIo::bytes(void *p, size_t size) {
    if (!_reading) {
        const auto *b = static_cast<const uint8_t *>(p);
        _data.insert(_data.end(), b, b + size);
        return;
    }
    if (_failed || _at + size > _data.size()) {
        _failed = true;
        std::memset(p, 0, size);
        return;
    }
    std::memcpy(p, _data.data() + _at, size);
    _at += size;
}

void StateIo::whole(int64_t &n) {
    // Little-endian, eight bytes.
    uint8_t b[8];
    if (!_reading)
        for (int i = 0; i < 8; ++i)
            b[i] = uint8_t(uint64_t(n) >> (8 * i));
    bytes(b, sizeof b);
    if (_reading) {
        uint64_t u = 0;
        for (int i = 0; i < 8; ++i)
            u |= uint64_t(b[i]) << (8 * i);
        n = int64_t(u);
    }
}

void StateIo::io(bool &v) {
    int64_t n = v ? 1 : 0;
    whole(n);
    v = n != 0;
}

void StateIo::io(float &v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, sizeof u);
    int64_t n = u;
    whole(n);
    u = uint32_t(n);
    std::memcpy(&v, &u, sizeof v);
}

void StateIo::io(double &v) {
    uint64_t u = 0;
    std::memcpy(&u, &v, sizeof u);
    int64_t n = int64_t(u);
    whole(n);
    u = uint64_t(n);
    std::memcpy(&v, &u, sizeof v);
}

void StateIo::io(std::string &v) {
    int64_t n = int64_t(v.size());
    whole(n);
    if (_reading) {
        if (n < 0 || _at + size_t(n) > _data.size()) {
            _failed = true;
            v.clear();
            return;
        }
        v.assign(reinterpret_cast<const char *>(_data.data() + _at), size_t(n));
        _at += size_t(n);
        return;
    }
    bytes(v.data(), v.size());
}

void StateIo::io(const TextObject *&v) {
    // Its tree's name and its place there; none, an empty name.
    std::string tree;
    int64_t place = 0;
    if (!_reading && v) {
        const auto it = _places.find(v);
        if (it != _places.end()) {
            tree = _rootNames[it->second.first];
            place = it->second.second;
        } else {
            ++_missed;
        }
    }
    io(tree);
    whole(place);
    if (!_reading)
        return;
    v = nullptr;
    if (tree.empty())
        return;
    auto found = _trees.find(tree);
    if (found == _trees.end()) {
        std::vector<const TextObject *> all;
        if (const TextObject *root = _resolve ? _resolve(tree) : nullptr)
            collect(root, all);
        found = _trees.emplace(tree, std::move(all)).first;
    }
    if (place < 0 || size_t(place) >= found->second.size()) {
        _failed = true;
        return;
    }
    v = found->second[size_t(place)];
}

} // namespace reluva::stawayteam
