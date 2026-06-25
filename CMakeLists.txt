cmake_minimum_required(VERSION 2.6)
project(TestOscpack)
set(CMAKE_CXX_STANDARD 14)

set(OSCPACK_BUILD_EXAMPLES ON CACHE BOOL "Should we build examples")

set(CMAKE_INCLUDE_CURRENT_DIR 1)
set(CMAKE_POSITION_INDEPENDENT_CODE 1)

if(WIN32)
 set(IpSystemTypePath oscpack/ip/win32)
else()
 set(IpSystemTypePath oscpack/ip/posix)
endif()

add_library(oscpack INTERFACE)

include_directories(oscpack)

target_include_directories(oscpack INTERFACE .)
if(WIN32)
  target_link_libraries(oscpack INTERFACE ws2_32 winmm)
endif()

if(OSCPACK_BUILD_EXAMPLES)
  add_executable(OscUnitTests tests/OscUnitTests.cpp)
  target_link_libraries(OscUnitTests oscpack)

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

set(OSCPACK_BUILD_FUZZERS OFF CACHE BOOL "Build the libFuzzer fuzz target (requires Clang)")
set(OSCPACK_FUZZER_STANDALONE OFF CACHE BOOL "Build the fuzz target with the standalone driver instead of libFuzzer (works with g++/ASan, no fuzzer runtime needed)")

if(OSCPACK_BUILD_FUZZERS OR OSCPACK_FUZZER_STANDALONE)
  if(OSCPACK_FUZZER_STANDALONE)
    add_executable(fuzz_parse fuzz/fuzz_parse.cpp fuzz/standalone_main.cpp)
    target_compile_options(fuzz_parse PRIVATE -g -O1 -fsanitize=address,undefined)
    target_link_libraries(fuzz_parse oscpack -fsanitize=address,undefined)
  else()
    # real coverage-guided libFuzzer build (Clang only)
    add_executable(fuzz_parse fuzz/fuzz_parse.cpp)
    target_compile_options(fuzz_parse PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined)
    target_link_libraries(fuzz_parse oscpack -fsanitize=fuzzer,address,undefined)
  endif()
  set_target_properties(fuzz_parse PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
endif()

if(MSVC)
  target_compile_options(oscpack INTERFACE /W4)
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  target_compile_options(oscpack INTERFACE -Wall -Wextra -Wno-long-long -pedantic)
endif()
