#pragma once
#include "osctap/ip/AbstractUdpSocket.h"
#include "osctap/ip/NetworkingUtils.h"

#if defined(_WIN32)
#include "osctap/ip/win32/UdpSocket.h"
#else
#include "osctap/ip/posix/UdpSocket.h"
#endif

namespace tap::osc {
    namespace detail {
#if defined(_WIN32)
        using Implementation = tap::osc::win32::Implementation;
#else
        using Implementation = tap::osc::posix::Implementation;
#endif
    } // namespace detail

    using UdpTransmitSocket         = detail::UdpTransmitSocket<detail::Implementation>;
    using UdpReceiveSocket          = detail::UdpReceiveSocket<detail::Implementation>;
    using UdpListeningReceiveSocket = detail::UdpListeningReceiveSocket<detail::Implementation>;
} // namespace tap::osc

// Backwards-compatibility aliases: the canonical namespace is tap::osc.
// The former names (osctap, and oscpack before it) keep compiling.
namespace osctap  = tap::osc;
namespace oscpack = tap::osc;
