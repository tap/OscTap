# OscTap

[![CI](https://github.com/tap/OscTap/actions/workflows/ci.yml/badge.svg)](https://github.com/tap/OscTap/actions/workflows/ci.yml)

**OscTap** is the actively-maintained, security-hardened continuation of
[oscpack](http://www.rossbencina.com/code/oscpack) — Ross Bencina's C++ library for
packing and unpacking [Open Sound Control](https://opensoundcontrol.stanford.edu/)
(OSC) packets, with a minimal set of UDP networking classes for Windows and POSIX.

It is a drop-in successor: the `oscpack` namespace and include paths are retained as a
deprecated compatibility alias, so existing code keeps building while new code uses the
`osctap` name.

> **Status:** modernization in progress. The parsing path has been audited and
> hardened (see the security fixes in the history), fuzzed, and is covered by CI across
> Linux/macOS/Windows and GCC/Clang/MSVC at C++17 and C++20. See
> [`ROADMAP.md`](ROADMAP.md) for what's done and what's next.

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
| `oscpack/osc/` | OSC packet classes (parsing, printing, outbound packing, listeners) |
| `oscpack/ip/` | UDP networking; `posix/` and `win32/` backends |
| `tests/` | unit tests (incl. malformed-input regression tests) and send/receive examples |
| `examples/` | `OscDump`, `SimpleSend`, `SimpleReceive` |
| `fuzz/` | libFuzzer harness, corpus, and standalone driver — see [`fuzz/README.md`](fuzz/README.md) |

## Documentation

- [`ROADMAP.md`](ROADMAP.md) — plan of record, design decisions, sanitizer strategy.
- [`docs/HERITAGE.md`](docs/HERITAGE.md) — lineage and credits.
- [`fuzz/README.md`](fuzz/README.md) — how to fuzz the parser.
- [`README`](README) — original oscpack build notes (legacy reference).
- [`LICENSE`](LICENSE) — MIT-style license.

## Heritage & license

OscTap continues oscpack by Ross Bencina (2004–2014), via the header-only line
developed by Jean-Michaël Celerier and others. Original copyright notices are preserved
throughout the source; see [`docs/HERITAGE.md`](docs/HERITAGE.md) for full credits and
[`LICENSE`](LICENSE) for terms.
