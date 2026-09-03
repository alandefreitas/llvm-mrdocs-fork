//===------ ExecutorAddress.h - Executing process address -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represents an address in the executing program.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_EXECUTORADDRESS_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_EXECUTORADDRESS_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimplePackedSerialization.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#if __has_feature(ptrauth_calls)
#include <ptrauth.h>
#endif
#include <type_traits>

namespace llvm {
namespace orc {

/// Difference between two executor addresses, in bytes.
using ExecutorAddrDiff = uint64_t;

/// Represents an address in the executor process.
class ExecutorAddr {
public:
  /// A wrap/unwrap function that leaves pointers unmodified.
  using rawPtr = llvm::identity;

#if __has_feature(ptrauth_calls)
  template <typename T> class PtrauthSignDefault {
  public:
    constexpr T *operator()(T *P) {
      if (std::is_function_v<T>)
        return ptrauth_sign_unauthenticated(P, ptrauth_key_function_pointer, 0);
      else
        return P;
    }
  };

  template <typename T> class PtrauthStripDefault {
  public:
    constexpr T *operator()(T *P) {
      return ptrauth_strip(P, ptrauth_key_function_pointer);
    }
  };

  /// Default wrap function to use on this host.
  template <typename T> using defaultWrap = PtrauthSignDefault<T>;

  /// Default unwrap function to use on this host.
  template <typename T> using defaultUnwrap = PtrauthStripDefault<T>;

#else

  /// Default wrap function to use on this host.
  template <typename T> using defaultWrap = rawPtr;

  /// Default unwrap function to use on this host.
  template <typename T> using defaultUnwrap = rawPtr;

#endif

  /// Merges a tag into the raw address value:
  ///   P' = P | (TagValue << TagOffset).
  class Tag {
  public:
    /// Construct a tagger that ORs \p TagValue shifted by \p TagOffset.
    /// @param TagValue Tag bits to merge into the pointer.
    /// @param TagOffset Bit offset at which to place the tag.
    constexpr Tag(uintptr_t TagValue, uintptr_t TagOffset)
        : TagMask(TagValue << TagOffset) {}

    /// Return \p P with this tag merged into its address bits.
    /// @param P Pointer to tag.
    /// @return \p P with this tag ORed into its address bits.
    template <typename T> constexpr T *operator()(T *P) {
      return reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(P) | TagMask);
    }

  private:
    uintptr_t TagMask;
  };

  /// Strips a tag of the given length from the given offset within the pointer:
  /// P' = P & ~(((1 << TagLen) -1) << TagOffset)
  class Untag {
  public:
    /// Construct a stripper for \p TagLen bits starting at \p TagOffset.
    /// @param TagLen Number of tag bits to clear.
    /// @param TagOffset Bit offset of the tag field.
    constexpr Untag(uintptr_t TagLen, uintptr_t TagOffset)
        : UntagMask(~(((uintptr_t(1) << TagLen) - 1) << TagOffset)) {}

    /// Return \p P with this tag field cleared.
    /// @param P Pointer to untag.
    /// @return \p P with this tag field cleared from its address bits.
    template <typename T> constexpr T *operator()(T *P) {
      return reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(P) & UntagMask);
    }

  private:
    uintptr_t UntagMask;
  };

  /// Construct a null executor address.
  ExecutorAddr() = default;

  /// Create an ExecutorAddr from the given value.
  /// @param Addr Raw address value in the executor process.
  explicit constexpr ExecutorAddr(uint64_t Addr) : Addr(Addr) {}

  /// Create an ExecutorAddr from the given pointer.
  /// Warning: This should only be used when JITing in-process.
  /// @param Ptr Host pointer to convert.
  /// @param Unwrap Function applied to \p Ptr before taking its address bits.
  /// @return An ExecutorAddr holding the unwrapped host pointer bits.
  template <typename T, typename UnwrapFn = defaultUnwrap<T>>
  static ExecutorAddr fromPtr(T *Ptr, UnwrapFn &&Unwrap = UnwrapFn()) {
    return ExecutorAddr(
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(Unwrap(Ptr))));
  }

  /// Cast this ExecutorAddr to a pointer of the given type.
  /// Warning: This should only be used when JITing in-process.
  /// @param Wrap Function applied to the reconstructed host pointer.
  /// @return A host pointer of type \p T for this address.
  template <typename T, typename WrapFn = defaultWrap<std::remove_pointer_t<T>>>
  std::enable_if_t<std::is_pointer<T>::value, T>
  toPtr(WrapFn &&Wrap = WrapFn()) const {
    uintptr_t IntPtr = static_cast<uintptr_t>(Addr);
    assert(IntPtr == Addr && "ExecutorAddr value out of range for uintptr_t");
    return Wrap(reinterpret_cast<T>(IntPtr));
  }

  /// Cast this ExecutorAddr to a pointer of the given function type.
  /// Warning: This should only be used when JITing in-process.
  /// @param Wrap Function applied to the reconstructed host function pointer.
  /// @return A host function pointer of type \p T * for this address.
  template <typename T, typename WrapFn = defaultWrap<T>>
  std::enable_if_t<std::is_function<T>::value, T *>
  toPtr(WrapFn &&Wrap = WrapFn()) const {
    uintptr_t IntPtr = static_cast<uintptr_t>(Addr);
    assert(IntPtr == Addr && "ExecutorAddr value out of range for uintptr_t");
    return Wrap(reinterpret_cast<T *>(IntPtr));
  }

  /// Return the raw address value.
  /// @return The raw 64-bit address value.
  uint64_t getValue() const { return Addr; }
  /// Set the raw address value to \p Addr.
  /// @param Addr New raw address value.
  void setValue(uint64_t Addr) { this->Addr = Addr; }
  /// Return true if this address is null.
  /// @return True if this address is null.
  bool isNull() const { return Addr == 0; }

  /// Return true if this address is non-null.
  /// @return True if this address is non-null.
  explicit operator bool() const { return Addr != 0; }

  /// Return true if \p LHS and \p RHS have the same value.
  /// @param LHS Left-hand address.
  /// @param RHS Right-hand address.
  /// @return True if \p LHS and \p RHS have the same value.
  friend bool operator==(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr == RHS.Addr;
  }

  /// Return true if \p LHS and \p RHS have different values.
  /// @param LHS Left-hand address.
  /// @param RHS Right-hand address.
  /// @return True if \p LHS and \p RHS have different values.
  friend bool operator!=(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr != RHS.Addr;
  }

  /// Return true if \p LHS is less than \p RHS.
  /// @param LHS Left-hand address.
  /// @param RHS Right-hand address.
  /// @return True if \p LHS is less than \p RHS.
  friend bool operator<(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr < RHS.Addr;
  }

  /// Return true if \p LHS is less than or equal to \p RHS.
  /// @param LHS Left-hand address.
  /// @param RHS Right-hand address.
  /// @return True if \p LHS is less than or equal to \p RHS.
  friend bool operator<=(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr <= RHS.Addr;
  }

  /// Return true if \p LHS is greater than \p RHS.
  /// @param LHS Left-hand address.
  /// @param RHS Right-hand address.
  /// @return True if \p LHS is greater than \p RHS.
  friend bool operator>(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr > RHS.Addr;
  }

  /// Return true if \p LHS is greater than or equal to \p RHS.
  /// @param LHS Left-hand address.
  /// @param RHS Right-hand address.
  /// @return True if \p LHS is greater than or equal to \p RHS.
  friend bool operator>=(const ExecutorAddr &LHS, const ExecutorAddr &RHS) {
    return LHS.Addr >= RHS.Addr;
  }

  /// Prefixed increment: advance this address by one byte.
  /// @return Reference to this address after incrementing.
  ExecutorAddr &operator++() {
    ++Addr;
    return *this;
  }
  /// Prefixed decrement: retreat this address by one byte.
  /// @return Reference to this address after decrementing.
  ExecutorAddr &operator--() {
    --Addr;
    return *this;
  }
  /// Postfix increment: advance this address by one byte and return the old value.
  /// @param Unused Dummy parameter distinguishing postfix from prefix.
  /// @return Copy of this address before the increment.
  ExecutorAddr operator++(int Unused) { return ExecutorAddr(Addr++); }
  /// Postfix decrement: retreat this address by one byte and return the old value.
  /// @param Unused Dummy parameter distinguishing postfix from prefix.
  /// @return Copy of this address before the decrement.
  ExecutorAddr operator--(int Unused) { return ExecutorAddr(Addr--); }

  /// Advance this address by \p Delta bytes.
  /// @param Delta Byte offset to add.
  /// @return Reference to this address after adding \p Delta.
  ExecutorAddr &operator+=(const ExecutorAddrDiff &Delta) {
    Addr += Delta;
    return *this;
  }

  /// Retreat this address by \p Delta bytes.
  /// @param Delta Byte offset to subtract.
  /// @return Reference to this address after subtracting \p Delta.
  ExecutorAddr &operator-=(const ExecutorAddrDiff &Delta) {
    Addr -= Delta;
    return *this;
  }

private:
  uint64_t Addr = 0;
};

/// Subtracting two addresses yields an offset.
/// @param LHS Minuend address.
/// @param RHS Subtrahend address.
/// @return Byte difference between \p LHS and \p RHS.
inline ExecutorAddrDiff operator-(const ExecutorAddr &LHS,
                                  const ExecutorAddr &RHS) {
  return ExecutorAddrDiff(LHS.getValue() - RHS.getValue());
}

/// Adding an offset and an address yields an address.
/// @param LHS Base address.
/// @param RHS Byte offset to add.
/// @return Address at \p LHS plus \p RHS bytes.
inline ExecutorAddr operator+(const ExecutorAddr &LHS,
                              const ExecutorAddrDiff &RHS) {
  return ExecutorAddr(LHS.getValue() + RHS);
}

/// Adding an address and an offset yields an address.
/// @param LHS Byte offset to add.
/// @param RHS Base address.
/// @return Address at \p RHS plus \p LHS bytes.
inline ExecutorAddr operator+(const ExecutorAddrDiff &LHS,
                              const ExecutorAddr &RHS) {
  return ExecutorAddr(LHS + RHS.getValue());
}

/// Subtracting an offset from an address yields an address.
/// @param LHS Base address.
/// @param RHS Byte offset to subtract.
/// @return Address at \p LHS minus \p RHS bytes.
inline ExecutorAddr operator-(const ExecutorAddr &LHS,
                              const ExecutorAddrDiff &RHS) {
  return ExecutorAddr(LHS.getValue() - RHS);
}

/// Taking the modulus of an address and a diff yields a diff.
/// @param LHS Address whose value is the dividend.
/// @param RHS Byte modulus.
/// @return Remainder of \p LHS's value modulo \p RHS.
inline ExecutorAddrDiff operator%(const ExecutorAddr &LHS,
                                  const ExecutorAddrDiff &RHS) {
  return ExecutorAddrDiff(LHS.getValue() % RHS);
}

/// Represents an address range in the exceutor process.
struct ExecutorAddrRange {
  /// Construct an empty address range.
  ExecutorAddrRange() = default;
  /// Construct a range from \p Start inclusive to \p End exclusive.
  /// @param Start First address in the range.
  /// @param End One-past-the-end address.
  ExecutorAddrRange(ExecutorAddr Start, ExecutorAddr End)
      : Start(Start), End(End) {}
  /// Construct a range starting at \p Start with length \p Size.
  /// @param Start First address in the range.
  /// @param Size Length of the range in bytes.
  ExecutorAddrRange(ExecutorAddr Start, ExecutorAddrDiff Size)
      : Start(Start), End(Start + Size) {}

  /// Create a range from host pointers \p Start and \p End.
  /// @param Start Pointer to the first element.
  /// @param End Pointer one past the last element.
  /// @param Unwrap Function applied to each pointer before conversion.
  /// @return An ExecutorAddrRange spanning the converted pointers.
  template <typename T, typename UnwrapFn = ExecutorAddr::defaultUnwrap<T>>
  static ExecutorAddrRange fromPtrRange(T *Start, T *End,
                                        UnwrapFn &&Unwrap = UnwrapFn()) {
    return {ExecutorAddr::fromPtr(Start, Unwrap),
            ExecutorAddr::fromPtr(End, Unwrap)};
  }

  /// Create a range from host pointer \p Ptr with length \p Size.
  /// @param Ptr Pointer to the first element.
  /// @param Size Length of the range in elements converted via address size.
  /// @param Unwrap Function applied to \p Ptr before conversion.
  /// @return An ExecutorAddrRange starting at \p Ptr with length \p Size.
  template <typename T, typename UnwrapFn = ExecutorAddr::defaultUnwrap<T>>
  static ExecutorAddrRange fromPtrRange(T *Ptr, ExecutorAddrDiff Size,
                                        UnwrapFn &&Unwrap = UnwrapFn()) {
    return {ExecutorAddr::fromPtr(Ptr, std::forward<UnwrapFn>(Unwrap)), Size};
  }

  /// Return true if this range is empty.
  /// @return True if Start equals End.
  bool empty() const { return Start == End; }
  /// Return the size of this range in bytes.
  /// @return Byte length of this range (End minus Start).
  ExecutorAddrDiff size() const { return End - Start; }

  /// Return true if \p LHS and \p RHS span the same addresses.
  /// @param LHS Left-hand range.
  /// @param RHS Right-hand range.
  /// @return True if \p LHS and \p RHS span the same addresses.
  friend bool operator==(const ExecutorAddrRange &LHS,
                         const ExecutorAddrRange &RHS) {
    return LHS.Start == RHS.Start && LHS.End == RHS.End;
  }
  /// Return true if \p LHS and \p RHS span different addresses.
  /// @param LHS Left-hand range.
  /// @param RHS Right-hand range.
  /// @return True if \p LHS and \p RHS span different addresses.
  friend bool operator!=(const ExecutorAddrRange &LHS,
                         const ExecutorAddrRange &RHS) {
    return !(LHS == RHS);
  }
  /// Return true if \p LHS is ordered before \p RHS.
  /// @param LHS Left-hand range.
  /// @param RHS Right-hand range.
  /// @return True if \p LHS is ordered before \p RHS.
  friend bool operator<(const ExecutorAddrRange &LHS,
                        const ExecutorAddrRange &RHS) {
    return LHS.Start < RHS.Start ||
           (LHS.Start == RHS.Start && LHS.End < RHS.End);
  }
  /// Return true if \p LHS is ordered before or equal to \p RHS.
  /// @param LHS Left-hand range.
  /// @param RHS Right-hand range.
  /// @return True if \p LHS is ordered before or equal to \p RHS.
  friend bool operator<=(const ExecutorAddrRange &LHS,
                         const ExecutorAddrRange &RHS) {
    return LHS.Start < RHS.Start ||
           (LHS.Start == RHS.Start && LHS.End <= RHS.End);
  }
  /// Return true if \p LHS is ordered after \p RHS.
  /// @param LHS Left-hand range.
  /// @param RHS Right-hand range.
  /// @return True if \p LHS is ordered after \p RHS.
  friend bool operator>(const ExecutorAddrRange &LHS,
                        const ExecutorAddrRange &RHS) {
    return LHS.Start > RHS.Start ||
           (LHS.Start == RHS.Start && LHS.End > RHS.End);
  }
  /// Return true if \p LHS is ordered after or equal to \p RHS.
  /// @param LHS Left-hand range.
  /// @param RHS Right-hand range.
  /// @return True if \p LHS is ordered after or equal to \p RHS.
  friend bool operator>=(const ExecutorAddrRange &LHS,
                         const ExecutorAddrRange &RHS) {
    return LHS.Start > RHS.Start ||
           (LHS.Start == RHS.Start && LHS.End >= RHS.End);
  }

  /// Return true if \p Addr lies in [Start, End).
  /// @param Addr Address to test.
  /// @return True if \p Addr lies in [Start, End).
  bool contains(ExecutorAddr Addr) const { return Start <= Addr && Addr < End; }
  /// Return true if \p Other lies entirely within this range.
  /// @param Other Range to test for containment.
  /// @return True if \p Other lies entirely within this range.
  bool contains(const ExecutorAddrRange &Other) {
    return (Other.Start >= Start && Other.End <= End);
  }
  /// Return true if \p Other overlaps this range.
  /// @param Other Range to test for overlap.
  /// @return True if \p Other overlaps this range.
  bool overlaps(const ExecutorAddrRange &Other) {
    return !(Other.End <= Start || End <= Other.Start);
  }

  /// First address in the range (inclusive).
  ExecutorAddr Start;
  /// One-past-the-end address of the range (exclusive).
  ExecutorAddr End;
};

/// Write \p A to \p OS as a hexadecimal address.
/// @param OS Output stream.
/// @param A Address to print.
/// @return Reference to \p OS after writing.
inline raw_ostream &operator<<(raw_ostream &OS, const ExecutorAddr &A) {
  return OS << formatv("{0:x}", A.getValue());
}

/// Write \p R to \p OS as a hexadecimal start/end pair.
/// @param OS Output stream.
/// @param R Range to print.
/// @return Reference to \p OS after writing.
inline raw_ostream &operator<<(raw_ostream &OS, const ExecutorAddrRange &R) {
  return OS << formatv("{0:x} -- {1:x}", R.Start.getValue(), R.End.getValue());
}

namespace shared {

/// SPS tag type for ExecutorAddr.
class SPSExecutorAddr {};

/// SPS serializatior for ExecutorAddr.
template <> class SPSSerializationTraits<SPSExecutorAddr, ExecutorAddr> {
public:
  /// Return the serialized size of \p EA.
  /// @param EA Address to measure.
  /// @return Number of bytes needed to serialize \p EA.
  static size_t size(const ExecutorAddr &EA) {
    return SPSArgList<uint64_t>::size(EA.getValue());
  }

  /// Serialize \p EA into \p BOB.
  /// @param BOB Output buffer.
  /// @param EA Address to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &BOB, const ExecutorAddr &EA) {
    return SPSArgList<uint64_t>::serialize(BOB, EA.getValue());
  }

  /// Deserialize an ExecutorAddr from \p BIB into \p EA.
  /// @param BIB Input buffer.
  /// @param EA Destination address.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &BIB, ExecutorAddr &EA) {
    uint64_t Tmp;
    if (!SPSArgList<uint64_t>::deserialize(BIB, Tmp))
      return false;
    EA = ExecutorAddr(Tmp);
    return true;
  }
};

/// SPS tag type for ExecutorAddrRange as a start/end address pair.
using SPSExecutorAddrRange = SPSTuple<SPSExecutorAddr, SPSExecutorAddr>;

/// Serialization traits for address ranges.
template <>
class SPSSerializationTraits<SPSExecutorAddrRange, ExecutorAddrRange> {
public:
  /// Return the serialized size of \p Value.
  /// @param Value Range to measure.
  /// @return Number of bytes needed to serialize \p Value.
  static size_t size(const ExecutorAddrRange &Value) {
    return SPSArgList<SPSExecutorAddr, SPSExecutorAddr>::size(Value.Start,
                                                              Value.End);
  }

  /// Serialize \p Value into \p BOB.
  /// @param BOB Output buffer.
  /// @param Value Range to serialize.
  /// @return True if serialization succeeded.
  static bool serialize(SPSOutputBuffer &BOB, const ExecutorAddrRange &Value) {
    return SPSArgList<SPSExecutorAddr, SPSExecutorAddr>::serialize(
        BOB, Value.Start, Value.End);
  }

  /// Deserialize an ExecutorAddrRange from \p BIB into \p Value.
  /// @param BIB Input buffer.
  /// @param Value Destination range.
  /// @return True if deserialization succeeded.
  static bool deserialize(SPSInputBuffer &BIB, ExecutorAddrRange &Value) {
    return SPSArgList<SPSExecutorAddr, SPSExecutorAddr>::deserialize(
        BIB, Value.Start, Value.End);
  }
};

/// SPS tag type for a sequence of ExecutorAddrRange values.
using SPSExecutorAddrRangeSequence = SPSSequence<SPSExecutorAddrRange>;

} // End namespace shared.
} // End namespace orc.

/// DenseMapInfo specialization for ExecutorAddr.
template <> struct DenseMapInfo<orc::ExecutorAddr> {
  /// Return a hash of the raw address value in \p Addr.
  /// @param Addr Address to hash.
  /// @return Hash of \p Addr's raw address value.
  static unsigned getHashValue(const orc::ExecutorAddr &Addr) {
    return DenseMapInfo<uint64_t>::getHashValue(Addr.getValue());
  }

  /// Return true if \p LHS and \p RHS have equal address values.
  /// @param LHS Left-hand address.
  /// @param RHS Right-hand address.
  /// @return True if \p LHS and \p RHS have equal address values.
  static bool isEqual(const orc::ExecutorAddr &LHS,
                      const orc::ExecutorAddr &RHS) {
    return DenseMapInfo<uint64_t>::isEqual(LHS.getValue(), RHS.getValue());
  }
};

} // End namespace llvm.

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_EXECUTORADDRESS_H
