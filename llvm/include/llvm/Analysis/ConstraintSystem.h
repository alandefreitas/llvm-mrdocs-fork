//===- ConstraintSystem.h -  A system of linear constraints. --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CONSTRAINTSYSTEM_H
#define LLVM_ANALYSIS_CONSTRAINTSYSTEM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"

#include <string>

namespace llvm {

class Value;

/// A system of linear constraints of the form 'c >= v1 * c1 + ... + vn * cn'.
class ConstraintSystem {
public:
  /// A coefficient and variable-id pair in a constraint row.
  struct Entry {
    /// Multiplier of the variable, or the constant when Id is 0.
    int64_t Coefficient;
    /// Variable index in the system; 0 denotes the constant term.
    uint16_t Id;

    /// Construct an entry from a coefficient and variable id.
    ///
    /// \param Coefficient Multiplier of the variable, or the constant term.
    /// \param Id Variable index; 0 denotes the constant term.
    Entry(int64_t Coefficient, uint16_t Id)
        : Coefficient(Coefficient), Id(Id) {}
  };

  /// A single constraint of the form 'c >= v1 * c1 + ... + vn * cn'.
  using RowTy = SmallVector<Entry, 8>;

private:
  static int64_t getLastCoefficient(ArrayRef<Entry> R, uint16_t Id) {
    if (R.empty() || R.back().Id != Id)
      return 0;
    return R.back().Coefficient;
  }

  /// Returns true if \p R has an entry for the constant part.
  static bool hasConstantEntry(ArrayRef<Entry> R) {
    return !R.empty() && R.front().Id == 0;
  }

  /// Returns true if \p R does not have an entry for any variable, i.e. it is
  /// of the form 'c >= 0'.
  static bool isConstantOnly(ArrayRef<Entry> R) {
    return R.empty() || (R.size() == 1 && R.front().Id == 0);
  }

  /// Returns the constant part of \p R, which is 0 if \p R does not have an
  /// entry for it.
  static int64_t getConstant(ArrayRef<Entry> R) {
    return hasConstantEntry(R) ? R.front().Coefficient : 0;
  }

  /// Number of variables in the system, not counting the constant part. The
  /// variables use the indices 1 to NumVariables.
  size_t NumVariables = 0;

  /// Current linear constraints in the system.
  /// Each entry represents a constraint like
  ///   c0 >= v0 * c1 + .... + v{n-1} * cn
  SmallVector<RowTy, 4> Constraints;

  /// A map of variables (IR values) to their corresponding index in the
  /// constraint system.
  DenseMap<Value *, unsigned> Value2Index;

  // Eliminate constraints from the system using Fourier–Motzkin elimination.
  bool eliminateUsingFM();

  /// Returns true if there may be a solution for the constraints in the system.
  bool mayHaveSolutionImpl();

  /// Get list of variable names from the Value2Index map.
  SmallVector<std::string> getVarNamesList() const;

public:
  /// Construct an empty constraint system.
  ConstraintSystem() = default;
  /// Construct a system with one variable per function argument.
  ///
  /// \param FunctionArgs IR values to register as variables, in order.
  ConstraintSystem(ArrayRef<Value *> FunctionArgs) {
    NumVariables += FunctionArgs.size();
    for (auto *Arg : FunctionArgs) {
      Value2Index.insert({Arg, Value2Index.size() + 1});
    }
  }
  /// Construct a system from an existing value-to-index map.
  ///
  /// \param Value2Index Map from IR values to their variable indices.
  ConstraintSystem(const DenseMap<Value *, unsigned> &Value2Index)
      : NumVariables(Value2Index.size()), Value2Index(Value2Index) {}

  /// Adds \p R to the system unless it is constant-only.
  ///
  /// Non-zero coefficients are stored and \p NumVars is used to grow the
  /// system's variable count.
  ///
  /// \param R Constraint of the form 'c >= v1 * c1 + ... + vn * cn'.
  /// \param NumVars Number of variables the row may refer to.
  /// \return True if the row was added; false if it was constant-only.
  bool addRow(ArrayRef<Entry> R, size_t NumVars) {
    // If all variable coefficients are 0, the constraint does not provide any
    // usable information.
    if (isConstantOnly(R))
      return false;

    assert(NumVars >= R.back().Id && "NumVars must cover all variables in R");
    NumVariables = std::max(NumVars, NumVariables);
    // Only keep non-zero coefficients; in particular drop the entry for the
    // constant part if it is 0.
    RowTy &NewRow = Constraints.emplace_back();
    for (const Entry &E : R)
      if (E.Coefficient != 0)
        NewRow.push_back(E);
    return true;
  }

  /// Returns the map from IR values to variable indices.
  ///
  /// \return Mutable map from IR values to their variable indices.
  DenseMap<Value *, unsigned> &getValue2Index() { return Value2Index; }
  /// Returns the map from IR values to variable indices.
  ///
  /// \return Const map from IR values to their variable indices.
  const DenseMap<Value *, unsigned> &getValue2Index() const {
    return Value2Index;
  }

  /// Returns true if there may be a solution for the constraints in the system.
  ///
  /// \return True if the system may still have a solution.
  LLVM_ABI bool mayHaveSolution();

  /// Negate constraint \p R by multiplying by -1 and adding 1.
  ///
  /// Returns an empty row on overflow. Does not modify the original row.
  ///
  /// \param R The row of coefficients to be negated.
  /// \return The negated row, or an empty row on overflow.
  static RowTy negate(RowTy R) {
    assert(hasConstantEntry(R) && "row must have a constant entry");
    // The negated constraint R is obtained by multiplying by -1 and adding 1 to
    // the constant.
    if (AddOverflow(R[0].Coefficient, int64_t(1), R[0].Coefficient))
      return {};

    return negateOrEqual(std::move(R));
  }

  /// Multiplies each coefficient in the given row by -1. Returns an empty row
  /// on overflow. Does not modify the original row.
  ///
  /// \param R The row of coefficients to be negated.
  /// \return The negated row, or an empty row on overflow.
  static RowTy negateOrEqual(RowTy R) {
    // The negated constraint R is obtained by multiplying by -1.
    for (Entry &E : R)
      if (MulOverflow(E.Coefficient, int64_t(-1), E.Coefficient))
        return {};
    return R;
  }

  /// Converts the given row to form a strict less than inequality. Returns an
  /// empty row on overflow. Does not modify the original row.
  ///
  /// \param R The row of coefficients to be converted.
  /// \return The converted row, or an empty row on overflow.
  static RowTy toStrictLessThan(RowTy R) {
    assert(hasConstantEntry(R) && "row must have a constant entry");
    // The strict less than is obtained by subtracting 1 from the constant.
    if (SubOverflow(R[0].Coefficient, int64_t(1), R[0].Coefficient))
      return {};
    return R;
  }

  /// Build a sub-system of constraints connected to query \p R.
  ///
  /// The sub-system contains constraints connected (transitively) to \p R,
  /// with variables compacted to a dense index range. Also translate \p R's
  /// entries to the sub-system.
  ///
  /// \param R Query constraint whose connected component is extracted.
  /// \return The connected sub-system and \p R rewritten with dense indices.
  LLVM_ABI std::pair<ConstraintSystem, RowTy>
  getSubSystem(ArrayRef<Entry> R) const;

  /// Returns true if constraint \p R is implied by the current system.
  ///
  /// Adds the negation of \p R to a copy of the system and reports whether
  /// that copy has no solution.
  ///
  /// \param R Constraint to test for implication.
  /// \return True if \p R is implied by the current constraints.
  LLVM_ABI bool isConditionImplied(RowTy R) const;
  /// Returns true if \p R is implied by the connected sub-system.
  ///
  /// Builds the sub-system of constraints transitively connected to \p R and
  /// tests implication there.
  ///
  /// \param R Constraint to test for implication.
  /// \return True if \p R is implied by its connected sub-system.
  LLVM_ABI bool isConditionImpliedInSubSystem(ArrayRef<Entry> R) const;

  /// Returns the most recently added constraint.
  ///
  /// \return The last constraint row in the system.
  const RowTy &getLastConstraint() const {
    assert(!Constraints.empty() && "Constraint system is empty");
    return Constraints.back();
  }

  /// Removes the most recently added constraint.
  void popLastConstraint() { Constraints.pop_back(); }
  /// Drops the last \p N variables from the system's variable count.
  ///
  /// \param N Number of trailing variables to drop.
  void popLastNVariables(unsigned N) {
    assert(NumVariables >= N);
    NumVariables -= N;
  }

  /// Returns the number of rows in the constraint system.
  ///
  /// \return The number of constraint rows currently stored.
  unsigned size() const { return Constraints.size(); }

  /// Print the constraints in the system.
  LLVM_ABI void dump() const;
};
} // namespace llvm

#endif // LLVM_ANALYSIS_CONSTRAINTSYSTEM_H
