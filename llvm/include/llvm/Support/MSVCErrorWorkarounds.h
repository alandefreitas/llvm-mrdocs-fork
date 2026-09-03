//===--- MSVCErrorWorkarounds.h - Enable future<Error> in MSVC --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MSVC's promise/future implementation requires types to be default
// constructible, so this header provides analogues of Error an Expected
// that are default constructed in a safely destructible state.
//
// FIXME: Kill off this header and migrate all users to Error/Expected once we
//        move to MSVC versions that support non-default-constructible types.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MSVCERRORWORKAROUNDS_H
#define LLVM_SUPPORT_MSVCERRORWORKAROUNDS_H

#include "llvm/Support/Error.h"

namespace llvm {

/// A default-constructible Error suitable for use with MSVC's std::future.
class MSVCPError : public Error {
public:
  /// Default-construct a success value in a safely destructible state.
  MSVCPError() { (void)!!*this; }

  /// Move-construct from \p Other.
  ///
  /// \param Other Error to move from.
  MSVCPError(MSVCPError &&Other) : Error(std::move(Other)) {}

  /// Assign from \p Other by moving its contents.
  ///
  /// \param Other Error to assign from (taken by value).
  /// \return A reference to this error.
  MSVCPError &operator=(MSVCPError Other) {
    Error::operator=(std::move(Other));
    return *this;
  }

  /// Construct from an existing \p Err.
  ///
  /// \param Err Error whose payload is adopted.
  MSVCPError(Error Err) : Error(std::move(Err)) {}
};

/// A default-constructible Expected suitable for use with MSVC's std::future.
template <typename T> class MSVCPExpected : public Expected<T> {
public:
  /// Default-construct a success value in a safely destructible state.
  MSVCPExpected()
      : Expected<T>(make_error<StringError>("", inconvertibleErrorCode())) {
    consumeError(this->takeError());
  }

  /// Move-construct from \p Other.
  ///
  /// \param Other Expected value to move from.
  MSVCPExpected(MSVCPExpected &&Other) : Expected<T>(std::move(Other)) {}

  /// Move-assign from \p Other.
  ///
  /// \param Other Expected value to move from.
  /// \return A reference to this Expected.
  MSVCPExpected &operator=(MSVCPExpected &&Other) {
    Expected<T>::operator=(std::move(Other));
    return *this;
  }

  /// Construct an error Expected from \p Err.
  ///
  /// \param Err Failure Error whose payload is stored.
  MSVCPExpected(Error Err) : Expected<T>(std::move(Err)) {}

  /// Construct a success value from \p Val, which must be convertible to T.
  ///
  /// \param Val Value to store on the success path.
  template <typename OtherT>
  MSVCPExpected(
      OtherT &&Val,
      std::enable_if_t<std::is_convertible<OtherT, T>::value> *EnableIf =
          nullptr)
      : Expected<T>(std::move(Val)) {}

  /// Move-construct from an Expected<\p OtherT> convertible to T.
  ///
  /// \param Other Expected value to move from.
  template <class OtherT>
  MSVCPExpected(
      Expected<OtherT> &&Other,
      std::enable_if_t<std::is_convertible<OtherT, T>::value> *EnableIf =
          nullptr)
      : Expected<T>(std::move(Other)) {}

  /// Explicitly move-construct from an Expected<\p OtherT> not convertible to T.
  ///
  /// \param Other Expected value to move from.
  template <class OtherT>
  explicit MSVCPExpected(
      Expected<OtherT> &&Other,
      std::enable_if_t<!std::is_convertible<OtherT, T>::value> *EnableIf =
          nullptr)
      : Expected<T>(std::move(Other)) {}
};

} // end namespace llvm

#endif // LLVM_SUPPORT_MSVCERRORWORKAROUNDS_H
