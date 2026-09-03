//===- llvm/Support/YAMLTraits.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_YAMLTRAITS_H
#define LLVM_SUPPORT_YAMLTRAITS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/AlignOf.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLParser.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <cassert>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace llvm {

class VersionTuple;

namespace yaml {

/// Kind of YAML node being read or written.
enum class NodeKind : uint8_t {
  /// A YAML scalar value.
  Scalar,
  /// A YAML mapping (key/value pairs).
  Map,
  /// A YAML sequence (ordered list).
  Sequence,
};

/// Empty context type used when no mapping context is required.
struct EmptyContext {};

/// This class should be specialized by any type that needs to be converted
/// to/from a YAML mapping.  For example:
///
///     struct MappingTraits<MyStruct> {
///       static void mapping(IO &io, MyStruct &s) {
///         io.mapRequired("name", s.name);
///         io.mapRequired("size", s.size);
///         io.mapOptional("age",  s.age);
///       }
///     };
template <class T> struct MappingTraits {
  // Must provide:
  // static void mapping(IO &io, T &fields);
  // Optionally may provide:
  // static std::string validate(IO &io, T &fields);
  // static void enumInput(IO &io, T &value);
  //
  // The optional flow flag will cause generated YAML to use a flow mapping
  // (e.g. { a: 0, b: 1 }):
  // static const bool flow = true;
};

/// This class is similar to MappingTraits<T> but allows you to pass in
/// additional context for each map operation.  For example:
///
///     struct MappingContextTraits<MyStruct, MyContext> {
///       static void mapping(IO &io, MyStruct &s, MyContext &c) {
///         io.mapRequired("name", s.name);
///         io.mapRequired("size", s.size);
///         io.mapOptional("age",  s.age);
///         ++c.TimesMapped;
///       }
///     };
template <class T, class Context> struct MappingContextTraits {
  // Must provide:
  // static void mapping(IO &io, T &fields, Context &Ctx);
  // Optionally may provide:
  // static std::string validate(IO &io, T &fields, Context &Ctx);
  //
  // The optional flow flag will cause generated YAML to use a flow mapping
  // (e.g. { a: 0, b: 1 }):
  // static const bool flow = true;
};

/// Traits for converting an integral type to/from a YAML enumeration scalar.
///
/// This class should be specialized by any integral type that converts
/// to/from a YAML scalar where there is a one-to-one mapping between
/// in-memory values and a string in YAML.  For example:
///
///     struct ScalarEnumerationTraits<Colors> {
///         static void enumeration(IO &io, Colors &value) {
///           io.enumCase(value, "red",   cRed);
///           io.enumCase(value, "blue",  cBlue);
///           io.enumCase(value, "green", cGreen);
///         }
///       };
template <typename T, typename Enable = void> struct ScalarEnumerationTraits {
  // Must provide:
  // static void enumeration(IO &io, T &value);
};

/// This class should be specialized by any integer type that is a union
/// of bit values and the YAML representation is a flow sequence of
/// strings.  For example:
///
///      struct ScalarBitSetTraits<MyFlags> {
///        static void bitset(IO &io, MyFlags &value) {
///          io.bitSetCase(value, "big",   flagBig);
///          io.bitSetCase(value, "flat",  flagFlat);
///          io.bitSetCase(value, "round", flagRound);
///        }
///      };
template <typename T, typename Enable = void> struct ScalarBitSetTraits {
  // Must provide:
  // static void bitset(IO &io, T &value);
};

/// Quoting style to use when emitting a YAML scalar.
///
/// Some non-printable characters need to be double-quoted, while some others
/// are fine with simple-quoting, and some don't need any quoting.
enum class QuotingType {
  /// No quotes are required.
  None,
  /// Use single quotes.
  Single,
  /// Use double quotes.
  Double,
};

/// This class should be specialized by type that requires custom conversion
/// to/from a yaml scalar.  For example:
///
///    template<>
///    struct ScalarTraits<MyType> {
///      static void output(const MyType &val, void*, llvm::raw_ostream &out) {
///        // stream out custom formatting
///        out << llvm::format("%x", val);
///      }
///      static StringRef input(StringRef scalar, void*, MyType &value) {
///        // parse scalar and set `value`
///        // return empty string on success, or error string
///        return StringRef();
///      }
///      static QuotingType mustQuote(StringRef) { return QuotingType::Single; }
///    };
template <typename T, typename Enable = void> struct ScalarTraits {
  // Must provide:
  //
  // Function to write the value as a string:
  // static void output(const T &value, void *ctxt, llvm::raw_ostream &out);
  //
  // Function to convert a string to a value.  Returns the empty
  // StringRef on success or an error string if string is malformed:
  // static StringRef input(StringRef scalar, void *ctxt, T &value);
  //
  // Function to determine if the value should be quoted.
  // static QuotingType mustQuote(StringRef);
};

/// This class should be specialized by type that requires custom conversion
/// to/from a YAML literal block scalar. For example:
///
///    template <>
///    struct BlockScalarTraits<MyType> {
///      static void output(const MyType &Value, void*, llvm::raw_ostream &Out)
///      {
///        // stream out custom formatting
///        Out << Value;
///      }
///      static StringRef input(StringRef Scalar, void*, MyType &Value) {
///        // parse scalar and set `value`
///        // return empty string on success, or error string
///        return StringRef();
///      }
///    };
template <typename T> struct BlockScalarTraits {
  // Must provide:
  //
  // Function to write the value as a string:
  // static void output(const T &Value, void *ctx, llvm::raw_ostream &Out);
  //
  // Function to convert a string to a value.  Returns the empty
  // StringRef on success or an error string if string is malformed:
  // static StringRef input(StringRef Scalar, void *ctxt, T &Value);
  //
  // Optional:
  // static StringRef inputTag(T &Val, std::string Tag)
  // static void outputTag(const T &Val, raw_ostream &Out)
};

/// This class should be specialized by type that requires custom conversion
/// to/from a YAML scalar with optional tags. For example:
///
///    template <>
///    struct TaggedScalarTraits<MyType> {
///      static void output(const MyType &Value, void*, llvm::raw_ostream
///      &ScalarOut, llvm::raw_ostream &TagOut)
///      {
///        // stream out custom formatting including optional Tag
///        Out << Value;
///      }
///      static StringRef input(StringRef Scalar, StringRef Tag, void*, MyType
///      &Value) {
///        // parse scalar and set `value`
///        // return empty string on success, or error string
///        return StringRef();
///      }
///      static QuotingType mustQuote(const MyType &Value, StringRef) {
///        return QuotingType::Single;
///      }
///    };
template <typename T> struct TaggedScalarTraits {
  // Must provide:
  //
  // Function to write the value and tag as strings:
  // static void output(const T &Value, void *ctx, llvm::raw_ostream &ScalarOut,
  // llvm::raw_ostream &TagOut);
  //
  // Function to convert a string to a value.  Returns the empty
  // StringRef on success or an error string if string is malformed:
  // static StringRef input(StringRef Scalar, StringRef Tag, void *ctxt, T
  // &Value);
  //
  // Function to determine if the value should be quoted.
  // static QuotingType mustQuote(const T &Value, StringRef Scalar);
};

/// This class should be specialized by any type that needs to be converted
/// to/from a YAML sequence.  For example:
///
///    template<>
///    struct SequenceTraits<MyContainer> {
///      static size_t size(IO &io, MyContainer &seq) {
///        return seq.size();
///      }
///      static MyType& element(IO &, MyContainer &seq, size_t index) {
///        if ( index >= seq.size() )
///          seq.resize(index+1);
///        return seq[index];
///      }
///    };
template <typename T, typename EnableIf = void> struct SequenceTraits {
  // Must provide:
  // static size_t size(IO &io, T &seq);
  // static T::value_type& element(IO &io, T &seq, size_t index);
  //
  // The following is option and will cause generated YAML to use
  // a flow sequence (e.g. [a,b,c]).
  // static const bool flow = true;
};

/// This class should be specialized by any type for which vectors of that
/// type need to be converted to/from a YAML sequence.
template <typename T, typename EnableIf = void> struct SequenceElementTraits {
  // Must provide:
  // static const bool flow;
};

/// This class should be specialized by any type that needs to be converted
/// to/from a list of YAML documents.
template <typename T> struct DocumentListTraits {
  // Must provide:
  // static size_t size(IO &io, T &seq);
  // static T::value_type& element(IO &io, T &seq, size_t index);
};

/// Traits for converting a type to/from a YAML mapping with dynamic keys.
///
/// This class should be specialized by any type that needs to be converted
/// to/from a YAML mapping in the case where the names of the keys are not known
/// in advance, e.g. a string map.
template <typename T> struct CustomMappingTraits {
  // static void inputOne(IO &io, StringRef key, T &elem);
  // static void output(IO &io, T &elem);
};

/// This class should be specialized by any type that can be represented as
/// a scalar, map, or sequence, decided dynamically. For example:
///
///    typedef std::unique_ptr<MyBase> MyPoly;
///
///    template<>
///    struct PolymorphicTraits<MyPoly> {
///      static NodeKind getKind(const MyPoly &poly) {
///        return poly->getKind();
///      }
///      static MyScalar& getAsScalar(MyPoly &poly) {
///        if (!poly || !isa<MyScalar>(poly))
///          poly.reset(new MyScalar());
///        return *cast<MyScalar>(poly.get());
///      }
///      // ...
///    };
template <typename T> struct PolymorphicTraits {
  // Must provide:
  // static NodeKind getKind(const T &poly);
  // static scalar_type &getAsScalar(T &poly);
  // static map_type &getAsMap(T &poly);
  // static sequence_type &getAsSequence(T &poly);
};

/// Incomplete type used to produce a clearer error when traits are missing.
template <typename T> struct MissingTrait;

/// Detects whether \c ScalarEnumerationTraits<T> provides \c enumeration.
template <class T> struct has_ScalarEnumerationTraits {
  /// Function-pointer type matching \c ScalarEnumerationTraits::enumeration.
  using SignatureEnumeration = void (*)(class IO &, T &);

  /// Checks that \c U exposes a matching \c enumeration member.
  template <class U>
  using check =
      SameType<SignatureEnumeration, &ScalarEnumerationTraits<U>::enumeration>;

  /// True when \c ScalarEnumerationTraits<T>::enumeration is available.
  static constexpr bool value = is_detected<check, T>::value;
};

/// Detects whether \c ScalarBitSetTraits<T> provides \c bitset.
template <class T> struct has_ScalarBitSetTraits {
  /// Function-pointer type matching \c ScalarBitSetTraits::bitset.
  using SignatureBitset = void (*)(class IO &, T &);

  /// Checks that \c U exposes a matching \c bitset member.
  template <class U>
  using check = SameType<SignatureBitset, &ScalarBitSetTraits<U>::bitset>;

  /// True when \c ScalarBitSetTraits<T>::bitset is available.
  static constexpr bool value = is_detected<check, T>::value;
};

/// Detects whether \c ScalarTraits<T> provides input/output/mustQuote.
template <class T> struct has_ScalarTraits {
  /// Function-pointer type matching \c ScalarTraits::input.
  using SignatureInput = StringRef (*)(StringRef, void *, T &);
  /// Function-pointer type matching \c ScalarTraits::output.
  using SignatureOutput = void (*)(const T &, void *, raw_ostream &);
  /// Function-pointer type matching \c ScalarTraits::mustQuote.
  using SignatureMustQuote = QuotingType (*)(StringRef);

  /// Checks that \c U exposes matching input, output, and mustQuote members.
  template <class U>
  using check = std::tuple<SameType<SignatureInput, &U::input>,
                           SameType<SignatureOutput, &U::output>,
                           SameType<SignatureMustQuote, &U::mustQuote>>;

  /// True when \c ScalarTraits<T> defines the required scalar methods.
  static constexpr bool value = is_detected<check, ScalarTraits<T>>::value;
};

/// Detects whether \c BlockScalarTraits<T> provides input/output.
template <class T> struct has_BlockScalarTraits {
  /// Function-pointer type matching \c BlockScalarTraits::input.
  using SignatureInput = StringRef (*)(StringRef, void *, T &);
  /// Function-pointer type matching \c BlockScalarTraits::output.
  using SignatureOutput = void (*)(const T &, void *, raw_ostream &);

  /// Checks that \c U exposes matching input and output members.
  template <class U>
  using check = std::tuple<SameType<SignatureInput, &U::input>,
                           SameType<SignatureOutput, &U::output>>;

  /// True when \c BlockScalarTraits<T> defines the required methods.
  static constexpr bool value = is_detected<check, BlockScalarTraits<T>>::value;
};

/// Detects whether \c TaggedScalarTraits<T> provides input/output/mustQuote.
template <class T> struct has_TaggedScalarTraits {
  /// Function-pointer type matching \c TaggedScalarTraits::input.
  using SignatureInput = StringRef (*)(StringRef, StringRef, void *, T &);
  /// Function-pointer type matching \c TaggedScalarTraits::output.
  using SignatureOutput = void (*)(const T &, void *, raw_ostream &,
                                   raw_ostream &);
  /// Function-pointer type matching \c TaggedScalarTraits::mustQuote.
  using SignatureMustQuote = QuotingType (*)(const T &, StringRef);

  /// Checks that \c U exposes matching input, output, and mustQuote members.
  template <class U>
  using check = std::tuple<SameType<SignatureInput, &U::input>,
                           SameType<SignatureOutput, &U::output>,
                           SameType<SignatureMustQuote, &U::mustQuote>>;

  /// True when \c TaggedScalarTraits<T> defines the required methods.
  static constexpr bool value =
      is_detected<check, TaggedScalarTraits<T>>::value;
};

/// Detects whether \c MappingContextTraits<T, Context> provides \c mapping.
template <class T, class Context> struct has_MappingTraits {
  /// Function-pointer type matching \c MappingContextTraits::mapping.
  using SignatureMapping = void (*)(class IO &, T &, Context &);

  /// Checks that \c U exposes a matching \c mapping member.
  template <class U> using check = SameType<SignatureMapping, &U::mapping>;

  /// True when \c MappingContextTraits<T, Context>::mapping is available.
  static constexpr bool value =
      is_detected<check, MappingContextTraits<T, Context>>::value;
};

/// Detects whether \c MappingTraits<T> provides \c mapping (no context).
template <class T> struct has_MappingTraits<T, EmptyContext> {
  /// Function-pointer type matching \c MappingTraits::mapping.
  using SignatureMapping = void (*)(class IO &, T &);

  /// Checks that \c U exposes a matching \c mapping member.
  template <class U> using check = SameType<SignatureMapping, &U::mapping>;

  /// True when \c MappingTraits<T>::mapping is available.
  static constexpr bool value = is_detected<check, MappingTraits<T>>::value;
};

/// Detects whether \c MappingContextTraits<T, Context> provides \c validate.
template <class T, class Context> struct has_MappingValidateTraits {
  /// Function-pointer type matching \c MappingContextTraits::validate.
  using SignatureValidate = std::string (*)(class IO &, T &, Context &);

  /// Checks that \c U exposes a matching \c validate member.
  template <class U> using check = SameType<SignatureValidate, &U::validate>;

  /// True when \c MappingContextTraits<T, Context>::validate is available.
  static constexpr bool value =
      is_detected<check, MappingContextTraits<T, Context>>::value;
};

/// Detects whether \c MappingTraits<T> provides \c validate (no context).
template <class T> struct has_MappingValidateTraits<T, EmptyContext> {
  /// Function-pointer type matching \c MappingTraits::validate.
  using SignatureValidate = std::string (*)(class IO &, T &);

  /// Checks that \c U exposes a matching \c validate member.
  template <class U> using check = SameType<SignatureValidate, &U::validate>;

  /// True when \c MappingTraits<T>::validate is available.
  static constexpr bool value = is_detected<check, MappingTraits<T>>::value;
};

/// Detects whether \c MappingContextTraits<T, Context> provides \c enumInput.
template <class T, class Context> struct has_MappingEnumInputTraits {
  /// Function-pointer type matching \c MappingContextTraits::enumInput.
  using SignatureEnumInput = void (*)(class IO &, T &);

  /// Checks that \c U exposes a matching \c enumInput member.
  template <class U> using check = SameType<SignatureEnumInput, &U::enumInput>;

  /// True when \c MappingContextTraits<T, Context>::enumInput is available.
  static constexpr bool value =
      is_detected<check, MappingContextTraits<T, Context>>::value;
};

/// Detects whether \c MappingTraits<T> provides \c enumInput (no context).
template <class T> struct has_MappingEnumInputTraits<T, EmptyContext> {
  /// Function-pointer type matching \c MappingTraits::enumInput.
  using SignatureEnumInput = void (*)(class IO &, T &);

  /// Checks that \c U exposes a matching \c enumInput member.
  template <class U> using check = SameType<SignatureEnumInput, &U::enumInput>;

  /// True when \c MappingTraits<T>::enumInput is available.
  static constexpr bool value = is_detected<check, MappingTraits<T>>::value;
};

/// Detects whether \c SequenceTraits<T> provides a \c size method.
template <class T> struct has_SequenceMethodTraits {
  /// Function-pointer type matching \c SequenceTraits::size.
  using SignatureSize = size_t (*)(class IO &, T &);

  /// Checks that \c U exposes a matching \c size member.
  template <class U> using check = SameType<SignatureSize, &U::size>;

  /// True when \c SequenceTraits<T>::size is available.
  static constexpr bool value = is_detected<check, SequenceTraits<T>>::value;
};

/// Detects whether \c CustomMappingTraits<T> provides \c inputOne.
template <class T> struct has_CustomMappingTraits {
  /// Function-pointer type matching \c CustomMappingTraits::inputOne.
  using SignatureInput = void (*)(IO &io, StringRef key, T &v);

  /// Checks that \c U exposes a matching \c inputOne member.
  template <class U> using check = SameType<SignatureInput, &U::inputOne>;

  /// True when \c CustomMappingTraits<T>::inputOne is available.
  static constexpr bool value =
      is_detected<check, CustomMappingTraits<T>>::value;
};

/// Detects whether type \c T defines a static \c flow flag.
template <typename T> struct has_FlowTraits {
  /// Checks that \c U exposes a static \c flow member.
  template <class U> using check = decltype(&U::flow);

  /// True when \c T::flow is available.
  static constexpr bool value = is_detected<check, T>::value;
};

/// True when \c SequenceTraits<T> is defined via a \c size method.
template <typename T>
struct has_SequenceTraits
    : public std::bool_constant<has_SequenceMethodTraits<T>::value> {};

/// Detects whether \c DocumentListTraits<T> provides a \c size method.
template <class T> struct has_DocumentListTraits {
  /// Function-pointer type matching \c DocumentListTraits::size.
  using SignatureSize = size_t (*)(class IO &, T &);

  /// Checks that \c U exposes a matching \c size member.
  template <class U> using check = SameType<SignatureSize, &U::size>;

  /// True when \c DocumentListTraits<T>::size is available.
  static constexpr bool value =
      is_detected<check, DocumentListTraits<T>>::value;
};

/// Detects whether \c PolymorphicTraits<T> provides \c getKind.
template <class T> struct has_PolymorphicTraits {
  /// Function-pointer type matching \c PolymorphicTraits::getKind.
  using SignatureGetKind = NodeKind (*)(const T &);

  /// Checks that \c U exposes a matching \c getKind member.
  template <class U> using check = SameType<SignatureGetKind, &U::getKind>;

  /// True when \c PolymorphicTraits<T>::getKind is available.
  static constexpr bool value = is_detected<check, PolymorphicTraits<T>>::value;
};

/// Return true if \p S looks like a YAML numeric scalar.
/// \param S Candidate scalar text.
/// \returns true when \p S matches YAML 1.2 numeric forms.
inline bool isNumeric(StringRef S) {
  const auto skipDigits = [](StringRef Input) {
    return Input.ltrim("0123456789");
  };

  // Make S.front() and S.drop_front().front() (if S.front() is [+-]) calls
  // safe.
  if (S.empty() || S == "+" || S == "-")
    return false;

  if (S == ".nan" || S == ".NaN" || S == ".NAN")
    return true;

  // Infinity and decimal numbers can be prefixed with sign.
  StringRef Tail = (S.front() == '-' || S.front() == '+') ? S.drop_front() : S;

  // Check for infinity first, because checking for hex and oct numbers is more
  // expensive.
  if (Tail == ".inf" || Tail == ".Inf" || Tail == ".INF")
    return true;

  // Section 10.3.2 Tag Resolution
  // YAML 1.2 Specification prohibits Base 8 and Base 16 numbers prefixed with
  // [-+], so S should be used instead of Tail.
  if (S.starts_with("0o"))
    return S.size() > 2 &&
           S.drop_front(2).find_first_not_of("01234567") == StringRef::npos;

  if (S.starts_with("0x"))
    return S.size() > 2 && S.drop_front(2).find_first_not_of(
                               "0123456789abcdefABCDEF") == StringRef::npos;

  // Parse float: [-+]? (\. [0-9]+ | [0-9]+ (\. [0-9]* )?) ([eE] [-+]? [0-9]+)?
  S = Tail;

  // Handle cases when the number starts with '.' and hence needs at least one
  // digit after dot (as opposed by number which has digits before the dot), but
  // doesn't have one.
  if (S.starts_with(".") &&
      (S == "." ||
       (S.size() > 1 && std::strchr("0123456789", S[1]) == nullptr)))
    return false;

  if (S.starts_with("E") || S.starts_with("e"))
    return false;

  enum ParseState {
    Default,
    FoundDot,
    FoundExponent,
  };
  ParseState State = Default;

  S = skipDigits(S);

  // Accept decimal integer.
  if (S.empty())
    return true;

  if (S.front() == '.') {
    State = FoundDot;
    S = S.drop_front();
  } else if (S.front() == 'e' || S.front() == 'E') {
    State = FoundExponent;
    S = S.drop_front();
  } else {
    return false;
  }

  if (State == FoundDot) {
    S = skipDigits(S);
    if (S.empty())
      return true;

    if (S.front() == 'e' || S.front() == 'E') {
      State = FoundExponent;
      S = S.drop_front();
    } else {
      return false;
    }
  }

  assert(State == FoundExponent && "Should have found exponent at this point.");
  if (S.empty())
    return false;

  if (S.front() == '+' || S.front() == '-') {
    S = S.drop_front();
    if (S.empty())
      return false;
  }

  return skipDigits(S).empty();
}

/// Return true if \p S is a YAML null scalar token.
/// \param S Candidate scalar text.
/// \returns true when \p S is null/Null/NULL/~.
inline bool isNull(StringRef S) {
  return S == "null" || S == "Null" || S == "NULL" || S == "~";
}

/// Return true if \p S is a YAML boolean scalar token.
/// \param S Candidate scalar text.
/// \returns true when \p S is a true/false spelling.
inline bool isBool(StringRef S) {
  // FIXME: using parseBool is causing multiple tests to fail.
  return S == "true" || S == "True" || S == "TRUE" || S == "false" ||
         S == "False" || S == "FALSE";
}

/// Determine the quoting style required for scalar \p S.
///
/// 5.1. Character Set
/// The allowed character range explicitly excludes the C0 control block #x0-#x1F
/// (except for TAB #x9, LF #xA, and CR #xD which are allowed), DEL #x7F, the C1
/// control block #x80-#x9F (except for NEL #x85 which is allowed), the surrogate
/// block #xD800-#xDFFF, #xFFFE, and #xFFFF.
///
/// Some strings are valid YAML values even unquoted, but without quotes are
/// interpreted as non-string type, for instance null, boolean or numeric values.
/// If ForcePreserveAsString is set, such strings are quoted.
///
/// \param S Scalar text to inspect.
/// \param ForcePreserveAsString When true, quote null/bool/numeric lookalikes.
/// \returns The minimum quoting style required for \p S.
inline QuotingType needsQuotes(StringRef S, bool ForcePreserveAsString = true) {
  if (S.empty())
    return QuotingType::Single;

  QuotingType MaxQuotingNeeded = QuotingType::None;
  if (isSpace(static_cast<unsigned char>(S.front())) ||
      isSpace(static_cast<unsigned char>(S.back())))
    MaxQuotingNeeded = QuotingType::Single;
  if (ForcePreserveAsString) {
    if (isNull(S))
      MaxQuotingNeeded = QuotingType::Single;
    if (isBool(S))
      MaxQuotingNeeded = QuotingType::Single;
    if (isNumeric(S))
      MaxQuotingNeeded = QuotingType::Single;
  }

  // 7.3.3 Plain Style
  // Plain scalars must not begin with most indicators, as this would cause
  // ambiguity with other YAML constructs.
  if (std::strchr(R"(-?:\,[]{}#&*!|>'"%@`)", S[0]) != nullptr)
    MaxQuotingNeeded = QuotingType::Single;

  for (unsigned char C : S) {
    // Alphanum is safe.
    if (isAlnum(C))
      continue;

    switch (C) {
    // Safe scalar characters.
    case '_':
    case '-':
    case '^':
    case '.':
    case ',':
    case ' ':
    // TAB (0x9) is allowed in unquoted strings.
    case 0x9:
      continue;
    // LF(0xA) and CR(0xD) may delimit values and so require at least single
    // quotes. LLVM YAML parser cannot handle single quoted multiline so use
    // double quoting to produce valid YAML.
    case 0xA:
    case 0xD:
      return QuotingType::Double;
    // DEL (0x7F) are excluded from the allowed character range.
    case 0x7F:
      return QuotingType::Double;
    // Forward slash is allowed to be unquoted, but we quote it anyway.  We have
    // many tests that use FileCheck against YAML output, and this output often
    // contains paths.  If we quote backslashes but not forward slashes then
    // paths will come out either quoted or unquoted depending on which platform
    // the test is run on, making FileCheck comparisons difficult.
    case '/':
    default: {
      // C0 control block (0x0 - 0x1F) is excluded from the allowed character
      // range.
      if (C <= 0x1F)
        return QuotingType::Double;

      // Always double quote UTF-8.
      if ((C & 0x80) != 0)
        return QuotingType::Double;

      // The character is not safe, at least simple quoting needed.
      MaxQuotingNeeded = QuotingType::Single;
    }
    }
  }

  return MaxQuotingNeeded;
}

/// True when no YAML traits are defined for \c T with \c Context.
template <typename T, typename Context>
struct missingTraits
    : public std::bool_constant<
          !has_ScalarEnumerationTraits<T>::value &&
          !has_ScalarBitSetTraits<T>::value && !has_ScalarTraits<T>::value &&
          !has_BlockScalarTraits<T>::value &&
          !has_TaggedScalarTraits<T>::value &&
          !has_MappingTraits<T, Context>::value &&
          !has_SequenceTraits<T>::value && !has_CustomMappingTraits<T>::value &&
          !has_DocumentListTraits<T>::value &&
          !has_PolymorphicTraits<T>::value> {};

/// True when \c T has mapping traits that include a validate hook.
template <typename T, typename Context>
struct validatedMappingTraits
    : public std::bool_constant<has_MappingTraits<T, Context>::value &&
                                has_MappingValidateTraits<T, Context>::value> {
};

/// True when \c T has mapping traits without a validate hook.
template <typename T, typename Context>
struct unvalidatedMappingTraits
    : public std::bool_constant<has_MappingTraits<T, Context>::value &&
                                !has_MappingValidateTraits<T, Context>::value> {
};

/// Base class for YAML input and output conversions.
class LLVM_ABI IO {
public:
  /// Construct an IO object with optional user context \p Ctxt.
  /// \param Ctxt Opaque client context pointer (may be null).
  IO(void *Ctxt = nullptr);
  /// Destroy the IO object.
  virtual ~IO();

  /// Return true when this IO is writing YAML rather than reading it.
  /// \returns true when writing YAML; false when reading.
  virtual bool outputting() const = 0;

  /// Begin reading or writing a block-style sequence.
  /// \returns The number of elements when reading; unused when writing.
  virtual unsigned beginSequence() = 0;
  /// Prepare element \p Index of a block sequence.
  /// \param Index Zero-based element index.
  /// \param SaveInfo Opaque state restored in \c postflightElement.
  /// \returns true if the element should be processed.
  virtual bool preflightElement(unsigned Index, void *&SaveInfo) = 0;
  /// Finish processing a block-sequence element.
  /// \param SaveInfo Opaque state from \c preflightElement.
  virtual void postflightElement(void *SaveInfo) = 0;
  /// Finish reading or writing a block-style sequence.
  virtual void endSequence() = 0;
  /// Return true if empty sequences may be omitted on output.
  /// \returns true if empty sequences may be omitted on output.
  virtual bool canElideEmptySequence() = 0;

  /// Begin reading or writing a flow-style sequence.
  /// \returns The number of elements when reading; unused when writing.
  virtual unsigned beginFlowSequence() = 0;
  /// Prepare element \p Index of a flow sequence.
  /// \param Index Zero-based element index.
  /// \param SaveInfo Opaque state restored in \c postflightFlowElement.
  /// \returns true if the element should be processed.
  virtual bool preflightFlowElement(unsigned Index, void *&SaveInfo) = 0;
  /// Finish processing a flow-sequence element.
  /// \param SaveInfo Opaque state from \c preflightFlowElement.
  virtual void postflightFlowElement(void *SaveInfo) = 0;
  /// Finish reading or writing a flow-style sequence.
  virtual void endFlowSequence() = 0;

  /// Match or emit mapping tag \p Tag.
  /// \param Tag YAML tag string.
  /// \param Default Whether this tag is the default when unmatched.
  /// \returns true when the tag matches (input) or was accepted (output).
  virtual bool mapTag(StringRef Tag, bool Default = false) = 0;
  /// Begin reading or writing a block-style mapping.
  virtual void beginMapping() = 0;
  /// Finish reading or writing a block-style mapping.
  virtual void endMapping() = 0;
  /// Prepare key \p Key in the current mapping.
  /// \param Key Mapping key text.
  /// \param Required Whether the key must be present on input.
  /// \param SameAsDefault Whether the value equals its default on output.
  /// \param UseDefault Set to true when the default should be used.
  /// \param SaveInfo Opaque state restored in \c postflightKey.
  /// \returns true if the key/value should be processed.
  virtual bool preflightKey(StringRef Key, bool Required, bool SameAsDefault,
                            bool &UseDefault, void *&SaveInfo) = 0;
  /// Finish processing a mapping key.
  /// \param SaveInfo Opaque state from \c preflightKey.
  virtual void postflightKey(void *SaveInfo) = 0;
  /// Return the keys present in the current mapping (input).
  /// \returns The keys present in the current mapping.
  virtual std::vector<StringRef> keys() = 0;

  /// Begin reading or writing a flow-style mapping.
  virtual void beginFlowMapping() = 0;
  /// Finish reading or writing a flow-style mapping.
  virtual void endFlowMapping() = 0;

  /// Begin converting an enumeration scalar.
  virtual void beginEnumScalar() = 0;
  /// Match enumeration case \p Str, optionally treating it as selected.
  /// \param Str Enumeration case spelling.
  /// \param Match When true on output, this case is the selected value.
  /// \returns true when this case matches the input scalar.
  virtual bool matchEnumScalar(StringRef Str, bool Match) = 0;
  /// Attempt the enumeration fallback conversion path.
  /// \returns true when fallback conversion should run.
  virtual bool matchEnumFallback() = 0;
  /// Finish converting an enumeration scalar.
  virtual void endEnumScalar() = 0;

  /// Begin converting a bit-set scalar.
  /// \param DoClear Set to true when the value should be cleared first.
  /// \returns true if bit-set conversion should proceed.
  virtual bool beginBitSetScalar(bool &DoClear) = 0;
  /// Match bit-set case \p Str, optionally treating it as selected.
  /// \param Str Bit-set case spelling.
  /// \param Match When true on output, this bit is set in the value.
  /// \returns true when this case matches on input.
  virtual bool bitSetMatch(StringRef Str, bool Match) = 0;
  /// Finish converting a bit-set scalar.
  virtual void endBitSetScalar() = 0;

  /// Read or write a plain scalar string.
  /// \param Str Scalar text (written on output; filled on input).
  /// \param Quote Quoting style to use when writing.
  virtual void scalarString(StringRef &Str, QuotingType Quote) = 0;
  /// Read or write a literal block scalar string.
  /// \param Str Block scalar text (written on output; filled on input).
  virtual void blockScalarString(StringRef &Str) = 0;
  /// Read or write the YAML tag associated with a scalar.
  /// \param Tag Tag text (written on output; filled on input).
  virtual void scalarTag(std::string &Tag) = 0;

  /// Return the kind of the current YAML node.
  /// \returns The kind of the current YAML node.
  virtual NodeKind getNodeKind() = 0;

  /// Record error \p Message for this IO operation.
  /// \param Message Human-readable error description.
  virtual void setError(const Twine &Message) = 0;
  /// Return the error state from this IO operation.
  /// \returns The error state from this IO operation.
  virtual std::error_code error() = 0;
  /// Allow (or reject) unknown mapping keys during input.
  /// \param Allow When true, unknown keys are ignored instead of errors.
  virtual void setAllowUnknownKeys(bool Allow);

  /// Map enumeration value \p Val to/from spelling \p Str and constant \p ConstVal.
  /// \param Val In-memory enumeration value.
  /// \param Str YAML spelling for this case.
  /// \param ConstVal Constant matching this case.
  template <typename T> void enumCase(T &Val, StringRef Str, const T ConstVal) {
    if (matchEnumScalar(Str, outputting() && Val == ConstVal)) {
      Val = ConstVal;
    }
  }

  /// Map enumeration \p Val to/from \p Str using anonymous uint32 constant \p ConstVal.
  /// \param Val In-memory enumeration value.
  /// \param Str YAML spelling for this case.
  /// \param ConstVal Anonymous integer constant for this case.
  template <typename T>
  void enumCase(T &Val, StringRef Str, const uint32_t ConstVal) {
    if (matchEnumScalar(Str, outputting() && Val == static_cast<T>(ConstVal))) {
      Val = ConstVal;
    }
  }

  /// Fall back to converting \p Val via strong typedef base type \c FBT.
  /// \param Val Enumeration value to convert when no case matched.
  template <typename FBT, typename T> void enumFallback(T &Val) {
    if (matchEnumFallback()) {
      EmptyContext Context;
      // FIXME: Force integral conversion to allow strong typedefs to convert.
      FBT Res = static_cast<typename FBT::BaseType>(Val);
      yamlize(*this, Res, true, Context);
      Val = static_cast<T>(static_cast<typename FBT::BaseType>(Res));
    }
  }

  /// Map bit-set flag \p ConstVal to/from spelling \p Str in \p Val.
  /// \param Val In-memory bit-set value.
  /// \param Str YAML spelling for this flag.
  /// \param ConstVal Flag bit(s) for this case.
  template <typename T>
  void bitSetCase(T &Val, StringRef Str, const T ConstVal) {
    if (bitSetMatch(Str, outputting() && (Val & ConstVal) == ConstVal)) {
      Val = static_cast<T>(Val | ConstVal);
    }
  }

  /// Map bit-set flag \p ConstVal to/from \p Str using an anonymous uint32 constant.
  /// \param Val In-memory bit-set value.
  /// \param Str YAML spelling for this flag.
  /// \param ConstVal Anonymous integer flag bit(s) for this case.
  template <typename T>
  void bitSetCase(T &Val, StringRef Str, const uint32_t ConstVal) {
    if (bitSetMatch(Str, outputting() && (Val & ConstVal) == ConstVal)) {
      Val = static_cast<T>(Val | ConstVal);
    }
  }

  /// Map masked bit-set field (\p ConstVal under \p Mask) to/from \p Str in \p Val.
  /// \param Val In-memory bit-set value.
  /// \param Str YAML spelling for this case.
  /// \param ConstVal Value of the masked field for this case.
  /// \param Mask Bits that participate in the comparison.
  template <typename T>
  void maskedBitSetCase(T &Val, StringRef Str, T ConstVal, T Mask) {
    if (bitSetMatch(Str, outputting() && (Val & Mask) == ConstVal))
      Val = Val | ConstVal;
  }

  /// Map masked bit-set field using anonymous uint32 \p ConstVal and \p Mask.
  /// \param Val In-memory bit-set value.
  /// \param Str YAML spelling for this case.
  /// \param ConstVal Value of the masked field for this case.
  /// \param Mask Bits that participate in the comparison.
  template <typename T>
  void maskedBitSetCase(T &Val, StringRef Str, uint32_t ConstVal,
                        uint32_t Mask) {
    if (bitSetMatch(Str, outputting() && (Val & Mask) == ConstVal))
      Val = Val | ConstVal;
  }

  /// Return the opaque client context pointer.
  /// \returns The opaque client context pointer.
  void *getContext() const;
  /// Set the opaque client context pointer to \p Context.
  /// \param Context New client context (may be null).
  void setContext(void *Context);

  /// Require mapping key \p Key and convert its value into \p Val.
  /// \param Key Mapping key text.
  /// \param Val Destination (input) or source (output) value.
  template <typename T> void mapRequired(StringRef Key, T &Val) {
    EmptyContext Ctx;
    this->processKey(Key, Val, true, Ctx);
  }

  /// Require mapping key \p Key and convert \p Val using context \p Ctx.
  /// \param Key Mapping key text.
  /// \param Val Destination (input) or source (output) value.
  /// \param Ctx Extra mapping context passed to traits.
  template <typename T, typename Context>
  void mapRequired(StringRef Key, T &Val, Context &Ctx) {
    this->processKey(Key, Val, true, Ctx);
  }

  /// Optionally map key \p Key to/from \p Val.
  /// \param Key Mapping key text.
  /// \param Val Destination (input) or source (output) value.
  template <typename T> void mapOptional(StringRef Key, T &Val) {
    EmptyContext Ctx;
    mapOptionalWithContext(Key, Val, Ctx);
  }

  /// Optionally map key \p Key to/from \p Val, using \p Default when absent.
  /// \param Key Mapping key text.
  /// \param Val Destination (input) or source (output) value.
  /// \param Default Value used when the key is missing or elided.
  template <typename T, typename DefaultT>
  void mapOptional(StringRef Key, T &Val, const DefaultT &Default) {
    EmptyContext Ctx;
    mapOptionalWithContext(Key, Val, Default, Ctx);
  }

  /// Optionally map key \p Key to/from \p Val using context \p Ctx.
  /// \param Key Mapping key text.
  /// \param Val Destination (input) or source (output) value.
  /// \param Ctx Extra mapping context passed to traits.
  template <typename T, typename Context>
  void mapOptionalWithContext(StringRef Key, T &Val, Context &Ctx) {
    if constexpr (has_SequenceTraits<T>::value) {
      // omit key/value instead of outputting empty sequence
      if (this->canElideEmptySequence() && Val.begin() == Val.end())
        return;
    }
    this->processKey(Key, Val, false, Ctx);
  }

  /// Optionally map key \p Key to/from optional \p Val using context \p Ctx.
  /// \param Key Mapping key text.
  /// \param Val Optional destination/source value.
  /// \param Ctx Extra mapping context passed to traits.
  template <typename T, typename Context>
  void mapOptionalWithContext(StringRef Key, std::optional<T> &Val,
                              Context &Ctx) {
    this->processKeyWithDefault(Key, Val, std::optional<T>(),
                                /*Required=*/false, Ctx);
  }

  /// Optionally map key \p Key to/from \p Val with \p Default and context \p Ctx.
  /// \param Key Mapping key text.
  /// \param Val Destination (input) or source (output) value.
  /// \param Default Value used when the key is missing or elided.
  /// \param Ctx Extra mapping context passed to traits.
  template <typename T, typename Context, typename DefaultT>
  void mapOptionalWithContext(StringRef Key, T &Val, const DefaultT &Default,
                              Context &Ctx) {
    static_assert(std::is_convertible<DefaultT, T>::value,
                  "Default type must be implicitly convertible to value type!");
    this->processKeyWithDefault(Key, Val, static_cast<const T &>(Default),
                                false, Ctx);
  }

private:
  template <typename T, typename Context>
  void processKeyWithDefault(StringRef Key, std::optional<T> &Val,
                             const std::optional<T> &DefaultValue,
                             bool Required, Context &Ctx);

  template <typename T, typename Context>
  void processKeyWithDefault(StringRef Key, T &Val, const T &DefaultValue,
                             bool Required, Context &Ctx) {
    void *SaveInfo;
    bool UseDefault;
    const bool sameAsDefault = outputting() && Val == DefaultValue;
    if (this->preflightKey(Key, Required, sameAsDefault, UseDefault,
                           SaveInfo)) {
      yamlize(*this, Val, Required, Ctx);
      this->postflightKey(SaveInfo);
    } else {
      if (UseDefault)
        Val = DefaultValue;
    }
  }

  template <typename T, typename Context>
  void processKey(StringRef Key, T &Val, bool Required, Context &Ctx) {
    void *SaveInfo;
    bool UseDefault;
    if (this->preflightKey(Key, Required, false, UseDefault, SaveInfo)) {
      yamlize(*this, Val, Required, Ctx);
      this->postflightKey(SaveInfo);
    }
  }

private:
  void *Ctxt;
};

namespace detail {

template <typename T, typename Context>
void doMapping(IO &io, T &Val, Context &Ctx) {
  MappingContextTraits<T, Context>::mapping(io, Val, Ctx);
}

template <typename T> void doMapping(IO &io, T &Val, EmptyContext &Ctx) {
  MappingTraits<T>::mapping(io, Val);
}

} // end namespace detail

/// Convert enumeration \p Val through \c ScalarEnumerationTraits.
/// \param io YAML IO object.
/// \param Val Value to read or write.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Unused empty context.
template <typename T>
std::enable_if_t<has_ScalarEnumerationTraits<T>::value, void>
yamlize(IO &io, T &Val, bool Required, EmptyContext &Ctx) {
  io.beginEnumScalar();
  ScalarEnumerationTraits<T>::enumeration(io, Val);
  io.endEnumScalar();
}

/// Convert bit-set \p Val through \c ScalarBitSetTraits.
/// \param io YAML IO object.
/// \param Val Value to read or write.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Unused empty context.
template <typename T>
std::enable_if_t<has_ScalarBitSetTraits<T>::value, void>
yamlize(IO &io, T &Val, bool Required, EmptyContext &Ctx) {
  bool DoClear;
  if (io.beginBitSetScalar(DoClear)) {
    if (DoClear)
      Val = T();
    ScalarBitSetTraits<T>::bitset(io, Val);
    io.endBitSetScalar();
  }
}

/// Convert scalar \p Val through \c ScalarTraits.
/// \param io YAML IO object.
/// \param Val Value to read or write.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Unused empty context.
template <typename T>
std::enable_if_t<has_ScalarTraits<T>::value, void>
yamlize(IO &io, T &Val, bool Required, EmptyContext &Ctx) {
  if (io.outputting()) {
    SmallString<128> Storage;
    raw_svector_ostream Buffer(Storage);
    ScalarTraits<T>::output(Val, io.getContext(), Buffer);
    StringRef Str = Buffer.str();
    io.scalarString(Str, ScalarTraits<T>::mustQuote(Str));
  } else {
    StringRef Str;
    io.scalarString(Str, ScalarTraits<T>::mustQuote(Str));
    StringRef Result = ScalarTraits<T>::input(Str, io.getContext(), Val);
    if (!Result.empty()) {
      io.setError(Twine(Result));
    }
  }
}

/// Convert block scalar \p Val through \c BlockScalarTraits.
/// \param YamlIO YAML IO object.
/// \param Val Value to read or write.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Unused empty context.
template <typename T>
std::enable_if_t<has_BlockScalarTraits<T>::value, void>
yamlize(IO &YamlIO, T &Val, bool Required, EmptyContext &Ctx) {
  if (YamlIO.outputting()) {
    std::string Storage;
    raw_string_ostream Buffer(Storage);
    BlockScalarTraits<T>::output(Val, YamlIO.getContext(), Buffer);
    StringRef Str(Storage);
    YamlIO.blockScalarString(Str);
  } else {
    StringRef Str;
    YamlIO.blockScalarString(Str);
    StringRef Result =
        BlockScalarTraits<T>::input(Str, YamlIO.getContext(), Val);
    if (!Result.empty())
      YamlIO.setError(Twine(Result));
  }
}

/// Convert tagged scalar \p Val through \c TaggedScalarTraits.
/// \param io YAML IO object.
/// \param Val Value to read or write.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Unused empty context.
template <typename T>
std::enable_if_t<has_TaggedScalarTraits<T>::value, void>
yamlize(IO &io, T &Val, bool Required, EmptyContext &Ctx) {
  if (io.outputting()) {
    std::string ScalarStorage, TagStorage;
    raw_string_ostream ScalarBuffer(ScalarStorage), TagBuffer(TagStorage);
    TaggedScalarTraits<T>::output(Val, io.getContext(), ScalarBuffer,
                                  TagBuffer);
    io.scalarTag(TagStorage);
    StringRef ScalarStr(ScalarStorage);
    io.scalarString(ScalarStr,
                    TaggedScalarTraits<T>::mustQuote(Val, ScalarStr));
  } else {
    std::string Tag;
    io.scalarTag(Tag);
    StringRef Str;
    io.scalarString(Str, QuotingType::None);
    StringRef Result =
        TaggedScalarTraits<T>::input(Str, Tag, io.getContext(), Val);
    if (!Result.empty()) {
      io.setError(Twine(Result));
    }
  }
}

namespace detail {

template <typename T, typename Context>
std::string doValidate(IO &io, T &Val, Context &Ctx) {
  return MappingContextTraits<T, Context>::validate(io, Val, Ctx);
}

template <typename T> std::string doValidate(IO &io, T &Val, EmptyContext &) {
  return MappingTraits<T>::validate(io, Val);
}

} // namespace detail

/// Convert mapping \p Val with validation through mapping traits.
/// \param io YAML IO object.
/// \param Val Value to read or write.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Mapping context passed to traits.
template <typename T, typename Context>
std::enable_if_t<validatedMappingTraits<T, Context>::value, void>
yamlize(IO &io, T &Val, bool Required, Context &Ctx) {
  if (has_FlowTraits<MappingTraits<T>>::value)
    io.beginFlowMapping();
  else
    io.beginMapping();
  if (io.outputting()) {
    std::string Err = detail::doValidate(io, Val, Ctx);
    if (!Err.empty()) {
      errs() << Err << "\n";
      assert(Err.empty() && "invalid struct trying to be written as yaml");
    }
  }
  detail::doMapping(io, Val, Ctx);
  if (!io.outputting()) {
    std::string Err = detail::doValidate(io, Val, Ctx);
    if (!Err.empty())
      io.setError(Err);
  }
  if (has_FlowTraits<MappingTraits<T>>::value)
    io.endFlowMapping();
  else
    io.endMapping();
}

/// Try converting mapping \p Val via an \c enumInput trait on input.
/// \param io YAML IO object.
/// \param Val Value to read.
/// \returns true when enum-input conversion handled \p Val.
template <typename T, typename Context>
bool yamlizeMappingEnumInput(IO &io, T &Val) {
  if constexpr (has_MappingEnumInputTraits<T, Context>::value) {
    if (io.outputting())
      return false;

    io.beginEnumScalar();
    MappingTraits<T>::enumInput(io, Val);
    bool Matched = !io.matchEnumFallback();
    io.endEnumScalar();
    return Matched;
  }
  return false;
}

/// Convert mapping \p Val without a validate hook.
/// \param io YAML IO object.
/// \param Val Value to read or write.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Mapping context passed to traits.
template <typename T, typename Context>
std::enable_if_t<unvalidatedMappingTraits<T, Context>::value, void>
yamlize(IO &io, T &Val, bool Required, Context &Ctx) {
  if (yamlizeMappingEnumInput<T, Context>(io, Val))
    return;
  if (has_FlowTraits<MappingTraits<T>>::value) {
    io.beginFlowMapping();
    detail::doMapping(io, Val, Ctx);
    io.endFlowMapping();
  } else {
    io.beginMapping();
    detail::doMapping(io, Val, Ctx);
    io.endMapping();
  }
}

/// Convert custom mapping \p Val through \c CustomMappingTraits.
/// \param io YAML IO object.
/// \param Val Value to read or write.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Unused empty context.
template <typename T>
std::enable_if_t<has_CustomMappingTraits<T>::value, void>
yamlize(IO &io, T &Val, bool Required, EmptyContext &Ctx) {
  if (io.outputting()) {
    io.beginMapping();
    CustomMappingTraits<T>::output(io, Val);
    io.endMapping();
  } else {
    io.beginMapping();
    for (StringRef key : io.keys())
      CustomMappingTraits<T>::inputOne(io, key, Val);
    io.endMapping();
  }
}

/// Convert polymorphic \p Val through \c PolymorphicTraits.
/// \param io YAML IO object.
/// \param Val Value to read or write.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Unused empty context.
template <typename T>
std::enable_if_t<has_PolymorphicTraits<T>::value, void>
yamlize(IO &io, T &Val, bool Required, EmptyContext &Ctx) {
  switch (io.outputting() ? PolymorphicTraits<T>::getKind(Val)
                          : io.getNodeKind()) {
  case NodeKind::Scalar:
    return yamlize(io, PolymorphicTraits<T>::getAsScalar(Val), true, Ctx);
  case NodeKind::Map:
    return yamlize(io, PolymorphicTraits<T>::getAsMap(Val), true, Ctx);
  case NodeKind::Sequence:
    return yamlize(io, PolymorphicTraits<T>::getAsSequence(Val), true, Ctx);
  }
}

/// Intentionally ill-formed overload that diagnoses missing YAML traits for \c T.
/// \param io YAML IO object.
/// \param Val Unused value reference.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Unused empty context.
template <typename T>
std::enable_if_t<missingTraits<T, EmptyContext>::value, void>
yamlize(IO &io, T &Val, bool Required, EmptyContext &Ctx) {
  char missing_yaml_trait_for_type[sizeof(MissingTrait<T>)];
}

/// Convert sequence \p Seq through \c SequenceTraits.
/// \param io YAML IO object.
/// \param Seq Sequence to read or write.
/// \param Required Whether the value is required; unused by this overload.
/// \param Ctx Context passed to element conversion.
template <typename T, typename Context>
std::enable_if_t<has_SequenceTraits<T>::value, void>
yamlize(IO &io, T &Seq, bool Required, Context &Ctx) {
  if (has_FlowTraits<SequenceTraits<T>>::value) {
    unsigned incnt = io.beginFlowSequence();
    unsigned count = io.outputting() ? SequenceTraits<T>::size(io, Seq) : incnt;
    for (unsigned i = 0; i < count; ++i) {
      void *SaveInfo;
      if (io.preflightFlowElement(i, SaveInfo)) {
        yamlize(io, SequenceTraits<T>::element(io, Seq, i), true, Ctx);
        io.postflightFlowElement(SaveInfo);
      }
    }
    io.endFlowSequence();
  } else {
    unsigned incnt = io.beginSequence();
    unsigned count = io.outputting() ? SequenceTraits<T>::size(io, Seq) : incnt;
    for (unsigned i = 0; i < count; ++i) {
      void *SaveInfo;
      if (io.preflightElement(i, SaveInfo)) {
        yamlize(io, SequenceTraits<T>::element(io, Seq, i), true, Ctx);
        io.postflightElement(SaveInfo);
      }
    }
    io.endSequence();
  }
}

/// YAML scalar traits for \c bool.
template <> struct ScalarTraits<bool> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const bool &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, bool &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c StringRef.
template <> struct ScalarTraits<StringRef> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const StringRef &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, StringRef &Val);
  /// Return the quoting style required for \p S.
  /// \param S Scalar text to inspect.
  /// \returns The minimum quoting style required for \p S.
  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

/// YAML scalar traits for \c std::string.
template <> struct ScalarTraits<std::string> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const std::string &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, std::string &Val);
  /// Return the quoting style required for \p S.
  /// \param S Scalar text to inspect.
  /// \returns The minimum quoting style required for \p S.
  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

/// YAML scalar traits for \c uint8_t.
template <> struct ScalarTraits<uint8_t> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const uint8_t &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, uint8_t &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c uint16_t.
template <> struct ScalarTraits<uint16_t> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const uint16_t &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, uint16_t &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c uint32_t.
template <> struct ScalarTraits<uint32_t> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const uint32_t &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, uint32_t &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c uint64_t.
template <> struct ScalarTraits<uint64_t> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const uint64_t &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, uint64_t &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c int8_t.
template <> struct ScalarTraits<int8_t> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const int8_t &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, int8_t &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c int16_t.
template <> struct ScalarTraits<int16_t> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const int16_t &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, int16_t &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c int32_t.
template <> struct ScalarTraits<int32_t> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const int32_t &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, int32_t &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c int64_t.
template <> struct ScalarTraits<int64_t> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const int64_t &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, int64_t &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c float.
template <> struct ScalarTraits<float> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const float &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, float &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c double.
template <> struct ScalarTraits<double> {
  /// Write \p Val to \p Out.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const double &Val, void *Ctx, raw_ostream &Out);
  /// Parse \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, double &Val);
  /// Scalars of this type never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

// For endian types, we use existing scalar Traits class for the underlying
// type.  This way endian aware types are supported whenever the traits are
// defined for the underlying type.
/// YAML scalar traits for packed endian integral types.
template <typename value_type, llvm::endianness endian, size_t alignment>
struct ScalarTraits<support::detail::packed_endian_specific_integral<
                        value_type, endian, alignment>,
                    std::enable_if_t<has_ScalarTraits<value_type>::value>> {
  /// Underlying packed endian integral type.
  using endian_type =
      support::detail::packed_endian_specific_integral<value_type, endian,
                                                       alignment>;

  /// Write endian value \p E by delegating to the underlying scalar traits.
  /// \param E Value to write.
  /// \param Ctx Client context forwarded to underlying traits.
  /// \param Stream Output stream.
  static void output(const endian_type &E, void *Ctx, raw_ostream &Stream) {
    ScalarTraits<value_type>::output(static_cast<value_type>(E), Ctx, Stream);
  }

  /// Parse \p Str into endian value \p E via the underlying scalar traits.
  /// \param Str YAML scalar text.
  /// \param Ctx Client context forwarded to underlying traits.
  /// \param E Destination endian value.
  /// \returns Empty on success; otherwise an error string.
  static StringRef input(StringRef Str, void *Ctx, endian_type &E) {
    value_type V;
    auto R = ScalarTraits<value_type>::input(Str, Ctx, V);
    E = static_cast<endian_type>(V);
    return R;
  }

  /// Return quoting style by delegating to the underlying scalar traits.
  /// \param Str Scalar text to inspect.
  /// \returns The quoting style from the underlying scalar traits.
  static QuotingType mustQuote(StringRef Str) {
    return ScalarTraits<value_type>::mustQuote(Str);
  }
};

/// YAML enumeration traits for packed endian integral types.
template <typename value_type, llvm::endianness endian, size_t alignment>
struct ScalarEnumerationTraits<
    support::detail::packed_endian_specific_integral<value_type, endian,
                                                     alignment>,
    std::enable_if_t<has_ScalarEnumerationTraits<value_type>::value>> {
  /// Underlying packed endian integral type.
  using endian_type =
      support::detail::packed_endian_specific_integral<value_type, endian,
                                                       alignment>;

  /// Convert endian enumeration \p E via the underlying enumeration traits.
  /// \param io YAML IO object.
  /// \param E Endian value to read or write.
  static void enumeration(IO &io, endian_type &E) {
    value_type V = E;
    ScalarEnumerationTraits<value_type>::enumeration(io, V);
    E = V;
  }
};

/// YAML bit-set traits for packed endian integral types.
template <typename value_type, llvm::endianness endian, size_t alignment>
struct ScalarBitSetTraits<
    support::detail::packed_endian_specific_integral<value_type, endian,
                                                     alignment>,
    std::enable_if_t<has_ScalarBitSetTraits<value_type>::value>> {
  /// Underlying packed endian integral type.
  using endian_type =
      support::detail::packed_endian_specific_integral<value_type, endian,
                                                       alignment>;
  /// Convert endian bit-set \p E via the underlying bit-set traits.
  /// \param io YAML IO object.
  /// \param E Endian value to read or write.
  static void bitset(IO &io, endian_type &E) {
    value_type V = E;
    ScalarBitSetTraits<value_type>::bitset(io, V);
    E = V;
  }
};

/// Stack-allocated helper that [de]normalizes \c TFinal via \c TNorm for YAML I/O.
///
/// Utility for use within MappingTraits<>::mapping() method
/// to [de]normalize an object for use with YAML conversion.
template <typename TNorm, typename TFinal> struct MappingNormalization {
  /// Construct a normalization buffer for \p Obj using \p i_o.
  /// \param i_o YAML IO object controlling input vs output.
  /// \param Obj Final object being mapped.
  MappingNormalization(IO &i_o, TFinal &Obj)
      : io(i_o), BufPtr(nullptr), Result(Obj) {
    if (io.outputting()) {
      BufPtr = new (&Buffer) TNorm(io, Obj);
    } else {
      BufPtr = new (&Buffer) TNorm(io);
    }
  }

  /// Destroy the normalized object and denormalize back into the final object.
  ~MappingNormalization() {
    if (!io.outputting()) {
      Result = BufPtr->denormalize(io);
    }
    BufPtr->~TNorm();
  }

  /// Access the normalized object.
  /// \returns A pointer to the normalized object.
  TNorm *operator->() { return BufPtr; }

private:
  using Storage = AlignedCharArrayUnion<TNorm>;

  Storage Buffer;
  IO &io;
  TNorm *BufPtr;
  TFinal &Result;
};

/// Heap/bump-allocated helper that [de]normalizes \c TFinal via \c TNorm for YAML I/O.
///
/// Utility for use within MappingTraits<>::mapping() method
/// to [de]normalize an object for use with YAML conversion.
template <typename TNorm, typename TFinal> struct MappingNormalizationHeap {
  /// Construct a normalization buffer for \p Obj, optionally using \p allocator.
  /// \param i_o YAML IO object controlling input vs output.
  /// \param Obj Final object being mapped.
  /// \param allocator Optional bump allocator for the normalized object.
  MappingNormalizationHeap(IO &i_o, TFinal &Obj, BumpPtrAllocator *allocator)
      : io(i_o), Result(Obj) {
    if (io.outputting()) {
      BufPtr = new (&Buffer) TNorm(io, Obj);
    } else if (allocator) {
      BufPtr = allocator->Allocate<TNorm>();
      new (BufPtr) TNorm(io);
    } else {
      BufPtr = new TNorm(io);
    }
  }

  /// Destroy the normalized object and denormalize back into the final object.
  ~MappingNormalizationHeap() {
    if (io.outputting()) {
      BufPtr->~TNorm();
    } else {
      Result = BufPtr->denormalize(io);
    }
  }

  /// Access the normalized object.
  /// \returns A pointer to the normalized object.
  TNorm *operator->() { return BufPtr; }

private:
  using Storage = AlignedCharArrayUnion<TNorm>;

  Storage Buffer;
  IO &io;
  TNorm *BufPtr = nullptr;
  TFinal &Result;
};

/// Parses YAML text into in-memory structs and vectors.
///
/// It works by using YAMLParser to do a syntax parse of the entire yaml
/// document, then the Input class builds a graph of HNodes which wraps
/// each yaml Node.  The extra layer is buffering.  The low level yaml
/// parser only lets you look at each node once.  The buffering layer lets
/// you search and interate multiple times.  This is necessary because
/// the mapRequired() method calls may not be in the same order
/// as the keys in the document.
class LLVM_ABI Input : public IO {
public:
  /// Construct a YAML input from string content \p InputContent.
  /// \param InputContent YAML document text.
  /// \param Ctxt Optional opaque client context.
  /// \param DiagHandler Optional diagnostic callback.
  /// \param DiagHandlerCtxt Context passed to \p DiagHandler.
  Input(StringRef InputContent, void *Ctxt = nullptr,
        SourceMgr::DiagHandlerTy DiagHandler = nullptr,
        void *DiagHandlerCtxt = nullptr);
  /// Construct a YAML input from memory buffer \p Input.
  /// \param Input YAML document buffer.
  /// \param Ctxt Optional opaque client context.
  /// \param DiagHandler Optional diagnostic callback.
  /// \param DiagHandlerCtxt Context passed to \p DiagHandler.
  Input(MemoryBufferRef Input, void *Ctxt = nullptr,
        SourceMgr::DiagHandlerTy DiagHandler = nullptr,
        void *DiagHandlerCtxt = nullptr);
  /// Destroy the input object and release buffered nodes.
  ~Input() override;

  /// Return any syntax or semantic error encountered during parsing.
  /// \returns The error code from parsing, if any.
  std::error_code error() override;

private:
  bool outputting() const override;
  bool mapTag(StringRef, bool) override;
  void beginMapping() override;
  void endMapping() override;
  bool preflightKey(StringRef Key, bool, bool, bool &, void *&) override;
  void postflightKey(void *) override;
  std::vector<StringRef> keys() override;
  void beginFlowMapping() override;
  void endFlowMapping() override;
  unsigned beginSequence() override;
  void endSequence() override;
  bool preflightElement(unsigned index, void *&) override;
  void postflightElement(void *) override;
  unsigned beginFlowSequence() override;
  bool preflightFlowElement(unsigned, void *&) override;
  void postflightFlowElement(void *) override;
  void endFlowSequence() override;
  void beginEnumScalar() override;
  bool matchEnumScalar(StringRef, bool) override;
  bool matchEnumFallback() override;
  void endEnumScalar() override;
  bool beginBitSetScalar(bool &) override;
  bool bitSetMatch(StringRef, bool) override;
  void endBitSetScalar() override;
  void scalarString(StringRef &, QuotingType) override;
  void blockScalarString(StringRef &) override;
  void scalarTag(std::string &) override;
  NodeKind getNodeKind() override;
  void setError(const Twine &message) override;
  bool canElideEmptySequence() override;

  class HNode {
  public:
    HNode(Node *n) : _node(n) {}

    static bool classof(const HNode *) { return true; }

    Node *_node;
  };

  class EmptyHNode : public HNode {
  public:
    EmptyHNode(Node *n) : HNode(n) {}

    static bool classof(const HNode *n) { return NullNode::classof(n->_node); }

    static bool classof(const EmptyHNode *) { return true; }
  };

  class ScalarHNode : public HNode {
  public:
    ScalarHNode(Node *n, StringRef s) : HNode(n), _value(s) {}

    StringRef value() const { return _value; }

    static bool classof(const HNode *n) {
      return ScalarNode::classof(n->_node) ||
             BlockScalarNode::classof(n->_node);
    }

    static bool classof(const ScalarHNode *) { return true; }

  protected:
    StringRef _value;
  };

  class MapHNode : public HNode {
  public:
    MapHNode(Node *n) : HNode(n) {}

    static bool classof(const HNode *n) {
      return MappingNode::classof(n->_node);
    }

    static bool classof(const MapHNode *) { return true; }

    using NameToNodeAndLoc = StringMap<std::pair<HNode *, SMRange>>;

    NameToNodeAndLoc Mapping;
    SmallVector<std::string, 6> ValidKeys;
  };

  class SequenceHNode : public HNode {
  public:
    SequenceHNode(Node *n) : HNode(n) {}

    static bool classof(const HNode *n) {
      return SequenceNode::classof(n->_node);
    }

    static bool classof(const SequenceHNode *) { return true; }

    std::vector<HNode *> Entries;
  };

  void saveAliasHNode(Node *node, HNode *hnode);
  Input::HNode *createHNodes(Node *node);
  void setError(HNode *hnode, const Twine &message);
  void setError(Node *node, const Twine &message);
  void setError(const SMRange &Range, const Twine &message);

  void reportWarning(HNode *hnode, const Twine &message);
  void reportWarning(Node *hnode, const Twine &message);
  void reportWarning(const SMRange &Range, const Twine &message);

  /// Release memory used by HNodes.
  void releaseHNodeBuffers();

public:
  /// Position input on the current YAML document (used by \c operator>>).
  /// \returns true when a current document is available.
  bool setCurrentDocument();
  /// Advance to the next YAML document in the stream (used by \c operator>>).
  /// \returns true when another document is available.
  bool nextDocument();

  /// Returns the current node that's being parsed by the YAML Parser.
  /// \returns The current YAML parser node, or null if none.
  const Node *getCurrentNode() const;

  /// Allow (or reject) unknown mapping keys during input.
  /// \param Allow When true, unknown keys are ignored instead of errors.
  void setAllowUnknownKeys(bool Allow) override;

private:
  SourceMgr SrcMgr; // must be before Strm
  std::unique_ptr<llvm::yaml::Stream> Strm;
  HNode *TopNode = nullptr;
  std::error_code EC;
  BumpPtrAllocator StringAllocator;
  SpecificBumpPtrAllocator<EmptyHNode> EmptyHNodeAllocator;
  SpecificBumpPtrAllocator<ScalarHNode> ScalarHNodeAllocator;
  SpecificBumpPtrAllocator<MapHNode> MapHNodeAllocator;
  SpecificBumpPtrAllocator<SequenceHNode> SequenceHNodeAllocator;
  document_iterator DocIterator;
  llvm::BitVector BitValuesUsed;
  HNode *CurrentNode = nullptr;
  bool ScalarMatchFound = false;
  bool AllowUnknownKeys = false;
  DenseMap<StringRef, HNode *> AliasMap;
};

/// Generates YAML text from in-memory structs and vectors.
class LLVM_ABI Output : public IO {
public:
  /// Construct a YAML output writing to \p Out.
  /// \param Out Destination stream.
  /// \param Ctxt Optional opaque client context.
  /// \param WrapColumn Column at which to wrap flow output (default 70).
  Output(raw_ostream &Out, void *Ctxt = nullptr, int WrapColumn = 70);
  /// Destroy the output object.
  ~Output() override;

  /// Control whether optional values equal to their defaults are written.
  ///
  /// By default, when outputting if you attempt to write a value that is equal
  /// to the default, the value gets ignored. Sometimes, it is useful to be able
  /// to see these in the resulting YAML anyway.
  ///
  /// \param Write When true, write optional values even when equal to default.
  void setWriteDefaultValues(bool Write) { WriteDefaultValues = Write; }

  /// Always returns true because this IO writes YAML.
  /// \returns true.
  bool outputting() const override;
  /// Emit mapping tag \p Tag when appropriate.
  /// \param Tag YAML tag string.
  /// \param Default Whether this tag is the default tag.
  /// \returns true when the tag was accepted.
  bool mapTag(StringRef Tag, bool Default = false) override;
  /// Begin writing a block-style mapping.
  void beginMapping() override;
  /// Finish writing a block-style mapping.
  void endMapping() override;
  /// Prepare to write mapping key \p Key.
  /// \param Key Mapping key text.
  /// \param Required Whether the key is required.
  /// \param SameAsDefault Whether the value equals its default.
  /// \param UseDefault Set when the default path is taken.
  /// \param SaveInfo Opaque state for \c postflightKey.
  /// \returns true if the key/value should be written.
  bool preflightKey(StringRef Key, bool Required, bool SameAsDefault,
                    bool &UseDefault, void *&SaveInfo) override;
  /// Finish writing a mapping key.
  /// \param SaveInfo Opaque state from \c preflightKey.
  void postflightKey(void *SaveInfo) override;
  /// Unsupported on output; returns an empty key list.
  /// \returns An empty list of keys.
  std::vector<StringRef> keys() override;
  /// Begin writing a flow-style mapping.
  void beginFlowMapping() override;
  /// Finish writing a flow-style mapping.
  void endFlowMapping() override;
  /// Begin writing a block-style sequence.
  /// \returns Unused on output.
  unsigned beginSequence() override;
  /// Finish writing a block-style sequence.
  void endSequence() override;
  /// Prepare sequence element \p Index.
  /// \param Index Zero-based element index.
  /// \param SaveInfo Opaque state for \c postflightElement.
  /// \returns true if the element should be written.
  bool preflightElement(unsigned Index, void *&SaveInfo) override;
  /// Finish writing a sequence element.
  /// \param SaveInfo Opaque state from \c preflightElement.
  void postflightElement(void *SaveInfo) override;
  /// Begin writing a flow-style sequence.
  /// \returns Unused on output.
  unsigned beginFlowSequence() override;
  /// Prepare flow-sequence element \p Index.
  /// \param Index Zero-based element index.
  /// \param SaveInfo Opaque state for \c postflightFlowElement.
  /// \returns true if the element should be written.
  bool preflightFlowElement(unsigned Index, void *&SaveInfo) override;
  /// Finish writing a flow-sequence element.
  /// \param SaveInfo Opaque state from \c preflightFlowElement.
  void postflightFlowElement(void *SaveInfo) override;
  /// Finish writing a flow-style sequence.
  void endFlowSequence() override;
  /// Begin writing an enumeration scalar.
  void beginEnumScalar() override;
  /// Select enumeration case \p Str when \p Match is true.
  /// \param Str Enumeration case spelling.
  /// \param Match Whether this case is the selected value.
  /// \returns true when this case is selected.
  bool matchEnumScalar(StringRef Str, bool Match) override;
  /// Handle enumeration fallback on output.
  /// \returns true when fallback conversion should run.
  bool matchEnumFallback() override;
  /// Finish writing an enumeration scalar.
  void endEnumScalar() override;
  /// Begin writing a bit-set scalar.
  /// \param DoClear Set when the bit-set should be cleared first.
  /// \returns true if bit-set conversion should proceed.
  bool beginBitSetScalar(bool &DoClear) override;
  /// Emit bit-set case \p Str when \p Match is true.
  /// \param Str Bit-set case spelling.
  /// \param Match Whether this bit is set in the value.
  /// \returns true when this bit case matches.
  bool bitSetMatch(StringRef Str, bool Match) override;
  /// Finish writing a bit-set scalar.
  void endBitSetScalar() override;
  /// Write scalar string \p Str with quoting style \p Quote.
  /// \param Str Scalar text to write.
  /// \param Quote Quoting style to apply.
  void scalarString(StringRef &Str, QuotingType Quote) override;
  /// Write block scalar string \p Str.
  /// \param Str Block scalar text to write.
  void blockScalarString(StringRef &Str) override;
  /// Write scalar tag \p Tag.
  /// \param Tag Tag text to write.
  void scalarTag(std::string &Tag) override;
  /// Unsupported on output; reports an error if called.
  /// \returns The node kind; not meaningful on output.
  NodeKind getNodeKind() override;
  /// Record output error \p message.
  /// \param message Human-readable error description.
  void setError(const Twine &message) override;
  /// Return the current output error state.
  /// \returns The current output error state.
  std::error_code error() override;
  /// Return true if empty sequences may be omitted.
  /// \returns true if empty sequences may be omitted.
  bool canElideEmptySequence() override;

  /// Begin a multi-document YAML stream (used by \c operator<<).
  void beginDocuments();
  /// Prepare document \p Index in the stream.
  /// \param Index Zero-based document index.
  /// \returns true if the document should be written.
  bool preflightDocument(unsigned Index);
  /// Finish writing the current document.
  void postflightDocument();
  /// Finish the multi-document YAML stream.
  void endDocuments();

private:
  void output(StringRef s);
  void output(StringRef, QuotingType);
  void outputUpToEndOfLine(StringRef s);
  void newLineCheck(bool EmptySequence = false);
  void outputNewLine();
  void paddedKey(StringRef key);
  void flowKey(StringRef Key);

  enum InState {
    inSeqFirstElement,
    inSeqOtherElement,
    inFlowSeqFirstElement,
    inFlowSeqOtherElement,
    inMapFirstKey,
    inMapOtherKey,
    inFlowMapFirstKey,
    inFlowMapOtherKey
  };

  static bool inSeqAnyElement(InState State);
  static bool inFlowSeqAnyElement(InState State);
  static bool inMapAnyKey(InState State);
  static bool inFlowMapAnyKey(InState State);

  raw_ostream &Out;
  int WrapColumn;
  SmallVector<InState, 8> StateStack;
  int Column = 0;
  int ColumnAtFlowStart = 0;
  int ColumnAtMapFlowStart = 0;
  bool NeedBitValueComma = false;
  bool NeedFlowSequenceComma = false;
  bool EnumerationMatchFound = false;
  bool WriteDefaultValues = false;
  StringRef Padding;
  StringRef PaddingBeforeContainer;
};

template <typename T, typename Context>
void IO::processKeyWithDefault(StringRef Key, std::optional<T> &Val,
                               const std::optional<T> &DefaultValue,
                               bool Required, Context &Ctx) {
  assert(!DefaultValue && "std::optional<T> shouldn't have a value!");
  void *SaveInfo;
  bool UseDefault = true;
  const bool sameAsDefault = outputting() && !Val;
  if (!outputting() && !Val)
    Val = T();
  if (Val &&
      this->preflightKey(Key, Required, sameAsDefault, UseDefault, SaveInfo)) {

    // When reading an std::optional<X> key from a YAML description, we allow
    // the special "<none>" value, which can be used to specify that no value
    // was requested, i.e. the DefaultValue will be assigned. The DefaultValue
    // is usually None.
    bool IsNone = false;
    if (!outputting())
      if (const auto *Node =
              dyn_cast<ScalarNode>(((Input *)this)->getCurrentNode()))
        // We use rtrim to ignore possible white spaces that might exist when a
        // comment is present on the same line.
        IsNone = Node->getRawValue().rtrim(' ') == "<none>";

    if (IsNone)
      Val = DefaultValue;
    else
      yamlize(*this, *Val, Required, Ctx);
    this->postflightKey(SaveInfo);
  } else {
    if (UseDefault)
      Val = DefaultValue;
  }
}

/// Define a distinct strong typedef \c _type over integral base \c _base.
///
/// YAML I/O does conversion based on types. But often native data types
/// are just a typedef of built in intergral types (e.g. int).  But the C++
/// type matching system sees through the typedef and all the typedefed types
/// look like a built in type. This will cause the generic YAML I/O conversion
/// to be used. To provide better control over the YAML conversion, you can
/// use this macro instead of typedef.  It will create a class with one field
/// and automatic conversion operators to and from the base type.
/// Based on BOOST_STRONG_TYPEDEF
/// \param _base Underlying integral type.
/// \param _type Name of the strong typedef to define.
#define LLVM_YAML_STRONG_TYPEDEF(_base, _type)                                 \
  struct _type {                                                               \
    _type() = default;                                                         \
    /** Construct from base value \p v. */                                     \
    /** \param v Value to store. */                                            \
    _type(const _base v) : value(v) {}                                         \
    /** Copy-construct from another \c _type. */                               \
    /** \param v Value to copy. */                                             \
    _type(const _type &v) = default;                                           \
    /** Copy-assign from another \c _type. */                                  \
    /** \param rhs Value to assign. */                                         \
    _type &operator=(const _type &rhs) = default;                              \
    /** Assign from base value \p rhs. */                                      \
    /** \param rhs Base value to assign. */                                    \
    _type &operator=(const _base &rhs) {                                       \
      value = rhs;                                                             \
      return *this;                                                            \
    }                                                                          \
    /** Convert to a const reference to the base value. */                     \
    operator const _base &() const { return value; }                           \
    /** Return true if this equals \p rhs. */                                  \
    /** \param rhs Value to compare. */                                        \
    bool operator==(const _type &rhs) const { return value == rhs.value; }     \
    /** Return true if this equals base value \p rhs. */                       \
    /** \param rhs Base value to compare. */                                   \
    bool operator==(const _base &rhs) const { return value == rhs; }           \
    /** Return true if this is less than \p rhs. */                            \
    /** \param rhs Value to compare. */                                        \
    bool operator<(const _type &rhs) const { return value < rhs.value; }       \
    _base value;                                                               \
    /** Underlying integral base type. */                                      \
    using BaseType = _base;                                                    \
  };

// Use these types instead of uintXX_t in any mapping to have
// its yaml output formatted as hexadecimal.

/// Unsigned uint8_t value formatted as hexadecimal in YAML.
struct Hex8 {
  /// Construct a zero-initialized hexadecimal value.
  Hex8() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  Hex8(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c Hex8.
  /// \param v Value to copy.
  Hex8(const Hex8 &v) = default;
  /// Copy-assign from another \c Hex8.
  /// \param rhs Value to assign.
  /// \returns A reference to this object.
  Hex8 &operator=(const Hex8 &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \returns A reference to this object.
  Hex8 &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \returns A const reference to the stored value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \returns true if the values are equal.
  bool operator==(const Hex8 &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \returns true if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \returns true if this value is less than \p rhs.
  bool operator<(const Hex8 &rhs) const { return value < rhs.value; }
  /// Underlying integer value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};
/// Unsigned uint16_t value formatted as hexadecimal in YAML.
struct Hex16 {
  /// Construct a zero-initialized hexadecimal value.
  Hex16() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  Hex16(const uint16_t v) : value(v) {}
  /// Copy-construct from another \c Hex16.
  /// \param v Value to copy.
  Hex16(const Hex16 &v) = default;
  /// Copy-assign from another \c Hex16.
  /// \param rhs Value to assign.
  /// \returns A reference to this object.
  Hex16 &operator=(const Hex16 &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \returns A reference to this object.
  Hex16 &operator=(const uint16_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \returns A const reference to the stored value.
  operator const uint16_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \returns true if the values are equal.
  bool operator==(const Hex16 &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \returns true if the values are equal.
  bool operator==(const uint16_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \returns true if this value is less than \p rhs.
  bool operator<(const Hex16 &rhs) const { return value < rhs.value; }
  /// Underlying integer value.
  uint16_t value;
  /// Underlying integral base type.
  using BaseType = uint16_t;
};
/// Unsigned uint32_t value formatted as hexadecimal in YAML.
struct Hex32 {
  /// Construct a zero-initialized hexadecimal value.
  Hex32() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  Hex32(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c Hex32.
  /// \param v Value to copy.
  Hex32(const Hex32 &v) = default;
  /// Copy-assign from another \c Hex32.
  /// \param rhs Value to assign.
  /// \returns A reference to this object.
  Hex32 &operator=(const Hex32 &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \returns A reference to this object.
  Hex32 &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \returns A const reference to the stored value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \returns true if the values are equal.
  bool operator==(const Hex32 &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \returns true if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \returns true if this value is less than \p rhs.
  bool operator<(const Hex32 &rhs) const { return value < rhs.value; }
  /// Underlying integer value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};
/// Unsigned uint64_t value formatted as hexadecimal in YAML.
struct Hex64 {
  /// Construct a zero-initialized hexadecimal value.
  Hex64() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  Hex64(const uint64_t v) : value(v) {}
  /// Copy-construct from another \c Hex64.
  /// \param v Value to copy.
  Hex64(const Hex64 &v) = default;
  /// Copy-assign from another \c Hex64.
  /// \param rhs Value to assign.
  /// \returns A reference to this object.
  Hex64 &operator=(const Hex64 &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \returns A reference to this object.
  Hex64 &operator=(const uint64_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \returns A const reference to the stored value.
  operator const uint64_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \returns true if the values are equal.
  bool operator==(const Hex64 &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \returns true if the values are equal.
  bool operator==(const uint64_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \returns true if this value is less than \p rhs.
  bool operator<(const Hex64 &rhs) const { return value < rhs.value; }
  /// Underlying integer value.
  uint64_t value;
  /// Underlying integral base type.
  using BaseType = uint64_t;
};

/// YAML scalar traits that format \c Hex8 as hexadecimal.
template <> struct ScalarTraits<Hex8> {
  /// Write \p Val to \p Out as hexadecimal.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const Hex8 &Val, void *Ctx, raw_ostream &Out);
  /// Parse hexadecimal \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, Hex8 &Val);
  /// Hex scalars never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits that format \c Hex16 as hexadecimal.
template <> struct ScalarTraits<Hex16> {
  /// Write \p Val to \p Out as hexadecimal.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const Hex16 &Val, void *Ctx, raw_ostream &Out);
  /// Parse hexadecimal \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, Hex16 &Val);
  /// Hex scalars never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits that format \c Hex32 as hexadecimal.
template <> struct ScalarTraits<Hex32> {
  /// Write \p Val to \p Out as hexadecimal.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const Hex32 &Val, void *Ctx, raw_ostream &Out);
  /// Parse hexadecimal \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, Hex32 &Val);
  /// Hex scalars never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits that format \c Hex64 as hexadecimal.
template <> struct ScalarTraits<Hex64> {
  /// Write \p Val to \p Out as hexadecimal.
  /// \param Val Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const Hex64 &Val, void *Ctx, raw_ostream &Out);
  /// Parse hexadecimal \p Scalar into \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, Hex64 &Val);
  /// Hex scalars never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// YAML scalar traits for \c VersionTuple.
template <> struct ScalarTraits<VersionTuple> {
  /// Write version tuple \p Value to \p Out.
  /// \param Value Value to write.
  /// \param Ctx Unused client context.
  /// \param Out Output stream.
  LLVM_ABI static void output(const VersionTuple &Value, void *Ctx,
                              llvm::raw_ostream &Out);
  /// Parse \p Scalar into version tuple \p Val.
  /// \param Scalar YAML scalar text.
  /// \param Ctx Unused client context.
  /// \param Val Destination value.
  /// \returns Empty on success; otherwise an error string.
  LLVM_ABI static StringRef input(StringRef Scalar, void *Ctx, VersionTuple &Val);
  /// Version tuples never require quotes.
  /// \param Scalar Unused scalar text.
  /// \returns \c QuotingType::None.
  static QuotingType mustQuote(StringRef Scalar) { return QuotingType::None; }
};

/// Stream document list \p docList from YAML input \p yin.
/// \param yin YAML input.
/// \param docList Destination document list.
/// \returns A reference to the YAML input.
template <typename T>
inline std::enable_if_t<has_DocumentListTraits<T>::value, Input &>
operator>>(Input &yin, T &docList) {
  int i = 0;
  EmptyContext Ctx;
  while (yin.setCurrentDocument()) {
    yamlize(yin, DocumentListTraits<T>::element(yin, docList, i), true, Ctx);
    if (yin.error())
      return yin;
    yin.nextDocument();
    ++i;
  }
  return yin;
}

/// Stream mapping document \p docMap from YAML input \p yin.
/// \param yin YAML input.
/// \param docMap Destination mapping value.
/// \returns A reference to the YAML input.
template <typename T>
inline std::enable_if_t<has_MappingTraits<T, EmptyContext>::value, Input &>
operator>>(Input &yin, T &docMap) {
  EmptyContext Ctx;
  yin.setCurrentDocument();
  yamlize(yin, docMap, true, Ctx);
  return yin;
}

/// Stream sequence document \p docSeq from YAML input \p yin.
/// \param yin YAML input.
/// \param docSeq Destination sequence value.
/// \returns A reference to the YAML input.
template <typename T>
inline std::enable_if_t<has_SequenceTraits<T>::value, Input &>
operator>>(Input &yin, T &docSeq) {
  EmptyContext Ctx;
  if (yin.setCurrentDocument())
    yamlize(yin, docSeq, true, Ctx);
  return yin;
}

/// Stream block scalar \p Val from YAML input \p In.
/// \param In YAML input.
/// \param Val Destination block scalar value.
/// \returns A reference to the YAML input.
template <typename T>
inline std::enable_if_t<has_BlockScalarTraits<T>::value, Input &>
operator>>(Input &In, T &Val) {
  EmptyContext Ctx;
  if (In.setCurrentDocument())
    yamlize(In, Val, true, Ctx);
  return In;
}

/// Stream custom mapping \p Val from YAML input \p In.
/// \param In YAML input.
/// \param Val Destination custom mapping value.
/// \returns A reference to the YAML input.
template <typename T>
inline std::enable_if_t<has_CustomMappingTraits<T>::value, Input &>
operator>>(Input &In, T &Val) {
  EmptyContext Ctx;
  if (In.setCurrentDocument())
    yamlize(In, Val, true, Ctx);
  return In;
}

/// Stream polymorphic value \p Val from YAML input \p In.
/// \param In YAML input.
/// \param Val Destination polymorphic value.
/// \returns A reference to the YAML input.
template <typename T>
inline std::enable_if_t<has_PolymorphicTraits<T>::value, Input &>
operator>>(Input &In, T &Val) {
  EmptyContext Ctx;
  if (In.setCurrentDocument())
    yamlize(In, Val, true, Ctx);
  return In;
}

/// Ill-formed input overload that diagnoses missing YAML traits for \c T.
/// \param yin YAML input.
/// \param docSeq Unused destination reference.
/// \returns A reference to the YAML input.
template <typename T>
inline std::enable_if_t<missingTraits<T, EmptyContext>::value, Input &>
operator>>(Input &yin, T &docSeq) {
  char missing_yaml_trait_for_type[sizeof(MissingTrait<T>)];
  return yin;
}

/// Stream document list \p docList to YAML output \p yout.
/// \param yout YAML output.
/// \param docList Source document list.
/// \returns A reference to the YAML output.
template <typename T>
inline std::enable_if_t<has_DocumentListTraits<T>::value, Output &>
operator<<(Output &yout, T &docList) {
  EmptyContext Ctx;
  yout.beginDocuments();
  const size_t count = DocumentListTraits<T>::size(yout, docList);
  for (size_t i = 0; i < count; ++i) {
    if (yout.preflightDocument(i)) {
      yamlize(yout, DocumentListTraits<T>::element(yout, docList, i), true,
              Ctx);
      yout.postflightDocument();
    }
  }
  yout.endDocuments();
  return yout;
}

/// Stream mapping \p map to YAML output \p yout.
/// \param yout YAML output.
/// \param map Source mapping value.
/// \returns A reference to the YAML output.
template <typename T>
inline std::enable_if_t<has_MappingTraits<T, EmptyContext>::value, Output &>
operator<<(Output &yout, T &map) {
  EmptyContext Ctx;
  yout.beginDocuments();
  if (yout.preflightDocument(0)) {
    yamlize(yout, map, true, Ctx);
    yout.postflightDocument();
  }
  yout.endDocuments();
  return yout;
}

/// Stream sequence \p seq to YAML output \p yout.
/// \param yout YAML output.
/// \param seq Source sequence value.
/// \returns A reference to the YAML output.
template <typename T>
inline std::enable_if_t<has_SequenceTraits<T>::value, Output &>
operator<<(Output &yout, T &seq) {
  EmptyContext Ctx;
  yout.beginDocuments();
  if (yout.preflightDocument(0)) {
    yamlize(yout, seq, true, Ctx);
    yout.postflightDocument();
  }
  yout.endDocuments();
  return yout;
}

/// Stream block scalar \p Val to YAML output \p Out.
/// \param Out YAML output.
/// \param Val Source block scalar value.
/// \returns A reference to the YAML output.
template <typename T>
inline std::enable_if_t<has_BlockScalarTraits<T>::value, Output &>
operator<<(Output &Out, T &Val) {
  EmptyContext Ctx;
  Out.beginDocuments();
  if (Out.preflightDocument(0)) {
    yamlize(Out, Val, true, Ctx);
    Out.postflightDocument();
  }
  Out.endDocuments();
  return Out;
}

/// Stream custom mapping \p Val to YAML output \p Out.
/// \param Out YAML output.
/// \param Val Source custom mapping value.
/// \returns A reference to the YAML output.
template <typename T>
inline std::enable_if_t<has_CustomMappingTraits<T>::value, Output &>
operator<<(Output &Out, T &Val) {
  EmptyContext Ctx;
  Out.beginDocuments();
  if (Out.preflightDocument(0)) {
    yamlize(Out, Val, true, Ctx);
    Out.postflightDocument();
  }
  Out.endDocuments();
  return Out;
}

/// Stream polymorphic value \p Val to YAML output \p Out.
/// \param Out YAML output.
/// \param Val Source polymorphic value.
/// \returns A reference to the YAML output.
template <typename T>
inline std::enable_if_t<has_PolymorphicTraits<T>::value, Output &>
operator<<(Output &Out, T &Val) {
  EmptyContext Ctx;
  Out.beginDocuments();
  if (Out.preflightDocument(0)) {
    // FIXME: The parser does not support explicit documents terminated with a
    // plain scalar; the end-marker is included as part of the scalar token.
    assert(PolymorphicTraits<T>::getKind(Val) != NodeKind::Scalar &&
           "plain scalar documents are not supported");
    yamlize(Out, Val, true, Ctx);
    Out.postflightDocument();
  }
  Out.endDocuments();
  return Out;
}

/// Ill-formed output overload that diagnoses missing YAML traits for \c T.
/// \param yout YAML output.
/// \param seq Unused source reference.
/// \returns A reference to the YAML output.
template <typename T>
inline std::enable_if_t<missingTraits<T, EmptyContext>::value, Output &>
operator<<(Output &yout, T &seq) {
  char missing_yaml_trait_for_type[sizeof(MissingTrait<T>)];
  return yout;
}

/// Empty base used when a sequence is not flow-formatted.
template <bool B> struct IsFlowSequenceBase {};
/// Base that marks a sequence specialization as flow-formatted.
template <> struct IsFlowSequenceBase<true> {
  /// When true, emit the sequence in flow style.
  static const bool flow = true;
};

/// Detects whether \c T supports \c resize(0).
template <typename T>
using check_resize_t = decltype(std::declval<T>().resize(0));

/// Base providing resizable (or bounds-checked) sequence element access.
template <typename T> struct IsResizableBase {
  /// Element type of sequence \c T.
  using type = typename T::value_type;

  /// Return a reference to element \p index of \p seq, growing it when possible.
  /// \param io YAML IO used to report overflow for fixed-size sequences.
  /// \param seq Sequence container.
  /// \param index Zero-based element index.
  /// \returns A reference to the element at \p index.
  static type &element(IO &io, T &seq, size_t index) {
    if constexpr (is_detected<check_resize_t, T>::value) {
      if (index >= seq.size())
        seq.resize(index + 1);
    } else {
      if (index >= seq.size()) {
        io.setError(Twine("value sequence extends beyond static size (") +
                    Twine(seq.size()) + ")");
        return seq[0];
      }
    }
    return seq[index];
  }
};

/// Shared \c SequenceTraits implementation for standard sequence containers.
template <typename T, bool Flow>
struct SequenceTraitsImpl : IsFlowSequenceBase<Flow>, IsResizableBase<T> {
  /// Return the number of elements in \p seq.
  /// \param io Unused YAML IO object.
  /// \param seq Sequence container.
  /// \returns The number of elements in \p seq.
  static size_t size(IO &io, T &seq) { return seq.size(); }
};

/// Helper that validates an expression can be used as a bool template argument.
template <bool> struct CheckIsBool {
  /// Always true when the template argument is a valid bool constant.
  static const bool value = true;
};

/// \c SequenceTraits for \c std::vector when element flow traits exist.
template <typename T>
struct SequenceTraits<
    std::vector<T>,
    std::enable_if_t<CheckIsBool<SequenceElementTraits<T>::flow>::value>>
    : SequenceTraitsImpl<std::vector<T>, SequenceElementTraits<T>::flow> {};
/// \c SequenceTraits for \c std::array when element flow traits exist.
template <typename T, size_t N>
struct SequenceTraits<
    std::array<T, N>,
    std::enable_if_t<CheckIsBool<SequenceElementTraits<T>::flow>::value>>
    : SequenceTraitsImpl<std::array<T, N>, SequenceElementTraits<T>::flow> {};
/// \c SequenceTraits for \c SmallVector when element flow traits exist.
template <typename T, unsigned N>
struct SequenceTraits<
    SmallVector<T, N>,
    std::enable_if_t<CheckIsBool<SequenceElementTraits<T>::flow>::value>>
    : SequenceTraitsImpl<SmallVector<T, N>, SequenceElementTraits<T>::flow> {};
/// \c SequenceTraits for \c SmallVectorImpl when element flow traits exist.
template <typename T>
struct SequenceTraits<
    SmallVectorImpl<T>,
    std::enable_if_t<CheckIsBool<SequenceElementTraits<T>::flow>::value>>
    : SequenceTraitsImpl<SmallVectorImpl<T>, SequenceElementTraits<T>::flow> {};
/// \c SequenceTraits for \c MutableArrayRef when element flow traits exist.
template <typename T>
struct SequenceTraits<
    MutableArrayRef<T>,
    std::enable_if_t<CheckIsBool<SequenceElementTraits<T>::flow>::value>>
    : SequenceTraitsImpl<MutableArrayRef<T>, SequenceElementTraits<T>::flow> {};

/// Sequences of fundamental types use flow formatting.
template <typename T>
struct SequenceElementTraits<T, std::enable_if_t<std::is_fundamental_v<T>>> {
  /// Emit sequences of this element type in flow style.
  static const bool flow = true;
};

/// Sequences of \c std::string use block formatting.
template <> struct SequenceElementTraits<std::string> {
  /// Emit sequences of strings in block style.
  static const bool flow = false;
};
/// Sequences of \c StringRef use block formatting.
template <> struct SequenceElementTraits<StringRef> {
  /// Emit sequences of string refs in block style.
  static const bool flow = false;
};
/// Sequences of string pairs use block formatting.
template <> struct SequenceElementTraits<std::pair<std::string, std::string>> {
  /// Emit sequences of string pairs in block style.
  static const bool flow = false;
};

/// Implementation of CustomMappingTraits for std::map<std::string, T>.
template <typename T> struct StdMapStringCustomMappingTraitsImpl {
  /// Map type specialized by this trait helper.
  using map_type = std::map<std::string, T>;

  /// Read mapping entry \p key into \p v.
  /// \param io YAML IO object.
  /// \param key Mapping key text.
  /// \param v Destination string map.
  static void inputOne(IO &io, StringRef key, map_type &v) {
    io.mapRequired(key, v[std::string(key)]);
  }

  /// Write all entries of \p v as required mapping keys.
  /// \param io YAML IO object.
  /// \param v Source string map.
  static void output(IO &io, map_type &v) {
    for (auto &p : v)
      io.mapRequired(p.first, p.second);
  }
};

} // end namespace yaml
} // end namespace llvm

#define LLVM_YAML_IS_SEQUENCE_VECTOR_IMPL(TYPE, FLOW)                          \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  static_assert(                                                               \
      !std::is_fundamental_v<TYPE> && !std::is_same_v<TYPE, std::string> &&    \
          !std::is_same_v<TYPE, llvm::StringRef>,                              \
      "only use LLVM_YAML_IS_SEQUENCE_VECTOR for types you control");          \
  template <> struct SequenceElementTraits<TYPE> {                             \
    static const bool flow = FLOW;                                             \
  };                                                                           \
  }                                                                            \
  }

/// Utility for declaring that a std::vector of a particular type
/// should be considered a YAML sequence.
/// \param type Element type of the sequence vector.
#define LLVM_YAML_IS_SEQUENCE_VECTOR(type)                                     \
  LLVM_YAML_IS_SEQUENCE_VECTOR_IMPL(type, false)

/// Utility for declaring that a std::vector of a particular type
/// should be considered a YAML flow sequence.
/// \param type Element type of the flow sequence vector.
#define LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(type)                                \
  LLVM_YAML_IS_SEQUENCE_VECTOR_IMPL(type, true)

#define LLVM_YAML_DECLARE_MAPPING_TRAITS(Type)                                 \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <> struct LLVM_ABI MappingTraits<Type> {                            \
    static void mapping(IO &IO, Type &Obj);                                    \
  };                                                                           \
  }                                                                            \
  }

#define LLVM_YAML_DECLARE_MAPPING_TRAITS_PRIVATE(Type)                         \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <> struct MappingTraits<Type> {                                     \
    static void mapping(IO &IO, Type &Obj);                                    \
  };                                                                           \
  }                                                                            \
  }

#define LLVM_YAML_DECLARE_ENUM_TRAITS(Type)                                    \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <> struct LLVM_ABI ScalarEnumerationTraits<Type> {                  \
    static void enumeration(IO &io, Type &Value);                              \
  };                                                                           \
  }                                                                            \
  }

#define LLVM_YAML_DECLARE_BITSET_TRAITS(Type)                                  \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <> struct LLVM_ABI ScalarBitSetTraits<Type> {                       \
    static void bitset(IO &IO, Type &Options);                                 \
  };                                                                           \
  }                                                                            \
  }

#define LLVM_YAML_DECLARE_SCALAR_TRAITS(Type, MustQuote)                       \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <> struct LLVM_ABI ScalarTraits<Type> {                             \
    static void output(const Type &Value, void *ctx, raw_ostream &Out);        \
    static StringRef input(StringRef Scalar, void *ctxt, Type &Value);         \
    static QuotingType mustQuote(StringRef) { return MustQuote; }              \
  };                                                                           \
  }                                                                            \
  }

/// Utility for declaring that a std::vector of a particular type
/// should be considered a YAML document list.
/// \param _type Element type of the document-list vector.
#define LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(_type)                               \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <unsigned N>                                                        \
  struct DocumentListTraits<SmallVector<_type, N>>                             \
      : public SequenceTraitsImpl<SmallVector<_type, N>, false> {};            \
  template <>                                                                  \
  struct DocumentListTraits<std::vector<_type>>                                \
      : public SequenceTraitsImpl<std::vector<_type>, false> {};               \
  }                                                                            \
  }

/// Utility for declaring that std::map<std::string, _type> should be considered
/// a YAML map.
/// \param _type Mapped value type for the string map.
#define LLVM_YAML_IS_STRING_MAP(_type)                                         \
  namespace llvm {                                                             \
  namespace yaml {                                                             \
  template <>                                                                  \
  struct CustomMappingTraits<std::map<std::string, _type>>                     \
      : public StdMapStringCustomMappingTraitsImpl<_type> {};                  \
  }                                                                            \
  }

namespace llvm {
namespace yaml {
/// Sequences of \c Hex64 use flow formatting.
template <> struct SequenceElementTraits<Hex64> {
  /// Emit sequences of Hex64 in flow style.
  static const bool flow = true;
};
/// Sequences of \c Hex32 use flow formatting.
template <> struct SequenceElementTraits<Hex32> {
  /// Emit sequences of Hex32 in flow style.
  static const bool flow = true;
};
/// Sequences of \c Hex16 use flow formatting.
template <> struct SequenceElementTraits<Hex16> {
  /// Emit sequences of Hex16 in flow style.
  static const bool flow = true;
};
/// Sequences of \c Hex8 use flow formatting.
template <> struct SequenceElementTraits<Hex8> {
  /// Emit sequences of Hex8 in flow style.
  static const bool flow = true;
};
} // end namespace yaml
} // end namespace llvm

#endif // LLVM_SUPPORT_YAMLTRAITS_H
