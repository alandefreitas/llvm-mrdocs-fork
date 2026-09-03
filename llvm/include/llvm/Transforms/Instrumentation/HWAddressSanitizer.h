//===--------- Definition of the HWAddressSanitizer class -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the Hardware AddressSanitizer class which is a port of the
// legacy HWAddressSanitizer pass to use the new PassManager infrastructure.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_HWADDRESSSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_HWADDRESSSANITIZER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Module;
class StringRef;
class raw_ostream;

/// Options that control Hardware AddressSanitizer instrumentation.
struct HWAddressSanitizerOptions {
  /// Construct options with all features disabled.
  HWAddressSanitizerOptions()
      : HWAddressSanitizerOptions(false, false, false){};
  /// Construct options with the given instrumentation settings.
  /// @param CompileKernel Instrument for KernelHWAddressSanitizer instead of
  ///        user-space HWASan.
  /// @param Recover Continue after detecting an error instead of terminating.
  /// @param DisableOptimization Disable optimizations that interfere with
  ///        HWASan instrumentation.
  HWAddressSanitizerOptions(bool CompileKernel, bool Recover,
                            bool DisableOptimization)
      : CompileKernel(CompileKernel), Recover(Recover),
        DisableOptimization(DisableOptimization){};
  /// Instrument for KernelHWAddressSanitizer instead of user-space HWASan.
  bool CompileKernel;
  /// Continue after detecting an error instead of terminating.
  bool Recover;
  /// Disable optimizations that interfere with HWASan instrumentation.
  bool DisableOptimization;
};

/// This is a public interface to the hardware address sanitizer.
///
/// pass for instrumenting code to check for various memory errors at runtime, similar to AddressSanitizer but based on partial hardware assistance.
class HWAddressSanitizerPass
    : public RequiredPassInfoMixin<HWAddressSanitizerPass> {
public:
  /// Construct a Hardware AddressSanitizer pass with the given options.
  /// @param Options Instrumentation options for the pass.
  explicit HWAddressSanitizerPass(HWAddressSanitizerOptions Options)
      : Options(Options){};
  /// Run Hardware AddressSanitizer instrumentation over the module.
  /// @param M Module to instrument.
  /// @param MAM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  HWAddressSanitizerOptions Options;
};

/// Bit-field layout for the accessinfo parameter to
/// llvm.hwasan.check.memaccess.
///
/// Shared between the pass and the backend. Bits 0-15 are also used by the
/// runtime.
namespace HWASanAccessInfo {

/// Bit-field shift positions within the HWASan accessinfo encoding.
enum {
  AccessSizeShift = 0, ///< Shift for the 4-bit access-size field.
  IsWriteShift = 4,    ///< Shift for the write/store bit.
  RecoverShift = 5,    ///< Shift for the recover-on-error bit.
  MatchAllShift = 16,  ///< Shift for the 8-bit match-all tag field.
  HasMatchAllShift = 24, ///< Shift for the has-match-all flag bit.
  CompileKernelShift = 25, ///< Shift for the compile-kernel flag bit.
};

/// Mask covering the bits of accessinfo that are used by the runtime.
enum {
  RuntimeMask = 0xffff ///< Mask of bits 0-15 shared with the runtime.
};

} // namespace HWASanAccessInfo

} // namespace llvm

#endif
