/*
    OscTap compatibility shim.

    The library directory and public include prefix moved from <oscpack/...>
    to <osctap/...> (the namespace likewise moved oscpack -> osctap, kept as a
    deprecated alias). This header redirects the old include path to the new one
    so existing <oscpack/ip/posix/UdpSocket.h> consumers keep compiling unchanged.

    Deprecated: prefer <osctap/ip/posix/UdpSocket.h>. See ROADMAP.md / docs/STATUS.md.
*/
#include <osctap/ip/posix/UdpSocket.h>
