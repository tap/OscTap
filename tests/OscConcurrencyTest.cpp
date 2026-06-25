/*
    OscTap concurrency test (the basis for the ThreadSanitizer CI job).

    The OSC parser is single-threaded by design; the one genuine concurrency in
    the library is the network receive loop: SocketReceiveMultiplexer::Run()
    blocks in select() on one thread while another thread asks it to stop via
    AsynchronousBreak() (the documented cross-thread / signal-handler exit path).
    The break is delivered two ways at once -- an atomic flag and a self-pipe
    write that wakes select() -- so it is exactly the kind of code TSan vets.

    This test drives that interaction (plus a real loopback packet, so the
    ProcessPacket() callback runs on the receive thread concurrently with the
    sender). Under -DOSCTAP_TSAN=ON it is built with -fsanitize=thread; on the
    rest of the matrix it is a plain functional test that the receive loop starts
    and stops cleanly. ThreadSanitizer ships with GCC/Clang, so no extra runtime
    is needed. (POSIX only -- the dedicated TSan job runs on Linux.)
*/

#include "ip/UdpSocket.h"
#include "ip/IpEndpointName.h"
#include "ip/PacketListener.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

using namespace osctap;

namespace {
class CountingListener : public PacketListener {
  public:
    std::atomic<int> packets{ 0 };
    void ProcessPacket( const char * /*data*/, int /*size*/,
                        const IpEndpointName & /*remoteEndpoint*/ ) override
    {
        packets.fetch_add( 1, std::memory_order_relaxed );
    }
};
} // namespace

int main()
{
    CountingListener listener;

    // Bind a receive socket to loopback on an OS-assigned port (4-octet ctor
    // avoids a DNS lookup); LocalPort() reports the chosen port.
    UdpListeningReceiveSocket receiver(
        IpEndpointName( 127, 0, 0, 1, 0 ), &listener );
    const int port = receiver.LocalPort();

    // Run the receive loop on its own thread; it blocks in select().
    std::atomic<bool> finished{ false };
    std::thread runner( [&] {
        receiver.Run();
        finished.store( true, std::memory_order_release );
    } );

    // Best-effort: send one packet so ProcessPacket() runs on the receive thread
    // concurrently with this one (exercises the receive path, not just the
    // break). This is genuinely best-effort -- some sandboxed CI environments
    // disallow even loopback UDP connect/send (e.g. macOS runners throw
    // "unable to connect udp socket") -- so any failure is swallowed and the
    // receive-loop break test below proceeds regardless. Receipt is never
    // asserted.
    bool sent = false;
    try {
        UdpTransmitSocket sender( IpEndpointName( 127, 0, 0, 1, port ) );
        const char ping[] = { '/','p',0,0, ',',0,0,0 }; // minimal valid OSC message
        sender.Send( ping, sizeof( ping ) );
        sent = true;
    } catch( const std::exception & ) {
        // networking restricted in this environment; skip the receive coverage.
    }

    // If a packet was sent, wait (bounded) for it to be processed so
    // ProcessPacket() really does run concurrently before we stop the loop. The
    // wait is capped so a dropped datagram can't hang the test -- termination is
    // driven by the break loop below regardless.
    if( sent ){
        for( int i = 0; i < 200 && listener.packets.load( std::memory_order_relaxed ) == 0; ++i )
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }

    // Stop Run() from this thread. Run() resets its break flag when it starts, so
    // a single AsynchronousBreak() could race ahead of that reset and be missed;
    // signalling in a loop until the run thread returns is race-free and is
    // guaranteed to terminate. Repeated breaks are harmless.
    while( !finished.load( std::memory_order_acquire ) ){
        receiver.AsynchronousBreak();
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
    }

    runner.join();

    std::cout << "concurrency test: Run() vs AsynchronousBreak() OK ("
              << listener.packets.load() << " packet(s) received)\n";
    return 0;
}
