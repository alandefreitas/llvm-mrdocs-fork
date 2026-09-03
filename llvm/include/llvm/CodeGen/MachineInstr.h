//===- llvm/CodeGen/MachineInstr.h - MachineInstr class ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MachineInstr class, which is the
// basic representation for all target dependent machine instructions used by
// the back end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEINSTR_H
#define LLVM_CODEGEN_MACHINEINSTR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerSumType.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ArrayRecycler.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TrailingObjects.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>

namespace llvm {

/// Metadata class for DWARF/debug label instructions.
class DILabel;
class Instruction;
class MDNode;
class AAResults;
class BatchAAResults;
class DIExpression;
class DILocalVariable;
class LiveRegUnits;
class MachineBasicBlock;
class MachineFunction;
class MachineRegisterInfo;
class ModuleSlotTracker;
class raw_ostream;
template <typename T> class SmallVectorImpl;
class SmallBitVector;
class StringRef;
class TargetInstrInfo;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class TargetRegisterInfo;

//===----------------------------------------------------------------------===//
/// Representation of each machine instruction.
///
/// This class isn't a POD type, but it must have a trivial destructor. When a
/// MachineFunction is deleted, all the contained MachineInstrs are deallocated
/// without having their destructor called.
///
class MachineInstr
    : public ilist_node_with_parent<MachineInstr, MachineBasicBlock,
                                    ilist_sentinel_tracking<true>> {
public:
  /// Iterator over MachineMemOperand pointers attached to an instruction.
  using mmo_iterator = ArrayRef<MachineMemOperand *>::iterator;

  /// Underlying integer type for AsmPrinter CommentFlag bit values.
  using AsmPrinterFlagTy = uint8_t;

  /// Flags to specify different kinds of comments to output in
  /// assembly code.  These flags carry semantic information not
  /// otherwise easily derivable from the IR text.
  enum CommentFlag : AsmPrinterFlagTy {
    /// Operand was rematerialized / reused after a reload (AsmPrinter comment).
    ReloadReuse = 0x1, // higher bits are reserved for target dep comments.
    /// Suppress scheduler-related AsmPrinter comments.
    NoSchedComment = 0x2,
    /// Base value for target-specific AsmPrinter comment flags.
    TAsmComments = 0x4 // Target Asm comments should start from this value.
  };

  /// Semantic flags carried on a MachineInstr beyond the MCInstrDesc.
  enum MIFlag {
    /// No MI flags are set.
    NoFlags = 0,
    /// Instruction is part of function frame setup code.
    FrameSetup = 1 << 0,
    /// Instruction is part of function frame destruction code.
    FrameDestroy = 1 << 1,
    /// Instruction has bundled predecessors.
    BundledPred = 1 << 2,
    /// Instruction has bundled successors.
    BundledSucc = 1 << 3,
    /// Instruction does not support Fast Math NaN values.
    FmNoNans = 1 << 4,
    /// Instruction does not support Fast Math infinity values.
    FmNoInfs = 1 << 5,
    /// Instruction is not required to retain signed zero values.
    FmNsz = 1 << 6,
    /// Instruction supports Fast Math reciprocal approximations.
    FmArcp = 1 << 7,
    /// Instruction supports Fast Math contraction (e.g. fma).
    FmContract = 1 << 8,
    /// Instruction may map to a Fast Math intrinsic approximation.
    FmAfn = 1 << 9,
    /// Instruction supports Fast Math reassociation of operand order.
    FmReassoc = 1 << 10,
    /// Binary operator has no unsigned wrap.
    NoUWrap = 1 << 11,
    /// Binary operator has no signed wrap.
    NoSWrap = 1 << 12,
    /// Division is known to be exact.
    IsExact = 1 << 13,
    /// Instruction does not raise floating-point exceptions.
    NoFPExcept = 1 << 14,
    /// Passes that drop source locations should skip this instruction.
    NoMerge = 1 << 15,
    /// Instruction has an unpredictable condition.
    Unpredictable = 1 << 16,
    /// Call does not require convergence guarantees.
    NoConvergent = 1 << 17,
    /// The operand is non-negative.
    NonNeg = 1 << 18,
    /// Each bit is zero in at least one of the inputs.
    Disjoint = 1 << 19,
    /// GEP has no unsigned/signed wrap.
    NoUSWrap = 1 << 20,
    /// Both operands have the same sign.
    SameSign = 1 << 21,
    /// Pointer arithmetic remains in-bounds (implies NoUSWrap).
    InBounds = 1 << 22,
    /// Instruction used for live-range splitting.
    LRSplit = 1 << 23
  };

private:
  const MCInstrDesc *MCID;              // Instruction descriptor.
  MachineBasicBlock *Parent = nullptr;  // Pointer to the owning basic block.

  // Operands are allocated by an ArrayRecycler.
  MachineOperand *Operands = nullptr;   // Pointer to the first operand.

#define LLVM_MI_NUMOPERANDS_BITS 24
#define LLVM_MI_FLAGS_BITS 32
#define LLVM_MI_ASMPRINTERFLAGS_BITS 8

  /// Number of operands on instruction.
  uint32_t NumOperands : LLVM_MI_NUMOPERANDS_BITS;

  // OperandCapacity has uint8_t size, so it should be next to NumOperands
  // to properly pack.
  using OperandCapacity = ArrayRecycler<MachineOperand>::Capacity;
  OperandCapacity CapOperands;          // Capacity of the Operands array.

  /// Various bits of additional information about the machine instruction.
  uint32_t Flags;

  /// Various bits of information used by the AsmPrinter to emit helpful
  /// comments.  This is *not* semantic information.  Do not use this for
  /// anything other than to convey comment information to AsmPrinter.
  AsmPrinterFlagTy AsmPrinterFlags;

  /// Cached opcode from MCID.
  uint32_t Opcode;

  /// Unique instruction number. Used by DBG_INSTR_REFs to refer to the values
  /// defined by this instruction.
  unsigned DebugInstrNum;

  /// Internal implementation detail class that provides out-of-line storage for
  /// extra info used by the machine instruction when this info cannot be stored
  /// in-line within the instruction itself.
  ///
  /// This has to be defined eagerly due to the implementation constraints of
  /// `PointerSumType` where it is used.
  class ExtraInfo final
      : TrailingObjects<ExtraInfo, MachineMemOperand *, MCSymbol *, MDNode *,
                        uint32_t, Value *> {
  public:
    static ExtraInfo *create(BumpPtrAllocator &Allocator,
                             ArrayRef<MachineMemOperand *> MMOs,
                             MCSymbol *PreInstrSymbol = nullptr,
                             MCSymbol *PostInstrSymbol = nullptr,
                             MDNode *HeapAllocMarker = nullptr,
                             MDNode *PCSections = nullptr, uint32_t CFIType = 0,
                             MDNode *MMRAs = nullptr, Value *DS = nullptr) {
      bool HasPreInstrSymbol = PreInstrSymbol != nullptr;
      bool HasPostInstrSymbol = PostInstrSymbol != nullptr;
      bool HasHeapAllocMarker = HeapAllocMarker != nullptr;
      bool HasMMRAs = MMRAs != nullptr;
      bool HasCFIType = CFIType != 0;
      bool HasPCSections = PCSections != nullptr;
      bool HasDS = DS != nullptr;
      auto *Result = new (Allocator.Allocate(
          totalSizeToAlloc<MachineMemOperand *, MCSymbol *, MDNode *, uint32_t,
                           Value *>(
              MMOs.size(), HasPreInstrSymbol + HasPostInstrSymbol,
              HasHeapAllocMarker + HasPCSections + HasMMRAs, HasCFIType, HasDS),
          alignof(ExtraInfo)))
          ExtraInfo(MMOs.size(), HasPreInstrSymbol, HasPostInstrSymbol,
                    HasHeapAllocMarker, HasPCSections, HasCFIType, HasMMRAs,
                    HasDS);

      // Copy the actual data into the trailing objects.
      llvm::copy(MMOs, Result->getTrailingObjects<MachineMemOperand *>());

      unsigned MDNodeIdx = 0;

      if (HasPreInstrSymbol)
        Result->getTrailingObjects<MCSymbol *>()[0] = PreInstrSymbol;
      if (HasPostInstrSymbol)
        Result->getTrailingObjects<MCSymbol *>()[HasPreInstrSymbol] =
            PostInstrSymbol;
      if (HasHeapAllocMarker)
        Result->getTrailingObjects<MDNode *>()[MDNodeIdx++] = HeapAllocMarker;
      if (HasPCSections)
        Result->getTrailingObjects<MDNode *>()[MDNodeIdx++] = PCSections;
      if (HasCFIType)
        Result->getTrailingObjects<uint32_t>()[0] = CFIType;
      if (HasMMRAs)
        Result->getTrailingObjects<MDNode *>()[MDNodeIdx++] = MMRAs;
      if (HasDS)
        Result->getTrailingObjects<Value *>()[0] = DS;

      return Result;
    }

    ArrayRef<MachineMemOperand *> getMMOs() const {
      return ArrayRef(getTrailingObjects<MachineMemOperand *>(), NumMMOs);
    }

    MCSymbol *getPreInstrSymbol() const {
      return HasPreInstrSymbol ? getTrailingObjects<MCSymbol *>()[0] : nullptr;
    }

    MCSymbol *getPostInstrSymbol() const {
      return HasPostInstrSymbol
                 ? getTrailingObjects<MCSymbol *>()[HasPreInstrSymbol]
                 : nullptr;
    }

    MDNode *getHeapAllocMarker() const {
      return HasHeapAllocMarker ? getTrailingObjects<MDNode *>()[0] : nullptr;
    }

    MDNode *getPCSections() const {
      return HasPCSections
                 ? getTrailingObjects<MDNode *>()[HasHeapAllocMarker]
                 : nullptr;
    }

    uint32_t getCFIType() const {
      return HasCFIType ? getTrailingObjects<uint32_t>()[0] : 0;
    }

    MDNode *getMMRAMetadata() const {
      return HasMMRAs ? getTrailingObjects<MDNode *>()[HasHeapAllocMarker +
                                                       HasPCSections]
                      : nullptr;
    }

    Value *getDeactivationSymbol() const {
      return HasDS ? getTrailingObjects<Value *>()[0] : 0;
    }

  private:
    friend TrailingObjects;

    // Description of the extra info, used to interpret the actual optional
    // data appended.
    //
    // Note that this is not terribly space optimized. This leaves a great deal
    // of flexibility to fit more in here later.
    const int NumMMOs;
    const bool HasPreInstrSymbol;
    const bool HasPostInstrSymbol;
    const bool HasHeapAllocMarker;
    const bool HasPCSections;
    const bool HasCFIType;
    const bool HasMMRAs;
    const bool HasDS;

    // Implement the `TrailingObjects` internal API.
    size_t numTrailingObjects(OverloadToken<MachineMemOperand *>) const {
      return NumMMOs;
    }
    size_t numTrailingObjects(OverloadToken<MCSymbol *>) const {
      return HasPreInstrSymbol + HasPostInstrSymbol;
    }
    size_t numTrailingObjects(OverloadToken<MDNode *>) const {
      return HasHeapAllocMarker + HasPCSections;
    }
    size_t numTrailingObjects(OverloadToken<uint32_t>) const {
      return HasCFIType;
    }
    size_t numTrailingObjects(OverloadToken<Value *>) const { return HasDS; }

    // Just a boring constructor to allow us to initialize the sizes. Always use
    // the `create` routine above.
    ExtraInfo(int NumMMOs, bool HasPreInstrSymbol, bool HasPostInstrSymbol,
              bool HasHeapAllocMarker, bool HasPCSections, bool HasCFIType,
              bool HasMMRAs, bool HasDS)
        : NumMMOs(NumMMOs), HasPreInstrSymbol(HasPreInstrSymbol),
          HasPostInstrSymbol(HasPostInstrSymbol),
          HasHeapAllocMarker(HasHeapAllocMarker), HasPCSections(HasPCSections),
          HasCFIType(HasCFIType), HasMMRAs(HasMMRAs), HasDS(HasDS) {}
  };

  /// Enumeration of the kinds of inline extra info available. It is important
  /// that the `MachineMemOperand` inline kind has a tag value of zero to make
  /// it accessible as an `ArrayRef`.
  enum ExtraInfoInlineKinds {
    EIIK_MMO = 0,
    EIIK_PreInstrSymbol,
    EIIK_PostInstrSymbol,
    EIIK_OutOfLine
  };

  // We store extra information about the instruction here. The common case is
  // expected to be nothing or a single pointer (typically a MMO or a symbol).
  // We work to optimize this common case by storing it inline here rather than
  // requiring a separate allocation, but we fall back to an allocation when
  // multiple pointers are needed.
  PointerSumType<ExtraInfoInlineKinds,
                 PointerSumTypeMember<EIIK_MMO, MachineMemOperand *>,
                 PointerSumTypeMember<EIIK_PreInstrSymbol, MCSymbol *>,
                 PointerSumTypeMember<EIIK_PostInstrSymbol, MCSymbol *>,
                 PointerSumTypeMember<EIIK_OutOfLine, ExtraInfo *>>
      Info;

  DebugLoc DbgLoc; // Source line information.

  // Intrusive list support
  friend struct ilist_traits<MachineInstr>;
  friend struct ilist_callback_traits<MachineBasicBlock>;
  void setParent(MachineBasicBlock *P) { Parent = P; }

  /// This constructor creates a copy of the given
  /// MachineInstr in the given MachineFunction.
  MachineInstr(MachineFunction &, const MachineInstr &);

  /// This constructor create a MachineInstr and add the implicit operands.
  /// It reserves space for number of operands specified by
  /// MCInstrDesc.  An explicit DebugLoc is supplied.
  MachineInstr(MachineFunction &, const MCInstrDesc &TID, DebugLoc DL,
               bool NoImp = false);

  // MachineInstrs are pool-allocated and owned by MachineFunction.
  friend class MachineFunction;

  void
  dumprImpl(const MachineRegisterInfo &MRI, unsigned Depth, unsigned MaxDepth,
            SmallPtrSetImpl<const MachineInstr *> &AlreadySeenInstrs) const;

  static bool opIsRegDef(const MachineOperand &Op) {
    return Op.isReg() && Op.isDef();
  }

  static bool opIsRegUse(const MachineOperand &Op) {
    return Op.isReg() && Op.isUse();
  }

  MutableArrayRef<MachineOperand> operands_impl() {
    return {Operands, NumOperands};
  }
  ArrayRef<MachineOperand> operands_impl() const {
    return {Operands, NumOperands};
  }

public:
  /// MachineInstr is not copyable; create instructions via MachineFunction.
  /// @param Other Unused; copy construction is deleted.
  MachineInstr(const MachineInstr &Other) = delete;
  /// Assignment is deleted; MachineInstr is not copyable.
  /// @param Other Unused; copy assignment is deleted.
  MachineInstr &operator=(const MachineInstr &Other) = delete;
  /// Destructor is deleted; use MachineFunction::DeleteMachineInstr() instead.
  ~MachineInstr() = delete;

  /// Return the basic block that contains this instruction.
  /// @return Const pointer to the parent MachineBasicBlock, or null.
  const MachineBasicBlock* getParent() const { return Parent; }
  /// Return the mutable basic block that contains this instruction.
  /// @return Mutable pointer to the parent MachineBasicBlock, or null.
  MachineBasicBlock* getParent() { return Parent; }

  /// Move this instruction before \p MovePos.
  /// @param MovePos Instruction before which this one is moved.
  LLVM_ABI void moveBefore(MachineInstr *MovePos);

  /// Return the MachineFunction that contains this instruction.
  ///
  /// Note: undefined behavior if the instruction has no parent.
  /// @return Const pointer to the owning MachineFunction.
  LLVM_ABI const MachineFunction *getMF() const;
  /// Return the mutable MachineFunction that contains this instruction.
  /// @return Mutable pointer to the owning MachineFunction.
  MachineFunction *getMF() {
    return const_cast<MachineFunction *>(
        static_cast<const MachineInstr *>(this)->getMF());
  }

  /// Return the asm printer flags bitvector.
  /// @return Bitvector of AsmPrinter CommentFlag values.
  AsmPrinterFlagTy getAsmPrinterFlags() const { return AsmPrinterFlags; }

  /// Clear the AsmPrinter bitvector.
  void clearAsmPrinterFlags() { AsmPrinterFlags = 0; }

  /// Return whether AsmPrinter flag \p Flag is set.
  /// @param Flag AsmPrinter comment flag to test.
  /// @return True if \p Flag is set.
  bool getAsmPrinterFlag(AsmPrinterFlagTy Flag) const {
    assert(isUInt<LLVM_MI_ASMPRINTERFLAGS_BITS>(Flag) &&
           "Flag is out of range for the AsmPrinterFlags field");
    return AsmPrinterFlags & Flag;
  }

  /// Set AsmPrinter flag \p Flag.
  /// @param Flag AsmPrinter comment flag to set.
  void setAsmPrinterFlag(AsmPrinterFlagTy Flag) {
    assert(isUInt<LLVM_MI_ASMPRINTERFLAGS_BITS>(Flag) &&
           "Flag is out of range for the AsmPrinterFlags field");
    AsmPrinterFlags |= Flag;
  }

  /// Clear AsmPrinter flag \p Flag.
  /// @param Flag AsmPrinter comment flag to clear.
  void clearAsmPrinterFlag(AsmPrinterFlagTy Flag) {
    assert(isUInt<LLVM_MI_ASMPRINTERFLAGS_BITS>(Flag) &&
           "Flag is out of range for the AsmPrinterFlags field");
    AsmPrinterFlags &= ~Flag;
  }

  /// Return the MI flags bitvector.
  /// @return Bitvector of MIFlag values.
  uint32_t getFlags() const {
    return Flags;
  }

  /// Return whether MI flag \p Flag is set.
  /// @param Flag MI flag to test.
  /// @return True if \p Flag is set.
  bool getFlag(MIFlag Flag) const {
    assert(isUInt<LLVM_MI_FLAGS_BITS>(unsigned(Flag)) &&
           "Flag is out of range for the Flags field");
    return Flags & Flag;
  }

  /// Set MI flag \p Flag.
  /// @param Flag MI flag to set.
  void setFlag(MIFlag Flag) {
    assert(isUInt<LLVM_MI_FLAGS_BITS>(unsigned(Flag)) &&
           "Flag is out of range for the Flags field");
    Flags |= (uint32_t)Flag;
  }

  /// Set MI flags from bitvector \p flags, preserving bundle flags.
  /// @param flags Bitmask of MIFlag values to set.
  void setFlags(unsigned flags) {
    assert(isUInt<LLVM_MI_FLAGS_BITS>(flags) &&
           "flags to be set are out of range for the Flags field");
    // Filter out the automatically maintained flags.
    unsigned Mask = BundledPred | BundledSucc;
    Flags = (Flags & Mask) | (flags & ~Mask);
  }

  /// Clear MI flag \p Flag.
  /// @param Flag MI flag to clear.
  void clearFlag(MIFlag Flag) {
    assert(isUInt<LLVM_MI_FLAGS_BITS>(unsigned(Flag)) &&
           "Flag to clear is out of range for the Flags field");
    Flags &= ~((uint32_t)Flag);
  }

  /// Clear the MI flags bits specified by \p flags.
  /// @param flags Bitmask of MIFlag values to clear.
  void clearFlags(unsigned flags) {
    assert(isUInt<LLVM_MI_FLAGS_BITS>(flags) &&
           "flags to be cleared are out of range for the Flags field");
    Flags &= ~flags;
  }

  /// Return true if MI is in a bundle (but not the first MI in a bundle).
  ///
  /// A bundle looks like this before it's finalized:
  ///   ----------------
  ///   |      MI      |
  ///   ----------------
  ///          |
  ///   ----------------
  ///   |      MI    * |
  ///   ----------------
  ///          |
  ///   ----------------
  ///   |      MI    * |
  ///   ----------------
  /// In this case, the first MI starts a bundle but is not inside a bundle, the
  /// next 2 MIs are considered "inside" the bundle.
  ///
  /// After a bundle is finalized, it looks like this:
  ///   ----------------
  ///   |    Bundle    |
  ///   ----------------
  ///          |
  ///   ----------------
  ///   |      MI    * |
  ///   ----------------
  ///          |
  ///   ----------------
  ///   |      MI    * |
  ///   ----------------
  ///          |
  ///   ----------------
  ///   |      MI    * |
  ///   ----------------
  /// The first instruction has the special opcode "BUNDLE". It's not "inside"
  /// a bundle, but the next three MIs are.
  /// @return True if BundledPred is set.
  bool isInsideBundle() const {
    return getFlag(BundledPred);
  }

  /// Return true if this instruction is part of a bundle.
  /// @return True if bundled with a predecessor or successor.
  bool isBundled() const {
    return isBundledWithPred() || isBundledWithSucc();
  }

  /// Return true if this instruction is bundled with a predecessor.
  /// @return True if BundledPred is set.
  bool isBundledWithPred() const { return getFlag(BundledPred); }

  /// Return true if this instruction is bundled with a successor.
  /// @return True if BundledSucc is set.
  bool isBundledWithSucc() const { return getFlag(BundledSucc); }

  /// Bundle this instruction with its predecessor. This can be an unbundled
  /// instruction, or it can be the first instruction in a bundle.
  LLVM_ABI void bundleWithPred();

  /// Bundle this instruction with its successor. This can be an unbundled
  /// instruction, or it can be the last instruction in a bundle.
  LLVM_ABI void bundleWithSucc();

  /// Break bundle above this instruction.
  LLVM_ABI void unbundleFromPred();

  /// Break bundle below this instruction.
  LLVM_ABI void unbundleFromSucc();

  /// Returns the debug location of this MachineInstr.
  /// @return Debug location attached to this instruction.
  const DebugLoc &getDebugLoc() const { return DbgLoc; }

  /// Return the offset operand used when this DBG_VALUE is indirect.
  ///
  /// Invalid register if not indirect; otherwise an immediate (often 0).
  /// @return Const reference to the debug offset operand.
  const MachineOperand &getDebugOffset() const {
    assert(isNonListDebugValue() && "not a DBG_VALUE");
    return getOperand(1);
  }
  /// Return the mutable offset operand used when this DBG_VALUE is indirect.
  /// @return Mutable reference to the debug offset operand.
  MachineOperand &getDebugOffset() {
    assert(isNonListDebugValue() && "not a DBG_VALUE");
    return getOperand(1);
  }

  /// Return the const operand holding this DBG_VALUE's variable.
  /// @return Const reference to the debug variable operand.
  LLVM_ABI const MachineOperand &getDebugVariableOp() const;
  /// Return the mutable operand holding this DBG_VALUE's variable.
  /// @return Mutable reference to the debug variable operand.
  LLVM_ABI MachineOperand &getDebugVariableOp();

  /// Return the debug variable referenced by this DBG_VALUE instruction.
  /// @return Debug variable metadata for this instruction.
  LLVM_ABI const DILocalVariable *getDebugVariable() const;

  /// Return the const operand holding this DBG_VALUE's expression.
  /// @return Const reference to the debug expression operand.
  LLVM_ABI const MachineOperand &getDebugExpressionOp() const;
  /// Return the mutable operand holding this DBG_VALUE's expression.
  /// @return Mutable reference to the debug expression operand.
  LLVM_ABI MachineOperand &getDebugExpressionOp();

  /// Return the complex address expression referenced by this DBG_VALUE.
  /// @return Debug expression metadata for this instruction.
  LLVM_ABI const DIExpression *getDebugExpression() const;

  /// Return the debug label referenced by this DBG_LABEL instruction.
  /// @return Debug label metadata for this instruction.
  LLVM_ABI const DILabel *getDebugLabel() const;

  /// Fetch this instruction's debug number, assigning one if needed.
  /// @return Debug instruction number for this instruction.
  LLVM_ABI unsigned getDebugInstrNum();

  /// Fetch or assign a debug instruction number before insertion into \p MF.
  ///
  /// Needed when creating an instruction that is not immediately inserted.
  /// @param MF Machine function that owns the instruction number space.
  /// @return Debug instruction number for this instruction.
  LLVM_ABI unsigned getDebugInstrNum(MachineFunction &MF);

  /// Examine this instruction's debug number without assigning one.
  /// @return Current debug instruction number, or 0 if unassigned.
  unsigned peekDebugInstrNum() const { return DebugInstrNum; }

  /// Set this instruction's debug instruction number.
  ///
  /// Avoid using unless deserializing this information.
  /// @param Num Debug instruction number to assign.
  void setDebugInstrNum(unsigned Num) { DebugInstrNum = Num; }

  /// Drop variable-location debugging information associated with this instruction.
  ///
  /// Use when the instruction no longer defines the value it used to.
  void dropDebugNumber() { DebugInstrNum = 0; }

  /// For inline asm, return the !srcloc metadata node if present.
  /// @return Source-location cookie metadata, or null if none.
  LLVM_ABI const MDNode *getLocCookieMD() const;

  /// Emit an error at this instruction's source location for impossible inline asm.
  ///
  /// Other errors should have been handled much earlier.
  /// @param ErrMsg Error message text.
  LLVM_ABI void emitInlineAsmError(const Twine &ErrMsg) const;

  /// Emit an error referring to this instruction's source location if available.
  /// @param ErrMsg Error message text.
  LLVM_ABI void emitGenericError(const Twine &ErrMsg) const;

  /// Returns the target instruction descriptor of this MachineInstr.
  /// @return Reference to this instruction's MCInstrDesc.
  const MCInstrDesc &getDesc() const { return *MCID; }

  /// Returns the opcode of this MachineInstr.
  /// @return Opcode number of this instruction.
  unsigned getOpcode() const { return Opcode; }

  /// Return the total number of operands.
  /// @return Operand count.
  unsigned getNumOperands() const { return NumOperands; }

  /// Returns the total number of operands which are debug locations.
  /// @return Count of debug operands.
  unsigned getNumDebugOperands() const { return size(debug_operands()); }

  /// Return const operand \p i.
  /// @param i Operand index.
  /// @return Const reference to the operand.
  const MachineOperand &getOperand(unsigned i) const {
    return operands_impl()[i];
  }
  /// Return mutable operand \p i.
  /// @param i Operand index.
  /// @return Mutable reference to the operand.
  MachineOperand &getOperand(unsigned i) { return operands_impl()[i]; }

  /// Return the debug operand at \p Index within debug_operands().
  /// @param Index Index into the debug operand range.
  /// @return Mutable reference to the debug operand.
  MachineOperand &getDebugOperand(unsigned Index) {
    assert(Index < getNumDebugOperands() && "getDebugOperand() out of range!");
    return *(debug_operands().begin() + Index);
  }
  /// Return the const debug operand at \p Index within debug_operands().
  /// @param Index Index into the debug operand range.
  /// @return Const reference to the debug operand.
  const MachineOperand &getDebugOperand(unsigned Index) const {
    assert(Index < getNumDebugOperands() && "getDebugOperand() out of range!");
    return *(debug_operands().begin() + Index);
  }

  /// Return true if this debug value has a debug operand using \p Reg.
  /// @param Reg Register to look for among debug operands.
  /// @return True if a debug operand uses \p Reg.
  bool hasDebugOperandForReg(Register Reg) const {
    return any_of(debug_operands(), [Reg](const MachineOperand &Op) {
      return Op.isReg() && Op.getReg() == Reg;
    });
  }

  /// Return debug operands that use register \p Reg.
  /// @param Reg Register whose debug uses are sought.
  /// @return Filtered const range of matching debug operands.
  LLVM_ABI iterator_range<filter_iterator<
      const MachineOperand *, std::function<bool(const MachineOperand &Op)>>>
  getDebugOperandsForReg(Register Reg) const;
  /// Return mutable debug operands that use register \p Reg.
  /// @param Reg Register whose debug uses are sought.
  /// @return Filtered mutable range of matching debug operands.
  LLVM_ABI
  iterator_range<filter_iterator<MachineOperand *,
                                 std::function<bool(MachineOperand &Op)>>>
  getDebugOperandsForReg(Register Reg);

  /// Return true if \p Op is one of this instruction's debug operands.
  /// @param Op Operand pointer to test.
  /// @return True if \p Op lies in debug_operands().
  bool isDebugOperand(const MachineOperand *Op) const {
    return Op >= adl_begin(debug_operands()) && Op <= adl_end(debug_operands());
  }

  /// Return the index of debug operand \p Op within \c debug_operands().
  /// @param Op Debug operand pointer within this instruction.
  /// @return Index of \p Op in the debug operand range.
  unsigned getDebugOperandIndex(const MachineOperand *Op) const {
    assert(isDebugOperand(Op) && "Expected a debug operand.");
    return std::distance(adl_begin(debug_operands()), Op);
  }

  /// Returns the total number of definitions (explicit and implicit).
  /// @return Total definition count.
  unsigned getNumDefs() const {
    return getNumExplicitDefs() + MCID->implicit_defs().size();
  }

  /// Returns true if the instruction has an implicit definition.
  /// @return True if any implicit operand is a def.
  bool hasImplicitDef() const {
    for (const MachineOperand &MO : implicit_operands())
      if (MO.isDef())
        return true;
    return false;
  }

  /// Returns the number of implicit operands.
  /// @return Count of implicit operands.
  unsigned getNumImplicitOperands() const {
    return getNumOperands() - getNumExplicitOperands();
  }

  /// Return true if operand \p OpIdx is a subregister index.
  /// @param OpIdx Operand index to check.
  /// @return True if the operand is a subregister index immediate.
  bool isOperandSubregIdx(unsigned OpIdx) const {
    assert(getOperand(OpIdx).isImm() && "Expected MO_Immediate operand type.");
    if (isExtractSubreg() && OpIdx == 2)
      return true;
    if (isInsertSubreg() && OpIdx == 3)
      return true;
    if (isRegSequence() && OpIdx > 1 && (OpIdx % 2) == 0)
      return true;
    if (isSubregToReg() && OpIdx == 2)
      return true;
    return false;
  }

  /// Returns the number of non-implicit operands.
  /// @return Count of explicit operands.
  LLVM_ABI unsigned getNumExplicitOperands() const;

  /// Returns the number of non-implicit definitions.
  /// @return Count of explicit definition operands.
  LLVM_ABI unsigned getNumExplicitDefs() const;

  /// iterator/begin/end - Iterate over all operands of a machine instruction.

  // The operands must always be in the following order:
  // - explicit reg defs,
  // - other explicit operands (reg uses, immediates, etc.),
  // - implicit reg defs
  // - implicit reg uses
  using mop_iterator = MachineOperand *;
  /// Const iterator over this instruction's machine operands.
  using const_mop_iterator = const MachineOperand *;

  /// Mutable iterator range over machine operands.
  using mop_range = iterator_range<mop_iterator>;
  /// Const iterator range over machine operands.
  using const_mop_range = iterator_range<const_mop_iterator>;

  /// Return a begin iterator over this instruction's operands.
  /// @return Mutable begin operand iterator.
  mop_iterator operands_begin() { return Operands; }
  /// Return a past-the-end iterator over this instruction's operands.
  /// @return Mutable past-the-end operand iterator.
  mop_iterator operands_end() { return Operands + NumOperands; }

  /// Return a const begin iterator over this instruction's operands.
  /// @return Const begin operand iterator.
  const_mop_iterator operands_begin() const { return Operands; }
  /// Return a past-the-end const iterator over this instruction's operands.
  /// @return Const past-the-end operand iterator.
  const_mop_iterator operands_end() const { return Operands + NumOperands; }

  /// Return a range over all operands of this instruction.
  /// @return Mutable range of all operands.
  mop_range operands() { return operands_impl(); }
  /// Return a const range over all operands of this instruction.
  /// @return Const range of all operands.
  const_mop_range operands() const { return operands_impl(); }

  /// Return a range over this instruction's explicit operands.
  /// @return Mutable range of explicit operands.
  mop_range explicit_operands() {
    return operands_impl().take_front(getNumExplicitOperands());
  }
  /// Return a const range over this instruction's explicit operands.
  /// @return Const range of explicit operands.
  const_mop_range explicit_operands() const {
    return operands_impl().take_front(getNumExplicitOperands());
  }
  /// Return a range over this instruction's implicit operands.
  /// @return Mutable range of implicit operands.
  mop_range implicit_operands() {
    return operands_impl().drop_front(getNumExplicitOperands());
  }
  /// Return a const range over this instruction's implicit operands.
  /// @return Const range of implicit operands.
  const_mop_range implicit_operands() const {
    return operands_impl().drop_front(getNumExplicitOperands());
  }

  /// Return operands that determine the variable location for this debug value.
  /// @return Mutable range of debug location operands.
  mop_range debug_operands() {
    assert(isDebugValueLike() && "Must be a debug value instruction.");
    return isNonListDebugValue() ? operands_impl().take_front(1)
                                 : operands_impl().drop_front(2);
  }
  /// \copydoc debug_operands()
  /// @return Const range of debug location operands.
  const_mop_range debug_operands() const {
    assert(isDebugValueLike() && "Must be a debug value instruction.");
    return isNonListDebugValue() ? operands_impl().take_front(1)
                                 : operands_impl().drop_front(2);
  }
  /// Return all explicit register definition operands (implicits excluded).
  /// @return Mutable range of explicit defs.
  mop_range defs() { return operands_impl().take_front(getNumExplicitDefs()); }
  /// \copydoc defs()
  /// @return Const range of explicit defs.
  const_mop_range defs() const {
    return operands_impl().take_front(getNumExplicitDefs());
  }
  /// Return operands that may be register uses (may include non-reg operands).
  /// @return Mutable range starting after explicit defs.
  mop_range uses() { return operands_impl().drop_front(getNumExplicitDefs()); }
  /// \copydoc uses()
  /// @return Const range starting after explicit defs.
  const_mop_range uses() const {
    return operands_impl().drop_front(getNumExplicitDefs());
  }
  /// Return a range over explicit operands that are not explicit defs.
  /// @return Mutable range of explicit use-side operands.
  mop_range explicit_uses() {
    return operands_impl()
        .take_front(getNumExplicitOperands())
        .drop_front(getNumExplicitDefs());
  }
  /// Return a const range over explicit operands that are not explicit defs.
  /// @return Const range of explicit use-side operands.
  const_mop_range explicit_uses() const {
    return operands_impl()
        .take_front(getNumExplicitOperands())
        .drop_front(getNumExplicitDefs());
  }

  /// Filtered mutable operand range (e.g. all_defs / all_uses).
  using filtered_mop_range = iterator_range<
      filter_iterator<mop_iterator, bool (*)(const MachineOperand &)>>;
  /// Filtered const operand range (e.g. all_defs / all_uses).
  using filtered_const_mop_range = iterator_range<
      filter_iterator<const_mop_iterator, bool (*)(const MachineOperand &)>>;

  /// Return a range over all explicit or implicit register def operands.
  /// @return Filtered range of register def operands.
  filtered_mop_range all_defs() {
    return make_filter_range(operands(), opIsRegDef);
  }
  /// \copydoc all_defs()
  /// @return Filtered const range of register def operands.
  filtered_const_mop_range all_defs() const {
    return make_filter_range(operands(), opIsRegDef);
  }

  /// Return a range over all explicit or implicit register use operands.
  /// @return Filtered range of register use operands.
  filtered_mop_range all_uses() {
    return make_filter_range(uses(), opIsRegUse);
  }
  /// \copydoc all_uses()
  /// @return Filtered const range of register use operands.
  filtered_const_mop_range all_uses() const {
    return make_filter_range(uses(), opIsRegUse);
  }

  /// Return the operand index corresponding to iterator \p I.
  /// @param I Operand iterator into this instruction.
  /// @return Zero-based operand index of \p I.
  unsigned getOperandNo(const_mop_iterator I) const {
    return I - operands_begin();
  }

  /// Return this instruction's memory operands.
  ///
  /// An empty list does not imply the instruction accesses no memory; callers
  /// must behave conservatively.
  /// @return Array of MachineMemOperand pointers (possibly empty).
  ArrayRef<MachineMemOperand *> memoperands() const {
    if (!Info)
      return {};

    if (Info.is<EIIK_MMO>())
      return ArrayRef(Info.getAddrOfZeroTagPointer(), 1);

    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getMMOs();

    return {};
  }

  /// Return a begin iterator over this instruction's memory operands.
  ///
  /// An empty range does not imply the instruction accesses no memory.
  /// @return Begin iterator for memrefs.
  mmo_iterator memoperands_begin() const { return memoperands().begin(); }

  /// Return a past-the-end iterator over this instruction's memory operands.
  ///
  /// An empty range does not imply the instruction accesses no memory.
  /// @return Past-the-end memref iterator.
  mmo_iterator memoperands_end() const { return memoperands().end(); }

  /// Return true if this instruction has no describing memory operands.
  ///
  /// Callers must be conservative when this is true.
  /// @return True if the memref list is empty.
  bool memoperands_empty() const { return memoperands().empty(); }

  /// Return true if this instruction has exactly one MachineMemOperand.
  /// @return True if there is exactly one memory operand.
  bool hasOneMemOperand() const { return memoperands().size() == 1; }

  /// Return the number of memory operands.
  /// @return Number of MachineMemOperands attached to this instruction.
  unsigned getNumMemOperands() const { return memoperands().size(); }

  /// Helper to extract a pre-instruction symbol if one has been added.
  /// @return Pre-instruction symbol, or null if none is set.
  MCSymbol *getPreInstrSymbol() const {
    if (!Info)
      return nullptr;
    if (MCSymbol *S = Info.get<EIIK_PreInstrSymbol>())
      return S;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getPreInstrSymbol();

    return nullptr;
  }

  /// Helper to extract a post-instruction symbol if one has been added.
  /// @return Post-instruction symbol, or null if none is set.
  MCSymbol *getPostInstrSymbol() const {
    if (!Info)
      return nullptr;
    if (MCSymbol *S = Info.get<EIIK_PostInstrSymbol>())
      return S;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getPostInstrSymbol();

    return nullptr;
  }

  /// Helper to extract a heap alloc marker if one has been added.
  /// @return Heap-alloc marker metadata, or null if none is set.
  MDNode *getHeapAllocMarker() const {
    if (!Info)
      return nullptr;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getHeapAllocMarker();

    return nullptr;
  }

  /// Helper to extract PCSections metadata target sections.
  /// @return PC-sections metadata node, or null if none is set.
  MDNode *getPCSections() const {
    if (!Info)
      return nullptr;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getPCSections();

    return nullptr;
  }

  /// Helper to extract mmra.op metadata.
  /// @return MMRA metadata node, or null if none is set.
  MDNode *getMMRAMetadata() const {
    if (!Info)
      return nullptr;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getMMRAMetadata();
    return nullptr;
  }

  /// Return the deactivation symbol attached to this instruction, if any.
  /// @return Deactivation symbol value, or null if none is set.
  Value *getDeactivationSymbol() const {
    if (!Info)
      return nullptr;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getDeactivationSymbol();
    return nullptr;
  }

  /// Helper to extract a CFI type hash if one has been added.
  /// @return CFI type hash, or 0 if none is set.
  uint32_t getCFIType() const {
    if (!Info)
      return 0;
    if (ExtraInfo *EI = Info.get<EIIK_OutOfLine>())
      return EI->getCFIType();

    return 0;
  }

  /// API for querying MachineInstr properties. They are the same as MCInstrDesc
  /// queries but they are bundle aware.

  /// How property queries treat instruction bundles.
  enum QueryType {
    /// Ignore bundling; query only this instruction.
    IgnoreBundle,
    /// Return true if any instruction in the bundle has the property.
    AnyInBundle,
    /// Return true if all instructions in the bundle have the property.
    AllInBundle
  };

  /// Return true if this instruction (or its bundle) has property \p MCFlag.
  ///
  /// \p Type controls whether and how instruction bundles are searched.
  /// @param MCFlag Property flag from MCID being queried.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the property is present under \p Type's rules.
  bool hasProperty(unsigned MCFlag, QueryType Type = AnyInBundle) const {
    assert(MCFlag < 64 &&
           "MCFlag out of range for bit mask in getFlags/hasPropertyInBundle.");
    // Inline the fast path for unbundled or bundle-internal instructions.
    if (Type == IgnoreBundle || !isBundled() || isBundledWithPred())
      return getDesc().getFlags() & (1ULL << MCFlag);

    // If this is the first instruction in a bundle, take the slow path.
    return hasPropertyInBundle(1ULL << MCFlag, Type);
  }

  /// Return true if this opcode should go through usual legalization steps.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is a pre-ISel opcode.
  bool isPreISelOpcode(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::PreISelOpcode, Type);
  }

  /// Return true if this instruction can have a variable number of operands.
  ///
  /// Variable operands follow the normal operands and precede implicits.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is variadic.
  bool isVariadic(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Variadic, Type);
  }

  /// Return true if this instruction has an optional definition.
  ///
  /// E.g. ARM instructions that set condition codes when the 's' bit is set.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if an optional definition is present.
  bool hasOptionalDef(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::HasOptionalDef, Type);
  }

  /// Return true if this is a pseudo instruction (not a real machine insn).
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is a pseudo instruction.
  bool isPseudo(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Pseudo, Type);
  }

  /// Return true if this instruction produces no executable output.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is a meta instruction.
  bool isMetaInstruction(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Meta, Type);
  }

  /// Return true if this is a return instruction.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is a return.
  bool isReturn(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::Return, Type);
  }

  /// Return true if this marks the end of an EH scope (catchpad/cleanuppad).
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is an EH scope return.
  bool isEHScopeReturn(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::EHScopeReturn, Type);
  }

  /// Return true if this is a call instruction.
  ///
  /// When \p Type is AnyInBundle, any instruction in the bundle may match.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is a call.
  bool isCall(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::Call, Type);
  }

  /// Return true if this call may have additional call-site information.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if additional call info may be associated.
  LLVM_ABI bool
  isCandidateForAdditionalCallInfo(QueryType Type = IgnoreBundle) const;

  /// Return true if additional call-site info must be updated for this MI.
  ///
  /// See \ref MachineFunction::copyAdditionalCallInfo,
  /// \ref MachineFunction::moveAdditionalCallInfo, and
  /// \ref MachineFunction::eraseAdditionalCallInfo.
  /// @return True if additional call info must be updated.
  LLVM_ABI bool shouldUpdateAdditionalCallInfo() const;

  /// Return true if this instruction stops fall-through to the next instruction.
  ///
  /// Examples include unconditional branches and returns.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is a barrier.
  bool isBarrier(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::Barrier, Type);
  }

  /// Return true if this instruction is part of a basic-block terminator.
  ///
  /// Typically returns and branches. Passes use this to insert code before
  /// control flow.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is a terminator.
  bool isTerminator(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::Terminator, Type);
  }

  /// Return true if this is a conditional, unconditional, or indirect branch.
  ///
  /// Use the more specific predicates or TargetInstrInfo::analyzeBranch.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is any kind of branch.
  bool isBranch(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::Branch, Type);
  }

  /// Return true if this is an indirect branch (e.g. through a register).
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is an indirect branch.
  bool isIndirectBranch(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::IndirectBranch, Type);
  }

  /// Return true if this branch may fall through or transfer to another block.
  ///
  /// Use TargetInstrInfo::analyzeBranch for more detail.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is a conditional branch.
  bool isConditionalBranch(QueryType Type = AnyInBundle) const {
    return isBranch(Type) && !isBarrier(Type) && !isIndirectBranch(Type);
  }

  /// Return true if this branch always transfers control to another block.
  ///
  /// Use TargetInstrInfo::analyzeBranch for more detail.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if this is an unconditional branch.
  bool isUnconditionalBranch(QueryType Type = AnyInBundle) const {
    return isBranch(Type) && isBarrier(Type) && !isIndirectBranch(Type);
  }

  /// Return true if this instruction has a predicate operand.
  ///
  /// The predicate may be 'always' or other values; TargetInstrInfo can modify
  /// it.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is predicable.
  bool isPredicable(QueryType Type = AllInBundle) const {
    // If it's a bundle than all bundled instructions must be predicable for this
    // to return true.
    return hasProperty(MCID::Predicable, Type);
  }

  /// Return true if this instruction is a comparison.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is a comparison.
  bool isCompare(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Compare, Type);
  }

  /// Return true if this is a move-immediate (including conditional moves).
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is a move immediate.
  bool isMoveImmediate(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::MoveImm, Type);
  }

  /// Return true if this instruction is a register move.
  ///
  /// Includes moving values from subreg to reg.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is a register move.
  bool isMoveReg(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::MoveReg, Type);
  }

  /// Return true if this instruction is a bitcast instruction.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is a bitcast.
  bool isBitcast(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Bitcast, Type);
  }

  /// Return true if this instruction is a select instruction.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is a select.
  bool isSelect(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Select, Type);
  }

  /// Return true if this instruction cannot be safely duplicated.
  ///
  /// For example, unique labels would cause multiple-definition errors.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if duplication is unsafe.
  bool isNotDuplicable(QueryType Type = AnyInBundle) const {
    if (getPreInstrSymbol() || getPostInstrSymbol())
      return true;
    return hasProperty(MCID::NotDuplicable, Type);
  }

  /// Return true if this instruction is convergent.
  ///
  /// Convergent instructions cannot be made control-dependent on new values.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is convergent.
  bool isConvergent(QueryType Type = AnyInBundle) const {
    if (isInlineAsm()) {
      unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
      if (ExtraInfo & InlineAsm::Extra_IsConvergent)
        return true;
    }
    if (getFlag(NoConvergent))
      return false;
    return hasProperty(MCID::Convergent, Type);
  }

  /// Return true if this instruction has a delay slot that must be filled.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction has a delay slot.
  bool hasDelaySlot(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::DelaySlot, Type);
  }

  /// Return true if this instruction can be folded as a memory operand.
  ///
  /// Typically simple loads or constant-pool-like materializations with a
  /// single virtual register definition.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction can fold as a load.
  bool canFoldAsLoad(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::FoldableAsLoad, Type);
  }

  /// Return true if this behaves like a generic REG_SEQUENCE.
  ///
  /// Targets should override TargetInstrInfo::getRegSequenceLikeInputs.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is reg-sequence-like.
  bool isRegSequenceLike(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::RegSequence, Type);
  }

  /// Return true if this behaves like a generic EXTRACT_SUBREG.
  ///
  /// Targets should override TargetInstrInfo::getExtractSubregLikeInputs.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is extract-subreg-like.
  bool isExtractSubregLike(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::ExtractSubreg, Type);
  }

  /// Return true if this behaves like a generic INSERT_SUBREG.
  ///
  /// Targets should override TargetInstrInfo::getInsertSubregLikeInputs.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is insert-subreg-like.
  bool isInsertSubregLike(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::InsertSubreg, Type);
  }

  //===--------------------------------------------------------------------===//
  // Side Effect Analysis
  //===--------------------------------------------------------------------===//

  /// Return true if this instruction could possibly read memory.
  ///
  /// Not limited to simple loads; the value may also be modified.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction may load.
  bool mayLoad(QueryType Type = AnyInBundle) const {
    if (isInlineAsm()) {
      unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
      if (ExtraInfo & InlineAsm::Extra_MayLoad)
        return true;
    }
    return hasProperty(MCID::MayLoad, Type);
  }

  /// Return true if this instruction could possibly modify memory.
  ///
  /// Not limited to simple stores; the access may be conditional or derived.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction may store.
  bool mayStore(QueryType Type = AnyInBundle) const {
    if (isInlineAsm()) {
      unsigned ExtraInfo = getOperand(InlineAsm::MIOp_ExtraInfo).getImm();
      if (ExtraInfo & InlineAsm::Extra_MayStore)
        return true;
    }
    return hasProperty(MCID::MayStore, Type);
  }

  /// Return true if this instruction could possibly read or modify memory.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction may load or store.
  bool mayLoadOrStore(QueryType Type = AnyInBundle) const {
    return mayLoad(Type) || mayStore(Type);
  }

  /// Return true if this instruction could raise a floating-point exception.
  ///
  /// Requires MCID::MayRaiseFPException and that NoFPExcept is not set.
  /// @return True if a floating-point exception may be raised.
  bool mayRaiseFPException() const {
    return hasProperty(MCID::MayRaiseFPException) &&
           !getFlag(MachineInstr::MIFlag::NoFPExcept);
  }

  //===--------------------------------------------------------------------===//
  // Flags that indicate whether an instruction can be modified by a method.
  //===--------------------------------------------------------------------===//

  /// Return true if this instruction may be commutable (swap source operands).
  ///
  /// When set, TargetInstrInfo::commuteInstruction may rewrite the instruction.
  /// The flag can be set for instructions that are only sometimes commutable.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction may be commutable.
  bool isCommutable(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::Commutable, Type);
  }

  /// Return true if this 2-address instruction can become 3-address if needed.
  ///
  /// Lets the register allocator keep a 2-address form when possible, or
  /// convert via TargetInstrInfo::convertToThreeAddress when not.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if conversion to a 3-address form is supported.
  bool isConvertibleTo3Addr(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::ConvertibleTo3Addr, Type);
  }

  /// Return true if DAG scheduling needs a custom insertion hook for this.
  ///
  /// Typically a SelectionDAG pseudo expanded by the target via
  /// TargetLoweringInfo::InsertAtEndOfBasicBlock.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if a custom insertion hook is required.
  bool usesCustomInsertionHook(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::UsesCustomInserter, Type);
  }

  /// Return true if this instruction needs a post-ISel target adjustment hook.
  ///
  /// E.g. filling ARM 's' optional operand from condition-flag use.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if a post-ISel hook is required.
  bool hasPostISelHook(QueryType Type = IgnoreBundle) const {
    return hasProperty(MCID::HasPostISelHook, Type);
  }

  /// Return true if this instruction is a rematerialization candidate.
  ///
  /// Deprecated; when set, isReMaterializableImpl() still verifies remat.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the rematerializable property is set.
  bool isRematerializable(QueryType Type = AllInBundle) const {
    // It's only possible to re-mat a bundle if all bundled instructions are
    // re-materializable.
    return hasProperty(MCID::Rematerializable, Type);
  }

  /// Return true if this instruction costs no more than a move.
  ///
  /// Useful when deciding whether to rematerialize or hoist. Same-class copies
  /// are not marked with this flag.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if the instruction is as cheap as a move.
  bool isAsCheapAsAMove(QueryType Type = AllInBundle) const {
    // Only returns true for a bundle if all bundled instructions are cheap.
    return hasProperty(MCID::CheapAsAMove, Type);
  }

  /// Return true if sources have special regalloc requirements beyond classes.
  ///
  /// E.g. ARM::STRD sources must be an even/odd pair. Post-RA passes should not
  /// change allocations for such sources.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if extra source register allocation requirements apply.
  bool hasExtraSrcRegAllocReq(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::ExtraSrcRegAllocReq, Type);
  }

  /// Return true if defs have special regalloc requirements beyond classes.
  ///
  /// E.g. ARM::LDRD defs must be an even/odd pair. Post-RA passes should not
  /// change allocations for such definitions.
  /// @param Type How to query when this instruction is part of a bundle.
  /// @return True if extra def register allocation requirements apply.
  bool hasExtraDefRegAllocReq(QueryType Type = AnyInBundle) const {
    return hasProperty(MCID::ExtraDefRegAllocReq, Type);
  }

  /// Controls which operands are compared by isIdenticalTo.
  enum MICheckType {
    /// Compare all operands for equality.
    CheckDefs,
    /// Compare all operands including kill and dead markers.
    CheckKillDead,
    /// Ignore all definitions when comparing.
    IgnoreDefs,
    /// Ignore virtual register definitions when comparing operands.
    IgnoreVRegDefs
  };

  /// Return true if this instruction is identical to \p Other.
  ///
  /// Same opcode and identical operands (per MachineOperand::isIdenticalTo).
  /// Liveness flags (dead, undef, kill) do not affect identity unless
  /// requested via \p Check.
  /// @param Other Other instruction to compare against.
  /// @param Check How strictly operands and defs are compared.
  /// @return True if the instructions are identical under \p Check.
  LLVM_ABI bool isIdenticalTo(const MachineInstr &Other,
                              MICheckType Check = CheckDefs) const;

  /// Return true if this debug instruction represents the same value as \p Other.
  ///
  /// Compares variables, debug locations, debug operands, and DIExpressions
  /// (including directness flags).
  /// @param Other Other debug instruction to compare against.
  /// @return True if the debug values are equivalent.
  LLVM_ABI bool isEquivalentDbgInstr(const MachineInstr &Other) const;

  /// Unlink this instruction from its basic block and return it without deleting.
  ///
  /// Cannot be used on bundled instructions; use removeFromBundle() instead.
  /// @return Pointer to this instruction after unlinking.
  LLVM_ABI MachineInstr *removeFromParent();

  /// Unlink this instruction from its basic block and return it without
  /// deleting it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle remain bundled.
  /// @return Pointer to this instruction after unlinking.
  LLVM_ABI MachineInstr *removeFromBundle();

  /// Unlink 'this' from the containing basic block and delete it.
  ///
  /// If this instruction is the header of a bundle, the whole bundle is erased.
  /// This function can not be used for instructions inside a bundle, use
  /// eraseFromBundle() to erase individual bundled instructions.
  /// \returns the iterator following the erased instruction. If this is the
  /// header of a bundle it returns the iterator following the erased bundle
  /// iterator.
  LLVM_ABI MachineInstrBundleIterator<MachineInstr> eraseFromParent();

  /// Unlink 'this' from its basic block and delete it.
  ///
  /// If the instruction is part of a bundle, the other instructions in the
  /// bundle remain bundled.
  LLVM_ABI void eraseFromBundle();

  /// Return true if this is an EH_LABEL instruction.
  /// @return True if the opcode is EH_LABEL.
  bool isEHLabel() const { return getOpcode() == TargetOpcode::EH_LABEL; }
  /// Return true if this is a GC_LABEL instruction.
  /// @return True if the opcode is GC_LABEL.
  bool isGCLabel() const { return getOpcode() == TargetOpcode::GC_LABEL; }
  /// Return true if this is an ANNOTATION_LABEL instruction.
  /// @return True if the opcode is ANNOTATION_LABEL.
  bool isAnnotationLabel() const {
    return getOpcode() == TargetOpcode::ANNOTATION_LABEL;
  }

  /// Return true if this is a LIFETIME_START or LIFETIME_END marker.
  /// @return True if the opcode is a lifetime marker.
  bool isLifetimeMarker() const {
    return getOpcode() == TargetOpcode::LIFETIME_START ||
           getOpcode() == TargetOpcode::LIFETIME_END;
  }

  /// Returns true if the MachineInstr represents a label.
  /// @return True if this is an EH, GC, or annotation label.
  bool isLabel() const {
    return isEHLabel() || isGCLabel() || isAnnotationLabel();
  }

  /// Return true if this is a CFI_INSTRUCTION.
  /// @return True if the opcode is CFI_INSTRUCTION.
  bool isCFIInstruction() const {
    return getOpcode() == TargetOpcode::CFI_INSTRUCTION;
  }

  /// Return true if this is a PSEUDO_PROBE instruction.
  /// @return True if the opcode is PSEUDO_PROBE.
  bool isPseudoProbe() const {
    return getOpcode() == TargetOpcode::PSEUDO_PROBE;
  }

  /// Return true if the instruction marks a position in the function.
  ///
  /// FIXME: Why are LIFETIME markers not considered in MachineInstr::isPosition?
  /// @return True if this is a label or CFI instruction.
  bool isPosition() const { return isLabel() || isCFIInstruction(); }

  /// Return true if this is a non-list DBG_VALUE instruction.
  /// @return True if the opcode is DBG_VALUE.
  bool isNonListDebugValue() const {
    return getOpcode() == TargetOpcode::DBG_VALUE;
  }
  /// Return true if this is a DBG_VALUE_LIST instruction.
  /// @return True if the opcode is DBG_VALUE_LIST.
  bool isDebugValueList() const {
    return getOpcode() == TargetOpcode::DBG_VALUE_LIST;
  }
  /// Return true if the instruction is a DBG_VALUE or DBG_VALUE_LIST.
  /// @return True if the opcode is a debug value.
  bool isDebugValue() const {
    return isNonListDebugValue() || isDebugValueList();
  }
  /// Return true if this instruction's opcode is DBG_LABEL.
  /// @return True if the opcode is DBG_LABEL.
  bool isDebugLabel() const { return getOpcode() == TargetOpcode::DBG_LABEL; }
  /// Return true if this instruction's opcode is DBG_INSTR_REF.
  /// @return True if the opcode is DBG_INSTR_REF.
  bool isDebugRef() const { return getOpcode() == TargetOpcode::DBG_INSTR_REF; }
  /// Return true if this is a debug value or debug instruction reference.
  /// @return True if this is DBG_VALUE-like or DBG_INSTR_REF.
  bool isDebugValueLike() const { return isDebugValue() || isDebugRef(); }
  /// Return true if this is a DBG_PHI instruction.
  /// @return True if the opcode is DBG_PHI.
  bool isDebugPHI() const { return getOpcode() == TargetOpcode::DBG_PHI; }
  /// Return true if this is any kind of debug instruction.
  /// @return True for DBG_VALUE, DBG_LABEL, DBG_INSTR_REF, or DBG_PHI.
  bool isDebugInstr() const {
    return isDebugValue() || isDebugLabel() || isDebugRef() || isDebugPHI();
  }
  /// Return true if this is a debug instruction or a pseudo probe.
  /// @return True if this is a debug or PSEUDO_PROBE instruction.
  bool isDebugOrPseudoInstr() const {
    return isDebugInstr() || isPseudoProbe();
  }

  /// Return true if this non-list DBG_VALUE has an immediate offset operand.
  /// @return True if the debug offset operand is an immediate.
  bool isDebugOffsetImm() const {
    return isNonListDebugValue() && getDebugOffset().isImm();
  }

  /// Return true if this DBG_VALUE is indirect (reg location + imm offset).
  /// @return True if the location is a register and the offset is immediate.
  bool isIndirectDebugValue() const {
    return isDebugOffsetImm() && getDebugOperand(0).isReg();
  }

  /// Return true if this DBG_VALUE is an entry value expression.
  /// @return True if the expression contains DW_OP_LLVM_entry_value.
  LLVM_ABI bool isDebugEntryValue() const;

  /// Return true if this debug value describes part of a variable as unavailable.
  /// @return True if any debug location operand is an invalid register.
  bool isUndefDebugValue() const {
    if (!isDebugValue())
      return false;
    // If any $noreg locations are given, this DV is undef.
    for (const MachineOperand &Op : debug_operands())
      if (Op.isReg() && !Op.getReg().isValid())
        return true;
    return false;
  }

  /// Return true if this is a JUMP_TABLE_DEBUG_INFO instruction.
  /// @return True if the opcode is JUMP_TABLE_DEBUG_INFO.
  bool isJumpTableDebugInfo() const {
    return getOpcode() == TargetOpcode::JUMP_TABLE_DEBUG_INFO;
  }

  /// Return true if this is a PHI or G_PHI instruction.
  /// @return True if the opcode is a PHI.
  bool isPHI() const {
    return getOpcode() == TargetOpcode::PHI ||
           getOpcode() == TargetOpcode::G_PHI;
  }
  /// Return true if this is a KILL instruction.
  /// @return True if the opcode is KILL.
  bool isKill() const { return getOpcode() == TargetOpcode::KILL; }
  /// Return true if this is an IMPLICIT_DEF instruction.
  /// @return True if the opcode is IMPLICIT_DEF.
  bool isImplicitDef() const { return getOpcode()==TargetOpcode::IMPLICIT_DEF; }
  /// Return true if this is an INLINEASM or INLINEASM_BR instruction.
  /// @return True if the opcode is inline assembly.
  bool isInlineAsm() const {
    return getOpcode() == TargetOpcode::INLINEASM ||
           getOpcode() == TargetOpcode::INLINEASM_BR;
  }
  /// Return true if register operand \p OpId may fold into a frame index.
  ///
  /// Checks the InlineAsm::Flag immediate at OpId - 1.
  /// @param OpId Index of the register operand to check.
  /// @return True if the operand can be folded with a load or store.
  LLVM_ABI bool mayFoldInlineAsmRegOp(unsigned OpId) const;

  /// Return true if this is inline asm that requests stack alignment.
  /// @return True if the inline asm requests stack alignment.
  LLVM_ABI bool isStackAligningInlineAsm() const;
  /// Return the inline assembly dialect used by this instruction.
  /// @return InlineAsm dialect for this inline asm instruction.
  LLVM_ABI InlineAsm::AsmDialect getInlineAsmDialect() const;

  /// Return true if this is an INSERT_SUBREG instruction.
  /// @return True if the opcode is INSERT_SUBREG.
  bool isInsertSubreg() const {
    return getOpcode() == TargetOpcode::INSERT_SUBREG;
  }

  /// Return true if this is a SUBREG_TO_REG instruction.
  /// @return True if the opcode is SUBREG_TO_REG.
  bool isSubregToReg() const {
    return getOpcode() == TargetOpcode::SUBREG_TO_REG;
  }

  /// Return true if this is a REG_SEQUENCE instruction.
  /// @return True if the opcode is REG_SEQUENCE.
  bool isRegSequence() const {
    return getOpcode() == TargetOpcode::REG_SEQUENCE;
  }

  /// Return true if this is a BUNDLE header instruction.
  /// @return True if the opcode is BUNDLE.
  bool isBundle() const {
    return getOpcode() == TargetOpcode::BUNDLE;
  }

  /// Return true if this is a COPY instruction.
  /// @return True if the opcode is COPY.
  bool isCopy() const {
    return getOpcode() == TargetOpcode::COPY;
  }

  /// Return true if this is a COPY_LANEMASK instruction.
  /// @return True if the opcode is COPY_LANEMASK.
  bool isCopyLaneMask() const {
    return getOpcode() == TargetOpcode::COPY_LANEMASK;
  }

  /// Return true if this is a COPY with no subregister on either operand.
  /// @return True if this is a full-register COPY.
  bool isFullCopy() const {
    return isCopy() && !getOperand(0).getSubReg() && !getOperand(1).getSubReg();
  }

  /// Return true if this is an EXTRACT_SUBREG instruction.
  /// @return True if the opcode is EXTRACT_SUBREG.
  bool isExtractSubreg() const {
    return getOpcode() == TargetOpcode::EXTRACT_SUBREG;
  }

  /// Return true if this is a FAKE_USE instruction.
  /// @return True if the opcode is FAKE_USE.
  bool isFakeUse() const { return getOpcode() == TargetOpcode::FAKE_USE; }

  /// Return true if this instruction behaves like a copy (excluding COPY).
  /// @return True for copy-like opcodes such as SUBREG_TO_REG.
  bool isCopyLike() const {
    return isCopy() || isSubregToReg();
  }

  /// Return true if this is an identity copy (same reg and subreg).
  /// @return True if source and destination registers match exactly.
  bool isIdentityCopy() const {
    return isCopy() && getOperand(0).getReg() == getOperand(1).getReg() &&
      getOperand(0).getSubReg() == getOperand(1).getSubReg();
  }

  /// Return true if this instruction is transient (likely eliminated / free).
  ///
  /// Includes copy-like instructions and other ops without execution-time cost.
  /// @return True if the instruction is transient.
  bool isTransient() const {
    switch (getOpcode()) {
    default:
      return isMetaInstruction();
    // Copy-like instructions are usually eliminated during register allocation.
    case TargetOpcode::PHI:
    case TargetOpcode::G_PHI:
    case TargetOpcode::COPY:
    case TargetOpcode::COPY_LANEMASK:
    case TargetOpcode::INSERT_SUBREG:
    case TargetOpcode::SUBREG_TO_REG:
    case TargetOpcode::REG_SEQUENCE:
      return true;
    }
  }

  /// Return the number of instructions inside this bundle, excluding the header.
  ///
  /// This is the number MachineBasicBlock::iterator skips; 0 if unbundled.
  /// @return Bundle size excluding the header, or 0 if not bundled.
  LLVM_ABI unsigned getBundleSize() const;

  /// Return true if this instruction reads \p Reg.
  ///
  /// With a non-null \p TRI, also matches a read of a super-register.
  /// Partial redefines of virtual registers are not counted as reads.
  /// @param Reg Register whose read is queried.
  /// @param TRI Optional target register info for super-register checks.
  /// @return True if \p Reg is read.
  bool readsRegister(Register Reg, const TargetRegisterInfo *TRI) const {
    return findRegisterUseOperandIdx(Reg, TRI, false) != -1;
  }

  /// Return true if this instruction reads virtual register \p Reg.
  ///
  /// Partial defines count as reads (read-modify-write).
  /// @param Reg Virtual register to inspect.
  /// @return True if \p Reg is read.
  bool readsVirtualRegister(Register Reg) const {
    return readsWritesVirtualRegister(Reg).first;
  }

  /// Return whether this instruction reads and/or writes virtual register \p Reg.
  ///
  /// Partial defines count as both. When \p Ops is non-null, operand indices
  /// for \p Reg are appended.
  /// @param Reg Virtual register to inspect.
  /// @param Ops Optional output vector of matching operand indices.
  /// @return Pair of (reads, writes) booleans.
  LLVM_ABI std::pair<bool, bool>
  readsWritesVirtualRegister(Register Reg,
                             SmallVectorImpl<unsigned> *Ops = nullptr) const;

  /// Return true if this instruction kills \p Reg.
  ///
  /// With a non-null \p TRI, also matches a kill of a super-register.
  /// @param Reg Register whose kill is queried.
  /// @param TRI Optional target register info for super-register checks.
  /// @return True if \p Reg is killed.
  bool killsRegister(Register Reg, const TargetRegisterInfo *TRI) const {
    return findRegisterUseOperandIdx(Reg, TRI, true) != -1;
  }

  /// Return true if this instruction fully defines \p Reg.
  ///
  /// With a non-null \p TRI, also matches a def of a super-register.
  /// Ignores subreg indices on virtual registers.
  /// @param Reg Register whose definition is queried.
  /// @param TRI Optional target register info for super-register checks.
  /// @return True if \p Reg is fully defined.
  bool definesRegister(Register Reg, const TargetRegisterInfo *TRI) const {
    return findRegisterDefOperandIdx(Reg, TRI, false, false) != -1;
  }

  /// Return true if this instruction modifies (fully or partially) \p Reg.
  ///
  /// Ignores subreg indices on virtual registers.
  /// @param Reg Register whose modification is queried.
  /// @param TRI Optional target register info for super-register checks.
  /// @return True if \p Reg is defined or partially defined.
  bool modifiesRegister(Register Reg, const TargetRegisterInfo *TRI) const {
    return findRegisterDefOperandIdx(Reg, TRI, false, true) != -1;
  }

  /// Return true if \p Reg is dead in this instruction.
  ///
  /// With a non-null \p TRI, also matches a dead def of a super-register.
  /// @param Reg Register whose deadness is queried.
  /// @param TRI Optional target register info for super-register checks.
  /// @return True if a dead def of \p Reg (or a super-reg) exists.
  bool registerDefIsDead(Register Reg, const TargetRegisterInfo *TRI) const {
    return findRegisterDefOperandIdx(Reg, TRI, true, false) != -1;
  }

  /// Return true if there is an implicit use of exactly register \p Reg.
  /// @param Reg Register to look for as an exact implicit use.
  /// @return True if an implicit-use operand of \p Reg exists.
  LLVM_ABI bool hasRegisterImplicitUseOperand(Register Reg) const;

  /// Return the operand index that uses \p Reg, or -1 if not found.
  ///
  /// When \p isKill is true, only killing uses are considered.
  /// @param Reg Register whose use operand is sought.
  /// @param TRI Target register info used for super-register checks.
  /// @param isKill If true, require a killing use.
  /// @return Operand index of the matching use, or -1.
  LLVM_ABI int findRegisterUseOperandIdx(Register Reg,
                                         const TargetRegisterInfo *TRI,
                                         bool isKill = false) const;

  /// Find a use operand for \p Reg, or null if none matches.
  /// @param Reg Register whose use operand is sought.
  /// @param TRI Target register info used for super-register checks.
  /// @param isKill If true, require a killing use.
  /// @return Pointer to the matching use operand, or null.
  MachineOperand *findRegisterUseOperand(Register Reg,
                                         const TargetRegisterInfo *TRI,
                                         bool isKill = false) {
    int Idx = findRegisterUseOperandIdx(Reg, TRI, isKill);
    return (Idx == -1) ? nullptr : &getOperand(Idx);
  }

  /// Find a const use operand for \p Reg, or null if none matches.
  /// @param Reg Register whose use operand is sought.
  /// @param TRI Target register info used for super-register checks.
  /// @param isKill If true, require a killing use.
  /// @return Pointer to the matching use operand, or null.
  const MachineOperand *findRegisterUseOperand(Register Reg,
                                               const TargetRegisterInfo *TRI,
                                               bool isKill = false) const {
    return const_cast<MachineInstr *>(this)->findRegisterUseOperand(Reg, TRI,
                                                                    isKill);
  }

  /// Return the operand index that defines \p Reg, or -1 if not found.
  ///
  /// When \p isDead is true, non-dead defs are skipped. When \p Overlap is
  /// true, overlapping defs and register masks are considered. With a non-null
  /// \p TRI, super-register defs are also matched.
  /// @param Reg Register whose defining operand is sought.
  /// @param TRI Target register info used for super-register checks.
  /// @param isDead If true, skip defs that are not dead.
  /// @param Overlap If true, also match overlapping defs / regmasks.
  /// @return Operand index of the matching def, or -1.
  LLVM_ABI int findRegisterDefOperandIdx(Register Reg,
                                         const TargetRegisterInfo *TRI,
                                         bool isDead = false,
                                         bool Overlap = false) const;

  /// Find a def operand for \p Reg, or null if none matches.
  /// @param Reg Register whose defining operand is sought.
  /// @param TRI Target register info used for super-register checks.
  /// @param isDead If true, skip defs that are not dead.
  /// @param Overlap If true, also match overlapping defs / regmasks.
  /// @return Pointer to the matching def operand, or null.
  MachineOperand *findRegisterDefOperand(Register Reg,
                                         const TargetRegisterInfo *TRI,
                                         bool isDead = false,
                                         bool Overlap = false) {
    int Idx = findRegisterDefOperandIdx(Reg, TRI, isDead, Overlap);
    return (Idx == -1) ? nullptr : &getOperand(Idx);
  }

  /// Find a const def operand for \p Reg, or null if none matches.
  /// @param Reg Register whose defining operand is sought.
  /// @param TRI Target register info used for super-register checks.
  /// @param isDead If true, skip defs that are not dead.
  /// @param Overlap If true, also match overlapping defs / regmasks.
  /// @return Pointer to the matching def operand, or null.
  const MachineOperand *findRegisterDefOperand(Register Reg,
                                               const TargetRegisterInfo *TRI,
                                               bool isDead = false,
                                               bool Overlap = false) const {
    return const_cast<MachineInstr *>(this)->findRegisterDefOperand(
        Reg, TRI, isDead, Overlap);
  }

  /// Find the index of the first predicate operand, or -1 if none.
  /// @return Index of the first predicate operand, or -1.
  LLVM_ABI int findFirstPredOperandIdx() const;

  /// Find the inline-asm flag-word operand index for operand \p OpIdx.
  ///
  /// Returns -1 if the operand is not in an inline-asm operand group.
  /// When \p GroupNo is non-null, it receives the operand group number.
  /// @param OpIdx Operand index within the inline asm instruction.
  /// @param GroupNo Optional out-parameter for the operand group number.
  /// @return Flag-word operand index, or -1 if not applicable.
  LLVM_ABI int findInlineAsmFlagIdx(unsigned OpIdx,
                                    unsigned *GroupNo = nullptr) const;

  /// Compute the static register class constraint for operand \p OpIdx.
  ///
  /// Derived from MCInstrDesc for normal instructions, or flag words for
  /// inline asm. Returns null when the constraint cannot be determined.
  /// @param OpIdx Operand index to constrain.
  /// @param TII Target instruction info.
  /// @param TRI Target register info.
  /// @return Static register class constraint, or null.
  LLVM_ABI const TargetRegisterClass *
  getRegClassConstraint(unsigned OpIdx, const TargetInstrInfo *TII,
                        const TargetRegisterInfo *TRI) const;

  /// Apply this instruction's constraints on \p Reg to class \p CurRC.
  ///
  /// When \p ExploreBundle is set and this MI is in a bundle, all instructions
  /// in the bundle contribute. Returns null if no class satisfies both.
  ///
  /// \pre CurRC must not be NULL.
  /// @param Reg Virtual register whose constraints are applied.
  /// @param CurRC Current candidate register class (non-null).
  /// @param TII Target instruction info.
  /// @param TRI Target register info.
  /// @param ExploreBundle Whether to include other instructions in the bundle.
  /// @return Constrained register class, or null if none exists.
  LLVM_ABI const TargetRegisterClass *getRegClassConstraintEffectForVReg(
      Register Reg, const TargetRegisterClass *CurRC,
      const TargetInstrInfo *TII, const TargetRegisterInfo *TRI,
      bool ExploreBundle = false) const;

  /// Apply operand \p OpIdx's constraints to register class \p CurRC.
  ///
  /// Returns the class satisfying both \p CurRC and the operand, or null.
  ///
  /// \pre CurRC must not be NULL.
  /// \pre The operand at \p OpIdx must be a register.
  /// @param OpIdx Index of the register operand applying constraints.
  /// @param CurRC Current candidate register class (non-null).
  /// @param TII Target instruction info.
  /// @param TRI Target register info.
  /// @return Constrained register class, or null if none exists.
  LLVM_ABI const TargetRegisterClass *
  getRegClassConstraintEffect(unsigned OpIdx, const TargetRegisterClass *CurRC,
                              const TargetInstrInfo *TII,
                              const TargetRegisterInfo *TRI) const;

  /// Tie register operands at \p DefIdx and \p UseIdx to the same physreg.
  ///
  /// Explicit MCInstrDesc ties are automatic; use this for cases like inline
  /// asm.
  /// @param DefIdx Index of the defining register operand.
  /// @param UseIdx Index of the using register operand.
  LLVM_ABI void tieOperands(unsigned DefIdx, unsigned UseIdx);

  /// Return the index of the operand tied to register operand \p OpIdx.
  ///
  /// Defs are tied to uses and vice versa. The tied operand must exist.
  /// @param OpIdx Index of a tied register operand.
  /// @return Index of the matching tied operand.
  LLVM_ABI unsigned findTiedOperandIdx(unsigned OpIdx) const;

  /// Return true if def operand \p DefOpIdx is tied to a use operand.
  ///
  /// Ties arise from two-address elimination or inline-asm constraints.
  /// When \p UseOpIdx is non-null, it receives the first tied use index.
  /// @param DefOpIdx Index of the def operand to check.
  /// @param UseOpIdx Optional out-parameter for the tied use index.
  /// @return True if the def is tied to a use.
  bool isRegTiedToUseOperand(unsigned DefOpIdx,
                             unsigned *UseOpIdx = nullptr) const {
    const MachineOperand &MO = getOperand(DefOpIdx);
    if (!MO.isReg() || !MO.isDef() || !MO.isTied())
      return false;
    if (UseOpIdx)
      *UseOpIdx = findTiedOperandIdx(DefOpIdx);
    return true;
  }

  /// Return true if use operand \p UseOpIdx is tied to a def operand.
  ///
  /// When \p DefOpIdx is non-null, it receives the tied def operand index.
  /// @param UseOpIdx Index of the use operand to check.
  /// @param DefOpIdx Optional out-parameter for the tied def index.
  /// @return True if the use is tied to a def.
  bool isRegTiedToDefOperand(unsigned UseOpIdx,
                             unsigned *DefOpIdx = nullptr) const {
    const MachineOperand &MO = getOperand(UseOpIdx);
    if (!MO.isReg() || !MO.isUse() || !MO.isTied())
      return false;
    if (DefOpIdx)
      *DefOpIdx = findTiedOperandIdx(UseOpIdx);
    return true;
  }

  /// Clears kill flags on all operands.
  LLVM_ABI void clearKillInfo();

  /// Replace all occurrences of \p FromReg with \p ToReg:\p SubIdx.
  ///
  /// Composes subreg indices where necessary.
  /// @param FromReg Register being replaced.
  /// @param ToReg Replacement register.
  /// @param SubIdx Subregister index composed onto \p ToReg.
  /// @param RegInfo Target register info used for composition.
  LLVM_ABI void substituteRegister(Register FromReg, Register ToReg,
                                   unsigned SubIdx,
                                   const TargetRegisterInfo &RegInfo);

  /// Mark the operand that uses \p IncomingReg as a kill.
  ///
  /// If \p AddIfNotFound is true, add an implicit use operand when missing.
  /// @param IncomingReg Register that is killed by this instruction.
  /// @param RegInfo Target register info used for alias queries.
  /// @param AddIfNotFound If true, add an implicit operand when not found.
  /// @return True if the operand exists or was added.
  LLVM_ABI bool addRegisterKilled(Register IncomingReg,
                                  const TargetRegisterInfo *RegInfo,
                                  bool AddIfNotFound = false);

  /// Clear all kill flags affecting \p Reg (and aliases if \p RegInfo is set).
  /// @param Reg Register whose kill flags are cleared.
  /// @param RegInfo Optional target register info for alias queries.
  LLVM_ABI void clearRegisterKills(Register Reg,
                                   const TargetRegisterInfo *RegInfo);

  /// Mark the operand that defines \p Reg as dead.
  ///
  /// If \p AddIfNotFound is true, add an implicit def operand when missing.
  /// @param Reg Register that was defined without a use.
  /// @param RegInfo Target register info used for alias queries.
  /// @param AddIfNotFound If true, add an implicit operand when not found.
  /// @return True if the operand exists or was added.
  LLVM_ABI bool addRegisterDead(Register Reg, const TargetRegisterInfo *RegInfo,
                                bool AddIfNotFound = false);

  /// Clear all dead flags on operands defining register \p Reg.
  /// @param Reg Register whose defining operands lose the dead flag.
  LLVM_ABI void clearRegisterDeads(Register Reg);

  /// Mark subregister defs of \p Reg with the undef flag.
  ///
  /// Used when a subregister is defined in an otherwise undefined super-reg.
  /// @param Reg Super-register whose subreg defs are updated.
  /// @param IsUndef Whether to set or clear the undef flag.
  LLVM_ABI void setRegisterDefReadUndef(Register Reg, bool IsUndef = true);

  /// Ensure this instruction has an operand that defines \p Reg.
  /// @param Reg Register that must be defined.
  /// @param RegInfo Optional target register info for aliases.
  LLVM_ABI void addRegisterDefined(Register Reg,
                                   const TargetRegisterInfo *RegInfo = nullptr);

  /// Mark every physreg used by this instruction dead except \p UsedRegs.
  ///
  /// On instructions with register mask operands, also add implicit-def
  /// operands for all registers in \p UsedRegs.
  /// @param UsedRegs Physical registers that must remain live.
  /// @param TRI Target register info used for alias queries.
  LLVM_ABI void setPhysRegsDeadExcept(ArrayRef<Register> UsedRegs,
                                      const TargetRegisterInfo &TRI);

  /// Return true if it is safe to move this instruction.
  ///
  /// If \p SawStore is set to true, a store or call lies between the current
  /// location and the intended destination.
  /// @param SawStore Set to true when a store/call is seen while checking.
  /// @return True if moving this instruction is safe.
  LLVM_ABI bool isSafeToMove(bool &SawStore) const;

  /// Return true if this would be trivially dead if all defined regs were dead.
  /// @return True if the instruction has no hard-to-model side effects.
  LLVM_ABI bool wouldBeTriviallyDead() const;

  /// Return true if this instruction is dead.
  ///
  /// If \p LivePhysRegs is provided, it is assumed to be at this instruction
  /// and used to check liveness of physical defs; otherwise physreg defs are
  /// assumed live. For trivially dead instructions, deadness is decided from
  /// defs that are flagged dead or unused.
  /// @param MRI Register info used to inspect uses of defined registers.
  /// @param LivePhysRegs Optional live physical registers at this position.
  /// @return True if the instruction is dead.
  LLVM_ABI bool isDead(const MachineRegisterInfo &MRI,
                       LiveRegUnits *LivePhysRegs = nullptr) const;

  /// Return true if this instruction's memory access may alias \p Other.
  ///
  /// Assumes physical registers used for addresses have the same value in
  /// both instructions. Returns false if neither writes memory.
  /// @param AA Optional batch alias analysis for comparing memrefs.
  /// @param Other Other instruction to check for aliasing.
  /// @param UseTBAA Whether to pass TBAA information to alias analysis.
  /// @return True if the memory accesses may alias.
  LLVM_ABI bool mayAlias(BatchAAResults *AA, const MachineInstr &Other,
                         bool UseTBAA) const;
  /// Return true if this instruction's memory access may alias \p Other.
  /// @param AA Optional alias analysis for comparing memrefs.
  /// @param Other Other instruction to check for aliasing.
  /// @param UseTBAA Whether to pass TBAA information to alias analysis.
  /// @return True if the memory accesses may alias.
  LLVM_ABI bool mayAlias(AAResults *AA, const MachineInstr &Other,
                         bool UseTBAA) const;

  /// Return true if this may have an ordered/volatile memory reference.
  ///
  /// Also returns true when memory-reference information is unavailable.
  /// @return False only when known to have no ordered/volatile memrefs.
  LLVM_ABI bool hasOrderedMemoryRef() const;

  /// Return true if this load never traps and reads invariant memory.
  ///
  /// Examples include constant-pool loads or unchanged argument-area loads.
  /// For multiple loads, all must be dereferenceable and invariant.
  /// @return True if every load is dereferenceable and invariant.
  LLVM_ABI bool isDereferenceableInvariantLoad() const;

  /// If this PHI always merges the same virtual register, return that register.
  /// @return The constant PHI register, or an invalid Register otherwise.
  LLVM_ABI Register isConstantValuePHI() const;

  /// Return true if this instruction has side effects beyond mayLoad/mayStore.
  ///
  /// For most instructions the property is in MCInstrDesc::Flags; for INLINEASM
  /// it is encoded in an operand (see InlineAsm::Extra_HasSideEffect).
  /// @return True if unmodeled side effects are present.
  LLVM_ABI bool hasUnmodeledSideEffects() const;

  /// Returns true if it is illegal to fold a load across this instruction.
  /// @return True if load folding across this instruction is illegal.
  LLVM_ABI bool isLoadFoldBarrier() const;

  /// Return true if all the defs of this instruction are dead.
  /// @return True if every def operand is marked dead.
  LLVM_ABI bool allDefsAreDead() const;

  /// Return true if all the implicit defs of this instruction are dead.
  /// @return True if every implicit def operand is marked dead.
  LLVM_ABI bool allImplicitDefsAreDead() const;

  /// Return a valid size if the instruction is a spill instruction.
  /// @param TII Target instruction info used to classify the instruction.
  /// @return Spill/restore size when applicable; otherwise std::nullopt.
  LLVM_ABI std::optional<LocationSize>
  getSpillSize(const TargetInstrInfo *TII) const;

  /// Return a valid size if the instruction is a folded spill instruction.
  /// @param TII Target instruction info used to classify the instruction.
  /// @return Spill/restore size when applicable; otherwise std::nullopt.
  LLVM_ABI std::optional<LocationSize>
  getFoldedSpillSize(const TargetInstrInfo *TII) const;

  /// Return a valid size if the instruction is a restore instruction.
  /// @param TII Target instruction info used to classify the instruction.
  /// @return Spill/restore size when applicable; otherwise std::nullopt.
  LLVM_ABI std::optional<LocationSize>
  getRestoreSize(const TargetInstrInfo *TII) const;

  /// Return a valid size if the instruction is a folded restore instruction.
  /// @param TII Target instruction info used to classify the instruction.
  /// @return Spill/restore size when applicable; otherwise std::nullopt.
  LLVM_ABI std::optional<LocationSize>
  getFoldedRestoreSize(const TargetInstrInfo *TII) const;

  /// Copy implicit register operands from \p MI onto this instruction.
  /// @param MF Machine function used to allocate operands.
  /// @param MI Source instruction whose implicit operands are copied.
  LLVM_ABI void copyImplicitOps(MachineFunction &MF, const MachineInstr &MI);

  /// Debugging support
  /// @{
  /// Determine the generic type to print for operand \p OpIdx, if needed.
  /// @param OpIdx Operand index whose type may be printed.
  /// @param PrintedTypes Tracks which types have already been printed.
  /// @param MRI Register info used to look up types.
  /// @return Generic LLT to print for the operand, if any.
  LLVM_ABI LLT getTypeToPrint(unsigned OpIdx, SmallBitVector &PrintedTypes,
                              const MachineRegisterInfo &MRI) const;

  /// Return true if this instruction has ties not implied by its descriptor.
  ///
  /// Useful for MIR printing to decide whether ties must be printed explicitly.
  /// @return True if complex (non-descriptor) register ties are present.
  LLVM_ABI bool hasComplexRegisterTies() const;

  /// Print this instruction to \p OS.
  ///
  /// When \p IsStandalone is false, omit information inferable from other
  /// instructions (typical when printing a fragment). \p SkipOpers prints
  /// only defs and opcode; otherwise operands are printed, and the debug
  /// location unless \p SkipDebugLoc. \p TII supplies opcode names when set.
  /// @param OS Output stream.
  /// @param IsStandalone Whether to print standalone (non-fragment) details.
  /// @param SkipOpers If true, print only defs and the opcode.
  /// @param SkipDebugLoc If true, omit the debug location.
  /// @param AddNewLine If true, terminate the printout with a newline.
  /// @param TII Optional target instr info for opcode names.
  LLVM_ABI void print(raw_ostream &OS, bool IsStandalone = true,
                      bool SkipOpers = false, bool SkipDebugLoc = false,
                      bool AddNewLine = true,
                      const TargetInstrInfo *TII = nullptr) const;
  /// Print this instruction to \p OS using module slot tracker \p MST.
  /// @param OS Output stream.
  /// @param MST Module slot tracker for IR name resolution.
  /// @param IsStandalone Whether to print standalone (non-fragment) details.
  /// @param SkipOpers If true, print only defs and the opcode.
  /// @param SkipDebugLoc If true, omit the debug location.
  /// @param AddNewLine If true, terminate the printout with a newline.
  /// @param TII Optional target instr info for opcode names.
  LLVM_ABI void print(raw_ostream &OS, ModuleSlotTracker &MST,
                      bool IsStandalone = true, bool SkipOpers = false,
                      bool SkipDebugLoc = false, bool AddNewLine = true,
                      const TargetInstrInfo *TII = nullptr) const;
  /// Dump this instruction to dbgs().
  LLVM_ABI void dump() const;
  /// Dump this instruction and defining instructions up to \p MaxDepth.
  /// @param MRI Register info used to walk defining instructions.
  /// @param MaxDepth Maximum recursion depth when dumping defs.
  LLVM_ABI void dumpr(const MachineRegisterInfo &MRI,
                      unsigned MaxDepth = UINT_MAX) const;
  /// @}

  //===--------------------------------------------------------------------===//
  // Accessors used to build up machine instructions.

  /// Add operand \p Op to this instruction.
  ///
  /// Implicit operands are appended; explicit operands are inserted before
  /// the first implicit operand. \p MF must be the function that allocated
  /// this instruction. MachineInstrBuilder is usually more convenient.
  /// @param MF Machine function that owns this instruction.
  /// @param Op Operand to add.
  LLVM_ABI void addOperand(MachineFunction &MF, const MachineOperand &Op);

  /// Add operand \p Op without an explicit MachineFunction reference.
  ///
  /// Only works for instructions already inserted in a basic block.
  /// Prefer MachineInstrBuilder or addOperand(MF, Op).
  /// @param Op Operand to append.
  LLVM_ABI void addOperand(const MachineOperand &Op);

  /// Insert operands \p Ops before iterator \p InsertBefore.
  ///
  /// May untie and retie tied operands as needed.
  /// @param InsertBefore Insertion point in the operand list.
  /// @param Ops Operands to insert.
  LLVM_ABI void insert(mop_iterator InsertBefore, ArrayRef<MachineOperand> Ops);

  /// Replace this instruction's descriptor (and thus opcode) with \p TID.
  /// @param TID New target instruction descriptor.
  LLVM_ABI void setDesc(const MCInstrDesc &TID);

  /// Replace this instruction's debug location with \p DL.
  ///
  /// Prefer setting the location via the constructor when possible.
  /// @param DL New debug location.
  void setDebugLoc(DebugLoc DL) { DbgLoc = std::move(DL); }

  /// Erase operand \p OpNo, leaving the instruction with one fewer operand.
  /// @param OpNo Index of the operand to remove.
  LLVM_ABI void removeOperand(unsigned OpNo);

  /// Clear this instruction's memory reference descriptor list.
  ///
  /// Resets memrefs to their most conservative state. Use only as a last
  /// resort since it greatly pessimizes knowledge of the memory access.
  /// @param MF Machine function used for extra-info updates.
  LLVM_ABI void dropMemRefs(MachineFunction &MF);

  /// Assign this instruction's memory reference descriptor list.
  ///
  /// Unlike other methods, this allocates them into a new array associated
  /// with the provided MachineFunction.
  /// @param MF Machine function used for memref allocation.
  /// @param MemRefs New list of memory operands.
  LLVM_ABI void setMemRefs(MachineFunction &MF,
                           ArrayRef<MachineMemOperand *> MemRefs);

  /// Append MachineMemOperand \p MO to this instruction's memref list.
  ///
  /// Prefer setMemRefs for setting up the full list; use this only occasionally.
  /// @param MF Machine function used for memref allocation.
  /// @param MO Memory operand to append.
  LLVM_ABI void addMemOperand(MachineFunction &MF, MachineMemOperand *MO);

  /// Replace our memrefs with a clone of \p MI's memref list.
  ///
  /// Note that `*this` may be the incoming MI!
  ///
  /// Prefer this API whenever possible as it can avoid allocations in common
  /// cases.
  /// @param MF Machine function used for memref allocation.
  /// @param MI Source instruction whose memrefs are cloned.
  LLVM_ABI void cloneMemRefs(MachineFunction &MF, const MachineInstr &MI);

  /// Replace our memrefs with the merged memrefs of instructions in \p MIs.
  ///
  /// Note that `*this` may be one of the incoming MIs!
  ///
  /// Prefer this API whenever possible as it can avoid allocations in common
  /// cases.
  /// @param MF Machine function used for memref allocation.
  /// @param MIs Instructions whose memrefs are merged.
  LLVM_ABI void cloneMergedMemRefs(MachineFunction &MF,
                                   ArrayRef<const MachineInstr *> MIs);

  /// Set a symbol emitted immediately before this instruction.
  ///
  /// Setting this to null removes any such symbol.
  ///
  /// FIXME: This is not fully implemented yet.
  /// @param MF Machine function used for extra-info allocation.
  /// @param Symbol Symbol to emit before the instruction, or null.
  LLVM_ABI void setPreInstrSymbol(MachineFunction &MF, MCSymbol *Symbol);

  /// Set a symbol emitted immediately after this instruction.
  ///
  /// Setting this to null removes any such symbol.
  ///
  /// FIXME: This is not fully implemented yet.
  /// @param MF Machine function used for extra-info allocation.
  /// @param Symbol Symbol to emit after the instruction, or null.
  LLVM_ABI void setPostInstrSymbol(MachineFunction &MF, MCSymbol *Symbol);

  /// Clone another instruction's pre- and post-instruction symbols onto this.
  /// @param MF Machine function used for extra-info allocation.
  /// @param MI Source instruction whose symbols are cloned.
  LLVM_ABI void cloneInstrSymbols(MachineFunction &MF, const MachineInstr &MI);

  /// Set a heap-alloc site marker used to emit labels after ISel/opts.
  ///
  /// Label creation is deferred so it still works if the instruction is
  /// removed or duplicated.
  /// @param MF Machine function used for extra-info allocation.
  /// @param MD Heap-alloc marker metadata, or null to clear.
  LLVM_ABI void setHeapAllocMarker(MachineFunction &MF, MDNode *MD);

  /// Set metadata naming sections that should receive this instruction address.
  /// @param MF Machine function used for extra-info allocation.
  /// @param MD PC-sections metadata node, or null to clear.
  LLVM_ABI void setPCSections(MachineFunction &MF, MDNode *MD);

  /// Set mmra.op metadata on this instruction.
  /// @param MF Machine function used for extra-info allocation.
  /// @param MMRAs MMRA metadata node, or null to clear.
  LLVM_ABI void setMMRAMetadata(MachineFunction &MF, MDNode *MMRAs);

  /// Set the CFI type hash for this instruction.
  /// @param MF Machine function used for extra-info allocation.
  /// @param Type CFI type hash value.
  LLVM_ABI void setCFIType(MachineFunction &MF, uint32_t Type);

  /// Set the deactivation symbol value attached to this instruction.
  /// @param MF Machine function used for extra-info allocation.
  /// @param DS Deactivation symbol value, or null to clear.
  LLVM_ABI void setDeactivationSymbol(MachineFunction &MF, Value *DS);

  /// Return the MIFlags that represent both this instruction and \p Other.
  ///
  /// Use when merging two MachineInstrs into one. Does not modify this
  /// instruction's flags.
  /// @param Other Other instruction whose flags are merged with this one.
  /// @return Combined MIFlags bitvector.
  LLVM_ABI uint32_t mergeFlagsWith(const MachineInstr &Other) const;

  /// Build MIFlags bits corresponding to the IR flags on instruction \p I.
  /// @param I LLVM IR instruction whose flags are translated.
  /// @return MIFlags bitvector derived from \p I.
  LLVM_ABI static uint32_t copyFlagsFromInstruction(const Instruction &I);

  /// Copy IR flags from instruction \p I into this MachineInstr's MIFlags.
  /// @param I LLVM IR instruction whose flags are copied.
  LLVM_ABI void copyIRFlags(const Instruction &I);

  /// Break any tie involving operand index \p OpIdx.
  /// @param OpIdx Index of the tied register operand to untie.
  void untieRegOperand(unsigned OpIdx) {
    MachineOperand &MO = getOperand(OpIdx);
    if (MO.isReg() && MO.isTied()) {
      getOperand(findTiedOperandIdx(OpIdx)).TiedTo = 0;
      MO.TiedTo = 0;
    }
  }

  /// Add all implicit def and use operands to this instruction.
  /// @param MF Machine function used to allocate new operands.
  LLVM_ABI void addImplicitDefUseOperands(MachineFunction &MF);

  /// Collect matching DBG_VALUEs that immediately follow this instruction.
  /// @param DbgValues Output vector that receives the collected debug values.
  LLVM_ABI void collectDebugValues(SmallVectorImpl<MachineInstr *> &DbgValues);

  /// Retarget DBG_VALUEs that refer to this instruction's def to \p Reg.
  /// @param Reg Register that should replace this instruction's defined register.
  LLVM_ABI void changeDebugValuesDefReg(Register Reg);

  /// Remove all PHI incoming values from basic block \p MBB.
  ///
  /// Does not erase the PHI even if it has a single remaining incoming value
  /// or none at all; the caller decides how to process the PHI afterwards.
  /// @param MBB Predecessor block whose incoming PHI operands are removed.
  /// @return Number of operands deleted.
  LLVM_ABI unsigned removePHIIncomingValueFor(const MachineBasicBlock &MBB);

  /// Sets all register debug operands in this debug value instruction to be
  /// undef.
  void setDebugValueUndef() {
    assert(isDebugValue() && "Must be a debug value instruction.");
    for (MachineOperand &MO : debug_operands()) {
      if (MO.isReg()) {
        MO.setReg(0);
        MO.setSubReg(0);
      }
    }
  }

  /// Return the registers of the first two operands.
  /// @return Tuple of registers from the first two operands.
  std::tuple<Register, Register> getFirst2Regs() const {
    return std::tuple(getOperand(0).getReg(), getOperand(1).getReg());
  }

  /// Return the registers of the first three operands.
  /// @return Tuple of registers from the first three operands.
  std::tuple<Register, Register, Register> getFirst3Regs() const {
    return std::tuple(getOperand(0).getReg(), getOperand(1).getReg(),
                      getOperand(2).getReg());
  }

  /// Return the registers of the first four operands.
  /// @return Tuple of registers from the first four operands.
  std::tuple<Register, Register, Register, Register> getFirst4Regs() const {
    return std::tuple(getOperand(0).getReg(), getOperand(1).getReg(),
                      getOperand(2).getReg(), getOperand(3).getReg());
  }

  /// Return the registers of the first five operands.
  /// @return Tuple of registers from the first five operands.
  std::tuple<Register, Register, Register, Register, Register>
  getFirst5Regs() const {
    return std::tuple(getOperand(0).getReg(), getOperand(1).getReg(),
                      getOperand(2).getReg(), getOperand(3).getReg(),
                      getOperand(4).getReg());
  }

  /// Return the LLTs of the first two operands.
  /// @return Tuple of LLTs for the first two operands.
  LLVM_ABI std::tuple<LLT, LLT> getFirst2LLTs() const;
  /// Return the LLTs of the first three operands.
  /// @return Tuple of LLTs for the first three operands.
  LLVM_ABI std::tuple<LLT, LLT, LLT> getFirst3LLTs() const;
  /// Return the LLTs of the first four operands.
  /// @return Tuple of LLTs for the first four operands.
  LLVM_ABI std::tuple<LLT, LLT, LLT, LLT> getFirst4LLTs() const;
  /// Return the LLTs of the first five operands.
  /// @return Tuple of LLTs for the first five operands.
  LLVM_ABI std::tuple<LLT, LLT, LLT, LLT, LLT> getFirst5LLTs() const;

  /// Return the registers and LLTs of the first two operands.
  /// @return Tuple of (reg0, llt0, reg1, llt1) for the first two operands.
  LLVM_ABI std::tuple<Register, LLT, Register, LLT> getFirst2RegLLTs() const;
  /// Return the registers and LLTs of the first three operands.
  /// @return Tuple of (reg, LLT) pairs for the first three operands.
  LLVM_ABI std::tuple<Register, LLT, Register, LLT, Register, LLT>
  getFirst3RegLLTs() const;
  /// Return the registers and LLTs of the first four operands.
  /// @return Tuple of (reg, LLT) pairs for the first four operands.
  LLVM_ABI
  std::tuple<Register, LLT, Register, LLT, Register, LLT, Register, LLT>
  getFirst4RegLLTs() const;
  /// Return the registers and LLTs of the first five operands.
  /// @return Tuple of (reg, LLT) pairs for the first five operands.
  LLVM_ABI std::tuple<Register, LLT, Register, LLT, Register, LLT, Register,
                      LLT, Register, LLT>
  getFirst5RegLLTs() const;

private:
  /// If this instruction is embedded into a MachineFunction, return the
  /// MachineRegisterInfo object for the current function, otherwise
  /// return null.
  MachineRegisterInfo *getRegInfo();
  const MachineRegisterInfo *getRegInfo() const;

  /// Unlink all of the register operands in this instruction from their
  /// respective use lists.  This requires that the operands already be on their
  /// use lists.
  void removeRegOperandsFromUseLists(MachineRegisterInfo&);

  /// Add all of the register operands in this instruction from their
  /// respective use lists.  This requires that the operands not be on their
  /// use lists yet.
  void addRegOperandsToUseLists(MachineRegisterInfo&);

  /// Slow path for hasProperty when we're dealing with a bundle.
  LLVM_ABI bool hasPropertyInBundle(uint64_t Mask, QueryType Type) const;

  /// Implements the logic of getRegClassConstraintEffectForVReg for the
  /// this MI and the given operand index \p OpIdx.
  /// If the related operand does not constrained Reg, this returns CurRC.
  const TargetRegisterClass *getRegClassConstraintEffectForVRegImpl(
      unsigned OpIdx, Register Reg, const TargetRegisterClass *CurRC,
      const TargetInstrInfo *TII, const TargetRegisterInfo *TRI) const;

  /// Stores extra instruction information inline or allocates as ExtraInfo
  /// based on the number of pointers.
  void setExtraInfo(MachineFunction &MF, ArrayRef<MachineMemOperand *> MMOs,
                    MCSymbol *PreInstrSymbol, MCSymbol *PostInstrSymbol,
                    MDNode *HeapAllocMarker, MDNode *PCSections,
                    uint32_t CFIType, MDNode *MMRAs, Value *DS);
};

/// DenseMapInfo traits that compare MachineInstr* by instruction value.
///
/// Hashing and equality ignore definitions, which is useful for CSE and
/// similar value-based lookups.
struct MachineInstrExpressionTrait : DenseMapInfo<MachineInstr *> {
  /// Compute a hash of \p MI based on opcode and operands, ignoring defs.
  /// @param MI Instruction to hash.
  /// @return Hash value for value-based DenseMap lookup.
  LLVM_ABI static unsigned getHashValue(const MachineInstr *const &MI);

  /// Return true if \p LHS and \p RHS are identical ignoring virtual defs.
  /// @param LHS Left-hand instruction pointer.
  /// @param RHS Right-hand instruction pointer.
  /// @return True if the instructions compare equal under IgnoreVRegDefs.
  static bool isEqual(const MachineInstr *const &LHS,
                      const MachineInstr *const &RHS) {
    return LHS->isIdenticalTo(*RHS, MachineInstr::IgnoreVRegDefs);
  }
};

//===----------------------------------------------------------------------===//
// Debugging Support

/// Print machine instruction \p MI to stream \p OS.
/// @param OS Output stream.
/// @param MI Instruction to print.
/// @return \p OS after printing.
inline raw_ostream& operator<<(raw_ostream &OS, const MachineInstr &MI) {
  MI.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEINSTR_H
