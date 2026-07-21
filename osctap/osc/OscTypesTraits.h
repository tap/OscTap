#pragma once
#include <type_traits>

#include "OscReceivedElements.h"
namespace tap::osc {
    // Helpers to get the values.
    template <tap::osc::TypeTagValues>
    struct OscpackFunction;

    // For the ones that requires access to more than the type.
    struct object_required_trait {};

    // For the ones where the value is included in the type.
    struct object_useless_trait {};

    template <>
    struct OscpackFunction<tap::osc::INT32_TYPE_TAG> {
        using conversion_mode                         = object_required_trait;
        static const constexpr auto convert           = &tap::osc::ReceivedMessageArgument::AsInt32;
        static const constexpr auto convert_unchecked = &tap::osc::ReceivedMessageArgument::AsInt32Unchecked;
    };

    template <>
    struct OscpackFunction<tap::osc::INT64_TYPE_TAG> {
        using conversion_mode                         = object_required_trait;
        static const constexpr auto convert           = &tap::osc::ReceivedMessageArgument::AsInt64;
        static const constexpr auto convert_unchecked = &tap::osc::ReceivedMessageArgument::AsInt64Unchecked;
    };

    template <>
    struct OscpackFunction<tap::osc::FLOAT_TYPE_TAG> {
        using conversion_mode                         = object_required_trait;
        static const constexpr auto convert           = &tap::osc::ReceivedMessageArgument::AsFloat;
        static const constexpr auto convert_unchecked = &tap::osc::ReceivedMessageArgument::AsFloatUnchecked;
    };

    template <>
    struct OscpackFunction<tap::osc::DOUBLE_TYPE_TAG> {
        using conversion_mode                         = object_required_trait;
        static const constexpr auto convert           = &tap::osc::ReceivedMessageArgument::AsDouble;
        static const constexpr auto convert_unchecked = &tap::osc::ReceivedMessageArgument::AsDoubleUnchecked;
    };

    template <>
    struct OscpackFunction<tap::osc::CHAR_TYPE_TAG> {
        using conversion_mode                         = object_required_trait;
        static const constexpr auto convert           = &tap::osc::ReceivedMessageArgument::AsChar;
        static const constexpr auto convert_unchecked = &tap::osc::ReceivedMessageArgument::AsCharUnchecked;
    };

    template <>
    struct OscpackFunction<tap::osc::STRING_TYPE_TAG> {
        using conversion_mode                         = object_required_trait;
        static const constexpr auto convert           = &tap::osc::ReceivedMessageArgument::AsString;
        static const constexpr auto convert_unchecked = &tap::osc::ReceivedMessageArgument::AsStringUnchecked;
    };

    template <>
    struct OscpackFunction<tap::osc::SYMBOL_TYPE_TAG> {
        using conversion_mode                         = object_required_trait;
        static const constexpr auto convert           = &tap::osc::ReceivedMessageArgument::AsSymbol;
        static const constexpr auto convert_unchecked = &tap::osc::ReceivedMessageArgument::AsSymbolUnchecked;
    };

    template <>
    struct OscpackFunction<tap::osc::BLOB_TYPE_TAG> {
        using conversion_mode                         = object_required_trait;
        static const constexpr auto convert           = &tap::osc::ReceivedMessageArgument::AsBlob;
        static const constexpr auto convert_unchecked = &tap::osc::ReceivedMessageArgument::AsBlobUnchecked;
    };

    template <>
    struct OscpackFunction<tap::osc::TRUE_TYPE_TAG> {
        using conversion_mode = object_useless_trait;
        static bool                 true_fun() { return true; }
        static const constexpr auto convert           = &true_fun;
        static const constexpr auto convert_unchecked = &true_fun;
    };

    template <>
    struct OscpackFunction<tap::osc::FALSE_TYPE_TAG> {
        using conversion_mode = object_useless_trait;
        static bool                 false_fun() { return false; }
        static const constexpr auto convert           = &false_fun;
        static const constexpr auto convert_unchecked = &false_fun;
    };

    template <>
    struct OscpackFunction<tap::osc::INFINITUM_TYPE_TAG> {
        using conversion_mode = object_useless_trait;
        static InfinitumType        impulse_fun() { return {}; }
        static const constexpr auto convert           = &impulse_fun;
        static const constexpr auto convert_unchecked = &impulse_fun;
    };

    template <>
    struct OscpackFunction<tap::osc::NIL_TYPE_TAG> {
        using conversion_mode = object_useless_trait;
        static NilType              nil_fun() { return {}; }
        static const constexpr auto convert           = &nil_fun;
        static const constexpr auto convert_unchecked = &nil_fun;
    };

    template <tap::osc::TypeTagValues val>
    auto convert(
        tap::osc::ReceivedMessageArgument arg,
        std::enable_if_t<std::is_same<typename OscpackFunction<val>::conversion_mode, object_required_trait>::value>* =
            nullptr) {
        return (arg.*OscpackFunction<val>::convert)();
    }

    template <tap::osc::TypeTagValues val>
    auto convert(
        tap::osc::ReceivedMessageArgument,
        std::enable_if_t<std::is_same<typename OscpackFunction<val>::conversion_mode, object_useless_trait>::value>* =
            nullptr) {
        return (*OscpackFunction<val>::convert)();
    }

} // namespace tap::osc

// Backwards-compatibility aliases: the canonical namespace is tap::osc.
// The former names (osctap, and oscpack before it) keep compiling.
namespace osctap  = tap::osc;
namespace oscpack = tap::osc;
