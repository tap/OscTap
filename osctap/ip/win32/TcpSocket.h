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
#ifndef INCLUDED_OSCTAP_WIN32_TCPSOCKET_H
#define INCLUDED_OSCTAP_WIN32_TCPSOCKET_H

// Reuse the win32 socket includes, NetworkInitializer (WSAStartup), and the
// SockaddrFromIpEndpointName / IpEndpointNameFromSockaddr helpers from the UDP
// backend.
#include <atomic>
#include <map>

#include <ws2tcpip.h> // TCP_NODELAY, IPPROTO_TCP

#include "ip/IpEndpointName.h" // complete type before the helpers below use it
#include "ip/PacketListener.h"
#include "ip/win32/UdpSocket.h"
#include "osc/OscStreamFraming.h"

namespace tap::osc {
    namespace win32 {

        // Mirrors ip/posix/TcpSocket.h on Winsock. The connection-aware server uses
        // select() (Winsock supports select() over SOCKETs) with a self-connected UDP
        // "break" socket standing in for the posix self-pipe, so AsynchronousBreak()
        // works from another thread / signal handler.
        //
        // NOTE: this backend is built by the windows-latest CI legs and cross-compiled +
        // link-checked with MinGW, but -- unlike the posix backend -- it is not yet
        // runtime-tested in CI (no Windows runner). The posix backend is the
        // runtime-verified reference.

        class TcpTransmitSocket {
          public:
            explicit TcpTransmitSocket(const IpEndpointName& remoteEndpoint) {
                NetworkInitializer::instance();

                socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
                if (socket_ == INVALID_SOCKET)
                    throw std::runtime_error("unable to create tcp socket\n");

                int one = 1;
                setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));

                struct sockaddr_in addr;
                SockaddrFromIpEndpointName(addr, remoteEndpoint);
                if (::connect(socket_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                    closesocket(socket_);
                    socket_ = INVALID_SOCKET;
                    throw std::runtime_error("unable to connect tcp socket\n");
                }
            }

            ~TcpTransmitSocket() {
                if (socket_ != INVALID_SOCKET)
                    closesocket(socket_);
            }

            TcpTransmitSocket(const TcpTransmitSocket&)            = delete;
            TcpTransmitSocket& operator=(const TcpTransmitSocket&) = delete;

            void Send(const char* data, std::size_t size) {
                char header[OSC_STREAM_FRAME_HEADER_SIZE];
                WriteOscStreamFrameHeader(header, (uint32_t)size);
                SendAll(header, OSC_STREAM_FRAME_HEADER_SIZE);
                SendAll(data, size);
            }

            SOCKET Socket() const { return socket_; }

          private:
            void SendAll(const char* p, std::size_t n) {
                std::size_t sent = 0;
                while (sent < n) {
                    int r = ::send(socket_, p + sent, (int)(n - sent), 0);
                    if (r == SOCKET_ERROR)
                        throw std::runtime_error("tcp send failed\n");
                    sent += (std::size_t)r;
                }
            }

            SOCKET socket_ = INVALID_SOCKET;
        };

        class TcpListeningReceiveSocket {
            struct Connection {
                IpEndpointName    peer;
                OscStreamDeframer deframer;
                Connection(const IpEndpointName& p, uint32_t maxFrame)
                    : peer(p)
                    , deframer(maxFrame) {}
            };

          public:
            TcpListeningReceiveSocket(const IpEndpointName& localEndpoint, PacketListener* listener,
                                      uint32_t maxFrameSize = OSC_DEFAULT_MAX_FRAME_SIZE)
                : listener_(listener)
                , maxFrameSize_(maxFrameSize) {
                NetworkInitializer::instance();
                CreateBreakSocket();

                listenSocket_ = ::socket(AF_INET, SOCK_STREAM, 0);
                if (listenSocket_ == INVALID_SOCKET) {
                    closesocket(breakSocket_);
                    throw std::runtime_error("unable to create tcp socket\n");
                }

                int reuse = 1;
                setsockopt(listenSocket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

                struct sockaddr_in addr;
                SockaddrFromIpEndpointName(addr, localEndpoint);
                if (::bind(listenSocket_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                    Cleanup();
                    throw std::runtime_error("unable to bind tcp socket\n");
                }
                if (::listen(listenSocket_, SOMAXCONN) == SOCKET_ERROR) {
                    Cleanup();
                    throw std::runtime_error("unable to listen on tcp socket\n");
                }
            }

            ~TcpListeningReceiveSocket() { Cleanup(); }

            TcpListeningReceiveSocket(const TcpListeningReceiveSocket&)            = delete;
            TcpListeningReceiveSocket& operator=(const TcpListeningReceiveSocket&) = delete;

            IpEndpointName LocalEndpointFor(const IpEndpointName& requested) const {
                struct sockaddr_in addr;
                socklen_t          len = sizeof(addr);
                if (getsockname(listenSocket_, (struct sockaddr*)&addr, &len) == 0)
                    return IpEndpointName(requested.address, ntohs(addr.sin_port));
                return requested;
            }

            void Run() {
                break_ = false;
                char buf[4096];

                while (!break_) {
                    fd_set readfds;
                    FD_ZERO(&readfds);
                    FD_SET(listenSocket_, &readfds);
                    FD_SET(breakSocket_, &readfds);
                    for (const auto& kv : connections_)
                        FD_SET(kv.first, &readfds);

                    if (select(0, &readfds, 0, 0, 0) == SOCKET_ERROR) {
                        if (break_)
                            break;
                        throw std::runtime_error("select failed\n");
                    }

                    if (FD_ISSET(breakSocket_, &readfds)) {
                        char c;
                        recv(breakSocket_, &c, 1, 0);
                    }
                    if (break_)
                        break;

                    if (FD_ISSET(listenSocket_, &readfds))
                        AcceptConnection();

                    std::vector<SOCKET> ready;
                    for (const auto& kv : connections_)
                        if (FD_ISSET(kv.first, &readfds))
                            ready.push_back(kv.first);

                    for (SOCKET fd : ready) {
                        ServiceConnection(fd, buf, sizeof(buf));
                        if (break_)
                            break;
                    }
                }
            }

            void Break() { break_ = true; }

            void AsynchronousBreak() {
                break_ = true;
                send(breakSocket_, "!", 1, 0); // wake select()
            }

            SOCKET Socket() const { return listenSocket_; }

          private:
            void CreateBreakSocket() {
                // A loopback UDP socket connected to itself: writing a byte to it wakes the
                // select() loop (the Winsock analogue of the posix self-pipe).
                breakSocket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
                if (breakSocket_ == INVALID_SOCKET)
                    throw std::runtime_error("creation of asynchronous break socket failed\n");

                struct sockaddr_in addr;
                std::memset(&addr, 0, sizeof(addr));
                addr.sin_family      = AF_INET;
                addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                addr.sin_port        = 0;

                // bind -> read back the assigned port -> connect to self. If any step
                // fails, AsynchronousBreak() could never wake Run() (it would hang in
                // select()), so treat it as fatal.
                socklen_t len = sizeof(addr);
                if (bind(breakSocket_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR
                    || getsockname(breakSocket_, (struct sockaddr*)&addr, &len) == SOCKET_ERROR
                    || connect(breakSocket_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
                    closesocket(breakSocket_);
                    breakSocket_ = INVALID_SOCKET;
                    throw std::runtime_error("setup of asynchronous break socket failed\n");
                }
            }

            void AcceptConnection() {
                struct sockaddr_in peerAddr;
                socklen_t          len  = sizeof(peerAddr);
                SOCKET             conn = ::accept(listenSocket_, (struct sockaddr*)&peerAddr, &len);
                if (conn == INVALID_SOCKET)
                    return;

                // Winsock select() works over an fd_set array bounded by FD_SETSIZE
                // (default 64). Beyond it, FD_SET silently drops sockets and those
                // connections would stall forever -- so refuse new connections at the
                // limit instead. (v1 targets a handful of connections; high connection
                // counts are a future poll/epoll concern -- see issue #14.)
                if (connections_.size() + 2 >= FD_SETSIZE) {
                    closesocket(conn);
                    return;
                }

                int one = 1;
                setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
                connections_.emplace(std::piecewise_construct, std::forward_as_tuple(conn),
                                     std::forward_as_tuple(IpEndpointNameFromSockaddr(peerAddr), maxFrameSize_));
            }

            void ServiceConnection(SOCKET fd, char* buf, std::size_t bufSize) {
                auto it = connections_.find(fd);
                if (it == connections_.end())
                    return;

                int n = ::recv(fd, buf, (int)bufSize, 0);
                if (n <= 0) { // 0 = peer closed; SOCKET_ERROR -> drop
                    CloseConnection(it);
                    return;
                }

                const bool ok =
                    it->second.deframer.Consume(buf, (std::size_t)n, [&](const char* packet, uint32_t size) {
                        listener_->ProcessPacket(packet, (int)size, it->second.peer);
                    });

                if (!ok)
                    CloseConnection(it);
            }

            void CloseConnection(std::map<SOCKET, Connection>::iterator it) {
                closesocket(it->first);
                connections_.erase(it);
            }

            void Cleanup() {
                for (auto& kv : connections_)
                    closesocket(kv.first);
                connections_.clear();
                if (listenSocket_ != INVALID_SOCKET) {
                    closesocket(listenSocket_);
                    listenSocket_ = INVALID_SOCKET;
                }
                if (breakSocket_ != INVALID_SOCKET) {
                    closesocket(breakSocket_);
                    breakSocket_ = INVALID_SOCKET;
                }
            }

            SOCKET                       listenSocket_ = INVALID_SOCKET;
            SOCKET                       breakSocket_  = INVALID_SOCKET;
            PacketListener*              listener_;
            uint32_t                     maxFrameSize_;
            std::atomic_bool             break_{false};
            std::map<SOCKET, Connection> connections_;
        };

    } // namespace win32
} // namespace tap::osc

#endif /* INCLUDED_OSCTAP_WIN32_TCPSOCKET_H */
