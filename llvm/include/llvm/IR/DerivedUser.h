//===- DerivedUser.h - Base for non-IR Users --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DERIVEDUSER_H
#define LLVM_IR_DERIVEDUSER_H

#include "llvm/IR/User.h"

namespace llvm {

class Type;
class Use;

/// Extension point for User subclasses defined outside of lib/IR.
///
/// All classes outside of lib/IR that wish to inherit from User should instead
/// inherit from DerivedUser. Inheriting from this class is discouraged.
///
/// Generally speaking, Value is the base of a closed class hierarchy that can't
/// be extended by code outside of lib/IR. This class creates a loophole that
/// allows classes outside of lib/IR to extend User to leverage its use/def list
/// machinery.
class DerivedUser : public User {
protected:
  /// Function pointer type used to destroy a DerivedUser instance.
  using  DeleteValueTy = void (*)(DerivedUser *);

private:
  friend class Value;

  DeleteValueTy DeleteValue;

public:
  /// Construct a DerivedUser with the given type, value ID, allocation info,
  /// and deletion callback.
  ///
  /// \param Ty The type of this value.
  /// \param VK The value ID identifying this kind of user.
  /// \param AllocInfo How this user and its operands were allocated.
  /// \param DeleteValue Callback invoked to destroy this DerivedUser.
  DerivedUser(Type *Ty, unsigned VK, AllocInfo AllocInfo,
              DeleteValueTy DeleteValue)
      : User(Ty, VK, AllocInfo), DeleteValue(DeleteValue) {}
};

} // end namespace llvm

#endif // LLVM_IR_DERIVEDUSER_H
