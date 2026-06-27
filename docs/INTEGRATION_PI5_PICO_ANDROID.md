# Integration tutorial: Raspberry Pi 5 ⇄ Pico 2W ⇄ Android over OSC

A worked end-to-end example wiring three nodes together with OscTap and OSC/UDP:

- **Raspberry Pi 5** — full Linux (aarch64). The **hub / router**: receives OSC,
  prints it, and relays/translates between the controller and the device.
- **Raspberry Pi Pico 2W** — RP2350 microcontroller + Wi-Fi. The **I/O device**:
  drives an LED/PWM from OSC and publishes sensor readings.
- **Android app** — the **controller / UI**: sends commands, shows telemetry.

OscTap runs the OSC (de)serialization on **all three** (header-only C++ core); each
node uses whatever UDP transport is native to it. The OSC *wire format* is the
contract, so any node could equally be a different OSC implementation.

```
        ┌────────────────────┐   /hub/led, /hub/pwm        ┌────────────────────┐
        │   Android app      │ ─────────────────────────▶  │   Raspberry Pi 5   │
        │  (controller/UI)   │                             │     hub / router   │
        │  java.net UDP +    │ ◀───────────────────────── │  pi5_hub (POSIX)   │
        │  OscTap via NDK    │   /ui/temp, /ui/name        └─────────┬──────────┘
        └────────────────────┘   (relayed telemetry)                 │
                                                       /led, /pwm     │  ▲ /sensor/temp
                                                                      ▼  │ /sensor/name
                                                            ┌────────────────────┐
                                                            │  Pico 2W (RP2350)  │
                                                            │  lwIP UDP +        │
                                                            │  OscTap freestyle  │
                                                            └────────────────────┘
```

> What's verified in this repo: the Pi 5 hub + sender demos build and run
> (loopback), the aarch64 build runs green under qemu (CI `aarch64-qemu` job), and
> the Android JNI bridge compiles against `jni.h` + the OscTap core. The Pico
> firmware and the full Android NDK/Gradle build are integration recipes — they
> need the Pico SDK / Android NDK on your machine.

## 1. The OSC contract (address map)

Agree this up front; it's the only thing the three nodes truly share.

| Direction | Address | Args | Meaning |
|-----------|---------|------|---------|
| Android → Pi 5 | `/hub/led` | `int` (0/1) | request LED state |
| Android → Pi 5 | `/hub/pwm` | `float` (0..1) | request PWM duty |
| Pi 5 → Pico | `/led` | `int` | hub relays the LED command |
| Pi 5 → Pico | `/pwm` | `float` | hub relays the PWM command |
| Pico → Pi 5 | `/sensor/temp` | `float` (°C) | temperature reading |
| Pico → Pi 5 | `/sensor/name` | `string` | device label |
| Pi 5 → Android | `/ui/temp` | `float` | relayed telemetry |
| Pi 5 → Android | `/ui/name` | `string` | relayed telemetry |

The hub's rule is mechanical: `/hub/<x>` → `/<x>` to the Pico; `/sensor/<x>` →
`/ui/<x>` to the Android. See `demos/pi5_hub.cpp`.

## 2. Addresses & ports

Put all three on the same LAN/subnet. Example:

| Node | IP (example) | Listens on | Sends to |
|------|--------------|-----------|----------|
| Pi 5 hub | `192.168.1.10` | `9000` | Pico `:9000`, Android `:9001` |
| Pico 2W | `192.168.1.50` | `9000` | Pi 5 `:9000` |
| Android | `192.168.1.20` | `9001` | Pi 5 `:9000` |

Substitute your real IPs (find them with `hostname -I` on the Pi, your router's
DHCP table for the Pico, and Wi-Fi settings on the phone).

## 3. Node 1 — Raspberry Pi 5 (the hub)

Build the demos (POSIX sockets; identical source on aarch64 and x86-64):

```sh
cmake -S . -B build -DOSCTAP_BUILD_DEMOS=ON
cmake --build build --target pi5_hub osc_send
```

Run the hub, telling it where the Pico and Android are:

```sh
./build/pi5_hub 9000 192.168.1.50:9000 192.168.1.20:9001
# OscTap Pi 5 hub listening on UDP 9000 (Ctrl-C to stop)
```

It prints every message it receives and logs each relay. `osc_send` is a CLI for
poking the system before the real devices exist:

```sh
./build/osc_send 192.168.1.10 9000 /hub/led i:1
./build/osc_send 192.168.1.10 9000 /hub/pwm f:0.75
./build/osc_send 192.168.1.10 9000 /sensor/temp f:21.4
```

The hub guards itself against malformed UDP: parsing throws, and it catches the
exception to drop the bad datagram rather than crash (see `HubListener::ProcessPacket`).

## 4. Node 2 — Raspberry Pi Pico 2W (the device)

Full embedded setup is in [`EMBEDDED_PICO2W.md`](EMBEDDED_PICO2W.md) (freestanding
profile, Pico SDK + lwIP, the exceptions/untrusted-input trade-off). For *this*
contract, the Pico does two things.

**Receive `/led` and `/pwm`** in the lwIP `udp_recv` callback:

```cpp
#include "osc/OscReceivedElements.h"

static void on_osc(void*, udp_pcb*, pbuf* p, const ip_addr_t*, u16_t) {
    if (!p) return;
    alignas(4) static char buf[256];
    const u16_t n = pbuf_copy_partial(p, buf, sizeof(buf), 0);
    pbuf_free(p);

    osctap::ReceivedMessage m(osctap::ReceivedPacket(buf, n));   // (try/catch if exceptions on)
    const char* a = m.AddressPattern();
    auto arg = m.ArgumentsBegin();
    if      (std::strcmp(a, "/led") == 0) set_led(arg->AsInt32Unchecked() != 0);
    else if (std::strcmp(a, "/pwm") == 0) set_pwm(arg->AsFloatUnchecked());
}
```

**Publish `/sensor/temp`** periodically into a `pbuf` and `udp_sendto` the Pi 5
(buffer on the stack, no heap):

```cpp
#include "osc/OscOutboundPacketStream.h"

void publish_temp(udp_pcb* pcb, const ip_addr_t* hub, uint16_t port, float c) {
    char buffer[64];
    osctap::OutboundPacketStream p(buffer, sizeof(buffer));
    p << osctap::BeginMessage("/sensor/temp") << c << osctap::EndMessage();
    pbuf* pb = pbuf_alloc(PBUF_TRANSPORT, p.Size(), PBUF_RAM);
    std::memcpy(pb->payload, p.Data(), p.Size());
    udp_sendto(pcb, pb, hub, port);
    pbuf_free(pb);
}
```

> Untrusted-input reminder: on a Wi-Fi LAN, prefer **exceptions enabled** on the
> Pico so a malformed packet is caught and dropped, not fatal. On a trusted/closed
> link you can run the no-exceptions freestanding profile. Full rationale in the
> Pico guide.

## 5. Node 3 — Android (the controller)

Two ways to speak OSC from the app. Pick one.

### 5a. OscTap via the NDK (native core)

Reuse the exact OscTap C++ core on Android through a thin JNI bridge. Files in
[`../android/`](../android): `osctap_jni.cpp` (bridge), `CMakeLists.txt` (NDK
build), `OscTap.kt` (Kotlin facade + JVM UDP transport).

Wire it into your app module's `build.gradle`:

```gradle
android {
    defaultConfig {
        externalNativeBuild { cmake { cppFlags "-std=c++17 -fexceptions" } }
        ndk { abiFilters "arm64-v8a", "armeabi-v7a", "x86_64" }
    }
    externalNativeBuild {
        cmake { path "../android/CMakeLists.txt" ; version "3.22.1" }
    }
}
```

The CMake passes `OSCTAP_ROOT` to the header-only core (override if the repo
lives elsewhere). Or build a single ABI by hand to sanity-check the toolchain:

```sh
cmake -B build-android android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
  -DOSCTAP_ROOT=$PWD
cmake --build build-android      # -> libosctap_jni.so
```

Then in Kotlin (`OscTap.kt`):

```kotlin
val hub = "192.168.1.10"
val tx = OscUdp()
tx.send(hub, 9000, "/hub/led", 1)        // Int  -> OSC int
tx.send(hub, 9000, "/hub/pwm", 0.75f)    // Float -> OSC float

val rx = OscUdp(DatagramSocket(9001))    // telemetry relayed by the hub
thread { while (true) rx.receive()?.let { updateUi(it) } }   // off the main thread
```

Add the permission to `AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.INTERNET"/>
```

The bridge builds/parses OSC natively; `java.net.DatagramSocket` carries the
bytes (idiomatic Android networking). Parsing throws on malformed input and the
bridge re-throws it as a Kotlin `IllegalArgumentException`, which `receive()`
catches to drop the datagram.

### 5b. Simpler: a JVM OSC library (no native code)

If you don't want an NDK toolchain in your build, the app can use a pure-JVM OSC
library (e.g. **JavaOSC / illposed**) and talk to the same hub — the wire format
is identical, so OscTap on the Pi/Pico interoperates with it transparently:

```kotlin
// implementation("com.illposed.osc:javaosc-core:0.8")
val sender = OSCPortOut(InetAddress.getByName("192.168.1.10"), 9000)
sender.send(OSCMessage("/hub/led", listOf(1)))
```

Trade-off: 5a dogfoods OscTap end-to-end and shares one code path across all
nodes; 5b is less setup for app developers. Both are wire-compatible.

## 6. Bring-up order (test each leg in isolation)

1. **Hub alone**: run `pi5_hub`, then `osc_send … /hub/led i:1` from the same Pi.
   You should see the recv line and a `-> Pico` relay line.
2. **Add a fake Pico**: run another `osc_send … /sensor/temp f:20` and watch the
   `-> Android` relay. Point the hub's Android target at a host running
   `nc -ul 9001` (or another listener) to confirm bytes arrive.
3. **Real Pico**: flash the firmware; confirm `/sensor/*` shows up at the hub and
   `/led`/`/pwm` actuate.
4. **Real Android**: send from the app; watch the hub log; confirm telemetry
   reaches the phone.

## 7. Troubleshooting

- **Nothing arrives**: same subnet? Host firewall (`sudo ufw allow 9000/udp` on
  the Pi)? Phone on Wi-Fi, not cellular? Bind the receiver to `ANY_ADDRESS`
  (the hub does) so it accepts on all interfaces, not just loopback.
- **Garbled values**: OSC is big-endian; OscTap handles byte order on every node,
  so garbling almost always means an **address/type mismatch** vs. the table in §1
  (e.g. sending an int where a float is expected). The hub prints the decoded type
  per arg — compare against the contract.
- **String shows as `true`/garbage**: you're on an OscTap build predating the
  `const char*` overload fix — pass `osctap::string_view`/`std::string`, or update.
- **App ANR / NetworkOnMainThreadException**: do all socket I/O off the main
  thread (a coroutine or `thread { }`).
- **Pico resets on bad input**: that's the no-exceptions fatal handler — switch to
  exceptions-enabled on the Pico for untrusted LANs (Pico guide, §"Handling
  validation when exceptions are off").

## See also

- [`EMBEDDED_PICO2W.md`](EMBEDDED_PICO2W.md) — the Pico 2W deep dive.
- [`../demos/pi5_hub.cpp`](../demos/pi5_hub.cpp), [`../demos/osc_send.cpp`](../demos/osc_send.cpp) — the runnable Pi 5 side.
- [`../android/`](../android) — the JNI bridge, NDK CMake, and Kotlin facade.
- [`../ROADMAP.md`](../ROADMAP.md) — where this fits in Phase 2 ("Reach").
