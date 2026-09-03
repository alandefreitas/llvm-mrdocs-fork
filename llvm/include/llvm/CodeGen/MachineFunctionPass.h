//===-- MachineFunctionPass.h - Pass for MachineFunctions --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachineFunctionPass class.  MachineFunctionPass's are
// just FunctionPass's, except they operate on machine code as part of a code
// generator.  Because they operate on machine code, not the LLVM
// representation, MachineFunctionPass's are not allowed to modify the LLVM
// representation.  Due to this limitation, the MachineFunctionPass class takes
// care of declaring that no LLVM passes are invalidated.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEFUNCTIONPASS_H
#define LLVM_CODEGEN_MACHINEFUNCTIONPASS_H

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// FunctionPass adapter for passes that operate on a MachineFunction.
///
/// This class adapts the FunctionPass interface to allow convenient creation
/// of passes that operate on the MachineFunction representation. Instead of
/// overriding runOnFunction, subclasses override runOnMachineFunction.
class LLVM_ABI MachineFunctionPass : public FunctionPass {
public:
  /// Cache this pass's MachineFunctionProperties at module initialization.
  ///
  /// Properties are recorded once at module-init time so they are not
  /// reconstructed for every function.
  /// @param M Module for which this pass is being initialized.
  /// @return Always false; this initializer does not modify the module.
  bool doInitialization(Module &M) override {
    // Cache the properties info at module-init time so we don't have to
    // construct them for every function.
    RequiredProperties = getRequiredProperties();
    SetProperties = getSetProperties();
    ClearedProperties = getClearedProperties();
    return false;
  }
protected:
  /// Construct a MachineFunctionPass with the given pass identifier.
  /// @param ID Address of the pass's static identification character.
  explicit MachineFunctionPass(char &ID) : FunctionPass(ID) {}

  /// runOnMachineFunction - This method must be overloaded to perform the
  /// desired machine code transformation or analysis.
  ///
  /// @param MF Machine function to transform or analyze.
  /// @return True if the machine function was modified; false otherwise.
  virtual bool runOnMachineFunction(MachineFunction &MF) = 0;

  /// getAnalysisUsage - Subclasses that override getAnalysisUsage
  /// must call this.
  ///
  /// For MachineFunctionPasses, calling AU.preservesCFG() indicates that
  /// the pass does not modify the MachineBasicBlock CFG.
  ///
  /// @param AU Analysis usage object to update with this pass's requirements.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Return the MachineFunctionProperties this pass requires before running.
  /// @return Required properties; empty by default.
  virtual MachineFunctionProperties getRequiredProperties() const {
    return MachineFunctionProperties();
  }
  /// Return the MachineFunctionProperties this pass sets after running.
  /// @return Properties set by this pass; empty by default.
  virtual MachineFunctionProperties getSetProperties() const {
    return MachineFunctionProperties();
  }
  /// Return the MachineFunctionProperties this pass clears when it runs.
  /// @return Properties cleared by this pass; empty by default.
  virtual MachineFunctionProperties getClearedProperties() const {
    return MachineFunctionProperties();
  }

private:
  MachineFunctionProperties RequiredProperties;
  MachineFunctionProperties SetProperties;
  MachineFunctionProperties ClearedProperties;

  /// createPrinterPass - Get a machine function printer pass.
  Pass *createPrinterPass(raw_ostream &O,
                          const std::string &Banner) const override;

  bool runOnFunction(Function &F) override;

  bool printIRUnit(raw_ostream &OS, Function &F) override;
};

} // End llvm namespace

#endif
