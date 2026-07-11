/*
    OscTap freestanding / embedded-profile smoke test.

    Built with -fno-exceptions -fno-rtti -DOSCTAP_FREESTANDING (see CMake option
    OSCTAP_FREESTANDING and the `freestanding` CI job). Its job is to prove that
    the parse/serialize core compiles and runs with C++ exceptions disabled and
    without the hosted-only facilities (<iostream>, std::vector OwnedMessage)
    that the freestanding profile drops -- the exact shape an embedded target
    such as a Raspberry Pi Pico 2W builds. See docs/EMBEDDED_PICO2W.md.

    Note: with exceptions disabled, validation failures abort (OSCTAP_THROW ->
    fatal handler). This test therefore exercises only the valid-input happy
    path; malformed-input behaviour is covered by the hosted OscUnitTests suite.
*/

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "osc/OscOutboundPacketStream.h"
#include "osc/OscReceivedElements.h"

// Guard rails: this TU must be compiled as the freestanding profile.
#if OSCTAP_HAS_EXCEPTIONS
#error "OscFreestandingTest must be built with exceptions disabled (-fno-exceptions)"
#endif
#ifndef OSCTAP_FREESTANDING
#error "OscFreestandingTest must be built with -DOSCTAP_FREESTANDING"
#endif

static int failures = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::printf("FAIL: %s (line %d)\n", #cond, __LINE__);                                                      \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

int main() {
    // --- serialize a message on the stack (no heap) ------------------------
    char        buffer[256];
    const char* runtimeStr = "pico"; // a runtime const char* (not a literal):
                                     // must serialize as a string, not a bool.
    osctap::OutboundPacketStream p(buffer, sizeof(buffer));
    p << osctap::BeginMessage("/freestanding") << true << (int32_t)2350 << (float)3.14159f << runtimeStr
      << osctap::EndMessage();

    CHECK(p.IsReady());
    CHECK(p.Size() > 0);

    // --- parse it back -----------------------------------------------------
    osctap::ReceivedMessage msg(osctap::ReceivedPacket(p.Data(), p.Size()));

    CHECK(std::strcmp(msg.AddressPattern(), "/freestanding") == 0);

    // Checked accessors: these route validation through OSCTAP_THROW, so their
    // mere compilation here proves the no-exceptions seam builds. Input is
    // valid, so no fatal handler fires.
    osctap::ReceivedMessage::const_iterator arg = msg.ArgumentsBegin();
    CHECK(arg->AsBool() == true);
    ++arg;
    CHECK(arg->AsInt32() == 2350);
    ++arg;
    CHECK(arg->AsFloat() > 3.14f && arg->AsFloat() < 3.15f);
    ++arg;
    CHECK(std::strcmp(arg->AsString(), "pico") == 0);
    ++arg;
    CHECK(arg == msg.ArgumentsEnd());

    // Realtime read path: the throw-free *Unchecked accessors over a known-valid
    // message -- the hot loop an audio/embedded integrator runs every packet.
    arg = msg.ArgumentsBegin();
    CHECK(arg->AsBoolUnchecked() == true);
    ++arg;
    CHECK(arg->AsInt32Unchecked() == 2350);
    ++arg;
    CHECK(arg->AsFloatUnchecked() > 3.14f);
    ++arg;
    CHECK(std::strcmp(arg->AsStringUnchecked(), "pico") == 0);

    // --- non-throwing validation gate (the point of TryInit/TryValidatePacket) ---
    // On this build OSCTAP_THROW would abort via the fatal handler, so the only
    // safe way to handle untrusted input is to gate it first. Reaching these lines
    // at all proves the gate returns instead of throwing/aborting.
    typedef osctap::osc_bundle_element_size_t sz_t;
    CHECK(osctap::TryValidatePacket(p.Data(), (sz_t)p.Size()) == nullptr); // valid -> accepted

    // truncate the valid message by one 4-byte word: arguments now exceed size.
    CHECK(osctap::TryValidatePacket(p.Data(), (sz_t)(p.Size() - 4)) != nullptr);

    // structurally bogus little buffer (not a valid message): rejected, not fatal.
    const char bad[6] = {'/', 'x', '\0', '\0', ',', 'i'};
    CHECK(osctap::TryValidatePacket(bad, (sz_t)sizeof(bad)) != nullptr);

    // The same gate also drives a no-abort ReceivedMessage parse:
    osctap::ReceivedMessage probe;
    CHECK(probe.TryInit(p.Data(), (sz_t)p.Size()) == nullptr);
    CHECK(std::strcmp(probe.AddressPattern(), "/freestanding") == 0);

    if (failures == 0)
        std::printf("OscFreestandingTest: OK (exceptions disabled, freestanding)\n");
    return failures == 0 ? 0 : 1;
}
