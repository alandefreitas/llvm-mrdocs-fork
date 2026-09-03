//===- AddressSanitizer.h - AddressSanitizer instrumentation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the AddressSanitizer class which is a port of the legacy
// AddressSanitizer pass to use the new PassManager infrastructure.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_ADDRESSSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_ADDRESSSANITIZER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"

namespace llvm {
class Module;
class raw_ostream;

/// Options that control AddressSanitizer instrumentation.
struct AddressSanitizerOptions {
  /// Instrument for KernelAddressSanitizer instead of user-space ASan.
  bool CompileKernel = false;
  /// Continue after detecting an error instead of terminating.
  bool Recover = false;
  /// Detect stack-use-after-scope bugs.
  bool UseAfterScope = false;
  /// Mode for detecting stack-use-after-return bugs.
  AsanDetectStackUseAfterReturnMode UseAfterReturn =
      AsanDetectStackUseAfterReturnMode::Runtime;
  /// Use callbacks instead of inline checks when a function has more than this
  /// many memory accesses (-1 means never use callbacks).
  int InstrumentationWithCallsThreshold = 7000;
  /// Inline shadow poisoning for blocks up to this size in bytes.
  uint32_t MaxInlinePoisoningSize = 64;
  /// Guard against compiler/runtime version mismatch.
  bool InsertVersionCheck = true;
};

/// Public interface to the address sanitizer module pass for instrumenting code
/// to check for various memory errors.
///
/// This adds 'asan.module_ctor' to 'llvm.global_ctors'. This pass may also
/// run intependently of the function address sanitizer.
class AddressSanitizerPass
    : public RequiredPassInfoMixin<AddressSanitizerPass> {
public:
  /// Construct an AddressSanitizer module pass with the given options.
  /// @param Options Instrumentation options for the pass.
  /// @param UseGlobalGC Whether to use globals GC / live support.
  /// @param UseOdrIndicator Whether to use ODR indicators for better reporting.
  /// @param DestructorKind Kind of ASan module destructor to emit.
  /// @param ConstructorKind Kind of ASan module constructor to emit.
  LLVM_ABI
  AddressSanitizerPass(const AddressSanitizerOptions &Options,
                       bool UseGlobalGC = true, bool UseOdrIndicator = true,
                       AsanDtorKind DestructorKind = AsanDtorKind::Global,
                       AsanCtorKind ConstructorKind = AsanCtorKind::Global);
  /// Run AddressSanitizer instrumentation over the module.
  /// @param M Module to instrument.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  AddressSanitizerOptions Options;
  bool UseGlobalGC;
  bool UseOdrIndicator;
  AsanDtorKind DestructorKind;
  AsanCtorKind ConstructorKind;
};

/// Decoded ASan memory-access descriptor shared by the pass and backend.
struct ASanAccessInfo {
  /// Packed bitfield encoding of the access descriptor.
  const int32_t Packed;
  /// Log2 of the access size in bytes (0 for 1 byte, 1 for 2, …).
  const uint8_t AccessSizeIndex;
  /// True if the access is a store; false if it is a load.
  const bool IsWrite;
  /// True if the access is instrumented for KernelAddressSanitizer.
  const bool CompileKernel;

  /// Decode an ASan access descriptor from its packed encoding.
  /// @param Packed Packed bitfield of write/kernel/size fields.
  LLVM_ABI explicit ASanAccessInfo(int32_t Packed);
  /// Build an ASan access descriptor from its individual fields.
  /// @param IsWrite Whether the access is a store.
  /// @param CompileKernel Whether this is a kernel ASan access.
  /// @param AccessSizeIndex Log2 of the access size in bytes.
  LLVM_ABI ASanAccessInfo(bool IsWrite, bool CompileKernel,
                          uint8_t AccessSizeIndex);
};

} // namespace llvm

#endif
