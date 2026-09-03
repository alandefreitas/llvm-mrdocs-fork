//===- MCLabel.h - Machine Code Directional Local Labels --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCLabel class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCLABEL_H
#define LLVM_MC_MCLABEL_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class raw_ostream;

/// Represents a label name in the MC file.
///
/// MCLabels are created and uniqued by the MCContext class. MCLabel
/// should only be constructed for valid instances in the object file.
class MCLabel {
  // The instance number of this Directional Local Label.
  unsigned Instance;

private: // MCContext creates and uniques these.
  friend class MCContext;

  MCLabel(unsigned instance) : Instance(instance) {}

public:
  /// Deleted copy constructor.
  ///
  /// \param Other - Unused; copy construction is deleted.
  MCLabel(const MCLabel &Other) = delete;
  /// Deleted copy assignment.
  ///
  /// \param Other - Unused; copy assignment is deleted.
  MCLabel &operator=(const MCLabel &Other) = delete;

  /// Get the current instance of this Directional Local Label.
  ///
  /// \return The current instance number.
  unsigned getInstance() const { return Instance; }

  /// Increment the current instance of this Directional Local Label.
  ///
  /// \return The new instance number after incrementing.
  unsigned incInstance() { return ++Instance; }

  /// Print the value to the stream \p OS.
  ///
  /// \param OS - Stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Print the value to stderr.
  LLVM_ABI void dump() const;
};

/// Print \p Label to \p OS.
///
/// \param OS - Stream to print to.
/// \param Label - Label to print.
/// \return A reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const MCLabel &Label) {
  Label.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_MC_MCLABEL_H
