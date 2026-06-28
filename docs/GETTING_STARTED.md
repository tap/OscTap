# Getting started with OscTap

A 10-minute tour: build OscTap, send an OSC message over UDP, receive and parse
one. This covers the common case (OSC over UDP on a desktop/SBC). For other
transports and targets see [OSC over TCP](OSC_OVER_TCP.md), the
[Pico 2W embedded guide](EMBEDDED_PICO2W.md), and the
[Pi 5 ⇄ Pico ⇄ Android tutorial](INTEGRATION_PI5_PICO_ANDROID.md). For the full
public surface see the [API reference](API.md).

## Install / build

OscTap's **core is header-only** — add the repo root to your include path and
`#include` what you need; there's nothing to compile or link for the OSC packet
classes. The UDP/TCP socket classes are also header-only but link a platform
socket library (`ws2_32`/`winmm` on Windows; nothing extra on POSIX).

With CMake, link the interface target:

```cmake
add_subdirectory(OscTap)        # or vendor the headers
target_link_libraries(myapp PRIVATE oscpack)   # header-only INTERFACE target
```

Or just point at the headers:

```sh
g++ -std=c++17 -I path/to/OscTap -I path/to/OscTap/osctap myapp.cpp -o myapp
```

Public headers live under `<osctap/...>`; the old `<oscpack/...>` paths and the
`oscpack::` namespace still work as a deprecated compatibility alias, so code
written for oscpack keeps compiling.

## Send a message

Serialize into a buffer **you** own (no heap allocation), then put the bytes on
the wire. The buffer can be on the stack.

```cpp
#include "osc/OscOutboundPacketStream.h"
#include "ip/UdpSocket.h"

int main()
{
    osctap::UdpTransmitSocket tx( osctap::IpEndpointName( "127.0.0.1", 7000 ) );

    char buffer[1024];
    osctap::OutboundPacketStream p( buffer, sizeof(buffer) );
    p << osctap::BeginMessage( "/synth/freq" )
        << 440.0f << true << "sine"
      << osctap::EndMessage();

    tx.Send( p.Data(), p.Size() );
}
```

`OutboundPacketStream` writes OSC types from their C++ counterparts: `int32_t`,
`float`, `double`, `bool` (→ `T`/`F`), `const char*`/`std::string` (→ string),
plus `Blob`, `TimeTag`, `MidiMessage`, `RgbaColor`, `OscNil()`, `Infinitum()`. If
the message would overflow your buffer it throws `OutOfBufferMemoryException`, so
size the buffer for your largest message.

## Receive and parse a message

Subclass `OscPacketListener` (it unpacks bundles for you and bounds nesting
depth), bind a socket, and run the receive loop.

```cpp
#include "osc/OscPacketListener.h"
#include "ip/UdpSocket.h"
#include <cstring>
#include <iostream>

class MyListener : public osctap::OscPacketListener {
protected:
    void ProcessMessage( const osctap::ReceivedMessage& m,
                         const osctap::IpEndpointName& from ) override
    {
        try {
            if( std::strcmp( m.AddressPattern(), "/synth/freq" ) == 0 ) {
                auto arg = m.ArgumentsBegin();
                float freq   = (arg++)->AsFloat();
                bool  on     = (arg++)->AsBool();
                const char* wave = (arg++)->AsString();
                std::cout << "freq=" << freq << " on=" << on << " wave=" << wave << "\n";
            }
        } catch( const osctap::Exception& e ) {
            // wrong/missing argument types are reported by exception
            std::cout << "bad message: " << e.what() << "\n";
        }
    }
};

int main()
{
    MyListener listener;
    osctap::UdpListeningReceiveSocket s(
        osctap::IpEndpointName( osctap::IpEndpointName::ANY_ADDRESS, 7000 ), &listener );
    s.Run();   // blocks; call s.AsynchronousBreak() from another thread/handler to stop
}
```

### Reading arguments

Three styles, pick what fits:

- **Checked accessors** — `arg->AsInt32()`, `AsFloat()`, `AsString()`, … throw
  `WrongArgumentTypeException` / `MissingArgumentException` on a mismatch. Safe
  default.
- **Type-test then unchecked** — `if (arg->IsFloat()) arg->AsFloatUnchecked()`.
  The `*Unchecked` accessors don't validate; they're the realtime-safe read path
  once you've checked the tag (or validated the whole packet — see below).
- **Argument stream** — `m.ArgumentStream() >> a >> b >> endTag;` for fixed
  layouts.

### Bundles

`OscPacketListener` traverses bundles automatically and calls `ProcessMessage`
for each contained message. To build one:

```cpp
p << osctap::BeginBundleImmediate()
    << osctap::BeginMessage( "/a" ) << 1 << osctap::EndMessage()
    << osctap::BeginMessage( "/b" ) << 2 << osctap::EndMessage()
  << osctap::EndBundle();
```

## Handling untrusted input

OSC arriving over a network is untrusted. By default the parser **throws** on a
malformed packet (`Malformed{Packet,Message,Bundle}Exception`), which you catch to
drop it. If you build with **exceptions disabled** (the freestanding/embedded
profile), validation instead aborts — so first gate the bytes:

```cpp
if( osctap::TryValidatePacket( data, size ) == nullptr ) {
    osctap::ReceivedPacket p( data, size );   // guaranteed safe to read
    // ...
} // else: drop it
```

`TryValidatePacket` is non-throwing, non-allocating, and recurses through bundles
with a nesting bound. See the [Pico 2W guide](EMBEDDED_PICO2W.md) for the embedded
story.

## Try the demos

Runnable programs under [`../demos/`](../demos) (`-DOSCTAP_BUILD_DEMOS=ON`):

```sh
cmake -S . -B build -DOSCTAP_BUILD_DEMOS=ON
cmake --build build --target osc_send pi5_hub
./build/pi5_hub 9000 &                       # a UDP hub/monitor + router
./build/osc_send 127.0.0.1 9000 /hub/led i:1 # a typed CLI sender
```

The canonical oscpack examples (`SimpleSend`, `SimpleReceive`, `OscDump`) are in
[`../examples/`](../examples).

## Where next

- [API reference](API.md) — every public type, grouped by header.
- [OSC over TCP](OSC_OVER_TCP.md) — reliable/stream transport.
- [Embedded (Pico 2W)](EMBEDDED_PICO2W.md) — no-heap / no-exceptions builds.
- [`../ROADMAP.md`](../ROADMAP.md) — design decisions and project direction.
