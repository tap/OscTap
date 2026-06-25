# ClusterFuzzLite build environment for OscTap.
#
# Uses the OSS-Fuzz base-builder image, which provides the Clang/libFuzzer/
# sanitizer toolchain and exports $CC/$CXX/$CFLAGS/$CXXFLAGS/$LIB_FUZZING_ENGINE
# for build.sh to consume. This same layout is what an OSS-Fuzz `projects/osctap`
# integration would use, so the step up to OSS-Fuzz later is small.
FROM gcr.io/oss-fuzz-base/base-builder:latest

COPY . $SRC/osctap
WORKDIR $SRC/osctap
COPY .clusterfuzzlite/build.sh $SRC/
