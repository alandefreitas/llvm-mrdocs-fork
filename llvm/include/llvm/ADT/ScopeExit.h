//===- llvm/ADT/ScopeExit.h - Execute code at scope exit --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the scope_exit class, which executes user-defined
/// cleanup logic at scope exit.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SCOPEEXIT_H
#define LLVM_ADT_SCOPEEXIT_H

#include <utility>

namespace llvm {

/// RAII guard that invokes a callable when leaving the current scope.
///
/// Move ownership transfers the cleanup; copy construction and assignment are
/// deleted. Call \c release() to disarm the guard without running the cleanup.
template <typename Callable> class [[nodiscard]] scope_exit {
  Callable ExitFunction;
  bool Engaged = true; // False once moved-from or release()d.

public:
  /// Construct a guard that will call \p F on destruction unless released.
  template <typename Fp>
  explicit scope_exit(Fp &&F) : ExitFunction(std::forward<Fp>(F)) {}

  /// Move construction transfers the cleanup responsibility from \p Rhs.
  scope_exit(scope_exit &&Rhs)
      : ExitFunction(std::move(Rhs.ExitFunction)), Engaged(Rhs.Engaged) {
    Rhs.release();
  }
  /// Copy construction is deleted; each guard owns a unique cleanup.
  scope_exit(const scope_exit &) = delete;
  /// Move assignment is deleted.
  scope_exit &operator=(scope_exit &&) = delete;
  /// Copy assignment is deleted.
  scope_exit &operator=(const scope_exit &) = delete;

  /// Disarm the guard so the exit callable is not run on destruction.
  void release() { Engaged = false; }

  /// Run the exit callable if this guard is still engaged.
  ~scope_exit() {
    if (Engaged)
      ExitFunction();
  }
};

/// Deduce \c scope_exit from a callable argument.
template <typename Callable> scope_exit(Callable) -> scope_exit<Callable>;

} // end namespace llvm

#endif
