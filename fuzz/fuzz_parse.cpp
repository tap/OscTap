/*
  oscpack / OscTap -- libFuzzer entry point for the packet-parsing path.

  This is the untrusted attack surface: arbitrary bytes arrive over the network
  and are parsed by ReceivedPacket / ReceivedBundle / ReceivedMessage. The
  harness walks a packet exactly as a consumer would -- reading the address,
  every type tag, and every argument's bytes (including blob payloads) -- so
  that any out-of-bounds read surfaces under AddressSanitizer.

  Build with real libFuzzer (preferred):
      clang++ -std=c++17 -g -O1 -I oscpack \
          -fsanitize=fuzzer,address,undefined fuzz/fuzz_parse.cpp -o fuzz_parse
      ./fuzz_parse fuzz/corpus

  Or, where the libFuzzer/ASan runtime is unavailable, link the standalone
  driver in fuzz/standalone_main.cpp (see fuzz/README.md).
*/
#include <cstddef>
#include <cstdint>
#include <exception>
#include <sstream>
#include <vector>

#include "osc/OscPrintReceivedElements.h"
#include "osc/OscReceivedElements.h"

using namespace oscpack;

static void WalkMessage(const ReceivedMessage& m) {
    (void)m.AddressPattern();
    if (m.AddressPatternIsUInt32())
        (void)m.AddressPatternAsUInt32();

    for (ReceivedMessage::const_iterator it = m.ArgumentsBegin(); it != m.ArgumentsEnd(); ++it) {
        const ReceivedMessageArgument& a = *it;
        switch (a.TypeTag()) {
        case INT32_TYPE_TAG:
            (void)a.AsInt32Unchecked();
            break;
        case FLOAT_TYPE_TAG:
            (void)a.AsFloatUnchecked();
            break;
        case CHAR_TYPE_TAG:
            (void)a.AsCharUnchecked();
            break;
        case RGBA_COLOR_TYPE_TAG:
            (void)a.AsRgbaColorUnchecked();
            break;
        case MIDI_MESSAGE_TYPE_TAG:
            (void)a.AsMidiMessageUnchecked();
            break;
        case INT64_TYPE_TAG:
            (void)a.AsInt64Unchecked();
            break;
        case TIME_TAG_TYPE_TAG:
            (void)a.AsTimeTagUnchecked();
            break;
        case DOUBLE_TYPE_TAG:
            (void)a.AsDoubleUnchecked();
            break;
        case STRING_TYPE_TAG:
            (void)a.AsStringUnchecked();
            break;
        case SYMBOL_TYPE_TAG:
            (void)a.AsSymbolUnchecked();
            break;
        case BLOB_TYPE_TAG: {
            const void*               data;
            osc_bundle_element_size_t size;
            a.AsBlobUnchecked(data, size);
            // Touch every blob byte so an out-of-bounds size is caught by ASan.
            const volatile char* p    = static_cast<const char*>(data);
            volatile char        sink = 0;
            for (osc_bundle_element_size_t i = 0; i < size; ++i)
                sink = p[i];
            (void)sink;
            break;
        }
        default:
            break; // T/F/N/I and array markers carry no argument data
        }
    }
}

static void WalkBundle(const ReceivedBundle& b) {
    (void)b.TimeTag();
    for (ReceivedBundle::const_iterator it = b.ElementsBegin(); it != b.ElementsEnd(); ++it) {
        if (it->IsBundle())
            WalkBundle(ReceivedBundle(*it));
        else
            WalkMessage(ReceivedMessage(*it));
    }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // OSC packets are a whole number of 4-byte words. Trim the tail so more
    // inputs reach the parser; the multiple-of-four rejection itself is already
    // covered by the unit tests.
    size -= (size & 3);
    if (size == 0)
        return 0;

    // Copy into an exactly-sized heap buffer: operator new yields max-aligned
    // storage (satisfying the big-endian word reads) and, crucially, ASan
    // places redzones immediately after `size`, so any read past the declared
    // packet length is flagged.
    std::vector<char> buffer(data, data + size);

    try {
        ReceivedPacket p(buffer.data(), (osc_bundle_element_size_t)size);
        if (p.IsBundle())
            WalkBundle(ReceivedBundle(p));
        else
            WalkMessage(ReceivedMessage(p));

        // Independently exercise the streaming printer, a separate consumer path.
        std::ostringstream oss;
        oss << p;
    }
    catch (const oscpack::Exception&) {
        // Expected: malformed input is rejected by design.
    }
    catch (const std::exception&) {
        // Tolerate std exceptions (e.g. bad_alloc) -- not a memory-safety finding.
    }

    return 0;
}
