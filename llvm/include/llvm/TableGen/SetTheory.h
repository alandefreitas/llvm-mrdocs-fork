//===- SetTheory.h - Generate ordered sets from DAG expressions -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SetTheory class that computes ordered sets of
// Records from DAG expressions. Operators for standard set operations are
// predefined, and it is possible to add special purpose set operators as well.
//
// The user may define named sets as Records of predefined classes. Set
// expanders can be added to a SetTheory instance to teach it how to find the
// elements of such a named set.
//
// These are the predefined operators. The argument lists can be individual
// elements (defs), other sets (defs of expandable classes), lists, or DAG
// expressions that are evaluated recursively.
//
// - (add S1, S2 ...) Union sets. This is also how sets are created from element
//   lists.
//
// - (sub S1, S2, ...) Set difference. Every element in S1 except for the
//   elements in S2, ...
//
// - (and S1, S2) Set intersection. Every element in S1 that is also in S2.
//
// - (shl S, N) Shift left. Remove the first N elements from S.
//
// - (trunc S, N) Truncate. The first N elements of S.
//
// - (rotl S, N) Rotate left. Same as (add (shl S, N), (trunc S, N)).
//
// - (rotr S, N) Rotate right.
//
// - (decimate S, N) Decimate S by picking every N'th element, starting with
//   the first one. For instance, (decimate S, 2) returns the even elements of
//   S.
//
// - (sequence "Format", From, To, [Stride]) Generate a sequence of defs with
//   printf. For instance, (sequence "R%u", 0, 3) -> [ R0, R1, R2, R3 ] and
//   (sequence "R%u", 20, 30, 5) -> [ R20, R25, R30 ].
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_SETTHEORY_H
#define LLVM_TABLEGEN_SETTHEORY_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/SMLoc.h"
#include <map>
#include <memory>
#include <vector>

namespace llvm {

class DagInit;
class Init;
class Record;

/// Computes ordered sets of Records from DAG expressions.
///
/// Operators for standard set operations are predefined, and special-purpose
/// operators and expanders can be registered to evaluate named sets.
class SetTheory {
public:
  /// Ordered vector of Record pointers produced by set expansion.
  using RecVec = std::vector<const Record *>;
  /// Insertion-ordered set of Record pointers used while evaluating expressions.
  using RecSet = SmallSetVector<const Record *, 16>;

  /// Operator - A callback representing a DAG operator.
  class LLVM_ABI Operator {
    virtual void anchor();

  public:
    /// Destroy this DAG operator.
    virtual ~Operator() = default;

    /// Apply this operator to Expr's arguments and insert the result in Elts.
    ///
    /// \param ST Set theory instance used to evaluate nested expressions.
    /// \param Expr DAG expression whose operator is this callback.
    /// \param Elts Destination set that receives the operator result.
    /// \param Loc Source locations used for diagnostics.
    virtual void apply(SetTheory &ST, const DagInit *Expr, RecSet &Elts,
                       ArrayRef<SMLoc> Loc) = 0;
  };

  /// Callback that expands a Record representing a set into its elements.
  ///
  /// Expanders provide a way for users to define named sets that can be used
  /// in DAG expressions.
  class LLVM_ABI Expander {
    virtual void anchor();

  public:
    /// Destroy this set expander.
    virtual ~Expander() = default;

    /// Expand \p Set into elements and insert them into \p Elts.
    ///
    /// \param ST Set theory instance used to evaluate nested expressions.
    /// \param Set Record representing the named set to expand.
    /// \param Elts Destination set that receives the expanded elements.
    virtual void expand(SetTheory &ST, const Record *Set, RecSet &Elts) = 0;
  };

private:
  // Map set defs to their fully expanded contents. This serves as a memoization
  // cache and it makes it possible to return const references on queries.
  using ExpandMap = std::map<const Record *, RecVec>;
  ExpandMap Expansions;

  // Known DAG operators by name.
  StringMap<std::unique_ptr<Operator>> Operators;

  // Typed expanders by class name.
  StringMap<std::unique_ptr<Expander>> Expanders;

public:
  /// Create a SetTheory instance with only the standard operators.
  LLVM_ABI SetTheory();

  /// Add an expander for Records with the named super class.
  ///
  /// \param ClassName Superclass name that selects Records for this expander.
  /// \param E Expander invoked for matching Records.
  LLVM_ABI void addExpander(StringRef ClassName, std::unique_ptr<Expander> E);

  /// Add an expander for ClassName that evaluates FieldName for set elements.
  ///
  /// That is all that is needed for a class like:
  ///
  ///   class Set<dag d> {
  ///     dag Elts = d;
  ///   }
  ///
  /// \param ClassName Superclass name that selects Records for this expander.
  /// \param FieldName Record field whose value is evaluated as the set.
  LLVM_ABI void addFieldExpander(StringRef ClassName, StringRef FieldName);

  /// Add a DAG operator.
  ///
  /// \param Name Operator name as it appears in DAG expressions.
  /// \param Op Operator callback invoked for matching DAG nodes.
  LLVM_ABI void addOperator(StringRef Name, std::unique_ptr<Operator> Op);

  /// Evaluate Expr and append the resulting set to Elts.
  ///
  /// \param Expr Initializer expression to evaluate as a set.
  /// \param Elts Destination set that receives the evaluated elements.
  /// \param Loc Source locations used for diagnostics.
  LLVM_ABI void evaluate(const Init *Expr, RecSet &Elts, ArrayRef<SMLoc> Loc);

  /// Evaluate a sequence of Inits and append to Elts.
  ///
  /// \param begin Iterator to the first Init to evaluate.
  /// \param end Iterator past the last Init to evaluate.
  /// \param Elts Destination set that receives the evaluated elements.
  /// \param Loc Source locations used for diagnostics.
  template<typename Iter>
  void evaluate(Iter begin, Iter end, RecSet &Elts, ArrayRef<SMLoc> Loc) {
    while (begin != end)
      evaluate(*begin++, Elts, Loc);
  }

  /// Expand a record into a set of elements if possible.
  ///
  /// \param Set Record to expand into ordered set elements.
  /// \return Pointer to the expanded elements, or NULL if Set cannot be
  /// expanded further.
  LLVM_ABI const RecVec *expand(const Record *Set);
};

} // end namespace llvm

#endif // LLVM_TABLEGEN_SETTHEORY_H
