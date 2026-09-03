//===-- SaveAndRestore.h - Utility  -------------------------------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides utility classes that use RAII to save and restore
/// values.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SAVEANDRESTORE_H
#define LLVM_SUPPORT_SAVEANDRESTORE_H

#include <utility>

namespace llvm {

/// A utility class that uses RAII to save and restore the value of a variable.
template <typename T> struct SaveAndRestore {
  /// Save the current value of \p X without changing it.
  ///
  /// \param X Variable whose value is saved and restored on destruction.
  SaveAndRestore(T &X) : X(X), OldValue(X) {}
  /// Save the current value of \p X and assign \p NewValue to it.
  ///
  /// \param X Variable whose value is saved and restored on destruction.
  /// \param NewValue Value assigned to \p X for the lifetime of this object.
  SaveAndRestore(T &X, const T &NewValue) : X(X), OldValue(X) { X = NewValue; }
  /// Save the current value of \p X and move-assign \p NewValue to it.
  ///
  /// \param X Variable whose value is saved and restored on destruction.
  /// \param NewValue Value move-assigned to \p X for the lifetime of this
  ///                 object.
  SaveAndRestore(T &X, T &&NewValue) : X(X), OldValue(std::move(X)) {
    X = std::move(NewValue);
  }
  /// Restore the saved value to the referenced variable.
  ~SaveAndRestore() { X = std::move(OldValue); }
  /// Return the saved original value.
  ///
  /// \return The value of the referenced variable when this object was
  ///         constructed.
  const T &get() { return OldValue; }

private:
  T &X;
  T OldValue;
};

// User-defined CTAD guides.
/// Deduce \c SaveAndRestore from a reference to the saved variable.
template <typename T> SaveAndRestore(T &) -> SaveAndRestore<T>;
/// Deduce \c SaveAndRestore from a reference and a const new value.
template <typename T> SaveAndRestore(T &, const T &) -> SaveAndRestore<T>;
/// Deduce \c SaveAndRestore from a reference and a movable new value.
template <typename T> SaveAndRestore(T &, T &&) -> SaveAndRestore<T>;

} // namespace llvm

#endif
