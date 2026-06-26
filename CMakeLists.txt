cmake_minimum_required(VERSION 3.16)
project(TestOscpack)
# C++17 floor (overridable on the command line, e.g. -DCMAKE_CXX_STANDARD=20).
if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

set(OSCPACK_BUILD_EXAMPLES ON CACHE BOOL "Should we build examples")
set(OSCTAP_RTSAN OFF CACHE BOOL "Build the realtime-safety test under RealtimeSanitizer + function-effect analysis (requires Clang >= 20)")
set(OSCTAP_TSAN OFF CACHE BOOL "Build the concurrency test under ThreadSanitizer (POSIX; GCC/Clang)")
set(OSCTAP_FREESTANDING OFF CACHE BOOL "Build the freestanding/embedded-profile smoke test with exceptions and RTTI disabled (Phase 2 'Reach')")

set(CMAKE_INCLUDE_CURRENT_DIR 1)
set(CMAKE_POSITION_INDEPENDENT_CODE 1)

if(WIN32)
 set(IpSystemTypePath osctap/ip/win32)
else()
 set(IpSystemTypePath osctap/ip/posix)
endif()

add_library(oscpack INTERFACE)

# Public headers now live under osctap/ (prefix <osctap/...>); the top-level
# oscpack/ tree is a thin redirect shim kept for <oscpack/...> compatibility.
# Adding osctap/ to the search path lets the in-tree quoted "osc/..."/"ip/..."
# includes resolve, while the repo root (added to the INTERFACE below) keeps
# both <osctap/...> and the deprecated <oscpack/...> prefixes working.
include_directories(osctap)

target_include_directories(oscpack INTERFACE .)
if(WIN32)
  target_link_libraries(oscpack INTERFACE ws2_32 winmm)
endif()

if(OSCPACK_BUILD_EXAMPLES)
  add_executable(OscUnitTests tests/OscUnitTests.cpp)
  target_link_libraries(OscUnitTests oscpack)
  add_test(NAME OscUnitTests COMMAND OscUnitTests)

  # Live coverage for the compatibility shims: this TU deliberately includes via
  # the deprecated <oscpack/...> paths and uses the oscpack:: namespace alias, so
  # CI fails if either shim regresses. Do not migrate it to <osctap/...>.
  add_executable(CompatIncludeShim tests/CompatIncludeShim.cpp)
  target_link_libraries(CompatIncludeShim oscpack)
  add_test(NAME CompatIncludeShim COMMAND CompatIncludeShim)

  # Realtime-safety test. Runs as a plain functional test of the read/dispatch
  # hot path everywhere (OSCTAP_REALTIME is a no-op off Clang>=20). With
  # -DOSCTAP_RTSAN=ON (Clang>=20) it additionally enforces the realtime contract:
  # RealtimeSanitizer at runtime (-fsanitize=realtime) and the function-effect
  # analysis statically (-Wfunction-effects -Werror). See ROADMAP.md.
  add_executable(OscRealtimeTest tests/OscRealtimeTest.cpp)
  target_link_libraries(OscRealtimeTest oscpack)
  add_test(NAME OscRealtimeTest COMMAND OscRealtimeTest)
  if(OSCTAP_RTSAN)
    target_compile_options(OscRealtimeTest PRIVATE -fsanitize=realtime -Wfunction-effects -Werror)
    target_link_options(OscRealtimeTest PRIVATE -fsanitize=realtime)
  endif()

  # Concurrency test for the receive loop: Run() on one thread vs
  # AsynchronousBreak() (+ a loopback packet) from another. Runs as a plain
  # functional test on POSIX; with -DOSCTAP_TSAN=ON it is built under
  # ThreadSanitizer (the basis for the dedicated TSan CI job). Uses real sockets
  # + std::thread, so it is gated to non-Windows for now.
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    add_executable(OscConcurrencyTest tests/OscConcurrencyTest.cpp)
    target_link_libraries(OscConcurrencyTest oscpack Threads::Threads)
    add_test(NAME OscConcurrencyTest COMMAND OscConcurrencyTest)
    if(OSCTAP_TSAN)
      target_compile_options(OscConcurrencyTest PRIVATE -fsanitize=thread -g)
      target_link_options(OscConcurrencyTest PRIVATE -fsanitize=thread)
    endif()
  endif()

  #add_executable(OscSendTests tests/OscSendTests.cpp)
  #target_link_libraries(OscSendTests oscpack)

  #add_executable(OscReceiveTest tests/OscReceiveTest.cpp)
  #target_link_libraries(OscReceiveTest oscpack)

  #add_executable(OscDump examples/OscDump.cpp)
  #target_link_libraries(OscDump oscpack)

  #add_executable(SimpleReceive examples/SimpleReceive.cpp)
  #target_link_libraries(SimpleReceive oscpack)

  #add_executable(SimpleSend examples/SimpleSend.cpp)
  #target_link_libraries(SimpleSend oscpack)
endif()

# Freestanding / embedded profile (Phase 2 "Reach"). Builds a single smoke test
# of the parse/serialize core with C++ exceptions and RTTI disabled and the
# hosted-only facilities dropped (-DOSCTAP_FREESTANDING). The flags are PRIVATE
# to this target so the exception-based tests above are unaffected; this target
# is the standing guard that the core stays embedded-buildable (e.g. a Raspberry
# Pi Pico 2W — see docs/EMBEDDED_PICO2W.md).
if(OSCTAP_FREESTANDING)
  add_executable(OscFreestandingTest tests/OscFreestandingTest.cpp)
  target_link_libraries(OscFreestandingTest oscpack)
  target_compile_definitions(OscFreestandingTest PRIVATE OSCTAP_FREESTANDING)
  if(MSVC)
    target_compile_options(OscFreestandingTest PRIVATE /EHs-c- /GR-)
  else()
    target_compile_options(OscFreestandingTest PRIVATE -fno-exceptions -fno-rtti)
  endif()
  add_test(NAME OscFreestandingTest COMMAND OscFreestandingTest)
endif()

set(OSCTAP_BUILD_FUZZERS OFF CACHE BOOL "Build the libFuzzer fuzz target (requires Clang)")
set(OSCTAP_FUZZER_STANDALONE OFF CACHE BOOL "Build the fuzz target with the standalone driver instead of libFuzzer (works with g++/ASan, no fuzzer runtime needed)")

if(OSCTAP_BUILD_FUZZERS OR OSCTAP_FUZZER_STANDALONE)
  if(OSCTAP_FUZZER_STANDALONE)
    add_executable(fuzz_parse fuzz/fuzz_parse.cpp fuzz/standalone_main.cpp)
    target_compile_options(fuzz_parse PRIVATE -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
    target_link_libraries(fuzz_parse oscpack -fsanitize=address,undefined)
  else()
    # real coverage-guided libFuzzer build (Clang only)
    add_executable(fuzz_parse fuzz/fuzz_parse.cpp)
    target_compile_options(fuzz_parse PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
    target_link_libraries(fuzz_parse oscpack -fsanitize=fuzzer,address,undefined)
  endif()
  set_target_properties(fuzz_parse PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
endif()

# Warnings-as-errors is opt-in (default OFF) so that consumers building against
# the INTERFACE library are never forced to fail on our warning bar; CI turns it
# on (-DOSCTAP_WARNINGS_AS_ERRORS=ON) to keep the tree clean.
set(OSCTAP_WARNINGS_AS_ERRORS OFF CACHE BOOL "Treat compiler warnings as errors (used in CI)")

if(MSVC)
  target_compile_options(oscpack INTERFACE /W4)
  if(OSCTAP_WARNINGS_AS_ERRORS)
    target_compile_options(oscpack INTERFACE /WX)
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(oscpack INTERFACE -Wall -Wextra -Wno-long-long -pedantic)
  if(OSCTAP_WARNINGS_AS_ERRORS)
    target_compile_options(oscpack INTERFACE -Werror)
  endif()
endif()
