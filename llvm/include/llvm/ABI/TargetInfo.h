//===----- TargetInfo.h - Target ABI information ------------------- C++
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Target-specific ABI information and factory functions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ABI_TARGETINFO_H
#define LLVM_ABI_TARGETINFO_H

#include "llvm/ABI/FunctionInfo.h"
#include "llvm/ABI/Types.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <memory>

namespace llvm {
namespace abi {

/// How a record type should be passed as a function argument under the ABI.
enum RecordArgABI {
  /// Pass it using the normal C aggregate rules for the ABI, potentially
  /// introducing extra copies and passing some or all of it in registers.
  RAA_Default = 0,

  /// Pass it on the stack using its defined layout.  The argument must be
  /// evaluated directly into the correct stack position in the arguments area,
  /// and the call machinery must not move it or introduce extra copies.
  RAA_DirectInMemory,

  /// Pass it as a pointer to temporary memory.
  RAA_Indirect
};

/// Flags controlling target-specific ABI compatibility behaviour.
///
/// Construct with the default constructor for the current ABI, or use
/// fromVersion() to get the flags that match a specific Clang version.
struct ABICompatInfo {
  /// Pass __int128 vector types in memory rather than in registers.
  bool PassInt128VectorsInMem : 1;
  /// Return C++ records larger than 128 bits indirectly in memory.
  bool ReturnCXXRecordGreaterThan128InMem : 1;
  /// Classify integer MMX vectors using SSE classes instead of integer.
  bool ClassifyIntegerMMXAsSSE : 1;
  /// Honor AMD64 ABI revision 0.98 classification of X87Up with non-X87 Lo.
  bool HonorsRevision98 : 1;
  /// Match Clang 11 union classification (members do not span the union size).
  bool Clang11Compat : 1;

  /// Construct flags for the current (newest) ABI behaviour.
  ABICompatInfo()
      : PassInt128VectorsInMem(true), ReturnCXXRecordGreaterThan128InMem(true),
        ClassifyIntegerMMXAsSSE(true), HonorsRevision98(true),
        Clang11Compat(true) {}

  /// Return flags matching the ABI emitted by the given Clang major version.
  ///
  /// \param ClangMajor The Clang major version whose ABI behaviour to match.
  /// \return ABI compatibility flags that match the given Clang major version.
  // TODO: fill in per-version flag overrides.
  static ABICompatInfo fromVersion([[maybe_unused]] unsigned ClangMajor) {
    return ABICompatInfo();
  }
};

/// Abstract interface for target-specific ABI classification of functions.
class TargetInfo {
private:
  ABICompatInfo CompatInfo;

public:
  /// Construct a TargetInfo with default ABI compatibility flags.
  TargetInfo() : CompatInfo() {}
  /// Construct a TargetInfo with the given ABI compatibility flags.
  ///
  /// \param Info Compatibility flags that control target-specific ABI quirks.
  explicit TargetInfo(const ABICompatInfo &Info) : CompatInfo(Info) {}

  /// Destroy this TargetInfo.
  virtual ~TargetInfo() = default;

  /// Populate FI with the target's ABI-lowering decisions for each argument
  /// and return value.
  ///
  /// \param FI The function signature whose argument and return infos to fill.
  virtual void computeInfo(FunctionInfo &FI) const = 0;
  /// Return true if \p Ty should be passed by reference under this ABI.
  ///
  /// \param Ty The type to query.
  /// \return True if \p Ty should be passed by reference; false otherwise.
  virtual bool isPassByRef(const Type *Ty) const { return false; }
  /// Return the ABI compatibility flags associated with this target.
  ///
  /// \return The ABI compatibility flags associated with this target.
  const ABICompatInfo &getABICompatInfo() const { return CompatInfo; }

protected:
  /// Return how record type \p RT should be passed as a function argument.
  ///
  /// \param RT The record type to classify.
  /// \return How \p RT should be passed as a function argument under this ABI.
  LLVM_ABI RecordArgABI getRecordArgABI(const RecordType *RT) const;
  /// Return how type \p Ty should be passed when it is a record, else default.
  ///
  /// \param Ty The type to classify; non-records yield RAA_Default.
  /// \return How \p Ty should be passed when it is a record, or RAA_Default.
  LLVM_ABI RecordArgABI getRecordArgABI(const Type *Ty) const;
  /// Return true if integer type \p IT is narrower than int and should be
  /// promoted (sign- or zero-extended) when passed or returned.
  ///
  /// \param IT The integer type to query.
  /// \return True if \p IT is promotable; false otherwise.
  LLVM_ABI bool isPromotableInteger(const IntegerType *IT) const;
  /// Return ArgInfo for passing \p Ty indirectly at its natural alignment.
  ///
  /// \param Ty    The type being passed or returned indirectly.
  /// \param ByVal True if the callee receives a by-value copy through the
  ///              pointer.
  /// \return ArgInfo describing an indirect pass of \p Ty at natural alignment.
  LLVM_ABI ArgInfo getNaturalAlignIndirect(const Type *Ty,
                                           bool ByVal = true) const;
  /// Return true if \p Ty is an aggregate for ABI classification purposes.
  ///
  /// \param Ty The type to query.
  /// \return True if \p Ty is an aggregate for ABI purposes; false otherwise.
  LLVM_ABI bool isAggregateTypeForABI(const Type *Ty) const;

  /// If Ty is a transparent union, return its first field type; otherwise
  /// return Ty unchanged.
  ///
  /// \param Ty The type that may be a transparent union.
  /// \return The first field type if \p Ty is a transparent union; else \p Ty.
  LLVM_ABI const Type *useFirstFieldIfTransparentUnion(const Type *Ty) const;

  /// Apply rules for classifying return types that are common to all targets.
  ///
  /// \param FI The function whose return type may be classified in place.
  /// \return True if the return type was classified and no further target
  ///         rules are needed; false if the target must classify it itself.
  LLVM_ABI bool maybeCommonClassifyReturnType(FunctionInfo &FI) const;
};

/// Create a TargetInfo for the BPF ABI.
///
/// \param TB Type builder used to construct coerced types.
/// \return A TargetInfo that implements the BPF ABI.
LLVM_ABI std::unique_ptr<TargetInfo> createBPFTargetInfo(TypeBuilder &TB);

/// The AVX ABI level for X86 targets.
enum class X86AVXABILevel {
  /// No AVX; use the baseline SSE vector ABI.
  None,
  /// AVX (256-bit) vector ABI.
  AVX,
  /// AVX-512 (512-bit) vector ABI.
  AVX512,
  /// Sentinel equal to the highest AVX ABI level; must remain last.
  Last = AVX512 // must be last
};

/// Create a TargetInfo for the x86-64 System V ABI.
///
/// \param TB               Type builder used to construct coerced types.
/// \param AVXLevel         The AVX vector ABI level to use.
/// \param Has64BitPointers True when the data model uses 64-bit pointers.
/// \param Compat           Compatibility flags for Clang ABI version quirks.
/// \return A TargetInfo that implements the x86-64 System V ABI.
LLVM_ABI std::unique_ptr<TargetInfo>
createX86_64TargetInfo(TypeBuilder &TB, X86AVXABILevel AVXLevel,
                       bool Has64BitPointers, const ABICompatInfo &Compat);

/// The AArch64 procedure-call standard variant to use.
enum class AArch64ABIKind {
  /// ARM Architecture Procedure Call Standard (AAPCS64).
  AAPCS = 0,
  /// Apple Darwin procedure-call standard.
  DarwinPCS,
  /// Windows x64-on-AArch64 calling convention.
  Win64,
  /// Soft-float AAPCS (floating-point values in general registers).
  AAPCSSoft,
};

/// Create a TargetInfo for the AArch64 ABI.
///
/// \param TB   Type builder used to construct coerced types.
/// \param Kind The AArch64 PCS variant to implement.
/// \return A TargetInfo that implements the requested AArch64 ABI variant.
LLVM_ABI std::unique_ptr<TargetInfo>
createAArch64TargetInfo(TypeBuilder &TB, AArch64ABIKind Kind);

} // namespace abi
} // namespace llvm

#endif // LLVM_ABI_TARGETINFO_H
