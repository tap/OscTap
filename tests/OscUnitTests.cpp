/*
	oscpack -- Open Sound Control (OSC) packet manipulation library
    http://www.rossbencina.com/code/oscpack

    Copyright (c) 2004-2013 Ross Bencina <rossb@audiomulch.com>

	Permission is hereby granted, free of charge, to any person obtaining
	a copy of this software and associated documentation files
	(the "Software"), to deal in the Software without restriction,
	including without limitation the rights to use, copy, modify, merge,
	publish, distribute, sublicense, and/or sell copies of the Software,
	and to permit persons to whom the Software is furnished to do so,
	subject to the following conditions:

	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
	ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
	CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
	WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*
	The text above constitutes the entire oscpack license; however, 
	the oscpack developer(s) also make the following non-binding requests:

	Any person wishing to distribute modifications to the Software is
	requested to send the modifications to the original developer so that
	they can be incorporated into the canonical version. It is also 
	requested that these non-binding requests be included whenever the
	above license is reproduced.
*/
#include "OscUnitTests.h"

#include <cstring>
#include <iomanip>
#include <iostream>

#include "osc/OscReceivedElements.h"
#include "osc/OscPrintReceivedElements.h"
#include "osc/OscOutboundPacketStream.h"

#if defined(__BORLANDC__) // workaround for BCB4 release build intrinsics bug
namespace std {
using ::__strcmp__;  // avoid error: E2316 '__strcmp__' is not a member of 'std'.
using ::__strcpy__;  // avoid error: E2316 '__strcpy__' is not a member of 'std'.
}
#endif

namespace osc{

  using namespace oscpack;

static int passCount_=0, failCount_=0;

void PrintTestSummary()
{
    std::cout << (passCount_+failCount_) << " tests run, " << passCount_ << " passed, " << failCount_ << " failed.\n";
}

void pass_equality( const char *slhs, const char *srhs, const char *file, int line )
{
    ++passCount_;
    std::cout << file << "(" << line << "): PASSED : " << slhs << " == " << srhs << "\n";
}

void fail_equality( const char *slhs, const char *srhs, const char *file, int line )
{
    ++failCount_;
    std::cout << file << "(" << line << "): FAILED : " << slhs << " != " << srhs << "\n";
}

template <typename T>
void assertEqual_( const T& lhs, const T& rhs, const char *slhs, const char *srhs, const char *file, int line )
{
    if( lhs == rhs )
        pass_equality( slhs, srhs, file, line );
    else
        fail_equality( slhs, srhs, file, line );
}

template <typename T>
void assertEqual_( const T* lhs, const T* rhs, const char *slhs, const char *srhs, const char *file, int line )
{
    if( lhs == rhs )
        pass_equality( slhs, srhs, file, line );
    else
        fail_equality( slhs, srhs, file, line );
}

template <>
void assertEqual_( const char* lhs, const char* rhs, const char *slhs, const char *srhs, const char *file, int line )
{
    if( std::strcmp( lhs, rhs ) == 0 )
        pass_equality( slhs, srhs, file, line );
    else
        fail_equality( slhs, srhs, file, line );
}


#define assertEqual( a, b ) assertEqual_( (a), (b), #a, #b, __FILE__, __LINE__ )

//---------------------------------------------------------------------------
char * AllocateAligned4( unsigned long size )
{
    char *s = new char[ size + 4 ];   //allocate on stack to get 4 byte alignment
    return (char*)((long)(s-1) & (~0x03L)) + 4;
}

// allocate a 4 byte aligned copy of s
char * NewMessageBuffer( const char *s, unsigned long length )
{
    char *p = AllocateAligned4( length );
    std::memcpy( p, s, length );
    return p;
}

void test1()
{
    const char s[] = "/test\0\0\0,fiT\0\0\0\0\0\0\0\0\0\0\0A";
    char *buffer = NewMessageBuffer( s, sizeof(s)-1 );

    // test argument iterator interface
    bool unexpectedExceptionCaught = false;
    try{
        ReceivedMessage m( ReceivedPacket(buffer, sizeof(s)-1) );

        assertEqual( std::strcmp( m.AddressPattern(), "/test" ), 0 );
        assertEqual( std::strcmp( m.TypeTags(), "fiT" ), 0 );
        
        ReceivedMessage::const_iterator i = m.ArgumentsBegin();
        ++i;
        ++i;
        ++i;
        assertEqual( i, m.ArgumentsEnd() );

        i = m.ArgumentsBegin();
        float f = (i++)->AsFloat();
        (void)f;
        int n = (i++)->AsInt32();
        (void)n;
        bool b = (i++)->AsBool();
        (void)b;
        
        i = m.ArgumentsBegin();
        bool exceptionThrown = false;
        try{
            int n = (i++)->AsInt32();
            (void)n;
        }catch( Exception& ){
            exceptionThrown = true;
        }
        assertEqual( exceptionThrown, true );

    }catch( Exception& e ){
        std::cout << "unexpected exception: " << e.what() << "\n";
        unexpectedExceptionCaught = true;
    }
    assertEqual( unexpectedExceptionCaught, false );


    // test argument stream interface
    unexpectedExceptionCaught = false;
    try{
        ReceivedMessage m( ReceivedPacket(buffer, sizeof(s)-1) );
        ReceivedMessageArgumentStream args = m.ArgumentStream();
        assertEqual( args.Eos(), false );

        float f;
        int32_t n;
        bool b;
        args >> f >> n >> b;

        (void) f;
        (void) n;
        (void) b;
        
        assertEqual( args.Eos(), true );

    }catch( Exception& e ){
        std::cout << "unexpected exception: " << e.what() << "\n";
        unexpectedExceptionCaught = true;
    }
    assertEqual( unexpectedExceptionCaught, false );
}

//---------------------------------------------------------------------------


#define TEST2_PRINT( ss )\
    {\
        const char s[] = ss;\
        ReceivedPacket p( NewMessageBuffer( s, sizeof(s)-1 ), sizeof(s)-1 ); \
        ReceivedMessage m( p );\
        std::cout << m << "\n";\
    }

void test2()
{
    bool unexpectedExceptionCaught = false;
    try{
        //            012301230 1 2 3
        TEST2_PRINT( "/no_args\0\0\0\0" );

        //            012301230 1 2 3 01 2 3
        TEST2_PRINT( "/no_args\0\0\0\0,\0\0\0" );
        
        //            01230123 012 3 0 1 2 3
        TEST2_PRINT( "/an_int\0,i\0\0\0\0\0A" );
        //            012301230 1 2 3 012 3 0 1 2 3 
        TEST2_PRINT( "/a_float\0\0\0\0,f\0\0\0\0\0\0" );
        //            0123012301 2 3 012 3 012301230123 
        TEST2_PRINT( "/a_string\0\0\0,s\0\0hello world\0" );
        //            01230123 012 3 0 1 2 3  0  1  2  3
        TEST2_PRINT( "/a_blob\0,b\0\0\0\0\0\x4\x0\x1\x2\x3" );

        //            0123012301 2 3 012 3 0 1 2 3 0 1 2 3
        TEST2_PRINT( "/an_int64\0\0\0,h\0\0\0\0\0\0\0\0\0\x1" );
        //            01230123012 3 012 3 0 1 2 3 0 1 2 3
        TEST2_PRINT( "/a_timetag\0\0,t\0\0\0\0\0\0\0\0\0\x1" );
        //            0123012301 2 3 012 3 0 1 2 3 0 1 2 3
        TEST2_PRINT( "/a_double\0\0\0,d\0\0\0\0\0\0\0\0\0\0" );
        //            0123012301 2 3 012 3 012301230123 
        TEST2_PRINT( "/a_symbol\0\0\0,S\0\0hello world\0" );
        //            01230123 012 3 0 1 2 3
        TEST2_PRINT( "/a_char\0,c\0\0\0\0\0A" );
        //            012301230 1 2 3 012 3 0 1 2 3 
        TEST2_PRINT( "/a_color\0\0\0\0,r\0\0\0\0\0\0" );
        //            012301230123012 3 012 3 0 1 2 3
        TEST2_PRINT( "/a_midimessage\0\0,m\0\0\0\0\0\0" );
        //            01230123 012 3 
        TEST2_PRINT( "/a_bool\0,T\0\0" );
        //            01230123 012 3 
        TEST2_PRINT( "/a_bool\0,F\0\0" );
        //            01230 1 2 3 012 3
        TEST2_PRINT( "/Nil\0\0\0\0,N\0\0" );
        //            01230 1 2 3 012 3
        TEST2_PRINT( "/Inf\0\0\0\0,I\0\0" );
        //            0123012 3 0123012 3 0 1 2 3  0 1 2 3  0 1 2 3  
        TEST2_PRINT( "/Array\0\0,[iii]\0\0\0\0\0\x1\0\0\0\x2\0\0\0\x3" );

        TEST2_PRINT( "/test\0\0\0,fiT\0\0\0\0\0\0\0\0\0\0\0A" );
                                                        
        bool exceptionThrown = false;
        try{
            TEST2_PRINT( "/a_char\0,x\0\0\0\0\0A" ); // unknown type tag 'x'
        }catch( Exception& ){
            exceptionThrown = true;
        }
        assertEqual( exceptionThrown, true );
        
    }catch( Exception& e ){
        std::cout << "unexpected exception: " << e.what() << "\n";
        unexpectedExceptionCaught = true;
    }
    assertEqual( unexpectedExceptionCaught, false );
}

//-----------------------------------------------------------------------

// pack a message and then unpack it and check that the result is the same
// also print each message
// repeat the process inside a bundle

#define TEST_PACK_UNPACK0( addressPattern, argument, value, recieveGetter ) \
    {                                    \
        std::memset( buffer, 0x74, bufferSize );   \
        OutboundPacketStream ps( buffer, bufferSize ); \
        ps << BeginMessage( addressPattern )  \
            << argument \
            << oscpack::EndMessage();\
        assertEqual( ps.IsReady(), true );\
        ReceivedMessage m( ReceivedPacket(ps.Data(), ps.Size()) );\
        std::cout << m << "\n";\
        assertEqual( m.ArgumentsBegin()-> recieveGetter () , value );\
    }  \
    {                                    \
        std::memset( buffer, 0x74, bufferSize );   \
        OutboundPacketStream ps( buffer, bufferSize ); \
        ps << BeginBundle( 1234 ) \
            << BeginMessage( addressPattern )  \
            << argument \
            << oscpack::EndMessage() \
            << EndBundle();\
        assertEqual( ps.IsReady(), true );\
        ReceivedBundle b( ReceivedPacket(ps.Data(), ps.Size()) );\
        ReceivedMessage m( *b.ElementsBegin() );\
        std::cout << m << "\n";\
        assertEqual( m.ArgumentsBegin()-> recieveGetter () , value );\
    }
    
#define TEST_PACK_UNPACK( addressPattern, argument, type, recieveGetter ) \
    {                                    \
        std::memset( buffer, 0x74, bufferSize );   \
        OutboundPacketStream ps( buffer, bufferSize ); \
        ps << BeginMessage( addressPattern )  \
            << argument \
            << oscpack::EndMessage();\
        assertEqual( ps.IsReady(), true );\
        ReceivedMessage m( ReceivedPacket(ps.Data(), ps.Size()) );\
        std::cout << m << "\n";\
        assertEqual( m.ArgumentsBegin()-> recieveGetter () , ( type ) argument );\
    }  \
    {                                    \
        std::memset( buffer, 0x74, bufferSize );   \
        OutboundPacketStream ps( buffer, bufferSize ); \
        ps << BeginBundle( 1234 ) \
            << BeginMessage( addressPattern )  \
            << argument \
            << oscpack::EndMessage() \
            << EndBundle();\
        assertEqual( ps.IsReady(), true );\
        ReceivedBundle b( ReceivedPacket(ps.Data(), ps.Size()) );\
        ReceivedMessage m( *b.ElementsBegin() );\
        std::cout << m << "\n";\
        assertEqual( m.ArgumentsBegin()-> recieveGetter () , ( type ) argument );\
    }

void test3()
{
    int bufferSize = 1000;
    char *buffer = AllocateAligned4( bufferSize );

// single message tests
    // empty message
    {
        std::memset( buffer, 0x74, bufferSize );
        OutboundPacketStream ps( buffer, bufferSize );
        ps << BeginMessage( "/no_arguments" )
            << oscpack::EndMessage();
        assertEqual( ps.IsReady(), true );
        ReceivedMessage m( ReceivedPacket(ps.Data(), ps.Size()) );
        std::cout << m << "\n";\
    }

    TEST_PACK_UNPACK( "/a_bool", true, bool, AsBool );
    TEST_PACK_UNPACK( "/a_bool", false, bool, AsBool );
    TEST_PACK_UNPACK( "/a_bool", (bool)1, bool, AsBool );


    TEST_PACK_UNPACK0( "/nil", OscNil(), true, IsNil );
    TEST_PACK_UNPACK0( "/inf", Infinitum(), true, IsInfinitum );

    TEST_PACK_UNPACK( "/an_int", (int32_t)1234, int32_t, AsInt32 );

    TEST_PACK_UNPACK( "/a_float", 3.1415926f, float, AsFloat );

    TEST_PACK_UNPACK( "/a_char", 'c', char, AsChar );

    TEST_PACK_UNPACK( "/an_rgba_color", RgbaColor(0x22334455), uint32_t, AsRgbaColor );

    TEST_PACK_UNPACK( "/a_midi_message", MidiMessage(0x7F), uint32_t, AsMidiMessage );

    TEST_PACK_UNPACK( "/an_int64_t", (int64_t)(0xFFFFFFFF), int64_t, AsInt64 );

    TEST_PACK_UNPACK( "/a_time_tag", TimeTag(0xFFFFFFFF), uint64_t, AsTimeTag );

    TEST_PACK_UNPACK( "/a_double", (double)3.1415926, double, AsDouble );

    // blob
    {
        char blobData[] = "abcd";
        std::memset( buffer, 0x74, bufferSize );
        OutboundPacketStream ps( buffer, bufferSize );
        ps << BeginMessage( "/a_blob" )
            << Blob( blobData, 4 )
            << oscpack::EndMessage();
        assertEqual( ps.IsReady(), true );
        ReceivedMessage m( ReceivedPacket(ps.Data(), ps.Size()) );
        std::cout << m << "\n";

        const void *value;
        osc_bundle_element_size_t size;
        m.ArgumentsBegin()->AsBlob( value, size );
        assertEqual( size, (osc_bundle_element_size_t)4 );
        assertEqual( (memcmp( value, blobData, 4 ) == 0), true );
    }

    // array
    {
        int32_t arrayData[] = {1,2,3,4};
        const std::size_t sourceArrayItemCount = 4;
        std::memset( buffer, 0x74, bufferSize );
        OutboundPacketStream ps( buffer, bufferSize );
        ps << BeginMessage( "/an_array" )
            << BeginArray();
        for( std::size_t j=0; j < sourceArrayItemCount; ++j )
            ps << arrayData[j];
        ps << EndArray() << oscpack::EndMessage();
        assertEqual( ps.IsReady(), true );
        ReceivedMessage m( ReceivedPacket(ps.Data(), ps.Size()) );
        std::cout << m << "\n";

        ReceivedMessageArgumentIterator i = m.ArgumentsBegin();
        assertEqual( i->IsArrayBegin(), true );
        assertEqual( i->ComputeArrayItemCount(), sourceArrayItemCount );
        std::size_t arrayItemCount = i->ComputeArrayItemCount();
        ++i; // move past array begin marker        
        for( std::size_t j=0; j < arrayItemCount; ++j ){
            assertEqual( true, i->IsInt32() );
            int32_t k = i->AsInt32();
            assertEqual( k, arrayData[j] );
            ++i;
        }

        assertEqual( i->IsArrayEnd(), true );
    }



    TEST_PACK_UNPACK( "/a_string", "hello world", const char*, AsString );

    TEST_PACK_UNPACK( "/a_symbol", Symbol("foobar"), const char*, AsSymbol );


    // nested bundles, and multiple messages in bundles...

    {
        std::memset( buffer, 0x74, bufferSize );
        OutboundPacketStream ps( buffer, bufferSize );
        ps << BeginBundle()
            << BeginMessage( "/message_one" ) << 1 << 2 << 3 << 4 << oscpack::EndMessage()
            << BeginMessage( "/message_two" ) << 1 << 2 << 3 << 4 << oscpack::EndMessage()
            << BeginMessage( "/message_three" ) << 1 << 2 << 3 << 4 << oscpack::EndMessage()
            << BeginMessage( "/message_four" ) << 1 << 2 << 3 << 4 << oscpack::EndMessage()
            << EndBundle();
        assertEqual( ps.IsReady(), true );
        ReceivedBundle b( ReceivedPacket(ps.Data(), ps.Size()) );
        std::cout << b << "\n";
    }
}


//---------------------------------------------------------------------------
// Regression tests for malformed-packet handling. Several of these crafted
// packets previously slipped past validation -- most importantly the blob-size
// bounds check in ReceivedMessage::Init(), which constructed but never threw
// its MalformedMessageException, allowing an out-of-bounds read. Each packet
// below must now be rejected with an osc::Exception.

static bool ParsingMessageThrows( const char *data, std::size_t size )
{
    char *buffer = NewMessageBuffer( data, size );
    try{
        ReceivedMessage m( ReceivedPacket( buffer, size ) );
        // Walking the arguments should never be reached for these inputs --
        // validation in Init() should reject them up front -- but iterate
        // anyway so the test fails loudly rather than reading out of bounds.
        for( ReceivedMessage::const_iterator i = m.ArgumentsBegin();
                i != m.ArgumentsEnd(); ++i )
            (void)i->TypeTag();
    }catch( const Exception& ){
        return true;
    }
    return false;
}

static bool ParsingBundleThrows( const char *data, std::size_t size )
{
    char *buffer = NewMessageBuffer( data, size );
    try{
        ReceivedBundle b( ReceivedPacket( buffer, size ) );
        (void)b.ElementCount();
    }catch( const Exception& ){
        return true;
    }
    return false;
}

void test4()
{
    // CRITICAL: a blob whose declared size extends far past the packet.
    // address "/b", type tags ",b", then a 4-byte blob size of 0x10000000
    // (256 MB, in-range for IsValidElementSizeValue) with no payload present.
    {
        char m[] = { '/','b',0,0,  ',','b',0,0,  0,0,0,0 };
        m[8] = 0x10; // big-endian 0x10000000 blob size, no data follows
        assertEqual( ParsingMessageThrows( m, sizeof(m) ), true );
    }

    // a blob size that is out of range (0xFFFFFFFF == negative int32) is rejected
    {
        char m[] = { '/','b',0,0,  ',','b',0,0,  0,0,0,0 };
        m[8] = (char)0xFF; m[9] = (char)0xFF; m[10] = (char)0xFF; m[11] = (char)0xFF;
        assertEqual( ParsingMessageThrows( m, sizeof(m) ), true );
    }

    // unterminated type tag string (no null terminator before end of packet)
    {
        const char m[] = { '/','x',0,0,  ',','i','i','i' };
        assertEqual( ParsingMessageThrows( m, sizeof(m) ), true );
    }

    // fixed-size argument truncated: type tag 'i' but no 4 bytes of data follow
    {
        const char m[] = { '/','x',0,0,  ',','i',0,0 };
        assertEqual( ParsingMessageThrows( m, sizeof(m) ), true );
    }

    // array close ']' with no matching open '[' (array-level underflow)
    {
        const char m[] = { '/','x',0,0,  ',',']',0,0 };
        assertEqual( ParsingMessageThrows( m, sizeof(m) ), true );
    }

    // unterminated array: '[' with no closing ']'
    {
        const char m[] = { '/','x',0,0,  ',','[',0,0 };
        assertEqual( ParsingMessageThrows( m, sizeof(m) ), true );
    }

    // a bundle element whose declared size extends past the packet
    {
        char b[] = { '#','b','u','n','d','l','e',0,
                     0,0,0,0,0,0,0,0,    // time tag
                     0,0,0,0 };          // element size
        b[16] = 0x10; // 0x10000000-byte element, no data follows
        assertEqual( ParsingBundleThrows( b, sizeof(b) ), true );
    }

    // positive control: a well-formed blob must still parse and round-trip.
    {
        char buffer[64];
        std::memset( buffer, 0, sizeof(buffer) );
        OutboundPacketStream ps( buffer, sizeof(buffer) );
        const char payload[] = { 1, 2, 3, 4, 5 };
        ps << BeginMessage( "/b" ) << Blob( payload, sizeof(payload) ) << oscpack::EndMessage();

        bool ok = true;
        try{
            ReceivedMessage m( ReceivedPacket( ps.Data(), ps.Size() ) );
            ReceivedMessage::const_iterator i = m.ArgumentsBegin();
            const void *data;
            osc_bundle_element_size_t size;
            i->AsBlob( data, size );
            assertEqual( size, (osc_bundle_element_size_t)sizeof(payload) );
        }catch( const Exception& ){
            ok = false;
        }
        assertEqual( ok, true );
    }
}

//---------------------------------------------------------------------------


void RunUnitTests()
{
    test1();
    test2();
    test3();
    test4();
    PrintTestSummary();
}

} // namespace osc


#ifndef NO_OSC_TEST_MAIN

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    osc::RunUnitTests();
}

#endif
