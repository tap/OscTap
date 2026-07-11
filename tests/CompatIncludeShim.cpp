/*
    OscTap compatibility coverage.

    Phase 0 renamed the C++ namespace oscpack -> osctap (keeping `oscpack` as a
    deprecated alias); Phase 1 renamed the on-disk directory / public include
    prefix oscpack/ -> osctap/ (keeping a redirecting <oscpack/...> shim tree).

    Both compatibility layers are the migration "moat" for the existing oscpack
    install base, so they must not regress. This translation unit is the live,
    CI-built guard for them: it deliberately includes via the OLD <oscpack/...>
    paths and uses the OLD `oscpack::` namespace. If either shim is removed, this
    stops compiling. Do NOT "modernize" it to <osctap/...> / `osctap::`.
*/

// Deprecated include paths on purpose -- exercises the redirect shim under oscpack/.
#include <cstring>
#include <iostream>

#include <oscpack/ip/IpEndpointName.h>
#include <oscpack/osc/OscOutboundPacketStream.h>
#include <oscpack/osc/OscPrintReceivedElements.h>
#include <oscpack/osc/OscReceivedElements.h>

int main() {
    // Pack a message through the old `oscpack::` namespace + old include paths...
    char                          buffer[256];
    oscpack::OutboundPacketStream p(buffer, sizeof(buffer));
    p << oscpack::BeginMessage("/test") << (int32_t)42 << "hello" << oscpack::EndMessage();

    // ...then parse it back and confirm the shimmed types interoperate.
    oscpack::ReceivedPacket  packet(p.Data(), p.Size());
    oscpack::ReceivedMessage msg(packet);

    if (std::strcmp(msg.AddressPattern(), "/test") != 0) {
        std::cerr << "compat-shim: unexpected address pattern\n";
        return 1;
    }

    oscpack::ReceivedMessage::const_iterator arg = msg.ArgumentsBegin();
    int32_t                                  i   = (arg++)->AsInt32();
    const char*                              s   = (arg++)->AsString();
    if (i != 42 || std::strcmp(s, "hello") != 0) {
        std::cerr << "compat-shim: unexpected argument values\n";
        return 1;
    }

    std::cout << "compat-shim: <oscpack/...> include paths and oscpack:: namespace OK\n";
    return 0;
}
