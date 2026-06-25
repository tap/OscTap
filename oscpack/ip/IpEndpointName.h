/*
    OscTap compatibility shim.

    The library directory and public include prefix moved from <oscpack/...>
    to <osctap/...> (the namespace likewise moved oscpack -> osctap, kept as a
    deprecated alias). This header redirects the old include path to the new one
    so existing <oscpack/ip/IpEndpointName.h> consumers keep compiling unchanged.

    Deprecated: prefer <osctap/ip/IpEndpointName.h>. See ROADMAP.md / docs/STATUS.md.
*/
#include <osctap/ip/IpEndpointName.h>
