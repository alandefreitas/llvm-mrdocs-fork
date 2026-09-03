//===- llvm/BasicBlock.h - Represent a basic block in the VM ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the BasicBlock class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_BASICBLOCK_H
#define LLVM_IR_BASICBLOCK_H

#include "llvm-c/Types.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <iterator>

namespace llvm {

class AssemblyAnnotationWriter;
class CallInst;
class DataLayout;
class Function;
class LandingPadInst;
class LLVMContext;
class Module;
class PHINode;
class ValueSymbolTable;
class DbgVariableRecord;
class DbgMarker;

/// LLVM Basic Block Representation
///
/// This represents a single basic block in LLVM. A basic block is simply a
/// container of instructions that execute sequentially. Basic blocks are Values
/// because they are referenced by instructions such as branches and switch
/// tables. The type of a BasicBlock is "Type::LabelTy" because the basic block
/// represents a label to which a branch can jump.
///
/// A well formed basic block is formed of a list of non-terminating
/// instructions followed by a single terminator instruction. Terminator
/// instructions may not occur in the middle of basic blocks, and must terminate
/// the blocks. The BasicBlock class allows malformed basic blocks to occur
/// because it may be useful in the intermediate stage of constructing or
/// modifying a program. However, the verifier will ensure that basic blocks are
/// "well formed".
class BasicBlock final : public Value, // Basic blocks are data objects also
                         public ilist_node_with_parent<BasicBlock, Function> {
public:
  /// Instruction list type used to store this block's instructions.
  using InstListType = SymbolTableList<Instruction, ilist_iterator_bits<true>,
                                       ilist_parent<BasicBlock>>;

private:
  // Allow Function to renumber blocks.
  friend class Function;
  /// Per-function unique number.
  unsigned Number = ~0u;

  friend class BlockAddress;
  friend class SymbolTableListTraits<BasicBlock>;

  InstListType InstList;
  Function *Parent;

public:
  /// Attach a DbgMarker to the given instruction. Enables the storage of any
  /// debug-info at this position in the program.
  /// \param I Instruction to attach a DbgMarker to.
  /// \return The DbgMarker attached at \p I.
  LLVM_ABI DbgMarker *createMarker(Instruction *I);
  /// Attach a \c DbgMarker before the instruction at \p It in this block's
  /// instruction list.
  /// \param It Iterator position at which to create the marker.
  /// \return The DbgMarker at the given iterator position.
  LLVM_ABI DbgMarker *createMarker(InstListType::iterator It);

  /// Convert dbg.value intrinsics into DbgMarkers / DbgRecords.
  ///
  /// Convert variable location debugging information stored in dbg.value
  /// intrinsics into DbgMarkers / DbgRecords. Deletes all dbg.values in
  /// the process and sets IsNewDbgInfoFormat = true.
  LLVM_ABI void convertToNewDbgValues();

  /// Convert DbgMarkers / DbgRecords back into dbg.value intrinsics.
  ///
  /// Convert variable location debugging information stored in DbgMarkers and
  /// DbgRecords into the dbg.value intrinsic representation. Sets
  /// IsNewDbgInfoFormat = false.
  LLVM_ABI void convertFromNewDbgValues();

  /// Return this block's unique number within its parent function.
  /// \return This block's per-function unique number.
  unsigned getNumber() const {
    assert(getParent() && "only basic blocks in functions have valid numbers");
    return Number;
  }

  /// Record that \p M holds DbgRecords trailing after this block's last instruction.
  ///
  /// These are equivalent to dbg.value intrinsics that exist at the end of a
  /// basic block with no terminator (a transient state that occurs regularly).
  /// \param M Marker owning the trailing DbgRecords.
  LLVM_ABI void setTrailingDbgRecords(DbgMarker *M);

  /// Fetch the collection of DbgRecords that "trail" after the last instruction
  /// of this block, see \ref setTrailingDbgRecords. If there are none, returns
  /// nullptr.
  /// \return The trailing DbgMarker, or nullptr if there are none.
  LLVM_ABI DbgMarker *getTrailingDbgRecords();

  /// Delete any trailing DbgRecords at the end of this block, see
  /// \ref setTrailingDbgRecords.
  LLVM_ABI void deleteTrailingDbgRecords();

  /// Dump DbgMarkers attached to instructions in this block to the debug stream.
  LLVM_ABI void dumpDbgValues() const;

  /// Return the DbgMarker for the position given by \p It.
  ///
  /// This allows DbgRecords to be inserted there. The result is either nullptr
  /// if not present, a DbgMarker, or TrailingDbgRecords if It is end().
  /// \param It Instruction-list iterator identifying the marker position.
  /// \return The DbgMarker at \p It, TrailingDbgRecords if \p It is end(), or
  /// nullptr if none is present.
  LLVM_ABI DbgMarker *getMarker(InstListType::iterator It);

  /// Return the DbgMarker for the position that comes after \p I. \see
  /// BasicBlock::getMarker, this can be nullptr, a DbgMarker, or
  /// TrailingDbgRecords if there is no next instruction.
  /// \param I Instruction whose following marker position is requested.
  /// \return The DbgMarker after \p I, TrailingDbgRecords, or nullptr.
  LLVM_ABI DbgMarker *getNextMarker(Instruction *I);

  /// Insert a DbgRecord into a block at the position given by \p I.
  /// \param DR DbgRecord to insert.
  /// \param I Instruction after which to insert \p DR.
  LLVM_ABI void insertDbgRecordAfter(DbgRecord *DR, Instruction *I);

  /// Insert a DbgRecord into a block at the position given by \p Here.
  /// \param DR DbgRecord to insert.
  /// \param Here Iterator position at which to insert \p DR.
  LLVM_ABI void insertDbgRecordBefore(DbgRecord *DR,
                                      InstListType::iterator Here);

  /// Eject any debug-info trailing at the end of a block.
  ///
  /// DbgRecords can transiently be located "off the end" of a block if the
  /// block's terminator is temporarily removed. Once a terminator is
  /// re-inserted this method will move such DbgRecords back to the right place
  /// (ahead of the terminator).
  LLVM_ABI void flushTerminatorDbgRecords();

  /// Re-attach debug records when re-inserting a speculatively removed instruction.
  ///
  /// In rare circumstances instructions can be speculatively removed from
  /// blocks, and then be re-inserted back into that position later. When this
  /// happens in RemoveDIs debug-info mode, some special patching-up needs to
  /// occur: inserting into the middle of a sequence of dbg.value intrinsics
  /// does not have an equivalent with DbgRecords.
  /// \param I Instruction being re-inserted.
  /// \param Pos Optional iterator into the DbgRecord sequence at the insert point.
  LLVM_ABI void
  reinsertInstInDbgRecords(Instruction *I,
                           std::optional<DbgRecord::self_iterator> Pos);

private:
  void setParent(Function *parent);

  /// Constructor.
  ///
  /// If the function parameter is specified, the basic block is automatically
  /// inserted at either the end of the function (if InsertBefore is null), or
  /// before the specified basic block.
  LLVM_ABI explicit BasicBlock(LLVMContext &C, const Twine &Name = "",
                               Function *Parent = nullptr,
                               BasicBlock *InsertBefore = nullptr);

public:
  /// Basic blocks are not copy-constructible.
  /// \param Unused Unused copy source (deleted).
  BasicBlock(const BasicBlock &Unused) = delete;
  /// Basic blocks are not copy-assignable.
  /// \param Unused Unused copy source (deleted).
  BasicBlock &operator=(const BasicBlock &Unused) = delete;
  /// Destroy this basic block and its instructions.
  LLVM_ABI ~BasicBlock();

  /// Get the context in which this basic block lives.
  /// \return The LLVMContext for this basic block.
  LLVM_ABI LLVMContext &getContext() const;

  /// Instruction iterators...
  using iterator = InstListType::iterator;
  /// Const iterator over instructions in this basic block.
  using const_iterator = InstListType::const_iterator;
  /// Reverse iterator over instructions in this basic block.
  using reverse_iterator = InstListType::reverse_iterator;
  /// Const reverse iterator over instructions in this basic block.
  using const_reverse_iterator = InstListType::const_reverse_iterator;

  // These functions and classes need access to the instruction list.
  friend void Instruction::removeFromParent();
  friend BasicBlock::iterator Instruction::eraseFromParent();
  friend BasicBlock::iterator Instruction::insertInto(BasicBlock *BB,
                                                      BasicBlock::iterator It);
  friend class llvm::SymbolTableListTraits<
      llvm::Instruction, ilist_iterator_bits<true>, ilist_parent<BasicBlock>>;
  friend class llvm::ilist_node_with_parent<llvm::Instruction, llvm::BasicBlock,
                                            ilist_iterator_bits<true>,
                                            ilist_parent<BasicBlock>>;

  // Friendly methods that need to access us for the maintenence of
  // debug-info attachments.
  friend void Instruction::insertBefore(BasicBlock::iterator InsertPos);
  friend void Instruction::insertAfter(Instruction *InsertPos);
  friend void Instruction::insertAfter(BasicBlock::iterator InsertPos);
  friend void Instruction::insertBefore(BasicBlock &BB,
                                        InstListType::iterator InsertPos);
  friend void Instruction::moveBeforeImpl(BasicBlock &BB,
                                          InstListType::iterator I,
                                          bool Preserve);
  friend iterator_range<DbgRecord::self_iterator>
  Instruction::cloneDebugInfoFrom(
      const Instruction *From, std::optional<DbgRecord::self_iterator> FromHere,
      bool InsertAtHead);

  /// Creates a new BasicBlock.
  ///
  /// If the Parent parameter is specified, the basic block is automatically
  /// inserted at either the end of the function (if InsertBefore is 0), or
  /// before the specified basic block.
  /// \param Context LLVM context in which to create the block.
  /// \param Name Optional name for the new basic block.
  /// \param Parent Optional function to insert the block into.
  /// \param InsertBefore Optional insertion point within \p Parent.
  /// \return The newly created basic block.
  static BasicBlock *Create(LLVMContext &Context, const Twine &Name = "",
                            Function *Parent = nullptr,
                            BasicBlock *InsertBefore = nullptr) {
    return new BasicBlock(Context, Name, Parent, InsertBefore);
  }

  /// Return the enclosing method, or null if none.
  /// \return The parent Function, or nullptr if unlinked.
  const Function *getParent() const { return Parent; }
  /// Return the enclosing method, or null if none.
  /// \return The parent Function, or nullptr if unlinked.
  Function *getParent() { return Parent; }

  /// Return the module owning the function this basic block belongs to, or
  /// nullptr if the function does not have a module.
  ///
  /// Note: this is undefined behavior if the block does not have a parent.
  /// \return The parent Module, or nullptr if the function has none.
  LLVM_ABI const Module *getModule() const;
  /// Return the module owning the function this basic block belongs to.
  /// \return The parent Module, or nullptr if the function has none.
  Module *getModule() {
    return const_cast<Module *>(
        static_cast<const BasicBlock *>(this)->getModule());
  }

  /// Get the data layout of the module this basic block belongs to.
  ///
  /// Requires the basic block to have a parent module.
  /// \return The DataLayout of the parent module.
  LLVM_ABI const DataLayout &getDataLayout() const;

  /// Returns whether the block has a terminator.
  /// \return True if the last instruction is a terminator.
  bool hasTerminator() const LLVM_READONLY {
    return !InstList.empty() && InstList.back().isTerminator();
  }

  /// Returns the terminator instruction; assumes that the block is well-formed.
  /// \return The terminating instruction of this block.
  const Instruction *getTerminator() const LLVM_READONLY {
    assert(hasTerminator() && "cannot get terminator of non-well-formed block");
    return &InstList.back();
  }
  /// Returns the terminator instruction; assumes that the block is well-formed.
  /// \return The terminating instruction of this block.
  Instruction *getTerminator() {
    return const_cast<Instruction *>(
        static_cast<const BasicBlock *>(this)->getTerminator());
  }

  /// Returns the terminator instruction if the block is well formed or
  /// null if the block is not well formed.
  /// \return The terminator, or nullptr if the block is not well formed.
  const Instruction *getTerminatorOrNull() const LLVM_READONLY {
    return hasTerminator() ? getTerminator() : nullptr;
  }
  /// Returns the terminator instruction if well formed, or null otherwise.
  /// \return The terminator, or nullptr if the block is not well formed.
  Instruction *getTerminatorOrNull() {
    return hasTerminator() ? getTerminator() : nullptr;
  }

  /// Return the \@llvm.experimental.deoptimize call before the terminator.
  ///
  /// Looks for a call to \@llvm.experimental.deoptimize prior to the
  /// terminating return instruction of this basic block, if such a call is
  /// present. Otherwise, returns null.
  /// \return The terminating deoptimize call, or nullptr if none is present.
  LLVM_ABI const CallInst *getTerminatingDeoptimizeCall() const;
  /// Returns the \@llvm.experimental.deoptimize call before this block's
  /// terminating return, or null if none is present.
  /// \return The terminating deoptimize call, or nullptr if none is present.
  CallInst *getTerminatingDeoptimizeCall() {
    return const_cast<CallInst *>(
        static_cast<const BasicBlock *>(this)->getTerminatingDeoptimizeCall());
  }

  /// Return a postdominating \@llvm.experimental.deoptimize call, if any.
  ///
  /// Returns the call instruction calling \@llvm.experimental.deoptimize that
  /// is present either in the current basic block or in a block that is a
  /// unique successor to the current block, if such a call is present.
  /// Otherwise, returns null.
  /// \return The postdominating deoptimize call, or nullptr if none is present.
  LLVM_ABI const CallInst *getPostdominatingDeoptimizeCall() const;
  /// Return a postdominating \@llvm.experimental.deoptimize call, if any.
  /// \return The postdominating deoptimize call, or nullptr if none is present.
  CallInst *getPostdominatingDeoptimizeCall() {
    return const_cast<CallInst *>(static_cast<const BasicBlock *>(this)
                                      ->getPostdominatingDeoptimizeCall());
  }

  /// Return the musttail call before this block's terminating return, if any.
  ///
  /// Returns the call instruction marked 'musttail' prior to the terminating
  /// return instruction of this basic block, if such a call is present.
  /// Otherwise, returns null.
  /// \return The terminating musttail call, or nullptr if none is present.
  LLVM_ABI const CallInst *getTerminatingMustTailCall() const;
  /// Return the musttail call before this block's terminating return, if any.
  /// \return The terminating musttail call, or nullptr if none is present.
  CallInst *getTerminatingMustTailCall() {
    return const_cast<CallInst *>(
        static_cast<const BasicBlock *>(this)->getTerminatingMustTailCall());
  }

  /// Returns an iterator to the first instruction in this block that is not a
  /// PHINode instruction.
  ///
  /// When adding instructions to the beginning of the basic block, they should
  /// be added before the returned value, not before the first instruction,
  /// which might be PHI. Returns end() if there's no non-PHI instruction.
  ///
  /// Avoid unwrapping the iterator to an Instruction* before inserting here,
  /// as important debug-info is preserved in the iterator.
  /// \return An iterator to the first non-PHI instruction, or end().
  LLVM_ABI InstListType::const_iterator getFirstNonPHIIt() const;
  /// Returns an iterator to the first non-PHI instruction in this block.
  /// \return An iterator to the first non-PHI instruction, or end().
  InstListType::iterator getFirstNonPHIIt() {
    BasicBlock::iterator It =
        static_cast<const BasicBlock *>(this)->getFirstNonPHIIt().getNonConst();
    It.setHeadBit(true);
    return It;
  }

  /// Return an iterator past PHIs and debug intrinsics in this block.
  ///
  /// Skips any pseudo operation as well when \c SkipPseudoOp is true.
  /// \param SkipPseudoOp If true, also skip pseudo operations.
  /// \return An iterator to the first non-PHI, non-debug instruction.
  LLVM_ABI InstListType::const_iterator
  getFirstNonPHIOrDbg(bool SkipPseudoOp = true) const;
  /// Return an iterator past PHIs and debug intrinsics in this block.
  /// \param SkipPseudoOp If true, also skip pseudo operations.
  /// \return An iterator to the first non-PHI, non-debug instruction.
  InstListType::iterator getFirstNonPHIOrDbg(bool SkipPseudoOp = true) {
    return static_cast<const BasicBlock *>(this)
        ->getFirstNonPHIOrDbg(SkipPseudoOp)
        .getNonConst();
  }

  /// Return an iterator past PHIs, debug, and lifetime intrinsics.
  ///
  /// Skips any pseudo operation as well when \c SkipPseudoOp is true.
  /// \param SkipPseudoOp If true, also skip pseudo operations.
  /// \return An iterator past PHIs, debug, and lifetime intrinsics.
  LLVM_ABI InstListType::const_iterator
  getFirstNonPHIOrDbgOrLifetime(bool SkipPseudoOp = true) const;
  /// Returns an iterator to the first non-PHI, non-debug, non-lifetime instruction.
  /// \param SkipPseudoOp If true, also skip pseudo operations.
  /// \return An iterator past PHIs, debug, and lifetime intrinsics.
  InstListType::iterator
  getFirstNonPHIOrDbgOrLifetime(bool SkipPseudoOp = true) {
    return static_cast<const BasicBlock *>(this)
        ->getFirstNonPHIOrDbgOrLifetime(SkipPseudoOp)
        .getNonConst();
  }

  /// Returns an iterator to the first instruction in this block that is
  /// suitable for inserting a non-PHI instruction.
  ///
  /// In particular, it skips all PHIs and LandingPad instructions.
  /// \return An iterator to the first valid non-PHI insertion point.
  LLVM_ABI const_iterator getFirstInsertionPt() const;
  /// Returns an iterator to the first position suitable for a non-PHI insert.
  /// \return An iterator to the first valid non-PHI insertion point.
  iterator getFirstInsertionPt() {
    return static_cast<const BasicBlock *>(this)
        ->getFirstInsertionPt()
        .getNonConst();
  }

  /// Return true if this block has a valid non-PHI insertion point.
  ///
  /// Returns false for blocks that can only contain PHI nodes, such as blocks
  /// with a catchswitch terminator.
  ///
  /// This is an O(1) check, unlike getFirstInsertionPt() which must scan
  /// through all PHI nodes.
  /// \return True if a non-PHI instruction may be inserted in this block.
  bool hasInsertionPt() const {
    const Instruction *Term = getTerminator();
    return Term && Term->getOpcode() != Instruction::CatchSwitch;
  }

  /// Returns an iterator to the first instruction in this block that is
  /// not a PHINode, a debug intrinsic, a static alloca or any pseudo operation.
  /// \return An iterator past PHIs, debug intrinsics, and static allocas.
  LLVM_ABI const_iterator getFirstNonPHIOrDbgOrAlloca() const;
  /// Returns an iterator past PHIs, debug intrinsics, and static allocas.
  /// \return An iterator past PHIs, debug intrinsics, and static allocas.
  iterator getFirstNonPHIOrDbgOrAlloca() {
    return static_cast<const BasicBlock *>(this)
        ->getFirstNonPHIOrDbgOrAlloca()
        .getNonConst();
  }

  /// Return the first instruction that may fault under asynchronous EH.
  ///
  /// Currently it checks for loads/stores (which may dereference a null
  /// pointer) and calls/invokes (which may propagate exceptions).
  /// \return The first may-fault instruction, or nullptr if none.
  LLVM_ABI const Instruction *getFirstMayFaultInst() const;
  /// Returns the first load/store/call/invoke that may fault or throw.
  /// \return The first may-fault instruction, or nullptr if none.
  Instruction *getFirstMayFaultInst() {
    return const_cast<Instruction *>(
        static_cast<const BasicBlock *>(this)->getFirstMayFaultInst());
  }

  /// Unlink 'this' from the containing function, but do not delete it.
  LLVM_ABI void removeFromParent();

  /// Unlink 'this' from the containing function and delete it.
  ///
  /// \return An iterator pointing to the element after the erased one.
  LLVM_ABI SymbolTableList<BasicBlock>::iterator eraseFromParent();

  /// Unlink this basic block from its current function and insert it into
  /// the function that \p MovePos lives in, right before \p MovePos.
  /// \param MovePos Basic block before which to insert this block.
  inline void moveBefore(BasicBlock *MovePos) {
    moveBefore(MovePos->getIterator());
  }
  /// Unlink this block and insert it before \p MovePos in that function's list.
  /// \param MovePos Iterator to the insertion point in the function's block list.
  LLVM_ABI void moveBefore(SymbolTableList<BasicBlock>::iterator MovePos);

  /// Unlink this basic block from its current function and insert it
  /// right after \p MovePos in the function \p MovePos lives in.
  /// \param MovePos Basic block after which to insert this block.
  LLVM_ABI void moveAfter(BasicBlock *MovePos);

  /// Insert unlinked basic block into a function.
  ///
  /// Inserts an unlinked basic block into \c Parent.  If \c InsertBefore is
  /// provided, inserts before that basic block, otherwise inserts at the end.
  ///
  /// \pre \a getParent() is \c nullptr.
  /// \param Parent Function into which to insert this unlinked block.
  /// \param InsertBefore Optional block before which to insert; null appends.
  LLVM_ABI void insertInto(Function *Parent,
                           BasicBlock *InsertBefore = nullptr);

  /// Return the predecessor of this block if it has a single predecessor
  /// block. Otherwise return a null pointer.
  /// \return The sole predecessor, or nullptr if there is not exactly one.
  LLVM_ABI const BasicBlock *getSinglePredecessor() const;
  /// Return the sole predecessor of this block, or null if there is not exactly one.
  /// \return The sole predecessor, or nullptr if there is not exactly one.
  BasicBlock *getSinglePredecessor() {
    return const_cast<BasicBlock *>(
        static_cast<const BasicBlock *>(this)->getSinglePredecessor());
  }

  /// Return the predecessor of this block if it has a unique predecessor
  /// block. Otherwise return a null pointer.
  ///
  /// Note that unique predecessor doesn't mean single edge, there can be
  /// multiple edges from the unique predecessor to this block (for example a
  /// switch statement with multiple cases having the same destination).
  /// \return The unique predecessor, or nullptr if there is not one.
  LLVM_ABI const BasicBlock *getUniquePredecessor() const;
  /// Return the unique predecessor of this block, or null if there is not one.
  /// \return The unique predecessor, or nullptr if there is not one.
  BasicBlock *getUniquePredecessor() {
    return const_cast<BasicBlock *>(
        static_cast<const BasicBlock *>(this)->getUniquePredecessor());
  }

  /// Return true if this block has exactly N predecessors.
  /// \param N Exact predecessor count to test for.
  /// \return True if this block has exactly \p N predecessors.
  LLVM_ABI bool hasNPredecessors(unsigned N) const;

  /// Return true if this block has N predecessors or more.
  /// \param N Minimum predecessor count to test for.
  /// \return True if this block has at least \p N predecessors.
  LLVM_ABI bool hasNPredecessorsOrMore(unsigned N) const;

  /// Return the successor of this block if it has a single successor.
  /// Otherwise return a null pointer.
  ///
  /// This method is analogous to getSinglePredecessor above.
  /// \return The sole successor, or nullptr if there is not exactly one.
  LLVM_ABI const BasicBlock *getSingleSuccessor() const;
  /// Return the sole successor of this block, or null if there is not exactly one.
  /// \return The sole successor, or nullptr if there is not exactly one.
  BasicBlock *getSingleSuccessor() {
    return const_cast<BasicBlock *>(
        static_cast<const BasicBlock *>(this)->getSingleSuccessor());
  }

  /// Return the successor of this block if it has a unique successor.
  /// Otherwise return a null pointer.
  ///
  /// This method is analogous to getUniquePredecessor above.
  /// \return The unique successor, or nullptr if there is not one.
  LLVM_ABI const BasicBlock *getUniqueSuccessor() const;
  /// Return the unique successor of this block, or null if there is not one.
  /// \return The unique successor, or nullptr if there is not one.
  BasicBlock *getUniqueSuccessor() {
    return const_cast<BasicBlock *>(
        static_cast<const BasicBlock *>(this)->getUniqueSuccessor());
  }

  /// Print the basic block to an output stream with an optional
  /// AssemblyAnnotationWriter.
  /// \param OS Stream to print to.
  /// \param AAW Optional annotation writer for assembly comments.
  /// \param ShouldPreserveUseListOrder If true, preserve use-list order in output.
  /// \param IsForDebug If true, include extra debug formatting.
  LLVM_ABI void print(raw_ostream &OS, AssemblyAnnotationWriter *AAW = nullptr,
                      bool ShouldPreserveUseListOrder = false,
                      bool IsForDebug = false) const;

  //===--------------------------------------------------------------------===//
  // Instruction iterator methods
  //===--------------------------------------------------------------------===//

  /// Return an iterator to the first instruction in this block.
  /// \return An iterator to the first instruction.
  inline iterator begin() {
    iterator It = InstList.begin();
    // Set the head-inclusive bit to indicate that this iterator includes
    // any debug-info at the start of the block. This is a no-op unless the
    // appropriate CMake flag is set.
    It.setHeadBit(true);
    return It;
  }
  /// Return a const iterator to the first instruction in this block.
  /// \return A const iterator to the first instruction.
  inline const_iterator begin() const {
    const_iterator It = InstList.begin();
    It.setHeadBit(true);
    return It;
  }
  /// Return an iterator past the last instruction in this block.
  /// \return An iterator past the last instruction.
  inline iterator end() { return InstList.end(); }
  /// Return a const iterator past the last instruction in this block.
  /// \return A const iterator past the last instruction.
  inline const_iterator end() const { return InstList.end(); }

  /// Return a reverse iterator to the last instruction in this block.
  /// \return A reverse iterator to the last instruction.
  inline reverse_iterator rbegin() { return InstList.rbegin(); }
  /// Return a const reverse iterator to the last instruction in this block.
  /// \return A const reverse iterator to the last instruction.
  inline const_reverse_iterator rbegin() const { return InstList.rbegin(); }
  /// Return a reverse iterator past the first instruction in this block.
  /// \return A reverse iterator past the first instruction.
  inline reverse_iterator rend() { return InstList.rend(); }
  /// Return a const reverse iterator past the first instruction in this block.
  /// \return A const reverse iterator past the first instruction.
  inline const_reverse_iterator rend() const { return InstList.rend(); }

  /// Return the number of instructions in this block.
  /// \return The number of instructions in this block.
  inline size_t size() const { return InstList.size(); }
  /// Return true if this block contains no instructions.
  /// \return True if this block contains no instructions.
  inline bool empty() const { return InstList.empty(); }
  /// Return the first instruction in the block.
  /// \return A reference to the first instruction.
  inline const Instruction &front() const { return InstList.front(); }
  /// Return the first instruction in the block.
  /// \return A reference to the first instruction.
  inline Instruction &front() { return InstList.front(); }
  /// Return the last instruction in the block.
  /// \return A reference to the last instruction.
  inline const Instruction &back() const { return InstList.back(); }
  /// Return the last instruction in the block.
  /// \return A reference to the last instruction.
  inline Instruction &back() { return InstList.back(); }

  /// Iterator to walk just the phi nodes in the basic block.
  template <typename PHINodeT = PHINode, typename BBIteratorT = iterator>
  class phi_iterator_impl
      : public iterator_facade_base<phi_iterator_impl<PHINodeT, BBIteratorT>,
                                    std::forward_iterator_tag, PHINodeT> {
    friend BasicBlock;

    PHINodeT *PN;

    phi_iterator_impl(PHINodeT *PN) : PN(PN) {}

  public:
    /// Default-construct an empty iterator that is not associated with a PHI.
    phi_iterator_impl() = default;

    // Allow conversion between instantiations where valid.
    /// Convert from a compatible phi_iterator_impl instantiation.
    /// \param Arg Source iterator whose PHI pointer is adopted.
    template <typename PHINodeU, typename BBIteratorU,
              typename = std::enable_if_t<
                  std::is_convertible<PHINodeU *, PHINodeT *>::value>>
    phi_iterator_impl(const phi_iterator_impl<PHINodeU, BBIteratorU> &Arg)
        : PN(Arg.PN) {}

    /// Return true if both iterators refer to the same PHI node (or both are empty).
    /// \param Arg Other phi iterator to compare against.
    /// \return True if both iterators refer to the same PHI node.
    bool operator==(const phi_iterator_impl &Arg) const { return PN == Arg.PN; }

    /// Dereference to the current PHI node.
    /// \return A reference to the current PHI node.
    PHINodeT &operator*() const { return *PN; }

    /// Expose post-increment from \c iterator_facade_base.
    using phi_iterator_impl::iterator_facade_base::operator++;
    /// Advance to the next PHI node in the block, or to the end.
    /// \return A reference to this iterator after advancing.
    phi_iterator_impl &operator++() {
      assert(PN && "Cannot increment the end iterator!");
      PN = dyn_cast<PHINodeT>(std::next(BBIteratorT(PN)));
      return *this;
    }
  };
  /// Mutable iterator over PHI nodes at the start of a basic block.
  using phi_iterator = phi_iterator_impl<>;
  /// Const iterator over PHI nodes at the start of a basic block.
  using const_phi_iterator =
      phi_iterator_impl<const PHINode, BasicBlock::const_iterator>;

  /// Returns a range that iterates over the phis in the basic block.
  ///
  /// Note that this cannot be used with basic blocks that have no terminator.
  /// \return A range over the PHI nodes in this block.
  iterator_range<const_phi_iterator> phis() const {
    return const_cast<BasicBlock *>(this)->phis();
  }
  /// Returns a range that iterates over the PHI nodes in this basic block.
  /// \return A range over the PHI nodes in this block.
  LLVM_ABI iterator_range<phi_iterator> phis();

private:
  /// Return the underlying instruction list container.
  /// This is deliberately private because we have implemented an adequate set
  /// of functions to modify the list, including BasicBlock::splice(),
  /// BasicBlock::erase(), Instruction::insertInto() etc.
  const InstListType &getInstList() const { return InstList; }
  InstListType &getInstList() { return InstList; }

  /// Returns a pointer to a member of the instruction list.
  /// This is private on purpose, just like `getInstList()`.
  static InstListType BasicBlock::*getSublistAccess(Instruction *) {
    return &BasicBlock::InstList;
  }

  /// Dedicated function for splicing debug-info: when we have an empty
  /// splice (i.e. zero instructions), the caller may still intend any
  /// debug-info in between the two "positions" to be spliced.
  void spliceDebugInfoEmptyBlock(BasicBlock::iterator ToIt, BasicBlock *FromBB,
                                 BasicBlock::iterator FromBeginIt,
                                 BasicBlock::iterator FromEndIt);

  /// Perform any debug-info specific maintenence for the given splice
  /// activity. In the DbgRecord debug-info representation, debug-info is not
  /// in instructions, and so it does not automatically move from one block
  /// to another.
  void spliceDebugInfo(BasicBlock::iterator ToIt, BasicBlock *FromBB,
                       BasicBlock::iterator FromBeginIt,
                       BasicBlock::iterator FromEndIt);
  void spliceDebugInfoImpl(BasicBlock::iterator ToIt, BasicBlock *FromBB,
                           BasicBlock::iterator FromBeginIt,
                           BasicBlock::iterator FromEndIt);

  enum {
    HasAddressTaken = 1 << 0,
    InstrOrderValid = 1 << 1,
  };

  void setHasAddressTaken(bool B) {
    if (B)
      SubclassOptionalData |= HasAddressTaken;
    else
      SubclassOptionalData &= ~HasAddressTaken;
  }

  /// Shadow Value::setValueSubclassData with a private forwarding method so
  /// that any future subclasses cannot accidentally use it.
  void setValueSubclassData(unsigned short D) {
    Value::setValueSubclassData(D);
  }

public:
  /// Returns a pointer to the symbol table if one exists.
  /// \return The value symbol table, or nullptr if none exists.
  LLVM_ABI ValueSymbolTable *getValueSymbolTable();

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param V Value to test for BasicBlock identity.
  /// \return True if \p V is a BasicBlock.
  static bool classof(const Value *V) {
    return V->getValueID() == Value::BasicBlockVal;
  }

  /// Cause all subinstructions to "let go" of all the references that said
  /// subinstructions are maintaining.
  ///
  /// This allows one to 'delete' a whole class at a time, even though there may
  /// be circular references... first all references are dropped, and all use
  /// counts go to zero.  Then everything is delete'd for real.  Note that no
  /// operations are valid on an object that has "dropped all references",
  /// except operator delete.
  LLVM_ABI void dropAllReferences();

  /// Update PHI nodes in this BasicBlock before removal of predecessor \p Pred.
  ///
  /// Note that this function does not actually remove the predecessor.
  ///
  /// If \p KeepOneInputPHIs is true then don't remove PHIs that are left with
  /// zero or one incoming values, and don't simplify PHIs with all incoming
  /// values the same.
  /// \param Pred Predecessor block about to be removed from this block.
  /// \param KeepOneInputPHIs If true, leave trivial single-input PHIs in place.
  LLVM_ABI void removePredecessor(BasicBlock *Pred,
                                  bool KeepOneInputPHIs = false);

  /// Return true if this block's predecessors can be split.
  /// \return True if this block's predecessors can be split.
  LLVM_ABI bool canSplitPredecessors() const;

  /// Split the basic block into two basic blocks at the specified instruction.
  ///
  /// Note that all instructions BEFORE the specified iterator
  /// stay as part of the original basic block, an unconditional branch is added
  /// to the original BB, and the rest of the instructions in the BB are moved
  /// to the new BB, including the old terminator.  The newly formed basic block
  /// is returned. This function invalidates the specified iterator.
  ///
  /// Note that this only works on well formed basic blocks (must have a
  /// terminator), and \p 'I' must not be the end of instruction list (which
  /// would cause a degenerate basic block to be formed, having a terminator
  /// inside of the basic block).
  ///
  /// Also note that this doesn't preserve any passes. To split blocks while
  /// keeping loop information consistent, use the SplitBlock utility function.
  /// \param I Split point; instructions before this stay in the original block.
  /// \param BBName Optional name for the newly created basic block.
  /// \return The newly created basic block containing the split-off instructions.
  LLVM_ABI BasicBlock *splitBasicBlock(iterator I, const Twine &BBName = "");
  /// Split this block at \p I, moving \p I and later instructions to a new block.
  /// \param I Instruction at which to split the block.
  /// \param BBName Optional name for the newly created basic block.
  /// \return The newly created basic block containing the split-off instructions.
  BasicBlock *splitBasicBlock(Instruction *I, const Twine &BBName = "") {
    return splitBasicBlock(I->getIterator(), BBName);
  }

  /// Split the basic block into two basic blocks at the specified instruction
  /// and insert the new basic blocks as the predecessor of the current block.
  ///
  /// This function ensures all instructions AFTER and including the specified
  /// iterator \p I are part of the original basic block. All Instructions
  /// BEFORE the iterator \p I are moved to the new BB and an unconditional
  /// branch is added to the new BB. The new basic block is returned.
  ///
  /// Note that this only works on well formed basic blocks (must have a
  /// terminator), and \p 'I' must not be the end of instruction list (which
  /// would cause a degenerate basic block to be formed, having a terminator
  /// inside of the basic block).  \p 'I' cannot be a iterator for a PHINode
  /// with multiple incoming blocks.
  ///
  /// Also note that this doesn't preserve any passes. To split blocks while
  /// keeping loop information consistent, use the SplitBlockBefore utility
  /// function.
  /// \param I Split point; instructions before this move to the new predecessor.
  /// \param BBName Optional name for the newly created basic block.
  /// \return The newly created predecessor basic block.
  LLVM_ABI BasicBlock *splitBasicBlockBefore(iterator I,
                                             const Twine &BBName = "");
  /// Split before \p I, inserting the new block as this block's predecessor.
  /// \param I Instruction at which to split; stays in the original block.
  /// \param BBName Optional name for the newly created basic block.
  /// \return The newly created predecessor basic block.
  BasicBlock *splitBasicBlockBefore(Instruction *I, const Twine &BBName = "") {
    return splitBasicBlockBefore(I->getIterator(), BBName);
  }

  /// Transfer all instructions from \p FromBB to this basic block at \p ToIt.
  /// \param ToIt Insertion point in this block.
  /// \param FromBB Source block whose instructions are moved.
  void splice(BasicBlock::iterator ToIt, BasicBlock *FromBB) {
    splice(ToIt, FromBB, FromBB->begin(), FromBB->end());
  }

  /// Transfer one instruction from \p FromBB at \p FromIt to this basic block
  /// at \p ToIt.
  /// \param ToIt Insertion point in this block.
  /// \param FromBB Source block of the instruction.
  /// \param FromIt Iterator to the single instruction to move.
  void splice(BasicBlock::iterator ToIt, BasicBlock *FromBB,
              BasicBlock::iterator FromIt) {
    auto FromItNext = std::next(FromIt);
    // Single-element splice is a noop if destination == source.
    if (ToIt == FromIt || ToIt == FromItNext)
      return;
    splice(ToIt, FromBB, FromIt, FromItNext);
  }

  /// Transfer a range of instructions that belong to \p FromBB from \p
  /// FromBeginIt to \p FromEndIt, to this basic block at \p ToIt.
  /// \param ToIt Insertion point in this block.
  /// \param FromBB Source block of the instruction range.
  /// \param FromBeginIt Start of the range to move (inclusive).
  /// \param FromEndIt End of the range to move (exclusive).
  LLVM_ABI void splice(BasicBlock::iterator ToIt, BasicBlock *FromBB,
                       BasicBlock::iterator FromBeginIt,
                       BasicBlock::iterator FromEndIt);

  /// Erases a range of instructions from \p FromIt to (not including) \p ToIt.
  ///
  /// \returns \p ToIt.
  /// \param FromIt Start of the erase range (inclusive).
  /// \param ToIt End of the erase range (exclusive); also the return value.
  LLVM_ABI BasicBlock::iterator erase(BasicBlock::iterator FromIt,
                                      BasicBlock::iterator ToIt);

  /// Returns true if there are any uses of this basic block other than
  /// direct branches, switches, etc. to it.
  /// \return True if this block's address has been taken.
  bool hasAddressTaken() const {
    return SubclassOptionalData & HasAddressTaken;
  }

  /// Update all phi nodes in this basic block to refer to basic block \p New
  /// instead of basic block \p Old.
  /// \param Old Predecessor block name being replaced in PHI nodes.
  /// \param New Replacement predecessor block for PHI incoming edges.
  LLVM_ABI void replacePhiUsesWith(BasicBlock *Old, BasicBlock *New);

  /// Update all phi nodes in this basic block's successors to refer to basic
  /// block \p New instead of basic block \p Old.
  /// \param Old Predecessor block name being replaced in successor PHIs.
  /// \param New Replacement predecessor block for successor PHI edges.
  LLVM_ABI void replaceSuccessorsPhiUsesWith(BasicBlock *Old, BasicBlock *New);

  /// Update all phi nodes in this basic block's successors to refer to basic
  /// block \p New instead of to it.
  /// \param New Replacement predecessor block for successor PHI edges.
  LLVM_ABI void replaceSuccessorsPhiUsesWith(BasicBlock *New);

  /// Return true if this basic block is an exception handling block.
  /// \return True if the first non-PHI instruction is an EH pad.
  bool isEHPad() const { return getFirstNonPHIIt()->isEHPad(); }

  /// Return true if this basic block is a landing pad.
  ///
  /// Being a ``landing pad'' means that the basic block is the destination of
  /// the 'unwind' edge of an invoke instruction.
  /// \return True if this block is a landing pad.
  LLVM_ABI bool isLandingPad() const;

  /// Return the landingpad instruction associated with the landing pad.
  /// \return The LandingPadInst for this landing pad block.
  LLVM_ABI const LandingPadInst *getLandingPadInst() const;
  /// Return the landingpad instruction associated with the landing pad.
  /// \return The LandingPadInst for this landing pad block.
  LandingPadInst *getLandingPadInst() {
    return const_cast<LandingPadInst *>(
        static_cast<const BasicBlock *>(this)->getLandingPadInst());
  }

  /// Return true if it is legal to hoist instructions into this block.
  /// \return True if instructions may legally be hoisted into this block.
  LLVM_ABI bool isLegalToHoistInto() const;

  /// Return true if this is the entry block of the containing function.
  /// This method can only be used on blocks that have a parent function.
  /// \return True if this is the entry block of the parent function.
  LLVM_ABI bool isEntryBlock() const;

  /// Return the irregular-loop header weight metadata value, if present.
  /// \return The irregular-loop header weight, or std::nullopt if absent.
  LLVM_ABI std::optional<uint64_t> getIrrLoopHeaderWeight() const;

  /// Returns true if the Order field of child Instructions is valid.
  /// \return True if child instruction Order fields are valid.
  bool isInstrOrderValid() const {
    return SubclassOptionalData & InstrOrderValid;
  }

  /// Mark instruction ordering invalid. Done on every instruction insert.
  void invalidateOrders() {
    validateInstrOrdering();
    SubclassOptionalData &= ~InstrOrderValid;
  }

  /// Renumber instructions and mark the ordering as valid.
  LLVM_ABI void renumberInstructions();

  /// Assert that instruction order numbers are invalid or strictly ascending.
  ///
  /// This is constant time if the ordering is invalid, and linear in the number
  /// of instructions if the ordering is valid. Callers should be careful not to
  /// call this in ways that make common operations O(n^2). For example, it takes
  /// O(n) time to assign order numbers to instructions, so the order should be
  /// validated no more than once after each ordering to ensure that transforms
  /// have the same algorithmic complexity when asserts are enabled as when they
  /// are disabled.
  LLVM_ABI void validateInstrOrdering() const;
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an opaque \c LLVMBasicBlockRef to a \c BasicBlock pointer.
/// \param P Opaque C API basic-block reference to unwrap.
/// \return The unwrapped BasicBlock pointer.
inline BasicBlock *unwrap(LLVMBasicBlockRef P) {
  return reinterpret_cast<BasicBlock *>(P);
}
/// Convert a \c BasicBlock pointer to an opaque \c LLVMBasicBlockRef.
/// \param P Basic block to wrap for the C API.
/// \return The opaque C API basic-block reference.
inline LLVMBasicBlockRef wrap(const BasicBlock *P) {
  return reinterpret_cast<LLVMBasicBlockRef>(const_cast<BasicBlock *>(P));
}

/// Advance \p It while it points to a debug instruction and return the result.
///
/// This assumes that \p It is not at the end of a block.
/// \param It Iterator into a basic block's instruction list.
/// \return An iterator past any leading debug intrinsics.
LLVM_ABI BasicBlock::iterator skipDebugIntrinsics(BasicBlock::iterator It);

#ifdef NDEBUG
/// In release builds, this is a no-op. For !NDEBUG builds, the checks are
/// implemented in the .cpp file to avoid circular header deps.
inline void BasicBlock::validateInstrOrdering() const {}
#endif

/// DenseMapInfo specialization for \c BasicBlock::iterator.
///
/// Hashing and equality use the instruction-list node pointer and the
/// debug-info "head" bit so iterators can be installed into maps and sets.
template <> struct DenseMapInfo<BasicBlock::iterator> {
  /// Return a hash of the iterator's node pointer and head bit.
  /// \param It Iterator whose node pointer and head bit are hashed.
  /// \return A hash combining the node pointer and head bit.
  static unsigned getHashValue(const BasicBlock::iterator &It) {
    return DenseMapInfo<void *>::getHashValue(
               reinterpret_cast<void *>(It.getNodePtr())) ^
           (unsigned)It.getHeadBit();
  }

  /// Return true if \p LHS and \p RHS refer to the same position and head bit.
  /// \param LHS Left-hand iterator.
  /// \param RHS Right-hand iterator.
  /// \return True if both iterators have the same position and head bit.
  static bool isEqual(const BasicBlock::iterator &LHS,
                      const BasicBlock::iterator &RHS) {
    return LHS == RHS && LHS.getHeadBit() == RHS.getHeadBit();
  }
};

} // end namespace llvm

#endif // LLVM_IR_BASICBLOCK_H
