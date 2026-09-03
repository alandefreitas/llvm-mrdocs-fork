//===-- llvm/CodeGen/DebugHandlerBase.h -----------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Common functionality for different debug information format backends.
// LLVM currently supports DWARF and CodeView.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DEBUGHANDLERBASE_H
#define LLVM_CODEGEN_DEBUGHANDLERBASE_H

#include "llvm/CodeGen/AsmPrinterHandler.h"
#include "llvm/CodeGen/DbgEntityHistoryCalculator.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include <optional>

namespace llvm {

class AsmPrinter;
class MachineInstr;
class MachineModuleInfo;

/// Represents the location at which a variable is stored.
struct DbgVariableLocation {
  /// Base register.
  MCRegister Register;

  /// Chain of offsetted loads necessary to load the value if it lives in
  /// memory. Every load except for the last is pointer-sized.
  SmallVector<int64_t, 1> LoadChain;

  /// Present if the location is part of a larger variable.
  std::optional<llvm::DIExpression::FragmentInfo> FragmentInfo;

  /// Extract a variable location from a machine instruction.
  ///
  /// This will only work if Instruction is a debug value instruction
  /// and the associated DIExpression is in one of the supported forms.
  /// If these requirements are not met, the returned Optional will not
  /// have a value.
  ///
  /// \param Instruction Machine instruction to extract a location from.
  /// \return Extracted location, or an empty optional if extraction fails.
  LLVM_ABI static std::optional<DbgVariableLocation>
  extractFromMachineInstruction(const MachineInstr &Instruction);
};

/// Base class for debug information backends. Common functionality related to
/// tracking which variables and scopes are alive at a given PC live here.
class LLVM_ABI DebugHandlerBase : public AsmPrinterHandler {
protected:
  /// Construct a debug handler bound to the given AsmPrinter.
  ///
  /// \param A AsmPrinter that will emit the debug information.
  DebugHandlerBase(AsmPrinter *A);

  /// Target of debug info emission.
  AsmPrinter *Asm = nullptr;

  /// Collected machine module information.
  MachineModuleInfo *MMI = nullptr;

  /// Previous instruction's source location.
  ///
  /// Used to determine label location to indicate scope boundaries in debug
  /// info. Tracks the previous instruction's source location when it is not
  /// line 0.
  DebugLoc PrevInstLoc;

  /// Label associated with the previous instruction, if any.
  MCSymbol *PrevLabel = nullptr;

  /// Basic block that contained the previous instruction.
  const MachineBasicBlock *PrevInstBB = nullptr;

  /// This location indicates end of function prologue and beginning of
  /// function body.
  const MachineInstr *PrologEndLoc;

  /// This block includes epilogue instructions.
  const MachineBasicBlock *EpilogBeginBlock = nullptr;

  /// If nonnull, stores the current machine instruction we're processing.
  const MachineInstr *CurMI = nullptr;

  /// Lexical scopes for the current function.
  LexicalScopes LScopes;

  /// History of DBG_VALUE and clobber instructions for each user
  /// variable.  Variables are listed in order of appearance.
  DbgValueHistoryMap DbgValues;

  /// Mapping of inlined labels and DBG_LABEL machine instruction.
  DbgLabelInstrMap DbgLabels;

  /// Maps instruction with label emitted before instruction.
  /// FIXME: Make this private from DwarfDebug, we have the necessary accessors
  /// for it.
  DenseMap<const MachineInstr *, MCSymbol *> LabelsBeforeInsn;

  /// Maps instruction with label emitted after instruction.
  DenseMap<const MachineInstr *, MCSymbol *> LabelsAfterInsn;

  /// Indentify instructions that are marking the beginning of or
  /// ending of a scope.
  void identifyScopeMarkers();

  /// Ensure that a label will be emitted before MI.
  ///
  /// \param MI Instruction that needs a preceding label.
  void requestLabelBeforeInsn(const MachineInstr *MI) {
    LabelsBeforeInsn.try_emplace(MI);
  }

  /// Ensure that a label will be emitted after MI.
  ///
  /// \param MI Instruction that needs a following label.
  void requestLabelAfterInsn(const MachineInstr *MI) {
    LabelsAfterInsn.try_emplace(MI);
  }

  /// Gather pre-function debug information for a concrete backend.
  ///
  /// \param MF Function about to be emitted.
  virtual void beginFunctionImpl(const MachineFunction *MF) = 0;

  /// Gather post-function debug information for a concrete backend.
  ///
  /// \param MF Function that has just been emitted.
  virtual void endFunctionImpl(const MachineFunction *MF) = 0;

  /// Called when a function has no debug information to emit.
  virtual void skippedNonDebugFunction() {}

private:
  InstructionOrdering InstOrdering;

  // AsmPrinterHandler overrides.
public:
  /// Destroy this debug handler.
  ~DebugHandlerBase() override;

  /// Process the beginning of a module.
  ///
  /// \param M Module about to be emitted.
  void beginModule(Module *M) override;

  /// Process beginning of an instruction.
  ///
  /// \param MI Instruction about to be emitted.
  void beginInstruction(const MachineInstr *MI) override;

  /// Process end of an instruction.
  void endInstruction() override;

  /// Gather pre-function debug information.
  ///
  /// \param MF Function about to be emitted.
  void beginFunction(const MachineFunction *MF) override;

  /// Gather post-function debug information.
  ///
  /// \param MF Function that has just been emitted.
  void endFunction(const MachineFunction *MF) override;

  /// Process the beginning of a new basic-block-section within a function.
  ///
  /// \param MBB First basic block of the section being started.
  void beginBasicBlockSection(const MachineBasicBlock &MBB) override;

  /// Process the end of a basic-block-section within a function.
  ///
  /// \param MBB Last basic block of the section being ended.
  void endBasicBlockSection(const MachineBasicBlock &MBB) override;

  /// Return Label preceding the instruction.
  ///
  /// \param MI Instruction whose preceding label is requested.
  /// \return Label emitted before \p MI, or nullptr if none.
  MCSymbol *getLabelBeforeInsn(const MachineInstr *MI);

  /// Return Label immediately following the instruction.
  ///
  /// \param MI Instruction whose following label is requested.
  /// \return Label emitted immediately after \p MI, or nullptr if none.
  MCSymbol *getLabelAfterInsn(const MachineInstr *MI);

  /// If this type is derived from a base type then return base type size.
  ///
  /// \param Ty Debug info type whose base type size is requested.
  /// \return Size of the base type in bits.
  static uint64_t getBaseTypeSize(const DIType *Ty);

  /// Return true if type encoding is unsigned.
  ///
  /// \param Ty Debug info type whose encoding is checked.
  /// \return True if the type encoding is unsigned.
  static bool isUnsignedDIType(const DIType *Ty);

  /// Return the instruction ordering for the current function.
  ///
  /// \return Instruction ordering for the current function.
  const InstructionOrdering &getInstOrdering() const { return InstOrdering; }

  /// Return the lexical scopes for the current function.
  ///
  /// \return Lexical scopes for the current function.
  const LexicalScopes &getLexicalScopes() const { return LScopes; }
};

} // namespace llvm

#endif
