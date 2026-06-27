#!/bin/bash -eu
#
# ClusterFuzzLite / OSS-Fuzz build script for OscTap.
#
# The library is header-only, so we compile the libFuzzer harness directly with
# the environment's compiler, sanitizer flags ($CXXFLAGS) and fuzzing engine
# ($LIB_FUZZING_ENGINE) rather than going through the project's CMake (which pins
# its own sanitizer flags for local use). WORKDIR is the project root.

# fuzz_parse: the OSC packet parser (untrusted bytes -> ReceivedPacket/...).
$CXX $CXXFLAGS -std=c++17 -I osctap \
    fuzz/fuzz_parse.cpp \
    $LIB_FUZZING_ENGINE \
    -o "$OUT/fuzz_parse"

# fuzz_deframe: the OSC-over-TCP stream deframer + parser (length-prefix
# reassembly across arbitrary chunk boundaries, bounded-buffer / DoS guard).
$CXX $CXXFLAGS -std=c++17 -I osctap \
    fuzz/fuzz_deframe.cpp \
    $LIB_FUZZING_ENGINE \
    -o "$OUT/fuzz_deframe"

# Ship the seed corpora next to the targets. OSS-Fuzz / ClusterFuzzLite
# automatically load <target>_seed_corpus.zip before fuzzing.
zip -j "$OUT/fuzz_parse_seed_corpus.zip" fuzz/corpus/*
zip -j "$OUT/fuzz_deframe_seed_corpus.zip" fuzz/corpus_deframe/*
