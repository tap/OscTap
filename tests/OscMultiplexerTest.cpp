/*
    OscTap SocketReceiveMultiplexer test (all platforms).

    The multiplexer is the oscpack API for running several UDP sockets and
    periodic timers in one Run() loop; UdpListeningReceiveSocket is the one-socket
    convenience wrapper over it. Until now the facade had no public alias and its
    AttachSocketListener() took the platform implementation type rather than the
    public UdpSocket, so it was unusable as documented. This test uses it the way
    docs/API.md describes:

      - two UdpReceiveSockets on loopback, each with its own PacketListener;
      - a periodic TimerListener that counts expiries and Break()s the loop;
      - a best-effort loopback packet to each socket, asserted only if the send
        path is available (some sandboxed CI forbids loopback UDP sends).

    Single-threaded and deterministic: the timer ends Run(), so no thread or
    AsynchronousBreak() is needed. SKIPs if loopback sockets cannot be bound.
*/

#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>

#include "osctap/ip/IpEndpointName.h"
#include "osctap/ip/PacketListener.h"
#include "osctap/ip/TimerListener.h"
#include "osctap/ip/UdpSocket.h"

namespace {

    class CountingListener : public osctap::PacketListener {
      public:
        int  packets = 0;
        void ProcessPacket(const char*, int, const osctap::IpEndpointName&) override { ++packets; }
    };

    // Breaks the loop once both packets have arrived (or after a bounded number
    // of expiries if they never do), so Run() always returns.
    class StopTimer : public osctap::TimerListener {
      public:
        StopTimer(osctap::SocketReceiveMultiplexer& mux, const CountingListener& a, const CountingListener& b)
            : mux_(mux)
            , a_(a)
            , b_(b) {}

        int expiries = 0;

        void TimerExpired() override {
            ++expiries;
            if ((a_.packets > 0 && b_.packets > 0) || expiries >= 200) {
                mux_.Break();
            }
        }

      private:
        osctap::SocketReceiveMultiplexer& mux_;
        const CountingListener&           a_;
        const CountingListener&           b_;
    };

    int failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                                                           \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

} // namespace

int main() {
    osctap::UdpReceiveSocket* rxA = nullptr;
    osctap::UdpReceiveSocket* rxB = nullptr;
    try {
        rxA = new osctap::UdpReceiveSocket(osctap::IpEndpointName(127, 0, 0, 1, 0));
        rxB = new osctap::UdpReceiveSocket(osctap::IpEndpointName(127, 0, 0, 1, 0));
    }
    catch (const std::exception& e) {
        std::printf("OscMultiplexerTest: SKIP (cannot bind loopback UDP: %s)\n", e.what());
        delete rxA;
        return 0;
    }
    CHECK(rxA->LocalPort() > 0);
    CHECK(rxB->LocalPort() > 0);
    CHECK(rxA->LocalPort() != rxB->LocalPort());

    CountingListener listenerA, listenerB;

    osctap::SocketReceiveMultiplexer mux;
    StopTimer                        timer(mux, listenerA, listenerB);
    mux.AttachSocketListener(rxA, &listenerA);
    mux.AttachSocketListener(rxB, &listenerB);
    mux.AttachPeriodicTimerListener(5, &timer);

    // Best-effort sends: one datagram to each socket. If the environment forbids
    // loopback sends, the timer still ends Run() after its bounded expiry count.
    bool sent = false;
    try {
        osctap::UdpTransmitSocket txA(osctap::IpEndpointName(127, 0, 0, 1, rxA->LocalPort()));
        osctap::UdpTransmitSocket txB(osctap::IpEndpointName(127, 0, 0, 1, rxB->LocalPort()));
        const char                ping[] = {'/', 'p', 0, 0, ',', 0, 0, 0}; // minimal valid OSC message
        txA.Send(ping, sizeof(ping));
        txB.Send(ping, sizeof(ping));
        sent = true;
    }
    catch (const std::exception& e) {
        std::printf("OscMultiplexerTest: loopback UDP send unavailable (%s); checking the timer path only\n", e.what());
    }

    mux.Run(); // returns when the timer Break()s

    CHECK(timer.expiries >= 1);
    if (sent) {
        CHECK(listenerA.packets == 1);
        CHECK(listenerB.packets == 1);
    }

    // A second Run() on the same multiplexer works too (the timer re-arms and
    // breaks again), proving the Break() above was consumed on exit.
    const int before = timer.expiries;
    mux.Run();
    CHECK(timer.expiries > before);

    mux.DetachPeriodicTimerListener(&timer);
    mux.DetachSocketListener(rxB, &listenerB);
    mux.DetachSocketListener(rxA, &listenerA);
    delete rxB;
    delete rxA;

    if (failures) {
        std::printf("OscMultiplexerTest: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("OscMultiplexerTest: OK (%d timer expiries, packets A=%d B=%d)\n", timer.expiries, listenerA.packets,
                listenerB.packets);
    return 0;
}
