/*
  oscpack / OscTap -- libFuzzer entry point for the OSC-over-TCP receive path.

  TCP delivers a byte stream with no message boundaries, so OscStreamDeframer
  reassembles complete packets from arbitrarily chunked reads (and caps the frame
  size so a hostile length prefix can't make it buffer unbounded data). This is
  attacker-controlled input, so it is fuzzed: the harness feeds the input through
  the deframer in pseudo-random chunks (exercising reassembly across every read
  boundary) and runs each reassembled frame through the OSC parser, exactly as the
  TcpListeningReceiveSocket loop does. Any out-of-bounds read surfaces under ASan.

  Build with real libFuzzer (preferred):
      clang++ -std=c++17 -g -O1 -I oscpack \
          -fsanitize=fuzzer,address,undefined fuzz/fuzz_deframe.cpp -o fuzz_deframe
      ./fuzz_deframe fuzz/corpus_deframe

  Or link the standalone driver in fuzz/standalone_main.cpp where the libFuzzer
  runtime is unavailable (see fuzz/README.md).
*/
#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>

#include "osc/OscStreamFraming.h"
#include "osc/OscReceivedElements.h"

using namespace oscpack;

// Run one reassembled frame through the parser, exactly as a consumer would.
static void HandleFrame( const char* data, uint32_t size )
{
    if( size == 0 )
        return;
    // Exactly-sized heap copy: ASan redzones flag any read past the frame length.
    std::vector<char> buffer( data, data + size );
    try{
        ReceivedPacket p( buffer.data(), (osc_bundle_element_size_t)size );
        if( p.IsBundle() ){
            ReceivedBundle b( p );
            for( auto it = b.ElementsBegin(); it != b.ElementsEnd(); ++it )
                if( !it->IsBundle() ){ ReceivedMessage m( *it ); (void)m.ArgumentCount(); }
        }else{
            ReceivedMessage m( p );
            for( auto it = m.ArgumentsBegin(); it != m.ArgumentsEnd(); ++it )
                (void)it->TypeTag();
        }
    }catch( const oscpack::Exception& ){
        // Expected: malformed frame rejected by the parser.
    }catch( const std::exception& ){
        // Tolerate std exceptions (e.g. bad_alloc) -- not a memory-safety finding.
    }
}

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
    // A small cap so the fuzzer reaches the oversized-frame rejection path quickly.
    osctap::OscStreamDeframer deframer( 4096 );

    // Feed the byte stream in pseudo-random 1..8-byte chunks (derived from the
    // data itself) so reassembly across arbitrary read boundaries is exercised.
    size_t i = 0;
    while( i < size ){
        size_t chunk = ( (size_t)data[i] & 0x7 ) + 1; // 1..8
        if( i + chunk > size )
            chunk = size - i;
        const bool ok = deframer.Consume(
            reinterpret_cast<const char*>( data ) + i, chunk,
            []( const char* p, uint32_t n ){ HandleFrame( p, n ); } );
        if( !ok )
            break; // oversized frame: the real loop drops the connection here
        i += chunk;
    }
    return 0;
}
