//===- SelectionDAGAddressAnalysis.h - DAG Address Analysis -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAGADDRESSANALYSIS_H
#define LLVM_CODEGEN_SELECTIONDAGADDRESSANALYSIS_H

#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

class SelectionDAG;

/// Helper struct to parse and store a memory address as base + index + offset.
///
/// We ignore sign extensions when it is safe to do so.
/// The following two expressions are not equivalent. To differentiate we need
/// to store whether there was a sign extension involved in the index
/// computation.
///  (load (i64 add (i64 copyfromreg %c)
///                 (i64 signextend (add (i8 load %index)
///                                      (i8 1))))
/// vs
///
/// (load (i64 add (i64 copyfromreg %c)
///                (i64 signextend (i32 add (i32 signextend (i8 load %index))
///                                         (i32 1)))))
class BaseIndexOffset {
private:
  SDValue Base;
  SDValue Index;
  std::optional<int64_t> Offset;
  bool IsIndexSignExt = false;

public:
  /// Construct an empty address with no base, index, or offset.
  BaseIndexOffset() = default;
  /// Construct an address from a base, index, and index sign-extension flag.
  ///
  /// \param Base Base pointer of the address.
  /// \param Index Index value of the address.
  /// \param IsIndexSignExt Whether the index was sign-extended.
  BaseIndexOffset(SDValue Base, SDValue Index, bool IsIndexSignExt)
      : Base(Base), Index(Index), IsIndexSignExt(IsIndexSignExt) {}
  /// Construct an address from a base, index, offset, and sign-extension flag.
  ///
  /// \param Base Base pointer of the address.
  /// \param Index Index value of the address.
  /// \param Offset Constant byte offset from the base and index.
  /// \param IsIndexSignExt Whether the index was sign-extended.
  BaseIndexOffset(SDValue Base, SDValue Index, int64_t Offset,
                  bool IsIndexSignExt)
      : Base(Base), Index(Index), Offset(Offset),
        IsIndexSignExt(IsIndexSignExt) {}

  /// Return the base pointer of the address.
  ///
  /// \return The base pointer of the address.
  SDValue getBase() { return Base; }
  /// Return the base pointer of the address.
  ///
  /// \return The base pointer of the address.
  SDValue getBase() const { return Base; }
  /// Return the index value of the address.
  ///
  /// \return The index value of the address.
  SDValue getIndex() { return Index; }
  /// Return the index value of the address.
  ///
  /// \return The index value of the address.
  SDValue getIndex() const { return Index; }
  /// Add \p VectorOff to the stored constant offset.
  ///
  /// \param VectorOff Byte offset to add; if no offset was set, treats it as
  ///        zero first.
  void addToOffset(int64_t VectorOff) {
    Offset = Offset.value_or(0) + VectorOff;
  }
  /// Return true if this address has a known constant offset.
  ///
  /// \return True if a constant offset is known.
  bool hasValidOffset() const { return Offset.has_value(); }
  /// Return the constant offset from the base and index.
  ///
  /// \return The constant byte offset.
  int64_t getOffset() const { return *Offset; }

  /// Return true if \p Other and `*this` are offsets from the same base.
  ///
  /// In that case, \p Off is set to the offset between `*this` and \p Other
  /// (negative if \p Other is before `*this`).
  ///
  /// \param Other Address to compare against.
  /// \param DAG SelectionDAG used for pointer analysis.
  /// \param Off Set to the byte offset from `*this` to \p Other on success.
  /// \return True if both addresses share the same base and index.
  LLVM_ABI bool equalBaseIndex(const BaseIndexOffset &Other,
                               const SelectionDAG &DAG, int64_t &Off) const;

  /// Return true if \p Other and `*this` are offsets from the same base.
  ///
  /// \param Other Address to compare against.
  /// \param DAG SelectionDAG used for pointer analysis.
  /// \return True if both addresses share the same base and index.
  bool equalBaseIndex(const BaseIndexOffset &Other,
                      const SelectionDAG &DAG) const {
    int64_t Off;
    return equalBaseIndex(Other, DAG, Off);
  }

  /// Return true if \p Other is fully contained in `*this`.
  ///
  /// \param DAG SelectionDAG used for pointer analysis.
  /// \param BitSize Size in bits of the memory region for `*this`.
  /// \param Other Address of the candidate contained region.
  /// \param OtherBitSize Size in bits of the memory region for \p Other.
  /// \param BitOffset Set to the bit offset of \p Other within `*this` on
  ///        success.
  /// \return True if \p Other is fully contained in `*this`.
  LLVM_ABI bool contains(const SelectionDAG &DAG, int64_t BitSize,
                         const BaseIndexOffset &Other, int64_t OtherBitSize,
                         int64_t &BitOffset) const;

  /// Return true if \p Other is fully contained in `*this`.
  ///
  /// \param DAG SelectionDAG used for pointer analysis.
  /// \param BitSize Size in bits of the memory region for `*this`.
  /// \param Other Address of the candidate contained region.
  /// \param OtherBitSize Size in bits of the memory region for \p Other.
  /// \return True if \p Other is fully contained in `*this`.
  bool contains(const SelectionDAG &DAG, int64_t BitSize,
                const BaseIndexOffset &Other, int64_t OtherBitSize) const {
    int64_t BitOffset;
    return contains(DAG, BitSize, Other, OtherBitSize, BitOffset);
  }

  /// Return true if aliasing between \p Op0 and \p Op1 can be proven.
  ///
  /// When proven, \p IsAlias is set to true if they alias and false if they do
  /// not.
  ///
  /// \param Op0 First memory operation.
  /// \param NumBytes0 Access size of \p Op0.
  /// \param Op1 Second memory operation.
  /// \param NumBytes1 Access size of \p Op1.
  /// \param DAG SelectionDAG used for pointer analysis.
  /// \param IsAlias Set to whether the operations alias when the result is
  ///        true.
  /// \return True if aliasing between the operations was proven.
  LLVM_ABI static bool computeAliasing(const SDNode *Op0,
                                       const LocationSize NumBytes0,
                                       const SDNode *Op1,
                                       const LocationSize NumBytes1,
                                       const SelectionDAG &DAG, bool &IsAlias);

  /// Parses tree in N for base, index, offset addresses.
  ///
  /// \param N Memory-addressing node to parse.
  /// \param DAG SelectionDAG that owns \p N.
  /// \return The parsed base, index, and offset for \p N.
  LLVM_ABI static BaseIndexOffset match(const SDNode *N,
                                        const SelectionDAG &DAG);

  /// Print this address to \p OS.
  ///
  /// \param OS Stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;
  /// Dump this address to the debug stream.
  LLVM_ABI void dump() const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_SELECTIONDAGADDRESSANALYSIS_H
