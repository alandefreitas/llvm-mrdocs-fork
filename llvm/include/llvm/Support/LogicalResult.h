//===- LogicalResult.h - Utilities for handling success/failure -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_LOGICALRESULT_H
#define LLVM_SUPPORT_LOGICALRESULT_H

#include <cassert>
#include <optional>

namespace llvm {
/// Efficient success-or-failure status, preferred over ambiguous bool results.
///
/// This class represents an efficient way to signal success or failure. It
/// should be preferred over the use of `bool` when appropriate, as it avoids
/// all of the ambiguity that arises in interpreting a boolean result. This
/// class is marked as NODISCARD to ensure that the result is processed. Users
/// may explicitly discard a result by using `(void)`, e.g.
/// `(void)functionThatReturnsALogicalResult();`. Given the intended nature of
/// this class, it generally shouldn't be used as the result of functions that
/// very frequently have the result ignored. This class is intended to be used
/// in conjunction with the utility functions below.
struct [[nodiscard]] LogicalResult {
public:
  /// If isSuccess is true a `success` result is generated, otherwise a
  /// 'failure' result is generated.
  ///
  /// \param IsSuccess When true, produce success; otherwise failure.
  /// \returns A success result when \p IsSuccess is true; otherwise failure.
  static LogicalResult success(bool IsSuccess = true) {
    return LogicalResult(IsSuccess);
  }

  /// If isFailure is true a `failure` result is generated, otherwise a
  /// 'success' result is generated.
  ///
  /// \param IsFailure When true, produce failure; otherwise success.
  /// \returns A failure result when \p IsFailure is true; otherwise success.
  static LogicalResult failure(bool IsFailure = true) {
    return LogicalResult(!IsFailure);
  }

  /// Returns true if the provided LogicalResult corresponds to a success value.
  ///
  /// \returns True if this is a success result; false if failure.
  constexpr bool succeeded() const { return IsSuccess; }

  /// Returns true if the provided LogicalResult corresponds to a failure value.
  ///
  /// \returns True if this is a failure result; false if success.
  constexpr bool failed() const { return !IsSuccess; }

private:
  LogicalResult(bool IsSuccess) : IsSuccess(IsSuccess) {}

  /// Boolean indicating if this is a success result, if false this is a
  /// failure result.
  bool IsSuccess;
};

/// Utility function to generate a LogicalResult. If isSuccess is true a
/// `success` result is generated, otherwise a 'failure' result is generated.
///
/// \param IsSuccess When true, produce success; otherwise failure.
/// \returns A success result when \p IsSuccess is true; otherwise failure.
inline LogicalResult success(bool IsSuccess = true) {
  return LogicalResult::success(IsSuccess);
}

/// Utility function to generate a LogicalResult. If isFailure is true a
/// `failure` result is generated, otherwise a 'success' result is generated.
///
/// \param IsFailure When true, produce failure; otherwise success.
/// \returns A failure result when \p IsFailure is true; otherwise success.
inline LogicalResult failure(bool IsFailure = true) {
  return LogicalResult::failure(IsFailure);
}

/// Utility function that returns true if the provided LogicalResult corresponds
/// to a success value.
///
/// \param Result Status value to test.
/// \returns True if \p Result is success; false if failure.
inline bool succeeded(LogicalResult Result) { return Result.succeeded(); }

/// Utility function that returns true if the provided LogicalResult corresponds
/// to a failure value.
///
/// \param Result Status value to test.
/// \returns True if \p Result is failure; false if success.
inline bool failed(LogicalResult Result) { return Result.failed(); }

/// Either a failure, or a valid value of type \c T.
///
/// This class provides support for representing a failure result, or a valid
/// value of type `T`. This allows for integrating with LogicalResult, while
/// also providing a value on the success path.
///
/// Inherits the optional storage privately and re-exports the public
/// \c std::optional interface with documentation. Boolean conversion and
/// \c has_value are kept private to avoid confusion with LogicalResult.
template <typename T>
class [[nodiscard]] FailureOr : private std::optional<T> {
private:
  using Base = std::optional<T>;

public:
  /// Contained value type on the success path.
  using value_type = typename Base::value_type;

  /// Constructs a value in place on the success path.
  using Base::emplace;
  /// Clears any stored value, leaving failure.
  using Base::reset;
  /// Swaps the contained state with another optional.
  using Base::swap;
  /// Returns the stored value, or throws if unset.
  using Base::value;
  /// Returns the stored value, or a fallback when unset.
  using Base::value_or;
  /// Accesses the stored value.
  using Base::operator*;
  /// Accesses members of the stored value.
  using Base::operator->;
  /// Assigns from another optional or value.
  using Base::operator=;
#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202110L
  /// Chains a continuation when a value is present.
  using Base::and_then;
  /// Supplies an alternate optional when unset.
  using Base::or_else;
  /// Transforms the stored value when present.
  using Base::transform;
#endif

  /// Allow constructing from a LogicalResult. The result *must* be a failure.
  /// Success results should use a proper instance of type `T`.
  ///
  /// \param Result Failure status used to construct an empty FailureOr.
  FailureOr(LogicalResult Result) {
    assert(failed(Result) &&
           "success should be constructed with an instance of 'T'");
  }
  /// Construct a failure result with no value.
  FailureOr() : FailureOr(failure()) {}
  /// Construct a success result by moving \p Y.
  ///
  /// \param Y Value produced on the success path.
  FailureOr(T &&Y) : Base(std::forward<T>(Y)) {}
  /// Construct a success result by copying \p Y.
  ///
  /// \param Y Value produced on the success path.
  FailureOr(const T &Y) : Base(Y) {}
  /// Construct by converting from another FailureOr of a related type.
  ///
  /// \param Other Source FailureOr to convert from.
  template <typename U,
            std::enable_if_t<std::is_constructible<T, U>::value> * = nullptr>
  FailureOr(const FailureOr<U> &Other)
      : Base(failed(Other) ? Base() : Base(*Other)) {}

  /// Convert to LogicalResult: success when a value is present.
  ///
  /// \returns Success when a value is present; failure otherwise.
  operator LogicalResult() const { return success(this->has_value()); }

private:
  /// Hide the bool conversion as it easily creates confusion.
  using Base::operator bool;
  /// Hide has_value; prefer LogicalResult conversion and succeeded/failed.
  using Base::has_value;
};

/// Wrap a value on the success path in a FailureOr of the same value type.
///
/// \param Y Value to store in the returned FailureOr.
/// \returns A FailureOr containing \p Y on the success path.
template <typename T,
          typename = std::enable_if_t<!std::is_convertible_v<T, bool>>>
inline auto success(T &&Y) {
  return FailureOr<std::decay_t<T>>(std::forward<T>(Y));
}

/// Success/failure status for parsing-style chains using \c ||.
///
/// This class represents success/failure for parsing-like operations that find
/// it important to chain together failable operations with `||`.  This is an
/// extended version of `LogicalResult` that allows for explicit conversion to
/// bool.
///
/// This class should not be used for general error handling cases - we prefer
/// to keep the logic explicit with the `succeeded`/`failed` predicates.
/// However, traditional monadic-style parsing logic can sometimes get
/// swallowed up in boilerplate without this, so we provide this for narrow
/// cases where it is important.
class [[nodiscard]] ParseResult : public LogicalResult {
public:
  /// Construct from a LogicalResult, defaulting to success.
  ///
  /// \param Result Success or failure status to store.
  ParseResult(LogicalResult Result = success()) : LogicalResult(Result) {}

  /// Failure is true in a boolean context.
  ///
  /// \returns True if this result is failure; false on success.
  constexpr explicit operator bool() const { return failed(); }
};
} // namespace llvm

#endif // LLVM_SUPPORT_LOGICALRESULT_H
