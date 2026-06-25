# Fuzzing OscTap

The packet-parsing path (`ReceivedPacket` / `ReceivedBundle` / `ReceivedMessage`)
is the library's untrusted attack surface: it consumes arbitrary bytes from the
network. `fuzz_parse.cpp` exercises it the way a consumer would — reading the
address, every type tag, and every argument's bytes (including blob payloads) —
so out-of-bounds reads surface under AddressSanitizer.

## Real fuzzing (preferred) — Clang + libFuzzer

Requires Clang with the libFuzzer + sanitizer runtimes installed.

```sh
clang++ -std=c++17 -g -O1 -I osctap \
    -fsanitize=fuzzer,address,undefined \
    fuzz/fuzz_parse.cpp -o fuzz_parse
./fuzz_parse fuzz/corpus            # seed corpus bootstraps coverage
```

Via CMake:

```sh
cmake -B build -DOSCTAP_BUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build --target fuzz_parse
./build/fuzz_parse fuzz/corpus
```

## Standalone driver — g++ / any compiler, no fuzzer runtime

When the libFuzzer runtime is unavailable, link `standalone_main.cpp`. It is
**not** coverage-guided: it replays inputs given on the command line and then
runs a bounded, deterministic random-mutation loop over them. Useful for CI
smoke tests, crash-repro replay, and sanitizer coverage without libFuzzer.

```sh
g++ -std=c++17 -g -O1 -I osctap -fsanitize=address,undefined \
    fuzz/fuzz_parse.cpp fuzz/standalone_main.cpp -o fuzz_parse_standalone
./fuzz_parse_standalone fuzz/corpus/*
```

Via CMake: `-DOSCTAP_FUZZER_STANDALONE=ON` (works with g++).

## Continuous fuzzing — ClusterFuzzLite

[ClusterFuzzLite](https://google.github.io/clusterfuzzlite/) runs this harness in
CI (OSS-Fuzz's in-repo sibling). Config lives in [`../.clusterfuzzlite/`](../.clusterfuzzlite/):
`Dockerfile` (OSS-Fuzz base-builder image) and `build.sh` (compiles `fuzz_parse.cpp`
with the environment's `$CXXFLAGS`/`$LIB_FUZZING_ENGINE` and ships the seed corpus).
Two workflows drive it, each across ASan and UBSan:

- **`.github/workflows/cflite_pr.yml`** — fuzzes the code changed in each PR
  (`mode: code-change`, 120 s) and fails the check on any new crash.
- **`.github/workflows/cflite_batch.yml`** — a longer daily campaign
  (`mode: batch`, 600 s).

These build an OSS-Fuzz Docker image, so they are heavier than the in-tree
`fuzz-smoke` job in `ci.yml` (which stays as a fast standalone+ASan smoke test).
Without a configured `storage-repo`, each run starts from the in-repo seed corpus;
the same `.clusterfuzzlite/` layout is also the basis for an OSS-Fuzz
`projects/osctap` submission.

## Corpus and crash repros

- `corpus/` — small valid seed packets (a simple message, a message with a blob
  and a string, a message with int64/double/char/array, and a bundle), generated
  with `OutboundPacketStream`. Seeds bootstrap coverage; libFuzzer will grow the
  corpus as it explores.
- To reproduce a crash, pass the offending input file directly:
  `./fuzz_parse <crash-file>`. Keep regression repros under version control so
  they can be replayed by the standalone driver in CI.

## Notes

- A malformed packet that throws `oscpack::Exception` is **expected** behaviour,
  not a finding; the harness catches it. Only memory errors (reported by ASan)
  or unexpected termination indicate a bug.
- This harness found nothing on the current tree; built against the pre-fix
  blob-validation code it reports a heap-buffer-overflow within a few inputs,
  which is exactly the regression the fuzzer exists to prevent.
