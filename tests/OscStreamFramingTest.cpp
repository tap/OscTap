/*
    OscTap length-prefix stream-framing test.

    Exercises OscStreamDeframer's reassembly across every chunk boundary a byte
    stream (TCP) can impose: coalesced packets, packets split across reads, the
    4-byte header itself split across reads, empty payloads, and the oversized-
    frame DoS guard. This is the security-critical half of OSC-over-TCP, so it is
    tested independently of any socket.
*/

#include "osc/OscStreamFraming.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond) do{ if(!(cond)){ std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } }while(0)

// Collects emitted packets so we can compare against what was framed.
struct Collector {
    std::vector<std::string> packets;
    void operator()( const char* p, uint32_t n ) { packets.emplace_back( p, p + n ); }
};

// Build a wire buffer: each input packet length-prefixed and concatenated.
static std::vector<char> Wire( const std::vector<std::string>& packets )
{
    std::vector<char> w;
    for( const auto& pkt : packets ){
        char hdr[4];
        osctap::WriteOscStreamFrameHeader( hdr, (uint32_t)pkt.size() );
        w.insert( w.end(), hdr, hdr + 4 );
        w.insert( w.end(), pkt.begin(), pkt.end() );
    }
    return w;
}

// Feed `wire` to a fresh deframer in fixed-size chunks; return the emitted packets.
static std::vector<std::string> DeframeInChunks( const std::vector<char>& wire, std::size_t chunk )
{
    osctap::OscStreamDeframer d;
    Collector c;
    bool ok = true;
    for( std::size_t i = 0; i < wire.size(); i += chunk ){
        std::size_t n = (i + chunk <= wire.size()) ? chunk : (wire.size() - i);
        ok = d.Consume( wire.data() + i, n, c ) && ok;
    }
    if( !ok ) c.packets.clear(); // signal protocol error to the caller's check
    return c.packets;
}

int main()
{
    const std::vector<std::string> packets = {
        std::string( "/a\0\0,i\0\0\0\0\0\1", 12 ),  // a small "message-ish" blob
        std::string( "hello" ),
        std::string( 1000, 'x' ),                    // spans typical read sizes
        std::string( "" ),                           // empty payload (valid frame)
        std::string( "/z" ),
    };
    const std::vector<char> wire = Wire( packets );

    // Feed the same stream at every chunk size from 1 byte up to the whole buffer:
    // reassembly must be invariant to how the bytes are split.
    for( std::size_t chunk = 1; chunk <= wire.size(); ++chunk ){
        const std::vector<std::string> got = DeframeInChunks( wire, chunk );
        bool same = ( got.size() == packets.size() );
        for( std::size_t i = 0; same && i < got.size(); ++i )
            same = ( got[i] == packets[i] );
        if( !same ){
            std::printf( "FAIL: mismatch at chunk size %zu (got %zu packets)\n", chunk, got.size() );
            ++failures;
        }
    }

    // Coalesced: the whole stream in one Consume() yields every packet in order.
    {
        osctap::OscStreamDeframer d; Collector c;
        CHECK( d.Consume( wire.data(), wire.size(), c ) );
        CHECK( c.packets.size() == packets.size() );
    }

    // Two deframers fed the same split stream agree (no shared/static state).
    {
        CHECK( DeframeInChunks( wire, 3 ) == DeframeInChunks( wire, 5 ) );
    }

    // Oversized-frame DoS guard: a header announcing > maxFrameSize -> Consume
    // returns false, and does so even when only the header has arrived.
    {
        osctap::OscStreamDeframer d( 16 ); // tiny cap
        char hdr[4];
        osctap::WriteOscStreamFrameHeader( hdr, 1u << 20 ); // 1 MiB announced
        Collector c;
        CHECK( d.Consume( hdr, 4, c ) == false );
        CHECK( c.packets.empty() );
    }

    // A frame exactly at the cap is accepted; one byte over is rejected.
    {
        osctap::OscStreamDeframer ok( 8 ), over( 8 );
        Collector c1, c2;
        const std::vector<char> w8  = Wire( { std::string( 8, 'a' ) } );
        const std::vector<char> w9  = Wire( { std::string( 9, 'b' ) } );
        CHECK( ok.Consume( w8.data(), w8.size(), c1 ) == true );
        CHECK( c1.packets.size() == 1 && c1.packets[0].size() == 8 );
        CHECK( over.Consume( w9.data(), w9.size(), c2 ) == false );
    }

    // FrameOscPacket convenience: correct framing + capacity check.
    {
        char out[16];
        std::size_t n = osctap::FrameOscPacket( "hey", 3, out, sizeof(out) );
        CHECK( n == 7 );
        CHECK( osctap::ToUInt32( out ) == 3 );
        CHECK( std::memcmp( out + 4, "hey", 3 ) == 0 );
        CHECK( osctap::FrameOscPacket( "hey", 3, out, 6 ) == 0 ); // 4+3 > 6 -> no fit
    }

    if( failures == 0 ) std::printf( "OscStreamFramingTest: OK\n" );
    return failures == 0 ? 0 : 1;
}
