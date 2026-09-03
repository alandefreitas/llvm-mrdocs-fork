//==------ llvm/CodeGen/GlobalISel/MIPatternMatch.h -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Contains matchers for matching SSA Machine Instructions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_MIPATTERNMATCH_H
#define LLVM_CODEGEN_GLOBALISEL_MIPATTERNMATCH_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/InstrTypes.h"
#include <tuple>
#include <utility>

namespace llvm {
/// Matchers for GlobalISel SSA MachineInstr patterns.
namespace MIPatternMatch {

/// Match register-like value \p R against pattern \p P.
///
/// \param R Register or register-like value to match.
/// \param MRI Register info used by the pattern.
/// \param P Pattern object providing match().
/// \return True if the pattern matches.
template <typename Reg, typename Pattern>
[[nodiscard]] bool mi_match(Reg R, const MachineRegisterInfo &MRI,
                            Pattern &&P) {
  return P.match(MRI, R);
}

/// Match machine instruction \p MI against pattern \p P.
///
/// \param MI Instruction to match.
/// \param MRI Register info used by the pattern.
/// \param P Pattern object providing match().
/// \return True if the pattern matches.
template <typename Pattern>
[[nodiscard]] bool mi_match(MachineInstr &MI, const MachineRegisterInfo &MRI,
                            Pattern &&P) {
  return P.match(MRI, &MI);
}

/// Match const machine instruction \p MI against pattern \p P.
///
/// \param MI Instruction to match.
/// \param MRI Register info used by the pattern.
/// \param P Pattern object providing match().
/// \return True if the pattern matches.
template <typename Pattern>
[[nodiscard]] bool mi_match(const MachineInstr &MI,
                            const MachineRegisterInfo &MRI, Pattern &&P) {
  return P.match(MRI, &MI);
}

/// Matcher that requires a register to have exactly one use.
///
/// TODO: Extend for N use.
template <typename SubPatternT> struct OneUse_match {
  /// Nested sub-pattern applied after the use-count check.
  SubPatternT SubPat;
  /// Construct a one-use matcher around \p SP.
  ///
  /// \param SP Nested sub-pattern to apply after the use check.
  OneUse_match(const SubPatternT &SP) : SubPat(SP) {}

  /// Match \p Reg if it has one use and SubPat matches.
  ///
  /// \param MRI Register info used for use-count and nested matching.
  /// \param Reg Register to match.
  /// \return True if the use-count and nested pattern succeed.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    return MRI.hasOneUse(Reg) && SubPat.match(MRI, Reg);
  }
};

/// Matches \p SP only when the register has exactly one use.
///
/// \param SP Nested sub-pattern to apply after the use check.
/// \return A matcher that applies \p SP only when the register has exactly one use.
template <typename SubPat>
inline OneUse_match<SubPat> m_OneUse(const SubPat &SP) {
  return SP;
}

/// Matcher that requires a register to have exactly one non-debug use.
template <typename SubPatternT> struct OneNonDBGUse_match {
  /// Nested sub-pattern applied after the use-count check.
  SubPatternT SubPat;
  /// Construct a one-non-debug-use matcher around \p SP.
  ///
  /// \param SP Nested sub-pattern to apply after the use check.
  OneNonDBGUse_match(const SubPatternT &SP) : SubPat(SP) {}

  /// Match \p Reg if it has one non-debug use and SubPat matches.
  ///
  /// \param MRI Register info used for use-count and nested matching.
  /// \param Reg Register to match.
  /// \return True if the use-count and nested pattern succeed.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    return MRI.hasOneNonDBGUse(Reg) && SubPat.match(MRI, Reg);
  }
};

/// Matches \p SP only when the register has exactly one non-debug use.
///
/// \param SP Nested sub-pattern to apply after the use check.
/// \return A matcher that applies \p SP only when the register has exactly one non-debug use.
template <typename SubPat>
inline OneNonDBGUse_match<SubPat> m_OneNonDBGUse(const SubPat &SP) {
  return SP;
}

/// Try to match \p Reg as an integer constant of type ConstT.
///
/// \param Reg Register that may hold an integer constant.
/// \param MRI Register info used to inspect constants.
/// \return The constant value, or nullopt if matching fails.
template <typename ConstT>
inline std::optional<ConstT> matchConstant(Register Reg,
                                           const MachineRegisterInfo &MRI);

/// Match \p Reg as an APInt integer constant.
///
/// \param Reg Register that may hold an integer constant.
/// \param MRI Register info used to inspect constants.
/// \return The constant APInt, or nullopt if matching fails.
template <>
inline std::optional<APInt> matchConstant(Register Reg,
                                          const MachineRegisterInfo &MRI) {
  return getIConstantVRegVal(Reg, MRI);
}

/// Match \p Reg as a sign-extended int64 integer constant.
///
/// \param Reg Register that may hold an integer constant.
/// \param MRI Register info used to inspect constants.
/// \return The constant value, or nullopt if matching fails.
template <>
inline std::optional<int64_t> matchConstant(Register Reg,
                                            const MachineRegisterInfo &MRI) {
  return getIConstantVRegSExtVal(Reg, MRI);
}

/// Matcher that binds an integer constant of type ConstT.
template <typename ConstT> struct ConstantMatch {
  /// Output that receives the matched constant.
  ConstT &CR;
  /// Construct a matcher that binds into \p C.
  ///
  /// \param C Output that receives the matched constant.
  ConstantMatch(ConstT &C) : CR(C) {}
  /// Match \p Reg if it is an integer constant of type ConstT.
  ///
  /// \param MRI Register info used to inspect constants.
  /// \param Reg Register to match.
  /// \return True if a constant was bound.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    if (auto MaybeCst = matchConstant<ConstT>(Reg, MRI)) {
      CR = *MaybeCst;
      return true;
    }
    return false;
  }
};

/// Matches an integer constant, binding an APInt to \p Cst.
///
/// \param Cst Output that receives the matched constant.
/// \return A matcher for an integer constant, binding an APInt to \p Cst.
inline ConstantMatch<APInt> m_ICst(APInt &Cst) {
  return ConstantMatch<APInt>(Cst);
}
/// Matches an integer constant, binding a sign-extended int64 to \p Cst.
///
/// \param Cst Output that receives the matched constant.
/// \return A matcher for an integer constant, binding a sign-extended int64 to \p Cst.
inline ConstantMatch<int64_t> m_ICst(int64_t &Cst) {
  return ConstantMatch<int64_t>(Cst);
}

/// Try to match \p Reg as an integer constant splat of type ConstT.
///
/// \param Reg Register that may hold a constant splat.
/// \param MRI Register info used to inspect constants.
/// \return The splat lane value, or nullopt if matching fails.
template <typename ConstT>
inline std::optional<ConstT> matchConstantSplat(Register Reg,
                                                const MachineRegisterInfo &MRI);

/// Match \p Reg as an APInt constant splat.
///
/// \param Reg Register that may hold a constant splat.
/// \param MRI Register info used to inspect constants.
/// \return The splat lane APInt, or nullopt if matching fails.
template <>
inline std::optional<APInt> matchConstantSplat(Register Reg,
                                               const MachineRegisterInfo &MRI) {
  return getIConstantSplatVal(Reg, MRI);
}

/// Match \p Reg as a sign-extended int64 constant splat.
///
/// \param Reg Register that may hold a constant splat.
/// \param MRI Register info used to inspect constants.
/// \return The splat lane value, or nullopt if matching fails.
template <>
inline std::optional<int64_t>
matchConstantSplat(Register Reg, const MachineRegisterInfo &MRI) {
  return getIConstantSplatSExtVal(Reg, MRI);
}

/// Matcher for an integer constant or constant splat, binding the value.
template <typename ConstT> struct ICstOrSplatMatch {
  /// Output that receives the matched constant or splat lane value.
  ConstT &CR;
  /// Construct a matcher that binds into \p C.
  ///
  /// \param C Output that receives the matched constant.
  ICstOrSplatMatch(ConstT &C) : CR(C) {}
  /// Match \p Reg if it is a constant or constant splat.
  ///
  /// \param MRI Register info used to inspect constants.
  /// \param Reg Register to match.
  /// \return True if a constant or splat was bound.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    if (auto MaybeCst = matchConstant<ConstT>(Reg, MRI)) {
      CR = *MaybeCst;
      return true;
    }

    if (auto MaybeCstSplat = matchConstantSplat<ConstT>(Reg, MRI)) {
      CR = *MaybeCstSplat;
      return true;
    }

    return false;
  };
};

/// Matches an integer constant or splat, binding an APInt to \p Cst.
///
/// \param Cst Output that receives the matched constant.
/// \return A matcher for an integer constant or splat, binding an APInt to \p Cst.
inline ICstOrSplatMatch<APInt> m_ICstOrSplat(APInt &Cst) {
  return ICstOrSplatMatch<APInt>(Cst);
}

/// Matches an integer constant or splat, binding a sign-extended int64.
///
/// \param Cst Output that receives the matched constant.
/// \return A matcher for an integer constant or splat, binding a sign-extended int64.
inline ICstOrSplatMatch<int64_t> m_ICstOrSplat(int64_t &Cst) {
  return ICstOrSplatMatch<int64_t>(Cst);
}

/// Matcher for an integer constant with look-through, binding value and vreg.
struct GCstAndRegMatch {
  /// Output that receives the matched integer value and defining register.
  std::optional<ValueAndVReg> &ValReg;
  /// Construct a matcher that binds into \p ValReg.
  ///
  /// \param ValReg Output that receives the matched value and vreg.
  GCstAndRegMatch(std::optional<ValueAndVReg> &ValReg) : ValReg(ValReg) {}
  /// Match \p Reg if it is an integer constant (with look-through).
  ///
  /// \param MRI Register info used to inspect constants.
  /// \param Reg Register to match.
  /// \return True if an integer constant was bound.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    ValReg = getIConstantVRegValWithLookThrough(Reg, MRI);
    return ValReg ? true : false;
  }
};

/// Matches an integer constant with look-through, binding value and vreg.
///
/// \param ValReg Output that receives the matched value and vreg.
/// \return A matcher for an integer constant with look-through, binding value and vreg.
inline GCstAndRegMatch m_GCst(std::optional<ValueAndVReg> &ValReg) {
  return GCstAndRegMatch(ValReg);
}

/// Matcher for an FP constant with look-through, binding value and vreg.
struct GFCstAndRegMatch {
  /// Output that receives the matched FP value and defining register.
  std::optional<FPValueAndVReg> &FPValReg;
  /// Construct a matcher that binds into \p FPValReg.
  ///
  /// \param FPValReg Output that receives the matched FP value and vreg.
  GFCstAndRegMatch(std::optional<FPValueAndVReg> &FPValReg)
      : FPValReg(FPValReg) {}
  /// Match \p Reg if it is an FP constant (with look-through).
  ///
  /// \param MRI Register info used to inspect constants.
  /// \param Reg Register to match.
  /// \return True if an FP constant was bound.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    FPValReg = getFConstantVRegValWithLookThrough(Reg, MRI);
    return FPValReg ? true : false;
  }
};

/// Matches an FP constant with look-through, binding value and defining vreg.
///
/// \param FPValReg Output that receives the matched FP value and vreg.
/// \return A matcher for an FP constant with look-through, binding value and defining vreg.
inline GFCstAndRegMatch m_GFCst(std::optional<FPValueAndVReg> &FPValReg) {
  return GFCstAndRegMatch(FPValReg);
}

/// Matcher for an FP constant or FP splat, binding value and defining vreg.
struct GFCstOrSplatGFCstMatch {
  /// Output that receives the matched FP value and defining register.
  std::optional<FPValueAndVReg> &FPValReg;
  /// Construct a matcher that binds into \p FPValReg.
  ///
  /// \param FPValReg Output that receives the matched FP value and vreg.
  GFCstOrSplatGFCstMatch(std::optional<FPValueAndVReg> &FPValReg)
      : FPValReg(FPValReg) {}
  /// Match \p Reg if it is an FP constant or FP splat.
  ///
  /// \param MRI Register info used to inspect constants.
  /// \param Reg Register to match.
  /// \return True if an FP constant or splat was bound.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    return (FPValReg = getFConstantSplat(Reg, MRI)) ||
           (FPValReg = getFConstantVRegValWithLookThrough(Reg, MRI));
  };
};

/// Matches an FP constant or FP splat, binding value and defining vreg.
///
/// \param FPValReg Output that receives the matched FP value and vreg.
/// \return A matcher for an FP constant or FP splat, binding value and defining vreg.
inline GFCstOrSplatGFCstMatch
m_GFCstOrSplat(std::optional<FPValueAndVReg> &FPValReg) {
  return GFCstOrSplatGFCstMatch(FPValReg);
}

/// Matches an FP constant whose value satisfies the given predicate.
template <typename Pred> struct GFCstPredMatch {
  /// Predicate applied to the matched APFloat.
  Pred P;
  /// Construct a matcher from predicate \p P.
  ///
  /// \param P Predicate applied to the floating-point value.
  GFCstPredMatch(Pred P) : P(P) {}
  /// Match \p Reg if it is an FP constant accepted by P.
  ///
  /// \param MRI Register info used to inspect constants.
  /// \param Reg Register to match.
  /// \return True if the constant satisfies the predicate.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    if (const ConstantFP *FPImm = getConstantFPVRegVal(Reg, MRI))
      return P(FPImm->getValueAPF());
    return false;
  }
};
/// Deduction guide for GFCstPredMatch from a predicate object.
template <typename Pred> GFCstPredMatch(Pred) -> GFCstPredMatch<Pred>;

/// Matches a floating-point positive zero.
///
/// \return A matcher for floating-point positive zero.
inline auto m_PosZeroFP() {
  return GFCstPredMatch([](const APFloat &V) { return V.isPosZero(); });
}

/// Matcher for a specific constant value.
struct SpecificConstantMatch {
  /// Constant value that must be matched.
  APInt RequestedVal;
  /// Construct a matcher for constant \p RequestedVal.
  ///
  /// \param RequestedVal Constant value that must match.
  SpecificConstantMatch(const APInt &RequestedVal)
      : RequestedVal(RequestedVal) {}
  /// Match \p Reg if it is the requested constant.
  ///
  /// \param MRI Register info used to inspect constants.
  /// \param Reg Register to match.
  /// \return True if the constant matches.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    APInt MatchedVal;
    if (mi_match(Reg, MRI, m_ICst(MatchedVal))) {
      if (MatchedVal.getBitWidth() > RequestedVal.getBitWidth())
        RequestedVal = RequestedVal.sext(MatchedVal.getBitWidth());
      else
        MatchedVal = MatchedVal.sext(RequestedVal.getBitWidth());

      return APInt::isSameValue(MatchedVal, RequestedVal);
    }
    return false;
  }
};

/// Matches a constant equal to \p RequestedValue.
///
/// \param RequestedValue Constant value that must match.
/// \return A matcher for a constant equal to \p RequestedValue.
inline SpecificConstantMatch m_SpecificICst(const APInt &RequestedValue) {
  return SpecificConstantMatch(RequestedValue);
}

/// Matches a constant equal to \p RequestedValue.
///
/// \param RequestedValue Signed integer constant that must match.
/// \return A matcher for a constant equal to \p RequestedValue.
inline SpecificConstantMatch m_SpecificICst(int64_t RequestedValue) {
  return SpecificConstantMatch(APInt(64, RequestedValue, /* isSigned */ true));
}

/// Matcher for a specific immediate operand value.
struct SpecificImmMatch {
  /// Immediate value that must be matched.
  int64_t RequestedVal;
  /// Construct a matcher for immediate \p RequestedVal.
  ///
  /// \param RequestedVal Immediate value that must match.
  SpecificImmMatch(int64_t RequestedVal) : RequestedVal(RequestedVal) {}
  /// Match if \p Imm equals RequestedVal.
  ///
  /// \param Imm Candidate immediate.
  /// \return True if the immediates are equal.
  bool match(int64_t Imm) const { return Imm == RequestedVal; }
};

/// Matches an immediate operand equal to \p RequestedValue.
///
/// \param RequestedValue Immediate value that must match.
/// \return A matcher for an immediate operand equal to \p RequestedValue.
inline SpecificImmMatch m_SpecificImm(int64_t RequestedValue) {
  return SpecificImmMatch(RequestedValue);
}

/// Matcher that binds an immediate operand's value.
struct BindImmMatch {
  /// Output that receives the matched immediate.
  int64_t &ImmOut;
  /// Construct a binder for immediate output \p ImmOut.
  ///
  /// \param ImmOut Output that receives the matched immediate.
  BindImmMatch(int64_t &ImmOut) : ImmOut(ImmOut) {}
  /// Bind \p Imm into ImmOut.
  ///
  /// \param Imm Immediate value to bind.
  /// \return Always true.
  bool match(int64_t Imm) const {
    ImmOut = Imm;
    return true;
  }
};

/// Binds an immediate operand's value to \p Imm.
///
/// \param Imm Output that receives the matched immediate.
/// \return A matcher that binds an immediate operand's value to \p Imm.
inline BindImmMatch m_Imm(int64_t &Imm) { return BindImmMatch(Imm); }

/// Matches an integer constant with all bits set, regardless of width.
struct AllOnesConstantMatch {
  /// Match \p Reg if it is an all-ones integer constant.
  ///
  /// \param MRI Register info used to inspect constants.
  /// \param Reg Register to match.
  /// \return True if the constant is all ones.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    APInt MatchedVal;
    return mi_match(Reg, MRI, m_ICst(MatchedVal)) && MatchedVal.isAllOnes();
  }
};

/// Matches an integer constant with all bits set.
///
/// \return A matcher for an all-ones integer constant.
inline AllOnesConstantMatch m_AllOnes() { return {}; }

/// Matcher for a specific constant splat.
struct SpecificConstantSplatMatch {
  /// Splat lane value that must be matched.
  APInt RequestedVal;
  /// Construct a matcher for splat value \p RequestedVal.
  ///
  /// \param RequestedVal Splat lane value to match.
  SpecificConstantSplatMatch(const APInt &RequestedVal)
      : RequestedVal(RequestedVal) {}
  /// Match \p Reg if it is a splat of RequestedVal.
  ///
  /// \param MRI Register info used to inspect build-vector constants.
  /// \param Reg Register to match.
  /// \return True if the splat matches.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    return isBuildVectorConstantSplat(Reg, MRI, RequestedVal,
                                      /* AllowUndef */ false);
  }
};

/// Matches a constant splat of \p RequestedValue.
///
/// \param RequestedValue Splat lane value to match.
/// \return A matcher for a constant splat of \p RequestedValue.
inline SpecificConstantSplatMatch
m_SpecificICstSplat(const APInt &RequestedValue) {
  return SpecificConstantSplatMatch(RequestedValue);
}

/// Matches a constant splat of \p RequestedValue.
///
/// \param RequestedValue Signed integer splat lane value.
/// \return A matcher for a constant splat of \p RequestedValue.
inline SpecificConstantSplatMatch m_SpecificICstSplat(int64_t RequestedValue) {
  return SpecificConstantSplatMatch(
      APInt(64, RequestedValue, /* isSigned */ true));
}

/// Matcher for a specific constant or constant splat.
struct SpecificConstantOrSplatMatch {
  /// Constant or splat lane value that must be matched.
  APInt RequestedVal;
  /// Construct a matcher for \p RequestedVal.
  ///
  /// \param RequestedVal Constant or splat lane value to match.
  SpecificConstantOrSplatMatch(const APInt &RequestedVal)
      : RequestedVal(RequestedVal) {}
  /// Match \p Reg if it is RequestedVal or a splat of it.
  ///
  /// \param MRI Register info used to inspect constants.
  /// \param Reg Register to match.
  /// \return True if the constant or splat matches.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    APInt MatchedVal;
    if (mi_match(Reg, MRI, m_ICst(MatchedVal))) {
      if (MatchedVal.getBitWidth() > RequestedVal.getBitWidth())
        RequestedVal = RequestedVal.sext(MatchedVal.getBitWidth());
      else
        MatchedVal = MatchedVal.sext(RequestedVal.getBitWidth());

      if (APInt::isSameValue(MatchedVal, RequestedVal))
        return true;
    }
    return isBuildVectorConstantSplat(Reg, MRI, RequestedVal,
                                      /* AllowUndef */ false);
  }
};

/// Matches \p RequestedValue or a constant splat of that value.
///
/// \param RequestedValue Constant or splat lane value to match.
/// \return A matcher that matches \p RequestedValue or a constant splat of that value.
inline SpecificConstantOrSplatMatch
m_SpecificICstOrSplat(const APInt &RequestedValue) {
  return SpecificConstantOrSplatMatch(RequestedValue);
}

/// Matches \p RequestedValue or a constant splat of that value.
///
/// \param RequestedValue Signed integer constant or splat lane value.
/// \return A matcher that matches \p RequestedValue or a constant splat of that value.
inline SpecificConstantOrSplatMatch
m_SpecificICstOrSplat(int64_t RequestedValue) {
  return SpecificConstantOrSplatMatch(
      APInt(64, RequestedValue, /* isSigned */ true));
}

/// Convenience matchers for specific integer values.
///
/// \return A matcher for the integer constant zero.
inline SpecificConstantMatch m_ZeroInt() {
  return SpecificConstantMatch(APInt::getZero(64));
}
/// Matches the integer constant with all bits set (64-bit APInt form).
///
/// \return A matcher for an all-ones integer constant.
inline SpecificConstantMatch m_AllOnesInt() {
  return SpecificConstantMatch(APInt::getAllOnes(64));
}

/// Matcher for a specific register.
struct SpecificRegisterMatch {
  /// Register that must be matched exactly.
  Register RequestedReg;
  /// Construct a matcher for \p RequestedReg.
  ///
  /// \param RequestedReg Register that must match exactly.
  SpecificRegisterMatch(Register RequestedReg) : RequestedReg(RequestedReg) {}
  /// Match if \p Reg equals RequestedReg.
  ///
  /// \param MRI Unused register info.
  /// \param Reg Candidate register.
  /// \return True if the registers are equal.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    return Reg == RequestedReg;
  }
};

/// Matches a register only if it equals \p RequestedReg.
///
/// \param RequestedReg Register that must match exactly.
/// \return A matcher for a register only if it equals \p RequestedReg.
inline SpecificRegisterMatch m_SpecificReg(Register RequestedReg) {
  return SpecificRegisterMatch(RequestedReg);
}

// TODO: Rework this for different kinds of MachineOperand.
// Currently assumes the Src for a match is a register.
// We might want to support taking in some MachineOperands and call getReg on
// that.

/// Matcher that accepts any register operand.
struct operand_type_match {
  /// Always match a register value.
  ///
  /// \param MRI Unused register info.
  /// \param Reg Unused register.
  /// \return Always true.
  bool match(const MachineRegisterInfo &MRI, Register Reg) { return true; }
  /// Match if \p MO is a register operand.
  ///
  /// \param MRI Unused register info.
  /// \param MO Machine operand to test.
  /// \return True if the operand is a register.
  bool match(const MachineRegisterInfo &MRI, MachineOperand *MO) {
    return MO->isReg();
  }
};

/// Matches any register operand without binding it.
///
/// \return A matcher for any register operand without binding it.
inline operand_type_match m_Reg() { return operand_type_match(); }

/// Empty And combinator that always matches.
template <typename... Preds> struct And {
  /// Always succeed; base case for the And combinator.
  ///
  /// \param MRI Unused register info.
  /// \param src Unused match source.
  /// \return Always true.
  template <typename MatchSrc>
  bool match(const MachineRegisterInfo &MRI, MatchSrc &&src) {
    return true;
  }
};

/// And combinator that requires every nested sub-pattern to match.
template <typename Pred, typename... Preds>
struct And<Pred, Preds...> : And<Preds...> {
  /// Head sub-pattern in this And chain.
  Pred P;
  /// Construct an And combinator from nested sub-patterns.
  ///
  /// \param p Head sub-pattern.
  /// \param preds Remaining sub-patterns.
  And(Pred &&p, Preds &&... preds)
      : And<Preds...>(std::forward<Preds>(preds)...), P(std::forward<Pred>(p)) {
  }
  /// Match \p src if every nested sub-pattern matches.
  ///
  /// \param MRI Register info passed to nested matchers.
  /// \param src Value or instruction to match.
  /// \return True if all sub-patterns match.
  template <typename MatchSrc>
  bool match(const MachineRegisterInfo &MRI, MatchSrc &&src) {
    return P.match(MRI, src) && And<Preds...>::match(MRI, src);
  }
};

/// Empty Or combinator that never matches.
template <typename... Preds> struct Or {
  /// Always fail; base case for the Or combinator.
  ///
  /// \param MRI Unused register info.
  /// \param src Unused match source.
  /// \return Always false.
  template <typename MatchSrc>
  bool match(const MachineRegisterInfo &MRI, MatchSrc &&src) {
    return false;
  }
};

/// Or combinator that succeeds if any nested sub-pattern matches.
template <typename Pred, typename... Preds>
struct Or<Pred, Preds...> : Or<Preds...> {
  /// Head sub-pattern in this Or chain.
  Pred P;
  /// Construct an Or combinator from nested sub-patterns.
  ///
  /// \param p Head sub-pattern.
  /// \param preds Remaining sub-patterns.
  Or(Pred &&p, Preds &&... preds)
      : Or<Preds...>(std::forward<Preds>(preds)...), P(std::forward<Pred>(p)) {}
  /// Match \p src if any nested sub-pattern matches.
  ///
  /// \param MRI Register info passed to nested matchers.
  /// \param src Value or instruction to match.
  /// \return True if any sub-pattern matches.
  template <typename MatchSrc>
  bool match(const MachineRegisterInfo &MRI, MatchSrc &&src) {
    return P.match(MRI, src) || Or<Preds...>::match(MRI, src);
  }
};

/// Match only if every sub-pattern in \p preds matches.
///
/// \param preds Sub-patterns that must all succeed.
/// \return An And combinator over the given sub-patterns.
template <typename... Preds> And<Preds...> m_all_of(Preds &&... preds) {
  return And<Preds...>(std::forward<Preds>(preds)...);
}

/// Match if any sub-pattern in \p preds matches.
///
/// \param preds Sub-patterns tried until one succeeds.
/// \return An Or combinator over the given sub-patterns.
template <typename... Preds> Or<Preds...> m_any_of(Preds &&... preds) {
  return Or<Preds...>(std::forward<Preds>(preds)...);
}

/// Helper that stores a matched value into an output reference.
template <typename BindTy> struct bind_helper {
  /// Assign \p V to \p VR.
  ///
  /// \param MRI Unused register info (kept for a uniform signature).
  /// \param VR Output that receives the value.
  /// \param V Value to bind.
  /// \return Always true.
  static bool bind(const MachineRegisterInfo &MRI, BindTy &VR, BindTy &V) {
    VR = V;
    return true;
  }
};

/// Binding helper specialized for MachineInstr pointers.
template <> struct bind_helper<MachineInstr *> {
  /// Bind the defining instruction of \p Reg to \p MI.
  ///
  /// \param MRI Register info used to look up the definition.
  /// \param MI Output that receives the defining instruction.
  /// \param Reg Register whose definition is bound.
  /// \return True if a defining instruction exists.
  static bool bind(const MachineRegisterInfo &MRI, MachineInstr *&MI,
                   Register Reg) {
    MI = MRI.getVRegDef(Reg);
    if (MI)
      return true;
    return false;
  }
  /// Bind instruction pointer \p Inst to \p MI.
  ///
  /// \param MRI Unused register info (kept for a uniform signature).
  /// \param MI Output that receives the instruction.
  /// \param Inst Instruction pointer to bind.
  /// \return True if Inst is non-null.
  static bool bind(const MachineRegisterInfo &MRI, MachineInstr *&MI,
                   MachineInstr *Inst) {
    MI = Inst;
    return MI;
  }
};

/// Binding helper specialized for const MachineInstr pointers.
template <> struct bind_helper<const MachineInstr *> {
  /// Bind the defining instruction of \p Reg to \p MI.
  ///
  /// \param MRI Register info used to look up the definition.
  /// \param MI Output that receives the defining instruction.
  /// \param Reg Register whose definition is bound.
  /// \return True if a defining instruction exists.
  static bool bind(const MachineRegisterInfo &MRI, const MachineInstr *&MI,
                   Register Reg) {
    MI = MRI.getVRegDef(Reg);
    return MI;
  }
  /// Bind const instruction pointer \p Inst to \p MI.
  ///
  /// \param MRI Unused register info (kept for a uniform signature).
  /// \param MI Output that receives the instruction.
  /// \param Inst Instruction pointer to bind.
  /// \return True if Inst is non-null.
  static bool bind(const MachineRegisterInfo &MRI, const MachineInstr *&MI,
                   const MachineInstr *Inst) {
    MI = Inst;
    return MI;
  }
};

/// Binding helper specialized for LLT values.
template <> struct bind_helper<LLT> {
  /// Bind the type of \p Reg to \p Ty.
  ///
  /// \param MRI Register info used to query the type.
  /// \param Ty Output that receives the type.
  /// \param Reg Register whose type is bound.
  /// \return True if the type is valid.
  static bool bind(const MachineRegisterInfo &MRI, LLT &Ty, Register Reg) {
    Ty = MRI.getType(Reg);
    if (Ty.isValid())
      return true;
    return false;
  }
};

/// Binding helper specialized for ConstantFP pointers.
template <> struct bind_helper<const ConstantFP *> {
  /// Bind the floating-point constant defining \p Reg to \p F.
  ///
  /// \param MRI Register info used to look up the constant.
  /// \param F Output that receives the ConstantFP.
  /// \param Reg Register whose constant value is bound.
  /// \return True if a floating-point constant exists.
  static bool bind(const MachineRegisterInfo &MRI, const ConstantFP *&F,
                   Register Reg) {
    F = getConstantFPVRegVal(Reg, MRI);
    if (F)
      return true;
    return false;
  }
};

/// Matcher that binds a matched value into an output reference.
template <typename Class> struct bind_ty {
  /// Output that receives the bound value.
  Class &VR;

  /// Construct a binder around output \p V.
  ///
  /// \param V Output that receives the matched value.
  bind_ty(Class &V) : VR(V) {}

  /// Bind \p V into VR using the Class-specific helper.
  ///
  /// \param MRI Register info used by binding helpers.
  /// \param V Candidate value to bind.
  /// \return True if binding succeeded.
  template <typename ITy> bool match(const MachineRegisterInfo &MRI, ITy &&V) {
    return bind_helper<Class>::bind(MRI, VR, V);
  }
};

/// Binds the matched register to \p R.
///
/// \param R Output that receives the matched register.
/// \return A matcher that binds the matched register to \p R.
inline bind_ty<Register> m_Reg(Register &R) { return R; }
/// Binds the matched defining instruction to \p MI.
///
/// \param MI Output that receives the matched MachineInstr.
/// \return A matcher that binds the matched defining instruction to \p MI.
inline bind_ty<MachineInstr *> m_MInstr(MachineInstr *&MI) { return MI; }
/// Binds the matched defining instruction to const \p MI.
///
/// \param MI Output that receives the matched MachineInstr.
/// \return A matcher that binds the matched defining instruction to const \p MI.
inline bind_ty<const MachineInstr *> m_MInstr(const MachineInstr *&MI) {
  return MI;
}
/// Binds the matched register's LLT to \p Ty.
///
/// \param Ty Output that receives the matched type.
/// \return A matcher that binds the matched register's LLT to \p Ty.
inline bind_ty<LLT> m_Type(LLT &Ty) { return Ty; }
/// Binds the matched compare predicate to \p P.
///
/// \param P Output that receives the matched predicate.
/// \return A matcher that binds the matched compare predicate to \p P.
inline bind_ty<CmpInst::Predicate> m_Pred(CmpInst::Predicate &P) { return P; }
/// Matches any compare predicate without binding it.
///
/// \return A matcher for any compare predicate without binding it.
inline operand_type_match m_Pred() { return operand_type_match(); }
/// Binds the matched FP class test to \p T.
///
/// \param T Output that receives the matched FP class test.
/// \return A matcher that binds the matched FP class test to \p T.
inline bind_ty<FPClassTest> m_FPClassTest(FPClassTest &T) { return T; }

/// Optional trailing operand that binds a matched instruction's MI flags.
///
/// Wraps a MIFlags output for use as an optional trailing operand of an
/// instruction matcher (e.g. m_GPtrAdd(L, R, m_MIFlags(Flags))). On a
/// successful match the matched instruction's flags are written to Flags.
struct MIFlagsRef {
  /// Output that receives the matched instruction flags.
  uint32_t &Flags;
};

/// Wrap \p Flags as an optional trailing instruction-matcher operand.
///
/// \param Flags Output that receives the matched instruction flags.
/// \return A MIFlagsRef wrapping \p Flags.
inline MIFlagsRef m_MIFlags(uint32_t &Flags) { return {Flags}; }

/// Optional trailing operand that binds a load's MachineMemOperand.
///
/// Used as in m_GLoad(m_Reg(Ptr), m_MMO(MMO)).
struct MMORef {
  /// Output that receives the matched MachineMemOperand.
  const MachineMemOperand *&MMO;
};

/// Wrap \p MMO as an optional trailing load-matcher operand.
///
/// \param MMO Output that receives the matched MachineMemOperand.
/// \return An MMORef wrapping \p MMO.
inline MMORef m_MMO(const MachineMemOperand *&MMO) { return {MMO}; }

/// Helper that compares a previously bound value to a candidate.
template <typename BindTy> struct deferred_helper {
  /// Return true if \p VR equals \p V.
  ///
  /// \param MRI Unused register info (kept for a uniform signature).
  /// \param VR Previously bound value.
  /// \param V Candidate value.
  /// \return True if the values compare equal.
  static bool match(const MachineRegisterInfo &MRI, BindTy &VR, BindTy &V) {
    return VR == V;
  }
};

/// Deferred comparison helper specialized for LLT values.
template <> struct deferred_helper<LLT> {
  /// Return true if register \p R has type \p VT.
  ///
  /// \param MRI Register info used to query the type.
  /// \param VT Previously bound type.
  /// \param R Candidate register.
  /// \return True if the register type equals VT.
  static bool match(const MachineRegisterInfo &MRI, LLT VT, Register R) {
    return VT == MRI.getType(R);
  }
};

/// Matcher that compares against a value bound earlier in the expression.
template <typename Class> struct deferred_ty {
  /// Reference to the previously bound value being compared.
  Class &VR;

  /// Construct a deferred matcher around bound value \p V.
  ///
  /// \param V Previously bound value that must equal the matched operand.
  deferred_ty(Class &V) : VR(V) {}

  /// Match \p V if it equals the previously bound value.
  ///
  /// \param MRI Register info used by type-aware deferred helpers.
  /// \param V Candidate value to compare.
  /// \return True if the candidate equals the bound value.
  template <typename ITy> bool match(const MachineRegisterInfo &MRI, ITy &&V) {
    return deferred_helper<Class>::match(MRI, VR, V);
  }
};

/// Matches a register equal to a value bound earlier in the same expression.
///
/// Similar to m_SpecificReg/Type, but the specific value to match originated
/// from an earlier sub-pattern in the same mi_match expression. For example,
/// we cannot match `(add X, X)` with `m_GAdd(m_Reg(X), m_SpecificReg(X))`
/// because `X` is not initialized at the time it's passed to `m_SpecificReg`.
/// Instead, we can use `m_GAdd(m_Reg(x), m_DeferredReg(X))`.
///
/// \param R Register bound by an earlier sub-pattern and compared here.
/// \return A deferred matcher comparing against the earlier-bound register \p R.
inline deferred_ty<Register> m_DeferredReg(Register &R) { return R; }
/// Matches a type equal to a value bound earlier in the same expression.
///
/// \param Ty Type bound by an earlier sub-pattern and compared here.
/// \return A deferred matcher comparing against the earlier-bound type \p Ty.
inline deferred_ty<LLT> m_DeferredType(LLT &Ty) { return Ty; }

/// Matcher for a G_IMPLICIT_DEF defining instruction.
struct ImplicitDefMatch {
  /// Match \p Reg if it is defined by G_IMPLICIT_DEF.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Reg Register whose defining instruction is matched.
  /// \return True if the definition is G_IMPLICIT_DEF.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    MachineInstr *TmpMI;
    if (mi_match(Reg, MRI, m_MInstr(TmpMI)))
      return TmpMI->getOpcode() == TargetOpcode::G_IMPLICIT_DEF;
    return false;
  }
};

/// Matches a G_IMPLICIT_DEF.
///
/// \return A matcher for G_IMPLICIT_DEF.
inline ImplicitDefMatch m_GImplicitDef() { return ImplicitDefMatch(); }

/// Binds the defining instruction of a register if it is a Class.
///
/// Prefer the named helpers below so the opcode is spelled out at the call
/// site.
template <typename Class> struct GInstrBind {
  /// Output that receives the matched instruction.
  Class *&Inst;

  /// Construct a binder for instructions of type Class.
  ///
  /// \param Inst Output that receives the matched instruction.
  GInstrBind(Class *&Inst) : Inst(Inst) {}
  /// Match \p Reg if its defining instruction is a Class.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Reg Register whose defining instruction is matched.
  /// \return True if the defining instruction was bound.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    MachineInstr *TmpMI;
    if (mi_match(Reg, MRI, m_MInstr(TmpMI))) {
      if (auto *Cst = dyn_cast<Class>(TmpMI)) {
        Inst = Cst;
        return true;
      }
    }
    return false;
  }
};

/// Matches a literal G_CONSTANT (no look-through of splats or copies).
///
/// \param Inst Output that receives the matched GConstant.
/// \return A matcher for a literal G_CONSTANT (no look-through of splats or copies).
inline GInstrBind<GConstant> m_GConstant(GConstant *&Inst) { return Inst; }
/// Matches a literal const G_CONSTANT (no look-through of splats or copies).
///
/// \param Inst Output that receives the matched GConstant.
/// \return A matcher for a literal const G_CONSTANT (no look-through of splats or copies).
inline GInstrBind<const GConstant> m_GConstant(const GConstant *&Inst) {
  return Inst;
}

/// Matcher that binds raw bits from a G_CONSTANT or G_FCONSTANT.
struct GConstantBitsMatch {
  /// Output that receives the constant's integer bit pattern.
  APInt &Bits;
  /// Match \p Reg if it is a literal G_CONSTANT or G_FCONSTANT.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Reg Register whose defining instruction is matched.
  /// \return True if a constant was bound.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    MachineInstr *MI = MRI.getVRegDef(Reg);
    if (MI->getOpcode() == TargetOpcode::G_CONSTANT) {
      Bits = MI->getOperand(1).getCImm()->getValue();
      return true;
    }
    if (MI->getOpcode() == TargetOpcode::G_FCONSTANT) {
      Bits = MI->getOperand(1).getFPImm()->getValueAPF().bitcastToAPInt();
      return true;
    }
    return false;
  }
};

/// Matches a literal G_CONSTANT or G_FCONSTANT, binding raw bits to \p Bits.
///
/// For floats, bits are the value reinterpreted as an integer.
///
/// \param Bits Output that receives the constant bit pattern.
/// \return A matcher that binds raw bits from a G_CONSTANT or G_FCONSTANT to \p Bits.
inline GConstantBitsMatch m_GConstantOrFConstantBits(APInt &Bits) {
  return {Bits};
}

/// Matcher for a load of type Class with optional instruction/MMO binding.
///
/// Binds the pointer operand like IR's m_Load, and optionally the instruction
/// and/or its MachineMemOperand.
template <typename Class, typename PtrP> struct LoadOp_match {
  /// Sub-pattern for the pointer operand.
  PtrP Ptr;
  /// Optional output that receives the matched load instruction.
  Class **InstOut = nullptr;
  /// Optional output that receives the matched MachineMemOperand.
  const MachineMemOperand **MMOOut = nullptr;

  /// Construct a load matcher that only binds the pointer.
  ///
  /// \param Ptr Sub-pattern for the pointer operand.
  LoadOp_match(const PtrP &Ptr) : Ptr(Ptr) {}
  /// Construct a load matcher that also binds the memoperand.
  ///
  /// \param Ptr Sub-pattern for the pointer operand.
  /// \param MMO Output reference that receives the MachineMemOperand.
  LoadOp_match(const PtrP &Ptr, MMORef MMO) : Ptr(Ptr), MMOOut(&MMO.MMO) {}
  /// Construct a load matcher that binds the instruction and pointer.
  ///
  /// \param Inst Output that receives the matched load.
  /// \param Ptr Sub-pattern for the pointer operand.
  LoadOp_match(Class *&Inst, const PtrP &Ptr) : Ptr(Ptr), InstOut(&Inst) {}
  /// Construct a load matcher that binds instruction, pointer, and memoperand.
  ///
  /// \param Inst Output that receives the matched load.
  /// \param Ptr Sub-pattern for the pointer operand.
  /// \param MMO Output reference that receives the MachineMemOperand.
  LoadOp_match(Class *&Inst, const PtrP &Ptr, MMORef MMO)
      : Ptr(Ptr), InstOut(&Inst), MMOOut(&MMO.MMO) {}

  /// Match \p Reg if it is defined by a Class load with a matching pointer.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Reg Register whose defining instruction is matched.
  /// \return True if the load matches.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    MachineInstr *TmpMI;
    if (!mi_match(Reg, MRI, m_MInstr(TmpMI)))
      return false;
    auto *Load = dyn_cast<Class>(TmpMI);
    if (!Load || !Ptr.match(MRI, Load->getPointerReg()))
      return false;
    if (InstOut)
      *InstOut = Load;
    if (MMOOut)
      *MMOOut = &Load->getMMO();
    return true;
  }
};

/// Matches any load (GAnyLoad), binding its pointer operand.
///
/// \param Ptr Sub-pattern for the pointer operand.
/// \return A matcher for any load (GAnyLoad), binding its pointer operand.
template <typename PtrP>
inline LoadOp_match<GAnyLoad, PtrP> m_GAnyLoad(const PtrP &Ptr) {
  return LoadOp_match<GAnyLoad, PtrP>(Ptr);
}
/// Matches any load, binding the instruction and pointer operand.
///
/// \param Inst Output that receives the matched GAnyLoad.
/// \param Ptr Sub-pattern for the pointer operand.
/// \return A matcher for any load, binding the instruction and pointer operand.
template <typename PtrP>
inline LoadOp_match<GAnyLoad, PtrP> m_GAnyLoad(GAnyLoad *&Inst,
                                               const PtrP &Ptr) {
  return LoadOp_match<GAnyLoad, PtrP>(Inst, Ptr);
}
/// Matches any load, binding instruction, pointer, and memoperand.
///
/// \param Inst Output that receives the matched GAnyLoad.
/// \param Ptr Sub-pattern for the pointer operand.
/// \param MMO Output reference that receives the MachineMemOperand.
/// \return A matcher for any load, binding instruction, pointer, and memoperand.
template <typename PtrP>
inline LoadOp_match<GAnyLoad, PtrP> m_GAnyLoad(GAnyLoad *&Inst, const PtrP &Ptr,
                                               MMORef MMO) {
  return LoadOp_match<GAnyLoad, PtrP>(Inst, Ptr, MMO);
}
/// Matches a G_LOAD, binding its pointer operand.
///
/// \param Ptr Sub-pattern for the pointer operand.
/// \return A matcher for a G_LOAD, binding its pointer operand.
template <typename PtrP>
inline LoadOp_match<GLoad, PtrP> m_GLoad(const PtrP &Ptr) {
  return LoadOp_match<GLoad, PtrP>(Ptr);
}
/// Matches a G_LOAD, binding its pointer and memoperand.
///
/// \param Ptr Sub-pattern for the pointer operand.
/// \param MMO Output reference that receives the MachineMemOperand.
/// \return A matcher for a G_LOAD, binding its pointer and memoperand.
template <typename PtrP>
inline LoadOp_match<GLoad, PtrP> m_GLoad(const PtrP &Ptr, MMORef MMO) {
  return LoadOp_match<GLoad, PtrP>(Ptr, MMO);
}

/// Binds a G_UNMERGE_VALUES defining instruction.
///
/// \param Inst Output that receives the matched GUnmerge.
/// \return A matcher that binds a G_UNMERGE_VALUES defining instruction.
inline GInstrBind<GUnmerge> m_GUnmerge(GUnmerge *&Inst) { return Inst; }
/// Binds a G_VSCALE defining instruction.
///
/// \param Inst Output that receives the matched GVScale.
/// \return A matcher that binds a G_VSCALE defining instruction.
inline GInstrBind<GVScale> m_GVScale(GVScale *&Inst) { return Inst; }
/// Binds a G_BUILD_VECTOR defining instruction.
///
/// \param Inst Output that receives the matched GBuildVector.
/// \return A matcher that binds a G_BUILD_VECTOR defining instruction.
inline GInstrBind<GBuildVector> m_GBuildVector(GBuildVector *&Inst) {
  return Inst;
}
/// Binds a G_CONCAT_VECTORS defining instruction.
///
/// \param Inst Output that receives the matched GConcatVectors.
/// \return A matcher that binds a G_CONCAT_VECTORS defining instruction.
inline GInstrBind<GConcatVectors> m_GConcatVectors(GConcatVectors *&Inst) {
  return Inst;
}

/// Binds the defining instruction if it is a GIntrinsic.
///
/// Matches any of the four G_INTRINSIC* opcodes.
///
/// \param Inst Output that receives the matched GIntrinsic.
/// \return A matcher that binds the defining instruction if it is a GIntrinsic.
inline GInstrBind<GIntrinsic> m_GIntrinsic(GIntrinsic *&Inst) { return Inst; }
/// Binds the defining instruction if it is a const GIntrinsic.
///
/// \param Inst Output that receives the matched GIntrinsic.
/// \return A matcher that binds the defining instruction if it is a const GIntrinsic.
inline GInstrBind<const GIntrinsic> m_GIntrinsic(const GIntrinsic *&Inst) {
  return Inst;
}

/// Matcher for a GIntrinsic with a fixed intrinsic ID and argument patterns.
template <Intrinsic::ID IntrID, typename... OpMatchers>
struct GIntrinsic_match {
  /// Sub-patterns for the intrinsic's leading argument operands.
  std::tuple<OpMatchers...> Operands;

  /// Construct an intrinsic matcher from argument sub-patterns.
  ///
  /// \param Ops Sub-patterns for leading intrinsic arguments.
  GIntrinsic_match(const OpMatchers &...Ops) : Operands(Ops...) {}

  /// Match \p Op if it is the expected GIntrinsic with matching arguments.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Op Value or instruction to match.
  /// \return True if the intrinsic matches.
  template <typename OpTy>
  bool match(const MachineRegisterInfo &MRI, OpTy &&Op) {
    MachineInstr *TmpMI;
    if (!mi_match(Op, MRI, m_MInstr(TmpMI)))
      return false;
    auto *GI = dyn_cast<GIntrinsic>(TmpMI);
    if (!GI || !GI->is(IntrID))
      return false;
    return matchOperands(MRI, *GI, std::index_sequence_for<OpMatchers...>{});
  }

private:
  template <size_t... Is>
  bool matchOperands(const MachineRegisterInfo &MRI, GIntrinsic &GI,
                     std::index_sequence<Is...>) {
    // Intrinsic arguments follow the ID operand.
    unsigned FirstArg = GI.getNumExplicitDefs() + 1;
    return (std::get<Is>(Operands).match(
                MRI, GI.getOperand(FirstArg + Is).getReg()) &&
            ...);
  }
};

/// Matches a GIntrinsic with ID IntrID and the given argument sub-patterns.
///
/// \param Ops Sub-patterns for leading intrinsic arguments.
/// \return A matcher for a GIntrinsic with ID IntrID and the given argument sub-patterns.
template <Intrinsic::ID IntrID, typename... OpMatchers>
inline GIntrinsic_match<IntrID, OpMatchers...>
m_GIntrinsic(const OpMatchers &...Ops) {
  return GIntrinsic_match<IntrID, OpMatchers...>(Ops...);
}

/// Matcher for G_SHUFFLE_VECTOR with source and mask bindings.
template <typename Src1Ty, typename Src2Ty> struct ShuffleVectorMatch {
  /// Sub-pattern for the first shuffle source.
  Src1Ty Src1;
  /// Sub-pattern for the second shuffle source.
  Src2Ty Src2;
  /// Output that receives the shuffle mask.
  ArrayRef<int> &Mask;

  /// Construct a shuffle-vector matcher.
  ///
  /// \param Src1 Sub-pattern for the first source.
  /// \param Src2 Sub-pattern for the second source.
  /// \param Mask Output that receives the shuffle mask.
  ShuffleVectorMatch(const Src1Ty &Src1, const Src2Ty &Src2,
                     ArrayRef<int> &Mask)
      : Src1(Src1), Src2(Src2), Mask(Mask) {}
  /// Match \p Reg if it is defined by a matching G_SHUFFLE_VECTOR.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Reg Register whose defining instruction is matched.
  /// \return True if the definition matches.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    MachineInstr *TmpMI;
    if (!mi_match(Reg, MRI, m_MInstr(TmpMI)))
      return false;
    auto *Shuf = dyn_cast<GShuffleVector>(TmpMI);
    if (!Shuf || !Src1.match(MRI, Shuf->getSrc1Reg()) ||
        !Src2.match(MRI, Shuf->getSrc2Reg()))
      return false;
    Mask = Shuf->getMask();
    return true;
  }
};

/// Matches a G_SHUFFLE_VECTOR, binding sources and mask.
///
/// \param Src1 Sub-pattern for the first source.
/// \param Src2 Sub-pattern for the second source.
/// \param Mask Output that receives the shuffle mask.
/// \return A matcher for a G_SHUFFLE_VECTOR, binding sources and mask.
template <typename Src1Ty, typename Src2Ty>
inline ShuffleVectorMatch<Src1Ty, Src2Ty>
m_GShuffleVector(const Src1Ty &Src1, const Src2Ty &Src2, ArrayRef<int> &Mask) {
  return ShuffleVectorMatch<Src1Ty, Src2Ty>(Src1, Src2, Mask);
}

/// Matcher that binds the frame index from a G_FRAME_INDEX.
struct GFrameIndexMatch {
  /// Output that receives the matched frame index.
  int &FI;
  /// Match \p Reg if it is defined by G_FRAME_INDEX.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Reg Register whose defining instruction is matched.
  /// \return True if the definition is a G_FRAME_INDEX.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    MachineInstr *TmpMI;
    if (mi_match(Reg, MRI, m_MInstr(TmpMI)) &&
        TmpMI->getOpcode() == TargetOpcode::G_FRAME_INDEX) {
      FI = TmpMI->getOperand(1).getIndex();
      return true;
    }
    return false;
  }
};

/// Matches a G_FRAME_INDEX, binding its frame index to \p FI.
///
/// \param FI Output that receives the matched frame index.
/// \return A matcher for a G_FRAME_INDEX, binding its frame index to \p FI.
inline GFrameIndexMatch m_GFrameIndex(int &FI) { return {FI}; }

/// Binds a G_FCONSTANT value to \p C.
///
/// \param C Output that receives the matched floating-point constant.
/// \return A matcher that binds a G_FCONSTANT value to \p C.
inline bind_ty<const ConstantFP *> m_GFCst(const ConstantFP *&C) { return C; }

/// Matcher for a fixed binary generic opcode such as G_ADD or G_SUB.
template <typename LHS_P, typename RHS_P, unsigned Opcode,
          bool Commutable = false, unsigned Flags = MachineInstr::NoFlags>
struct BinaryOp_match {
  /// Sub-pattern for the left-hand operand.
  LHS_P L;
  /// Sub-pattern for the right-hand operand.
  RHS_P R;
  /// Optional output that receives the matched instruction's flags.
  uint32_t *FlagsOut = nullptr;

  /// Construct a binary matcher from two operand sub-patterns.
  ///
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  BinaryOp_match(const LHS_P &LHS, const RHS_P &RHS) : L(LHS), R(RHS) {}
  /// Construct a binary matcher that also binds instruction flags.
  ///
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  /// \param FlagsOut Output reference that receives the matched MI flags.
  BinaryOp_match(const LHS_P &LHS, const RHS_P &RHS, MIFlagsRef FlagsOut)
      : L(LHS), R(RHS), FlagsOut(&FlagsOut.Flags) {}
  /// Match \p Op if it is Opcode with matching operands and required flags.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Op Value or instruction to match.
  /// \return True if the instruction matches.
  template <typename OpTy>
  bool match(const MachineRegisterInfo &MRI, OpTy &&Op) {
    const MachineInstr *TmpMI;
    if (mi_match(Op, MRI, m_MInstr(TmpMI))) {
      if (TmpMI->getOpcode() == Opcode && TmpMI->getNumOperands() == 3) {
        if ((!L.match(MRI, TmpMI->getOperand(1).getReg()) ||
             !R.match(MRI, TmpMI->getOperand(2).getReg())) &&
            // NOTE: When trying the alternative operand ordering
            // with a commutative operation, it is imperative to always run
            // the LHS sub-pattern  (i.e. `L`) before the RHS sub-pattern
            // (i.e. `R`). Otherwise, m_DeferredReg/Type will not work as
            // expected.
            (!Commutable || !L.match(MRI, TmpMI->getOperand(2).getReg()) ||
             !R.match(MRI, TmpMI->getOperand(1).getReg())))
          return false;
        if ((TmpMI->getFlags() & Flags) != Flags)
          return false;
        if (FlagsOut)
          *FlagsOut = TmpMI->getFlags();
        return true;
      }
    }
    return false;
  }
};

/// Matcher for a binary generic opcode supplied at construction time.
template <typename LHS_P, typename RHS_P, bool Commutable = false>
struct BinaryOpc_match {
  /// Opcode that must be matched.
  unsigned Opc;
  /// Sub-pattern for the left-hand operand.
  LHS_P L;
  /// Sub-pattern for the right-hand operand.
  RHS_P R;

  /// Construct a binary opcode matcher.
  ///
  /// \param Opcode Generic opcode to match.
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  BinaryOpc_match(unsigned Opcode, const LHS_P &LHS, const RHS_P &RHS)
      : Opc(Opcode), L(LHS), R(RHS) {}
  /// Match \p Op if it is Opc with matching operands.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Op Value or instruction to match.
  /// \return True if the instruction matches.
  template <typename OpTy>
  bool match(const MachineRegisterInfo &MRI, OpTy &&Op) {
    MachineInstr *TmpMI;
    if (mi_match(Op, MRI, m_MInstr(TmpMI))) {
      if (TmpMI->getOpcode() == Opc && TmpMI->getNumDefs() == 1 &&
          TmpMI->getNumOperands() == 3) {
        return (L.match(MRI, TmpMI->getOperand(1).getReg()) &&
                R.match(MRI, TmpMI->getOperand(2).getReg())) ||
               // NOTE: When trying the alternative operand ordering
               // with a commutative operation, it is imperative to always run
               // the LHS sub-pattern  (i.e. `L`) before the RHS sub-pattern
               // (i.e. `R`). Otherwise, m_DeferredReg/Type will not work as
               // expected.
               (Commutable && (L.match(MRI, TmpMI->getOperand(2).getReg()) &&
                               R.match(MRI, TmpMI->getOperand(1).getReg())));
      }
    }
    return false;
  }
};

/// Matches a binary opcode with the given operand sub-patterns.
///
/// \param Opcode Generic opcode to match.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a binary opcode with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, false> m_BinOp(unsigned Opcode, const LHS &L,
                                                const RHS &R) {
  return BinaryOpc_match<LHS, RHS, false>(Opcode, L, R);
}

/// Matches a commutative binary opcode with the given operand sub-patterns.
///
/// \param Opcode Generic opcode to match.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a commutative binary opcode with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOpc_match<LHS, RHS, true>
m_CommutativeBinOp(unsigned Opcode, const LHS &L, const RHS &R) {
  return BinaryOpc_match<LHS, RHS, true>(Opcode, L, R);
}

/// Matches a G_ADD with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_ADD with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_ADD, true>
m_GAdd(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_ADD, true>(L, R);
}

/// Matches a G_BUILD_VECTOR with two source sub-patterns.
///
/// \param L Sub-pattern for the first element.
/// \param R Sub-pattern for the second element.
/// \return A matcher for a G_BUILD_VECTOR with two source sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_BUILD_VECTOR, false>
m_GBuildVector(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_BUILD_VECTOR, false>(L, R);
}

/// Matches a G_BUILD_VECTOR_TRUNC with two source sub-patterns.
///
/// \param L Sub-pattern for the first element.
/// \param R Sub-pattern for the second element.
/// \return A matcher for a G_BUILD_VECTOR_TRUNC with two source sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_BUILD_VECTOR_TRUNC, false>
m_GBuildVectorTrunc(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_BUILD_VECTOR_TRUNC, false>(L,
                                                                             R);
}

/// Matches a G_PTR_ADD with the given operand sub-patterns.
///
/// \param L Sub-pattern for the pointer operand.
/// \param R Sub-pattern for the offset operand.
/// \return A matcher for a G_PTR_ADD with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_PTR_ADD, false>
m_GPtrAdd(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_PTR_ADD, false>(L, R);
}

/// Matches a G_PTR_ADD and binds the instruction flags.
///
/// \param L Sub-pattern for the pointer operand.
/// \param R Sub-pattern for the offset operand.
/// \param Flags Output reference that receives the matched MI flags.
/// \return A matcher for a G_PTR_ADD and binds the instruction flags.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_PTR_ADD, false>
m_GPtrAdd(const LHS &L, const RHS &R, MIFlagsRef Flags) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_PTR_ADD, false>(L, R, Flags);
}

/// Matches a G_SUB with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_SUB with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_SUB> m_GSub(const LHS &L,
                                                            const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_SUB>(L, R);
}

/// Matches a G_SUB and binds the instruction flags.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \param Flags Output reference that receives the matched MI flags.
/// \return A matcher for a G_SUB and binds the instruction flags.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_SUB>
m_GSub(const LHS &L, const RHS &R, MIFlagsRef Flags) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_SUB>(L, R, Flags);
}

/// Matches a G_MUL (integer multiply) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_MUL (integer multiply) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_MUL, true>
m_GMul(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_MUL, true>(L, R);
}

/// Matches a G_FADD (floating-point add) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_FADD (floating-point add) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_FADD, true>
m_GFAdd(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_FADD, true>(L, R);
}

/// Matches a G_FMUL (floating-point multiply) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_FMUL (floating-point multiply) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_FMUL, true>
m_GFMul(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_FMUL, true>(L, R);
}

/// Matches a G_FSUB (floating-point subtract) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_FSUB (floating-point subtract) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_FSUB, false>
m_GFSub(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_FSUB, false>(L, R);
}

/// Matches a G_AND (bitwise and) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_AND (bitwise and) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_AND, true>
m_GAnd(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_AND, true>(L, R);
}

/// Matches a G_XOR (bitwise xor) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_XOR (bitwise xor) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_XOR, true>
m_GXor(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_XOR, true>(L, R);
}

/// Matches a G_OR with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_OR with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_OR, true> m_GOr(const LHS &L,
                                                                const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_OR, true>(L, R);
}

/// Matches a G_OR that is marked disjoint.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_OR that is marked disjoint.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_OR, true,
                      MachineInstr::Disjoint>
m_GDisjointOr(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_OR, true,
                        MachineInstr::Disjoint>(L, R);
}

/// Matches a G_ADD or a disjoint G_OR (add-like forms).
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for G_ADD or a disjoint G_OR.
template <typename LHS, typename RHS>
inline auto m_GAddLike(const LHS &L, const RHS &R) {
  return m_any_of(m_GAdd(L, R), m_GDisjointOr(L, R));
}

/// Matches a G_SHL (left-shift) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_SHL (left-shift) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_SHL, false>
m_GShl(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_SHL, false>(L, R);
}

/// Matches a G_LSHR (logical right-shift) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_LSHR (logical right-shift) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_LSHR, false>
m_GLShr(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_LSHR, false>(L, R);
}

/// Matches a G_ASHR (arithmetic right-shift) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_ASHR (arithmetic right-shift) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_ASHR, false>
m_GAShr(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_ASHR, false>(L, R);
}

/// Matches a G_SMAX (signed maximum) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_SMAX (signed maximum) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_SMAX, true>
m_GSMax(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_SMAX, true>(L, R);
}

/// Matches a G_SMIN (signed minimum) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_SMIN (signed minimum) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_SMIN, true>
m_GSMin(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_SMIN, true>(L, R);
}

/// Matches a G_UMAX (unsigned maximum) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_UMAX (unsigned maximum) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_UMAX, true>
m_GUMax(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_UMAX, true>(L, R);
}

/// Matches a G_UMIN (unsigned minimum) with the given operand sub-patterns.
///
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_UMIN (unsigned minimum) with the given operand sub-patterns.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_UMIN, true>
m_GUMin(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_UMIN, true>(L, R);
}

/// Matcher for a unary generic opcode with one source sub-pattern.
template <typename SrcTy, unsigned Opcode> struct UnaryOp_match {
  /// Sub-pattern for the source operand.
  SrcTy L;

  /// Construct a unary matcher from a source sub-pattern.
  ///
  /// \param LHS Sub-pattern for the source operand.
  UnaryOp_match(const SrcTy &LHS) : L(LHS) {}
  /// Match \p Op if it is Opcode with a matching source operand.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Op Value or instruction to match.
  /// \return True if the instruction matches.
  template <typename OpTy>
  bool match(const MachineRegisterInfo &MRI, OpTy &&Op) {
    MachineInstr *TmpMI;
    if (mi_match(Op, MRI, m_MInstr(TmpMI))) {
      if (TmpMI->getOpcode() == Opcode && TmpMI->getNumOperands() == 2) {
        return L.match(MRI, TmpMI->getOperand(1).getReg());
      }
    }
    return false;
  }
};

/// Matches a G_ANYEXT of \p Src.
///
/// \param Src Sub-pattern for the any-extended operand.
/// \return A matcher for a G_ANYEXT of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_ANYEXT>
m_GAnyExt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_ANYEXT>(Src);
}

/// Matches a G_SEXT of \p Src.
///
/// \param Src Sub-pattern for the sign-extended operand.
/// \return A matcher for a G_SEXT of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_SEXT> m_GSExt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_SEXT>(Src);
}

/// Matches a G_ZEXT of \p Src.
///
/// \param Src Sub-pattern for the zero-extended operand.
/// \return A matcher for a G_ZEXT of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_ZEXT> m_GZExt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_ZEXT>(Src);
}

/// Matches a G_FPEXT of \p Src.
///
/// \param Src Sub-pattern for the floating-point extended operand.
/// \return A matcher for a G_FPEXT of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_FPEXT> m_GFPExt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_FPEXT>(Src);
}

/// Matches a G_TRUNC of \p Src.
///
/// \param Src Sub-pattern for the truncated operand.
/// \return A matcher for a G_TRUNC of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_TRUNC> m_GTrunc(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_TRUNC>(Src);
}

/// Matches a G_BITCAST of \p Src.
///
/// \param Src Sub-pattern for the bitcast operand.
/// \return A matcher for a G_BITCAST of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_BITCAST>
m_GBitcast(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_BITCAST>(Src);
}

/// Matches a G_PTRTOINT of \p Src.
///
/// \param Src Sub-pattern for the ptrtoint operand.
/// \return A matcher for a G_PTRTOINT of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_PTRTOINT>
m_GPtrToInt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_PTRTOINT>(Src);
}

/// Matches a G_INTTOPTR of \p Src.
///
/// \param Src Sub-pattern for the inttoptr operand.
/// \return A matcher for a G_INTTOPTR of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_INTTOPTR>
m_GIntToPtr(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_INTTOPTR>(Src);
}

/// Matches a G_FPTRUNC of \p Src.
///
/// \param Src Sub-pattern for the floating-point truncated operand.
/// \return A matcher for a G_FPTRUNC of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_FPTRUNC>
m_GFPTrunc(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_FPTRUNC>(Src);
}

/// Matcher for an opcode with one source register and one immediate operand.
///
/// Used for ops such as G_SEXT_INREG and G_ASSERT_ZEXT.
template <typename SrcTy, typename ImmTy, unsigned Opcode>
struct SrcImmOp_match {
  /// Sub-pattern for the source register operand.
  SrcTy L;
  /// Sub-pattern for the immediate operand.
  ImmTy Imm;

  /// Construct a source-plus-immediate matcher.
  ///
  /// \param LHS Sub-pattern for the source register.
  /// \param Imm Sub-pattern for the immediate.
  SrcImmOp_match(const SrcTy &LHS, const ImmTy &Imm) : L(LHS), Imm(Imm) {}
  /// Match \p Op if it is Opcode with matching source and immediate.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Op Value or instruction to match.
  /// \return True if the instruction matches.
  template <typename OpTy>
  bool match(const MachineRegisterInfo &MRI, OpTy &&Op) {
    MachineInstr *TmpMI;
    return mi_match(Op, MRI, m_MInstr(TmpMI)) && TmpMI->getOpcode() == Opcode &&
           L.match(MRI, TmpMI->getOperand(1).getReg()) &&
           Imm.match(TmpMI->getOperand(2).getImm());
  }
};

/// Matches any immediate operand.
struct AnyImmMatch {
  /// Always succeed for any immediate value.
  ///
  /// \param Imm Immediate value (ignored).
  /// \return Always true.
  bool match(int64_t Imm) const { return true; }
};

/// Matches a G_SEXT_INREG, accepting any immediate width.
///
/// \param Src Sub-pattern for the sign-extended source.
/// \return A matcher for a G_SEXT_INREG, accepting any immediate width.
template <typename SrcTy>
inline SrcImmOp_match<SrcTy, AnyImmMatch, TargetOpcode::G_SEXT_INREG>
m_GSExtInReg(const SrcTy &Src) {
  return {Src, AnyImmMatch()};
}

/// Matches a G_SEXT_INREG with source and immediate sub-patterns.
///
/// \param Src Sub-pattern for the sign-extended source.
/// \param Imm Sub-pattern for the width immediate.
/// \return A matcher for a G_SEXT_INREG with source and immediate sub-patterns.
template <typename SrcTy, typename ImmTy>
inline SrcImmOp_match<SrcTy, ImmTy, TargetOpcode::G_SEXT_INREG>
m_GSExtInReg(const SrcTy &Src, const ImmTy &Imm) {
  return {Src, Imm};
}

/// Matches a G_ASSERT_ZEXT, accepting any immediate bit width.
///
/// \param Src Sub-pattern for the asserted source.
/// \return A matcher for a G_ASSERT_ZEXT, accepting any immediate bit width.
template <typename SrcTy>
inline SrcImmOp_match<SrcTy, AnyImmMatch, TargetOpcode::G_ASSERT_ZEXT>
m_GAssertZext(const SrcTy &Src) {
  return {Src, AnyImmMatch()};
}

/// Matches a G_ASSERT_ZEXT with source and immediate sub-patterns.
///
/// \param Src Sub-pattern for the asserted source.
/// \param Imm Sub-pattern for the bit-width immediate.
/// \return A matcher for a G_ASSERT_ZEXT with source and immediate sub-patterns.
template <typename SrcTy, typename ImmTy>
inline SrcImmOp_match<SrcTy, ImmTy, TargetOpcode::G_ASSERT_ZEXT>
m_GAssertZext(const SrcTy &Src, const ImmTy &Imm) {
  return {Src, Imm};
}

/// Matches a G_FABS of \p Src.
///
/// \param Src Sub-pattern for the absolute-value operand.
/// \return A matcher for a G_FABS of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_FABS> m_GFabs(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_FABS>(Src);
}

/// Matches a G_FNEG of \p Src.
///
/// \param Src Sub-pattern for the negated operand.
/// \return A matcher for a G_FNEG of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_FNEG> m_GFNeg(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_FNEG>(Src);
}

/// Matches a COPY of \p Src.
///
/// \param Src Sub-pattern for the copied operand.
/// \return A matcher for a COPY of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::COPY> m_Copy(SrcTy &&Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::COPY>(std::forward<SrcTy>(Src));
}

/// Matches a G_FSQRT of \p Src.
///
/// \param Src Sub-pattern for the square-root operand.
/// \return A matcher for a G_FSQRT of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_FSQRT> m_GFSqrt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_FSQRT>(Src);
}

/// Matches a G_FFLOOR of \p Src.
///
/// \param Src Sub-pattern for the floor operand.
/// \return A matcher for a G_FFLOOR of \p Src.
template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_FFLOOR>
m_GFFloor(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_FFLOOR>(Src);
}

/// Matcher for generic compare opcodes such as G_ICMP and G_FCMP.
///
/// TODO: Allow checking a specific predicate.
template <typename Pred_P, typename LHS_P, typename RHS_P, unsigned Opcode,
          bool Commutable = false>
struct CompareOp_match {
  /// Sub-pattern for the compare predicate.
  Pred_P P;
  /// Sub-pattern for the left-hand operand.
  LHS_P L;
  /// Sub-pattern for the right-hand operand.
  RHS_P R;

  /// Construct a compare matcher from predicate and operand sub-patterns.
  ///
  /// \param Pred Sub-pattern for the compare predicate.
  /// \param LHS Sub-pattern for the left-hand operand.
  /// \param RHS Sub-pattern for the right-hand operand.
  CompareOp_match(const Pred_P &Pred, const LHS_P &LHS, const RHS_P &RHS)
      : P(Pred), L(LHS), R(RHS) {}

  /// Match \p Op if it is Opcode with matching predicate and operands.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Op Value or instruction to match.
  /// \return True if the instruction matches.
  template <typename OpTy>
  bool match(const MachineRegisterInfo &MRI, OpTy &&Op) {
    MachineInstr *TmpMI;
    if (!mi_match(Op, MRI, m_MInstr(TmpMI)) || TmpMI->getOpcode() != Opcode)
      return false;

    auto TmpPred =
        static_cast<CmpInst::Predicate>(TmpMI->getOperand(1).getPredicate());
    if (!P.match(MRI, TmpPred))
      return false;
    Register LHS = TmpMI->getOperand(2).getReg();
    Register RHS = TmpMI->getOperand(3).getReg();
    if (L.match(MRI, LHS) && R.match(MRI, RHS))
      return true;
    // NOTE: When trying the alternative operand ordering
    // with a commutative operation, it is imperative to always run
    // the LHS sub-pattern  (i.e. `L`) before the RHS sub-pattern
    // (i.e. `R`). Otherwise, m_DeferredReg/Type will not work as expected.
    if (Commutable && L.match(MRI, RHS) && R.match(MRI, LHS) &&
        P.match(MRI, CmpInst::getSwappedPredicate(TmpPred)))
      return true;
    return false;
  }
};

/// Matcher for a classify-style opcode such as G_IS_FPCLASS.
template <typename LHS_P, typename Test_P, unsigned Opcode>
struct ClassifyOp_match {
  /// Sub-pattern for the classified value.
  LHS_P L;
  /// Sub-pattern for the class-test immediate.
  Test_P T;

  /// Construct a classify matcher from value and test sub-patterns.
  ///
  /// \param LHS Sub-pattern for the classified value.
  /// \param Tst Sub-pattern for the class-test immediate.
  ClassifyOp_match(const LHS_P &LHS, const Test_P &Tst) : L(LHS), T(Tst) {}

  /// Match \p Op if it is Opcode with matching value and class test.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Op Value or instruction to match.
  /// \return True if the instruction matches.
  template <typename OpTy>
  bool match(const MachineRegisterInfo &MRI, OpTy &&Op) {
    MachineInstr *TmpMI;
    if (!mi_match(Op, MRI, m_MInstr(TmpMI)) || TmpMI->getOpcode() != Opcode)
      return false;

    Register LHS = TmpMI->getOperand(1).getReg();
    if (!L.match(MRI, LHS))
      return false;

    FPClassTest TmpClass =
        static_cast<FPClassTest>(TmpMI->getOperand(2).getImm());
    if (T.match(MRI, TmpClass))
      return true;

    return false;
  }
};

/// Matches a G_ICMP with the given predicate and operand sub-patterns.
///
/// \param P Sub-pattern for the integer predicate.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_ICMP with the given predicate and operand sub-patterns.
template <typename Pred, typename LHS, typename RHS>
inline CompareOp_match<Pred, LHS, RHS, TargetOpcode::G_ICMP>
m_GICmp(const Pred &P, const LHS &L, const RHS &R) {
  return CompareOp_match<Pred, LHS, RHS, TargetOpcode::G_ICMP>(P, L, R);
}

/// Matches a G_FCMP with the given predicate and operand sub-patterns.
///
/// \param P Sub-pattern for the floating-point predicate.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A matcher for a G_FCMP with the given predicate and operand sub-patterns.
template <typename Pred, typename LHS, typename RHS>
inline CompareOp_match<Pred, LHS, RHS, TargetOpcode::G_FCMP>
m_GFCmp(const Pred &P, const LHS &L, const RHS &R) {
  return CompareOp_match<Pred, LHS, RHS, TargetOpcode::G_FCMP>(P, L, R);
}

/// Matches a G_ICMP, including commuted operand order.
///
/// E.g.
///
/// m_c_GICmp(m_Pred(...), m_GAdd(...), m_GSub(...))
///
/// Could match both of:
///
/// icmp ugt (add x, y) (sub a, b)
/// icmp ult (sub a, b) (add x, y)
///
/// \param P Sub-pattern for the integer predicate.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A commutative matcher for G_ICMP.
template <typename Pred, typename LHS, typename RHS>
inline CompareOp_match<Pred, LHS, RHS, TargetOpcode::G_ICMP, true>
m_c_GICmp(const Pred &P, const LHS &L, const RHS &R) {
  return CompareOp_match<Pred, LHS, RHS, TargetOpcode::G_ICMP, true>(P, L, R);
}

/// Matches a G_FCMP, including commuted operand order.
///
/// E.g.
///
/// m_c_GFCmp(m_Pred(...), m_FAdd(...), m_GFMul(...))
///
/// Could match both of:
///
/// fcmp ogt (fadd x, y) (fmul a, b)
/// fcmp olt (fmul a, b) (fadd x, y)
///
/// \param P Sub-pattern for the floating-point predicate.
/// \param L Sub-pattern for the left-hand operand.
/// \param R Sub-pattern for the right-hand operand.
/// \return A commutative matcher for G_FCMP.
template <typename Pred, typename LHS, typename RHS>
inline CompareOp_match<Pred, LHS, RHS, TargetOpcode::G_FCMP, true>
m_c_GFCmp(const Pred &P, const LHS &L, const RHS &R) {
  return CompareOp_match<Pred, LHS, RHS, TargetOpcode::G_FCMP, true>(P, L, R);
}

/// Matches a G_IS_FPCLASS test against a value and class mask.
///
/// G_IS_FPCLASS %val, 96
///
/// \param L Sub-pattern for the tested value.
/// \param T Sub-pattern for the FP class test immediate.
/// \return A matcher for a G_IS_FPCLASS test against a value and class mask.
template <typename LHS, typename Test>
inline ClassifyOp_match<LHS, Test, TargetOpcode::G_IS_FPCLASS>
m_GIsFPClass(const LHS &L, const Test &T) {
  return ClassifyOp_match<LHS, Test, TargetOpcode::G_IS_FPCLASS>(L, T);
}

/// Matcher that requires a register to have a specific LLT.
struct CheckType {
  /// Required type of the matched register.
  LLT Ty;
  /// Construct a type check for \p Ty.
  ///
  /// \param Ty Exact LLT required of the matched register.
  CheckType(const LLT Ty) : Ty(Ty) {}

  /// Match if \p Reg has type \p Ty.
  ///
  /// \param MRI Register info used to query the type.
  /// \param Reg Register whose type is checked.
  /// \return True if the register type equals Ty.
  bool match(const MachineRegisterInfo &MRI, Register Reg) {
    return MRI.getType(Reg) == Ty;
  }
};

/// Matches a register whose LLT equals \p Ty.
///
/// \param Ty Exact type that the register must have.
/// \return A matcher for a register whose LLT equals \p Ty.
inline CheckType m_SpecificType(LLT Ty) { return Ty; }

/// Matcher for a ternary generic opcode with three operand sub-patterns.
template <typename Src0Ty, typename Src1Ty, typename Src2Ty, unsigned Opcode>
struct TernaryOp_match {
  /// Sub-pattern for the first source operand.
  Src0Ty Src0;
  /// Sub-pattern for the second source operand.
  Src1Ty Src1;
  /// Sub-pattern for the third source operand.
  Src2Ty Src2;

  /// Construct a ternary matcher from three operand sub-patterns.
  ///
  /// \param Src0 Sub-pattern for the first source operand.
  /// \param Src1 Sub-pattern for the second source operand.
  /// \param Src2 Sub-pattern for the third source operand.
  TernaryOp_match(const Src0Ty &Src0, const Src1Ty &Src1, const Src2Ty &Src2)
      : Src0(Src0), Src1(Src1), Src2(Src2) {}
  /// Match \p Op if it is Opcode with three sources matching the sub-patterns.
  ///
  /// \param MRI Register info used to inspect definitions.
  /// \param Op Value or instruction to match.
  /// \return True if the instruction matches.
  template <typename OpTy>
  bool match(const MachineRegisterInfo &MRI, OpTy &&Op) {
    MachineInstr *TmpMI;
    if (mi_match(Op, MRI, m_MInstr(TmpMI))) {
      if (TmpMI->getOpcode() == Opcode && TmpMI->getNumOperands() == 4) {
        return (Src0.match(MRI, TmpMI->getOperand(1).getReg()) &&
                Src1.match(MRI, TmpMI->getOperand(2).getReg()) &&
                Src2.match(MRI, TmpMI->getOperand(3).getReg()));
      }
    }
    return false;
  }
};
/// Matches a G_INSERT_VECTOR_ELT with the given operand sub-patterns.
///
/// \param Src0 Sub-pattern for the destination vector.
/// \param Src1 Sub-pattern for the inserted element.
/// \param Src2 Sub-pattern for the insert index.
/// \return A matcher for a G_INSERT_VECTOR_ELT with the given operand sub-patterns.
template <typename Src0Ty, typename Src1Ty, typename Src2Ty>
inline TernaryOp_match<Src0Ty, Src1Ty, Src2Ty,
                       TargetOpcode::G_INSERT_VECTOR_ELT>
m_GInsertVecElt(const Src0Ty &Src0, const Src1Ty &Src1, const Src2Ty &Src2) {
  return TernaryOp_match<Src0Ty, Src1Ty, Src2Ty,
                         TargetOpcode::G_INSERT_VECTOR_ELT>(Src0, Src1, Src2);
}

/// Matches a G_SELECT with the given condition and operand sub-patterns.
///
/// \param Src0 Sub-pattern for the condition operand.
/// \param Src1 Sub-pattern for the true operand.
/// \param Src2 Sub-pattern for the false operand.
/// \return A matcher for a G_SELECT with the given condition and operand sub-patterns.
template <typename Src0Ty, typename Src1Ty, typename Src2Ty>
inline TernaryOp_match<Src0Ty, Src1Ty, Src2Ty, TargetOpcode::G_SELECT>
m_GISelect(const Src0Ty &Src0, const Src1Ty &Src1, const Src2Ty &Src2) {
  return TernaryOp_match<Src0Ty, Src1Ty, Src2Ty, TargetOpcode::G_SELECT>(
      Src0, Src1, Src2);
}

/// Matches a register negated by a G_SUB from zero.
///
/// G_SUB 0, %negated_reg
///
/// \param Src Sub-pattern for the value being negated.
/// \return A matcher for negation as G_SUB from zero.
template <typename SrcTy>
inline BinaryOp_match<SpecificConstantMatch, SrcTy, TargetOpcode::G_SUB>
m_Neg(const SrcTy &&Src) {
  return m_GSub(m_ZeroInt(), Src);
}

/// Matches a register bitwise-not'ed by a G_XOR against all-ones.
///
/// G_XOR %not_reg, -1
///
/// \param Src Sub-pattern for the value being complemented.
/// \return A matcher for bitwise not as G_XOR against all-ones.
template <typename SrcTy>
inline BinaryOp_match<SrcTy, SpecificConstantMatch, TargetOpcode::G_XOR, true>
m_Not(const SrcTy &&Src) {
  return m_GXor(Src, m_AllOnesInt());
}

} // namespace MIPatternMatch
} // namespace llvm

#endif
