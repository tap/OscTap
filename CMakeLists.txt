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

  # Differential test: the non-throwing TryValidatePacket() gate must agree with
  # the throwing parse path (single source of truth). See OscValidateTest.cpp.
  add_executable(OscValidateTest tests/OscValidateTest.cpp)
  target_link_libraries(OscValidateTest oscpack)
  add_test(NAME OscValidateTest COMMAND OscValidateTest)

  # Length-prefix stream framing (OSC over TCP). The reassembly/DoS-guard logic is
  # transport-independent and tested without sockets. See OscStreamFramingTest.cpp.
  add_executable(OscStreamFramingTest tests/OscStreamFramingTest.cpp)
  target_link_libraries(OscStreamFramingTest oscpack)
  add_test(NAME OscStreamFramingTest COMMAND OscStreamFramingTest)

  # Compile/link smoke for the win32 socket backend, which otherwise has no
  # compiled coverage (the POSIX demos/tests are gated off Windows). Built and run
  # by the existing windows-latest CI legs. The win32 backend is /W4-clean, so it
  # builds under the INTERFACE /W4 /WX like everything else (a per-target /WX-
  # would be ineffective anyway -- CMake appends INTERFACE options last).
  if(WIN32)
    add_executable(Win32SocketSmoke tests/Win32SocketSmoke.cpp)
    target_link_libraries(Win32SocketSmoke oscpack)
    add_test(NAME Win32SocketSmoke COMMAND Win32SocketSmoke)
  endif()

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

    # OSC-over-UDP and OSC-over-TCP loopback tests: real socket client + server,
    # asserting that messages arrive and decode correctly (the TCP one also spans
    # TCP segments to exercise deframer reassembly). Both SKIP gracefully if the
    # environment forbids loopback networking. POSIX-only (std::thread + sockets);
    # also built under the TSan job to vet Run() vs AsynchronousBreak().
    foreach(socktest OscUdpTest OscTcpTest)
      add_executable(${socktest} tests/${socktest}.cpp)
      target_link_libraries(${socktest} oscpack Threads::Threads)
      add_test(NAME ${socktest} COMMAND ${socktest})
      if(OSCTAP_TSAN)
        target_compile_options(${socktest} PRIVATE -fsanitize=thread -g)
        target_link_options(${socktest} PRIVATE -fsanitize=thread)
      endif()
    endforeach()
  endif()

  # Canonical oscpack examples, compile-checked so they can't bit-rot (they use
  # the deprecated oscpack:: alias on purpose). Not run -- they bind sockets /
  # block on input; see demos/ for the modern, tested equivalents.
  foreach(example SimpleSend SimpleReceive OscDump)
    add_executable(${example} examples/${example}.cpp)
    target_link_libraries(${example} oscpack)
  endforeach()
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

# Integration demos (Pi 5 <-> Pico 2W <-> Android tutorial). Real, runnable OSC
# programs over the POSIX UDP backend: a hub/router and a CLI sender. POSIX-only
# (they use the ip/posix sockets + a SIGINT handler), mirroring the Pi 5 target.
# See docs/INTEGRATION_PI5_PICO_ANDROID.md.
set(OSCTAP_BUILD_DEMOS OFF CACHE BOOL "Build the Pi 5 integration demos (POSIX sockets)")
if(OSCTAP_BUILD_DEMOS AND NOT WIN32)
  add_executable(pi5_hub demos/pi5_hub.cpp)
  target_link_libraries(pi5_hub oscpack)

  add_executable(osc_send demos/osc_send.cpp)
  target_link_libraries(osc_send oscpack)

  # OSC-over-TCP demo pair (see docs/OSC_OVER_TCP.md): a server/monitor and a
  # CLI client, counterparts to pi5_hub / osc_send.
  add_executable(tcp_server demos/tcp_server.cpp)
  target_link_libraries(tcp_server oscpack)

  add_executable(tcp_send demos/tcp_send.cpp)
  target_link_libraries(tcp_send oscpack)
endif()

set(OSCTAP_BUILD_FUZZERS OFF CACHE BOOL "Build the libFuzzer fuzz target (requires Clang)")
set(OSCTAP_FUZZER_STANDALONE OFF CACHE BOOL "Build the fuzz target with the standalone driver instead of libFuzzer (works with g++/ASan, no fuzzer runtime needed)")

if(OSCTAP_BUILD_FUZZERS OR OSCTAP_FUZZER_STANDALONE)
  # fuzz_parse: the OSC packet parser. fuzz_deframe: the OSC-over-TCP stream
  # deframer + parser (length-prefix reassembly, the bounded-buffer/DoS guard).
  foreach(fuzzer fuzz_parse fuzz_deframe)
    if(OSCTAP_FUZZER_STANDALONE)
      add_executable(${fuzzer} fuzz/${fuzzer}.cpp fuzz/standalone_main.cpp)
      target_compile_options(${fuzzer} PRIVATE -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all)
      target_link_libraries(${fuzzer} oscpack -fsanitize=address,undefined)
    else()
      # real coverage-guided libFuzzer build (Clang only)
      add_executable(${fuzzer} fuzz/${fuzzer}.cpp)
      target_compile_options(${fuzzer} PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all)
      target_link_libraries(${fuzzer} oscpack -fsanitize=fuzzer,address,undefined)
    endif()
    set_target_properties(${fuzzer} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  endforeach()
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
