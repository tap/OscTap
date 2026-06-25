# OscTap Roadmap

OscTap is the actively-maintained, security-hardened, modern-C++ continuation of
**oscpack**, Ross Bencina's long-standing Open Sound Control (OSC) library. This
document is the source of truth for the rebrand and the work plan. See
[`docs/HERITAGE.md`](docs/HERITAGE.md) for lineage and credits.

> Status: planning / Phase 0 in progress on branch `claude/oscpack-review-audit-h8n268`.

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
| Benchmarks | Measure **realtime-safety** (zero heap alloc in hot path, bounded worst-case latency), not raw throughput | that's what the audio community actually cares about |
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
- [ ] Commit ROADMAP.md + HERITAGE.md (this commit).
- [ ] Rename to `osctap` namespace; keep `oscpack` as a deprecated alias.
- [ ] Land audit fixes (#1 critical first, then #2–#4).
- [ ] Malformed-input test suite (oversized blob, truncated string, unterminated
      type tags, bad bundle sizes, deep nesting, array under/overflow).
- [ ] libFuzzer harness + seed corpus under `fuzz/`.
- [ ] GitHub Actions: Linux/macOS/Windows × GCC/Clang/MSVC, C++17 and C++20.
- [ ] Docs skeleton.
- [ ] oscpack-compatibility shim verified against existing call sites.

### Phase 1 — Hardening & modernization
- [ ] OSS-Fuzz submission (free continuous fuzzing for OSS).
- [ ] Sanitizer CI: ASan / UBSan / TSan.
- [ ] Replace union type-punning with `std::bit_cast` / `memcpy`; `constexpr` parsing.
- [ ] Realtime-safety benchmarks: assert zero heap allocation in parse/serialize hot
      paths; record worst-case latency bounds.
- [ ] Bounded bundle-nesting depth (configurable).

### Phase 2 — Reach (only as demand appears)
- [ ] QEMU aarch64 / armv7 CI.
- [ ] Android NDK build.
- [ ] Freestanding/embedded profile (no exceptions/RTTI option).
- [ ] Multicast receive (cherry-pick from `stephram/oscpack`).

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
