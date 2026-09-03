//===- CSEConfigBase.h - A CSEConfig interface ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CSECONFIGBASE_H
#define LLVM_CODEGEN_CSECONFIGBASE_H

namespace llvm {
/// Base configuration for CSE during GlobalISel CSEInfo analysis.
///
/// Defined here because TargetPassConfig can't depend on the GlobalISel
/// library. Used in the interface between them so that derived classes in
/// GISel can reference generic opcodes.
class CSEConfigBase {
public:
  /// Virtual destructor.
  virtual ~CSEConfigBase() = default;
  /// Return whether the given generic opcode should be CSEd.
  ///
  /// GISelCSEInfo currently only calls this hook when dealing with generic
  /// opcodes.
  ///
  /// \param Opc Opcode to check for CSE eligibility.
  /// \return True if the opcode should be common-subexpression eliminated.
  virtual bool shouldCSEOpc(unsigned Opc) { return false; }
};

} // namespace llvm

#endif // LLVM_CODEGEN_CSECONFIGBASE_H
