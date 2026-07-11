#pragma once
#include "AbstractUdpSocket.h"
#include "ip/NetworkingUtils.h"

#if defined(_WIN32)
#include "win32/UdpSocket.h"
#else
#include "posix/UdpSocket.h"
#endif

namespace osctap {
    namespace detail {
#if defined(_WIN32)
        using Implementation = osctap::win32::Implementation;
#else
        using Implementation = osctap::posix::Implementation;
#endif
    } // namespace detail

    using UdpTransmitSocket         = detail::UdpTransmitSocket<detail::Implementation>;
    using UdpReceiveSocket          = detail::UdpReceiveSocket<detail::Implementation>;
    using UdpListeningReceiveSocket = detail::UdpListeningReceiveSocket<detail::Implementation>;
} // namespace osctap

// Backwards-compatibility alias: this library was formerly named oscpack.
// Existing code that uses the oscpack:: namespace continues to compile.
namespace oscpack = osctap;
