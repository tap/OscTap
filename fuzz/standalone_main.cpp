/*
  oscpack / OscTap -- standalone driver for fuzz_parse.cpp.

  Lets the fuzz target be built and exercised WITHOUT the libFuzzer runtime
  (e.g. with g++ + AddressSanitizer). This is NOT a coverage-guided fuzzer: it
  replays any inputs given on the command line and then runs a bounded,
  deterministic random-mutation loop over them. Use real libFuzzer
  (clang -fsanitize=fuzzer) for actual fuzzing; this driver is for CI smoke
  tests, crash-repro replay, and environments without the sanitizer runtime.

  Build:
      g++ -std=c++17 -g -O1 -I oscpack -fsanitize=address,undefined \
          fuzz/fuzz_parse.cpp fuzz/standalone_main.cpp -o fuzz_parse_standalone
      ./fuzz_parse_standalone fuzz/corpus/*
*/
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <random>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size );

static std::vector<uint8_t> ReadFile( const char* path )
{
    std::vector<uint8_t> bytes;
    FILE* f = std::fopen( path, "rb" );
    if( !f ){
        std::fprintf( stderr, "warning: could not open %s\n", path );
        return bytes;
    }
    uint8_t chunk[4096];
    size_t n;
    while( (n = std::fread( chunk, 1, sizeof(chunk), f )) > 0 )
        bytes.insert( bytes.end(), chunk, chunk + n );
    std::fclose( f );
    return bytes;
}

int main( int argc, char** argv )
{
    std::vector<std::vector<uint8_t>> seeds;
    for( int i = 1; i < argc; ++i )
        seeds.push_back( ReadFile( argv[i] ) );

    // 1) Replay every input verbatim (corpus entries and crash repros).
    for( const auto& s : seeds )
        LLVMFuzzerTestOneInput( s.data(), s.size() );

    if( seeds.empty() ){
        std::printf( "no inputs given; pass corpus files to replay/mutate\n" );
        return 0;
    }

    // 2) Bounded deterministic random-mutation loop over the seeds. Fixed seed
    //    so runs are reproducible.
    std::mt19937 rng( 0x05CADAB7u );
    const int kIterations = 200000;
    for( int iter = 0; iter < kIterations; ++iter ){
        std::vector<uint8_t> buf = seeds[ rng() % seeds.size() ];
        if( buf.empty() )
            continue;

        // a handful of random single-byte flips
        int flips = 1 + (rng() % 6);
        for( int k = 0; k < flips; ++k )
            buf[ rng() % buf.size() ] = (uint8_t)( rng() & 0xFF );

        // occasionally truncate to probe short/edge-length handling
        if( (rng() & 7) == 0 && buf.size() > 4 )
            buf.resize( rng() % buf.size() );

        LLVMFuzzerTestOneInput( buf.data(), buf.size() );
    }

    std::printf( "standalone fuzz driver: replayed %zu seed(s), ran %d mutations -- no crash\n",
                 seeds.size(), kIterations );
    return 0;
}
