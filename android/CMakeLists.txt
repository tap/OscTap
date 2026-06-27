# Android NDK build for the OscTap JNI bridge (libosctap_jni.so).
#
# Built by Gradle's externalNativeBuild, or directly with the NDK toolchain:
#
#   cmake -B build-android android \
#     -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 \
#     -DOSCTAP_ROOT=/abs/path/to/OscTap
#   cmake --build build-android
#
# (Repeat per ABI: arm64-v8a, armeabi-v7a, x86_64 — or let Gradle fan out.)
cmake_minimum_required(VERSION 3.22)
project(osctap_jni CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OscTap repo root (header-only core). Defaults to the parent of android/.
set(OSCTAP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "Path to the OscTap repo root")

add_library(osctap_jni SHARED osctap_jni.cpp)
target_include_directories(osctap_jni PRIVATE ${OSCTAP_ROOT} ${OSCTAP_ROOT}/osctap)

# Parsing untrusted OSC throws on malformed input; keep exceptions enabled so the
# bridge can catch and surface a Java exception instead of aborting. (RTTI is not
# required.) The NDK enables exceptions by default for CMake projects; we set it
# explicitly so the contract is unambiguous.
target_compile_options(osctap_jni PRIVATE -fexceptions -fno-rtti)

# Uncomment if you add __android_log_print logging in the bridge:
# find_library(log-lib log)
# target_link_libraries(osctap_jni ${log-lib})
