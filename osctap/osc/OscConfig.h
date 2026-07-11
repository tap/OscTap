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
#ifndef INCLUDED_OSCTAP_OSCCONFIG_H
#define INCLUDED_OSCTAP_OSCCONFIG_H

/*
  OscTap build-configuration seam.

  This header centralises the knobs the Phase 2 "freestanding / embedded
  profile" relies on: whether C++ exceptions are available, and how the library
  should report an unrecoverable validation error when they are not. Hosted
  builds are unaffected -- OSCTAP_THROW expands to a plain `throw`, exactly as
  before, so the existing exception-based API and tests are unchanged.

  See docs/EMBEDDED_PICO2W.md for a worked embedded target (Raspberry Pi
  Pico 2W / RP2350) that builds the core with exceptions disabled.
*/

/* --- OSCTAP_HAS_EXCEPTIONS --------------------------------------------------
   Auto-detected from the compiler unless the user forces it. A build with
   -fno-exceptions (GCC/Clang) or /EHs-c- (MSVC) sets this to 0; everything
   else leaves the normal throwing behaviour in place. Force it explicitly by
   pre-defining OSCTAP_HAS_EXCEPTIONS to 0 or 1 on the command line. */
#ifndef OSCTAP_HAS_EXCEPTIONS
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || (defined(_MSC_VER) && defined(_CPPUNWIND))
#define OSCTAP_HAS_EXCEPTIONS 1
#else
#define OSCTAP_HAS_EXCEPTIONS 0
#endif
#endif

/* --- OSCTAP_FREESTANDING ----------------------------------------------------
   User-defined (e.g. -DOSCTAP_FREESTANDING) to drop the parts of the library
   that pull in hosted/heavyweight facilities -- <iostream> and the
   std::vector-backed OwnedMessage. The realtime parse/serialize core needs
   neither, so an embedded target can build the core without them. Defining
   OSCTAP_FREESTANDING does NOT by itself disable exceptions; pair it with
   -fno-exceptions on the toolchain (which flips OSCTAP_HAS_EXCEPTIONS to 0). */

/* --- OSCTAP_THROW -----------------------------------------------------------
   Raise an OscTap exception, or -- when exceptions are disabled -- report it
   to a fatal handler that does not return. Usage mirrors `throw`:

       OSCTAP_THROW( MalformedPacketException( "invalid packet size" ) );

   With exceptions enabled this is literally `throw EXC`. With exceptions
   disabled, validation failures are unrecoverable, so the default is to
   terminate via std::abort(). Embedded integrators that prefer to log + reset
   can route this anywhere by pre-defining OSCTAP_FATAL_HANDLER(whatCStr)
   before including any OscTap header -- it receives the exception's .what()
   string and must not return. */
#if OSCTAP_HAS_EXCEPTIONS
#define OSCTAP_THROW(EXC) throw EXC
#else
#if defined(OSCTAP_FATAL_HANDLER)
#define OSCTAP_THROW(EXC) (OSCTAP_FATAL_HANDLER((EXC).what()))
#else
#include <cstdlib> // std::abort
namespace osctap {
    namespace detail {
        // Default fatal handler used when exceptions are disabled and the integrator
        // has not supplied OSCTAP_FATAL_HANDLER. Marked [[noreturn]] so the compiler
        // knows the post-validation code is unreachable (no spurious "control reaches
        // end of non-void function" diagnostics at the former throw sites).
        [[noreturn]] inline void OscFatalError(const char* /*what*/) {
            std::abort();
        }
    } // namespace detail
} // namespace osctap
#define OSCTAP_THROW(EXC) (::osctap::detail::OscFatalError((EXC).what()))
#endif
#endif

#endif /* INCLUDED_OSCTAP_OSCCONFIG_H */
