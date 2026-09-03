//===- JITLinkReentryTrampolines.h -- JITLink-based trampolines -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emit reentry trampolines via JITLink.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_JITLINKREENTRYTRAMPOLINES_H
#define LLVM_EXECUTIONENGINE_ORC_JITLINKREENTRYTRAMPOLINES_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/LazyReexports.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm::jitlink {
class Block;
class LinkGraph;
class Section;
class Symbol;
} // namespace llvm::jitlink

namespace llvm::orc {

class ObjectLinkingLayer;
class RedirectableSymbolManager;

/// Produces trampolines on request using JITLink.
class JITLinkReentryTrampolines {
public:
  /// Architecture-specific function that emits one reentry trampoline.
  using EmitTrampolineFn = unique_function<jitlink::Symbol &(
      jitlink::LinkGraph &G, jitlink::Section &Sec,
      jitlink::Symbol &ReentrySym)>;
  /// Callback invoked with emitted trampoline addresses, or an error.
  using OnTrampolinesReadyFn = unique_function<void(
      Expected<std::vector<ExecutorSymbolDef>> EntryAddrs)>;

  /// Create trampolines using the default reentry trampoline function for
  /// the session triple.
  /// \param ObjLinkingLayer Object linking layer used to emit trampoline
  ///        graphs.
  /// \return A new JITLinkReentryTrampolines instance, or an error if the
  ///         session triple has no default reentry trampoline.
  LLVM_ABI static Expected<std::unique_ptr<JITLinkReentryTrampolines>>
  Create(ObjectLinkingLayer &ObjLinkingLayer);

  /// Construct trampolines for the given object linking layer.
  /// \param ObjLinkingLayer Object linking layer used to emit trampoline
  ///        graphs.
  /// \param EmitTrampoline Architecture-specific function that emits one
  ///        trampoline.
  LLVM_ABI JITLinkReentryTrampolines(ObjectLinkingLayer &ObjLinkingLayer,
                                     EmitTrampolineFn EmitTrampoline);
  /// Move construction is deleted.
  ///
  /// \param Other Unused; move construction is not supported.
  JITLinkReentryTrampolines(JITLinkReentryTrampolines &&Other) = delete;
  /// Move assignment is deleted.
  ///
  /// \param Other Unused; move assignment is not supported.
  JITLinkReentryTrampolines &
  operator=(JITLinkReentryTrampolines &&Other) = delete;

  /// Emit the given number of reentry trampolines.
  /// \param RT Resource tracker that owns the emitted trampolines.
  /// \param NumTrampolines Number of trampolines to emit.
  /// \param OnTrampolinesReady Callback invoked with the trampoline addresses,
  ///        or an error if emission fails.
  LLVM_ABI void emit(ResourceTrackerSP RT, size_t NumTrampolines,
                     OnTrampolinesReadyFn OnTrampolinesReady);

private:
  class TrampolineAddrScraperPlugin;

  ObjectLinkingLayer &ObjLinkingLayer;
  TrampolineAddrScraperPlugin *TrampolineAddrScraper = nullptr;
  EmitTrampolineFn EmitTrampoline;
  std::atomic<size_t> ReentryGraphIdx{0};
};

/// Create a LazyReexportsManager that emits reentry trampolines via JITLink.
/// \param ObjLinkingLayer Object linking layer used to emit trampoline graphs.
/// \param RSMgr Redirectable symbol manager used for lazy reexports.
/// \param PlatformJD JITDylib that holds platform/runtime symbols.
/// \param L Optional listener notified about lazy reexport events.
/// \return A LazyReexportsManager that emits reentry trampolines via JITLink,
///         or an error on failure.
LLVM_ABI Expected<std::unique_ptr<LazyReexportsManager>>
createJITLinkLazyReexportsManager(ObjectLinkingLayer &ObjLinkingLayer,
                                  RedirectableSymbolManager &RSMgr,
                                  JITDylib &PlatformJD,
                                  LazyReexportsManager::Listener *L = nullptr);

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_JITLINKREENTRYTRAMPOLINES_H
