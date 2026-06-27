# OscTap on the Raspberry Pi Pico 2W (RP2350)

> Status: Phase 2 ("Reach") groundwork. The freestanding profile this guide uses
> is landed and CI-guarded; the Pico SDK glue below is an integration recipe, not
> a vendored board build. See [`../ROADMAP.md`](../ROADMAP.md).

The Pico 2W pairs the dual-core **RP2350** (Arm Cortex-M33 @ 150 MHz) with the
**CYW43439** wireless chip, so it can speak OSC over Wi-Fi/UDP. OscTap's core —
the `osc/` headers — is header-only, dependency-free, allocation-free on the
read/serialize hot path, and freestanding-friendly, which makes it a natural fit
for an embedded OSC endpoint.

This guide shows how to build the OscTap **core** against the Pico SDK + lwIP,
using the freestanding profile (exceptions/RTTI off) introduced for Phase 2.

## What you use — and what you don't

| OscTap layer | On the Pico 2W |
|--------------|----------------|
| `osc/` core — `OscReceivedElements.h`, `OscOutboundPacketStream.h`, `OscTypes.h`, `OscUtilities.h` | **Use it.** Parses/builds OSC into/out of a plain byte buffer. |
| `ip/` sockets — `ip/posix`, `ip/win32` (`UdpSocket`, `NetworkingUtils`) | **Don't use it.** Those are POSIX/WinSock backends. On the Pico, networking is **lwIP** (the SDK's TCP/IP stack), so you call lwIP's UDP API directly and hand the payload to the OscTap core. |

The split is deliberate: the OSC wire format is the reusable part; the transport
is whatever your platform provides.

## The freestanding profile (`OSCTAP_FREESTANDING`)

The Pico SDK builds C++ **without exceptions or RTTI by default**
(`PICO_CXX_ENABLE_EXCEPTIONS=0`, `PICO_CXX_ENABLE_RTTI=0`). OscTap supports that
through a single build seam (`osc/OscConfig.h`):

- `OSCTAP_HAS_EXCEPTIONS` is auto-detected from the compiler. Under
  `-fno-exceptions` it becomes `0` and every `throw` in the library is routed
  through `OSCTAP_THROW`.
- `OSCTAP_FREESTANDING` (you define it) drops the hosted-only conveniences:
  `<iostream>`, the `std::vector`-backed `OwnedMessage`, and the
  `operator<<(const std::string&)` overload. Pass `const char*`, a string
  literal, or `osctap::string_view` instead.

That combination is exactly what the `freestanding` CI job and
`tests/OscFreestandingTest.cpp` build and run on every push, so the core is kept
embedded-buildable.

### Handling validation when exceptions are off

OSC packets arriving off the network are **untrusted input**. With exceptions
enabled, OscTap's parser *throws* (`MalformedPacketException`,
`MalformedMessageException`, `MalformedBundleException`) when it rejects a bad
packet, and you `catch` it to drop that packet. With exceptions **disabled**,
there is nothing to catch — `OSCTAP_THROW` instead calls a fatal handler that
**must not return** (the default `std::abort()`s).

That has a security consequence you must design around:

> On a no-exceptions build, a single malformed packet from anyone who can reach
> the device will trigger the fatal handler — i.e. a remote reset / DoS.

Pick the model that matches your threat surface:

1. **Trusted / closed link** (a fixed sender on a private wire or AP, no exposure
   to arbitrary senders): the freestanding no-exceptions build is fine. Define
   `OSCTAP_FATAL_HANDLER` to log and reset deliberately — reaching it means a
   programming error or a genuinely corrupt link, not routine traffic.

2. **Untrusted / open network**: keep **exceptions enabled** on the Pico
   (`pico_enable_exceptions(<target>)` / `PICO_CXX_ENABLE_EXCEPTIONS=1`) and
   `catch` the `Malformed*Exception` types to drop bad packets. The OscTap API is
   identical either way — `OSCTAP_THROW` is just `throw` here. You still get the
   rest of the embedded posture (no heap on the hot path, small code).

3. **Untrusted / open network, exceptions still off** — gate with the non-throwing
   validator. `osctap::TryValidatePacket(data, size)` returns `nullptr` when the
   packet is fully well-formed (and therefore safe to construct and read without any
   `OSCTAP_THROW` firing), else a static error string. It recurses through bundles
   with a nesting bound and never throws or allocates, so you can reject bad input
   on a `-fno-exceptions` build instead of aborting:

   ```cpp
   if (osctap::TryValidatePacket(buf, n) == nullptr) {
       osctap::ReceivedMessage m(osctap::ReceivedPacket(buf, n));  // won't abort
       // ... read m ...
   } // else: drop the datagram
   ```

Either way, **validation runs in the lwIP receive callback, off any audio/render
thread** — consistent with OscTap's realtime contract (validation may throw and
is not part of the RT hot path; the throw-free `*Unchecked` accessors are).

## Build integration (CMake + Pico SDK)

Add the OscTap include root and define the profile. OscTap's core is
header-only, so there is no library to compile or link.

```cmake
# After pico_sdk_init() and your add_executable(osc_demo ...)

target_include_directories(osc_demo PRIVATE
    ${OSCTAP_DIR})                 # repo root: enables <osctap/osc/...> and the
                                   # in-tree quoted "osc/..." includes

target_compile_definitions(osc_demo PRIVATE
    OSCTAP_FREESTANDING)           # drop hosted-only facilities

# Wi-Fi UDP via lwIP (threadsafe-background or poll arch):
target_link_libraries(osc_demo
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background)

# Trusted-link model: keep exceptions off (SDK default) and provide a handler.
# Open-network model instead: pico_enable_exceptions(osc_demo)
```

If you choose the trusted-link model, define the fatal handler **before**
including any OscTap header (e.g. via a small `osctap_config.h` you force-include,
or a `-D`):

```cpp
// Logs and resets; must not return.
#define OSCTAP_FATAL_HANDLER(whatCStr)  osc_fatal((whatCStr))

[[noreturn]] void osc_fatal(const char* what);   // defined in your app
```

## Receiving OSC (lwIP UDP → OscTap)

lwIP hands you the datagram in a `pbuf` from your `udp_recv` callback. Validate +
dispatch there (off the realtime thread), then read with the throw-free
accessors in your hot loop. *Illustrative — adapt names to your SDK version:*

```cpp
#include "osc/OscReceivedElements.h"

static void on_osc_packet(void* /*arg*/, struct udp_pcb* /*pcb*/,
                          struct pbuf* p, const ip_addr_t* /*addr*/, u16_t /*port*/)
{
    if (!p) return;

    // Copy the (possibly chained) pbuf into a contiguous, 4-byte-aligned buffer.
    alignas(4) static char buf[1472];           // <= typical Ethernet MTU payload
    const u16_t n = pbuf_copy_partial(p, buf, sizeof(buf), 0);
    pbuf_free(p);

    // Open-network model: wrap construction in try/catch (exceptions ON) so a
    // malformed packet is dropped, not fatal. Trusted-link model: drop the
    // try/catch — construction aborts via OSCTAP_FATAL_HANDLER on bad input.
    osctap::ReceivedPacket packet(buf, n);
    if (!packet.IsMessage()) return;             // (handle bundles similarly)

    osctap::ReceivedMessage msg(packet);

    if (std::strcmp(msg.AddressPattern(), "/led") == 0) {
        // Known-valid from here: realtime-safe, throw-free reads.
        auto arg = msg.ArgumentsBegin();
        const bool on = arg->AsBoolUnchecked();
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
    }
}

// setup:  udp_recv(pcb, on_osc_packet, nullptr);
```

## Sending OSC (OscTap → lwIP UDP)

Serialize into a stack buffer (no heap), then ship it through an lwIP `pbuf`:

```cpp
#include "osc/OscOutboundPacketStream.h"

void send_fader(udp_pcb* pcb, const ip_addr_t* dst, uint16_t port, float v)
{
    char buffer[64];
    osctap::OutboundPacketStream p(buffer, sizeof(buffer));
    p << osctap::BeginMessage("/fader/1") << v << osctap::EndMessage();

    struct pbuf* pb = pbuf_alloc(PBUF_TRANSPORT, p.Size(), PBUF_RAM);
    std::memcpy(pb->payload, p.Data(), p.Size());
    udp_sendto(pcb, pb, dst, port);
    pbuf_free(pb);
}
```

`OutboundPacketStream` never allocates and writes only into the buffer you give
it; if the message would overflow that buffer it raises
`OutOfBufferMemoryException` (caught, or fatal, per your exception model), so size
the buffer for your largest message.

## Realtime / no-heap checklist

- Parse/serialize touch **only** your byte buffer — no `malloc` on the OscTap
  side. Keep the buffers static or on the stack.
- Read the audio-thread hot path through the `*Unchecked` accessors
  (`AsInt32Unchecked`, `AsFloatUnchecked`, …); they are the functions OscTap
  annotates realtime-safe (`OSCTAP_REALTIME`).
- Do all **validation** (constructing `ReceivedPacket`/`ReceivedMessage`) in the
  network callback, never on the audio render core.
- lwIP's own `pbuf`/heap pools are separate from OscTap; tune them via
  `lwipopts.h` as usual.

## See also

- [`../ROADMAP.md`](../ROADMAP.md) — Phase 2 plan and the realtime contract.
- [`STATUS.md`](STATUS.md) — landmines and build matrix.
- `tests/OscFreestandingTest.cpp` — the CI-built proof that the core compiles and
  runs with exceptions/RTTI disabled.
