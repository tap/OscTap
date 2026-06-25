/*
    OscTap realtime-safety test.

    Exercises the realtime contract from ROADMAP.md: parsing and dispatching a
    *known-valid* OSC message is allocation- and exception-free. The library's
    read/iterate hot path is marked OSCTAP_REALTIME ([[clang::nonblocking]] on
    Clang >= 20); this test calls that surface from inside a nonblocking function
    so the contract is enforced two ways when built with the RTSan CMake option
    (-DOSCTAP_RTSAN=ON, Clang >= 20):

      * statically  -- -Wfunction-effects -Werror: the nonblocking functions, and
        everything they transitively call, must be provably free of allocation,
        locking and exceptions.
      * at runtime  -- -fsanitize=realtime: RealtimeSanitizer aborts if any
        real-time-unsafe call (malloc/free/syscall/throw/...) is reached while
        inside ReadHotPath().

    Off Clang>=20 / without the option, OSCTAP_REALTIME is a no-op and this is a
    plain functional test of the read path, so it runs on the whole CI matrix.

    What is deliberately NOT in the realtime region (per the contract -- these may
    throw and run off the audio thread): message construction/validation
    (ReceivedMessage's constructor calls Init(), which validates), the *checked*
    accessors (AsInt32() etc., which throw on type mismatch), AsBoolUnchecked()
    and AsBlobUnchecked() (which still validate), and serialization (operator<<
    throws on buffer overflow). Iterating *past* a blob is realtime-safe, so the
    message below includes one; only the validating blob accessor is avoided.
*/

#include "osc/OscReceivedElements.h"
#include "osc/OscOutboundPacketStream.h"

#include <cstdint>
#include <cstring>
#include <iostream>

using namespace osctap;

static int g_failures = 0;
#define CHECK(cond) do { if(!(cond)) { \
    std::cerr << "realtime test FAILED: " #cond " (line " << __LINE__ << ")\n"; \
    ++g_failures; } } while(0)

// A small struct so the realtime function returns data without allocating.
struct ReadResult {
    int64_t  i32 = 0, i64 = 0;
    double   f = 0.0, d = 0.0;
    uint32_t rgba = 0, midi = 0;
    uint64_t timetag = 0;
    char     ch = 0;
    bool     boolTrue = false, boolFalse = true;
    const char *str = nullptr, *sym = nullptr;
    uint32_t argCount = 0;
    char     firstAddrChar = 0;
};

// THE REALTIME HOT PATH: iterate an already-validated message and read every
// argument through the throw-free OSCTAP_REALTIME accessors. No allocation, no
// exceptions -- RTSan and -Wfunction-effects enforce this.
static ReadResult ReadHotPath( const ReceivedMessage& m ) OSCTAP_REALTIME
{
    ReadResult r;
    r.firstAddrChar = m.AddressPattern()[0];
    r.argCount = m.ArgumentCount();

    for( ReceivedMessage::const_iterator i = m.ArgumentsBegin();
         i != m.ArgumentsEnd(); ++i ){
        switch( i->TypeTag() ){
            case INT32_TYPE_TAG:        r.i32     = i->AsInt32Unchecked();      break;
            case FLOAT_TYPE_TAG:        r.f       = i->AsFloatUnchecked();      break;
            case CHAR_TYPE_TAG:         r.ch      = i->AsCharUnchecked();       break;
            case RGBA_COLOR_TYPE_TAG:   r.rgba    = i->AsRgbaColorUnchecked();  break;
            case MIDI_MESSAGE_TYPE_TAG: r.midi    = i->AsMidiMessageUnchecked();break;
            case INT64_TYPE_TAG:        r.i64     = i->AsInt64Unchecked();      break;
            case TIME_TAG_TYPE_TAG:     r.timetag = i->AsTimeTagUnchecked();    break;
            case DOUBLE_TYPE_TAG:       r.d       = i->AsDoubleUnchecked();     break;
            case STRING_TYPE_TAG:       r.str     = i->AsStringUnchecked();     break;
            case SYMBOL_TYPE_TAG:       r.sym     = i->AsSymbolUnchecked();     break;
            // bool's value lives in the type tag itself -- read it RT-safely
            // without the (validating) AsBoolUnchecked().
            case TRUE_TYPE_TAG:         r.boolTrue  = true;                     break;
            case FALSE_TYPE_TAG:        r.boolFalse = false;                    break;
            // nil / infinitum / array markers / blob: iterating past them is
            // realtime-safe (Advance() does no allocation or throwing); we just
            // don't read the blob payload here (that accessor validates).
            default: break;
        }
    }
    return r;
}

int main()
{
    // --- off the realtime thread: build + validate a known-good message ---
    char buffer[512];
    OutboundPacketStream p( buffer, sizeof(buffer) );
    const unsigned char blobBytes[] = { 1, 2, 3, 4, 5 };
    p << BeginMessage( "/rt/test" )
      << (int32_t)42
      << 3.5f
      << 'z'
      << RgbaColor( 0x11223344u )
      << MidiMessage( 0x55667788u )
      << (int64_t)0x0123456789ABCDEFLL
      << TimeTag( 0xFEDCBA9876543210ULL )
      << 2.5
      << "hello"
      << Symbol( "sym" )
      << true
      << false
      << OscNil()
      << Infinitum()
      << Blob( blobBytes, (osc_bundle_element_size_t)sizeof(blobBytes) )
      // Empty array: exercises iterating past the '[' and ']' markers without a
      // nested element colliding with the top-level scalars asserted below.
      << BeginArray() << EndArray()
      << EndMessage();
    CHECK( p.IsReady() );

    // Construction validates the packet (may throw) -- explicitly off-RT.
    ReceivedMessage m( ReceivedPacket( p.Data(), p.Size() ) );

    // --- realtime region ---
    ReadResult r = ReadHotPath( m );

    // --- verify the hot path read everything correctly ---
    CHECK( r.firstAddrChar == '/' );
    CHECK( r.i32 == 42 );
    CHECK( r.f == 3.5 );
    CHECK( r.ch == 'z' );
    CHECK( r.rgba == 0x11223344u );
    CHECK( r.midi == 0x55667788u );
    CHECK( r.i64 == 0x0123456789ABCDEFLL );
    CHECK( r.timetag == 0xFEDCBA9876543210ULL );
    CHECK( r.d == 2.5 );
    CHECK( r.str != nullptr && std::strcmp( r.str, "hello" ) == 0 );
    CHECK( r.sym != nullptr && std::strcmp( r.sym, "sym" ) == 0 );
    CHECK( r.boolTrue == true );
    CHECK( r.boolFalse == false );
    CHECK( r.argCount > 0 );

    if( g_failures == 0 )
        std::cout << "realtime test: read hot path OK (RT-safe)\n";
    return g_failures == 0 ? 0 : 1;
}
