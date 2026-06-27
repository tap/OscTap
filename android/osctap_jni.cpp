/*
    OscTap demo: Android NDK / JNI bridge.

    Part of the Pi 5 <-> Pico 2W <-> Android integration tutorial
    (docs/INTEGRATION_PI5_PICO_ANDROID.md). Exposes the OscTap C++ core to
    Kotlin/Java so an Android app can build and parse OSC packets natively; the
    UDP transport itself stays on the JVM side (java.net.DatagramSocket).

    Two entry points, matching OscTap.kt (package org.osctap.demo):
      * buildMessage(String address, Object[] args) -> byte[]
            args may be Integer / Float / Boolean / String (mapped to OSC i f T/F s).
      * describe(byte[] packet) -> String
            parse one OSC *message* and return a human-readable summary.

    Build with the Android NDK via android/CMakeLists.txt (see the tutorial). The
    core is header-only, so there is nothing to link but this bridge.

    NOTE on untrusted input: parsing throws on malformed packets. Exceptions must
    be ENABLED for the NDK target (the CMakeLists sets -fexceptions); describe()
    catches and surfaces a Java exception instead of aborting.
*/

#include <jni.h>
#include <string>

#include "osc/OscReceivedElements.h"
#include "osc/OscOutboundPacketStream.h"

namespace {

// Append one boxed Java argument (Integer/Float/Boolean/String) as the matching
// OSC argument. Returns false if the type is unsupported.
bool AppendBoxedArg( JNIEnv* env, osctap::OutboundPacketStream& p, jobject arg )
{
    if( arg == nullptr ) return false;

    jclass integerCls = env->FindClass( "java/lang/Integer" );
    jclass floatCls   = env->FindClass( "java/lang/Float" );
    jclass boolCls    = env->FindClass( "java/lang/Boolean" );
    jclass stringCls  = env->FindClass( "java/lang/String" );

    if( env->IsInstanceOf( arg, integerCls ) ){
        jint v = env->CallIntMethod( arg, env->GetMethodID( integerCls, "intValue", "()I" ) );
        p << (int32_t)v;
    } else if( env->IsInstanceOf( arg, floatCls ) ){
        jfloat v = env->CallFloatMethod( arg, env->GetMethodID( floatCls, "floatValue", "()F" ) );
        p << (float)v;
    } else if( env->IsInstanceOf( arg, boolCls ) ){
        jboolean v = env->CallBooleanMethod( arg, env->GetMethodID( boolCls, "booleanValue", "()Z" ) );
        p << (bool)(v == JNI_TRUE);
    } else if( env->IsInstanceOf( arg, stringCls ) ){
        const char* s = env->GetStringUTFChars( (jstring)arg, nullptr );
        p << s;  // const char* overload -> OSC string
        env->ReleaseStringUTFChars( (jstring)arg, s );
    } else {
        return false;
    }
    return true;
}

void ThrowJava( JNIEnv* env, const char* cls, const char* msg )
{
    jclass c = env->FindClass( cls );
    if( c ) env->ThrowNew( c, msg );
}

} // namespace

extern "C" JNIEXPORT jbyteArray JNICALL
Java_org_osctap_demo_OscTap_buildMessage( JNIEnv* env, jclass, jstring jaddress, jobjectArray args )
{
    const char* address = env->GetStringUTFChars( jaddress, nullptr );
    char buffer[1024];
    jbyteArray result = nullptr;

    try {
        osctap::OutboundPacketStream p( buffer, sizeof(buffer) );
        p << osctap::BeginMessage( address );

        const jsize n = args ? env->GetArrayLength( args ) : 0;
        for( jsize i = 0; i < n; ++i ){
            jobject a = env->GetObjectArrayElement( args, i );
            if( !AppendBoxedArg( env, p, a ) ){
                env->ReleaseStringUTFChars( jaddress, address );
                ThrowJava( env, "java/lang/IllegalArgumentException",
                           "unsupported OSC argument type (use Integer/Float/Boolean/String)" );
                return nullptr;
            }
        }
        p << osctap::EndMessage();

        result = env->NewByteArray( (jsize)p.Size() );
        env->SetByteArrayRegion( result, 0, (jsize)p.Size(),
                                 reinterpret_cast<const jbyte*>( p.Data() ) );
    } catch( const osctap::Exception& e ) {
        ThrowJava( env, "java/lang/IllegalArgumentException", e.what() );
    }

    env->ReleaseStringUTFChars( jaddress, address );
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_osctap_demo_OscTap_describe( JNIEnv* env, jclass, jbyteArray packet )
{
    const jsize n = env->GetArrayLength( packet );
    jbyte* bytes = env->GetByteArrayElements( packet, nullptr );

    std::string out;
    try {
        // The parser uses byte-assembly (de)serialization, so the buffer needs
        // no special alignment. Untrusted input -> wrap in try/catch.
        osctap::ReceivedMessage m( osctap::ReceivedPacket(
            reinterpret_cast<const char*>( bytes ), (std::size_t)n ) );

        out = m.AddressPattern();
        for( auto a = m.ArgumentsBegin(); a != m.ArgumentsEnd(); ++a ){
            out += ' ';
            if( a->IsInt32() )       out += std::to_string( a->AsInt32Unchecked() );
            else if( a->IsFloat() )  out += std::to_string( a->AsFloatUnchecked() );
            else if( a->IsString() ) out += std::string("\"") + a->AsStringUnchecked() + "\"";
            else if( a->IsBool() )   out += a->AsBoolUnchecked() ? "true" : "false";
            else                     out += '?';
        }
    } catch( const osctap::Exception& e ) {
        env->ReleaseByteArrayElements( packet, bytes, JNI_ABORT );
        ThrowJava( env, "java/lang/IllegalArgumentException", e.what() );
        return nullptr;
    }

    env->ReleaseByteArrayElements( packet, bytes, JNI_ABORT );
    return env->NewStringUTF( out.c_str() );
}
