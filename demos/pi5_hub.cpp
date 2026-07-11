/*
    OscTap demo: Raspberry Pi 5 OSC hub / router.

    Part of the Pi 5 <-> Pico 2W <-> Android integration tutorial
    (docs/INTEGRATION_PI5_PICO_ANDROID.md). Runs on the Pi 5 (or any POSIX host;
    the source path is identical on aarch64 and x86-64) as the central node:

      * binds a UDP socket and listens for OSC,
      * prints every message it receives (address + typed arguments + sender),
      * translates/relays between the controller (Android) and the device (Pico):
          - from Android:  /hub/led <int>      -> Pico as  /led <int>
                           /hub/pwm <float>    -> Pico as  /pwm <float>
          - from Pico:     /sensor/<name> ...  -> Android as /ui/<name> ... (telemetry)

    Build via the OSCTAP_BUILD_DEMOS CMake option, or directly:
        g++ -std=c++17 -I. -Iosctap demos/pi5_hub.cpp -o pi5_hub

    Usage:
        pi5_hub [listenPort] [picoHost:picoPort] [androidHost:androidPort]
    Defaults:
        listenPort      9000
        pico            192.168.1.50:9000
        android         192.168.1.20:9001
*/

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "ip/IpEndpointName.h"
#include "ip/UdpSocket.h"
#include "osc/OscOutboundPacketStream.h"
#include "osc/OscPacketListener.h"

namespace {

    // Parse "host:port" (port optional -> fallback). IPv4 dotted or hostname.
    osctap::IpEndpointName ParseEndpoint(const char* s, int fallbackPort) {
        const char* colon = std::strrchr(s, ':');
        if (!colon)
            return osctap::IpEndpointName(s, fallbackPort);
        std::string host(s, colon - s);
        int         port = std::atoi(colon + 1);
        return osctap::IpEndpointName(host.c_str(), port ? port : fallbackPort);
    }

    void PrintMessage(const osctap::ReceivedMessage& m, const osctap::IpEndpointName& from) {
        char who[osctap::IpEndpointName::ADDRESS_AND_PORT_STRING_LENGTH];
        from.AddressAndPortAsString(who);
        std::cout << "[recv " << who << "] " << m.AddressPattern() << " (" << m.ArgumentCount() << " args)";
        for (auto a = m.ArgumentsBegin(); a != m.ArgumentsEnd(); ++a) {
            std::cout << ' ';
            if (a->IsInt32())
                std::cout << a->AsInt32Unchecked();
            else if (a->IsFloat())
                std::cout << a->AsFloatUnchecked();
            else if (a->IsString())
                std::cout << '"' << a->AsStringUnchecked() << '"';
            else if (a->IsBool())
                std::cout << (a->AsBoolUnchecked() ? "true" : "false");
            else
                std::cout << '?';
        }
        std::cout << '\n';
    }

    class HubListener : public osctap::OscPacketListener {
      public:
        HubListener(const osctap::IpEndpointName& pico, const osctap::IpEndpointName& android)
            : pico_(pico)
            , android_(android) {}

        // Guard the dispatch: parsing untrusted UDP can throw on a malformed packet.
        // Catch it so one bad datagram drops instead of taking the hub down.
        void ProcessPacket(const char* data, int size, const osctap::IpEndpointName& from) override {
            try {
                osctap::OscPacketListener::ProcessPacket(data, size, from);
            }
            catch (const osctap::Exception& e) {
                std::cerr << "[drop] malformed packet (" << e.what() << ")\n";
            }
        }

      protected:
        void ProcessMessage(const osctap::ReceivedMessage& m, const osctap::IpEndpointName& from) override {
            PrintMessage(m, from);

            const char* addr = m.AddressPattern();
            char        out[256];

            // Controller -> device: re-address /hub/<x> to /<x> and forward to Pico.
            if (std::strncmp(addr, "/hub/", 5) == 0) {
                osctap::OutboundPacketStream p(out, sizeof(out));
                p << osctap::BeginMessage(addr + 4); // "/hub/led" -> "/led"
                for (auto a = m.ArgumentsBegin(); a != m.ArgumentsEnd(); ++a)
                    CopyArg(p, a);
                p << osctap::EndMessage();
                Forward(pico_, p, "Pico");
            }
            // Device -> controller: re-address /sensor/<x> to /ui/<x> (telemetry).
            else if (std::strncmp(addr, "/sensor/", 8) == 0) {
                std::string                  ui = std::string("/ui/") + (addr + 8);
                osctap::OutboundPacketStream p(out, sizeof(out));
                p << osctap::BeginMessage(ui.c_str());
                for (auto a = m.ArgumentsBegin(); a != m.ArgumentsEnd(); ++a)
                    CopyArg(p, a);
                p << osctap::EndMessage();
                Forward(android_, p, "Android");
            }
        }

      private:
        static void CopyArg(osctap::OutboundPacketStream& p, osctap::ReceivedMessage::const_iterator a) {
            if (a->IsInt32())
                p << a->AsInt32Unchecked();
            else if (a->IsFloat())
                p << a->AsFloatUnchecked();
            else if (a->IsString())
                p << a->AsStringUnchecked();
            else if (a->IsBool())
                p << a->AsBoolUnchecked();
        }

        void Forward(const osctap::IpEndpointName& to, const osctap::OutboundPacketStream& p, const char* label) {
            try {
                osctap::UdpTransmitSocket(to).Send(p.Data(), p.Size());
                char dst[osctap::IpEndpointName::ADDRESS_AND_PORT_STRING_LENGTH];
                to.AddressAndPortAsString(dst);
                std::cout << "  -> " << label << " (" << dst << ")\n";
            }
            catch (const std::exception& e) {
                std::cerr << "  -> " << label << " send failed: " << e.what() << '\n';
            }
        }

        osctap::IpEndpointName pico_;
        osctap::IpEndpointName android_;
    };

    osctap::UdpListeningReceiveSocket* gSocket = nullptr;
    void                               HandleSigInt(int) {
        if (gSocket)
            gSocket->AsynchronousBreak();
    }

} // namespace

int main(int argc, char* argv[]) {
    int                    listenPort = (argc > 1) ? std::atoi(argv[1]) : 9000;
    osctap::IpEndpointName pico =
        (argc > 2) ? ParseEndpoint(argv[2], 9000) : osctap::IpEndpointName("192.168.1.50", 9000);
    osctap::IpEndpointName android =
        (argc > 3) ? ParseEndpoint(argv[3], 9001) : osctap::IpEndpointName("192.168.1.20", 9001);

    HubListener                       listener(pico, android);
    osctap::UdpListeningReceiveSocket socket(osctap::IpEndpointName(osctap::IpEndpointName::ANY_ADDRESS, listenPort),
                                             &listener);
    gSocket = &socket;
    std::signal(SIGINT, HandleSigInt);

    std::cout << "OscTap Pi 5 hub listening on UDP " << listenPort << " (Ctrl-C to stop)\n";
    socket.Run();
    std::cout << "\nhub stopped.\n";
    return 0;
}
