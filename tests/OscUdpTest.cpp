/*
    OscTap OSC-over-UDP loopback test (POSIX).

    Asserting counterpart to the best-effort packet in OscConcurrencyTest: it binds
    a real UdpListeningReceiveSocket on loopback, sends several OSC messages through
    a UdpTransmitSocket, and verifies each arrives and decodes correctly. The TCP
    path already had such a test (OscTcpTest); this gives the UDP socket path the
    same asserting coverage.

    Resilient to sandboxed CI that forbids loopback UDP (some macOS runners throw
    on connect/send): if the environment denies networking before any data flows,
    the test SKIPs (prints a notice, returns success) rather than failing.
*/

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "ip/IpEndpointName.h"
#include "ip/UdpSocket.h"
#include "osc/OscOutboundPacketStream.h"
#include "osc/OscPacketListener.h"

namespace {

    class RecordingListener : public osctap::OscPacketListener {
      public:
        std::atomic<int>         count{0};
        std::vector<std::string> addresses; // written by receive thread, read after join
        std::vector<int>         values;

      protected:
        void ProcessMessage(const osctap::ReceivedMessage& m, const osctap::IpEndpointName&) override {
            addresses.emplace_back(m.AddressPattern());
            int  v = 0;
            auto a = m.ArgumentsBegin();
            if (a != m.ArgumentsEnd()) {
                if (a->IsInt32())
                    v = a->AsInt32Unchecked();
                else if (a->IsString())
                    v = (int)std::strlen(a->AsStringUnchecked());
            }
            values.push_back(v);
            count.fetch_add(1, std::memory_order_relaxed);
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

} // namespace

int main() {
    RecordingListener listener;

    // Bind the receiver to an OS-assigned loopback port. A bind failure here means
    // the environment forbids loopback networking -> skip.
    osctap::UdpListeningReceiveSocket* receiver = nullptr;
    try {
        receiver = new osctap::UdpListeningReceiveSocket(osctap::IpEndpointName(127, 0, 0, 1, 0), &listener);
    }
    catch (const std::exception& e) {
        std::printf("OscUdpTest: SKIP (cannot bind loopback UDP: %s)\n", e.what());
        return 0;
    }
    const int port = receiver->LocalPort();

    std::thread runner([&] { receiver->Run(); });

    bool sent = false;
    try {
        osctap::UdpTransmitSocket sender(osctap::IpEndpointName(127, 0, 0, 1, port));
        char                      buf[256];
        const char*               addrs[] = {"/u1", "/u2", "/u3"};
        const int                 ints[]  = {11, 22, 33};
        for (int i = 0; i < 3; ++i) {
            osctap::OutboundPacketStream p(buf, sizeof(buf));
            p << osctap::BeginMessage(addrs[i]) << (int32_t)ints[i] << osctap::EndMessage();
            sender.Send(p.Data(), p.Size());
        }
        sent = true;
    }
    catch (const std::exception& e) {
        std::printf("OscUdpTest: SKIP (loopback UDP send unavailable: %s)\n", e.what());
    }

    if (sent) {
        for (int i = 0; i < 500 && listener.count.load() < 3; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    receiver->AsynchronousBreak();
    runner.join();
    delete receiver;

    if (!sent)
        return 0; // skipped: send path unavailable in this environment

    CHECK(listener.count.load() == 3);
    if (listener.addresses.size() == 3) {
        CHECK(listener.addresses[0] == "/u1" && listener.values[0] == 11);
        CHECK(listener.addresses[1] == "/u2" && listener.values[1] == 22);
        CHECK(listener.addresses[2] == "/u3" && listener.values[2] == 33);
    }

    if (failures == 0)
        std::printf("OscUdpTest: OK (3 packets over UDP loopback)\n");
    return failures == 0 ? 0 : 1;
}
