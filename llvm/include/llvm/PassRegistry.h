//===- llvm/PassRegistry.h - Pass Information Registry ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines PassRegistry, a class that is used in the initialization
// and registration of passes.  At application startup, passes are registered
// with the PassRegistry, which is later provided to the PassManager for
// dependency resolution and similar tasks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSREGISTRY_H
#define LLVM_PASSREGISTRY_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/RWMutex.h"
#include <memory>
#include <vector>

namespace llvm {

class PassInfo;
/// Callback interface notified when passes are registered or enumerated.
struct PassRegistrationListener;

/// Manages registration and initialization of the pass subsystem.
///
/// Assists the PassManager in resolving pass dependencies. Passes are
/// registered with the registry at application startup.
///
/// NOTE: PassRegistry is NOT thread-safe.  If you want to use LLVM on multiple
/// threads simultaneously, you will need to use a separate PassRegistry on
/// each thread.
class PassRegistry {
  mutable sys::SmartRWMutex<true> Lock;

  /// PassInfoMap - Keep track of the PassInfo object for each registered pass.
  using MapType = DenseMap<const void *, const PassInfo *>;
  MapType PassInfoMap;

  using StringMapType = StringMap<const PassInfo *>;
  StringMapType PassInfoStringMap;

  std::vector<std::unique_ptr<const PassInfo>> ToFree;
  std::vector<PassRegistrationListener *> Listeners;

public:
  /// Constructs an empty pass registry.
  PassRegistry() = default;
  /// Destroys the registry and frees owned PassInfo objects.
  LLVM_ABI ~PassRegistry();

  /// getPassRegistry - Access the global registry object, which is
  /// automatically initialized at application launch and destroyed by
  /// llvm_shutdown.
  /// \return The global PassRegistry instance.
  LLVM_ABI static PassRegistry *getPassRegistry();

  /// getPassInfo - Look up a pass' corresponding PassInfo, indexed by the pass'
  /// type identifier (&MyPass::ID).
  /// \param TI Type-identifier pointer for the pass (&MyPass::ID).
  /// \return The PassInfo for the given type identifier, or null if unregistered.
  LLVM_ABI const PassInfo *getPassInfo(const void *TI) const;

  /// getPassInfo - Look up a pass' corresponding PassInfo, indexed by the pass'
  /// argument string.
  /// \param Arg Command-line argument string that identifies the pass.
  /// \return The PassInfo for the given argument string, or null if unregistered.
  LLVM_ABI const PassInfo *getPassInfo(StringRef Arg) const;

  /// registerPass - Register a pass (by means of its PassInfo) with the
  /// registry.  Required in order to use the pass with a PassManager.
  /// \param PI Pass metadata to register.
  /// \param ShouldFree If true, the registry takes ownership and frees \p PI.
  LLVM_ABI void registerPass(const PassInfo &PI, bool ShouldFree = false);

  /// enumerateWith - Enumerate the registered passes, calling the provided
  /// PassRegistrationListener's passEnumerate() callback on each of them.
  /// \param L Listener whose passEnumerate() is invoked for each pass.
  LLVM_ABI void enumerateWith(PassRegistrationListener *L);

  /// addRegistrationListener - Register the given PassRegistrationListener
  /// to receive passRegistered() callbacks whenever a new pass is registered.
  /// \param L Listener to notify on subsequent pass registrations.
  LLVM_ABI void addRegistrationListener(PassRegistrationListener *L);

  /// removeRegistrationListener - Unregister a PassRegistrationListener so that
  /// it no longer receives passRegistered() callbacks.
  /// \param L Listener previously added with addRegistrationListener().
  LLVM_ABI void removeRegistrationListener(PassRegistrationListener *L);
};

} // end namespace llvm

#endif // LLVM_PASSREGISTRY_H
