//===- JITEventListener.h - Exposes events from JIT compilation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the JITEventListener interface, which lets users get
// callbacks when significant events happen during the JIT compilation process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITEVENTLISTENER_H
#define LLVM_EXECUTIONENGINE_JITEVENTLISTENER_H

#include "llvm-c/ExecutionEngine.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

/// Wrapper around the Intel JIT Events API used for testing and reporting.
class IntelJITEventsWrapper;
/// Wrapper around the OProfile JIT agent API used for testing and reporting.
class OProfileWrapper;

namespace object {

class ObjectFile;

} // end namespace object

/// Abstract interface used by the JIT to notify clients about significant
/// compilation events.
///
/// For example, to notify profilers and debuggers that need to know where
/// functions have been emitted.
///
/// The default implementation of each method does nothing.
class LLVM_ABI JITEventListener {
public:
  /// Opaque key that identifies a loaded object across listener callbacks.
  using ObjectKey = uint64_t;

  /// Construct a JIT event listener with default no-op callbacks.
  JITEventListener() = default;
  /// Destroy the JIT event listener.
  virtual ~JITEventListener() = default;

  /// Called after an object has had its sections allocated and addresses
  /// assigned to all symbols.
  ///
  /// Note: Section memory will not have been relocated yet.
  /// notifyFunctionLoaded will not be called for individual functions in the
  /// object.
  ///
  /// ELF-specific information
  /// The ObjectImage contains the generated object image
  /// with section headers updated to reflect the address at which sections
  /// were loaded and with relocations performed in-place on debug sections.
  /// \param K Key identifying the loaded object.
  /// \param Obj Object file whose sections were allocated.
  /// \param L Info describing where the object was loaded.
  virtual void notifyObjectLoaded(ObjectKey K, const object::ObjectFile &Obj,
                                  const RuntimeDyld::LoadedObjectInfo &L) {}

  /// Called just before the memory associated with a previously emitted object
  /// is released.
  /// \param K Key identifying the object about to be freed.
  virtual void notifyFreeingObject(ObjectKey K) {}

  /// Return the singleton listener that registers emitted code with GDB.
  /// \returns The GDB registration listener singleton.
  static JITEventListener *createGDBRegistrationListener();

#if LLVM_USE_INTEL_JITEVENTS
  /// Return a listener that reports JIT events to the Intel JIT Events API.
  /// \returns A new Intel JIT event listener, or null if unavailable.
  static JITEventListener *createIntelJITEventListener();

  /// Return an Intel JIT event listener that uses \p AlternativeImpl for
  /// testing.
  /// \param AlternativeImpl Test wrapper substituted for the real Intel API.
  /// \returns A new Intel JIT event listener using \p AlternativeImpl.
  static JITEventListener *createIntelJITEventListener(
                                      IntelJITEventsWrapper* AlternativeImpl);
#else
  /// Return a listener that reports JIT events to the Intel JIT Events API.
  /// \returns A new Intel JIT event listener, or null if unavailable.
  static JITEventListener *createIntelJITEventListener() { return nullptr; }

  /// Return an Intel JIT event listener that uses \p AlternativeImpl for
  /// testing.
  /// \param AlternativeImpl Test wrapper substituted for the real Intel API.
  /// \returns A new Intel JIT event listener using \p AlternativeImpl.
  static JITEventListener *createIntelJITEventListener(
                                      IntelJITEventsWrapper* AlternativeImpl) {
    return nullptr;
  }
#endif // USE_INTEL_JITEVENTS

#if LLVM_USE_OPROFILE
  /// Return a listener that reports JIT events to OProfile.
  /// \returns A new OProfile JIT event listener, or null if unavailable.
  static JITEventListener *createOProfileJITEventListener();

  /// Return an OProfile JIT event listener that uses \p AlternativeImpl for
  /// testing.
  /// \param AlternativeImpl Test wrapper substituted for the real opagent API.
  /// \returns A new OProfile JIT event listener using \p AlternativeImpl.
  static JITEventListener *createOProfileJITEventListener(
                                      OProfileWrapper* AlternativeImpl);
#else
  /// Return a listener that reports JIT events to OProfile.
  /// \returns A new OProfile JIT event listener, or null if unavailable.
  static JITEventListener *createOProfileJITEventListener() { return nullptr; }

  /// Return an OProfile JIT event listener that uses \p AlternativeImpl for
  /// testing.
  /// \param AlternativeImpl Test wrapper substituted for the real opagent API.
  /// \returns A new OProfile JIT event listener using \p AlternativeImpl.
  static JITEventListener *createOProfileJITEventListener(
                                      OProfileWrapper* AlternativeImpl) {
    return nullptr;
  }
#endif // USE_OPROFILE

#if LLVM_USE_PERF
  /// Return a listener that reports JIT events to Linux perf.
  /// \returns A new perf JIT event listener, or null if unavailable.
  static JITEventListener *createPerfJITEventListener();
#else
  /// Return a listener that reports JIT events to Linux perf.
  /// \returns A new perf JIT event listener, or null if unavailable.
  static JITEventListener *createPerfJITEventListener()
  {
    return nullptr;
  }
#endif // USE_PERF

private:
  virtual void anchor();
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// C API conversion helpers for \c JITEventListener /
/// \c LLVMJITEventListenerRef, including \c unwrap and \c wrap.
/// \param P Value to convert between the C++ type and the C API reference.
/// \returns The corresponding C++ pointer or C API reference.
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(JITEventListener, LLVMJITEventListenerRef)

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITEVENTLISTENER_H
