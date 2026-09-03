//===- Module.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_MODULE_H
#define LLVM_SANDBOXIR_MODULE_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {

class DataLayout;

namespace sandboxir {

class Context;
class Function;
class GlobalVariable;
class Type;
class Constant;
class GlobalAlias;
class GlobalIFunc;

/// In SandboxIR the Module is mainly used to access the list of global objects.
class Module {
  llvm::Module &LLVMM;
  Context &Ctx;

  Module(llvm::Module &LLVMM, Context &Ctx) : LLVMM(LLVMM), Ctx(Ctx) {}
  friend class Context; // For constructor.

public:
  /// Return the SandboxIR context that owns this module.
  /// \return The context that owns this module.
  Context &getContext() const { return Ctx; }

  /// Look up a function by name in the module symbol table.
  /// \param Name Function name to look up.
  /// \return The function with the given name, or null if not found.
  LLVM_ABI Function *getFunction(StringRef Name) const;

  /// Return the data layout for the module's target platform.
  /// \return The data layout for this module.
  const DataLayout &getDataLayout() const { return LLVMM.getDataLayout(); }

  /// Return the source file name recorded for this module.
  /// \return The source file name for this module.
  const std::string &getSourceFileName() const {
    return LLVMM.getSourceFileName();
  }

  /// Look up a global variable by name, optionally including internal linkage.
  ///
  /// Look up the specified global variable in the module symbol table. If it
  /// does not exist, return null. If AllowInternal is set to true, this
  /// function will return types that have InternalLinkage. By default, these
  /// types are not returned.
  /// \param Name Global variable name to look up.
  /// \param AllowInternal True to also return internally-linked variables.
  /// \return The global variable with the given name, or null if not found.
  LLVM_ABI GlobalVariable *getGlobalVariable(StringRef Name,
                                             bool AllowInternal) const;
  /// Look up a global variable by name, ignoring internal linkage.
  /// \param Name Global variable name to look up.
  /// \return The global variable with the given name, or null if not found.
  GlobalVariable *getGlobalVariable(StringRef Name) const {
    return getGlobalVariable(Name, /*AllowInternal=*/false);
  }
  /// Return the global variable in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  /// \param Name Global variable name to look up, including internals.
  /// \return The global variable with the given name, or null if not found.
  GlobalVariable *getNamedGlobal(StringRef Name) const {
    return getGlobalVariable(Name, true);
  }

  // TODO: missing getOrInsertGlobal().

  /// Return the global alias in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  /// \param Name Global alias name to look up.
  /// \return The global alias with the given name, or null if not found.
  LLVM_ABI GlobalAlias *getNamedAlias(StringRef Name) const;

  /// Return the global ifunc in the module with the specified name, of
  /// arbitrary type. This method returns null if a global with the specified
  /// name is not found.
  /// \param Name Global ifunc name to look up.
  /// \return The global ifunc with the given name, or null if not found.
  LLVM_ABI GlobalIFunc *getNamedIFunc(StringRef Name) const;

  // TODO: Missing removeGlobalVariable() eraseGlobalVariable(),
  // insertGlobalVariable()

  // TODO: Missing global_begin(), global_end(), globals().

  // TODO: Missing many other functions.

#ifndef NDEBUG
  /// Dump this module to \p OS.
  /// \param OS Output stream.
  void dumpOS(raw_ostream &OS) const;
  /// Dump this module to standard error.
  LLVM_DUMP_METHOD void dump() const;
#endif // NDEBUG
};

} // namespace sandboxir
} // namespace llvm

#endif // LLVM_SANDBOXIR_MODULE_H
