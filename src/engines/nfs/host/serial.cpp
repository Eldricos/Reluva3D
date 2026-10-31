// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Serial ports for nfs3.exe's serial (null-modem) play: COM1-COM9 are one
// link over TCP (Options::serial), what one copy writes the other reads,
// as through a null-modem cable. One copy listens, the other connects.
//
// And a modem for its modem play: TAPI32 (which the game loads itself),
// one line device whose calls are the same kind of link (Options::modem).
// Dialing connects (to the number typed, if it's an address, else to the
// option's host); a line opened to answer listens, and a connection coming
// in is a call offered. The call's data goes through the comm handle
// lineGetID gives ("comm/datamodem"), as on Windows. From Microsoft's
// published TAPI 1.4 documentation.
// The comm functions (KERNEL32's GetCommState, SetCommTimeouts...) are
// from Microsoft's published Win32 documentation; the line settings are
// kept and change nothing, as a TCP link has no speed or parity.

#include "engines/nfs/host/game_host.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <optional>

namespace reluva::nfs {

namespace {

using x86::Cpu;
using Result = net::Sockets::Result;

constexpr uint32_t kMaxDword = 0xffffffffu, kErrorGenFailure = 31;

} // namespace

// The link: a TCP connection, made when the port is opened (connecting)
// or when the other end connects (listening).
// A COM port as the comm functions see it: its line settings and
// timeouts, whether the other end is there, and what's waiting.
class GameHost::CommPort : public x86::DeviceObject {
public:
    virtual bool connected() = 0;
    virtual void purge() = 0;

    std::array<uint32_t, 5> timeouts_{}; // COMMTIMEOUTS
    std::array<uint8_t, 28> dcb_{};      // the DCB last set
    bool dcbSet_ = false;
};

class GameHost::SerialLink : public CommPort {
public:
    SerialLink(GameHost& host, net::Sockets& net) : host_(host), net_(net) {}
    ~SerialLink() override {
        if (stream_ >= 0)
            net_.close(stream_);
        if (listener_ >= 0)
            net_.close(listener_);
    }

    // Starts listening on `at` or connecting to it.
    bool start(bool listen, const net::Address& at) {
        if (listen) {
            listener_ = net_.open(net::Sockets::Stream);
            if (listener_ < 0)
                return false;
            net_.setOption(listener_, net::Sockets::ReuseAddress, 1);
            if (net_.bind(listener_, at) != Result::Ok || net_.listen(listener_, 1) != Result::Ok)
                return false;
            host_.say("  serial: listening on " + at.toString());
            return true;
        }
        target_ = at;
        host_.say("  serial: connecting to " + at.toString());
        return dial();
    }

    // A connection to target_ under way (again, after one that failed:
    // the other copy may not be listening yet).
    bool dial() {
        if (stream_ >= 0)
            net_.close(stream_);
        lastDial_ = std::chrono::steady_clock::now();
        stream_ = net_.open(net::Sockets::Stream);
        if (stream_ < 0)
            return false;
        if (host_.options_.netAddress)
            net_.bind(stream_, {host_.options_.netAddress, 0});
        const Result r = net_.connect(stream_, target_);
        connecting_ = r == Result::InProgress || r == Result::WouldBlock;
        connected_ = r == Result::Ok;
        return true;
    }

    // Whether the other end is there (the cable's carrier); accepts or
    // finishes connecting first, without waiting.
    bool connected() override {
        if (connected_)
            return true;
        if (listener_ >= 0 && stream_ < 0) {
            int accepted = -1;
            net::Address from;
            if (net_.accept(listener_, accepted, from) == Result::Ok) {
                stream_ = accepted;
                connected_ = true;
                net_.setOption(stream_, net::Sockets::NoDelay, 1);
                host_.say("  serial: connected from " + from.toString());
            }
        } else if (connecting_) {
            const net::Sockets::Ready ready = net_.ready(stream_);
            if (ready.failed) {
                connecting_ = false;
            } else if (ready.write) {
                connecting_ = false;
                connected_ = true;
                net_.setOption(stream_, net::Sockets::NoDelay, 1);
                host_.say("  serial: connected");
            }
        }
        // Not there yet: tried again every half second.
        if (!connected_ && !connecting_ && target_.port &&
            std::chrono::steady_clock::now() - lastDial_ > std::chrono::milliseconds(500))
            dial();
        return connected_;
    }

    // ReadFile, as COMMTIMEOUTS has it: ReadIntervalTimeout MAXDWORD with
    // the totals 0 returns what's there at once; otherwise it waits for
    // `size` bytes up to multiplier * size + constant milliseconds (all 0:
    // for ever). Overlapped, it returns what's there at once: the game reads
    // overlapped holding a lock its other thread wants, which a read that
    // waited would keep from it.
    bool read(Cpu& c, uint32_t buffer, uint32_t size, bool overlapped, uint32_t& done, uint32_t& error) override {
        done = 0;
        error = 0;
        const bool immediate = overlapped || (timeouts_[0] == kMaxDword && timeouts_[1] == 0 && timeouts_[2] == 0);
        const bool forEver = timeouts_[0] == 0 && timeouts_[1] == 0 && timeouts_[2] == 0;
        const uint32_t total = timeouts_[1] * size + timeouts_[2];
        auto take = [&] {
            if (!connected() || done >= size)
                return done >= size;
            size_t n = 0;
            const Result r = net_.receive(stream_, c.memory().data() + buffer + done, size - done, n, false);
            if (r == Result::Ok && n == 0) { // the other end went
                connected_ = false;
                return true;
            }
            if (r == Result::Ok && n) {
                host_.logTraffic(false, 0, target_, c.memory().data() + buffer + done, n);
                done += uint32_t(n);
            }
            return done >= size;
        };
        if (take() || immediate || size == 0)
            return true;
        host_.process_->wait([&] { return take() || host_.process_->ending(); }, forEver ? kMaxDword : total);
        return true;
    }

    bool write(Cpu& c, uint32_t buffer, uint32_t size, uint32_t& done, uint32_t& error) override {
        done = 0;
        error = 0;
        if (!connected()) { // nothing at the other end of the cable: the bytes are lost
            done = size;
            return true;
        }
        if (size && host_.dropOutgoing()) { // lost on the line
            host_.logTraffic(true, 0, target_, c.memory().data() + buffer, size, true);
            done = size;
            return true;
        }
        while (done < size) {
            size_t n = 0;
            const Result r = net_.send(stream_, c.memory().data() + buffer + done, size - done, n);
            if (r == Result::Ok) {
                host_.logTraffic(true, 0, target_, c.memory().data() + buffer + done, n);
                done += uint32_t(n);
                continue;
            }
            if (r != Result::WouldBlock) {
                connected_ = false;
                done = size;
                return true;
            }
            host_.process_->wait([&] { return net_.ready(stream_).write || host_.process_->ending(); }, 1000);
            if (host_.process_->ending()) {
                error = kErrorGenFailure;
                return false;
            }
        }
        return true;
    }

    // Whether a read would find something (or the other end has gone).
    bool readable() override {
        if (!connected())
            return false;
        return net_.ready(stream_).read;
    }

    // Bytes waiting to be read.
    uint32_t waiting() {
        uint32_t bytes = 0;
        if (connected())
            net_.pending(stream_, bytes);
        return bytes;
    }
    // Throws away what's waiting (PurgeComm's PURGE_RXCLEAR).
    void purge() override {
        uint8_t scrap[512];
        for (uint32_t n = waiting(); n > 0; n = waiting()) {
            size_t got = 0;
            if (net_.receive(stream_, scrap, std::min<size_t>(n, sizeof scrap), got, false) != Result::Ok || !got)
                break;
        }
    }

private:
    GameHost& host_;
    net::Sockets& net_;
    int listener_ = -1, stream_ = -1;
    bool connecting_ = false, connected_ = false;
    net::Address target_; // where a connecting link connects to
    std::chrono::steady_clock::time_point lastDial_;
};

// "listen:port" (or ":port") or "host:port" to an address; false if the
// host can't be found.
bool parseLink(net::Sockets& net, const std::string& option, uint32_t netAddress, bool& listen, net::Address& at) {
    std::string text = option;
    listen = text.rfind("listen:", 0) == 0 || text.rfind(":", 0) == 0;
    if (listen)
        text = text.substr(text.find(':') + 1);
    auto [hostName, tcpPort] = net::splitHostPort(listen ? "0.0.0.0:" + text : text, 2300);
    if (!net::parseAddress(hostName, tcpPort, at)) {
        uint32_t ip = 0;
        if (!net.resolve(hostName, ip))
            return false;
        at = {ip, tcpPort};
    }
    if (listen && !at.ip && netAddress)
        at.ip = netAddress;
    return true;
}

void GameHost::addSerial() {
    const char* k = "KERNEL32.dll";
    net::Sockets* net = options_.sockets.get();
    // "listen:port" (or ":port"), or "host:port": the link every COM port
    // is. Without a network or the option there are no ports.
    if (net && !options_.serial.empty()) {
        process_->openSerialPort = [this, net](int port) -> std::shared_ptr<x86::DeviceObject> {
            bool listen = false;
            net::Address at;
            if (!parseLink(*net, options_.serial, options_.netAddress, listen, at)) {
                say("  serial: can't find " + options_.serial);
                return nullptr;
            }
            auto link = std::make_shared<SerialLink>(*this, *net);
            if (!link->start(listen, at))
                return nullptr;
            say("  serial: COM" + std::to_string(port) + " is the link");
            return link;
        };
    }
    auto find = [this](uint32_t handle) { return process_->object<CommPort>(handle); };

    // GetCommState(handle, DCB*): the DCB last set, else 9600 8N1, binary.
    provide(k, "GetCommState", 2, [find](Cpu& c) -> uint32_t {
        auto link = find(c.arg(0));
        if (!link)
            return 0;
        if (link->dcbSet_) {
            for (uint32_t i = 0; i < 28; ++i)
                c.write8(c.arg(1) + i, link->dcb_[i]);
        } else {
            for (uint32_t i = 0; i < 28; ++i)
                c.write8(c.arg(1) + i, 0);
            c.write32(c.arg(1), 28);     // DCBlength
            c.write32(c.arg(1) + 4, 9600); // BaudRate
            c.write32(c.arg(1) + 8, 1);  // fBinary
            c.write8(c.arg(1) + 18, 8);  // ByteSize
        }
        return 1;
    });
    provide(k, "SetCommState", 2, [find](Cpu& c) -> uint32_t {
        auto link = find(c.arg(0));
        if (!link)
            return 0;
        for (uint32_t i = 0; i < 28; ++i)
            link->dcb_[i] = c.read8(c.arg(1) + i);
        link->dcbSet_ = true;
        return 1;
    });
    // SetCommTimeouts(handle, COMMTIMEOUTS*): five DWORDs, kept for ReadFile.
    provide(k, "SetCommTimeouts", 2, [find](Cpu& c) -> uint32_t {
        auto link = find(c.arg(0));
        if (!link)
            return 0;
        for (uint32_t i = 0; i < 5; ++i)
            link->timeouts_[i] = c.read32(c.arg(1) + 4 * i);
        return 1;
    });
    // GetCommProperties(handle, COMMPROP*): an RS-232 port, any speed.
    provide(k, "GetCommProperties", 2, [find](Cpu& c) -> uint32_t {
        auto link = find(c.arg(0));
        if (!link)
            return 0;
        const uint32_t p = c.arg(1);
        for (uint32_t i = 0; i < 64; ++i)
            c.write8(p + i, 0);
        c.write16(p, 64);              // wPacketLength
        c.write16(p + 2, 2);           // wPacketVersion
        c.write32(p + 4, 1);           // dwServiceMask: SP_SERIALCOMM
        c.write32(p + 16, 0x10000000); // dwMaxBaud: BAUD_USER
        c.write32(p + 20, 1);          // dwProvSubType: PST_RS232
        c.write32(p + 24, 0xff);       // dwProvCapabilities: DTR/DSR, RTS/CTS, RLSD, parity, XON/XOFF...
        c.write32(p + 28, 0x7f);       // dwSettableParams
        c.write32(p + 32, 0x1006ffff); // dwSettableBaud: up to 115200, and any
        c.write16(p + 36, 0x0f);       // wSettableData: 5-8 bits
        c.write16(p + 38, 0x1f07);     // wSettableStopParity
        c.write32(p + 40, 4096);       // dwCurrentTxQueue
        c.write32(p + 44, 4096);       // dwCurrentRxQueue
        return 1;
    });
    // GetCommModemStatus(handle, DWORD*): CTS and DSR on, and the carrier
    // (RLSD) while the other end is connected.
    provide(k, "GetCommModemStatus", 2, [find](Cpu& c) -> uint32_t {
        auto link = find(c.arg(0));
        if (!link)
            return 0;
        c.write32(c.arg(1), 0x10 | 0x20 | (link->connected() ? 0x80u : 0u));
        return 1;
    });
    // PurgeComm(handle, flags): PURGE_RXCLEAR (8) throws away what's waiting.
    provide(k, "PurgeComm", 2, [find](Cpu& c) -> uint32_t {
        auto link = find(c.arg(0));
        if (!link)
            return 0;
        if (c.arg(1) & 8)
            link->purge();
        return 1;
    });
    // EscapeCommFunction(handle, function): DTR, RTS, breaks; nothing to do.
    provide(k, "EscapeCommFunction", 2, [find](Cpu& c) -> uint32_t { return find(c.arg(0)) ? 1u : 0u; });
    if (net && !options_.modem.empty())
        addModem();
}

// The modem's COM port (lineGetID's "comm/datamodem"): the call's link
// while there's one, else a line with nothing on it.
class GameHost::ModemPort : public CommPort {
public:
    std::shared_ptr<SerialLink> call;
    bool connected() override { return call && call->connected(); }
    void purge() override {
        if (call)
            call->purge();
    }
    bool readable() override { return call && call->readable(); }
    bool read(Cpu& c, uint32_t buffer, uint32_t size, bool overlapped, uint32_t& done, uint32_t& error) override {
        done = error = 0;
        if (!call)
            return true;
        call->timeouts_ = timeouts_;
        return call->read(c, buffer, size, overlapped, done, error);
    }
    bool write(Cpu& c, uint32_t buffer, uint32_t size, uint32_t& done, uint32_t& error) override {
        error = 0;
        done = size; // no call: the bytes go nowhere, as to a modem on hook
        return !call || call->write(c, buffer, size, done, error);
    }
};

void GameHost::addModem() {
    const char* t = "TAPI32.DLL";
    net::Sockets* net = options_.sockets.get();
    constexpr uint32_t kLine = 0x4c494e45, kCall = 0x43414c4c; // the line's and the call's handles
    constexpr uint32_t kLineCallState = 2, kLineReply = 12;
    constexpr uint32_t kIdle = 0x1, kOffering = 0x2, kConnected = 0x100, kDisconnected = 0x4000;
    constexpr uint32_t kOperationUnavailable = 0x80000048, kInvalCallHandle = 0x80000018;
    struct Modem {
        uint32_t callback = 0, instance = 0, thread = 0;
        uint32_t nextRequest = 1;
        bool answering = false, offered = false, dialing = false, connected = false;
        std::chrono::steady_clock::time_point dialedAt;
        std::optional<std::chrono::steady_clock::time_point> answeredAt; // when CONNECTED goes
        std::shared_ptr<SerialLink> listener, call;
        std::shared_ptr<ModemPort> port = std::make_shared<ModemPort>();
        std::deque<std::array<uint32_t, 5>> events; // device, message, param1-3
    };
    auto m = std::make_shared<Modem>();
    auto queue = [this, m](uint32_t device, uint32_t message, uint32_t p1, uint32_t p2 = 0, uint32_t p3 = 0) {
        m->events.push_back({device, message, p1, p2, p3});
        post(m->thread, {0, 0, 0, 0, uint32_t(process_->now())}); // wakes GetMessage: WM_NULL
    };
    // What's happened on the line since: a call coming in, a dial
    // answered or not; then the events, to the callback of the thread
    // that initialised TAPI (as its hidden window would deliver them).
    // Whether there's news for the game: the line looked at (a call coming
    // in, a dial answered or not), its events queued (without waking
    // anyone: this runs as a thread's wait condition).
    auto news = [this, m] {
        auto add = [m](uint32_t state, uint32_t p2) { m->events.push_back({kCall, kLineCallState, state, p2, 0}); };
        if (m->answering && !m->offered && !m->call && m->listener && m->listener->connected()) {
            m->offered = true;
            say("  modem: a call is coming in");
            add(kOffering, 0x10); // LINEMEDIAMODE_DATAMODEM
        }
        // Modems take seconds to connect, and the game counts on it: its
        // main thread waits for the TAPI thread to start before it waits
        // for the call, and misses one connected at once.
        const auto now = std::chrono::steady_clock::now();
        if (m->answeredAt && now >= *m->answeredAt) {
            m->answeredAt.reset();
            add(kConnected, 0x10);
        }
        if (m->dialing && m->call && now - m->dialedAt > std::chrono::seconds(2)) {
            if (m->call->connected()) {
                m->dialing = false;
                m->connected = true;
                say("  modem: the call is answered");
                add(kConnected, 0x10);
            } else if (std::chrono::steady_clock::now() - m->dialedAt > std::chrono::seconds(30)) {
                m->dialing = false;
                say("  modem: no answer");
                add(kDisconnected, 0x8); // LINEDISCONNECTMODE_NOANSWER
            }
        }
        return !m->events.empty() && m->callback;
    };
    tapiNews_ = news;
    tapiBusy_ = [m] { return m->call != nullptr || m->listener != nullptr; };
    auto deliver = [this, m, news](Cpu& c) {
        if (process_->currentThread() && process_->currentThread()->id != m->thread)
            return;
        news();
        while (!m->events.empty() && m->callback) {
            const auto e = m->events.front();
            m->events.pop_front();
            uint32_t ignored = 0;
            c.call(m->callback, {e[0], e[1], m->instance, e[2], e[3], e[4]}, ignored);
        }
    };
    tapiDeliver_ = deliver;
    // The game waits for a call by sleeping, not by asking for messages.
    process_->beforeSleep = deliver;
    auto request = [m, queue](uint32_t result = 0) {
        const uint32_t id = m->nextRequest++;
        queue(0, kLineReply, id, result);
        return id;
    };
    auto sizeOk = [](Cpu& c, uint32_t at, uint32_t needed) {
        // A TAPI structure: dwTotalSize, dwNeededSize, dwUsedSize first.
        const uint32_t total = c.read32(at);
        c.write32(at + 4, needed);
        if (total < needed) {
            c.write32(at + 8, 12);
            return false;
        }
        for (uint32_t i = 12; i < needed; ++i)
            c.write8(at + i, 0);
        c.write32(at + 8, needed);
        return true;
    };

    // lineInitialize(LPHLINEAPP, hInstance, callback, appName, LPDWORD devices)
    provide(t, "lineInitialize", 5, [this, m](Cpu& c) {
        m->callback = c.arg(2);
        m->thread = process_->currentThread() ? process_->currentThread()->id : 0;
        tapiThread_ = m->thread;
        c.write32(c.arg(0), 1);
        c.write32(c.arg(4), 1);
        say("  modem: TAPI started, one line");
        return 0u;
    });
    // lineNegotiateAPIVersion(app, device, low, high, LPDWORD version, LPLINEEXTENSIONID)
    provide(t, "lineNegotiateAPIVersion", 6, [](Cpu& c) {
        c.write32(c.arg(4), std::min<uint32_t>(c.arg(3), 0x00010004));
        for (uint32_t i = 0; i < 16; ++i)
            c.write8(c.arg(5) + i, 0);
        return 0u;
    });
    // lineGetDevCaps(app, device, version, extVersion, LPLINEDEVCAPS): a
    // data modem, named.
    provide(t, "lineGetDevCaps", 5, [sizeOk](Cpu& c) {
        const uint32_t at = c.arg(4);
        const std::string name = "Reluva TCP modem";
        constexpr uint32_t kFixed = 236;
        if (!sizeOk(c, at, kFixed + uint32_t(name.size()) + 1))
            return 0u;
        c.write32(at + 28, 1);                   // dwPermanentLineID
        c.write32(at + 32, uint32_t(name.size()) + 1); // dwLineNameSize
        c.write32(at + 36, kFixed);              // dwLineNameOffset
        c.write32(at + 40, 1);                   // dwStringFormat: ASCII
        c.write32(at + 44, 1);                   // dwAddressModes: LINEADDRESSMODE_ADDRESSID
        c.write32(at + 48, 1);                   // dwNumAddresses
        c.write32(at + 52, 0x4);                 // dwBearerModes: VOICE
        c.write32(at + 56, 28800);               // dwMaxRate
        c.write32(at + 60, 0x10);                // dwMediaModes: DATAMODEM
        c.write32(at + 100, 1);                  // dwMaxNumActiveCalls
        c.writeString(at + kFixed, name, uint32_t(name.size()) + 1);
        return 0u;
    });
    // lineGetDevConfig(device, LPVARSTRING, class), lineSetDevConfig: a
    // config of no bytes, taken and kept as nothing.
    provide(t, "lineGetDevConfig", 3, [sizeOk](Cpu& c) {
        if (!sizeOk(c, c.arg(1), 24))
            return 0u;
        c.write32(c.arg(1) + 12, 4); // dwStringFormat: STRINGFORMAT_BINARY
        return 0u;
    });
    provide(t, "lineSetDevConfig", 4, [](Cpu&) { return 0u; });
    // lineOpen(app, device, LPHLINE, version, extVersion, instance,
    // privileges, mediaModes, callParams): owning it (privilege 4) answers
    // calls: the link listens.
    provide(t, "lineOpen", 9, [this, net, m](Cpu& c) {
        c.write32(c.arg(2), kLine);
        m->instance = c.arg(5);
        m->answering = (c.arg(6) & 4) != 0;
        if (m->answering && !m->listener) {
            bool listen = false;
            net::Address at;
            if (parseLink(*net, options_.modem, options_.netAddress, listen, at) && listen) {
                m->listener = std::make_shared<SerialLink>(*this, *net);
                if (!m->listener->start(true, at))
                    m->listener.reset();
            }
        }
        return 0u;
    });
    provide(t, "lineSetStatusMessages", 3, [](Cpu&) { return 0u; });
    // lineGetLineDevStatus(line, LPLINEDEVSTATUS): nothing to report.
    provide(t, "lineGetLineDevStatus", 2, [sizeOk](Cpu& c) {
        sizeOk(c, c.arg(1), 104);
        return 0u;
    });
    // lineMakeCall(line, LPHCALL, number, country, callParams): the number
    // as an address ("127.0.0.2:2300", or with * for the dots, as a phone
    // keypad has), else the option's host.
    provide(t, "lineMakeCall", 5, [this, net, m, request, deliver](Cpu& c) {
        deliver(c);
        std::string number = c.arg(2) ? c.string(c.arg(2), 128) : "";
        std::string text;
        for (char ch : number)
            if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == ':' || ch == '*')
                text += ch == '*' ? '.' : ch;
        net::Address at;
        bool listen = false;
        if (!net::parseAddress(text, 2300, at) &&
            (!parseLink(*net, options_.modem, options_.netAddress, listen, at) || listen)) {
            say("  modem: nowhere to dial for \"" + number + "\"");
            return kOperationUnavailable;
        }
        m->call = std::make_shared<SerialLink>(*this, *net);
        m->call->start(false, at);
        m->port->call = m->call;
        m->dialing = true;
        m->connected = false;
        m->dialedAt = std::chrono::steady_clock::now();
        c.write32(c.arg(1), kCall);
        say("  modem: dialing " + at.toString() + " for \"" + number + "\"");
        return request();
    });
    // lineAnswer(call, userInfo, size): the call coming in, answered.
    provide(t, "lineAnswer", 3, [this, m, request, queue](Cpu& c) {
        if (!m->offered || !m->listener)
            return kInvalCallHandle;
        m->call = m->listener;
        m->port->call = m->call;
        m->listener.reset();
        m->connected = true;
        const uint32_t id = request();
        m->answeredAt = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        say("  modem: answered");
        (void)c;
        return id;
    });
    // lineGetID(line, address, call, select, LPVARSTRING, class): for
    // "comm/datamodem", a handle to the modem's port (and its name), the
    // line's or the call's: the same port, which is the call's link.
    provide(t, "lineGetID", 6, [this, m, sizeOk](Cpu& c) -> uint32_t {
        const std::string cls = c.string(c.arg(5), 64);
        if (cls != "comm/datamodem")
            return kOperationUnavailable;
        const uint32_t at = c.arg(4);
        if (!sizeOk(c, at, 24 + 4 + 5))
            return 0u;
        c.write32(at + 12, 4);  // dwStringFormat: STRINGFORMAT_BINARY
        c.write32(at + 16, 9);  // dwStringSize
        c.write32(at + 20, 24); // dwStringOffset
        c.write32(at + 24, process_->addHandle(m->port));
        c.writeString(at + 28, "COM1", 5);
        return 0u;
    });
    // lineDrop(call, userInfo, size): hung up.
    provide(t, "lineDrop", 3, [this, m, request, queue](Cpu&) {
        m->call.reset();
        m->port->call.reset();
        m->dialing = m->connected = m->offered = false;
        const uint32_t id = request();
        queue(kCall, kLineCallState, kIdle);
        say("  modem: hung up");
        return id;
    });
    provide(t, "lineDeallocateCall", 1, [](Cpu&) { return 0u; });
    provide(t, "lineClose", 1, [m](Cpu&) {
        m->listener.reset();
        m->call.reset();
        m->port->call.reset();
        m->answering = m->offered = m->dialing = m->connected = false;
        return 0u;
    });
    provide(t, "lineShutdown", 1, [m](Cpu&) {
        m->callback = 0;
        m->events.clear();
        return 0u;
    });
}

} // namespace reluva::nfs
