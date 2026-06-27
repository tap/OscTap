# OSC over TCP

OscTap speaks OSC over TCP as well as UDP. This is useful when you need reliable,
ordered delivery (no dropped packets), to traverse a connection-oriented link, or
to interoperate with the many tools that default to OSC-over-TCP (SuperCollider,
Max, JUCE, liblo's `osc.tcp`).

> Status: v1 (issue #14). Length-prefix framing, a single-threaded
> multi-connection server, `TCP_NODELAY`, and a frame-size cap. SLIP framing, TLS,
> WebSocket, and `epoll` are intentionally deferred until there's demand.

## Framing

UDP gives you message boundaries for free — one datagram is exactly one OSC
packet. TCP is a byte stream with **no** boundaries, so packets must be framed.
OscTap uses the de-facto convention: each packet is a **4-byte big-endian length**
followed by that many payload bytes (the same shape as a bundle element's size
slot). Both ends must agree on this out of band — it's what SuperCollider/Max/
JUCE/liblo's `osc.tcp` use, so OscTap interoperates with them.

The framing codec ([`osc/OscStreamFraming.h`](../osctap/osc/OscStreamFraming.h)) is
transport-agnostic and usable on its own (e.g. over a serial link or your own
socket loop):

```cpp
#include "osc/OscStreamFraming.h"

// Decode: feed received bytes in whatever chunks arrive; the deframer reassembles
// complete packets and calls your sink once per packet.
osctap::OscStreamDeframer deframer;            // default 64 KiB frame-size cap
bool ok = deframer.Consume( data, n, []( const char* packet, uint32_t size ){
    osctap::ReceivedPacket p( packet, size );  // one whole OSC packet
    // ... dispatch ...
});
if( !ok ) { /* a peer announced an over-cap frame: drop the connection */ }
```

## Server (receive)

`TcpListeningReceiveSocket` accepts any number of clients and dispatches each
complete packet to your `PacketListener` / `OscPacketListener`, exactly like the
UDP `UdpListeningReceiveSocket` — the listener contract is identical, so existing
listeners work unchanged.

```cpp
#include "ip/TcpSocket.h"
#include "osc/OscPacketListener.h"

class MyListener : public osctap::OscPacketListener {
protected:
    void ProcessMessage( const osctap::ReceivedMessage& m,
                         const osctap::IpEndpointName& from ) override {
        // ... handle m ...
    }
};

MyListener listener;
osctap::TcpListeningReceiveSocket server(
    osctap::IpEndpointName( osctap::IpEndpointName::ANY_ADDRESS, 9000 ), &listener );
server.Run();                 // blocks; call server.AsynchronousBreak() to stop
```

`Run()` is single-threaded and `select()`-based; `Break()` /
`AsynchronousBreak()` stop it (the latter from another thread or a signal
handler). Connections are reaped on disconnect.

## Client (send)

`TcpTransmitSocket` connects and sends length-prefixed packets, looping over
partial writes; `TCP_NODELAY` is on (Nagle off — without it OSC-over-TCP latency
is a classic footgun).

```cpp
#include "ip/TcpSocket.h"
#include "osc/OscOutboundPacketStream.h"

osctap::TcpTransmitSocket client( osctap::IpEndpointName( "127.0.0.1", 9000 ) );

char buf[1024];
osctap::OutboundPacketStream p( buf, sizeof(buf) );
p << osctap::BeginMessage( "/fader/1" ) << 0.75f << osctap::EndMessage();
client.Send( p.Data(), p.Size() );   // prepends the 4-byte length frame
```

## Security: the length prefix is attacker-controlled

A hostile peer can announce a 2 GB frame. The deframer therefore **caps** the
frame size (default 64 KiB; configurable per socket/deframer) and refuses to
buffer beyond it — `Consume()` returns `false` and the server drops that
connection. This is the same bounded-size discipline as the blob-size fixes
(audit #1/#4), and the deframer is continuously fuzzed (`fuzz/fuzz_deframe.cpp`,
wired into ClusterFuzzLite).

```cpp
osctap::TcpListeningReceiveSocket server( endpoint, &listener, /*maxFrameSize=*/ 4096 );
```

As always, parsing the reassembled packet still validates the OSC structure
itself (and throws on malformed input, or — on a no-exceptions build — gate it
with `TryValidatePacket()`; see the embedded guide).

## Realtime note

Reassembly buffering happens on the network thread, which is **off** the realtime
contract by design — consistent with OscTap's split between validation (may
allocate/throw, off the audio thread) and the throw-free `*Unchecked` read path.
Parsing a reassembled packet is the same allocation-free RT read path as for UDP.

## Status / caveats

- **POSIX is the runtime-verified backend** (`tests/OscTcpTest.cpp` exercises a
  real loopback client+server, including a message that spans TCP segments, and is
  clean under ASan/UBSan and TSan).
- **Windows** (`ip/win32/TcpSocket.h`) mirrors it on Winsock and is compile/link-
  verified (MinGW + the windows-latest CI legs) but not yet runtime-tested in CI.
- Deferred to a future version: SLIP framing, TLS, WebSocket transport, and an
  `epoll`/`poll` loop for very high connection counts (`select()`/`FD_SETSIZE` is
  fine for a handful of connections).

## Try it (runnable demos)

A server/monitor and a CLI client (`OSCTAP_BUILD_DEMOS`, POSIX), counterparts to
the UDP `pi5_hub` / `osc_send`:

```sh
cmake -S . -B build -DOSCTAP_BUILD_DEMOS=ON
cmake --build build --target tcp_server tcp_send

./build/tcp_server 9000 &                          # prints each received message
./build/tcp_send 127.0.0.1 9000 /fader/1 f:0.75
./build/tcp_send 127.0.0.1 9000 /chat s:hello T
```

## See also

- [`../osctap/osc/OscStreamFraming.h`](../osctap/osc/OscStreamFraming.h) — the framing codec.
- [`../osctap/ip/TcpSocket.h`](../osctap/ip/TcpSocket.h) — the public TCP types.
- [`../ROADMAP.md`](../ROADMAP.md) — where this fits in Phase 2.
