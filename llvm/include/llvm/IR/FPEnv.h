//===- FPEnv.h ---- FP Environment ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// @file
/// This file contains the declarations of entities that describe floating
/// point environment and related functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_FPENV_H
#define LLVM_IR_FPENV_H

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/IR/FMF.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {
class StringRef;

namespace Intrinsic {
/// Opaque intrinsic identifier used by constrained FP helpers in this header.
typedef unsigned ID;
}

class Instruction;

/// Floating-point environment utilities for constrained floating-point
/// operations.
namespace fp {

/// Exception behavior used for floating point operations.
///
/// Each of these values correspond to some metadata argument value of a
/// constrained floating point intrinsic. See the LLVM Language Reference Manual
/// for details.
enum ExceptionBehavior : uint8_t {
  ebIgnore,  ///< This corresponds to "fpexcept.ignore".
  ebMayTrap, ///< This corresponds to "fpexcept.maytrap".
  ebStrict   ///< This corresponds to "fpexcept.strict".
};

}

/// Returns a valid RoundingMode enumerator when given a string
/// that is valid as input in constrained intrinsic rounding mode
/// metadata.
/// \param RoundingArg Rounding-mode metadata string to parse.
/// \return The parsed rounding mode, or \c std::nullopt if \p RoundingArg
/// is not a valid constrained-intrinsic rounding-mode string.
LLVM_ABI std::optional<RoundingMode>
    convertStrToRoundingMode(StringRef RoundingArg);

/// For any RoundingMode enumerator, returns a string valid as input in
/// constrained intrinsic rounding mode metadata.
/// \param UseRounding Rounding mode to convert to a metadata string.
/// \return The metadata string for \p UseRounding, or \c std::nullopt if
/// there is no corresponding constrained-intrinsic rounding-mode string.
LLVM_ABI std::optional<StringRef>
    convertRoundingModeToStr(RoundingMode UseRounding);

/// Returns a valid ExceptionBehavior enumerator when given a string
/// valid as input in constrained intrinsic exception behavior metadata.
/// \param ExceptionArg Exception-behavior metadata string to parse.
/// \return The parsed exception behavior, or \c std::nullopt if
/// \p ExceptionArg is not a valid constrained-intrinsic exception-behavior
/// string.
LLVM_ABI std::optional<fp::ExceptionBehavior>
    convertStrToExceptionBehavior(StringRef ExceptionArg);

/// For any ExceptionBehavior enumerator, returns a string valid as
/// input in constrained intrinsic exception behavior metadata.
/// \param UseExcept Exception behavior to convert to a metadata string.
/// \return The metadata string for \p UseExcept, or \c std::nullopt if
/// there is no corresponding constrained-intrinsic exception-behavior string.
LLVM_ABI std::optional<StringRef>
    convertExceptionBehaviorToStr(fp::ExceptionBehavior UseExcept);

/// Returns true if the exception handling behavior and rounding mode
/// match what is used in the default floating point environment.
/// \param EB Exception behavior to compare against the default.
/// \param RM Rounding mode to compare against the default.
/// \return True if \p EB and \p RM match the default floating-point
/// environment.
inline bool isDefaultFPEnvironment(fp::ExceptionBehavior EB, RoundingMode RM) {
  return EB == fp::ebIgnore && RM == RoundingMode::NearestTiesToEven;
}

/// Return the constrained intrinsic ID for an instruction under strictfp.
///
/// If the instruction is already a constrained intrinsic or does not have a
/// constrained intrinsic counterpart, the function returns zero.
/// \param Instr Instruction to map to a constrained intrinsic.
/// \return The constrained intrinsic ID for \p Instr, or zero if it is
/// already constrained or has no constrained counterpart.
LLVM_ABI Intrinsic::ID getConstrainedIntrinsicID(const Instruction &Instr);

/// Returns true if the rounding mode RM may be QRM at compile time or
/// at run time.
/// \param RM Rounding mode under consideration.
/// \param QRM Candidate rounding mode to compare against \p RM.
/// \return True if \p RM may be \p QRM at compile time or at run time.
inline bool canRoundingModeBe(RoundingMode RM, RoundingMode QRM) {
  return RM == QRM || RM == RoundingMode::Dynamic;
}

/// Returns true if the possibility of a signaling NaN can be safely
/// ignored.
/// \param EB Exception behavior governing floating-point traps.
/// \param FMF Fast-math flags that may imply NaNs are absent.
/// \return True if signaling NaNs can be safely ignored under \p EB and
/// \p FMF.
inline bool canIgnoreSNaN(fp::ExceptionBehavior EB, FastMathFlags FMF) {
  return (EB == fp::ebIgnore || FMF.noNaNs());
}
}
#endif
