//===- MIRPrinter.h - MIR serialization format printer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the functions that print out the LLVM IR and the machine
// functions using the MIR serialization format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRPRINTER_H
#define LLVM_CODEGEN_MIRPRINTER_H

#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MachineModuleInfo;
class Module;
class VirtRegMap;
template <typename T> class SmallVectorImpl;

/// Pass that prints LLVM IR of a module in the MIR serialization format.
class PrintMIRPreparePass : public RequiredPassInfoMixin<PrintMIRPreparePass> {
  raw_ostream &OS;

public:
  /// Construct a pass that prints MIR-format LLVM IR to \p OS.
  ///
  /// \param OS Output stream that receives the printed IR (defaults to errs()).
  PrintMIRPreparePass(raw_ostream &OS = errs()) : OS(OS) {}
  /// Print the module's LLVM IR in the MIR serialization format.
  ///
  /// \param M Module whose IR is printed.
  /// \param MFAM Module analysis manager (unused; required by the pass API).
  /// \returns Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MFAM);
};

/// Pass that prints a machine function in the MIR serialization format.
class PrintMIRPass : public RequiredPassInfoMixin<PrintMIRPass> {
  raw_ostream &OS;

public:
  /// Construct a pass that prints MIR for a machine function to \p OS.
  ///
  /// \param OS Output stream that receives the printed MIR (defaults to
  ///          errs()).
  PrintMIRPass(raw_ostream &OS = errs()) : OS(OS) {}
  /// Print the machine function in the MIR serialization format.
  ///
  /// \param MF Machine function to print.
  /// \param MFAM Machine function analysis manager providing supporting
  ///             analyses.
  /// \returns Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// Print LLVM IR using the MIR serialization format to the given output stream.
///
/// \param OS Output stream that receives the printed IR.
/// \param M Module whose LLVM IR is printed.
LLVM_ABI void printMIR(raw_ostream &OS, const Module &M);

/// Print MIR using Legacy Pass Manager (uses MachineModuleInfo).
/// If \p VRM is non-null, the printer will serialize VirtRegMap state
/// (split-from, assigned-phys).
///
/// \param OS Output stream that receives the printed MIR.
/// \param MMI Machine module info associated with \p MF.
/// \param MF Machine function to print.
/// \param VRM Optional virtual register map whose state is serialized when
///            non-null.
LLVM_ABI void printMIR(raw_ostream &OS, const MachineModuleInfo &MMI,
                       const MachineFunction &MF,
                       const VirtRegMap *VRM = nullptr);

/// Print MIR using New Pass Manager (uses FunctionAnalysisManager).
/// If \p VRM is non-null, the printer will serialize VirtRegMap state
/// (split-from, assigned-phys).
///
/// \param OS Output stream that receives the printed MIR.
/// \param FAM Function analysis manager associated with \p MF.
/// \param MF Machine function to print.
/// \param VRM Optional virtual register map whose state is serialized when
///            non-null.
LLVM_ABI void printMIR(raw_ostream &OS, FunctionAnalysisManager &FAM,
                       const MachineFunction &MF,
                       const VirtRegMap *VRM = nullptr);

/// Guess likely successors of a machine basic block from its operands.
///
/// Determine a possible list of successors of a basic block based on the
/// basic block machine operand being used inside the block. This should give
/// you the correct list of successor blocks in most cases except for things
/// like jump tables where the basic block references can't easily be found.
/// The MIRPRinter will skip printing successors if they match the result of
/// this function and the parser will use this function to construct a list if
/// it is missing.
///
/// \param MBB Machine basic block whose successor operands are inspected.
/// \param Result Filled with the guessed successor blocks.
/// \param IsFallthrough Set to true if \p MBB appears to fall through to the
///                      next block.
LLVM_ABI void guessSuccessors(const MachineBasicBlock &MBB,
                              SmallVectorImpl<MachineBasicBlock *> &Result,
                              bool &IsFallthrough);

} // end namespace llvm

#endif
