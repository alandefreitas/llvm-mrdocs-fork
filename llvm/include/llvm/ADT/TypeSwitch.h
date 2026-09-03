//===- TypeSwitch.h - Switch functionality for RTTI casting -*- C++ -*-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///  This file implements the TypeSwitch template, which mimics a switch()
///  statement whose cases are type names.
///
//===-----------------------------------------------------------------------===/

#ifndef LLVM_ADT_TYPESWITCH_H
#define LLVM_ADT_TYPESWITCH_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LogicalResult.h"
#include <optional>

namespace llvm {
namespace detail {

template <typename DerivedT, typename T> class TypeSwitchBase {
public:
  TypeSwitchBase(const T &value) : value(value) {}
  TypeSwitchBase(TypeSwitchBase &&other) : value(other.value) {}
  ~TypeSwitchBase() = default;

  /// TypeSwitchBase is not copyable.
  TypeSwitchBase(const TypeSwitchBase &) = delete;
  /// Copy-assignment is deleted; TypeSwitchBase is move-only.
  /// @param other Unused; copy assignment is not supported.
  void operator=(const TypeSwitchBase &other) = delete;
  /// Move-assignment is deleted because the root value is const.
  /// @param other Unused; move assignment is not supported.
  void operator=(TypeSwitchBase &&other) = delete;

  /// Invoke a case on the derived class with multiple case types.
  /// @param caseFn Callable invoked for each matching case type.
  template <typename CaseT, typename CaseT2, typename... CaseTs,
            typename CallableT>
  // This is marked always_inline and nodebug so it doesn't show up in stack
  // traces at -O0 (or other optimization levels).  Large TypeSwitch's are
  // common, are equivalent to a switch, and don't add any value to stack
  // traces.
  LLVM_ATTRIBUTE_ALWAYS_INLINE LLVM_ATTRIBUTE_NODEBUG DerivedT &
  Case(CallableT &&caseFn) {
    DerivedT &derived = static_cast<DerivedT &>(*this);
    return derived.template Case<CaseT>(caseFn)
        .template Case<CaseT2, CaseTs...>(caseFn);
  }

  /// Invoke a case, inferring the case type from the callable's first argument.
  ///
  /// Note: This inference rules for this overload are very simple: strip
  ///       pointers and references.
  /// @param caseFn Callable whose first parameter type selects the case.
  template <typename CallableT> DerivedT &Case(CallableT &&caseFn) {
    using Traits = function_traits<std::decay_t<CallableT>>;
    using CaseT = std::remove_cv_t<std::remove_pointer_t<
        std::remove_reference_t<typename Traits::template arg_t<0>>>>;

    DerivedT &derived = static_cast<DerivedT &>(*this);
    return derived.template Case<CaseT>(std::forward<CallableT>(caseFn));
  }

protected:
  /// Attempt to dyn_cast the given `value` to `CastT`.
  /// @param value Value to cast to \c CastT.
  template <typename CastT, typename ValueT>
  static decltype(auto) castValue(ValueT &&value) {
    return dyn_cast<CastT>(value);
  }

  /// The root value we are switching on.
  const T value;
};
} // end namespace detail

/// A switch()-like dispatch on a value of type \c T using dyn_cast.
///
/// Each `Case<T>` takes a callable to be invoked if the root value isa<T>, the
/// callable is invoked with the result of dyn_cast<T>() as a parameter.
///
/// Example:
///  Operation *op = ...;
///  LogicalResult result = TypeSwitch<Operation *, LogicalResult>(op)
///    .Case<ConstantOp>([](ConstantOp op) { ... })
///    .Default([](Operation *op) { ... });
///
template <typename T, typename ResultT = void>
class TypeSwitch : public detail::TypeSwitchBase<TypeSwitch<T, ResultT>, T> {
public:
  /// CRTP base that provides Case() helpers for this TypeSwitch.
  using BaseT = detail::TypeSwitchBase<TypeSwitch<T, ResultT>, T>;
  /// Inherit constructors from the CRTP base.
  using BaseT::BaseT;
  /// Inherit Case() overloads from the CRTP base.
  using BaseT::Case;
  /// Move-construct from another TypeSwitch.
  /// @param other Switch state to move from.
  TypeSwitch(TypeSwitch &&other) = default;

  /// Add a case on the given type.
  /// @param caseFn Callable invoked with the cast value when the case matches.
  /// @return This switch for further chaining.
  template <typename CaseT, typename CallableT>
  TypeSwitch<T, ResultT> &Case(CallableT &&caseFn) {
    if (result)
      return *this;

    // Check to see if CaseT applies to 'value'.
    if (auto caseValue = BaseT::template castValue<CaseT>(this->value))
      result.emplace(caseFn(caseValue));
    return *this;
  }

  /// As a default, invoke the given callable within the root value.
  /// @param defaultFn Callable invoked with the root value when no case matched.
  /// @return The matched result, or the value returned by \p defaultFn.
  template <typename CallableT>
  [[nodiscard]] ResultT Default(CallableT &&defaultFn) {
    if (result)
      return std::move(*result);
    return defaultFn(this->value);
  }

  /// As a default, return the given value.
  /// @param defaultResult Value returned when no case matched.
  /// @return The matched result, or \p defaultResult if no case matched.
  [[nodiscard]] ResultT Default(ResultT defaultResult) {
    if (result)
      return std::move(*result);
    return defaultResult;
  }

  /// Default for pointer-like results types that accept `nullptr`.
  /// @param Null Unused nullptr literal used to select this overload.
  /// @return The matched result, or a nullptr-constructed ResultT if none matched.
  template <typename ArgT = ResultT,
            typename =
                std::enable_if_t<std::is_constructible_v<ArgT, std::nullptr_t>>>
  [[nodiscard]] ResultT Default(std::nullptr_t Null) {
    return Default(ResultT(nullptr));
  }

  /// Default for optional results types that accept `std::nullopt`.
  /// @param None Unused nullopt literal used to select this overload.
  /// @return The matched result, or a nullopt-constructed ResultT if none matched.
  template <typename ArgT = ResultT,
            typename =
                std::enable_if_t<std::is_constructible_v<ArgT, std::nullopt_t>>>
  [[nodiscard]] ResultT Default(std::nullopt_t None) {
    return Default(ResultT(std::nullopt));
  }

  /// Default for result types constructible from `LogicalResult` (e.g.,
  /// `FailureOr<T>`).
  /// @param result LogicalResult used to construct the default return value.
  /// @return The matched result, or a ResultT constructed from \p result.
  template <typename ArgT = ResultT,
            typename =
                std::enable_if_t<std::is_constructible_v<ArgT, LogicalResult> &&
                                 !std::is_same_v<ArgT, LogicalResult>>>
  [[nodiscard]] ResultT Default(LogicalResult result) {
    return Default(ResultT(result));
  }

  /// Declare default as unreachable, making sure that all cases were handled.
  /// @param message Abort message if no case matched.
  /// @return The matched result; aborts if no case matched.
  [[nodiscard]] ResultT DefaultUnreachable(
      const char *message = "Fell off the end of a type-switch") {
    if (result)
      return std::move(*result);
    llvm_unreachable(message);
  }

  /// Convert to \c ResultT, treating a missing case as unreachable.
  /// @return The matched result; aborts if no case matched.
  [[nodiscard]] operator ResultT() { return DefaultUnreachable(); }

private:
  /// The pointer to the result of this switch statement, once known,
  /// null before that.
  std::optional<ResultT> result;
};

/// Specialization of TypeSwitch for void returning callables.
template <typename T>
class TypeSwitch<T, void>
    : public detail::TypeSwitchBase<TypeSwitch<T, void>, T> {
public:
  /// CRTP base that provides Case() helpers for this TypeSwitch.
  using BaseT = detail::TypeSwitchBase<TypeSwitch<T, void>, T>;
  /// Inherit constructors from the CRTP base.
  using BaseT::BaseT;
  /// Inherit Case() overloads from the CRTP base.
  using BaseT::Case;
  /// Move-construct from another TypeSwitch.
  /// @param other Switch state to move from.
  TypeSwitch(TypeSwitch &&other) = default;

  /// Add a case on the given type.
  /// @param caseFn Callable invoked with the cast value when the case matches.
  /// @return This switch for further chaining.
  template <typename CaseT, typename CallableT>
  TypeSwitch<T, void> &Case(CallableT &&caseFn) {
    if (foundMatch)
      return *this;

    // Check to see if any of the types apply to 'value'.
    if (auto caseValue = BaseT::template castValue<CaseT>(this->value)) {
      caseFn(caseValue);
      foundMatch = true;
    }
    return *this;
  }

  /// As a default, invoke the given callable within the root value.
  /// @param defaultFn Callable invoked with the root value when no case matched.
  template <typename CallableT> void Default(CallableT &&defaultFn) {
    if (!foundMatch)
      defaultFn(this->value);
  }

  /// Declare default as unreachable, making sure that all cases were handled.
  /// @param message Abort message if no case matched.
  void DefaultUnreachable(
      const char *message = "Fell off the end of a type-switch") {
    if (!foundMatch)
      llvm_unreachable(message);
  }

private:
  /// A flag detailing if we have already found a match.
  bool foundMatch = false;
};
} // end namespace llvm

#endif // LLVM_ADT_TYPESWITCH_H
