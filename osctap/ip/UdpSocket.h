#pragma once
#include "AbstractUdpSocket.h"
#include <osctap/ip/NetworkingUtils.h>

#if defined(_WIN32)
#include "win32/UdpSocket.h"
#else
#include "posix/UdpSocket.h"
#endif

namespace osctap
{
namespace detail
{
#if defined(_WIN32)
using Implementation = osctap::win32::Implementation;
#else
using Implementation = osctap::posix::Implementation;
#endif
}

using UdpTransmitSocket = detail::UdpTransmitSocket<detail::Implementation>;
using UdpReceiveSocket = detail::UdpReceiveSocket<detail::Implementation>;
using UdpListeningReceiveSocket = detail::UdpListeningReceiveSocket<detail::Implementation>;
}

// Backwards-compatibility alias: this library was formerly named oscpack.
// Existing code that uses the oscpack:: namespace continues to compile.
namespace oscpack = osctap;
