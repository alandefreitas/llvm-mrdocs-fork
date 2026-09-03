//===--- ARMEHABI.h - ARM Exception Handling ABI ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the constants for the ARM unwind opcodes and exception
// handling table entry kinds.
//
// The enumerations and constants in this file reflect the ARM EHABI
// Specification as published by ARM.
//
// Exception Handling ABI for the ARM Architecture r2.09 - November 30, 2012
//
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0038a/IHI0038A_ehabi.pdf
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ARMEHABI_H
#define LLVM_SUPPORT_ARMEHABI_H

namespace llvm {
/// ARM architecture support utilities and constants.
namespace ARM {
/// ARM Exception Handling ABI (EHABI) constants and enumerations.
namespace EHABI {
  /// ARM exception handling table entry kinds
  enum EHTEntryKind {
    EHT_GENERIC = 0x00, ///< Generic (non-compact) exception-handling table entry.
    EHT_COMPACT = 0x80  ///< Compact exception-handling table entry.
  };

  /// Index table sentinel values used by the ARM EHABI.
  enum {
    /// Special entry for the function never unwind
    EXIDX_CANTUNWIND = 0x1
  };

  /// ARM-defined frame unwinding opcodes
  enum UnwindOpcodes {
    /// Increment VSP by ((x << 2) + 4).
    ///
    /// Format: 00xxxxxx
    UNWIND_OPCODE_INC_VSP = 0x00,

    /// Decrement VSP by ((x << 2) + 4).
    ///
    /// Format: 01xxxxxx
    UNWIND_OPCODE_DEC_VSP = 0x40,

    /// Refuse to unwind.
    ///
    /// Format: 10000000 00000000
    UNWIND_OPCODE_REFUSE = 0x8000,

    /// Pop registers r[15:12] and r[11:4] from the mask.
    ///
    /// Format: 1000xxxx xxxxxxxx
    /// Constraint: x != 0
    UNWIND_OPCODE_POP_REG_MASK_R4 = 0x8000,

    /// Set VSP to register r[x].
    ///
    /// Format: 1001xxxx
    /// Constraint: x != 13 && x != 15
    UNWIND_OPCODE_SET_VSP = 0x90,

    /// Pop consecutive registers r[(4+x):4].
    ///
    /// Format: 10100xxx
    UNWIND_OPCODE_POP_REG_RANGE_R4 = 0xa0,

    /// Pop r14 and consecutive registers r[(4+x):4].
    ///
    /// Format: 10101xxx
    UNWIND_OPCODE_POP_REG_RANGE_R4_R14 = 0xa8,

    /// Finish the unwind opcode sequence.
    ///
    /// Format: 10110000
    UNWIND_OPCODE_FINISH = 0xb0,

    /// Pop the Return Address Authentication Code.
    ///
    /// Format: 10110100
    UNWIND_OPCODE_POP_RA_AUTH_CODE = 0xb4,

    /// Pop registers r[3:0] from the mask.
    ///
    /// Format: 10110001 0000xxxx
    /// Constraint: x != 0
    UNWIND_OPCODE_POP_REG_MASK = 0xb100,

    /// Increment VSP by ((x << 2) + 0x204) with a ULEB128 operand.
    ///
    /// Format: 10110010 x(uleb128)
    UNWIND_OPCODE_INC_VSP_ULEB128 = 0xb2,

    /// Pop VFP registers d[(x+y):x] with FSTMFDX encoding.
    ///
    /// Format: 10110011 xxxxyyyy
    UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDX = 0xb300,

    /// Pop VFP registers d[(8+x):8] with FSTMFDX encoding.
    ///
    /// Format: 10111xxx
    UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDX_D8 = 0xb8,

    /// Pop Wireless MMX registers wR[(10+x):10].
    ///
    /// Format: 11000xxx
    UNWIND_OPCODE_POP_WIRELESS_MMX_REG_RANGE_WR10 = 0xc0,

    /// Pop Wireless MMX registers wR[(x+y):x].
    ///
    /// Format: 11000110 xxxxyyyy
    UNWIND_OPCODE_POP_WIRELESS_MMX_REG_RANGE = 0xc600,

    /// Pop Wireless MMX control registers wCGR[3:0] from the mask.
    ///
    /// Format: 11000111 0000xxxx
    /// Constraint: x != 0
    UNWIND_OPCODE_POP_WIRELESS_MMX_REG_MASK = 0xc700,

    /// Pop VFP registers d[(16+x+y):(16+x)] with FSTMFDD encoding.
    ///
    /// Format: 11001000 xxxxyyyy
    UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDD_D16 = 0xc800,

    /// Pop VFP registers d[(x+y):x] with FSTMFDD encoding.
    ///
    /// Format: 11001001 xxxxyyyy
    UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDD = 0xc900,

    /// Pop VFP registers d[(8+x):8] with FSTMFDD encoding.
    ///
    /// Format: 11010xxx
    UNWIND_OPCODE_POP_VFP_REG_RANGE_FSTMFDD_D8 = 0xd0
  };

  /// ARM-defined Personality Routine Index
  ///
  /// To make the exception handling table become more compact, ARM defined
  /// several personality routines in EHABI.  There are 3 different
  /// personality routines in ARM EHABI currently.  It is possible to have 16
  /// pre-defined personality routines at most.
  enum PersonalityRoutineIndex {
    AEABI_UNWIND_CPP_PR0 = 0, ///< Compact personality routine 0 (__aeabi_unwind_cpp_pr0).
    AEABI_UNWIND_CPP_PR1 = 1, ///< Compact personality routine 1 (__aeabi_unwind_cpp_pr1).
    AEABI_UNWIND_CPP_PR2 = 2, ///< Compact personality routine 2 (__aeabi_unwind_cpp_pr2).

    NUM_PERSONALITY_INDEX ///< Number of predefined personality routine indices.
  };
}
}
}

#endif
