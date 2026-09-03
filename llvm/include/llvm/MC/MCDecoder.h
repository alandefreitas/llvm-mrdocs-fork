//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Disassembler decoder helper functions.
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCDECODER_H
#define LLVM_MC_MCDECODER_H

#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/Support/MathExtras.h"
#include <bitset>
#include <cassert>

namespace llvm {
/// Helpers and opcodes for MC disassembler instruction decoding.
namespace MCD {

/// Propagate SoftFail status by combining \p Out with \p In.
///
/// Returns false if the combined status is Fail; callers are expected to
/// early-exit in that condition. (Note, the '&' operator is correct to
/// propagate the values of this enum; see comment on 'enum DecodeStatus'.)
///
/// \param Out - Accumulated decode status updated with \p In.
/// \param In - New decode status to combine into \p Out.
/// \return False if the resulting status is Fail; true otherwise.
inline bool Check(MCDisassembler::DecodeStatus &Out,
                  MCDisassembler::DecodeStatus In) {
  Out = static_cast<MCDisassembler::DecodeStatus>(Out & In);
  return Out != MCDisassembler::Fail;
}

/// Extract a span of bits from an integral instruction word.
///
/// \param Insn - Instruction bits to extract from.
/// \param StartBit - Index of the least significant bit of the field.
/// \param NumBits - Width of the field in bits.
/// \return The extracted field as an integer of type \p IntType.
template <typename IntType>
#if defined(_MSC_VER) && !defined(__clang__)
__declspec(noinline)
#endif
inline std::enable_if_t<std::is_integral_v<IntType>, IntType>
fieldFromInstruction(const IntType &Insn, unsigned StartBit, unsigned NumBits) {
  assert(StartBit + NumBits <= 64 && "Cannot support >64-bit extractions!");
  assert(StartBit + NumBits <= (sizeof(IntType) * 8) &&
         "Instruction field out of bounds!");
  const IntType Mask = maskTrailingOnes<IntType>(NumBits);
  return (Insn >> StartBit) & Mask;
}

/// Extract a span of bits from a non-integral instruction representation.
///
/// \param Insn - Instruction object supporting \c extractBitsAsZExtValue.
/// \param StartBit - Index of the least significant bit of the field.
/// \param NumBits - Width of the field in bits.
/// \return The extracted field as a zero-extended 64-bit value.
template <typename InsnType>
inline std::enable_if_t<!std::is_integral_v<InsnType>, uint64_t>
fieldFromInstruction(const InsnType &Insn, unsigned StartBit,
                     unsigned NumBits) {
  return Insn.extractBitsAsZExtValue(NumBits, StartBit);
}

/// Extract a span of bits from a \c std::bitset instruction encoding.
///
/// \param Insn - Bitset holding the instruction bits.
/// \param StartBit - Index of the least significant bit of the field.
/// \param NumBits - Width of the field in bits.
/// \return The extracted field as a zero-extended 64-bit value.
template <size_t N>
uint64_t fieldFromInstruction(const std::bitset<N> &Insn, unsigned StartBit,
                              unsigned NumBits) {
  assert(StartBit + NumBits <= N && "Instruction field out of bounds!");
  assert(NumBits <= 64 && "Cannot support >64-bit extractions!");
  const std::bitset<N> Mask(maskTrailingOnes<uint64_t>(NumBits));
  return ((Insn >> StartBit) & Mask).to_ullong();
}

} // namespace MCD
} // namespace llvm

#endif // LLVM_MC_MCDECODER_H
