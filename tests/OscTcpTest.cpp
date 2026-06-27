/*
    OscTap OSC-over-TCP loopback test (POSIX).

    Drives the real TcpListeningReceiveSocket / TcpTransmitSocket over loopback:
    a server runs on one thread; a client connects and sends several OSC messages,
    including a large one that spans multiple TCP segments (exercising the
    per-connection deframer's reassembly through actual sockets, not just a unit
    test). Verifies every packet arrives intact and in order, then stops the
    server with AsynchronousBreak() from the main thread.
*/

#include "ip/TcpSocket.h"
#include "ip/IpEndpointName.h"
#include "osc/OscPacketListener.h"
#include "osc/OscOutboundPacketStream.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

class RecordingListener : public osctap::OscPacketListener {
public:
    std::atomic<int> count{ 0 };
    std::vector<std::string> addresses; // written by server thread, read after join
    std::vector<int>         sizes;     // payload size hint per message

protected:
    void ProcessMessage( const osctap::ReceivedMessage& m, const osctap::IpEndpointName& ) override
    {
        addresses.emplace_back( m.AddressPattern() );
        int sz = 0;
        auto a = m.ArgumentsBegin();
        if( a != m.ArgumentsEnd() ){
            if( a->IsInt32() )       sz = a->AsInt32Unchecked();
            else if( a->IsString() ) sz = (int)std::strlen( a->AsStringUnchecked() );
        }
        sizes.push_back( sz );
        count.fetch_add( 1, std::memory_order_relaxed );
    }
};

int failures = 0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } }while(0)

} // namespace

int main()
{
    RecordingListener listener;

    // Bind the server to an OS-assigned loopback port, then discover it.
    osctap::TcpListeningReceiveSocket server(
        osctap::IpEndpointName( 127, 0, 0, 1, 0 ), &listener );
    const int port = server.LocalEndpointFor( osctap::IpEndpointName( 127, 0, 0, 1, 0 ) ).port;
    CHECK( port > 0 );

    std::thread serverThread( [&]{ server.Run(); } );

    // Client: connect and send four messages. The last is large enough to be
    // split across TCP segments, forcing the server-side deframer to reassemble.
    const std::string big( 4000, 'x' );
    try {
        osctap::TcpTransmitSocket client( osctap::IpEndpointName( 127, 0, 0, 1, port ) );
        char buf[8192];
        {
            osctap::OutboundPacketStream p( buf, sizeof(buf) );
            p << osctap::BeginMessage( "/m1" ) << (int32_t)11 << osctap::EndMessage();
            client.Send( p.Data(), p.Size() );
        }
        {
            osctap::OutboundPacketStream p( buf, sizeof(buf) );
            p << osctap::BeginMessage( "/m2" ) << (int32_t)22 << osctap::EndMessage();
            client.Send( p.Data(), p.Size() );
        }
        {
            osctap::OutboundPacketStream p( buf, sizeof(buf) );
            p << osctap::BeginMessage( "/m3" ) << "hello" << osctap::EndMessage();
            client.Send( p.Data(), p.Size() );
        }
        {
            osctap::OutboundPacketStream p( buf, sizeof(buf) );
            p << osctap::BeginMessage( "/big" ) << big.c_str() << osctap::EndMessage();
            client.Send( p.Data(), p.Size() );
        }
    } catch( const std::exception& e ) {
        std::printf( "FAIL: client error: %s\n", e.what() );
        ++failures;
    }

    // Wait (bounded) for all four to arrive, then stop the server.
    for( int i = 0; i < 500 && listener.count.load() < 4; ++i )
        std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );

    server.AsynchronousBreak();
    serverThread.join();

    CHECK( listener.count.load() == 4 );
    if( listener.addresses.size() == 4 ){
        CHECK( listener.addresses[0] == "/m1" && listener.sizes[0] == 11 );
        CHECK( listener.addresses[1] == "/m2" && listener.sizes[1] == 22 );
        CHECK( listener.addresses[2] == "/m3" && listener.sizes[2] == 5 );      // strlen("hello")
        CHECK( listener.addresses[3] == "/big" && listener.sizes[3] == 4000 );  // reassembled
    }

    if( failures == 0 )
        std::printf( "OscTcpTest: OK (4 packets over TCP, incl. a reassembled 4000-byte message)\n" );
    return failures == 0 ? 0 : 1;
}
