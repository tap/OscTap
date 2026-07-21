#pragma once
#include <iostream>

#include "OscReceivedElements.h"

namespace tap::osc {

    template <typename Stream>
    auto& debug(Stream& s, const ReceivedMessage& mess) {
        s << mess.AddressPattern() << " ";
        for (auto arg : mess) {
            if (arg.IsString()) {
                s << arg.AsString() << " ";
            }
            else if (arg.IsInt32()) {
                s << arg.AsInt32() << " ";
            }
            else if (arg.IsFloat()) {
                s << arg.AsFloat() << " ";
            }
            else if (arg.IsBool()) {
                s << arg.AsBool() << " ";
            }
            else if (arg.IsChar()) {
                s << arg.AsChar() << " ";
            }
            else if (arg.IsInt64()) {
                s << arg.AsInt64() << " ";
            }
            else if (arg.IsDouble()) {
                s << arg.AsDouble() << " ";
            }
        }

        return s;
    }
} // namespace tap::osc

// Backwards-compatibility aliases: the canonical namespace is tap::osc.
// The former names (osctap, and oscpack before it) keep compiling.
namespace osctap  = tap::osc;
namespace oscpack = tap::osc;
