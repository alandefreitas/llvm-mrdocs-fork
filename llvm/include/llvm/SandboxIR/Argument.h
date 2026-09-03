//===- Argument.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_ARGUMENT_H
#define LLVM_SANDBOXIR_ARGUMENT_H

#include "llvm/IR/Argument.h"
#include "llvm/SandboxIR/Value.h"

namespace llvm {
/// Transactional IR layer over LLVM IR with save/restore and an LLVM-like API.
namespace sandboxir {

/// Argument of a sandboxir::Function.
class Argument : public sandboxir::Value {
  Argument(llvm::Argument *Arg, sandboxir::Context &Ctx)
      : Value(ClassID::Argument, Arg, Ctx) {}
  friend class Context; // For constructor.

public:
  /// For isa/dyn_cast.
  /// \param From Value to test for Argument.
  /// \Returns True if \p From is an Argument.
  static bool classof(const sandboxir::Value *From) {
    return From->getSubclassID() == ClassID::Argument;
  }
#ifndef NDEBUG
  /// Verify that this wraps an LLVM Argument.
  void verify() const final {
    assert(isa<llvm::Argument>(Val) && "Expected Argument!");
  }
  /// Print this argument as an operand to \p OS.
  /// \param OS Output stream.
  void printAsOperand(raw_ostream &OS) const;
  /// Dump this argument to \p OS.
  /// \param OS Output stream.
  LLVM_ABI void dumpOS(raw_ostream &OS) const final;
#endif
};

} // namespace sandboxir
} // namespace llvm

#endif // LLVM_SANDBOXIR_ARGUMENT_H
