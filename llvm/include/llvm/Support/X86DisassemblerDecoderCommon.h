//===-- X86DisassemblerDecoderCommon.h - Disassembler decoder ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the X86 Disassembler.
// It contains common definitions used by both the disassembler and the table
//  generator.
// Documentation for the disassembler can be found in X86Disassembler.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_X86DISASSEMBLERDECODERCOMMON_H
#define LLVM_SUPPORT_X86DISASSEMBLERDECODERCOMMON_H

#include "llvm/Support/DataTypes.h"

namespace llvm {
/// X86 instruction disassembler decoder tables, contexts, and shared helpers.
namespace X86Disassembler {

#define INSTRUCTIONS_SYM x86DisassemblerInstrSpecifiers
#define CONTEXTS_SYM x86DisassemblerContexts
#define ONEBYTE_SYM x86DisassemblerOneByteOpcodes
#define TWOBYTE_SYM x86DisassemblerTwoByteOpcodes
#define THREEBYTE38_SYM x86DisassemblerThreeByte38Opcodes
#define THREEBYTE3A_SYM x86DisassemblerThreeByte3AOpcodes
#define SPARSE_OPCODE_DECISIONS_SYM x86DisassemblerSparseOpcodeDecisions
#define SPARSE_OPCODE_DECISION_INDICES_SYM                                     \
  x86DisassemblerSparseOpcodeDecisionIndices

#define INSTRUCTIONS_STR "x86DisassemblerInstrSpecifiers"
#define CONTEXTS_STR "x86DisassemblerContexts"
#define ONEBYTE_STR "x86DisassemblerOneByteOpcodes"
#define TWOBYTE_STR "x86DisassemblerTwoByteOpcodes"
#define THREEBYTE38_STR "x86DisassemblerThreeByte38Opcodes"
#define THREEBYTE3A_STR "x86DisassemblerThreeByte3AOpcodes"
#define SPARSE_OPCODE_DECISIONS_STR "x86DisassemblerSparseOpcodeDecisions"
#define SPARSE_OPCODE_DECISION_INDICES_STR                                     \
  "x86DisassemblerSparseOpcodeDecisionIndices"

/// Attributes of an instruction known before the opcode can be decoded.
///
/// Most of these indicate the presence of particular prefixes, but ATTR_64BIT
/// is simply an attribute of the decoding context.
enum attributeBits {
  ATTR_NONE = 0x00,       ///< No special attributes.
  ATTR_64BIT = 0x1 << 0,  ///< Decode in 64-bit mode.
  ATTR_XS = 0x1 << 1,     ///< XS (0xF3) prefix present.
  ATTR_XD = 0x1 << 2,     ///< XD (0xF2) prefix present.
  ATTR_REXW = 0x1 << 3,   ///< REX.W prefix present.
  ATTR_OPSIZE = 0x1 << 4, ///< Operand-size override (0x66).
  ATTR_ADSIZE = 0x1 << 5, ///< Address-size override (0x67).
  ATTR_VEX = 0x1 << 6,    ///< VEX prefix present.
  ATTR_VEXL = 0x1 << 7,   ///< VEX.L (vector length) set.
  ATTR_EVEX = 0x1 << 8,   ///< EVEX prefix present.
  ATTR_EVEXL2 = 0x1 << 9, ///< EVEX.L2 set.
  ATTR_EVEXK = 0x1 << 10,  ///< EVEX write-mask (aaa) present.
  ATTR_EVEXKZ = 0x1 << 11, ///< EVEX zeroing/merging (z) set.
  ATTR_EVEXB = 0x1 << 12,  ///< EVEX broadcast/RC/SAE (b) set.
  ATTR_REX2 = 0x1 << 13,   ///< REX2 prefix present.
  ATTR_EVEXNF = 0x1 << 14, ///< EVEX NF (no flags) set.
  ATTR_EVEXU = 0x1 << 15,  ///< EVEX U bit set.
  ATTR_max = 0x1 << 16,    ///< Sentinel past the last attribute bit.
};

// Combinations of the above attributes that are relevant to instruction
// decode. Although other combinations are possible, they can be reduced to
// these without affecting the ultimately decoded instruction.

//           Class name           Rank  Rationale for rank assignment
#define INSTRUCTION_CONTEXTS                                                   \
  ENUM_ENTRY(IC, 0, "says nothing about the instruction")                      \
  ENUM_ENTRY(IC_64BIT, 1,                                                      \
             "says the instruction applies in 64-bit mode but no more")        \
  ENUM_ENTRY(IC_OPSIZE, 3,                                                     \
             "requires an OPSIZE prefix, so operands change width")            \
  ENUM_ENTRY(IC_ADSIZE, 3,                                                     \
             "requires an ADSIZE prefix, so operands change width")            \
  ENUM_ENTRY(IC_OPSIZE_ADSIZE, 4, "requires ADSIZE and OPSIZE prefixes")       \
  ENUM_ENTRY(IC_XD, 2,                                                         \
             "may say something about the opcode but not the operands")        \
  ENUM_ENTRY(IC_XS, 2,                                                         \
             "may say something about the opcode but not the operands")        \
  ENUM_ENTRY(IC_XD_OPSIZE, 3,                                                  \
             "requires an OPSIZE prefix, so operands change width")            \
  ENUM_ENTRY(IC_XS_OPSIZE, 3,                                                  \
             "requires an OPSIZE prefix, so operands change width")            \
  ENUM_ENTRY(IC_XD_ADSIZE, 3,                                                  \
             "requires an ADSIZE prefix, so operands change width")            \
  ENUM_ENTRY(IC_XS_ADSIZE, 3,                                                  \
             "requires an ADSIZE prefix, so operands change width")            \
  ENUM_ENTRY(IC_64BIT_REXW, 5,                                                 \
             "requires a REX.W prefix, so operands change width; overrides "   \
             "IC_OPSIZE")                                                      \
  ENUM_ENTRY(IC_64BIT_REXW_ADSIZE, 6,                                          \
             "requires a REX.W prefix and 0x67 prefix")                        \
  ENUM_ENTRY(IC_64BIT_OPSIZE, 3, "Just as meaningful as IC_OPSIZE")            \
  ENUM_ENTRY(IC_64BIT_ADSIZE, 3, "Just as meaningful as IC_ADSIZE")            \
  ENUM_ENTRY(IC_64BIT_OPSIZE_ADSIZE, 4,                                        \
             "Just as meaningful as IC_OPSIZE/IC_ADSIZE")                      \
  ENUM_ENTRY(IC_64BIT_XD, 6, "XD instructions are SSE; REX.W is secondary")    \
  ENUM_ENTRY(IC_64BIT_XS, 6, "Just as meaningful as IC_64BIT_XD")              \
  ENUM_ENTRY(IC_64BIT_XD_OPSIZE, 3, "Just as meaningful as IC_XD_OPSIZE")      \
  ENUM_ENTRY(IC_64BIT_XS_OPSIZE, 3, "Just as meaningful as IC_XS_OPSIZE")      \
  ENUM_ENTRY(IC_64BIT_XD_ADSIZE, 3, "Just as meaningful as IC_XD_ADSIZE")      \
  ENUM_ENTRY(IC_64BIT_XS_ADSIZE, 3, "Just as meaningful as IC_XS_ADSIZE")      \
  ENUM_ENTRY(IC_64BIT_REXW_XS, 7, "OPSIZE could mean a different opcode")      \
  ENUM_ENTRY(IC_64BIT_REXW_XD, 7, "Just as meaningful as IC_64BIT_REXW_XS")    \
  ENUM_ENTRY(IC_64BIT_REXW_OPSIZE, 8,                                          \
             "The Dynamic Duo!  Prefer over all else because this changes "    \
             "most operands' meaning")                                         \
  ENUM_ENTRY(IC_64BIT_REX2, 2, "requires a REX2 prefix")                       \
  ENUM_ENTRY(IC_64BIT_REX2_REXW, 3, "requires a REX2 and the W prefix")        \
  ENUM_ENTRY(IC_VEX, 1, "requires a VEX prefix")                               \
  ENUM_ENTRY(IC_VEX_XS, 2, "requires VEX and the XS prefix")                   \
  ENUM_ENTRY(IC_VEX_XD, 2, "requires VEX and the XD prefix")                   \
  ENUM_ENTRY(IC_VEX_OPSIZE, 2, "requires VEX and the OpSize prefix")           \
  ENUM_ENTRY(IC_VEX_W, 3, "requires VEX and the W prefix")                     \
  ENUM_ENTRY(IC_VEX_W_XS, 4, "requires VEX, W, and XS prefix")                 \
  ENUM_ENTRY(IC_VEX_W_XD, 4, "requires VEX, W, and XD prefix")                 \
  ENUM_ENTRY(IC_VEX_W_OPSIZE, 4, "requires VEX, W, and OpSize")                \
  ENUM_ENTRY(IC_VEX_L, 3, "requires VEX and the L prefix")                     \
  ENUM_ENTRY(IC_VEX_L_XS, 4, "requires VEX and the L and XS prefix")           \
  ENUM_ENTRY(IC_VEX_L_XD, 4, "requires VEX and the L and XD prefix")           \
  ENUM_ENTRY(IC_VEX_L_OPSIZE, 4, "requires VEX, L, and OpSize")                \
  ENUM_ENTRY(IC_VEX_L_W, 4, "requires VEX, L and W")                           \
  ENUM_ENTRY(IC_VEX_L_W_XS, 5, "requires VEX, L, W and XS prefix")             \
  ENUM_ENTRY(IC_VEX_L_W_XD, 5, "requires VEX, L, W and XD prefix")             \
  ENUM_ENTRY(IC_VEX_L_W_OPSIZE, 5, "requires VEX, L, W and OpSize")            \
  ENUM_ENTRY(IC_EVEX, 1, "requires an EVEX prefix")                            \
  ENUM_ENTRY(IC_EVEX_NF, 2, "requires EVEX and NF prefix")                     \
  ENUM_ENTRY(IC_EVEX_XS, 2, "requires EVEX and the XS prefix")                 \
  ENUM_ENTRY(IC_EVEX_XS_ADSIZE, 3, "requires EVEX, XS and the ADSIZE prefix")  \
  ENUM_ENTRY(IC_EVEX_XD, 2, "requires EVEX and the XD prefix")                 \
  ENUM_ENTRY(IC_EVEX_XD_ADSIZE, 3, "requires EVEX, XD and the ADSIZE prefix")  \
  ENUM_ENTRY(IC_EVEX_OPSIZE, 2, "requires EVEX and the OpSize prefix")         \
  ENUM_ENTRY(IC_EVEX_OPSIZE_NF, 3, "requires EVEX, NF and the OpSize prefix")  \
  ENUM_ENTRY(IC_EVEX_OPSIZE_ADSIZE, 3,                                         \
             "requires EVEX, OPSIZE and the ADSIZE prefix")                    \
  ENUM_ENTRY(IC_EVEX_W, 3, "requires EVEX and the W prefix")                   \
  ENUM_ENTRY(IC_EVEX_W_NF, 4, "requires EVEX, W and NF prefix")                \
  ENUM_ENTRY(IC_EVEX_W_XS, 4, "requires EVEX, W, and XS prefix")               \
  ENUM_ENTRY(IC_EVEX_W_XD, 4, "requires EVEX, W, and XD prefix")               \
  ENUM_ENTRY(IC_EVEX_W_OPSIZE, 4, "requires EVEX, W, and OpSize")              \
  ENUM_ENTRY(IC_EVEX_L, 3, "requires EVEX and the L prefix")                   \
  ENUM_ENTRY(IC_EVEX_L_XS, 4, "requires EVEX and the L and XS prefix")         \
  ENUM_ENTRY(IC_EVEX_L_XD, 4, "requires EVEX and the L and XD prefix")         \
  ENUM_ENTRY(IC_EVEX_L_OPSIZE, 4, "requires EVEX, L, and OpSize")              \
  ENUM_ENTRY(IC_EVEX_L_W, 3, "requires EVEX, L and W")                         \
  ENUM_ENTRY(IC_EVEX_L_W_XS, 4, "requires EVEX, L, W and XS prefix")           \
  ENUM_ENTRY(IC_EVEX_L_W_XD, 4, "requires EVEX, L, W and XD prefix")           \
  ENUM_ENTRY(IC_EVEX_L_W_OPSIZE, 4, "requires EVEX, L, W and OpSize")          \
  ENUM_ENTRY(IC_EVEX_L2, 3, "requires EVEX and the L2 prefix")                 \
  ENUM_ENTRY(IC_EVEX_L2_XS, 4, "requires EVEX and the L2 and XS prefix")       \
  ENUM_ENTRY(IC_EVEX_L2_XD, 4, "requires EVEX and the L2 and XD prefix")       \
  ENUM_ENTRY(IC_EVEX_L2_OPSIZE, 4, "requires EVEX, L2, and OpSize")            \
  ENUM_ENTRY(IC_EVEX_L2_W, 3, "requires EVEX, L2 and W")                       \
  ENUM_ENTRY(IC_EVEX_L2_W_XS, 4, "requires EVEX, L2, W and XS prefix")         \
  ENUM_ENTRY(IC_EVEX_L2_W_XD, 4, "requires EVEX, L2, W and XD prefix")         \
  ENUM_ENTRY(IC_EVEX_L2_W_OPSIZE, 4, "requires EVEX, L2, W and OpSize")        \
  ENUM_ENTRY(IC_EVEX_K, 1, "requires an EVEX_K prefix")                        \
  ENUM_ENTRY(IC_EVEX_XS_K, 2, "requires EVEX_K and the XS prefix")             \
  ENUM_ENTRY(IC_EVEX_XD_K, 2, "requires EVEX_K and the XD prefix")             \
  ENUM_ENTRY(IC_EVEX_OPSIZE_K, 2, "requires EVEX_K and the OpSize prefix")     \
  ENUM_ENTRY(IC_EVEX_W_K, 3, "requires EVEX_K and the W prefix")               \
  ENUM_ENTRY(IC_EVEX_W_XS_K, 4, "requires EVEX_K, W, and XS prefix")           \
  ENUM_ENTRY(IC_EVEX_W_XD_K, 4, "requires EVEX_K, W, and XD prefix")           \
  ENUM_ENTRY(IC_EVEX_W_OPSIZE_K, 4, "requires EVEX_K, W, and OpSize")          \
  ENUM_ENTRY(IC_EVEX_L_K, 3, "requires EVEX_K and the L prefix")               \
  ENUM_ENTRY(IC_EVEX_L_XS_K, 4, "requires EVEX_K and the L and XS prefix")     \
  ENUM_ENTRY(IC_EVEX_L_XD_K, 4, "requires EVEX_K and the L and XD prefix")     \
  ENUM_ENTRY(IC_EVEX_L_OPSIZE_K, 4, "requires EVEX_K, L, and OpSize")          \
  ENUM_ENTRY(IC_EVEX_L_W_K, 3, "requires EVEX_K, L and W")                     \
  ENUM_ENTRY(IC_EVEX_L_W_XS_K, 4, "requires EVEX_K, L, W and XS prefix")       \
  ENUM_ENTRY(IC_EVEX_L_W_XD_K, 4, "requires EVEX_K, L, W and XD prefix")       \
  ENUM_ENTRY(IC_EVEX_L_W_OPSIZE_K, 4, "requires EVEX_K, L, W and OpSize")      \
  ENUM_ENTRY(IC_EVEX_L2_K, 3, "requires EVEX_K and the L2 prefix")             \
  ENUM_ENTRY(IC_EVEX_L2_XS_K, 4, "requires EVEX_K and the L2 and XS prefix")   \
  ENUM_ENTRY(IC_EVEX_L2_XD_K, 4, "requires EVEX_K and the L2 and XD prefix")   \
  ENUM_ENTRY(IC_EVEX_L2_OPSIZE_K, 4, "requires EVEX_K, L2, and OpSize")        \
  ENUM_ENTRY(IC_EVEX_L2_W_K, 3, "requires EVEX_K, L2 and W")                   \
  ENUM_ENTRY(IC_EVEX_L2_W_XS_K, 4, "requires EVEX_K, L2, W and XS prefix")     \
  ENUM_ENTRY(IC_EVEX_L2_W_XD_K, 4, "requires EVEX_K, L2, W and XD prefix")     \
  ENUM_ENTRY(IC_EVEX_L2_W_OPSIZE_K, 4, "requires EVEX_K, L2, W and OpSize")    \
  ENUM_ENTRY(IC_EVEX_B, 1, "requires an EVEX_B prefix")                        \
  ENUM_ENTRY(IC_EVEX_B_NF, 2, "requires EVEX_NF and EVEX_B prefix")            \
  ENUM_ENTRY(IC_EVEX_XS_B, 2, "requires EVEX_B and the XS prefix")             \
  ENUM_ENTRY(IC_EVEX_XD_B, 2, "requires EVEX_B and the XD prefix")             \
  ENUM_ENTRY(IC_EVEX_OPSIZE_B, 2, "requires EVEX_B and the OpSize prefix")     \
  ENUM_ENTRY(IC_EVEX_OPSIZE_B_NF, 3, "requires EVEX_B, NF and Opsize prefix")  \
  ENUM_ENTRY(IC_EVEX_W_B, 3, "requires EVEX_B and the W prefix")               \
  ENUM_ENTRY(IC_EVEX_W_B_NF, 4, "requires EVEX_NF, EVEX_B and the W prefix")   \
  ENUM_ENTRY(IC_EVEX_W_XS_B, 4, "requires EVEX_B, W, and XS prefix")           \
  ENUM_ENTRY(IC_EVEX_W_XD_B, 4, "requires EVEX_B, W, and XD prefix")           \
  ENUM_ENTRY(IC_EVEX_W_OPSIZE_B, 4, "requires EVEX_B, W, and OpSize")          \
  ENUM_ENTRY(IC_EVEX_L_B, 3, "requires EVEX_B and the L prefix")               \
  ENUM_ENTRY(IC_EVEX_L_XS_B, 4, "requires EVEX_B and the L and XS prefix")     \
  ENUM_ENTRY(IC_EVEX_L_XD_B, 4, "requires EVEX_B and the L and XD prefix")     \
  ENUM_ENTRY(IC_EVEX_L_OPSIZE_B, 4, "requires EVEX_B, L, and OpSize")          \
  ENUM_ENTRY(IC_EVEX_L_W_B, 3, "requires EVEX_B, L and W")                     \
  ENUM_ENTRY(IC_EVEX_L_W_XS_B, 4, "requires EVEX_B, L, W and XS prefix")       \
  ENUM_ENTRY(IC_EVEX_L_W_XD_B, 4, "requires EVEX_B, L, W and XD prefix")       \
  ENUM_ENTRY(IC_EVEX_L_W_OPSIZE_B, 4, "requires EVEX_B, L, W and OpSize")      \
  ENUM_ENTRY(IC_EVEX_L2_B, 3, "requires EVEX_B and the L2 prefix")             \
  ENUM_ENTRY(IC_EVEX_L2_XS_B, 4, "requires EVEX_B and the L2 and XS prefix")   \
  ENUM_ENTRY(IC_EVEX_L2_XD_B, 4, "requires EVEX_B and the L2 and XD prefix")   \
  ENUM_ENTRY(IC_EVEX_L2_OPSIZE_B, 4, "requires EVEX_B, L2, and OpSize")        \
  ENUM_ENTRY(IC_EVEX_L2_W_B, 3, "requires EVEX_B, L2 and W")                   \
  ENUM_ENTRY(IC_EVEX_L2_W_XS_B, 4, "requires EVEX_B, L2, W and XS prefix")     \
  ENUM_ENTRY(IC_EVEX_L2_W_XD_B, 4, "requires EVEX_B, L2, W and XD prefix")     \
  ENUM_ENTRY(IC_EVEX_L2_W_OPSIZE_B, 4, "requires EVEX_B, L2, W and OpSize")    \
  ENUM_ENTRY(IC_EVEX_K_B, 1, "requires EVEX_B and EVEX_K prefix")              \
  ENUM_ENTRY(IC_EVEX_XS_K_B, 2, "requires EVEX_B, EVEX_K and the XS prefix")   \
  ENUM_ENTRY(IC_EVEX_XD_K_B, 2, "requires EVEX_B, EVEX_K and the XD prefix")   \
  ENUM_ENTRY(IC_EVEX_OPSIZE_K_B, 2,                                            \
             "requires EVEX_B, EVEX_K and the OpSize prefix")                  \
  ENUM_ENTRY(IC_EVEX_W_K_B, 3, "requires EVEX_B, EVEX_K and the W prefix")     \
  ENUM_ENTRY(IC_EVEX_W_XS_K_B, 4, "requires EVEX_B, EVEX_K, W, and XS prefix") \
  ENUM_ENTRY(IC_EVEX_W_XD_K_B, 4, "requires EVEX_B, EVEX_K, W, and XD prefix") \
  ENUM_ENTRY(IC_EVEX_W_OPSIZE_K_B, 4,                                          \
             "requires EVEX_B, EVEX_K, W, and OpSize")                         \
  ENUM_ENTRY(IC_EVEX_L_K_B, 3, "requires EVEX_B, EVEX_K and the L prefix")     \
  ENUM_ENTRY(IC_EVEX_L_XS_K_B, 4,                                              \
             "requires EVEX_B, EVEX_K and the L and XS prefix")                \
  ENUM_ENTRY(IC_EVEX_L_XD_K_B, 4,                                              \
             "requires EVEX_B, EVEX_K and the L and XD prefix")                \
  ENUM_ENTRY(IC_EVEX_L_OPSIZE_K_B, 4,                                          \
             "requires EVEX_B, EVEX_K, L, and OpSize")                         \
  ENUM_ENTRY(IC_EVEX_L_W_K_B, 3, "requires EVEX_B, EVEX_K, L and W")           \
  ENUM_ENTRY(IC_EVEX_L_W_XS_K_B, 4,                                            \
             "requires EVEX_B, EVEX_K, L, W and XS prefix")                    \
  ENUM_ENTRY(IC_EVEX_L_W_XD_K_B, 4,                                            \
             "requires EVEX_B, EVEX_K, L, W and XD prefix")                    \
  ENUM_ENTRY(IC_EVEX_L_W_OPSIZE_K_B, 4,                                        \
             "requires EVEX_B, EVEX_K, L, W and OpSize")                       \
  ENUM_ENTRY(IC_EVEX_L2_K_B, 3, "requires EVEX_B, EVEX_K and the L2 prefix")   \
  ENUM_ENTRY(IC_EVEX_L2_XS_K_B, 4,                                             \
             "requires EVEX_B, EVEX_K and the L2 and XS prefix")               \
  ENUM_ENTRY(IC_EVEX_L2_XD_K_B, 4,                                             \
             "requires EVEX_B, EVEX_K and the L2 and XD prefix")               \
  ENUM_ENTRY(IC_EVEX_L2_OPSIZE_K_B, 4,                                         \
             "requires EVEX_B, EVEX_K, L2, and OpSize")                        \
  ENUM_ENTRY(IC_EVEX_L2_W_K_B, 3, "requires EVEX_B, EVEX_K, L2 and W")         \
  ENUM_ENTRY(IC_EVEX_L2_W_XS_K_B, 4,                                           \
             "requires EVEX_B, EVEX_K, L2, W and XS prefix")                   \
  ENUM_ENTRY(IC_EVEX_L2_W_XD_K_B, 4,                                           \
             "requires EVEX_B, EVEX_K, L2, W and XD prefix")                   \
  ENUM_ENTRY(IC_EVEX_L2_W_OPSIZE_K_B, 4,                                       \
             "requires EVEX_B, EVEX_K, L2, W and OpSize")                      \
  ENUM_ENTRY(IC_EVEX_KZ_B, 1, "requires EVEX_B and EVEX_KZ prefix")            \
  ENUM_ENTRY(IC_EVEX_XS_KZ_B, 2, "requires EVEX_B, EVEX_KZ and the XS prefix") \
  ENUM_ENTRY(IC_EVEX_XD_KZ_B, 2, "requires EVEX_B, EVEX_KZ and the XD prefix") \
  ENUM_ENTRY(IC_EVEX_OPSIZE_KZ_B, 2,                                           \
             "requires EVEX_B, EVEX_KZ and the OpSize prefix")                 \
  ENUM_ENTRY(IC_EVEX_W_KZ_B, 3, "requires EVEX_B, EVEX_KZ and the W prefix")   \
  ENUM_ENTRY(IC_EVEX_W_XS_KZ_B, 4,                                             \
             "requires EVEX_B, EVEX_KZ, W, and XS prefix")                     \
  ENUM_ENTRY(IC_EVEX_W_XD_KZ_B, 4,                                             \
             "requires EVEX_B, EVEX_KZ, W, and XD prefix")                     \
  ENUM_ENTRY(IC_EVEX_W_OPSIZE_KZ_B, 4,                                         \
             "requires EVEX_B, EVEX_KZ, W, and OpSize")                        \
  ENUM_ENTRY(IC_EVEX_L_KZ_B, 3, "requires EVEX_B, EVEX_KZ and the L prefix")   \
  ENUM_ENTRY(IC_EVEX_L_XS_KZ_B, 4,                                             \
             "requires EVEX_B, EVEX_KZ and the L and XS prefix")               \
  ENUM_ENTRY(IC_EVEX_L_XD_KZ_B, 4,                                             \
             "requires EVEX_B, EVEX_KZ and the L and XD prefix")               \
  ENUM_ENTRY(IC_EVEX_L_OPSIZE_KZ_B, 4,                                         \
             "requires EVEX_B, EVEX_KZ, L, and OpSize")                        \
  ENUM_ENTRY(IC_EVEX_L_W_KZ_B, 3, "requires EVEX_B, EVEX_KZ, L and W")         \
  ENUM_ENTRY(IC_EVEX_L_W_XS_KZ_B, 4,                                           \
             "requires EVEX_B, EVEX_KZ, L, W and XS prefix")                   \
  ENUM_ENTRY(IC_EVEX_L_W_XD_KZ_B, 4,                                           \
             "requires EVEX_B, EVEX_KZ, L, W and XD prefix")                   \
  ENUM_ENTRY(IC_EVEX_L_W_OPSIZE_KZ_B, 4,                                       \
             "requires EVEX_B, EVEX_KZ, L, W and OpSize")                      \
  ENUM_ENTRY(IC_EVEX_L2_KZ_B, 3, "requires EVEX_B, EVEX_KZ and the L2 prefix") \
  ENUM_ENTRY(IC_EVEX_L2_XS_KZ_B, 4,                                            \
             "requires EVEX_B, EVEX_KZ and the L2 and XS prefix")              \
  ENUM_ENTRY(IC_EVEX_L2_XD_KZ_B, 4,                                            \
             "requires EVEX_B, EVEX_KZ and the L2 and XD prefix")              \
  ENUM_ENTRY(IC_EVEX_L2_OPSIZE_KZ_B, 4,                                        \
             "requires EVEX_B, EVEX_KZ, L2, and OpSize")                       \
  ENUM_ENTRY(IC_EVEX_L2_W_KZ_B, 3, "requires EVEX_B, EVEX_KZ, L2 and W")       \
  ENUM_ENTRY(IC_EVEX_L2_W_XS_KZ_B, 4,                                          \
             "requires EVEX_B, EVEX_KZ, L2, W and XS prefix")                  \
  ENUM_ENTRY(IC_EVEX_L2_W_XD_KZ_B, 4,                                          \
             "requires EVEX_B, EVEX_KZ, L2, W and XD prefix")                  \
  ENUM_ENTRY(IC_EVEX_L2_W_OPSIZE_KZ_B, 4,                                      \
             "requires EVEX_B, EVEX_KZ, L2, W and OpSize")                     \
  ENUM_ENTRY(IC_EVEX_KZ, 1, "requires an EVEX_KZ prefix")                      \
  ENUM_ENTRY(IC_EVEX_XS_KZ, 2, "requires EVEX_KZ and the XS prefix")           \
  ENUM_ENTRY(IC_EVEX_XD_KZ, 2, "requires EVEX_KZ and the XD prefix")           \
  ENUM_ENTRY(IC_EVEX_OPSIZE_KZ, 2, "requires EVEX_KZ and the OpSize prefix")   \
  ENUM_ENTRY(IC_EVEX_W_KZ, 3, "requires EVEX_KZ and the W prefix")             \
  ENUM_ENTRY(IC_EVEX_W_XS_KZ, 4, "requires EVEX_KZ, W, and XS prefix")         \
  ENUM_ENTRY(IC_EVEX_W_XD_KZ, 4, "requires EVEX_KZ, W, and XD prefix")         \
  ENUM_ENTRY(IC_EVEX_W_OPSIZE_KZ, 4, "requires EVEX_KZ, W, and OpSize")        \
  ENUM_ENTRY(IC_EVEX_L_KZ, 3, "requires EVEX_KZ and the L prefix")             \
  ENUM_ENTRY(IC_EVEX_L_XS_KZ, 4, "requires EVEX_KZ and the L and XS prefix")   \
  ENUM_ENTRY(IC_EVEX_L_XD_KZ, 4, "requires EVEX_KZ and the L and XD prefix")   \
  ENUM_ENTRY(IC_EVEX_L_OPSIZE_KZ, 4, "requires EVEX_KZ, L, and OpSize")        \
  ENUM_ENTRY(IC_EVEX_L_W_KZ, 3, "requires EVEX_KZ, L and W")                   \
  ENUM_ENTRY(IC_EVEX_L_W_XS_KZ, 4, "requires EVEX_KZ, L, W and XS prefix")     \
  ENUM_ENTRY(IC_EVEX_L_W_XD_KZ, 4, "requires EVEX_KZ, L, W and XD prefix")     \
  ENUM_ENTRY(IC_EVEX_L_W_OPSIZE_KZ, 4, "requires EVEX_KZ, L, W and OpSize")    \
  ENUM_ENTRY(IC_EVEX_L2_KZ, 3, "requires EVEX_KZ and the L2 prefix")           \
  ENUM_ENTRY(IC_EVEX_L2_XS_KZ, 4, "requires EVEX_KZ and the L2 and XS prefix") \
  ENUM_ENTRY(IC_EVEX_L2_XD_KZ, 4, "requires EVEX_KZ and the L2 and XD prefix") \
  ENUM_ENTRY(IC_EVEX_L2_OPSIZE_KZ, 4, "requires EVEX_KZ, L2, and OpSize")      \
  ENUM_ENTRY(IC_EVEX_L2_W_KZ, 3, "requires EVEX_KZ, L2 and W")                 \
  ENUM_ENTRY(IC_EVEX_L2_W_XS_KZ, 4, "requires EVEX_KZ, L2, W and XS prefix")   \
  ENUM_ENTRY(IC_EVEX_L2_W_XD_KZ, 4, "requires EVEX_KZ, L2, W and XD prefix")   \
  ENUM_ENTRY(IC_EVEX_L2_W_OPSIZE_KZ, 4, "requires EVEX_KZ, L2, W and OpSize")  \
  ENUM_ENTRY(IC_EVEX_B_U, 2, "requires EVEX_B and EVEX_U prefix")              \
  ENUM_ENTRY(IC_EVEX_XS_B_U, 3, "requires EVEX_B, XS and EVEX_U prefix")       \
  ENUM_ENTRY(IC_EVEX_XD_B_U, 3, "requires EVEX_B, XD and EVEX_U prefix")       \
  ENUM_ENTRY(IC_EVEX_OPSIZE_B_U, 3,                                            \
             "requires EVEX_B, OpSize and EVEX_U prefix")                      \
  ENUM_ENTRY(IC_EVEX_W_B_U, 4, "requires EVEX_B, W, and EVEX_U prefix")        \
  ENUM_ENTRY(IC_EVEX_W_XS_B_U, 5, "requires EVEX_B, W, XS, and EVEX_U prefix") \
  ENUM_ENTRY(IC_EVEX_W_XD_B_U, 5, "requires EVEX_B, W, XD, and EVEX_U prefix") \
  ENUM_ENTRY(IC_EVEX_W_OPSIZE_B_U, 5,                                          \
             "requires EVEX_B, W, OpSize and EVEX_U prefix")                   \
  ENUM_ENTRY(IC_EVEX_K_B_U, 2, "requires EVEX_B, EVEX_K and EVEX_U prefix")    \
  ENUM_ENTRY(IC_EVEX_XS_K_B_U, 3,                                              \
             "requires EVEX_B, EVEX_K, XS and the EVEX_U prefix")              \
  ENUM_ENTRY(IC_EVEX_XD_K_B_U, 3,                                              \
             "requires EVEX_B, EVEX_K, XD and the EVEX_U prefix")              \
  ENUM_ENTRY(IC_EVEX_OPSIZE_K_B_U, 3,                                          \
             "requires EVEX_B, EVEX_K, OpSize and the EVEX_U prefix")          \
  ENUM_ENTRY(IC_EVEX_W_K_B_U, 4,                                               \
             "requires EVEX_B, EVEX_K, W,  and the EVEX_U prefix")             \
  ENUM_ENTRY(IC_EVEX_W_XS_K_B_U, 5,                                            \
             "requires EVEX_B, EVEX_K, W, XS, and EVEX_U prefix")              \
  ENUM_ENTRY(IC_EVEX_W_XD_K_B_U, 5,                                            \
             "requires EVEX_B, EVEX_K, W, XD, and EVEX_U prefix")              \
  ENUM_ENTRY(IC_EVEX_W_OPSIZE_K_B_U, 5,                                        \
             "requires EVEX_B, EVEX_K, W, OpSize, and EVEX_U prefix")          \
  ENUM_ENTRY(IC_EVEX_KZ_B_U, 2, "requires EVEX_B, EVEX_KZ and EVEX_U prefix")  \
  ENUM_ENTRY(IC_EVEX_XS_KZ_B_U, 3,                                             \
             "requires EVEX_B, EVEX_KZ, XS, and the EVEX_U prefix")            \
  ENUM_ENTRY(IC_EVEX_XD_KZ_B_U, 3,                                             \
             "requires EVEX_B, EVEX_KZ, XD, and the EVEX_U prefix")            \
  ENUM_ENTRY(IC_EVEX_OPSIZE_KZ_B_U, 3,                                         \
             "requires EVEX_B, EVEX_KZ, OpSize and EVEX_U prefix")             \
  ENUM_ENTRY(IC_EVEX_W_KZ_B_U, 4,                                              \
             "requires EVEX_B, EVEX_KZ, W and the EVEX_U prefix")              \
  ENUM_ENTRY(IC_EVEX_W_XS_KZ_B_U, 5,                                           \
             "requires EVEX_B, EVEX_KZ, W, XS, and EVEX_U prefix")             \
  ENUM_ENTRY(IC_EVEX_W_XD_KZ_B_U, 5,                                           \
             "requires EVEX_B, EVEX_KZ, W, XD, and EVEX_U prefix")             \
  ENUM_ENTRY(IC_EVEX_W_OPSIZE_KZ_B_U, 5,                                       \
             "requires EVEX_B, EVEX_KZ, W, OpSize and EVEX_U prefix")

/// Combinations of attribute bits relevant to instruction decode.
///
/// Although other combinations are possible, they can be reduced to these
/// without affecting the ultimately decoded instruction.
enum InstructionContext {
  IC,                       ///< Says nothing about the instruction.
  IC_64BIT,                 ///< Says the instruction applies in 64-bit mode but no more.
  IC_OPSIZE,                ///< Requires an OPSIZE prefix, so operands change width.
  IC_ADSIZE,                ///< Requires an ADSIZE prefix, so operands change width.
  IC_OPSIZE_ADSIZE,         ///< Requires ADSIZE and OPSIZE prefixes.
  IC_XD,                    ///< May say something about the opcode but not the operands.
  IC_XS,                    ///< May say something about the opcode but not the operands.
  IC_XD_OPSIZE,             ///< Requires an OPSIZE prefix, so operands change width.
  IC_XS_OPSIZE,             ///< Requires an OPSIZE prefix, so operands change width.
  IC_XD_ADSIZE,             ///< Requires an ADSIZE prefix, so operands change width.
  IC_XS_ADSIZE,             ///< Requires an ADSIZE prefix, so operands change width.
  IC_64BIT_REXW,            ///< Requires a REX.W prefix, so operands change width; overrides IC_OPSIZE.
  IC_64BIT_REXW_ADSIZE,     ///< Requires a REX.W prefix and 0x67 prefix.
  IC_64BIT_OPSIZE,          ///< Just as meaningful as IC_OPSIZE.
  IC_64BIT_ADSIZE,          ///< Just as meaningful as IC_ADSIZE.
  IC_64BIT_OPSIZE_ADSIZE,   ///< Just as meaningful as IC_OPSIZE/IC_ADSIZE.
  IC_64BIT_XD,              ///< XD instructions are SSE; REX.W is secondary.
  IC_64BIT_XS,              ///< Just as meaningful as IC_64BIT_XD.
  IC_64BIT_XD_OPSIZE,       ///< Just as meaningful as IC_XD_OPSIZE.
  IC_64BIT_XS_OPSIZE,       ///< Just as meaningful as IC_XS_OPSIZE.
  IC_64BIT_XD_ADSIZE,       ///< Just as meaningful as IC_XD_ADSIZE.
  IC_64BIT_XS_ADSIZE,       ///< Just as meaningful as IC_XS_ADSIZE.
  IC_64BIT_REXW_XS,         ///< OPSIZE could mean a different opcode.
  IC_64BIT_REXW_XD,         ///< Just as meaningful as IC_64BIT_REXW_XS.
  IC_64BIT_REXW_OPSIZE,     ///< The Dynamic Duo!  Prefer over all else because this changes most operands' meaning.
  IC_64BIT_REX2,            ///< Requires a REX2 prefix.
  IC_64BIT_REX2_REXW,       ///< Requires a REX2 and the W prefix.
  IC_VEX,                   ///< Requires a VEX prefix.
  IC_VEX_XS,                ///< Requires VEX and the XS prefix.
  IC_VEX_XD,                ///< Requires VEX and the XD prefix.
  IC_VEX_OPSIZE,            ///< Requires VEX and the OpSize prefix.
  IC_VEX_W,                 ///< Requires VEX and the W prefix.
  IC_VEX_W_XS,              ///< Requires VEX, W, and XS prefix.
  IC_VEX_W_XD,              ///< Requires VEX, W, and XD prefix.
  IC_VEX_W_OPSIZE,          ///< Requires VEX, W, and OpSize.
  IC_VEX_L,                 ///< Requires VEX and the L prefix.
  IC_VEX_L_XS,              ///< Requires VEX and the L and XS prefix.
  IC_VEX_L_XD,              ///< Requires VEX and the L and XD prefix.
  IC_VEX_L_OPSIZE,          ///< Requires VEX, L, and OpSize.
  IC_VEX_L_W,               ///< Requires VEX, L and W.
  IC_VEX_L_W_XS,            ///< Requires VEX, L, W and XS prefix.
  IC_VEX_L_W_XD,            ///< Requires VEX, L, W and XD prefix.
  IC_VEX_L_W_OPSIZE,        ///< Requires VEX, L, W and OpSize.
  IC_EVEX,                  ///< Requires an EVEX prefix.
  IC_EVEX_NF,               ///< Requires EVEX and NF prefix.
  IC_EVEX_XS,               ///< Requires EVEX and the XS prefix.
  IC_EVEX_XS_ADSIZE,        ///< Requires EVEX, XS and the ADSIZE prefix.
  IC_EVEX_XD,               ///< Requires EVEX and the XD prefix.
  IC_EVEX_XD_ADSIZE,        ///< Requires EVEX, XD and the ADSIZE prefix.
  IC_EVEX_OPSIZE,           ///< Requires EVEX and the OpSize prefix.
  IC_EVEX_OPSIZE_NF,        ///< Requires EVEX, NF and the OpSize prefix.
  IC_EVEX_OPSIZE_ADSIZE,    ///< Requires EVEX, OPSIZE and the ADSIZE prefix.
  IC_EVEX_W,                ///< Requires EVEX and the W prefix.
  IC_EVEX_W_NF,             ///< Requires EVEX, W and NF prefix.
  IC_EVEX_W_XS,             ///< Requires EVEX, W, and XS prefix.
  IC_EVEX_W_XD,             ///< Requires EVEX, W, and XD prefix.
  IC_EVEX_W_OPSIZE,         ///< Requires EVEX, W, and OpSize.
  IC_EVEX_L,                ///< Requires EVEX and the L prefix.
  IC_EVEX_L_XS,             ///< Requires EVEX and the L and XS prefix.
  IC_EVEX_L_XD,             ///< Requires EVEX and the L and XD prefix.
  IC_EVEX_L_OPSIZE,         ///< Requires EVEX, L, and OpSize.
  IC_EVEX_L_W,              ///< Requires EVEX, L and W.
  IC_EVEX_L_W_XS,           ///< Requires EVEX, L, W and XS prefix.
  IC_EVEX_L_W_XD,           ///< Requires EVEX, L, W and XD prefix.
  IC_EVEX_L_W_OPSIZE,       ///< Requires EVEX, L, W and OpSize.
  IC_EVEX_L2,               ///< Requires EVEX and the L2 prefix.
  IC_EVEX_L2_XS,            ///< Requires EVEX and the L2 and XS prefix.
  IC_EVEX_L2_XD,            ///< Requires EVEX and the L2 and XD prefix.
  IC_EVEX_L2_OPSIZE,        ///< Requires EVEX, L2, and OpSize.
  IC_EVEX_L2_W,             ///< Requires EVEX, L2 and W.
  IC_EVEX_L2_W_XS,          ///< Requires EVEX, L2, W and XS prefix.
  IC_EVEX_L2_W_XD,          ///< Requires EVEX, L2, W and XD prefix.
  IC_EVEX_L2_W_OPSIZE,      ///< Requires EVEX, L2, W and OpSize.
  IC_EVEX_K,                ///< Requires an EVEX_K prefix.
  IC_EVEX_XS_K,             ///< Requires EVEX_K and the XS prefix.
  IC_EVEX_XD_K,             ///< Requires EVEX_K and the XD prefix.
  IC_EVEX_OPSIZE_K,         ///< Requires EVEX_K and the OpSize prefix.
  IC_EVEX_W_K,              ///< Requires EVEX_K and the W prefix.
  IC_EVEX_W_XS_K,           ///< Requires EVEX_K, W, and XS prefix.
  IC_EVEX_W_XD_K,           ///< Requires EVEX_K, W, and XD prefix.
  IC_EVEX_W_OPSIZE_K,       ///< Requires EVEX_K, W, and OpSize.
  IC_EVEX_L_K,              ///< Requires EVEX_K and the L prefix.
  IC_EVEX_L_XS_K,           ///< Requires EVEX_K and the L and XS prefix.
  IC_EVEX_L_XD_K,           ///< Requires EVEX_K and the L and XD prefix.
  IC_EVEX_L_OPSIZE_K,       ///< Requires EVEX_K, L, and OpSize.
  IC_EVEX_L_W_K,            ///< Requires EVEX_K, L and W.
  IC_EVEX_L_W_XS_K,         ///< Requires EVEX_K, L, W and XS prefix.
  IC_EVEX_L_W_XD_K,         ///< Requires EVEX_K, L, W and XD prefix.
  IC_EVEX_L_W_OPSIZE_K,     ///< Requires EVEX_K, L, W and OpSize.
  IC_EVEX_L2_K,             ///< Requires EVEX_K and the L2 prefix.
  IC_EVEX_L2_XS_K,          ///< Requires EVEX_K and the L2 and XS prefix.
  IC_EVEX_L2_XD_K,          ///< Requires EVEX_K and the L2 and XD prefix.
  IC_EVEX_L2_OPSIZE_K,      ///< Requires EVEX_K, L2, and OpSize.
  IC_EVEX_L2_W_K,           ///< Requires EVEX_K, L2 and W.
  IC_EVEX_L2_W_XS_K,        ///< Requires EVEX_K, L2, W and XS prefix.
  IC_EVEX_L2_W_XD_K,        ///< Requires EVEX_K, L2, W and XD prefix.
  IC_EVEX_L2_W_OPSIZE_K,    ///< Requires EVEX_K, L2, W and OpSize.
  IC_EVEX_B,                ///< Requires an EVEX_B prefix.
  IC_EVEX_B_NF,             ///< Requires EVEX_NF and EVEX_B prefix.
  IC_EVEX_XS_B,             ///< Requires EVEX_B and the XS prefix.
  IC_EVEX_XD_B,             ///< Requires EVEX_B and the XD prefix.
  IC_EVEX_OPSIZE_B,         ///< Requires EVEX_B and the OpSize prefix.
  IC_EVEX_OPSIZE_B_NF,      ///< Requires EVEX_B, NF and Opsize prefix.
  IC_EVEX_W_B,              ///< Requires EVEX_B and the W prefix.
  IC_EVEX_W_B_NF,           ///< Requires EVEX_NF, EVEX_B and the W prefix.
  IC_EVEX_W_XS_B,           ///< Requires EVEX_B, W, and XS prefix.
  IC_EVEX_W_XD_B,           ///< Requires EVEX_B, W, and XD prefix.
  IC_EVEX_W_OPSIZE_B,       ///< Requires EVEX_B, W, and OpSize.
  IC_EVEX_L_B,              ///< Requires EVEX_B and the L prefix.
  IC_EVEX_L_XS_B,           ///< Requires EVEX_B and the L and XS prefix.
  IC_EVEX_L_XD_B,           ///< Requires EVEX_B and the L and XD prefix.
  IC_EVEX_L_OPSIZE_B,       ///< Requires EVEX_B, L, and OpSize.
  IC_EVEX_L_W_B,            ///< Requires EVEX_B, L and W.
  IC_EVEX_L_W_XS_B,         ///< Requires EVEX_B, L, W and XS prefix.
  IC_EVEX_L_W_XD_B,         ///< Requires EVEX_B, L, W and XD prefix.
  IC_EVEX_L_W_OPSIZE_B,     ///< Requires EVEX_B, L, W and OpSize.
  IC_EVEX_L2_B,             ///< Requires EVEX_B and the L2 prefix.
  IC_EVEX_L2_XS_B,          ///< Requires EVEX_B and the L2 and XS prefix.
  IC_EVEX_L2_XD_B,          ///< Requires EVEX_B and the L2 and XD prefix.
  IC_EVEX_L2_OPSIZE_B,      ///< Requires EVEX_B, L2, and OpSize.
  IC_EVEX_L2_W_B,           ///< Requires EVEX_B, L2 and W.
  IC_EVEX_L2_W_XS_B,        ///< Requires EVEX_B, L2, W and XS prefix.
  IC_EVEX_L2_W_XD_B,        ///< Requires EVEX_B, L2, W and XD prefix.
  IC_EVEX_L2_W_OPSIZE_B,    ///< Requires EVEX_B, L2, W and OpSize.
  IC_EVEX_K_B,              ///< Requires EVEX_B and EVEX_K prefix.
  IC_EVEX_XS_K_B,           ///< Requires EVEX_B, EVEX_K and the XS prefix.
  IC_EVEX_XD_K_B,           ///< Requires EVEX_B, EVEX_K and the XD prefix.
  IC_EVEX_OPSIZE_K_B,       ///< Requires EVEX_B, EVEX_K and the OpSize prefix.
  IC_EVEX_W_K_B,            ///< Requires EVEX_B, EVEX_K and the W prefix.
  IC_EVEX_W_XS_K_B,         ///< Requires EVEX_B, EVEX_K, W, and XS prefix.
  IC_EVEX_W_XD_K_B,         ///< Requires EVEX_B, EVEX_K, W, and XD prefix.
  IC_EVEX_W_OPSIZE_K_B,     ///< Requires EVEX_B, EVEX_K, W, and OpSize.
  IC_EVEX_L_K_B,            ///< Requires EVEX_B, EVEX_K and the L prefix.
  IC_EVEX_L_XS_K_B,         ///< Requires EVEX_B, EVEX_K and the L and XS prefix.
  IC_EVEX_L_XD_K_B,         ///< Requires EVEX_B, EVEX_K and the L and XD prefix.
  IC_EVEX_L_OPSIZE_K_B,     ///< Requires EVEX_B, EVEX_K, L, and OpSize.
  IC_EVEX_L_W_K_B,          ///< Requires EVEX_B, EVEX_K, L and W.
  IC_EVEX_L_W_XS_K_B,       ///< Requires EVEX_B, EVEX_K, L, W and XS prefix.
  IC_EVEX_L_W_XD_K_B,       ///< Requires EVEX_B, EVEX_K, L, W and XD prefix.
  IC_EVEX_L_W_OPSIZE_K_B,   ///< Requires EVEX_B, EVEX_K, L, W and OpSize.
  IC_EVEX_L2_K_B,           ///< Requires EVEX_B, EVEX_K and the L2 prefix.
  IC_EVEX_L2_XS_K_B,        ///< Requires EVEX_B, EVEX_K and the L2 and XS prefix.
  IC_EVEX_L2_XD_K_B,        ///< Requires EVEX_B, EVEX_K and the L2 and XD prefix.
  IC_EVEX_L2_OPSIZE_K_B,    ///< Requires EVEX_B, EVEX_K, L2, and OpSize.
  IC_EVEX_L2_W_K_B,         ///< Requires EVEX_B, EVEX_K, L2 and W.
  IC_EVEX_L2_W_XS_K_B,      ///< Requires EVEX_B, EVEX_K, L2, W and XS prefix.
  IC_EVEX_L2_W_XD_K_B,      ///< Requires EVEX_B, EVEX_K, L2, W and XD prefix.
  IC_EVEX_L2_W_OPSIZE_K_B,  ///< Requires EVEX_B, EVEX_K, L2, W and OpSize.
  IC_EVEX_KZ_B,             ///< Requires EVEX_B and EVEX_KZ prefix.
  IC_EVEX_XS_KZ_B,          ///< Requires EVEX_B, EVEX_KZ and the XS prefix.
  IC_EVEX_XD_KZ_B,          ///< Requires EVEX_B, EVEX_KZ and the XD prefix.
  IC_EVEX_OPSIZE_KZ_B,      ///< Requires EVEX_B, EVEX_KZ and the OpSize prefix.
  IC_EVEX_W_KZ_B,           ///< Requires EVEX_B, EVEX_KZ and the W prefix.
  IC_EVEX_W_XS_KZ_B,        ///< Requires EVEX_B, EVEX_KZ, W, and XS prefix.
  IC_EVEX_W_XD_KZ_B,        ///< Requires EVEX_B, EVEX_KZ, W, and XD prefix.
  IC_EVEX_W_OPSIZE_KZ_B,    ///< Requires EVEX_B, EVEX_KZ, W, and OpSize.
  IC_EVEX_L_KZ_B,           ///< Requires EVEX_B, EVEX_KZ and the L prefix.
  IC_EVEX_L_XS_KZ_B,        ///< Requires EVEX_B, EVEX_KZ and the L and XS prefix.
  IC_EVEX_L_XD_KZ_B,        ///< Requires EVEX_B, EVEX_KZ and the L and XD prefix.
  IC_EVEX_L_OPSIZE_KZ_B,    ///< Requires EVEX_B, EVEX_KZ, L, and OpSize.
  IC_EVEX_L_W_KZ_B,         ///< Requires EVEX_B, EVEX_KZ, L and W.
  IC_EVEX_L_W_XS_KZ_B,      ///< Requires EVEX_B, EVEX_KZ, L, W and XS prefix.
  IC_EVEX_L_W_XD_KZ_B,      ///< Requires EVEX_B, EVEX_KZ, L, W and XD prefix.
  IC_EVEX_L_W_OPSIZE_KZ_B,  ///< Requires EVEX_B, EVEX_KZ, L, W and OpSize.
  IC_EVEX_L2_KZ_B,          ///< Requires EVEX_B, EVEX_KZ and the L2 prefix.
  IC_EVEX_L2_XS_KZ_B,       ///< Requires EVEX_B, EVEX_KZ and the L2 and XS prefix.
  IC_EVEX_L2_XD_KZ_B,       ///< Requires EVEX_B, EVEX_KZ and the L2 and XD prefix.
  IC_EVEX_L2_OPSIZE_KZ_B,   ///< Requires EVEX_B, EVEX_KZ, L2, and OpSize.
  IC_EVEX_L2_W_KZ_B,        ///< Requires EVEX_B, EVEX_KZ, L2 and W.
  IC_EVEX_L2_W_XS_KZ_B,     ///< Requires EVEX_B, EVEX_KZ, L2, W and XS prefix.
  IC_EVEX_L2_W_XD_KZ_B,     ///< Requires EVEX_B, EVEX_KZ, L2, W and XD prefix.
  IC_EVEX_L2_W_OPSIZE_KZ_B, ///< Requires EVEX_B, EVEX_KZ, L2, W and OpSize.
  IC_EVEX_KZ,               ///< Requires an EVEX_KZ prefix.
  IC_EVEX_XS_KZ,            ///< Requires EVEX_KZ and the XS prefix.
  IC_EVEX_XD_KZ,            ///< Requires EVEX_KZ and the XD prefix.
  IC_EVEX_OPSIZE_KZ,        ///< Requires EVEX_KZ and the OpSize prefix.
  IC_EVEX_W_KZ,             ///< Requires EVEX_KZ and the W prefix.
  IC_EVEX_W_XS_KZ,          ///< Requires EVEX_KZ, W, and XS prefix.
  IC_EVEX_W_XD_KZ,          ///< Requires EVEX_KZ, W, and XD prefix.
  IC_EVEX_W_OPSIZE_KZ,      ///< Requires EVEX_KZ, W, and OpSize.
  IC_EVEX_L_KZ,             ///< Requires EVEX_KZ and the L prefix.
  IC_EVEX_L_XS_KZ,          ///< Requires EVEX_KZ and the L and XS prefix.
  IC_EVEX_L_XD_KZ,          ///< Requires EVEX_KZ and the L and XD prefix.
  IC_EVEX_L_OPSIZE_KZ,      ///< Requires EVEX_KZ, L, and OpSize.
  IC_EVEX_L_W_KZ,           ///< Requires EVEX_KZ, L and W.
  IC_EVEX_L_W_XS_KZ,        ///< Requires EVEX_KZ, L, W and XS prefix.
  IC_EVEX_L_W_XD_KZ,        ///< Requires EVEX_KZ, L, W and XD prefix.
  IC_EVEX_L_W_OPSIZE_KZ,    ///< Requires EVEX_KZ, L, W and OpSize.
  IC_EVEX_L2_KZ,            ///< Requires EVEX_KZ and the L2 prefix.
  IC_EVEX_L2_XS_KZ,         ///< Requires EVEX_KZ and the L2 and XS prefix.
  IC_EVEX_L2_XD_KZ,         ///< Requires EVEX_KZ and the L2 and XD prefix.
  IC_EVEX_L2_OPSIZE_KZ,     ///< Requires EVEX_KZ, L2, and OpSize.
  IC_EVEX_L2_W_KZ,          ///< Requires EVEX_KZ, L2 and W.
  IC_EVEX_L2_W_XS_KZ,       ///< Requires EVEX_KZ, L2, W and XS prefix.
  IC_EVEX_L2_W_XD_KZ,       ///< Requires EVEX_KZ, L2, W and XD prefix.
  IC_EVEX_L2_W_OPSIZE_KZ,   ///< Requires EVEX_KZ, L2, W and OpSize.
  IC_EVEX_B_U,              ///< Requires EVEX_B and EVEX_U prefix.
  IC_EVEX_XS_B_U,           ///< Requires EVEX_B, XS and EVEX_U prefix.
  IC_EVEX_XD_B_U,           ///< Requires EVEX_B, XD and EVEX_U prefix.
  IC_EVEX_OPSIZE_B_U,       ///< Requires EVEX_B, OpSize and EVEX_U prefix.
  IC_EVEX_W_B_U,            ///< Requires EVEX_B, W, and EVEX_U prefix.
  IC_EVEX_W_XS_B_U,         ///< Requires EVEX_B, W, XS, and EVEX_U prefix.
  IC_EVEX_W_XD_B_U,         ///< Requires EVEX_B, W, XD, and EVEX_U prefix.
  IC_EVEX_W_OPSIZE_B_U,     ///< Requires EVEX_B, W, OpSize and EVEX_U prefix.
  IC_EVEX_K_B_U,            ///< Requires EVEX_B, EVEX_K and EVEX_U prefix.
  IC_EVEX_XS_K_B_U,         ///< Requires EVEX_B, EVEX_K, XS and the EVEX_U prefix.
  IC_EVEX_XD_K_B_U,         ///< Requires EVEX_B, EVEX_K, XD and the EVEX_U prefix.
  IC_EVEX_OPSIZE_K_B_U,     ///< Requires EVEX_B, EVEX_K, OpSize and the EVEX_U prefix.
  IC_EVEX_W_K_B_U,          ///< Requires EVEX_B, EVEX_K, W,  and the EVEX_U prefix.
  IC_EVEX_W_XS_K_B_U,       ///< Requires EVEX_B, EVEX_K, W, XS, and EVEX_U prefix.
  IC_EVEX_W_XD_K_B_U,       ///< Requires EVEX_B, EVEX_K, W, XD, and EVEX_U prefix.
  IC_EVEX_W_OPSIZE_K_B_U,   ///< Requires EVEX_B, EVEX_K, W, OpSize, and EVEX_U prefix.
  IC_EVEX_KZ_B_U,           ///< Requires EVEX_B, EVEX_KZ and EVEX_U prefix.
  IC_EVEX_XS_KZ_B_U,        ///< Requires EVEX_B, EVEX_KZ, XS, and the EVEX_U prefix.
  IC_EVEX_XD_KZ_B_U,        ///< Requires EVEX_B, EVEX_KZ, XD, and the EVEX_U prefix.
  IC_EVEX_OPSIZE_KZ_B_U,    ///< Requires EVEX_B, EVEX_KZ, OpSize and EVEX_U prefix.
  IC_EVEX_W_KZ_B_U,         ///< Requires EVEX_B, EVEX_KZ, W and the EVEX_U prefix.
  IC_EVEX_W_XS_KZ_B_U,      ///< Requires EVEX_B, EVEX_KZ, W, XS, and EVEX_U prefix.
  IC_EVEX_W_XD_KZ_B_U,      ///< Requires EVEX_B, EVEX_KZ, W, XD, and EVEX_U prefix.
  IC_EVEX_W_OPSIZE_KZ_B_U,  ///< Requires EVEX_B, EVEX_KZ, W, OpSize and EVEX_U prefix.
  IC_max                   ///< Sentinel past the last instruction context.
};

/// Opcode map selecting which decode table to consult.
///
/// Matches the opcode maps in the Intel manuals and in the decoder tables.
enum OpcodeType {
  ONEBYTE = 0,       ///< One-byte opcode map.
  TWOBYTE = 1,       ///< Two-byte opcode map (0F).
  THREEBYTE_38 = 2,  ///< Three-byte map 0F 38.
  THREEBYTE_3A = 3,  ///< Three-byte map 0F 3A.
  XOP8_MAP = 4,      ///< XOP map 8.
  XOP9_MAP = 5,      ///< XOP map 9.
  XOPA_MAP = 6,      ///< XOP map A.
  THREEDNOW_MAP = 7, ///< 3DNow! opcode map.
  MAP4 = 8,          ///< EVEX/VEX map 4.
  MAP5 = 9,          ///< EVEX/VEX map 5.
  MAP6 = 10,         ///< EVEX/VEX map 6.
  MAP7 = 11          ///< EVEX/VEX map 7.
};

/// Unique identifier for an instruction specifier in the decode tables.
///
/// After determining the instruction's class (which IC_* constant applies), the
/// decoder reads the opcode. Some instructions require specific values of the
/// ModR/M byte, so the ModR/M byte indexes into the final table. If a ModR/M
/// byte is not required, "required" is left unset, and the values for each
/// instruction ID are identical.
typedef uint16_t InstrUID;

// ModRMDecisionType - describes the type of ModR/M decision, allowing the
// consumer to determine the number of entries in it.
//
// MODRM_ONEENTRY - No matter what the value of the ModR/M byte is, the decoded
//                  instruction is the same.
// MODRM_SPLITRM  - If the ModR/M byte is between 0x00 and 0xbf, the opcode
//                  corresponds to one instruction; otherwise, it corresponds to
//                  a different instruction.
// MODRM_SPLITMISC- If the ModR/M byte is between 0x00 and 0xbf, ModR/M byte
//                  divided by 8 is used to select instruction; otherwise, each
//                  value of the ModR/M byte could correspond to a different
//                  instruction.
// MODRM_SPLITREG - ModR/M byte divided by 8 is used to select instruction. This
//                  corresponds to instructions that use reg field as opcode.
// MODRM_FULL     - Potentially, each value of the ModR/M byte could correspond
//                  to a different instruction.
#define MODRMTYPES                                                             \
  ENUM_ENTRY(MODRM_ONEENTRY)                                                   \
  ENUM_ENTRY(MODRM_SPLITRM)                                                    \
  ENUM_ENTRY(MODRM_SPLITMISC)                                                  \
  ENUM_ENTRY(MODRM_SPLITREG)                                                   \
  ENUM_ENTRY(MODRM_FULL)

/// Type of ModR/M decision, determining how many table entries it has.
enum ModRMDecisionType {
  MODRM_ONEENTRY,  ///< Same instruction for every ModR/M value.
  MODRM_SPLITRM,   ///< One instruction for 0x00-0xbf; another otherwise.
  MODRM_SPLITMISC, ///< Memory form uses ModR/M/8; register form is per-byte.
  MODRM_SPLITREG,  ///< Instruction selected by ModR/M reg field (byte/8).
  MODRM_FULL,      ///< Each ModR/M value may select a different instruction.
  MODRM_max        ///< Sentinel past the last ModR/M decision type.
};

#define CASE_ENCODING_RM                                                       \
  case ENCODING_RM:                                                            \
  case ENCODING_RM_CD2:                                                        \
  case ENCODING_RM_CD4:                                                        \
  case ENCODING_RM_CD8:                                                        \
  case ENCODING_RM_CD16:                                                       \
  case ENCODING_RM_CD32:                                                       \
  case ENCODING_RM_CD64

#define CASE_ENCODING_VSIB                                                     \
  case ENCODING_VSIB:                                                          \
  case ENCODING_VSIB_CD2:                                                      \
  case ENCODING_VSIB_CD4:                                                      \
  case ENCODING_VSIB_CD8:                                                      \
  case ENCODING_VSIB_CD16:                                                     \
  case ENCODING_VSIB_CD32:                                                     \
  case ENCODING_VSIB_CD64

// Physical encodings of instruction operands.
#define ENCODINGS                                                              \
  ENUM_ENTRY(ENCODING_NONE, "No operand encoding")                             \
  ENUM_ENTRY(ENCODING_REG, "Register operand in ModR/M byte.")                 \
  ENUM_ENTRY(ENCODING_RM, "R/M operand in ModR/M byte.")                       \
  ENUM_ENTRY(ENCODING_RM_CD2, "R/M operand with CDisp scaling of 2")           \
  ENUM_ENTRY(ENCODING_RM_CD4, "R/M operand with CDisp scaling of 4")           \
  ENUM_ENTRY(ENCODING_RM_CD8, "R/M operand with CDisp scaling of 8")           \
  ENUM_ENTRY(ENCODING_RM_CD16, "R/M operand with CDisp scaling of 16")         \
  ENUM_ENTRY(ENCODING_RM_CD32, "R/M operand with CDisp scaling of 32")         \
  ENUM_ENTRY(ENCODING_RM_CD64, "R/M operand with CDisp scaling of 64")         \
  ENUM_ENTRY(ENCODING_SIB, "Force SIB operand in ModR/M byte.")                \
  ENUM_ENTRY(ENCODING_VSIB, "VSIB operand in ModR/M byte.")                    \
  ENUM_ENTRY(ENCODING_VSIB_CD2, "VSIB operand with CDisp scaling of 2")        \
  ENUM_ENTRY(ENCODING_VSIB_CD4, "VSIB operand with CDisp scaling of 4")        \
  ENUM_ENTRY(ENCODING_VSIB_CD8, "VSIB operand with CDisp scaling of 8")        \
  ENUM_ENTRY(ENCODING_VSIB_CD16, "VSIB operand with CDisp scaling of 16")      \
  ENUM_ENTRY(ENCODING_VSIB_CD32, "VSIB operand with CDisp scaling of 32")      \
  ENUM_ENTRY(ENCODING_VSIB_CD64, "VSIB operand with CDisp scaling of 64")      \
  ENUM_ENTRY(ENCODING_VVVV, "Register operand in VEX.vvvv byte.")              \
  ENUM_ENTRY(ENCODING_WRITEMASK, "Register operand in EVEX.aaa byte.")         \
  ENUM_ENTRY(ENCODING_IB, "1-byte immediate")                                  \
  ENUM_ENTRY(ENCODING_IW, "2-byte")                                            \
  ENUM_ENTRY(ENCODING_ID, "4-byte")                                            \
  ENUM_ENTRY(ENCODING_IO, "8-byte")                                            \
  ENUM_ENTRY(ENCODING_RB,                                                      \
             "(AL..DIL, R8B..R15B) Register code added to the opcode byte")    \
  ENUM_ENTRY(ENCODING_RW, "(AX..DI, R8W..R15W)")                               \
  ENUM_ENTRY(ENCODING_RD, "(EAX..EDI, R8D..R15D)")                             \
  ENUM_ENTRY(ENCODING_RO, "(RAX..RDI, R8..R15)")                               \
  ENUM_ENTRY(ENCODING_FP, "Position on floating-point stack in ModR/M byte.")  \
  ENUM_ENTRY(ENCODING_Iv, "Immediate of operand size")                         \
  ENUM_ENTRY(ENCODING_Ia, "Immediate of address size")                         \
  ENUM_ENTRY(ENCODING_IRC, "Immediate for static rounding control")            \
  ENUM_ENTRY(ENCODING_Rv,                                                      \
             "Register code of operand size added to the opcode byte")         \
  ENUM_ENTRY(ENCODING_CC, "Condition code encoded in opcode")                  \
  ENUM_ENTRY(ENCODING_CF, "Condition flags encoded in EVEX.VVVV")              \
  ENUM_ENTRY(ENCODING_DUP,                                                     \
             "Duplicate of another operand; ID is encoded in type")            \
  ENUM_ENTRY(ENCODING_SI, "Source index; encoded in OpSize/Adsize prefix")     \
  ENUM_ENTRY(ENCODING_DI, "Destination index; encoded in prefixes")

/// Physical encodings of instruction operands.
enum OperandEncoding {
  ENCODING_NONE,      ///< No operand encoding.
  ENCODING_REG,       ///< Register operand in ModR/M byte.
  ENCODING_RM,        ///< R/M operand in ModR/M byte.
  ENCODING_RM_CD2,    ///< R/M operand with CDisp scaling of 2.
  ENCODING_RM_CD4,    ///< R/M operand with CDisp scaling of 4.
  ENCODING_RM_CD8,    ///< R/M operand with CDisp scaling of 8.
  ENCODING_RM_CD16,   ///< R/M operand with CDisp scaling of 16.
  ENCODING_RM_CD32,   ///< R/M operand with CDisp scaling of 32.
  ENCODING_RM_CD64,   ///< R/M operand with CDisp scaling of 64.
  ENCODING_SIB,       ///< Force SIB operand in ModR/M byte.
  ENCODING_VSIB,      ///< VSIB operand in ModR/M byte.
  ENCODING_VSIB_CD2,  ///< VSIB operand with CDisp scaling of 2.
  ENCODING_VSIB_CD4,  ///< VSIB operand with CDisp scaling of 4.
  ENCODING_VSIB_CD8,  ///< VSIB operand with CDisp scaling of 8.
  ENCODING_VSIB_CD16, ///< VSIB operand with CDisp scaling of 16.
  ENCODING_VSIB_CD32, ///< VSIB operand with CDisp scaling of 32.
  ENCODING_VSIB_CD64, ///< VSIB operand with CDisp scaling of 64.
  ENCODING_VVVV,      ///< Register operand in VEX.vvvv byte.
  ENCODING_WRITEMASK, ///< Register operand in EVEX.aaa byte.
  ENCODING_IB,        ///< 1-byte immediate.
  ENCODING_IW,        ///< 2-byte immediate.
  ENCODING_ID,        ///< 4-byte immediate.
  ENCODING_IO,        ///< 8-byte immediate.
  ENCODING_RB,        ///< (AL..DIL, R8B..R15B) Register code added to the opcode byte.
  ENCODING_RW,        ///< (AX..DI, R8W..R15W) Register code added to the opcode byte.
  ENCODING_RD,        ///< (EAX..EDI, R8D..R15D) Register code added to the opcode byte.
  ENCODING_RO,        ///< (RAX..RDI, R8..R15) Register code added to the opcode byte.
  ENCODING_FP,        ///< Position on floating-point stack in ModR/M byte.
  ENCODING_Iv,        ///< Immediate of operand size.
  ENCODING_Ia,        ///< Immediate of address size.
  ENCODING_IRC,       ///< Immediate for static rounding control.
  ENCODING_Rv,        ///< Register code of operand size added to the opcode byte.
  ENCODING_CC,        ///< Condition code encoded in opcode.
  ENCODING_CF,        ///< Condition flags encoded in EVEX.VVVV.
  ENCODING_DUP,       ///< Duplicate of another operand; ID is encoded in type.
  ENCODING_SI,        ///< Source index; encoded in OpSize/Adsize prefix.
  ENCODING_DI,        ///< Destination index; encoded in prefixes.
  ENCODING_max       ///< Sentinel past the last operand encoding.
};

// Semantic interpretations of instruction operands.
#define TYPES                                                                  \
  ENUM_ENTRY(TYPE_NONE, "No operand type")                                     \
  ENUM_ENTRY(TYPE_REL, "immediate address")                                    \
  ENUM_ENTRY(TYPE_R8, "1-byte register operand")                               \
  ENUM_ENTRY(TYPE_R16, "2-byte")                                               \
  ENUM_ENTRY(TYPE_R32, "4-byte")                                               \
  ENUM_ENTRY(TYPE_R64, "8-byte")                                               \
  ENUM_ENTRY(TYPE_IMM, "immediate operand")                                    \
  ENUM_ENTRY(TYPE_UIMM8, "1-byte unsigned immediate operand")                  \
  ENUM_ENTRY(TYPE_M, "Memory operand")                                         \
  ENUM_ENTRY(TYPE_MSIB, "Memory operand force sib encoding")                   \
  ENUM_ENTRY(TYPE_MVSIBX, "Memory operand using XMM index")                    \
  ENUM_ENTRY(TYPE_MVSIBY, "Memory operand using YMM index")                    \
  ENUM_ENTRY(TYPE_MVSIBZ, "Memory operand using ZMM index")                    \
  ENUM_ENTRY(TYPE_SRCIDX, "memory at source index")                            \
  ENUM_ENTRY(TYPE_DSTIDX, "memory at destination index")                       \
  ENUM_ENTRY(TYPE_MOFFS, "memory offset (relative to segment base)")           \
  ENUM_ENTRY(TYPE_ST, "Position on the floating-point stack")                  \
  ENUM_ENTRY(TYPE_MM64, "8-byte MMX register")                                 \
  ENUM_ENTRY(TYPE_XMM, "16-byte")                                              \
  ENUM_ENTRY(TYPE_YMM, "32-byte")                                              \
  ENUM_ENTRY(TYPE_ZMM, "64-byte")                                              \
  ENUM_ENTRY(TYPE_VK, "mask register")                                         \
  ENUM_ENTRY(TYPE_VK_PAIR, "mask register pair")                               \
  ENUM_ENTRY(TYPE_TMM, "tile")                                                 \
  ENUM_ENTRY(TYPE_SEGMENTREG, "Segment register operand")                      \
  ENUM_ENTRY(TYPE_DEBUGREG, "Debug register operand")                          \
  ENUM_ENTRY(TYPE_CONTROLREG, "Control register operand")                      \
  ENUM_ENTRY(TYPE_BNDR, "MPX bounds register")                                 \
  ENUM_ENTRY(TYPE_Rv, "Register operand of operand size")                      \
  ENUM_ENTRY(TYPE_RELv, "Immediate address of operand size")                   \
  ENUM_ENTRY(TYPE_DUP0, "Duplicate of operand 0")                              \
  ENUM_ENTRY(TYPE_DUP1, "operand 1")                                           \
  ENUM_ENTRY(TYPE_DUP2, "operand 2")                                           \
  ENUM_ENTRY(TYPE_DUP3, "operand 3")                                           \
  ENUM_ENTRY(TYPE_DUP4, "operand 4")

/// Semantic interpretations of instruction operands.
enum OperandType {
  TYPE_NONE,       ///< No operand type.
  TYPE_REL,        ///< Immediate address.
  TYPE_R8,         ///< 1-byte register operand.
  TYPE_R16,        ///< 2-byte register operand.
  TYPE_R32,        ///< 4-byte register operand.
  TYPE_R64,        ///< 8-byte register operand.
  TYPE_IMM,        ///< Immediate operand.
  TYPE_UIMM8,      ///< 1-byte unsigned immediate operand.
  TYPE_M,          ///< Memory operand.
  TYPE_MSIB,       ///< Memory operand force sib encoding.
  TYPE_MVSIBX,     ///< Memory operand using XMM index.
  TYPE_MVSIBY,     ///< Memory operand using YMM index.
  TYPE_MVSIBZ,     ///< Memory operand using ZMM index.
  TYPE_SRCIDX,     ///< Memory at source index.
  TYPE_DSTIDX,     ///< Memory at destination index.
  TYPE_MOFFS,      ///< Memory offset (relative to segment base).
  TYPE_ST,         ///< Position on the floating-point stack.
  TYPE_MM64,       ///< 8-byte MMX register.
  TYPE_XMM,        ///< 16-byte XMM register.
  TYPE_YMM,        ///< 32-byte YMM register.
  TYPE_ZMM,        ///< 64-byte ZMM register.
  TYPE_VK,         ///< Mask register.
  TYPE_VK_PAIR,    ///< Mask register pair.
  TYPE_TMM,        ///< Tile (TMM) register.
  TYPE_SEGMENTREG, ///< Segment register operand.
  TYPE_DEBUGREG,   ///< Debug register operand.
  TYPE_CONTROLREG, ///< Control register operand.
  TYPE_BNDR,       ///< MPX bounds register.
  TYPE_Rv,         ///< Register operand of operand size.
  TYPE_RELv,       ///< Immediate address of operand size.
  TYPE_DUP0,       ///< Duplicate of operand 0.
  TYPE_DUP1,       ///< Duplicate of operand 1.
  TYPE_DUP2,       ///< Duplicate of operand 2.
  TYPE_DUP3,       ///< Duplicate of operand 3.
  TYPE_DUP4,       ///< Duplicate of operand 4.
  TYPE_max        ///< Sentinel past the last operand type.
};

/// The specification for how to extract and interpret one operand.
struct OperandSpecifier {
  /// Physical encoding of the operand (an \c OperandEncoding value).
  uint8_t encoding;
  /// Semantic type of the operand (an \c OperandType value).
  uint8_t type;
};

static const unsigned X86_MAX_OPERANDS = 6;

/// Decoding mode for the Intel disassembler.
///
/// 16-bit, 32-bit, and 64-bit mode are supported, and represent real mode,
/// IA-32e, and IA-32e in 64-bit mode, respectively.
enum DisassemblerMode {
  MODE_16BIT, ///< 16-bit real mode.
  MODE_32BIT, ///< 32-bit protected / IA-32e mode.
  MODE_64BIT  ///< 64-bit IA-32e mode.
};

} // namespace X86Disassembler
} // namespace llvm

#endif
