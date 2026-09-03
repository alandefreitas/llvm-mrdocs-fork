//===- OptSpecifier.h - Option Specifiers -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPTION_OPTSPECIFIER_H
#define LLVM_OPTION_OPTSPECIFIER_H

#include "llvm/Support/Compiler.h"

namespace llvm {
namespace opt {

class Option;

/// OptSpecifier - Wrapper class for abstracting references to option IDs.
class OptSpecifier {
  unsigned ID = 0;

public:
  /// Construct an invalid option specifier with ID zero.
  OptSpecifier() = default;
  /// Deleted; construction from bool is not allowed.
  ///
  /// This prevents accidental conversion from bool through unsigned.
  ///
  /// \param Value Unused; construction from bool is deleted.
  explicit OptSpecifier(bool Value) = delete;
  /// Construct a specifier from a raw option ID.
  ///
  /// \param ID Option identifier to wrap.
  /*implicit*/ OptSpecifier(unsigned ID) : ID(ID) {}
  /// Construct a specifier from an Option pointer.
  ///
  /// \param Opt Option whose ID is used, or nullptr for an invalid specifier.
  /*implicit*/ LLVM_ABI OptSpecifier(const Option *Opt);

  /// Return true if this specifier refers to a valid option ID.
  ///
  /// \return True if the wrapped option ID is non-zero.
  bool isValid() const { return ID != 0; }

  /// Return the option ID wrapped by this specifier.
  ///
  /// \return The option identifier, or zero for an invalid specifier.
  unsigned getID() const { return ID; }

  /// Return true if this specifier wraps the same option ID as \p Opt.
  ///
  /// \param Opt Specifier to compare against.
  /// \return True if both specifiers wrap the same option ID.
  bool operator==(OptSpecifier Opt) const { return ID == Opt.getID(); }
  /// Return true if this specifier wraps a different option ID than \p Opt.
  ///
  /// \param Opt Specifier to compare against.
  /// \return True if the specifiers wrap different option IDs.
  bool operator!=(OptSpecifier Opt) const { return !(*this == Opt); }
};

} // end namespace opt
} // end namespace llvm

#endif // LLVM_OPTION_OPTSPECIFIER_H
