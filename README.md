# OscTap

[![CI](https://github.com/tap/OscTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/OscTap/actions/workflows/ci.yml)

**OscTap** is the actively-maintained, security-hardened continuation of
[oscpack](http://www.rossbencina.com/code/oscpack) — Ross Bencina's C++ library for
packing and unpacking [Open Sound Control](https://opensoundcontrol.stanford.edu/)
(OSC) packets, with UDP **and TCP** networking classes for Windows and POSIX.

It is a drop-in successor: the `oscpack` namespace and include paths are retained as a
deprecated compatibility alias, so existing code keeps building while new code uses the
`osctap` name.

> **Status:** actively modernized. The parsing path is audited, hardened, and fuzzed;
> a non-throwing validation gate, a freestanding/embedded profile, OSC-over-TCP, and an
> aarch64 (Raspberry Pi 5) target have landed. CI spans Linux/macOS/Windows ×
> GCC/Clang/MSVC at C++17 and C++20, plus ASan/UBSan, RTSan, TSan, fuzzing, aarch64
> (QEMU), Windows-runtime (Wine), and ~85% coverage. See [`ROADMAP.md`](ROADMAP.md).

## What it is

OscTap is a set of C++ classes for constructing, sending, receiving, and parsing OSC
packets. It is **not** an application framework — there's no namespace routing or
service infrastructure, just the packet machinery. The networking classes are enough to
write many OSC applications, but you can pair the packet classes with any transport.

Design goals (inherited from oscpack, and the reason it's worth continuing):

- A simple and complete implementation of OSC 1.0.
- Portable across a wide range of platforms.
- **Robust against malformed input** — it should be impossible to crash a receiver by
  sending it a hostile packet. (This is the focus of the current hardening work.)

## Quick start

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Override the language standard with `-DCMAKE_CXX_STANDARD=20` (C++17 is the floor).
The library itself is header-only; the build targets are the tests and examples.

## Layout

| Path | Contents |
|------|----------|
| `osctap/osc/` | OSC packet classes (parsing, printing, outbound packing, listeners, stream framing) |
| `osctap/ip/` | UDP + TCP networking; `posix/` and `win32/` backends |
| `oscpack/` | redirect shim — old `<oscpack/...>` include paths forwarding to `<osctap/...>` (deprecated compatibility) |
| `tests/` | unit tests (malformed-input regression, validation, framing, UDP/TCP loopback), the compat-shim guard |
| `demos/` | runnable OSC programs (UDP hub + sender, TCP server + sender) |
| `examples/` | the canonical oscpack examples — `OscDump`, `SimpleSend`, `SimpleReceive` |
| `android/` | Android NDK JNI bridge + Kotlin facade |
| `fuzz/` | libFuzzer harnesses (parser + TCP deframer), corpora, standalone driver — see [`fuzz/README.md`](fuzz/README.md) |
| `docs/` | guides (below); `docs/legacy/` keeps the original oscpack README/CHANGES/TODO |

## Documentation

- [`docs/GETTING_STARTED.md`](docs/GETTING_STARTED.md) — **start here**: build, send, receive, parse (OSC over UDP).
- [`docs/API.md`](docs/API.md) — the public API, grouped by header.
- [`docs/OSC_OVER_TCP.md`](docs/OSC_OVER_TCP.md) — reliable/stream transport.
- [`docs/EMBEDDED_PICO2W.md`](docs/EMBEDDED_PICO2W.md) — no-heap / no-exceptions builds (Raspberry Pi Pico 2W).
- [`docs/INTEGRATION_PI5_PICO_ANDROID.md`](docs/INTEGRATION_PI5_PICO_ANDROID.md) — a worked Pi 5 ⇄ Pico 2W ⇄ Android system.
- [`ROADMAP.md`](ROADMAP.md) — plan of record, design decisions, sanitizer strategy.
- [`docs/HERITAGE.md`](docs/HERITAGE.md) — lineage and credits.
- [`fuzz/README.md`](fuzz/README.md) — how to fuzz the parser and deframer.
- [`docs/legacy/`](docs/legacy/) — original oscpack README / CHANGES / TODO (historical).
- [`LICENSE`](LICENSE) — MIT-style license.

## Heritage & license

OscTap continues oscpack by Ross Bencina (2004–2014), via the header-only line
developed by Jean-Michaël Celerier and others. Original copyright notices are preserved
throughout the source; see [`docs/HERITAGE.md`](docs/HERITAGE.md) for full credits and
[`LICENSE`](LICENSE) for terms.
