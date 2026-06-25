#!/bin/bash -eu
#
# ClusterFuzzLite / OSS-Fuzz build script for OscTap.
#
# The library is header-only, so we compile the libFuzzer harness directly with
# the environment's compiler, sanitizer flags ($CXXFLAGS) and fuzzing engine
# ($LIB_FUZZING_ENGINE) rather than going through the project's CMake (which pins
# its own sanitizer flags for local use). WORKDIR is the project root.

$CXX $CXXFLAGS -std=c++17 -I osctap \
    fuzz/fuzz_parse.cpp \
    $LIB_FUZZING_ENGINE \
    -o "$OUT/fuzz_parse"

# Ship the seed corpus next to the target. OSS-Fuzz / ClusterFuzzLite
# automatically load <target>_seed_corpus.zip before fuzzing.
zip -j "$OUT/fuzz_parse_seed_corpus.zip" fuzz/corpus/*
