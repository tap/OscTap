#pragma once
#include "OscReceivedElements.h"

#include <type_traits>
namespace osctap
{
// Helpers to get the values.
template<osctap::TypeTagValues>
struct OscpackFunction;

// For the ones that requires access to more than the type.
struct object_required_trait {};

// For the ones where the value is included in the type.
struct object_useless_trait   {};

template<>
struct OscpackFunction<osctap::INT32_TYPE_TAG>
{
    using conversion_mode = object_required_trait;
    static const constexpr auto convert =           &osctap::ReceivedMessageArgument::AsInt32;
    static const constexpr auto convert_unchecked = &osctap::ReceivedMessageArgument::AsInt32Unchecked;
};

template<>
struct OscpackFunction<osctap::INT64_TYPE_TAG>
{
    using conversion_mode = object_required_trait;
    static const constexpr auto convert =           &osctap::ReceivedMessageArgument::AsInt64;
    static const constexpr auto convert_unchecked = &osctap::ReceivedMessageArgument::AsInt64Unchecked;
};

template<>
struct OscpackFunction<osctap::FLOAT_TYPE_TAG>
{
    using conversion_mode = object_required_trait;
    static const constexpr auto convert =           &osctap::ReceivedMessageArgument::AsFloat;
    static const constexpr auto convert_unchecked = &osctap::ReceivedMessageArgument::AsFloatUnchecked;
};

template<>
struct OscpackFunction<osctap::DOUBLE_TYPE_TAG>
{
    using conversion_mode = object_required_trait;
    static const constexpr auto convert =           &osctap::ReceivedMessageArgument::AsDouble;
    static const constexpr auto convert_unchecked = &osctap::ReceivedMessageArgument::AsDoubleUnchecked;
};

template<>
struct OscpackFunction<osctap::CHAR_TYPE_TAG>
{
    using conversion_mode = object_required_trait;
    static const constexpr auto convert =           &osctap::ReceivedMessageArgument::AsChar;
    static const constexpr auto convert_unchecked = &osctap::ReceivedMessageArgument::AsCharUnchecked;
};

template<>
struct OscpackFunction<osctap::STRING_TYPE_TAG>
{
    using conversion_mode = object_required_trait;
    static const constexpr auto convert =           &osctap::ReceivedMessageArgument::AsString;
    static const constexpr auto convert_unchecked = &osctap::ReceivedMessageArgument::AsStringUnchecked;
};

template<>
struct OscpackFunction<osctap::SYMBOL_TYPE_TAG>
{
    using conversion_mode = object_required_trait;
    static const constexpr auto convert =           &osctap::ReceivedMessageArgument::AsSymbol;
    static const constexpr auto convert_unchecked = &osctap::ReceivedMessageArgument::AsSymbolUnchecked;
};

template<>
struct OscpackFunction<osctap::BLOB_TYPE_TAG>
{
    using conversion_mode = object_required_trait;
    static const constexpr auto convert =           &osctap::ReceivedMessageArgument::AsBlob;
    static const constexpr auto convert_unchecked = &osctap::ReceivedMessageArgument::AsBlobUnchecked;
};

template<>
struct OscpackFunction<osctap::TRUE_TYPE_TAG>
{
    using conversion_mode = object_useless_trait;
    static bool true_fun() { return true; }
    static const constexpr auto convert =           &true_fun;
    static const constexpr auto convert_unchecked = &true_fun;
};

template<>
struct OscpackFunction<osctap::FALSE_TYPE_TAG>
{
    using conversion_mode = object_useless_trait;
    static bool false_fun() { return false; }
    static const constexpr auto convert =           &false_fun;
    static const constexpr auto convert_unchecked = &false_fun;
};

template<>
struct OscpackFunction<osctap::INFINITUM_TYPE_TAG>
{
    using conversion_mode = object_useless_trait;
    static InfinitumType impulse_fun() { return {}; }
    static const constexpr auto convert =           &impulse_fun;
    static const constexpr auto convert_unchecked = &impulse_fun;
};

template<>
struct OscpackFunction<osctap::NIL_TYPE_TAG>
{
    using conversion_mode = object_useless_trait;
    static NilType nil_fun() { return {}; }
    static const constexpr auto convert =           &nil_fun;
    static const constexpr auto convert_unchecked = &nil_fun;
};

template<osctap::TypeTagValues val>
auto convert(osctap::ReceivedMessageArgument arg,
             std::enable_if_t<
              std::is_same<
               typename OscpackFunction<val>::conversion_mode,
               object_required_trait
              >::value
             >* = nullptr)
{
  return (arg.*OscpackFunction<val>::convert)();
}

template<osctap::TypeTagValues val>
auto convert(osctap::ReceivedMessageArgument,
             std::enable_if_t<
              std::is_same<
               typename OscpackFunction<val>::conversion_mode,
               object_useless_trait
              >::value
             >* = nullptr)
{
  return (*OscpackFunction<val>::convert)();
}

}

// Backwards-compatibility alias: this library was formerly named oscpack.
// Existing code that uses the oscpack:: namespace continues to compile.
namespace oscpack = osctap;
