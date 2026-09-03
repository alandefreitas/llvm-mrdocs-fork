//===- llvm/IR/IRUnitRef.h - Reference to an IR unit ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines IRUnitRef, a type-erased reference to the IR unit a pass
/// or analysis is running on, and IRUnitKindTraits, which IR units specialize
/// to opt into being referred to by one.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_IRUNITREF_H
#define LLVM_IR_IRUNITREF_H

#include "llvm/Support/Casting.h"
#include <cstddef>
#include <type_traits>

namespace llvm {

class Function;
class Loop;
class MachineFunction;
class Module;

/// The IR units a pass can run on.
enum class IRUnitKind {
  Module,           ///< An LLVM Module.
  Function,         ///< An LLVM Function.
  Loop,             ///< An LLVM Loop.
  MachineFunction,  ///< A MachineFunction.
  LazyCallGraphSCC, ///< A LazyCallGraph strongly connected component (SCC).
};

/// Map an IR unit type to its IRUnitKind.
template <typename IRUnitT> struct IRUnitKindTraits {};

/// Maps \c Module to \c IRUnitKind::Module.
template <> struct IRUnitKindTraits<Module> {
  /// The IR unit kind for \c Module.
  static constexpr IRUnitKind Kind = IRUnitKind::Module;
};
/// Maps \c Function to \c IRUnitKind::Function.
template <> struct IRUnitKindTraits<Function> {
  /// The IR unit kind for \c Function.
  static constexpr IRUnitKind Kind = IRUnitKind::Function;
};
/// Maps \c Loop to \c IRUnitKind::Loop.
template <> struct IRUnitKindTraits<Loop> {
  /// The IR unit kind for \c Loop.
  static constexpr IRUnitKind Kind = IRUnitKind::Loop;
};
/// Maps \c MachineFunction to \c IRUnitKind::MachineFunction.
template <> struct IRUnitKindTraits<MachineFunction> {
  /// The IR unit kind for \c MachineFunction.
  static constexpr IRUnitKind Kind = IRUnitKind::MachineFunction;
};

// IRUnitKindTraits<LazyCallGraph::SCC> needs to be defined in
// Analysis/LazyCallGraph.h.

/// A type-erased reference to the IR unit a pass or analysis is running on,
/// together with the kind of IR unit it refers to.
class IRUnitRef {
  template <typename To, typename From, typename Enable> friend struct CastInfo;

  const void *Ptr;
  IRUnitKind Kind;

  /// Which kind of IR unit is wrapped.
  IRUnitKind getKind() const { return Kind; }

  /// The wrapped IR unit, type-erased.
  const void *getPointer() const { return Ptr; }

public:
  /// Construct a reference wrapping IR unit \p IR.
  /// \param IR The IR unit to refer to.
  template <typename IRUnitT, IRUnitKind K = IRUnitKindTraits<IRUnitT>::Kind>
  IRUnitRef(const IRUnitT &IR) : Ptr(&IR), Kind(K) {}
};

static_assert(!std::is_constructible_v<IRUnitRef, std::nullptr_t>,
              "IRUnitRef must not be constructible from nullptr");

/// Lets isa/cast/dyn_cast query which IR unit an IRUnitRef holds, naming the IR
/// unit itself rather than a pointer to it: dyn_cast<Module>(IR).
template <typename To> struct CastInfo<To, IRUnitRef> {
  /// Returns true if \p IR holds a \c To IR unit.
  /// \param IR The type-erased IR unit reference to test.
  /// \return True if \p IR holds a \c To IR unit.
  static bool isPossible(IRUnitRef IR) {
    return IR.getKind() == IRUnitKindTraits<To>::Kind;
  }

  /// Unconditionally casts \p IR to a pointer to \c To.
  /// \param IR The type-erased IR unit reference to cast.
  /// \return A pointer to the \c To IR unit held by \p IR.
  static const To *doCast(IRUnitRef IR) {
    return static_cast<const To *>(IR.getPointer());
  }

  /// Returns null when a cast to \c To fails.
  /// \return Null, used as the failed-cast sentinel for \c To.
  static const To *castFailed() { return nullptr; }

  /// Casts \p IR to \c To if possible; otherwise returns \c castFailed().
  /// \param IR The type-erased IR unit reference to cast.
  /// \return A pointer to the \c To IR unit, or null if the cast is not possible.
  static const To *doCastIfPossible(IRUnitRef IR) {
    return isPossible(IR) ? doCast(IR) : castFailed();
  }
};

/// CastInfo specialization for const IRUnitRef, forwarding to the non-const case.
template <typename To>
struct CastInfo<To, const IRUnitRef> : public CastInfo<To, IRUnitRef> {};

} // end namespace llvm

#endif // LLVM_IR_IRUNITREF_H
