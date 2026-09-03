//===- GlobalStatus.h - Compute status info for globals ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_GLOBALSTATUS_H
#define LLVM_TRANSFORMS_UTILS_GLOBALSTATUS_H

#include "llvm/IR/Instructions.h"
#include "llvm/Support/AtomicOrdering.h"

namespace llvm {

class Constant;
class Function;
class Value;

/// Return true if it is safe to destroy constant \p C.
///
/// It is safe to destroy a constant iff it is only used by constants itself.
/// Note that constants cannot be cyclic, so this test is pretty easy to
/// implement recursively.
///
/// \param C Constant whose users are inspected.
/// \return True if \p C can be destroyed.
LLVM_ABI bool isSafeToDestroyConstant(const Constant *C);

/// Status of how a global or thread-local variable is used.
///
/// As we analyze each global or thread-local variable, keep track of some
/// information about it.  If we find out that the address of the global is
/// taken, none of this info will be accurate.
struct GlobalStatus {
  /// True if the global's address is used in a comparison.
  bool IsCompared = false;

  /// True if the global is ever loaded.  If the global isn't ever loaded it
  /// can be deleted.
  bool IsLoaded = false;

  /// Number of stores to the global.
  unsigned NumStores = 0;

  /// Keep track of what stores to the global look like.
  enum StoredType {
    /// There is no store to this global.  It can thus be marked constant.
    NotStored,

    /// This global is stored to, but the only thing stored is the constant it
    /// was initialized with. This is only tracked for scalar globals.
    InitializerStored,

    /// This global is stored to, but only its initializer and one other value
    /// is ever stored to it.  If this global isStoredOnce, we track the value
    /// stored to it via StoredOnceStore below.  This is only tracked for scalar
    /// globals.
    StoredOnce,

    /// This global is stored to by multiple values or something else that we
    /// cannot track.
    Stored
  } StoredType = NotStored;

  /// If only one value (besides the initializer constant) is ever stored to
  /// this global, keep track of what value it is via the store instruction.
  const StoreInst *StoredOnceStore = nullptr;

  /// If only one value (besides the initializer constant) is ever stored to
  /// this global, return the stored value.
  ///
  /// \return The stored value, or nullptr if the global is not StoredOnce or
  ///         there is no StoredOnceStore.
  Value *getStoredOnceValue() const {
    return (StoredType == StoredOnce && StoredOnceStore)
               ? StoredOnceStore->getOperand(0)
               : nullptr;
  }

  /// First function that loads or stores this global, or null.
  ///
  /// These start out null/false.  When the first accessing function is noticed,
  /// it is recorded. When a second different accessing function is noticed,
  /// HasMultipleAccessingFunctions is set to true.
  const Function *AccessingFunction = nullptr;

  /// True if more than one function loads or stores this global.
  bool HasMultipleAccessingFunctions = false;

  /// Set to the strongest atomic ordering requirement.
  AtomicOrdering Ordering = AtomicOrdering::NotAtomic;

  /// Construct a default-initialized GlobalStatus.
  LLVM_ABI GlobalStatus();

  /// Fill in a GlobalStatus from all uses of a global.
  ///
  /// Look at all uses of the global and fill in the GlobalStatus structure.  If
  /// the global has its address taken, return true to indicate we can't do
  /// anything with it.
  ///
  /// \param V Global or thread-local value whose uses are analyzed.
  /// \param GS Status structure to fill in.
  /// \return True if the address of \p V is taken.
  LLVM_ABI static bool analyzeGlobal(const Value *V, GlobalStatus &GS);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_GLOBALSTATUS_H
