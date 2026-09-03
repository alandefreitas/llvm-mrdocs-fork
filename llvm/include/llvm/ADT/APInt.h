//===-- llvm/ADT/APInt.h - For Arbitrary Precision Integer -----*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a class to represent arbitrary precision
/// integral constant values and operations on them.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_APINT_H
#define LLVM_ADT_APINT_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/float128.h"
#include <cassert>
#include <climits>
#include <cstring>
#include <optional>
#include <utility>

namespace llvm {
/// Folding set node identifier. @seebelow
class FoldingSetNodeID;
class StringRef;
class hash_code;
class raw_ostream;
/// Memory alignment value. @seebelow
struct Align;
/// Dynamically sized arbitrary-precision integer. @seebelow
class DynamicAPInt;

template <typename T> class SmallVectorImpl;
template <typename T> class ArrayRef;
template <typename T, typename Enable> struct DenseMapInfo;

class APInt;

inline APInt operator-(APInt);

//===----------------------------------------------------------------------===//
//                              APInt Class
//===----------------------------------------------------------------------===//

/// Class for arbitrary precision integers.
///
/// APInt is a functional replacement for common case unsigned integer type like
/// "unsigned", "unsigned long" or "uint64_t", but also allows non-byte-width
/// integer sizes and large integer value types such as 3-bits, 15-bits, or more
/// than 64-bits of precision. APInt provides a variety of arithmetic operators
/// and methods to manipulate integer values of any bit-width. It supports both
/// the typical integer arithmetic and comparison operations as well as bitwise
/// manipulation.
///
/// The class has several invariants worth noting:
///   * All bit, byte, and word positions are zero-based.
///   * Once the bit width is set, it doesn't change except by the Truncate,
///     SignExtend, or ZeroExtend operations.
///   * All binary operators must be on APInt instances of the same bit width.
///     Attempting to use these operators on instances with different bit
///     widths will yield an assertion.
///   * The value is stored canonically as an unsigned value. For operations
///     where it makes a difference, there are both signed and unsigned variants
///     of the operation. For example, sdiv and udiv. However, because the bit
///     widths must be the same, operations such as Mul and Add produce the same
///     results regardless of whether the values are interpreted as signed or
///     not.
///   * In general, the class tries to follow the style of computation that LLVM
///     uses in its IR. This simplifies its use for LLVM.
///   * APInt supports zero-bit-width values, but operations that require bits
///     are not defined on it (e.g. you cannot ask for the sign of a zero-bit
///     integer).  This means that operations like zero extension and logical
///     shifts are defined, but sign extension and ashr is not.  Zero bit values
///     compare and hash equal to themselves, and countLeadingZeros returns 0.
///
class [[nodiscard]] APInt {
public:
  /// Native limb type used to store APInt words.
  using WordType = uint64_t;

  /// Byte size of a word.
  static constexpr unsigned APINT_WORD_SIZE = sizeof(WordType);

  /// Bits in a word.
  static constexpr unsigned APINT_BITS_PER_WORD = APINT_WORD_SIZE * CHAR_BIT;

  /// Rounding mode for converting floating-point values to APInt.
  enum class Rounding {
    /// Round toward negative infinity.
    DOWN,
    /// Round toward zero.
    TOWARD_ZERO,
    /// Round toward positive infinity.
    UP,
  };

  /// Maximum value representable in a single WordType limb.
  static constexpr WordType WORDTYPE_MAX = ~WordType(0);

  /// \name Constructors
  /// @{

  /// Create a new APInt of numBits width, initialized as val.
  ///
  /// If isSigned is true then val is treated as if it were a signed value
  /// (i.e. as an int64_t) and the appropriate sign extension to the bit width
  /// will be done. Otherwise, no sign extension occurs (high order bits beyond
  /// the range of val are zero filled).
  ///
  /// \param numBits the bit width of the constructed APInt
  /// \param val the initial value of the APInt
  /// \param isSigned how to treat signedness of val
  /// \param implicitTrunc allow implicit truncation of non-zero/sign bits of
  ///                      val beyond the range of numBits
  APInt(unsigned numBits, uint64_t val, bool isSigned = false,
        bool implicitTrunc = false)
      : BitWidth(numBits) {
    if (!implicitTrunc) {
      if (isSigned) {
        if (BitWidth == 0) {
          assert((val == 0 || val == uint64_t(-1)) &&
                 "Value must be 0 or -1 for signed 0-bit APInt");
        } else {
          assert(llvm::isIntN(BitWidth, val) &&
                 "Value is not an N-bit signed value");
        }
      } else {
        if (BitWidth == 0) {
          assert(val == 0 && "Value must be zero for unsigned 0-bit APInt");
        } else {
          assert(llvm::isUIntN(BitWidth, val) &&
                 "Value is not an N-bit unsigned value");
        }
      }
    }
    if (isSingleWord()) {
      U.VAL = val;
      if (implicitTrunc || isSigned)
        clearUnusedBits();
    } else {
      initSlowCase(val, isSigned);
    }
  }

  /// Construct an APInt of numBits width, initialized as bigVal[].
  ///
  /// Note that bigVal.size() can be smaller or larger than the corresponding
  /// bit width but any extraneous bits will be dropped.
  ///
  /// \param numBits the bit width of the constructed APInt
  /// \param bigVal a sequence of words to form the initial value of the APInt
  LLVM_ABI APInt(unsigned numBits, ArrayRef<uint64_t> bigVal);

  /// Deleted constructor that previously took a raw word pointer.
  ///
  /// Was equivalent to APInt(numBits, ArrayRef<uint64_t>(bigVal, numWords))
  /// historically, but is now deleted because this constructor is prone to
  /// ambiguity with the APInt(unsigned, uint64_t, bool) constructor.
  ///
  /// \param numBits Bit width of the intended value
  /// \param numWords Number of words pointed to by \p bigVal
  /// \param bigVal Pointer to the word array (unused; constructor is deleted)
  LLVM_ABI APInt(unsigned numBits, unsigned numWords,
                 const uint64_t bigVal[]) = delete;

  /// Construct an APInt from a string representation.
  ///
  /// This constructor interprets the string \p str in the given radix. The
  /// interpretation stops when the first character that is not suitable for the
  /// radix is encountered, or the end of the string. Acceptable radix values
  /// are 2, 8, 10, 16, and 36. It is an error for the value implied by the
  /// string to require more bits than numBits.
  ///
  /// \param numBits the bit width of the constructed APInt
  /// \param str the string to be interpreted
  /// \param radix the radix to use for the conversion
  LLVM_ABI APInt(unsigned numBits, StringRef str, uint8_t radix);

  /// Default constructor that creates an APInt with a 1-bit zero value.
  explicit APInt() { U.VAL = 0; }

  /// Copy constructor.
  ///
  /// \param that Source APInt to copy
  APInt(const APInt &that) : BitWidth(that.BitWidth) {
    if (isSingleWord())
      U.VAL = that.U.VAL;
    else
      initSlowCase(that);
  }

  /// Move constructor.
  ///
  /// \param that Source APInt to move from
  APInt(APInt &&that) : BitWidth(that.BitWidth) {
    memcpy(&U, &that.U, sizeof(U));
    that.BitWidth = 0;
  }

  /// Destructor.
  ~APInt() {
    if (needsCleanup())
      delete[] U.pVal;
  }

  /// @}
  /// \name Value Generators
  /// @{

  /// Get the '0' value for the specified bit-width.
  ///
  /// \param numBits Bit width of the result
  /// \returns An APInt of \p numBits with value 0.
  static APInt getZero(unsigned numBits) { return APInt(numBits, 0); }

  /// Return an APInt zero bits wide.
  ///
  /// \returns A zero-width APInt.
  static APInt getZeroWidth() { return getZero(0); }

  /// Gets maximum unsigned value of APInt for specific bit width.
  ///
  /// \param numBits Bit width of the result
  /// \returns The maximum unsigned APInt of \p numBits.
  static APInt getMaxValue(unsigned numBits) { return getAllOnes(numBits); }

  /// Gets maximum signed value of APInt for a specific bit width.
  ///
  /// \param numBits Bit width of the result
  /// \returns The maximum signed APInt of \p numBits.
  static APInt getSignedMaxValue(unsigned numBits) {
    APInt API = getAllOnes(numBits);
    API.clearBit(numBits - 1);
    return API;
  }

  /// Gets minimum unsigned value of APInt for a specific bit width.
  ///
  /// \param numBits Bit width of the result
  /// \returns The minimum unsigned APInt of \p numBits (zero).
  static APInt getMinValue(unsigned numBits) { return APInt(numBits, 0); }

  /// Gets minimum signed value of APInt for a specific bit width.
  ///
  /// \param numBits Bit width of the result
  /// \returns The minimum signed APInt of \p numBits.
  static APInt getSignedMinValue(unsigned numBits) {
    APInt API(numBits, 0);
    API.setBit(numBits - 1);
    return API;
  }

  /// Get the SignMask for a specific bit width.
  ///
  /// This is just a wrapper function of getSignedMinValue(), and it helps code
  /// readability when we want to get a SignMask.
  ///
  /// \param BitWidth Bit width of the result
  /// \returns An APInt with only the sign bit set.
  static APInt getSignMask(unsigned BitWidth) {
    return getSignedMinValue(BitWidth);
  }

  /// Return an APInt of a specified width with all bits set.
  ///
  /// \param numBits Bit width of the result
  /// \returns An APInt of \p numBits with all bits set.
  static APInt getAllOnes(unsigned numBits) {
    return APInt(numBits, WORDTYPE_MAX, true);
  }

  /// Return an APInt with exactly one bit set in the result.
  ///
  /// \param numBits Bit width of the result
  /// \param BitNo Zero-based index of the bit to set
  /// \returns An APInt with only bit \p BitNo set.
  static APInt getOneBitSet(unsigned numBits, unsigned BitNo) {
    APInt Res(numBits, 0);
    Res.setBit(BitNo);
    return Res;
  }

  /// Get a value with a block of bits set.
  ///
  /// Constructs an APInt value that has a contiguous range of bits set. The
  /// bits from loBit (inclusive) to hiBit (exclusive) will be set. All other
  /// bits will be zero. For example, with parameters(32, 0, 16) you would get
  /// 0x0000FFFF. Please call getBitsSetWithWrap if \p loBit may be greater than
  /// \p hiBit.
  ///
  /// \param numBits the intended bit width of the result
  /// \param loBit the index of the lowest bit set.
  /// \param hiBit the index of the highest bit set.
  ///
  /// \returns An APInt value with the requested bits set.
  static APInt getBitsSet(unsigned numBits, unsigned loBit, unsigned hiBit) {
    APInt Res(numBits, 0);
    Res.setBits(loBit, hiBit);
    return Res;
  }

  /// Construct an APInt with a possibly wrapping contiguous range of bits set.
  ///
  /// If \p hiBit is bigger than \p loBit, this is the same as getBitsSet.
  /// If \p hiBit is not bigger than \p loBit, the set bits "wrap". For example,
  /// with parameters (32, 28, 4), you would get 0xF000000F.
  /// If \p hiBit is equal to \p loBit, you would get a result with all bits
  /// set.
  ///
  /// \param numBits Bit width of the result
  /// \param loBit First bit of the range (inclusive)
  /// \param hiBit End of the range (exclusive), or wrap point
  /// \returns An APInt with the requested (possibly wrapping) bits set.
  static APInt getBitsSetWithWrap(unsigned numBits, unsigned loBit,
                                  unsigned hiBit) {
    APInt Res(numBits, 0);
    Res.setBitsWithWrap(loBit, hiBit);
    return Res;
  }

  /// Construct an APInt with bits set from \p loBit to the top of the value.
  ///
  /// The bits from loBit (inclusive) to numBits (exclusive) will be set. All
  /// other bits will be zero. For example, with parameters(32, 12) you would
  /// get 0xFFFFF000.
  ///
  /// \param numBits the intended bit width of the result
  /// \param loBit the index of the lowest bit to set.
  ///
  /// \returns An APInt value with the requested bits set.
  static APInt getBitsSetFrom(unsigned numBits, unsigned loBit) {
    APInt Res(numBits, 0);
    Res.setBitsFrom(loBit);
    return Res;
  }

  /// Constructs an APInt value that has the top hiBitsSet bits set.
  ///
  /// \param numBits the bitwidth of the result
  /// \param hiBitsSet the number of high-order bits set in the result.
  /// \returns An APInt with the top \p hiBitsSet bits set.
  static APInt getHighBitsSet(unsigned numBits, unsigned hiBitsSet) {
    APInt Res(numBits, 0);
    Res.setHighBits(hiBitsSet);
    return Res;
  }

  /// Constructs an APInt value that has the bottom loBitsSet bits set.
  ///
  /// \param numBits the bitwidth of the result
  /// \param loBitsSet the number of low-order bits set in the result.
  /// \returns An APInt with the bottom \p loBitsSet bits set.
  static APInt getLowBitsSet(unsigned numBits, unsigned loBitsSet) {
    APInt Res(numBits, 0);
    Res.setLowBits(loBitsSet);
    return Res;
  }

  /// Return a value containing \p V broadcasted over \p NewLen bits.
  ///
  /// \param NewLen Bit width of the result
  /// \param V Pattern to splat
  /// \returns \p V broadcasted over \p NewLen bits.
  LLVM_ABI static APInt getSplat(unsigned NewLen, const APInt &V);

  /// @}
  /// \name Value Tests
  /// @{

  /// Determine if this APInt just has one word to store value.
  ///
  /// \returns true if the number of bits <= 64, false otherwise.
  bool isSingleWord() const { return BitWidth <= APINT_BITS_PER_WORD; }

  /// Determine sign of this APInt.
  ///
  /// This tests the high bit of this APInt to determine if it is set.
  ///
  /// \returns true if this APInt is negative, false otherwise
  bool isNegative() const { return (*this)[BitWidth - 1]; }

  /// Determine if this APInt Value is non-negative (>= 0)
  ///
  /// This tests the high bit of the APInt to determine if it is unset.
  ///
  /// \returns True if this APInt is non-negative.
  bool isNonNegative() const { return !isNegative(); }

  /// Determine if sign bit of this APInt is set.
  ///
  /// This tests the high bit of this APInt to determine if it is set.
  ///
  /// \returns true if this APInt has its sign bit set, false otherwise.
  bool isSignBitSet() const { return (*this)[BitWidth - 1]; }

  /// Determine if sign bit of this APInt is clear.
  ///
  /// This tests the high bit of this APInt to determine if it is clear.
  ///
  /// \returns true if this APInt has its sign bit clear, false otherwise.
  bool isSignBitClear() const { return !isSignBitSet(); }

  /// Determine if this APInt Value is positive.
  ///
  /// This tests if the value of this APInt is positive (> 0). Note
  /// that 0 is not a positive value.
  ///
  /// \returns true if this APInt is positive.
  bool isStrictlyPositive() const { return isNonNegative() && !isZero(); }

  /// Determine if this APInt Value is non-positive (<= 0).
  ///
  /// \returns true if this APInt is non-positive.
  bool isNonPositive() const { return !isStrictlyPositive(); }

  /// Determine if this APInt Value only has the specified bit set.
  ///
  /// \param BitNo Zero-based index of the sole set bit
  /// \returns true if this APInt only has the specified bit set.
  bool isOneBitSet(unsigned BitNo) const {
    return (*this)[BitNo] && popcount() == 1;
  }

  /// Determine if all bits are set.  This is true for zero-width values.
  ///
  /// \returns True if every bit is set.
  bool isAllOnes() const {
    if (BitWidth == 0)
      return true;
    if (isSingleWord())
      return U.VAL == WORDTYPE_MAX >> (APINT_BITS_PER_WORD - BitWidth);
    return countTrailingOnesSlowCase() == BitWidth;
  }

  /// Determine if this value is zero, i.e. all bits are clear.
  ///
  /// \returns True if every bit is clear.
  bool isZero() const {
    if (isSingleWord())
      return U.VAL == 0;
    return countLeadingZerosSlowCase() == BitWidth;
  }

  /// Determine if this is a value of 1.
  ///
  /// This checks to see if the value of this APInt is one.
  ///
  /// \returns True if this APInt equals one.
  bool isOne() const {
    if (isSingleWord())
      return U.VAL == 1;
    return countLeadingZerosSlowCase() == BitWidth - 1;
  }

  /// Determine if this is the largest unsigned value.
  ///
  /// This checks to see if the value of this APInt is the maximum unsigned
  /// value for the APInt's bit width.
  ///
  /// \returns True if this is the maximum unsigned value.
  bool isMaxValue() const { return isAllOnes(); }

  /// Determine if this is the largest signed value.
  ///
  /// This checks to see if the value of this APInt is the maximum signed
  /// value for the APInt's bit width.
  ///
  /// \returns True if this is the maximum signed value.
  bool isMaxSignedValue() const {
    if (isSingleWord()) {
      assert(BitWidth && "zero width values not allowed");
      return U.VAL == ((WordType(1) << (BitWidth - 1)) - 1);
    }
    return !isNegative() && countTrailingOnesSlowCase() == BitWidth - 1;
  }

  /// Determine if this is the smallest unsigned value.
  ///
  /// This checks to see if the value of this APInt is the minimum unsigned
  /// value for the APInt's bit width.
  ///
  /// \returns True if this is the minimum unsigned value.
  bool isMinValue() const { return isZero(); }

  /// Determine if this is the smallest signed value.
  ///
  /// This checks to see if the value of this APInt is the minimum signed
  /// value for the APInt's bit width.
  ///
  /// \returns True if this is the minimum signed value.
  bool isMinSignedValue() const {
    if (isSingleWord()) {
      assert(BitWidth && "zero width values not allowed");
      return U.VAL == (WordType(1) << (BitWidth - 1));
    }
    return isNegative() && countTrailingZerosSlowCase() == BitWidth - 1;
  }

  /// Check if this APInt has an N-bit unsigned integer value.
  ///
  /// \param N Maximum unsigned bit width
  /// \returns True if this value fits in \p N unsigned bits.
  bool isIntN(unsigned N) const { return getActiveBits() <= N; }

  /// Check if this APInt has an N-bit signed integer value.
  ///
  /// \param N Maximum signed bit width
  /// \returns True if this value fits in \p N signed bits.
  bool isSignedIntN(unsigned N) const { return getSignificantBits() <= N; }

  /// Check if this APInt's value is a power of two greater than zero.
  ///
  /// \returns true if the argument APInt value is a power of two > 0.
  bool isPowerOf2() const {
    if (isSingleWord()) {
      assert(BitWidth && "zero width values not allowed");
      return isPowerOf2_64(U.VAL);
    }
    return isPowerOf2SlowCase();
  }

  /// Check if this APInt's negated value is a power of two greater than zero.
  ///
  /// \returns True if the negated value is a power of two greater than zero.
  bool isNegatedPowerOf2() const {
    assert(BitWidth && "zero width values not allowed");
    if (isNonNegative())
      return false;
    // NegatedPowerOf2 - shifted mask in the top bits.
    unsigned LO = countl_one();
    unsigned TZ = countr_zero();
    return (LO + TZ) == BitWidth;
  }

  /// Return true if this APInt, interpreted as an address, is aligned to \p A.
  ///
  /// \param A Required alignment
  /// \returns True if this value is aligned to \p A.
  LLVM_ABI bool isAligned(Align A) const;

  /// Check if the APInt's value is returned by getSignMask.
  ///
  /// \returns true if this is the value returned by getSignMask.
  bool isSignMask() const { return isMinSignedValue(); }

  /// Convert APInt to a boolean value.
  ///
  /// This converts the APInt to a boolean value as a test against zero.
  ///
  /// \returns True if this APInt is non-zero.
  bool getBoolValue() const { return !isZero(); }

  /// Return this value, or \p Limit if this value is larger.
  ///
  /// This causes the value to saturate to the limit.
  ///
  /// \param Limit Maximum value to return
  /// \returns This value, or \p Limit if this value is larger.
  uint64_t getLimitedValue(uint64_t Limit = UINT64_MAX) const {
    return ugt(Limit) ? Limit : getZExtValue();
  }

  /// Check if the APInt consists of a repeated bit pattern.
  ///
  /// e.g. 0x01010101 satisfies isSplat(8).
  /// \param SplatSizeInBits The size of the pattern in bits. Must divide bit
  /// width without remainder.
  /// \returns True if this APInt is a repeated \p SplatSizeInBits pattern.
  LLVM_ABI bool isSplat(unsigned SplatSizeInBits) const;

  /// Return true if this APInt is a low mask of \p numBits ones.
  ///
  /// That is, a sequence of \p numBits ones starting at the least significant
  /// bit with the remainder zero.
  ///
  /// \param numBits Number of trailing ones required
  /// \returns True if this is a low mask of \p numBits ones.
  bool isMask(unsigned numBits) const {
    assert(numBits != 0 && "numBits must be non-zero");
    assert(numBits <= BitWidth && "numBits out of range");
    if (isSingleWord())
      return U.VAL == (WORDTYPE_MAX >> (APINT_BITS_PER_WORD - numBits));
    unsigned Ones = countTrailingOnesSlowCase();
    return (numBits == Ones) &&
           ((Ones + countLeadingZerosSlowCase()) == BitWidth);
  }

  /// Return true if this APInt is a non-empty low mask of ones.
  ///
  /// That is, a non-empty sequence of ones starting at the least significant
  /// bit with the remainder zero. Ex. isMask(0x0000FFFFU) == true.
  ///
  /// \returns True if this is a non-empty low mask of ones.
  bool isMask() const {
    if (isSingleWord())
      return isMask_64(U.VAL);
    unsigned Ones = countTrailingOnesSlowCase();
    return (Ones > 0) && ((Ones + countLeadingZerosSlowCase()) == BitWidth);
  }

  /// Return true if this APInt value contains a non-empty sequence of ones with
  /// the remainder zero.
  ///
  /// \returns True if this is a non-empty shifted mask of ones.
  bool isShiftedMask() const {
    if (isSingleWord())
      return isShiftedMask_64(U.VAL);
    unsigned Ones = countPopulationSlowCase();
    unsigned LeadZ = countLeadingZerosSlowCase();
    return (Ones + LeadZ + countTrailingZerosSlowCase()) == BitWidth;
  }

  /// Return true if this APInt is a non-empty shifted mask of ones.
  ///
  /// If true, \p MaskIdx is set to the index of the lowest set bit and
  /// \p MaskLen to the length of the mask; otherwise neither is updated.
  ///
  /// \param MaskIdx Out-parameter for the lowest set bit index
  /// \param MaskLen Out-parameter for the consecutive ones length
  /// \returns True if this is a non-empty shifted mask of ones.
  bool isShiftedMask(unsigned &MaskIdx, unsigned &MaskLen) const {
    if (isSingleWord())
      return isShiftedMask_64(U.VAL, MaskIdx, MaskLen);
    unsigned Ones = countPopulationSlowCase();
    unsigned LeadZ = countLeadingZerosSlowCase();
    unsigned TrailZ = countTrailingZerosSlowCase();
    if ((Ones + LeadZ + TrailZ) != BitWidth)
      return false;
    MaskLen = Ones;
    MaskIdx = TrailZ;
    return true;
  }

  /// Compute an APInt containing \p numBits high bits from this APInt.
  ///
  /// Get an APInt with the same BitWidth as this APInt, just zero mask the low
  /// bits and right shift to the least significant bit.
  ///
  /// \param numBits Number of high bits to extract
  /// \returns the high "numBits" bits of this APInt.
  LLVM_ABI APInt getHiBits(unsigned numBits) const;

  /// Compute an APInt containing \p numBits low bits from this APInt.
  ///
  /// Get an APInt with the same BitWidth as this APInt, just zero mask the high
  /// bits.
  ///
  /// \param numBits Number of low bits to extract
  /// \returns the low "numBits" bits of this APInt.
  LLVM_ABI APInt getLoBits(unsigned numBits) const;

  /// Return true if two APInts have the same numeric value after matching widths.
  ///
  /// Zero-extends or sign-extends (if \p SignedCompare) one of them when needed
  /// so the bit-widths match before comparing.
  ///
  /// \param I1 First value
  /// \param I2 Second value
  /// \param SignedCompare If true, sign-extend the narrower value; else zero-extend
  /// \returns True if \p I1 and \p I2 have the same numeric value.
  static bool isSameValue(const APInt &I1, const APInt &I2,
                          bool SignedCompare = false) {
    if (I1.getBitWidth() == I2.getBitWidth())
      return I1 == I2;

    auto ZExtOrSExt = [SignedCompare](const APInt &I, unsigned BitWidth) {
      return SignedCompare ? I.sext(BitWidth) : I.zext(BitWidth);
    };

    if (I1.getBitWidth() > I2.getBitWidth())
      return I1 == ZExtOrSExt(I2, I1.getBitWidth());

    return ZExtOrSExt(I1, I2.getBitWidth()) == I2;
  }

  /// Overload to compute a hash_code for an APInt value.
  ///
  /// \param Arg Value to hash
  /// \returns Hash code for \p Arg.
  LLVM_ABI friend hash_code hash_value(const APInt &Arg);

  /// This function returns a pointer to the internal storage of the APInt.
  /// This is useful for writing out the APInt in binary form without any
  /// conversions.
  ///
  /// \returns Pointer to the internal word storage.
  const uint64_t *getRawData() const {
    if (isSingleWord())
      return &U.VAL;
    return &U.pVal[0];
  }

  /// @}
  /// \name Unary Operators
  /// @{

  /// Postfix increment operator. Increment *this by 1.
  ///
  /// \param Ignored Dummy parameter distinguishing postfix from prefix
  /// \returns a new APInt value representing the original value of *this.
  APInt operator++(int Ignored) {
    APInt API(*this);
    ++(*this);
    return API;
  }

  /// Prefix increment operator.
  ///
  /// \returns *this incremented by one
  LLVM_ABI APInt &operator++();

  /// Postfix decrement operator. Decrement *this by 1.
  ///
  /// \param Ignored Dummy parameter distinguishing postfix from prefix
  /// \returns a new APInt value representing the original value of *this.
  APInt operator--(int Ignored) {
    APInt API(*this);
    --(*this);
    return API;
  }

  /// Prefix decrement operator.
  ///
  /// \returns *this decremented by one.
  LLVM_ABI APInt &operator--();

  /// Logical negation operation on this APInt returns true if zero, like normal
  /// integers.
  ///
  /// \returns True if this APInt is zero.
  bool operator!() const { return isZero(); }

  /// @}
  /// \name Assignment Operators
  /// @{

  /// Copy assignment operator.
  ///
  /// \param RHS Source value
  /// \returns *this after assignment of RHS.
  APInt &operator=(const APInt &RHS) {
    // The common case (both source or dest being inline) doesn't require
    // allocation or deallocation.
    if (isSingleWord() && RHS.isSingleWord()) {
      U.VAL = RHS.U.VAL;
      BitWidth = RHS.BitWidth;
      return *this;
    }

    assignSlowCase(RHS);
    return *this;
  }

  /// Move assignment operator.
  ///
  /// \param that Source value (moved from)
  /// \returns *this after move assignment from \p that.
  APInt &operator=(APInt &&that) {
#ifdef EXPENSIVE_CHECKS
    // Some std::shuffle implementations still do self-assignment.
    if (this == &that)
      return *this;
#endif
    assert(this != &that && "Self-move not supported");
    if (!isSingleWord())
      delete[] U.pVal;

    // Use memcpy so that type based alias analysis sees both VAL and pVal
    // as modified.
    memcpy(&U, &that.U, sizeof(U));

    BitWidth = that.BitWidth;
    that.BitWidth = 0;
    return *this;
  }

  /// Assignment operator.
  ///
  /// The RHS value is assigned to *this. If the significant bits in RHS exceed
  /// the bit width, the excess bits are truncated. If the bit width is larger
  /// than 64, the value is zero filled in the unspecified high order bits.
  ///
  /// \param RHS Source 64-bit value
  /// \returns *this after assignment of RHS value.
  APInt &operator=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL = RHS;
      return clearUnusedBits();
    }
    U.pVal[0] = RHS;
    memset(U.pVal + 1, 0, (getNumWords() - 1) * APINT_WORD_SIZE);
    return *this;
  }

  /// Bitwise AND assignment operator.
  ///
  /// Performs a bitwise AND operation on this APInt and RHS. The result is
  /// assigned to *this.
  ///
  /// \param RHS Right-hand operand
  /// \returns *this after ANDing with RHS.
  APInt &operator&=(const APInt &RHS) {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      U.VAL &= RHS.U.VAL;
    else
      andAssignSlowCase(RHS);
    return *this;
  }

  /// Bitwise AND assignment operator.
  ///
  /// Performs a bitwise AND operation on this APInt and RHS. RHS is
  /// logically zero-extended or truncated to match the bit-width of
  /// the LHS.
  ///
  /// \param RHS Right-hand 64-bit operand
  /// \returns *this after ANDing with \p RHS.
  APInt &operator&=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL &= RHS;
      return *this;
    }
    U.pVal[0] &= RHS;
    memset(U.pVal + 1, 0, (getNumWords() - 1) * APINT_WORD_SIZE);
    return *this;
  }

  /// Bitwise OR assignment operator.
  ///
  /// Performs a bitwise OR operation on this APInt and RHS. The result is
  /// assigned *this;
  ///
  /// \param RHS Right-hand operand
  /// \returns *this after ORing with RHS.
  APInt &operator|=(const APInt &RHS) {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      U.VAL |= RHS.U.VAL;
    else
      orAssignSlowCase(RHS);
    return *this;
  }

  /// Bitwise OR assignment operator.
  ///
  /// Performs a bitwise OR operation on this APInt and RHS. RHS is
  /// logically zero-extended or truncated to match the bit-width of
  /// the LHS.
  ///
  /// \param RHS Right-hand 64-bit operand
  /// \returns *this after ORing with \p RHS.
  APInt &operator|=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL |= RHS;
      return clearUnusedBits();
    }
    U.pVal[0] |= RHS;
    return *this;
  }

  /// Bitwise XOR assignment operator.
  ///
  /// Performs a bitwise XOR operation on this APInt and RHS. The result is
  /// assigned to *this.
  ///
  /// \param RHS Right-hand operand
  /// \returns *this after XORing with RHS.
  APInt &operator^=(const APInt &RHS) {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      U.VAL ^= RHS.U.VAL;
    else
      xorAssignSlowCase(RHS);
    return *this;
  }

  /// Bitwise XOR assignment operator.
  ///
  /// Performs a bitwise XOR operation on this APInt and RHS. RHS is
  /// logically zero-extended or truncated to match the bit-width of
  /// the LHS.
  ///
  /// \param RHS Right-hand 64-bit operand
  /// \returns *this after XORing with \p RHS.
  APInt &operator^=(uint64_t RHS) {
    if (isSingleWord()) {
      U.VAL ^= RHS;
      return clearUnusedBits();
    }
    U.pVal[0] ^= RHS;
    return *this;
  }

  /// Multiplication assignment operator.
  ///
  /// Multiplies this APInt by RHS and assigns the result to *this.
  ///
  /// \param RHS Multiplier
  /// \returns *this
  LLVM_ABI APInt &operator*=(const APInt &RHS);
  /// Multiply by a 64-bit value and assign.
  ///
  /// \param RHS Multiplier
  /// \returns *this
  LLVM_ABI APInt &operator*=(uint64_t RHS);

  /// Addition assignment operator.
  ///
  /// Adds RHS to *this and assigns the result to *this.
  ///
  /// \param RHS Addend
  /// \returns *this
  LLVM_ABI APInt &operator+=(const APInt &RHS);
  /// Add a 64-bit value and assign.
  ///
  /// \param RHS Addend
  /// \returns *this
  LLVM_ABI APInt &operator+=(uint64_t RHS);

  /// Subtraction assignment operator.
  ///
  /// Subtracts RHS from *this and assigns the result to *this.
  ///
  /// \param RHS Subtrahend
  /// \returns *this
  LLVM_ABI APInt &operator-=(const APInt &RHS);
  /// Subtract a 64-bit value and assign.
  ///
  /// \param RHS Subtrahend
  /// \returns *this
  LLVM_ABI APInt &operator-=(uint64_t RHS);

  /// Left-shift assignment function.
  ///
  /// Shifts *this left by shiftAmt and assigns the result to *this.
  ///
  /// \param ShiftAmt Number of bits to shift
  /// \returns *this after shifting left by ShiftAmt
  APInt &operator<<=(unsigned ShiftAmt) {
    assert(ShiftAmt <= BitWidth && "Invalid shift amount");
    if (isSingleWord()) {
      if (ShiftAmt == BitWidth)
        U.VAL = 0;
      else
        U.VAL <<= ShiftAmt;
      return clearUnusedBits();
    }
    shlSlowCase(ShiftAmt);
    return *this;
  }

  /// Left-shift assignment function.
  ///
  /// Shifts *this left by shiftAmt and assigns the result to *this.
  ///
  /// \param ShiftAmt Number of bits to shift
  /// \returns *this after shifting left by ShiftAmt
  LLVM_ABI APInt &operator<<=(const APInt &ShiftAmt);

  /// @}
  /// \name Binary Operators
  /// @{

  /// Multiplication operator.
  ///
  /// Multiplies this APInt by RHS and returns the result.
  ///
  /// \param RHS Multiplier
  /// \returns The product of *this and \p RHS.
  LLVM_ABI APInt operator*(const APInt &RHS) const;

  /// Left logical shift operator.
  ///
  /// Shifts this APInt left by \p Bits and returns the result.
  ///
  /// \param Bits Number of bits to shift
  /// \returns *this shifted left by \p Bits.
  APInt operator<<(unsigned Bits) const { return shl(Bits); }

  /// Left logical shift operator.
  ///
  /// Shifts this APInt left by \p Bits and returns the result.
  ///
  /// \param Bits Number of bits to shift
  /// \returns *this shifted left by \p Bits.
  APInt operator<<(const APInt &Bits) const { return shl(Bits); }

  /// Arithmetic right-shift function.
  ///
  /// Arithmetic right-shift this APInt by shiftAmt.
  ///
  /// \param ShiftAmt Number of bits to shift
  /// \returns *this arithmetically shifted right by \p ShiftAmt.
  APInt ashr(unsigned ShiftAmt) const {
    APInt R(*this);
    R.ashrInPlace(ShiftAmt);
    return R;
  }

  /// Arithmetic right-shift this APInt by \p ShiftAmt in place.
  ///
  /// \param ShiftAmt Number of bits to shift
  void ashrInPlace(unsigned ShiftAmt) {
    assert(ShiftAmt <= BitWidth && "Invalid shift amount");
    if (isSingleWord()) {
      int64_t SExtVAL = SignExtend64(U.VAL, BitWidth);
      if (ShiftAmt == BitWidth)
        U.VAL = SExtVAL >> (APINT_BITS_PER_WORD - 1); // Fill with sign bit.
      else
        U.VAL = SExtVAL >> ShiftAmt;
      clearUnusedBits();
      return;
    }
    ashrSlowCase(ShiftAmt);
  }

  /// Logical right-shift function.
  ///
  /// Logical right-shift this APInt by shiftAmt.
  ///
  /// \param shiftAmt Number of bits to shift
  /// \returns *this logically shifted right by \p shiftAmt.
  APInt lshr(unsigned shiftAmt) const {
    APInt R(*this);
    R.lshrInPlace(shiftAmt);
    return R;
  }

  /// Logical right-shift this APInt by \p ShiftAmt in place.
  ///
  /// \param ShiftAmt Number of bits to shift
  void lshrInPlace(unsigned ShiftAmt) {
    assert(ShiftAmt <= BitWidth && "Invalid shift amount");
    if (isSingleWord()) {
      if (ShiftAmt == BitWidth)
        U.VAL = 0;
      else
        U.VAL >>= ShiftAmt;
      return;
    }
    lshrSlowCase(ShiftAmt);
  }

  /// Left-shift function.
  ///
  /// Left-shift this APInt by shiftAmt.
  ///
  /// \param shiftAmt Number of bits to shift
  /// \returns *this shifted left by \p shiftAmt.
  APInt shl(unsigned shiftAmt) const {
    APInt R(*this);
    R <<= shiftAmt;
    return R;
  }

  /// Relative logical shift right (negative amounts shift left).
  ///
  /// \param RelativeShift Signed shift amount
  /// \returns *this logically shifted by \p RelativeShift (negative = left).
  APInt relativeLShr(int RelativeShift) const {
    return RelativeShift > 0 ? lshr(RelativeShift) : shl(-RelativeShift);
  }

  /// Relative logical shift left (negative amounts shift right).
  ///
  /// \param RelativeShift Signed shift amount
  /// \returns *this logically shifted left by \p RelativeShift (negative = right).
  APInt relativeLShl(int RelativeShift) const {
    return relativeLShr(-RelativeShift);
  }

  /// Relative arithmetic shift right (negative amounts shift left).
  ///
  /// \param RelativeShift Signed shift amount
  /// \returns *this arithmetically shifted by \p RelativeShift (negative = left).
  APInt relativeAShr(int RelativeShift) const {
    return RelativeShift > 0 ? ashr(RelativeShift) : shl(-RelativeShift);
  }

  /// Relative arithmetic shift left (negative amounts shift right).
  ///
  /// \param RelativeShift Signed shift amount
  /// \returns *this arithmetically shifted left by \p RelativeShift (negative = right).
  APInt relativeAShl(int RelativeShift) const {
    return relativeAShr(-RelativeShift);
  }

  /// Rotate left by \p rotateAmt.
  ///
  /// \param rotateAmt Rotation amount in bits
  /// \returns *this rotated left by \p rotateAmt.
  LLVM_ABI APInt rotl(unsigned rotateAmt) const;

  /// Rotate right by \p rotateAmt.
  ///
  /// \param rotateAmt Rotation amount in bits
  /// \returns *this rotated right by \p rotateAmt.
  LLVM_ABI APInt rotr(unsigned rotateAmt) const;

  /// Arithmetic right-shift function.
  ///
  /// Arithmetic right-shift this APInt by shiftAmt.
  ///
  /// \param ShiftAmt Number of bits to shift
  /// \returns *this arithmetically shifted right by \p ShiftAmt.
  APInt ashr(const APInt &ShiftAmt) const {
    APInt R(*this);
    R.ashrInPlace(ShiftAmt);
    return R;
  }

  /// Arithmetic right-shift this APInt by \p shiftAmt in place.
  ///
  /// \param shiftAmt Number of bits to shift
  LLVM_ABI void ashrInPlace(const APInt &shiftAmt);

  /// Logical right-shift function.
  ///
  /// Logical right-shift this APInt by shiftAmt.
  ///
  /// \param ShiftAmt Number of bits to shift
  /// \returns *this logically shifted right by \p ShiftAmt.
  APInt lshr(const APInt &ShiftAmt) const {
    APInt R(*this);
    R.lshrInPlace(ShiftAmt);
    return R;
  }

  /// Logical right-shift this APInt by \p ShiftAmt in place.
  ///
  /// \param ShiftAmt Number of bits to shift
  LLVM_ABI void lshrInPlace(const APInt &ShiftAmt);

  /// Left-shift function.
  ///
  /// Left-shift this APInt by shiftAmt.
  ///
  /// \param ShiftAmt Number of bits to shift
  /// \returns *this shifted left by \p ShiftAmt.
  APInt shl(const APInt &ShiftAmt) const {
    APInt R(*this);
    R <<= ShiftAmt;
    return R;
  }

  /// Rotate left by \p rotateAmt.
  ///
  /// \param rotateAmt Rotation amount
  /// \returns *this rotated left by \p rotateAmt.
  LLVM_ABI APInt rotl(const APInt &rotateAmt) const;

  /// Rotate right by \p rotateAmt.
  ///
  /// \param rotateAmt Rotation amount
  /// \returns *this rotated right by \p rotateAmt.
  LLVM_ABI APInt rotr(const APInt &rotateAmt) const;

  /// Concatenate the bits from \p NewLSB onto the bottom of *this.
  ///
  /// This is equivalent to:
  ///   (this->zext(NewWidth) << NewLSB.getBitWidth()) | NewLSB.zext(NewWidth)
  ///
  /// \param NewLSB Bits placed in the low part of the result
  /// \returns *this concatenated with \p NewLSB in the low bits.
  APInt concat(const APInt &NewLSB) const {
    if (getBitWidth() == 0)
      return NewLSB;
    /// If the result will be small, then both the merged values are small.
    unsigned NewWidth = getBitWidth() + NewLSB.getBitWidth();
    if (NewWidth <= APINT_BITS_PER_WORD)
      return APInt(NewWidth, (U.VAL << NewLSB.getBitWidth()) | NewLSB.U.VAL);
    return concatSlowCase(NewLSB);
  }

  /// Unsigned division operation.
  ///
  /// Perform an unsigned divide operation on this APInt by RHS. Both this and
  /// RHS are treated as unsigned quantities for purposes of this division.
  ///
  /// \param RHS Divisor
  /// \returns a new APInt value containing the division result, rounded towards
  /// zero.
  LLVM_ABI APInt udiv(const APInt &RHS) const;
  /// Unsigned divide by a 64-bit value.
  ///
  /// \param RHS Divisor
  /// \returns the quotient, rounded towards zero.
  LLVM_ABI APInt udiv(uint64_t RHS) const;

  /// Signed division function for APInt.
  ///
  /// Signed divide this APInt by APInt RHS. The result is rounded towards zero.
  ///
  /// \param RHS Divisor
  /// \returns The quotient of *this / \p RHS, rounded towards zero.
  LLVM_ABI APInt sdiv(const APInt &RHS) const;
  /// Signed divide by a 64-bit value.
  ///
  /// The result is rounded towards zero.
  ///
  /// \param RHS Divisor
  /// \returns The quotient of *this / \p RHS, rounded towards zero.
  LLVM_ABI APInt sdiv(int64_t RHS) const;

  /// Unsigned remainder operation.
  ///
  /// Perform an unsigned remainder operation on this APInt with RHS being the
  /// divisor. Both this and RHS are treated as unsigned quantities for purposes
  /// of this operation.
  ///
  /// \param RHS Divisor
  /// \returns a new APInt value containing the remainder result
  LLVM_ABI APInt urem(const APInt &RHS) const;
  /// Unsigned remainder with a 64-bit divisor.
  ///
  /// \param RHS Divisor
  /// \returns The unsigned remainder of *this modulo \p RHS.
  LLVM_ABI uint64_t urem(uint64_t RHS) const;

  /// Function for signed remainder operation.
  ///
  /// Signed remainder operation on APInt.
  ///
  /// Note that this is a true remainder operation and not a modulo operation
  /// because the sign follows the sign of the dividend which is *this.
  ///
  /// \param RHS Divisor
  /// \returns The signed remainder of *this modulo \p RHS.
  LLVM_ABI APInt srem(const APInt &RHS) const;
  /// Signed remainder with a 64-bit divisor.
  ///
  /// \param RHS Divisor
  /// \returns The signed remainder of *this modulo \p RHS.
  LLVM_ABI int64_t srem(int64_t RHS) const;

  /// Dual unsigned division/remainder interface.
  ///
  /// Sometimes it is convenient to divide two APInt values and obtain both the
  /// quotient and remainder. This function does both operations in the same
  /// computation making it a little more efficient. The pair of input arguments
  /// may overlap with the pair of output arguments. It is safe to call
  /// udivrem(X, Y, X, Y), for example.
  ///
  /// \param LHS Dividend
  /// \param RHS Divisor
  /// \param Quotient Quotient output
  /// \param Remainder Remainder output
  LLVM_ABI static void udivrem(const APInt &LHS, const APInt &RHS,
                               APInt &Quotient, APInt &Remainder);
  /// Unsigned divide/remainder with a 64-bit divisor.
  ///
  /// \param LHS Dividend
  /// \param RHS Divisor
  /// \param Quotient Quotient output
  /// \param Remainder Remainder output
  LLVM_ABI static void udivrem(const APInt &LHS, uint64_t RHS, APInt &Quotient,
                               uint64_t &Remainder);

  /// Signed divide/remainder of two APInts.
  ///
  /// \param LHS Dividend
  /// \param RHS Divisor
  /// \param Quotient Quotient output
  /// \param Remainder Remainder output
  LLVM_ABI static void sdivrem(const APInt &LHS, const APInt &RHS,
                               APInt &Quotient, APInt &Remainder);
  /// Signed divide/remainder with a 64-bit divisor.
  ///
  /// \param LHS Dividend
  /// \param RHS Divisor
  /// \param Quotient Quotient output
  /// \param Remainder Remainder output
  LLVM_ABI static void sdivrem(const APInt &LHS, int64_t RHS, APInt &Quotient,
                               int64_t &Remainder);

  /// Operations that return overflow indicators.
  /// Signed add that records whether the result overflowed.
  ///
  /// \param RHS Right-hand operand
  /// \param Overflow Set to true if the operation overflowed
  /// \returns The signed sum of *this and \p RHS.
  LLVM_ABI APInt sadd_ov(const APInt &RHS, bool &Overflow) const;
  /// Unsigned add that records whether the result overflowed.
  ///
  /// \param RHS Right-hand operand
  /// \param Overflow Set to true if the operation overflowed
  /// \returns The unsigned sum of *this and \p RHS.
  LLVM_ABI APInt uadd_ov(const APInt &RHS, bool &Overflow) const;
  /// Signed subtract that records whether the result overflowed.
  ///
  /// \param RHS Right-hand operand
  /// \param Overflow Set to true if the operation overflowed
  /// \returns The signed difference of *this and \p RHS.
  LLVM_ABI APInt ssub_ov(const APInt &RHS, bool &Overflow) const;
  /// Unsigned subtract that records whether the result overflowed.
  ///
  /// \param RHS Right-hand operand
  /// \param Overflow Set to true if the operation overflowed
  /// \returns The unsigned difference of *this and \p RHS.
  LLVM_ABI APInt usub_ov(const APInt &RHS, bool &Overflow) const;
  /// Signed divide that records whether the result overflowed.
  ///
  /// \param RHS Right-hand operand
  /// \param Overflow Set to true if the operation overflowed
  /// \returns The signed quotient of *this and \p RHS.
  LLVM_ABI APInt sdiv_ov(const APInt &RHS, bool &Overflow) const;
  /// Signed multiply that records whether the result overflowed.
  ///
  /// \param RHS Right-hand operand
  /// \param Overflow Set to true if the operation overflowed
  /// \returns The signed product of *this and \p RHS.
  LLVM_ABI APInt smul_ov(const APInt &RHS, bool &Overflow) const;
  /// Unsigned multiply that records whether the result overflowed.
  ///
  /// \param RHS Right-hand operand
  /// \param Overflow Set to true if the operation overflowed
  /// \returns The unsigned product of *this and \p RHS.
  LLVM_ABI APInt umul_ov(const APInt &RHS, bool &Overflow) const;
  /// Signed left shift that records whether the result overflowed.
  ///
  /// \param Amt Shift amount
  /// \param Overflow Set to true if the operation overflowed
  /// \returns *this shifted left by \p Amt with signed overflow detection.
  LLVM_ABI APInt sshl_ov(const APInt &Amt, bool &Overflow) const;
  /// Signed left shift by a bit count that records overflow.
  ///
  /// \param Amt Shift amount in bits
  /// \param Overflow Set to true if the operation overflowed
  /// \returns *this shifted left by \p Amt with signed overflow detection.
  LLVM_ABI APInt sshl_ov(unsigned Amt, bool &Overflow) const;
  /// Unsigned left shift that records whether the result overflowed.
  ///
  /// \param Amt Shift amount
  /// \param Overflow Set to true if the operation overflowed
  /// \returns *this shifted left by \p Amt with unsigned overflow detection.
  LLVM_ABI APInt ushl_ov(const APInt &Amt, bool &Overflow) const;
  /// Unsigned left shift by a bit count that records overflow.
  ///
  /// \param Amt Shift amount in bits
  /// \param Overflow Set to true if the operation overflowed
  /// \returns *this shifted left by \p Amt with unsigned overflow detection.
  LLVM_ABI APInt ushl_ov(unsigned Amt, bool &Overflow) const;

  /// Signed integer floor division that records overflow.
  ///
  /// Rounds towards negative infinity, i.e. 5 / -2 = -3. Iff minimum value
  /// divided by -1 set Overflow to true.
  ///
  /// \param RHS Divisor
  /// \param Overflow Set to true if the operation overflowed
  /// \returns The signed floor quotient of *this and \p RHS.
  LLVM_ABI APInt sfloordiv_ov(const APInt &RHS, bool &Overflow) const;

  /// Operations that saturate on overflow.
  /// Signed add saturating to the signed min/max of the bit width.
  ///
  /// \param RHS Right-hand operand
  /// \returns The saturating signed sum of *this and \p RHS.
  LLVM_ABI APInt sadd_sat(const APInt &RHS) const;
  /// Unsigned add saturating to the unsigned max of the bit width.
  ///
  /// \param RHS Right-hand operand
  /// \returns The saturating unsigned sum of *this and \p RHS.
  LLVM_ABI APInt uadd_sat(const APInt &RHS) const;
  /// Signed subtract saturating to the signed min/max of the bit width.
  ///
  /// \param RHS Right-hand operand
  /// \returns The saturating signed difference of *this and \p RHS.
  LLVM_ABI APInt ssub_sat(const APInt &RHS) const;
  /// Unsigned subtract saturating at zero.
  ///
  /// \param RHS Right-hand operand
  /// \returns The saturating unsigned difference of *this and \p RHS.
  LLVM_ABI APInt usub_sat(const APInt &RHS) const;
  /// Signed multiply saturating to the signed min/max of the bit width.
  ///
  /// \param RHS Right-hand operand
  /// \returns The saturating signed product of *this and \p RHS.
  LLVM_ABI APInt smul_sat(const APInt &RHS) const;
  /// Unsigned multiply saturating to the unsigned max of the bit width.
  ///
  /// \param RHS Right-hand operand
  /// \returns The saturating unsigned product of *this and \p RHS.
  LLVM_ABI APInt umul_sat(const APInt &RHS) const;
  /// Signed left shift saturating to the signed min/max of the bit width.
  ///
  /// \param RHS Right-hand operand
  /// \returns *this shifted left by \p RHS with signed saturation.
  LLVM_ABI APInt sshl_sat(const APInt &RHS) const;
  /// Signed left shift by a bit count with saturation.
  ///
  /// \param RHS Shift amount in bits
  /// \returns *this shifted left by \p RHS with signed saturation.
  LLVM_ABI APInt sshl_sat(unsigned RHS) const;
  /// Unsigned left shift saturating to the unsigned max of the bit width.
  ///
  /// \param RHS Shift amount
  /// \returns *this shifted left by \p RHS with unsigned saturation.
  LLVM_ABI APInt ushl_sat(const APInt &RHS) const;
  /// Unsigned left shift by a bit count with saturation.
  ///
  /// \param RHS Shift amount in bits
  /// \returns *this shifted left by \p RHS with unsigned saturation.
  LLVM_ABI APInt ushl_sat(unsigned RHS) const;

  /// Array-indexing support for individual bits.
  ///
  /// \param bitPosition Zero-based bit index
  /// \returns the bit value at bitPosition
  bool operator[](unsigned bitPosition) const {
    assert(bitPosition < getBitWidth() && "Bit position out of bounds!");
    return (maskBit(bitPosition) & getWord(bitPosition)) != 0;
  }

  /// @}
  /// \name Comparison Operators
  /// @{

  /// Equality operator.
  ///
  /// Compares this APInt with RHS for the validity of the equality
  /// relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns True if *this == \p RHS.
  bool operator==(const APInt &RHS) const {
    assert(BitWidth == RHS.BitWidth && "Comparison requires equal bit widths");
    if (isSingleWord())
      return U.VAL == RHS.U.VAL;
    return equalSlowCase(RHS);
  }

  /// Equality operator.
  ///
  /// Compares this APInt with a uint64_t for the validity of the equality
  /// relationship.
  ///
  /// \param Val 64-bit value to compare against
  /// \returns true if *this == Val
  bool operator==(uint64_t Val) const {
    return (isSingleWord() || getActiveBits() <= 64) && getZExtValue() == Val;
  }

  /// Equality comparison.
  ///
  /// Compares this APInt with RHS for the validity of the equality
  /// relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this == RHS
  bool eq(const APInt &RHS) const { return (*this) == RHS; }

  /// Inequality operator.
  ///
  /// Compares this APInt with RHS for the validity of the inequality
  /// relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this != RHS
  bool operator!=(const APInt &RHS) const { return !((*this) == RHS); }

  /// Inequality operator.
  ///
  /// Compares this APInt with a uint64_t for the validity of the inequality
  /// relationship.
  ///
  /// \param Val 64-bit value to compare against
  /// \returns true if *this != Val
  bool operator!=(uint64_t Val) const { return !((*this) == Val); }

  /// Inequality comparison.
  ///
  /// Compares this APInt with RHS for the validity of the inequality
  /// relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this != RHS
  bool ne(const APInt &RHS) const { return !((*this) == RHS); }

  /// Unsigned less than comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// the validity of the less-than relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this < RHS when both are considered unsigned.
  bool ult(const APInt &RHS) const { return compare(RHS) < 0; }

  /// Unsigned less than comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the less-than relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this < RHS when considered unsigned.
  bool ult(uint64_t RHS) const {
    // Only need to check active bits if not a single word.
    return (isSingleWord() || getActiveBits() <= 64) && getZExtValue() < RHS;
  }

  /// Signed less than comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for
  /// validity of the less-than relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this < RHS when both are considered signed.
  bool slt(const APInt &RHS) const { return compareSigned(RHS) < 0; }

  /// Signed less than comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for
  /// the validity of the less-than relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this < RHS when considered signed.
  bool slt(int64_t RHS) const {
    return (!isSingleWord() && getSignificantBits() > 64)
               ? isNegative()
               : getSExtValue() < RHS;
  }

  /// Unsigned less or equal comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// validity of the less-or-equal relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this <= RHS when both are considered unsigned.
  bool ule(const APInt &RHS) const { return compare(RHS) <= 0; }

  /// Unsigned less or equal comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the less-or-equal relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this <= RHS when considered unsigned.
  bool ule(uint64_t RHS) const { return !ugt(RHS); }

  /// Signed less or equal comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for
  /// validity of the less-or-equal relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this <= RHS when both are considered signed.
  bool sle(const APInt &RHS) const { return compareSigned(RHS) <= 0; }

  /// Signed less or equal comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for the
  /// validity of the less-or-equal relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this <= RHS when considered signed.
  bool sle(uint64_t RHS) const { return !sgt(RHS); }

  /// Unsigned greater than comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// the validity of the greater-than relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this > RHS when both are considered unsigned.
  bool ugt(const APInt &RHS) const { return !ule(RHS); }

  /// Unsigned greater than comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the greater-than relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this > RHS when considered unsigned.
  bool ugt(uint64_t RHS) const {
    // Only need to check active bits if not a single word.
    return (!isSingleWord() && getActiveBits() > 64) || getZExtValue() > RHS;
  }

  /// Signed greater than comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for the
  /// validity of the greater-than relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this > RHS when both are considered signed.
  bool sgt(const APInt &RHS) const { return !sle(RHS); }

  /// Signed greater than comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for
  /// the validity of the greater-than relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this > RHS when considered signed.
  bool sgt(int64_t RHS) const {
    return (!isSingleWord() && getSignificantBits() > 64)
               ? !isNegative()
               : getSExtValue() > RHS;
  }

  /// Unsigned greater or equal comparison
  ///
  /// Regards both *this and RHS as unsigned quantities and compares them for
  /// validity of the greater-or-equal relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this >= RHS when both are considered unsigned.
  bool uge(const APInt &RHS) const { return !ult(RHS); }

  /// Unsigned greater or equal comparison
  ///
  /// Regards both *this as an unsigned quantity and compares it with RHS for
  /// the validity of the greater-or-equal relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this >= RHS when considered unsigned.
  bool uge(uint64_t RHS) const { return !ult(RHS); }

  /// Signed greater or equal comparison
  ///
  /// Regards both *this and RHS as signed quantities and compares them for
  /// validity of the greater-or-equal relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this >= RHS when both are considered signed.
  bool sge(const APInt &RHS) const { return !slt(RHS); }

  /// Signed greater or equal comparison
  ///
  /// Regards both *this as a signed quantity and compares it with RHS for
  /// the validity of the greater-or-equal relationship.
  ///
  /// \param RHS Value to compare against
  /// \returns true if *this >= RHS when considered signed.
  bool sge(int64_t RHS) const { return !slt(RHS); }

  /// Return true if any corresponding bits are set in both this and \p RHS.
  ///
  /// \param RHS Other value to test against
  /// \returns True if *this and \p RHS share any set bits.
  bool intersects(const APInt &RHS) const {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      return (U.VAL & RHS.U.VAL) != 0;
    return intersectsSlowCase(RHS);
  }

  /// Return true if every bit set in this APInt is also set in \p RHS.
  ///
  /// \param RHS Superset candidate
  /// \returns True if every set bit in *this is also set in \p RHS.
  bool isSubsetOf(const APInt &RHS) const {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      return (U.VAL & ~RHS.U.VAL) == 0;
    return isSubsetOfSlowCase(RHS);
  }

  /// Return true if every bit is set in exactly one of this or \p RHS.
  ///
  /// \param RHS Other value to test against
  /// \returns True if *this is the bitwise inverse of \p RHS.
  bool isInverseOf(const APInt &RHS) const {
    assert(BitWidth == RHS.BitWidth && "Bit widths must be the same");
    if (isSingleWord())
      return (U.VAL ^ RHS.U.VAL) == llvm::maskTrailingOnes<WordType>(BitWidth);
    return isInverseOfSlowCase(RHS);
  }

  /// @}
  /// \name Resizing Operators
  /// @{

  /// Truncate to a new width.
  ///
  /// It is an error to specify a width greater than the current width.
  ///
  /// \param width New bit width
  /// \returns This value truncated to \p width bits.
  LLVM_ABI APInt trunc(unsigned width) const;

  /// Truncate to a new width with unsigned saturation.
  ///
  /// If the APInt, treated as unsigned, can be losslessly truncated to the new
  /// bit width, return the truncated value; otherwise return the max value.
  ///
  /// \param width New bit width
  /// \returns This value truncated to \p width with unsigned saturation.
  LLVM_ABI APInt truncUSat(unsigned width) const;

  /// Truncate to a new width with signed saturation to a signed result.
  ///
  /// If this APInt, treated as signed, can be losslessly truncated, return the
  /// truncated value; otherwise return signed min or max.
  ///
  /// \param width New bit width
  /// \returns This value truncated to \p width with signed saturation.
  LLVM_ABI APInt truncSSat(unsigned width) const;

  /// Truncate to a new width with signed saturation to an unsigned result.
  ///
  /// If this APInt, treated as signed, can be losslessly truncated, return the
  /// truncated value; otherwise return zero (if negative) or unsigned max.
  /// If \p width matches the current bit width then no changes are made.
  ///
  /// \param width New bit width
  /// \returns This value truncated to \p width with signed-to-unsigned saturation.
  LLVM_ABI APInt truncSSatU(unsigned width) const;

  /// Sign-extend to a new width.
  ///
  /// If the high-order bit is set, fill on the left with ones; otherwise zeros.
  /// It is an error to specify a width less than the current width.
  ///
  /// \param width New bit width
  /// \returns This value sign-extended to \p width bits.
  LLVM_ABI APInt sext(unsigned width) const;

  /// Zero-extend to a new width.
  ///
  /// High-order bits are filled with zeros. It is an error to specify a width
  /// less than the current width.
  ///
  /// \param width New bit width
  /// \returns This value zero-extended to \p width bits.
  LLVM_ABI APInt zext(unsigned width) const;

  /// Sign-extend or truncate to \p width.
  ///
  /// \param width Desired bit width
  /// \returns This value sign-extended or truncated to \p width.
  LLVM_ABI APInt sextOrTrunc(unsigned width) const;

  /// Zero-extend or truncate to \p width.
  ///
  /// \param width Desired bit width
  /// \returns This value zero-extended or truncated to \p width.
  LLVM_ABI APInt zextOrTrunc(unsigned width) const;

  /// @}
  /// \name Bit Manipulation Operators
  /// @{

  /// Set every bit to 1.
  void setAllBits() {
    if (isSingleWord())
      U.VAL = WORDTYPE_MAX;
    else
      // Set all the bits in all the words.
      memset(U.pVal, -1, getNumWords() * APINT_WORD_SIZE);
    // Clear the unused ones
    clearUnusedBits();
  }

  /// Set the bit at \p BitPosition to 1.
  ///
  /// \param BitPosition Zero-based bit index to set
  void setBit(unsigned BitPosition) {
    assert(BitPosition < BitWidth && "BitPosition out of range");
    WordType Mask = maskBit(BitPosition);
    if (isSingleWord())
      U.VAL |= Mask;
    else
      U.pVal[whichWord(BitPosition)] |= Mask;
  }

  /// Set the sign bit to 1.
  void setSignBit() { setBit(BitWidth - 1); }

  /// Set the bit at \p BitPosition to \p BitValue.
  ///
  /// \param BitPosition Zero-based bit index
  /// \param BitValue Value to store (true = 1, false = 0)
  void setBitVal(unsigned BitPosition, bool BitValue) {
    if (BitValue)
      setBit(BitPosition);
    else
      clearBit(BitPosition);
  }

  /// Set bits from \p loBit to \p hiBit, wrapping if \p loBit >= \p hiBit.
  ///
  /// When \p loBit < \p hiBit, this calls setBits. When they wrap
  /// (\p loBit >= \p hiBit), high and low ranges are set. For
  /// \p loBit == \p hiBit, every bit is set.
  ///
  /// \param loBit First bit of the range (inclusive)
  /// \param hiBit End of the range (exclusive), or wrap point
  void setBitsWithWrap(unsigned loBit, unsigned hiBit) {
    assert(hiBit <= BitWidth && "hiBit out of range");
    assert(loBit <= BitWidth && "loBit out of range");
    if (loBit < hiBit) {
      setBits(loBit, hiBit);
      return;
    }
    setLowBits(hiBit);
    setHighBits(BitWidth - loBit);
  }

  /// Set bits from \p loBit (inclusive) to \p hiBit (exclusive).
  ///
  /// Requires \p loBit <= \p hiBit.
  ///
  /// \param loBit First bit to set (inclusive)
  /// \param hiBit One past the last bit to set
  void setBits(unsigned loBit, unsigned hiBit) {
    assert(hiBit <= BitWidth && "hiBit out of range");
    assert(loBit <= hiBit && "loBit greater than hiBit");
    if (loBit == hiBit)
      return;
    if (hiBit <= APINT_BITS_PER_WORD) {
      uint64_t mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - (hiBit - loBit));
      mask <<= loBit;
      if (isSingleWord())
        U.VAL |= mask;
      else
        U.pVal[0] |= mask;
    } else {
      setBitsSlowCase(loBit, hiBit);
    }
  }

  /// Set all bits from \p loBit through the top of the value.
  ///
  /// \param loBit First bit to set (inclusive)
  void setBitsFrom(unsigned loBit) { return setBits(loBit, BitWidth); }

  /// Set the bottom \p loBits bits.
  ///
  /// \param loBits Number of low bits to set
  void setLowBits(unsigned loBits) { return setBits(0, loBits); }

  /// Set the top \p hiBits bits.
  ///
  /// \param hiBits Number of high bits to set
  void setHighBits(unsigned hiBits) {
    return setBits(BitWidth - hiBits, BitWidth);
  }

  /// Set every bit to 0.
  void clearAllBits() {
    if (isSingleWord())
      U.VAL = 0;
    else
      memset(U.pVal, 0, getNumWords() * APINT_WORD_SIZE);
  }

  /// Clear the bit at \p BitPosition.
  ///
  /// \param BitPosition Zero-based bit index to clear
  void clearBit(unsigned BitPosition) {
    assert(BitPosition < BitWidth && "BitPosition out of range");
    WordType Mask = ~maskBit(BitPosition);
    if (isSingleWord())
      U.VAL &= Mask;
    else
      U.pVal[whichWord(BitPosition)] &= Mask;
  }

  /// Clear bits from \p LoBit (inclusive) to \p HiBit (exclusive).
  ///
  /// Requires \p LoBit <= \p HiBit.
  ///
  /// \param LoBit First bit to clear (inclusive)
  /// \param HiBit One past the last bit to clear
  void clearBits(unsigned LoBit, unsigned HiBit) {
    assert(HiBit <= BitWidth && "HiBit out of range");
    assert(LoBit <= HiBit && "LoBit greater than HiBit");
    if (LoBit == HiBit)
      return;
    if (HiBit <= APINT_BITS_PER_WORD) {
      uint64_t Mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - (HiBit - LoBit));
      Mask = ~(Mask << LoBit);
      if (isSingleWord())
        U.VAL &= Mask;
      else
        U.pVal[0] &= Mask;
    } else {
      clearBitsSlowCase(LoBit, HiBit);
    }
  }

  /// Clear the bottom \p loBits bits.
  ///
  /// \param loBits Number of low bits to clear
  void clearLowBits(unsigned loBits) {
    assert(loBits <= BitWidth && "More bits than bitwidth");
    APInt Keep = getHighBitsSet(BitWidth, BitWidth - loBits);
    *this &= Keep;
  }

  /// Clear the top \p hiBits bits.
  ///
  /// \param hiBits Number of high bits to clear
  void clearHighBits(unsigned hiBits) {
    assert(hiBits <= BitWidth && "More bits than bitwidth");
    APInt Keep = getLowBitsSet(BitWidth, BitWidth - hiBits);
    *this &= Keep;
  }

  /// Set the sign bit to 0.
  void clearSignBit() { clearBit(BitWidth - 1); }

  /// Toggle every bit to its opposite value.
  void flipAllBits() {
    if (isSingleWord()) {
      U.VAL ^= WORDTYPE_MAX;
      clearUnusedBits();
    } else {
      flipAllBitsSlowCase();
    }
  }

  /// Toggle the bit at \p bitPosition to its opposite value.
  ///
  /// \param bitPosition Zero-based bit index to flip
  LLVM_ABI void flipBit(unsigned bitPosition);

  /// Negate this APInt in place.
  void negate() {
    flipAllBits();
    ++(*this);
  }

  /// Insert the bits from a smaller APInt starting at \p bitPosition.
  ///
  /// \param SubBits Bits to insert
  /// \param bitPosition Starting bit index in this APInt
  LLVM_ABI void insertBits(const APInt &SubBits, unsigned bitPosition);
  /// Insert \p numBits low bits from \p SubBits starting at \p bitPosition.
  ///
  /// \param SubBits Source bits (low \p numBits used)
  /// \param bitPosition Starting bit index in this APInt
  /// \param numBits Number of bits to insert
  LLVM_ABI void insertBits(uint64_t SubBits, unsigned bitPosition,
                           unsigned numBits);

  /// Return an APInt with bits [bitPosition, bitPosition+numBits).
  ///
  /// \param numBits Number of bits to extract
  /// \param bitPosition Starting bit index
  /// \returns An APInt holding the extracted bit field.
  LLVM_ABI APInt extractBits(unsigned numBits, unsigned bitPosition) const;
  /// Extract bits as a zero-extended 64-bit value.
  ///
  /// \param numBits Number of bits to extract
  /// \param bitPosition Starting bit index
  /// \returns The extracted bits as a zero-extended uint64_t.
  LLVM_ABI uint64_t extractBitsAsZExtValue(unsigned numBits,
                                           unsigned bitPosition) const;

  /// @}
  /// \name Value Characterization Functions
  /// @{

  /// Return the number of bits in the APInt.
  ///
  /// \returns The bit width of this APInt.
  unsigned getBitWidth() const { return BitWidth; }

  /// Get the number of words.
  ///
  /// Here one word's bitwidth equals to that of uint64_t.
  ///
  /// \returns the number of words to hold the integer value of this APInt.
  unsigned getNumWords() const { return getNumWords(BitWidth); }

  /// Get the number of words needed for a given bit width.
  ///
  /// *NOTE* Here one word's bitwidth equals to that of uint64_t.
  ///
  /// \param BitWidth Bit width of the integer
  /// \returns the number of words to hold an integer of that width
  static unsigned getNumWords(unsigned BitWidth) {
    return ((uint64_t)BitWidth + APINT_BITS_PER_WORD - 1) / APINT_BITS_PER_WORD;
  }

  /// Compute the number of active bits in the value
  ///
  /// This function returns the number of active bits which is defined as the
  /// bit width minus the number of leading zeros. This is used in several
  /// computations to see how "wide" the value is.
  ///
  /// \returns The number of active bits in this APInt.
  unsigned getActiveBits() const { return BitWidth - countl_zero(); }

  /// Compute the number of active words in the value of this APInt.
  ///
  /// This is used in conjunction with getActiveData to extract the raw value of
  /// the APInt.
  ///
  /// \returns The number of active words in this APInt.
  unsigned getActiveWords() const {
    unsigned numActiveBits = getActiveBits();
    return numActiveBits ? whichWord(numActiveBits - 1) + 1 : 1;
  }

  /// Get the minimum bit size for this signed APInt
  ///
  /// Computes the minimum bit width for this APInt while considering it to be a
  /// signed (and probably negative) value. If the value is not negative, this
  /// function returns the same value as getActiveBits()+1. Otherwise, it
  /// returns the smallest bit width that will retain the negative value. For
  /// example, -1 can be written as 0b1 or 0xFFFFFFFFFF. 0b1 is shorter and so
  /// for -1, this function will always return 1.
  ///
  /// \returns The minimum bit width needed to hold this value as signed.
  unsigned getSignificantBits() const {
    return BitWidth - getNumSignBits() + 1;
  }

  /// Get zero extended value
  ///
  /// This method attempts to return the value of this APInt as a zero extended
  /// uint64_t. The bitwidth must be <= 64 or the value must fit within a
  /// uint64_t. Otherwise an assertion will result.
  ///
  /// \returns This APInt zero-extended to uint64_t.
  uint64_t getZExtValue() const {
    if (isSingleWord())
      return U.VAL;
    assert(getActiveBits() <= 64 && "Too many bits for uint64_t");
    return U.pVal[0];
  }

  /// Get zero extended value if possible
  ///
  /// This method attempts to return the value of this APInt as a zero extended
  /// uint64_t. The bitwidth must be <= 64 or the value must fit within a
  /// uint64_t. Otherwise no value is returned.
  ///
  /// \returns The zero-extended value, or std::nullopt if it does not fit.
  std::optional<uint64_t> tryZExtValue() const {
    return (getActiveBits() <= 64) ? std::optional<uint64_t>(getZExtValue())
                                   : std::nullopt;
  };

  /// Get sign extended value
  ///
  /// This method attempts to return the value of this APInt as a sign extended
  /// int64_t. The bit width must be <= 64 or the value must fit within an
  /// int64_t. Otherwise an assertion will result.
  ///
  /// \returns This APInt sign-extended to int64_t.
  int64_t getSExtValue() const {
    if (isSingleWord())
      return SignExtend64(U.VAL, BitWidth);
    assert(getSignificantBits() <= 64 && "Too many bits for int64_t");
    return int64_t(U.pVal[0]);
  }

  /// Get sign extended value if possible
  ///
  /// This method attempts to return the value of this APInt as a sign extended
  /// int64_t. The bitwidth must be <= 64 or the value must fit within an
  /// int64_t. Otherwise no value is returned.
  ///
  /// \returns The sign-extended value, or std::nullopt if it does not fit.
  std::optional<int64_t> trySExtValue() const {
    return (getSignificantBits() <= 64) ? std::optional<int64_t>(getSExtValue())
                                        : std::nullopt;
  };

  /// Determine how many bits are required to hold the value of a string.
  ///
  /// \param str String representation of the integer
  /// \param radix Numeric base of \p str
  /// \returns The number of bits needed to hold the value of \p str.
  LLVM_ABI static unsigned getBitsNeeded(StringRef str, uint8_t radix);

  /// Estimate bits needed for a string without fully parsing it.
  ///
  /// This may overestimate the amount of bits required.
  ///
  /// \param Str String representation of the integer
  /// \param Radix Numeric base of \p Str
  /// \returns An upper bound on bits needed to hold the value of \p Str.
  LLVM_ABI static unsigned getSufficientBitsNeeded(StringRef Str,
                                                   uint8_t Radix);

  /// The APInt version of std::countl_zero.
  ///
  /// It counts the number of zeros from the most significant bit to the first
  /// one bit.
  ///
  /// \returns BitWidth if the value is zero, otherwise returns the number of
  ///   zeros from the most significant bit to the first one bits.
  unsigned countl_zero() const {
    if (isSingleWord()) {
      unsigned unusedBits = APINT_BITS_PER_WORD - BitWidth;
      return llvm::countl_zero(U.VAL) - unusedBits;
    }
    return countLeadingZerosSlowCase();
  }

  /// Alias for countl_zero().
  ///
  /// \returns The number of leading zero bits.
  unsigned countLeadingZeros() const { return countl_zero(); }

  /// Count the number of leading one bits.
  ///
  /// This function is an APInt version of std::countl_one. It counts the number
  /// of ones from the most significant bit to the first zero bit.
  ///
  /// \returns 0 if the high order bit is not set, otherwise returns the number
  /// of 1 bits from the most significant to the least
  unsigned countl_one() const {
    if (isSingleWord()) {
      if (LLVM_UNLIKELY(BitWidth == 0))
        return 0;
      return llvm::countl_one(U.VAL << (APINT_BITS_PER_WORD - BitWidth));
    }
    return countLeadingOnesSlowCase();
  }

  /// Alias for countl_one().
  ///
  /// \returns The number of leading one bits.
  unsigned countLeadingOnes() const { return countl_one(); }

  /// Computes the number of leading bits of this APInt that are equal to its
  /// sign bit.
  ///
  /// \returns The number of leading bits equal to the sign bit.
  unsigned getNumSignBits() const {
    return isNegative() ? countl_one() : countl_zero();
  }

  /// Count the number of trailing zero bits.
  ///
  /// This function is an APInt version of std::countr_zero. It counts the
  /// number of zeros from the least significant bit to the first set bit.
  ///
  /// \returns BitWidth if the value is zero, otherwise returns the number of
  /// zeros from the least significant bit to the first one bit.
  unsigned countr_zero() const {
    if (isSingleWord()) {
      unsigned TrailingZeros = llvm::countr_zero(U.VAL);
      return (TrailingZeros > BitWidth ? BitWidth : TrailingZeros);
    }
    return countTrailingZerosSlowCase();
  }

  /// Alias for countr_zero().
  ///
  /// \returns The number of trailing zero bits.
  unsigned countTrailingZeros() const { return countr_zero(); }

  /// Count the number of trailing one bits.
  ///
  /// This function is an APInt version of std::countr_one. It counts the number
  /// of ones from the least significant bit to the first zero bit.
  ///
  /// \returns BitWidth if the value is all ones, otherwise returns the number
  /// of ones from the least significant bit to the first zero bit.
  unsigned countr_one() const {
    if (isSingleWord())
      return llvm::countr_one(U.VAL);
    return countTrailingOnesSlowCase();
  }

  /// Alias for countr_one().
  ///
  /// \returns The number of trailing one bits.
  unsigned countTrailingOnes() const { return countr_one(); }

  /// Count the number of bits set.
  ///
  /// This function is an APInt version of std::popcount. It counts the number
  /// of 1 bits in the APInt value.
  ///
  /// \returns 0 if the value is zero, otherwise returns the number of set bits.
  unsigned popcount() const {
    if (isSingleWord())
      return llvm::popcount(U.VAL);
    return countPopulationSlowCase();
  }

  /// @}
  /// \name Conversion Functions
  /// @{

  /// Print this APInt to a stream.
  ///
  /// \param OS Output stream
  /// \param isSigned If true, print as a signed value
  LLVM_ABI void print(raw_ostream &OS, bool isSigned) const;

  /// Convert this APInt to a string and append it to \p Str.
  ///
  /// \p Str is commonly a SmallString. If Radix > 10, UpperCase determines the
  /// case of letter digits.
  ///
  /// \param Str Destination string buffer
  /// \param Radix Numeric base (2, 8, 10, 16, or 36)
  /// \param Signed If true, treat the value as signed
  /// \param formatAsCLiteral If true, emit a C-style literal prefix/suffix
  /// \param UpperCase If true, use uppercase letters for digits above 9
  /// \param InsertSeparators If true, insert digit-group separators
  LLVM_ABI void toString(SmallVectorImpl<char> &Str, unsigned Radix,
                         bool Signed, bool formatAsCLiteral = false,
                         bool UpperCase = true,
                         bool InsertSeparators = false) const;

  /// Convert this APInt as unsigned into a string in the given radix.
  ///
  /// The radix can be 2, 8, 10, 16, or 36.
  ///
  /// \param Str Destination string buffer
  /// \param Radix Numeric base
  void toStringUnsigned(SmallVectorImpl<char> &Str, unsigned Radix = 10) const {
    toString(Str, Radix, false, false);
  }

  /// Convert this APInt as signed into a string in the given radix.
  ///
  /// The radix can be 2, 8, 10, 16, or 36.
  ///
  /// \param Str Destination string buffer
  /// \param Radix Numeric base
  void toStringSigned(SmallVectorImpl<char> &Str, unsigned Radix = 10) const {
    toString(Str, Radix, true, false);
  }

  /// Return a byte-swapped representation of this APInt.
  ///
  /// \returns A byte-swapped copy of this APInt.
  LLVM_ABI APInt byteSwap() const;

  /// Return this APInt with its bit representation reversed.
  ///
  /// \returns This APInt with its bits reversed.
  LLVM_ABI APInt reverseBits() const;

  /// Convert this APInt to a double value.
  ///
  /// \param isSigned If true, treat this APInt as signed
  /// \returns This APInt converted to double.
  LLVM_ABI double roundToDouble(bool isSigned) const;

  /// Converts this unsigned APInt to a double value.
  ///
  /// \returns This unsigned APInt converted to double.
  double roundToDouble() const { return roundToDouble(false); }

  /// Converts this signed APInt to a double value.
  ///
  /// \returns This signed APInt converted to double.
  double signedRoundToDouble() const { return roundToDouble(true); }

  /// Converts APInt bits to a double
  ///
  /// The conversion does not do a translation from integer to double, it just
  /// re-interprets the bits as a double. Note that it is valid to do this on
  /// any bit width. Exactly 64 bits will be translated.
  ///
  /// \returns The low 64 bits of this APInt reinterpreted as a double.
  double bitsToDouble() const { return llvm::bit_cast<double>(getWord(0)); }

#ifdef HAS_IEE754_FLOAT128
  float128 bitsToQuad() const {
    __uint128_t ul = ((__uint128_t)U.pVal[1] << 64) + U.pVal[0];
    return llvm::bit_cast<float128>(ul);
  }
#endif

  /// Converts APInt bits to a float
  ///
  /// The conversion does not do a translation from integer to float, it just
  /// re-interprets the bits as a float. Note that it is valid to do this on
  /// any bit width. Exactly 32 bits will be translated.
  ///
  /// \returns The low 32 bits of this APInt reinterpreted as a float.
  float bitsToFloat() const {
    return llvm::bit_cast<float>(static_cast<uint32_t>(getWord(0)));
  }

  /// Reinterpret the bits of a double as an APInt.
  ///
  /// The conversion does not translate from double to integer; it just
  /// re-interprets the bits of the double.
  ///
  /// \param V Double whose bit pattern is copied
  /// \returns An APInt holding the bit pattern of \p V.
  static APInt doubleToBits(double V) {
    return APInt(sizeof(double) * CHAR_BIT, llvm::bit_cast<uint64_t>(V));
  }

  /// Reinterpret the bits of a float as an APInt.
  ///
  /// The conversion does not translate from float to integer; it just
  /// re-interprets the bits of the float.
  ///
  /// \param V Float whose bit pattern is copied
  /// \returns An APInt holding the bit pattern of \p V.
  static APInt floatToBits(float V) {
    return APInt(sizeof(float) * CHAR_BIT, llvm::bit_cast<uint32_t>(V));
  }

  /// @}
  /// \name Mathematics Operations
  /// @{

  /// Return the floor of the log base 2 of this APInt.
  ///
  /// \returns The floor of the log base 2 of this APInt.
  unsigned logBase2() const { return getActiveBits() - 1; }

  /// Return the ceil of the log base 2 of this APInt.
  ///
  /// \returns The ceil of the log base 2 of this APInt.
  unsigned ceilLogBase2() const {
    APInt temp(*this);
    --temp;
    return temp.getActiveBits();
  }

  /// \returns the nearest log base 2 of this APInt. Ties round up.
  ///
  /// NOTE: When we have a BitWidth of 1, we define:
  ///
  ///   log2(0) = UINT32_MAX
  ///   log2(1) = 0
  ///
  /// to get around any mathematical concerns resulting from
  /// referencing 2 in a space where 2 does no exist.
  LLVM_ABI unsigned nearestLogBase2() const;

  /// Return log base 2 if this APInt is an exact power of two, otherwise -1.
  ///
  /// \returns Floor log base 2 if this is a power of two, otherwise -1.
  int32_t exactLogBase2() const {
    if (!isPowerOf2())
      return -1;
    return logBase2();
  }

  /// Compute the floor of the square root of the unsigned value.
  ///
  /// \returns The floor of the square root of this unsigned APInt.
  LLVM_ABI APInt sqrtFloor() const;

  /// Return the absolute value of this APInt.
  ///
  /// If *this is < 0 then return -(*this), otherwise *this. Note that the
  /// "most negative" signed number (e.g. -128 for 8 bit wide APInt) is
  /// unchanged due to how negation works.
  ///
  /// \returns The absolute value of this APInt.
  APInt abs() const {
    if (isNegative())
      return -(*this);
    return *this;
  }

  /// Return the multiplicative inverse of an odd APInt modulo 2^BitWidth.
  ///
  /// \returns The multiplicative inverse of this odd APInt modulo 2^BitWidth.
  LLVM_ABI APInt multiplicativeInverse() const;

  /// @}
  /// \name Building-block Operations for APInt and APFloat
  /// @{

  // These building block operations operate on a representation of arbitrary
  // precision, two's-complement, bignum integer values. They should be
  // sufficient to implement APInt and APFloat bignum requirements. Inputs are
  // generally a pointer to the base of an array of integer parts, representing
  // an unsigned bignum, and a count of how many parts there are.

  /// Set the least significant limb of a bignum and zero higher limbs.
  ///
  /// \param dst Destination limb array
  /// \param part Value for the least significant limb
  /// \param parts Number of limbs in \p dst
  LLVM_ABI static void tcSet(WordType *dst, WordType part, unsigned parts);

  /// Assign one bignum to another.
  ///
  /// \param dst Destination limb array
  /// \param src Source limb array
  /// \param parts Number of limbs to copy
  LLVM_ABI static void tcAssign(WordType *dst, const WordType *src,
                                unsigned parts);

  /// Return true if a bignum is zero.
  ///
  /// \param parts Limb array of the bignum
  /// \param n Number of limbs in \p parts
  /// \returns True if the bignum is zero.
  LLVM_ABI static bool tcIsZero(const WordType *parts, unsigned n);

  /// Extract the given zero-based bit of a bignum; returns 0 or 1.
  ///
  /// \param parts Limb array of the bignum
  /// \param bit Zero-based bit index
  /// \returns 0 or 1 for the bit at \p bit.
  LLVM_ABI static int tcExtractBit(const WordType *parts, unsigned bit);

  /// Extract a contiguous bit-field from one bignum into another.
  ///
  /// Copy the bit vector of width srcBITS from SRC, starting at bit srcLSB, to
  /// DST, of dstCOUNT parts, such that the bit srcLSB becomes the least
  /// significant bit of DST. All high bits above srcBITS in DST are
  /// zero-filled.
  ///
  /// \param dst Destination limb array
  /// \param dstCount Number of limbs in \p dst
  /// \param src Source limb array
  /// \param srcBits Width of the bit-field to copy
  /// \param srcLSB Least-significant bit index of the field in \p src
  LLVM_ABI static void tcExtract(WordType *dst, unsigned dstCount,
                                 const WordType *src, unsigned srcBits,
                                 unsigned srcLSB);

  /// Set the given zero-based bit of a bignum.
  ///
  /// \param parts Limb array to modify
  /// \param bit Zero-based bit index to set
  LLVM_ABI static void tcSetBit(WordType *parts, unsigned bit);

  /// Clear the given zero-based bit of a bignum.
  ///
  /// \param parts Limb array to modify
  /// \param bit Zero-based bit index to clear
  LLVM_ABI static void tcClearBit(WordType *parts, unsigned bit);

  /// Return the bit index of the least significant set bit in a bignum.
  ///
  /// If the input number has no bits set, -1U is returned.
  ///
  /// \param parts Limb array of the bignum
  /// \param n Number of limbs in \p parts
  /// \returns Bit index of the LSB, or -1U if no bits are set.
  LLVM_ABI static unsigned tcLSB(const WordType *parts, unsigned n);
  /// Return the bit index of the most significant set bit in \p parts.
  ///
  /// \param parts limb array of the bignum
  /// \param n number of limbs in \p parts
  /// \returns bit index of the MSB, or -1U if no bits are set
  LLVM_ABI static unsigned tcMSB(const WordType *parts, unsigned n);

  /// Negate a bignum in place (two's complement).
  ///
  /// \param dst Limb array to negate
  /// \param parts Number of limbs in \p dst
  LLVM_ABI static void tcNegate(WordType *dst, unsigned parts);

  /// Add two bignums with an incoming carry and return the outgoing carry.
  ///
  /// Computes DST += RHS + CARRY where CARRY is zero or one.
  ///
  /// \param dst Destination / left-hand limb array (updated in place)
  /// \param rhs Right-hand limb array
  /// \param carry Incoming carry (0 or 1)
  /// \param parts Number of limbs in each operand
  /// \returns Outgoing carry flag
  LLVM_ABI static WordType tcAdd(WordType *dst, const WordType *rhs,
                                 WordType carry, unsigned parts);
  /// Add a single limb to a bignum and return the carry flag.
  ///
  /// \param dst Destination limb array (updated in place)
  /// \param rhs Single limb to add
  /// \param parts Number of limbs in \p dst
  /// \returns Outgoing carry flag
  LLVM_ABI static WordType tcAddPart(WordType *dst, WordType rhs,
                                     unsigned parts);

  /// Subtract a bignum plus carry from another and return the borrow flag.
  ///
  /// Computes DST -= RHS + CARRY where CARRY is zero or one.
  ///
  /// \param dst Destination / left-hand limb array (updated in place)
  /// \param rhs Right-hand limb array
  /// \param carry Incoming borrow (0 or 1)
  /// \param parts Number of limbs in each operand
  /// \returns Outgoing borrow flag
  LLVM_ABI static WordType tcSubtract(WordType *dst, const WordType *rhs,
                                      WordType carry, unsigned parts);
  /// Subtract a single limb from a bignum and return the borrow flag.
  ///
  /// \param dst Destination limb array (updated in place)
  /// \param rhs Single limb to subtract
  /// \param parts Number of limbs in \p dst
  /// \returns Outgoing borrow flag
  LLVM_ABI static WordType tcSubtractPart(WordType *dst, WordType rhs,
                                          unsigned parts);

  /// Multiply a bignum by a single limb, optionally adding into the destination.
  ///
  /// DST += SRC * MULTIPLIER + PART   if add is true
  /// DST  = SRC * MULTIPLIER + PART   if add is false
  ///
  /// Requires 0 <= DSTPARTS <= SRCPARTS + 1. If DST overlaps SRC they must
  /// start at the same point, i.e. DST == SRC.
  ///
  /// If DSTPARTS == SRC_PARTS + 1 no overflow occurs and zero is returned.
  /// Otherwise DST is filled with the least significant DSTPARTS parts of the
  /// result, and if all of the omitted higher parts were zero return zero,
  /// otherwise overflow occurred and return one.
  ///
  /// \param dst Destination limb array
  /// \param src Source limb array
  /// \param multiplier Single-limb multiplier
  /// \param carry Initial carry / additive part
  /// \param srcParts Number of limbs in \p src
  /// \param dstParts Number of limbs in \p dst
  /// \param add If true, add the product into \p dst; otherwise overwrite
  /// \returns 1 if overflow occurred, otherwise 0
  LLVM_ABI static int tcMultiplyPart(WordType *dst, const WordType *src,
                                     WordType multiplier, WordType carry,
                                     unsigned srcParts, unsigned dstParts,
                                     bool add);

  /// Multiply two equal-width bignums into a same-width destination.
  ///
  /// DST = LHS * RHS, where DST has the same width as the operands and is
  /// filled with the least significant parts of the result. Returns one if
  /// overflow occurred, otherwise zero. DST must be disjoint from both
  /// operands.
  ///
  /// \param dst Destination limb array (same width as operands)
  /// \param lhs Left-hand limb array
  /// \param rhs Right-hand limb array
  /// \param parts Number of limbs in each operand and in \p dst
  /// \returns 1 if overflow occurred, otherwise 0
  LLVM_ABI static int tcMultiply(WordType *dst, const WordType *lhs,
                                 const WordType *rhs, unsigned parts);

  /// Multiply two bignums into a full-width (sum of widths) destination.
  ///
  /// DST = LHS * RHS, where DST has width the sum of the widths of the
  /// operands. No overflow occurs. DST must be disjoint from both operands.
  ///
  /// \param dst Destination limb array (lhsParts + rhsParts wide)
  /// \param lhs Left-hand limb array
  /// \param rhs Right-hand limb array
  /// \param lhsParts Number of limbs in \p lhs
  /// \param rhsParts Number of limbs in \p rhs
  LLVM_ABI static void tcFullMultiply(WordType *dst, const WordType *lhs,
                                      const WordType *rhs, unsigned lhsParts,
                                      unsigned rhsParts);

  /// Divide bignums: set \p lhs to quotient and \p remainder to the remainder.
  ///
  /// If RHS is zero, LHS and REMAINDER are left unchanged and one is returned.
  /// Otherwise set LHS to LHS / RHS with the fractional part discarded, set
  /// REMAINDER to the remainder, and return zero. i.e.
  ///
  ///  OLD_LHS = RHS * LHS + REMAINDER
  ///
  /// SCRATCH is a bignum of the same size as the operands and result for use by
  /// the routine; its contents need not be initialized and are destroyed. LHS,
  /// REMAINDER and SCRATCH must be distinct.
  ///
  /// \param lhs Dividend in, quotient out
  /// \param rhs Divisor
  /// \param remainder Remainder output
  /// \param scratch Scratch limb array (destroyed)
  /// \param parts Number of limbs in each bignum
  /// \returns 0 on success, 1 if \p rhs is zero
  LLVM_ABI static int tcDivide(WordType *lhs, const WordType *rhs,
                               WordType *remainder, WordType *scratch,
                               unsigned parts);

  /// Shift a bignum left by \p Count bits; shifted-in bits are zero.
  ///
  /// There are no restrictions on \p Count.
  ///
  /// \param dst Limb array to shift in place
  /// \param Words Number of limbs in \p dst
  /// \param Count Number of bits to shift left
  LLVM_ABI static void tcShiftLeft(WordType *dst, unsigned Words,
                                   unsigned Count);

  /// Shift a bignum right by \p Count bits; shifted-in bits are zero.
  ///
  /// There are no restrictions on \p Count.
  ///
  /// \param dst Limb array to shift in place
  /// \param Words Number of limbs in \p dst
  /// \param Count Number of bits to shift right
  LLVM_ABI static void tcShiftRight(WordType *dst, unsigned Words,
                                    unsigned Count);

  /// Compare two bignums as unsigned values.
  ///
  /// \param lhs Left-hand limb array
  /// \param rhs Right-hand limb array
  /// \param parts Number of limbs in each operand
  /// \returns -1, 0, or 1 if \p lhs is less than, equal to, or greater than \p rhs
  LLVM_ABI static int tcCompare(const WordType *lhs, const WordType *rhs,
                                unsigned parts);

  /// Increment a bignum in-place and return the carry flag.
  ///
  /// \param dst Limb array to increment
  /// \param parts Number of limbs in \p dst
  /// \returns Outgoing carry flag.
  static WordType tcIncrement(WordType *dst, unsigned parts) {
    return tcAddPart(dst, 1, parts);
  }

  /// Decrement a bignum in-place and return the borrow flag.
  ///
  /// \param dst Limb array to decrement
  /// \param parts Number of limbs in \p dst
  /// \returns Outgoing borrow flag.
  static WordType tcDecrement(WordType *dst, unsigned parts) {
    return tcSubtractPart(dst, 1, parts);
  }

  /// Profile this APInt into a FoldingSet node ID.
  ///
  /// Used to insert APInt objects, or objects that contain APInt objects, into
  /// FoldingSets.
  ///
  /// \param id FoldingSet node ID to update
  LLVM_ABI void Profile(FoldingSetNodeID &id) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// debug method
  LLVM_DUMP_METHOD void dump() const;
#endif

  /// Returns whether this instance allocated memory.
  ///
  /// \returns True if this APInt owns heap storage.
  bool needsCleanup() const { return !isSingleWord(); }

private:
  /// This union is used to store the integer value. When the
  /// integer bit-width <= 64, it uses VAL, otherwise it uses pVal.
  union {
    uint64_t VAL;   ///< Used to store the <= 64 bits integer value.
    uint64_t *pVal; ///< Used to store the >64 bits integer value.
  } U;

  unsigned BitWidth = 1; ///< The number of bits in this APInt.

  friend struct DenseMapInfo<APInt, void>;
  friend class APSInt;

  // Make DynamicAPInt a friend so it can access BitWidth directly.
  friend DynamicAPInt;

  /// This constructor is used only internally for speed of construction of
  /// temporaries. It is unsafe since it takes ownership of the pointer, so it
  /// is not public.
  APInt(uint64_t *val, unsigned bits) : BitWidth(bits) { U.pVal = val; }

  /// Determine which word a bit is in.
  ///
  /// \returns the word position for the specified bit position.
  static unsigned whichWord(unsigned bitPosition) {
    return bitPosition / APINT_BITS_PER_WORD;
  }

  /// Determine which bit in a word the specified bit position is in.
  static unsigned whichBit(unsigned bitPosition) {
    return bitPosition % APINT_BITS_PER_WORD;
  }

  /// Get a single bit mask.
  ///
  /// \returns a uint64_t with only bit at "whichBit(bitPosition)" set
  /// This method generates and returns a uint64_t (word) mask for a single
  /// bit at a specific bit position. This is used to mask the bit in the
  /// corresponding word.
  static uint64_t maskBit(unsigned bitPosition) {
    return 1ULL << whichBit(bitPosition);
  }

  /// Clear unused high order bits
  ///
  /// This method is used internally to clear the top "N" bits in the high order
  /// word that are not used by the APInt. This is needed after the most
  /// significant word is assigned a value to ensure that those bits are
  /// zero'd out.
  APInt &clearUnusedBits() {
    // Compute how many bits are used in the final word.
    unsigned WordBits = ((BitWidth - 1) % APINT_BITS_PER_WORD) + 1;

    // Mask out the high bits.
    uint64_t mask = WORDTYPE_MAX >> (APINT_BITS_PER_WORD - WordBits);
    if (LLVM_UNLIKELY(BitWidth == 0))
      mask = 0;

    if (isSingleWord())
      U.VAL &= mask;
    else
      U.pVal[getNumWords() - 1] &= mask;
    return *this;
  }

  /// Get the word corresponding to a bit position
  /// \returns the corresponding word for the specified bit position.
  uint64_t getWord(unsigned bitPosition) const {
    return isSingleWord() ? U.VAL : U.pVal[whichWord(bitPosition)];
  }

  /// Utility method to change the bit width of this APInt to new bit width,
  /// allocating and/or deallocating as necessary. There is no guarantee on the
  /// value of any bits upon return. Caller should populate the bits after.
  void reallocate(unsigned NewBitWidth);

  /// Convert a char array into an APInt
  ///
  /// \param radix 2, 8, 10, 16, or 36
  /// Converts a string into a number.  The string must be non-empty
  /// and well-formed as a number of the given base. The bit-width
  /// must be sufficient to hold the result.
  ///
  /// This is used by the constructors that take string arguments.
  ///
  /// StringRef::getAsInteger is superficially similar but (1) does
  /// not assume that the string is well-formed and (2) grows the
  /// result to hold the input.
  void fromString(unsigned numBits, StringRef str, uint8_t radix);

  /// An internal division function for dividing APInts.
  ///
  /// This is used by the toString method to divide by the radix. It simply
  /// provides a more convenient form of divide for internal use since KnuthDiv
  /// has specific constraints on its inputs. If those constraints are not met
  /// then it provides a simpler form of divide.
  static void divide(const WordType *LHS, unsigned lhsWords,
                     const WordType *RHS, unsigned rhsWords, WordType *Quotient,
                     WordType *Remainder);

  /// out-of-line slow case for inline constructor
  LLVM_ABI void initSlowCase(uint64_t val, bool isSigned);

  /// shared code between two array constructors
  void initFromArray(ArrayRef<uint64_t> array);

  /// out-of-line slow case for inline copy constructor
  LLVM_ABI void initSlowCase(const APInt &that);

  /// out-of-line slow case for shl
  LLVM_ABI void shlSlowCase(unsigned ShiftAmt);

  /// out-of-line slow case for lshr.
  LLVM_ABI void lshrSlowCase(unsigned ShiftAmt);

  /// out-of-line slow case for ashr.
  LLVM_ABI void ashrSlowCase(unsigned ShiftAmt);

  /// out-of-line slow case for operator=
  LLVM_ABI void assignSlowCase(const APInt &RHS);

  /// out-of-line slow case for operator==
  LLVM_ABI bool equalSlowCase(const APInt &RHS) const LLVM_READONLY;

  /// out-of-line slow case for countLeadingZeros
  LLVM_ABI unsigned countLeadingZerosSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countLeadingOnes.
  LLVM_ABI unsigned countLeadingOnesSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countTrailingZeros.
  LLVM_ABI unsigned countTrailingZerosSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countTrailingOnes
  LLVM_ABI unsigned countTrailingOnesSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for countPopulation
  LLVM_ABI unsigned countPopulationSlowCase() const LLVM_READONLY;

  /// out-of-line slow case for isPowerOf2
  LLVM_ABI bool isPowerOf2SlowCase() const LLVM_READONLY;

  /// out-of-line slow case for intersects.
  LLVM_ABI bool intersectsSlowCase(const APInt &RHS) const LLVM_READONLY;

  /// out-of-line slow case for isSubsetOf.
  LLVM_ABI bool isSubsetOfSlowCase(const APInt &RHS) const LLVM_READONLY;

  /// out-of-line slow case for isInverseOf.
  LLVM_ABI bool isInverseOfSlowCase(const APInt &RHS) const LLVM_READONLY;

  /// out-of-line slow case for setBits.
  LLVM_ABI void setBitsSlowCase(unsigned loBit, unsigned hiBit);

  /// out-of-line slow case for clearBits.
  LLVM_ABI void clearBitsSlowCase(unsigned LoBit, unsigned HiBit);

  /// out-of-line slow case for flipAllBits.
  LLVM_ABI void flipAllBitsSlowCase();

  /// out-of-line slow case for concat.
  LLVM_ABI APInt concatSlowCase(const APInt &NewLSB) const;

  /// out-of-line slow case for operator&=.
  LLVM_ABI void andAssignSlowCase(const APInt &RHS);

  /// out-of-line slow case for operator|=.
  LLVM_ABI void orAssignSlowCase(const APInt &RHS);

  /// out-of-line slow case for operator^=.
  LLVM_ABI void xorAssignSlowCase(const APInt &RHS);

  /// Unsigned comparison. Returns -1, 0, or 1 if this APInt is less than, equal
  /// to, or greater than RHS.
  LLVM_ABI int compare(const APInt &RHS) const LLVM_READONLY;

  /// Signed comparison. Returns -1, 0, or 1 if this APInt is less than, equal
  /// to, or greater than RHS.
  LLVM_ABI int compareSigned(const APInt &RHS) const LLVM_READONLY;

  /// @}
};

/// Compare a 64-bit value with an APInt for equality.
///
/// \param V1 Left-hand 64-bit value
/// \param V2 Right-hand APInt
/// \returns True if \p V1 equals \p V2.
inline bool operator==(uint64_t V1, const APInt &V2) { return V2 == V1; }

/// Compare a 64-bit value with an APInt for inequality.
///
/// \param V1 Left-hand 64-bit value
/// \param V2 Right-hand APInt
/// \returns True if \p V1 is not equal to \p V2.
inline bool operator!=(uint64_t V1, const APInt &V2) { return V2 != V1; }

/// Unary bitwise complement operator.
///
/// \param v Value to complement
/// \returns an APInt that is the bitwise complement of \p v
inline APInt operator~(APInt v) {
  v.flipAllBits();
  return v;
}

/// Bitwise AND of two APInts.
///
/// \param a Left-hand operand (taken by value)
/// \param b Right-hand operand
/// \returns The bitwise AND of \p a and \p b.
inline APInt operator&(APInt a, const APInt &b) {
  a &= b;
  return a;
}

/// Bitwise AND that prefers moving from \p b.
///
/// \param a Left-hand operand
/// \param b Right-hand operand (moved from)
/// \returns The bitwise AND of \p a and \p b.
inline APInt operator&(const APInt &a, APInt &&b) {
  b &= a;
  return std::move(b);
}

/// Bitwise AND of an APInt and a 64-bit value.
///
/// \param a Left-hand APInt
/// \param RHS Right-hand 64-bit value
/// \returns The bitwise AND of \p a and \p RHS.
inline APInt operator&(APInt a, uint64_t RHS) {
  a &= RHS;
  return a;
}

/// Bitwise AND of a 64-bit value and an APInt.
///
/// \param LHS Left-hand 64-bit value
/// \param b Right-hand APInt
/// \returns The bitwise AND of \p LHS and \p b.
inline APInt operator&(uint64_t LHS, APInt b) {
  b &= LHS;
  return b;
}

/// Bitwise OR of two APInts.
///
/// \param a Left-hand operand (taken by value)
/// \param b Right-hand operand
/// \returns The bitwise OR of \p a and \p b.
inline APInt operator|(APInt a, const APInt &b) {
  a |= b;
  return a;
}

/// Bitwise OR that prefers moving from \p b.
///
/// \param a Left-hand operand
/// \param b Right-hand operand (moved from)
/// \returns The bitwise OR of \p a and \p b.
inline APInt operator|(const APInt &a, APInt &&b) {
  b |= a;
  return std::move(b);
}

/// Bitwise OR of an APInt and a 64-bit value.
///
/// \param a Left-hand APInt
/// \param RHS Right-hand 64-bit value
/// \returns The bitwise OR of \p a and \p RHS.
inline APInt operator|(APInt a, uint64_t RHS) {
  a |= RHS;
  return a;
}

/// Bitwise OR of a 64-bit value and an APInt.
///
/// \param LHS Left-hand 64-bit value
/// \param b Right-hand APInt
/// \returns The bitwise OR of \p LHS and \p b.
inline APInt operator|(uint64_t LHS, APInt b) {
  b |= LHS;
  return b;
}

/// Bitwise XOR of two APInts.
///
/// \param a Left-hand operand (taken by value)
/// \param b Right-hand operand
/// \returns The bitwise XOR of \p a and \p b.
inline APInt operator^(APInt a, const APInt &b) {
  a ^= b;
  return a;
}

/// Bitwise XOR that prefers moving from \p b.
///
/// \param a Left-hand operand
/// \param b Right-hand operand (moved from)
/// \returns The bitwise XOR of \p a and \p b.
inline APInt operator^(const APInt &a, APInt &&b) {
  b ^= a;
  return std::move(b);
}

/// Bitwise XOR of an APInt and a 64-bit value.
///
/// \param a Left-hand APInt
/// \param RHS Right-hand 64-bit value
/// \returns The bitwise XOR of \p a and \p RHS.
inline APInt operator^(APInt a, uint64_t RHS) {
  a ^= RHS;
  return a;
}

/// Bitwise XOR of a 64-bit value and an APInt.
///
/// \param LHS Left-hand 64-bit value
/// \param b Right-hand APInt
/// \returns The bitwise XOR of \p LHS and \p b.
inline APInt operator^(uint64_t LHS, APInt b) {
  b ^= LHS;
  return b;
}

/// Print \p I to \p OS as a signed decimal.
///
/// \param OS Output stream
/// \param I Value to print
/// \returns Reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const APInt &I) {
  I.print(OS, true);
  return OS;
}

/// Negate an APInt.
///
/// \param v Value to negate
/// \returns The negation of \p v.
inline APInt operator-(APInt v) {
  v.negate();
  return v;
}

/// Add two APInts.
///
/// \param a Left-hand operand (taken by value)
/// \param b Right-hand operand
/// \returns The sum of \p a and \p b.
inline APInt operator+(APInt a, const APInt &b) {
  a += b;
  return a;
}

/// Add that prefers moving from \p b.
///
/// \param a Left-hand operand
/// \param b Right-hand operand (moved from)
/// \returns The sum of \p a and \p b.
inline APInt operator+(const APInt &a, APInt &&b) {
  b += a;
  return std::move(b);
}

/// Add an APInt and a 64-bit value.
///
/// \param a Left-hand APInt
/// \param RHS Right-hand 64-bit value
/// \returns The sum of \p a and \p RHS.
inline APInt operator+(APInt a, uint64_t RHS) {
  a += RHS;
  return a;
}

/// Add a 64-bit value and an APInt.
///
/// \param LHS Left-hand 64-bit value
/// \param b Right-hand APInt
/// \returns The sum of \p LHS and \p b.
inline APInt operator+(uint64_t LHS, APInt b) {
  b += LHS;
  return b;
}

/// Subtract two APInts.
///
/// \param a Left-hand operand (taken by value)
/// \param b Right-hand operand
/// \returns The difference of \p a and \p b.
inline APInt operator-(APInt a, const APInt &b) {
  a -= b;
  return a;
}

/// Subtract that prefers moving from \p b.
///
/// \param a Left-hand operand
/// \param b Right-hand operand (moved from after negation)
/// \returns The difference of \p a and \p b.
inline APInt operator-(const APInt &a, APInt &&b) {
  b.negate();
  b += a;
  return std::move(b);
}

/// Subtract a 64-bit value from an APInt.
///
/// \param a Left-hand APInt
/// \param RHS Right-hand 64-bit value
/// \returns The difference of \p a and \p RHS.
inline APInt operator-(APInt a, uint64_t RHS) {
  a -= RHS;
  return a;
}

/// Subtract an APInt from a 64-bit value.
///
/// \param LHS Left-hand 64-bit value
/// \param b Right-hand APInt
/// \returns The difference of \p LHS and \p b.
inline APInt operator-(uint64_t LHS, APInt b) {
  b.negate();
  b += LHS;
  return b;
}

/// Multiply an APInt by a 64-bit value.
///
/// \param a Left-hand APInt
/// \param RHS Right-hand 64-bit value
/// \returns The product of \p a and \p RHS.
inline APInt operator*(APInt a, uint64_t RHS) {
  a *= RHS;
  return a;
}

/// Multiply a 64-bit value by an APInt.
///
/// \param LHS Left-hand 64-bit value
/// \param b Right-hand APInt
/// \returns The product of \p LHS and \p b.
inline APInt operator*(uint64_t LHS, APInt b) {
  b *= LHS;
  return b;
}

/// Additional APInt utility operations.
namespace APIntOps {

/// Determine the smaller of two APInts considered to be signed.
///
/// \param A First value
/// \param B Second value
/// \returns The smaller of \p A and \p B as signed values.
inline const APInt &smin(const APInt &A, const APInt &B) {
  return A.slt(B) ? A : B;
}

/// Determine the larger of two APInts considered to be signed.
///
/// \param A First value
/// \param B Second value
/// \returns The larger of \p A and \p B as signed values.
inline const APInt &smax(const APInt &A, const APInt &B) {
  return A.sgt(B) ? A : B;
}

/// Determine the smaller of two APInts considered to be unsigned.
///
/// \param A First value
/// \param B Second value
/// \returns The smaller of \p A and \p B as unsigned values.
inline const APInt &umin(const APInt &A, const APInt &B) {
  return A.ult(B) ? A : B;
}

/// Determine the larger of two APInts considered to be unsigned.
///
/// \param A First value
/// \param B Second value
/// \returns The larger of \p A and \p B as unsigned values.
inline const APInt &umax(const APInt &A, const APInt &B) {
  return A.ugt(B) ? A : B;
}

/// Determine the absolute difference of two APInts considered to be signed.
///
/// \param A First value
/// \param B Second value
/// \returns The absolute difference of \p A and \p B as signed values.
inline APInt abds(const APInt &A, const APInt &B) {
  return A.sge(B) ? (A - B) : (B - A);
}

/// Determine the absolute difference of two APInts considered to be unsigned.
///
/// \param A First value
/// \param B Second value
/// \returns The absolute difference of \p A and \p B as unsigned values.
inline APInt abdu(const APInt &A, const APInt &B) {
  return A.uge(B) ? (A - B) : (B - A);
}

/// Compute the floor of the signed average of \p C1 and \p C2.
///
/// \param C1 First operand
/// \param C2 Second operand
/// \returns The floor of the signed average of \p C1 and \p C2.
LLVM_ABI APInt avgFloorS(const APInt &C1, const APInt &C2);

/// Compute the floor of the unsigned average of \p C1 and \p C2.
///
/// \param C1 First operand
/// \param C2 Second operand
/// \returns The floor of the unsigned average of \p C1 and \p C2.
LLVM_ABI APInt avgFloorU(const APInt &C1, const APInt &C2);

/// Compute the ceil of the signed average of \p C1 and \p C2.
///
/// \param C1 First operand
/// \param C2 Second operand
/// \returns The ceil of the signed average of \p C1 and \p C2.
LLVM_ABI APInt avgCeilS(const APInt &C1, const APInt &C2);

/// Compute the ceil of the unsigned average of \p C1 and \p C2.
///
/// \param C1 First operand
/// \param C2 Second operand
/// \returns The ceil of the unsigned average of \p C1 and \p C2.
LLVM_ABI APInt avgCeilU(const APInt &C1, const APInt &C2);

/// Multiply sign-extended operands and return the high N bits of the product.
///
/// \param C1 First N-bit operand (sign-extended to 2N)
/// \param C2 Second N-bit operand (sign-extended to 2N)
/// \returns The high N bits of the product of sign-extended \p C1 and \p C2.
LLVM_ABI APInt mulhs(const APInt &C1, const APInt &C2);

/// Multiply zero-extended operands and return the high N bits of the product.
///
/// \param C1 First N-bit operand (zero-extended to 2N)
/// \param C2 Second N-bit operand (zero-extended to 2N)
/// \returns The high N bits of the product of zero-extended \p C1 and \p C2.
LLVM_ABI APInt mulhu(const APInt &C1, const APInt &C2);

/// Multiply sign-extended operands and return the full 2N-bit product.
///
/// \param C1 First N-bit operand (sign-extended to 2N)
/// \param C2 Second N-bit operand (sign-extended to 2N)
/// \returns The full 2N-bit product of sign-extended \p C1 and \p C2.
LLVM_ABI APInt mulsExtended(const APInt &C1, const APInt &C2);

/// Multiply zero-extended operands and return the full 2N-bit product.
///
/// \param C1 First N-bit operand (zero-extended to 2N)
/// \param C2 Second N-bit operand (zero-extended to 2N)
/// \returns The full 2N-bit product of zero-extended \p C1 and \p C2.
LLVM_ABI APInt muluExtended(const APInt &C1, const APInt &C2);

/// Compute \p X raised to the power \p N for N >= 0.
///
/// 0^0 is supported and returns 1.
///
/// \param X Base value
/// \param N Non-negative exponent
/// \returns \p X raised to the power \p N.
LLVM_ABI APInt pow(const APInt &X, int64_t N);

/// Compute the GCD of two APInt values using Stein's algorithm.
///
/// \param A First value
/// \param B Second value
/// \param IsSigned If true, take absolute values of both arguments first
/// \returns the greatest common divisor of A and B
LLVM_ABI APInt GreatestCommonDivisor(APInt A, APInt B, bool IsSigned = false);

/// Convert \p APIVal to a double, treating it as unsigned.
///
/// \param APIVal Integer value to convert
/// \returns \p APIVal converted to double as an unsigned integer.
inline double RoundAPIntToDouble(const APInt &APIVal) {
  return APIVal.roundToDouble();
}

/// Convert \p APIVal to a double, treating it as signed.
///
/// \param APIVal Integer value to convert
/// \returns \p APIVal converted to double as a signed integer.
inline double RoundSignedAPIntToDouble(const APInt &APIVal) {
  return APIVal.signedRoundToDouble();
}

/// Convert \p APIVal to a float, treating it as unsigned.
///
/// \param APIVal Integer value to convert
/// \returns \p APIVal converted to float as an unsigned integer.
inline float RoundAPIntToFloat(const APInt &APIVal) {
  return float(RoundAPIntToDouble(APIVal));
}

/// Convert \p APIVal to a float, treating it as signed.
///
/// \param APIVal Integer value to convert
/// \returns \p APIVal converted to float as a signed integer.
inline float RoundSignedAPIntToFloat(const APInt &APIVal) {
  return float(APIVal.signedRoundToDouble());
}

/// Convert a double value to an APInt of the given width.
///
/// \param Double Floating-point value to convert
/// \param width Bit width of the resulting APInt
/// \returns An APInt of \p width representing \p Double.
LLVM_ABI APInt RoundDoubleToAPInt(double Double, unsigned width);

/// Convert a float value to an APInt of the given width.
///
/// \param Float Floating-point value to convert
/// \param width Bit width of the resulting APInt
/// \returns An APInt of \p width representing \p Float.
inline APInt RoundFloatToAPInt(float Float, unsigned width) {
  return RoundDoubleToAPInt(double(Float), width);
}

/// Return \p A unsigned-divided by \p B, rounded by \p RM.
///
/// \param A Dividend
/// \param B Divisor
/// \param RM Rounding mode
/// \returns \p A unsigned-divided by \p B, rounded per \p RM.
LLVM_ABI APInt RoundingUDiv(const APInt &A, const APInt &B, APInt::Rounding RM);

/// Return \p A signed-divided by \p B, rounded by \p RM.
///
/// \param A Dividend
/// \param B Divisor
/// \param RM Rounding mode
/// \returns \p A signed-divided by \p B, rounded per \p RM.
LLVM_ABI APInt RoundingSDiv(const APInt &A, const APInt &B, APInt::Rounding RM);

/// Find the smallest n where quadratic q(n) = An^2 + Bn + C wraps the value range.
///
/// Let q(n) = An^2 + Bn + C, and BW = bit width of the value range
/// (e.g. 32 for i32).
/// This function finds the smallest number n, such that
/// (a) n >= 0 and q(n) = 0, or
/// (b) n >= 1 and q(n-1) and q(n), when evaluated in the set of all
///     integers, belong to two different intervals [Rk, Rk+R),
///     where R = 2^BW, and k is an integer.
/// The idea here is to find when q(n) "overflows" 2^BW, while at the
/// same time "allowing" subtraction. In unsigned modulo arithmetic a
/// subtraction (treated as addition of negated numbers) would always
/// count as an overflow, but here we want to allow values to decrease
/// and increase as long as they are within the same interval.
/// Specifically, adding of two negative numbers should not cause an
/// overflow (as long as the magnitude does not exceed the bit width).
/// On the other hand, given a positive number, adding a negative
/// number to it can give a negative result, which would cause the
/// value to go from [-2^BW, 0) to [0, 2^BW). In that sense, zero is
/// treated as a special case of an overflow.
///
/// This function returns std::nullopt if after finding k that minimizes the
/// positive solution to q(n) = kR, both solutions are contained between
/// two consecutive integers.
///
/// There are cases where q(n) > T, and q(n+1) < T (assuming evaluation
/// in arithmetic modulo 2^BW, and treating the values as signed) by the
/// virtue of *signed* overflow. This function will *not* find such an n,
/// however it may find a value of n satisfying the inequalities due to
/// an *unsigned* overflow (if the values are treated as unsigned).
/// To find a solution for a signed overflow, treat it as a problem of
/// finding an unsigned overflow with a range with of BW-1.
///
/// The returned value may have a different bit width from the input
/// coefficients.
///
/// \param A Quadratic coefficient of n^2
/// \param B Linear coefficient of n
/// \param C Constant term
/// \param RangeWidth Bit width BW of the modular value range
/// \returns The smallest wrapping n, or std::nullopt if none exists.
LLVM_ABI std::optional<APInt>
SolveQuadraticEquationWrap(APInt A, APInt B, APInt C, unsigned RangeWidth);

/// Return the index of the most significant bit that differs between two values.
///
/// \param A First value
/// \param B Second value
/// \returns Bit index of the highest differing bit, or std::nullopt if equal
LLVM_ABI std::optional<unsigned> GetMostSignificantDifferentBit(const APInt &A,
                                                                const APInt &B);

/// Splat/merge neighboring bits to widen or narrow the bitmask in \p A.
///
/// MatchAnyBits (default):
/// e.g. ScaleBitMask(0b0101, 8) -> 0b00110011
/// e.g. ScaleBitMask(0b00011011, 4) -> 0b0111
///
/// MatchAllBits:
/// e.g. ScaleBitMask(0b0101, 8) -> 0b00110011
/// e.g. ScaleBitMask(0b00011011, 4) -> 0b0001
///
/// \p A.getBitWidth() or \p NewBitWidth must be a whole multiple of the other.
///
/// \param A Source bitmask
/// \param NewBitWidth Desired bit width of the result
/// \param MatchAllBits If true, require all mapped source bits set when
///        narrowing; otherwise any set bit suffices
/// \returns \p A rescaled to \p NewBitWidth by splat/merge of neighboring bits.
LLVM_ABI APInt ScaleBitMask(const APInt &A, unsigned NewBitWidth,
                            bool MatchAllBits = false);

/// Perform a funnel shift left.
///
/// Concatenate Hi and Lo (Hi is the most significant bits of the wide value),
/// the combined value is shifted left by Shift (modulo the bit width of the
/// original arguments), and the most significant bits are extracted to produce
/// a result that is the same size as the original arguments.
///
/// Examples:
/// (1) fshl(i8 255, i8 0, i8 15) = 128 (0b10000000)
/// (2) fshl(i8 15, i8 15, i8 11) = 120 (0b01111000)
/// (3) fshl(i8 0, i8 255, i8 8)  = 0   (0b00000000)
/// (4) fshl(i8 255, i8 0, i8 15) = fshl(i8 255, i8 0, i8 7) // 15 % 8
///
/// \param Hi High half of the concatenated value
/// \param Lo Low half of the concatenated value
/// \param Shift Funnel shift amount (modulo bit width)
/// \returns The high bits after a funnel shift left of \p Hi and \p Lo.
LLVM_ABI APInt fshl(const APInt &Hi, const APInt &Lo, const APInt &Shift);

/// Perform a funnel shift right.
///
/// Concatenate Hi and Lo (Hi is the most significant bits of the wide value),
/// the combined value is shifted right by Shift (modulo the bit width of the
/// original arguments), and the least significant bits are extracted to produce
/// a result that is the same size as the original arguments.
///
/// Examples:
/// (1) fshr(i8 255, i8 0, i8 15) = 254 (0b11111110)
/// (2) fshr(i8 15, i8 15, i8 11) = 225 (0b11100001)
/// (3) fshr(i8 0, i8 255, i8 8)  = 255 (0b11111111)
/// (4) fshr(i8 255, i8 0, i8 9)  = fshr(i8 255, i8 0, i8 1) // 9 % 8
///
/// \param Hi High half of the concatenated value
/// \param Lo Low half of the concatenated value
/// \param Shift Funnel shift amount (modulo bit width)
/// \returns The low bits after a funnel shift right of \p Hi and \p Lo.
LLVM_ABI APInt fshr(const APInt &Hi, const APInt &Lo, const APInt &Shift);

/// Perform a carry-less multiply, also known as XOR multiplication, and return
/// low-bits. All arguments and result have the same bitwidth.
///
/// Examples:
/// (1) clmul(i4 1, i4 2)   = 2
/// (2) clmul(i4 5, i4 6)   = 14
/// (3) clmul(i4 -4, i4 2)  = -8
/// (4) clmul(i4 -4, i4 -5) = 4
///
/// \param LHS Left-hand operand
/// \param RHS Right-hand operand
/// \returns The low bits of the carry-less product of \p LHS and \p RHS.
LLVM_ABI APInt clmul(const APInt &LHS, const APInt &RHS);

/// Perform a reversed carry-less multiply.
///
/// clmulr(a, b) = bitreverse(clmul(bitreverse(a), bitreverse(b)))
///
/// \param LHS Left-hand operand
/// \param RHS Right-hand operand
/// \returns The reversed carry-less product of \p LHS and \p RHS.
LLVM_ABI APInt clmulr(const APInt &LHS, const APInt &RHS);

/// Perform a carry-less multiply, and return high-bits. All arguments and
/// result have the same bitwidth.
///
/// clmulh(a, b) = clmulr(a, b) >> 1
///
/// \param LHS Left-hand operand
/// \param RHS Right-hand operand
/// \returns The high bits of the carry-less product of \p LHS and \p RHS.
LLVM_ABI APInt clmulh(const APInt &LHS, const APInt &RHS);

/// Perform a "compress" operation, also known as pext or bext.
///
/// Selects the bits from \p Val at the positions where \p Mask has a 1-bit,
/// and packs them contiguously into the least significant bits of the result.
///
/// Examples:
/// (1) pext(i8 0b1010'1010, i8 0b1100'1100) = 0b0000'1010
/// (2) pext(i8 0b1111'1111, i8 0b1010'1010) = 0b0000'1111
///
/// \param Val Source value whose bits are selected
/// \param Mask Bitmask selecting which bits of \p Val to pack
/// \returns The selected bits of \p Val packed into the low bits.
LLVM_ABI APInt pext(const APInt &Val, const APInt &Mask);

/// Perform an "expand" operation, also known as pdep or bdep.
///
/// Places the least significant bits of \p Val at the positions where \p Mask
/// has a 1-bit, and zeros the remaining bits.
///
/// Examples:
/// (1) pdep(i8 0b0000'1010, i8 0b1100'1100) = 0b1000'1000
/// (2) pdep(i8 0b0000'1111, i8 0b1010'1010) = 0b1010'1010
///
/// \param Val Source bits to scatter into mask positions
/// \param Mask Bitmask selecting destination bit positions
/// \returns \p Val's bits expanded into the positions selected by \p Mask.
LLVM_ABI APInt pdep(const APInt &Val, const APInt &Mask);

} // namespace APIntOps

// See friend declaration above. This additional declaration is required in
// order to compile LLVM with IBM xlC compiler.
LLVM_ABI hash_code hash_value(const APInt &Arg);

/// Fill \p StoreBytes bytes of memory starting at \p Dst with \p IntVal.
///
/// \param IntVal Integer value to store
/// \param Dst Destination buffer
/// \param StoreBytes Number of bytes to write
LLVM_ABI void StoreIntToMemory(const APInt &IntVal, uint8_t *Dst,
                               unsigned StoreBytes);

/// Load \p LoadBytes bytes from \p Src into \p IntVal.
///
/// \p IntVal is assumed to be wide enough and initially zero.
///
/// \param IntVal Destination APInt (must be wide enough and zeroed)
/// \param Src Source buffer
/// \param LoadBytes Number of bytes to read
LLVM_ABI void LoadIntFromMemory(APInt &IntVal, const uint8_t *Src,
                                unsigned LoadBytes);

/// Provide DenseMapInfo for APInt.
template <> struct DenseMapInfo<APInt, void> {
  /// Compute a hash code for \p Key.
  ///
  /// \param Key APInt to hash
  /// \returns Hash code for \p Key.
  LLVM_ABI static unsigned getHashValue(const APInt &Key);

  /// Return true if \p LHS and \p RHS are equal, including bit width.
  ///
  /// \param LHS First APInt
  /// \param RHS Second APInt
  /// \returns True if \p LHS and \p RHS are equal, including bit width.
  static bool isEqual(const APInt &LHS, const APInt &RHS) {
    return LHS.getBitWidth() == RHS.getBitWidth() && LHS == RHS;
  }
};

} // namespace llvm

#endif
