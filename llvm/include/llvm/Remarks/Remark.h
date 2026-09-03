//===-- llvm/Remarks/Remark.h - The remark type -----------------*- C++/-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an abstraction for handling remarks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_REMARK_H
#define LLVM_REMARKS_REMARK_H

#include "llvm-c/Remarks.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <string>

namespace llvm {
namespace remarks {

/// The current version of the remark entry.
constexpr uint64_t CurrentRemarkVersion = 0;

/// The debug location used to track a remark back to the source file.
struct RemarkLocation {
  /// Absolute path of the source file corresponding to this remark.
  StringRef SourceFilePath;
  /// Source line number corresponding to this remark (1-based).
  unsigned SourceLine = 0;
  /// Source column number corresponding to this remark (1-based).
  unsigned SourceColumn = 0;

  /// Implement operator<< on RemarkLocation.
  /// @param OS Stream to write to.
  LLVM_ABI void print(raw_ostream &OS) const;
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// C API conversion helpers for \c RemarkLocation /
/// \c LLVMRemarkDebugLocRef, including \c unwrap and \c wrap.
/// @param P Value to convert between the C++ type and the C API reference.
/// @return The corresponding C++ pointer or C API reference.
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(RemarkLocation, LLVMRemarkDebugLocRef)

/// A key-value pair with a debug location that is used to display the remarks
/// at the right place in the source.
struct Argument {
  /// Key of this remark argument.
  StringRef Key;
  // FIXME: We might want to be able to store other types than strings here.
  /// String value of this remark argument.
  StringRef Val;
  /// Debug location corresponding to the value, if set.
  std::optional<RemarkLocation> Loc;

  /// Construct an empty argument with no key, value, or location.
  Argument() = default;
  /// Construct an argument from \p Key and \p Val.
  /// @param Key Argument key.
  /// @param Val Argument value.
  Argument(StringRef Key, StringRef Val) : Key(Key), Val(Val) {}

  /// Implement operator<< on Argument.
  /// @param OS Stream to write to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Return the value of argument as an integer of type T.
  /// @param Radix Numeric base used to parse \c Val (default 10).
  /// @return Parsed integer of type T, or \c std::nullopt if parsing fails.
  template <typename T>
  std::optional<T> getValAsInt(unsigned Radix = 10) const {
    StringRef Str = Val;
    T Res;
    if (Str.consumeInteger<T>(Radix, Res) || !Str.empty())
      return std::nullopt;
    return Res;
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// C API conversion helpers for \c Argument / \c LLVMRemarkArgRef, including
/// \c unwrap and \c wrap.
/// @param P Value to convert between the C++ type and the C API reference.
/// @return The corresponding C++ pointer or C API reference.
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(Argument, LLVMRemarkArgRef)

/// The type of the remark.
enum class Type {
  /// Unspecified or unrecognized remark type.
  Unknown,
  /// An optimization was successfully applied.
  Passed,
  /// An optimization opportunity was missed.
  Missed,
  /// An analysis remark providing informational output.
  Analysis,
  /// An analysis remark about floating-point commutation.
  AnalysisFPCommute,
  /// An analysis remark about memory aliasing.
  AnalysisAliasing,
  /// A failure that prevented an optimization or analysis.
  Failure,
  /// First enumerator value; equal to \c Unknown.
  First = Unknown,
  /// Last enumerator value; equal to \c Failure.
  Last = Failure
};

/// Return the string spelling of remark type \p Ty.
/// @param Ty Remark type to convert.
/// @return Textual name of \p Ty.
inline StringRef typeToStr(Type Ty) {
  switch (Ty) {
  case Type::Unknown:
    return "Unknown";
  case Type::Missed:
    return "Missed";
  case Type::Passed:
    return "Passed";
  case Type::Analysis:
    return "Analysis";
  case Type::AnalysisFPCommute:
    return "AnalysisFPCommute";
  case Type::AnalysisAliasing:
    return "AnalysisAliasing";
  default:
    return "Failure";
  }
}

/// A remark type used for both emission and parsing.
struct Remark {
  /// The type of the remark.
  Type RemarkType = Type::Unknown;

  /// Name of the pass that triggers the emission of this remark.
  StringRef PassName;

  /// Textual identifier for the remark (single-word, camel-case). Can be used
  /// by external tools reading the output file for remarks to identify the
  /// remark.
  StringRef RemarkName;

  /// Mangled name of the function that triggers the emssion of this remark.
  StringRef FunctionName;

  /// The location in the source file of the remark.
  std::optional<RemarkLocation> Loc;

  /// If profile information is available, this is the number of times the
  /// corresponding code was executed in a profile instrumentation run.
  std::optional<uint64_t> Hotness;

  /// Arguments collected via the streaming interface.
  SmallVector<Argument, 5> Args;

  /// Construct an empty remark with default field values.
  Remark() = default;
  /// Move-construct a remark.
  /// @param RemarkToMove Remark to move from.
  Remark(Remark &&RemarkToMove) = default;
  /// Move-assign a remark.
  /// @param RemarkToMove Remark to move from.
  /// @return Reference to this remark.
  Remark &operator=(Remark &&RemarkToMove) = default;

  /// Return a message composed from the arguments as a string.
  /// @return Message string built from this remark's arguments.
  LLVM_ABI std::string getArgsAsMsg() const;

  /// Return the first argument with the specified key or nullptr if no such
  /// argument was found.
  /// @param Key Argument key to search for.
  /// @return Pointer to the matching argument, or nullptr if none was found.
  LLVM_ABI Argument *getArgByKey(StringRef Key);

  /// Clone this remark to explicitly ask for a copy.
  /// @return A copy of this remark.
  Remark clone() const { return *this; }

  /// Implement operator<< on Remark.
  /// @param OS Stream to write to.
  LLVM_ABI void print(raw_ostream &OS) const;

private:
  /// In order to avoid unwanted copies, "delete" the copy constructor.
  /// If a copy is needed, it should be done through `Remark::clone()`.
  Remark(const Remark &) = default;
  Remark& operator=(const Remark &) = default;
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// C API conversion helpers for \c Remark / \c LLVMRemarkEntryRef, including
/// \c unwrap and \c wrap.
/// @param P Value to convert between the C++ type and the C API reference.
/// @return The corresponding C++ pointer or C API reference.
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(Remark, LLVMRemarkEntryRef)

/// Comparison operators for Remark objects and dependent objects.

/// Compare optional values so that unset entries sort before set ones.
///
/// Sorting based on optionals should result in all `None` entries to appear
/// before the valid entries. For example, remarks with no debug location will
/// appear first.
/// @param LHS Left-hand optional value.
/// @param RHS Right-hand optional value.
/// @return true if \p LHS should sort before \p RHS.
template <typename T>
bool operator<(const std::optional<T> &LHS, const std::optional<T> &RHS) {
  if (!LHS && !RHS)
    return false;
  if (!LHS && RHS)
    return true;
  if (LHS && !RHS)
    return false;
  return *LHS < *RHS;
}

/// Return true if remark locations \p LHS and \p RHS are equal.
/// @param LHS Left-hand location.
/// @param RHS Right-hand location.
/// @return true if \p LHS and \p RHS are equal.
inline bool operator==(const RemarkLocation &LHS, const RemarkLocation &RHS) {
  return LHS.SourceFilePath == RHS.SourceFilePath &&
         LHS.SourceLine == RHS.SourceLine &&
         LHS.SourceColumn == RHS.SourceColumn;
}

/// Return true if remark locations \p LHS and \p RHS differ.
/// @param LHS Left-hand location.
/// @param RHS Right-hand location.
/// @return true if \p LHS and \p RHS differ.
inline bool operator!=(const RemarkLocation &LHS, const RemarkLocation &RHS) {
  return !(LHS == RHS);
}

/// Return true if location \p LHS should sort before \p RHS.
/// @param LHS Left-hand location.
/// @param RHS Right-hand location.
/// @return true if \p LHS should sort before \p RHS.
inline bool operator<(const RemarkLocation &LHS, const RemarkLocation &RHS) {
  return std::make_tuple(LHS.SourceFilePath, LHS.SourceLine, LHS.SourceColumn) <
         std::make_tuple(RHS.SourceFilePath, RHS.SourceLine, RHS.SourceColumn);
}

/// Return true if arguments \p LHS and \p RHS are equal.
/// @param LHS Left-hand argument.
/// @param RHS Right-hand argument.
/// @return true if \p LHS and \p RHS are equal.
inline bool operator==(const Argument &LHS, const Argument &RHS) {
  return LHS.Key == RHS.Key && LHS.Val == RHS.Val && LHS.Loc == RHS.Loc;
}

/// Return true if arguments \p LHS and \p RHS differ.
/// @param LHS Left-hand argument.
/// @param RHS Right-hand argument.
/// @return true if \p LHS and \p RHS differ.
inline bool operator!=(const Argument &LHS, const Argument &RHS) {
  return !(LHS == RHS);
}

/// Return true if argument \p LHS should sort before \p RHS.
/// @param LHS Left-hand argument.
/// @param RHS Right-hand argument.
/// @return true if \p LHS should sort before \p RHS.
inline bool operator<(const Argument &LHS, const Argument &RHS) {
  return std::make_tuple(LHS.Key, LHS.Val, LHS.Loc) <
         std::make_tuple(RHS.Key, RHS.Val, RHS.Loc);
}

/// Return true if remarks \p LHS and \p RHS are equal.
/// @param LHS Left-hand remark.
/// @param RHS Right-hand remark.
/// @return true if \p LHS and \p RHS are equal.
inline bool operator==(const Remark &LHS, const Remark &RHS) {
  return LHS.RemarkType == RHS.RemarkType && LHS.PassName == RHS.PassName &&
         LHS.RemarkName == RHS.RemarkName &&
         LHS.FunctionName == RHS.FunctionName && LHS.Loc == RHS.Loc &&
         LHS.Hotness == RHS.Hotness && LHS.Args == RHS.Args;
}

/// Return true if remarks \p LHS and \p RHS differ.
/// @param LHS Left-hand remark.
/// @param RHS Right-hand remark.
/// @return true if \p LHS and \p RHS differ.
inline bool operator!=(const Remark &LHS, const Remark &RHS) {
  return !(LHS == RHS);
}

/// Return true if remark \p LHS should sort before \p RHS.
/// @param LHS Left-hand remark.
/// @param RHS Right-hand remark.
/// @return true if \p LHS should sort before \p RHS.
inline bool operator<(const Remark &LHS, const Remark &RHS) {
  return std::make_tuple(LHS.RemarkType, LHS.PassName, LHS.RemarkName,
                         LHS.FunctionName, LHS.Loc, LHS.Hotness, LHS.Args) <
         std::make_tuple(RHS.RemarkType, RHS.PassName, RHS.RemarkName,
                         RHS.FunctionName, RHS.Loc, RHS.Hotness, RHS.Args);
}

/// Write remark location \p RLoc to \p OS.
/// @param OS Stream to write to.
/// @param RLoc Location to print.
/// @return Reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const RemarkLocation &RLoc) {
  RLoc.print(OS);
  return OS;
}

/// Write argument \p Arg to \p OS.
/// @param OS Stream to write to.
/// @param Arg Argument to print.
/// @return Reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const Argument &Arg) {
  Arg.print(OS);
  return OS;
}

/// Write remark \p Remark to \p OS.
/// @param OS Stream to write to.
/// @param Remark Remark to print.
/// @return Reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const Remark &Remark) {
  Remark.print(OS);
  return OS;
}

} // end namespace remarks
} // end namespace llvm

#endif /* LLVM_REMARKS_REMARK_H */
