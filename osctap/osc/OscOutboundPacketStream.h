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
#ifndef INCLUDED_OSCTAP_OSCOUTBOUNDPACKETSTREAM_H
#define INCLUDED_OSCTAP_OSCOUTBOUNDPACKETSTREAM_H

#include <cassert>
#include <cstddef> // ptrdiff_t
#include <cstring> // size_t
#include <cstring> // memcpy, memmove, strcpy, strlen
#ifndef OSCTAP_FREESTANDING
#include <iostream>
#include <string> // std::string operator<< overload (hosted convenience)
#endif
#include <string_view>

namespace osctap {
    using string_view = std::string_view;
}

#include "OscConfig.h" // OSCTAP_THROW, OSCTAP_FREESTANDING
#include "OscException.h"
#include "OscHostEndianness.h"
#include "OscTypes.h"
#include "OscUtilities.h"

#if defined(__WIN32__) || defined(WIN32) || defined(_WIN32)
#include <malloc.h> // for alloca
#else
// #include <alloca.h> // alloca on Linux (also OSX)
#include <stdlib.h> // alloca on OSX and FreeBSD (and Linux?)
#endif

namespace osctap {

    class OutOfBufferMemoryException : public Exception {
      public:
        OutOfBufferMemoryException(const char* w = "out of buffer memory")
            : Exception(w) {}
    };

    class BundleNotInProgressException : public Exception {
      public:
        BundleNotInProgressException(const char* w = "call to EndBundle when bundle is not in progress")
            : Exception(w) {}
    };

    class MessageInProgressException : public Exception {
      public:
        MessageInProgressException(const char* w = "opening or closing bundle or message while message is in progress")
            : Exception(w) {}
    };

    class MessageNotInProgressException : public Exception {
      public:
        MessageNotInProgressException(const char* w = "call to EndMessage when message is not in progress")
            : Exception(w) {}
    };

    struct BeginMessageN {
        explicit BeginMessageN(osctap::string_view str)
            : addressPattern{str} {}

        osctap::string_view addressPattern;
    };

    class OutboundPacketStream {
      public:
        OutboundPacketStream(char* buffer, std::size_t capacity)
            : data_(buffer)
            , end_(data_ + capacity)
            , typeTagsCurrent_(end_)
            , messageCursor_(data_)
            , argumentCurrent_(data_)
            , elementSizePtr_(0)
            , messageIsInProgress_(false) {
            // sanity check integer types declared in OscTypes.h
            // you'll need to fix OscTypes.h if any of these asserts fail
            assert(sizeof(int32_t) == 4);
            assert(sizeof(uint32_t) == 4);
            assert(sizeof(int64_t) == 8);
            assert(sizeof(uint64_t) == 8);
        }
        ~OutboundPacketStream() {}

        void Clear() {
            typeTagsCurrent_     = end_;
            messageCursor_       = data_;
            argumentCurrent_     = data_;
            elementSizePtr_      = 0;
            messageIsInProgress_ = false;
        }

        std::size_t Capacity() const { return end_ - data_; }

        // invariant: size() is valid even while building a message.
        std::size_t Size() const {
            std::size_t result = argumentCurrent_ - data_;
            if (IsMessageInProgress()) {
                // account for the length of the type tag string. the total type tag
                // includes an initial comma, plus at least one terminating \0
                result += RoundUp4(static_cast<uint32_t>((end_ - typeTagsCurrent_) + 2));
            }

            return result;
        }

        const char* Data() const { return data_; }

        // indicates that all messages have been closed with a matching EndMessage
        // and all bundles have been closed with a matching EndBundle
        bool IsReady() const { return (!IsMessageInProgress() && !IsBundleInProgress()); }

        bool IsMessageInProgress() const { return messageIsInProgress_; }
        bool IsBundleInProgress() const { return (elementSizePtr_ != 0); }

        template <typename T, typename std::enable_if_t<!std::is_same<char, std::remove_const_t<T>>::value>* = nullptr>
        OutboundPacketStream& operator<<(T* rhs) = delete;

        OutboundPacketStream& operator<<(BundleInitiator rhs) {
            if (IsMessageInProgress())
                OSCTAP_THROW(MessageInProgressException());

            CheckForAvailableBundleSpace();

            messageCursor_ = BeginElement(messageCursor_);

            std::memcpy(messageCursor_, "#bundle\0", 8);
            FromUInt64(messageCursor_ + 8, rhs.timeTag);

            messageCursor_ += 16;
            argumentCurrent_ = messageCursor_;

            return *this;
        }

        OutboundPacketStream& operator<<(BundleTerminator rhs) {
            (void)rhs;

            if (!IsBundleInProgress())
                OSCTAP_THROW(BundleNotInProgressException());
            if (IsMessageInProgress())
                OSCTAP_THROW(MessageInProgressException());

            EndElement(messageCursor_);

            return *this;
        }

        OutboundPacketStream& operator<<(BeginMessage rhs) {
            if (IsMessageInProgress())
                OSCTAP_THROW(MessageInProgressException());

            std::size_t rhsLength = std::strlen(rhs.addressPattern);
            CheckForAvailableMessageSpace(rhsLength);

            messageCursor_ = BeginElement(messageCursor_);

            // memcpy (not strcpy) of the known length incl. terminator: avoids the
            // MSVC C4996 'unsafe' deprecation and is a bounded copy.
            std::memcpy(messageCursor_, rhs.addressPattern, rhsLength + 1);
            messageCursor_ += rhsLength + 1;

            // zero pad to 4-byte boundary
            std::size_t i = rhsLength + 1;
            while (i & 0x3) {
                *messageCursor_++ = '\0';
                ++i;
            }

            argumentCurrent_ = messageCursor_;
            typeTagsCurrent_ = end_;

            messageIsInProgress_ = true;

            return *this;
        }
        OutboundPacketStream& operator<<(BeginMessageN rhs) {
            if (IsMessageInProgress())
                OSCTAP_THROW(MessageInProgressException());

            CheckForAvailableMessageSpace(rhs.addressPattern.size());

            messageCursor_ = BeginElement(messageCursor_);

            std::memcpy(messageCursor_, rhs.addressPattern.data(), rhs.addressPattern.size());

            messageCursor_ += rhs.addressPattern.size();
            *messageCursor_++ = '\0';

            // zero pad to 4-byte boundary
            std::size_t i = rhs.addressPattern.size() + 1;
            while (i & 0x3) {
                *messageCursor_++ = '\0';
                ++i;
            }

            argumentCurrent_ = messageCursor_;
            typeTagsCurrent_ = end_;

            messageIsInProgress_ = true;

            return *this;
        }
        OutboundPacketStream& operator<<(MessageTerminator rhs) {
            (void)rhs;

            if (!IsMessageInProgress())
                OSCTAP_THROW(MessageNotInProgressException());

            std::size_t typeTagsCount = end_ - typeTagsCurrent_;

            if (typeTagsCount) {
                char* tempTypeTags = (char*)alloca(typeTagsCount);
                std::memcpy(tempTypeTags, typeTagsCurrent_, typeTagsCount);

                // slot size includes comma and null terminator
                std::size_t typeTagSlotSize = RoundUp4(static_cast<uint32_t>(typeTagsCount + 2));

                std::size_t argumentsSize = argumentCurrent_ - messageCursor_;

                std::memmove(messageCursor_ + typeTagSlotSize, messageCursor_, argumentsSize);

                messageCursor_[0] = ',';
                // copy type tags in reverse (really forward) order
                for (std::size_t i = 0; i < typeTagsCount; ++i)
                    messageCursor_[i + 1] = tempTypeTags[(typeTagsCount - 1) - i];

                char* p = messageCursor_ + 1 + typeTagsCount;
                for (std::size_t i = 0; i < (typeTagSlotSize - (typeTagsCount + 1)); ++i)
                    *p++ = '\0';

                typeTagsCurrent_ = end_;

                // advance messageCursor_ for next message
                messageCursor_ += typeTagSlotSize + argumentsSize;
            }
            else {
                // send an empty type tags string
                std::memcpy(messageCursor_, ",\0\0\0", 4);

                // advance messageCursor_ for next message
                messageCursor_ += 4;
            }

            argumentCurrent_ = messageCursor_;

            EndElement(messageCursor_);

            messageIsInProgress_ = false;

            return *this;
        }

        OutboundPacketStream& operator<<(bool rhs) {
            CheckForAvailableArgumentSpace(0);

            *(--typeTagsCurrent_) = (char)((rhs) ? TRUE_TYPE_TAG : FALSE_TYPE_TAG);

            return *this;
        }

        OutboundPacketStream& operator<<(InfinitumType rhs) {
            (void)rhs;
            CheckForAvailableArgumentSpace(0);

            *(--typeTagsCurrent_) = INFINITUM_TYPE_TAG;

            return *this;
        }

        OutboundPacketStream& operator<<(NilType rhs) {
            (void)rhs;
            CheckForAvailableArgumentSpace(0);

            *(--typeTagsCurrent_) = NIL_TYPE_TAG;

            return *this;
        }

        OutboundPacketStream& operator<<(int32_t rhs) {
            CheckForAvailableArgumentSpace(4);

            *(--typeTagsCurrent_) = INT32_TYPE_TAG;
            FromInt32(argumentCurrent_, rhs);
            argumentCurrent_ += 4;

            return *this;
        }

        OutboundPacketStream& operator<<(float rhs) {
            CheckForAvailableArgumentSpace(4);

            *(--typeTagsCurrent_) = FLOAT_TYPE_TAG;
            FromUInt32(argumentCurrent_, BitCast<uint32_t>(rhs));
            argumentCurrent_ += 4;

            return *this;
        }
        OutboundPacketStream& operator<<(char rhs) {
            CheckForAvailableArgumentSpace(4);

            *(--typeTagsCurrent_) = CHAR_TYPE_TAG;
            FromInt32(argumentCurrent_, rhs);
            argumentCurrent_ += 4;

            return *this;
        }

        OutboundPacketStream& operator<<(const RgbaColor& rhs) {
            CheckForAvailableArgumentSpace(4);

            *(--typeTagsCurrent_) = RGBA_COLOR_TYPE_TAG;
            FromUInt32(argumentCurrent_, rhs);
            argumentCurrent_ += 4;

            return *this;
        }

        OutboundPacketStream& operator<<(const MidiMessage& rhs) {
            CheckForAvailableArgumentSpace(4);

            *(--typeTagsCurrent_) = MIDI_MESSAGE_TYPE_TAG;
            FromUInt32(argumentCurrent_, rhs);
            argumentCurrent_ += 4;

            return *this;
        }

        OutboundPacketStream& operator<<(int64_t rhs) {
            CheckForAvailableArgumentSpace(8);

            *(--typeTagsCurrent_) = INT64_TYPE_TAG;
            FromInt64(argumentCurrent_, rhs);
            argumentCurrent_ += 8;

            return *this;
        }

        OutboundPacketStream& operator<<(const TimeTag& rhs) {
            CheckForAvailableArgumentSpace(8);

            *(--typeTagsCurrent_) = TIME_TAG_TYPE_TAG;
            FromUInt64(argumentCurrent_, rhs);
            argumentCurrent_ += 8;

            return *this;
        }

        OutboundPacketStream& operator<<(double rhs) {
            CheckForAvailableArgumentSpace(8);

            *(--typeTagsCurrent_) = DOUBLE_TYPE_TAG;
            FromUInt64(argumentCurrent_, BitCast<uint64_t>(rhs));
            argumentCurrent_ += 8;

            return *this;
        }

        OutboundPacketStream& operator<<(osctap::string_view rhs) {
            CheckForAvailableArgumentSpace(RoundUp4(static_cast<uint32_t>(rhs.size() + 1)));

            *(--typeTagsCurrent_) = STRING_TYPE_TAG;
            if (!rhs.empty())
                std::memcpy(argumentCurrent_, rhs.data(), rhs.size());
            argumentCurrent_ += rhs.size();
            *argumentCurrent_++ = '\0';

            // zero pad to 4-byte boundary
            std::size_t i = rhs.size() + 1;
            while (i & 0x3) {
                *argumentCurrent_++ = '\0';
                ++i;
            }

            return *this;
        }

        // A runtime const char* would otherwise bind to operator<<(bool): the
        // standard pointer->bool conversion outranks the user-defined conversion to
        // string_view, so a C-string pointer was silently serialized as a boolean.
        // This overload makes a const char* serialize as an OSC string, as expected.
        // (String *literals* still match the more specialised const char(&)[N]
        // overload below; this catches decayed/runtime pointers.) Freestanding-safe:
        // forwards to the string_view overload, no heap.
        OutboundPacketStream& operator<<(const char* rhs) {
            operator<<(osctap::string_view(rhs));
            return *this;
        }

#ifndef OSCTAP_FREESTANDING
        // Hosted convenience: std::string pulls in <string> (and heap). The
        // freestanding profile omits it; pass const char*, a char array, or
        // osctap::string_view instead.
        OutboundPacketStream& operator<<(const std::string& rhs) {
            operator<<(osctap::string_view(rhs));
            return *this;
        }
#endif

        template <int N>
        OutboundPacketStream& operator<<(const char (&ref)[N]) {
            CheckForAvailableArgumentSpace(RoundUp4(N));

            *(--typeTagsCurrent_) = STRING_TYPE_TAG;
            std::memcpy(argumentCurrent_, ref, N);
            argumentCurrent_ += N; // already 0-terminated

            // zero pad to 4-byte boundary
            std::size_t i = N;
            while (i & 0x3) {
                *argumentCurrent_++ = '\0';
                ++i;
            }

            return *this;
        }

        OutboundPacketStream& operator<<(const Symbol& rhs) {
            CheckForAvailableArgumentSpace(RoundUp4(static_cast<uint32_t>(std::strlen(rhs) + 1)));

            *(--typeTagsCurrent_) = SYMBOL_TYPE_TAG;
            std::size_t rhsLength = std::strlen(rhs);
            // memcpy (not strcpy) of the known length incl. terminator: avoids the
            // MSVC C4996 'unsafe' deprecation and is a bounded copy.
            std::memcpy(argumentCurrent_, rhs, rhsLength + 1);
            argumentCurrent_ += rhsLength + 1;

            // zero pad to 4-byte boundary
            std::size_t i = rhsLength + 1;
            while (i & 0x3) {
                *argumentCurrent_++ = '\0';
                ++i;
            }

            return *this;
        }

        OutboundPacketStream& operator<<(const Blob& rhs) {
            CheckForAvailableArgumentSpace(4 + RoundUp4(rhs.size));

            *(--typeTagsCurrent_) = BLOB_TYPE_TAG;
            FromUInt32(argumentCurrent_, rhs.size);
            argumentCurrent_ += 4;

            std::memcpy(argumentCurrent_, rhs.data, rhs.size);
            argumentCurrent_ += rhs.size;

            // zero pad to 4-byte boundary
            unsigned long i = rhs.size;
            while (i & 0x3) {
                *argumentCurrent_++ = '\0';
                ++i;
            }

            return *this;
        }

        OutboundPacketStream& operator<<(const ArrayInitiator& rhs) {
            (void)rhs;
            CheckForAvailableArgumentSpace(0);

            *(--typeTagsCurrent_) = ARRAY_BEGIN_TYPE_TAG;

            return *this;
        }

        OutboundPacketStream& operator<<(const ArrayTerminator& rhs) {
            (void)rhs;
            CheckForAvailableArgumentSpace(0);

            *(--typeTagsCurrent_) = ARRAY_END_TYPE_TAG;

            return *this;
        }

      private:
        char* BeginElement(char* beginPtr) {
            if (elementSizePtr_ == 0) {
                elementSizePtr_ = data_;

                return beginPtr;
            }
            else {
                // store an offset to the old element size ptr in the element size slot
                // we store an offset rather than the actual pointer to be 64 bit clean.
                // (this temporary offset is overwritten with the real size in
                // EndElement; it is written and read back through the same byte order,
                // so it never appears on the wire.)
                FromUInt32(beginPtr, (uint32_t)(elementSizePtr_ - data_));

                elementSizePtr_ = beginPtr;

                return beginPtr + 4;
            }
        }
        void EndElement(char* endPtr) {
            assert(elementSizePtr_ != 0);

            if (elementSizePtr_ == data_) {
                elementSizePtr_ = 0;
            }
            else {
                // while building an element, an offset to the containing element's
                // size slot is stored in the elements size slot (or a ptr to data_
                // if there is no containing element). We retrieve that here
                char* previousElementSizePtr = data_ + ToUInt32(elementSizePtr_);

                // then we store the element size in the slot. note that the element
                // size does not include the size slot, hence the - 4 below.

                std::ptrdiff_t d = endPtr - elementSizePtr_;
                // assert( d >= 4 && d <= 0x7FFFFFFF ); // assume packets smaller than 2Gb

                uint32_t elementSize = static_cast<uint32_t>(d - 4);
                FromUInt32(elementSizePtr_, elementSize);

                // finally, we reset the element size ptr to the containing element
                elementSizePtr_ = previousElementSizePtr;
            }
        }

        bool ElementSizeSlotRequired() const { return (elementSizePtr_ != 0); }
        void CheckForAvailableBundleSpace() {
            std::size_t required = Size() + ((ElementSizeSlotRequired()) ? 4 : 0) + 16;

            if (required > Capacity())
                OSCTAP_THROW(OutOfBufferMemoryException());
        }
        void CheckForAvailableMessageSpace(std::size_t addressPatternSize) {
            // plus 4 for at least four bytes of type tag
            std::size_t required = Size() + ((ElementSizeSlotRequired()) ? 4 : 0)
                                   + RoundUp4(static_cast<uint32_t>(addressPatternSize + 1)) + 4;

            if (required > Capacity())
                OSCTAP_THROW(OutOfBufferMemoryException());
        }
        void CheckForAvailableArgumentSpace(std::size_t argumentLength) {
            // plus three for extra type tag, comma and null terminator
            std::size_t required = (argumentCurrent_ - data_) + argumentLength
                                   + RoundUp4(static_cast<uint32_t>((end_ - typeTagsCurrent_) + 3));

            if (required > Capacity())
                OSCTAP_THROW(OutOfBufferMemoryException());
        }

        char* const data_;
        char* const end_;

        char* typeTagsCurrent_; // stored in reverse order
        char* messageCursor_;
        char* argumentCurrent_;

        // elementSizePtr_ has two special values: 0 indicates that a bundle
        // isn't open, and elementSizePtr_==data_ indicates that a bundle is
        // open but that it doesn't have a size slot (ie the outermost bundle)
        char* elementSizePtr_;

        bool messageIsInProgress_;
    };

} // namespace osctap

// Backwards-compatibility alias: this library was formerly named oscpack.
// Existing code that uses the oscpack:: namespace continues to compile.
namespace oscpack = osctap;

#endif /* INCLUDED_OSCTAP_OSCOUTBOUNDPACKETSTREAM_H */
