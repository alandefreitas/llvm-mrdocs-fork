//===- LegacyPassManager.h - Legacy Container for Passes --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the legacy PassManager class.  This class is used to hold,
// maintain, and optimize execution of Passes.  The PassManager class ensures
// that analysis results are available before a pass runs, and that Pass's are
// destroyed when the PassManager is destroyed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_LEGACYPASSMANAGER_H
#define LLVM_IR_LEGACYPASSMANAGER_H

#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Function;
class Pass;
class Module;

namespace legacy {

/// Return true if the \c -debug-pass command-line option has been specified.
///
/// Used to check whether legacy pass debugging is enabled alongside the new
/// pass manager.
/// @return True if \c -debug-pass was specified; false otherwise.
LLVM_ABI bool debugPassSpecified();

/// Internal implementation of the legacy module \c PassManager.
class PassManagerImpl;
/// Internal implementation of the legacy \c FunctionPassManager.
class FunctionPassManagerImpl;

/// PassManagerBase - An abstract interface to allow code to add passes to
/// a pass manager without having to hard-code what kind of pass manager
/// it is.
class LLVM_ABI PassManagerBase {
public:
  /// Destroy the pass manager and any passes it owns.
  virtual ~PassManagerBase();

  /// Add a pass to the queue of passes to run.
  ///
  /// This passes ownership of the Pass to the PassManager. When the
  /// PassManager is destroyed, the pass will be destroyed as well, so there is
  /// no need to delete the pass. This may even destroy the pass right away if
  /// it is found to be redundant. This implies that all passes MUST be
  /// allocated with 'new'.
  /// \param P Pass to enqueue; ownership is transferred to this manager.
  virtual void add(Pass *P) = 0;
};

/// PassManager manages ModulePassManagers
class LLVM_ABI PassManager : public PassManagerBase {
public:

  /// Construct an empty legacy module pass manager.
  PassManager();
  /// Destroy the pass manager and any passes it owns.
  ~PassManager() override;

  /// Add a pass to the queue of passes to run.
  /// \param P Pass to enqueue; ownership is transferred to this manager.
  void add(Pass *P) override;

  /// run - Execute all of the passes scheduled for execution.  Keep track of
  /// whether any of the passes modifies the module, and if so, return true.
  /// \param M Module on which to run the scheduled passes.
  /// @return True if any pass modified the module; false otherwise.
  bool run(Module &M);

private:
  /// PassManagerImpl_New is the actual class. PassManager is just the
  /// wraper to publish simple pass manager interface
  PassManagerImpl *PM;
};

/// FunctionPassManager manages FunctionPasses.
class LLVM_ABI FunctionPassManager : public PassManagerBase {
public:
  /// Construct a function pass manager for the given module.
  ///
  /// Initializes the pass manager. It needs, but does not take ownership of,
  /// the specified Module.
  /// \param M Module whose functions will be processed; not owned.
  explicit FunctionPassManager(Module *M);
  /// Destroy the pass manager and any passes it owns.
  ~FunctionPassManager() override;

  /// Add a pass to the queue of passes to run.
  /// \param P Pass to enqueue; ownership is transferred to this manager.
  void add(Pass *P) override;

  /// run - Execute all of the passes scheduled for execution.  Keep
  /// track of whether any of the passes modifies the function, and if
  /// so, return true.
  ///
  /// \param F Function on which to run the scheduled passes.
  /// @return True if any pass modified the function; false otherwise.
  bool run(Function &F);

  /// doInitialization - Run all of the initializers for the function passes.
  ///
  /// @return True if any initializer modified the module; false otherwise.
  bool doInitialization();

  /// doFinalization - Run all of the finalizers for the function passes.
  ///
  /// @return True if any finalizer modified the module; false otherwise.
  bool doFinalization();

private:
  FunctionPassManagerImpl *FPM;
  Module *M;
};

} // End legacy namespace

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an opaque \c LLVMPassManagerRef to a \c PassManagerBase pointer.
/// \param P Opaque C API pass-manager reference to unwrap.
/// @return Pointer to the underlying \c PassManagerBase.
inline legacy::PassManagerBase *unwrap(LLVMPassManagerRef P) {
  return reinterpret_cast<legacy::PassManagerBase *>(P);
}

/// Convert a \c PassManagerBase pointer to an opaque \c LLVMPassManagerRef.
/// \param P Pass manager to wrap for the C API.
/// @return Opaque C API reference to \p P.
inline LLVMPassManagerRef wrap(const legacy::PassManagerBase *P) {
  return reinterpret_cast<LLVMPassManagerRef>(
      const_cast<legacy::PassManagerBase *>(P));
}

/// Unwrap an opaque \c LLVMPassManagerRef as a pointer of type \p T.
/// \param P Opaque C API pass-manager reference to unwrap.
/// @return Pointer of type \p T to the underlying pass manager.
template <typename T> inline T *unwrap(LLVMPassManagerRef P) {
  T *Q = (T *)unwrap(P);
  assert(Q && "Invalid cast!");
  return Q;
}

} // End llvm namespace

#endif
