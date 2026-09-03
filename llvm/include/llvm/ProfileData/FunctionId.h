//===--- FunctionId.h - Sample profile function object ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Defines FunctionId class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_FUNCTIONID_H
#define LLVM_PROFILEDATA_FUNCTIONID_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MD5.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

namespace llvm {
/// Sample profile data structures and helpers.
namespace sampleprof {

/// Represents a function name from a sample profile as a string or MD5 hash.
///
/// It comes with two forms: a string or a hash code. The latter form is the
/// 64-bit MD5 of the function name for efficient storage supported by ExtBinary
/// profile format, and when reading the profile, this class can represent it
/// without converting it to a string first.
/// When representing a hash code, we utilize the LengthOrHashCode field to
/// store it, and Name is set to null. When representing a string, it is same as
/// StringRef.
class FunctionId {

  const char *Data = nullptr;

  // Use uint64_t instead of size_t so that it can also hold a MD5 value on
  // 32-bit system.
  uint64_t LengthOrHashCode = 0;

  /// Extension to memcmp to handle hash code representation. If both are hash
  /// values, Lhs and Rhs are both null, function returns 0 (and needs an extra
  /// comparison using getIntValue). If only one is hash code, it is considered
  /// less than the StringRef one. Otherwise perform normal string comparison.
  static int compareMemory(const char *Lhs, const char *Rhs, uint64_t Length) {
    if (Lhs == Rhs)
      return 0;
    if (!Lhs)
      return -1;
    if (!Rhs)
      return 1;
    return ::memcmp(Lhs, Rhs, (size_t)Length);
  }

public:
  /// Construct an empty FunctionId.
  FunctionId() = default;

  /// Constructor from a StringRef.
  /// @param Str Function name to reference.
  explicit FunctionId(StringRef Str)
      : Data(Str.data()), LengthOrHashCode(Str.size()) {
  }

  /// Constructor from a hash code.
  /// @param HashCode Non-zero MD5 hash of the function name.
  explicit FunctionId(uint64_t HashCode)
      : LengthOrHashCode(HashCode) {
    assert(HashCode != 0);
  }

  /// Check for equality with \p Other.
  ///
  /// Similar to StringRef::equals, but will also cover for the case where one
  /// or both are hash codes. Comparing their int values are sufficient. A hash
  /// code FunctionId is considered not equal to a StringRef FunctionId
  /// regardless of actual contents.
  /// @param Other FunctionId to compare against.
  /// @return True if this FunctionId equals \p Other.
  bool equals(const FunctionId &Other) const {
    return LengthOrHashCode == Other.LengthOrHashCode &&
           compareMemory(Data, Other.Data, LengthOrHashCode) == 0;
  }

  /// Compare this FunctionId with \p Other for total order.
  ///
  /// If both FunctionId are StringRef, this is the same as StringRef::compare.
  /// If one of them is StringRef, it is considered greater than the hash code
  /// FunctionId. Otherwise this is the the same as comparing their int values.
  /// @param Other FunctionId to compare against.
  /// @return Negative, zero, or positive for less-than, equal, or greater-than.
  int compare(const FunctionId &Other) const {
    auto Res = compareMemory(
        Data, Other.Data, std::min(LengthOrHashCode, Other.LengthOrHashCode));
    if (Res != 0)
      return Res;
    if (LengthOrHashCode == Other.LengthOrHashCode)
      return 0;
    return LengthOrHashCode < Other.LengthOrHashCode ? -1 : 1;
  }

  /// Convert to a string, usually for output purpose. Use caution on return
  /// value's lifetime when converting to StringRef.
  /// @return String form of the function name or its hash code.
  std::string str() const {
    if (Data)
      return std::string(Data, LengthOrHashCode);
    if (LengthOrHashCode != 0)
      return std::to_string(LengthOrHashCode);
    return std::string();
  }

  /// Convert to StringRef when this object holds a string, not a hash.
  ///
  /// This is only allowed when it is known this object is representing a
  /// StringRef, not a hash code. Calling this function on a hash code is
  /// considered an error.
  /// @return StringRef view of the function name, or empty if unset.
  StringRef stringRef() const {
    if (Data)
      return StringRef(Data, LengthOrHashCode);
    assert(LengthOrHashCode == 0 &&
           "Cannot convert MD5 FunctionId to StringRef");
    return StringRef();
  }

  friend raw_ostream &operator<<(raw_ostream &OS, const FunctionId &Obj);

  /// Get the MD5 hash code for this FunctionId.
  ///
  /// Returns this object's hash code if it is already representing one,
  /// otherwise returns the MD5 of its string content. Note that it is not the
  /// same as std::hash because we want to keep the consistency that the same
  /// sample profile function in string form or MD5 form has the same hash code.
  /// @return MD5 hash code for this FunctionId.
  uint64_t getHashCode() const {
    if (Data)
      return MD5Hash(StringRef(Data, LengthOrHashCode));
    return LengthOrHashCode;
  }

  /// Return true if this FunctionId is empty.
  /// @return True if this FunctionId is empty.
  bool empty() const { return LengthOrHashCode == 0; }

  /// Check if this object represents a StringRef, or a hash code.
  /// @return True if this object represents a StringRef.
  bool isStringRef() const { return Data != nullptr; }
};

/// Return true if \p LHS and \p RHS are equal.
/// @param LHS Left-hand FunctionId.
/// @param RHS Right-hand FunctionId.
/// @return True if \p LHS and \p RHS are equal.
inline bool operator==(const FunctionId &LHS, const FunctionId &RHS) {
  return LHS.equals(RHS);
}

/// Return true if \p LHS and \p RHS are not equal.
/// @param LHS Left-hand FunctionId.
/// @param RHS Right-hand FunctionId.
/// @return True if \p LHS and \p RHS are not equal.
inline bool operator!=(const FunctionId &LHS, const FunctionId &RHS) {
  return !LHS.equals(RHS);
}

/// Return true if \p LHS is ordered before \p RHS.
/// @param LHS Left-hand FunctionId.
/// @param RHS Right-hand FunctionId.
/// @return True if \p LHS is ordered before \p RHS.
inline bool operator<(const FunctionId &LHS, const FunctionId &RHS) {
  return LHS.compare(RHS) < 0;
}

/// Return true if \p LHS is ordered before or equal to \p RHS.
/// @param LHS Left-hand FunctionId.
/// @param RHS Right-hand FunctionId.
/// @return True if \p LHS is ordered before or equal to \p RHS.
inline bool operator<=(const FunctionId &LHS, const FunctionId &RHS) {
  return LHS.compare(RHS) <= 0;
}

/// Return true if \p LHS is ordered after \p RHS.
/// @param LHS Left-hand FunctionId.
/// @param RHS Right-hand FunctionId.
/// @return True if \p LHS is ordered after \p RHS.
inline bool operator>(const FunctionId &LHS, const FunctionId &RHS) {
  return LHS.compare(RHS) > 0;
}

/// Return true if \p LHS is ordered after or equal to \p RHS.
/// @param LHS Left-hand FunctionId.
/// @param RHS Right-hand FunctionId.
/// @return True if \p LHS is ordered after or equal to \p RHS.
inline bool operator>=(const FunctionId &LHS, const FunctionId &RHS) {
  return LHS.compare(RHS) >= 0;
}

/// Write \p Obj to \p OS as a string or hash code.
/// @param OS Output stream.
/// @param Obj FunctionId to print.
/// @return Reference to \p OS after writing.
inline raw_ostream &operator<<(raw_ostream &OS, const FunctionId &Obj) {
  if (Obj.Data)
    return OS << StringRef(Obj.Data, Obj.LengthOrHashCode);
  if (Obj.LengthOrHashCode != 0)
    return OS << Obj.LengthOrHashCode;
  return OS;
}

/// Return the MD5 hash code of \p Obj.
/// @param Obj FunctionId to hash.
/// @return MD5 hash code of \p Obj.
inline uint64_t MD5Hash(const FunctionId &Obj) {
  return Obj.getHashCode();
}

/// Return a hash value for \p Obj suitable for LLVM hashing utilities.
/// @param Obj FunctionId to hash.
/// @return Hash code suitable for LLVM hashing utilities.
inline uint64_t hash_value(const FunctionId &Obj) {
  return Obj.getHashCode();
}

} // end namespace sampleprof

/// Template specialization for FunctionId so that it can be used in LLVM map
/// containers.
template <> struct DenseMapInfo<sampleprof::FunctionId, void> {

  /// Compute a DenseMap hash for \p Val.
  /// @param Val FunctionId to hash.
  /// @return DenseMap hash derived from the FunctionId hash code.
  static unsigned getHashValue(const sampleprof::FunctionId &Val) {
    return Val.getHashCode();
  }

  /// Return true if \p LHS and \p RHS are equal.
  /// @param LHS Left-hand FunctionId.
  /// @param RHS Right-hand FunctionId.
  /// @return True if \p LHS and \p RHS are equal.
  static bool isEqual(const sampleprof::FunctionId &LHS,
                      const sampleprof::FunctionId &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

namespace std {

/// Template specialization for FunctionId so that it can be used in STL
/// containers.
template <> struct hash<llvm::sampleprof::FunctionId> {
  size_t operator()(const llvm::sampleprof::FunctionId &Val) const {
    return Val.getHashCode();
  }
};

} // end namespace std

#endif // LLVM_PROFILEDATA_FUNCTIONID_H
