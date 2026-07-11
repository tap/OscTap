/*
    OscTap demo: command-line OSC-over-TCP sender.

    Connects to an OSC-over-TCP server (e.g. tcp_server) and sends one OSC message.
    TCP counterpart to the UDP osc_send demo. See docs/OSC_OVER_TCP.md.

    Build via the OSCTAP_BUILD_DEMOS CMake option, or directly:
        g++ -std=c++17 -I. -Iosctap demos/tcp_send.cpp -o tcp_send

    Usage:
        tcp_send <host> <port> <address> [args...]

    Each arg is typed by a one-letter prefix (default is auto: int, else float,
    else string):
        i:42      int32        f:3.14    float
        s:hello   string       T / F     bool true / false

    Examples:
        tcp_send 127.0.0.1 9000 /fader/1 f:0.75
        tcp_send 127.0.0.1 9000 /chat s:hello T
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "ip/IpEndpointName.h"
#include "ip/TcpSocket.h"
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
        std::cerr << "usage: tcp_send <host> <port> <address> [args...]\n";
        return 2;
    }
    const char* host    = argv[1];
    const int   port    = std::atoi(argv[2]);
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
        osctap::TcpTransmitSocket client(osctap::IpEndpointName(host, port));
        client.Send(p.Data(), p.Size());
    }
    catch (const std::exception& e) {
        std::cerr << "send failed: " << e.what() << '\n';
        return 1;
    }

    std::cout << "sent " << p.Size() << " bytes to " << host << ':' << port << "  " << address << " (TCP)\n";
    return 0;
}
