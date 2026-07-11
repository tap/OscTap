#pragma once

#if defined(_WIN32)
#include "ip/win32/NetworkingUtils.h"
#else
#include "ip/posix/NetworkingUtils.h"
#endif
