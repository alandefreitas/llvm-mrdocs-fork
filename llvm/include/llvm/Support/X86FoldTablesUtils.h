//===-- X86FoldTablesUtils.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_X86FOLDTABLESUTILS_H
#define LLVM_SUPPORT_X86FOLDTABLESUTILS_H

namespace llvm {
/// Bitfield flags describing X86 fold-table entries for register/memory folding.
enum {
  /// Unfold memory operand at index 0.
  TB_INDEX_0 = 0,
  /// Unfold memory operand at index 1.
  TB_INDEX_1 = 1,
  /// Unfold memory operand at index 2.
  TB_INDEX_2 = 2,
  /// Unfold memory operand at index 3.
  TB_INDEX_3 = 3,
  /// Unfold memory operand at index 4.
  TB_INDEX_4 = 4,
  /// Mask for the memory operand index field (bits 0 - 2).
  TB_INDEX_MASK = 0x7,

  /// Do not insert the reverse map (MemOp -> RegOp) into the table.
  ///
  /// This may be needed because there is a many -> one mapping.
  TB_NO_REVERSE = 1 << 3,

  /// Do not insert the forward map (RegOp -> MemOp) into the table.
  ///
  /// This is needed for Native Client, which prohibits branch
  /// instructions from using a memory operand.
  TB_NO_FORWARD = 1 << 4,

  /// Entry folds a load from memory into the register form.
  TB_FOLDED_LOAD = 1 << 5,
  /// Entry folds a store to memory into the register form.
  TB_FOLDED_STORE = 1 << 6,

  /// Bit shift for the minimum alignment field (stored in bits 7 - 9).
  ///
  /// Used for RegOp->MemOp conversion. Encoded as Log2(Align).
  TB_ALIGN_SHIFT = 7,
  /// Require 1-byte alignment for the folded memory operand.
  TB_ALIGN_1 = 0 << TB_ALIGN_SHIFT,
  /// Require 16-byte alignment for the folded memory operand.
  TB_ALIGN_16 = 4 << TB_ALIGN_SHIFT,
  /// Require 32-byte alignment for the folded memory operand.
  TB_ALIGN_32 = 5 << TB_ALIGN_SHIFT,
  /// Require 64-byte alignment for the folded memory operand.
  TB_ALIGN_64 = 6 << TB_ALIGN_SHIFT,
  /// Mask for the minimum alignment field (bits 7 - 9).
  TB_ALIGN_MASK = 0x7 << TB_ALIGN_SHIFT,

  /// Bit shift for the broadcast type field (stored in bits 10 - 12).
  TB_BCAST_TYPE_SHIFT = TB_ALIGN_SHIFT + 3,
  /// Broadcast a 16-bit (word) element.
  TB_BCAST_W = 1 << TB_BCAST_TYPE_SHIFT,
  /// Broadcast a 32-bit (dword) element.
  TB_BCAST_D = 2 << TB_BCAST_TYPE_SHIFT,
  /// Broadcast a 64-bit (qword) element.
  TB_BCAST_Q = 3 << TB_BCAST_TYPE_SHIFT,
  /// Broadcast a scalar single-precision float.
  TB_BCAST_SS = 4 << TB_BCAST_TYPE_SHIFT,
  /// Broadcast a scalar double-precision float.
  TB_BCAST_SD = 5 << TB_BCAST_TYPE_SHIFT,
  /// Broadcast a scalar half-precision float.
  TB_BCAST_SH = 6 << TB_BCAST_TYPE_SHIFT,
  /// Mask for the broadcast type field (bits 10 - 12).
  TB_BCAST_MASK = 0x7 << TB_BCAST_TYPE_SHIFT,

  // Unused bits 14-16
};
} // namespace llvm
#endif // LLVM_SUPPORT_X86FOLDTABLESUTILS_H
