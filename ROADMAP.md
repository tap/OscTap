# OscTap Roadmap

OscTap is the actively-maintained, security-hardened, modern-C++ continuation of
**oscpack**, Ross Bencina's long-standing Open Sound Control (OSC) library. This
document is the source of truth for the rebrand and the work plan. See
[`docs/HERITAGE.md`](docs/HERITAGE.md) for lineage and credits.

> Status: Phase 0 and Phase 1 complete. Phase 2 ("Reach") prep in progress — the
> freestanding/embedded profile groundwork has landed (see Phase 2 below); the
> remaining Reach items stay demand-gated.

## Why OscTap exists

oscpack is stable and battle-tested but effectively frozen upstream (last release
v1.1.0 in April 2013; last commit September 2014; 13 open issues / 6 open PRs
unmerged). The OSC 1.0 protocol itself is frozen, so the codebase is an asset, not
a liability — what it lacks is an active maintainer, a security posture, modern C++,
documentation, and CI. OscTap supplies those while preserving drop-in compatibility
with the enormous existing oscpack install base.

### What OscTap competes on (the moat)

1. **A credible, active maintainer** in the OSC/audio community.
2. **A security/safety posture** — continuous fuzzing, memory-safety, sanitizer CI.
3. **Drop-in migration** — existing `oscpack` include paths and namespace remain
   available as a deprecated compatibility alias. This compatibility is the moat;
   protect it deliberately.
4. **Documentation + curated heritage** — something oscpack never had.

Non-goal: "winning a market." OSC is niche and frozen. The realistic, worthy goal is
to be the obvious library people are pointed to when they ask "is oscpack maintained?"

## Key decisions (locked)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Name | **OscTap**, C++ namespace `osctap` | maintainer identity; `oscpack` kept as deprecated alias |
| Compatibility | Keep `oscpack` namespace + include paths as a deprecated shim | inherit oscpack's install base; the migration path is the moat |
| Language floor | **C++17 floor, C++20 baseline target** | C++20 on modern desktop toolchains; C++17 keeps embedded/older-NDK cross-compilers viable |
| C++20 feature discipline | Use `bit_cast`, `concepts`, `span`, `constexpr`, `[[likely]]`; **avoid** modules, `std::format`, heavy `<ranges>` in the core | embedded/QEMU/Android matrix must stay green; core stays freestanding-friendly |
| Layout | Header-only core, dependency-free, freestanding-friendly | realtime-safety + embedded reach |
| GitHub identity | **Detach fork from `RossBencina/oscpack` network, then rename** to `osctap` | standalone root-repo identity + search visibility, while preserving history, stars, and URL redirects |
| Plan storage | In-repo docs (this file + HERITAGE.md) **+** GitHub milestones/issues | version-controlled source of truth, decomposed for execution tracking |
| Benchmarks / RT-safety | Enforce **realtime-safety** primarily via **RTSan** (`[[clang::nonblocking]]` on hot paths), not hand-rolled allocation benchmarks; measure worst-case latency as a secondary signal | a compiler-checked guarantee beats an eyeballed benchmark; it's what the audio community cares about |
| Realtime contract | Hot path = parsing/serializing a *known-valid* message (allocation- and exception-free, RTSan-clean). Validation may `throw` and is **not** part of the RT contract — it runs off the realtime thread | `throw` allocates (`__cxa_allocate_exception`); drawing the boundary explicitly is what makes the RT claim honest |
| Heritage | Preserve Ross Bencina's copyright headers; credit CNMAT, jcelerier/ossia, contributors; seek Ross's blessing | turns "yet another fork" into "sanctioned successor" |

## Security audit findings (initial Phase 0 work list)

From the review of the current codebase (see git history of this branch):

| # | Severity | Issue | Location |
|---|----------|-------|----------|
| 1 | 🔴 Critical | Blob size validation `throw` missing → remote out-of-bounds read | `osc/OscReceivedElements.h` (blob case in `ReceivedMessage::Init`) |
| 2 | 🟠 High | Remote `__stop_` magic string can shut down the receive loop | `ip/posix/UdpSocket.h` |
| 3 | 🟡 Medium | `RoundUp4` integer overflow for sizes near `UINT32_MAX` | `osc/OscUtilities.h` |
| 4 | 🟡 Medium | 32-bit pointer-overflow in bundle/blob length walk | `osc/OscReceivedElements.h` |
| 5 | 🟡 Medium | Unbounded recursion on nested bundles (stack exhaustion) | `osc/OscPacketListener.h` |
| 6 | 🟢 Low | array-level underflow, union type-punning UB, signed blob sizes, empty `SmallString.h`, unfinished `OwnedMessage`, etc. | various |

## Phased plan

### Phase 0 — Credibility core (current)
- [x] Commit ROADMAP.md + HERITAGE.md.
- [x] Land audit fix #1 (critical blob OOB read) plus #4 (overflow-safe bundle/blob
      size checks) and the array-level-underflow guard.
- [x] Land remaining security fixes: #2 (`__stop_` remote shutdown) and #5 (bundle
      nesting recursion bound).
- [x] Malformed-input test suite (oversized/out-of-range blob, truncated string,
      unterminated type tags, bad bundle sizes, array under/overflow).
- [x] libFuzzer harness + seed corpus under `fuzz/` (with a standalone g++/ASan
      driver for runtimes without libFuzzer; verified it catches the pre-fix bug).
- [x] Rename to `osctap` namespace; keep `oscpack` as a deprecated alias.
- [x] GitHub Actions: Linux/macOS/Windows × GCC/Clang/MSVC, C++17 and C++20,
      plus ASan/UBSan and a standalone-fuzzer smoke-test job.
- [x] Docs skeleton (README.md front page + ROADMAP/HERITAGE/fuzz docs).
- [x] oscpack-compatibility shim verified against existing call sites (tests and
      examples still use `oscpack::` and compile against the alias).

**Phase 0 complete.** The C++ namespace was renamed; the on-disk directory/include
layout was intentionally left at `oscpack/` to keep that step low-risk. The layout
rename is the first item of Phase 1, below.

### Phase 1 — Hardening & modernization
- [x] **Directory/include-path rename** `oscpack/` → `osctap/`. Public headers now live
      under `osctap/` and use the `<osctap/...>` prefix; the old `<oscpack/...>` paths are
      preserved by a redirect shim tree under `oscpack/` (each header forwards to its
      `<osctap/...>` counterpart). `tests/CompatIncludeShim.cpp` is the CI-built guard for
      both the include-path shim and the `oscpack::` namespace alias. Deferred: renaming
      the cosmetic `INCLUDED_OSCPACK_*` include guards.
- [x] **ClusterFuzzLite** — in-repo continuous fuzzing (OSS-Fuzz's CI-driven sibling).
      `.clusterfuzzlite/` (Dockerfile + build.sh over the existing `fuzz/` harness + seed
      corpus) plus two workflows: per-PR code-change fuzzing (`cflite_pr.yml`) and a daily
      batch campaign (`cflite_batch.yml`), each across ASan and UBSan. The layout doubles as
      the basis for an OSS-Fuzz `projects/osctap` integration.
      Deferred — **OSS-Fuzz submission**: the upstream PR to `google/oss-fuzz` (external) and,
      optionally, a `storage-repo` so ClusterFuzzLite accumulates a corpus / dedupes crashes.
- [x] Replace union type-punning with `std::bit_cast` / `memcpy`; `constexpr` parsing.
      `OscUtilities.h` now does all int/float (de)serialization through endian-agnostic
      big-endian byte assembly (no `#ifdef OSC_HOST_*_ENDIAN`) plus a `BitCast` helper
      (`std::bit_cast` on C++20, `memcpy` fallback on C++17) — removing the union
      type-punning and `reinterpret_cast` aliasing UB flagged in audit finding #6, including
      the outbound stream's `elementSizePtr_` (now a `char*` + byte helpers). The integer
      load/store helpers are `constexpr` (signed/float too, on C++20); `OscUnitTests.cpp`
      has `static_assert`s proving it. Verified clean under ASan/UBSan and RTSan, and the
      RT read accessors stay `[[clang::nonblocking]]` (memcpy is a safe builtin there).
- [x] Clean up compiler warnings across MSVC/GCC/Clang (size_t→uint32_t narrowing,
      `strcpy`/`gethostbyname`/`ctime` deprecations, shadowing, `(char)0xFF` constant
      truncation), **then** enable `-Werror`/`/WX` in CI via the opt-in
      `OSCTAP_WARNINGS_AS_ERRORS` option (default OFF so downstream consumers of the
      INTERFACE library are not forced onto our warning bar). The Clang warning flags now
      match GCC's, and the win32 `GetHostByName` was ported to `getaddrinfo` (mirroring the
      posix backend). Deferred: the uncompiled `ip/*/UdpSocket.h` socket backends still use
      `strcpy`/`gethostbyname` and will be cleaned when they enter the compiled CI surface.
- [x] **RTSan**: the read/dispatch hot path (iterating and reading a known-valid message
      via the throw-free `*Unchecked` accessors) is annotated `OSCTAP_REALTIME`
      (`noexcept [[clang::nonblocking]]` on Clang ≥ 20). A dedicated Clang-20 CI job builds
      `tests/OscRealtimeTest.cpp` with `-fsanitize=realtime` (runtime) **and**
      `-Wfunction-effects -Werror` (static), so the contract is enforced both ways. The
      validating/throwing surface (message construction/`Init()`, the checked accessors,
      `AsBoolUnchecked`/`AsBlobUnchecked`, and serialization's overflow check) is
      deliberately left off the contract — it runs off the audio thread.
      Deferred: a non-throwing realtime blob accessor, and recording worst-case latency as
      a secondary benchmark.
- [x] **TSan**: `tests/OscConcurrencyTest.cpp` runs `SocketReceiveMultiplexer::Run()` on
      one thread and stops it via `AsynchronousBreak()` from another (signalling in a loop
      so it can't race ahead of `Run()`'s break-flag reset), plus a best-effort loopback
      packet so `ProcessPacket()` runs concurrently. A dedicated GCC TSan CI job
      (`-DOSCTAP_TSAN=ON`, `-fsanitize=thread`, `halt_on_error=1`) vets it; it also runs as
      a plain functional test on the POSIX matrix legs. TSan is clean, as expected — the
      value is the standing guard for the only real concurrency in the library.

See [Sanitizer strategy](#sanitizer-strategy) for scope and rationale.

## Sanitizer strategy

| Sanitizer | Scope | Phase | Notes |
|-----------|-------|-------|-------|
| ASan + UBSan | full test suite + fuzzer | 0 | passing; the critical-fix commit is clean under it |
| RTSan | annotated read/dispatch hot path (`-fsanitize=realtime` + `-Wfunction-effects`) | 1 | **landed** — read path marked `OSCTAP_REALTIME`; dedicated Clang-20 job enforces it at runtime *and* statically. Turns the RT-safety claim into a compiler-checked guarantee. Defines the realtime contract above. |
| TSan | networking / `SocketReceiveMultiplexer` only | 1 | **landed** — `OscConcurrencyTest` exercises `Run()` vs `AsynchronousBreak()`; dedicated GCC TSan job. The parser is single-threaded by design, so this is the only real concurrency. Clean, as expected. |
| MSan | optional | later | catches uninitialized-memory reads (cf. the past "uninitialized OSC address bytes" fix); high setup friction (instrumented libc++), so not a standing job. |

### Phase 2 — Reach (only as demand appears)
- [x] **Freestanding/embedded profile (no exceptions/RTTI option)** — *groundwork
      landed.* A single build seam (`osc/OscConfig.h`) auto-detects
      `OSCTAP_HAS_EXCEPTIONS` and routes every core `throw` through `OSCTAP_THROW`
      (a plain `throw` when exceptions are on; a non-returning, user-overridable
      fatal handler — `OSCTAP_FATAL_HANDLER`, default `std::abort()` — when they are
      off). `OSCTAP_FREESTANDING` drops the hosted-only facilities (`<iostream>`, the
      `std::vector`-backed `OwnedMessage`, the `std::string` `operator<<`). The
      `OSCTAP_FREESTANDING` CMake option builds `tests/OscFreestandingTest.cpp` with
      `-fno-exceptions -fno-rtti`; a `freestanding` CI job (GCC + Clang) keeps it
      green. Hosted builds are byte-for-byte unchanged. **Demand signal: Raspberry
      Pi Pico 2W (RP2350)** — see [`docs/EMBEDDED_PICO2W.md`](docs/EMBEDDED_PICO2W.md).
      Deferred: a **non-throwing `TryInit`/validate** entry point so a no-exceptions
      build can *reject* untrusted packets by returning an error instead of aborting
      (today, malformed input on an exceptions-off build is fatal — safe only on a
      trusted link; open networks should keep exceptions on and `catch`).
- [ ] QEMU aarch64 / armv7 CI. *(Demand-gated — adds standing CI surface; stand up
      only when a real big-endian/cross target needs it. The freestanding profile
      already exercises the no-OS code paths without QEMU's cost.)*
- [ ] Android NDK build. *(Demand-gated — pursue when an Android consumer appears.)*
- [ ] Multicast receive (cherry-pick from `stephram/oscpack`). *(Self-contained;
      the next demand-driven feature pickup after the freestanding groundwork.)*

## Milestones → GitHub

- **Milestone: Phase 0 — Credibility core** ⇒ audit-fix issues #1–#6, fuzzer, CI, docs.
- **Milestone: Phase 1 — Hardening & modernization**
- **Milestone: Phase 2 — Reach**

## Primary risk

**Sustainability**, not feasibility. A large CI matrix + benchmarks + docs + fuzzing
is significant ongoing surface for a niche, single-maintainer library — the graveyard
of 100+ abandoned oscpack forks is the cautionary tale. Scope each phase to what can be
kept green indefinitely; grow the matrix only as real users request targets.

## Repository identity migration (owner actions)

1. Confirm `github.com/tap/osctap` is free.
2. Open a GitHub Support request to **detach** `tap/oscpack` from the
   `RossBencina/oscpack` fork network (preserves history, stars, redirects; becomes a
   root repo). Draft text tracked with this rebrand.
3. **Settings → General → Rename** `oscpack` → `osctap`. Old URLs auto-redirect.
4. Update local remotes: `git remote set-url origin <new-url>`.
5. Update README, package metadata, include paths, and CI references to `osctap`.
