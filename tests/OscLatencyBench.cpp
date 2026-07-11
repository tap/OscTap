/*
    OscTap realtime latency benchmark (secondary RT signal).

    The primary realtime guarantee is compiler-checked (OSCTAP_REALTIME +
    RealtimeSanitizer / -Wfunction-effects; see OscRealtimeTest.cpp and ROADMAP.md).
    This benchmark is the *secondary* signal the ROADMAP calls for: it measures the
    worst-case wall-clock latency of the two hot paths over many iterations so a
    regression (e.g. an accidental allocation, or a much slower max than median)
    shows up as a number.

    It asserts nothing and always exits 0 -- it prints a distribution
    (min/median/p99/max ns). Run it standalone:
        cmake -S . -B build && cmake --build build --target OscLatencyBench
        ./build/OscLatencyBench [iterations]
*/

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "osc/OscOutboundPacketStream.h"
#include "osc/OscReceivedElements.h"

using namespace oscpack;

// volatile sink: keeps the optimiser from deleting the work we are timing.
static volatile int64_t g_sink = 0;

static std::size_t BuildMessage(char* buf, std::size_t cap) {
    OutboundPacketStream p(buf, cap);
    const unsigned char  blob[] = {1, 2, 3, 4, 5, 6, 7, 8};
    p << BeginMessage("/bench/path") << (int32_t)42 << 3.14159f << (int64_t)123456789 << "a-string-argument" << true
      << Blob(blob, (osc_bundle_element_size_t)sizeof(blob)) << EndMessage();
    return p.Size();
}

// The realtime read/dispatch hot path over a known-valid message.
static int64_t ReadHotPath(const ReceivedMessage& m) {
    int64_t acc = m.AddressPattern()[0];
    for (ReceivedMessage::const_iterator i = m.ArgumentsBegin(); i != m.ArgumentsEnd(); ++i) {
        switch (i->TypeTag()) {
        case INT32_TYPE_TAG:
            acc += i->AsInt32Unchecked();
            break;
        case FLOAT_TYPE_TAG:
            acc += (int64_t)i->AsFloatUnchecked();
            break;
        case INT64_TYPE_TAG:
            acc += i->AsInt64Unchecked();
            break;
        case STRING_TYPE_TAG:
            acc += i->AsStringUnchecked()[0];
            break;
        case TRUE_TYPE_TAG:
            acc += 1;
            break;
        case BLOB_TYPE_TAG: {
            const void*               d;
            osc_bundle_element_size_t s;
            i->AsBlobUnchecked(d, s);
            acc += s;
        } break;
        default:
            break;
        }
    }
    return acc;
}

static void Report(const char* label, std::vector<double>& ns) {
    std::sort(ns.begin(), ns.end());
    auto pct = [&](double p) { return ns[(std::size_t)(p * (ns.size() - 1))]; };
    std::printf("  %-16s  min=%6.0f  median=%6.0f  p99=%7.0f  max=%8.0f  ns/op\n", label, ns.front(), pct(0.5),
                pct(0.99), ns.back());
}

int main(int argc, char** argv) {
    const int N = (argc > 1) ? std::atoi(argv[1]) : 200000;
    using clk   = std::chrono::high_resolution_clock;

    char              buffer[256];
    const std::size_t size = BuildMessage(buffer, sizeof(buffer));

    // --- read/dispatch hot path: read an already-validated message ---
    ReceivedMessage m(ReceivedPacket(buffer, size));
    for (int i = 0; i < 1000; ++i)
        g_sink += ReadHotPath(m); // warm up

    std::vector<double> readNs;
    readNs.reserve(N);
    for (int i = 0; i < N; ++i) {
        const auto t0 = clk::now();
        g_sink += ReadHotPath(m);
        const auto t1 = clk::now();
        readNs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    // --- serialize: build the message into a buffer ---
    char obuf[256];
    for (int i = 0; i < 1000; ++i)
        g_sink += (int)BuildMessage(obuf, sizeof(obuf)); // warm up

    std::vector<double> sendNs;
    sendNs.reserve(N);
    for (int i = 0; i < N; ++i) {
        const auto        t0 = clk::now();
        const std::size_t s  = BuildMessage(obuf, sizeof(obuf));
        const auto        t1 = clk::now();
        g_sink += (int64_t)s + obuf[0];
        sendNs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    std::printf("OscLatencyBench (%d iterations; timer overhead included):\n", N);
    Report("read hot path", readNs);
    Report("serialize", sendNs);
    return 0;
}
