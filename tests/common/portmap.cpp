// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Asking a router to pass a port on: the pieces of UPnP and NAT-PMP, and
// the whole against routers made here.
//
//   reluva-tests common.unit.portmap

#include "common/portmap.h"
#include "tests/support/test.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace reluva;

namespace {

void testPortMap() {
    using namespace net;
    std::string location;
    CHECK(ssdpLocation(
              "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nLocation: http://192.168.1.1:5000/rootDesc.xml\r\n\r\n",
              location) &&
          location == "http://192.168.1.1:5000/rootDesc.xml");
    Url u;
    CHECK(parseUrl(location, u) && u.ip == 0xC0A80101 && u.port == 5000 && u.path == "/rootDesc.xml");
    CHECK(parseUrl("http://10.0.0.138", u) && u.port == 80 && u.path == "/");
    CHECK(!parseUrl("https://10.0.0.1/", u));
    int status = 0;
    CHECK(httpBody("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n",
                   status) == "hello world" &&
          status == 200);
    CHECK(xmlValue("<s:Body><u:R><NewExternalIPAddress> 1.2.3.4 </NewExternalIPAddress></u:R>",
                   "NewExternalIPAddress") == "1.2.3.4");
    CHECK(xmlValue("<a:x>7</a:x>", "x") == "7" && xmlValue("<y>1</y>", "x").empty());

    const std::string description =
        "<root><device><serviceList><service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
        "<controlURL>/ctl/L3F</controlURL></service></serviceList><deviceList><device><serviceList><service>"
        "<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>ctl/IPConn</controlURL>"
        "</service></serviceList></device></deviceList></device></root>";
    std::string type;
    Url control;
    CHECK(parseUrl(location, u) && findWanService(description, u, type, control) &&
          type == "urn:schemas-upnp-org:service:WANIPConnection:1" && control.path == "/ctl/IPConn" &&
          control.port == 5000);

    const auto mapRequest = natPmpMapRequest(28520, 28520, 7200);
    CHECK(mapRequest.size() == 12 && mapRequest[1] == 1 && mapRequest[4] == 0x6F && mapRequest[5] == 0x68 &&
          mapRequest[11] == 0x20 && mapRequest[10] == 0x1C);
    uint16_t external = 0;
    uint32_t lifetime = 0;
    uint32_t ip = 0;
    int result = -1;
    CHECK(natPmpMapReply({0, 129, 0, 0, 0, 0, 0, 1, 0x6F, 0x68, 0x6F, 0x69, 0, 0, 0x1C, 0x20}, external, lifetime,
                         result) &&
          result == 0 && external == 28521 && lifetime == 7200);
    CHECK(natPmpAddressReply({0, 128, 0, 0, 0, 0, 0, 1, 203, 0, 113, 7}, ip, result) && ip == 0xCB007107);

    // A UPnP router: found by SSDP, described, asked to map, asked its
    // address, and later to take the mapping away.
    std::vector<std::string> posted;
    PortMapper::Transport upnp;
    upnp.udp = [](const Address &to, const std::vector<uint8_t> &d, int) {
        std::vector<std::pair<Address, std::vector<uint8_t>>> r;
        const std::string text(d.begin(), d.end());
        if (to.ip == 0xEFFFFFFA && to.port == 1900 && text.find("InternetGatewayDevice") != std::string::npos) {
            const std::string reply = "HTTP/1.1 200 OK\r\nLOCATION: http://192.168.1.1:5000/rootDesc.xml\r\n\r\n";
            r.push_back({{0xC0A80101, 1900}, std::vector<uint8_t>(reply.begin(), reply.end())});
        }
        return r;
    };
    upnp.http = [&](const Address &to, const std::string &request, std::string &response, uint32_t &localIp) {
        localIp = 0xC0A80117; // 192.168.1.23
        if (to.ip != 0xC0A80101 || to.port != 5000)
            return false;
        if (request.rfind("GET /rootDesc.xml", 0) == 0)
            response = "HTTP/1.1 200 OK\r\n\r\n" + description;
        else if (request.find("#GetExternalIPAddress") != std::string::npos)
            response = "HTTP/1.1 200 OK\r\n\r\n<s:Envelope><s:Body><u:GetExternalIPAddressResponse>"
                       "<NewExternalIPAddress>203.0.113.7</NewExternalIPAddress></u:GetExternalIPAddressResponse>"
                       "</s:Body></s:Envelope>";
        else {
            posted.push_back(request);
            response = "HTTP/1.1 200 OK\r\n\r\n";
        }
        return true;
    };
    {
        PortMapper mapper(upnp, 28520, false);
        const PortMapping m = mapper.map();
        CHECK(m.way == PortMapping::Way::kUpnp && m.externalIp == 0xCB007107 && m.externalPort == 28520 &&
              mapper.done());
        CHECK(posted.size() == 1 && posted[0].find("POST /ctl/IPConn") == 0 &&
              posted[0].find("<NewInternalClient>192.168.1.23</NewInternalClient>") != std::string::npos &&
              posted[0].find("<NewProtocol>UDP</NewProtocol>") != std::string::npos &&
              posted[0].find("SOAPAction: \"urn:schemas-upnp-org:service:WANIPConnection:1#AddPortMapping\"") !=
                  std::string::npos);
        mapper.unmap();
        CHECK(posted.size() == 2 && posted[1].find("#DeletePortMapping") != std::string::npos);
    }

    // A NAT-PMP router only.
    std::vector<std::vector<uint8_t>> asked;
    PortMapper::Transport pmp;
    pmp.gateways = {0x0A000001};
    pmp.udp = [&](const Address &to, const std::vector<uint8_t> &d, int) {
        std::vector<std::pair<Address, std::vector<uint8_t>>> r;
        if (to.ip != 0x0A000001 || to.port != 5351)
            return r;
        asked.push_back(d);
        if (d.size() == 2)
            r.push_back({to, {0, 128, 0, 0, 0, 0, 0, 9, 198, 51, 100, 2}});
        else if (d.size() == 12)
            r.push_back({to, {0, 129, 0, 0, 0, 0, 0, 9, d[4], d[5], d[6], d[7], d[8], d[9], d[10], d[11]}});
        return r;
    };
    {
        PortMapper mapper(pmp, 28520, false);
        const PortMapping m = mapper.map();
        CHECK(m.way == PortMapping::Way::kNatPmp && m.externalPort == 28520 && m.externalIp == 0xC6336402);
        mapper.unmap();
        CHECK(!asked.empty() && asked.back().size() == 12 && asked.back()[11] == 0 && asked.back()[8] == 0);
    }

    // On a thread of its own, as the game has it: mapped, then taken away
    // when it goes.
    asked.clear();
    {
        PortMapper mapper(pmp, 28520);
        for (int i = 0; i < 2000 && !mapper.done(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        CHECK(mapper.done() && mapper.result().way == PortMapping::Way::kNatPmp);
    }
    CHECK(asked.size() >= 3 && asked.back().size() == 12 && asked.back()[11] == 0);

    // Neither: said so.
    PortMapper none(PortMapper::Transport{}, 28520, false);
    const PortMapping m = none.map();
    CHECK(m.way == PortMapping::Way::kNone && !m.why.empty());
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.portmap",
                                        {
                                            {"port map", [](const std::filesystem::path &) { testPortMap(); }},
                                        });
