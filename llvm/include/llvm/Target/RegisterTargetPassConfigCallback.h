//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file contains the registry for PassConfigCallbacks that enable changes
/// to the TargetPassConfig during the initialization of TargetMachine.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_REGISTERTARGETPASSCONFIGCALLBACK_H
#define LLVM_TARGET_REGISTERTARGETPASSCONFIGCALLBACK_H

#include "TargetMachine.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Callback that customizes a \c TargetPassConfig during TargetMachine setup.
using PassConfigCallback =
    std::function<void(TargetMachine &, PassManagerBase &, TargetPassConfig *)>;

/// RAII helper that registers a \c PassConfigCallback in the global registry.
///
/// Constructing an instance adds \c Callback to the list invoked by
/// \c invokeGlobalTargetPassConfigCallbacks; destruction removes it.
class RegisterTargetPassConfigCallback {
public:
  /// The callback invoked when configuring a target pass pipeline.
  PassConfigCallback Callback;

  /// Register \p C in the global TargetPassConfig callback list.
  /// \param C Callback moved into this object and invoked later.
  LLVM_ABI explicit RegisterTargetPassConfigCallback(PassConfigCallback &&C);
  /// Unregister this callback from the global TargetPassConfig callback list.
  LLVM_ABI ~RegisterTargetPassConfigCallback();
  /// Copy construction is deleted; each registration owns a unique slot.
  /// \param RHS Unused; copy construction is not supported.
  RegisterTargetPassConfigCallback(const RegisterTargetPassConfigCallback &RHS) =
      delete;
  /// Copy assignment is deleted; each registration owns a unique slot.
  /// \param RHS Unused; copy assignment is not supported.
  RegisterTargetPassConfigCallback &
  operator=(const RegisterTargetPassConfigCallback &RHS) = delete;
};

/// Invoke every registered \c PassConfigCallback for the given target setup.
/// \param TM Target machine being configured.
/// \param PM Pass manager that will run the code-generation pipeline.
/// \param PassConfig Target pass configuration being populated.
LLVM_ABI void
invokeGlobalTargetPassConfigCallbacks(TargetMachine &TM, PassManagerBase &PM,
                                      TargetPassConfig *PassConfig);
} // namespace llvm

#endif // LLVM_TARGET_REGISTERTARGETPASSCONFIGCALLBACK_H
