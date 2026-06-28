# OscTap API reference

A curated reference of the public surface, grouped by header. Everything lives in
namespace `osctap` (the `oscpack` alias is retained, deprecated). This is a
hand-written overview — the headers themselves are the authoritative source.

New here? Start with [Getting Started](GETTING_STARTED.md).

Conventions:
- **(RT)** = realtime-safe (allocation-/exception-free; annotated `OSCTAP_REALTIME`).
- **(throws)** = validates and may throw on bad input / state.

---

## Building OSC — `osc/OscOutboundPacketStream.h`

### `OutboundPacketStream`
Serializes OSC into a caller-owned buffer (no heap).

| Member | Notes |
|--------|-------|
| `OutboundPacketStream(char* buf, size_t capacity)` | wrap a buffer |
| `const char* Data() const` · `size_t Size() const` | the framed bytes so far |
| `size_t Capacity() const` | buffer size |
| `void Clear()` | reset to empty |
| `bool IsReady() const` | all messages/bundles closed |
| `bool IsMessageInProgress() const` · `bool IsBundleInProgress() const` | |
| `operator<<( T )` | append a value or manipulator (below) |

Streamed value types: `bool` (→`T`/`F`), `int32_t`, `int64_t`, `float`, `double`,
`char`, `const char*` / `std::string` / `string_view` (→ string), `Blob`,
`Symbol`, `TimeTag`, `MidiMessage`, `RgbaColor`. Overflowing the buffer throws
`OutOfBufferMemoryException`.

### Manipulators & types — `osc/OscTypes.h`
`BeginMessage(addr)` · `EndMessage()` · `BeginBundle(timeTag=1)` ·
`BeginBundleImmediate()` · `EndBundle()` · `BeginArray()` · `EndArray()` ·
`OscNil()` · `Infinitum()`. Value wrappers: `Blob(ptr, size)`, `Symbol(s)`,
`TimeTag(v)`, `MidiMessage(v)`, `RgbaColor(v)`.

### Exceptions
`OutOfBufferMemoryException`, `MessageInProgressException`,
`MessageNotInProgressException`, `BundleNotInProgressException` — all derive from
`osctap::Exception`.

---

## Parsing OSC — `osc/OscReceivedElements.h`

### `ReceivedPacket`
| Member | Notes |
|--------|-------|
| `ReceivedPacket(const char* data, size_t size)` | (throws) validates size |
| `bool IsBundle() const` · `bool IsMessage() const` | |
| `const char* Contents() const` · `osc_bundle_element_size_t Size() const` | |
| `static const char* ValidateSizeNoThrow(size)` | non-throwing size check (`nullptr` = ok) |

### `ReceivedMessage`
| Member | Notes |
|--------|-------|
| `ReceivedMessage(const ReceivedPacket&)` / `(const ReceivedBundleElement&)` | (throws) |
| `ReceivedMessage()` + `const char* TryInit(data, size)` | **non-throwing** parse (`nullptr` = ok) |
| `static const char* Validate(data, size)` | non-throwing structural check |
| `const char* AddressPattern() const` | (RT) |
| `uint32_t ArgumentCount() const` · `const char* TypeTags() const` | (RT) |
| `const_iterator ArgumentsBegin() / ArgumentsEnd() const` | (RT) |
| `ReceivedMessageArgumentStream ArgumentStream() const` | for `>>` reads |

### `ReceivedMessageArgument`
Type tests (all `bool`, RT): `IsBool IsInt32 IsInt64 IsFloat IsDouble IsChar
IsString IsSymbol IsBlob IsRgbaColor IsMidiMessage IsTimeTag IsNil IsInfinitum
IsArrayBegin IsArrayEnd`.

Accessors — checked (throw `WrongArgumentTypeException`) and `*Unchecked` (RT):
`AsBool AsInt32 AsInt64 AsFloat AsDouble AsChar AsString AsSymbol AsRgbaColor
AsMidiMessage AsTimeTag` (+ `…Unchecked`). Blobs:
`void AsBlob(const void*& data, osc_bundle_element_size_t& size)` (+ `Unchecked`).

`char TypeTag() const` (RT) returns the raw tag. Iterate with
`ReceivedMessageArgumentIterator`, or read positionally with
`ReceivedMessageArgumentStream`: `args >> b >> i >> f >> endTag;` (where `endTag`
is a `MessageTerminator` lvalue; an over-read throws `ExcessArgumentException`).

### `ReceivedBundle`
`TimeTag()`, `ElementCount()`, `ElementsBegin()/ElementsEnd()`, plus the same
non-throwing `TryInit` / `static Validate` pair as `ReceivedMessage`. Iterate with
`ReceivedBundleElementIterator`; each element is a `ReceivedBundleElement` you
construct a `ReceivedMessage`/`ReceivedBundle` from.

### Non-throwing whole-packet gate
```cpp
const char* TryValidatePacket(const char* data, osc_bundle_element_size_t size,
                              unsigned int maxBundleNestingDepth = 64);
```
Returns `nullptr` iff the packet (message, or bundle whose elements are all
well-formed, recursively) is safe to construct **and read in full** without any
throw/abort. The gate to use for untrusted input on a no-exceptions build.

### Exceptions
`MalformedPacketException`, `MalformedMessageException`, `MalformedBundleException`,
`WrongArgumentTypeException`, `MissingArgumentException`, `ExcessArgumentException`.

---

## Dispatch — `osc/OscPacketListener.h`
`OscPacketListener : public PacketListener` — unpacks bundles (depth-bounded) and
calls your `ProcessMessage`.
- `virtual void ProcessMessage(const ReceivedMessage&, const IpEndpointName&)` — override this.
- `virtual void ProcessBundle(...)` — override to handle time tags.
- `void SetMaxBundleNestingDepth(unsigned)` (default 64).

## Pretty-printing — `osc/OscPrintReceivedElements.h`
`std::ostream& operator<<(std::ostream&, const ReceivedPacket& | ReceivedMessage&
| ReceivedBundle& | ReceivedMessageArgument&)` — human-readable dump (see `OscDump`).

---

## Stream framing (OSC over TCP) — `osc/OscStreamFraming.h`
- `void WriteOscStreamFrameHeader(char header[4], uint32_t packetSize)` — length prefix.
- `size_t FrameOscPacket(const char* pkt, uint32_t size, char* out, size_t cap)` — frame into one buffer (`0` if it doesn't fit).
- `class OscStreamDeframer` — `explicit OscStreamDeframer(uint32_t maxFrameSize = 65536)`; `bool Consume(const char* data, size_t size, Sink&& sink)` (calls `sink(packet, size)` per complete packet; returns `false` on an over-cap frame); `void Reset()`; `uint32_t MaxFrameSize() const`. See [OSC over TCP](OSC_OVER_TCP.md).

---

## Networking — `ip/`

### `IpEndpointName` (`ip/IpEndpointName.h`)
Ctors: `(const char* host, int port)`, `(uint32_t ip, int port)`,
`(a,b,c,d, port)`, `(int port)`. Constants `ANY_ADDRESS`, `ANY_PORT`. Helpers
`AddressAsString(char*)`, `AddressAndPortAsString(char*)`, `IsMulticastAddress()`.

### `PacketListener` (`ip/PacketListener.h`)
Abstract base: `virtual void ProcessPacket(const char* data, int size, const IpEndpointName& from)`.

### UDP (`ip/UdpSocket.h`)
- `UdpTransmitSocket(const IpEndpointName& remote)` — `Send(data, size)`, `SendTo(to, data, size)`.
- `UdpReceiveSocket`, and `UdpListeningReceiveSocket(local, PacketListener*)` —
  `Run()` (blocks), `Break()`, `AsynchronousBreak()`, `int LocalPort()`.
- `SocketReceiveMultiplexer` — multiple sockets/timers in one `Run()` loop
  (`AttachSocketListener`, `AttachPeriodicTimerListener`, …).

### TCP (`ip/TcpSocket.h`)
- `TcpTransmitSocket(const IpEndpointName& remote)` — `Send(data, size)` (length-prefixed; `TCP_NODELAY` on).
- `TcpListeningReceiveSocket(local, PacketListener*, uint32_t maxFrameSize = 65536)` —
  `Run()`, `Break()`, `AsynchronousBreak()`, `IpEndpointName LocalEndpointFor(requested)`.
  Accepts multiple clients, each deframed independently.

---

## Build-configuration seam — `osc/OscConfig.h`
- `OSCTAP_HAS_EXCEPTIONS` — auto-detected (0 under `-fno-exceptions`).
- `OSCTAP_FREESTANDING` — define to drop hosted-only facilities (`<iostream>`,
  `std::vector` `OwnedMessage`, `std::string operator<<`).
- `OSCTAP_THROW(EXC)` — `throw` when exceptions are on; otherwise a non-returning
  fatal handler.
- `OSCTAP_FATAL_HANDLER(whatCStr)` — pre-define to route the no-exceptions failure
  path (default `std::abort()`). See [Embedded (Pico 2W)](EMBEDDED_PICO2W.md).

## Base exception — `osc/OscException.h`
`class Exception : public std::exception` — base of every OscTap exception;
`const char* what() const noexcept`.
