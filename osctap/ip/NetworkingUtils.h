#pragma once

#if defined(_WIN32)
#include <osctap/ip/win32/NetworkingUtils.h>
#else
#include <osctap/ip/posix/NetworkingUtils.h>
#endif
