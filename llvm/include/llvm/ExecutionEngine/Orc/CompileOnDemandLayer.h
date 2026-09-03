//===- CompileOnDemandLayer.h - Compile each function on demand -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// JIT layer for breaking up modules and inserting callbacks to allow
// individual functions to be compiled on demand.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_COMPILEONDEMANDLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_COMPILEONDEMANDLAYER_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/ExecutionEngine/Orc/LazyReexports.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcError.h"
#include "llvm/ExecutionEngine/Orc/Speculation.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <utility>

namespace llvm {
namespace orc {

/// A layer that breaks up modules and inserts callbacks so individual functions
/// can be compiled on demand.
class LLVM_ABI CompileOnDemandLayer : public IRLayer {
public:
  /// Builder for IndirectStubsManagers.
  using IndirectStubsManagerBuilder =
      std::function<std::unique_ptr<IndirectStubsManager>()>;

  /// Construct a CompileOnDemandLayer.
  /// \param ES Execution session for this layer.
  /// \param BaseLayer Layer to emit partitioned modules to.
  /// \param LCTMgr Manager for lazy call-through trampolines.
  /// \param BuildIndirectStubsManager Builder for per-dylib indirect stubs
  ///        managers.
  CompileOnDemandLayer(ExecutionSession &ES, IRLayer &BaseLayer,
                       LazyCallThroughManager &LCTMgr,
                       IndirectStubsManagerBuilder BuildIndirectStubsManager);
  /// Sets the ImplSymbolMap.
  /// \param Imp Map used to track implementation symbols for lazy call-throughs,
  ///        or nullptr to clear it.
  void setImplMap(ImplSymbolMap *Imp);

  /// Emits the given module. This should not be called by clients: it will be
  /// called by the JIT when a definition added via the add method is requested.
  /// \param R Materialization responsibility for the definitions being emitted.
  /// \param TSM Thread-safe module to partition and emit.
  void emit(std::unique_ptr<MaterializationResponsibility> R,
            ThreadSafeModule TSM) override;

private:
  struct PerDylibResources {
  public:
    PerDylibResources(JITDylib &ImplD,
                      std::unique_ptr<IndirectStubsManager> ISMgr)
        : ImplD(ImplD), ISMgr(std::move(ISMgr)) {}
    JITDylib &getImplDylib() { return ImplD; }
    IndirectStubsManager &getISManager() { return *ISMgr; }

  private:
    JITDylib &ImplD;
    std::unique_ptr<IndirectStubsManager> ISMgr;
  };

  using PerDylibResourcesMap = std::map<const JITDylib *, PerDylibResources>;

  PerDylibResources &getPerDylibResources(JITDylib &TargetD);

  mutable std::mutex CODLayerMutex;

  IRLayer &BaseLayer;
  LazyCallThroughManager &LCTMgr;
  IndirectStubsManagerBuilder BuildIndirectStubsManager;
  PerDylibResourcesMap DylibResources;
  ImplSymbolMap *AliaseeImpls = nullptr;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_COMPILEONDEMANDLAYER_H
