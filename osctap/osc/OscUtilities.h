#pragma once
#include <cstdint>
#include <cstring> // std::memcpy (bit_cast fallback)
#include <type_traits>

// std::bit_cast (C++20) gives a well-defined, constexpr type pun; on C++17 we
// fall back to std::memcpy, which is equally well-defined (just not constexpr).
// On C++20 the bit_cast paths are constexpr; on C++17 (memcpy fallback) they are
// merely inline. (constexpr implies inline, so the C++17 branch must spell out
// inline to keep these header functions ODR-safe.)
#if defined(__cpp_lib_bit_cast) && __cpp_lib_bit_cast >= 201806L
#include <bit>
#define OSCTAP_HAS_STD_BIT_CAST 1
#define OSCTAP_BITCAST_CONSTEXPR constexpr
#else
#define OSCTAP_HAS_STD_BIT_CAST 0
#define OSCTAP_BITCAST_CONSTEXPR inline
#endif

namespace osctap {

    // Reinterpret the bits of one trivially-copyable type as another of the same
    // size, without the undefined behaviour of union type-punning or pointer casts.
    template <class To, class From>
    OSCTAP_BITCAST_CONSTEXPR To BitCast(const From& src) noexcept {
        static_assert(sizeof(To) == sizeof(From), "BitCast requires equal sizes");
        static_assert(std::is_trivially_copyable<To>::value && std::is_trivially_copyable<From>::value,
                      "BitCast requires trivially-copyable types");
#if OSCTAP_HAS_STD_BIT_CAST
        return std::bit_cast<To>(src);
#else
        To dst;
        std::memcpy(&dst, &src, sizeof(To));
        return dst;
#endif
    }

    // OSC encodes integers and floats in big-endian (network) byte order. Assemble
    // and disassemble them byte-by-byte: this is endian-agnostic and free of the
    // strict-aliasing / misalignment UB that the old union + reinterpret_cast had.
    // (uint8_t() of a possibly-signed char yields the raw byte, modulo 256.)

    constexpr uint32_t LoadBigEndian32(const char* p) noexcept {
        return (uint32_t(uint8_t(p[0])) << 24) | (uint32_t(uint8_t(p[1])) << 16) | (uint32_t(uint8_t(p[2])) << 8)
               | uint32_t(uint8_t(p[3]));
    }

    constexpr uint64_t LoadBigEndian64(const char* p) noexcept {
        return (uint64_t(uint8_t(p[0])) << 56) | (uint64_t(uint8_t(p[1])) << 48) | (uint64_t(uint8_t(p[2])) << 40)
               | (uint64_t(uint8_t(p[3])) << 32) | (uint64_t(uint8_t(p[4])) << 24) | (uint64_t(uint8_t(p[5])) << 16)
               | (uint64_t(uint8_t(p[6])) << 8) | uint64_t(uint8_t(p[7]));
    }

    constexpr void StoreBigEndian32(char* p, uint32_t x) noexcept {
        p[0] = char(uint8_t(x >> 24));
        p[1] = char(uint8_t(x >> 16));
        p[2] = char(uint8_t(x >> 8));
        p[3] = char(uint8_t(x));
    }

    constexpr void StoreBigEndian64(char* p, uint64_t x) noexcept {
        p[0] = char(uint8_t(x >> 56));
        p[1] = char(uint8_t(x >> 48));
        p[2] = char(uint8_t(x >> 40));
        p[3] = char(uint8_t(x >> 32));
        p[4] = char(uint8_t(x >> 24));
        p[5] = char(uint8_t(x >> 16));
        p[6] = char(uint8_t(x >> 8));
        p[7] = char(uint8_t(x));
    }

    // round up to the next highest multiple of 4. unless x is already a multiple of 4
    constexpr uint32_t RoundUp4(uint32_t x) noexcept {
        return (x + 3) & ~((uint32_t)0x03);
    }

    OSCTAP_BITCAST_CONSTEXPR void FromInt32(char* p, int32_t x) {
        StoreBigEndian32(p, BitCast<uint32_t>(x));
    }
    constexpr void FromUInt32(char* p, uint32_t x) {
        StoreBigEndian32(p, x);
    }
    OSCTAP_BITCAST_CONSTEXPR void FromInt64(char* p, int64_t x) {
        StoreBigEndian64(p, BitCast<uint64_t>(x));
    }
    constexpr void FromUInt64(char* p, uint64_t x) {
        StoreBigEndian64(p, x);
    }

    // return the first 4 byte boundary after the end of a str4
    // be careful about calling this version if you don't know whether
    // the string is terminated correctly.
    inline const char* FindStr4End(const char* p) {
        if (p[0] == '\0') // special case for SuperCollider integer address pattern
            return p + 4;

        p += 3;

        while (*p)
            p += 4;

        return p + 1;
    }

    // return the first 4 byte boundary after the end of a str4
    // returns 0 if p == end or if the string is unterminated
    inline const char* FindStr4End(const char* p, const char* end) {
        if (p >= end)
            return 0;

        if (p[0] == '\0') // special case for SuperCollider integer address pattern
            return p + 4;

        p += 3;
        end -= 1;

        while (p < end && *p)
            p += 4;

        if (*p)
            return 0;
        else
            return p + 1;
    }

    OSCTAP_BITCAST_CONSTEXPR int32_t ToInt32(const char* p) {
        return BitCast<int32_t>(LoadBigEndian32(p));
    }
    constexpr uint32_t ToUInt32(const char* p) noexcept {
        return LoadBigEndian32(p);
    }
    OSCTAP_BITCAST_CONSTEXPR int64_t ToInt64(const char* p) {
        return BitCast<int64_t>(LoadBigEndian64(p));
    }
    constexpr uint64_t ToUInt64(const char* p) noexcept {
        return LoadBigEndian64(p);
    }
} // namespace osctap

// Backwards-compatibility alias: this library was formerly named oscpack.
// Existing code that uses the oscpack:: namespace continues to compile.
namespace oscpack = osctap;
