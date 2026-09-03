//===-- LVSupport.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines support functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSUPPORT_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSUPPORT_H

#include "llvm/ADT/Twine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVStringPool.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <bitset>
#include <cctype>
#include <map>
#include <sstream>
#include <type_traits>

namespace llvm {
namespace logicalview {

/// Return the unique string pool instance used by logical-view readers.
/// \returns Reference to the shared LVStringPool.
LLVM_ABI LVStringPool &getStringPool();

/// Ordered collection of string references.
using LVStringRefs = std::vector<StringRef>;
/// Outer and inner lexical name components as string references.
using LVLexicalComponent = std::tuple<StringRef, StringRef>;
/// Pair of indices into an LVStringRefs sequence.
using LVLexicalIndex =
    std::tuple<LVStringRefs::size_type, LVStringRefs::size_type>;

/// Bit-set of characteristics keyed by an enumeration type.
///
/// Used to record specific characteristics about the objects.
template <typename T> class LVProperties {
  static constexpr unsigned N_PROPS = static_cast<unsigned>(T::LastEntry);
  // Use uint32_t as the underlying type if the `T` enum has at most 32
  // enumerators; otherwise, fallback to the generic `std::bitset` case.
  std::conditional_t<(N_PROPS > 32), std::bitset<N_PROPS>, uint32_t> Bits{};

public:
  /// Construct an empty property set with all bits clear.
  LVProperties() = default;

  /// Set the property bit identified by \p Idx.
  /// \param Idx Enumerator selecting which property bit to set.
  void set(T Idx) {
    if constexpr (std::is_same_v<decltype(Bits), uint32_t>)
      Bits |= 1 << static_cast<unsigned>(Idx);
    else
      Bits.set(static_cast<unsigned>(Idx));
  }
  /// Clear the property bit identified by \p Idx.
  /// \param Idx Enumerator selecting which property bit to clear.
  void reset(T Idx) {
    if constexpr (std::is_same_v<decltype(Bits), uint32_t>)
      Bits &= ~(1 << static_cast<unsigned>(Idx));
    else
      Bits.reset(static_cast<unsigned>(Idx));
  }
  /// Return whether the property bit identified by \p Idx is set.
  /// \param Idx Enumerator selecting which property bit to query.
  /// \returns True when the selected property bit is set.
  bool get(T Idx) const {
    if constexpr (std::is_same_v<decltype(Bits), uint32_t>)
      return Bits & (1 << static_cast<unsigned>(Idx));
    else
      return Bits[static_cast<unsigned>(Idx)];
  }
};

// Generate get, set and reset 'bool' functions for LVProperties instances.
// FAMILY: instance name.
// ENUM: enumeration instance.
// FIELD: enumerator instance.
// F1, F2, F3: optional 'set' functions to be called.
#define BOOL_BIT(FAMILY, ENUM, FIELD)                                          \
  bool get##FIELD() const { return FAMILY.get(ENUM::FIELD); }                  \
  void set##FIELD() { FAMILY.set(ENUM::FIELD); }                               \
  void reset##FIELD() { FAMILY.reset(ENUM::FIELD); }

#define BOOL_BIT_1(FAMILY, ENUM, FIELD, F1)                                    \
  bool get##FIELD() const { return FAMILY.get(ENUM::FIELD); }                  \
  void set##FIELD() {                                                          \
    FAMILY.set(ENUM::FIELD);                                                   \
    set##F1();                                                                 \
  }                                                                            \
  void reset##FIELD() { FAMILY.reset(ENUM::FIELD); }

#define BOOL_BIT_2(FAMILY, ENUM, FIELD, F1, F2)                                \
  bool get##FIELD() const { return FAMILY.get(ENUM::FIELD); }                  \
  void set##FIELD() {                                                          \
    FAMILY.set(ENUM::FIELD);                                                   \
    set##F1();                                                                 \
    set##F2();                                                                 \
  }                                                                            \
  void reset##FIELD() { FAMILY.reset(ENUM::FIELD); }

#define BOOL_BIT_3(FAMILY, ENUM, FIELD, F1, F2, F3)                            \
  bool get##FIELD() const { return FAMILY.get(ENUM::FIELD); }                  \
  void set##FIELD() {                                                          \
    FAMILY.set(ENUM::FIELD);                                                   \
    set##F1();                                                                 \
    set##F2();                                                                 \
    set##F3();                                                                 \
  }                                                                            \
  void reset##FIELD() { FAMILY.reset(ENUM::FIELD); }

// Generate get, set and reset functions for 'properties'.
#define PROPERTY(ENUM, FIELD) BOOL_BIT(Properties, ENUM, FIELD)
#define PROPERTY_1(ENUM, FIELD, F1) BOOL_BIT_1(Properties, ENUM, FIELD, F1)
#define PROPERTY_2(ENUM, FIELD, F1, F2)                                        \
  BOOL_BIT_2(Properties, ENUM, FIELD, F1, F2)
#define PROPERTY_3(ENUM, FIELD, F1, F2, F3)                                    \
  BOOL_BIT_3(Properties, ENUM, FIELD, F1, F2, F3)

// Generate get, set and reset functions for 'kinds'.
#define KIND(ENUM, FIELD) BOOL_BIT(Kinds, ENUM, FIELD)
#define KIND_1(ENUM, FIELD, F1) BOOL_BIT_1(Kinds, ENUM, FIELD, F1)
#define KIND_2(ENUM, FIELD, F1, F2) BOOL_BIT_2(Kinds, ENUM, FIELD, F1, F2)
#define KIND_3(ENUM, FIELD, F1, F2, F3)                                        \
  BOOL_BIT_3(Kinds, ENUM, FIELD, F1, F2, F3)

static constexpr int DEC_WIDTH = 8;
/// Format \p N as a decimal FormattedNumber with the given field width.
/// \param N Value to format as decimal.
/// \param Width Minimum field width for the decimal digits.
/// \returns FormattedNumber for decimal output.
inline FormattedNumber decValue(uint64_t N, unsigned Width = DEC_WIDTH) {
  return format_decimal(N, Width);
}

/// Return the decimal string representation of \p Value.
/// \param Value Integer value to format as decimal.
/// \param Width Minimum field width for the decimal digits.
/// \returns Decimal representation of \p Value as a string.
inline std::string decString(uint64_t Value, size_t Width = DEC_WIDTH) {
  std::string String;
  raw_string_ostream Stream(String);
  Stream << decValue(Value, Width);
  return String;
}

static constexpr int HEX_WIDTH = 12;
/// Format \p N as a hexadecimal FormattedNumber with the given field width.
/// \param N Value to format as hexadecimal.
/// \param Width Minimum field width for the hex digits.
/// \param Upper Whether to use uppercase hex digits.
/// \returns FormattedNumber for hexadecimal output.
inline FormattedNumber hexValue(uint64_t N, unsigned Width = HEX_WIDTH,
                                bool Upper = false) {
  return format_hex(N, Width, Upper);
}

/// Return the hexadecimal string representation of \p Value.
///
/// Uses a '[0x%08x]'-style hex format without surrounding brackets.
/// \param Value Integer value to format as hexadecimal.
/// \param Width Minimum field width for the hex digits.
/// \returns Hexadecimal representation of \p Value as a string.
inline std::string hexString(uint64_t Value, size_t Width = HEX_WIDTH) {
  std::string String;
  raw_string_ostream Stream(String);
  Stream << hexValue(Value, Width, false);
  return String;
}

/// Return a hexadecimal string for \p Value enclosed in square brackets.
/// \param Value Integer value to format as hexadecimal.
/// \returns Hexadecimal representation wrapped in '[' and ']'.
inline std::string hexSquareString(uint64_t Value) {
  return (Twine("[") + Twine(hexString(Value)) + Twine("]")).str();
}

/// Return a string with \p First and \p Others separated by spaces.
/// \param First Leading attribute string.
/// \param Others Additional attribute strings to append.
/// \returns Space-separated attribute string, with a trailing space when
/// non-empty.
template <typename... Args>
std::string formatAttributes(const StringRef First, Args... Others) {
  const auto List = {First, Others...};
  std::stringstream Stream;
  size_t Size = 0;
  for (const StringRef &Item : List) {
    Stream << (Size ? " " : "") << Item.str();
    Size = Item.size();
  }
  Stream << (Size ? " " : "");
  return Stream.str();
}

/// Append \p Value to the small vector stored under \p Key in \p Map.
/// \param Map Map whose values are small vectors of \p ValueType.
/// \param Key Key identifying the vector to update.
/// \param Value Item to append under \p Key.
template <typename MapType, typename KeyType, typename ValueType>
void addItem(MapType *Map, KeyType Key, ValueType Value) {
  (*Map)[Key].push_back(Value);
}

/// Nested map from a pair of keys to a pointer value.
///
/// Double map data structure.
template <typename FirstKeyType, typename SecondKeyType, typename ValueType>
class LVDoubleMap {
  static_assert(std::is_pointer<ValueType>::value,
                "ValueType must be a pointer.");
  using LVSecondMapType = std::map<SecondKeyType, ValueType>;
  using LVFirstMapType =
      std::map<FirstKeyType, std::unique_ptr<LVSecondMapType>>;
  using LVAuxMapType = std::map<SecondKeyType, FirstKeyType>;
  using LVValueTypes = std::vector<ValueType>;
  LVFirstMapType FirstMap;
  LVAuxMapType AuxMap;

public:
  /// Insert \p Value under the key pair (\p FirstKey, \p SecondKey).
  /// \param FirstKey Outer map key.
  /// \param SecondKey Inner map key.
  /// \param Value Pointer value to store; existing entries are left unchanged.
  void add(FirstKeyType FirstKey, SecondKeyType SecondKey, ValueType Value) {
    typename LVFirstMapType::iterator FirstIter = FirstMap.find(FirstKey);
    if (FirstIter == FirstMap.end()) {
      auto SecondMapSP = std::make_unique<LVSecondMapType>();
      SecondMapSP->emplace(SecondKey, Value);
      FirstMap.emplace(FirstKey, std::move(SecondMapSP));
    } else {
      LVSecondMapType *SecondMap = FirstIter->second.get();
      if (SecondMap->find(SecondKey) == SecondMap->end())
        SecondMap->emplace(SecondKey, Value);
    }

    typename LVAuxMapType::iterator AuxIter = AuxMap.find(SecondKey);
    if (AuxIter == AuxMap.end()) {
      AuxMap.emplace(SecondKey, FirstKey);
    }
  }

  /// Return the inner map associated with \p FirstKey, if present.
  /// \param FirstKey Outer map key to look up.
  /// \returns Pointer to the inner map, or nullptr when \p FirstKey is absent.
  LVSecondMapType *findMap(FirstKeyType FirstKey) const {
    typename LVFirstMapType::const_iterator FirstIter = FirstMap.find(FirstKey);
    if (FirstIter == FirstMap.end())
      return nullptr;

    return FirstIter->second.get();
  }

  /// Look up the value stored under (\p FirstKey, \p SecondKey).
  /// \param FirstKey Outer map key.
  /// \param SecondKey Inner map key.
  /// \returns Stored pointer, or nullptr when the key pair is absent.
  ValueType find(FirstKeyType FirstKey, SecondKeyType SecondKey) const {
    LVSecondMapType *SecondMap = findMap(FirstKey);
    if (!SecondMap)
      return nullptr;

    typename LVSecondMapType::const_iterator SecondIter =
        SecondMap->find(SecondKey);
    return (SecondIter != SecondMap->end()) ? SecondIter->second : nullptr;
  }

  /// Look up the value associated with \p SecondKey via the auxiliary map.
  /// \param SecondKey Inner key used to recover the outer key.
  /// \returns Stored pointer, or nullptr when \p SecondKey is absent.
  ValueType find(SecondKeyType SecondKey) const {
    typename LVAuxMapType::const_iterator AuxIter = AuxMap.find(SecondKey);
    if (AuxIter == AuxMap.end())
      return nullptr;
    return find(AuxIter->second, SecondKey);
  }

  /// Return a vector with all stored \c ValueType values.
  /// \returns All pointer values currently held in the nested maps.
  LVValueTypes find() const {
    LVValueTypes Values;
    if (FirstMap.empty())
      return Values;
    for (typename LVFirstMapType::const_reference FirstEntry : FirstMap) {
      LVSecondMapType &SecondMap = *FirstEntry.second;
      for (typename LVSecondMapType::const_reference SecondEntry : SecondMap)
        Values.push_back(SecondEntry.second);
    }
    return Values;
  }
};

/// Normalize \p Path to a unified, lowercase pathname.
///
/// Converts characters to lowercase, replaces '\\' with '/', and collapses
/// duplicate '/' separators.
/// \param Path Pathname to transform.
/// \returns Normalized pathname string.
LLVM_ABI std::string transformPath(StringRef Path);
/// Convert \p Path into a flattened, filesystem-safe lowercase name.
///
/// Replaces '/', '\\', '<', '>', '.', ':', '%', '*', '?', '|', '"', and
/// spaces with '_'.
/// \param Path Pathname to flatten.
/// \returns Flattened pathname string.
LLVM_ABI std::string flattenedFilePath(StringRef Path);

/// Return \p Kind wrapped in curly braces for display.
/// \param Kind Kind string to format.
/// \returns Display string of the form `{Kind}`.
inline std::string formattedKind(StringRef Kind) {
  return (Twine("{") + Twine(Kind) + Twine("}")).str();
}

/// Return \p Name wrapped in single quotes for display.
/// \param Name Name string to format.
/// \returns Display string of the form `'Name'`.
inline std::string formattedName(StringRef Name) {
  return (Twine("'") + Twine(Name) + Twine("'")).str();
}

/// Return the concatenation of \p Name1 and \p Name2 wrapped in single quotes.
/// \param Name1 Leading name fragment.
/// \param Name2 Trailing name fragment.
/// \returns Display string of the form `'Name1Name2'`.
inline std::string formattedNames(StringRef Name1, StringRef Name2) {
  return (Twine("'") + Twine(Name1) + Twine(Name2) + Twine("'")).str();
}

/// Return the outermost and innermost lexical components of \p Name.
///
/// The given string represents a symbol or type name with optional enclosing
/// scopes, such as: name, name<..>, scope::name, scope::..::name, etc.
/// The string can have multiple references to template instantiations.
/// \param Name Scoped symbol or type name to split.
/// \returns Tuple of (outer scopes, innermost component); outer is empty
/// when \p Name has a single component.
LLVM_ABI LVLexicalComponent getInnerComponent(StringRef Name);
/// Split \p Name into lexical components separated by `::`.
/// \param Name Scoped symbol or type name to split.
/// \returns Ordered list of lexical name components.
LLVM_ABI LVStringRefs getAllLexicalComponents(StringRef Name);
/// Join \p Components into a scoped name, optionally prefixed by \p BaseName.
/// \param Components Lexical name components to join with `::`.
/// \param BaseName Optional leading scope prepended before the components.
/// \returns Scoped name string built from \p BaseName and \p Components.
LLVM_ABI std::string getScopedName(const LVStringRefs &Components,
                                   StringRef BaseName = {});

/// Restore the full CodeView symbol opcode from a truncated \p Code byte.
///
/// These are the values assigned to the debug location record IDs.
/// See DebugInfo/CodeView/CodeViewSymbols.def.
/// S_DEFRANGE                               0x113f
/// S_DEFRANGE_SUBFIELD                      0x1140
/// S_DEFRANGE_REGISTER                      0x1141
/// S_DEFRANGE_FRAMEPOINTER_REL              0x1142
/// S_DEFRANGE_SUBFIELD_REGISTER             0x1143
/// S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE   0x1144
/// S_DEFRANGE_REGISTER_REL                  0x1145
/// S_DEFRANGE_REGISTER_REL_INDIR            0x1177
/// When recording CodeView debug location, the above values are truncated
/// to a uint8_t value in order to fit the 'OpCode' used for the logical
/// debug location operations.
/// \param Code Truncated low byte of a CodeView debug-location record ID.
/// \returns Original CodeView enum value with the 0x1100 base restored.
inline uint16_t getCodeViewOperationCode(uint8_t Code) { return 0x1100 | Code; }

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSUPPORT_H
