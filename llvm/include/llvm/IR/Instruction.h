//===-- llvm/Instruction.h - Instruction class definition -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Instruction class, which is the
// base class for all of the LLVM instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INSTRUCTION_H
#define LLVM_IR_INSTRUCTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Bitfields.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/SymbolTableListTraits.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ModRef.h"
#include <cstdint>
#include <utility>

namespace llvm {

class BasicBlock;
class DataLayout;
class DbgMarker;
class FastMathFlags;
class MDNode;
class Module;
struct AAMDNodes;
class DbgMarker;
class DbgRecord;

/// ilist allocation traits for Instruction that destroy via deleteValue.
template <> struct ilist_alloc_traits<Instruction> {
  /// Destroy an instruction node removed from an instruction list.
  ///
  /// \param V The instruction to delete.
  static inline void deleteNode(Instruction *V);
};

LLVM_ABI iterator_range<simple_ilist<DbgRecord>::iterator>
getDbgRecordRange(DbgMarker *);

/// Represents a position in a basic block's instruction list for insertion.
class InsertPosition {
  using InstListType = SymbolTableList<Instruction, ilist_iterator_bits<true>,
                                       ilist_parent<BasicBlock>>;
  InstListType::iterator InsertAt;

public:
  /// Construct an empty (invalid) insert position.
  ///
  /// \param Unused Null sentinel used to select this constructor.
  InsertPosition(std::nullptr_t Unused) : InsertAt() {}
  /// Construct an insert position at the end of \p InsertAtEnd.
  ///
  /// \param InsertAtEnd The basic block whose end is the insertion point.
  LLVM_ABI InsertPosition(BasicBlock *InsertAtEnd);
  /// Construct from an instruction-list iterator insertion point.
  ///
  /// \param InsertAt The iterator insertion point.
  InsertPosition(InstListType::iterator InsertAt) : InsertAt(InsertAt) {}
  /// Convert to the underlying instruction-list iterator.
  ///
  /// \return The insertion-point iterator.
  operator InstListType::iterator() const { return InsertAt; }
  /// Return true if this insert position refers to a valid iterator.
  ///
  /// \return True if the position is valid.
  bool isValid() const { return InsertAt.isValid(); }
  /// Return the basic block that owns this insert position.
  ///
  /// \return The parent basic block.
  BasicBlock *getBasicBlock() { return InsertAt.getNodeParent(); }
};

/// Base class for all LLVM IR instructions.
///
/// Instructions are Values that represent a single operation in a basic block.
/// They are also nodes in the basic block's intrusive instruction list.
class Instruction : public User,
                    public ilist_node_with_parent<Instruction, BasicBlock,
                                                  ilist_iterator_bits<true>,
                                                  ilist_parent<BasicBlock>> {
public:
  /// Intrusive instruction list type used by BasicBlock.
  using InstListType = SymbolTableList<Instruction, ilist_iterator_bits<true>,
                                       ilist_parent<BasicBlock>>;

  /// Iterator type that casts an operand to a basic block.
  ///
  /// All terminators store successors as adjacent operands.
  struct succ_iterator
      : iterator_adaptor_base<succ_iterator, op_iterator,
                              std::random_access_iterator_tag, BasicBlock *,
                              ptrdiff_t, BasicBlock *, BasicBlock *> {
    /// Construct a default (singular) successor iterator.
    succ_iterator() = default;
    /// Construct a successor iterator from an operand iterator.
    ///
    /// \param I Operand iterator pointing at a successor Use.
    explicit succ_iterator(op_iterator I) : iterator_adaptor_base(I) {}

    /// Dereference to the successor basic block.
    ///
    /// \return The successor basic block.
    BasicBlock *operator*() const { return cast<BasicBlock>(*I); }
    /// Access members of the successor basic block.
    ///
    /// \return The successor basic block.
    BasicBlock *operator->() const { return operator*(); }

    /// Return the underlying operand Use iterator.
    ///
    /// \return The wrapped operand iterator.
    op_iterator getUse() const { return I; }
  };

  /// The const version of `succ_iterator`.
  struct const_succ_iterator
      : iterator_adaptor_base<const_succ_iterator, const_op_iterator,
                              std::random_access_iterator_tag,
                              const BasicBlock *, ptrdiff_t, const BasicBlock *,
                              const BasicBlock *> {
    /// Construct a default (singular) const successor iterator.
    const_succ_iterator() = default;
    /// Construct a const successor iterator from a const operand iterator.
    ///
    /// \param I Const operand iterator pointing at a successor Use.
    explicit const_succ_iterator(const_op_iterator I)
        : iterator_adaptor_base(I) {}

    /// Dereference to the successor basic block.
    ///
    /// \return The successor basic block.
    const BasicBlock *operator*() const { return cast<BasicBlock>(*I); }
    /// Access members of the successor basic block.
    ///
    /// \return The successor basic block.
    const BasicBlock *operator->() const { return operator*(); }

    /// Return the underlying const operand Use iterator.
    ///
    /// \return The wrapped const operand iterator.
    const_op_iterator getUse() const { return I; }
  };

private:
  DebugLoc DbgLoc;                         // 'dbg' Metadata cache.

  friend class Value;
  /// Index of first metadata attachment in context, or zero.
  unsigned MetadataIndex = 0;

  /// Relative order of this instruction in its parent basic block. Used for
  /// O(1) local dominance checks between instructions.
  mutable unsigned Order = 0;

public:
  /// Optional marker for debugging information immediately before this instruction.
  ///
  /// Null unless there is debugging information present.
  DbgMarker *DebugMarker = nullptr;

  /// Clone any debug-info attached to \p From onto this instruction.
  ///
  /// Used to copy debugging information from one block to another, when copying
  /// entire blocks. \see DebugProgramInstruction.h , because the ordering of
  /// DbgRecords is still important, fine grain control of which instructions
  /// are moved and where they go is necessary.
  /// \param From The instruction to clone debug-info from.
  /// \param FromHere Optional iterator to limit DbgRecords cloned to the range
  ///        from FromHere to end().
  /// \param InsertAtHead Whether the cloned DbgRecords should be placed at the
  ///        beginning of existing DbgRecords attached to this.
  /// \return A range over the newly cloned DbgRecords.
  LLVM_ABI iterator_range<simple_ilist<DbgRecord>::iterator> cloneDebugInfoFrom(
      const Instruction *From,
      std::optional<simple_ilist<DbgRecord>::iterator> FromHere = std::nullopt,
      bool InsertAtHead = false);

  /// Return a range over the DbgRecords attached to this instruction.
  ///
  /// \return A range of attached DbgRecords.
  iterator_range<simple_ilist<DbgRecord>::iterator> getDbgRecordRange() const {
    return llvm::getDbgRecordRange(DebugMarker);
  }

  /// Return an iterator to the next DbgRecord after this instruction.
  ///
  /// This is the position to pass to BasicBlock::reinsertInstInDbgRecords when
  /// re-inserting an instruction, or std::nullopt if there is none.
  /// \return Optional iterator to the next DbgRecord, or nullopt.
  LLVM_ABI std::optional<simple_ilist<DbgRecord>::iterator>
  getDbgReinsertionPosition();

  /// Return true if any DbgRecords are attached to this instruction.
  ///
  /// \return True if DbgRecords are attached.
  LLVM_ABI bool hasDbgRecords() const;

  /// Transfer any DbgRecords on the position \p It onto this instruction.
  ///
  /// Adopts the sequence of DbgRecords (which is efficient) if possible, or
  /// merges two sequences otherwise.
  /// \param BB The basic block owning \p It.
  /// \param It Iterator position whose DbgRecords are transferred.
  /// \param InsertAtHead Whether adopted records are inserted at the head.
  LLVM_ABI void adoptDbgRecords(BasicBlock *BB, InstListType::iterator It,
                                bool InsertAtHead);

  /// Erase any DbgRecords attached to this instruction.
  LLVM_ABI void dropDbgRecords();

  /// Erase a single DbgRecord \p I that is attached to this instruction.
  ///
  /// \param I The DbgRecord to erase.
  LLVM_ABI void dropOneDbgRecord(DbgRecord *I);

  /// Handle the debug-info implications of this instruction being removed. Any
  /// attached DbgRecords need to "fall" down onto the next instruction.
  LLVM_ABI void handleMarkerRemoval();

protected:
  // All 16 bits of `Value::SubclassData` are available for subclasses of
  // `Instruction` to use.
  /// Opaque 16-bit bitfield covering the full Instruction subclass data.
  using OpaqueField = Bitfield::Element<uint16_t, 0, 16>;

  // Template alias so that all Instruction storing alignment use the same
  // definiton.
  // Valid alignments are powers of two from 2^0 to 2^MaxAlignmentExponent =
  // 2^32. We store them as Log2(Alignment), so we need 6 bits to encode the 33
  // possible values.
  /// Bitfield element template for an alignment at bit offset \p Offset.
  template <unsigned Offset>
  using AlignmentBitfieldElementT =
      typename Bitfield::Element<unsigned, Offset, 6,
                                 Value::MaxAlignmentExponent>;

  /// Bitfield element template for a single boolean flag at bit offset \p Offset.
  template <unsigned Offset>
  using BoolBitfieldElementT = typename Bitfield::Element<bool, Offset, 1>;

  /// Bitfield element template for atomic ordering at bit offset \p Offset.
  template <unsigned Offset>
  using AtomicOrderingBitfieldElementT =
      typename Bitfield::Element<AtomicOrdering, Offset, 3,
                                 AtomicOrdering::LAST>;

protected:
  /// Destroy this instruction; prefer deleteValue() for a generic Instruction.
  LLVM_ABI ~Instruction(); // Use deleteValue() to delete a generic Instruction.

public:
  /// Instructions are not copy-constructible; clone or recreate instead.
  ///
  /// \param Other The instruction that would be copied from (deleted).
  Instruction(const Instruction &Other) = delete;
  /// Instructions are not copy-assignable; clone or recreate instead.
  ///
  /// \param Other The instruction that would be assigned from (deleted).
  Instruction &operator=(const Instruction &Other) = delete;

  /// Return the first user of this instruction as an Instruction.
  ///
  /// Instructions can only be used by other instructions.
  /// \return The first user cast to Instruction.
  Instruction       *user_back()       { return cast<Instruction>(*user_begin());}
  /// Return the first user of this instruction as a const Instruction.
  ///
  /// \return The first user cast to const Instruction.
  const Instruction *user_back() const { return cast<Instruction>(*user_begin());}

  /// Return the module owning the function this instruction belongs to, or
  /// nullptr if the function does not have a module.
  ///
  /// Note: this is undefined behavior if the instruction does not have a
  /// parent, or the parent basic block does not have a parent function.
  /// \return The parent module, or null.
  LLVM_ABI const Module *getModule() const;
  /// Return the module owning the function this instruction belongs to.
  ///
  /// \return The parent module, or null.
  Module *getModule() {
    return const_cast<Module *>(
                           static_cast<const Instruction *>(this)->getModule());
  }

  /// Return the function this instruction belongs to.
  ///
  /// Note: it is undefined behavior to call this on an instruction not
  /// currently inserted into a function.
  /// \return The parent function.
  LLVM_ABI const Function *getFunction() const;
  /// Return the function this instruction belongs to.
  ///
  /// \return The parent function.
  Function *getFunction() {
    return const_cast<Function *>(
                         static_cast<const Instruction *>(this)->getFunction());
  }

  /// Return the data layout of the module this instruction belongs to.
  ///
  /// Requires the instruction to have a parent module.
  /// \return The module's data layout.
  LLVM_ABI const DataLayout &getDataLayout() const;

  /// This method unlinks 'this' from the containing basic block, but does not
  /// delete it.
  LLVM_ABI void removeFromParent();

  /// This method unlinks 'this' from the containing basic block and deletes it.
  ///
  /// \returns an iterator pointing to the element after the erased one
  LLVM_ABI InstListType::iterator eraseFromParent();

  /// Insert an unlinked instruction into a basic block immediately before
  /// the specified position.
  ///
  /// \param InsertPos Iterator insertion point in the destination block.
  LLVM_ABI void insertBefore(InstListType::iterator InsertPos);

  /// Insert an unlinked instruction into a basic block immediately after the
  /// specified instruction.
  ///
  /// \param InsertPos The instruction after which to insert.
  LLVM_ABI void insertAfter(Instruction *InsertPos);

  /// Insert an unlinked instruction into a basic block immediately after the
  /// specified position.
  ///
  /// \param InsertPos Iterator insertion point after which to insert.
  LLVM_ABI void insertAfter(InstListType::iterator InsertPos);

  /// Insert an unlinked instruction into \p ParentBB at position \p It.
  ///
  /// \param ParentBB The basic block to insert into.
  /// \param It Iterator insertion point in \p ParentBB.
  /// \return Iterator to the inserted instruction.
  LLVM_ABI InstListType::iterator insertInto(BasicBlock *ParentBB,
                                             InstListType::iterator It);

  /// Insert an unlinked instruction into \p BB immediately before \p InsertPos.
  ///
  /// \param BB The basic block to insert into.
  /// \param InsertPos Iterator insertion point in \p BB.
  LLVM_ABI void insertBefore(BasicBlock &BB, InstListType::iterator InsertPos);

  /// Unlink this instruction from its current basic block and insert it into
  /// the basic block that MovePos lives in, right before MovePos.
  ///
  /// \param InsertPos Iterator insertion point before which to move.
  LLVM_ABI void moveBefore(InstListType::iterator InsertPos);

  /// Move this instruction before \p MovePos while preserving instruction order.
  ///
  /// Signals that the caller intends to preserve the original ordering of
  /// instructions. This implicitly means that any adjacent debug-info should
  /// move with this instruction.
  /// \param MovePos Iterator insertion point before which to move.
  LLVM_ABI void moveBeforePreserving(InstListType::iterator MovePos);

  /// Move this instruction before \p I in \p BB while preserving instruction order.
  ///
  /// Signals that the caller intends to preserve the original ordering of
  /// instructions. This implicitly means that any adjacent debug-info should
  /// move with this instruction.
  /// \param BB The destination basic block.
  /// \param I Iterator insertion point in \p BB.
  LLVM_ABI void moveBeforePreserving(BasicBlock &BB, InstListType::iterator I);

private:
  /// RemoveDIs project: all other moves implemented with this method,
  /// centralising debug-info updates into one place.
  void moveBeforeImpl(BasicBlock &BB, InstListType::iterator I, bool Preserve);

public:
  /// Unlink this instruction and insert into BB before I.
  ///
  /// \pre I is a valid iterator into BB.
  /// \param BB The destination basic block.
  /// \param I Iterator insertion point in \p BB.
  LLVM_ABI void moveBefore(BasicBlock &BB, InstListType::iterator I);

  /// Unlink this instruction from its current basic block and insert it into
  /// the basic block that MovePos lives in, right after MovePos.
  ///
  /// \param MovePos The instruction after which to move.
  LLVM_ABI void moveAfter(Instruction *MovePos);

  /// Unlink this instruction from its current basic block and insert it into
  /// the basic block that MovePos lives in, right after MovePos.
  ///
  /// \param MovePos Iterator insertion point after which to move.
  LLVM_ABI void moveAfter(InstListType::iterator MovePos);

  /// Move this instruction after \p MovePos while preserving instruction order.
  ///
  /// See \ref moveBeforePreserving.
  /// \param MovePos The instruction after which to move.
  LLVM_ABI void moveAfterPreserving(Instruction *MovePos);

  /// Return true if this instruction comes before \p Other in the same block.
  ///
  /// \p Other must be in the same basic block as this instruction. In the worst
  /// case, this takes linear time in the number of instructions in the block.
  /// The results are cached, so in common cases when the block remains
  /// unmodified, it takes constant time.
  /// \param Other The instruction to compare against in the same block.
  /// \return True if this instruction comes before \p Other.
  LLVM_ABI bool comesBefore(const Instruction *Other) const;

  /// Return the first insertion point at which this instruction's result is defined.
  ///
  /// This is *not* the directly following instruction in a number of cases,
  /// e.g. phi nodes or terminators that return values. This function may return
  /// null if the insertion after the definition is not possible, e.g. due to a
  /// catchswitch terminator.
  /// \return An iterator to a valid insertion point, or nullopt if none exists.
  LLVM_ABI std::optional<InstListType::iterator> getInsertionPointAfterDef();

  //===--------------------------------------------------------------------===//
  // Subclass classification.
  //===--------------------------------------------------------------------===//

  /// Return the opcode of this instruction as a member of the opcode enums.
  ///
  /// \return A value such as Instruction::Add.
  unsigned getOpcode() const { return getValueID() - InstructionVal; }

  /// Return the mnemonic name for this instruction's opcode.
  ///
  /// \return The opcode name string (e.g. "add", "ret").
  const char *getOpcodeName() const { return getOpcodeName(getOpcode()); }
  /// Return true if this instruction is a terminator.
  ///
  /// \return True if this instruction terminates a basic block.
  bool isTerminator() const { return isTerminator(getOpcode()); }
  /// Return true if this instruction is a unary operator.
  ///
  /// \return True if this instruction is a unary operator.
  bool isUnaryOp() const { return isUnaryOp(getOpcode()); }
  /// Return true if this instruction is a binary operator.
  ///
  /// \return True if this instruction is a binary operator.
  bool isBinaryOp() const { return isBinaryOp(getOpcode()); }
  /// Return true if this instruction is an integer division or remainder.
  ///
  /// \return True if this instruction is an integer div/rem.
  bool isIntDivRem() const { return isIntDivRem(getOpcode()); }
  /// Return true if this instruction is a floating-point division or remainder.
  ///
  /// \return True if this instruction is a floating-point div/rem.
  bool isFPDivRem() const { return isFPDivRem(getOpcode()); }
  /// Return true if this instruction is a shift.
  ///
  /// \return True if this instruction is a shift.
  bool isShift() const { return isShift(getOpcode()); }
  /// Return true if this instruction is a cast.
  ///
  /// \return True if this instruction is a cast.
  bool isCast() const { return isCast(getOpcode()); }
  /// Return true if this instruction is a funclet pad.
  ///
  /// \return True if this instruction is a funclet pad.
  bool isFuncletPad() const { return isFuncletPad(getOpcode()); }
  /// Return true if this instruction is a special terminator.
  ///
  /// \return True if this instruction is a special terminator.
  bool isSpecialTerminator() const { return isSpecialTerminator(getOpcode()); }

  /// Return true if this instruction is the only user of at least one operand.
  ///
  /// \return True if this is the sole user of any of its operands.
  LLVM_ABI bool isOnlyUserOfAnyOperand();

  /// Return the mnemonic name for the given opcode (e.g. "add", "ret").
  ///
  /// \param Opcode The opcode to name.
  /// \return The opcode name string.
  LLVM_ABI static const char *getOpcodeName(unsigned Opcode);

  /// Return true if the opcode is a terminator instruction.
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is a terminator.
  static inline bool isTerminator(unsigned Opcode) {
    return Opcode >= TermOpsBegin && Opcode < TermOpsEnd;
  }

  /// Return true if the opcode is a unary operator.
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is a unary operator.
  static inline bool isUnaryOp(unsigned Opcode) {
    return Opcode >= UnaryOpsBegin && Opcode < UnaryOpsEnd;
  }
  /// Return true if the opcode is a binary operator.
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is a binary operator.
  static inline bool isBinaryOp(unsigned Opcode) {
    return Opcode >= BinaryOpsBegin && Opcode < BinaryOpsEnd;
  }

  /// Return true if the opcode is an integer division or remainder.
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is an integer div/rem.
  static inline bool isIntDivRem(unsigned Opcode) {
    return Opcode == UDiv || Opcode == SDiv || Opcode == URem || Opcode == SRem;
  }

  /// Return true if the opcode is a floating-point division or remainder.
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is a floating-point div/rem.
  static inline bool isFPDivRem(unsigned Opcode) {
    return Opcode == FDiv || Opcode == FRem;
  }

  /// Return true if \p Opcode is one of the shift instructions.
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is a shift.
  static inline bool isShift(unsigned Opcode) {
    return Opcode >= Shl && Opcode <= AShr;
  }

  /// Return true if this is a logical shift left or a logical shift right.
  ///
  /// \return True if this is shl or lshr.
  inline bool isLogicalShift() const {
    return getOpcode() == Shl || getOpcode() == LShr;
  }

  /// Return true if this is an arithmetic shift right.
  ///
  /// \return True if this is ashr.
  inline bool isArithmeticShift() const {
    return getOpcode() == AShr;
  }

  /// Return true if \p Opcode is and/or/xor.
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is a bitwise logic operator.
  static inline bool isBitwiseLogicOp(unsigned Opcode) {
    return Opcode == And || Opcode == Or || Opcode == Xor;
  }

  /// Return true if this is and/or/xor.
  ///
  /// \return True if this instruction is a bitwise logic operator.
  inline bool isBitwiseLogicOp() const {
    return isBitwiseLogicOp(getOpcode());
  }

  /// Return true if \p Opcode is one of the CastInst instructions.
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is a cast.
  static inline bool isCast(unsigned Opcode) {
    return Opcode >= CastOpsBegin && Opcode < CastOpsEnd;
  }

  /// Return true if \p Opcode is one of the FuncletPadInst instructions.
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is a funclet pad.
  static inline bool isFuncletPad(unsigned Opcode) {
    return Opcode >= FuncletPadOpsBegin && Opcode < FuncletPadOpsEnd;
  }

  /// Return true if \p Opcode is a special terminator.
  ///
  /// Special terminators do more than branch to a successor (e.g. have a side
  /// effect or return a value).
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is a special terminator.
  static inline bool isSpecialTerminator(unsigned Opcode) {
    switch (Opcode) {
    case Instruction::CatchSwitch:
    case Instruction::CatchRet:
    case Instruction::CleanupRet:
    case Instruction::Invoke:
    case Instruction::Resume:
    case Instruction::CallBr:
      return true;
    default:
      return false;
    }
  }

  //===--------------------------------------------------------------------===//
  // Metadata manipulation.
  //===--------------------------------------------------------------------===//

  /// Return true if this instruction has any metadata attached to it.
  ///
  /// \return True if any metadata (including debug location) is attached.
  bool hasMetadata() const { return DbgLoc || MetadataIndex != 0; }

  /// Return true if this instruction contains loop metadata other than a debug
  /// location.
  ///
  /// \return True if non-debug loop metadata is present.
  LLVM_ABI bool hasNonDebugLocLoopMetadata() const;

  /// Return true if this instruction has metadata attached to it other than a
  /// debug location.
  ///
  /// \return True if non-debug metadata is attached.
  bool hasMetadataOtherThanDebugLoc() const { return MetadataIndex != 0; }

  /// Return true if this instruction has the given type of metadata attached.
  ///
  /// \param KindID The metadata kind ID to look up.
  /// \return True if metadata of kind \p KindID is attached.
  bool hasMetadata(unsigned KindID) const {
    return getMetadata(KindID) != nullptr;
  }

  /// Return true if this instruction has the given type of metadata attached.
  ///
  /// \param Kind The metadata kind name to look up.
  /// \return True if metadata of kind \p Kind is attached.
  bool hasMetadata(StringRef Kind) const {
    return getMetadata(Kind) != nullptr;
  }

  /// Get the metadata of given kind attached to this Instruction.
  ///
  /// If the metadata is not found then return null.
  /// \param KindID The metadata kind ID to look up.
  /// \return The metadata node, or null if not present.
  MDNode *getMetadata(unsigned KindID) const {
    // Handle 'dbg' as a special case since it is not stored in the hash table.
    if (KindID == LLVMContext::MD_dbg)
      return DbgLoc.getAsMDNode();
    return hasMetadataOtherThanDebugLoc() ? Value::getMetadataImpl(KindID)
                                          : nullptr;
  }

  /// Get the metadata of given kind attached to this Instruction.
  ///
  /// If the metadata is not found then return null.
  /// \param Kind The metadata kind name to look up.
  /// \return The metadata node, or null if not present.
  MDNode *getMetadata(StringRef Kind) const {
    if (!hasMetadata()) return nullptr;
    return getMetadataImpl(Kind);
  }

  /// Get all metadata attached to this Instruction.
  ///
  /// The first element of each pair returned is the KindID, the second element
  /// is the metadata value. This list is returned sorted by the KindID.
  /// \param MDs Vector filled with (KindID, MDNode *) pairs.
  void
  getAllMetadata(SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs) const {
    if (hasMetadata())
      getAllMetadataImpl(MDs);
  }

  /// Get all metadata attached to this Instruction except the debug location.
  ///
  /// \param MDs Vector filled with (KindID, MDNode *) pairs.
  void getAllMetadataOtherThanDebugLoc(
      SmallVectorImpl<std::pair<unsigned, MDNode *>> &MDs) const {
    Value::getAllMetadata(MDs);
  }

  /// Set metadata of kind \p KindID to \p Node.
  ///
  /// Updates or replaces metadata if already present, or removes it if Node is
  /// null.
  /// \param KindID The metadata kind ID.
  /// \param Node The metadata node to attach, or null to remove.
  LLVM_ABI void setMetadata(unsigned KindID, MDNode *Node);
  /// Set metadata of kind \p Kind to \p Node.
  ///
  /// Updates or replaces metadata if already present, or removes it if Node is
  /// null.
  /// \param Kind The metadata kind name.
  /// \param Node The metadata node to attach, or null to remove.
  LLVM_ABI void setMetadata(StringRef Kind, MDNode *Node);

  /// Copy metadata from \p SrcInst onto this instruction.
  ///
  /// \p WL, if not empty, specifies the list of metadata kinds that need to be
  /// copied. If \p WL is empty, all metadata will be copied.
  /// \param SrcInst The instruction to copy metadata from.
  /// \param WL Optional whitelist of metadata kind IDs to copy.
  LLVM_ABI void copyMetadata(const Instruction &SrcInst,
                             ArrayRef<unsigned> WL = ArrayRef<unsigned>());

  /// Copy debug, profile, and memprof metadata from \p SrcInst.
  ///
  /// Does not copy alias-analysis or type-dependent metadata.
  /// TODO: Include additional metadata in the future if appropriate.
  /// \param SrcInst The instruction to copy metadata from.
  LLVM_ABI void copyProfileAndDebugMetadata(const Instruction &SrcInst);

  /// Erase all metadata that matches the predicate.
  ///
  /// \param Pred Predicate receiving (KindID, MDNode *) and returning true to erase.
  LLVM_ABI void eraseMetadataIf(function_ref<bool(unsigned, MDNode *)> Pred);

  /// If the instruction has "branch_weights" MD_prof metadata and the MDNode
  /// has three operands (including name string), swap the order of the
  /// metadata.
  LLVM_ABI void swapProfMetadata();

  /// Drop all unknown metadata except for debug locations.
  ///
  /// Passes are required to drop metadata they don't understand. This is a
  /// convenience method for passes to do so.
  /// dropUBImplyingAttrsAndUnknownMetadata should be used instead of
  /// this API if the Instruction being modified is a call.
  /// \param KnownIDs Metadata kind IDs that should be retained.
  LLVM_ABI void dropUnknownNonDebugMetadata(ArrayRef<unsigned> KnownIDs = {});

  /// Add an !annotation metadata node with \p Annotation.
  ///
  /// If this instruction already has !annotation metadata, append \p Annotation
  /// to the existing node.
  /// \param Annotation The annotation string to add.
  LLVM_ABI void addAnnotationMetadata(StringRef Annotation);
  /// Add an !annotation metadata node with an array of \p Annotations.
  ///
  /// If this instruction already has !annotation metadata, append the tuple to
  /// the existing node.
  /// \param Annotations The annotation strings to add as a tuple.
  LLVM_ABI void addAnnotationMetadata(SmallVector<StringRef> Annotations);
  /// Return the AA metadata for this instruction.
  ///
  /// \return The alias-analysis metadata nodes for this instruction.
  LLVM_ABI AAMDNodes getAAMetadata() const;

  /// Set the AA metadata on this instruction from the AAMDNodes structure.
  ///
  /// \param N The alias-analysis metadata to attach.
  LLVM_ABI void setAAMetadata(const AAMDNodes &N);

  /// Sets the nosanitize metadata on this instruction.
  LLVM_ABI void setNoSanitizeMetadata();

  /// Retrieve total raw weight values of a branch.
  ///
  /// Returns true on success with profile total weights filled in.
  /// Returns false if no metadata was found.
  /// \param TotalVal On success, set to the total profile weight.
  /// \return True if profile total weight metadata was found.
  LLVM_ABI bool extractProfTotalWeight(uint64_t &TotalVal) const;

  /// Set the debug location information for this instruction.
  ///
  /// \param Loc The debug location to attach.
  void setDebugLoc(DebugLoc Loc) { DbgLoc = std::move(Loc).getCopied(); }

  /// Return the debug location for this node as a DebugLoc.
  ///
  /// \return The debug location attached to this instruction.
  const DebugLoc &getDebugLoc() const { return DbgLoc; }

  /// Return a stable debug location for this instruction.
  ///
  /// For debug intrinsics, fetch the debug location of the next non-debug node.
  /// \return A debug location suitable for stable reporting.
  LLVM_ABI const DebugLoc &getStableDebugLoc() const;

  /// Set or clear the nuw flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param b True to set the flag; false to clear it.
  LLVM_ABI void setHasNoUnsignedWrap(bool b = true);

  /// Set or clear the nsw flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param b True to set the flag; false to clear it.
  LLVM_ABI void setHasNoSignedWrap(bool b = true);

  /// Set or clear the exact flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param b True to set the flag; false to clear it.
  LLVM_ABI void setIsExact(bool b = true);

  /// Set or clear the nneg flag on this instruction.
  ///
  /// The instruction must be a zext instruction.
  /// \param b True to set the flag; false to clear it.
  LLVM_ABI void setNonNeg(bool b = true);

  /// Determine whether the no unsigned wrap flag is set.
  ///
  /// \return True if the nuw flag is set.
  LLVM_ABI bool hasNoUnsignedWrap() const LLVM_READONLY;

  /// Determine whether the no signed wrap flag is set.
  ///
  /// \return True if the nsw flag is set.
  LLVM_ABI bool hasNoSignedWrap() const LLVM_READONLY;

  /// Determine whether the nneg flag is set.
  ///
  /// \return True if the nneg flag is set.
  LLVM_ABI bool hasNonNeg() const LLVM_READONLY;

  /// Return true if this operator has flags which may cause poison.
  ///
  /// Such flags may cause this instruction to evaluate to poison despite having
  /// non-poison inputs.
  /// \return True if poison-generating flags are present.
  LLVM_ABI bool hasPoisonGeneratingFlags() const LLVM_READONLY;

  /// Drops flags that may cause this instruction to evaluate to poison despite
  /// having non-poison inputs.
  LLVM_ABI void dropPoisonGeneratingFlags();

  /// Return true if this instruction has poison-generating metadata.
  ///
  /// \return True if poison-generating metadata is present.
  LLVM_ABI bool hasPoisonGeneratingMetadata() const LLVM_READONLY;

  /// Drops metadata that may generate poison.
  LLVM_ABI void dropPoisonGeneratingMetadata();

  /// Return true if this instruction has poison-generating attribute.
  ///
  /// \return True if poison-generating attributes are present.
  LLVM_ABI bool hasPoisonGeneratingAttributes() const LLVM_READONLY;

  /// Drops attributes that may generate poison.
  LLVM_ABI void dropPoisonGeneratingAttributes();

  /// Return true if this instruction has poison-generating flags, attributes or
  /// metadata.
  ///
  /// \return True if any poison-generating annotation is present.
  bool hasPoisonGeneratingAnnotations() const {
    return hasPoisonGeneratingFlags() || hasPoisonGeneratingAttributes() ||
           hasPoisonGeneratingMetadata();
  }

  /// Drops flags, attributes and metadata that may generate poison.
  void dropPoisonGeneratingAnnotations() {
    dropPoisonGeneratingFlags();
    dropPoisonGeneratingAttributes();
    dropPoisonGeneratingMetadata();
  }

  /// Drop non-debug unknown metadata and UB-implying call attributes.
  ///
  /// This drops non-debug unknown metadata (through dropUnknownNonDebugMetadata).
  /// For calls, it also drops parameter and return attributes that can cause
  /// undefined behaviour. Both of these should be done by passes which move
  /// instructions in IR.
  /// \param KnownIDs Metadata kind IDs that should be retained.
  LLVM_ABI void
  dropUBImplyingAttrsAndUnknownMetadata(ArrayRef<unsigned> KnownIDs = {});

  /// Drop attributes or metadata that can cause immediate undefined behavior.
  ///
  /// Retain other attributes/metadata on a best-effort basis, as well as those
  /// passed in `Keep`. This should be used when speculating instructions.
  /// \param Keep Metadata kind IDs that should be retained.
  LLVM_ABI void dropUBImplyingAttrsAndMetadata(ArrayRef<unsigned> Keep = {});

  /// Return true if this instruction has UB-implying attributes that can cause
  /// immediate undefined behavior.
  ///
  /// \return True if UB-implying attributes are present.
  LLVM_ABI bool hasUBImplyingAttrs() const LLVM_READONLY;

  /// Determine whether the exact flag is set.
  ///
  /// \return True if the exact flag is set.
  LLVM_ABI bool isExact() const LLVM_READONLY;

  /// Set or clear all fast-math-flags on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set all fast-math flags; false to clear them.
  LLVM_ABI void setFast(bool B);

  /// Set or clear the reassociation flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag; false to clear it.
  LLVM_ABI void setHasAllowReassoc(bool B);

  /// Set or clear the no-nans flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag; false to clear it.
  LLVM_ABI void setHasNoNaNs(bool B);

  /// Set or clear the no-infs flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag; false to clear it.
  LLVM_ABI void setHasNoInfs(bool B);

  /// Set or clear the no-signed-zeros flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag; false to clear it.
  LLVM_ABI void setHasNoSignedZeros(bool B);

  /// Set or clear the allow-reciprocal flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag; false to clear it.
  LLVM_ABI void setHasAllowReciprocal(bool B);

  /// Set or clear the allow-contract flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag; false to clear it.
  LLVM_ABI void setHasAllowContract(bool B);

  /// Set or clear the approximate-math-functions flag on this instruction.
  ///
  /// The instruction must be an operator which supports this flag. See
  /// LangRef.html for the meaning of this flag.
  /// \param B True to set the flag; false to clear it.
  LLVM_ABI void setHasApproxFunc(bool B);

  /// Set multiple fast-math flags on this instruction.
  ///
  /// The instruction must be an operator which supports these flags. See
  /// LangRef.html for the meaning of these flags.
  /// \param FMF The fast-math flags to apply.
  LLVM_ABI void setFastMathFlags(FastMathFlags FMF);

  /// Transfer all fast-math flag values onto this instruction.
  ///
  /// The instruction must be an operator which supports these flags. See
  /// LangRef.html for the meaning of these flags.
  /// \param FMF The fast-math flags to copy.
  LLVM_ABI void copyFastMathFlags(FastMathFlags FMF);

  /// Determine whether all fast-math-flags are set.
  ///
  /// \return True if all fast-math flags are set.
  LLVM_ABI bool isFast() const LLVM_READONLY;

  /// Determine whether the allow-reassociation flag is set.
  ///
  /// \return True if allow-reassociation is set.
  LLVM_ABI bool hasAllowReassoc() const LLVM_READONLY;

  /// Determine whether the no-NaNs flag is set.
  ///
  /// \return True if no-NaNs is set.
  LLVM_ABI bool hasNoNaNs() const LLVM_READONLY;

  /// Determine whether the no-infs flag is set.
  ///
  /// \return True if no-infs is set.
  LLVM_ABI bool hasNoInfs() const LLVM_READONLY;

  /// Determine whether the no-signed-zeros flag is set.
  ///
  /// \return True if no-signed-zeros is set.
  LLVM_ABI bool hasNoSignedZeros() const LLVM_READONLY;

  /// Determine whether the allow-reciprocal flag is set.
  ///
  /// \return True if allow-reciprocal is set.
  LLVM_ABI bool hasAllowReciprocal() const LLVM_READONLY;

  /// Determine whether the allow-contract flag is set.
  ///
  /// \return True if allow-contract is set.
  LLVM_ABI bool hasAllowContract() const LLVM_READONLY;

  /// Determine whether the approximate-math-functions flag is set.
  ///
  /// \return True if approximate-math-functions is set.
  LLVM_ABI bool hasApproxFunc() const LLVM_READONLY;

  /// Return all fast-math flags for this instruction.
  ///
  /// The instruction must be an operator which supports these flags. See
  /// LangRef.html for the meaning of these flags.
  /// \return The fast-math flags on this instruction.
  LLVM_ABI FastMathFlags getFastMathFlags() const LLVM_READONLY;

  /// Return fast-math flags, or default flags when not a FPMathOperator.
  ///
  /// \return This instruction's fast-math flags, or default-constructed flags.
  LLVM_ABI FastMathFlags getFastMathFlagsOrNone() const LLVM_READONLY;

  /// Copy fast-math flags from instruction \p I onto this instruction.
  ///
  /// \param I The instruction whose fast-math flags are copied.
  LLVM_ABI void copyFastMathFlags(const Instruction *I);

  /// Copy supported exact, fast-math, and optionally wrapping flags from \p V.
  ///
  /// \param V The value whose IR flags are copied.
  /// \param IncludeWrapFlags When true, also copy wrapping flags.
  LLVM_ABI void copyIRFlags(const Value *V, bool IncludeWrapFlags = true);

  /// Intersect wrapping, exact, and fast-math flags of \p V with this instruction.
  ///
  /// \param V The value whose IR flags are and-ed with this instruction.
  LLVM_ABI void andIRFlags(const Value *V);

  /// Merge two debug locations and apply the result to this instruction.
  ///
  /// If the instruction is a CallInst, we need to traverse the inline chain to
  /// find the common scope. This is not efficient for N-way merging as each time
  /// you merge 2 iterations, you need to rebuild the hashmap to find the
  /// common scope. However, we still choose this API because:
  ///  1) Simplicity: it takes 2 locations instead of a list of locations.
  ///  2) In worst case, it increases the complexity from O(N*I) to
  ///     O(2*N*I), where N is # of Instructions to merge, and I is the
  ///     maximum level of inline stack. So it is still linear.
  ///  3) Merging of call instructions should be extremely rare in real
  ///     applications, thus the N-way merging should be in code path.
  /// The DebugLoc attached to this instruction will be overwritten by the
  /// merged DebugLoc.
  /// \param LocA The first debug location to merge.
  /// \param LocB The second debug location to merge.
  LLVM_ABI void applyMergedLocation(DebugLoc LocA, DebugLoc LocB);

  /// Update the debug location after hoisting this instruction to a predecessor.
  ///
  /// Note: it is undefined behavior to call this on an instruction not
  /// currently inserted into a function.
  LLVM_ABI void updateLocationAfterHoist();

  /// Drop the instruction's debug location.
  ///
  /// This does not guarantee removal of the !dbg source location attachment, as
  /// it must set a line 0 location with scope information attached on call
  /// instructions. To guarantee removal of the !dbg attachment, use the
  /// \ref setDebugLoc() API.
  /// Note: it is undefined behavior to call this on an instruction not
  /// currently inserted into a function.
  LLVM_ABI void dropLocation();

  /// Merge DIAssignID metadata from this instruction and \p SourceInstructions.
  ///
  /// This process performs a RAUW on the MetadataAsValue uses of the merged
  /// DIAssignID nodes. Not every instruction in \p SourceInstructions needs to
  /// have DIAssignID metadata. If none of them do then nothing happens. If this
  /// instruction does not have a DIAssignID attachment but at least one in
  /// \p SourceInstructions does then the merged one will be attached to it.
  /// However, instructions without attachments in \p SourceInstructions are not
  /// modified.
  /// \param SourceInstructions Instructions whose DIAssignID metadata is merged.
  LLVM_ABI void
  mergeDIAssignID(ArrayRef<const Instruction *> SourceInstructions);

private:
  // These are all implemented in Metadata.cpp.
  LLVM_ABI MDNode *getMetadataImpl(StringRef Kind) const;
  LLVM_ABI void
  getAllMetadataImpl(SmallVectorImpl<std::pair<unsigned, MDNode *>> &) const;

  /// Update the LLVMContext ID-to-Instruction(s) mapping. If \p ID is nullptr
  /// then clear the mapping for this instruction.
  void updateDIAssignIDMapping(DIAssignID *ID);

public:
  //===--------------------------------------------------------------------===//
  // Predicates and helper methods.
  //===--------------------------------------------------------------------===//

  /// Return true if the instruction is associative.
  ///
  /// Associative operators satisfy:  x op (y op z) === (x op y) op z
  ///
  /// In LLVM, the Add, Mul, And, Or, and Xor operators are associative.
  /// \return True if this instruction is associative.
  LLVM_ABI bool isAssociative() const LLVM_READONLY;
  /// Return true if \p Opcode is an associative binary operator.
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is associative.
  static bool isAssociative(unsigned Opcode) {
    return Opcode == And || Opcode == Or || Opcode == Xor ||
           Opcode == Add || Opcode == Mul;
  }

  /// Return true if the instruction is commutative.
  ///
  /// Commutative operators satisfy: (x op y) === (y op x)
  ///
  /// In LLVM, these are the commutative operators, plus SetEQ and SetNE, when
  /// applied to any type.
  /// \return True if this instruction is commutative.
  LLVM_ABI bool isCommutative() const LLVM_READONLY;

  /// Return true if operand \p Op may be swapped in this commutative instruction.
  ///
  /// In commutative operations, not all operands might be commutable, e.g. for
  /// fmuladd only the first two operands are commutable.
  /// \param Op Zero-based operand index.
  /// \return True if the operand at \p Op is commutable.
  LLVM_ABI bool isCommutableOperand(unsigned Op) const LLVM_READONLY;

  /// Return true if \p Opcode is a commutative binary operator.
  ///
  /// Covers add, fadd, mul, fmul, and, or, and xor.
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is commutative.
  static bool isCommutative(unsigned Opcode) {
    switch (Opcode) {
    case Add: case FAdd:
    case Mul: case FMul:
    case And: case Or: case Xor:
      return true;
    default:
      return false;
  }
  }

  /// Return true if the instruction is idempotent.
  ///
  /// Idempotent operators satisfy:  x op x === x
  ///
  /// In LLVM, the And and Or operators are idempotent.
  /// \return True if this instruction is idempotent.
  bool isIdempotent() const { return isIdempotent(getOpcode()); }
  /// Return true if \p Opcode is an idempotent operator (And or Or in LLVM).
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is idempotent.
  static bool isIdempotent(unsigned Opcode) {
    return Opcode == And || Opcode == Or;
  }

  /// Return true if the instruction is nilpotent.
  ///
  /// Nilpotent operators satisfy:  x op x === Id,
  ///
  /// where Id is the identity for the operator, i.e. a constant such that
  ///   x op Id === x and Id op x === x for all x.
  ///
  /// In LLVM, the Xor operator is nilpotent.
  /// \return True if this instruction is nilpotent.
  bool isNilpotent() const { return isNilpotent(getOpcode()); }
  /// Return true if \p Opcode is a nilpotent operator (Xor in LLVM).
  ///
  /// \param Opcode The opcode to test.
  /// \return True if \p Opcode is nilpotent.
  static bool isNilpotent(unsigned Opcode) {
    return Opcode == Xor;
  }

  /// Return the memory effects of this instruction.
  ///
  /// Argmem here refers to the operands of the instruction.
  /// \return The memory effects for this instruction.
  LLVM_ABI MemoryEffects getMemoryEffects() const LLVM_READONLY;

  /// Return true if this instruction may modify memory.
  ///
  /// \return True if this instruction may write memory.
  LLVM_ABI bool mayWriteToMemory() const LLVM_READONLY;

  /// Return true if this instruction may read memory.
  ///
  /// \return True if this instruction may read memory.
  LLVM_ABI bool mayReadFromMemory() const LLVM_READONLY;

  /// Return true if this instruction may read or write memory.
  ///
  /// \return True if this instruction may access memory.
  bool mayReadOrWriteMemory() const {
    return mayReadFromMemory() || mayWriteToMemory();
  }

  /// Return true if this instruction has an AtomicOrdering of unordered or
  /// higher.
  ///
  /// \return True if this instruction is atomic.
  LLVM_ABI bool isAtomic() const LLVM_READONLY;

  /// Return true if this atomic instruction loads from memory.
  ///
  /// \return True if this atomic instruction has a load.
  LLVM_ABI bool hasAtomicLoad() const LLVM_READONLY;

  /// Return true if this atomic instruction stores to memory.
  ///
  /// \return True if this atomic instruction has a store.
  LLVM_ABI bool hasAtomicStore() const LLVM_READONLY;

  /// Return true if this instruction has a volatile memory access.
  ///
  /// \return True if this instruction is volatile.
  LLVM_ABI bool isVolatile() const LLVM_READONLY;

  /// Return true if this instruction may synchronize.
  ///
  /// Synchronization here means it may introduce a synchronizes-with edge.
  /// \return True if this instruction may synchronize.
  LLVM_ABI bool maySynchronize() const LLVM_READONLY;

  /// Return the type this instruction accesses in memory, if any.
  ///
  /// \return The accessed type, or null if there is none.
  LLVM_ABI Type *getAccessType() const LLVM_READONLY;

  /// Return true if this instruction may throw an exception.
  ///
  /// If IncludePhaseOneUnwind is set, this will also include cases where
  /// phase one unwinding may unwind past this frame due to skipping of
  /// cleanup landingpads.
  /// \param IncludePhaseOneUnwind Whether to count phase-one unwind as throwing.
  /// \return True if this instruction may throw.
  LLVM_ABI bool
  mayThrow(bool IncludePhaseOneUnwind = false) const LLVM_READONLY;

  /// Return true if this instruction behaves like a memory fence.
  ///
  /// A fence-like instruction can load or store to memory without being given
  /// an explicit memory location.
  /// \return True if this instruction is fence-like.
  bool isFenceLike() const {
    switch (getOpcode()) {
    default:
      return false;
    // This list should be kept in sync with the list in mayWriteToMemory for
    // all opcodes which don't have a memory location.
    case Instruction::Fence:
    case Instruction::CatchPad:
    case Instruction::CatchRet:
    case Instruction::Call:
    case Instruction::Invoke:
      return true;
    }
  }

  /// Return true if the instruction may have side effects.
  ///
  /// Side effects are:
  ///  * Writing to memory.
  ///  * Unwinding.
  ///  * Not returning (e.g. an infinite loop).
  ///
  /// Note that this does not consider malloc and alloca to have side
  /// effects because the newly allocated memory is completely invisible to
  /// instructions which don't use the returned value.  For cases where this
  /// matters, isSafeToSpeculativelyExecute may be more appropriate.
  /// \return True if this instruction may have side effects.
  LLVM_ABI bool mayHaveSideEffects() const LLVM_READONLY;

  /// Return true if the instruction can be removed if the result is unused.
  ///
  /// When constant folding some instructions cannot be removed even if their
  /// results are unused. Specifically terminator instructions and calls that
  /// may have side effects cannot be removed without semantically changing the
  /// generated program.
  /// \return True if an unused result allows removing this instruction.
  LLVM_ABI bool isSafeToRemove() const LLVM_READONLY;

  /// Return true if the instruction will return.
  ///
  /// Unwinding is considered a form of returning control flow here.
  /// \return True if this instruction will return.
  LLVM_ABI bool willReturn() const LLVM_READONLY;

  /// Return true if the instruction is a variety of EH-block.
  ///
  /// \return True if this instruction is an exception-handling pad.
  bool isEHPad() const {
    switch (getOpcode()) {
    case Instruction::CatchSwitch:
    case Instruction::CatchPad:
    case Instruction::CleanupPad:
    case Instruction::LandingPad:
      return true;
    default:
      return false;
    }
  }

  /// Return true if the instruction is a llvm.lifetime.start or
  /// llvm.lifetime.end marker.
  ///
  /// \return True if this is a lifetime start or end intrinsic.
  LLVM_ABI bool isLifetimeStartOrEnd() const LLVM_READONLY;

  /// Return true if the instruction is a llvm.launder.invariant.group or
  /// llvm.strip.invariant.group.
  ///
  /// \return True if this is a launder or strip invariant-group intrinsic.
  LLVM_ABI bool isLaunderOrStripInvariantGroup() const LLVM_READONLY;

  /// Return true if the instruction is a DbgInfoIntrinsic or PseudoProbeInst.
  ///
  /// \return True if this is a debug-info or pseudo-probe instruction.
  LLVM_ABI bool isDebugOrPseudoInst() const LLVM_READONLY;

  /// Create a copy of this instruction that is identical except it has no
  /// parent and no name.
  ///
  /// \return A newly allocated clone of this instruction.
  LLVM_ABI Instruction *clone() const;

  /// Return true if \p I is exactly identical to this instruction.
  ///
  /// All operands must match and any extra information (e.g. load is volatile)
  /// must agree.
  /// \param I The instruction to compare against.
  /// \return True if the instructions are exactly identical.
  LLVM_ABI bool isIdenticalTo(const Instruction *I) const LLVM_READONLY;

  /// Return true if \p I matches this instruction ignoring undefined-result
  /// flags.
  ///
  /// Like \ref isIdenticalTo, except that it ignores the SubclassOptionalData
  /// flags, which may specify conditions under which the instruction's result
  /// is undefined.
  /// \param I The instruction to compare against.
  /// \param IntersectAttrs When true, compare call attributes by intersection.
  /// \return True if the instructions match when defined.
  LLVM_ABI bool
  isIdenticalToWhenDefined(const Instruction *I,
                           bool IntersectAttrs = false) const LLVM_READONLY;

  /// When checking for operation equivalence (using isSameOperationAs) it is
  /// sometimes useful to ignore certain attributes.
  enum OperationEquivalenceFlags {
    /// Check for equivalence ignoring load/store alignment.
    CompareIgnoringAlignment = 1 << 0,
    /// Check for equivalence treating a type and a vector of that type
    /// as equivalent.
    CompareUsingScalarTypes = 1 << 1,
    /// Check for equivalence with intersected callbase attrs.
    CompareUsingIntersectedAttrs = 1 << 2,
  };

  /// Return true if \p I executes the same operation as this instruction.
  ///
  /// The opcodes, type, operand types and any other factors affecting the
  /// operation must be the same. This is similar to isIdenticalTo except the
  /// operands themselves don't have to be identical.
  /// \param I The instruction to compare against.
  /// \param flags Bitmask of \ref OperationEquivalenceFlags to relax the check.
  /// \return True if the specified instruction is the same operation as this
  ///         one.
  LLVM_ABI bool isSameOperationAs(const Instruction *I,
                                  unsigned flags = 0) const LLVM_READONLY;

  /// Return true if \p I2 has the same opcode-specific state as this instruction.
  ///
  /// Opcode-specific details must match. As a common example, if comparing
  /// loads, this compares the alignments (among other things).
  /// \param I2 The instruction to compare against.
  /// \param IgnoreAlignment When true, do not compare load/store alignment.
  /// \param IntersectAttrs When true, compare call attributes by intersection.
  /// \return True if the instructions share the same opcode-specific state.
  LLVM_ABI bool
  hasSameSpecialState(const Instruction *I2, bool IgnoreAlignment = false,
                      bool IntersectAttrs = false) const LLVM_READONLY;

  /// Return true if any use of this instruction is outside \p BB.
  ///
  /// Note that PHI nodes are considered to evaluate their operands in the
  /// corresponding predecessor block.
  /// \param BB The block to treat as the local use block.
  /// \return True if there is a use outside \p BB.
  LLVM_ABI bool isUsedOutsideOfBlock(const BasicBlock *BB) const LLVM_READONLY;

  /// Return the number of successors that this terminator has.
  ///
  /// The instruction must be a terminator.
  /// \return The number of successor basic blocks.
  LLVM_ABI unsigned getNumSuccessors() const LLVM_READONLY;

  /// Return the successor at index \p Idx.
  ///
  /// This instruction must be a terminator.
  /// \param Idx Zero-based successor index.
  /// \return The successor basic block at \p Idx.
  LLVM_ABI BasicBlock *getSuccessor(unsigned Idx) const LLVM_READONLY;

  /// Update the successor at index \p Idx to point at \p BB.
  ///
  /// This instruction must be a terminator.
  /// \param Idx Zero-based successor index.
  /// \param BB The new successor basic block.
  LLVM_ABI void setSuccessor(unsigned Idx, BasicBlock *BB);

  /// Return a range over this terminator's successors.
  ///
  /// \return A const range of successor basic blocks.
  LLVM_ABI iterator_range<const_succ_iterator> successors() const LLVM_READONLY;
  /// Return a mutable range over this terminator's successors.
  ///
  /// \return A range of successor basic blocks.
  iterator_range<succ_iterator> successors() {
    auto Ops = static_cast<const Instruction *>(this)->successors();
    Use *Begin = const_cast<Use *>(Ops.begin().getUse());
    Use *End = const_cast<Use *>(Ops.end().getUse());
    return make_range(succ_iterator(Begin), succ_iterator(End));
  }

  /// Replace successor \p OldBB with \p NewBB.
  ///
  /// This instruction must be a terminator.
  /// \param OldBB The successor block to replace.
  /// \param NewBB The block that should replace \p OldBB.
  LLVM_ABI void replaceSuccessorWith(BasicBlock *OldBB, BasicBlock *NewBB);

  /// Return true if \p V is an Instruction.
  ///
  /// \param V The value to test.
  /// \return True if \p V is an instruction.
  static bool classof(const Value *V) {
    return V->getValueID() >= Value::InstructionVal;
  }

  //----------------------------------------------------------------------
  // Exported enumerations.
  //
  /// Opcodes that terminate a basic block (return, branch, switch, etc.).
  enum TermOps {
#define  FIRST_TERM_INST(N)             TermOpsBegin = N,
#define HANDLE_TERM_INST(N, OPC, CLASS) OPC = N,
#define   LAST_TERM_INST(N)             TermOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  /// Opcodes for unary instructions (e.g. fneg).
  enum UnaryOps {
#define  FIRST_UNARY_INST(N)             UnaryOpsBegin = N,
#define HANDLE_UNARY_INST(N, OPC, CLASS) OPC = N,
#define   LAST_UNARY_INST(N)             UnaryOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  /// Opcodes for binary operators (arithmetic, shifts, and bitwise logic).
  enum BinaryOps {
#define  FIRST_BINARY_INST(N)             BinaryOpsBegin = N,
#define HANDLE_BINARY_INST(N, OPC, CLASS) OPC = N,
#define   LAST_BINARY_INST(N)             BinaryOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  /// Opcodes for memory instructions (alloca, load, store, GEP, atomics, etc.).
  enum MemoryOps {
#define  FIRST_MEMORY_INST(N)             MemoryOpsBegin = N,
#define HANDLE_MEMORY_INST(N, OPC, CLASS) OPC = N,
#define   LAST_MEMORY_INST(N)             MemoryOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  /// Opcodes for cast instructions (trunc, zext, bitcast, addrspacecast, etc.).
  enum CastOps {
#define  FIRST_CAST_INST(N)             CastOpsBegin = N,
#define HANDLE_CAST_INST(N, OPC, CLASS) OPC = N,
#define   LAST_CAST_INST(N)             CastOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  /// Opcodes for funclet pad instructions (cleanuppad, catchpad).
  enum FuncletPadOps {
#define  FIRST_FUNCLETPAD_INST(N)             FuncletPadOpsBegin = N,
#define HANDLE_FUNCLETPAD_INST(N, OPC, CLASS) OPC = N,
#define   LAST_FUNCLETPAD_INST(N)             FuncletPadOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

  /// Opcodes for other non-terminating instructions (cmp, phi, call, select, etc.).
  enum OtherOps {
#define  FIRST_OTHER_INST(N)             OtherOpsBegin = N,
#define HANDLE_OTHER_INST(N, OPC, CLASS) OPC = N,
#define   LAST_OTHER_INST(N)             OtherOpsEnd = N+1
#include "llvm/IR/Instruction.def"
  };

private:
  friend class SymbolTableListTraits<Instruction, ilist_iterator_bits<true>,
                                     ilist_parent<BasicBlock>>;
  friend class BasicBlock; // For renumbering.

  // Shadow Value::setValueSubclassData with a private forwarding method so that
  // subclasses cannot accidentally use it.
  void setValueSubclassData(unsigned short D) {
    Value::setValueSubclassData(D);
  }

  unsigned short getSubclassDataFromValue() const {
    return Value::getSubclassDataFromValue();
  }

protected:
  // Instruction subclasses can stick up to 16 bits of stuff into the
  // SubclassData field of instruction with these members.

  /// Read a bitfield element from this instruction's subclass data.
  ///
  /// \tparam BitfieldElement Bitfield element type describing the layout.
  /// \return The decoded bitfield value.
  template <typename BitfieldElement>
  typename BitfieldElement::Type getSubclassData() const {
    return Bitfield::get<BitfieldElement>(getSubclassDataFromValue());
  }

  /// Store a bitfield element into this instruction's subclass data.
  ///
  /// \tparam BitfieldElement Bitfield element type describing the layout.
  /// \param Value The value to encode into the subclass data.
  template <typename BitfieldElement>
  void setSubclassData(typename BitfieldElement::Type Value) {
    auto Storage = getSubclassDataFromValue();
    Bitfield::set<BitfieldElement>(Storage, Value);
    setValueSubclassData(Storage);
  }

  /// Construct an instruction of type \p Ty with opcode \p iType.
  ///
  /// \param Ty The result type of the instruction.
  /// \param iType The opcode (subclass ID) for this instruction.
  /// \param AllocInfo Operand allocation information for the User base.
  /// \param InsertBefore Optional insertion point; null leaves the instruction
  ///        unlinked.
  LLVM_ABI Instruction(Type *Ty, unsigned iType, AllocInfo AllocInfo,
                       InsertPosition InsertBefore = nullptr);

private:
  /// Create a copy of this instruction.
  Instruction *cloneImpl() const;
};

/// Destroy an instruction node removed from an instruction list.
///
/// \param V The instruction to delete.
inline void ilist_alloc_traits<Instruction>::deleteNode(Instruction *V) {
  V->deleteValue();
}

} // end namespace llvm

#endif // LLVM_IR_INSTRUCTION_H
