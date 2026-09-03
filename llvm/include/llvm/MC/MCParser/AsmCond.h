//===- AsmCond.h - Assembly file conditional assembly  ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_ASMCOND_H
#define LLVM_MC_MCPARSER_ASMCOND_H

namespace llvm {

/// AsmCond - Class to support conditional assembly
///
/// The conditional assembly feature (.if, .else, .elseif and .endif) is
/// implemented with AsmCond that tells us what we are in the middle of
/// processing.  Ignore can be either true or false.  When true we are ignoring
/// the block of code in the middle of a conditional.

class AsmCond {
public:
  /// Kind of conditional-assembly directive currently being processed.
  enum ConditionalAssemblyType {
    NoCond,     ///< No conditional is being processed.
    IfCond,     ///< Inside an .if conditional.
    ElseIfCond, ///< Inside an .elseif conditional.
    ElseCond    ///< Inside an .else conditional.
  };

  /// Current conditional-assembly directive kind.
  ConditionalAssemblyType TheCond = NoCond;
  /// True if a prior branch of this conditional already matched.
  bool CondMet = false;
  /// True when the current conditional block should be skipped.
  bool Ignore = false;
};

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_ASMCOND_H
