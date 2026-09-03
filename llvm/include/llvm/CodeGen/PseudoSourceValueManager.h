//===-- llvm/CodeGen/PseudoSourceValueManager.h -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the PseudoSourceValueManager class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PSEUDOSOURCEVALUEMANAGER_H
#define LLVM_CODEGEN_PSEUDOSOURCEVALUEMANAGER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class GlobalValue;
class TargetMachine;

/// Manages creation of pseudo source values.
class PseudoSourceValueManager {
  const TargetMachine &TM;
  const PseudoSourceValue StackPSV, GOTPSV, JumpTablePSV, ConstantPoolPSV;
  SmallVector<std::unique_ptr<FixedStackPseudoSourceValue>> FSValues;
  StringMap<std::unique_ptr<const ExternalSymbolPseudoSourceValue>>
      ExternalCallEntries;
  ValueMap<const GlobalValue *,
           std::unique_ptr<const GlobalValuePseudoSourceValue>>
      GlobalCallEntries;

public:
  /// Construct a PseudoSourceValueManager for the given target.
  ///
  /// \param TM Target machine used to initialize shared pseudo source values.
  LLVM_ABI PseudoSourceValueManager(const TargetMachine &TM);

  /// Return a pseudo source value referencing the area below the stack frame of
  /// a function, e.g., the argument space.
  ///
  /// \return Pseudo source value for the area below the stack frame.
  LLVM_ABI const PseudoSourceValue *getStack();

  /// Return a pseudo source value referencing the global offset table
  /// (or something the like).
  ///
  /// \return Pseudo source value for the global offset table.
  LLVM_ABI const PseudoSourceValue *getGOT();

  /// Return a pseudo source value referencing the constant pool. Since constant
  /// pools are constant, this doesn't need to identify a specific constant
  /// pool entry.
  ///
  /// \return Pseudo source value for the constant pool.
  LLVM_ABI const PseudoSourceValue *getConstantPool();

  /// Return a pseudo source value referencing a jump table. Since jump tables
  /// are constant, this doesn't need to identify a specific jump table.
  ///
  /// \return Pseudo source value for a jump table.
  LLVM_ABI const PseudoSourceValue *getJumpTable();

  /// Return a pseudo source value referencing a fixed stack frame entry,
  /// e.g., a spill slot.
  ///
  /// \param FI Frame index of the fixed stack object.
  /// \return Pseudo source value for the fixed stack frame entry.
  LLVM_ABI const PseudoSourceValue *getFixedStack(int FI);

  /// Return a pseudo source value for a call entry referencing GlobalValue \p GV.
  ///
  /// \param GV Global value referenced by the call entry.
  /// \return Pseudo source value for the GlobalValue call entry.
  LLVM_ABI const PseudoSourceValue *
  getGlobalValueCallEntry(const GlobalValue *GV);

  /// Return a pseudo source value for a call entry referencing external symbol
  /// \p ES.
  ///
  /// \param ES External symbol name referenced by the call entry.
  /// \return Pseudo source value for the external symbol call entry.
  LLVM_ABI const PseudoSourceValue *getExternalSymbolCallEntry(const char *ES);
};

} // end namespace llvm

#endif
