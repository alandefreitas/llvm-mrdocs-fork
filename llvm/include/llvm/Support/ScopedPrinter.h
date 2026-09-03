//===-- ScopedPrinter.h ----------------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SCOPEDPRINTER_H
#define LLVM_SUPPORT_SCOPEDPRINTER_H

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Enum.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/raw_ostream.h"
#include <type_traits>

namespace llvm {

/// A numeric value formatted as hexadecimal when streamed.
///
/// To avoid sign-extension we have to explicitly cast to the appropriate
/// unsigned type. The overloads are here so that every type that is implicitly
/// convertible to an integer (including endian helpers) can be used without
/// requiring type traits or call-site changes.
struct HexNumber {
  /// Construct from a char, casting through unsigned char.
  ///
  /// \param Value Integer value to store.
  HexNumber(char Value) : Value(static_cast<unsigned char>(Value)) {}
  /// Construct from a signed char, casting through unsigned char.
  ///
  /// \param Value Integer value to store.
  HexNumber(signed char Value) : Value(static_cast<unsigned char>(Value)) {}
  /// Construct from a signed short, casting through unsigned short.
  ///
  /// \param Value Integer value to store.
  HexNumber(signed short Value) : Value(static_cast<unsigned short>(Value)) {}
  /// Construct from a signed int, casting through unsigned int.
  ///
  /// \param Value Integer value to store.
  HexNumber(signed int Value) : Value(static_cast<unsigned int>(Value)) {}
  /// Construct from a signed long, casting through unsigned long.
  ///
  /// \param Value Integer value to store.
  HexNumber(signed long Value) : Value(static_cast<unsigned long>(Value)) {}
  /// Construct from a signed long long, casting through unsigned long long.
  ///
  /// \param Value Integer value to store.
  HexNumber(signed long long Value)
      : Value(static_cast<unsigned long long>(Value)) {}
  /// Construct from an unsigned char.
  ///
  /// \param Value Integer value to store.
  HexNumber(unsigned char Value) : Value(Value) {}
  /// Construct from an unsigned short.
  ///
  /// \param Value Integer value to store.
  HexNumber(unsigned short Value) : Value(Value) {}
  /// Construct from an unsigned int.
  ///
  /// \param Value Integer value to store.
  HexNumber(unsigned int Value) : Value(Value) {}
  /// Construct from an unsigned long.
  ///
  /// \param Value Integer value to store.
  HexNumber(unsigned long Value) : Value(Value) {}
  /// Construct from an unsigned long long.
  ///
  /// \param Value Integer value to store.
  HexNumber(unsigned long long Value) : Value(Value) {}
  /// Construct from an enumeration by converting to its underlying type.
  ///
  /// \tparam EnumT Enumeration type convertible via \c to_underlying.
  /// \param Value Enumeration value to store.
  template <typename EnumT, typename = std::enable_if_t<std::is_enum_v<EnumT>>>
  HexNumber(EnumT Value) : HexNumber(llvm::to_underlying(Value)) {}

  /// The stored unsigned integer value.
  uint64_t Value;
};

/// A named flag bit or value used when printing flag sets.
struct FlagEntry {
  /// Construct from a name and char value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, char Value)
      : Name(Name), Value(static_cast<unsigned char>(Value)) {}
  /// Construct from a name and signed char value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, signed char Value)
      : Name(Name), Value(static_cast<unsigned char>(Value)) {}
  /// Construct from a name and signed short value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, signed short Value)
      : Name(Name), Value(static_cast<unsigned short>(Value)) {}
  /// Construct from a name and signed int value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, signed int Value)
      : Name(Name), Value(static_cast<unsigned int>(Value)) {}
  /// Construct from a name and signed long value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, signed long Value)
      : Name(Name), Value(static_cast<unsigned long>(Value)) {}
  /// Construct from a name and signed long long value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, signed long long Value)
      : Name(Name), Value(static_cast<unsigned long long>(Value)) {}
  /// Construct from a name and unsigned char value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, unsigned char Value) : Name(Name), Value(Value) {}
  /// Construct from a name and unsigned short value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, unsigned short Value) : Name(Name), Value(Value) {}
  /// Construct from a name and unsigned int value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, unsigned int Value) : Name(Name), Value(Value) {}
  /// Construct from a name and unsigned long value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, unsigned long Value) : Name(Name), Value(Value) {}
  /// Construct from a name and unsigned long long value.
  ///
  /// \param Name Display name of the flag.
  /// \param Value Integer value of the flag.
  FlagEntry(StringRef Name, unsigned long long Value)
      : Name(Name), Value(Value) {}
  /// Construct from a name and enumeration value.
  ///
  /// \tparam EnumT Enumeration type convertible via \c to_underlying.
  /// \param Name Display name of the flag.
  /// \param Value Enumeration value of the flag.
  template <typename EnumT, typename = std::enable_if_t<std::is_enum_v<EnumT>>>
  FlagEntry(StringRef Name, EnumT Value)
      : FlagEntry(Name, llvm::to_underlying(Value)) {}

  /// Display name of the flag.
  StringRef Name;
  /// Integer value of the flag.
  uint64_t Value;
};

/// Write \p Value to \p OS as a hexadecimal number.
///
/// \param OS Output stream.
/// \param Value Hex number to print.
/// \return The output stream.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const HexNumber &Value);

/// Convert \p Value to a string via \c operator<<.
///
/// \tparam T Type streamable to \c raw_ostream.
/// \param Value Value to stringify.
/// \return The printed representation of \p Value.
template <class T> std::string to_string(const T &Value) {
  std::string number;
  raw_string_ostream stream(number);
  stream << Value;
  return number;
}

/// Pretty-printer that emits indented, labeled text with nested scopes.
class LLVM_ABI ScopedPrinter {
public:
  /// Discriminator for \c ScopedPrinter subclasses used with LLVM RTTI.
  enum class ScopedPrinterKind {
    /// Plain text scoped printer.
    Base,
    /// JSON scoped printer.
    JSON,
  };

  /// Construct a scoped printer writing to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Kind RTTI kind tag for this printer.
  ScopedPrinter(raw_ostream &OS,
                ScopedPrinterKind Kind = ScopedPrinterKind::Base)
      : OS(OS), Kind(Kind) {}

  /// Return the RTTI kind of this printer.
  ///
  /// \return The \c ScopedPrinterKind tag for this printer.
  ScopedPrinterKind getKind() const { return Kind; }

  /// Return true if \p SP is a base \c ScopedPrinter.
  ///
  /// \param SP Printer to test.
  /// \return True when \p SP has kind \c Base.
  static bool classof(const ScopedPrinter *SP) {
    return SP->getKind() == ScopedPrinterKind::Base;
  }

  /// Destroy the scoped printer.
  virtual ~ScopedPrinter() = default;

  /// Flush the underlying output stream.
  void flush() { OS.flush(); }

  /// Increase the indentation level by \p Levels.
  ///
  /// \param Levels Number of indent steps to add.
  void indent(int Levels = 1) { IndentLevel += Levels; }

  /// Decrease the indentation level by \p Levels.
  ///
  /// \param Levels Number of indent steps to remove.
  void unindent(int Levels = 1) {
    IndentLevel = IndentLevel > Levels ? IndentLevel - Levels : 0;
  }

  /// Reset the indentation level to zero.
  void resetIndent() { IndentLevel = 0; }

  /// Return the current indentation level.
  ///
  /// \return The current number of indent steps.
  int getIndentLevel() { return IndentLevel; }

  /// Set the prefix printed before each indented line.
  ///
  /// \param P Prefix string applied by \c printIndent.
  void setPrefix(StringRef P) { Prefix = P; }

  /// Print the current prefix and indentation whitespace.
  void printIndent() {
    OS << Prefix;
    for (int i = 0; i < IndentLevel; ++i)
      OS << "  ";
  }

  /// Wrap \p Value as a \c HexNumber for hexadecimal printing.
  ///
  /// \tparam T Type convertible to \c HexNumber.
  /// \param Value Value to wrap.
  /// \return A \c HexNumber holding \p Value.
  template <typename T> HexNumber hex(T Value) { return HexNumber(Value); }

  /// Print an enumeration value by name when known, otherwise as hex.
  ///
  /// \tparam T Numeric type of the enumeration value.
  /// \tparam TEnum Enumeration type described by \p EnumValues.
  /// \tparam NumStrs Number of string entries in \p EnumValues.
  /// \param Label Field label to print.
  /// \param Value Enumeration value to print.
  /// \param EnumValues Mapping from values to display names.
  template <typename T, typename TEnum, unsigned NumStrs>
  void printEnum(StringRef Label, T Value,
                 EnumStrings<TEnum, NumStrs> EnumValues) {
    if (StringRef Name = EnumValues.toString(Value); !Name.empty())
      printHex(Label, Name, Value);
    else
      printHex(Label, Value);
  }

  /// Print a bitfield value and the named flags that are set.
  ///
  /// \tparam T Numeric type of the combined flag value.
  /// \tparam TFlag Flag enumeration type.
  /// \tparam NumStrs Number of string entries in \p Flags.
  /// \param Label Field label to print.
  /// \param Value Combined flag bits.
  /// \param Flags Known flag names and values.
  /// \param EnumMask1 Optional exclusive-enum mask group.
  /// \param EnumMask2 Optional exclusive-enum mask group.
  /// \param EnumMask3 Optional exclusive-enum mask group.
  /// \param ExtraFlags Additional flags always included in the set.
  template <typename T, typename TFlag, unsigned NumStrs>
  void printFlags(StringRef Label, T Value, EnumStrings<TFlag, NumStrs> Flags,
                  TFlag EnumMask1 = {}, TFlag EnumMask2 = {},
                  TFlag EnumMask3 = {}, ArrayRef<FlagEntry> ExtraFlags = {}) {
    SmallVector<FlagEntry, 10> SetFlags(ExtraFlags);

    for (const auto &Flag : Flags) {
      if (Flag.value() == TFlag{})
        continue;

      TFlag EnumMask{};
      if ((Flag.value() & EnumMask1) != TFlag{})
        EnumMask = EnumMask1;
      else if ((Flag.value() & EnumMask2) != TFlag{})
        EnumMask = EnumMask2;
      else if ((Flag.value() & EnumMask3) != TFlag{})
        EnumMask = EnumMask3;
      bool IsEnum = (Flag.value() & EnumMask) != TFlag{};
      if ((!IsEnum && (Value & Flag.value()) == Flag.value()) ||
          (IsEnum && (Value & EnumMask) == Flag.value())) {
        SetFlags.emplace_back(Flag.name(), Flag.value());
      }
    }

    llvm::sort(SetFlags, &flagName);
    printFlagsImpl(Label, hex(Value), SetFlags);
  }

  /// Print a bitfield value using a caller-provided list of set flags.
  ///
  /// \tparam T Numeric type of the combined flag value.
  /// \param Label Field label to print.
  /// \param Value Combined flag bits.
  /// \param SetFlags Mutable list of set flags; sorted before printing.
  template <typename T>
  void printFlags(StringRef Label, T Value,
                  SmallVectorImpl<FlagEntry> &SetFlags) {
    llvm::sort(SetFlags, &flagName);
    printFlagsImpl(Label, hex(Value), SetFlags);
  }

  /// Print a bitfield value by enumerating each set bit.
  ///
  /// \tparam T Numeric type of the combined flag value.
  /// \param Label Field label to print.
  /// \param Value Combined flag bits.
  template <typename T> void printFlags(StringRef Label, T Value) {
    SmallVector<HexNumber, 10> SetFlags;
    uint64_t Flag = 1;
    uint64_t Curr = Value;
    while (Curr > 0) {
      if (Curr & 1)
        SetFlags.emplace_back(Flag);
      Curr >>= 1;
      Flag <<= 1;
    }
    printFlagsImpl(Label, hex(Value), SetFlags);
  }

  /// Print a labeled char number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, char Value) {
    startLine() << Label << ": " << static_cast<int>(Value) << "\n";
  }

  /// Print a labeled signed char number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, signed char Value) {
    startLine() << Label << ": " << static_cast<int>(Value) << "\n";
  }

  /// Print a labeled unsigned char number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, unsigned char Value) {
    startLine() << Label << ": " << static_cast<unsigned>(Value) << "\n";
  }

  /// Print a labeled short number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, short Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  /// Print a labeled unsigned short number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, unsigned short Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  /// Print a labeled int number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, int Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  /// Print a labeled unsigned int number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, unsigned int Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  /// Print a labeled long number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, long Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  /// Print a labeled unsigned long number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, unsigned long Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  /// Print a labeled long long number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, long long Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  /// Print a labeled unsigned long long number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, unsigned long long Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  /// Print a labeled arbitrary-precision integer.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, const APSInt &Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  /// Print a labeled floating-point number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, float Value) {
    startLine() << Label << ": " << format("%5.1f", Value) << "\n";
  }

  /// Print a labeled double-precision number.
  ///
  /// \param Label Field label to print.
  /// \param Value Number to print.
  virtual void printNumber(StringRef Label, double Value) {
    startLine() << Label << ": " << format("%5.1f", Value) << "\n";
  }

  /// Print a labeled number with both a symbolic name and numeric value.
  ///
  /// \tparam T Type convertible to a printable string.
  /// \param Label Field label to print.
  /// \param Str Symbolic name associated with the value.
  /// \param Value Numeric value to print.
  template <typename T>
  void printNumber(StringRef Label, StringRef Str, T Value) {
    printNumberImpl(Label, Str, to_string(Value));
  }

  /// Print a labeled boolean as Yes or No.
  ///
  /// \param Label Field label to print.
  /// \param Value Boolean value to print.
  virtual void printBoolean(StringRef Label, bool Value) {
    startLine() << Label << ": " << (Value ? "Yes" : "No") << '\n';
  }

  /// Print a labeled dotted version number.
  ///
  /// \tparam T Type of the major version component.
  /// \tparam TArgs Types of the remaining version components.
  /// \param Label Field label to print.
  /// \param MajorVersion Leading version component.
  /// \param MinorVersions Trailing version components.
  template <typename T, typename... TArgs>
  void printVersion(StringRef Label, T MajorVersion, TArgs... MinorVersions) {
    startLine() << Label << ": ";
    getOStream() << MajorVersion;
    ((getOStream() << '.' << MinorVersions), ...);
    getOStream() << "\n";
  }

  /// Print a labeled list by converting each element to a string.
  ///
  /// \tparam T Element type convertible via \c to_string.
  /// \param Label Field label to print.
  /// \param List Elements to print.
  template <typename T>
  void printList(StringRef Label, const ArrayRef<T> List) {
    SmallVector<std::string, 10> StringList;
    for (const auto &Item : List)
      StringList.emplace_back(to_string(Item));
    printList(Label, StringList);
  }

  /// Print a labeled list of boolean values.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<bool> List) {
    printListImpl(Label, List);
  }

  /// Print a labeled list of strings.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<std::string> List) {
    printListImpl(Label, List);
  }

  /// Print a labeled list of 64-bit unsigned integers.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<uint64_t> List) {
    printListImpl(Label, List);
  }

  /// Print a labeled list of 32-bit unsigned integers.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<uint32_t> List) {
    printListImpl(Label, List);
  }

  /// Print a labeled list of 16-bit unsigned integers.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<uint16_t> List) {
    printListImpl(Label, List);
  }

  /// Print a labeled list of 8-bit unsigned integers.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<uint8_t> List) {
    SmallVector<unsigned> NumberList;
    for (const uint8_t &Item : List)
      NumberList.emplace_back(Item);
    printListImpl(Label, NumberList);
  }

  /// Print a labeled list of 64-bit signed integers.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<int64_t> List) {
    printListImpl(Label, List);
  }

  /// Print a labeled list of 32-bit signed integers.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<int32_t> List) {
    printListImpl(Label, List);
  }

  /// Print a labeled list of 16-bit signed integers.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<int16_t> List) {
    printListImpl(Label, List);
  }

  /// Print a labeled list of 8-bit signed integers.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<int8_t> List) {
    SmallVector<int> NumberList;
    for (const int8_t &Item : List)
      NumberList.emplace_back(Item);
    printListImpl(Label, NumberList);
  }

  /// Print a labeled list of arbitrary-precision integers.
  ///
  /// \param Label Field label to print.
  /// \param List Elements to print.
  virtual void printList(StringRef Label, const ArrayRef<APSInt> List) {
    printListImpl(Label, List);
  }

  /// Print a labeled list using a custom per-element printer.
  ///
  /// \tparam T Range type providing the list elements.
  /// \tparam U Callable that writes one element to a stream.
  /// \param Label Field label to print.
  /// \param List Elements to print.
  /// \param Printer Callback invoked for each element.
  template <typename T, typename U>
  void printList(StringRef Label, const T &List, const U &Printer) {
    startLine() << Label << ": [";
    ListSeparator LS;
    for (const auto &Item : List) {
      OS << LS;
      Printer(OS, Item);
    }
    OS << "]\n";
  }

  /// Print a labeled list of values in hexadecimal.
  ///
  /// \tparam T Range type providing the list elements.
  /// \param Label Field label to print.
  /// \param List Elements to print as hex.
  template <typename T> void printHexList(StringRef Label, const T &List) {
    SmallVector<HexNumber> HexList;
    for (const auto &Item : List)
      HexList.emplace_back(Item);
    printHexListImpl(Label, HexList);
  }

  /// Print a labeled hexadecimal value.
  ///
  /// \tparam T Type convertible to \c HexNumber.
  /// \param Label Field label to print.
  /// \param Value Value to print as hex.
  template <typename T> void printHex(StringRef Label, T Value) {
    printHexImpl(Label, hex(Value));
  }

  /// Print a labeled hexadecimal value with a symbolic name.
  ///
  /// \tparam T Type convertible to \c HexNumber.
  /// \param Label Field label to print.
  /// \param Str Symbolic name associated with the value.
  /// \param Value Value to print as hex.
  template <typename T> void printHex(StringRef Label, StringRef Str, T Value) {
    printHexImpl(Label, Str, hex(Value));
  }

  /// Print a labeled symbol name plus hexadecimal offset.
  ///
  /// \tparam T Type convertible to \c HexNumber.
  /// \param Label Field label to print.
  /// \param Symbol Symbol name.
  /// \param Value Offset from the symbol.
  template <typename T>
  void printSymbolOffset(StringRef Label, StringRef Symbol, T Value) {
    printSymbolOffsetImpl(Label, Symbol, hex(Value));
  }

  /// Print an unlabeled string on its own line.
  ///
  /// \param Value String to print.
  virtual void printString(StringRef Value) { startLine() << Value << "\n"; }

  /// Print a labeled string.
  ///
  /// \param Label Field label to print.
  /// \param Value String to print.
  virtual void printString(StringRef Label, StringRef Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  /// Print a labeled string with C-style escapes.
  ///
  /// \param Label Field label to print.
  /// \param Value String to print escaped.
  void printStringEscaped(StringRef Label, StringRef Value) {
    printStringEscapedImpl(Label, Value);
  }

  /// Print a labeled binary blob with a symbolic string value.
  ///
  /// \param Label Field label to print.
  /// \param Str Symbolic string associated with the bytes.
  /// \param Value Bytes to print.
  void printBinary(StringRef Label, StringRef Str, ArrayRef<uint8_t> Value) {
    printBinaryImpl(Label, Str, Value, false);
  }

  /// Print a labeled binary blob of chars with a symbolic string value.
  ///
  /// \param Label Field label to print.
  /// \param Str Symbolic string associated with the bytes.
  /// \param Value Bytes to print.
  void printBinary(StringRef Label, StringRef Str, ArrayRef<char> Value) {
    auto V =
        ArrayRef(reinterpret_cast<const uint8_t *>(Value.data()), Value.size());
    printBinaryImpl(Label, Str, V, false);
  }

  /// Print a labeled binary blob of unsigned bytes.
  ///
  /// \param Label Field label to print.
  /// \param Value Bytes to print.
  void printBinary(StringRef Label, ArrayRef<uint8_t> Value) {
    printBinaryImpl(Label, StringRef(), Value, false);
  }

  /// Print a labeled binary blob of chars.
  ///
  /// \param Label Field label to print.
  /// \param Value Bytes to print.
  void printBinary(StringRef Label, ArrayRef<char> Value) {
    auto V =
        ArrayRef(reinterpret_cast<const uint8_t *>(Value.data()), Value.size());
    printBinaryImpl(Label, StringRef(), V, false);
  }

  /// Print a labeled binary blob from a string's bytes.
  ///
  /// \param Label Field label to print.
  /// \param Value String whose bytes are printed.
  void printBinary(StringRef Label, StringRef Value) {
    auto V =
        ArrayRef(reinterpret_cast<const uint8_t *>(Value.data()), Value.size());
    printBinaryImpl(Label, StringRef(), V, false);
  }

  /// Print a labeled binary block starting at a byte offset.
  ///
  /// \param Label Field label to print.
  /// \param Value Bytes to print.
  /// \param StartOffset Starting offset shown with the block.
  void printBinaryBlock(StringRef Label, ArrayRef<uint8_t> Value,
                        uint32_t StartOffset) {
    printBinaryImpl(Label, StringRef(), Value, true, StartOffset);
  }

  /// Print a labeled binary block of unsigned bytes.
  ///
  /// \param Label Field label to print.
  /// \param Value Bytes to print.
  void printBinaryBlock(StringRef Label, ArrayRef<uint8_t> Value) {
    printBinaryImpl(Label, StringRef(), Value, true);
  }

  /// Print a labeled binary block from a string's bytes.
  ///
  /// \param Label Field label to print.
  /// \param Value String whose bytes are printed as a block.
  void printBinaryBlock(StringRef Label, StringRef Value) {
    auto V =
        ArrayRef(reinterpret_cast<const uint8_t *>(Value.data()), Value.size());
    printBinaryImpl(Label, StringRef(), V, true);
  }

  /// Print a labeled object by converting it to a string.
  ///
  /// \tparam T Type convertible via \c to_string.
  /// \param Label Field label to print.
  /// \param Value Object to stringify and print.
  template <typename T> void printObject(StringRef Label, const T &Value) {
    printString(Label, to_string(Value));
  }

  /// Begin an unlabeled object scope.
  virtual void objectBegin() { scopedBegin('{'); }

  /// Begin a labeled object scope.
  ///
  /// \param Label Field label for the object.
  virtual void objectBegin(StringRef Label) { scopedBegin(Label, '{'); }

  /// End the current object scope.
  virtual void objectEnd() { scopedEnd('}'); }

  /// Begin an unlabeled array scope.
  virtual void arrayBegin() { scopedBegin('['); }

  /// Begin a labeled array scope.
  ///
  /// \param Label Field label for the array.
  virtual void arrayBegin(StringRef Label) { scopedBegin(Label, '['); }

  /// End the current array scope.
  virtual void arrayEnd() { scopedEnd(']'); }

  /// Start a new indented line and return the output stream.
  ///
  /// \return The underlying output stream after printing indent.
  virtual raw_ostream &startLine() {
    printIndent();
    return OS;
  }

  /// Return the underlying output stream.
  ///
  /// \return The raw output stream used by this printer.
  virtual raw_ostream &getOStream() { return OS; }

private:
  static bool flagName(const FlagEntry &LHS, const FlagEntry &RHS) {
    return LHS.Name < RHS.Name;
  }

  virtual void printBinaryImpl(StringRef Label, StringRef Str,
                               ArrayRef<uint8_t> Value, bool Block,
                               uint32_t StartOffset = 0);

  virtual void printFlagsImpl(StringRef Label, HexNumber Value,
                              ArrayRef<FlagEntry> Flags) {
    startLine() << Label << " [ (" << Value << ")\n";
    for (const auto &Flag : Flags)
      startLine() << "  " << Flag.Name << " (" << hex(Flag.Value) << ")\n";
    startLine() << "]\n";
  }

  virtual void printFlagsImpl(StringRef Label, HexNumber Value,
                              ArrayRef<HexNumber> Flags) {
    startLine() << Label << " [ (" << Value << ")\n";
    for (const auto &Flag : Flags)
      startLine() << "  " << Flag << '\n';
    startLine() << "]\n";
  }

  template <typename T> void printListImpl(StringRef Label, const T List) {
    startLine() << Label << ": [";
    ListSeparator LS;
    for (const auto &Item : List)
      OS << LS << Item;
    OS << "]\n";
  }

  virtual void printHexListImpl(StringRef Label,
                                const ArrayRef<HexNumber> List) {
    startLine() << Label << ": [";
    ListSeparator LS;
    for (const auto &Item : List)
      OS << LS << hex(Item);
    OS << "]\n";
  }

  virtual void printHexImpl(StringRef Label, HexNumber Value) {
    startLine() << Label << ": " << Value << "\n";
  }

  virtual void printHexImpl(StringRef Label, StringRef Str, HexNumber Value) {
    startLine() << Label << ": " << Str << " (" << Value << ")\n";
  }

  virtual void printSymbolOffsetImpl(StringRef Label, StringRef Symbol,
                                     HexNumber Value) {
    startLine() << Label << ": " << Symbol << '+' << Value << '\n';
  }

  virtual void printNumberImpl(StringRef Label, StringRef Str,
                               StringRef Value) {
    startLine() << Label << ": " << Str << " (" << Value << ")\n";
  }

  virtual void printStringEscapedImpl(StringRef Label, StringRef Value) {
    startLine() << Label << ": ";
    OS.write_escaped(Value);
    OS << '\n';
  }

  void scopedBegin(char Symbol) {
    startLine() << Symbol << '\n';
    indent();
  }

  void scopedBegin(StringRef Label, char Symbol) {
    startLine() << Label;
    if (!Label.empty())
      OS << ' ';
    OS << Symbol << '\n';
    indent();
  }

  void scopedEnd(char Symbol) {
    unindent();
    startLine() << Symbol << '\n';
  }

  raw_ostream &OS;
  int IndentLevel = 0;
  StringRef Prefix;
  ScopedPrinterKind Kind;
};

/// Print a labeled little-endian 16-bit value as hexadecimal.
///
/// \param Label Field label to print.
/// \param Value Value to print as hex.
template <>
inline void
ScopedPrinter::printHex<support::ulittle16_t>(StringRef Label,
                                              support::ulittle16_t Value) {
  startLine() << Label << ": " << hex(Value) << "\n";
}

/// RAII base for opening and closing a delimited printer scope.
struct DelimitedScope {
  /// Construct a scope bound to printer \p W.
  ///
  /// \param W Printer that owns the nested scope.
  DelimitedScope(ScopedPrinter &W) : W(&W) {}
  /// Construct an unbound scope with no printer.
  DelimitedScope() : W(nullptr) {}
  /// Destroy the delimited scope.
  virtual ~DelimitedScope() = default;
  /// Bind this scope to \p W and begin the delimited region.
  ///
  /// \param W Printer that should own the nested scope.
  virtual void setPrinter(ScopedPrinter &W) = 0;
  /// Printer that owns this scope, or null when unbound.
  ScopedPrinter *W;
};

/// Scoped printer that emits JSON objects and arrays.
class JSONScopedPrinter : public ScopedPrinter {
private:
  enum class Scope {
    Array,
    Object,
  };

  enum class ScopeKind {
    NoAttribute,
    Attribute,
    NestedAttribute,
  };

  struct ScopeContext {
    Scope Context;
    ScopeKind Kind;
    ScopeContext(Scope Context, ScopeKind Kind = ScopeKind::NoAttribute)
        : Context(Context), Kind(Kind) {}
  };

  SmallVector<ScopeContext, 8> ScopeHistory;
  json::OStream JOS;
  std::unique_ptr<DelimitedScope> OuterScope;

public:
  /// Construct a JSON scoped printer writing to \p OS.
  ///
  /// \param OS Output stream to write JSON to.
  /// \param PrettyPrint Whether to pretty-print the JSON.
  /// \param OuterScope Optional outer delimited scope to own.
  LLVM_ABI JSONScopedPrinter(raw_ostream &OS, bool PrettyPrint = false,
                             std::unique_ptr<DelimitedScope> &&OuterScope =
                                 std::unique_ptr<DelimitedScope>{});

  /// Return true if \p SP is a \c JSONScopedPrinter.
  ///
  /// \param SP Printer to test.
  /// \return True when \p SP has kind \c JSON.
  static bool classof(const ScopedPrinter *SP) {
    return SP->getKind() == ScopedPrinter::ScopedPrinterKind::JSON;
  }

  /// Print a labeled char number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, char Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled signed char number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, signed char Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled unsigned char number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, unsigned char Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled short number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, short Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled unsigned short number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, unsigned short Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled int number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, int Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled unsigned int number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, unsigned int Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled long number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, long Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled unsigned long number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, unsigned long Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled long long number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, long long Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled unsigned long long number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, unsigned long long Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled floating-point number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, float Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled double-precision number as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, double Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled arbitrary-precision integer as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Number to emit.
  void printNumber(StringRef Label, const APSInt &Value) override {
    JOS.attributeBegin(Label);
    printAPSInt(Value);
    JOS.attributeEnd();
  }

  /// Print a labeled boolean as a JSON attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value Boolean value to emit.
  void printBoolean(StringRef Label, bool Value) override {
    JOS.attribute(Label, Value);
  }

  /// Print a labeled JSON array of boolean values.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<bool> List) override {
    printListImpl(Label, List);
  }

  /// Print a labeled JSON array of strings.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<std::string> List) override {
    printListImpl(Label, List);
  }

  /// Print a labeled JSON array of 64-bit unsigned integers.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<uint64_t> List) override {
    printListImpl(Label, List);
  }

  /// Print a labeled JSON array of 32-bit unsigned integers.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<uint32_t> List) override {
    printListImpl(Label, List);
  }

  /// Print a labeled JSON array of 16-bit unsigned integers.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<uint16_t> List) override {
    printListImpl(Label, List);
  }

  /// Print a labeled JSON array of 8-bit unsigned integers.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<uint8_t> List) override {
    printListImpl(Label, List);
  }

  /// Print a labeled JSON array of 64-bit signed integers.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<int64_t> List) override {
    printListImpl(Label, List);
  }

  /// Print a labeled JSON array of 32-bit signed integers.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<int32_t> List) override {
    printListImpl(Label, List);
  }

  /// Print a labeled JSON array of 16-bit signed integers.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<int16_t> List) override {
    printListImpl(Label, List);
  }

  /// Print a labeled JSON array of 8-bit signed integers.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<int8_t> List) override {
    printListImpl(Label, List);
  }

  /// Print a labeled JSON array of arbitrary-precision integers.
  ///
  /// \param Label JSON attribute name.
  /// \param List Elements to emit.
  void printList(StringRef Label, const ArrayRef<APSInt> List) override {
    JOS.attributeArray(Label, [&]() {
      for (const APSInt &Item : List) {
        printAPSInt(Item);
      }
    });
  }

  /// Print an unlabeled JSON string value.
  ///
  /// \param Value String to emit.
  void printString(StringRef Value) override { JOS.value(Value); }

  /// Print a labeled JSON string attribute.
  ///
  /// \param Label JSON attribute name.
  /// \param Value String to emit.
  void printString(StringRef Label, StringRef Value) override {
    JOS.attribute(Label, Value);
  }

  /// Begin an unlabeled JSON object.
  void objectBegin() override {
    scopedBegin({Scope::Object, ScopeKind::NoAttribute});
  }

  /// Begin a labeled JSON object attribute.
  ///
  /// \param Label JSON attribute name for the object.
  void objectBegin(StringRef Label) override {
    scopedBegin(Label, Scope::Object);
  }

  /// End the current JSON object.
  void objectEnd() override { scopedEnd(); }

  /// Begin an unlabeled JSON array.
  void arrayBegin() override {
    scopedBegin({Scope::Array, ScopeKind::NoAttribute});
  }

  /// Begin a labeled JSON array attribute.
  ///
  /// \param Label JSON attribute name for the array.
  void arrayBegin(StringRef Label) override {
    scopedBegin(Label, Scope::Array);
  }

  /// End the current JSON array.
  void arrayEnd() override { scopedEnd(); }

private:
  // Output HexNumbers as decimals so that they're easier to parse.
  uint64_t hexNumberToInt(HexNumber Hex) { return Hex.Value; }

  void printAPSInt(const APSInt &Value) {
    JOS.rawValueBegin() << Value;
    JOS.rawValueEnd();
  }

  void printFlagsImpl(StringRef Label, HexNumber Value,
                      ArrayRef<FlagEntry> Flags) override {
    JOS.attributeObject(Label, [&]() {
      JOS.attribute("Value", hexNumberToInt(Value));
      JOS.attributeArray("Flags", [&]() {
        for (const FlagEntry &Flag : Flags) {
          JOS.objectBegin();
          JOS.attribute("Name", Flag.Name);
          JOS.attribute("Value", Flag.Value);
          JOS.objectEnd();
        }
      });
    });
  }

  void printFlagsImpl(StringRef Label, HexNumber Value,
                      ArrayRef<HexNumber> Flags) override {
    JOS.attributeObject(Label, [&]() {
      JOS.attribute("Value", hexNumberToInt(Value));
      JOS.attributeArray("Flags", [&]() {
        for (const HexNumber &Flag : Flags) {
          JOS.value(Flag.Value);
        }
      });
    });
  }

  template <typename T> void printListImpl(StringRef Label, const T &List) {
    JOS.attributeArray(Label, [&]() {
      for (const auto &Item : List)
        JOS.value(Item);
    });
  }

  void printHexListImpl(StringRef Label,
                        const ArrayRef<HexNumber> List) override {
    JOS.attributeArray(Label, [&]() {
      for (const HexNumber &Item : List) {
        JOS.value(hexNumberToInt(Item));
      }
    });
  }

  void printHexImpl(StringRef Label, HexNumber Value) override {
    JOS.attribute(Label, hexNumberToInt(Value));
  }

  void printHexImpl(StringRef Label, StringRef Str, HexNumber Value) override {
    JOS.attributeObject(Label, [&]() {
      JOS.attribute("Name", Str);
      JOS.attribute("Value", hexNumberToInt(Value));
    });
  }

  void printSymbolOffsetImpl(StringRef Label, StringRef Symbol,
                             HexNumber Value) override {
    JOS.attributeObject(Label, [&]() {
      JOS.attribute("SymName", Symbol);
      JOS.attribute("Offset", hexNumberToInt(Value));
    });
  }

  void printNumberImpl(StringRef Label, StringRef Str,
                       StringRef Value) override {
    JOS.attributeObject(Label, [&]() {
      JOS.attribute("Name", Str);
      JOS.attributeBegin("Value");
      JOS.rawValueBegin() << Value;
      JOS.rawValueEnd();
      JOS.attributeEnd();
    });
  }

  void printBinaryImpl(StringRef Label, StringRef Str, ArrayRef<uint8_t> Value,
                       bool Block, uint32_t StartOffset = 0) override {
    JOS.attributeObject(Label, [&]() {
      if (!Str.empty())
        JOS.attribute("Value", Str);
      JOS.attribute("Offset", StartOffset);
      JOS.attributeArray("Bytes", [&]() {
        for (uint8_t Val : Value)
          JOS.value(Val);
      });
    });
  }

  void scopedBegin(ScopeContext ScopeCtx) {
    if (ScopeCtx.Context == Scope::Object)
      JOS.objectBegin();
    else if (ScopeCtx.Context == Scope::Array)
      JOS.arrayBegin();
    ScopeHistory.push_back(ScopeCtx);
  }

  void scopedBegin(StringRef Label, Scope Ctx) {
    ScopeKind Kind = ScopeKind::Attribute;
    if (ScopeHistory.empty() || ScopeHistory.back().Context != Scope::Object) {
      JOS.objectBegin();
      Kind = ScopeKind::NestedAttribute;
    }
    JOS.attributeBegin(Label);
    scopedBegin({Ctx, Kind});
  }

  void scopedEnd() {
    ScopeContext ScopeCtx = ScopeHistory.back();
    if (ScopeCtx.Context == Scope::Object)
      JOS.objectEnd();
    else if (ScopeCtx.Context == Scope::Array)
      JOS.arrayEnd();
    if (ScopeCtx.Kind == ScopeKind::Attribute ||
        ScopeCtx.Kind == ScopeKind::NestedAttribute)
      JOS.attributeEnd();
    if (ScopeCtx.Kind == ScopeKind::NestedAttribute)
      JOS.objectEnd();
    ScopeHistory.pop_back();
  }
};

/// RAII helper that opens and closes an object scope on a printer.
struct DictScope : DelimitedScope {
  /// Construct an unbound dictionary scope.
  explicit DictScope() = default;
  /// Begin an unlabeled object scope on \p W.
  ///
  /// \param W Printer that owns the object scope.
  explicit DictScope(ScopedPrinter &W) : DelimitedScope(W) { W.objectBegin(); }

  /// Begin a labeled object scope on \p W.
  ///
  /// \param W Printer that owns the object scope.
  /// \param N Label for the object.
  DictScope(ScopedPrinter &W, StringRef N) : DelimitedScope(W) {
    W.objectBegin(N);
  }

  /// Bind this scope to \p W and begin an unlabeled object.
  ///
  /// \param W Printer that should own the object scope.
  void setPrinter(ScopedPrinter &W) override {
    this->W = &W;
    W.objectBegin();
  }

  /// End the object scope if a printer is bound.
  ~DictScope() override {
    if (W)
      W->objectEnd();
  }
};

/// RAII helper that opens and closes an array scope on a printer.
struct ListScope : DelimitedScope {
  /// Construct an unbound list scope.
  explicit ListScope() = default;
  /// Begin an unlabeled array scope on \p W.
  ///
  /// \param W Printer that owns the array scope.
  explicit ListScope(ScopedPrinter &W) : DelimitedScope(W) { W.arrayBegin(); }

  /// Begin a labeled array scope on \p W.
  ///
  /// \param W Printer that owns the array scope.
  /// \param N Label for the array.
  ListScope(ScopedPrinter &W, StringRef N) : DelimitedScope(W) {
    W.arrayBegin(N);
  }

  /// Bind this scope to \p W and begin an unlabeled array.
  ///
  /// \param W Printer that should own the array scope.
  void setPrinter(ScopedPrinter &W) override {
    this->W = &W;
    W.arrayBegin();
  }

  /// End the array scope if a printer is bound.
  ~ListScope() override {
    if (W)
      W->arrayEnd();
  }
};

} // namespace llvm

#endif
