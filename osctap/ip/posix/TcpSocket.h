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
#ifndef INCLUDED_OSCTAP_POSIX_TCPSOCKET_H
#define INCLUDED_OSCTAP_POSIX_TCPSOCKET_H

// Reuse the posix socket includes and the SockaddrFromIpEndpointName /
// IpEndpointNameFromSockaddr helpers defined in the UDP backend.
#include <osctap/ip/IpEndpointName.h> // complete type before the helpers below use it
#include <osctap/ip/posix/UdpSocket.h>
#include <osctap/ip/PacketListener.h>
#include <osctap/osc/OscStreamFraming.h>

#include <netinet/tcp.h> // TCP_NODELAY
#include <cerrno>        // errno (don't rely on transitive includes)
#include <map>
#include <vector>

namespace osctap
{
namespace posix
{

// ---------------------------------------------------------------------------
// TcpTransmitSocket -- connect to a remote OSC-over-TCP server and send packets.
//
// Each Send() writes one length-prefixed frame (4-byte big-endian count +
// payload), looping over partial writes (a TCP send() may transfer fewer bytes
// than requested). TCP_NODELAY is enabled (Nagle off) -- OSC over TCP without it
// is a classic latency footgun.
// ---------------------------------------------------------------------------
class TcpTransmitSocket
{
public:
    explicit TcpTransmitSocket( const IpEndpointName& remoteEndpoint )
    {
        socket_ = ::socket( AF_INET, SOCK_STREAM, 0 );
        if( socket_ == -1 )
            throw std::runtime_error( "unable to create tcp socket\n" );

#ifdef SO_NOSIGPIPE
        int noSigpipe = 1; // macOS / BSD: suppress SIGPIPE on send to a closed peer
        setsockopt( socket_, SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe) );
#endif
        int one = 1;
        setsockopt( socket_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one) );

        struct sockaddr_in addr;
        SockaddrFromIpEndpointName( addr, remoteEndpoint );
        if( ::connect( socket_, (struct sockaddr*)&addr, sizeof(addr) ) < 0 ){
            ::close( socket_ );
            socket_ = -1;
            throw std::runtime_error( "unable to connect tcp socket\n" );
        }
    }

    ~TcpTransmitSocket() { if( socket_ != -1 ) ::close( socket_ ); }

    TcpTransmitSocket( const TcpTransmitSocket& ) = delete;
    TcpTransmitSocket& operator=( const TcpTransmitSocket& ) = delete;

    // Send one complete OSC packet, length-prefixed. Blocks until fully written.
    void Send( const char* data, std::size_t size )
    {
        char header[OSC_STREAM_FRAME_HEADER_SIZE];
        WriteOscStreamFrameHeader( header, (uint32_t)size );
        SendAll( header, OSC_STREAM_FRAME_HEADER_SIZE );
        SendAll( data, size );
    }

    int Socket() const { return socket_; }

private:
    void SendAll( const char* p, std::size_t n )
    {
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL; // Linux: don't raise SIGPIPE
#else
        const int flags = 0;
#endif
        std::size_t sent = 0;
        while( sent < n ){
            ssize_t r = ::send( socket_, p + sent, n - sent, flags );
            if( r < 0 ){
                if( errno == EINTR ) continue;
                throw std::runtime_error( "tcp send failed\n" );
            }
            sent += (std::size_t)r;
        }
    }

    int socket_ = -1;
};


// ---------------------------------------------------------------------------
// TcpListeningReceiveSocket -- listen for OSC-over-TCP clients and dispatch each
// complete packet to a PacketListener.
//
// Single-threaded, select()-based, and connection-aware: it accept()s any number
// of clients and keeps a per-connection OscStreamDeframer (each connection's byte
// stream reassembles independently). Run() blocks; Break()/AsynchronousBreak()
// stop it (the latter via a self-pipe, so it works from another thread or a
// signal handler -- mirroring the UDP multiplexer).
// ---------------------------------------------------------------------------
class TcpListeningReceiveSocket
{
    struct Connection
    {
        IpEndpointName peer;
        OscStreamDeframer deframer;
        Connection( const IpEndpointName& p, uint32_t maxFrame )
            : peer( p ), deframer( maxFrame ) {}
    };

public:
    TcpListeningReceiveSocket( const IpEndpointName& localEndpoint, PacketListener* listener,
                               uint32_t maxFrameSize = OSC_DEFAULT_MAX_FRAME_SIZE )
        : listener_( listener ), maxFrameSize_( maxFrameSize )
    {
        if( pipe( breakPipe_ ) != 0 )
            throw std::runtime_error( "creation of asynchronous break pipes failed\n" );

        listenSocket_ = ::socket( AF_INET, SOCK_STREAM, 0 );
        if( listenSocket_ == -1 ){
            close( breakPipe_[0] ); close( breakPipe_[1] );
            throw std::runtime_error( "unable to create tcp socket\n" );
        }

        int reuse = 1;
        setsockopt( listenSocket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse) );

        struct sockaddr_in addr;
        SockaddrFromIpEndpointName( addr, localEndpoint );
        if( ::bind( listenSocket_, (struct sockaddr*)&addr, sizeof(addr) ) < 0 ){
            Cleanup();
            throw std::runtime_error( "unable to bind tcp socket\n" );
        }
        if( ::listen( listenSocket_, SOMAXCONN ) < 0 ){
            Cleanup();
            throw std::runtime_error( "unable to listen on tcp socket\n" );
        }
    }

    ~TcpListeningReceiveSocket() { Cleanup(); }

    TcpListeningReceiveSocket( const TcpListeningReceiveSocket& ) = delete;
    TcpListeningReceiveSocket& operator=( const TcpListeningReceiveSocket& ) = delete;

    // The bound local endpoint (resolves the OS-assigned port when bound to 0).
    IpEndpointName LocalEndpointFor( const IpEndpointName& requested ) const
    {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        if( getsockname( listenSocket_, (struct sockaddr*)&addr, &len ) == 0 )
            return IpEndpointName( requested.address, ntohs( addr.sin_port ) );
        return requested;
    }

    void Run()
    {
        break_ = false;
        char buf[4096];

        while( !break_ ){
            fd_set readfds;
            FD_ZERO( &readfds );
            FD_SET( listenSocket_, &readfds );
            FD_SET( breakPipe_[0], &readfds );
            int fdmax = listenSocket_ > breakPipe_[0] ? listenSocket_ : breakPipe_[0];
            for( const auto& kv : connections_ ){
                FD_SET( kv.first, &readfds );
                if( kv.first > fdmax ) fdmax = kv.first;
            }

            if( select( fdmax + 1, &readfds, 0, 0, 0 ) < 0 ){
                if( break_ ) break;
                if( errno == EINTR ) continue;
                throw std::runtime_error( "select failed\n" );
            }

            if( FD_ISSET( breakPipe_[0], &readfds ) ){
                char c; ssize_t r = read( breakPipe_[0], &c, 1 ); (void)r;
            }
            if( break_ ) break;

            if( FD_ISSET( listenSocket_, &readfds ) )
                AcceptConnection();

            // Collect ready connection fds first; processing may erase entries.
            std::vector<int> ready;
            for( const auto& kv : connections_ )
                if( FD_ISSET( kv.first, &readfds ) ) ready.push_back( kv.first );

            for( int fd : ready ){
                ServiceConnection( fd, buf, sizeof(buf) );
                if( break_ ) break;
            }
        }
    }

    void Break() { break_ = true; }

    void AsynchronousBreak()
    {
        break_ = true;
        ssize_t r = write( breakPipe_[1], "!", 1 ); (void)r;
    }

    int Socket() const { return listenSocket_; }

private:
    void AcceptConnection()
    {
        struct sockaddr_in peerAddr;
        socklen_t len = sizeof(peerAddr);
        int conn = ::accept( listenSocket_, (struct sockaddr*)&peerAddr, &len );
        if( conn == -1 )
            return;
        int one = 1;
        setsockopt( conn, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one) );
        connections_.emplace( std::piecewise_construct,
            std::forward_as_tuple( conn ),
            std::forward_as_tuple( IpEndpointNameFromSockaddr( peerAddr ), maxFrameSize_ ) );
    }

    void ServiceConnection( int fd, char* buf, std::size_t bufSize )
    {
        auto it = connections_.find( fd );
        if( it == connections_.end() )
            return;

        ssize_t n = ::recv( fd, buf, bufSize, 0 );
        if( n <= 0 ){ // 0 = peer closed; <0 = error -> drop the connection
            CloseConnection( it );
            return;
        }

        // Reassemble and dispatch every complete packet in this read. The sink
        // runs synchronously, so `it` (and its peer) stays valid throughout.
        const bool ok = it->second.deframer.Consume( buf, (std::size_t)n,
            [&]( const char* packet, uint32_t size ){
                listener_->ProcessPacket( packet, (int)size, it->second.peer );
            } );

        if( !ok ) // a frame exceeded maxFrameSize -> protocol violation
            CloseConnection( it );
    }

    void CloseConnection( std::map<int, Connection>::iterator it )
    {
        ::close( it->first );
        connections_.erase( it );
    }

    void Cleanup()
    {
        for( auto& kv : connections_ )
            ::close( kv.first );
        connections_.clear();
        if( listenSocket_ != -1 ){ ::close( listenSocket_ ); listenSocket_ = -1; }
        close( breakPipe_[0] );
        close( breakPipe_[1] );
    }

    int listenSocket_ = -1;
    PacketListener* listener_;
    uint32_t maxFrameSize_;
    std::atomic_bool break_{ false };
    int breakPipe_[2];
    std::map<int, Connection> connections_;
};

} // namespace posix
} // namespace osctap

#endif /* INCLUDED_OSCTAP_POSIX_TCPSOCKET_H */
