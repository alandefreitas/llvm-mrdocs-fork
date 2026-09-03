//===- ReplaceConstant.h - Replacing LLVM constant expressions --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the utility function for replacing LLVM constant
// expressions by instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_REPLACECONSTANT_H
#define LLVM_IR_REPLACECONSTANT_H

#include "llvm/Support/Compiler.h"

namespace llvm {

template <typename T> class ArrayRef;
class Constant;
class Function;

/// Replace constant expressions users of the given constants with
/// instructions.
///
/// \param Consts The constants whose users should be converted to
/// instructions.
/// \param RestrictToFunc If non-null, restrict replacement to this
/// function's scope rather than performing it at module scope.
/// \param RemoveDeadConstants If true (the default), remove all dead
/// constants as the final step after replacement; if false, skip that
/// step.
/// \param IncludeSelf If true, also convert the passed constants
/// themselves to instructions, rather than only their users.
/// \return True if any users (or constants, when \p IncludeSelf) were
/// converted to instructions.
LLVM_ABI bool convertUsersOfConstantsToInstructions(
    ArrayRef<Constant *> Consts, Function *RestrictToFunc = nullptr,
    bool RemoveDeadConstants = true, bool IncludeSelf = false);

} // end namespace llvm

#endif // LLVM_IR_REPLACECONSTANT_H
