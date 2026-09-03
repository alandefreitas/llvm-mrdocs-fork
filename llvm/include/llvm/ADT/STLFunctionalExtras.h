//===- llvm/ADT/STLFunctionalExtras.h - Extras for <functional> -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains some extension to <functional>.
//
// No library is required when using these functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STLFUNCTIONALEXTRAS_H
#define LLVM_ADT_STLFUNCTIONALEXTRAS_H

#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/Support/Compiler.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace llvm {

//===----------------------------------------------------------------------===//
//     Extra additions to <functional>
//===----------------------------------------------------------------------===//

/// An efficient, type-erasing, non-owning reference to a callable.
///
/// This is intended for use as the type of a function parameter that is not
/// used after the function in question returns.
///
/// This class does not own the callable, so it is not in general safe to store
/// a function_ref.
template<typename Fn> class function_ref;

/// Partial specialization of function_ref for a given function signature.
template <typename Ret, typename... Params>
class LLVM_GSL_POINTER function_ref<Ret(Params...)> {
  Ret (*callback)(intptr_t callable, Params ...params) = nullptr;
  intptr_t callable;

  template<typename Callable>
  static Ret callback_fn(intptr_t callable, Params ...params) {
    return (*reinterpret_cast<Callable*>(callable))(
        std::forward<Params>(params)...);
  }

public:
  /// Construct an empty function_ref that does not bind a callable.
  function_ref() = default;
  /// Construct an empty function_ref from nullptr.
  /// \param Null Unused nullptr literal used to select this overload.
  function_ref(std::nullptr_t Null) {}

  /// Construct a function_ref that binds \p callable without taking ownership.
  ///
  /// Disabled when \p Callable is function_ref itself, so this is not the copy
  /// constructor, and when \p Callable cannot be invoked with \c Params to
  /// produce a type convertible to \c Ret (or \c Ret is void).
  /// \param callable Callable whose address is stored; must outlive this
  /// object.
  template <typename Callable>
  function_ref(
      Callable &&callable LLVM_LIFETIME_BOUND,
      // This is not the copy-constructor.
      std::enable_if_t<!std::is_same<remove_cvref_t<Callable>,
                                     function_ref>::value> * = nullptr,
      // Functor must be callable and return a suitable type.
      std::enable_if_t<std::is_void<Ret>::value ||
                       std::is_convertible<decltype(std::declval<Callable>()(
                                               std::declval<Params>()...)),
                                           Ret>::value> * = nullptr)
      : callback(callback_fn<std::remove_reference_t<Callable>>),
        callable(reinterpret_cast<intptr_t>(&callable)) {}

  /// Invoke the bound callable with \p params and return its result.
  /// \param params Arguments forwarded to the bound callable.
  /// \return Result of invoking the bound callable.
  Ret operator()(Params ...params) const {
    return callback(callable, std::forward<Params>(params)...);
  }

  /// Return true if this reference currently binds a callable.
  /// \return True if a callable is bound, false otherwise.
  explicit operator bool() const { return callback; }

  /// Return true if this and \p Other store the same callable address.
  /// \param Other Other function_ref to compare against.
  /// \return True if both store the same callable address.
  bool operator==(const function_ref<Ret(Params...)> &Other) const {
    return callable == Other.callable;
  }
};

} // end namespace llvm

#endif // LLVM_ADT_STLFUNCTIONALEXTRAS_H
