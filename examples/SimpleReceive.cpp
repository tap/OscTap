/*
    Example of two ways to process received OSC messages using OscTap.
    Receives the messages sent by SimpleSend.cpp.

    Canonical oscpack example, kept compiling against the current API (uses the
    deprecated `oscpack` alias on purpose). For a routing/monitoring receiver see
    demos/pi5_hub.cpp and demos/tcp_server.cpp.
*/

#include <csignal>
#include <cstring>
#include <iostream>

#include "ip/UdpSocket.h"
#include "osc/OscPacketListener.h"
#include "osc/OscReceivedElements.h"

using namespace oscpack; // OscTap's deprecated oscpack:: alias, exercised here

#define PORT 7000

class ExamplePacketListener : public OscPacketListener {
  protected:
    void ProcessMessage(const ReceivedMessage& m, const IpEndpointName& remoteEndpoint) override {
        (void)remoteEndpoint;

        try {
            // OscPacketListener handles bundle traversal; we just read messages.
            if (std::strcmp(m.AddressPattern(), "/test1") == 0) {
                // example #1 -- argument-stream interface
                ReceivedMessageArgumentStream args = m.ArgumentStream();
                bool                          a1;
                int32_t                       a2;
                float                         a3;
                const char*                   a4;
                MessageTerminator             end;
                args >> a1 >> a2 >> a3 >> a4 >> end;

                std::cout << "received '/test1' message with arguments: " << a1 << " " << a2 << " " << a3 << " " << a4
                          << "\n";
            }
            else if (std::strcmp(m.AddressPattern(), "/test2") == 0) {
                // example #2 -- argument-iterator interface (supports reflection,
                // e.g. arg->IsBool() to check the type of an overloaded argument)
                ReceivedMessage::const_iterator arg = m.ArgumentsBegin();
                bool                            a1  = (arg++)->AsBool();
                int                             a2  = (arg++)->AsInt32();
                float                           a3  = (arg++)->AsFloat();
                const char*                     a4  = (arg++)->AsString();
                if (arg != m.ArgumentsEnd())
                    throw ExcessArgumentException();

                std::cout << "received '/test2' message with arguments: " << a1 << " " << a2 << " " << a3 << " " << a4
                          << "\n";
            }
        }
        catch (Exception& e) {
            // parsing errors (wrong/missing argument types) are thrown
            std::cout << "error while parsing message: " << m.AddressPattern() << ": " << e.what() << "\n";
        }
    }
};

namespace {
    UdpListeningReceiveSocket* gSocket = nullptr;
    void                       HandleSigInt(int) {
        if (gSocket)
            gSocket->AsynchronousBreak();
    }
} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    ExamplePacketListener     listener;
    UdpListeningReceiveSocket s(IpEndpointName(IpEndpointName::ANY_ADDRESS, PORT), &listener);

    gSocket = &s;
    std::signal(SIGINT, HandleSigInt);

    std::cout << "press ctrl-c to end\n";
    s.Run();

    return 0;
}
