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
#ifndef INCLUDED_OSCTAP_OSCSTREAMFRAMING_H
#define INCLUDED_OSCTAP_OSCSTREAMFRAMING_H

#include <cstddef>
#include <cstdint>
#include <cstring> // memcpy
#include <vector>

#include "OscUtilities.h" // FromUInt32 / ToUInt32

/*
  Length-prefixed OSC stream framing -- the de-facto convention for OSC over a
  reliable byte stream such as TCP (CNMAT, liblo's osc.tcp, SuperCollider, Max,
  JUCE). Each packet on the wire is a 4-byte big-endian length followed by that
  many payload bytes -- the same shape as a bundle element's size slot.

  A datagram transport (UDP) hands you message boundaries for free; a byte stream
  does not, so the receiver must reassemble complete packets from arbitrarily
  chunked reads. The encoder here is trivial (write the header, then the payload);
  OscStreamDeframer is the real work, and it caps the frame size so a hostile
  length prefix cannot make it buffer unbounded data (cf. the blob-size discipline
  from audit findings #1/#4).

  This codec is transport-agnostic: it knows nothing about sockets. The TCP socket
  types (ip/TcpSocket.h) use it; you can equally drive it from any byte source.
  SLIP framing (the OSC 1.1 nominated alternative) is intentionally deferred.
*/

namespace osctap{

enum { OSC_STREAM_FRAME_HEADER_SIZE = 4 };

// Default cap on a single framed packet (and therefore on the per-connection
// reassembly buffer). 64 KiB comfortably exceeds any normal OSC packet while
// bounding the memory a peer can make you hold. Override per-deframer.
enum { OSC_DEFAULT_MAX_FRAME_SIZE = 64 * 1024 };

// Encoder: write the 4-byte big-endian length prefix for a `packetSize`-byte
// packet into `header`. The caller then writes the payload. Pure, non-allocating,
// freestanding-safe. (The socket transmit path writes the header then the payload
// directly, avoiding a copy; FrameOscPacket() below is the one-buffer convenience.)
inline void WriteOscStreamFrameHeader( char header[OSC_STREAM_FRAME_HEADER_SIZE], uint32_t packetSize )
{
    FromUInt32( header, packetSize );
}

// Convenience: write [4-byte length][payload] contiguously into `out` (capacity
// `outCapacity`). Returns the framed size (4 + packetSize), or 0 if it does not
// fit. Non-allocating.
inline std::size_t FrameOscPacket( const char* packet, uint32_t packetSize,
                                   char* out, std::size_t outCapacity )
{
    if( (std::size_t)packetSize + OSC_STREAM_FRAME_HEADER_SIZE > outCapacity )
        return 0;
    WriteOscStreamFrameHeader( out, packetSize );
    if( packetSize )
        std::memcpy( out + OSC_STREAM_FRAME_HEADER_SIZE, packet, packetSize );
    return (std::size_t)packetSize + OSC_STREAM_FRAME_HEADER_SIZE;
}

// Streaming decoder: feed it received bytes in whatever chunks the transport
// delivers; it emits each complete OSC packet exactly once. One instance per
// connection (it holds that connection's reassembly state).
//
// Non-throwing. Allocates at most one bounded reassembly buffer (<= maxFrameSize),
// and only when a packet straddles a read boundary -- packets contained whole in a
// single chunk are dispatched in place with no copy.
class OscStreamDeframer{
public:
    explicit OscStreamDeframer( uint32_t maxFrameSize = OSC_DEFAULT_MAX_FRAME_SIZE )
        : maxFrameSize_( maxFrameSize )
        , frameSize_( 0 )
        , headerFill_( 0 )
        , haveHeader_( false ) {}

    uint32_t MaxFrameSize() const { return maxFrameSize_; }

    // Feed `size` bytes received from the stream. For each complete packet, calls
    // sink(const char* packet, uint32_t packetSize). Returns true normally; returns
    // false as soon as a frame header announces a size greater than maxFrameSize()
    // -- a protocol violation / DoS attempt, on which the caller should drop the
    // connection (the deframer's state is then undefined until Reset()).
    template<class Sink>
    bool Consume( const char* data, std::size_t size, Sink&& sink )
    {
        const char* p = data;
        const char* const end = data + size;

        while( p < end ){
            if( !haveHeader_ ){
                if( headerFill_ == 0 && (std::size_t)(end - p) >= OSC_STREAM_FRAME_HEADER_SIZE ){
                    // whole header present contiguously, nothing carried over
                    frameSize_ = ToUInt32( p );
                    p += OSC_STREAM_FRAME_HEADER_SIZE;
                }else{
                    // accumulate the length prefix across reads
                    while( headerFill_ < OSC_STREAM_FRAME_HEADER_SIZE && p < end )
                        header_[headerFill_++] = *p++;
                    if( headerFill_ < OSC_STREAM_FRAME_HEADER_SIZE )
                        return true; // need more bytes to complete the header
                    frameSize_ = ToUInt32( header_ );
                    headerFill_ = 0;
                }

                if( frameSize_ > maxFrameSize_ )
                    return false; // oversized / hostile frame

                // A zero-length frame is structurally valid framing; it is
                // forwarded as an empty packet, which the OSC layer
                // (ReceivedPacket) then rejects -- framing doesn't judge OSC
                // validity, it only delimits packets.

                haveHeader_ = true;
            }

            // accumulate / dispatch the payload of frameSize_ bytes
            if( buffer_.empty() && (std::size_t)(end - p) >= frameSize_ ){
                // whole payload present contiguously -> dispatch in place, no copy
                sink( p, frameSize_ );
                p += frameSize_;
                haveHeader_ = false;
            }else{
                const std::size_t still = (std::size_t)frameSize_ - buffer_.size();
                const std::size_t avail = (std::size_t)(end - p);
                const std::size_t take = avail < still ? avail : still;
                buffer_.insert( buffer_.end(), p, p + take );
                p += take;
                if( buffer_.size() == (std::size_t)frameSize_ ){
                    sink( buffer_.data(), frameSize_ );
                    buffer_.clear();
                    haveHeader_ = false;
                }
            }
        }
        return true;
    }

    // Discard any partial-frame state (e.g. after a connection reset).
    void Reset()
    {
        buffer_.clear();
        frameSize_ = 0;
        headerFill_ = 0;
        haveHeader_ = false;
    }

private:
    std::vector<char> buffer_;        // accumulates a payload that spans reads
    uint32_t maxFrameSize_;
    uint32_t frameSize_;              // payload size of the frame in progress
    char header_[OSC_STREAM_FRAME_HEADER_SIZE];
    uint32_t headerFill_;             // bytes of the header accumulated so far
    bool haveHeader_;                 // false: reading header; true: reading payload
};

} // namespace osctap


// Backwards-compatibility alias: this library was formerly named oscpack.
// Existing code that uses the oscpack:: namespace continues to compile.
namespace oscpack = osctap;

#endif /* INCLUDED_OSCTAP_OSCSTREAMFRAMING_H */
