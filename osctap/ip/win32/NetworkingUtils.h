/*
  oscpack -- Open Sound Control (OSC) packet manipulation library
    http://www.rossbencina.com/code/oscpack

    Copyright (c) 2004-2013 Ross Bencina <rossb@audiomulch.com>

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files
  (the "Software"), to deal in the Software without restriction,
  including without limitation the rights to use, copy, modify, merge,
  publish, distribute, sublicense, and/or sell copies of the Software,
  and to permit persons to whom the Software is furnished to do so,
  subject to the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
  ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
  CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/*
  The text above constitutes the entire oscpack license; however,
  the oscpack developer(s) also make the following non-binding requests:

  Any person wishing to distribute modifications to the Software is
  requested to send the modifications to the original developer so that
  they can be incorporated into the canonical version. It is also
  requested that these non-binding requests be included whenever the
  above license is reproduced.
*/
#include <osctap/ip/NetworkingUtils.h>

#include <winsock2.h>   // this must come first to prevent errors with MSVC7
#include <ws2tcpip.h>   // getaddrinfo / freeaddrinfo
#include <windows.h>

#include <cstring>

namespace osctap
{
class NetworkInitializer
{
  public:
    static const NetworkInitializer& instance()
    {
      static const NetworkInitializer ne;
      return ne;
    }

  private:
    NetworkInitializer()
    {
      WSAData wsaData;
      WSAStartup(MAKEWORD(1, 1), &wsaData);
    }

    ~NetworkInitializer()
    {
      WSACleanup();
    }
};

inline unsigned long GetHostByName( const char *name )
{
  NetworkInitializer::instance();

  unsigned long result = 0;

  // getaddrinfo replaces the deprecated gethostbyname (MSVC C4996); mirrors the
  // posix backend.
  struct addrinfo hints;
  std::memset( &hints, 0, sizeof(hints) );
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;

  struct addrinfo *ai = nullptr;
  if( getaddrinfo( name, nullptr, &hints, &ai ) == 0 && ai ){
    auto *remote = reinterpret_cast<struct sockaddr_in *>( ai->ai_addr );
    result = ntohl( remote->sin_addr.s_addr );
  }
  if( ai )
    freeaddrinfo( ai );

  return result;
}
}

// Backwards-compatibility alias: this library was formerly named oscpack.
// Existing code that uses the oscpack:: namespace continues to compile.
namespace oscpack = osctap;
