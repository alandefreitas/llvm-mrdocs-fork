//===- llvm/CodeGen/RegAllocRegistry.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation for register allocator function
// pass registry (RegisterRegAlloc).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCREGISTRY_H
#define LLVM_CODEGEN_REGALLOCREGISTRY_H

#include "llvm/CodeGen/RegAllocCommon.h"
#include "llvm/CodeGen/MachinePassRegistry.h"

namespace llvm {

class FunctionPass;

//===----------------------------------------------------------------------===//
///
/// RegisterRegAllocBase class - Track the registration of register allocators.
///
//===----------------------------------------------------------------------===//
template <class SubClass>
class RegisterRegAllocBase : public MachinePassRegistryNode<FunctionPass *(*)()> {
public:
  /// Constructor type that builds a register-allocator FunctionPass.
  using FunctionPassCtor = FunctionPass *(*)();

  /// Global registry of register allocator constructors.
  static MachinePassRegistry<FunctionPassCtor> Registry;

  /// Register an allocator named \p N with description \p D and ctor \p C.
  ///
  /// \param N Command-line name for this register allocator.
  /// \param D Human-readable description of this register allocator.
  /// \param C Factory that constructs the FunctionPass.
  RegisterRegAllocBase(const char *N, const char *D, FunctionPassCtor C)
      : MachinePassRegistryNode(N, D, C) {
    Registry.Add(this);
  }

  /// Remove this allocator from the global registry.
  ~RegisterRegAllocBase() { Registry.Remove(this); }

  // Accessors.
  /// Return the next registry entry in the linked list.
  ///
  /// \return The next registry entry, or null if this is the last entry.
  SubClass *getNext() const {
    return static_cast<SubClass *>(MachinePassRegistryNode::getNext());
  }

  /// Return the head of the global register allocator registry list.
  ///
  /// \return The first registry entry, or null if the list is empty.
  static SubClass *getList() {
    return static_cast<SubClass *>(Registry.getList());
  }

  /// Return the default register allocator constructor.
  ///
  /// \return Default constructor used when no allocator name is selected.
  static FunctionPassCtor getDefault() { return Registry.getDefault(); }

  /// Set the default register allocator constructor.
  /// \param C Constructor to use when no allocator name is selected.
  static void setDefault(FunctionPassCtor C) { Registry.setDefault(C); }

  /// Install \p L as the listener notified when registry entries change.
  /// \param L Listener invoked when constructors are added or removed.
  static void setListener(MachinePassRegistryListener<FunctionPassCtor> *L) {
    Registry.setListener(L);
  }
};

/// Registry node that tracks registration of register allocator passes.
class RegisterRegAlloc : public RegisterRegAllocBase<RegisterRegAlloc> {
public:
  /// Register an allocator named \p N with description \p D and ctor \p C.
  ///
  /// \param N Command-line name for this register allocator.
  /// \param D Human-readable description of this register allocator.
  /// \param C Factory that constructs the FunctionPass.
  RegisterRegAlloc(const char *N, const char *D, FunctionPassCtor C)
    : RegisterRegAllocBase(N, D, C) {}
};

/// RegisterRegAlloc's global Registry tracks allocator registration.
template <class T>
MachinePassRegistry<typename RegisterRegAllocBase<T>::FunctionPassCtor>
    RegisterRegAllocBase<T>::Registry;

} // end namespace llvm

#endif // LLVM_CODEGEN_REGALLOCREGISTRY_H
