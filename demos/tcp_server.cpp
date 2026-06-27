/*
    OscTap demo: OSC-over-TCP server / monitor.

    Listens for OSC-over-TCP clients and prints every message it receives (address
    + typed arguments + peer). A TCP counterpart to the UDP pi5_hub demo, and the
    server side for testing tcp_send. See docs/OSC_OVER_TCP.md.

    Build via the OSCTAP_BUILD_DEMOS CMake option, or directly:
        g++ -std=c++17 -I. -Iosctap demos/tcp_server.cpp -o tcp_server

    Usage:
        tcp_server [port]          (default port 9000)
*/

#include "ip/TcpSocket.h"
#include "ip/IpEndpointName.h"
#include "osc/OscPacketListener.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

class PrintingListener : public osctap::OscPacketListener {
    // Untrusted input: parsing can throw on a malformed frame. Catch it so one bad
    // client can't take the server down.
    void ProcessPacket( const char *data, int size, const osctap::IpEndpointName& from ) override
    {
        try {
            osctap::OscPacketListener::ProcessPacket( data, size, from );
        } catch( const osctap::Exception& e ) {
            std::cerr << "[drop] malformed packet (" << e.what() << ")\n";
        }
    }

protected:
    void ProcessMessage( const osctap::ReceivedMessage& m, const osctap::IpEndpointName& from ) override
    {
        char who[ osctap::IpEndpointName::ADDRESS_AND_PORT_STRING_LENGTH ];
        from.AddressAndPortAsString( who );
        std::cout << "[recv " << who << "] " << m.AddressPattern()
                  << " (" << m.ArgumentCount() << " args)";
        for( auto a = m.ArgumentsBegin(); a != m.ArgumentsEnd(); ++a ){
            std::cout << ' ';
            if( a->IsInt32() )        std::cout << a->AsInt32Unchecked();
            else if( a->IsFloat() )   std::cout << a->AsFloatUnchecked();
            else if( a->IsString() )  std::cout << '"' << a->AsStringUnchecked() << '"';
            else if( a->IsBool() )    std::cout << (a->AsBoolUnchecked() ? "true" : "false");
            else                      std::cout << '?';
        }
        std::cout << '\n';
    }
};

osctap::TcpListeningReceiveSocket *gSocket = nullptr;
void HandleSigInt( int ) { if( gSocket ) gSocket->AsynchronousBreak(); }

} // namespace

int main( int argc, char *argv[] )
{
    const int port = (argc > 1) ? std::atoi( argv[1] ) : 9000;

    PrintingListener listener;
    osctap::TcpListeningReceiveSocket socket(
        osctap::IpEndpointName( osctap::IpEndpointName::ANY_ADDRESS, port ), &listener );
    gSocket = &socket;
    std::signal( SIGINT, HandleSigInt );

    std::cout << "OscTap TCP server listening on TCP " << port << " (Ctrl-C to stop)\n";
    socket.Run();
    std::cout << "\nserver stopped.\n";
    return 0;
}
