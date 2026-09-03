//===- IRCompileLayer.h -- Eagerly compile IR for JIT -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains the definition for a basic, eagerly compiling layer of the JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_IRCOMPILELAYER_H
#define LLVM_EXECUTIONENGINE_ORC_IRCOMPILELAYER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <functional>
#include <memory>
#include <mutex>

namespace llvm {

class Module;

namespace orc {

/// Eagerly compiles LLVM IR modules to object files for the JIT.
class LLVM_ABI IRCompileLayer : public IRLayer {
public:
  /// Abstract interface for compiling an IR module to an object file.
  class LLVM_ABI IRCompiler {
  public:
    /// Construct an IRCompiler with the given mangling options.
    /// \param MO Mangling options used when mapping IR symbols.
    IRCompiler(IRSymbolMapper::ManglingOptions MO) : MO(std::move(MO)) {}
    /// Destroy this IRCompiler.
    virtual ~IRCompiler();
    /// Return the mangling options used by this compiler.
    /// \return Mangling options used when mapping IR symbols.
    const IRSymbolMapper::ManglingOptions &getManglingOptions() const {
      return MO;
    }
    /// Compile the given module to an object file buffer.
    /// \param M Module to compile.
    /// \return Memory buffer holding the compiled object file, or an error.
    virtual Expected<std::unique_ptr<MemoryBuffer>> operator()(Module &M) = 0;

  protected:
    /// Return a mutable reference to the mangling options.
    /// \return Mutable reference to the mangling options.
    IRSymbolMapper::ManglingOptions &manglingOptions() { return MO; }

  private:
    IRSymbolMapper::ManglingOptions MO;
  };

  /// Callback invoked after a module has been compiled.
  using NotifyCompiledFunction = std::function<void(
      MaterializationResponsibility &R, ThreadSafeModule TSM)>;

  /// Construct an IRCompileLayer that compiles IR and emits objects to a base
  /// layer.
  /// \param ES Execution session for this layer.
  /// \param BaseLayer Object layer to emit compiled objects into.
  /// \param Compile Compiler used to turn IR modules into object files.
  IRCompileLayer(ExecutionSession &ES, ObjectLayer &BaseLayer,
                 std::unique_ptr<IRCompiler> Compile);

  /// Return the IR compiler used by this layer.
  /// \return Reference to the IR compiler.
  IRCompiler &getCompiler() { return *Compile; }

  /// Set a callback to invoke after each successful compilation.
  /// \param NotifyCompiled Function called with the materialization
  ///        responsibility and compiled module.
  void setNotifyCompiled(NotifyCompiledFunction NotifyCompiled);

  /// Compile the given module and emit the resulting object to the base layer.
  /// \param R Materialization responsibility for the definitions being emitted.
  /// \param TSM Thread-safe module to compile and emit.
  void emit(std::unique_ptr<MaterializationResponsibility> R,
            ThreadSafeModule TSM) override;

private:
  mutable std::mutex IRLayerMutex;
  ObjectLayer &BaseLayer;
  std::unique_ptr<IRCompiler> Compile;
  const IRSymbolMapper::ManglingOptions *ManglingOpts;
  NotifyCompiledFunction NotifyCompiled = NotifyCompiledFunction();
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_IRCOMPILELAYER_H
