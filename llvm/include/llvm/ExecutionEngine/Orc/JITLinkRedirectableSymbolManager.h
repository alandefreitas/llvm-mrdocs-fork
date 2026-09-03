//===- JITLinkRedirectableSymbolManager.h - JITLink redirection -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Redirectable Symbol Manager implementation using JITLink
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_JITLINKREDIRECABLESYMBOLMANAGER_H
#define LLVM_EXECUTIONENGINE_ORC_JITLINKREDIRECABLESYMBOLMANAGER_H

#include "llvm/ExecutionEngine/Orc/MemoryAccess.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/RedirectionManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/StringSaver.h"

#include <atomic>

namespace llvm {
namespace orc {

/// RedirectableSymbolManager implementation that uses JITLink stubs.
class LLVM_ABI JITLinkRedirectableSymbolManager
    : public RedirectableSymbolManager {
public:
  /// Create a redirection manager that uses a JITLink-based implementation.
  /// \param ObjLinkingLayer Object linking layer used to emit stub graphs.
  /// \param MemAccess Memory-access interface for the executor process.
  /// \return A new RedirectableSymbolManager, or an error if the architecture
  ///         is unsupported.
  static Expected<std::unique_ptr<RedirectableSymbolManager>>
  Create(ObjectLinkingLayer &ObjLinkingLayer, MemoryAccess &MemAccess) {
    auto AnonymousPtrCreator(jitlink::getAnonymousPointerCreator(
        ObjLinkingLayer.getExecutionSession().getTargetTriple()));
    auto PtrJumpStubCreator(jitlink::getPointerJumpStubCreator(
        ObjLinkingLayer.getExecutionSession().getTargetTriple()));
    if (!AnonymousPtrCreator || !PtrJumpStubCreator)
      return make_error<StringError>("Architecture not supported",
                                     inconvertibleErrorCode());
    return std::unique_ptr<RedirectableSymbolManager>(
        new JITLinkRedirectableSymbolManager(ObjLinkingLayer, MemAccess,
                                             AnonymousPtrCreator,
                                             PtrJumpStubCreator));
  }

  /// Construct a JITLink-based redirectable symbol manager.
  /// \param ObjLinkingLayer Object linking layer used to emit stub graphs.
  /// \param MemAccess Memory-access interface for the executor process.
  /// \param AnonymousPtrCreator Architecture-specific anonymous pointer
  ///        creator.
  /// \param PtrJumpStubCreator Architecture-specific pointer jump-stub
  ///        creator.
  JITLinkRedirectableSymbolManager(
      ObjectLinkingLayer &ObjLinkingLayer, MemoryAccess &MemAccess,
      jitlink::AnonymousPointerCreator &AnonymousPtrCreator,
      jitlink::PointerJumpStubCreator &PtrJumpStubCreator)
      : ObjLinkingLayer(ObjLinkingLayer), MemAccess(MemAccess),
        AnonymousPtrCreator(std::move(AnonymousPtrCreator)),
        PtrJumpStubCreator(std::move(PtrJumpStubCreator)) {}

  /// Return a reference to the ObjectLinkingLayer used by this manager.
  /// \return Reference to the ObjectLinkingLayer used by this manager.
  ObjectLinkingLayer &getObjectLinkingLayer() const { return ObjLinkingLayer; }

  /// Emit redirectable symbols with the given initial destinations.
  /// \param R Materialization responsibility for the symbols being emitted.
  /// \param InitialDests Map of symbol names to their initial destinations.
  void emitRedirectableSymbols(std::unique_ptr<MaterializationResponsibility> R,
                               SymbolMap InitialDests) override;

  /// Redirect named symbols in \p JD to the destinations in \p NewDests.
  /// \param JD JITDylib containing the symbols to redirect.
  /// \param NewDests Map of symbol names to new destination definitions.
  /// \return Success, or an error if redirection fails.
  Error redirect(JITDylib &JD, const SymbolMap &NewDests) override;

private:
  ObjectLinkingLayer &ObjLinkingLayer;
  MemoryAccess &MemAccess;
  jitlink::AnonymousPointerCreator AnonymousPtrCreator;
  jitlink::PointerJumpStubCreator PtrJumpStubCreator;
  std::atomic_size_t StubGraphIdx{0};
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_JITLINKREDIRECABLESYMBOLMANAGER_H
