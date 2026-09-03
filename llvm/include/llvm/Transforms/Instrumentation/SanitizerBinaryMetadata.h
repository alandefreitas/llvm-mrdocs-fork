//===------- Definition of the SanitizerBinaryMetadata class ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SanitizerBinaryMetadata pass.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_SANITIZERBINARYMETADATA_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_SANITIZERBINARYMETADATA_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/Instrumentation.h"

namespace llvm {
namespace vfs {
class FileSystem;
} // namespace vfs

/// Options that control which sanitizer binary metadata is emitted.
struct SanitizerBinaryMetadataOptions {
  /// Emit PCs for covered functions.
  bool Covered = false;
  /// Emit PCs for atomic operations.
  bool Atomics = false;
  /// Emit PCs for starts of functions subject to use-after-return checking.
  bool UAR = false;
  /// Construct options with all metadata features disabled.
  SanitizerBinaryMetadataOptions() = default;
};

/// Bit position of the atomics feature flag in the covered-function feature
/// mask.
inline constexpr int kSanitizerBinaryMetadataAtomicsBit = 0;
/// Bit position of the use-after-return feature flag in the covered-function
/// feature mask.
inline constexpr int kSanitizerBinaryMetadataUARBit = 1;
/// Bit position of the use-after-return-has-size feature flag in the
/// covered-function feature mask.
inline constexpr int kSanitizerBinaryMetadataUARHasSizeBit = 2;

/// Feature mask bit indicating the function contains atomic operations.
inline constexpr uint64_t kSanitizerBinaryMetadataAtomics =
    1 << kSanitizerBinaryMetadataAtomicsBit;
/// Feature mask bit indicating the function is subject to use-after-return
/// checking.
inline constexpr uint64_t kSanitizerBinaryMetadataUAR =
    1 << kSanitizerBinaryMetadataUARBit;
/// Feature mask bit indicating UAR metadata includes the stack frame size.
inline constexpr uint64_t kSanitizerBinaryMetadataUARHasSize =
    1 << kSanitizerBinaryMetadataUARHasSizeBit;

/// ELF section name for covered-function sanitizer metadata.
inline constexpr char kSanitizerBinaryMetadataCoveredSection[] =
    "sanmd_covered";
/// ELF section name for atomic-operation sanitizer metadata.
inline constexpr char kSanitizerBinaryMetadataAtomicsSection[] =
    "sanmd_atomics";

/// Public interface to the SanitizerBinaryMetadata module pass for emitting
/// metadata for binary analysis sanitizers.
//
/// The pass should be inserted after optimizations.
class SanitizerBinaryMetadataPass
    : public RequiredPassInfoMixin<SanitizerBinaryMetadataPass> {
public:
  /// Construct a SanitizerBinaryMetadata pass with the given options.
  /// @param Opts Metadata emission options for the pass.
  /// @param VFS Optional virtual filesystem used to load ignorelist files.
  /// @param IgnorelistFiles Paths to ignorelist files that suppress metadata.
  LLVM_ABI explicit SanitizerBinaryMetadataPass(
      SanitizerBinaryMetadataOptions Opts = {},
      IntrusiveRefCntPtr<vfs::FileSystem> VFS = nullptr,
      ArrayRef<std::string> IgnorelistFiles = {});
  /// Run SanitizerBinaryMetadata emission over the module.
  /// @param M Module to annotate with sanitizer binary metadata.
  /// @param AM Module analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  const SanitizerBinaryMetadataOptions Options;
  IntrusiveRefCntPtr<vfs::FileSystem> VFS;
  const ArrayRef<std::string> IgnorelistFiles;
};

} // namespace llvm

#endif
