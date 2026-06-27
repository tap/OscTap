/*
    Simple example of sending an OSC message bundle using OscTap.

    Canonical oscpack example, kept compiling against the current API (and using
    the deprecated `oscpack` alias on purpose, to exercise the migration shim).
    For a typed command-line sender see demos/osc_send.cpp.
*/

#include "osc/OscOutboundPacketStream.h"
#include "ip/UdpSocket.h"

using namespace oscpack; // OscTap's deprecated oscpack:: alias, exercised here

#define ADDRESS "127.0.0.1"
#define PORT 7000

#define OUTPUT_BUFFER_SIZE 1024

int main(int argc, char* argv[])
{
    (void) argc; // suppress unused parameter warnings
    (void) argv;

    UdpTransmitSocket transmitSocket( IpEndpointName( ADDRESS, PORT ) );

    char buffer[OUTPUT_BUFFER_SIZE];
    OutboundPacketStream p( buffer, OUTPUT_BUFFER_SIZE );

    p << BeginBundleImmediate()
        << BeginMessage( "/test1" )
            << true << (int32_t)23 << (float)3.1415f << "hello" << EndMessage()
        << BeginMessage( "/test2" )
            << true << (int32_t)24 << (float)10.8f << "world" << EndMessage()
      << EndBundle();

    transmitSocket.Send( p.Data(), p.Size() );

    return 0;
}
