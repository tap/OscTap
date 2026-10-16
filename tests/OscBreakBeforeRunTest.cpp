/*
    OscTap break-before-Run() regression test (all platforms).

    Regression test for issue #30. Every receive loop (posix/win32 x UDP/TCP) used
    to clear its break flag at the top of Run(), so a Break()/AsynchronousBreak()
    that landed *before* Run() started was silently discarded and Run() blocked
    forever -- the usual "AsynchronousBreak() then join()" shutdown could deadlock
    whenever the receive thread was slow to reach Run(). The fix consumes the stop
    request when Run() returns instead, so a pending break makes Run() return at
    once.

      1. AsynchronousBreak() then Run() -> Run() returns.
      2. Break() then Run()             -> Run() returns (a synchronous break
                                           requested while idle is honoured too).
      3. After a consumed break, Run() blocks normally and still services its
         socket until a break arrives mid-Run(): the flag was cleared on exit,
         and the leftover wake-up from (1) is harmless.
      4. The TCP server honours a pending break the same way.

    Steps 1, 2 and 4 are deterministic and single-threaded; a watchdog thread
    fails the test instead of letting a regression hang CI.

    Binds loopback sockets only. If the environment forbids even that (some
    sandboxed CI), the test SKIPs like OscUdpTest / OscTcpTest.
*/

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <thread>

#include "osctap/ip/IpEndpointName.h"
#include "osctap/ip/PacketListener.h"
#include "osctap/ip/TcpSocket.h"
#include "osctap/ip/UdpSocket.h"

namespace {

    class CountingListener : public osctap::PacketListener {
      public:
        std::atomic<int> packets{0};
        void             ProcessPacket(const char*, int, const osctap::IpEndpointName&) override {
            packets.fetch_add(1, std::memory_order_relaxed);
        }
    };

    int failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                                                           \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

    // Names the step in progress so a watchdog timeout says which Run() hung.
    std::atomic<const char*> currentStep{"startup"};

    void step(const char* name) {
        currentStep.store(name, std::memory_order_relaxed);
        std::printf("OscBreakBeforeRunTest: %s\n", name);
        std::fflush(stdout);
    }

} // namespace

int main() {
    // Watchdog: a lost break makes Run() block forever, which would otherwise
    // hang the whole ctest run. Fail loudly instead. Generous enough for slow
    // (sanitizer / emulated) CI legs; the happy path finishes in milliseconds.
    std::atomic<bool> done{false};
    std::thread       watchdog([&] {
        for (int i = 0; i < 200 && !done.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!done.load(std::memory_order_acquire)) {
            std::printf("FAIL: Run() did not return (hung at step: %s)\n", currentStep.load(std::memory_order_relaxed));
            std::fflush(stdout);
            std::_Exit(1);
        }
    });
    watchdog.detach();

    CountingListener listener;

    // --- UDP ---------------------------------------------------------------
    osctap::UdpListeningReceiveSocket* udp = nullptr;
    try {
        udp = new osctap::UdpListeningReceiveSocket(osctap::IpEndpointName(127, 0, 0, 1, 0), &listener);
    }
    catch (const std::exception& e) {
        std::printf("OscBreakBeforeRunTest: SKIP (cannot bind loopback UDP: %s)\n", e.what());
        done.store(true, std::memory_order_release);
        return 0;
    }

    // 1. An asynchronous break requested before Run() must make Run() return.
    step("UDP: AsynchronousBreak() before Run()");
    udp->AsynchronousBreak();
    udp->Run();

    // 2. A synchronous break requested while nothing is running is a stop
    //    request too; the next Run() honours it.
    step("UDP: Break() before Run()");
    udp->Break();
    udp->Run();

    // Again on the same socket: the pending wake-up left behind by (1) (a
    // self-pipe byte / signalled event) must not make a later Run() misbehave.
    step("UDP: second AsynchronousBreak() before Run()");
    udp->AsynchronousBreak();
    udp->Run();

    // 3. After a consumed break the loop runs normally: the flag was cleared on
    //    exit, so the next Run() blocks (rather than returning at once) until a
    //    break arrives *during* Run() -- the pre-existing path, which must not
    //    regress either. A best-effort loopback packet additionally shows the
    //    loop is servicing its socket while it blocks.
    step("UDP: Run() blocks normally after a consumed break");
    {
        std::atomic<bool> returned{false};
        std::thread       runner([&] {
            udp->Run();
            returned.store(true, std::memory_order_release);
        });

        bool sent = false;
        try {
            osctap::UdpTransmitSocket sender(osctap::IpEndpointName(127, 0, 0, 1, udp->LocalPort()));
            const char                ping[] = {'/', 'p', 0, 0, ',', 0, 0, 0}; // minimal valid OSC message
            sender.Send(ping, sizeof(ping));
            sent = true;
        }
        catch (const std::exception&) {
            // loopback UDP send unavailable in this environment (some sandboxed
            // CI); the blocking check below still runs.
        }
        for (int i = 0; i < 200 && sent && listener.packets.load(std::memory_order_relaxed) == 0; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        CHECK(!returned.load(std::memory_order_acquire)); // Run() must still be blocking
        if (sent) {
            CHECK(listener.packets.load(std::memory_order_relaxed) == 1);
        }

        udp->AsynchronousBreak(); // a single break, mid-Run(): the usual shutdown
        runner.join();
    }
    delete udp;

    // --- TCP ---------------------------------------------------------------
    osctap::TcpListeningReceiveSocket* tcp = nullptr;
    try {
        tcp = new osctap::TcpListeningReceiveSocket(osctap::IpEndpointName(127, 0, 0, 1, 0), &listener);
    }
    catch (const std::exception& e) {
        std::printf("OscBreakBeforeRunTest: TCP part SKIPPED (cannot bind loopback TCP: %s)\n", e.what());
        tcp = nullptr;
    }
    if (tcp) {
        // 4. The TCP server honours a pending break the same way.
        step("TCP: AsynchronousBreak() before Run()");
        tcp->AsynchronousBreak();
        tcp->Run();

        step("TCP: Break() before Run()");
        tcp->Break();
        tcp->Run();

        // And a break requested while Run() is already blocked still works.
        step("TCP: AsynchronousBreak() during Run()");
        std::atomic<bool> returned{false};
        std::thread       runner([&] {
            tcp->Run();
            returned.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(!returned.load(std::memory_order_acquire)); // Run() must still be blocking
        tcp->AsynchronousBreak();
        runner.join();
        delete tcp;
    }

    done.store(true, std::memory_order_release);

    if (failures) {
        std::printf("OscBreakBeforeRunTest: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("OscBreakBeforeRunTest: OK (a break requested before Run() is honoured)\n");
    return 0;
}
