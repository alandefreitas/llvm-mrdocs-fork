//===---------------- Layer.h -- Layer interfaces --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Layer interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LAYER_H
#define LLVM_EXECUTIONENGINE_ORC_LAYER_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Mangling.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ExtensibleRTTI.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace orc {

/// Convenient base class for MaterializationUnits wrapping LLVM IR.
///
/// Represents materialization responsibility for all symbols in the given
/// module. If symbols are overridden by other definitions, then their linkage
/// is changed to available-externally.
class LLVM_ABI IRMaterializationUnit : public MaterializationUnit {
public:
  /// Map from symbol names to their GlobalValue definitions in the module.
  using SymbolNameToDefinitionMap = std::map<SymbolStringPtr, GlobalValue *>;

  /// Create an IRMaterializationLayer. Scans the module to build the
  /// SymbolFlags and SymbolToDefinition maps.
  /// @param ES Execution session used to intern symbol names.
  /// @param MO Mangling options used when mapping IR symbols.
  /// @param TSM Thread-safe module whose definitions will be materialized.
  IRMaterializationUnit(ExecutionSession &ES,
                        const IRSymbolMapper::ManglingOptions &MO,
                        ThreadSafeModule TSM);

  /// Create an IRMaterializationUnit from a module and pre-built maps.
  ///
  /// The maps must provide entries for each definition in M. This constructor
  /// is useful for delegating work from one IRMaterializationUnit to another.
  /// @param TSM Thread-safe module whose definitions will be materialized.
  /// @param I Interface describing the symbols provided by this unit.
  /// @param SymbolToDefinition Map from symbol names to GlobalValue
  /// definitions.
  IRMaterializationUnit(ThreadSafeModule TSM, Interface I,
                        SymbolNameToDefinitionMap SymbolToDefinition);

  /// Return the ModuleIdentifier as the name for this MaterializationUnit.
  /// @return ModuleIdentifier used as this unit's name.
  StringRef getName() const override;

  /// Return a reference to the contained ThreadSafeModule.
  /// @return Reference to the contained ThreadSafeModule.
  const ThreadSafeModule &getModule() const { return TSM; }

protected:
  /// Thread-safe module containing the IR to materialize.
  ThreadSafeModule TSM;
  /// Map from symbol names to GlobalValue definitions in TSM.
  SymbolNameToDefinitionMap SymbolToDefinition;

private:
  static SymbolStringPtr getInitSymbol(ExecutionSession &ES,
                                       const ThreadSafeModule &TSM);

  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
};

/// Interface for layers that accept LLVM IR.
class LLVM_ABI IRLayer {
public:
  /// Construct an IRLayer for the given execution session.
  /// @param ES Execution session this layer belongs to.
  /// @param MO Reference to mangling options used by this layer.
  IRLayer(ExecutionSession &ES, const IRSymbolMapper::ManglingOptions *&MO)
      : ES(ES), MO(MO) {}

  /// Destroy this IRLayer.
  virtual ~IRLayer();

  /// Returns the ExecutionSession for this layer.
  /// @return Execution session for this layer.
  ExecutionSession &getExecutionSession() { return ES; }

  /// Get the mangling options for this layer.
  /// @return Mangling options for this layer.
  const IRSymbolMapper::ManglingOptions *&getManglingOptions() const {
    return MO;
  }

  /// Sets the CloneToNewContextOnEmit flag (false by default).
  ///
  /// When set, IR modules added to this layer will be cloned on to a new
  /// context before emit is called. This can be used by clients who want
  /// to load all IR using one LLVMContext (to save memory via type and
  /// constant uniquing), but want to move Modules to fresh contexts before
  /// compiling them to enable concurrent compilation.
  /// Single threaded clients, or clients who load every module on a new
  /// context, need not set this.
  /// @param CloneToNewContextOnEmit Whether to clone modules onto a new
  /// context before emit.
  void setCloneToNewContextOnEmit(bool CloneToNewContextOnEmit) {
    this->CloneToNewContextOnEmit = CloneToNewContextOnEmit;
  }

  /// Returns the current value of the CloneToNewContextOnEmit flag.
  /// @return Current value of the CloneToNewContextOnEmit flag.
  bool getCloneToNewContextOnEmit() const { return CloneToNewContextOnEmit; }

  /// Add a MaterializatinoUnit representing the given IR to the JITDylib
  /// targeted by the given tracker.
  /// @param RT Resource tracker for the target JITDylib.
  /// @param TSM Thread-safe module to add.
  /// @return Success, or an error if the module cannot be added.
  virtual Error add(ResourceTrackerSP RT, ThreadSafeModule TSM);

  /// Adds a MaterializationUnit representing the given IR to the given
  /// JITDylib.
  /// @param JD JITDylib to add the module to.
  /// @param TSM Thread-safe module to add.
  /// @return Success, or an error if the module cannot be added.
  Error add(JITDylib &JD, ThreadSafeModule TSM) {
    return add(JD.getDefaultResourceTracker(), std::move(TSM));
  }

  /// Emit should materialize the given IR.
  /// @param R Materialization responsibility for the symbols being emitted.
  /// @param TSM Thread-safe module to materialize.
  virtual void emit(std::unique_ptr<MaterializationResponsibility> R,
                    ThreadSafeModule TSM) = 0;

private:
  bool CloneToNewContextOnEmit = false;
  ExecutionSession &ES;
  const IRSymbolMapper::ManglingOptions *&MO;
};

/// MaterializationUnit that materializes modules by calling the 'emit' method
/// on the given IRLayer.
class LLVM_ABI BasicIRLayerMaterializationUnit : public IRMaterializationUnit {
public:
  /// Construct a materialization unit that emits via the given IRLayer.
  /// @param L IRLayer whose emit method will materialize the module.
  /// @param MO Mangling options used when mapping IR symbols.
  /// @param TSM Thread-safe module to materialize.
  BasicIRLayerMaterializationUnit(IRLayer &L,
                                  const IRSymbolMapper::ManglingOptions &MO,
                                  ThreadSafeModule TSM);

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;

  IRLayer &L;
};

/// Interface for Layers that accept object files.
class LLVM_ABI ObjectLayer : public RTTIExtends<ObjectLayer, RTTIRoot> {
public:
  /// RTTI identifier for this ObjectLayer type.
  static char ID;

  /// Construct an ObjectLayer for the given execution session.
  /// @param ES Execution session this layer belongs to.
  ObjectLayer(ExecutionSession &ES);
  /// Destroy this ObjectLayer.
  ~ObjectLayer() override;

  /// Returns the execution session for this layer.
  /// @return Execution session for this layer.
  ExecutionSession &getExecutionSession() { return ES; }

  /// Adds a MaterializationUnit for the object file in the given memory buffer
  /// to the JITDylib for the given ResourceTracker.
  /// @param RT Resource tracker for the target JITDylib.
  /// @param O Memory buffer containing the object file.
  /// @param I Interface describing the symbols provided by the object.
  /// @return Success, or an error if the object cannot be added.
  virtual Error add(ResourceTrackerSP RT, std::unique_ptr<MemoryBuffer> O,
                    MaterializationUnit::Interface I);

  /// Add an object file to the JITDylib for the given ResourceTracker.
  ///
  /// Adds a MaterializationUnit for the object file in the given memory buffer.
  /// The interface for the object will be built using the default object
  /// interface builder.
  /// @param RT Resource tracker for the target JITDylib.
  /// @param O Memory buffer containing the object file.
  /// @return Success, or an error if the object cannot be added.
  Error add(ResourceTrackerSP RT, std::unique_ptr<MemoryBuffer> O);

  /// Adds a MaterializationUnit for the object file in the given memory buffer
  /// to the given JITDylib.
  /// @param JD JITDylib to add the object to.
  /// @param O Memory buffer containing the object file.
  /// @param I Interface describing the symbols provided by the object.
  /// @return Success, or an error if the object cannot be added.
  Error add(JITDylib &JD, std::unique_ptr<MemoryBuffer> O,
            MaterializationUnit::Interface I) {
    return add(JD.getDefaultResourceTracker(), std::move(O), std::move(I));
  }

  /// Add an object file to the given JITDylib.
  ///
  /// Adds a MaterializationUnit for the object file in the given memory buffer.
  /// The interface for the object will be built using the default object
  /// interface builder.
  /// @param JD JITDylib to add the object to.
  /// @param O Memory buffer containing the object file.
  /// @return Success, or an error if the object cannot be added.
  Error add(JITDylib &JD, std::unique_ptr<MemoryBuffer> O);

  /// Emit should materialize the given IR.
  /// @param R Materialization responsibility for the symbols being emitted.
  /// @param O Memory buffer containing the object file to materialize.
  virtual void emit(std::unique_ptr<MaterializationResponsibility> R,
                    std::unique_ptr<MemoryBuffer> O) = 0;

private:
  ExecutionSession &ES;
};

/// Materializes the given object file (represented by a MemoryBuffer
/// instance) by calling 'emit' on the given ObjectLayer.
class LLVM_ABI BasicObjectLayerMaterializationUnit
    : public MaterializationUnit {
public:
  /// Create using the default object interface builder function.
  /// @param L ObjectLayer whose emit method will materialize the object.
  /// @param O Memory buffer containing the object file.
  /// @return A materialization unit for the object, or an error on failure.
  static Expected<std::unique_ptr<BasicObjectLayerMaterializationUnit>>
  Create(ObjectLayer &L, std::unique_ptr<MemoryBuffer> O);

  /// Construct a materialization unit that emits via the given ObjectLayer.
  /// @param L ObjectLayer whose emit method will materialize the object.
  /// @param O Memory buffer containing the object file.
  /// @param I Interface describing the symbols provided by the object.
  BasicObjectLayerMaterializationUnit(ObjectLayer &L,
                                      std::unique_ptr<MemoryBuffer> O,
                                      Interface I);

  /// Return the buffer's identifier as the name for this MaterializationUnit.
  /// @return Buffer identifier used as this unit's name.
  StringRef getName() const override;

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;

  ObjectLayer &L;
  std::unique_ptr<MemoryBuffer> O;
};

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LAYER_H
