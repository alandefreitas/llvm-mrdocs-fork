//===-- RISCVTargetParser - Parser for target features ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise hardware features
// for RISC-V CPUs.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_RISCVTARGETPARSER_H
#define LLVM_TARGETPARSER_RISCVTARGETPARSER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class Triple;

/// RISC-V CPU name, feature, and tune-feature parsing helpers.
namespace RISCV {

/// Hardware identification fields matching the RISC-V mvendorid/marchid/mimpid
/// CSRs.
struct CPUModel {
  uint32_t MVendorID; ///< Vendor ID from the mvendorid CSR.
  uint64_t MArchID;   ///< Architecture ID from the marchid CSR.
  uint64_t MImpID;    ///< Implementation ID from the mimpid CSR.

  /// Return true if all three ID fields are non-zero.
  ///
  /// \returns True if MVendorID, MArchID, and MImpID are all non-zero.
  bool isValid() const { return MVendorID != 0 && MArchID != 0 && MImpID != 0; }

  /// Return true if this model has the same IDs as \p Other.
  ///
  /// \param Other Other CPU model to compare against.
  /// \returns True if both models have identical vendor, architecture, and
  ///          implementation IDs.
  bool operator==(const CPUModel &Other) const {
    return MVendorID == Other.MVendorID && MArchID == Other.MArchID &&
           MImpID == Other.MImpID;
  }
};

/// Named RISC-V CPU and its default ISA string, alignment hints, and model IDs.
struct CPUInfo {
  StringLiteral Name;         ///< Canonical CPU name.
  StringLiteral DefaultMarch; ///< Default -march string for this CPU.
  bool FastScalarUnalignedAccess; ///< True if scalar unaligned access is fast.
  bool FastVectorUnalignedAccess; ///< True if vector unaligned access is fast.
  CPUModel Model;             ///< Hardware vendor/arch/implementation IDs.
  /// Return true if this CPU's default march is an RV64 ISA string.
  ///
  /// \returns True if \c DefaultMarch begins with "rv64".
  bool is64Bit() const { return DefaultMarch.starts_with("rv64"); }
};

/// Fatal errors encountered during parsing.
struct ParserError : public ErrorInfo<ParserError, StringError> {
  /// Inherit constructors from the parent error-info type.
  using ErrorInfo<ParserError, StringError>::ErrorInfo;
  /// Construct a ParserError with message \p S and an inconvertible error code.
  ///
  /// \param S Human-readable error message.
  explicit ParserError(const Twine &S)
      : ErrorInfo(S, inconvertibleErrorCode()) {}
  /// RTTI identifier used by ErrorInfo::classID.
  LLVM_ABI static char ID;
};

/// Warnings encountered during parsing.
struct ParserWarning : public ErrorInfo<ParserWarning, StringError> {
  /// Inherit constructors from the parent error-info type.
  using ErrorInfo<ParserWarning, StringError>::ErrorInfo;
  /// Construct a ParserWarning with message \p S and an inconvertible error
  /// code.
  ///
  /// \param S Human-readable warning message.
  explicit ParserWarning(const Twine &S)
      : ErrorInfo(S, inconvertibleErrorCode()) {}
  /// RTTI identifier used by ErrorInfo::classID.
  LLVM_ABI static char ID;
};

// We use 64 bits as the known part in the scalable vector types.
static constexpr unsigned RVVBitsPerBlock = 64;
static constexpr unsigned RVVBytesPerBlock = RVVBitsPerBlock / 8;

/// Append the target-feature strings enabled by CPU \p CPU.
///
/// Features are derived from the CPU's default march string. If \p NeedPlus is
/// true, each feature is prefixed with '+'; otherwise the leading '+' from
/// \c RISCVISAInfo::toFeatures is stripped.
///
/// \param CPU CPU name whose features are requested.
/// \param EnabledFeatures Vector that receives feature strings; cleared first.
/// \param NeedPlus If true, keep a leading '+' on each feature string.
LLVM_ABI void getFeaturesForCPU(StringRef CPU,
                                SmallVectorImpl<std::string> &EnabledFeatures,
                                bool NeedPlus = false);
/// Append every recognized RISC-V tune-feature name to \p TuneFeatures.
///
/// \param TuneFeatures Vector that receives tune-feature names.
LLVM_ABI void getAllTuneFeatures(SmallVectorImpl<StringRef> &TuneFeatures);
/// Append the tune-feature directives configurable for CPU \p CPU.
///
/// \param CPU CPU name whose configurable tune directives are requested.
/// \param Directives Vector that receives allowed directive names.
LLVM_ABI void
getCPUConfigurableTuneFeatures(StringRef CPU,
                               SmallVectorImpl<StringRef> &Directives);
/// Parse the tune feature string with the respective processor. If \p ProcName
/// is empty, directives are not filtered by processor.
///
/// \param ProcName Processor name used to filter allowed directives; empty
///        disables filtering.
/// \param TFString Comma-separated tune-feature directive string.
/// \param TuneFeatures Vector that receives +feature/-feature result strings.
/// \returns Success, or an error describing invalid tune-feature directives.
LLVM_ABI Error
parseTuneFeatureString(StringRef ProcName, StringRef TFString,
                       SmallVectorImpl<std::string> &TuneFeatures);
/// Return true if \p CPU is a recognized RISC-V CPU for the given bitness.
///
/// \param CPU CPU name to validate.
/// \param IsRV64 True to require an RV64 CPU; false for RV32.
/// \returns True if \p CPU is known and matches \p IsRV64.
LLVM_ABI bool parseCPU(StringRef CPU, bool IsRV64);
/// Return true if \p CPU is a recognized RISC-V tune CPU for the given bitness.
///
/// Falls back to \c parseCPU when \p CPU is not a dedicated tune-CPU name.
///
/// \param CPU Tune CPU name to validate.
/// \param IsRV64 True to require an RV64 CPU; false for RV32.
/// \returns True if \p CPU is a known tune CPU or a matching base CPU.
LLVM_ABI bool parseTuneCPU(StringRef CPU, bool IsRV64);
/// Return the default -march string for CPU \p CPU.
///
/// \param CPU CPU name whose default march is requested.
/// \returns Default march string, or an empty StringRef if \p CPU is unknown.
LLVM_ABI StringRef getMArchFromMcpu(StringRef CPU);
/// Append every valid RISC-V CPU name for the given bitness to \p Values.
///
/// \param Values Vector that receives CPU names.
/// \param IsRV64 True to list RV64 CPUs; false for RV32.
LLVM_ABI void fillValidCPUArchList(SmallVectorImpl<StringRef> &Values,
                                   bool IsRV64);
/// Append every valid RISC-V tune CPU name for the given bitness to \p Values.
///
/// Includes base CPU names and dedicated tune-CPU aliases.
///
/// \param Values Vector that receives tune CPU names.
/// \param IsRV64 True to list RV64 tune CPUs; false for RV32.
LLVM_ABI void fillValidTuneCPUArchList(SmallVectorImpl<StringRef> &Values,
                                       bool IsRV64);
/// Return true if CPU \p CPU treats scalar unaligned access as fast.
///
/// \param CPU CPU name to query.
/// \returns True if the CPU is known and has fast scalar unaligned access.
LLVM_ABI bool hasFastScalarUnalignedAccess(StringRef CPU);
/// Return true if CPU \p CPU treats vector unaligned access as fast.
///
/// \param CPU CPU name to query.
/// \returns True if the CPU is known and has fast vector unaligned access.
LLVM_ABI bool hasFastVectorUnalignedAccess(StringRef CPU);
/// Return true if CPU \p CPU has a valid non-zero hardware model.
///
/// \param CPU CPU name to query.
/// \returns True if \c getCPUModel(\p CPU) reports a valid model.
LLVM_ABI bool hasValidCPUModel(StringRef CPU);
/// Return the hardware model IDs for CPU \p CPU.
///
/// \param CPU CPU name to query.
/// \returns The CPU's \c CPUModel, or an all-zero model if \p CPU is unknown.
LLVM_ABI CPUModel getCPUModel(StringRef CPU);
/// Return the CPU name that matches hardware model \p Model.
///
/// \param Model Hardware vendor/arch/implementation IDs to look up.
/// \returns Matching CPU name, or an empty StringRef if none or invalid.
LLVM_ABI StringRef getCPUNameFromCPUModel(const CPUModel &Model);

} // namespace RISCV

/// RISC-V control-flow integrity (Zicfilp) label-scheme helpers.
namespace RISCVCFI {
/// Zicfilp control-flow branch label scheme kinds.
enum class ZicfilpLabelSchemeKind {
  Invalid,   ///< Unrecognized or unsupported label scheme.
  Unlabeled, ///< Unlabeled scheme ("unlabeled").
  FuncSig,   ///< Function-signature scheme ("func-sig").
};

// See clang::getCFBranchLabelSchemeFlagVal() for possible CFBranchLabelScheme.
/// Map a CF branch-label scheme name to a \c ZicfilpLabelSchemeKind.
///
/// \param CFBranchLabelScheme Scheme name such as "unlabeled" or "func-sig".
/// \returns Matching kind, or \c ZicfilpLabelSchemeKind::Invalid if unknown.
inline ZicfilpLabelSchemeKind
getZicfilpLabelScheme(const StringRef CFBranchLabelScheme) {
  return StringSwitch<ZicfilpLabelSchemeKind>(CFBranchLabelScheme)
      .Case("unlabeled", ZicfilpLabelSchemeKind::Unlabeled)
      .Case("func-sig", ZicfilpLabelSchemeKind::FuncSig)
      .Default(ZicfilpLabelSchemeKind::Invalid);
}
} // namespace RISCVCFI

/// RISC-V vector VTYPE encoding, decoding, and printing helpers.
namespace RISCVVType {
/// Vector register group multiplier (LMUL) encodings for VTYPE.
enum VLMUL : uint8_t {
  LMUL_1 = 0,     ///< LMUL = 1.
  LMUL_2,         ///< LMUL = 2.
  LMUL_4,         ///< LMUL = 4.
  LMUL_8,         ///< LMUL = 8.
  LMUL_RESERVED,  ///< Reserved LMUL encoding.
  LMUL_F8,        ///< Fractional LMUL = 1/8.
  LMUL_F4,        ///< Fractional LMUL = 1/4.
  LMUL_F2         ///< Fractional LMUL = 1/2.
};

/// VTYPE tail and mask policy bit values used with vsetvli.
enum {
  TAIL_UNDISTURBED_MASK_UNDISTURBED = 0, ///< Tail and mask undisturbed (tu, mu).
  TAIL_AGNOSTIC = 1,                     ///< Tail agnostic (ta) bit.
  MASK_AGNOSTIC = 2,                     ///< Mask agnostic (ma) bit.
};

// Is this a SEW value that can be encoded into the VTYPE format.
inline static bool isValidSEW(unsigned SEW) {
  return isPowerOf2_32(SEW) && SEW >= 8 && SEW <= 64;
}

// Is this a LMUL value that can be encoded into the VTYPE format.
inline static bool isValidLMUL(unsigned LMUL, bool Fractional) {
  return isPowerOf2_32(LMUL) && LMUL <= 8 && (!Fractional || LMUL != 1);
}

/// Encode VTYPE fields into the binary format used by vsetvli.
///
/// Bits | Name       | Description
/// -----+------------+------------------------------------------------
/// 8    | altfmt     | Alternative format for bf16/ofp8
/// 7    | vma        | Vector mask agnostic
/// 6    | vta        | Vector tail agnostic
/// 5:3  | vsew[2:0]  | Standard element width (SEW) setting
/// 2:0  | vlmul[2:0] | Vector register group multiplier (LMUL) setting
///
/// \param VLMUL Encoded LMUL value.
/// \param SEW Standard element width in bits.
/// \param TailAgnostic True to set the tail-agnostic (vta) bit.
/// \param MaskAgnostic True to set the mask-agnostic (vma) bit.
/// \param AltFmt True to set the alternative-format bit.
/// \returns Encoded VTYPE integer.
LLVM_ABI unsigned encodeVTYPE(VLMUL VLMUL, unsigned SEW, bool TailAgnostic,
                              bool MaskAgnostic, bool AltFmt = false);

/// Encode an XSfmm matrix VTYPE value from SEW, widen factor, and altfmt.
///
/// \param SEW Standard element width in bits.
/// \param Widen Widen factor; must be 1, 2, or 4.
/// \param AltFmt True to set the alternative-format bit.
/// \returns Encoded XSfmm VTYPE integer.
LLVM_ABI unsigned encodeXSfmmVType(unsigned SEW, unsigned Widen, bool AltFmt);

/// Integer Matrix Extension (IME) VTYPE field helpers.
namespace IME {
inline static bool isValidLambda(unsigned Lambda) {
  return Lambda == 0 || (isPowerOf2_32(Lambda) && Lambda <= 64);
}

/// Encode an IME lambda value into its three-bit VTYPE field encoding.
///
/// \param Lambda Lambda value; 0 or a power of two at most 64.
/// \returns Encoded lambda field (0 for lambda 0, otherwise log2(lambda)+1).
LLVM_ABI unsigned encodeLambda(unsigned Lambda);

/// Decode an IME lambda field encoding into a lambda value.
///
/// \param Encoding Three-bit lambda field encoding.
/// \returns Decoded lambda, or nullopt when \p Encoding is 0.
LLVM_ABI std::optional<unsigned> decodeLambda(unsigned Encoding);

/// Return a mask covering the IME-related fields in a VTYPE value.
///
/// \param XLen XLEN in bits; must be 32 or 64.
/// \returns Bitmask of the lambda, altfmt A/B, and block-size fields.
LLVM_ABI uint64_t getVTypeFieldsMask(unsigned XLen);

/// Encode IME VTYPE fields for the given XLEN.
///
/// \param XLen XLEN in bits; must be 32 or 64.
/// \param Lambda Lambda value to encode.
/// \param AltFmtA True to set alternative-format A.
/// \param AltFmtB True to set alternative-format B.
/// \param BlockSize16 True to set the 16-bit block-size flag.
/// \returns Encoded IME field bits (other VTYPE bits clear).
LLVM_ABI uint64_t encodeVTypeFields(unsigned XLen, unsigned Lambda,
                                    bool AltFmtA, bool AltFmtB,
                                    bool BlockSize16);

/// Replace the IME fields in \p VType with newly encoded values.
///
/// \param VType Existing VTYPE value whose non-IME bits are preserved.
/// \param XLen XLEN in bits; must be 32 or 64.
/// \param Lambda Lambda value to encode.
/// \param AltFmtA True to set alternative-format A.
/// \param AltFmtB True to set alternative-format B.
/// \param BlockSize16 True to set the 16-bit block-size flag.
/// \returns \p VType with IME fields updated.
LLVM_ABI uint64_t addVTypeFields(uint64_t VType, unsigned XLen, unsigned Lambda,
                                 bool AltFmtA, bool AltFmtB, bool BlockSize16);

/// Extract the raw three-bit IME lambda encoding from \p VType.
///
/// \param VType VTYPE value to inspect.
/// \param XLen XLEN in bits; must be 32 or 64.
/// \returns Raw lambda field encoding.
LLVM_ABI unsigned getLambdaEncoding(uint64_t VType, unsigned XLen);

/// Extract and decode the IME lambda value from \p VType.
///
/// \param VType VTYPE value to inspect.
/// \param XLen XLEN in bits; must be 32 or 64.
/// \returns Decoded lambda, or nullopt when the field encoding is 0.
LLVM_ABI std::optional<unsigned> getLambda(uint64_t VType, unsigned XLen);

/// Return true if the IME alternative-format A bit is set in \p VType.
///
/// \param VType VTYPE value to inspect.
/// \param XLen XLEN in bits; must be 32 or 64.
/// \returns True if altfmt A is set.
LLVM_ABI bool isAltFmtA(uint64_t VType, unsigned XLen);

/// Return true if the IME alternative-format B bit is set in \p VType.
///
/// \param VType VTYPE value to inspect.
/// \param XLen XLEN in bits; must be 32 or 64.
/// \returns True if altfmt B is set.
LLVM_ABI bool isAltFmtB(uint64_t VType, unsigned XLen);

/// Return true if the IME 16-bit block-size flag is set in \p VType.
///
/// \param VType VTYPE value to inspect.
/// \param XLen XLEN in bits; must be 32 or 64.
/// \returns True if block size 16 is set.
LLVM_ABI bool isBlockSize16(uint64_t VType, unsigned XLen);
} // namespace IME

inline static VLMUL getVLMUL(unsigned VType) {
  unsigned VLMul = VType & 0x7;
  return static_cast<VLMUL>(VLMul);
}

/// Decode \p VLMul into an LMUL magnitude and fractional indicator.
///
/// \param VLMul Encoded LMUL value.
/// \returns Pair of (LMUL magnitude in {1,2,4,8}, true if fractional).
LLVM_ABI std::pair<unsigned, bool> decodeVLMUL(VLMUL VLMul);

inline static VLMUL encodeLMUL(unsigned LMUL, bool Fractional) {
  assert(isValidLMUL(LMUL, Fractional) && "Unsupported LMUL");
  unsigned LmulLog2 = Log2_32(LMUL);
  return static_cast<VLMUL>(Fractional ? 8 - LmulLog2 : LmulLog2);
}

inline static unsigned decodeVSEW(unsigned VSEW) {
  assert(VSEW < 8 && "Unexpected VSEW value");
  return 1 << (VSEW + 3);
}

inline static unsigned encodeSEW(unsigned SEW) {
  assert(isValidSEW(SEW) && "Unexpected SEW value");
  return Log2_32(SEW) - 3;
}

inline static unsigned getSEW(unsigned VType) {
  unsigned VSEW = (VType >> 3) & 0x7;
  return decodeVSEW(VSEW);
}

inline static unsigned decodeTWiden(unsigned TWiden) {
  assert((TWiden == 1 || TWiden == 2 || TWiden == 3) &&
         "Unexpected TWiden value");
  return 1 << (TWiden - 1);
}

inline static bool hasXSfmmWiden(unsigned VType) {
  unsigned TWiden = (VType >> 9) & 0x3;
  return TWiden != 0;
}

inline static unsigned getXSfmmWiden(unsigned VType) {
  unsigned TWiden = (VType >> 9) & 0x3;
  assert(TWiden != 0 && "Invalid widen value");
  return 1 << (TWiden - 1);
}

inline static bool isTailAgnostic(unsigned VType) { return VType & 0x40; }

inline static bool isMaskAgnostic(unsigned VType) { return VType & 0x80; }

inline static bool isAltFmt(unsigned VType) { return VType & 0x100; }

inline static bool isValidVType(unsigned VType) {
  return getSEW(VType) <= 64 && getVLMUL(VType) != LMUL_RESERVED &&
         (!isAltFmt(VType) || getSEW(VType) < 32);
}

static inline bool isValidXSfmmVType(unsigned VTypeI) {
  return (VTypeI & ~0x738) == 0 && RISCVVType::hasXSfmmWiden(VTypeI) &&
         RISCVVType::getSEW(VTypeI) * RISCVVType::getXSfmmWiden(VTypeI) <= 64 &&
         isValidVType(VTypeI);
}

/// Print a human-readable VTYPE string such as "e32, m1, ta, ma" to \p OS.
///
/// \param VType Encoded VTYPE value to print.
/// \param OS Output stream to receive the formatted text.
LLVM_ABI void printVType(unsigned VType, raw_ostream &OS);

/// Print a human-readable XSfmm VTYPE string such as "e32, w2" to \p OS.
///
/// \param VType Encoded XSfmm VTYPE value to print.
/// \param OS Output stream to receive the formatted text.
LLVM_ABI void printXSfmmVType(unsigned VType, raw_ostream &OS);

/// Return the SEW/LMUL ratio for the given SEW and LMUL.
///
/// \param SEW Standard element width in bits.
/// \param VLMul Encoded LMUL value.
/// \returns Ratio of SEW to LMUL in a fixed-point representation.
LLVM_ABI unsigned getSEWLMULRatio(unsigned SEW, VLMUL VLMul);

/// Return an LMUL that preserves SEW/LMUL ratio \p Ratio for element width
/// \p EEW.
///
/// \param Ratio Existing SEW/LMUL ratio from \c getSEWLMULRatio.
/// \param EEW Effective element width in bits.
/// \returns Matching LMUL encoding, or nullopt if none is valid.
LLVM_ABI std::optional<VLMUL> getSameRatioLMUL(unsigned Ratio, unsigned EEW);
} // namespace RISCVVType

} // namespace llvm

#endif
