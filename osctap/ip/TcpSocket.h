#ifndef INCLUDED_OSCTAP_TCPSOCKET_H
#define INCLUDED_OSCTAP_TCPSOCKET_H

// Public OSC-over-TCP socket types. Length-prefix framing (see
// osc/OscStreamFraming.h); the platform backends mirror the UDP ones.
//
//   TcpTransmitSocket          -- client: connect + Send(packet, size)
//   TcpListeningReceiveSocket  -- server: accept N clients, dispatch each
//                                 complete packet to a PacketListener
//
// Unlike the UDP facade (which templates one class over a per-platform
// Implementation), the TCP types are concrete per-platform classes selected here
// by `using`. v1 scope: length-prefix framing, single-threaded multi-connection
// server, TCP_NODELAY, frame-size cap. SLIP/TLS/WebSocket/epoll are deferred.

#if defined(_WIN32)
#include "win32/TcpSocket.h"
#else
#include "posix/TcpSocket.h"
#endif

namespace osctap
{
#if defined(_WIN32)
using TcpTransmitSocket = win32::TcpTransmitSocket;
using TcpListeningReceiveSocket = win32::TcpListeningReceiveSocket;
#else
using TcpTransmitSocket = posix::TcpTransmitSocket;
using TcpListeningReceiveSocket = posix::TcpListeningReceiveSocket;
#endif
}

// Backwards-compatibility alias: this library was formerly named oscpack.
namespace oscpack = osctap;

#endif /* INCLUDED_OSCTAP_TCPSOCKET_H */
