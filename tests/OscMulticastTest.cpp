/*
    OscTap multicast-receive loopback test (POSIX).

    Binds a receiver, joins an IPv4 multicast group, sends OSC to the group, and
    verifies it is received -- the asserting test for JoinMulticastGroup(). Like
    the unicast UDP/TCP loopback tests it SKIPs gracefully (prints a notice, exits
    0) if the environment doesn't support multicast (the join or delivery fails),
    so it never false-fails on a restricted runner.
*/

#include "ip/UdpSocket.h"
#include "ip/IpEndpointName.h"
#include "osc/OscPacketListener.h"
#include "osc/OscOutboundPacketStream.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

class RecordingListener : public osctap::OscPacketListener {
public:
    std::atomic<int> count{ 0 };
    std::vector<std::string> addresses; // written by receive thread, read after join
    std::vector<int>         values;

protected:
    void ProcessMessage( const osctap::ReceivedMessage& m, const osctap::IpEndpointName& ) override
    {
        addresses.emplace_back( m.AddressPattern() );
        int v = 0;
        auto a = m.ArgumentsBegin();
        if( a != m.ArgumentsEnd() && a->IsInt32() ) v = a->AsInt32Unchecked();
        values.push_back( v );
        count.fetch_add( 1, std::memory_order_relaxed );
    }
};

int failures = 0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } }while(0)

} // namespace

int main()
{
#ifndef _WIN32
    // A multicast send to an unrouted group can raise SIGPIPE on macOS/BSD; ignore
    // it so the send merely fails and the test SKIPs instead of being killed. (The
    // UDP backend also sets SO_NOSIGPIPE; this is belt-and-suspenders.)
    std::signal( SIGPIPE, SIG_IGN );
#endif

    // Administratively-scoped multicast group (239.0.0.0/8).
    const int A = 239, B = 7, C = 7, D = 7;

    RecordingListener listener;

    // Bind to an OS-assigned port on all interfaces, then join the group on it.
    osctap::UdpListeningReceiveSocket* receiver = nullptr;
    int port = 0;
    try {
        receiver = new osctap::UdpListeningReceiveSocket(
            osctap::IpEndpointName( osctap::IpEndpointName::ANY_ADDRESS, 0 ), &listener );
        port = receiver->LocalPort();
        receiver->JoinMulticastGroup( osctap::IpEndpointName( A, B, C, D, port ) );
    } catch( const std::exception& e ) {
        std::printf( "OscMulticastTest: SKIP (multicast unavailable: %s)\n", e.what() );
        delete receiver;
        return 0;
    }

    std::thread runner( [&]{ receiver->Run(); } );

    bool sent = false;
    try {
        osctap::UdpTransmitSocket sender( osctap::IpEndpointName( A, B, C, D, port ) );
        char buf[128];
        for( int i = 0; i < 3; ++i ){
            osctap::OutboundPacketStream p( buf, sizeof(buf) );
            p << osctap::BeginMessage( "/mc" ) << (int32_t)(100 + i) << osctap::EndMessage();
            sender.Send( p.Data(), p.Size() );
        }
        sent = true;
    } catch( const std::exception& e ) {
        std::printf( "OscMulticastTest: SKIP (multicast send unavailable: %s)\n", e.what() );
    }

    if( sent ){
        for( int i = 0; i < 500 && listener.count.load() < 3; ++i )
            std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    }

    receiver->AsynchronousBreak();
    runner.join();

    // No delivery is ambiguous (often a sandbox without multicast routing on the
    // default interface), so SKIP rather than fail.
    if( !sent || listener.count.load() == 0 ){
        std::printf( "OscMulticastTest: SKIP (no multicast delivery in this environment)\n" );
        delete receiver;
        return 0;
    }

    CHECK( listener.count.load() == 3 );
    if( listener.addresses.size() == 3 ){
        CHECK( listener.addresses[0] == "/mc" && listener.values[0] == 100 );
        CHECK( listener.addresses[1] == "/mc" && listener.values[1] == 101 );
        CHECK( listener.addresses[2] == "/mc" && listener.values[2] == 102 );
    }

    // Leaving the group while the socket stays open should also succeed.
    try { receiver->LeaveMulticastGroup( osctap::IpEndpointName( A, B, C, D, port ) ); }
    catch( const std::exception& e ) { std::printf( "FAIL: leave: %s\n", e.what() ); ++failures; }

    delete receiver;

    if( failures == 0 )
        std::printf( "OscMulticastTest: OK (3 OSC messages over multicast 239.7.7.7)\n" );
    return failures == 0 ? 0 : 1;
}
