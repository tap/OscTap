# Project Status & Handoff

A snapshot for anyone (human or a fresh agent session) continuing this work without
the original conversation. For the full plan and rationale see
[`../ROADMAP.md`](../ROADMAP.md); for lineage see [`HERITAGE.md`](HERITAGE.md).

## Where things stand

- **Phase 0 is complete** (security audit fixes, fuzzer, CI, docs, namespace rename).
  See the scorecard in `ROADMAP.md`.
- **Phase 1 is complete** (directory rename + shim, ClusterFuzzLite, `bit_cast`/
  `constexpr` parsing, warnings-as-errors, RTSan, TSan).
- **Phase 2 ("Reach") is underway**: landed so far —
  - the freestanding/embedded profile (the `OSCTAP_THROW` seam in `osc/OscConfig.h`,
    `OSCTAP_FREESTANDING` + the `freestanding` CI job) and the Pico 2W guide
    ([`EMBEDDED_PICO2W.md`](EMBEDDED_PICO2W.md));
  - **aarch64 / Raspberry Pi 5 CI** under `qemu-user` (the `aarch64-qemu` job);
  - the **Pi 5 ⇄ Pico 2W ⇄ Android integration**: runnable Pi 5 demos
    (`demos/`, `OSCTAP_BUILD_DEMOS`), an Android JNI bridge (`android/`), and the
    tutorial ([`INTEGRATION_PI5_PICO_ANDROID.md`](INTEGRATION_PI5_PICO_ANDROID.md)).
  Remaining Reach items (multicast, armv7, a full Android sample app) stay demand-gated.
- All six audit findings are fixed with regression tests; see commit history and
  `tests/OscUnitTests.cpp` (`test4`/`test5`).
- **CI is the source of truth for build health.** `.github/workflows/ci.yml` builds and
  tests across Linux/macOS/Windows × GCC/Clang/MSVC × C++17 and C++20, plus an
  ASan/UBSan job and a standalone-fuzzer smoke job.

## Build / test / fuzz (quick reference)

```sh
# build + test
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
# pick the standard
cmake -S . -B build -DCMAKE_CXX_STANDARD=20

# sanitizers (unit tests)
cmake -S . -B build-san -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-san && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-san --output-on-failure

# fuzzing — real libFuzzer (Clang)
cmake -S . -B build-fuzz -DOSCTAP_BUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build-fuzz --target fuzz_parse && ./build-fuzz/fuzz_parse fuzz/corpus
# fuzzing — standalone driver (g++, no libFuzzer runtime needed)
cmake -S . -B build-fuzz -DOSCTAP_FUZZER_STANDALONE=ON
cmake --build build-fuzz --target fuzz_parse && ./build-fuzz/fuzz_parse fuzz/corpus/*

# freestanding / embedded profile (exceptions + RTTI off)
cmake -S . -B build-fs -DOSCPACK_BUILD_EXAMPLES=OFF -DOSCTAP_FREESTANDING=ON
cmake --build build-fs --target OscFreestandingTest && ./build-fs/OscFreestandingTest
```

## Landmines — read before changing things

- **The C++ namespace is `osctap`.** `oscpack` is a compatibility alias
  (`namespace oscpack = osctap;`) declared in every public header. **Do not remove it.**
- **`tests/` and `examples/` intentionally still use the `oscpack::` alias.** They are the
  live verification that the compatibility shim works. **Do not "modernize" them to
  `osctap::`** — doing so silently removes the only coverage of the alias.
  `tests/CompatIncludeShim.cpp` is the dedicated, CI-built guard for both the namespace
  alias and the include-path shim; **do not migrate it to `<osctap/...>`/`osctap::`.**
- **The on-disk directory is now `osctap/`** (public prefix `<osctap/...>`). The old
  `<oscpack/...>` paths still work via a redirect shim tree under `oscpack/` — every header
  there just `#include`s its `<osctap/...>` counterpart. **Do not delete the `oscpack/`
  shim tree**; it is the include-path half of the compatibility moat. In-tree headers use
  quoted relative includes (`"osc/..."`, `"ip/..."`) that resolve via `include_directories(osctap)`.
- **`-Werror`/`/WX` is now ON in CI** via the `OSCTAP_WARNINGS_AS_ERRORS` CMake option
  (default **OFF** so downstream consumers of the INTERFACE library are never forced onto
  our warning bar; `ci.yml` passes `-DOSCTAP_WARNINGS_AS_ERRORS=ON`). The MSVC `/W4` set
  that this cleared — `size_t`→`uint32_t` narrowing, `strcpy`/`gethostbyname`/`ctime`
  deprecations, a shadow, and `(char)0xFF` constant truncation — was read straight from the
  Windows CI logs, since those warnings don't appear under GCC/Clang `-Wall -Wextra`
  (approximate them locally with `clang++ -Wshorten-64-to-32 -Wshadow`). The compiled CI
  surface is the gate: the uncompiled `ip/*/UdpSocket.h` backends still use `strcpy`/
  `gethostbyname` and aren't yet covered — clean them when they enter the build.
- **`OSCTAP_REALTIME` marks the realtime hot path** (`OscTypes.h`). It is
  `noexcept [[clang::nonblocking]]` on Clang ≥ 20 and a **no-op everywhere else**, so it
  must stay applied only to genuinely allocation-/throw-free functions — the read/iterate
  path over a *known-valid* message. **Do not annotate anything that can throw or allocate**
  (message construction/`Init()`, checked accessors, `AsBoolUnchecked`/`AsBlobUnchecked`,
  serialization): the Clang-20 RTSan job (`-DOSCTAP_RTSAN=ON`) will fail it both at runtime
  (`-fsanitize=realtime`) and statically (`-Wfunction-effects -Werror`).
  `tests/OscRealtimeTest.cpp` is the guard and also runs as a plain functional test on the
  rest of the matrix. Local RTSan needs Clang ≥ 20 (`apt-get install clang-20 libclang-rt-20-dev`).
- **All int/float (de)serialization goes through `OscUtilities.h`** —
  `LoadBigEndian*`/`StoreBigEndian*` (endian-agnostic byte assembly) + `BitCast`
  (`std::bit_cast` on C++20, `memcpy` on C++17). **Do not reintroduce union type-punning,
  `reinterpret_cast<T*>` over the byte buffer, or `#ifdef OSC_HOST_*_ENDIAN`** — that was
  the audit-#6 UB, and UBSan guards against it. The byte helpers are `constexpr`; keep the
  RT read accessors routed through them (`memcpy` is RTSan/function-effects-safe).
- **`OSCTAP_THROW` is the only way the core raises** (`osc/OscConfig.h`). Every
  `throw` in `OscReceivedElements.h`/`OscOutboundPacketStream.h` goes through it so
  the library compiles under `-fno-exceptions`. **Do not reintroduce a bare `throw`
  in the core** — it breaks the `freestanding` CI job (a bare `throw` is a hard
  error under `-fno-exceptions`). With exceptions on, `OSCTAP_THROW(X)` *is* `throw X`,
  so hosted behaviour (and the `test4`/`test5` malformed-input asserts) is unchanged.
  Under `-fno-exceptions` it calls a non-returning fatal handler (default
  `std::abort()`, overridable via `OSCTAP_FATAL_HANDLER`).
- **`OSCTAP_FREESTANDING` drops hosted-only facilities** — `<iostream>`, the
  `std::vector`-backed `OwnedMessage`, and the `std::string` `operator<<`. If you add
  a new core feature that needs `<iostream>`/`<vector>`/`std::string`, guard it with
  `#ifndef OSCTAP_FREESTANDING` (and keep `tests/OscFreestandingTest.cpp` compiling),
  or it will break the `freestanding` job. The freestanding flags are **PRIVATE to
  the `OscFreestandingTest` target**, so the exception-based tests are unaffected.
  Embedded posture and the Pico 2W recipe live in `docs/EMBEDDED_PICO2W.md`.
- **Untrusted input on a no-exceptions build: gate it with `TryValidatePacket()`.**
  The parser validates by throwing, and with exceptions off `OSCTAP_THROW` aborts —
  so on a freestanding/no-exceptions build, call `osctap::TryValidatePacket(data, size)`
  (returns `nullptr` when fully well-formed, else a static error string) before
  constructing/reading; it recurses through bundles with a nesting bound and never
  throws/allocates. `ReceivedMessage::TryInit`/`ReceivedBundle::TryInit` are the
  per-element non-throwing parses. **These are the single source of truth** — the
  throwing `Init()`/constructors delegate to them (`Init` = `TryInit` + `OSCTAP_THROW`
  on the returned error), so **don't fork the validation logic**: edit `TryInit` and
  both paths stay in lock-step. `OscValidateTest` is the differential guard (gate vs.
  throwing path) and must stay green. Note `ReceivedMessage::size_` is no longer
  `const` (TryInit assigns it) and both classes gained a default ctor for the
  default-construct-then-`TryInit` pattern.
- **A runtime `const char*` now serializes as an OSC string**, via a dedicated
  `OutboundPacketStream::operator<<(const char*)`. Before, a `const char*` bound to
  `operator<<(bool)` (standard pointer→bool conversion outranks the user-defined
  `string_view` conversion) and was silently sent as a boolean — only string
  *literals* (the `const char(&)[N]` overload) worked. The fix is freestanding-safe
  (forwards to `string_view`); `OscFreestandingTest` sends a runtime `const char*`
  to guard it. **Don't remove it**, and don't assume `<< somePtr` ever meant bool.
- **The `ip/` networking layer now has compiled coverage** via the `demos/`
  (`OSCTAP_BUILD_DEMOS`, POSIX-only) and the `aarch64-qemu` CI job. Previously the
  POSIX/win32 `UdpSocket`/`NetworkingUtils` backends were header-only-but-uncompiled.
  The win32 backend and the deferred `strcpy`/`gethostbyname` cleanup (#4) are still
  not in the compiled surface — fold them in with multicast (#19).
- **`OSCTAP_BUILD_DEMOS` and the `aarch64-qemu` job**: demos are POSIX-only
  (`ip/posix` + a SIGINT handler) and gated `NOT WIN32`. The aarch64 job runs the
  suite under `qemu-user` but **excludes `OscConcurrencyTest`** (emulated threads +
  loopback sockets are flaky); keep new socket/thread tests off the emulated run or
  they'll flake CI.
- **Android lives in `android/`** (`osctap_jni.cpp` bridge, NDK `CMakeLists.txt`,
  `OscTap.kt`). The bridge is compile-checked against `jni.h` in CI-adjacent dev but
  there is **no NDK build in CI** yet — changes there aren't gated, so keep the
  bridge's OscTap API usage in sync by hand (or add an NDK job when one's warranted).
- **Include guards are named `INCLUDED_OSCTAP_*`** (renamed from `INCLUDED_OSCPACK_*`
  in Phase 2 cleanup). The `<oscpack/...>` *include paths* still work via the redirect
  shim tree — only the internal guard macro names changed.
- **`osc/SmallString.h` is an intentionally-empty, guarded no-op** (audit #6 closed).
  The outbound stream no longer includes it; it's kept only so the public path and its
  `<oscpack/...>` shim keep resolving. Don't "fill it in" without a real need.
- The test harness (`NewMessageBuffer`/`AllocateAligned4`) **intentionally leaks** its
  aligned scratch buffers, which is why the ASan job runs with
  `ASAN_OPTIONS=detect_leaks=0`. (Cleaning this up is a fine low-priority task.)
- **`OscConcurrencyTest` must signal `AsynchronousBreak()` in a loop, not once.**
  `Run()` resets its break flag at entry, so a single pre-emptive break can be lost and
  the receive loop would block forever. The loopback packet it sends is **best-effort**
  (receipt is not asserted — sandboxed CI may not deliver UDP). It's POSIX-only and built
  under `-fsanitize=thread` via `-DOSCTAP_TSAN=ON` (GCC ships `libtsan`; the distro Clang
  may lack the TSan runtime, so the CI job uses GCC).
- **ClusterFuzzLite builds the fuzzer via `.clusterfuzzlite/build.sh`, not CMake** —
  it compiles `fuzz/fuzz_parse.cpp` directly with the OSS-Fuzz toolchain's
  `$CXXFLAGS`/`$LIB_FUZZING_ENGINE` and `-I osctap`. The `cflite_*` workflows build an
  OSS-Fuzz Docker image, so they're slower than the rest of CI. Local fuzzing still goes
  through CMake (`OSCTAP_BUILD_FUZZERS` / `OSCTAP_FUZZER_STANDALONE`).

## Recommended Phase 1 starting point

1. ~~**Directory/include-path rename** `oscpack/` → `osctap/`, with a shim redirecting the
   old `<oscpack/...>` paths.~~ **Done** (redirect shim under `oscpack/`,
   `tests/CompatIncludeShim.cpp` guards it).
2. ~~**Warning cleanup → enable `-Werror`/`/WX`** in CI.~~ **Done**
   (`OSCTAP_WARNINGS_AS_ERRORS` option, on in CI).
3. ~~**RTSan** (Clang 20+) on the annotated read hot path.~~ **Done** (`OSCTAP_REALTIME`,
   `OscRealtimeTest` + the `rtsan` CI job).
4. ~~**TSan** concurrency test for the `SocketReceiveMultiplexer` (`Run()` vs
   `AsynchronousBreak()`).~~ **Done** (`OscConcurrencyTest` + the GCC `tsan` CI job).

See `ROADMAP.md` Phase 1 for the complete list, the sanitizer strategy, and rationale.

## GitHub

- Repo is **`tap/OscTap`**, detached from the `RossBencina/oscpack` fork network and
  renamed from `tap/oscpack`; old URLs redirect.
- The README CI badge tracks the **default branch**; it lights up once this work merges
  to the default branch.
- Phase 1 milestones/issues are **not yet created**. The plan (per the locked decision)
  is: `ROADMAP.md` is the source of truth, decomposed into GitHub milestones/issues for
  tracking.
