//===- CodeGen/MachineConstantPool.h - Abstract Constant Pool ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// This file declares the MachineConstantPool class which is an abstract
/// constant pool to keep track of constants referenced by a function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINECONSTANTPOOL_H
#define LLVM_CODEGEN_MACHINECONSTANTPOOL_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Alignment.h"
#include <climits>
#include <vector>

namespace llvm {

class Constant;
class DataLayout;
class FoldingSetNodeID;
class MachineConstantPool;
class raw_ostream;
class Type;

/// Abstract base class for all machine specific constantpool value subclasses.
///
class LLVM_ABI MachineConstantPoolValue {
  virtual void anchor();

  Type *Ty;

public:
  /// Construct a machine constant pool value of the given IR type.
  ///
  /// \param ty IR type of the constant.
  explicit MachineConstantPoolValue(Type *ty) : Ty(ty) {}
  /// Destroy the machine constant pool value.
  virtual ~MachineConstantPoolValue() = default;

  /// Return the IR type of this constant pool value.
  ///
  /// \return The IR type of this constant pool value.
  Type *getType() const { return Ty; }

  /// Return the size in bytes of this constant pool value.
  ///
  /// \param DL Data layout used to compute the size.
  /// \return Size in bytes of this constant pool value.
  virtual unsigned getSizeInBytes(const DataLayout &DL) const;

  /// Return the index of an existing matching entry, or -1 if none exists.
  ///
  /// \param CP Constant pool to search.
  /// \param Alignment Minimum required alignment.
  /// \return Index of a matching entry, or -1 if none exists.
  virtual int getExistingMachineCPValue(MachineConstantPool *CP,
                                        Align Alignment) = 0;

  /// Add this value to the CSE identifier for SelectionDAG.
  ///
  /// \param ID Folding set node ID to update.
  virtual void addSelectionDAGCSEId(FoldingSetNodeID &ID) = 0;

  /// Print this constant pool value.
  ///
  /// Implement operator<<.
  ///
  /// \param O Stream to print to.
  virtual void print(raw_ostream &O) const = 0;
};

/// Write a machine constant pool value to a stream.
///
/// \param OS Stream to write to.
/// \param V Value to print.
/// \return Reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS,
                               const MachineConstantPoolValue &V) {
  V.print(OS);
  return OS;
}

/// One entry in a MachineConstantPool.
///
/// This class is a data container for one entry in a MachineConstantPool.
/// It contains a pointer to the value and an offset from the start of
/// the constant pool.
class MachineConstantPoolEntry {
public:
  /// The constant itself.
  union {
    /// IR constant stored in this pool entry.
    const Constant *ConstVal;
    /// Target-specific constant pool value stored in this entry.
    MachineConstantPoolValue *MachineCPVal;
  } Val;

  /// The required alignment for this entry.
  Align Alignment;

  /// True if Val holds a MachineConstantPoolValue rather than a Constant.
  bool IsMachineConstantPoolEntry;

  /// Construct an entry wrapping an IR constant.
  ///
  /// \param V Constant to store.
  /// \param A Required alignment for the entry.
  MachineConstantPoolEntry(const Constant *V, Align A)
      : Alignment(A), IsMachineConstantPoolEntry(false) {
    Val.ConstVal = V;
  }

  /// Construct an entry wrapping a target-specific constant pool value.
  ///
  /// \param V Target-specific value to store.
  /// \param A Required alignment for the entry.
  MachineConstantPoolEntry(MachineConstantPoolValue *V, Align A)
      : Alignment(A), IsMachineConstantPoolEntry(true) {
    Val.MachineCPVal = V;
  }

  /// isMachineConstantPoolEntry - Return true if the MachineConstantPoolEntry
  /// is indeed a target specific constantpool entry, not a wrapper over a
  /// Constant.
  ///
  /// \return True if this entry holds a MachineConstantPoolValue.
  bool isMachineConstantPoolEntry() const { return IsMachineConstantPoolEntry; }

  /// Return the required alignment for this entry.
  ///
  /// \return Required alignment for this entry.
  Align getAlign() const { return Alignment; }

  /// Return the size in bytes of this entry's value.
  ///
  /// \param DL Data layout used to compute the size.
  /// \return Size in bytes of this entry's value.
  LLVM_ABI unsigned getSizeInBytes(const DataLayout &DL) const;

  /// Return true if this entry may generate a relocation.
  ///
  /// This method classifies the entry according to whether or not it may
  /// generate a relocation entry.  This must be conservative, so if it might
  /// codegen to a relocatable entry, it should say so.
  ///
  /// \return True if this entry may generate a relocation.
  LLVM_ABI bool needsRelocation() const;

  /// Return the section kind for this constant pool entry.
  ///
  /// \param DL Data layout used to classify the section.
  /// \return Section kind for this constant pool entry.
  LLVM_ABI SectionKind getSectionKind(const DataLayout *DL) const;
};

/// Tracks constants that must be spilled to a function's constant pool.
///
/// The MachineConstantPool class keeps track of constants referenced by a
/// function which must be spilled to memory.  This is used for constants which
/// are unable to be used directly as operands to instructions, which typically
/// include floating point and large integer constants.
///
/// Instructions reference the address of these constant pool constants through
/// the use of MO_ConstantPoolIndex values.  When emitting assembly or machine
/// code, these virtual address references are converted to refer to the
/// address of the function constant pool values.
class MachineConstantPool {
  Align PoolAlignment; ///< The alignment for the pool.
  std::vector<MachineConstantPoolEntry> Constants; ///< The pool of constants.
  /// MachineConstantPoolValues that use an existing MachineConstantPoolEntry.
  DenseSet<MachineConstantPoolValue*> MachineCPVsSharingEntries;
  const DataLayout &DL;

  const DataLayout &getDataLayout() const { return DL; }

public:
  /// Construct an empty constant pool for the given data layout.
  ///
  /// \param DL Data layout used for sizing and aligning pool entries.
  explicit MachineConstantPool(const DataLayout &DL)
      : PoolAlignment(1), DL(DL) {}
  /// Destroy the constant pool and any owned MachineConstantPoolValues.
  LLVM_ABI ~MachineConstantPool();

  /// Return the alignment required by the whole constant pool, of which the
  /// first element must be aligned.
  ///
  /// \return Alignment required by the whole constant pool.
  Align getConstantPoolAlign() const { return PoolAlignment; }

  /// Create a new entry in the constant pool or return an existing one.
  ///
  /// User must specify the minimum required alignment for the object.
  ///
  /// \param C Constant to add or look up.
  /// \param Alignment Minimum required alignment for the object.
  /// \return Index of the constant pool entry for \p C.
  LLVM_ABI unsigned getConstantPoolIndex(const Constant *C, Align Alignment);
  /// Create a new entry for a target-specific value or return an existing one.
  ///
  /// \param V Target-specific constant pool value to add or look up.
  /// \param Alignment Minimum required alignment for the object.
  /// \return Index of the constant pool entry for \p V.
  LLVM_ABI unsigned getConstantPoolIndex(MachineConstantPoolValue *V,
                                         Align Alignment);

  /// isEmpty - Return true if this constant pool contains no constants.
  ///
  /// \return True if this constant pool contains no constants.
  bool isEmpty() const { return Constants.empty(); }

  /// Return the entries currently stored in this constant pool.
  ///
  /// \return The entries currently stored in this constant pool.
  const std::vector<MachineConstantPoolEntry> &getConstants() const {
    return Constants;
  }

  /// Print information about constant pool objects.
  ///
  /// Used by the MachineFunction printer. Implemented in MachineFunction.cpp.
  ///
  /// \param OS Stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// dump - Call print(cerr) to be called from the debugger.
  LLVM_ABI void dump() const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINECONSTANTPOOL_H
