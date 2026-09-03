//===- LibcallLoweringInfo.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tracks which library function implementations to use for depending on a
// calling context (e.g., which library function a particular subtarget should
// use).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LIBCALLLOWERINGINFO_H
#define LLVM_ANALYSIS_LIBCALLLOWERINGINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Analysis/RuntimeLibcallInfo.h"
#include "llvm/IR/RuntimeLibcalls.h"
#include "llvm/Pass.h"

namespace llvm {

/// Tracks which library functions to use for a particular subtarget or
/// function.
class LibcallLoweringInfo {
private:
  const RTLIB::RuntimeLibcallsInfo &RTLCI;
  /// Stores the implementation choice for each libcall.
  RTLIB::LibcallImpl LibcallImpls[RTLIB::UNKNOWN_LIBCALL + 1] = {
      RTLIB::Unsupported};

public:
  /// Callback applying the caller's context-specific (e.g. subtarget) libcall
  /// rules on top of the module-level defaults.
  using ApplyContextRulesFn = function_ref<void(LibcallLoweringInfo &)>;

  /// Construct libcall lowering info from module-level runtime libcall info.
  ///
  /// Seeds the default implementation for every available libcall from the
  /// module-level \p RTLCI, then applies the optional \p ApplyContextRules
  /// callback for the caller's context-specific libcall rules.
  /// @param RTLCI Module-level runtime libcall availability information.
  /// @param ApplyContextRules Optional callback applying context-specific
  /// rules.
  LLVM_ABI LibcallLoweringInfo(const RTLIB::RuntimeLibcallsInfo &RTLCI,
                               ApplyContextRulesFn ApplyContextRules = {});

  /// Return the module-level runtime libcall information used by this lowering.
  /// @return Module-level runtime libcall information used by this lowering.
  const RTLIB::RuntimeLibcallsInfo &getRuntimeLibcallsInfo() const {
    return RTLCI;
  }

  /// Get the libcall routine name for the specified libcall.
  /// @param Call Libcall whose selected implementation name is requested.
  /// @return Null-terminated name of the selected implementation for \p Call.
  // FIXME: This should be removed. Only LibcallImpl should have a name.
  const char *getLibcallName(RTLIB::Libcall Call) const {
    // FIXME: Return StringRef
    return RTLIB::RuntimeLibcallsInfo::getLibcallImplName(LibcallImpls[Call])
        .data();
  }

  /// Return the lowering's selection of implementation call for \p Call.
  /// @param Call Libcall whose selected implementation is requested.
  /// @return Selected implementation for \p Call.
  RTLIB::LibcallImpl getLibcallImpl(RTLIB::Libcall Call) const {
    return LibcallImpls[Call];
  }

  /// Remap the default libcall routine name for the specified libcall.
  /// @param Call Libcall whose implementation selection is updated.
  /// @param Impl Implementation to select for \p Call.
  void setLibcallImpl(RTLIB::Libcall Call, RTLIB::LibcallImpl Impl) {
    LibcallImpls[Call] = Impl;
  }

  /// Get the CallingConv that should be used for the specified libcall.
  /// @param Call Libcall whose selected implementation calling convention is
  /// requested.
  /// @return Calling convention for the selected implementation of \p Call.
  // FIXME: Remove this wrapper in favor of directly using
  // getLibcallImplCallingConv
  CallingConv::ID getLibcallCallingConv(RTLIB::Libcall Call) const {
    return RTLCI.LibcallImplCallingConvs[LibcallImpls[Call]];
  }

  /// Get the CallingConv that should be used for the specified libcall.
  /// @param Call Libcall implementation whose calling convention is requested.
  /// @return Calling convention for the specified libcall implementation.
  CallingConv::ID getLibcallImplCallingConv(RTLIB::LibcallImpl Call) const {
    return RTLCI.LibcallImplCallingConvs[Call];
  }

  /// Return a function impl compatible with RTLIB::MEMCPY, or
  /// RTLIB::Unsupported if fully unsupported.
  /// @return A memcpy-compatible libcall implementation, falling back to
  /// memmove when memcpy is unsupported, or RTLIB::Unsupported if neither is
  /// available.
  RTLIB::LibcallImpl getMemcpyImpl() const {
    RTLIB::LibcallImpl Memcpy = getLibcallImpl(RTLIB::MEMCPY);
    if (Memcpy == RTLIB::Unsupported) {
      // Fallback to memmove if memcpy isn't available.
      return getLibcallImpl(RTLIB::MEMMOVE);
    }

    return Memcpy;
  }
};

/// Records a mapping from an opaque lowering context to its
/// LibcallLoweringInfo.
///
/// The context is identified by an opaque key (which should be
/// TargetSubtargetInfo*)
class ModuleLibcallLoweringInfo {
private:
  using LibcallLoweringMap = DenseMap<const void *, LibcallLoweringInfo>;
  mutable LibcallLoweringMap LoweringMap;
  const RTLIB::RuntimeLibcallsInfo *RTLCI = nullptr;

public:
  /// Default-construct an uninitialized module libcall lowering map.
  ModuleLibcallLoweringInfo() = default;
  /// Construct a module libcall lowering map backed by \p RTLCI.
  /// @param RTLCI Module-level runtime libcall availability information.
  ModuleLibcallLoweringInfo(RTLIB::RuntimeLibcallsInfo &RTLCI)
      : RTLCI(&RTLCI) {}

  /// Initialize this map with module-level runtime libcall info \p RT.
  /// @param RT Module-level runtime libcall availability information.
  void init(const RTLIB::RuntimeLibcallsInfo *RT) { RTLCI = RT; }

  /// Clear the stored runtime libcall info and all cached lowerings.
  void clear() {
    RTLCI = nullptr;
    LoweringMap.clear();
  }

  /// Return true if this map has been initialized with runtime libcall info.
  /// @return True if this map has been initialized with runtime libcall info.
  operator bool() const { return RTLCI != nullptr; }

  /// Invalidate this result unless it was preserved as a stateless analysis.
  /// @param M Module being invalidated (unused beyond the checker).
  /// @param PA Set of preserved analyses.
  /// @param Inv Invalidator for dependent analyses (unused).
  /// @return True if the result should be discarded.
  LLVM_ABI bool invalidate(Module &M, const PreservedAnalyses &PA,
                           ModuleAnalysisManager::Invalidator &Inv);

  /// Return the LibcallLoweringInfo for the context identified by \p Key,
  /// creating it (via \p ApplyContextRules) on first request. \p Key should be
  /// TargetSubtargetInfo*.
  /// @param Key Opaque context key, typically a TargetSubtargetInfo*.
  /// @param ApplyContextRules Optional callback applying context-specific
  /// rules when creating the lowering.
  /// @return Libcall lowering info for the context identified by \p Key.
  template <typename KeyT>
  const LibcallLoweringInfo &getLibcallLowering(
      const KeyT *Key,
      LibcallLoweringInfo::ApplyContextRulesFn ApplyContextRules = {}) const {
    return getLibcallLoweringForKey(static_cast<const void *>(Key),
                                    ApplyContextRules);
  }

private:
  const LibcallLoweringInfo &getLibcallLoweringForKey(
      const void *Key,
      LibcallLoweringInfo::ApplyContextRulesFn ApplyContextRules) const {
    auto It = LoweringMap.find(Key);
    if (It != LoweringMap.end())
      return It->second;
    return LoweringMap.try_emplace(Key, *RTLCI, ApplyContextRules)
        .first->second;
  }
};

/// Analysis that provides \c ModuleLibcallLoweringInfo for a module.
class LibcallLoweringModuleAnalysis
    : public AnalysisInfoMixin<LibcallLoweringModuleAnalysis> {
private:
  friend AnalysisInfoMixin<LibcallLoweringModuleAnalysis>;
  LLVM_ABI static AnalysisKey Key;

  ModuleLibcallLoweringInfo LibcallLoweringMap;

public:
  /// The analysis result type; per-context libcall lowering info for a module.
  using Result = ModuleLibcallLoweringInfo;

  /// Run the libcall-lowering analysis on module \p M.
  /// @param M Module to analyze.
  /// @param MAM Module analysis manager providing dependencies.
  /// @return Initialized module libcall lowering map for \p M.
  LLVM_ABI Result run(Module &M, ModuleAnalysisManager &MAM);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_LIBCALLLOWERINGINFO_H
