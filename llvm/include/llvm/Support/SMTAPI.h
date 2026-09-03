//===- SMTAPI.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a SMT generic Solver API, which will be the base class
//  for every SMT solver specific class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SMTAPI_H
#define LLVM_SUPPORT_SMTAPI_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {

/// Generic base class for SMT sorts
class SMTSort {
public:
  /// Construct an empty SMT sort.
  SMTSort() = default;
  /// Destroy this SMT sort.
  virtual ~SMTSort() = default;

  /// Returns true if the sort is a bitvector, calls isBitvectorSortImpl().
  ///
  /// \return True if the sort is a bitvector.
  virtual bool isBitvectorSort() const { return isBitvectorSortImpl(); }

  /// Returns true if the sort is a floating-point, calls isFloatSortImpl().
  ///
  /// \return True if the sort is a floating-point sort.
  virtual bool isFloatSort() const { return isFloatSortImpl(); }

  /// Returns true if the sort is a boolean, calls isBooleanSortImpl().
  ///
  /// \return True if the sort is a boolean sort.
  virtual bool isBooleanSort() const { return isBooleanSortImpl(); }

  /// Returns the bitvector size, fails if the sort is not a bitvector
  /// Calls getBitvectorSortSizeImpl().
  ///
  /// \return The bitvector size in bits.
  virtual unsigned getBitvectorSortSize() const {
    assert(isBitvectorSort() && "Not a bitvector sort!");
    unsigned Size = getBitvectorSortSizeImpl();
    assert(Size && "Size is zero!");
    return Size;
  };

  /// Returns the floating-point size, fails if the sort is not a floating-point
  /// Calls getFloatSortSizeImpl().
  ///
  /// \return The floating-point size in bits.
  virtual unsigned getFloatSortSize() const {
    assert(isFloatSort() && "Not a floating-point sort!");
    unsigned Size = getFloatSortSizeImpl();
    assert(Size && "Size is zero!");
    return Size;
  };

  /// Profile this sort into a FoldingSet node ID.
  ///
  /// \param ID FoldingSet node ID to profile into.
  virtual void Profile(llvm::FoldingSetNodeID &ID) const = 0;

  /// Compare this sort with \p Other by FoldingSet profile order.
  ///
  /// \param Other Sort to compare against.
  /// \return True if this sort sorts before \p Other.
  bool operator<(const SMTSort &Other) const {
    llvm::FoldingSetNodeID ID1, ID2;
    Profile(ID1);
    Other.Profile(ID2);
    return ID1 < ID2;
  }

  /// Return true if \p LHS and \p RHS are equal sorts.
  ///
  /// \param LHS Left-hand sort.
  /// \param RHS Right-hand sort.
  /// \return True if \p LHS and \p RHS are equal sorts.
  friend bool operator==(SMTSort const &LHS, SMTSort const &RHS) {
    return LHS.equal_to(RHS);
  }

  /// Print this sort to an output stream.
  ///
  /// \param OS Stream to print to.
  virtual void print(raw_ostream &OS) const = 0;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this sort to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif

protected:
  /// Query the SMT solver and returns true if two sorts are equal (same kind
  /// and bit width). This does not check if the two sorts are the same objects.
  ///
  /// \param other Sort to compare against.
  /// \return True if the sorts are equal in kind and bit width.
  virtual bool equal_to(SMTSort const &other) const = 0;

  /// Query the SMT solver and checks if a sort is bitvector.
  ///
  /// \return True if the sort is a bitvector.
  virtual bool isBitvectorSortImpl() const = 0;

  /// Query the SMT solver and checks if a sort is floating-point.
  ///
  /// \return True if the sort is a floating-point sort.
  virtual bool isFloatSortImpl() const = 0;

  /// Query the SMT solver and checks if a sort is boolean.
  ///
  /// \return True if the sort is a boolean sort.
  virtual bool isBooleanSortImpl() const = 0;

  /// Query the SMT solver and returns the sort bit width.
  ///
  /// \return The bitvector sort bit width.
  virtual unsigned getBitvectorSortSizeImpl() const = 0;

  /// Query the SMT solver and returns the sort bit width.
  ///
  /// \return The floating-point sort bit width.
  virtual unsigned getFloatSortSizeImpl() const = 0;
};

/// Shared pointer for SMTSorts, used by SMTSolver API.
using SMTSortRef = const SMTSort *;

/// Generic base class for SMT exprs
class SMTExpr {
public:
  /// Construct an empty SMT expression.
  SMTExpr() = default;
  /// Destroy this SMT expression.
  virtual ~SMTExpr() = default;

  /// Compare this expression with \p Other by FoldingSet profile order.
  ///
  /// \param Other Expression to compare against.
  /// \return True if this expression sorts before \p Other.
  bool operator<(const SMTExpr &Other) const {
    llvm::FoldingSetNodeID ID1, ID2;
    Profile(ID1);
    Other.Profile(ID2);
    return ID1 < ID2;
  }

  /// Profile this expression into a FoldingSet node ID.
  ///
  /// \param ID FoldingSet node ID to profile into.
  virtual void Profile(llvm::FoldingSetNodeID &ID) const = 0;

  /// Return true if \p LHS and \p RHS are equal expressions.
  ///
  /// \param LHS Left-hand expression.
  /// \param RHS Right-hand expression.
  /// \return True if \p LHS and \p RHS are equal expressions.
  friend bool operator==(SMTExpr const &LHS, SMTExpr const &RHS) {
    return LHS.equal_to(RHS);
  }

  /// Print this expression to an output stream.
  ///
  /// \param OS Stream to print to.
  virtual void print(raw_ostream &OS) const = 0;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this expression to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif

protected:
  /// Query the SMT solver and returns true if two sorts are equal (same kind
  /// and bit width). This does not check if the two sorts are the same objects.
  ///
  /// \param other Expression to compare against.
  /// \return True if the expressions are equal.
  virtual bool equal_to(SMTExpr const &other) const = 0;
};

/// Statistics collected from an SMT solver run.
class SMTSolverStatistics {
public:
  /// Construct empty solver statistics.
  SMTSolverStatistics() = default;
  /// Destroy these solver statistics.
  virtual ~SMTSolverStatistics() = default;

  /// Return a double-valued statistic by name.
  ///
  /// \param Name Statistic key to look up.
  /// \return The double-valued statistic for \p Name.
  virtual double getDouble(llvm::StringRef Name) const = 0;
  /// Return an unsigned statistic by name.
  ///
  /// \param Name Statistic key to look up.
  /// \return The unsigned statistic for \p Name.
  virtual unsigned getUnsigned(llvm::StringRef Name) const = 0;

  /// Print these statistics to an output stream.
  ///
  /// \param OS Stream to print to.
  virtual void print(raw_ostream &OS) const = 0;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump these statistics to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif
};

/// Shared pointer for SMTExprs, used by SMTSolver API.
using SMTExprRef = const SMTExpr *;

/// Generic base class for SMT Solvers
///
/// This class is responsible for wrapping all sorts and expression generation,
/// through the mk* methods. It also provides methods to create SMT expressions
/// straight from clang's AST, through the from* methods.
class SMTSolver {
public:
  /// Construct an empty SMT solver.
  SMTSolver() = default;
  /// Destroy this SMT solver.
  virtual ~SMTSolver() = default;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this solver to the debug stream.
  LLVM_DUMP_METHOD void dump() const;
#endif

  /// Return an appropriate floating-point sort for the given bitwidth.
  ///
  /// \param BitWidth Floating-point bit width (16, 32, 64, or 128).
  /// \return The floating-point sort for \p BitWidth.
  SMTSortRef getFloatSort(unsigned BitWidth) {
    switch (BitWidth) {
    case 16:
      return getFloat16Sort();
    case 32:
      return getFloat32Sort();
    case 64:
      return getFloat64Sort();
    case 128:
      return getFloat128Sort();
    default:;
    }
    llvm_unreachable("Unsupported floating-point bitwidth!");
  }

  /// Return a boolean sort.
  ///
  /// \return A boolean sort.
  virtual SMTSortRef getBoolSort() = 0;

  /// Return an appropriate bitvector sort for the given bitwidth.
  ///
  /// \param BitWidth Bitvector width in bits.
  /// \return A bitvector sort of width \p BitWidth.
  virtual SMTSortRef getBitvectorSort(const unsigned BitWidth) = 0;

  /// Return a floating-point sort of width 16.
  ///
  /// \return A 16-bit floating-point sort.
  virtual SMTSortRef getFloat16Sort() = 0;

  /// Return a floating-point sort of width 32.
  ///
  /// \return A 32-bit floating-point sort.
  virtual SMTSortRef getFloat32Sort() = 0;

  /// Return a floating-point sort of width 64.
  ///
  /// \return A 64-bit floating-point sort.
  virtual SMTSortRef getFloat64Sort() = 0;

  /// Return a floating-point sort of width 128.
  ///
  /// \return A 128-bit floating-point sort.
  virtual SMTSortRef getFloat128Sort() = 0;

  /// Return an appropriate sort for the given expression.
  ///
  /// \param AST Expression whose sort is requested.
  /// \return The sort of \p AST.
  virtual SMTSortRef getSort(const SMTExprRef &AST) = 0;

  /// Given a constraint, adds it to the solver
  ///
  /// \param Exp Constraint expression to add.
  virtual void addConstraint(const SMTExprRef &Exp) const = 0;

  /// Creates a bitvector addition operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for bitvector addition.
  virtual SMTExprRef mkBVAdd(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector subtraction operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for bitvector subtraction.
  virtual SMTExprRef mkBVSub(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector multiplication operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for bitvector multiplication.
  virtual SMTExprRef mkBVMul(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed modulus operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for signed bitvector remainder.
  virtual SMTExprRef mkBVSRem(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned modulus operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for unsigned bitvector remainder.
  virtual SMTExprRef mkBVURem(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed division operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for signed bitvector division.
  virtual SMTExprRef mkBVSDiv(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned division operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for unsigned bitvector division.
  virtual SMTExprRef mkBVUDiv(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector logical shift left operation
  ///
  /// \param LHS Value to shift.
  /// \param RHS Shift amount.
  /// \return An SMT expression for bitvector logical left shift.
  virtual SMTExprRef mkBVShl(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector arithmetic shift right operation
  ///
  /// \param LHS Value to shift.
  /// \param RHS Shift amount.
  /// \return An SMT expression for bitvector arithmetic right shift.
  virtual SMTExprRef mkBVAshr(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector logical shift right operation
  ///
  /// \param LHS Value to shift.
  /// \param RHS Shift amount.
  /// \return An SMT expression for bitvector logical right shift.
  virtual SMTExprRef mkBVLshr(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector negation operation
  ///
  /// \param Exp Bitvector expression to negate.
  /// \return An SMT expression for bitvector negation.
  virtual SMTExprRef mkBVNeg(const SMTExprRef &Exp) = 0;

  /// Creates a bitvector not operation
  ///
  /// \param Exp Bitvector expression to complement.
  /// \return An SMT expression for bitvector bitwise not.
  virtual SMTExprRef mkBVNot(const SMTExprRef &Exp) = 0;

  /// Creates a bitvector xor operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for bitvector xor.
  virtual SMTExprRef mkBVXor(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector or operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for bitvector or.
  virtual SMTExprRef mkBVOr(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector and operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for bitvector and.
  virtual SMTExprRef mkBVAnd(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned less-than operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for unsigned bitvector less-than.
  virtual SMTExprRef mkBVUlt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed less-than operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for signed bitvector less-than.
  virtual SMTExprRef mkBVSlt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned greater-than operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for unsigned bitvector greater-than.
  virtual SMTExprRef mkBVUgt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed greater-than operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for signed bitvector greater-than.
  virtual SMTExprRef mkBVSgt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned less-equal-than operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for unsigned bitvector less-equal.
  virtual SMTExprRef mkBVUle(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed less-equal-than operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for signed bitvector less-equal.
  virtual SMTExprRef mkBVSle(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector unsigned greater-equal-than operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for unsigned bitvector greater-equal.
  virtual SMTExprRef mkBVUge(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a bitvector signed greater-equal-than operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return An SMT expression for signed bitvector greater-equal.
  virtual SMTExprRef mkBVSge(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a boolean not operation
  ///
  /// \param Exp Boolean expression to negate.
  /// \return An SMT expression for boolean not.
  virtual SMTExprRef mkNot(const SMTExprRef &Exp) = 0;

  /// Creates a boolean equality operation
  ///
  /// \param LHS Left-hand operand.
  /// \param RHS Right-hand operand.
  /// \return An SMT expression for equality.
  virtual SMTExprRef mkEqual(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a boolean and operation
  ///
  /// \param LHS Left-hand boolean operand.
  /// \param RHS Right-hand boolean operand.
  /// \return An SMT expression for boolean and.
  virtual SMTExprRef mkAnd(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a boolean or operation
  ///
  /// \param LHS Left-hand boolean operand.
  /// \param RHS Right-hand boolean operand.
  /// \return An SMT expression for boolean or.
  virtual SMTExprRef mkOr(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a boolean ite operation
  ///
  /// \param Cond Condition expression.
  /// \param T Expression selected when \p Cond is true.
  /// \param F Expression selected when \p Cond is false.
  /// \return An SMT expression for the if-then-else.
  virtual SMTExprRef mkIte(const SMTExprRef &Cond, const SMTExprRef &T,
                           const SMTExprRef &F) = 0;

  /// Creates a bitvector sign extension operation
  ///
  /// \param i Number of bits to extend by.
  /// \param Exp Bitvector expression to extend.
  /// \return An SMT expression for the sign-extended bitvector.
  virtual SMTExprRef mkBVSignExt(unsigned i, const SMTExprRef &Exp) = 0;

  /// Creates a bitvector zero extension operation
  ///
  /// \param i Number of bits to extend by.
  /// \param Exp Bitvector expression to extend.
  /// \return An SMT expression for the zero-extended bitvector.
  virtual SMTExprRef mkBVZeroExt(unsigned i, const SMTExprRef &Exp) = 0;

  /// Creates a bitvector extract operation
  ///
  /// \param High High bit index (inclusive).
  /// \param Low Low bit index (inclusive).
  /// \param Exp Bitvector expression to extract from.
  /// \return An SMT expression for the extracted bitvector slice.
  virtual SMTExprRef mkBVExtract(unsigned High, unsigned Low,
                                 const SMTExprRef &Exp) = 0;

  /// Creates a bitvector concat operation
  ///
  /// \param LHS High-order bitvector operand.
  /// \param RHS Low-order bitvector operand.
  /// \return An SMT expression for the concatenated bitvector.
  virtual SMTExprRef mkBVConcat(const SMTExprRef &LHS,
                                const SMTExprRef &RHS) = 0;

  /// Creates a predicate that checks for overflow in a bitvector addition
  /// operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \param isSigned Whether to interpret the operands as signed.
  /// \return A predicate expression that is true when addition does not overflow.
  virtual SMTExprRef mkBVAddNoOverflow(const SMTExprRef &LHS,
                                       const SMTExprRef &RHS,
                                       bool isSigned) = 0;

  /// Creates a predicate that checks for underflow in a signed bitvector
  /// addition operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return A predicate expression that is true when signed addition does not underflow.
  virtual SMTExprRef mkBVAddNoUnderflow(const SMTExprRef &LHS,
                                        const SMTExprRef &RHS) = 0;

  /// Creates a predicate that checks for overflow in a signed bitvector
  /// subtraction operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return A predicate expression that is true when signed subtraction does not overflow.
  virtual SMTExprRef mkBVSubNoOverflow(const SMTExprRef &LHS,
                                       const SMTExprRef &RHS) = 0;

  /// Creates a predicate that checks for underflow in a bitvector subtraction
  /// operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \param isSigned Whether to interpret the operands as signed.
  /// \return A predicate expression that is true when subtraction does not underflow.
  virtual SMTExprRef mkBVSubNoUnderflow(const SMTExprRef &LHS,
                                        const SMTExprRef &RHS,
                                        bool isSigned) = 0;

  /// Creates a predicate that checks for overflow in a signed bitvector
  /// division/modulus operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return A predicate expression that is true when signed division does not overflow.
  virtual SMTExprRef mkBVSDivNoOverflow(const SMTExprRef &LHS,
                                        const SMTExprRef &RHS) = 0;

  /// Creates a predicate that checks for overflow in a bitvector negation
  /// operation
  ///
  /// \param Exp Bitvector expression to check.
  /// \return A predicate expression that is true when negation does not overflow.
  virtual SMTExprRef mkBVNegNoOverflow(const SMTExprRef &Exp) = 0;

  /// Creates a predicate that checks for overflow in a bitvector multiplication
  /// operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \param isSigned Whether to interpret the operands as signed.
  /// \return A predicate expression that is true when multiplication does not overflow.
  virtual SMTExprRef mkBVMulNoOverflow(const SMTExprRef &LHS,
                                       const SMTExprRef &RHS,
                                       bool isSigned) = 0;

  /// Creates a predicate that checks for underflow in a signed bitvector
  /// multiplication operation
  ///
  /// \param LHS Left-hand bitvector operand.
  /// \param RHS Right-hand bitvector operand.
  /// \return A predicate expression that is true when signed multiplication does not underflow.
  virtual SMTExprRef mkBVMulNoUnderflow(const SMTExprRef &LHS,
                                        const SMTExprRef &RHS) = 0;

  /// Creates a floating-point negation operation
  ///
  /// \param Exp Floating-point expression to negate.
  /// \return An SMT expression for floating-point negation.
  virtual SMTExprRef mkFPNeg(const SMTExprRef &Exp) = 0;

  /// Creates a floating-point isInfinite operation
  ///
  /// \param Exp Floating-point expression to test.
  /// \return An SMT expression testing whether the value is infinite.
  virtual SMTExprRef mkFPIsInfinite(const SMTExprRef &Exp) = 0;

  /// Creates a floating-point isNaN operation
  ///
  /// \param Exp Floating-point expression to test.
  /// \return An SMT expression testing whether the value is NaN.
  virtual SMTExprRef mkFPIsNaN(const SMTExprRef &Exp) = 0;

  /// Creates a floating-point isNormal operation
  ///
  /// \param Exp Floating-point expression to test.
  /// \return An SMT expression testing whether the value is normal.
  virtual SMTExprRef mkFPIsNormal(const SMTExprRef &Exp) = 0;

  /// Creates a floating-point isZero operation
  ///
  /// \param Exp Floating-point expression to test.
  /// \return An SMT expression testing whether the value is zero.
  virtual SMTExprRef mkFPIsZero(const SMTExprRef &Exp) = 0;

  /// Creates a floating-point multiplication operation
  ///
  /// \param LHS Left-hand floating-point operand.
  /// \param RHS Right-hand floating-point operand.
  /// \return An SMT expression for floating-point multiplication.
  virtual SMTExprRef mkFPMul(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point division operation
  ///
  /// \param LHS Left-hand floating-point operand.
  /// \param RHS Right-hand floating-point operand.
  /// \return An SMT expression for floating-point division.
  virtual SMTExprRef mkFPDiv(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point remainder operation
  ///
  /// \param LHS Left-hand floating-point operand.
  /// \param RHS Right-hand floating-point operand.
  /// \return An SMT expression for floating-point remainder.
  virtual SMTExprRef mkFPRem(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point addition operation
  ///
  /// \param LHS Left-hand floating-point operand.
  /// \param RHS Right-hand floating-point operand.
  /// \return An SMT expression for floating-point addition.
  virtual SMTExprRef mkFPAdd(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point subtraction operation
  ///
  /// \param LHS Left-hand floating-point operand.
  /// \param RHS Right-hand floating-point operand.
  /// \return An SMT expression for floating-point subtraction.
  virtual SMTExprRef mkFPSub(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point less-than operation
  ///
  /// \param LHS Left-hand floating-point operand.
  /// \param RHS Right-hand floating-point operand.
  /// \return An SMT expression for floating-point less-than.
  virtual SMTExprRef mkFPLt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point greater-than operation
  ///
  /// \param LHS Left-hand floating-point operand.
  /// \param RHS Right-hand floating-point operand.
  /// \return An SMT expression for floating-point greater-than.
  virtual SMTExprRef mkFPGt(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point less-than-or-equal operation
  ///
  /// \param LHS Left-hand floating-point operand.
  /// \param RHS Right-hand floating-point operand.
  /// \return An SMT expression for floating-point less-than-or-equal.
  virtual SMTExprRef mkFPLe(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point greater-than-or-equal operation
  ///
  /// \param LHS Left-hand floating-point operand.
  /// \param RHS Right-hand floating-point operand.
  /// \return An SMT expression for floating-point greater-than-or-equal.
  virtual SMTExprRef mkFPGe(const SMTExprRef &LHS, const SMTExprRef &RHS) = 0;

  /// Creates a floating-point equality operation
  ///
  /// \param LHS Left-hand floating-point operand.
  /// \param RHS Right-hand floating-point operand.
  /// \return An SMT expression for floating-point equality.
  virtual SMTExprRef mkFPEqual(const SMTExprRef &LHS,
                               const SMTExprRef &RHS) = 0;

  /// Creates a floating-point conversion from floatint-point to floating-point
  /// operation
  ///
  /// \param From Source floating-point expression.
  /// \param To Destination floating-point sort.
  /// \return An SMT expression converting \p From to sort \p To.
  virtual SMTExprRef mkFPtoFP(const SMTExprRef &From, const SMTSortRef &To) = 0;

  /// Creates a floating-point conversion from signed bitvector to
  /// floatint-point operation
  ///
  /// \param From Source signed bitvector expression.
  /// \param To Destination floating-point sort.
  /// \return An SMT expression converting \p From to floating-point sort \p To.
  virtual SMTExprRef mkSBVtoFP(const SMTExprRef &From,
                               const SMTSortRef &To) = 0;

  /// Creates a floating-point conversion from unsigned bitvector to
  /// floatint-point operation
  ///
  /// \param From Source unsigned bitvector expression.
  /// \param To Destination floating-point sort.
  /// \return An SMT expression converting \p From to floating-point sort \p To.
  virtual SMTExprRef mkUBVtoFP(const SMTExprRef &From,
                               const SMTSortRef &To) = 0;

  /// Creates a floating-point conversion from floatint-point to signed
  /// bitvector operation
  ///
  /// \param From Source floating-point expression.
  /// \param ToWidth Destination signed bitvector width.
  /// \return An SMT expression converting \p From to a signed bitvector.
  virtual SMTExprRef mkFPtoSBV(const SMTExprRef &From, unsigned ToWidth) = 0;

  /// Creates a floating-point conversion from floatint-point to unsigned
  /// bitvector operation
  ///
  /// \param From Source floating-point expression.
  /// \param ToWidth Destination unsigned bitvector width.
  /// \return An SMT expression converting \p From to an unsigned bitvector.
  virtual SMTExprRef mkFPtoUBV(const SMTExprRef &From, unsigned ToWidth) = 0;

  /// Creates a new symbol, given a name and a sort
  ///
  /// \param Name Symbol name.
  /// \param Sort Sort of the symbol.
  /// \return An SMT expression for the named symbol.
  virtual SMTExprRef mkSymbol(const char *Name, SMTSortRef Sort) = 0;

  /// Return an appropriate floating-point rounding mode.
  ///
  /// \return An SMT expression for the floating-point rounding mode.
  virtual SMTExprRef getFloatRoundingMode() = 0;

  /// If a model is available, return the value of a given bitvector symbol.
  ///
  /// \param Exp Bitvector symbol to interpret.
  /// \param BitWidth Bit width of the result.
  /// \param isUnsigned Whether to interpret the value as unsigned.
  /// \return The bitvector value from the model.
  virtual llvm::APSInt getBitvector(const SMTExprRef &Exp, unsigned BitWidth,
                                    bool isUnsigned) = 0;

  /// If a model is available, return the value of a given boolean symbol.
  ///
  /// \param Exp Boolean symbol to interpret.
  /// \return The boolean value from the model.
  virtual bool getBoolean(const SMTExprRef &Exp) = 0;

  /// Constructs an SMTExprRef from a boolean.
  ///
  /// \param b Boolean value to wrap.
  /// \return An SMT expression for \p b.
  virtual SMTExprRef mkBoolean(const bool b) = 0;

  /// Constructs an SMTExprRef from a finite APFloat.
  ///
  /// \param Float Finite floating-point value to wrap.
  /// \return An SMT expression for \p Float.
  virtual SMTExprRef mkFloat(const llvm::APFloat Float) = 0;

  /// Constructs an SMTExprRef from an APSInt and its bit width
  ///
  /// \param Int Integer value to wrap.
  /// \param BitWidth Bit width of the resulting bitvector.
  /// \return An SMT expression for the bitvector constant.
  virtual SMTExprRef mkBitvector(const llvm::APSInt Int, unsigned BitWidth) = 0;

  /// Given an expression, extract the value of this operand in the model.
  ///
  /// \param Exp Expression to interpret.
  /// \param Int Destination for the integer model value.
  /// \return True if a model value was written to \p Int.
  virtual bool getInterpretation(const SMTExprRef &Exp, llvm::APSInt &Int) = 0;

  /// Given an expression extract the value of this operand in the model.
  ///
  /// \param Exp Expression to interpret.
  /// \param Float Destination for the floating-point model value.
  /// \return True if a model value was written to \p Float.
  virtual bool getInterpretation(const SMTExprRef &Exp,
                                 llvm::APFloat &Float) = 0;

  /// Check if the constraints are satisfiable
  ///
  /// \return Satisfiability result, or std::nullopt if unknown.
  virtual std::optional<bool> check() const = 0;

  /// Push the current solver state
  virtual void push() = 0;

  /// Pop the previous solver state
  ///
  /// \param NumStates Number of solver states to pop.
  virtual void pop(unsigned NumStates = 1) = 0;

  /// Reset the solver and remove all constraints.
  virtual void reset() = 0;

  /// Checks if the solver supports floating-points.
  ///
  /// \return True if floating-point sorts and operations are supported.
  virtual bool isFPSupported() = 0;

  /// Print this solver to an output stream.
  ///
  /// \param OS Stream to print to.
  virtual void print(raw_ostream &OS) const = 0;

  /// Sets the requested option.
  ///
  /// \param Key Option name.
  /// \param Value Boolean option value.
  virtual void setBoolParam(StringRef Key, bool Value) = 0;
  /// Set an unsigned solver option.
  ///
  /// \param Key Option name.
  /// \param Value Unsigned option value.
  virtual void setUnsignedParam(StringRef Key, unsigned Value) = 0;

  /// Return statistics collected by this solver.
  ///
  /// \return Solver statistics owned by the caller.
  virtual std::unique_ptr<SMTSolverStatistics> getStatistics() const = 0;
};

/// Shared pointer for SMTSolvers.
using SMTSolverRef = std::shared_ptr<SMTSolver>;

/// Convenience method to create and Z3Solver object
///
/// \return A shared pointer to a new Z3-backed SMT solver.
LLVM_ABI SMTSolverRef CreateZ3Solver();

} // namespace llvm

#endif
