/*
    OscTap demo: command-line OSC sender.

    Part of the Pi 5 <-> Pico 2W <-> Android integration tutorial
    (docs/INTEGRATION_PI5_PICO_ANDROID.md). Sends a single OSC message, so you
    can drive the Pi 5 hub (or a Pico) from a shell to test the wiring before the
    real Android app / firmware exists.

    Build via the OSCTAP_BUILD_DEMOS CMake option, or directly:
        g++ -std=c++17 -I. -Iosctap demos/osc_send.cpp -o osc_send

    Usage:
        osc_send <host> <port> <address> [args...]

    Each arg is typed by a one-letter prefix (default is auto: int if it parses
    as an integer, else float if it parses as a float, else string):
        i:42      int32        f:3.14    float
        s:hello   string       T / F     bool true / false

    Examples:
        osc_send 192.168.1.10 9000 /hub/led i:1
        osc_send 192.168.1.10 9000 /hub/pwm f:0.75
        osc_send 192.168.1.10 9000 /sensor/temp f:21.4
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "ip/IpEndpointName.h"
#include "ip/UdpSocket.h"
#include "osc/OscOutboundPacketStream.h"

namespace {

    bool ParseInt(const char* s, int32_t& out) {
        char* end = nullptr;
        long  v   = std::strtol(s, &end, 10);
        if (end == s || *end != '\0')
            return false;
        out = static_cast<int32_t>(v);
        return true;
    }

    bool ParseFloat(const char* s, float& out) {
        char* end = nullptr;
        float v   = std::strtof(s, &end);
        if (end == s || *end != '\0')
            return false;
        out = v;
        return true;
    }

    void AppendArg(osctap::OutboundPacketStream& p, const char* tok) {
        if (std::strcmp(tok, "T") == 0) {
            p << true;
            return;
        }
        if (std::strcmp(tok, "F") == 0) {
            p << false;
            return;
        }

        if (std::strncmp(tok, "i:", 2) == 0) {
            p << (int32_t)std::atoi(tok + 2);
            return;
        }
        if (std::strncmp(tok, "f:", 2) == 0) {
            p << (float)std::atof(tok + 2);
            return;
        }
        if (std::strncmp(tok, "s:", 2) == 0) {
            const char* s = tok + 2;
            p << s;
            return;
        }

        // Auto: int, else float, else string.
        int32_t i;
        float   f;
        if (ParseInt(tok, i))
            p << i;
        else if (ParseFloat(tok, f))
            p << f;
        else
            p << tok;
    }

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "usage: osc_send <host> <port> <address> [args...]\n";
        return 2;
    }
    const char* host    = argv[1];
    int         port    = std::atoi(argv[2]);
    const char* address = argv[3];

    char                         buffer[1024];
    osctap::OutboundPacketStream p(buffer, sizeof(buffer));
    try {
        p << osctap::BeginMessage(address);
        for (int i = 4; i < argc; ++i)
            AppendArg(p, argv[i]);
        p << osctap::EndMessage();
    }
    catch (const osctap::Exception& e) {
        std::cerr << "failed to build message: " << e.what() << '\n';
        return 1;
    }

    try {
        osctap::UdpTransmitSocket(osctap::IpEndpointName(host, port)).Send(p.Data(), p.Size());
    }
    catch (const std::exception& e) {
        std::cerr << "send failed: " << e.what() << '\n';
        return 1;
    }

    std::cout << "sent " << p.Size() << " bytes to " << host << ':' << port << "  " << address << '\n';
    return 0;
}
