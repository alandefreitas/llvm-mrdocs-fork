//===- llvm/CodeGen/WinEHFuncInfo.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data structures and associated state for Windows exception handling schemes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_WINEHFUNCINFO_H
#define LLVM_CODEGEN_WINEHFUNCINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <limits>
#include <utility>

namespace llvm {

class AllocaInst;
class BasicBlock;
class FuncletPadInst;
class Function;
class GlobalVariable;
class Instruction;
class InvokeInst;
class MachineBasicBlock;
class MCSymbol;

// The following structs respresent the .xdata tables for various
// Windows-related EH personalities.

/// Pointer to either an IR basic block or a machine basic block.
using MBBOrBasicBlock = PointerUnion<const BasicBlock *, MachineBasicBlock *>;

/// One entry in the C++ EH unwind map for __CxxFrameHandler3.
struct CxxUnwindMapEntry {
  /// State to transition to if unwinding continues past this entry.
  int ToState;
  /// Cleanup funclet or block invoked when leaving this state.
  MBBOrBasicBlock Cleanup;
};

/// Similar to CxxUnwindMapEntry, but supports SEH filters.
struct SEHUnwindMapEntry {
  /// If unwinding continues through this handler, transition to the handler at
  /// this state. This indexes into SEHUnwindMap.
  int ToState = -1;

  /// True when this entry represents a __finally handler rather than __except.
  bool IsFinally = false;

  /// Holds the filter expression function.
  const Function *Filter = nullptr;

  /// Holds the __except or __finally basic block.
  MBBOrBasicBlock Handler;
};

/// Catch-handler descriptor stored in a try-block map entry.
struct WinEHHandlerType {
  /// Catch-handler adjectives from the catchpad (e.g. const/volatile/byref).
  int Adjectives;
  /// The CatchObj starts out life as an LLVM alloca and is eventually turned
  /// frame index.
  union {
    /// Alloca used for the catch object before frame indices are assigned.
    const AllocaInst *Alloca;
    /// Frame index of the catch object after stack layout.
    int FrameIndex;
  } CatchObj = {};
  /// RTTI type descriptor for the exception type this handler catches.
  GlobalVariable *TypeDescriptor;
  /// Catch handler block or funclet for this handler.
  MBBOrBasicBlock Handler;
};

/// Try-region descriptor with the catch handlers that cover it.
struct WinEHTryBlockMapEntry {
  /// Lowest state number belonging to this try region.
  int TryLow = -1;
  /// Highest state number belonging to this try region.
  int TryHigh = -1;
  /// Highest state number among catch handlers for this try region.
  int CatchHigh = -1;
  /// Catch handlers associated with this try region.
  SmallVector<WinEHHandlerType, 1> HandlerArray;
};

/// Kind of CoreCLR exception handler represented in ClrEHUnwindMap.
enum class ClrHandlerType {
  Catch,   ///< Catch handler for a typed exception.
  Finally, ///< Finally (cleanup) handler.
  Fault,   ///< Fault handler.
  Filter   ///< Filter-based handler.
};

/// One entry in the CoreCLR EH unwind map.
struct ClrEHUnwindMapEntry {
  /// Handler block or funclet for this unwind-map entry.
  MBBOrBasicBlock Handler;
  /// Type token identifying the exception type for catch handlers.
  uint32_t TypeToken;
  int HandlerParentState; ///< Outer handler enclosing this entry's handler
  int TryParentState; ///< Outer try region enclosing this entry's try region,
                      ///< treating later catches on same try as "outer"
  /// Classification of the handler (catch, finally, fault, or filter).
  ClrHandlerType HandlerType;
};

/// Per-function Windows EH tables, state maps, and related frame indices.
struct WinEHFuncInfo {
  /// Map from EH pad instructions to their assigned state numbers.
  DenseMap<const Instruction *, int> EHPadStateMap;
  /// Map from funclet pads to the base state of that funclet.
  DenseMap<const FuncletPadInst *, int> FuncletBaseStateMap;
  /// Map from invoke instructions to the state active at the invoke site.
  DenseMap<const InvokeInst *, int> InvokeStateMap;
  /// Map from IP labels to (state, end-label) pairs for the IP-to-state table.
  DenseMap<MCSymbol *, std::pair<int, MCSymbol *>> LabelToStateMap;
  /// Map from basic blocks to EH state numbers (asynchronous EH / -EHa).
  DenseMap<const BasicBlock *, int> BlockToStateMap; // for AsynchEH
  /// C++ unwind map entries used by __CxxFrameHandler3.
  SmallVector<CxxUnwindMapEntry, 4> CxxUnwindMap;
  /// Try-block map entries describing try regions and their catches.
  SmallVector<WinEHTryBlockMapEntry, 4> TryBlockMap;
  /// SEH unwind map entries for __C_specific_handler / table-based SEH.
  SmallVector<SEHUnwindMapEntry, 4> SEHUnwindMap;
  /// CoreCLR unwind map entries for the CLR personality.
  SmallVector<ClrEHUnwindMapEntry, 4> ClrEHUnwindMap;
  /// Frame index of the UnwindHelp slot used by C++ EH.
  int UnwindHelpFrameIdx = std::numeric_limits<int>::max();
  /// Frame index of the Previous Stack Pointer (PSP) symbol.
  int PSPSymFrameIdx = std::numeric_limits<int>::max();

  /// Return the most recently assigned C++ EH state number.
  /// @return Last state index in \c CxxUnwindMap, or -1 when empty.
  int getLastStateNumber() const { return CxxUnwindMap.size() - 1; }

  /// Record an IP-to-state range for \p II using its mapped invoke state.
  /// @param II Invoke whose precomputed state is stored in InvokeStateMap.
  /// @param InvokeBegin Label marking the start of the invoke site.
  /// @param InvokeEnd Label marking the end of the invoke site.
  LLVM_ABI void addIPToStateRange(const InvokeInst *II, MCSymbol *InvokeBegin,
                                  MCSymbol *InvokeEnd);

  /// Record an IP-to-state range for the explicit EH \p State.
  /// @param State EH state number associated with this IP range.
  /// @param InvokeBegin Label marking the start of the range.
  /// @param InvokeEnd Label marking the end of the range.
  LLVM_ABI void addIPToStateRange(int State, MCSymbol *InvokeBegin,
                                  MCSymbol *InvokeEnd);

  /// Frame index of the EH registration node (x86 SEH / C++ EH).
  int EHRegNodeFrameIndex = std::numeric_limits<int>::max();
  /// Byte offset from SP to the end of the EH registration node.
  int EHRegNodeEndOffset = std::numeric_limits<int>::max();
  /// Frame index of the EH guard (cookie) slot.
  int EHGuardFrameIndex = std::numeric_limits<int>::max();
  /// Stack offset recorded by llvm.x86.seh.setframe for SEH frame setup.
  int SEHSetFrameOffset = std::numeric_limits<int>::max();

  /// Construct an empty WinEHFuncInfo with default frame-index sentinels.
  LLVM_ABI WinEHFuncInfo();
};

/// Build WinEHFuncInfo state numbers and tables for __CxxFrameHandler3.
///
/// Analyze the IR in ParentFn and its handlers to build WinEHFuncInfo, which
/// describes the state numbers and tables used by __CxxFrameHandler3. This
/// analysis assumes that WinEHPrepare has already been run.
/// @param ParentFn Parent function whose EH IR is analyzed.
/// @param FuncInfo Structure populated with C++ EH state information.
LLVM_ABI void calculateWinCXXEHStateNumbers(const Function *ParentFn,
                                            WinEHFuncInfo &FuncInfo);

/// Build WinEHFuncInfo SEH state numbers and unwind-map tables.
/// @param ParentFn Parent function whose SEH IR is analyzed.
/// @param FuncInfo Structure populated with SEH state information.
LLVM_ABI void calculateSEHStateNumbers(const Function *ParentFn,
                                       WinEHFuncInfo &FuncInfo);

/// Build WinEHFuncInfo CoreCLR state numbers and unwind-map tables.
/// @param Fn Function whose CLR EH IR is analyzed.
/// @param FuncInfo Structure populated with CLR EH state information.
LLVM_ABI void calculateClrEHStateNumbers(const Function *Fn,
                                         WinEHFuncInfo &FuncInfo);

/// Propagate C++ EH state numbers through \p BB for asynchronous EH (-EHa).
/// @param BB Basic block at which to begin state propagation.
/// @param State Incoming EH state on normal paths into \p BB.
/// @param FuncInfo Function EH info updated with per-block states.
LLVM_ABI void calculateCXXStateForAsynchEH(const BasicBlock *BB, int State,
                                           WinEHFuncInfo &FuncInfo);
/// Propagate SEH state numbers through \p BB for asynchronous EH (-EHa).
/// @param BB Basic block at which to begin state propagation.
/// @param State Incoming EH state on normal paths into \p BB.
/// @param FuncInfo Function EH info updated with per-block states.
LLVM_ABI void calculateSEHStateForAsynchEH(const BasicBlock *BB, int State,
                                           WinEHFuncInfo &FuncInfo);

} // end namespace llvm

#endif // LLVM_CODEGEN_WINEHFUNCINFO_H
