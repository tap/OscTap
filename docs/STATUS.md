# Project Status & Handoff

A snapshot for anyone (human or a fresh agent session) continuing this work without
the original conversation. For the full plan and rationale see
[`../ROADMAP.md`](../ROADMAP.md); for lineage see [`HERITAGE.md`](HERITAGE.md).

## Where things stand

- **Phase 0 is complete** (security audit fixes, fuzzer, CI, docs, namespace rename).
  See the scorecard in `ROADMAP.md`.
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
- **`-Werror`/`/WX` is intentionally OFF.** Warnings exist (MSVC `size_t`→`uint32_t`
  narrowing, `strcpy`/`gethostbyname` deprecations, a shadow). Decision: clean up the
  warnings first, *then* turn on `-Werror` (ROADMAP Phase 1), so the matrix never goes
  red just from raising the bar.
- **Include guards are still named `INCLUDED_OSCPACK_*`** — cosmetic, left as-is.
- The test harness (`NewMessageBuffer`/`AllocateAligned4`) **intentionally leaks** its
  aligned scratch buffers, which is why the ASan job runs with
  `ASAN_OPTIONS=detect_leaks=0`. (Cleaning this up is a fine low-priority task.)

## Recommended Phase 1 starting point

1. ~~**Directory/include-path rename** `oscpack/` → `osctap/`, with a shim redirecting the
   old `<oscpack/...>` paths.~~ **Done** (redirect shim under `oscpack/`,
   `tests/CompatIncludeShim.cpp` guards it).
2. **Warning cleanup → enable `-Werror`/`/WX`** in CI.
3. **RTSan** (Clang 20+) on annotated hot paths; **TSan** concurrency test for the
   `SocketReceiveMultiplexer` (`Run()` vs `AsynchronousBreak()`).

See `ROADMAP.md` Phase 1 for the complete list, the sanitizer strategy, and rationale.

## GitHub

- Repo is **`tap/OscTap`**, detached from the `RossBencina/oscpack` fork network and
  renamed from `tap/oscpack`; old URLs redirect.
- The README CI badge tracks the **default branch**; it lights up once this work merges
  to the default branch.
- Phase 1 milestones/issues are **not yet created**. The plan (per the locked decision)
  is: `ROADMAP.md` is the source of truth, decomposed into GitHub milestones/issues for
  tracking.
