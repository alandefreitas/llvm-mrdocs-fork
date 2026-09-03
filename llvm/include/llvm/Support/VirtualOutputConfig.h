//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the OutputConfig class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_VIRTUALOUTPUTCONFIG_H
#define LLVM_SUPPORT_VIRTUALOUTPUTCONFIG_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class raw_ostream;

namespace sys::fs {
enum OpenFlags : unsigned;
} // end namespace sys::fs

namespace vfs {

namespace detail {
/// Unused and empty base class to allow OutputConfig constructor to be
/// constexpr, with commas before every field's initializer.
struct EmptyBaseClass {};
} // namespace detail

/// Full configuration for an output for use by the \a OutputBackend. Each
/// configuration flag is either \c true or \c false.
struct OutputConfig : detail::EmptyBaseClass {
public:
  /// Print this configuration to \p OS.
  /// \param OS Stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump this configuration to stderr.
  LLVM_ABI void dump() const;

#define HANDLE_OUTPUT_CONFIG_FLAG(NAME, DEFAULT)                               \
  constexpr bool get##NAME() const { return NAME; }                            \
  constexpr bool getNo##NAME() const { return !NAME; }                         \
  constexpr OutputConfig &set##NAME(bool Value) {                              \
    NAME = Value;                                                              \
    return *this;                                                              \
  }                                                                            \
  constexpr OutputConfig &set##NAME() { return set##NAME(true); }              \
  constexpr OutputConfig &setNo##NAME() { return set##NAME(false); }
#include "llvm/Support/VirtualOutputConfig.def"

  /// Configure for binary output by clearing Text and CRLF.
  /// \returns This configuration for chaining.
  constexpr OutputConfig &setBinary() { return setNoText().setNoCRLF(); }
  /// Configure for text output with CRLF line endings.
  /// \returns This configuration for chaining.
  constexpr OutputConfig &setTextWithCRLF() { return setText().setCRLF(); }
  /// Set text-with-CRLF when \p Value is true; otherwise set binary.
  /// \param Value Whether to enable text with CRLF, or binary instead.
  /// \returns This configuration for chaining.
  constexpr OutputConfig &setTextWithCRLF(bool Value) {
    return Value ? setText().setCRLF() : setBinary();
  }
  /// Return true if both Text and CRLF are enabled.
  /// \returns True if both Text and CRLF are enabled.
  constexpr bool getTextWithCRLF() const { return getText() && getCRLF(); }
  /// Return true if this config is for binary output (Text is false).
  /// \returns True if Text is disabled.
  constexpr bool getBinary() const { return !getText(); }

  /// Updates Text and CRLF flags based on \a sys::fs::OF_Text and \a
  /// sys::fs::OF_CRLF in \p Flags. Rejects CRLF without Text (calling
  /// \a setBinary()).
  /// \param Flags Open flags whose Text and CRLF bits are applied.
  /// \returns This configuration for chaining.
  LLVM_ABI OutputConfig &setOpenFlags(const sys::fs::OpenFlags &Flags);

  /// Construct a configuration with each flag at its default value.
  constexpr OutputConfig()
      : EmptyBaseClass()
  /// Expand to a member initializer for one OutputConfig flag.
  /// \param NAME Flag name used as the bitfield member.
  /// \param DEFAULT Default value for the flag.
#define HANDLE_OUTPUT_CONFIG_FLAG(NAME, DEFAULT) , NAME(DEFAULT)
#include "llvm/Support/VirtualOutputConfig.def"
  {
  }

  /// Return true if this configuration equals \p RHS.
  /// \param RHS Other configuration to compare.
  /// \returns True if all flags match \p RHS.
  bool operator==(OutputConfig RHS) const {
#define HANDLE_OUTPUT_CONFIG_FLAG(NAME, DEFAULT)                               \
  if (NAME != RHS.NAME)                                                        \
    return false;
#include "llvm/Support/VirtualOutputConfig.def"
    return true;
  }
  /// Return true if this configuration differs from \p RHS.
  /// \param RHS Other configuration to compare.
  /// \returns True if any flag differs from \p RHS.
  bool operator!=(OutputConfig RHS) const { return !operator==(RHS); }

private:
#define HANDLE_OUTPUT_CONFIG_FLAG(NAME, DEFAULT) bool NAME : 1;
#include "llvm/Support/VirtualOutputConfig.def"
};

} // namespace vfs

/// Print \p Config to \p OS.
/// \param OS Stream to print to.
/// \param Config Configuration to print.
/// \returns The stream \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, vfs::OutputConfig Config);

} // namespace llvm

#endif // LLVM_SUPPORT_VIRTUALOUTPUTCONFIG_H
