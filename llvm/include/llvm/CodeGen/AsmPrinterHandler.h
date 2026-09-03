//===-- llvm/CodeGen/AsmPrinterHandler.h -----------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a generic interface for AsmPrinter handlers,
// like debug and EH info emitters.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ASMPRINTERHANDLER_H
#define LLVM_CODEGEN_ASMPRINTERHANDLER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {

class AsmPrinter;
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MCSymbol;
class Module;

/// Callback that returns an exception symbol for a basic block.
///
/// \param Asm AsmPrinter emitting the current function.
/// \param MBB Basic block whose exception symbol is requested.
typedef MCSymbol *ExceptionSymbolProvider(AsmPrinter *Asm,
                                          const MachineBasicBlock *MBB);

/// Collects and handles AsmPrinter objects required to build debug
/// or EH information.
class LLVM_ABI AsmPrinterHandler {
public:
  /// Destroy this AsmPrinter handler.
  virtual ~AsmPrinterHandler();

  /// Process the beginning of a module.
  ///
  /// \param M Module about to be emitted.
  virtual void beginModule(Module *M) {}

  /// Emit all sections that should come after the content.
  virtual void endModule() = 0;

  /// Gather pre-function debug information.
  ///
  /// Every beginFunction(MF) call should be followed by an endFunction(MF)
  /// call.
  ///
  /// \param MF Function about to be emitted.
  virtual void beginFunction(const MachineFunction *MF) = 0;

  /// Emit any function end marker (like .cfi_endproc).
  ///
  /// This is called before endFunction and cannot switch sections.
  virtual void markFunctionEnd();

  /// Gather post-function debug information.
  ///
  /// \param MF Function that has just been emitted.
  virtual void endFunction(const MachineFunction *MF) = 0;

  /// Process the beginning of a new basic-block-section within a function.
  ///
  /// Always called immediately after beginFunction for the first basic-block.
  /// When basic-block-sections are enabled, called before the first block of
  /// each such section.
  ///
  /// \param MBB First basic block of the section being started.
  virtual void beginBasicBlockSection(const MachineBasicBlock &MBB) {}

  /// Process the end of a basic-block-section within a function.
  ///
  /// When basic-block-sections are enabled, called after the last block in each
  /// such section (including the last section in the function). When
  /// basic-block-sections are disabled, called at the end of a function,
  /// immediately prior to markFunctionEnd.
  ///
  /// \param MBB Last basic block of the section being ended.
  virtual void endBasicBlockSection(const MachineBasicBlock &MBB) {}

  /// For symbols that have a size designated (e.g. common symbols),
  /// this tracks that size.
  ///
  /// \param Sym Symbol whose size is being recorded.
  /// \param Size Size in bytes of the symbol.
  virtual void setSymbolSize(const MCSymbol *Sym, uint64_t Size) {}

  /// Process beginning of an instruction.
  ///
  /// \param MI Instruction about to be emitted.
  virtual void beginInstruction(const MachineInstr *MI) {}

  /// Process end of an instruction.
  virtual void endInstruction() {}

  /// Process the beginning of code alignment for a basic block.
  ///
  /// \param MBB Basic block whose alignment is about to be emitted.
  virtual void beginCodeAlignment(const MachineBasicBlock &MBB) {}

  /// Emit target-specific EH funclet machinery.
  ///
  /// \param MBB Basic block that begins the funclet.
  /// \param Sym Optional symbol marking the start of the funclet.
  virtual void beginFunclet(const MachineBasicBlock &MBB,
                            MCSymbol *Sym = nullptr) {}
  /// Process the end of an EH funclet.
  virtual void endFunclet() {}
};

} // End of namespace llvm

#endif
