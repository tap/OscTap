# OscTap Heritage & Credits

OscTap stands on two decades of prior work. This document records that lineage,
preserves credit where it is due, and explains how OscTap relates to its ancestors.

## The OSC protocol

Open Sound Control (OSC) was developed at **CNMAT, UC Berkeley**, principally by
**Matt Wright** and **Adrian Freed**. The OSC 1.0 specification (2002) is the protocol
OscTap implements. The protocol is stable and frozen, which is why a mature codebase
remains valuable rather than obsolete.

## Lineage

```
OSC 1.0 spec (CNMAT / Matt Wright, Adrian Freed)
        │
        ▼
oscpack — Ross Bencina  (~2004–2014, MIT license)
   originally hosted at code.google.com/p/oscpack
   v1.1.0 (April 2013) was the last release; last commit Sept 2014
        │
        ├─ exported to GitHub: RossBencina/oscpack (canonical, now frozen)
        │
        ▼
header-only fork — Jean-Michaël Celerier (jcelerier) (2016–2024)
   used in the ossia / libossia ecosystem; removed Boost, made header-only,
   modernized toward C++14/17, thread-safety and correctness fixes
        │
        │   + contributions from Alex Norman (2022)
        │   + Tim Place (2025)
        ▼
OscTap — actively maintained continuation
   C++17 floor / C++20 baseline, security-hardened, fuzzed, documented, CI-tested.
   The `oscpack` namespace and include paths are retained as a deprecated
   compatibility alias for drop-in migration.
```

## Credits

- **Matt Wright, Adrian Freed, and CNMAT** — the OSC protocol.
- **Ross Bencina** — the original oscpack, whose design and code are the foundation of
  OscTap. His copyright notices are preserved throughout the source as required by, and
  in the spirit of, the oscpack license.
- **Jean-Michaël Celerier (jcelerier)** — the header-only modernization and the ossia
  lineage that OscTap continues from.
- **Alex Norman** and **Tim Place** — subsequent fixes and modernization.

## License & attribution

oscpack is distributed under an MIT-style license (see `LICENSE`). It additionally
makes a *non-binding* request that modifications be sent upstream and that the request
itself be reproduced. OscTap:

- **Preserves Ross Bencina's copyright headers** in all derived source files.
- Adds OscTap copyright notices alongside, not in place of, the originals.
- Documents the heritage here rather than obscuring it.

## Relationship to upstream

OscTap is an independent, maintained continuation — not a hostile fork. Upstream
`RossBencina/oscpack` is treated as a frozen reference. Where upstream or sibling forks
carry useful patches (e.g. ARM64/aarch64 support, multicast receive), OscTap aims to
cherry-pick and credit them. A blessing from Ross Bencina is being sought to formalize
OscTap's status as the sanctioned successor; regardless of outcome, the heritage and
attribution above stand.
