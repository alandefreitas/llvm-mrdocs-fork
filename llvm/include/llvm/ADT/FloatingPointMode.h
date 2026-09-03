//===- llvm/Support/FloatingPointMode.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Utilities for dealing with flags related to floating point properties and
/// mode controls.
///
//===----------------------------------------------------------------------===/

#ifndef LLVM_ADT_FLOATINGPOINTMODE_H
#define LLVM_ADT_FLOATINGPOINTMODE_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Rounding mode.
///
/// Enumerates supported rounding modes, as well as some special values. The set
/// of the modes must agree with IEEE-754, 4.3.1 and 4.3.2. The constants
/// assigned to the IEEE rounding modes must agree with the values used by
/// FLT_ROUNDS (C11, 5.2.4.2.2p8).
///
/// This value is packed into bitfield in some cases, including \c FPOptions, so
/// the rounding mode values and the special value \c Dynamic must fit into the
/// the bit field (now - 3 bits). The value \c Invalid is used only in values
/// returned by intrinsics to indicate errors, it should never be stored as
/// rounding mode value, so it does not need to fit the bit fields.
///
enum class RoundingMode : int8_t {
  // Rounding mode defined in IEEE-754.
  TowardZero        = 0,    ///< roundTowardZero.
  NearestTiesToEven = 1,    ///< roundTiesToEven.
  TowardPositive    = 2,    ///< roundTowardPositive.
  TowardNegative    = 3,    ///< roundTowardNegative.
  NearestTiesToAway = 4,    ///< roundTiesToAway.

  // Special values.
  Dynamic = 7,    ///< Denotes mode unknown at compile time.
  Invalid = -1    ///< Denotes invalid value.
};

/// Returns text representation of the given rounding mode.
/// @param RM Rounding mode to spell.
/// @return Lowercase spelling of \p RM, or "invalid" if unrecognized.
inline StringRef spell(RoundingMode RM) {
  switch (RM) {
  case RoundingMode::TowardZero: return "towardzero";
  case RoundingMode::NearestTiesToEven: return "tonearest";
  case RoundingMode::TowardPositive: return "upward";
  case RoundingMode::TowardNegative: return "downward";
  case RoundingMode::NearestTiesToAway: return "tonearestaway";
  case RoundingMode::Dynamic: return "dynamic";
  default: return "invalid";
  }
}

/// Write the spelled name of \p RM to \p OS.
/// @param OS Output stream.
/// @param RM Rounding mode to print.
/// @return Reference to \p OS.
inline raw_ostream &operator << (raw_ostream &OS, RoundingMode RM) {
  OS << spell(RM);
  return OS;
}

/// Represent subnormal handling kind for floating point instruction inputs and
/// outputs.
struct DenormalMode {
  /// Represent handled modes for denormal (aka subnormal) modes in the floating
  /// point environment.
  enum DenormalModeKind : int8_t {
    /// Sentinel for an unset or erroneous denormal mode.
    Invalid = -1,

    /// IEEE-754 denormal numbers preserved.
    IEEE = 0,

    /// The sign of a flushed-to-zero number is preserved in the sign of 0
    PreserveSign = 1,

    /// Denormals are flushed to positive zero.
    PositiveZero = 2,

    /// Denormals have unknown treatment.
    Dynamic = 3
  };

  /// Denormal flushing mode for floating point instruction results in the
  /// default floating point environment.
  DenormalModeKind Output = DenormalModeKind::Invalid;

  /// Denormal treatment kind for floating point instruction inputs.
  ///
  /// Applies in the default floating-point environment. If this is not
  /// DenormalModeKind::IEEE, floating-point instructions implicitly treat the
  /// input value as 0.
  DenormalModeKind Input = DenormalModeKind::Invalid;

  /// Construct with invalid input and output modes.
  constexpr DenormalMode() = default;
  /// Copy-construct a denormal mode.
  /// @param Mode Mode to copy.
  constexpr DenormalMode(const DenormalMode &Mode) = default;
  /// Construct from separate output and input denormal mode kinds.
  /// @param Out Flushing mode for instruction results.
  /// @param In Treatment mode for instruction inputs.
  constexpr DenormalMode(DenormalModeKind Out, DenormalModeKind In) :
    Output(Out), Input(In) {}

  /// Copy-assign a denormal mode.
  /// @param Mode Mode to assign from.
  /// @return Reference to this mode.
  DenormalMode &operator=(const DenormalMode &Mode) = default;

  /// Return a mode with both input and output set to Invalid.
  /// @return Mode with both input and output set to Invalid.
  static constexpr DenormalMode getInvalid() {
    return DenormalMode(DenormalModeKind::Invalid, DenormalModeKind::Invalid);
  }

  /// Return the assumed default mode for a function without denormal-fp-math.
  /// @return Default denormal mode for a function without denormal-fp-math.
  static constexpr DenormalMode getDefault() {
    return getIEEE();
  }

  /// Return IEEE input and output denormal handling.
  /// @return Mode with IEEE input and output handling.
  static constexpr DenormalMode getIEEE() {
    return DenormalMode(DenormalModeKind::IEEE, DenormalModeKind::IEEE);
  }

  /// Return PreserveSign input and output denormal handling.
  /// @return Mode with PreserveSign input and output handling.
  static constexpr DenormalMode getPreserveSign() {
    return DenormalMode(DenormalModeKind::PreserveSign,
                        DenormalModeKind::PreserveSign);
  }

  /// Return PositiveZero input and output denormal handling.
  /// @return Mode with PositiveZero input and output handling.
  static constexpr DenormalMode getPositiveZero() {
    return DenormalMode(DenormalModeKind::PositiveZero,
                        DenormalModeKind::PositiveZero);
  }

  /// Return Dynamic input and output denormal handling.
  /// @return Mode with Dynamic input and output handling.
  static constexpr DenormalMode getDynamic() {
    return DenormalMode(DenormalModeKind::Dynamic, DenormalModeKind::Dynamic);
  }

  /// Pack input and output modes into a compact integer encoding.
  /// @return Bit-packed representation of this mode.
  constexpr uint32_t toIntValue() const {
    assert(Input != Invalid && Output != Invalid);
    return (static_cast<uint32_t>(Input) << 2) | static_cast<uint32_t>(Output);
  }

  /// Reconstruct a mode from the encoding produced by \ref toIntValue.
  /// @param Data Packed integer encoding.
  /// @return Corresponding DenormalMode.
  static constexpr DenormalMode createFromIntValue(uint32_t Data) {
    uint32_t OutputMode = Data & 0x3;
    uint32_t InputMode = (Data >> 2) & 0x3;

    return {static_cast<DenormalModeKind>(OutputMode),
            static_cast<DenormalModeKind>(InputMode)};
  }

  /// Return true when input and output modes match \p Other.
  /// @param Other Mode to compare against.
  /// @return True if the modes are equal.
  constexpr bool operator==(DenormalMode Other) const {
    return Output == Other.Output && Input == Other.Input;
  }

  /// Return true when this mode differs from \p Other.
  /// @param Other Mode to compare against.
  /// @return True if the modes differ.
  constexpr bool operator!=(DenormalMode Other) const {
    return !(*this == Other);
  }

  /// Return true when input and output use the same denormal mode kind.
  /// @return True if input and output use the same denormal mode kind.
  constexpr bool isSimple() const { return Input == Output; }

  /// Return true when neither input nor output mode is Invalid.
  /// @return True if neither input nor output mode is Invalid.
  constexpr bool isValid() const {
    return Output != DenormalModeKind::Invalid &&
           Input != DenormalModeKind::Invalid;
  }

  /// Return true if input denormals must be implicitly treated as 0.
  /// @return True if input denormals must be implicitly treated as 0.
  constexpr bool inputsAreZero() const {
    return Input == DenormalModeKind::PreserveSign ||
           Input == DenormalModeKind::PositiveZero;
  }

  /// Return true if input denormals may be implicitly treated as 0.
  /// @return True if input denormals may be implicitly treated as 0.
  constexpr bool inputsMayBeZero() const {
    return inputsAreZero() || Input == DenormalMode::Dynamic;
  }

  /// Return true if output denormals should be flushed to 0.
  /// @return True if output denormals should be flushed to 0.
  constexpr bool outputsAreZero() const {
    return Output == DenormalModeKind::PreserveSign ||
           Output == DenormalModeKind::PositiveZero;
  }

  /// Return true if output denormals may be implicitly treated as 0.
  /// @return True if output denormals may be implicitly treated as 0.
  constexpr bool outputsMayBeZero() const {
    return outputsAreZero() || Output == DenormalMode::Dynamic;
  }

  /// Return true if input denormals could be flushed to +0.
  /// @return True if input denormals could be flushed to +0.
  constexpr bool inputsMayBePositiveZero() const {
    return Input == DenormalMode::PositiveZero ||
           Input == DenormalMode::Dynamic;
  }

  /// Return true if output denormals could be flushed to +0.
  /// @return True if output denormals could be flushed to +0.
  constexpr bool outputsMayBePositiveZero() const {
    return Output == DenormalMode::PositiveZero ||
           Output == DenormalMode::Dynamic;
  }

  /// Get the effective denormal mode if the mode if this caller calls into a
  /// function with \p Callee. This promotes dynamic modes to the mode of the
  /// caller.
  /// @param Callee Callee denormal mode whose Dynamic fields are filled in.
  /// @return Mode with Dynamic fields replaced by this caller's modes.
  constexpr DenormalMode mergeCalleeMode(DenormalMode Callee) const {
    DenormalMode MergedMode = Callee;
    if (Callee.Input == DenormalMode::Dynamic)
      MergedMode.Input = Input;
    if (Callee.Output == DenormalMode::Dynamic)
      MergedMode.Output = Output;
    return MergedMode;
  }

  /// Write this mode to \p OS as attribute-style names.
  /// @param OS Output stream.
  /// @param Legacy Prefer hyphenated attribute names when true.
  /// @param OmitIfSame Skip printing the input mode when it matches output.
  inline void print(raw_ostream &OS, bool Legacy = true,
                    bool OmitIfSame = false) const;

  /// Return the printed form of this mode as a string.
  /// @return Printed representation of this mode as a string.
  inline std::string str() const {
    std::string storage;
    raw_string_ostream OS(storage);
    print(OS);
    return storage;
  }
};

/// Write \p Mode to \p OS via DenormalMode::print.
/// @param OS Output stream.
/// @param Mode Denormal mode to print.
/// @return Reference to \p OS.
inline raw_ostream& operator<<(raw_ostream &OS, DenormalMode Mode) {
  Mode.print(OS);
  return OS;
}

/// Parse the expected names from the denormal-fp-math attribute.
/// @param Str Attribute component name to parse.
/// @return Matching denormal mode kind, or Invalid if unrecognized.
inline DenormalMode::DenormalModeKind
parseDenormalFPAttributeComponent(StringRef Str) {
  // Assume ieee on unspecified attribute.
  return StringSwitch<DenormalMode::DenormalModeKind>(Str)
      .Cases({"", "ieee"}, DenormalMode::IEEE)
      .Cases({"preservesign", "preserve-sign"}, DenormalMode::PreserveSign)
      .Cases({"positivezero", "positive-zero"}, DenormalMode::PositiveZero)
      .Case("dynamic", DenormalMode::Dynamic)
      .Default(DenormalMode::Invalid);
}

/// Return the name used for the denormal handling mode used by the
/// expected names from the denormal-fp-math attribute.
/// @param Mode Denormal mode kind to name.
/// @param LegacyName Prefer hyphenated attribute names when true.
/// @return Attribute-style name for \p Mode, or empty if Invalid.
constexpr StringRef denormalModeKindName(DenormalMode::DenormalModeKind Mode,
                                         bool LegacyName = true) {
  switch (Mode) {
  case DenormalMode::IEEE:
    return "ieee";
  case DenormalMode::PreserveSign:
    return LegacyName ? "preserve-sign" : "preservesign";
  case DenormalMode::PositiveZero:
    return LegacyName ? "positive-zero" : "positivezero";
  case DenormalMode::Dynamic:
    return "dynamic";
  default:
    return "";
  }
}

/// Returns the denormal mode to use for inputs and outputs.
/// @param Str Full denormal-fp-math attribute value to parse.
/// @return Parsed output and input denormal modes.
inline DenormalMode parseDenormalFPAttribute(StringRef Str) {
  StringRef OutputStr, InputStr;
  std::tie(OutputStr, InputStr) = Str.split(',');

  DenormalMode Mode;
  Mode.Output = parseDenormalFPAttributeComponent(OutputStr);

  // Maintain compatibility with old form of the attribute which only specified
  // one component.
  Mode.Input = InputStr.empty() ? Mode.Output  :
               parseDenormalFPAttributeComponent(InputStr);

  return Mode;
}

void DenormalMode::print(raw_ostream &OS, bool Legacy, bool OmitIfSame) const {
  OS << denormalModeKindName(Output, Legacy);
  if (!OmitIfSame || Input != Output) {
    OS << (Legacy ? ',' : '|');
    OS << denormalModeKindName(Input, Legacy);
  }
}

/// Represents the full denormal controls for a function, including the default
/// mode and the f32 specific override.
struct DenormalFPEnv {
private:
  static constexpr unsigned BitsPerEntry = 2;
  static constexpr unsigned BitsPerMode = 4;
  static constexpr unsigned ModeMask = (1 << BitsPerMode) - 1;

public:
  /// Default denormal mode for types other than f32.
  DenormalMode DefaultMode;
  /// Denormal mode override applied to f32 operations.
  DenormalMode F32Mode;

  /// Construct from a base mode and optional f32-specific override.
  /// Invalid fields in \p FloatMode fall back to the corresponding base field.
  /// @param BaseMode Default denormal mode.
  /// @param FloatMode Optional f32 override; Invalid fields inherit from base.
  constexpr DenormalFPEnv(DenormalMode BaseMode,
                          DenormalMode FloatMode = DenormalMode::getInvalid())
      : DefaultMode(BaseMode),
        F32Mode(FloatMode.Output == DenormalMode::Invalid ? BaseMode.Output
                                                          : FloatMode.Output,
                FloatMode.Input == DenormalMode::Invalid ? BaseMode.Input
                                                         : FloatMode.Input) {}

  /// Return IEEE default and f32 denormal modes.
  /// @return Environment with IEEE modes for default and f32.
  static constexpr DenormalFPEnv getDefault() {
    return DenormalFPEnv(DenormalMode::getIEEE(), DenormalMode::getIEEE());
  }

  /// Pack default and f32 modes into a compact integer encoding.
  /// @return Bit-packed representation of this environment.
  constexpr uint32_t toIntValue() const {
    assert(DefaultMode.isValid() && F32Mode.isValid());
    uint32_t Data =
        DefaultMode.toIntValue() | (F32Mode.toIntValue() << BitsPerMode);

    assert(isUInt<8>(Data));
    return Data;
  }

  /// Reconstruct an environment from the encoding produced by \ref toIntValue.
  /// @param Data Packed integer encoding.
  /// @return Corresponding DenormalFPEnv.
  static constexpr DenormalFPEnv createFromIntValue(uint32_t Data) {
    return {DenormalMode::createFromIntValue(Data),
            DenormalMode::createFromIntValue(Data >> BitsPerMode)};
  }

  /// Return true when default and f32 modes match \p Other.
  /// @param Other Environment to compare against.
  /// @return True if the environments are equal.
  constexpr bool operator==(DenormalFPEnv Other) const {
    return DefaultMode == Other.DefaultMode && F32Mode == Other.F32Mode;
  }

  /// Return true when this environment differs from \p Other.
  /// @param Other Environment to compare against.
  /// @return True if the environments differ.
  constexpr bool operator!=(DenormalFPEnv Other) const {
    return !(*this == Other);
  }

  /// Write this environment to \p OS.
  /// @param OS Output stream.
  /// @param OmitIfSame Skip printing f32 when it matches the default mode.
  LLVM_ABI void print(raw_ostream &OS, bool OmitIfSame = true) const;

  /// Merge callee dynamic modes with this caller's concrete modes.
  /// @param Callee Callee denormal environment.
  /// @return Environment with Dynamic fields filled from this caller.
  DenormalFPEnv mergeCalleeMode(DenormalFPEnv Callee) const {
    return DenormalFPEnv{DefaultMode.mergeCalleeMode(Callee.DefaultMode),
                         F32Mode.mergeCalleeMode(Callee.F32Mode)};
  }
};

/// Write \p FPEnv to \p OS via DenormalFPEnv::print.
/// @param OS Output stream.
/// @param FPEnv Denormal environment to print.
/// @return Reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, DenormalFPEnv FPEnv) {
  FPEnv.print(OS);
  return OS;
}

/// Floating-point class tests, supported by 'is_fpclass' intrinsic. Actual
/// test may be an OR combination of basic tests.
enum FPClassTest : unsigned {
  /// Match no floating-point classes.
  fcNone = 0,

  /// Signaling NaN.
  fcSNan = 0x0001,
  /// Quiet NaN.
  fcQNan = 0x0002,
  /// Negative infinity.
  fcNegInf = 0x0004,
  /// Negative normal number.
  fcNegNormal = 0x0008,
  /// Negative subnormal number.
  fcNegSubnormal = 0x0010,
  /// Negative zero.
  fcNegZero = 0x0020,
  /// Positive zero.
  fcPosZero = 0x0040,
  /// Positive subnormal number.
  fcPosSubnormal = 0x0080,
  /// Positive normal number.
  fcPosNormal = 0x0100,
  /// Positive infinity.
  fcPosInf = 0x0200,

  /// Any NaN (signaling or quiet).
  fcNan = fcSNan | fcQNan,
  /// Positive or negative infinity.
  fcInf = fcPosInf | fcNegInf,
  /// Positive or negative normal number.
  fcNormal = fcPosNormal | fcNegNormal,
  /// Positive or negative subnormal number.
  fcSubnormal = fcPosSubnormal | fcNegSubnormal,
  /// Positive or negative zero.
  fcZero = fcPosZero | fcNegZero,
  /// Any finite positive value (normal, subnormal, or zero).
  fcPosFinite = fcPosNormal | fcPosSubnormal | fcPosZero,
  /// Any finite negative value (normal, subnormal, or zero).
  fcNegFinite = fcNegNormal | fcNegSubnormal | fcNegZero,
  /// Any finite value of either sign.
  fcFinite = fcPosFinite | fcNegFinite,
  /// Any positive value including +\infty.
  fcPositive = fcPosFinite | fcPosInf,
  /// Any negative value including -\infty.
  fcNegative = fcNegFinite | fcNegInf,

  /// Union of all class test flags.
  fcAllFlags = fcNan | fcInf | fcFinite,
};

/// Mark \c FPClassTest as a bitmask enumeration for bitwise operators.
template <> struct is_bitmask_enum<FPClassTest> : std::true_type {};
/// Largest individual bit value used by \c FPClassTest bitmask operations.
template <> struct largest_bitmask_enum_bit<FPClassTest> {
  /// Underlying integer encoding of the largest individual \c FPClassTest bit.
  static constexpr std::underlying_type_t<FPClassTest> value = fcPosInf;
};

/// Return the test mask which returns true if the value's sign bit is flipped.
/// @param Mask Floating-point class mask to negate.
/// @return Mask of classes obtained by flipping the sign of values matching \p Mask.
LLVM_ABI FPClassTest fneg(FPClassTest Mask);

/// Return the test mask which returns true after fabs is applied to the value.
/// @param Mask Floating-point class mask to transform.
/// @return Mask of classes that remain after applying fabs to values matching \p Mask.
LLVM_ABI FPClassTest inverse_fabs(FPClassTest Mask);

/// Return the test mask which returns true if the value could have the same set
/// of classes, but with a different sign.
/// @param Mask Floating-point class mask to broaden by sign.
/// @return Mask of classes that could match \p Mask with either sign.
LLVM_ABI FPClassTest unknown_sign(FPClassTest Mask);

/// Write a human readable form of \p Mask to \p OS.
/// @param OS Output stream.
/// @param Mask Floating-point class mask to print.
/// @return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, FPClassTest Mask);

/// Returns true if all values in \p LHS must be less than or equal to those in
/// \p RHS. That is, the comparison `fcmp ogt LHS, RHS` will always return
/// false.
///
/// If \p OrderedZeroSign is true, -0 will be treated as ordered less than +0,
/// unlike fcmp.
/// @param LHS Classes on the left-hand side of the comparison.
/// @param RHS Classes on the right-hand side of the comparison.
/// @param OrderedZeroSign Treat -0 as ordered less than +0 when true.
/// @return True if every value in \p LHS must be less than or equal to every value in \p RHS.
LLVM_ABI bool cannotOrderStrictlyGreater(FPClassTest LHS, FPClassTest RHS,
                                         bool OrderedZeroSign = false);

/// Returns true if all values in \p LHS must be less than those in \p RHS. That
/// is, the comparison `fcmp oge LHS, RHS` will always return false.
///
/// If \p OrderedZeroSign is true, -0 will be treated as ordered less than +0,
/// unlike fcmp.
/// @param LHS Classes on the left-hand side of the comparison.
/// @param RHS Classes on the right-hand side of the comparison.
/// @param OrderedZeroSign Treat -0 as ordered less than +0 when true.
/// @return True if every value in \p LHS must be less than every value in \p RHS.
LLVM_ABI bool cannotOrderStrictlyGreaterEq(FPClassTest LHS, FPClassTest RHS,
                                           bool OrderedZeroSign = false);

/// Returns true if all values in \p LHS must be greater than or equal to those
/// in \p RHS. That is, the comparison `fcmp olt LHS, RHS` will always return
/// false.
///
/// If \p OrderedZeroSign is true, -0 will be treated as ordered less than +0,
/// unlike fcmp.
/// @param LHS Classes on the left-hand side of the comparison.
/// @param RHS Classes on the right-hand side of the comparison.
/// @param OrderedZeroSign Treat -0 as ordered less than +0 when true.
/// @return True if every value in \p LHS must be greater than or equal to every value in \p RHS.
LLVM_ABI bool cannotOrderStrictlyLess(FPClassTest LHS, FPClassTest RHS,
                                      bool OrderedZeroSign = false);

/// Returns true if all values in \p LHS must be greater than to those in \p
/// RHS. That is, the comparison `fcmp ole LHS, RHS` will always return false.
///
/// If \p OrderedZeroSign is true, -0 will be treated as ordered less than +0,
/// unlike fcmp.
/// @param LHS Classes on the left-hand side of the comparison.
/// @param RHS Classes on the right-hand side of the comparison.
/// @param OrderedZeroSign Treat -0 as ordered less than +0 when true.
/// @return True if every value in \p LHS must be greater than every value in \p RHS.
LLVM_ABI bool cannotOrderStrictlyLessEq(FPClassTest LHS, FPClassTest RHS,
                                        bool OrderedZeroSign = false);

/// Return all FP classes strictly less than every value in \p Mask.
///
/// That is, return all classes for which the comparison `fcmp ole LHS, RHS`
/// will always return false. Ignores nan and treats the sign of the zeroes as
/// ordered.
///
/// If \p OrderedZeroSign is true, -0 will be treated as ordered less than +0,
/// unlike fcmp.
/// @param Mask Classes whose values bound the comparison from above.
/// @param OrderedZeroSign Treat -0 as ordered less than +0 when true.
/// @return All FP classes strictly less than every value in \p Mask.
LLVM_ABI FPClassTest orderedStrictlyLess(FPClassTest Mask,
                                         bool OrderedZeroSign = false);

/// Return all FP classes strictly greater than every value in \p Mask.
///
/// That is, return all classes for which the comparison `fcmp ole LHS, RHS`
/// will always return false. Ignores nan and treats the sign of the zeroes as
/// ordered.
///
/// If \p OrderedZeroSign is true, -0 will be treated as ordered less than +0,
/// unlike fcmp.
/// @param Mask Classes whose values bound the comparison from below.
/// @param OrderedZeroSign Treat -0 as ordered less than +0 when true.
/// @return All FP classes strictly greater than every value in \p Mask.
LLVM_ABI FPClassTest orderedStrictlyGreater(FPClassTest Mask,
                                            bool OrderedZeroSign = false);

} // namespace llvm

#endif // LLVM_ADT_FLOATINGPOINTMODE_H
