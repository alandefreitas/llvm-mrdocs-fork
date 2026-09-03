//===-- CostTable.h - Instruction Cost Table handling -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Cost tables and simple lookup functions
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_COSTTABLE_H_
#define LLVM_CODEGEN_COSTTABLE_H_

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include <cstdint>

namespace llvm {

/// Cost Table Entry
template <typename CostType>
struct CostTblEntryT {
  /// SelectionDAG opcode that this cost applies to.
  uint16_t ISD;
  /// Machine value type of the operation.
  MVT::SimpleValueType Type;
  /// Relative cost of the operation for \c Type.
  CostType Cost;
};
/// Cost table entry whose cost is a 16-bit integer.
using CostTblEntry = CostTblEntryT<uint16_t>;

/// Find in cost table.
///
/// \param Tbl Cost table to search.
/// \param ISD SelectionDAG opcode to match.
/// \param Ty Machine value type to match.
/// \returns The matching entry, or nullptr if none is found.
template <class CostType>
inline const CostTblEntryT<CostType> *
CostTableLookup(ArrayRef<CostTblEntryT<CostType>> Tbl, int ISD, MVT Ty) {
  auto I = find_if(Tbl, [=](const CostTblEntryT<CostType> &Entry) {
    return ISD == Entry.ISD && Ty == Entry.Type;
  });
  if (I != Tbl.end())
    return I;

  // Could not find an entry.
  return nullptr;
}

/// Find in a C-array cost table.
///
/// Wrapper to fix template argument deduction failures.
///
/// \param Table C-array of cost-table entries.
/// \param ISD SelectionDAG opcode to match.
/// \param Ty Machine value type to match.
/// \returns The matching entry, or nullptr if none is found.
template <size_t N, class CostType>
inline const CostTblEntryT<CostType> *
CostTableLookup(const CostTblEntryT<CostType> (&Table)[N], int ISD, MVT Ty) {
  return CostTableLookup<CostType>(Table, ISD, Ty);
}

/// Type Conversion Cost Table
template <typename CostType>
struct TypeConversionCostTblEntryT {
  /// SelectionDAG conversion opcode that this cost applies to.
  uint16_t ISD;
  /// Destination machine value type of the conversion.
  MVT::SimpleValueType Dst;
  /// Source machine value type of the conversion.
  MVT::SimpleValueType Src;
  /// Relative cost of converting from \c Src to \c Dst.
  CostType Cost;
};
/// Type-conversion cost table entry whose cost is a 16-bit integer.
using TypeConversionCostTblEntry = TypeConversionCostTblEntryT<uint16_t>;

/// Find in type conversion cost table.
///
/// \param Tbl Type-conversion cost table to search.
/// \param ISD SelectionDAG conversion opcode to match.
/// \param Dst Destination machine value type to match.
/// \param Src Source machine value type to match.
/// \returns The matching entry, or nullptr if none is found.
template <class CostType>
inline const TypeConversionCostTblEntryT<CostType> *
ConvertCostTableLookup(ArrayRef<TypeConversionCostTblEntryT<CostType>> Tbl,
                       int ISD, MVT Dst, MVT Src) {
  auto I =
      find_if(Tbl, [=](const TypeConversionCostTblEntryT<CostType> &Entry) {
        return ISD == Entry.ISD && Src == Entry.Src && Dst == Entry.Dst;
      });
  if (I != Tbl.end())
    return I;

  // Could not find an entry.
  return nullptr;
}

/// Find in a C-array type conversion cost table.
///
/// Wrapper to fix template argument deduction failures.
///
/// \param Table C-array of type-conversion cost-table entries.
/// \param ISD SelectionDAG conversion opcode to match.
/// \param Dst Destination machine value type to match.
/// \param Src Source machine value type to match.
/// \returns The matching entry, or nullptr if none is found.
template <size_t N, class CostType>
inline const TypeConversionCostTblEntryT<CostType> *
ConvertCostTableLookup(const TypeConversionCostTblEntryT<CostType> (&Table)[N],
                       int ISD, MVT Dst, MVT Src) {
  return ConvertCostTableLookup<CostType>(Table, ISD, Dst, Src);
}

} // namespace llvm

#endif /* LLVM_CODEGEN_COSTTABLE_H_ */
