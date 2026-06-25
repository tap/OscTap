#pragma once
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
namespace osctap
{
    // in general NetworkInitializer is only used internally, but if you're
    // application creates multiple sockets from different threads at runtime you
    // should instantiate one of these in main just to make sure the networking
    // layer is initialized.
    class NetworkInitializer {
    public:
        NetworkInitializer() {}
        ~NetworkInitializer() {}
    };

    // return ip address of host name in host byte order
    inline unsigned long GetHostByName(const char *name)
    {
      unsigned long result = 0;

      addrinfo hints = {};
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_DGRAM;
      hints.ai_protocol = IPPROTO_UDP;

      addrinfo* ai{};
      const int err = getaddrinfo(name, nullptr, &hints, &ai);

      if (err != 0)
      {
        freeaddrinfo(ai);
        return 0;
      }

      if(ai)
      {
        auto remote = reinterpret_cast<struct sockaddr_in *>(ai->ai_addr);
        result = remote->sin_addr.s_addr;
        result = ntohl(result);

        freeaddrinfo(ai);
      }
      return result;
    }
}

// Backwards-compatibility alias: this library was formerly named oscpack.
// Existing code that uses the oscpack:: namespace continues to compile.
namespace oscpack = osctap;
