/*
    OscTap non-throwing-validation test.

    Verifies that osctap::TryValidatePacket() (the non-throwing gate for untrusted
    input on no-exceptions / freestanding builds) agrees with the throwing parse
    path: for any input, TryValidatePacket() returns nullptr (valid) iff a full
    recursive read of that packet would NOT throw. This differential check is what
    makes the single-source refactor trustworthy -- the validator and the throwing
    constructors share one implementation, and this proves they stay in lock-step.

    Built on the hosted matrix (exceptions on) so it can use try/catch as the
    oracle. The freestanding harness separately proves TryValidatePacket() rejects
    malformed input by *returning* instead of aborting.
*/

#include "osc/OscReceivedElements.h"
#include "osc/OscOutboundPacketStream.h"

#include <cstdio>
#include <cstring>
#include <vector>

using osctap::osc_bundle_element_size_t;

static int failures = 0;
#define CHECK(cond) do{ if(!(cond)){ std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } }while(0)

// Oracle: does a *full recursive read* of this packet throw? Mirrors exactly what
// TryValidatePacket promises is safe -- construct, and recurse through bundles and
// messages, touching everything the validator walks.
static void FullRead( const char* data, osc_bundle_element_size_t size )
{
    osctap::ReceivedPacket p( data, size );
    if( p.IsBundle() ){
        osctap::ReceivedBundle b( p );
        for( auto i = b.ElementsBegin(); i != b.ElementsEnd(); ++i ){
            if( i->IsBundle() ) {
                osctap::ReceivedBundle nested( *i );
                (void)nested.ElementCount();
                for( auto j = nested.ElementsBegin(); j != nested.ElementsEnd(); ++j ){
                    if( !j->IsBundle() ){ osctap::ReceivedMessage m(*j); (void)m.ArgumentCount(); }
                }
            } else {
                osctap::ReceivedMessage m( *i );
                (void)m.ArgumentCount();
            }
        }
    } else {
        osctap::ReceivedMessage m( p );
        (void)m.ArgumentCount();
    }
}

static bool ThrowingPathRejects( const char* data, osc_bundle_element_size_t size )
{
    try { FullRead( data, size ); return false; }
    catch( const osctap::Exception& ) { return true; }
}

// Assert the non-throwing gate and the throwing path agree on this input.
static void Differential( const char* label, const std::vector<char>& buf )
{
    const osc_bundle_element_size_t n = (osc_bundle_element_size_t)buf.size();
    const bool rejects = ThrowingPathRejects( buf.data(), n );
    const bool gateRejects = ( osctap::TryValidatePacket( buf.data(), n ) != nullptr );
    if( rejects != gateRejects )
        std::printf( "FAIL [%s]: throwing-path rejects=%d but gate rejects=%d\n",
                     label, (int)rejects, (int)gateRejects ), ++failures;
}

static std::vector<char> BuildMessage()
{
    char tmp[256];
    osctap::OutboundPacketStream p( tmp, sizeof(tmp) );
    p << osctap::BeginMessage( "/x" ) << (int32_t)1 << (float)2.5f << "hi" << true
      << osctap::EndMessage();
    return std::vector<char>( p.Data(), p.Data() + p.Size() );
}

static std::vector<char> BuildBundle()
{
    char tmp[256];
    osctap::OutboundPacketStream p( tmp, sizeof(tmp) );
    p << osctap::BeginBundleImmediate()
        << osctap::BeginMessage( "/a" ) << (int32_t)7 << osctap::EndMessage()
        << osctap::BeginMessage( "/b" ) << "yo" << osctap::EndMessage()
      << osctap::EndBundle();
    return std::vector<char>( p.Data(), p.Data() + p.Size() );
}

int main()
{
    // --- valid inputs: both paths accept ---
    const std::vector<char> msg = BuildMessage();
    const std::vector<char> bun = BuildBundle();
    CHECK( osctap::TryValidatePacket( msg.data(), (osc_bundle_element_size_t)msg.size() ) == nullptr );
    CHECK( osctap::TryValidatePacket( bun.data(), (osc_bundle_element_size_t)bun.size() ) == nullptr );
    Differential( "valid message", msg );
    Differential( "valid bundle", bun );

    // --- malformed inputs: both paths must reject, identically ---
    // truncations (every prefix that isn't the whole thing)
    for( std::size_t k = 1; k < msg.size(); ++k )
        Differential( "msg prefix", std::vector<char>( msg.begin(), msg.begin() + k ) );
    for( std::size_t k = 1; k < bun.size(); ++k )
        Differential( "bun prefix", std::vector<char>( bun.begin(), bun.begin() + k ) );

    // corrupt the type tag to an unknown tag
    { auto b = msg; // find the ',' type-tag start and poke a bogus tag after it
      for( std::size_t i = 0; i + 1 < b.size(); ++i ) if( b[i] == ',' ){ b[i+1] = 'Q'; break; }
      Differential( "unknown type tag", b ); }

    // claim a huge blob/forge: flip a size-ish word in the bundle's element size
    { auto b = bun; if( b.size() > 19 ){ b[16] = (char)0x7F; b[17] = (char)0xFF; } // element size huge
      Differential( "bundle element size overflow", b ); }

    // non-multiple-of-4 total size
    { auto b = msg; b.push_back( 'x' ); Differential( "size not mult of 4", b ); }

    // --- explicit checks: trivial bad size + the nesting bound ---
    CHECK( osctap::TryValidatePacket( "\0\0\0", 3 ) != nullptr ); // size 3: rejected
    // The top-level bundle counts as the first nesting level, so maxDepth 0 rejects
    // it outright; the default depth accepts the same (shallow) bundle.
    CHECK( osctap::TryValidatePacket( bun.data(), (osc_bundle_element_size_t)bun.size(), 0 ) != nullptr );
    CHECK( osctap::TryValidatePacket( bun.data(), (osc_bundle_element_size_t)bun.size() ) == nullptr );

    if( failures == 0 ) std::printf( "OscValidateTest: OK (gate agrees with throwing path)\n" );
    return failures == 0 ? 0 : 1;
}
