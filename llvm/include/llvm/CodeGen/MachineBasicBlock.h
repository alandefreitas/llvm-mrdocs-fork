//===- llvm/CodeGen/MachineBasicBlock.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Collect the sequence of machine instructions for a basic block.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEBASICBLOCK_H
#define LLVM_CODEGEN_MACHINEBASICBLOCK_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineFunctionAnalysisManager.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/UniqueBBID.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace llvm {

class BasicBlock;
class MachineDomTreeUpdater;
class MachineFunction;
class MachineLoopInfo;
class MCSymbol;
class ModuleSlotTracker;
class Pass;
class Printable;
class SlotIndexes;
class StringRef;
class raw_ostream;
class LiveIntervals;
class LiveVariables;
/// Analysis that provides relative branch probabilities for CFG edges between
/// machine basic blocks.
class MachineBranchProbabilityInfo;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class TargetRegisterInfo;

/// Uniquely identifies a basic block section within a function.
///
/// Regular sections use \c {Type: Default, Number: N}. Special sections use
/// \c ExceptionSectionID (\c {Type: Exception, Number: 0}) and
/// \c ColdSectionID (\c {Type: Cold, Number: 0}).
struct MBBSectionID {
  /// Kind of basic-block section this ID refers to.
  enum SectionType {
    /// Regular section (distinguished by the Number field).
    Default = 0,
    /// Special section for exception-handling blocks.
    Exception,
    /// Special section for cold blocks.
    Cold,
  };
  /// Section kind for this ID.
  SectionType Type;
  /// Discriminator among regular (Default) sections.
  unsigned Number;

  /// Construct a regular section ID with number \p N.
  /// @param N Section number within the function.
  MBBSectionID(unsigned N) : Type(Default), Number(N) {}

  // Special unique sections for cold and exception blocks.
  /// Unique section ID for cold basic-block sections.
  LLVM_ABI const static MBBSectionID ColdSectionID;
  /// Unique section ID for exception basic-block sections.
  LLVM_ABI const static MBBSectionID ExceptionSectionID;

  /// Return true if this section ID equals \p Other.
  /// @param Other Section ID to compare against.
  /// @return True if this section ID equals \p Other.
  bool operator==(const MBBSectionID &Other) const {
    return Type == Other.Type && Number == Other.Number;
  }

  /// Return true if this section ID differs from \p Other.
  /// @param Other Section ID to compare against.
  /// @return True if this section ID differs from \p Other.
  bool operator!=(const MBBSectionID &Other) const { return !(*this == Other); }

private:
  // This is only used to construct the special cold and exception sections.
  MBBSectionID(SectionType T) : Type(T), Number(0) {}
};

/// DenseMapInfo specialization for MBBSectionID keys.
template <> struct DenseMapInfo<MBBSectionID> {
  /// DenseMapInfo for the section Type enum field.
  using TypeInfo = DenseMapInfo<MBBSectionID::SectionType>;
  /// DenseMapInfo for the section Number field.
  using NumberInfo = DenseMapInfo<unsigned>;

  /// Compute a hash value for section ID \p SecID.
  /// @param SecID Section ID to hash.
  /// @return A hash value for the section ID.
  static unsigned getHashValue(const MBBSectionID &SecID) {
    return detail::combineHashValue(TypeInfo::getHashValue(SecID.Type),
                                    NumberInfo::getHashValue(SecID.Number));
  }
  /// Return true if \p LHS and \p RHS identify the same section.
  /// @param LHS Left-hand section ID.
  /// @param RHS Right-hand section ID.
  /// @return True if \p LHS and \p RHS identify the same section.
  static bool isEqual(const MBBSectionID &LHS, const MBBSectionID &RHS) {
    return LHS == RHS;
  }
};

/// Intrusive list traits for MachineInstr nodes owned by a MachineBasicBlock.
template <> struct ilist_traits<MachineInstr> {
private:
  friend class MachineBasicBlock; // Set by the owning MachineBasicBlock.

  MachineBasicBlock *Parent;

  using instr_iterator =
      simple_ilist<MachineInstr, ilist_sentinel_tracking<true>>::iterator;

public:
  /// Notify traits that instruction \p N was added to the list.
  /// @param N Instruction that was inserted.
  LLVM_ABI void addNodeToList(MachineInstr *N);
  /// Notify traits that instruction \p N was removed from the list.
  /// @param N Instruction that was removed.
  LLVM_ABI void removeNodeFromList(MachineInstr *N);
  /// Transfer instructions [\p First, \p Last) from \p FromList into this list.
  /// @param FromList Source list traits transferring nodes.
  /// @param First Begin of the transferred instruction range.
  /// @param Last End of the transferred instruction range.
  LLVM_ABI void transferNodesFromList(ilist_traits &FromList,
                                      instr_iterator First,
                                      instr_iterator Last);
  /// Delete instruction node \p MI.
  /// @param MI Instruction to destroy.
  LLVM_ABI void deleteNode(MachineInstr *MI);
};

/// A sequence of machine instructions forming one basic block.
class MachineBasicBlock
    : public ilist_node_with_parent<MachineBasicBlock, MachineFunction> {
public:
  /// Pair of physical register and lane mask.
  /// This is not simply a std::pair typedef because the members should be named
  /// clearly as they both have an integer type.
  struct RegisterMaskPair {
  public:
    /// Physical register in this live-in / live-out pair.
    MCRegister PhysReg;
    /// Lane mask describing which subregisters of \c PhysReg are live.
    LaneBitmask LaneMask;

    /// Construct a register/lane pair for physical register \p PhysReg.
    /// @param PhysReg Physical register.
    /// @param LaneMask Lanes of \p PhysReg that are live.
    RegisterMaskPair(MCRegister PhysReg, LaneBitmask LaneMask)
        : PhysReg(PhysReg), LaneMask(LaneMask) {
      assert(PhysReg.isPhysical());
    }

    /// Return true if this pair equals \p other.
    /// @param other Other register/lane pair to compare against.
    /// @return True if this pair equals \p other.
    bool operator==(const RegisterMaskPair &other) const {
      return PhysReg == other.PhysReg && LaneMask == other.LaneMask;
    }
  };

private:
  using Instructions = ilist<MachineInstr, ilist_sentinel_tracking<true>>;

  const BasicBlock *BB;
  int Number;

  /// The call frame size on entry to this basic block due to call frame setup
  /// instructions in a predecessor. This is usually zero, unless basic blocks
  /// are split in the middle of a call sequence.
  ///
  /// This information is only maintained until PrologEpilogInserter eliminates
  /// call frame pseudos.
  unsigned CallFrameSize = 0;

  MachineFunction *xParent;
  Instructions Insts;

  /// Keep track of the predecessor / successor basic blocks.
  SmallVector<MachineBasicBlock *, 4> Predecessors;
  SmallVector<MachineBasicBlock *, 2> Successors;

  /// Keep track of the probabilities to the successors. This vector has the
  /// same order as Successors, or it is empty if we don't use it (disable
  /// optimization).
  std::vector<BranchProbability> Probs;
  using probability_iterator = std::vector<BranchProbability>::iterator;
  using const_probability_iterator =
      std::vector<BranchProbability>::const_iterator;

  std::optional<uint64_t> IrrLoopHeaderWeight;

  /// Keep track of the physical registers that are livein of the basicblock.
  using LiveInVector = std::vector<RegisterMaskPair>;
  LiveInVector LiveIns;

  /// Alignment of the basic block. One if the basic block does not need to be
  /// aligned.
  Align Alignment;
  /// Maximum amount of bytes that can be added to align the basic block. If the
  /// alignment cannot be reached in this many bytes, no bytes are emitted.
  /// Zero to represent no maximum.
  unsigned MaxBytesForAlignment = 0;

  /// Indicate that this basic block is entered via an exception handler.
  bool IsEHPad = false;

  /// Indicate that this MachineBasicBlock is referenced somewhere other than
  /// as predecessor/successor, a terminator MachineInstr, or a jump table.
  bool MachineBlockAddressTaken = false;

  /// Relatively stable number used for analyses.
  unsigned AnalysisNumber = 0;

  /// If this MachineBasicBlock corresponds to an IR-level "blockaddress"
  /// constant, this contains a pointer to that block.
  BasicBlock *AddressTakenIRBlock = nullptr;

  /// Indicate that this basic block needs its symbol be emitted regardless of
  /// whether the flow just falls-through to it.
  bool LabelMustBeEmitted = false;

  /// Indicate that this basic block is the entry block of an EH scope, i.e.,
  /// the block that used to have a catchpad or cleanuppad instruction in the
  /// LLVM IR.
  bool IsEHScopeEntry = false;

  /// Indicates if this is a target of Windows EH Continuation Guard.
  bool IsEHContTarget = false;

  /// Indicate that this basic block is the entry block of an EH funclet.
  bool IsEHFuncletEntry = false;

  /// Indicate that this basic block is the entry block of a cleanup funclet.
  bool IsCleanupFuncletEntry = false;

  /// Fixed unique ID assigned to this basic block upon creation. Used with
  /// basic block sections and basic block labels.
  std::optional<UniqueBBID> BBID;

  SmallVector<unsigned> PrefetchTargets;

  /// With basic block sections, this stores the Section ID of the basic block.
  MBBSectionID SectionID{0};

  // Indicate that this basic block begins a section.
  bool IsBeginSection = false;

  // Indicate that this basic block ends a section.
  bool IsEndSection = false;

  /// Indicate that this basic block is the indirect dest of an INLINEASM_BR.
  bool IsInlineAsmBrIndirectTarget = false;

  /// since getSymbol is a relatively heavy-weight operation, the symbol
  /// is only computed once and is cached.
  mutable MCSymbol *CachedMCSymbol = nullptr;

  /// Cached MCSymbol for this block (used if IsEHContTarget).
  mutable MCSymbol *CachedEHContMCSymbol = nullptr;

  /// Marks the end of the basic block. Used during basic block sections to
  /// calculate the size of the basic block, or the BB section ending with it.
  mutable MCSymbol *CachedEndMCSymbol = nullptr;

  // Intrusive list support
  MachineBasicBlock() = default;

  explicit MachineBasicBlock(MachineFunction &MF, const BasicBlock *BB);

  ~MachineBasicBlock();

  // MachineBasicBlocks are allocated and owned by MachineFunction.
  friend class MachineFunction;

public:
  /// Return the original LLVM IR basic block, or null if there is none.
  ///
  /// Note that this may be NULL if this instance does not correspond directly
  /// to an LLVM basic block.
  /// @return The original LLVM IR basic block, or null if there is none.
  const BasicBlock *getBasicBlock() const { return BB; }

  /// Remove the reference to the underlying IR BasicBlock. This is for
  /// reduction tools and should generally not be used.
  void clearBasicBlock() {
    BB = nullptr;
  }

  /// Check if there is a name of corresponding LLVM basic block.
  /// @return True if the corresponding LLVM basic block has a name.
  LLVM_ABI bool hasName() const;

  /// Return the name of the corresponding LLVM basic block, or an empty string.
  /// @return The name of the corresponding LLVM basic block, or an empty string.
  LLVM_ABI StringRef getName() const;

  /// Return a formatted string to identify this block and its parent function.
  /// @return A formatted string to identify this block and its parent function.
  LLVM_ABI std::string getFullName() const;

  /// Return true if this block's address is taken outside ordinary CFG uses.
  ///
  /// Test whether this block is used as something other than the target of a
  /// terminator, exception-handling target, or jump table. This is either the
  /// result of an IR-level "blockaddress", or some form of target-specific
  /// branch lowering.
  ///
  /// The name of this function `hasAddressTaken` implies that the address of
  /// the block is known and used in a general sense, but not necessarily that
  /// the address is used by an indirect branch instruction. So branch target
  /// enforcement need not put a BTI instruction (or equivalent) at the start
  /// of a block just because this function returns true. The decision about
  /// whether to add a BTI can be more subtle than that, and depends on the
  /// more detailed checks that this function aggregates together.
  /// @return True if this block's address is taken outside ordinary CFG uses.
  bool hasAddressTaken() const {
    return MachineBlockAddressTaken || AddressTakenIRBlock ||
           IsInlineAsmBrIndirectTarget;
  }

  /// Return true if this block's machine address is taken for non-CFG uses.
  ///
  /// Test whether this block is used as something other than the target of a
  /// terminator, exception-handling target, jump table, or IR blockaddress. For
  /// example, its address might be loaded into a register, or stored in some
  /// branch table that isn't part of MachineJumpTableInfo.
  ///
  /// If this function returns true, it _does_ mean that branch target
  /// enforcement needs to put a BTI or equivalent at the start of the block.
  /// @return True if this block's machine address is taken for non-CFG uses.
  bool isMachineBlockAddressTaken() const { return MachineBlockAddressTaken; }

  /// Test whether this block is the target of an IR BlockAddress.  (There can
  /// more than one MBB associated with an IR BB where the address is taken.)
  ///
  /// If this function returns true, it _does_ mean that branch target
  /// enforcement needs to put a BTI or equivalent at the start of the block.
  /// @return True if this block is the target of an IR BlockAddress.
  bool isIRBlockAddressTaken() const { return AddressTakenIRBlock; }

  /// Retrieves the BasicBlock which corresponds to this MachineBasicBlock.
  /// @return The IR basic block whose address is taken, or null.
  BasicBlock *getAddressTakenIRBlock() const { return AddressTakenIRBlock; }

  /// Mark that this block's machine address is taken for non-CFG uses.
  ///
  /// Set this block to indicate that its address is used as something other
  /// than the target of a terminator, exception-handling target, jump table, or
  /// IR-level "blockaddress".
  void setMachineBlockAddressTaken() { MachineBlockAddressTaken = true; }

  /// Set this block to reflect that it corresponds to an IR-level basic block
  /// with a BlockAddress.
  /// @param BB IR basic block whose address is taken.
  void setAddressTakenIRBlock(BasicBlock *BB) { AddressTakenIRBlock = BB; }

  /// Test whether this block must have its label emitted.
  /// @return True if this block must have its label emitted.
  bool hasLabelMustBeEmitted() const { return LabelMustBeEmitted; }

  /// Set this block to reflect that, regardless how we flow to it, we need
  /// its label be emitted.
  void setLabelMustBeEmitted() { LabelMustBeEmitted = true; }

  /// Return the MachineFunction containing this basic block.
  /// @return The MachineFunction containing this basic block.
  const MachineFunction *getParent() const { return xParent; }
  /// Return the mutable MachineFunction containing this basic block.
  /// @return The mutable MachineFunction containing this basic block.
  MachineFunction *getParent() { return xParent; }

  /// Returns true if the original IR terminator is an `indirectbr` with
  /// successor blocks. This typically corresponds to a `goto` in C, rather than
  /// jump tables.
  /// @return True if the original IR terminator is an `indirectbr` with successor blocks.
  bool terminatorIsComputedGotoWithSuccessors() const {
    return back().isIndirectBranch() && !succ_empty() &&
           llvm::all_of(successors(), [](const MachineBasicBlock *Succ) {
             return Succ->isIRBlockAddressTaken();
           });
  }

  /// Iterator over individual machine instructions (ignoring bundles).
  using instr_iterator = Instructions::iterator;
  /// Const iterator over individual machine instructions (ignoring bundles).
  using const_instr_iterator = Instructions::const_iterator;
  /// Reverse iterator over individual machine instructions (ignoring bundles).
  using reverse_instr_iterator = Instructions::reverse_iterator;
  /// Const reverse iterator over individual machine instructions.
  using const_reverse_instr_iterator = Instructions::const_reverse_iterator;

  /// Bundle-aware iterator over machine instructions.
  using iterator = MachineInstrBundleIterator<MachineInstr>;
  /// Const bundle-aware iterator over machine instructions.
  using const_iterator = MachineInstrBundleIterator<const MachineInstr>;
  /// Bundle-aware reverse iterator over machine instructions.
  using reverse_iterator = MachineInstrBundleIterator<MachineInstr, true>;
  /// Const bundle-aware reverse iterator over machine instructions.
  using const_reverse_iterator =
      MachineInstrBundleIterator<const MachineInstr, true>;

  /// Return the number of instructions in this block (including debug instrs).
  /// @return The number of instructions in this block (including debug instrs).
  unsigned size() const { return (unsigned)Insts.size(); }
  /// Return true if the non-debug instruction count exceeds \p Limit.
  /// @param Limit Maximum allowed non-debug instruction count.
  /// @return True if the non-debug instruction count exceeds \p Limit.
  LLVM_ABI bool sizeWithoutDebugLargerThan(unsigned Limit) const;
  /// Return true if this block contains no instructions.
  /// @return True if this block contains no instructions.
  bool empty() const { return Insts.empty(); }

  /// Return a mutable reference to the first instruction (ignoring bundles).
  /// @return A mutable reference to the first instruction (ignoring bundles).
  MachineInstr       &instr_front()       { return Insts.front(); }
  /// Return a mutable reference to the last instruction (ignoring bundles).
  /// @return A mutable reference to the last instruction (ignoring bundles).
  MachineInstr       &instr_back()        { return Insts.back();  }
  /// Return a const reference to the first instruction (ignoring bundles).
  /// @return A const reference to the first instruction (ignoring bundles).
  const MachineInstr &instr_front() const { return Insts.front(); }
  /// Return a const reference to the last instruction (ignoring bundles).
  /// @return A const reference to the last instruction (ignoring bundles).
  const MachineInstr &instr_back()  const { return Insts.back();  }

  /// Return the first instruction in the block (bundle-aware).
  /// @return The first instruction in the block (bundle-aware).
  MachineInstr       &front()             { return Insts.front(); }
  /// Return the last instruction in the block (bundle-aware).
  /// @return The last instruction in the block (bundle-aware).
  MachineInstr       &back()              { return *--end();      }
  /// Return the first instruction in the block (bundle-aware).
  /// @return The first instruction in the block (bundle-aware).
  const MachineInstr &front()       const { return Insts.front(); }
  /// Return the last instruction in the block (bundle-aware).
  /// @return The last instruction in the block (bundle-aware).
  const MachineInstr &back()        const { return *--end();      }

  /// Return an iterator to the first machine instruction in the block.
  /// @return An iterator to the first machine instruction in the block.
  instr_iterator                instr_begin()       { return Insts.begin();  }
  /// Return a const iterator to the first machine instruction in the block.
  /// @return A const iterator to the first machine instruction in the block.
  const_instr_iterator          instr_begin() const { return Insts.begin();  }
  /// Return an iterator past the last machine instruction in the block.
  /// @return An iterator past the last machine instruction in the block.
  instr_iterator                  instr_end()       { return Insts.end();    }
  /// Return a const iterator past the last machine instruction in the block.
  /// @return A const iterator past the last machine instruction in the block.
  const_instr_iterator            instr_end() const { return Insts.end();    }
  /// Return a reverse iterator to the last machine instruction in the block.
  /// @return A reverse iterator to the last machine instruction in the block.
  reverse_instr_iterator       instr_rbegin()       { return Insts.rbegin(); }
  /// Return a const reverse iterator to the last machine instruction.
  /// @return A const reverse iterator to the last machine instruction.
  const_reverse_instr_iterator instr_rbegin() const { return Insts.rbegin(); }
  /// Return a reverse iterator past the first machine instruction.
  /// @return A reverse iterator past the first machine instruction.
  reverse_instr_iterator       instr_rend  ()       { return Insts.rend();   }
  /// Return a const reverse iterator past the first machine instruction.
  /// @return A const reverse iterator past the first machine instruction.
  const_reverse_instr_iterator instr_rend  () const { return Insts.rend();   }

  /// Range over individual machine instructions (ignoring bundles).
  using instr_range = iterator_range<instr_iterator>;
  /// Const range over individual machine instructions (ignoring bundles).
  using const_instr_range = iterator_range<const_instr_iterator>;
  /// Return a range over the instructions in this block (ignoring bundles).
  /// @return A range over the instructions in this block (ignoring bundles).
  instr_range instrs() { return instr_range(instr_begin(), instr_end()); }
  /// Return a const range over the instructions in this block.
  /// @return A const range over the instructions in this block.
  const_instr_range instrs() const {
    return const_instr_range(instr_begin(), instr_end());
  }

  /// Return a bundle-aware iterator to the first instruction.
  /// @return A bundle-aware iterator to the first instruction.
  iterator                begin()       { return instr_begin();  }
  /// Return a const bundle-aware iterator to the first instruction.
  /// @return A const bundle-aware iterator to the first instruction.
  const_iterator          begin() const { return instr_begin();  }
  /// Return a bundle-aware iterator past the last instruction.
  /// @return A bundle-aware iterator past the last instruction.
  iterator                end  ()       { return instr_end();    }
  /// Return a const bundle-aware iterator past the last instruction.
  /// @return A const bundle-aware iterator past the last instruction.
  const_iterator          end  () const { return instr_end();    }
  /// Return a reverse bundle-aware iterator to the last instruction.
  /// @return A reverse bundle-aware iterator to the last instruction.
  reverse_iterator rbegin() {
    return reverse_iterator::getAtBundleBegin(instr_rbegin());
  }
  /// Return a const reverse bundle iterator to the last instruction.
  /// @return A const reverse bundle iterator to the last instruction.
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator::getAtBundleBegin(instr_rbegin());
  }
  /// Return a reverse bundle-aware iterator past the first instruction.
  /// @return A reverse bundle-aware iterator past the first instruction.
  reverse_iterator rend() { return reverse_iterator(instr_rend()); }
  /// Return a const reverse bundle-aware iterator past the first instruction.
  /// @return A const reverse bundle-aware iterator past the first instruction.
  const_reverse_iterator rend() const {
    return const_reverse_iterator(instr_rend());
  }

  /// Return the pointer-to-member used by MachineInstr::getNextNode().
  /// @param MI Unused; required by the ilist traits interface.
  /// @return Pointer-to-member for the instruction sublist.
  static Instructions MachineBasicBlock::*getSublistAccess(MachineInstr *MI) {
    return &MachineBasicBlock::Insts;
  }

  /// Return a range over this block's terminator instructions.
  /// @return A range over this block's terminator instructions.
  inline iterator_range<iterator> terminators() {
    return make_range(getFirstTerminator(), end());
  }
  /// Return a const range over this block's terminator instructions.
  /// @return A const range over this block's terminator instructions.
  inline iterator_range<const_iterator> terminators() const {
    return make_range(getFirstTerminator(), end());
  }

  /// Returns a range that iterates over the phis in the basic block.
  /// @return A range that iterates over the phis in the basic block.
  inline iterator_range<iterator> phis() {
    return make_range(begin(), getFirstNonPHI());
  }
  /// Return a const range over the PHI instructions in this block.
  /// @return A const range over the PHI instructions in this block.
  inline iterator_range<const_iterator> phis() const {
    return const_cast<MachineBasicBlock *>(this)->phis();
  }

  // Machine-CFG iterators
  /// Iterator over CFG predecessor MachineBasicBlocks.
  using pred_iterator = SmallVectorImpl<MachineBasicBlock *>::iterator;
  /// Const iterator over CFG predecessor MachineBasicBlocks.
  using const_pred_iterator =
      SmallVectorImpl<MachineBasicBlock *>::const_iterator;
  /// Iterator over CFG successor MachineBasicBlocks.
  using succ_iterator = SmallVectorImpl<MachineBasicBlock *>::iterator;
  /// Const iterator over CFG successor MachineBasicBlocks.
  using const_succ_iterator =
      SmallVectorImpl<MachineBasicBlock *>::const_iterator;
  /// Reverse iterator over predecessor MachineBasicBlocks.
  using pred_reverse_iterator =
      SmallVectorImpl<MachineBasicBlock *>::reverse_iterator;
  /// Const reverse iterator over predecessor MachineBasicBlocks.
  using const_pred_reverse_iterator =
      SmallVectorImpl<MachineBasicBlock *>::const_reverse_iterator;
  /// Reverse iterator over successor MachineBasicBlocks.
  using succ_reverse_iterator =
      SmallVectorImpl<MachineBasicBlock *>::reverse_iterator;
  /// Const reverse iterator over successor MachineBasicBlocks.
  using const_succ_reverse_iterator =
      SmallVectorImpl<MachineBasicBlock *>::const_reverse_iterator;
  /// Return an iterator to the first CFG predecessor.
  /// @return An iterator to the first CFG predecessor.
  pred_iterator        pred_begin()       { return Predecessors.begin(); }
  /// Return a const iterator to the first CFG predecessor.
  /// @return A const iterator to the first CFG predecessor.
  const_pred_iterator  pred_begin() const { return Predecessors.begin(); }
  /// Return an iterator past the last CFG predecessor.
  /// @return An iterator past the last CFG predecessor.
  pred_iterator        pred_end()         { return Predecessors.end();   }
  /// Return a const iterator past the last CFG predecessor.
  /// @return A const iterator past the last CFG predecessor.
  const_pred_iterator  pred_end()   const { return Predecessors.end();   }
  /// Return a reverse iterator to the last CFG predecessor.
  /// @return A reverse iterator to the last CFG predecessor.
  pred_reverse_iterator        pred_rbegin()
                                          { return Predecessors.rbegin();}
  /// Return a const reverse iterator to the last CFG predecessor.
  /// @return A const reverse iterator to the last CFG predecessor.
  const_pred_reverse_iterator  pred_rbegin() const
                                          { return Predecessors.rbegin();}
  /// Return a reverse iterator past the first CFG predecessor.
  /// @return A reverse iterator past the first CFG predecessor.
  pred_reverse_iterator        pred_rend()
                                          { return Predecessors.rend();  }
  /// Return a const reverse iterator past the first CFG predecessor.
  /// @return A const reverse iterator past the first CFG predecessor.
  const_pred_reverse_iterator  pred_rend()   const
                                          { return Predecessors.rend();  }
  /// Return the number of CFG predecessors.
  /// @return The number of CFG predecessors.
  unsigned             pred_size()  const {
    return (unsigned)Predecessors.size();
  }
  /// Return true if this block has no CFG predecessors.
  /// @return True if this block has no CFG predecessors.
  bool                 pred_empty() const { return Predecessors.empty(); }
  /// Return an iterator to the first CFG successor.
  /// @return An iterator to the first CFG successor.
  succ_iterator        succ_begin()       { return Successors.begin();   }
  /// Return a const iterator to the first CFG successor.
  /// @return A const iterator to the first CFG successor.
  const_succ_iterator  succ_begin() const { return Successors.begin();   }
  /// Return an iterator past the last CFG successor.
  /// @return An iterator past the last CFG successor.
  succ_iterator        succ_end()         { return Successors.end();     }
  /// Return a const iterator past the last CFG successor.
  /// @return A const iterator past the last CFG successor.
  const_succ_iterator  succ_end()   const { return Successors.end();     }
  /// Return a reverse iterator to the last CFG successor.
  /// @return A reverse iterator to the last CFG successor.
  succ_reverse_iterator        succ_rbegin()
                                          { return Successors.rbegin();  }
  /// Return a const reverse iterator to the last CFG successor.
  /// @return A const reverse iterator to the last CFG successor.
  const_succ_reverse_iterator  succ_rbegin() const
                                          { return Successors.rbegin();  }
  /// Return a reverse iterator past the first CFG successor.
  /// @return A reverse iterator past the first CFG successor.
  succ_reverse_iterator        succ_rend()
                                          { return Successors.rend();    }
  /// Return a const reverse iterator past the first CFG successor.
  /// @return A const reverse iterator past the first CFG successor.
  const_succ_reverse_iterator  succ_rend()   const
                                          { return Successors.rend();    }
  /// Return the number of CFG successors.
  /// @return The number of CFG successors.
  unsigned             succ_size()  const {
    return (unsigned)Successors.size();
  }
  /// Return true if this block has no CFG successors.
  /// @return True if this block has no CFG successors.
  bool                 succ_empty() const { return Successors.empty();   }

  /// Return a range over this block's CFG predecessors.
  /// @return A range over this block's CFG predecessors.
  inline iterator_range<pred_iterator> predecessors() {
    return make_range(pred_begin(), pred_end());
  }
  /// Return a const range over this block's CFG predecessors.
  /// @return A const range over this block's CFG predecessors.
  inline iterator_range<const_pred_iterator> predecessors() const {
    return make_range(pred_begin(), pred_end());
  }
  /// Return a range over this block's CFG successors.
  /// @return A range over this block's CFG successors.
  inline iterator_range<succ_iterator> successors() {
    return make_range(succ_begin(), succ_end());
  }
  /// Return a const range over this block's CFG successors.
  /// @return A const range over this block's CFG successors.
  inline iterator_range<const_succ_iterator> successors() const {
    return make_range(succ_begin(), succ_end());
  }

  // LiveIn management methods.

  /// Add physical register \p PhysReg as live-in to this block.
  ///
  /// Note that it is an error to add the same register to the same set more
  /// than once unless the intention is to call sortUniqueLiveIns after all
  /// registers are added.
  /// @param PhysReg Physical register to mark live-in.
  /// @param LaneMask Lanes of \p PhysReg that are live-in.
  void addLiveIn(MCRegister PhysReg,
                 LaneBitmask LaneMask = LaneBitmask::getAll()) {
    LiveIns.push_back(RegisterMaskPair(PhysReg, LaneMask));
  }
  /// Add live-in register/lane pair \p RegMaskPair to this block.
  /// @param RegMaskPair Physical register and lane mask to mark live-in.
  void addLiveIn(const RegisterMaskPair &RegMaskPair) {
    LiveIns.push_back(RegMaskPair);
  }

  /// Sort and unique the LiveIns vector for cheaper membership tests.
  ///
  /// It can be significantly faster to do this than repeatedly calling isLiveIn
  /// before calling addLiveIn for every LiveIn insertion.
  LLVM_ABI void sortUniqueLiveIns();

  /// Clear live in list.
  LLVM_ABI void clearLiveIns();

  /// Clear the live in list, and return the removed live in's in \p OldLiveIns.
  /// Requires that the vector \p OldLiveIns is empty.
  /// @param OldLiveIns Output vector that receives the previous live-ins.
  LLVM_ABI void clearLiveIns(std::vector<RegisterMaskPair> &OldLiveIns);

  /// Add \p PhysReg as live-in and create a virtual copy in class \p RC.
  ///
  /// Return the virtual register that is a copy of the live in PhysReg.
  /// @param PhysReg Physical register to mark live-in.
  /// @param RC Register class for the virtual copy.
  /// @return The virtual register that copies the live-in physical register.
  LLVM_ABI Register addLiveIn(MCRegister PhysReg,
                              const TargetRegisterClass *RC);

  /// Remove the specified register from the live in set.
  /// @param Reg Physical register to remove from the live-in set.
  /// @param LaneMask Lanes of \p Reg to clear from the live-in set.
  LLVM_ABI void removeLiveIn(MCRegister Reg,
                             LaneBitmask LaneMask = LaneBitmask::getAll());

  /// Remove \p Reg and overlapping live-in registers/lanes.
  ///
  /// The method is subreg-aware and removes Reg and its subregs from the live
  /// in set. It also clears the corresponding bitmask from its live-in super
  /// registers.
  /// @param Reg Physical register whose overlapping live-ins are removed.
  LLVM_ABI void removeLiveInOverlappedWith(MCRegister Reg);

  /// Return true if the specified register is in the live in set.
  /// @param Reg Physical register to query.
  /// @param LaneMask Lanes of \p Reg that must be live-in.
  /// @return True if the specified register is in the live in set.
  LLVM_ABI bool isLiveIn(MCRegister Reg,
                         LaneBitmask LaneMask = LaneBitmask::getAll()) const;

  // Iteration support for live in sets.  These sets are kept in sorted
  // order by their register number.
  /// Const iterator over live-in register/lane pairs.
  using livein_iterator = LiveInVector::const_iterator;

  /// Return a live-in begin iterator without verifying liveness accuracy.
  ///
  /// Unlike livein_begin, this method does not check that the liveness
  /// information is accurate. Still for debug purposes it may be useful to have
  /// iterators that won't assert if the liveness information is not current.
  /// @return A live-in begin iterator without verifying liveness accuracy.
  livein_iterator livein_begin_dbg() const { return LiveIns.begin(); }
  /// Return live-in registers without verifying that liveness data is current.
  /// @return Live-in registers without verifying that liveness data is current.
  iterator_range<livein_iterator> liveins_dbg() const {
    return make_range(livein_begin_dbg(), livein_end());
  }

  /// Return an iterator to the beginning of the live-in register list.
  ///
  /// Asserts that the MachineFunction tracks liveness accurately.
  /// @return An iterator to the beginning of the live-in register list.
  LLVM_ABI livein_iterator livein_begin() const;
  /// Return an iterator to the end of the live-in register list.
  /// @return An iterator to the end of the live-in register list.
  livein_iterator livein_end()   const { return LiveIns.end(); }
  /// Return true if this block has no live-in registers.
  /// @return True if this block has no live-in registers.
  bool            livein_empty() const { return LiveIns.empty(); }
  /// Return a range over this block's live-in registers.
  /// @return A range over this block's live-in registers.
  iterator_range<livein_iterator> liveins() const {
    return make_range(livein_begin(), livein_end());
  }

  /// Remove entry from the livein set and return iterator to the next.
  /// @param I Live-in entry to remove.
  /// @return Iterator to the next live-in entry after the removed one.
  LLVM_ABI livein_iterator removeLiveIn(livein_iterator I);

  /// Return the underlying live-in register/lane vector.
  /// @return Const reference to the live-in register/lane vector.
  const std::vector<RegisterMaskPair> &getLiveIns() const { return LiveIns; }

  /// Input iterator over registers potentially live out of this block.
  class liveout_iterator {
  public:
    /// Input iterator category tag.
    using iterator_category = std::input_iterator_tag;
    /// Iterator difference type.
    using difference_type = std::ptrdiff_t;
    /// Register mask pair for a live-out register.
    using value_type = RegisterMaskPair;
    /// Pointer to a live-out register mask pair.
    using pointer = const RegisterMaskPair *;
    /// Reference to a live-out register mask pair.
    using reference = const RegisterMaskPair &;

    /// Construct a live-out iterator over successors of \p MBB.
    /// @param MBB Block whose successor live-ins are scanned.
    /// @param ExceptionPointer EH exception pointer register to skip on pads.
    /// @param ExceptionSelector EH exception selector register to skip on pads.
    /// @param End If true, construct the past-the-end iterator.
    liveout_iterator(const MachineBasicBlock &MBB, MCRegister ExceptionPointer,
                     MCRegister ExceptionSelector, bool End)
        : ExceptionPointer(ExceptionPointer),
          ExceptionSelector(ExceptionSelector), BlockI(MBB.succ_begin()),
          BlockEnd(MBB.succ_end()) {
      if (End)
        BlockI = BlockEnd;
      else if (BlockI != BlockEnd) {
        LiveRegI = (*BlockI)->livein_begin();
        if (!advanceToValidPosition())
          return;
        if ((*BlockI)->isEHPad() && (LiveRegI->PhysReg == ExceptionPointer ||
                                     LiveRegI->PhysReg == ExceptionSelector))
          ++(*this);
      }
    }

    /// Advance to the next live-out register and return this iterator.
    /// @return Reference to this iterator after advancing.
    liveout_iterator &operator++() {
      do {
        ++LiveRegI;
        if (!advanceToValidPosition())
          return *this;
      } while ((*BlockI)->isEHPad() &&
               (LiveRegI->PhysReg == ExceptionPointer ||
                LiveRegI->PhysReg == ExceptionSelector));
      return *this;
    }

    /// Advance to the next live-out register, returning the previous position.
    /// @param Unused Unused postfix-discriminator parameter.
    /// @return Copy of the iterator before advancing.
    liveout_iterator operator++(int Unused) {
      liveout_iterator Tmp = *this;
      ++(*this);
      return Tmp;
    }

    /// Return a reference to the current live-out register/lane pair.
    /// @return Reference to the current live-out register/lane pair.
    reference operator*() const {
      return *LiveRegI;
    }

    /// Member access for the current live-out register/lane pair.
    /// @return Pointer to the current live-out register/lane pair.
    pointer operator->() const {
      return &*LiveRegI;
    }

    /// Return true if this iterator and \p RHS refer to the same position.
    /// @param RHS Other live-out iterator to compare against.
    /// @return True if this iterator and \p RHS refer to the same position.
    bool operator==(const liveout_iterator &RHS) const {
      if (BlockI != BlockEnd)
        return BlockI == RHS.BlockI && LiveRegI == RHS.LiveRegI;
      return RHS.BlockI == BlockEnd;
    }

    /// Return true if this iterator and \p RHS refer to different positions.
    /// @param RHS Other live-out iterator to compare against.
    /// @return True if this iterator and \p RHS refer to different positions.
    bool operator!=(const liveout_iterator &RHS) const {
      return !(*this == RHS);
    }
  private:
    bool advanceToValidPosition() {
      if (LiveRegI != (*BlockI)->livein_end())
        return true;

      do {
        ++BlockI;
      } while (BlockI != BlockEnd && (*BlockI)->livein_empty());
      if (BlockI == BlockEnd)
        return false;

      LiveRegI = (*BlockI)->livein_begin();
      return true;
    }

    MCRegister ExceptionPointer, ExceptionSelector;
    const_succ_iterator BlockI;
    const_succ_iterator BlockEnd;
    livein_iterator LiveRegI;
  };

  /// Return a begin iterator over registers potentially live out of this block.
  ///
  /// Scans successor basic blocks' liveins. There may be duplicates or
  /// overlapping registers in the list returned.
  /// @return A begin iterator over registers potentially live out of this block.
  LLVM_ABI liveout_iterator liveout_begin() const;
  /// Return the past-the-end live-out iterator for this block.
  /// @return The past-the-end live-out iterator for this block.
  liveout_iterator liveout_end() const {
    return liveout_iterator(*this, 0, 0, true);
  }
  /// Return a range over registers potentially live out of this block.
  /// @return A range over registers potentially live out of this block.
  iterator_range<liveout_iterator> liveouts() const {
    return make_range(liveout_begin(), liveout_end());
  }

  /// Get the clobber mask for the start of this basic block. Funclets use this
  /// to prevent register allocation across funclet transitions.
  /// @param TRI Target register info used to build the clobber mask.
  /// @return Clobber mask for the start of this basic block.
  LLVM_ABI const uint32_t *
  getBeginClobberMask(const TargetRegisterInfo *TRI) const;

  /// Get the clobber mask for the end of the basic block.
  /// \see getBeginClobberMask()
  /// @param TRI Target register info used to build the clobber mask.
  /// @return Clobber mask for the end of this basic block.
  LLVM_ABI const uint32_t *
  getEndClobberMask(const TargetRegisterInfo *TRI) const;

  /// Return alignment of the basic block.
  /// @return Alignment of the basic block.
  Align getAlignment() const { return Alignment; }

  /// Set alignment of the basic block.
  /// @param A Requested alignment for this block.
  void setAlignment(Align A) { Alignment = A; }

  /// Set alignment and the maximum padding allowed to achieve it.
  /// @param A Requested alignment for this block.
  /// @param MaxBytes Maximum padding bytes allowed for alignment.
  void setAlignment(Align A, unsigned MaxBytes) {
    setAlignment(A);
    setMaxBytesForAlignment(MaxBytes);
  }

  /// Return the maximum amount of padding allowed for aligning the basic block.
  /// @return Maximum padding bytes allowed for alignment.
  unsigned getMaxBytesForAlignment() const { return MaxBytesForAlignment; }

  /// Set the maximum amount of padding allowed for aligning the basic block
  /// @param MaxBytes Maximum padding bytes allowed for alignment.
  void setMaxBytesForAlignment(unsigned MaxBytes) {
    MaxBytesForAlignment = MaxBytes;
  }

  /// Returns true if the block is a landing pad. That is this basic block is
  /// entered via an exception handler.
  /// @return True if the block is a landing pad.
  bool isEHPad() const { return IsEHPad; }

  /// Indicates the block is a landing pad.  That is this basic block is entered
  /// via an exception handler.
  /// @param V Whether this block is an EH pad.
  void setIsEHPad(bool V = true) { IsEHPad = V; }

  /// Return true if any successor of this block is an EH pad.
  /// @return True if any successor of this block is an EH pad.
  LLVM_ABI bool hasEHPadSuccessor() const;

  /// Returns true if this is the entry block of the function.
  /// @return True if this is the entry block of the function.
  LLVM_ABI bool isEntryBlock() const;

  /// Returns true if this is the entry block of an EH scope, i.e., the block
  /// that used to have a catchpad or cleanuppad instruction in the LLVM IR.
  /// @return True if this is the entry block of an EH scope, i.e., the block that used to have a catchpad or cleanuppad instruction in the LLVM IR.
  bool isEHScopeEntry() const { return IsEHScopeEntry; }

  /// Indicates if this is the entry block of an EH scope, i.e., the block that
  /// that used to have a catchpad or cleanuppad instruction in the LLVM IR.
  /// @param V Whether this block is an EH scope entry.
  void setIsEHScopeEntry(bool V = true) { IsEHScopeEntry = V; }

  /// Returns true if this is a target of Windows EH Continuation Guard.
  /// @return True if this is a target of Windows EH Continuation Guard.
  bool isEHContTarget() const { return IsEHContTarget; }

  /// Indicates if this is a target of Windows EH Continuation Guard.
  /// @param V Whether this block is an EH continuation target.
  void setIsEHContTarget(bool V = true) { IsEHContTarget = V; }

  /// Returns true if this is the entry block of an EH funclet.
  /// @return True if this is the entry block of an EH funclet.
  bool isEHFuncletEntry() const { return IsEHFuncletEntry; }

  /// Indicates if this is the entry block of an EH funclet.
  /// @param V Whether this block is an EH funclet entry.
  void setIsEHFuncletEntry(bool V = true) { IsEHFuncletEntry = V; }

  /// Returns true if this is the entry block of a cleanup funclet.
  /// @return True if this is the entry block of a cleanup funclet.
  bool isCleanupFuncletEntry() const { return IsCleanupFuncletEntry; }

  /// Indicates if this is the entry block of a cleanup funclet.
  /// @param V Whether this block is a cleanup funclet entry.
  void setIsCleanupFuncletEntry(bool V = true) { IsCleanupFuncletEntry = V; }

  /// Returns true if this block begins any section.
  /// @return True if this block begins any section.
  bool isBeginSection() const { return IsBeginSection; }

  /// Returns true if this block ends any section.
  /// @return True if this block ends any section.
  bool isEndSection() const { return IsEndSection; }

  /// Mark whether this block is the first in its basic block section.
  /// @param V Whether this block begins a section.
  void setIsBeginSection(bool V = true) { IsBeginSection = V; }

  /// Mark whether this block is the last in its basic block section.
  /// @param V Whether this block ends a section.
  void setIsEndSection(bool V = true) { IsEndSection = V; }

  /// Return the optional unique basic-block ID assigned to this block.
  /// @return The unique basic-block ID, if one is assigned.
  std::optional<UniqueBBID> getBBID() const { return BBID; }

  /// Returns the section ID of this basic block.
  /// @return The section ID of this basic block.
  MBBSectionID getSectionID() const { return SectionID; }

  /// Sets the fixed BBID of this basic block.
  /// @param V Unique basic-block ID to assign.
  void setBBID(const UniqueBBID &V) {
    assert(!BBID.has_value() && "Cannot change BBID.");
    BBID = V;
  }

  /// Sets the section ID for this basic block.
  /// @param V Section identifier to assign.
  void setSectionID(MBBSectionID V) { SectionID = V; }

  /// Returns the MCSymbol marking the end of this basic block.
  /// @return The MCSymbol marking the end of this basic block.
  LLVM_ABI MCSymbol *getEndSymbol() const;

  /// Return true if this block may be targeted by an INLINEASM_BR.
  ///
  /// This is an overestimate, by checking if any of the successors are indirect
  /// targets of any inlineasm_br in the function.
  /// @return True if this block may be targeted by an INLINEASM_BR.
  LLVM_ABI bool mayHaveInlineAsmBr() const;

  /// Returns true if this is the indirect dest of an INLINEASM_BR.
  /// @return True if this is the indirect dest of an INLINEASM_BR.
  bool isInlineAsmBrIndirectTarget() const {
    return IsInlineAsmBrIndirectTarget;
  }

  /// Indicates if this is the indirect dest of an INLINEASM_BR.
  /// @param V Whether this block is an INLINEASM_BR indirect target.
  void setIsInlineAsmBrIndirectTarget(bool V = true) {
    IsInlineAsmBrIndirectTarget = V;
  }

  /// Returns true if it is legal to hoist instructions into this block.
  /// @return True if it is legal to hoist instructions into this block.
  LLVM_ABI bool isLegalToHoistInto() const;

  // Code Layout methods.

  /// Move this block before \p NewAfter without changing the CFG.
  ///
  /// This only moves the block, it does not modify the CFG or adjust potential
  /// fall-throughs at the end of the block.
  /// @param NewAfter Block that should follow this block in layout order.
  LLVM_ABI void moveBefore(MachineBasicBlock *NewAfter);
  /// Move this block after \p NewBefore without changing the CFG.
  /// @param NewBefore Block that should precede this block in layout order.
  LLVM_ABI void moveAfter(MachineBasicBlock *NewBefore);

  /// Returns true if this and MBB belong to the same section.
  /// @param MBB Other machine basic block to compare section IDs with.
  /// @return True if this and MBB belong to the same section.
  bool sameSection(const MachineBasicBlock *MBB) const {
    return getSectionID() == MBB->getSectionID();
  }

  /// Update terminators after a change to this block's layout.
  ///
  /// PreviousLayoutSuccessor should be set to the block which may have been
  /// used as fallthrough before the block layout was modified. If the block
  /// previously fell through to that block, it may now need a branch. If it
  /// previously branched to another block, it may now be able to fallthrough to
  /// the current layout successor.
  /// @param PreviousLayoutSuccessor Former layout successor before the move.
  LLVM_ABI void updateTerminator(MachineBasicBlock *PreviousLayoutSuccessor);

  // Machine-CFG mutators

  /// Add \p Succ as a successor of this MachineBasicBlock.
  ///
  /// The Predecessors list of Succ is automatically updated. PROB parameter is
  /// stored in Probabilities list. The default probability is set as unknown.
  /// Mixing known and unknown probabilities in successor list is not allowed.
  /// When all successors have unknown probabilities, 1 / N is returned as the
  /// probability for each successor, where N is the number of successors.
  ///
  /// Note that duplicate Machine CFG edges are not allowed.
  /// @param Succ Successor block to add.
  /// @param Prob Edge probability for the new successor.
  LLVM_ABI void
  addSuccessor(MachineBasicBlock *Succ,
               BranchProbability Prob = BranchProbability::getUnknown());

  /// Add \p Succ as a successor without recording an edge probability.
  ///
  /// The Predecessors list of Succ is automatically updated. The probability is
  /// not provided because BPI is not available (e.g. -O0 is used), in which
  /// case edge probabilities won't be used. Using this interface can save some
  /// space.
  /// @param Succ Successor block to add.
  LLVM_ABI void addSuccessorWithoutProb(MachineBasicBlock *Succ);

  /// Set successor probability of a given iterator.
  /// @param I Iterator identifying the successor edge.
  /// @param Prob New probability for that edge.
  LLVM_ABI void setSuccProbability(succ_iterator I, BranchProbability Prob);

  /// Normalize successor probabilities so they sum to one.
  ///
  /// This is usually done when the current update on this MBB is done, and the
  /// sum of its successors' probabilities is not guaranteed to be one. The user
  /// is responsible for the correct use of this function.
  /// MBB::removeSuccessor() has an option to do this automatically.
  void normalizeSuccProbs() {
    BranchProbability::normalizeProbabilities(Probs.begin(), Probs.end());
  }

  /// Validate successors' probabilities and check if the sum of them is
  /// approximate one. This only works in DEBUG mode.
  LLVM_ABI void validateSuccProbs() const;

  /// Remove \p Succ from this block's successor list.
  ///
  /// The Predecessors list of Succ is automatically updated. If
  /// NormalizeSuccProbs is true, then normalize successors' probabilities after
  /// the successor is removed.
  /// @param Succ Successor block to remove.
  /// @param NormalizeSuccProbs Whether to renormalize remaining probabilities.
  LLVM_ABI void removeSuccessor(MachineBasicBlock *Succ,
                                bool NormalizeSuccProbs = false);

  /// Remove the successor identified by iterator \p I.
  ///
  /// The Predecessors list of Succ is automatically updated. If
  /// NormalizeSuccProbs is true, then normalize successors' probabilities after
  /// the successor is removed. Return the iterator to the element after the one
  /// removed.
  /// @param I Iterator identifying the successor to remove.
  /// @param NormalizeSuccProbs Whether to renormalize remaining probabilities.
  /// @return Iterator to the element after the removed successor.
  LLVM_ABI succ_iterator removeSuccessor(succ_iterator I,
                                         bool NormalizeSuccProbs = false);

  /// Replace successor OLD with NEW and update probability info.
  /// @param Old Former successor to replace.
  /// @param New Replacement successor.
  LLVM_ABI void replaceSuccessor(MachineBasicBlock *Old,
                                 MachineBasicBlock *New);

  /// Copy a successor (and any probability info) from original block to this
  /// block's. Uses an iterator into the original blocks successors.
  ///
  /// This is useful when doing a partial clone of successors. Afterward, the
  /// probabilities may need to be normalized.
  /// @param Orig Source machine basic block.
  /// @param I Iterator identifying the successor in \p Orig to copy.
  LLVM_ABI void copySuccessor(const MachineBasicBlock *Orig, succ_iterator I);

  /// Split the old successor into old plus new and updates the probability
  /// info.
  /// @param Old Existing successor that is being split.
  /// @param New Additional successor created by the split.
  /// @param NormalizeSuccProbs Whether to renormalize probabilities afterward.
  LLVM_ABI void splitSuccessor(MachineBasicBlock *Old, MachineBasicBlock *New,
                               bool NormalizeSuccProbs = false);

  /// Transfers all the successors from MBB to this machine basic block (i.e.,
  /// copies all the successors FromMBB and remove all the successors from
  /// FromMBB).
  /// @param FromMBB Block whose successors are moved to this block.
  LLVM_ABI void transferSuccessors(MachineBasicBlock *FromMBB);

  /// Transfers all the successors, as in transferSuccessors, and update PHI
  /// operands in the successor blocks which refer to FromMBB to refer to this.
  /// @param FromMBB Block whose successors and PHI uses are transferred.
  LLVM_ABI void transferSuccessorsAndUpdatePHIs(MachineBasicBlock *FromMBB);

  /// Return true if any of the successors have probabilities attached to them.
  /// @return True if any of the successors have probabilities attached to them.
  bool hasSuccessorProbabilities() const { return !Probs.empty(); }

  /// Return true if the specified MBB is a predecessor of this block.
  /// @param MBB Candidate predecessor block.
  /// @return True if the specified MBB is a predecessor of this block.
  LLVM_ABI bool isPredecessor(const MachineBasicBlock *MBB) const;

  /// Return true if the specified MBB is a successor of this block.
  /// @param MBB Candidate successor block.
  /// @return True if the specified MBB is a successor of this block.
  LLVM_ABI bool isSuccessor(const MachineBasicBlock *MBB) const;

  /// Return true if \p MBB is the immediate layout successor of this block.
  ///
  /// That is, if this block exits by falling through, control will transfer to
  /// the specified MBB. Note that MBB need not be a successor at all, for
  /// example if this block ends with an unconditional branch to some other
  /// block.
  /// @param MBB Candidate layout successor.
  /// @return True if \p MBB is the immediate layout successor of this block.
  LLVM_ABI bool isLayoutSuccessor(const MachineBasicBlock *MBB) const;

  /// Return the successor of this block if it has a single successor.
  /// Otherwise return a null pointer.
  /// @return The successor of this block if it has a single successor.
  LLVM_ABI const MachineBasicBlock *getSingleSuccessor() const;
  /// Mutable overload of getSingleSuccessor.
  /// @return Same as getSingleSuccessor.
  MachineBasicBlock *getSingleSuccessor() {
    return const_cast<MachineBasicBlock *>(
        static_cast<const MachineBasicBlock *>(this)->getSingleSuccessor());
  }

  /// Return the predecessor of this block if it has a single predecessor.
  /// Otherwise return a null pointer.
  /// @return The predecessor of this block if it has a single predecessor.
  LLVM_ABI const MachineBasicBlock *getSinglePredecessor() const;
  /// Mutable overload of getSinglePredecessor.
  /// @return Same as getSinglePredecessor.
  MachineBasicBlock *getSinglePredecessor() {
    return const_cast<MachineBasicBlock *>(
        static_cast<const MachineBasicBlock *>(this)->getSinglePredecessor());
  }

  /// Return the fallthrough block if control can fall off this block's end.
  ///
  /// If an explicit branch to the fallthrough block is not allowed, set
  /// JumpToFallThrough to be false. Non-null return is a conservative answer.
  /// @param JumpToFallThrough Whether an explicit branch to the fallthrough is
  /// allowed when computing the result.
  /// @return The fallthrough block if control can fall off this block's end.
  LLVM_ABI MachineBasicBlock *getFallThrough(bool JumpToFallThrough = true);

  /// Return the logical fallthrough successor, by branch or layout fallthrough.
  ///
  /// Non-null return is a conservative answer.
  /// @return The logical fallthrough successor, by branch or layout fallthrough.
  MachineBasicBlock *getLogicalFallThrough() { return getFallThrough(false); }

  /// Return true if this block can fall through to the next layout block.
  ///
  /// This should return false if it can reach the block after it, but it uses
  /// an explicit branch to do so (e.g., a table jump). True is a conservative
  /// answer.
  /// @return True if this block can fall through to the next layout block.
  LLVM_ABI bool canFallThrough();

  /// Return an iterator to the first non-PHI instruction in this block.
  ///
  /// When adding instructions to the beginning of the basic block, they should
  /// be added before the returned value, not before the first instruction,
  /// which might be PHI. Returns end() if there's no non-PHI instruction.
  /// @return An iterator to the first non-PHI instruction in this block.
  LLVM_ABI iterator getFirstNonPHI();
  /// Const overload of getFirstNonPHI.
  /// @return Same as getFirstNonPHI.
  const_iterator getFirstNonPHI() const {
    return const_cast<MachineBasicBlock *>(this)->getFirstNonPHI();
  }

  /// Return the first instruction after \p I that is not a PHI or label.
  ///
  /// This is the correct point to insert lowered copies at the beginning of a
  /// basic block that must be before any debugging information.
  /// @param I Starting position in this block.
  /// @return The first instruction after \p I that is not a PHI or label.
  LLVM_ABI iterator SkipPHIsAndLabels(iterator I);

  /// Return the first instruction after \p I that is not a PHI, label, or debug.
  ///
  /// This is the correct point to insert copies at the beginning of a basic
  /// block. \p Reg is the register being used by a spill or defined for a
  /// restore/split during register allocation.
  /// @param I Starting position in this block.
  /// @param Reg Register associated with the spill/restore being inserted.
  /// @param SkipPseudoOp If true, also skip pseudo-probe instructions.
  /// @return The first instruction after \p I that is not a PHI, label, or debug.
  LLVM_ABI iterator SkipPHIsLabelsAndDebug(iterator I,
                                           Register Reg = Register(),
                                           bool SkipPseudoOp = true);

  /// Returns an iterator to the first terminator instruction of this basic
  /// block. If a terminator does not exist, it returns end().
  /// @return An iterator to the first terminator instruction of this basic block.
  LLVM_ABI iterator getFirstTerminator();
  /// Const overload returning the first terminator instruction, or end() if
  /// none exists.
  /// @return An iterator to the first terminator, or end() if none exists.
  const_iterator getFirstTerminator() const {
    return const_cast<MachineBasicBlock *>(this)->getFirstTerminator();
  }

  /// Same getFirstTerminator but it ignores bundles and return an
  /// instr_iterator instead.
  /// @return An instr_iterator to the first terminator, or instr_end().
  LLVM_ABI instr_iterator getFirstInstrTerminator();

  /// Find the first terminator by scanning forward from the block start.
  ///
  /// This can handle cases in GlobalISel where there may be non-terminator
  /// instructions between terminators, for which getFirstTerminator() will not
  /// work correctly.
  /// @return An iterator to the first terminator found by a forward scan.
  LLVM_ABI iterator getFirstTerminatorForward();

  /// Return an iterator to the first non-debug instruction, or end().
  ///
  /// Skip any pseudo probe operation if \c SkipPseudoOp is true. Pseudo probes
  /// are like debug instructions which do not turn into real machine code. We
  /// try to use the function to skip both debug instructions and pseudo probe
  /// operations to avoid API proliferation. This should work most of the time
  /// when considering optimizing the rest of code in the block, except for
  /// certain cases where pseudo probes are designed to block the optimizations.
  /// For example, code merge like optimizations are supposed to be blocked by
  /// pseudo probes for better AutoFDO profile quality. Therefore, they should
  /// be considered as a valid instruction when this function is called in a
  /// context of such optimizations. On the other hand, \c SkipPseudoOp should
  /// be true when it's used in optimizations that unlikely hurt profile
  /// quality, e.g., without block merging. The default value of \c SkipPseudoOp
  /// is set to true to maximize code quality in general, with an explict false
  /// value passed in in a few places like branch folding and if-conversion to
  /// favor profile quality.
  /// @param SkipPseudoOp If true, also skip pseudo-probe instructions.
  /// @return An iterator to the first non-debug instruction, or end().
  LLVM_ABI iterator getFirstNonDebugInstr(bool SkipPseudoOp = true);
  /// Const overload of getFirstNonDebugInstr.
  /// @param SkipPseudoOp If true, also skip pseudo-probe instructions.
  /// @return Same as getFirstNonDebugInstr.
  const_iterator getFirstNonDebugInstr(bool SkipPseudoOp = true) const {
    return const_cast<MachineBasicBlock *>(this)->getFirstNonDebugInstr(
        SkipPseudoOp);
  }

  /// Return an iterator to the last non-debug instruction, or end().
  ///
  /// Skip any pseudo operation if \c SkipPseudoOp is true. Pseudo probes are
  /// like debug instructions which do not turn into real machine code. We try
  /// to use the function to skip both debug instructions and pseudo probe
  /// operations to avoid API proliferation. This should work most of the time
  /// when considering optimizing the rest of code in the block, except for
  /// certain cases where pseudo probes are designed to block the optimizations.
  /// For example, code merge like optimizations are supposed to be blocked by
  /// pseudo probes for better AutoFDO profile quality. Therefore, they should
  /// be considered as a valid instruction when this function is called in a
  /// context of such optimizations. On the other hand, \c SkipPseudoOp should
  /// be true when it's used in optimizations that unlikely hurt profile
  /// quality, e.g., without block merging. The default value of \c SkipPseudoOp
  /// is set to true to maximize code quality in general, with an explict false
  /// value passed in in a few places like branch folding and if-conversion to
  /// favor profile quality.
  /// @param SkipPseudoOp If true, also skip pseudo-probe instructions.
  /// @return An iterator to the last non-debug instruction, or end().
  LLVM_ABI iterator getLastNonDebugInstr(bool SkipPseudoOp = true);
  /// Const overload of getLastNonDebugInstr.
  /// @param SkipPseudoOp If true, also skip pseudo-probe instructions.
  /// @return Same as getLastNonDebugInstr.
  const_iterator getLastNonDebugInstr(bool SkipPseudoOp = true) const {
    return const_cast<MachineBasicBlock *>(this)->getLastNonDebugInstr(
        SkipPseudoOp);
  }

  /// Convenience function that returns true if the block ends in a return
  /// instruction.
  /// @return True if the block ends in a return instruction.
  bool isReturnBlock() const {
    return !empty() && back().isReturn();
  }

  /// Convenience function that returns true if the bock ends in a EH scope
  /// return instruction.
  /// @return True if the block ends in an EH scope return instruction.
  bool isEHScopeReturnBlock() const {
    return !empty() && back().isEHScopeReturn();
  }

  /// Split this block after \p SplitInst into two consecutive blocks.
  ///
  /// A new block will be inserted after this block, and all instructions after
  /// \p SplitInst moved to it (\p SplitInst will be in the original block). If
  /// \p LIS is provided, LiveIntervals will be appropriately updated. \return
  /// the newly inserted block.
  ///
  /// If \p UpdateLiveIns is true, this will ensure the live ins list is
  /// accurate, including for physreg uses/defs in the original block.
  /// @param SplitInst Last instruction that remains in this block.
  /// @param UpdateLiveIns Whether to recompute live-ins for the split blocks.
  /// @param LIS Optional LiveIntervals analysis to update.
  LLVM_ABI MachineBasicBlock *splitAt(MachineInstr &SplitInst,
                                      bool UpdateLiveIns = true,
                                      LiveIntervals *LIS = nullptr);

  /// Analyses optionally updated when splitting a critical edge.
  struct SplitCriticalEdgeAnalyses {
    /// LiveIntervals analysis updated when splitting the critical edge.
    LiveIntervals *LIS;
    /// SlotIndexes analysis updated when splitting the critical edge.
    SlotIndexes *SI;
    /// LiveVariables analysis updated when splitting the critical edge.
    LiveVariables *LV;
    /// MachineLoopInfo analysis used when splitting the critical edge.
    MachineLoopInfo *MLI;
  };

  /// Split the critical edge from this block to \p Succ via the legacy PM.
  ///
  /// Updates analyses from the legacy pass manager when applicable, and returns
  /// the newly created block, or null if splitting is not possible.
  /// @param Succ Successor at the far end of the critical edge.
  /// @param P Legacy pass providing analyses to update.
  /// @param LiveInSets Optional live-in sets to update.
  /// @param MDTU Optional dominator tree updater.
  /// @return The newly created block, or null if splitting failed.
  MachineBasicBlock *
  SplitCriticalEdge(MachineBasicBlock *Succ, Pass &P,
                    std::vector<SparseBitVector<>> *LiveInSets = nullptr,
                    MachineDomTreeUpdater *MDTU = nullptr) {
    return SplitCriticalEdge(Succ, &P, nullptr, LiveInSets, MDTU);
  }

  /// Split the critical edge from this block to \p Succ via the new PM.
  ///
  /// Updates analyses from \p MFAM when applicable, and returns the newly
  /// created block, or null if splitting is not possible.
  /// @param Succ Successor at the far end of the critical edge.
  /// @param MFAM New pass manager analysis manager.
  /// @param LiveInSets Optional live-in sets to update.
  /// @param MDTU Optional dominator tree updater.
  /// @return The newly created block, or null if splitting failed.
  MachineBasicBlock *
  SplitCriticalEdge(MachineBasicBlock *Succ,
                    MachineFunctionAnalysisManager &MFAM,
                    std::vector<SparseBitVector<>> *LiveInSets = nullptr,
                    MachineDomTreeUpdater *MDTU = nullptr) {
    return SplitCriticalEdge(Succ, nullptr, &MFAM, LiveInSets, MDTU);
  }

  /// Split the critical edge using pre-collected analysis handles.
  ///
  /// Helper method for new pass manager migration.
  /// @param Succ Successor at the far end of the critical edge.
  /// @param Analyses Bundle of optional analyses to update.
  /// @param LiveInSets Optional live-in sets to update.
  /// @param MDTU Optional dominator tree updater.
  /// @return The newly created block, or null if splitting failed.
  LLVM_ABI MachineBasicBlock *
  SplitCriticalEdge(MachineBasicBlock *Succ,
                    const SplitCriticalEdgeAnalyses &Analyses,
                    std::vector<SparseBitVector<>> *LiveInSets = nullptr,
                    MachineDomTreeUpdater *MDTU = nullptr);

  /// Split the critical edge from this block to \p Succ.
  ///
  /// Updates analyses from legacy pass \p P or \p MFAM when applicable, and
  /// returns the newly created block, or null if splitting is not possible.
  /// @param Succ Successor at the far end of the critical edge.
  /// @param P Optional legacy pass providing analyses to update.
  /// @param MFAM Optional new pass manager analysis manager.
  /// @param LiveInSets Optional live-in sets to update.
  /// @param MDTU Optional dominator tree updater.
  /// @return The newly created block, or null if splitting failed.
  LLVM_ABI MachineBasicBlock *
  SplitCriticalEdge(MachineBasicBlock *Succ, Pass *P,
                    MachineFunctionAnalysisManager *MFAM,
                    std::vector<SparseBitVector<>> *LiveInSets = nullptr,
                    MachineDomTreeUpdater *MDTU = nullptr);

  /// Return true if the edge from this block to \p Succ can be split.
  ///
  /// If this returns true a subsequent call to SplitCriticalEdge is guaranteed
  /// to return a valid basic block if no changes occurred in the meantime.
  /// @param Succ Successor at the far end of the candidate critical edge.
  /// @param MLI Optional loop info used when checking split legality.
  /// @return True if the edge from this block to \p Succ can be split.
  LLVM_ABI bool
  canSplitCriticalEdge(const MachineBasicBlock *Succ,
                       const MachineLoopInfo *MLI = nullptr) const;

  /// Remove and destroy the first instruction in this block.
  void pop_front() { Insts.pop_front(); }
  /// Remove and destroy the last instruction in this block.
  void pop_back() { Insts.pop_back(); }
  /// Append instruction \p MI to the end of this block.
  /// @param MI Instruction to append.
  void push_back(MachineInstr *MI) { Insts.push_back(MI); }

  /// Insert MI into the instruction list before I, possibly inside a bundle.
  ///
  /// If the insertion point is inside a bundle, MI will be added to the bundle,
  /// otherwise MI will not be added to any bundle. That means this function
  /// alone can't be used to prepend or append instructions to bundles. See
  /// MIBundleBuilder::insert() for a more reliable way of doing that.
  /// @param I Insertion point in the instruction list.
  /// @param M Instruction to insert before \p I.
  /// @return Iterator to the inserted instruction.
  LLVM_ABI instr_iterator insert(instr_iterator I, MachineInstr *M);

  /// Insert a range of instructions into the instruction list before I.
  /// @param I Insertion point in the bundle-aware instruction list.
  /// @param S Begin of the instruction range to insert.
  /// @param E End of the instruction range to insert.
  template<typename IT>
  void insert(iterator I, IT S, IT E) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    Insts.insert(I.getInstrIterator(), S, E);
  }

  /// Insert MI into the instruction list before I.
  /// @param I Insertion point in the bundle-aware instruction list.
  /// @param MI Instruction to insert before \p I.
  /// @return Iterator to the inserted instruction.
  iterator insert(iterator I, MachineInstr *MI) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
           "Cannot insert instruction with bundle flags");
    return Insts.insert(I.getInstrIterator(), MI);
  }

  /// Insert MI into the instruction list after I.
  /// @param I Position after which \p MI is inserted.
  /// @param MI Instruction to insert after \p I.
  /// @return Iterator to the inserted instruction.
  iterator insertAfter(iterator I, MachineInstr *MI) {
    assert((I == end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
           "Cannot insert instruction with bundle flags");
    return Insts.insertAfter(I.getInstrIterator(), MI);
  }

  /// Insert MI after I, or after I's bundle if I is bundled.
  ///
  /// If I is bundled then insert MI into the instruction list after the end of
  /// the bundle, otherwise insert MI immediately after I.
  /// @param I Position after which \p MI is inserted.
  /// @param MI Instruction to insert.
  /// @return Iterator to the inserted instruction.
  instr_iterator insertAfterBundle(instr_iterator I, MachineInstr *MI) {
    assert((I == instr_end() || I->getParent() == this) &&
           "iterator points outside of basic block");
    assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
           "Cannot insert instruction with bundle flags");
    while (I->isBundledWithSucc())
      ++I;
    return Insts.insertAfter(I, MI);
  }

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  /// @param I Instruction to erase from this block.
  /// @return Iterator following the erased instruction.
  LLVM_ABI instr_iterator erase(instr_iterator I);

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  /// @param I Instruction to erase from this block.
  /// @return Iterator following the erased instruction.
  instr_iterator erase_instr(MachineInstr *I) {
    return erase(instr_iterator(I));
  }

  /// Remove a range of instructions from the instruction list and delete them.
  /// @param I Begin of the range to erase.
  /// @param E End of the range to erase.
  /// @return Iterator following the erased range.
  iterator erase(iterator I, iterator E) {
    return Insts.erase(I.getInstrIterator(), E.getInstrIterator());
  }

  /// Remove an instruction or bundle from the instruction list and delete it.
  ///
  /// If I points to a bundle of instructions, they are all erased.
  /// @param I Instruction or bundle to erase.
  /// @return Iterator following the erased instruction or bundle.
  iterator erase(iterator I) {
    return erase(I, std::next(I));
  }

  /// Remove an instruction from the instruction list and delete it.
  ///
  /// If I is the head of a bundle of instructions, the whole bundle will be
  /// erased.
  /// @param I Instruction (or bundle head) to erase.
  /// @return Iterator following the erased instruction or bundle.
  iterator erase(MachineInstr *I) {
    return erase(iterator(I));
  }

  /// Remove the unbundled instruction from the instruction list without
  /// deleting it.
  ///
  /// This function can not be used to remove bundled instructions, use
  /// remove_instr to remove individual instructions from a bundle.
  /// @param I Unbundled instruction to unlink.
  /// @return The unlinked instruction, still owned by the caller.
  MachineInstr *remove(MachineInstr *I) {
    assert(!I->isBundled() && "Cannot remove bundled instructions");
    return Insts.remove(instr_iterator(I));
  }

  /// Remove the possibly bundled instruction from the instruction list
  /// without deleting it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle will still be bundled after removing the single instruction.
  /// @param I Instruction to unlink from this block.
  /// @return The unlinked instruction, still owned by the caller.
  LLVM_ABI MachineInstr *remove_instr(MachineInstr *I);

  /// Remove all instructions from this basic block without deleting the block.
  void clear() {
    Insts.clear();
  }

  /// Take an instruction from MBB 'Other' at the position From, and insert it
  /// into this MBB right before 'Where'.
  ///
  /// If From points to a bundle of instructions, the whole bundle is moved.
  /// @param Where Insertion point in this block.
  /// @param Other Source machine basic block.
  /// @param From Instruction (or bundle) to move from \p Other.
  void splice(iterator Where, MachineBasicBlock *Other, iterator From) {
    // The range splice() doesn't allow noop moves, but this one does.
    if (Where != From)
      splice(Where, Other, From, std::next(From));
  }

  /// Take a block of instructions from MBB 'Other' in the range [From, To),
  /// and insert them into this MBB right before 'Where'.
  ///
  /// The instruction at 'Where' must not be included in the range of
  /// instructions to move.
  /// @param Where Insertion point in this block.
  /// @param Other Source machine basic block.
  /// @param From Begin of the instruction range to move.
  /// @param To End of the instruction range to move.
  void splice(iterator Where, MachineBasicBlock *Other,
              iterator From, iterator To) {
    Insts.splice(Where.getInstrIterator(), Other->Insts,
                 From.getInstrIterator(), To.getInstrIterator());
  }

  /// This method unlinks 'this' from the containing function, and returns it,
  /// but does not delete it.
  /// @return This block, unlinked from its parent function.
  LLVM_ABI MachineBasicBlock *removeFromParent();

  /// This method unlinks 'this' from the containing function and deletes it.
  LLVM_ABI void eraseFromParent();

  /// Given a machine basic block that branched to 'Old', change the code and
  /// CFG so that it branches to 'New' instead.
  /// @param Old Former branch/CFG target to replace.
  /// @param New Replacement branch/CFG target.
  LLVM_ABI void ReplaceUsesOfBlockWith(MachineBasicBlock *Old,
                                       MachineBasicBlock *New);

  /// Update all phi nodes in this basic block to refer to basic block \p New
  /// instead of basic block \p Old.
  /// @param Old Former predecessor referenced by PHI operands.
  /// @param New Replacement predecessor for PHI operands.
  LLVM_ABI void replacePhiUsesWith(MachineBasicBlock *Old,
                                   MachineBasicBlock *New);

  /// Find the next valid DebugLoc starting at \p MBBI, skipping debug instrs.
  ///
  /// Return UnknownLoc if there is none.
  /// @param MBBI Instruction iterator where the search starts.
  /// @return The next valid DebugLoc, or UnknownLoc if there is none.
  LLVM_ABI DebugLoc findDebugLoc(instr_iterator MBBI);
  /// Bundle-aware overload of findDebugLoc.
  /// @param MBBI Bundle iterator where the search starts.
  /// @return The next valid DebugLoc, or UnknownLoc if there is none.
  DebugLoc findDebugLoc(iterator MBBI) {
    return findDebugLoc(MBBI.getInstrIterator());
  }

  /// Find the next valid DebugLoc starting from reverse iterator \p MBBI.
  ///
  /// Same search as \ref findDebugLoc (toward the end of this MBB), but starts
  /// from a reverse iterator identifying the starting MI.
  /// @param MBBI Reverse instruction iterator where the search starts.
  /// @return The next valid DebugLoc, or UnknownLoc if there is none.
  LLVM_ABI DebugLoc rfindDebugLoc(reverse_instr_iterator MBBI);
  /// Bundle-aware reverse-iterator overload of rfindDebugLoc.
  /// @param MBBI Reverse bundle iterator where the search starts.
  /// @return The next valid DebugLoc, or UnknownLoc if there is none.
  DebugLoc rfindDebugLoc(reverse_iterator MBBI) {
    return rfindDebugLoc(MBBI.getInstrIterator());
  }

  /// Find the previous valid DebugLoc preceding \p MBBI.
  ///
  /// Skips debug instructions. It is possible to find the last DebugLoc in the
  /// MBB using findPrevDebugLoc(instr_end()). Return UnknownLoc if there is
  /// none.
  /// @param MBBI Instruction iterator where the backward search starts.
  /// @return The previous valid DebugLoc, or UnknownLoc if there is none.
  LLVM_ABI DebugLoc findPrevDebugLoc(instr_iterator MBBI);
  /// Bundle-aware overload of findPrevDebugLoc.
  /// @param MBBI Bundle iterator where the backward search starts.
  /// @return The previous valid DebugLoc, or UnknownLoc if there is none.
  DebugLoc findPrevDebugLoc(iterator MBBI) {
    return findPrevDebugLoc(MBBI.getInstrIterator());
  }

  /// Find the previous DebugLoc from a reverse instruction iterator.
  ///
  /// Same search as \ref findPrevDebugLoc (toward the beginning of this MBB),
  /// but starts from a reverse iterator. Unlike findPrevDebugLoc, scanning
  /// cannot begin at \c instr_end.
  /// @param MBBI Reverse instruction iterator where the search starts.
  /// @return The previous valid DebugLoc, or UnknownLoc if there is none.
  LLVM_ABI DebugLoc rfindPrevDebugLoc(reverse_instr_iterator MBBI);
  /// Same as findPrevDebugLoc, starting from reverse bundle iterator \p MBBI.
  /// @param MBBI Reverse bundle iterator where the search starts.
  /// @return The previous valid DebugLoc, or UnknownLoc if there is none.
  DebugLoc rfindPrevDebugLoc(reverse_iterator MBBI) {
    return rfindPrevDebugLoc(MBBI.getInstrIterator());
  }

  /// Find and return the merged DebugLoc of the branch instructions of the
  /// block. Return UnknownLoc if there is none.
  /// @return The merged DebugLoc of branch instructions, or UnknownLoc.
  LLVM_ABI DebugLoc findBranchDebugLoc();

  /// Possible outcome of a register liveness query to computeRegisterLiveness()
  enum LivenessQueryResult {
    LQR_Live,   ///< Register is known to be (at least partially) live.
    LQR_Dead,   ///< Register is known to be fully dead.
    LQR_Unknown ///< Register liveness not decidable from local neighborhood.
  };

  /// Return whether physical register \p Reg is live just before \p Before.
  ///
  /// Search is localised to a neighborhood of \p Neighborhood instructions
  /// before (searching for defs or kills) and \p Neighborhood instructions
  /// after (searching just for defs) \p Before.
  ///
  /// \p Reg must be a physical register.
  /// @param TRI Target register info used for alias queries.
  /// @param Reg Physical register whose liveness is queried.
  /// @param Before Position just after which liveness is evaluated.
  /// @param Neighborhood How many instructions to search before/after.
  /// @return Whether the register is live, dead, or unknown.
  LLVM_ABI LivenessQueryResult computeRegisterLiveness(
      const TargetRegisterInfo *TRI, MCRegister Reg, const_iterator Before,
      unsigned Neighborhood = 10) const;

  // Debugging methods.
  /// Dump this machine basic block to stderr for debugging.
  LLVM_ABI void dump() const;
  /// Print this basic block to \p OS, optionally using slot indexes.
  /// @param OS Output stream.
  /// @param Indexes Optional slot indexes for printing.
  /// @param IsStandalone Whether to print as a standalone block.
  LLVM_ABI void print(raw_ostream &OS, const SlotIndexes *Indexes = nullptr,
                      bool IsStandalone = true) const;
  /// Print this basic block to \p OS using \p MST, optionally with slot indexes.
  /// @param OS Output stream.
  /// @param MST Module slot tracker for IR name resolution.
  /// @param Indexes Optional slot indexes for printing.
  /// @param IsStandalone Whether to print as a standalone block.
  LLVM_ABI void print(raw_ostream &OS, ModuleSlotTracker &MST,
                      const SlotIndexes *Indexes = nullptr,
                      bool IsStandalone = true) const;

  /// Bit flags selecting optional details for printName.
  enum PrintNameFlag {
    PrintNameIr = (1 << 0), ///< Add IR name where available
    PrintNameAttributes = (1 << 1), ///< Print attributes
  };

  /// Print this block's name to \p os with optional IR/attribute details.
  /// @param os Output stream.
  /// @param printNameFlags Bitmask of PrintNameFlag values.
  /// @param moduleSlotTracker Optional tracker for IR name resolution.
  LLVM_ABI void printName(raw_ostream &os,
                          unsigned printNameFlags = PrintNameIr,
                          ModuleSlotTracker *moduleSlotTracker = nullptr) const;

  /// Print this block as a MIR operand (for example, \c %bb.N).
  ///
  /// Used by loop analyses and other MIR printers that refer to blocks by name.
  /// @param OS Output stream.
  /// @param PrintType Whether to print an optional type annotation.
  LLVM_ABI void printAsOperand(raw_ostream &OS, bool PrintType = true) const;

  /// MachineBasicBlocks are uniquely numbered at the function level, unless
  /// they're not in a MachineFunction yet, in which case this will return -1.
  /// @return The dense function-level block number, or -1 if unset.
  int getNumber() const { return Number; }
  /// Set this block's dense function-level number to \p N.
  /// @param N New block number, or -1 if not yet numbered.
  void setNumber(int N) { Number = N; }

  /// For analyses, blocks have a more stable number.
  /// @return The analysis-stable block number.
  int getAnalysisNumber() const { return AnalysisNumber; }
  /// Set this block's analysis number to \p N.
  /// @param N New analysis number.
  void setAnalysisNumber(int N) { AnalysisNumber = N; }

  /// Return the call frame size on entry to this basic block.
  /// @return The call frame size in bytes on entry to this block.
  unsigned getCallFrameSize() const { return CallFrameSize; }
  /// Set the call frame size on entry to this basic block.
  /// @param N Call frame size in bytes on entry to this block.
  void setCallFrameSize(unsigned N) { CallFrameSize = N; }

  /// Return the MCSymbol for this basic block.
  /// @return The MCSymbol for this basic block.
  LLVM_ABI MCSymbol *getSymbol() const;

  /// Return the Windows EH Continuation Symbol for this basic block.
  /// @return The Windows EH Continuation Symbol for this basic block.
  LLVM_ABI MCSymbol *getEHContSymbol() const;

  /// Return the optional irreducible loop header weight for this block.
  /// @return The irreducible loop header weight, if present.
  std::optional<uint64_t> getIrrLoopHeaderWeight() const {
    return IrrLoopHeaderWeight;
  }

  /// Record the irreducible loop header weight copied from the IR basic block.
  /// @param Weight Irreducible loop header weight to store.
  void setIrrLoopHeaderWeight(uint64_t Weight) {
    IrrLoopHeaderWeight = Weight;
  }

  /// Return the probability of the CFG edge from this block to \p Succ.
  ///
  /// This method should NOT be called directly; use
  /// MachineBranchProbabilityInfo::getEdgeProbability instead.
  /// @param Succ Iterator identifying the successor edge.
  /// @return The branch probability of the successor edge.
  LLVM_ABI BranchProbability getSuccProbability(const_succ_iterator Succ) const;

  /// Return true if successor probabilities match the equal-probability
  /// default, so MIR printing can omit them.
  /// @return True if successor probabilities match the equal-probability default, so MIR printing can omit them.
  LLVM_ABI bool canPredictBranchProbabilities() const;

  /// Remove all PHI incoming values that refer to predecessor \p PredMBB.
  ///
  /// Method does not erase PHI instructions even if they have single income or
  /// do not have incoming values at all. It is a caller responsibility to make
  /// decision how to process PHI instructions after incoming values removal.
  /// @param PredMBB Predecessor whose incoming PHI values should be removed.
  LLVM_ABI void
  removePHIsIncomingValuesForPredecessor(const MachineBasicBlock &PredMBB);

private:
  /// Return probability iterator corresponding to the I successor iterator.
  probability_iterator getProbabilityIterator(succ_iterator I);
  const_probability_iterator
  getProbabilityIterator(const_succ_iterator I) const;

  /// Friend so MachineBranchProbabilityInfo can access private probability
  /// iterators when computing edge weights.
  friend class MachineBranchProbabilityInfo;

  // Methods used to maintain doubly linked list of blocks...
  friend struct ilist_callback_traits<MachineBasicBlock>;

  // Machine-CFG mutators

  /// Add Pred as a predecessor of this MachineBasicBlock. Don't do this
  /// unless you know what you're doing, because it doesn't update Pred's
  /// successors list. Use Pred->addSuccessor instead.
  void addPredecessor(MachineBasicBlock *Pred);

  /// Remove Pred as a predecessor of this MachineBasicBlock. Don't do this
  /// unless you know what you're doing, because it doesn't update Pred's
  /// successors list. Use Pred->removeSuccessor instead.
  void removePredecessor(MachineBasicBlock *Pred);
};

/// Write machine basic block \p MBB to stream \p OS.
/// @param OS Output stream.
/// @param MBB Machine basic block to print.
/// @return The output stream \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const MachineBasicBlock &MBB);

/// Print a machine basic block reference for use in diagnostics and dumps.
///
/// The format is:
///   %bb.5           - a machine basic block with MBB.getNumber() == 5.
///
/// Usage: OS << printMBBReference(MBB) << '\n';
/// @param MBB Machine basic block to format as a printable reference.
/// @return A printable reference to the machine basic block.
LLVM_ABI Printable printMBBReference(const MachineBasicBlock &MBB);

/// Map a MachineBasicBlock pointer to its dense block number for IndexedMap.
struct MBB2NumberFunctor {
  /// Machine basic block pointer used as the IndexedMap key.
  using argument_type = const MachineBasicBlock *;
  /// Return the dense number of machine basic block \p MBB.
  /// @param MBB Machine basic block whose number is requested.
  /// @return The dense number of \p MBB.
  unsigned operator()(const MachineBasicBlock *MBB) const {
    return MBB->getNumber();
  }
};

//===--------------------------------------------------------------------===//
// GraphTraits specializations for machine basic block graphs (machine-CFGs)
//===--------------------------------------------------------------------===//

/// GraphTraits specialization treating a MachineBasicBlock as a CFG node via
/// successors.
template <> struct GraphTraits<MachineBasicBlock *> {
  /// Graph node type for a mutable machine basic block pointer.
  using NodeRef = MachineBasicBlock *;
  /// Iterator over successor machine basic blocks of a CFG node.
  using ChildIteratorType = MachineBasicBlock::succ_iterator;

  /// Return \p BB as the graph entry node.
  /// @param BB Machine basic block used as the entry node.
  /// @return The graph entry node.
  static NodeRef getEntryNode(MachineBasicBlock *BB) { return BB; }
  /// Return the begin iterator over successors of \p N.
  /// @param N Machine basic block whose successors are walked.
  /// @return Begin iterator over adjacent CFG nodes.
  static ChildIteratorType child_begin(NodeRef N) { return N->succ_begin(); }
  /// Return the end iterator over successors of \p N.
  /// @param N Machine basic block whose successors are walked.
  /// @return End iterator over adjacent CFG nodes.
  static ChildIteratorType child_end(NodeRef N) { return N->succ_end(); }

  /// Return the analysis number of machine basic block \p BB.
  /// @param BB Machine basic block whose number is requested.
  /// @return The analysis number of \p BB.
  static unsigned getNumber(MachineBasicBlock *BB) {
    assert(BB->getAnalysisNumber() >= 0 && "negative block number");
    return BB->getAnalysisNumber();
  }
};

static_assert(GraphHasNodeNumbers<MachineBasicBlock *>,
              "GraphTraits getNumber() not detected");

/// GraphTraits specialization treating a const MachineBasicBlock as a CFG node
/// via successors.
template <> struct GraphTraits<const MachineBasicBlock *> {
  /// Graph node type for a const machine basic block pointer.
  using NodeRef = const MachineBasicBlock *;
  /// Const iterator over successor machine basic blocks of a CFG node.
  using ChildIteratorType = MachineBasicBlock::const_succ_iterator;

  /// Return \p BB as the graph entry node.
  /// @param BB Machine basic block used as the entry node.
  /// @return The graph entry node.
  static NodeRef getEntryNode(const MachineBasicBlock *BB) { return BB; }
  /// Return the begin iterator over successors of \p N.
  /// @param N Machine basic block whose successors are walked.
  /// @return Begin iterator over adjacent CFG nodes.
  static ChildIteratorType child_begin(NodeRef N) { return N->succ_begin(); }
  /// Return the end iterator over successors of \p N.
  /// @param N Machine basic block whose successors are walked.
  /// @return End iterator over adjacent CFG nodes.
  static ChildIteratorType child_end(NodeRef N) { return N->succ_end(); }

  /// Return the analysis number of machine basic block \p BB.
  /// @param BB Machine basic block whose number is requested.
  /// @return The analysis number of \p BB.
  static unsigned getNumber(const MachineBasicBlock *BB) {
    assert(BB->getAnalysisNumber() >= 0 && "negative block number");
    return BB->getAnalysisNumber();
  }
};

static_assert(GraphHasNodeNumbers<const MachineBasicBlock *>,
              "GraphTraits getNumber() not detected");

/// GraphTraits specialization walking a MachineBasicBlock CFG in inverse order
/// via predecessors.
template <> struct GraphTraits<Inverse<MachineBasicBlock*>> {
  /// Graph node type for a mutable machine basic block pointer.
  using NodeRef = MachineBasicBlock *;
  /// Iterator over predecessor machine basic blocks of a CFG node.
  using ChildIteratorType = MachineBasicBlock::pred_iterator;

  /// Return the wrapped machine basic block as the inverse-graph entry node.
  /// @param G Inverse wrapper around the entry machine basic block.
  /// @return The graph entry node.
  static NodeRef getEntryNode(Inverse<MachineBasicBlock *> G) {
    return G.Graph;
  }

  /// Return the begin iterator over predecessors of \p N.
  /// @param N Machine basic block whose predecessors are walked.
  /// @return Begin iterator over adjacent CFG nodes.
  static ChildIteratorType child_begin(NodeRef N) { return N->pred_begin(); }
  /// Return the end iterator over predecessors of \p N.
  /// @param N Machine basic block whose predecessors are walked.
  /// @return End iterator over adjacent CFG nodes.
  static ChildIteratorType child_end(NodeRef N) { return N->pred_end(); }

  /// Return the analysis number of machine basic block \p BB.
  /// @param BB Machine basic block whose number is requested.
  /// @return The analysis number of \p BB.
  static unsigned getNumber(MachineBasicBlock *BB) {
    assert(BB->getAnalysisNumber() >= 0 && "negative block number");
    return BB->getAnalysisNumber();
  }
};

static_assert(GraphHasNodeNumbers<Inverse<MachineBasicBlock *>>,
              "GraphTraits getNumber() not detected");

/// GraphTraits specialization walking a const MachineBasicBlock CFG in inverse
/// order via predecessors.
template <> struct GraphTraits<Inverse<const MachineBasicBlock*>> {
  /// Graph node type for a const machine basic block pointer.
  using NodeRef = const MachineBasicBlock *;
  /// Const iterator over predecessor machine basic blocks of a CFG node.
  using ChildIteratorType = MachineBasicBlock::const_pred_iterator;

  /// Return the wrapped machine basic block as the inverse-graph entry node.
  /// @param G Inverse wrapper around the entry machine basic block.
  /// @return The graph entry node.
  static NodeRef getEntryNode(Inverse<const MachineBasicBlock *> G) {
    return G.Graph;
  }

  /// Return the begin iterator over predecessors of \p N.
  /// @param N Machine basic block whose predecessors are walked.
  /// @return Begin iterator over adjacent CFG nodes.
  static ChildIteratorType child_begin(NodeRef N) { return N->pred_begin(); }
  /// Return the end iterator over predecessors of \p N.
  /// @param N Machine basic block whose predecessors are walked.
  /// @return End iterator over adjacent CFG nodes.
  static ChildIteratorType child_end(NodeRef N) { return N->pred_end(); }

  /// Return the analysis number of machine basic block \p BB.
  /// @param BB Machine basic block whose number is requested.
  /// @return The analysis number of \p BB.
  static unsigned getNumber(const MachineBasicBlock *BB) {
    assert(BB->getAnalysisNumber() >= 0 && "negative block number");
    return BB->getAnalysisNumber();
  }
};

static_assert(GraphHasNodeNumbers<Inverse<const MachineBasicBlock *>>,
              "GraphTraits getNumber() not detected");

/// Return the CFG successors of machine basic block \p BB.
/// @param BB Machine basic block whose successors are returned.
/// @return The CFG successors of machine basic block \p BB.
inline auto successors(const MachineBasicBlock *BB) { return BB->successors(); }
/// Return the CFG predecessors of \p BB.
/// @param BB Machine basic block whose predecessors are returned.
/// @return The CFG predecessors of \p BB.
inline auto predecessors(const MachineBasicBlock *BB) {
  return BB->predecessors();
}
/// Return the number of CFG successors of \p BB.
/// @param BB Machine basic block whose successor count is requested.
/// @return The number of CFG successors of \p BB.
inline auto succ_size(const MachineBasicBlock *BB) { return BB->succ_size(); }
/// Return the number of CFG predecessors of \p BB.
/// @param BB Machine basic block whose predecessor count is requested.
/// @return The number of CFG predecessors of \p BB.
inline auto pred_size(const MachineBasicBlock *BB) { return BB->pred_size(); }
/// Return an iterator to the first CFG successor of \p BB.
/// @param BB Machine basic block whose successors are iterated.
/// @return An iterator to the first CFG successor of \p BB.
inline auto succ_begin(const MachineBasicBlock *BB) { return BB->succ_begin(); }
/// Return an iterator to the first CFG predecessor of \p BB.
/// @param BB Machine basic block whose predecessors are iterated.
/// @return An iterator to the first CFG predecessor of \p BB.
inline auto pred_begin(const MachineBasicBlock *BB) { return BB->pred_begin(); }
/// Return the end iterator for CFG successors of \p BB.
/// @param BB Machine basic block whose successor end is requested.
/// @return The end iterator for CFG successors of \p BB.
inline auto succ_end(const MachineBasicBlock *BB) { return BB->succ_end(); }
/// Return the end iterator for CFG predecessors of \p BB.
/// @param BB Machine basic block whose predecessor end is requested.
/// @return The end iterator for CFG predecessors of \p BB.
inline auto pred_end(const MachineBasicBlock *BB) { return BB->pred_end(); }

/// Iteration range covering an instruction and later insertions around it.
///
/// Provides an interface to get an iteration range containing the instruction
/// it was initialized with, along with all those instructions inserted prior
/// to or following that instruction at some point after the MachineInstrSpan
/// is constructed.
class MachineInstrSpan {
  MachineBasicBlock &MBB;
  MachineBasicBlock::iterator I, B, E;

public:
  /// Construct a span around instruction \p I in basic block \p BB.
  /// @param I Instruction that anchors the span.
  /// @param BB Machine basic block that owns \p I.
  MachineInstrSpan(MachineBasicBlock::iterator I, MachineBasicBlock *BB)
      : MBB(*BB), I(I), B(I == MBB.begin() ? MBB.end() : std::prev(I)),
        E(std::next(I)) {
    assert(I == BB->end() || I->getParent() == BB);
  }

  /// Return an iterator to the first instruction currently in the span.
  /// @return An iterator to the first instruction currently in the span.
  MachineBasicBlock::iterator begin() {
    return B == MBB.end() ? MBB.begin() : std::next(B);
  }
  /// Return an iterator past the last instruction currently in the span.
  /// @return An iterator past the last instruction currently in the span.
  MachineBasicBlock::iterator end() { return E; }
  /// Return true if the span currently contains no instructions.
  /// @return True if the span currently contains no instructions.
  bool empty() { return begin() == end(); }

  /// Return the iterator to the instruction that originally anchored the span.
  /// @return Iterator to the instruction that originally anchored the span.
  MachineBasicBlock::iterator getInitial() { return I; }
};

/// Advance \p It past debug (and optionally pseudo-probe) instructions.
///
/// Increment \p It until it points to a non-debug instruction or to \p End and
/// return the resulting iterator. This function should only be used with
/// MachineBasicBlock::{iterator, const_iterator, instr_iterator,
/// const_instr_iterator} and the respective reverse iterators.
/// @param It Iterator to advance from.
/// @param End Past-the-end iterator that bounds the search.
/// @param SkipPseudoOp If true, also skip pseudo-probe instructions.
/// @return Iterator to the next non-debug instruction, or \p End.
template <typename IterT>
inline IterT skipDebugInstructionsForward(IterT It, IterT End,
                                          bool SkipPseudoOp = true) {
  while (It != End &&
         (It->isDebugInstr() || (SkipPseudoOp && It->isPseudoProbe())))
    ++It;
  return It;
}

/// Retreat \p It past debug (and optionally pseudo-probe) instructions.
///
/// Decrement \p It until it points to a non-debug instruction or to \p Begin
/// and return the resulting iterator. This function should only be used with
/// MachineBasicBlock::{iterator, const_iterator, instr_iterator,
/// const_instr_iterator} and the respective reverse iterators.
/// @param It Iterator to retreat from.
/// @param Begin Begin iterator that bounds the search.
/// @param SkipPseudoOp If true, also skip pseudo-probe instructions.
/// @return Iterator to the previous non-debug instruction, or \p Begin.
template <class IterT>
inline IterT skipDebugInstructionsBackward(IterT It, IterT Begin,
                                           bool SkipPseudoOp = true) {
  while (It != Begin &&
         (It->isDebugInstr() || (SkipPseudoOp && It->isPseudoProbe())))
    --It;
  return It;
}

/// Move to the next non-debug instruction after \p It.
///
/// Increment \p It, then continue incrementing it while it points to a debug
/// instruction. A replacement for std::next.
/// @param It Iterator to step from.
/// @param End Past-the-end iterator that bounds the search.
/// @param SkipPseudoOp If true, also skip pseudo-probe instructions.
/// @return Iterator to the next non-debug instruction after \p It.
template <typename IterT>
inline IterT next_nodbg(IterT It, IterT End, bool SkipPseudoOp = true) {
  return skipDebugInstructionsForward(std::next(It), End, SkipPseudoOp);
}

/// Move to the previous non-debug instruction before \p It.
///
/// Decrement \p It, then continue decrementing it while it points to a debug
/// instruction. A replacement for std::prev.
/// @param It Iterator to step from.
/// @param Begin Begin iterator that bounds the search.
/// @param SkipPseudoOp If true, also skip pseudo-probe instructions.
/// @return Iterator to the previous non-debug instruction before \p It.
template <typename IterT>
inline IterT prev_nodbg(IterT It, IterT Begin, bool SkipPseudoOp = true) {
  return skipDebugInstructionsBackward(std::prev(It), Begin, SkipPseudoOp);
}

/// Return a range from \p It to \p End that skips debug instructions.
///
/// Construct a range iterator which begins at \p It and moves forwards until
/// \p End is reached, skipping any debug instructions.
/// @param It Begin iterator of the filtered range.
/// @param End Past-the-end iterator of the filtered range.
/// @param SkipPseudoOp If true, also skip pseudo-probe instructions.
/// @return A filtered range from \p It to \p End skipping debug instructions.
template <typename IterT>
inline auto instructionsWithoutDebug(IterT It, IterT End,
                                     bool SkipPseudoOp = true) {
  return make_filter_range(make_range(It, End), [=](const MachineInstr &MI) {
    return !MI.isDebugInstr() && !(SkipPseudoOp && MI.isPseudoProbe());
  });
}

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEBASICBLOCK_H
