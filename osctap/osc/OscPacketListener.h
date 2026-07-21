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
#ifndef INCLUDED_OSCTAP_OSCPACKETLISTENER_H
#define INCLUDED_OSCTAP_OSCPACKETLISTENER_H

#include "../ip/PacketListener.h"
#include "OscReceivedElements.h"

namespace tap::osc {

    class OscPacketListener : public PacketListener {
      public:
        // Maximum bundle nesting depth accepted by the default ProcessBundle()
        // implementation. Bundles nested deeper than this are ignored, to bound
        // stack usage when processing untrusted packets (a deeply-nested bundle is
        // otherwise valid OSC and would recurse once per level). Configurable for
        // the rare application that legitimately nests deeper.
        static const unsigned int DEFAULT_MAX_BUNDLE_NESTING_DEPTH = 64;

        void         SetMaxBundleNestingDepth(unsigned int depth) { maxBundleNestingDepth_ = depth; }
        unsigned int MaxBundleNestingDepth() const { return maxBundleNestingDepth_; }

      protected:
        virtual void ProcessBundle(const tap::osc::ReceivedBundle& b, const IpEndpointName& remoteEndpoint) {
            // ignore bundle time tag for now

            // Bound recursion depth so a deeply-nested bundle from an untrusted
            // sender cannot exhaust the stack. The guard restores the depth on the
            // way out even if ProcessMessage() or element construction throws.
            if (bundleNestingDepth_ >= maxBundleNestingDepth_)
                return;

            struct DepthGuard {
                unsigned int& depth;
                explicit DepthGuard(unsigned int& d)
                    : depth(d) {
                    ++depth;
                }
                ~DepthGuard() { --depth; }
            } depthGuard(bundleNestingDepth_);

            for (ReceivedBundle::const_iterator i = b.ElementsBegin(); i != b.ElementsEnd(); ++i) {
                if (i->IsBundle())
                    ProcessBundle(ReceivedBundle(*i), remoteEndpoint);
                else
                    ProcessMessage(ReceivedMessage(*i), remoteEndpoint);
            }
        }

        virtual void ProcessMessage(const tap::osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint) = 0;

      public:
        void ProcessPacket(const char* data, int size, const IpEndpointName& remoteEndpoint) override {
            tap::osc::ReceivedPacket p(data, size);
            if (p.IsBundle())
                ProcessBundle(ReceivedBundle(p), remoteEndpoint);
            else
                ProcessMessage(ReceivedMessage(p), remoteEndpoint);
        }

      private:
        unsigned int bundleNestingDepth_    = 0;
        unsigned int maxBundleNestingDepth_ = DEFAULT_MAX_BUNDLE_NESTING_DEPTH;
    };

} // namespace tap::osc

// Backwards-compatibility aliases: the canonical namespace is tap::osc.
// The former names (osctap, and oscpack before it) keep compiling.
namespace osctap  = tap::osc;
namespace oscpack = tap::osc;

#endif /* INCLUDED_OSCTAP_OSCPACKETLISTENER_H */
