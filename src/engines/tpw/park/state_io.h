// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/tpw/park/park.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

/**
 * Writing a park's state, and the screen's, out and reading it back: one
 * function a structure, listing its fields, for both. See
 * docs/engines/tpw/games/simcoaster/formats/reluva-saves.md.
 */
namespace reluva::tpw::stateio {

/** Writes the fields it's given, little-endian. */
class Writer {
public:
    static constexpr bool kReading = false;
    explicit Writer(std::vector<uint8_t> &out) : _out(out) {}
    void bytes(void *p, size_t n) {
        const auto *b = static_cast<const uint8_t *>(p);
        _out.insert(_out.end(), b, b + n);
    }
    // How many of a list follow.
    size_t count(size_t n) {
        uint32_t c = uint32_t(n);
        bytes(&c, sizeof c);
        return n;
    }
    bool ok() const { return true; }

private:
    std::vector<uint8_t> &_out;
};

/** Reads them back; past the end, or a count that can't be, and it's bad. */
class Reader {
public:
    static constexpr bool kReading = true;
    Reader(const uint8_t *data, size_t size) : _at(data), _left(size) {}
    void bytes(void *p, size_t n) {
        if (_bad || n > _left) {
            _bad = true;
            std::memset(p, 0, n);
            return;
        }
        std::memcpy(p, _at, n);
        _at += n;
        _left -= n;
    }
    size_t count(size_t) {
        uint32_t c = 0;
        bytes(&c, sizeof c);
        // No list has more members than bytes left to hold them.
        if (c > _left) {
            _bad = true;
            return 0;
        }
        return c;
    }
    bool ok() const { return !_bad; }
    bool atEnd() const { return _left == 0; }

private:
    const uint8_t *_at;
    size_t _left;
    bool _bad = false;
};

template<class A, class T>
void io(A &a, T &v)
    requires std::is_arithmetic_v<T> || std::is_enum_v<T>
{
    a.bytes(&v, sizeof v);
}

template<class A>
void io(A &a, std::string &s) {
    const size_t n = a.count(s.size());
    if constexpr (A::kReading)
        s.resize(n);
    if (n)
        a.bytes(s.data(), n);
}

template<class A, class T>
void io(A &a, std::vector<T> &v);
template<class A, class T, size_t N>
void io(A &a, std::array<T, N> &v);
template<class A, class K, class V>
void io(A &a, std::map<K, V> &m);
template<class A, class T>
void io(A &a, std::optional<T> &v);
template<class A, class T, class U>
void io(A &a, std::pair<T, U> &p);

template<class A>
void io(A &a, Park::Cell &c) {
    io(a, c.x);
    io(a, c.y);
}

template<class A>
void io(A &a, Park::Candidate &c) {
    io(a, c.name);
    io(a, c.experience);
    io(a, c.wage);
    io(a, c.agency);
}

template<class A>
void io(A &a, Park::StaffMember &m) {
    io(a, static_cast<Park::Candidate &>(m));
    io(a, m.id);
    io(a, m.kind);
    io(a, m.x);
    io(a, m.z);
    io(a, m.headingX);
    io(a, m.headingZ);
    io(a, m.walked);
    io(a, m.cell);
    io(a, m.from);
    io(a, m.route);
    io(a, m.working);
    io(a, m.workUntil);
    io(a, m.doing);
    io(a, m.energy);
    io(a, m.happiness);
    io(a, m.motivation);
    io(a, m.hiredDay);
    io(a, m.target);
    io(a, m.trained);
    io(a, m.patrolling);
    io(a, m.patrolFrom);
    io(a, m.patrolTo);
    io(a, m.ability);
    io(a, m.learning);
    io(a, m.chasing);
    io(a, m.chaseUntil);
}

template<class A>
void io(A &a, Park::WageDemand &d) {
    io(a, d.staff);
    io(a, d.raise);
}

template<class A>
void io(A &a, Park::Project &p) {
    io(a, p.kind);
    io(a, p.id);
    io(a, p.level);
    io(a, p.cost);
    io(a, p.done);
    io(a, p.scientists);
    io(a, p.finished);
}

template<class A>
void io(A &a, Park::Litter &l) {
    io(a, l.x);
    io(a, l.z);
    io(a, l.frame);
    io(a, l.vomit);
}

template<class A>
void io(A &a, Park::StinkBomb &b) {
    io(a, b.x);
    io(a, b.z);
    io(a, b.since);
    io(a, b.until);
}

template<class A>
void io(A &a, Park::Visitor &v) {
    io(a, v.id);
    io(a, v.generation);
    io(a, v.speed);
    io(a, v.walked);
    io(a, v.x);
    io(a, v.z);
    io(a, v.headingX);
    io(a, v.headingZ);
    uint64_t leg = v.leg;
    io(a, leg);
    v.leg = size_t(leg);
    io(a, v.paid);
    io(a, v.inPark);
    io(a, v.doing);
    io(a, v.cell);
    io(a, v.from);
    io(a, v.queue);
    io(a, v.along);
    io(a, v.lastQueue);
    io(a, v.rides);
    io(a, v.choices);
    io(a, v.type);
    io(a, v.cash);
    io(a, v.happiness);
    io(a, v.hunger);
    io(a, v.thirst);
    io(a, v.toilet);
    io(a, v.bored);
    io(a, v.daysLeft);
    io(a, v.target);
    io(a, v.route);
    io(a, v.gone);
    io(a, v.person);
    io(a, v.female);
    io(a, v.sickness);
    io(a, v.carrying);
    io(a, v.appearance);
    io(a, v.prankster);
    io(a, v.thought);
    io(a, v.thoughtUntil);
    io(a, v.nextThought);
    io(a, v.ridden);
    io(a, v.purchases);
    io(a, v.sideshowsPlayed);
    io(a, v.sideshowsWon);
    io(a, v.spent);
    io(a, v.arrived);
}

template<class A>
void io(A &a, Park::PlacedObject &o) {
    io(a, o.id);
    io(a, o.at);
    io(a, o.angle);
    io(a, o.repair);
    io(a, o.broken);
    io(a, o.open);
    io(a, o.water);
    io(a, o.engineer);
    io(a, o.gardener);
    io(a, o.users);
    io(a, o.lastUsers);
    io(a, o.takings);
    io(a, o.lastTakings);
    io(a, o.costs);
    io(a, o.lastCosts);
    io(a, o.goods);
    io(a, o.lastGoods);
    io(a, o.profit);
    io(a, o.placedDay);
    io(a, o.level);
    io(a, o.upgrading);
    io(a, o.name);
    io(a, o.prebuilt);
    io(a, o.active);
}

template<class A>
void io(A &a, Park::Queue &q) {
    io(a, q.entrance);
    io(a, q.cells);
    io(a, q.atPath);
    io(a, q.pathEnd);
    io(a, q.object);
    io(a, q.kiosk);
    io(a, q.fare);
    io(a, q.hasExit);
    io(a, q.exit);
    io(a, q.line);
}

template<class A>
void io(A &a, Park::Loan &l) {
    io(a, l.offer);
    io(a, l.lender);
    io(a, l.amount);
    io(a, l.rate);
    io(a, l.months);
    io(a, l.repayment);
    io(a, l.monthsLeft);
    io(a, l.balance);
}

template<class A>
void io(A &a, Park::Shares &s) {
    io(a, s.issued);
    io(a, s.owned);
    io(a, s.price);
}

template<class A>
void io(A &a, Park::Message::Line &l) {
    io(a, l.text);
    io(a, l.values);
    io(a, l.suffix);
}

template<class A>
void io(A &a, Park::Message &m) {
    io(a, m.title);
    io(a, m.text);
    io(a, m.values);
    io(a, m.lines);
    io(a, m.objectiveLetter);
    io(a, m.challenge);
    io(a, m.sender);
    io(a, m.rank);
}

// A challenge's own settings come from Challenges.sam; its standing is
// play's.
template<class A>
void io(A &a, Park::ChallengeState &c) {
    io(a, c.challenge.number);
    io(a, c.status);
    io(a, c.failures);
    io(a, c.daysLeft);
    io(a, c.baseline);
    io(a, c.baseline2);
    io(a, c.triggered);
    io(a, c.warned);
}

template<class A, class T>
void io(A &a, std::vector<T> &v) {
    const size_t n = a.count(v.size());
    if constexpr (A::kReading)
        v.assign(n, T{});
    for (T &x : v)
        io(a, x);
}

template<class A, class T, size_t N>
void io(A &a, std::array<T, N> &v) {
    for (T &x : v)
        io(a, x);
}

template<class A, class K, class V>
void io(A &a, std::map<K, V> &m) {
    const size_t n = a.count(m.size());
    if constexpr (A::kReading) {
        m.clear();
        for (size_t i = 0; i < n && a.ok(); ++i) {
            K k{};
            V v{};
            io(a, k);
            io(a, v);
            m.emplace(std::move(k), std::move(v));
        }
    } else {
        for (auto &[k, v] : m) {
            K key = k;
            io(a, key);
            io(a, v);
        }
    }
}

template<class A, class T>
void io(A &a, std::optional<T> &v) {
    bool has = v.has_value();
    io(a, has);
    if constexpr (A::kReading)
        v = has ? std::optional<T>(T{}) : std::nullopt;
    if (has)
        io(a, *v);
}

template<class A, class T, class U>
void io(A &a, std::pair<T, U> &p) {
    io(a, p.first);
    io(a, p.second);
}

} // namespace reluva::tpw::stateio
