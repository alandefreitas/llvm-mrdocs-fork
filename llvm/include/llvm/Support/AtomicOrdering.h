//===-- llvm/Support/AtomicOrdering.h ---Atomic Ordering---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Atomic ordering constants.
///
/// These values are used by LLVM to represent atomic ordering for C++11's
/// memory model and more, as detailed in docs/Atomics.md.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ATOMICORDERING_H
#define LLVM_SUPPORT_ATOMICORDERING_H

#include <cstddef>

namespace llvm {

/// Atomic ordering for C11 / C++11's memory models.
///
/// These values cannot change because they are shared with standard library
/// implementations as well as with other compilers.
enum class AtomicOrderingCABI {
  /// No synchronization or ordering constraints beyond atomicity
  /// (\c memory_order_relaxed).
  relaxed = 0,
  consume = 1,
  acquire = 2,
  release = 3,
  /// Acquire-release ordering for read-modify-write atomics, matching
  /// \c memory_order_acq_rel in C11 and C++11.
  acq_rel = 4,
  seq_cst = 5,
};

/// Ordering comparisons are deleted because C++ memory orders form a lattice,
/// not a linear order; use \c isAtLeastOrStrongerThan instead.
///
/// \param Lhs Left-hand CABI atomic ordering.
/// \param Rhs Right-hand CABI atomic ordering.
bool operator<(AtomicOrderingCABI Lhs, AtomicOrderingCABI Rhs) = delete;
/// Deleted; CABI atomic orders form a lattice, not a total order.
///
/// \param Lhs Left-hand CABI atomic ordering.
/// \param Rhs Right-hand CABI atomic ordering.
bool operator>(AtomicOrderingCABI Lhs, AtomicOrderingCABI Rhs) = delete;
/// Deleted; CABI atomic orders form a lattice, not a total order.
///
/// \param Lhs Left-hand CABI atomic ordering.
/// \param Rhs Right-hand CABI atomic ordering.
bool operator<=(AtomicOrderingCABI Lhs, AtomicOrderingCABI Rhs) = delete;
/// Deleted; CABI atomic orders form a lattice, not a total order.
///
/// \param Lhs Left-hand CABI atomic ordering.
/// \param Rhs Right-hand CABI atomic ordering.
bool operator>=(AtomicOrderingCABI Lhs, AtomicOrderingCABI Rhs) = delete;

// Validate an integral value which isn't known to fit within the enum's range
// is a valid AtomicOrderingCABI.
/// Return true if \p I is a valid \c AtomicOrderingCABI enumerator value.
///
/// \param I Integral value to check against the \c AtomicOrderingCABI range.
/// \return True if \p I is in the \c AtomicOrderingCABI enumerator range.
template <typename Int> inline bool isValidAtomicOrderingCABI(Int I) {
  return (Int)AtomicOrderingCABI::relaxed <= I &&
         I <= (Int)AtomicOrderingCABI::seq_cst;
}

/// Atomic ordering for LLVM's memory model.
///
/// C++ defines ordering as a lattice. LLVM supplements this with NotAtomic and
/// Unordered, which are both below the C++ orders.
///
/// not_atomic-->unordered-->relaxed-->release--------------->acq_rel-->seq_cst
///                                   \-->consume-->acquire--/
enum class AtomicOrdering : unsigned {
  /// Non-atomic access; the default for ordinary LLVM loads and stores.
  NotAtomic = 0,
  /// Weakest atomicity: well-defined under races, but no synchronization.
  Unordered = 1,
  /// Atomic with a consistent per-address order; equivalent to C++ \c relaxed.
  Monotonic = 2,
  // Consume = 3,  // Not specified yet.
  /// Synchronize with prior releases of the same atomic location (load/RMW).
  Acquire = 4,
  /// Make prior stores visible to subsequent acquires of the same location.
  Release = 5,
  /// Read-modify-write operations that both acquire and release, combining the
  /// effects of \c Acquire and \c Release on the same atomic access.
  AcquireRelease = 6,
  SequentiallyConsistent = 7,
  LAST = SequentiallyConsistent
};

/// Deleted; LLVM atomic orders form a lattice, not a total order.
///
/// \param Lhs Left-hand LLVM atomic ordering.
/// \param Rhs Right-hand LLVM atomic ordering.
bool operator<(AtomicOrdering Lhs, AtomicOrdering Rhs) = delete;
/// Deleted; LLVM atomic orders form a lattice, not a total order.
///
/// \param Lhs Left-hand LLVM atomic ordering.
/// \param Rhs Right-hand LLVM atomic ordering.
bool operator>(AtomicOrdering Lhs, AtomicOrdering Rhs) = delete;
/// Deleted; LLVM atomic orders form a lattice, not a total order.
///
/// \param Lhs Left-hand LLVM atomic ordering.
/// \param Rhs Right-hand LLVM atomic ordering.
bool operator<=(AtomicOrdering Lhs, AtomicOrdering Rhs) = delete;
/// Deleted; LLVM atomic orders form a lattice, not a total order.
///
/// \param Lhs Left-hand LLVM atomic ordering.
/// \param Rhs Right-hand LLVM atomic ordering.
bool operator>=(AtomicOrdering Lhs, AtomicOrdering Rhs) = delete;

// Validate an integral value which isn't known to fit within the enum's range
// is a valid AtomicOrdering.
/// Return true if \p I is a valid \c AtomicOrdering enumerator value.
///
/// The unused consume encoding (3) is rejected.
///
/// \param I Integral value to check against the \c AtomicOrdering range.
/// \return True if \p I is a valid \c AtomicOrdering value (not the unused
/// consume encoding).
template <typename Int> inline bool isValidAtomicOrdering(Int I) {
  return static_cast<Int>(AtomicOrdering::NotAtomic) <= I &&
         I <= static_cast<Int>(AtomicOrdering::SequentiallyConsistent) &&
         I != 3;
}

/// String used by LLVM IR to represent atomic ordering.
///
/// \param ao LLVM atomic ordering to convert to an IR mnemonic.
/// \return Null-terminated IR mnemonic for \p ao.
inline const char *toIRString(AtomicOrdering ao) {
  static const char *names[8] = {"not_atomic", "unordered", "monotonic",
                                 "consume",    "acquire",   "release",
                                 "acq_rel",    "seq_cst"};
  return names[static_cast<size_t>(ao)];
}

/// Returns true if ao is stronger than other as defined by the AtomicOrdering
/// lattice, which is based on C++'s definition.
///
/// \param AO Atomic ordering on the left-hand side of the comparison.
/// \param Other Atomic ordering on the right-hand side of the comparison.
/// \return True if \p AO is strictly stronger than \p Other in the lattice.
inline bool isStrongerThan(AtomicOrdering AO, AtomicOrdering Other) {
  static const bool lookup[8][8] = {
      //               NA     UN     RX     CO     AC     RE     AR     SC
      /* NotAtomic */ {false, false, false, false, false, false, false, false},
      /* Unordered */ { true, false, false, false, false, false, false, false},
      /* relaxed   */ { true,  true, false, false, false, false, false, false},
      /* consume   */ { true,  true,  true, false, false, false, false, false},
      /* acquire   */ { true,  true,  true,  true, false, false, false, false},
      /* release   */ { true,  true,  true, false, false, false, false, false},
      /* acq_rel   */ { true,  true,  true,  true,  true,  true, false, false},
      /* seq_cst   */ { true,  true,  true,  true,  true,  true,  true, false},
  };
  return lookup[static_cast<size_t>(AO)][static_cast<size_t>(Other)];
}

/// Return true if \p AO is at least as strong as \p Other in the atomic
/// ordering lattice.
///
/// \param AO Atomic ordering on the left-hand side of the comparison.
/// \param Other Atomic ordering on the right-hand side of the comparison.
/// \return True if \p AO is at least as strong as \p Other in the lattice.
inline bool isAtLeastOrStrongerThan(AtomicOrdering AO, AtomicOrdering Other) {
  static const bool lookup[8][8] = {
      //               NA     UN     RX     CO     AC     RE     AR     SC
      /* NotAtomic */ { true, false, false, false, false, false, false, false},
      /* Unordered */ { true,  true, false, false, false, false, false, false},
      /* relaxed   */ { true,  true,  true, false, false, false, false, false},
      /* consume   */ { true,  true,  true,  true, false, false, false, false},
      /* acquire   */ { true,  true,  true,  true,  true, false, false, false},
      /* release   */ { true,  true,  true, false, false,  true, false, false},
      /* acq_rel   */ { true,  true,  true,  true,  true,  true,  true, false},
      /* seq_cst   */ { true,  true,  true,  true,  true,  true,  true,  true},
  };
  return lookup[static_cast<size_t>(AO)][static_cast<size_t>(Other)];
}

/// Return true if \p AO is strictly stronger than \c Unordered.
///
/// \param AO Atomic ordering to test.
/// \return True if \p AO is strictly stronger than \c Unordered.
inline bool isStrongerThanUnordered(AtomicOrdering AO) {
  return isStrongerThan(AO, AtomicOrdering::Unordered);
}

/// Return true if \p AO is strictly stronger than \c Monotonic.
///
/// \param AO Atomic ordering to test.
/// \return True if \p AO is strictly stronger than \c Monotonic.
inline bool isStrongerThanMonotonic(AtomicOrdering AO) {
  return isStrongerThan(AO, AtomicOrdering::Monotonic);
}

/// Return true if \p AO is \c Acquire or stronger in the atomic ordering
/// lattice.
///
/// \param AO Atomic ordering to test.
/// \return True if \p AO is \c Acquire or stronger in the lattice.
inline bool isAcquireOrStronger(AtomicOrdering AO) {
  return isAtLeastOrStrongerThan(AO, AtomicOrdering::Acquire);
}

/// Return true if \p AO is \c Release or stronger in the atomic ordering
/// lattice.
///
/// \param AO Atomic ordering to test.
/// \return True if \p AO is \c Release or stronger in the lattice.
inline bool isReleaseOrStronger(AtomicOrdering AO) {
  return isAtLeastOrStrongerThan(AO, AtomicOrdering::Release);
}

/// Return a single atomic ordering that is at least as strong as both the \p AO
/// and \p Other orderings for an atomic operation.
///
/// \param AO First atomic ordering to merge.
/// \param Other Second atomic ordering to merge.
/// \return An atomic ordering at least as strong as both \p AO and \p Other.
inline AtomicOrdering getMergedAtomicOrdering(AtomicOrdering AO,
                                              AtomicOrdering Other) {
  if ((AO == AtomicOrdering::Acquire && Other == AtomicOrdering::Release) ||
      (AO == AtomicOrdering::Release && Other == AtomicOrdering::Acquire))
    return AtomicOrdering::AcquireRelease;
  return isStrongerThan(AO, Other) ? AO : Other;
}

/// Map LLVM \p AO to the corresponding C11/C++11 ABI memory-order encoding.
///
/// \param AO LLVM atomic ordering to convert.
/// \return The C11/C++11 ABI memory-order encoding for \p AO.
inline AtomicOrderingCABI toCABI(AtomicOrdering AO) {
  static const AtomicOrderingCABI lookup[8] = {
      /* NotAtomic */ AtomicOrderingCABI::relaxed,
      /* Unordered */ AtomicOrderingCABI::relaxed,
      /* relaxed   */ AtomicOrderingCABI::relaxed,
      /* consume   */ AtomicOrderingCABI::consume,
      /* acquire   */ AtomicOrderingCABI::acquire,
      /* release   */ AtomicOrderingCABI::release,
      /* acq_rel   */ AtomicOrderingCABI::acq_rel,
      /* seq_cst   */ AtomicOrderingCABI::seq_cst,
  };
  return lookup[static_cast<size_t>(AO)];
}

} // end namespace llvm

#endif // LLVM_SUPPORT_ATOMICORDERING_H
