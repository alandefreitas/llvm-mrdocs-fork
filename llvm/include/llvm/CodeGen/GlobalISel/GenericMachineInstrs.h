//===- llvm/CodeGen/GlobalISel/GenericMachineInstrs.h -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Declares convenience wrapper classes for interpreting MachineInstr instances
/// as specific generic operations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_GENERICMACHINEINSTRS_H
#define LLVM_CODEGEN_GLOBALISEL_GENERICMACHINEINSTRS_H

#include "llvm/ADT/APInt.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Casting.h"

namespace llvm {

/// A base class for all GenericMachineInstrs.
class GenericMachineInstr : public MachineInstr {
  constexpr static unsigned PoisonFlags =
      NoUWrap | NoSWrap | NoUSWrap | IsExact | Disjoint | NonNeg | FmNoNans |
      FmNoInfs | SameSign | InBounds;

public:
  /// This class is not default-constructible.
  GenericMachineInstr() = delete;

  /// Access the Idx'th operand as a register and return it.
  ///
  /// This assumes that the Idx'th operand is a Register type.
  /// \param Idx - Operand index of the register to access.
  /// \return The Idx'th operand as a register.
  Register getReg(unsigned Idx) const { return getOperand(Idx).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GenericMachineInstr.
  static bool classof(const MachineInstr *MI) {
    return isPreISelGenericOpcode(MI->getOpcode());
  }

  /// Return true if this instruction has any poison-generating flags set.
  /// \return True if this instruction has any poison-generating flags set.
  bool hasPoisonGeneratingFlags() const { return getFlags() & PoisonFlags; }

  /// Clear all poison-generating flags on this instruction.
  void dropPoisonGeneratingFlags() {
    clearFlags(PoisonFlags);
    assert(!hasPoisonGeneratingFlags());
  }
};

/// Provides common memory operand functionality.
class GMemOperation : public GenericMachineInstr {
public:
  /// Get the MachineMemOperand on this instruction.
  /// \return The MachineMemOperand on this instruction.
  MachineMemOperand &getMMO() const { return **memoperands_begin(); }

  /// Returns true if the attached MachineMemOperand  has the atomic flag set.
  /// \return True if the attached MachineMemOperand  has the atomic flag set.
  bool isAtomic() const { return getMMO().isAtomic(); }
  /// Returns true if the attached MachineMemOpeand as the volatile flag set.
  /// \return True if the attached MachineMemOpeand as the volatile flag set.
  bool isVolatile() const { return getMMO().isVolatile(); }
  /// Returns true if the memory operation is neither atomic or volatile.
  /// \return True if the memory operation is neither atomic or volatile.
  bool isSimple() const { return !isAtomic() && !isVolatile(); }
  /// Return true if this memory operation is unordered.
  ///
  /// An unordered operation has no ordering constraints other than normal
  /// aliasing. Volatile and (ordered) atomic memory operations can't be
  /// reordered.
  /// \return True if this memory operation is unordered.
  bool isUnordered() const { return getMMO().isUnordered(); }

  /// Return the minimum known alignment in bytes of the actual memory
  /// reference.
  /// \return The minimum known alignment in bytes of the actual memory
  /// reference.
  Align getAlign() const { return getMMO().getAlign(); }
  /// Returns the size in bytes of the memory access.
  /// \return The size in bytes of the memory access.
  LocationSize getMemSize() const { return getMMO().getSize(); }
  /// Returns the size in bits of the memory access.
  /// \return The size in bits of the memory access.
  LocationSize getMemSizeInBits() const { return getMMO().getSizeInBits(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GMemOperation.
  static bool classof(const MachineInstr *MI) {
    return GenericMachineInstr::classof(MI) && MI->hasOneMemOperand();
  }
};

/// Represents any type of generic load or store.
/// G_LOAD, G_STORE, G_ZEXTLOAD, G_SEXTLOAD, G_FPEXTLOAD, G_FPTRUNCSTORE.
class GLoadStore : public GMemOperation {
public:
  /// Get the source register of the pointer value.
  /// \return The source register of the pointer value.
  Register getPointerReg() const { return getOperand(1).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GLoadStore.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_LOAD:
    case TargetOpcode::G_STORE:
    case TargetOpcode::G_ZEXTLOAD:
    case TargetOpcode::G_SEXTLOAD:
    case TargetOpcode::G_FPEXTLOAD:
    case TargetOpcode::G_FPTRUNCSTORE:
      return true;
    default:
      return false;
    }
  }
};

/// Represents indexed loads.
///
/// These are different enough from regular loads that they get their own
/// class. Including them in GAnyLoad would probably make a footgun for
/// someone.
class GIndexedLoad : public GMemOperation {
public:
  /// Get the definition register of the loaded value.
  /// \return The definition register of the loaded value.
  Register getDstReg() const { return getOperand(0).getReg(); }
  /// Get the def register of the writeback value.
  /// \return The def register of the writeback value.
  Register getWritebackReg() const { return getOperand(1).getReg(); }
  /// Get the base register of the pointer value.
  /// \return The base register of the pointer value.
  Register getBaseReg() const { return getOperand(2).getReg(); }
  /// Get the offset register of the pointer value.
  /// \return The offset register of the pointer value.
  Register getOffsetReg() const { return getOperand(3).getReg(); }

  /// Return true if this is a pre-indexed load.
  /// \return True if this is a pre-indexed load.
  bool isPre() const { return getOperand(4).getImm() == 1; }
  /// Return true if this is a post-indexed load.
  /// \return True if this is a post-indexed load.
  bool isPost() const { return !isPre(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GIndexedLoad.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INDEXED_LOAD;
  }
};

/// Represents a G_INDEX_ZEXTLOAD/G_INDEXED_SEXTLOAD.
class GIndexedExtLoad : public GIndexedLoad {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GIndexedExtLoad.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INDEXED_SEXTLOAD ||
           MI->getOpcode() == TargetOpcode::G_INDEXED_ZEXTLOAD;
  }
};

/// Represents either G_INDEXED_LOAD, G_INDEXED_ZEXTLOAD or G_INDEXED_SEXTLOAD.
class GIndexedAnyExtLoad : public GIndexedLoad {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GIndexedAnyExtLoad.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_INDEXED_LOAD:
    case TargetOpcode::G_INDEXED_ZEXTLOAD:
    case TargetOpcode::G_INDEXED_SEXTLOAD:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a G_ZEXTLOAD.
class GIndexedZExtLoad : GIndexedExtLoad {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GIndexedZExtLoad.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INDEXED_ZEXTLOAD;
  }
};

/// Represents a G_SEXTLOAD.
class GIndexedSExtLoad : GIndexedExtLoad {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GIndexedSExtLoad.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INDEXED_SEXTLOAD;
  }
};

/// Represents indexed stores.
class GIndexedStore : public GMemOperation {
public:
  /// Get the def register of the writeback value.
  /// \return The def register of the writeback value.
  Register getWritebackReg() const { return getOperand(0).getReg(); }
  /// Get the stored value register.
  /// \return The stored value register.
  Register getValueReg() const { return getOperand(1).getReg(); }
  /// Get the base register of the pointer value.
  /// \return The base register of the pointer value.
  Register getBaseReg() const { return getOperand(2).getReg(); }
  /// Get the offset register of the pointer value.
  /// \return The offset register of the pointer value.
  Register getOffsetReg() const { return getOperand(3).getReg(); }

  /// Return true if this is a pre-indexed store.
  /// \return True if this is a pre-indexed store.
  bool isPre() const { return getOperand(4).getImm() == 1; }
  /// Return true if this is a post-indexed store.
  /// \return True if this is a post-indexed store.
  bool isPost() const { return !isPre(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GIndexedStore.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INDEXED_STORE;
  }
};

/// Represents any generic load, including sign/zero extending variants.
class GAnyLoad : public GLoadStore {
public:
  /// Get the definition register of the loaded value.
  /// \return The definition register of the loaded value.
  Register getDstReg() const { return getOperand(0).getReg(); }

  /// Returns the Ranges that describes the dereference.
  /// \return The Ranges that describes the dereference.
  const MDNode *getRanges() const {
    return getMMO().getRanges();
  }

  /// Returns the cache hint metadata for this load.
  /// \return The cache hint metadata for this load.
  const MDNode *getMemCacheHint() const { return getMMO().getMemCacheHint(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GAnyLoad.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_LOAD:
    case TargetOpcode::G_ZEXTLOAD:
    case TargetOpcode::G_SEXTLOAD:
    case TargetOpcode::G_FPEXTLOAD:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a G_LOAD.
class GLoad : public GAnyLoad {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GLoad.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_LOAD;
  }
};

/// Represents either a G_SEXTLOAD, G_ZEXTLOAD, or G_FPEXTLOAD.
class GExtLoad : public GAnyLoad {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GExtLoad.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SEXTLOAD ||
           MI->getOpcode() == TargetOpcode::G_ZEXTLOAD ||
           MI->getOpcode() == TargetOpcode::G_FPEXTLOAD;
  }
};

/// Represents a G_SEXTLOAD.
class GSExtLoad : public GExtLoad {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GSExtLoad.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SEXTLOAD;
  }
};

/// Represents a G_ZEXTLOAD.
class GZExtLoad : public GExtLoad {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GZExtLoad.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ZEXTLOAD;
  }
};

/// Represents a G_FPEXTLOAD.
class GFPExtLoad : public GAnyLoad {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GFPExtLoad.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_FPEXTLOAD;
  }
};

/// Represents any generic store, including truncating variants.
class GAnyStore : public GLoadStore {
public:
  /// Get the stored value register.
  /// \return The stored value register.
  Register getValueReg() const { return getOperand(0).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GAnyStore.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_STORE:
    case TargetOpcode::G_FPTRUNCSTORE:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a G_STORE.
class GStore : public GAnyStore {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GStore.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_STORE;
  }
};

/// Represents a G_FPTRUNCSTORE.
class GFPTruncStore : public GAnyStore {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GFPTruncStore.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_FPTRUNCSTORE;
  }
};

/// Represents a G_UNMERGE_VALUES.
class GUnmerge : public GenericMachineInstr {
public:
  /// Returns the number of def registers.
  /// \return The number of def registers.
  unsigned getNumDefs() const { return getNumOperands() - 1; }
  /// Get the unmerge source register.
  /// \return The unmerge source register.
  Register getSourceReg() const { return getOperand(getNumDefs()).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GUnmerge.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_UNMERGE_VALUES;
  }
};

/// Represents G_BUILD_VECTOR, G_CONCAT_VECTORS or G_MERGE_VALUES.
/// All these have the common property of generating a single value from
/// multiple sources.
class GMergeLikeInstr : public GenericMachineInstr {
public:
  /// Returns the number of source registers.
  /// \return The number of source registers.
  unsigned getNumSources() const { return getNumOperands() - 1; }
  /// Returns the I'th source register.
  /// \param I - Zero-based index of the source register.
  /// \return The I'th source register.
  Register getSourceReg(unsigned I) const { return getReg(I + 1); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GMergeLikeInstr.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_MERGE_VALUES:
    case TargetOpcode::G_CONCAT_VECTORS:
    case TargetOpcode::G_BUILD_VECTOR:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a G_MERGE_VALUES.
class GMerge : public GMergeLikeInstr {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GMerge.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_MERGE_VALUES;
  }
};

/// Represents a G_CONCAT_VECTORS.
class GConcatVectors : public GMergeLikeInstr {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GConcatVectors.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_CONCAT_VECTORS;
  }
};

/// Represents a G_BUILD_VECTOR.
class GBuildVector : public GMergeLikeInstr {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GBuildVector.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_BUILD_VECTOR;
  }
};

/// Represents a G_BUILD_VECTOR_TRUNC.
class GBuildVectorTrunc : public GMergeLikeInstr {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GBuildVectorTrunc.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_BUILD_VECTOR_TRUNC;
  }
};

/// Represents a G_SHUFFLE_VECTOR.
class GShuffleVector : public GenericMachineInstr {
public:
  /// Get the first source vector register.
  /// \return The first source vector register.
  Register getSrc1Reg() const { return getOperand(1).getReg(); }
  /// Get the second source vector register.
  /// \return The second source vector register.
  Register getSrc2Reg() const { return getOperand(2).getReg(); }
  /// Get the shuffle mask.
  /// \return The shuffle mask.
  ArrayRef<int> getMask() const { return getOperand(3).getShuffleMask(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GShuffleVector.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SHUFFLE_VECTOR;
  }
};

/// Represents a G_PTR_ADD.
class GPtrAdd : public GenericMachineInstr {
public:
  /// Get the base pointer register.
  /// \return The base pointer register.
  Register getBaseReg() const { return getReg(1); }
  /// Get the offset register.
  /// \return The offset register.
  Register getOffsetReg() const { return getReg(2); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GPtrAdd.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_PTR_ADD;
  }
};

/// Represents a G_IMPLICIT_DEF.
class GImplicitDef : public GenericMachineInstr {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GImplicitDef.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_IMPLICIT_DEF;
  }
};

/// Represents a G_SELECT.
class GSelect : public GenericMachineInstr {
public:
  /// Get the condition register.
  /// \return The condition register.
  Register getCondReg() const { return getReg(1); }
  /// Get the true-value register.
  /// \return The true-value register.
  Register getTrueReg() const { return getReg(2); }
  /// Get the false-value register.
  /// \return The false-value register.
  Register getFalseReg() const { return getReg(3); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GSelect.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SELECT;
  }
};

/// Represent a G_ICMP or G_FCMP.
class GAnyCmp : public GenericMachineInstr {
public:
  /// Get the comparison predicate.
  /// \return The comparison predicate.
  CmpInst::Predicate getCond() const {
    return static_cast<CmpInst::Predicate>(getOperand(1).getPredicate());
  }
  /// Get the left-hand side register.
  /// \return The left-hand side register.
  Register getLHSReg() const { return getReg(2); }
  /// Get the right-hand side register.
  /// \return The right-hand side register.
  Register getRHSReg() const { return getReg(3); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GAnyCmp.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ICMP ||
           MI->getOpcode() == TargetOpcode::G_FCMP;
  }
};

/// Represent a G_ICMP.
class GICmp : public GAnyCmp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GICmp.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ICMP;
  }
};

/// Represent a G_FCMP.
class GFCmp : public GAnyCmp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GFCmp.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_FCMP;
  }
};

/// Represents overflowing binary operations.
///
/// Only carry-out:
/// G_UADDO, G_SADDO, G_USUBO, G_SSUBO, G_UMULO, G_SMULO
/// Carry-in and carry-out:
/// G_UADDE, G_SADDE, G_USUBE, G_SSUBE
class GBinOpCarryOut : public GenericMachineInstr {
public:
  /// Get the destination register for the arithmetic result.
  /// \return The destination register for the arithmetic result.
  Register getDstReg() const { return getReg(0); }
  /// Get the carry-out register.
  /// \return The carry-out register.
  Register getCarryOutReg() const { return getReg(1); }
  /// Get the left-hand side operand.
  /// \return The left-hand side operand.
  MachineOperand &getLHS() { return getOperand(2); }
  /// Get the right-hand side operand.
  /// \return The right-hand side operand.
  MachineOperand &getRHS() { return getOperand(3); }
  /// Get the left-hand side register.
  /// \return The left-hand side register.
  Register getLHSReg() const { return getOperand(2).getReg(); }
  /// Get the right-hand side register.
  /// \return The right-hand side register.
  Register getRHSReg() const { return getOperand(3).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GBinOpCarryOut.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_UADDO:
    case TargetOpcode::G_SADDO:
    case TargetOpcode::G_USUBO:
    case TargetOpcode::G_SSUBO:
    case TargetOpcode::G_UADDE:
    case TargetOpcode::G_SADDE:
    case TargetOpcode::G_USUBE:
    case TargetOpcode::G_SSUBE:
    case TargetOpcode::G_UMULO:
    case TargetOpcode::G_SMULO:
      return true;
    default:
      return false;
    }
  }
};

/// Represents overflowing add/sub operations.
/// Only carry-out:
/// G_UADDO, G_SADDO, G_USUBO, G_SSUBO
/// Carry-in and carry-out:
/// G_UADDE, G_SADDE, G_USUBE, G_SSUBE
class GAddSubCarryOut : public GBinOpCarryOut {
public:
  /// Return true if this is an overflowing add.
  /// \return True if this is an overflowing add.
  bool isAdd() const {
    switch (getOpcode()) {
    case TargetOpcode::G_UADDO:
    case TargetOpcode::G_SADDO:
    case TargetOpcode::G_UADDE:
    case TargetOpcode::G_SADDE:
      return true;
    default:
      return false;
    }
  }
  /// Return true if this is an overflowing subtract.
  /// \return True if this is an overflowing subtract.
  bool isSub() const { return !isAdd(); }

  /// Return true if this is a signed overflowing add or subtract.
  /// \return True if this is a signed overflowing add or subtract.
  bool isSigned() const {
    switch (getOpcode()) {
    case TargetOpcode::G_SADDO:
    case TargetOpcode::G_SSUBO:
    case TargetOpcode::G_SADDE:
    case TargetOpcode::G_SSUBE:
      return true;
    default:
      return false;
    }
  }
  /// Return true if this is an unsigned overflowing add or subtract.
  /// \return True if this is an unsigned overflowing add or subtract.
  bool isUnsigned() const { return !isSigned(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GAddSubCarryOut.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_UADDO:
    case TargetOpcode::G_SADDO:
    case TargetOpcode::G_USUBO:
    case TargetOpcode::G_SSUBO:
    case TargetOpcode::G_UADDE:
    case TargetOpcode::G_SADDE:
    case TargetOpcode::G_USUBE:
    case TargetOpcode::G_SSUBE:
      return true;
    default:
      return false;
    }
  }
};

/// Represents overflowing add operations.
/// G_UADDO, G_SADDO
class GAddCarryOut : public GBinOpCarryOut {
public:
  /// Return true if this is a signed overflowing add.
  /// \return True if this is a signed overflowing add.
  bool isSigned() const { return getOpcode() == TargetOpcode::G_SADDO; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GAddCarryOut.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_UADDO:
    case TargetOpcode::G_SADDO:
      return true;
    default:
      return false;
    }
  }
};

/// Represents overflowing sub operations.
/// G_USUBO, G_SSUBO
class GSubCarryOut : public GBinOpCarryOut {
public:
  /// Return true if this is a signed overflowing subtract.
  /// \return True if this is a signed overflowing subtract.
  bool isSigned() const { return getOpcode() == TargetOpcode::G_SSUBO; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GSubCarryOut.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_USUBO:
    case TargetOpcode::G_SSUBO:
      return true;
    default:
      return false;
    }
  }
};

/// Represents overflowing add/sub operations that also consume a carry-in.
/// G_UADDE, G_SADDE, G_USUBE, G_SSUBE
class GAddSubCarryInOut : public GAddSubCarryOut {
public:
  /// Get the carry-in register.
  /// \return The carry-in register.
  Register getCarryInReg() const { return getReg(4); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GAddSubCarryInOut.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_UADDE:
    case TargetOpcode::G_SADDE:
    case TargetOpcode::G_USUBE:
    case TargetOpcode::G_SSUBE:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a call to an intrinsic.
class GIntrinsic final : public GenericMachineInstr {
public:
  /// Get the intrinsic identifier.
  /// \return The intrinsic identifier.
  Intrinsic::ID getIntrinsicID() const {
    return getOperand(getNumExplicitDefs()).getIntrinsicID();
  }

  /// Return true if this intrinsic call matches \p ID.
  /// \param ID - Intrinsic identifier to compare against.
  /// \return True if this intrinsic call matches \p ID.
  bool is(Intrinsic::ID ID) const { return getIntrinsicID() == ID; }

  /// Return true if this intrinsic may have side effects.
  /// \return True if this intrinsic may have side effects.
  bool hasSideEffects() const {
    switch (getOpcode()) {
    case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS:
    case TargetOpcode::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS:
      return true;
    default:
      return false;
    }
  }

  /// Return true if this intrinsic is convergent.
  /// \return True if this intrinsic is convergent.
  bool isConvergent() const {
    switch (getOpcode()) {
    case TargetOpcode::G_INTRINSIC_CONVERGENT:
    case TargetOpcode::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS:
      return true;
    default:
      return false;
    }
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GIntrinsic.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_INTRINSIC:
    case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS:
    case TargetOpcode::G_INTRINSIC_CONVERGENT:
    case TargetOpcode::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a (non-sequential) vector reduction operation.
class GVecReduce : public GenericMachineInstr {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GVecReduce.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_VECREDUCE_FADD:
    case TargetOpcode::G_VECREDUCE_FMUL:
    case TargetOpcode::G_VECREDUCE_FMAX:
    case TargetOpcode::G_VECREDUCE_FMIN:
    case TargetOpcode::G_VECREDUCE_FMAXIMUM:
    case TargetOpcode::G_VECREDUCE_FMINIMUM:
    case TargetOpcode::G_VECREDUCE_FMAXIMUMNUM:
    case TargetOpcode::G_VECREDUCE_FMINIMUMNUM:
    case TargetOpcode::G_VECREDUCE_ADD:
    case TargetOpcode::G_VECREDUCE_MUL:
    case TargetOpcode::G_VECREDUCE_AND:
    case TargetOpcode::G_VECREDUCE_OR:
    case TargetOpcode::G_VECREDUCE_XOR:
    case TargetOpcode::G_VECREDUCE_SMAX:
    case TargetOpcode::G_VECREDUCE_SMIN:
    case TargetOpcode::G_VECREDUCE_UMAX:
    case TargetOpcode::G_VECREDUCE_UMIN:
      return true;
    default:
      return false;
    }
  }

  /// Get the opcode for the equivalent scalar operation for this reduction.
  /// E.g. for G_VECREDUCE_FADD, this returns G_FADD.
  /// \return The opcode for the equivalent scalar operation for this reduction.
  unsigned getScalarOpcForReduction() {
    unsigned ScalarOpc;
    switch (getOpcode()) {
    case TargetOpcode::G_VECREDUCE_FADD:
      ScalarOpc = TargetOpcode::G_FADD;
      break;
    case TargetOpcode::G_VECREDUCE_FMUL:
      ScalarOpc = TargetOpcode::G_FMUL;
      break;
    case TargetOpcode::G_VECREDUCE_FMAX:
      ScalarOpc = TargetOpcode::G_FMAXNUM;
      break;
    case TargetOpcode::G_VECREDUCE_FMIN:
      ScalarOpc = TargetOpcode::G_FMINNUM;
      break;
    case TargetOpcode::G_VECREDUCE_FMAXIMUM:
      ScalarOpc = TargetOpcode::G_FMAXIMUM;
      break;
    case TargetOpcode::G_VECREDUCE_FMINIMUM:
      ScalarOpc = TargetOpcode::G_FMINIMUM;
      break;
    case TargetOpcode::G_VECREDUCE_FMAXIMUMNUM:
      ScalarOpc = TargetOpcode::G_FMAXIMUMNUM;
      break;
    case TargetOpcode::G_VECREDUCE_FMINIMUMNUM:
      ScalarOpc = TargetOpcode::G_FMINIMUMNUM;
      break;
    case TargetOpcode::G_VECREDUCE_ADD:
      ScalarOpc = TargetOpcode::G_ADD;
      break;
    case TargetOpcode::G_VECREDUCE_MUL:
      ScalarOpc = TargetOpcode::G_MUL;
      break;
    case TargetOpcode::G_VECREDUCE_AND:
      ScalarOpc = TargetOpcode::G_AND;
      break;
    case TargetOpcode::G_VECREDUCE_OR:
      ScalarOpc = TargetOpcode::G_OR;
      break;
    case TargetOpcode::G_VECREDUCE_XOR:
      ScalarOpc = TargetOpcode::G_XOR;
      break;
    case TargetOpcode::G_VECREDUCE_SMAX:
      ScalarOpc = TargetOpcode::G_SMAX;
      break;
    case TargetOpcode::G_VECREDUCE_SMIN:
      ScalarOpc = TargetOpcode::G_SMIN;
      break;
    case TargetOpcode::G_VECREDUCE_UMAX:
      ScalarOpc = TargetOpcode::G_UMAX;
      break;
    case TargetOpcode::G_VECREDUCE_UMIN:
      ScalarOpc = TargetOpcode::G_UMIN;
      break;
    default:
      llvm_unreachable("Unhandled reduction");
    }
    return ScalarOpc;
  }
};

/// Represents a G_PHI.
class GPhi : public GenericMachineInstr {
public:
  /// Returns the number of incoming values.
  /// \return The number of incoming values.
  unsigned getNumIncomingValues() const { return (getNumOperands() - 1) / 2; }
  /// Returns the I'th incoming vreg.
  /// \param I - Zero-based index of the incoming value.
  /// \return The I'th incoming vreg.
  Register getIncomingValue(unsigned I) const {
    return getOperand(I * 2 + 1).getReg();
  }
  /// Returns the I'th incoming basic block.
  /// \param I - Zero-based index of the incoming block.
  /// \return The I'th incoming basic block.
  MachineBasicBlock *getIncomingBlock(unsigned I) const {
    return getOperand(I * 2 + 2).getMBB();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GPhi.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_PHI;
  }
};

/// Represents a binary operation, i.e, x = y op z.
class GBinOp : public GenericMachineInstr {
public:
  /// Get the left-hand side register.
  /// \return The left-hand side register.
  Register getLHSReg() const { return getReg(1); }
  /// Get the right-hand side register.
  /// \return The right-hand side register.
  Register getRHSReg() const { return getReg(2); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GBinOp.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    // Integer.
    case TargetOpcode::G_ADD:
    case TargetOpcode::G_SUB:
    case TargetOpcode::G_MUL:
    case TargetOpcode::G_SDIV:
    case TargetOpcode::G_UDIV:
    case TargetOpcode::G_SREM:
    case TargetOpcode::G_UREM:
    case TargetOpcode::G_SMIN:
    case TargetOpcode::G_SMAX:
    case TargetOpcode::G_UMIN:
    case TargetOpcode::G_UMAX:
    // Floating point.
    case TargetOpcode::G_FMINNUM:
    case TargetOpcode::G_FMAXNUM:
    case TargetOpcode::G_FMINNUM_IEEE:
    case TargetOpcode::G_FMAXNUM_IEEE:
    case TargetOpcode::G_FMINIMUM:
    case TargetOpcode::G_FMAXIMUM:
    case TargetOpcode::G_FADD:
    case TargetOpcode::G_FSUB:
    case TargetOpcode::G_FMUL:
    case TargetOpcode::G_FDIV:
    case TargetOpcode::G_FPOW:
    // Logical.
    case TargetOpcode::G_AND:
    case TargetOpcode::G_OR:
    case TargetOpcode::G_XOR:
      return true;
    default:
      return false;
    }
  };
};

/// Represents an integer binary operation.
class GIntBinOp : public GBinOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GIntBinOp.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_ADD:
    case TargetOpcode::G_SUB:
    case TargetOpcode::G_MUL:
    case TargetOpcode::G_SDIV:
    case TargetOpcode::G_UDIV:
    case TargetOpcode::G_SREM:
    case TargetOpcode::G_UREM:
    case TargetOpcode::G_SMIN:
    case TargetOpcode::G_SMAX:
    case TargetOpcode::G_UMIN:
    case TargetOpcode::G_UMAX:
      return true;
    default:
      return false;
    }
  };
};

/// Represents a floating point binary operation.
class GFBinOp : public GBinOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GFBinOp.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_FMINNUM:
    case TargetOpcode::G_FMAXNUM:
    case TargetOpcode::G_FMINNUM_IEEE:
    case TargetOpcode::G_FMAXNUM_IEEE:
    case TargetOpcode::G_FMINIMUM:
    case TargetOpcode::G_FMAXIMUM:
    case TargetOpcode::G_FADD:
    case TargetOpcode::G_FSUB:
    case TargetOpcode::G_FMUL:
    case TargetOpcode::G_FDIV:
    case TargetOpcode::G_FPOW:
      return true;
    default:
      return false;
    }
  };
};

/// Represents a logical binary operation.
class GLogicalBinOp : public GBinOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GLogicalBinOp.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_AND:
    case TargetOpcode::G_OR:
    case TargetOpcode::G_XOR:
      return true;
    default:
      return false;
    }
  };
};

/// Represents an integer addition.
class GAdd : public GIntBinOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GAdd.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ADD;
  };
};

/// Represents a logical and.
class GAnd : public GLogicalBinOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GAnd.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_AND;
  };
};

/// Represents a logical or.
class GOr : public GLogicalBinOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GOr.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_OR;
  };
};

/// Represents an extract vector element.
class GExtractVectorElement : public GenericMachineInstr {
public:
  /// Get the source vector register.
  /// \return The source vector register.
  Register getVectorReg() const { return getOperand(1).getReg(); }
  /// Get the element index register.
  /// \return The element index register.
  Register getIndexReg() const { return getOperand(2).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GExtractVectorElement.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_EXTRACT_VECTOR_ELT;
  }
};

/// Represents an insert vector element.
class GInsertVectorElement : public GenericMachineInstr {
public:
  /// Get the destination vector register's source vector.
  /// \return The destination vector register's source vector.
  Register getVectorReg() const { return getOperand(1).getReg(); }
  /// Get the element value register to insert.
  /// \return The element value register to insert.
  Register getElementReg() const { return getOperand(2).getReg(); }
  /// Get the element index register.
  /// \return The element index register.
  Register getIndexReg() const { return getOperand(3).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GInsertVectorElement.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INSERT_VECTOR_ELT;
  }
};

/// Represents an extract subvector.
class GExtractSubvector : public GenericMachineInstr {
public:
  /// Get the source vector register.
  /// \return The source vector register.
  Register getSrcVec() const { return getOperand(1).getReg(); }
  /// Get the starting index immediate.
  /// \return The starting index immediate.
  uint64_t getIndexImm() const { return getOperand(2).getImm(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GExtractSubvector.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_EXTRACT_SUBVECTOR;
  }
};

/// Represents a insert subvector.
class GInsertSubvector : public GenericMachineInstr {
public:
  /// Get the large destination vector register.
  /// \return The large destination vector register.
  Register getBigVec() const { return getOperand(1).getReg(); }
  /// Get the subvector register to insert.
  /// \return The subvector register to insert.
  Register getSubVec() const { return getOperand(2).getReg(); }
  /// Get the insertion index immediate.
  /// \return The insertion index immediate.
  uint64_t getIndexImm() const { return getOperand(3).getImm(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GInsertSubvector.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INSERT_SUBVECTOR;
  }
};

/// Represents a freeze.
class GFreeze : public GenericMachineInstr {
public:
  /// Get the source register being frozen.
  /// \return The source register being frozen.
  Register getSourceReg() const { return getOperand(1).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GFreeze.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_FREEZE;
  }
};

/// Represents a cast operation.
/// It models the llvm::CastInst concept.
/// The exception is bitcast.
class GCastOp : public GenericMachineInstr {
public:
  /// Get the source register being cast.
  /// \return The source register being cast.
  Register getSrcReg() const { return getOperand(1).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GCastOp.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_ADDRSPACE_CAST:
    case TargetOpcode::G_FPEXT:
    case TargetOpcode::G_FPTOSI:
    case TargetOpcode::G_FPTOUI:
    case TargetOpcode::G_FPTOSI_SAT:
    case TargetOpcode::G_FPTOUI_SAT:
    case TargetOpcode::G_FPTRUNC:
    case TargetOpcode::G_INTTOPTR:
    case TargetOpcode::G_PTRTOINT:
    case TargetOpcode::G_SEXT:
    case TargetOpcode::G_SITOFP:
    case TargetOpcode::G_TRUNC:
    case TargetOpcode::G_TRUNC_SSAT_S:
    case TargetOpcode::G_TRUNC_SSAT_U:
    case TargetOpcode::G_TRUNC_USAT_U:
    case TargetOpcode::G_UITOFP:
    case TargetOpcode::G_ZEXT:
    case TargetOpcode::G_ANYEXT:
      return true;
    default:
      return false;
    }
  };
};

/// Represents a sext.
class GSext : public GCastOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GSext.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SEXT;
  };
};

/// Represents a zext.
class GZext : public GCastOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GZext.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ZEXT;
  };
};

/// Represents an any ext.
class GAnyExt : public GCastOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GAnyExt.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ANYEXT;
  };
};

/// Represents a trunc.
class GTrunc : public GCastOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GTrunc.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_TRUNC;
  };
};

/// Represents a vscale.
class GVScale : public GenericMachineInstr {
public:
  /// Get the scale factor constant.
  /// \return The scale factor constant.
  APInt getSrc() const { return getOperand(1).getCImm()->getValue(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GVScale.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_VSCALE;
  };
};

/// Represents a step vector.
class GStepVector : public GenericMachineInstr {
public:
  /// Get the step amount as a zero-extended immediate.
  /// \return The step amount as a zero-extended immediate.
  uint64_t getStep() const {
    return getOperand(1).getCImm()->getValue().getZExtValue();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GStepVector.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_STEP_VECTOR;
  };
};

/// Represents a G_CONSTANT.
class GConstant : public GenericMachineInstr {
public:
  /// Get the constant integer operand.
  /// \return The constant integer operand.
  const ConstantInt *getConstantInt() const { return getOperand(1).getCImm(); }
  /// Get the APInt value of the constant.
  /// \return The APInt value of the constant.
  const APInt &getValue() const { return getConstantInt()->getValue(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GConstant.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_CONSTANT;
  };
};

/// Represents an integer subtraction.
class GSub : public GIntBinOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GSub.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SUB;
  };
};

/// Represents an integer multiplication.
class GMul : public GIntBinOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GMul.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_MUL;
  };
};

/// Represents a shift left.
class GShl : public GenericMachineInstr {
public:
  /// Get the value register being shifted.
  /// \return The value register being shifted.
  Register getSrcReg() const { return getOperand(1).getReg(); }
  /// Get the shift amount register.
  /// \return The shift amount register.
  Register getShiftReg() const { return getOperand(2).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GShl.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SHL;
  };
};

/// Represents a threeway compare.
class GSUCmp : public GenericMachineInstr {
public:
  /// Get the left-hand side register.
  /// \return The left-hand side register.
  Register getLHSReg() const { return getOperand(1).getReg(); }
  /// Get the right-hand side register.
  /// \return The right-hand side register.
  Register getRHSReg() const { return getOperand(2).getReg(); }

  /// Return true if this is a signed three-way compare.
  /// \return True if this is a signed three-way compare.
  bool isSigned() const { return getOpcode() == TargetOpcode::G_SCMP; }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GSUCmp.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_SCMP:
    case TargetOpcode::G_UCMP:
      return true;
    default:
      return false;
    }
  };
};

/// Represents an integer-like extending operation.
class GExtOp : public GCastOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GExtOp.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_SEXT:
    case TargetOpcode::G_ZEXT:
    case TargetOpcode::G_ANYEXT:
      return true;
    default:
      return false;
    }
  };
};

/// Represents an integer-like extending or truncating operation.
class GExtOrTruncOp : public GCastOp {
public:
  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GExtOrTruncOp.
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_SEXT:
    case TargetOpcode::G_ZEXT:
    case TargetOpcode::G_ANYEXT:
    case TargetOpcode::G_TRUNC:
      return true;
    default:
      return false;
    }
  };
};

/// Represents a splat vector.
class GSplatVector : public GenericMachineInstr {
public:
  /// Get the scalar value register being splat.
  /// \return The scalar value register being splat.
  Register getScalarReg() const { return getOperand(1).getReg(); }

  /// Methods for support type inquiry through isa, cast, and dyn_cast.
  /// \param MI - Machine instruction to test.
  /// \return True if \p MI is a GSplatVector.
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SPLAT_VECTOR;
  };
};

} // namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_GENERICMACHINEINSTRS_H
