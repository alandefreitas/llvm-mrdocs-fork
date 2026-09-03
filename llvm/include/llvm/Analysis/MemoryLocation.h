//===- MemoryLocation.h - Memory location descriptions ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides utility analysis objects describing memory locations.
/// These are used both by the Alias Analysis infrastructure and more
/// specialized memory analysis layers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MEMORYLOCATION_H
#define LLVM_ANALYSIS_MEMORYLOCATION_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/TypeSize.h"

#include <optional>

namespace llvm {

class CallBase;
class Instruction;
class LoadInst;
class StoreInst;
class MemTransferInst;
class MemIntrinsic;
class AtomicCmpXchgInst;
class AtomicRMWInst;
class AnyMemTransferInst;
class AnyMemIntrinsic;
class TargetLibraryInfo;
class VAArgInst;

/// Represents the size of a MemoryLocation.
///
/// Logically, this is an \c std::optional<uint63_t> that also records whether
/// the stored size \p N is precise. A precise size means the referenced storage
/// is exactly \p N bytes. An imprecise size is an upper bound formed by
/// unioning precise sizes and may conservatively over-approximate the accessed
/// range.
///
/// For example, \c (%p, 4) is precise for \c store i32 0, i32* %p because %p
/// must refer to at least four bytes. The same size is imprecise at a \c memcpy
/// whose length is \c select i1 %foo, i64 1, i64 4 because at most four bytes
/// are copied but the actual amount is unknown.
///
/// Pathologically large values degrade to \c std::nullopt. Bit 62 of the
/// internal representation records scalable size information needed for alias
/// analysis on scalable quantities.
class LocationSize {
  enum : uint64_t {
    BeforeOrAfterPointer = ~uint64_t(0),
    ScalableBit = uint64_t(1) << 62,
    AfterPointer = (BeforeOrAfterPointer - 1) & ~ScalableBit,
    MapEmpty = BeforeOrAfterPointer - 2,
    MapTombstone = BeforeOrAfterPointer - 3,
    ImpreciseBit = uint64_t(1) << 63,

    // The maximum value we can represent without falling back to 'unknown'.
    MaxValue = (MapTombstone - 1) & ~(ImpreciseBit | ScalableBit),
  };

  uint64_t Value;

  constexpr LocationSize(uint64_t Raw) : Value(Raw) {}
  constexpr LocationSize(uint64_t Raw, bool Scalable)
      : Value(Raw > MaxValue ? AfterPointer
                             : Raw | (Scalable ? ScalableBit : uint64_t(0))) {}

  static_assert(AfterPointer & ImpreciseBit,
                "AfterPointer is imprecise by definition.");
  static_assert(BeforeOrAfterPointer & ImpreciseBit,
                "BeforeOrAfterPointer is imprecise by definition.");
  static_assert(~(MaxValue & ScalableBit), "Max value don't have bit 62 set");

public:
  /// Create a LocationSize with an exact (non-scalable) size.
  /// @param Value Exact size in address-units.
  /// @return LocationSize with exact size \p Value.
  static LocationSize precise(uint64_t Value) {
    return LocationSize(Value, false /*Scalable*/);
  }
  /// Create a LocationSize with an exact size, possibly scalable.
  /// @param Value Exact size as a TypeSize (fixed or scalable).
  /// @return LocationSize with exact size \p Value.
  static LocationSize precise(TypeSize Value) {
    return LocationSize(Value.getKnownMinValue(), Value.isScalable());
  }

  /// Create a LocationSize that is an upper bound on the accessed size.
  /// @param Value Upper bound on size in address-units.
  /// @return LocationSize that is an upper bound of \p Value bytes.
  static LocationSize upperBound(uint64_t Value) {
    // You can't go lower than 0, so give a precise result.
    if (LLVM_UNLIKELY(Value == 0))
      return precise(0);
    if (LLVM_UNLIKELY(Value > MaxValue))
      return afterPointer();
    return LocationSize(Value | ImpreciseBit);
  }
  /// Create a LocationSize that is an upper bound on a TypeSize access.
  /// @param Value Upper bound as a TypeSize; scalable sizes become afterPointer.
  /// @return LocationSize that is an upper bound of \p Value.
  static LocationSize upperBound(TypeSize Value) {
    if (Value.isScalable())
      return afterPointer();
    return upperBound(Value.getFixedValue());
  }

  /// Any location after the base pointer (but still within the underlying
  /// object).
  /// @return LocationSize representing any location after the base pointer.
  constexpr static LocationSize afterPointer() {
    return LocationSize(AfterPointer);
  }

  /// Any location before or after the base pointer (but still within the
  /// underlying object).
  /// @return LocationSize representing any location before or after the base
  /// pointer.
  constexpr static LocationSize beforeOrAfterPointer() {
    return LocationSize(BeforeOrAfterPointer);
  }

  /// Sentinel value used as the DenseMap tombstone key.
  /// @return LocationSize used as the DenseMap tombstone key.
  constexpr static LocationSize mapTombstone() {
    return LocationSize(MapTombstone);
  }
  /// Sentinel value used as the DenseMap empty key.
  /// @return LocationSize used as the DenseMap empty key.
  constexpr static LocationSize mapEmpty() {
    return LocationSize(MapEmpty);
  }

  /// Return a LocationSize that can correctly represent either \c *this or
  /// \p Other.
  /// @param Other Size to union with this one.
  /// @return LocationSize that can correctly represent either size.
  LocationSize unionWith(LocationSize Other) const {
    if (Other == *this)
      return *this;

    if (Value == BeforeOrAfterPointer || Other.Value == BeforeOrAfterPointer)
      return beforeOrAfterPointer();
    if (Value == AfterPointer || Other.Value == AfterPointer)
      return afterPointer();
    if (isScalable() || Other.isScalable())
      return afterPointer();

    return upperBound(
        std::max(getValue().getFixedValue(), Other.getValue().getFixedValue()));
  }

  /// Return true if this size has a known numeric value.
  /// @return True if this size has a known numeric value.
  bool hasValue() const {
    return Value != AfterPointer && Value != BeforeOrAfterPointer;
  }
  /// Return true if this size is scalable.
  /// @return True if this size is scalable.
  bool isScalable() const { return (Value & ScalableBit); }

  /// Return the stored size as a TypeSize.
  /// @return The stored size as a TypeSize.
  TypeSize getValue() const {
    assert(hasValue() && "Getting value from an unknown LocationSize!");
    assert((Value & ~(ImpreciseBit | ScalableBit)) < MaxValue &&
           "Scalable bit of value should be masked");
    return {Value & ~(ImpreciseBit | ScalableBit), isScalable()};
  }

  /// Return true if this size is precise (and therefore not unknown).
  /// @return True if this size is precise (and therefore not unknown).
  bool isPrecise() const { return (Value & ImpreciseBit) == 0; }

  /// Return true if this LocationSize's value is zero.
  /// @return True if this LocationSize's value is zero.
  bool isZero() const {
    return hasValue() && getValue().getKnownMinValue() == 0;
  }

  /// Whether accesses before the base pointer are possible.
  /// @return True if accesses before the base pointer are possible.
  bool mayBeBeforePointer() const { return Value == BeforeOrAfterPointer; }

  /// Return true if this size equals \p Other.
  /// @param Other Size to compare against.
  /// @return True if this size equals \p Other.
  bool operator==(const LocationSize &Other) const {
    return Value == Other.Value;
  }
  /// Return true if this size equals the precise size of \p Other.
  /// @param Other TypeSize to compare against as a precise LocationSize.
  /// @return True if this size equals the precise size of \p Other.
  bool operator==(const TypeSize &Other) const {
    return (*this == LocationSize::precise(Other));
  }
  /// Return true if this size equals the precise size \p Other.
  /// @param Other Exact size in address-units to compare against.
  /// @return True if this size equals the precise size \p Other.
  bool operator==(uint64_t Other) const {
    return (*this == LocationSize::precise(Other));
  }

  /// Return true if this size differs from \p Other.
  /// @param Other Size to compare against.
  /// @return True if this size differs from \p Other.
  bool operator!=(const LocationSize &Other) const { return !(*this == Other); }
  /// Return true if this size differs from \p Other after converting \p Other
  /// to a precise LocationSize.
  /// @param Other TypeSize to compare against as a precise LocationSize.
  /// @return True if this size differs from \p Other.
  bool operator!=(const TypeSize &Other) const { return !(*this == Other); }
  /// Return true if this size differs from the precise size \p Other.
  /// @param Other Exact size in address-units to compare against.
  /// @return True if this size differs from the precise size \p Other.
  bool operator!=(uint64_t Other) const { return !(*this == Other); }

  // Ordering operators are not provided, since it's unclear if there's only one
  // reasonable way to compare:
  // - values that don't exist against values that do, and
  // - precise values to imprecise values

  /// Print a human-readable representation of this size to \p OS.
  /// @param OS Stream to write the size to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Return an opaque value that represents this LocationSize.
  ///
  /// Cannot be reliably converted back into a LocationSize.
  /// @return Opaque raw representation of this LocationSize.
  uint64_t toRaw() const { return Value; }
};

/// Write \p Size to \p OS in a human-readable form.
/// @param OS Stream to write to.
/// @param Size LocationSize to print.
/// @return Reference to \p OS after writing.
inline raw_ostream &operator<<(raw_ostream &OS, LocationSize Size) {
  Size.print(OS);
  return OS;
}

/// Representation for a specific memory location.
///
/// This abstraction can be used to represent a specific location in memory.
/// The goal of the location is to represent enough information to describe
/// abstract aliasing, modification, and reference behaviors of whatever
/// value(s) are stored in memory at the particular location.
///
/// The primary user of this interface is LLVM's Alias Analysis, but other
/// memory analyses such as MemoryDependence can use it as well.
class MemoryLocation {
public:
  /// Sentinel values for size arguments in alias queries.
  enum : uint64_t {
    /// Special value indicating the caller does not know the sizes of the
    /// potential memory references.
    UnknownSize = ~UINT64_C(0)
  };

  /// The address of the start of the location.
  const Value *Ptr;

  /// The maximum size of the location, in address-units, or
  /// UnknownSize if the size is not known.
  ///
  /// Note that an unknown size does not mean the pointer aliases the entire
  /// virtual address space, because there are restrictions on stepping out of
  /// one object and into another. See
  /// http://llvm.org/docs/LangRef.html#pointeraliasing
  LocationSize Size;

  /// The metadata nodes which describes the aliasing of the location (each
  /// member is null if that kind of information is unavailable).
  AAMDNodes AATags;

  /// Print this memory location's pointer and size to \p OS.
  /// @param OS Stream to write the location to.
  void print(raw_ostream &OS) const { OS << *Ptr << " " << Size << "\n"; }

  /// Return a location describing the memory referenced by load \p LI.
  /// @param LI Load instruction to describe.
  /// @return MemoryLocation for the memory referenced by \p LI.
  LLVM_ABI static MemoryLocation get(const LoadInst *LI);
  /// Return a location describing the memory referenced by store \p SI.
  /// @param SI Store instruction to describe.
  /// @return MemoryLocation for the memory referenced by \p SI.
  LLVM_ABI static MemoryLocation get(const StoreInst *SI);
  /// Return a location describing the memory referenced by va_arg \p VI.
  /// @param VI VAArgInst to describe.
  /// @return MemoryLocation for the memory referenced by \p VI.
  LLVM_ABI static MemoryLocation get(const VAArgInst *VI);
  /// Return a location describing the memory referenced by cmpxchg \p CXI.
  /// @param CXI AtomicCmpXchgInst to describe.
  /// @return MemoryLocation for the memory referenced by \p CXI.
  LLVM_ABI static MemoryLocation get(const AtomicCmpXchgInst *CXI);
  /// Return a location describing the memory referenced by atomicrmw \p RMWI.
  /// @param RMWI AtomicRMWInst to describe.
  /// @return MemoryLocation for the memory referenced by \p RMWI.
  LLVM_ABI static MemoryLocation get(const AtomicRMWInst *RMWI);
  /// Return a location describing the memory referenced by \p Inst.
  /// @param Inst Instruction that must be a supported memory operation.
  /// @return MemoryLocation for the memory referenced by \p Inst.
  static MemoryLocation get(const Instruction *Inst) {
    return *MemoryLocation::getOrNone(Inst);
  }
  /// Return a location for \p Inst, or nullopt if it has no memory location.
  /// @param Inst Instruction that may or may not access memory.
  /// @return MemoryLocation for \p Inst, or nullopt if none applies.
  LLVM_ABI static std::optional<MemoryLocation>
  getOrNone(const Instruction *Inst);

  /// Return a location representing the source of transfer \p MTI.
  /// @param MTI Memory transfer instruction.
  /// @return MemoryLocation for the source of \p MTI.
  LLVM_ABI static MemoryLocation getForSource(const MemTransferInst *MTI);
  /// Return a location representing the source of transfer \p MTI.
  /// @param MTI Any memory transfer instruction.
  /// @return MemoryLocation for the source of \p MTI.
  LLVM_ABI static MemoryLocation getForSource(const AnyMemTransferInst *MTI);

  /// Return a location representing the destination of set/transfer \p MI.
  /// @param MI Memory set or transfer intrinsic.
  /// @return MemoryLocation for the destination of \p MI.
  LLVM_ABI static MemoryLocation getForDest(const MemIntrinsic *MI);
  /// Return a location representing the destination of set/transfer \p MI.
  /// @param MI Any memory set or transfer intrinsic.
  /// @return MemoryLocation for the destination of \p MI.
  LLVM_ABI static MemoryLocation getForDest(const AnyMemIntrinsic *MI);
  /// Return a destination location for call \p CI, or nullopt if unknown.
  /// @param CI Call that may have a known memory destination.
  /// @param TLI Target library info used to recognize memory builtins.
  /// @return Destination MemoryLocation for \p CI, or nullopt if unknown.
  LLVM_ABI static std::optional<MemoryLocation>
  getForDest(const CallBase *CI, const TargetLibraryInfo &TLI);

  /// Return a location representing argument \p ArgIdx of call \p Call.
  /// @param Call Call whose argument memory location is requested.
  /// @param ArgIdx Zero-based index of the argument.
  /// @param TLI Optional target library info for recognizing builtins.
  /// @return MemoryLocation for argument \p ArgIdx of \p Call.
  LLVM_ABI static MemoryLocation getForArgument(const CallBase *Call,
                                                unsigned ArgIdx,
                                                const TargetLibraryInfo *TLI);
  /// Return a location representing argument \p ArgIdx of call \p Call.
  /// @param Call Call whose argument memory location is requested.
  /// @param ArgIdx Zero-based index of the argument.
  /// @param TLI Target library info for recognizing builtins.
  /// @return MemoryLocation for argument \p ArgIdx of \p Call.
  static MemoryLocation getForArgument(const CallBase *Call, unsigned ArgIdx,
                                       const TargetLibraryInfo &TLI) {
    return getForArgument(Call, ArgIdx, &TLI);
  }

  /// Return a location that may access any location after Ptr, while remaining
  /// within the underlying object.
  /// @param Ptr Base pointer of the location.
  /// @param AATags Optional alias analysis metadata for the location.
  /// @return MemoryLocation that may access any location after \p Ptr.
  static MemoryLocation getAfter(const Value *Ptr,
                                 const AAMDNodes &AATags = AAMDNodes()) {
    return MemoryLocation(Ptr, LocationSize::afterPointer(), AATags);
  }

  /// Return a location that may access any location before or after Ptr, while
  /// remaining within the underlying object.
  /// @param Ptr Base pointer of the location.
  /// @param AATags Optional alias analysis metadata for the location.
  /// @return MemoryLocation that may access any location before or after \p Ptr.
  static MemoryLocation
  getBeforeOrAfter(const Value *Ptr, const AAMDNodes &AATags = AAMDNodes()) {
    return MemoryLocation(Ptr, LocationSize::beforeOrAfterPointer(), AATags);
  }

  /// Construct an empty location with a null pointer and beforeOrAfter size.
  MemoryLocation() : Ptr(nullptr), Size(LocationSize::beforeOrAfterPointer()) {}

  /// Construct a location at \p Ptr with size \p Size and optional \p AATags.
  /// @param Ptr Base pointer of the location.
  /// @param Size Maximum size of the location.
  /// @param AATags Optional alias analysis metadata for the location.
  explicit MemoryLocation(const Value *Ptr, LocationSize Size,
                          const AAMDNodes &AATags = AAMDNodes())
      : Ptr(Ptr), Size(Size), AATags(AATags) {}
  /// Construct a location at \p Ptr with precise size \p Size and optional
  /// \p AATags.
  /// @param Ptr Base pointer of the location.
  /// @param Size Exact size as a TypeSize.
  /// @param AATags Optional alias analysis metadata for the location.
  explicit MemoryLocation(const Value *Ptr, TypeSize Size,
                          const AAMDNodes &AATags = AAMDNodes())
      : Ptr(Ptr), Size(LocationSize::precise(Size)), AATags(AATags) {}
  /// Construct a location at \p Ptr with precise size \p Size and optional
  /// \p AATags.
  /// @param Ptr Base pointer of the location.
  /// @param Size Exact size in address-units.
  /// @param AATags Optional alias analysis metadata for the location.
  explicit MemoryLocation(const Value *Ptr, uint64_t Size,
                          const AAMDNodes &AATags = AAMDNodes())
      : Ptr(Ptr), Size(LocationSize::precise(Size)), AATags(AATags) {}

  /// Return a copy of this location with pointer replaced by \p NewPtr.
  /// @param NewPtr Replacement base pointer.
  /// @return Copy of this location with pointer replaced by \p NewPtr.
  MemoryLocation getWithNewPtr(const Value *NewPtr) const {
    MemoryLocation Copy(*this);
    Copy.Ptr = NewPtr;
    return Copy;
  }

  /// Return a copy of this location with size replaced by \p NewSize.
  /// @param NewSize Replacement LocationSize.
  /// @return Copy of this location with size replaced by \p NewSize.
  MemoryLocation getWithNewSize(LocationSize NewSize) const {
    MemoryLocation Copy(*this);
    Copy.Size = NewSize;
    return Copy;
  }
  /// Return a copy of this location with a precise size of \p NewSize.
  /// @param NewSize Exact size in address-units.
  /// @return Copy of this location with a precise size of \p NewSize.
  MemoryLocation getWithNewSize(uint64_t NewSize) const {
    return getWithNewSize(LocationSize::precise(NewSize));
  }
  /// Return a copy of this location with a precise size of \p NewSize.
  /// @param NewSize Exact size as a TypeSize.
  /// @return Copy of this location with a precise size of \p NewSize.
  MemoryLocation getWithNewSize(TypeSize NewSize) const {
    return getWithNewSize(LocationSize::precise(NewSize));
  }

  /// Return a copy of this location with alias analysis tags cleared.
  /// @return Copy of this location with alias analysis tags cleared.
  MemoryLocation getWithoutAATags() const {
    MemoryLocation Copy(*this);
    Copy.AATags = AAMDNodes();
    return Copy;
  }

  /// Return true if this location equals \p Other.
  /// @param Other Location to compare against.
  /// @return True if this location equals \p Other.
  bool operator==(const MemoryLocation &Other) const {
    return Ptr == Other.Ptr && Size == Other.Size && AATags == Other.AATags;
  }
};

/// Provide DenseMapInfo for LocationSize.
template <> struct DenseMapInfo<LocationSize> {
  /// Hash \p Val for use as a DenseMap key.
  /// @param Val LocationSize to hash.
  /// @return Hash value for \p Val.
  static unsigned getHashValue(const LocationSize &Val) {
    return DenseMapInfo<uint64_t>::getHashValue(Val.toRaw());
  }
  /// Return true if \p LHS and \p RHS represent the same size.
  /// @param LHS First LocationSize.
  /// @param RHS Second LocationSize.
  /// @return True if \p LHS and \p RHS represent the same size.
  static bool isEqual(const LocationSize &LHS, const LocationSize &RHS) {
    return LHS == RHS;
  }
};

/// Provide DenseMapInfo for MemoryLocation.
template <> struct DenseMapInfo<MemoryLocation> {
  /// Hash \p Val for use as a DenseMap key.
  /// @param Val MemoryLocation to hash.
  /// @return Hash value for \p Val.
  static unsigned getHashValue(const MemoryLocation &Val) {
    return DenseMapInfo<const Value *>::getHashValue(Val.Ptr) ^
           DenseMapInfo<LocationSize>::getHashValue(Val.Size) ^
           DenseMapInfo<AAMDNodes>::getHashValue(Val.AATags);
  }
  /// Return true if \p LHS and \p RHS represent the same location.
  /// @param LHS First MemoryLocation.
  /// @param RHS Second MemoryLocation.
  /// @return True if \p LHS and \p RHS represent the same location.
  static bool isEqual(const MemoryLocation &LHS, const MemoryLocation &RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

#endif
