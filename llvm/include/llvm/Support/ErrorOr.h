//===- llvm/Support/ErrorOr.h - Error Smart Pointer -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Provides ErrorOr<T> smart pointer.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ERROROR_H
#define LLVM_SUPPORT_ERROROR_H

#include <cassert>
#include <functional> // for std::reference_wrapper
#include <system_error>
#include <type_traits>
#include <utility>

namespace llvm {

/// Represents either an error or a value T.
///
/// ErrorOr<T> is a pointer-like class that represents the result of an
/// operation. The result is either an error, or a value of type T. This is
/// designed to emulate the usage of returning a pointer where nullptr indicates
/// failure. However instead of just knowing that the operation failed, we also
/// have an error_code and optional user data that describes why it failed.
///
/// It is used like the following.
/// \code
///   ErrorOr<Buffer> getBuffer();
///
///   auto buffer = getBuffer();
///   if (error_code ec = buffer.getError())
///     return ec;
///   buffer->write("adena");
/// \endcode
///
///
/// Implicit conversion to bool returns true if there is a usable value. The
/// unary * and -> operators provide pointer like access to the value. Accessing
/// the value when there is an error has undefined behavior.
///
/// When T is a reference type the behavior is slightly different. The reference
/// is held in a std::reference_wrapper<std::remove_reference<T>::type>, and
/// there is special handling to make operator -> work as if T was not a
/// reference.
///
/// T cannot be a rvalue reference.
template<class T>
class ErrorOr {
  template <class OtherT> friend class ErrorOr;

  static constexpr bool isRef = std::is_reference_v<T>;

  using wrap = std::reference_wrapper<std::remove_reference_t<T>>;

public:
  /// Storage type for either a value or an error code.
  using storage_type = std::conditional_t<isRef, wrap, T>;

private:
  using reference = std::remove_reference_t<T> &;
  using const_reference = const std::remove_reference_t<T> &;
  using pointer = std::remove_reference_t<T> *;
  using const_pointer = const std::remove_reference_t<T> *;

public:
  template <class E>
  /// Construct an error result from \p ErrorCode.
  ///
  /// \param ErrorCode Error-code or error-condition enum to store.
  /// \param EnableIf SFINAE parameter enabling this overload for error enums.
  ErrorOr(E ErrorCode,
          std::enable_if_t<std::is_error_code_enum<E>::value ||
                               std::is_error_condition_enum<E>::value,
                           void *> EnableIf = nullptr)
      : HasError(true) {
    (void)EnableIf;
    new (getErrorStorage()) std::error_code(make_error_code(ErrorCode));
  }

  /// Construct an error result from \p EC.
  ///
  /// \param EC Error code to store.
  ErrorOr(std::error_code EC) : HasError(true) {
    new (getErrorStorage()) std::error_code(EC);
  }

  template <class OtherT>
  /// Construct a success value from \p Val.
  ///
  /// \param Val Value to store on the success path.
  /// \param EnableIf SFINAE parameter enabling this overload when convertible.
  ErrorOr(OtherT &&Val,
          std::enable_if_t<std::is_convertible_v<OtherT, T>> *EnableIf =
              nullptr)
      : HasError(false) {
    (void)EnableIf;
    new (getStorage()) storage_type(std::forward<OtherT>(Val));
  }

  /// Copy-construct from \p Other.
  ///
  /// \param Other ErrorOr to copy from.
  ErrorOr(const ErrorOr &Other) {
    copyConstruct(Other);
  }

  template <class OtherT>
  /// Copy-construct from \p Other.
  ///
  /// \param Other ErrorOr to copy from.
  /// \param EnableIf SFINAE parameter enabling this overload when convertible.
  ErrorOr(const ErrorOr<OtherT> &Other,
          std::enable_if_t<std::is_convertible_v<OtherT, T>> *EnableIf =
              nullptr) {
    (void)EnableIf;
    copyConstruct(Other);
  }

  template <class OtherT>
  /// Explicit copy-construct from an incompatible \p Other.
  ///
  /// \param Other ErrorOr to copy from.
  /// \param EnableIf SFINAE parameter enabling this overload when not convertible.
  explicit ErrorOr(
      const ErrorOr<OtherT> &Other,
      std::enable_if_t<!std::is_convertible_v<OtherT, const T &>> *EnableIf =
          nullptr) {
    (void)EnableIf;
    copyConstruct(Other);
  }

  /// Move-construct from \p Other.
  ///
  /// \param Other ErrorOr to move from.
  ErrorOr(ErrorOr &&Other) {
    moveConstruct(std::move(Other));
  }

  template <class OtherT>
  /// Move-construct from \p Other.
  ///
  /// \param Other ErrorOr to move from.
  /// \param EnableIf SFINAE parameter enabling this overload when convertible.
  ErrorOr(ErrorOr<OtherT> &&Other,
          std::enable_if_t<std::is_convertible_v<OtherT, T>> *EnableIf =
              nullptr) {
    (void)EnableIf;
    moveConstruct(std::move(Other));
  }

  // This might eventually need SFINAE but it's more complex than is_convertible
  // & I'm too lazy to write it right now.
  template <class OtherT>
  /// Explicit move-construct from an incompatible \p Other.
  ///
  /// \param Other ErrorOr to move from.
  /// \param EnableIf SFINAE parameter enabling this overload when not convertible.
  explicit ErrorOr(
      ErrorOr<OtherT> &&Other,
      std::enable_if_t<!std::is_convertible_v<OtherT, T>> *EnableIf = nullptr) {
    (void)EnableIf;
    moveConstruct(std::move(Other));
  }

  /// Copy-assign from \p Other, replacing the contained value or error.
  ///
  /// \param Other ErrorOr to copy from.
  /// \returns A reference to this ErrorOr.
  ErrorOr &operator=(const ErrorOr &Other) {
    copyAssign(Other);
    return *this;
  }

  /// Move-assign from \p Other, replacing the contained value or error.
  ///
  /// \param Other ErrorOr to move from.
  /// \returns A reference to this ErrorOr.
  ErrorOr &operator=(ErrorOr &&Other) {
    moveAssign(std::move(Other));
    return *this;
  }

  /// Destroy the contained value or error code.
  ~ErrorOr() {
    if (!HasError)
      getStorage()->~storage_type();
  }

  /// Return false if there is an error.
  ///
  /// \returns True if a usable value is present; false on error.
  explicit operator bool() const {
    return !HasError;
  }

  /// Return a mutable reference to the stored value.
  ///
  /// \returns A mutable reference to the stored value.
  reference get() { return *getStorage(); }
  /// Return a const reference to the stored value.
  ///
  /// \returns A const reference to the stored value.
  const_reference get() const { return const_cast<ErrorOr<T> *>(this)->get(); }

  /// Return the stored error code, or a default-constructed code on success.
  ///
  /// \returns The stored error code, or a default-constructed code on success.
  std::error_code getError() const {
    return HasError ? *getErrorStorage() : std::error_code();
  }

  /// Return a pointer to the stored value.
  ///
  /// \returns A pointer to the stored value.
  pointer operator ->() {
    return toPointer(getStorage());
  }

  /// Return a const pointer to the stored value.
  ///
  /// \returns A const pointer to the stored value.
  const_pointer operator->() const { return toPointer(getStorage()); }

  /// Return a reference to the stored value.
  ///
  /// \returns A mutable reference to the stored value.
  reference operator *() {
    return *getStorage();
  }

  /// Return a const reference to the stored value.
  ///
  /// \returns A const reference to the stored value.
  const_reference operator*() const { return *getStorage(); }

private:
  template <class OtherT>
  void copyConstruct(const ErrorOr<OtherT> &Other) {
    if (!Other.HasError) {
      // Get the other value.
      HasError = false;
      new (getStorage()) storage_type(*Other.getStorage());
    } else {
      // Get other's error.
      HasError = true;
      new (getErrorStorage()) std::error_code(Other.getError());
    }
  }

  template <class T1>
  static bool compareThisIfSameType(const T1 &a, const T1 &b) {
    return &a == &b;
  }

  template <class T1, class T2>
  static bool compareThisIfSameType(const T1 &, const T2 &) {
    return false;
  }

  template <class OtherT>
  void copyAssign(const ErrorOr<OtherT> &Other) {
    if (compareThisIfSameType(*this, Other))
      return;

    this->~ErrorOr();
    new (this) ErrorOr(Other);
  }

  template <class OtherT>
  void moveConstruct(ErrorOr<OtherT> &&Other) {
    if (!Other.HasError) {
      // Get the other value.
      HasError = false;
      new (getStorage()) storage_type(std::move(*Other.getStorage()));
    } else {
      // Get other's error.
      HasError = true;
      new (getErrorStorage()) std::error_code(Other.getError());
    }
  }

  template <class OtherT>
  void moveAssign(ErrorOr<OtherT> &&Other) {
    if (compareThisIfSameType(*this, Other))
      return;

    this->~ErrorOr();
    new (this) ErrorOr(std::move(Other));
  }

  pointer toPointer(pointer Val) {
    return Val;
  }

  const_pointer toPointer(const_pointer Val) const { return Val; }

  pointer toPointer(wrap *Val) {
    return &Val->get();
  }

  const_pointer toPointer(const wrap *Val) const { return &Val->get(); }

  storage_type *getStorage() {
    assert(!HasError && "Cannot get value when an error exists!");
    return &TStorage;
  }

  const storage_type *getStorage() const {
    assert(!HasError && "Cannot get value when an error exists!");
    return &TStorage;
  }

  std::error_code *getErrorStorage() {
    assert(HasError && "Cannot get error when a value exists!");
    return &ErrorStorage;
  }

  const std::error_code *getErrorStorage() const {
    assert(HasError && "Cannot get error when a value exists!");
    return &ErrorStorage;
  }

  union {
    /// Storage for the successful value.
    storage_type TStorage;
    /// Storage for the error code on failure.
    std::error_code ErrorStorage;
  };
  bool HasError : 1;
};

template <class T, class E>
std::enable_if_t<std::is_error_code_enum<E>::value ||
                     std::is_error_condition_enum<E>::value,
                 bool>
/// Return true if \p Err holds error-code enum \p Code.
///
/// \param Err ErrorOr whose stored error is compared.
/// \param Code Error-code or error-condition enum to compare against.
/// \returns True if \p Err's error equals \p Code.
operator==(const ErrorOr<T> &Err, E Code) {
  return Err.getError() == Code;
}

} // end namespace llvm

#endif // LLVM_SUPPORT_ERROROR_H
