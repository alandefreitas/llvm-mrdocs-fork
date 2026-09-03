//===-- llvm/FMF.h - Fast math flags subclass -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the fast math flags.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_FMF_H
#define LLVM_IR_FMF_H

#include "llvm/Support/Compiler.h"
#include <cassert>

namespace llvm {
class raw_ostream;

/// Floating-point operator view that exposes FastMathFlags (defined in Operator.h).
class FPMathOperator;

/// Convenience struct for specifying and reasoning about fast-math flags.
class FastMathFlags {
private:
  /// \c FPMathOperator stores \c FastMathFlags inline and needs direct access
  /// to the underlying bitfield via \c getFastMathFlagsImpl().
  friend class FPMathOperator;

  unsigned Flags = 0;

public:
  /// Flag bits.
  enum {
    AllowReassoc    = (1 << 0), ///< Allow reassociation of floating-point operations.
    NoNaNs          = (1 << 1), ///< Assume arguments and results are not NaN.
    NoInfs          = (1 << 2), ///< Assume arguments and results are not infinite.
    NoSignedZeros   = (1 << 3), ///< Allow transformations that ignore signed zero.
    AllowReciprocal = (1 << 4), ///< Allow a / b to become a * (1 / b).
    AllowContract   = (1 << 5), ///< Allow fmul/fadd fusion (e.g. into FMA).
    ApproxFunc      = (1 << 6), ///< Allow approximate math library functions.
    FlagEnd         = (1 << 7)  ///< Sentinel one bit past the last flag.
  };

  /// Construct from a raw fast-math bit mask.
  /// \param F The raw flag bits; only the low 8 bits may be set.
  FastMathFlags(unsigned F) : Flags(F) {
    assert(((F & 0xff) == F) && "Flags value is not legal!");
  }

  /// Mask of all valid fast-math flag bits.
  constexpr static unsigned AllFlagsMask = FlagEnd - 1;

  /// Construct an empty set of fast-math flags.
  FastMathFlags() = default;

  /// Return flags with every fast-math bit set.
  /// \return A FastMathFlags value with every fast-math bit set.
  static FastMathFlags getFast() {
    FastMathFlags FMF;
    FMF.setFast();
    return FMF;
  }

  /// Return true if any fast-math flag is set.
  /// \return True if any fast-math flag is set.
  bool any() const { return Flags != 0; }
  /// Return true if no fast-math flags are set.
  /// \return True if no fast-math flags are set.
  bool none() const { return Flags == 0; }
  /// Return true if every fast-math flag is set.
  /// \return True if every fast-math flag is set.
  bool all() const { return Flags == AllFlagsMask; }

  /// Clear every fast-math flag.
  void clear() { Flags = 0; }
  /// Set every fast-math flag.
  void set() { Flags = AllFlagsMask; }

  /// Return true if this set allows reassociation of floating-point operations.
  /// \return True if the \c AllowReassoc flag is set.
  bool allowReassoc() const    { return 0 != (Flags & AllowReassoc); }
  /// Return true if this set assumes arguments and results are not NaN.
  /// \return True if the \c NoNaNs flag is set.
  bool noNaNs() const          { return 0 != (Flags & NoNaNs); }
  /// Return true if this set allows ignoring infinite results.
  /// \return True if the \c NoInfs flag is set.
  bool noInfs() const          { return 0 != (Flags & NoInfs); }
  /// Return true if this set allows ignoring the sign of zero.
  /// \return True if the \c NoSignedZeros flag is set.
  bool noSignedZeros() const   { return 0 != (Flags & NoSignedZeros); }
  /// Return true if reciprocal estimates of division are allowed.
  /// \return True if the \c AllowReciprocal flag is set.
  bool allowReciprocal() const { return 0 != (Flags & AllowReciprocal); }
  /// Return true if floating-point contraction (e.g. into FMA) is allowed.
  /// \return True if the \c AllowContract flag is set.
  bool allowContract() const   { return 0 != (Flags & AllowContract); }
  /// Return true if approximate function implementations are allowed.
  /// \return True if the \c ApproxFunc flag is set.
  bool approxFunc() const      { return 0 != (Flags & ApproxFunc); }
  /// Return true if every fast-math flag is set.
  ///
  /// 'Fast' means all bits are set.
  /// \return True if every fast-math flag is set.
  bool isFast() const          { return all(); }

  /// Flag setters
  /// Set or clear the \c AllowReassoc fast-math flag.
  /// \param B True to set the flag, false to clear it.
  void setAllowReassoc(bool B = true) {
    Flags = (Flags & ~AllowReassoc) | B * AllowReassoc;
  }
  /// Set or clear the \c NoNaNs fast-math flag.
  /// \param B True to set the flag, false to clear it.
  void setNoNaNs(bool B = true) {
    Flags = (Flags & ~NoNaNs) | B * NoNaNs;
  }
  /// Set or clear the \c NoInfs fast-math flag.
  /// \param B True to set the flag, false to clear it.
  void setNoInfs(bool B = true) {
    Flags = (Flags & ~NoInfs) | B * NoInfs;
  }
  /// Set or clear the \c NoSignedZeros fast-math flag.
  /// \param B True to set the flag, false to clear it.
  void setNoSignedZeros(bool B = true) {
    Flags = (Flags & ~NoSignedZeros) | B * NoSignedZeros;
  }
  /// Set or clear the \c AllowReciprocal fast-math flag.
  /// \param B True to set the flag, false to clear it.
  void setAllowReciprocal(bool B = true) {
    Flags = (Flags & ~AllowReciprocal) | B * AllowReciprocal;
  }
  /// Set or clear the \c AllowContract fast-math flag.
  /// \param B True to set the flag, false to clear it.
  void setAllowContract(bool B = true) {
    Flags = (Flags & ~AllowContract) | B * AllowContract;
  }
  /// Set or clear the \c ApproxFunc fast-math flag.
  /// \param B True to set the flag, false to clear it.
  void setApproxFunc(bool B = true) {
    Flags = (Flags & ~ApproxFunc) | B * ApproxFunc;
  }
  /// Set or clear all fast-math flags.
  /// \param B True to set every flag, false to clear every flag.
  void setFast(bool B = true) { B ? set() : clear(); }

  /// Intersect this object's flags with \p OtherFlags (bitwise AND).
  /// \param OtherFlags The flags to intersect with.
  void operator&=(const FastMathFlags &OtherFlags) {
    Flags &= OtherFlags.Flags;
  }
  /// Union this object's flags with \p OtherFlags (bitwise OR).
  /// \param OtherFlags The flags to union with.
  void operator|=(const FastMathFlags &OtherFlags) {
    Flags |= OtherFlags.Flags;
  }
  /// Return true if this flag set differs from \p OtherFlags.
  /// \param OtherFlags The flags to compare against.
  /// \return True if the flag sets differ.
  bool operator!=(const FastMathFlags &OtherFlags) const {
    return Flags != OtherFlags.Flags;
  }

  /// Return true if this flag set equals \p OtherFlags.
  /// \param OtherFlags The flags to compare against.
  /// \return True if the flag sets are equal.
  bool operator==(const FastMathFlags &OtherFlags) const {
    return Flags == OtherFlags.Flags;
  }

  /// Print fast-math flags to \p O.
  /// \param O The output stream.
  LLVM_ABI void print(raw_ostream &O) const;

  /// Intersect the rewrite-based flags of \p LHS and \p RHS.
  /// \param LHS The first set of flags.
  /// \param RHS The second set of flags.
  /// \return The rewrite-based flags present in both \p LHS and \p RHS.
  static inline FastMathFlags intersectRewrite(FastMathFlags LHS,
                                               FastMathFlags RHS) {
    const unsigned RewriteMask =
        AllowReassoc | AllowReciprocal | AllowContract | ApproxFunc;
    return FastMathFlags(RewriteMask & LHS.Flags & RHS.Flags);
  }

  /// Union the value-based flags of \p LHS and \p RHS.
  /// \param LHS The first set of flags.
  /// \param RHS The second set of flags.
  /// \return The value-based flags present in either \p LHS or \p RHS.
  static inline FastMathFlags unionValue(FastMathFlags LHS, FastMathFlags RHS) {
    const unsigned ValueMask = NoNaNs | NoInfs | NoSignedZeros;
    return FastMathFlags(ValueMask & (LHS.Flags | RHS.Flags));
  }
};

/// Bitwise-OR two fast-math flag sets.
/// \param LHS The left-hand flag set.
/// \param RHS The right-hand flag set.
/// \return The union of \p LHS and \p RHS.
inline FastMathFlags operator|(FastMathFlags LHS, FastMathFlags RHS) {
  LHS |= RHS;
  return LHS;
}

/// Bitwise-AND two fast-math flag sets.
/// \param LHS The left-hand flag set.
/// \param RHS The right-hand flag set.
/// \return The intersection of \p LHS and \p RHS.
inline FastMathFlags operator&(FastMathFlags LHS, FastMathFlags RHS) {
  LHS &= RHS;
  return LHS;
}

/// Print \p FMF to the output stream \p O.
/// \param O The output stream.
/// \param FMF The flags to print.
/// \return The output stream \p O.
inline raw_ostream &operator<<(raw_ostream &O, FastMathFlags FMF) {
  FMF.print(O);
  return O;
}

} // end namespace llvm

#endif // LLVM_IR_FMF_H
