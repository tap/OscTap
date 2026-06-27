/*
    OscTap compatibility shim.

    Redirects the old <oscpack/ip/TcpSocket.h> include path to the new
    <osctap/ip/TcpSocket.h>. (TCP support is new in OscTap; the shim exists only
    so the deprecated prefix keeps working uniformly across the library.)

    Deprecated: prefer <osctap/ip/TcpSocket.h>. See ROADMAP.md / docs/STATUS.md.
*/
#include <osctap/ip/TcpSocket.h>
