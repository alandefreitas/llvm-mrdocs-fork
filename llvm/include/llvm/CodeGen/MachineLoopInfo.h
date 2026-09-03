//===- llvm/CodeGen/MachineLoopInfo.h - Natural Loop Calculator -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachineLoopInfo class that is used to identify natural
// loops and determine the loop depth of various nodes of the CFG.  Note that
// natural loops may actually be several loops that share the same header node.
//
// This analysis calculates the nesting structure of loops in a function.  For
// each natural loop identified, this analysis identifies natural loops
// contained entirely within the loop and the basic blocks the make up the loop.
//
// It can calculate on the fly various bits of information, for example:
//
//  * whether there is a preheader for the loop
//  * the number of back edges to the header
//  * whether or not a particular block branches out of the loop
//  * the successor blocks of the loop
//  * the loop depth
//  * the trip count
//  * etc...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINELOOPINFO_H
#define LLVM_CODEGEN_MACHINELOOPINFO_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/GenericLoopInfo.h"

namespace llvm {

class MachineDominatorTree;
// Implementation in LoopInfoImpl.h
class MachineLoop;
/// Explicit instantiation of LoopBase for MachineBasicBlock and MachineLoop.
extern template class LLVM_TEMPLATE_ABI
    LoopBase<MachineBasicBlock, MachineLoop>;

/// Represents a single natural loop in a machine-function CFG.
class MachineLoop : public LoopBase<MachineBasicBlock, MachineLoop> {
public:
  /// Return the first contiguous block of this loop in layout order.
  ///
  /// This is the first block in the linear layout, ignoring any parts of the
  /// loop not contiguous with the part that contains the header.
  /// @return The first contiguous block of this loop in layout order.
  LLVM_ABI MachineBasicBlock *getTopBlock() const;

  /// Return the last contiguous block of this loop in layout order.
  ///
  /// This is the last block in the linear layout, ignoring any parts of the
  /// loop not contiguous with the part that contains the header.
  /// @return The last contiguous block of this loop in layout order.
  LLVM_ABI MachineBasicBlock *getBottomBlock() const;

  /// Find the block that contains the loop control variable and test.
  ///
  /// This will return the latch block if it's one of the exiting blocks.
  /// Otherwise, return the exiting block. Return 'null' when multiple exiting
  /// blocks are present.
  /// @return The latch or exiting block with the control test, or null.
  LLVM_ABI MachineBasicBlock *findLoopControlBlock() const;

  /// Return the debug location of the start of this loop.
  ///
  /// This looks for a BB terminating instruction with a known debug location by
  /// looking at the preheader and header blocks. If it cannot find a
  /// terminating instruction with location information, it returns an unknown
  /// location.
  /// @return Debug location of the start of this loop, or an unknown location.
  LLVM_ABI DebugLoc getStartLoc() const;

  /// Find the llvm.loop metadata for this loop.
  ///
  /// If each branch to the header of this loop contains the same llvm.loop
  /// metadata, then this metadata node is returned. Otherwise, if any latch
  /// instruction does not contain the llvm.loop metadata or multiple latch
  /// instructions contain different llvm.loop metadata nodes, then null is
  /// returned.
  /// @return The loop-ID metadata node, or nullptr if absent or inconsistent.
  LLVM_ABI MDNode *getLoopID() const;

  /// Return true if the instruction is loop invariant.
  ///
  /// I.e., all virtual register operands are defined outside of the loop,
  /// physical registers aren't accessed explicitly, and there are no side
  /// effects that aren't captured by the operands or other flags.
  /// ExcludeReg can be used to exclude the given register from the check
  /// i.e. when we're considering hoisting it's definition but not hoisted it
  /// yet
  /// @param I Instruction to test for loop invariance.
  /// @param ExcludeReg Optional register ignored by the invariance check.
  /// @return True if \p I is loop-invariant.
  LLVM_ABI bool isLoopInvariant(MachineInstr &I,
                                const Register ExcludeReg = 0) const;

  /// Dump this loop to stderr for debugging.
  LLVM_ABI void dump() const;

private:
  friend class LoopInfoBase<MachineBasicBlock, MachineLoop>;

  /// Returns true if the given physreg has no defs inside the loop.
  bool isLoopInvariantImplicitPhysReg(Register Reg) const;

  MachineLoop() = default;
};

// Implementation in LoopInfoImpl.h
extern template class LLVM_TEMPLATE_ABI
    LoopInfoBase<MachineBasicBlock, MachineLoop>;

/// Analysis that identifies natural loops in a machine function's CFG.
class MachineLoopInfo : public LoopInfoBase<MachineBasicBlock, MachineLoop> {
  friend class LoopBase<MachineBasicBlock, MachineLoop>;
  friend class MachineLoopInfoWrapperPass;

public:
  /// Construct an empty MachineLoopInfo.
  MachineLoopInfo() = default;
  /// Construct MachineLoopInfo by analyzing \p MDT.
  /// @param MDT Dominator tree used to discover natural loops.
  explicit MachineLoopInfo(MachineDominatorTree &MDT) { calculate(MDT); }
  /// Move-construct MachineLoopInfo from another instance.
  /// @param Arg MachineLoopInfo to move from.
  MachineLoopInfo(MachineLoopInfo &&Arg) = default;
  /// Deleted copy constructor; MachineLoopInfo is not copyable.
  /// @param Unused Ignored; copy construction is not supported.
  MachineLoopInfo(const MachineLoopInfo &Unused) = delete;
  /// Deleted copy assignment; MachineLoopInfo is not copyable.
  /// @param Unused Ignored; copy assignment is not supported.
  MachineLoopInfo &operator=(const MachineLoopInfo &Unused) = delete;

  /// Handle invalidation explicitly.
  /// @param MF Machine function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);

  /// Find a loop preheader, or a speculative preheader candidate.
  ///
  /// Find the block that either is the loop preheader, or could speculatively
  /// be used as the preheader. This is e.g. useful to place loop setup code.
  /// Code that cannot be speculated should not be placed here.
  /// SpeculativePreheader is controlling whether it also tries to find the
  /// speculative preheader if the regular preheader is not present. With
  /// FindMultiLoopPreheader = false, nullptr will be returned if the found
  /// preheader is the preheader of multiple loops.
  /// @param L Loop whose preheader is sought.
  /// @param SpeculativePreheader Whether to accept a speculative preheader.
  /// @param FindMultiLoopPreheader Whether a shared multi-loop preheader is OK.
  /// @return The preheader or speculative preheader, or nullptr if none.
  LLVM_ABI MachineBasicBlock *
  findLoopPreheader(MachineLoop *L, bool SpeculativePreheader = false,
                    bool FindMultiLoopPreheader = false) const;

  /// Calculate the natural loop information.
  /// @param MDT Dominator tree used to discover natural loops.
  LLVM_ABI void calculate(MachineDominatorTree &MDT);

  /// Rebuild the loop forest.
  ///
  /// \p GetDomTree is called only for an irreducible CFG.
  /// @param MF Machine function whose loops are recalculated.
  /// @param GetDomTree Callback that returns a dominator tree when needed.
  LLVM_ABI void
  calculate(MachineFunction &MF,
            function_ref<const DomTreeBase<MachineBasicBlock> &()> GetDomTree);
};

/// Analysis pass that exposes the \c MachineLoopInfo for a machine function.
class MachineLoopAnalysis : public AnalysisInfoMixin<MachineLoopAnalysis> {
  friend AnalysisInfoMixin<MachineLoopAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = MachineLoopInfo;
  /// Run the analysis over \p MF and produce MachineLoopInfo.
  /// @param MF Machine function to analyze.
  /// @param MFAM Machine function analysis manager providing dominators.
  /// @return The computed MachineLoopInfo for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c LoopAnalysis results.
class MachineLoopPrinterPass
    : public RequiredPassInfoMixin<MachineLoopPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed loop info.
  explicit MachineLoopPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print the MachineLoopInfo for \p MF and return all analyses preserved.
  /// @param MF Machine function whose MachineLoopInfo is printed.
  /// @param MFAM Machine function analysis manager providing MachineLoopAnalysis.
  /// @return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// The legacy pass manager's analysis pass to compute machine loop information.
class LLVM_ABI MachineLoopInfoWrapperPass : public MachineFunctionPass {
  MachineLoopInfo LI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy MachineLoopInfo wrapper pass.
  MachineLoopInfoWrapperPass();

  /// Calculate the natural loop information for a given machine function.
  /// @param F Machine function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnMachineFunction(MachineFunction &F) override;

  /// Release the MachineLoopInfo owned by this pass.
  void releaseMemory() override { LI.releaseMemory(); }

  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate with required and preserved analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Return the MachineLoopInfo computed by this pass.
  /// @return The MachineLoopInfo owned by this pass.
  MachineLoopInfo &getLI() { return LI; }
};

// Allow clients to walk the list of nested loops...
/// GraphTraits specialization for const MachineLoop pointers.
template <> struct GraphTraits<const MachineLoop*> {
  /// Graph node type for a const MachineLoop.
  using NodeRef = const MachineLoop *;
  /// Iterator over child (nested) loops.
  using ChildIteratorType = MachineLoopInfo::iterator;

  /// Return \p L as the graph entry node.
  /// @param L Loop used as the entry node.
  /// @return \p L as the entry node.
  static NodeRef getEntryNode(const MachineLoop *L) { return L; }
  /// Return the begin iterator over nested loops of \p N.
  /// @param N Parent loop.
  /// @return Begin iterator over the nested loops of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  /// Return the end iterator over nested loops of \p N.
  /// @param N Parent loop.
  /// @return End iterator over the nested loops of \p N.
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

/// GraphTraits specialization for mutable MachineLoop pointers.
template <> struct GraphTraits<MachineLoop*> {
  /// Graph node type for a MachineLoop.
  using NodeRef = MachineLoop *;
  /// Iterator over child (nested) loops.
  using ChildIteratorType = MachineLoopInfo::iterator;

  /// Return \p L as the graph entry node.
  /// @param L Loop used as the entry node.
  /// @return \p L as the entry node.
  static NodeRef getEntryNode(MachineLoop *L) { return L; }
  /// Return the begin iterator over nested loops of \p N.
  /// @param N Parent loop.
  /// @return Begin iterator over the nested loops of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  /// Return the end iterator over nested loops of \p N.
  /// @param N Parent loop.
  /// @return End iterator over the nested loops of \p N.
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINELOOPINFO_H
