//===- RuntimeLibcalls.h - Interface for runtime libcalls -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a common interface to work with library calls into a
// runtime that may be emitted by a given backend.
//
// FIXME: This should probably move to Analysis
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_RUNTIME_LIBCALLS_H
#define LLVM_IR_RUNTIME_LIBCALLS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Bitset.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/StringTable.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/SystemLibraries.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

/// Include RuntimeLibcalls.inc to define RTLIB::Libcall and LibcallImpl enums.
///
/// TableGen will produce 2 enums, RTLIB::Libcall and
/// RTLIB::LibcallImpl. RTLIB::Libcall describes abstract functionality the
/// compiler may choose to access, RTLIB::LibcallImpl describes a particular ABI
/// implementation, which includes a name and type signature.
#define GET_RUNTIME_LIBCALL_ENUM
#include "llvm/IR/RuntimeLibcalls.inc"

namespace llvm {

/// Specialization marking RTLIB::Libcall as safe to iterate with enum_seq.
template <> struct enum_iteration_traits<RTLIB::Libcall> {
  /// When true, RTLIB::Libcall may be iterated by enum_seq without the force tag.
  static constexpr bool is_iterable = true;
};

/// Specialization marking RTLIB::LibcallImpl as safe to iterate with enum_seq.
template <> struct enum_iteration_traits<RTLIB::LibcallImpl> {
  /// When true, RTLIB::LibcallImpl may be iterated by enum_seq without the force
  /// tag.
  static constexpr bool is_iterable = true;
};

class LibcallLoweringInfo;
class Type;

namespace Intrinsic {
typedef unsigned ID;
}

namespace RTLIB {

// Return an iterator over all Libcall values.
static inline auto libcalls() {
  return enum_seq(static_cast<RTLIB::Libcall>(0), RTLIB::UNKNOWN_LIBCALL);
}

static inline auto libcall_impls() {
  return enum_seq(static_cast<RTLIB::LibcallImpl>(1),
                  static_cast<RTLIB::LibcallImpl>(RTLIB::NumLibcallImpls));
}

/// Manage a bitset representing the list of available libcalls for a module.
class LibcallImplBitset : public Bitset<RTLIB::NumLibcallImpls> {
public:
  /// Construct an empty bitset of available libcall implementations.
  constexpr LibcallImplBitset() = default;
  /// Construct from a little-endian array of 64-bit words.
  /// @param Src Word array covering RTLIB::NumLibcallImpls bits.
  constexpr LibcallImplBitset(
      const std::array<uint64_t, (RTLIB::NumLibcallImpls + 63) / 64> &Src)
      : Bitset(Src) {}
};

/// A simple container for information about the supported runtime calls.
struct RuntimeLibcallsInfo {
private:
  /// Bitset of libcalls a module may emit a call to.
  LibcallImplBitset AvailableLibcallImpls;

public:
  friend class llvm::LibcallLoweringInfo;

  /// Construct with no available libcall implementations.
  RuntimeLibcallsInfo() = default;

  /// Construct available libcalls for target \p TT and the given ABI options.
  /// @param TT Target triple that selects the default libcall set.
  /// @param ExceptionModel Exception-handling model for the target.
  /// @param FloatABI Floating-point ABI used by the target.
  /// @param EABIVersion EABI version when applicable.
  /// @param ABIName Optional ABI name string for the target.
  /// @param VecLib Vector math library whose calls should be available.
  LLVM_ABI explicit RuntimeLibcallsInfo(
      const Triple &TT,
      ExceptionHandling ExceptionModel = ExceptionHandling::None,
      FloatABI::ABIType FloatABI = FloatABI::Default,
      EABI EABIVersion = EABI::Default, StringRef ABIName = "",
      VectorLibrary VecLib = VectorLibrary::NoLibrary);

  /// Construct available libcalls from module \p M and remaining ABI options.
  ///
  /// FIXME: The floating-point ABI is read from the "float-abi" module flag, but
  /// the ExceptionModel/EABIVersion/ABIName/VecLib parameters are still
  /// TargetOptions values that are not yet represented in the IR. Delete these
  /// parameters (and build everything from the Module) once those fields are
  /// migrated to module flags.
  /// @param M Module providing target triple and float-abi flag.
  /// @param ExceptionModel Exception-handling model for the target.
  /// @param EABIVersion EABI version when applicable.
  /// @param ABIName Optional ABI name string for the target.
  /// @param VecLib Vector math library whose calls should be available.
  LLVM_ABI explicit RuntimeLibcallsInfo(
      const Module &M,
      ExceptionHandling ExceptionModel = ExceptionHandling::None,
      EABI EABIVersion = EABI::Default, StringRef ABIName = "",
      VectorLibrary VecLib = VectorLibrary::NoLibrary);

  /// Invalidate cached runtime-libcall info when analyses are not preserved.
  /// @param M Module whose analyses may have changed.
  /// @param PA Set of analyses preserved by the last transformation.
  /// @param Inv Invalidator used to invalidate dependent analyses.
  /// @returns true if this analysis result should be discarded.
  LLVM_ABI bool invalidate(Module &M, const PreservedAnalyses &PA,
                           ModuleAnalysisManager::Invalidator &Inv);

  /// Get the libcall routine name for the specified libcall implementation.
  /// @param CallImpl Concrete libcall implementation whose name is requested.
  /// @returns Name of \p CallImpl, or an empty StringRef if unsupported.
  static StringRef getLibcallImplName(RTLIB::LibcallImpl CallImpl) {
    if (CallImpl == RTLIB::Unsupported)
      return StringRef();
    return StringRef(RuntimeLibcallImplNameTable.getCString(
                         RuntimeLibcallNameOffsetTable[CallImpl]),
                     RuntimeLibcallNameSizeTable[CallImpl]);
  }

  /// Set the CallingConv that should be used for the specified libcall
  /// implementation.
  /// @param Call Libcall implementation to update.
  /// @param CC Calling convention to use for \p Call.
  void setLibcallImplCallingConv(RTLIB::LibcallImpl Call, CallingConv::ID CC) {
    LibcallImplCallingConvs[Call] = CC;
  }

  /// Get the CallingConv that should be used for the specified libcall.
  /// @param Call Libcall implementation whose calling convention is requested.
  /// @returns Calling convention to use when emitting \p Call.
  CallingConv::ID getLibcallImplCallingConv(RTLIB::LibcallImpl Call) const {
    return LibcallImplCallingConvs[Call];
  }

  /// Return the libcall provided by \p Impl.
  /// @param Impl Concrete libcall implementation to map back to a Libcall.
  /// @returns Abstract Libcall kind that \p Impl implements.
  static RTLIB::Libcall getLibcallFromImpl(RTLIB::LibcallImpl Impl) {
    return ImplToLibcall[Impl];
  }

  /// Return the runtime libcall that the floating-point math intrinsic \p ID
  /// may be lowered to, or RTLIB::UNKNOWN_LIBCALL if there is no such mapping.
  ///
  /// \p FTy must be the intrinsic's call signature.
  /// @param ID Intrinsic identifier to look up.
  /// @param FTy Function type of the intrinsic call.
  /// @returns Matching Libcall, or RTLIB::UNKNOWN_LIBCALL if none.
  LLVM_ABI static RTLIB::Libcall getLibcallForIntrinsic(Intrinsic::ID ID,
                                                        FunctionType *FTy);

  /// Return the number of libcall implementations available for this module.
  /// @returns Count of available libcall implementations for this module.
  unsigned getNumAvailableLibcallImpls() const {
    return AvailableLibcallImpls.count();
  }

  /// Return true if libcall implementation \p Impl may be emitted for this
  /// module.
  /// @param Impl Libcall implementation to test.
  /// @returns true if \p Impl is available for this module.
  bool isAvailable(RTLIB::LibcallImpl Impl) const {
    return AvailableLibcallImpls.test(Impl);
  }

  /// Mark libcall implementation \p Impl as available for this module.
  /// @param Impl Libcall implementation to enable.
  void setAvailable(RTLIB::LibcallImpl Impl) {
    AvailableLibcallImpls.set(Impl);
  }

  /// Look up all known libcall implementations that use the function name \p
  /// Name.
  ///
  /// Check if a function name is a recognized runtime call of any kind. This
  /// does not consider if this call is available for any current compilation,
  /// just that it is a known call somewhere. This returns the set of all
  /// LibcallImpls which match the name; multiple implementations with the same
  /// name may exist but differ in interpretation based on the target context.
  ///
  /// Generated by tablegen.
  /// @param Name Function name to look up among known libcall implementations.
  /// @returns Range of LibcallImpl values whose names match \p Name.
  static inline iota_range<RTLIB::LibcallImpl>
  lookupLibcallImplName(StringRef Name){
  // Inlining the early exit on the string name appears to be worthwhile when
  // querying a real set of symbols
#define GET_LOOKUP_LIBCALL_IMPL_NAME_BODY
#include "llvm/IR/RuntimeLibcalls.inc"
  }

  /// Check if this is valid libcall for the current module, otherwise
  /// RTLIB::Unsupported.
  /// @param FuncName Function name to resolve to an available LibcallImpl.
  /// @returns Available LibcallImpl for \p FuncName, or RTLIB::Unsupported.
  RTLIB::LibcallImpl getSupportedLibcallImpl(StringRef FuncName) const {
    for (RTLIB::LibcallImpl Impl : lookupLibcallImplName(FuncName)) {
      if (isAvailable(Impl))
        return Impl;
    }

    return RTLIB::Unsupported;
  }

  /// Return the function type and attributes for \p LibcallImpl on target \p
  /// TT.
  ///
  /// If the function has incomplete type information, return nullptr for the
  /// function type.
  /// @param Ctx LLVM context used to build types and attributes.
  /// @param TT Target triple that selects ABI details of the libcall.
  /// @param DL Data layout used when constructing pointer and related types.
  /// @param LibcallImpl Concrete libcall implementation to describe.
  /// @returns Pair of function type (or nullptr) and attribute list for
  /// \p LibcallImpl.
  LLVM_ABI std::pair<FunctionType *, AttributeList>
  getFunctionTy(LLVMContext &Ctx, const Triple &TT, const DataLayout &DL,
                RTLIB::LibcallImpl LibcallImpl) const;

  /// Returns true if the function has a vector mask argument, which is assumed
  /// to be the last argument.
  /// @param Impl Libcall implementation to inspect for a trailing mask
  /// argument.
  /// @returns true if \p Impl has a trailing vector mask argument.
  LLVM_ABI static bool hasVectorMaskArgument(RTLIB::LibcallImpl Impl);

private:
  LLVM_ABI static iota_range<RTLIB::LibcallImpl>
  lookupLibcallImplNameImpl(StringRef Name);

  static_assert(static_cast<int>(CallingConv::C) == 0,
                "default calling conv should be encoded as 0");

  /// Stores the CallingConv that should be used for each libcall
  /// implementation.;
  CallingConv::ID LibcallImplCallingConvs[RTLIB::NumLibcallImpls] = {};

  /// Names of concrete implementations of runtime calls. e.g. __ashlsi3 for
  /// SHL_I32
  LLVM_ABI static const char RuntimeLibcallImplNameTableStorage[];
  LLVM_ABI static const StringTable RuntimeLibcallImplNameTable;
  LLVM_ABI static const uint16_t RuntimeLibcallNameOffsetTable[];
  LLVM_ABI static const uint8_t RuntimeLibcallNameSizeTable[];

  /// Map from a concrete LibcallImpl implementation to its RTLIB::Libcall kind.
  LLVM_ABI static const RTLIB::Libcall ImplToLibcall[RTLIB::NumLibcallImpls];

  /// Utility function for tablegenerated lookup function. Return a range of
  /// enum values that apply for the function name at \p NameOffsetEntry with
  /// the value \p StrOffset.
  static inline iota_range<RTLIB::LibcallImpl>
  libcallImplNameHit(uint16_t NameOffsetEntry, uint16_t StrOffset);

  static bool darwinHasSinCosStret(const Triple &TT) {
    if (!TT.isOSDarwin())
      return false;

    // Don't bother with 32 bit x86.
    if (TT.getArch() == Triple::x86)
      return false;
    // Macos < 10.9 has no sincos_stret.
    if (TT.isMacOSX())
      return !TT.isMacOSXVersionLT(10, 9) && TT.isArch64Bit();
    // iOS < 7.0 has no sincos_stret.
    if (TT.isiOS())
      return !TT.isOSVersionLT(7, 0);
    // Any other darwin such as WatchOS/TvOS is new enough.
    return true;
  }

  LLVM_READONLY
  static bool isAAPCS_ABI(const Triple &TT, StringRef ABIName);

  /// Generated by tablegen.
  void setTargetRuntimeLibcallSets(const Triple &TT,
                                   ExceptionHandling ExceptionModel,
                                   FloatABI::ABIType FloatABI, EABI ABIType,
                                   StringRef ABIName,
                                   LongDoubleFormat LongDoubleFormat);

  /// Set default libcall names. If a target wants to opt-out of a libcall it
  /// should be placed here.
  LLVM_ABI void initLibcalls(const Triple &TT, ExceptionHandling ExceptionModel,
                             FloatABI::ABIType FloatABI, EABI ABIType,
                             StringRef ABIName,
                             LongDoubleFormat LongDoubleFormat);
};

} // namespace RTLIB

} // namespace llvm

#endif // LLVM_IR_RUNTIME_LIBCALLS_H
