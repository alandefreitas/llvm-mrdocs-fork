//===- CmpPredicate.h - CmpInst Predicate with samesign information -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A CmpInst::Predicate with any samesign information (applicable to ICmpInst).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CMPPREDICATE_H
#define LLVM_IR_CMPPREDICATE_H

#include "llvm/IR/InstrTypes.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
/// An abstraction over a CmpInst predicate with optional samesign information.
///
/// Covers floating-point predicates and a pack of an integer predicate with
/// samesign information. Some functions in ICmpInst construct and return this
/// type in place of a Predicate.
class CmpPredicate {
  CmpInst::Predicate Pred;
  bool HasSameSign;

public:
  /// Default constructor.
  CmpPredicate() : Pred(CmpInst::BAD_ICMP_PREDICATE), HasSameSign(false) {}

  /// Constructed implictly with a either Predicate and samesign information, or
  /// just a Predicate, dropping samesign information.
  /// \param Pred The comparison predicate.
  /// \param HasSameSign Whether the integer predicate carries samesign.
  CmpPredicate(CmpInst::Predicate Pred, bool HasSameSign = false)
      : Pred(Pred), HasSameSign(HasSameSign) {
    assert(!HasSameSign || CmpInst::isIntPredicate(Pred));
  }

  /// Implictly converts to the underlying Predicate, dropping samesign
  /// information.
  /// \return The underlying Predicate without samesign information.
  operator CmpInst::Predicate() const { return Pred; }

  /// Query samesign information, for optimizations.
  /// \return True if this CmpPredicate carries samesign information.
  bool hasSameSign() const { return HasSameSign; }

  /// Drops samesign information. This is used when the samesign information
  /// should be dropped explicitly.
  /// \return The underlying Predicate without samesign information.
  CmpInst::Predicate dropSameSign() const { return Pred; }

  /// Compares two CmpPredicates taking samesign into account and returns the
  /// canonicalized CmpPredicate if they match. An alternative to operator==.
  ///
  /// For example,
  ///   samesign ult + samesign ult -> samesign ult
  ///   samesign ult + ult -> ult
  ///   samesign ult + slt -> slt
  ///   ult + ult -> ult
  ///   ult + slt -> std::nullopt
  /// \param A The first CmpPredicate.
  /// \param B The second CmpPredicate.
  /// \return The canonicalized matching CmpPredicate, or std::nullopt if they
  /// do not match.
  LLVM_ABI static std::optional<CmpPredicate> getMatching(CmpPredicate A,
                                                          CmpPredicate B);

  /// Attempt to return a signed CmpInst::Predicate from this CmpPredicate.
  ///
  /// If the CmpPredicate has samesign, return ICmpInst::getSignedPredicate,
  /// dropping samesign information. Otherwise, return the predicate, dropping
  /// samesign information.
  /// \return A preferred signed Predicate, without samesign information.
  LLVM_ABI CmpInst::Predicate getPreferredSignedPredicate() const;

  /// An operator== on the underlying Predicate.
  /// \param P The predicate to compare against.
  /// \return True if the underlying Predicate equals \p P.
  bool operator==(CmpInst::Predicate P) const { return Pred == P; }
  /// An operator!= on the underlying Predicate.
  /// \param P The predicate to compare against.
  /// \return True if the underlying Predicate does not equal \p P.
  bool operator!=(CmpInst::Predicate P) const { return Pred != P; }

  /// There is no operator== defined on CmpPredicate. Use getMatching instead to
  /// get the canonicalized matching CmpPredicate.
  /// \param Other Unused; this operator is deleted.
  bool operator==(CmpPredicate Other) const = delete;
  /// There is no operator!= defined on CmpPredicate. Use getMatching instead to
  /// get the canonicalized matching CmpPredicate.
  /// \param Other Unused; this operator is deleted.
  bool operator!=(CmpPredicate Other) const = delete;

  /// Do a ICmpInst::getCmpPredicate() or CmpInst::getPredicate(), as
  /// appropriate.
  /// \param Cmp The compare instruction to query.
  /// \return The CmpPredicate extracted from \p Cmp.
  LLVM_ABI static CmpPredicate get(const CmpInst *Cmp);

  /// Get the inverse predicate of a CmpPredicate.
  /// \param P The CmpPredicate to invert.
  /// \return The inverse of \p P.
  LLVM_ABI static CmpPredicate getInverse(CmpPredicate P);

  /// Get the swapped predicate of a CmpPredicate.
  /// \param P The CmpPredicate to swap.
  /// \return The swapped form of \p P.
  LLVM_ABI static CmpPredicate getSwapped(CmpPredicate P);

  /// Get the swapped predicate of a CmpInst.
  /// \param Cmp The compare instruction whose predicate is swapped.
  /// \return The swapped CmpPredicate of \p Cmp.
  LLVM_ABI static CmpPredicate getSwapped(const CmpInst *Cmp);
};
} // namespace llvm

#endif
