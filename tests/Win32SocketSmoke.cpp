/*
    OscTap win32 socket-backend compile/link smoke (Windows-only).

    The win32 socket backend (ip/win32/UdpSocket.h, NetworkingUtils.h) had no compiled
    coverage: the POSIX demos/tests are gated off Windows, and nothing else pulled
    the win32 sockets into a build. This TU closes that gap. It is built by the
    existing windows-latest CI legs via the WIN32-gated CMake target, so the
    getaddrinfo port and the rest of the win32 backend now actually compile + link
    on every push.

    It is a *compile/link* smoke, not a runtime network test: the socket paths are
    instantiated and linked (so a hard error or missing symbol fails the build) but
    are guarded behind a condition that is never taken, so CI needs no live network.
    Only IpEndpointName string formatting actually runs.
*/

#include "ip/UdpSocket.h"
#include "ip/TcpSocket.h"
#include "ip/IpEndpointName.h"
#include "ip/PacketListener.h"
#include "osc/OscOutboundPacketStream.h"

#include <cstdio>

namespace {

class NullListener : public osctap::PacketListener {
public:
    void ProcessPacket( const char *, int, const osctap::IpEndpointName & ) override {}
};

// Defined and ODR-used (its address is taken below) but never executed on CI.
// Forces the win32 UdpSocket / SocketReceiveMultiplexer template members -- the
// ctor, Bind/SendTo/Send, Run/AsynchronousBreak, and the getaddrinfo-based
// GetHostByName -- to compile and link.
void exercise_win32_backend()
{
    osctap::IpEndpointName ep( "127.0.0.1", 9000 );  // -> GetHostByName -> getaddrinfo

    osctap::UdpTransmitSocket tx( ep );
    char buf[8] = { 0 };
    tx.Send( buf, sizeof(buf) );

    NullListener listener;
    osctap::UdpListeningReceiveSocket rx(
        osctap::IpEndpointName( osctap::IpEndpointName::ANY_ADDRESS, 9000 ), &listener );
    rx.AsynchronousBreak();

    // TCP backend (ip/win32/TcpSocket.h): client Send + connection-aware server.
    osctap::TcpTransmitSocket tcpTx( ep );
    tcpTx.Send( buf, sizeof(buf) );

    osctap::TcpListeningReceiveSocket tcpRx(
        osctap::IpEndpointName( osctap::IpEndpointName::ANY_ADDRESS, 9001 ), &listener );
    tcpRx.Run();
    tcpRx.AsynchronousBreak();
}

} // namespace

int main( int argc, char ** /*argv*/ )
{
    // Actually runs (no network): exercise IpEndpointName formatting.
    char s[ osctap::IpEndpointName::ADDRESS_AND_PORT_STRING_LENGTH ];
    osctap::IpEndpointName( 127, 0, 0, 1, 9000 ).AddressAndPortAsString( s );
    std::printf( "win32 socket smoke: %s\n", s );

    // ODR-use the socket exercise so it links, but never call it on CI.
    void (*fn)() = &exercise_win32_backend;
    if( argc == 0x7fffffff )   // never true; opaque to the optimiser
        fn();

    return 0;
}
