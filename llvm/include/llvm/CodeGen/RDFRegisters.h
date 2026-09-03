//===- RDFRegisters.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_RDFREGISTERS_H
#define LLVM_CODEGEN_RDFREGISTERS_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegister.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace llvm {

class MachineFunction;
class raw_ostream;

namespace rdf {
struct RegisterAggr;

/// Dense identifier for a physical register, register unit, or register mask.
using RegisterId = uint32_t;

/// Return true if sets \p A and \p B have no elements in common.
/// \param A First ordered set.
/// \param B Second ordered set.
/// \return True when the intersection of \p A and \p B is empty.
template <typename T>
bool disjoint(const std::set<T> &A, const std::set<T> &B) {
  auto ItA = A.begin(), EndA = A.end();
  auto ItB = B.begin(), EndB = B.end();
  while (ItA != EndA && ItB != EndB) {
    if (*ItA < *ItB)
      ++ItA;
    else if (*ItB < *ItA)
      ++ItB;
    else
      return false;
  }
  return true;
}

/// Indexed set mapping dense \c uint32_t indices to values of type \p T.
///
/// Acts like an indexed set: upon insertion of a new object, it automatically
/// assigns a new index to it. Index of 0 is treated as invalid and is never
/// allocated.
template <typename T, unsigned N = 32> struct IndexedSet {
  /// Construct an empty indexed set with reserved capacity \p N.
  IndexedSet() { Map.reserve(N); }

  /// Return the value stored at index \p Idx.
  ///
  /// Index \p Idx corresponds to \c Map[Idx-1].
  /// \param Idx One-based index previously returned by \c insert or \c find.
  /// \return Value associated with \p Idx.
  T get(uint32_t Idx) const {
    // Index Idx corresponds to Map[Idx-1].
    assert(Idx != 0 && !Map.empty() && Idx - 1 < Map.size());
    return Map[Idx - 1];
  }

  /// Insert \p Val if absent and return its one-based index.
  ///
  /// Existing values are looked up with a linear search; duplicate inserts
  /// return the previously assigned index.
  /// \param Val Value to insert or look up.
  /// \return One-based index of \p Val (actual vector index plus one).
  uint32_t insert(T Val) {
    // Linear search.
    auto F = llvm::find(Map, Val);
    if (F != Map.end())
      return F - Map.begin() + 1;
    Map.push_back(Val);
    return Map.size(); // Return actual_index + 1.
  }

  /// Return the one-based index of an existing value \p Val.
  /// \param Val Value previously inserted into the set.
  /// \return One-based index of \p Val.
  uint32_t find(T Val) const {
    auto F = llvm::find(Map, Val);
    assert(F != Map.end());
    return F - Map.begin() + 1;
  }

  /// Return the number of stored values.
  /// \return Count of elements in the underlying vector.
  uint32_t size() const { return Map.size(); }

  /// Const iterator over stored values in insertion order.
  using const_iterator = typename std::vector<T>::const_iterator;

  /// Return an iterator to the first stored value.
  /// \return Begin iterator over the value vector.
  const_iterator begin() const { return Map.begin(); }
  /// Return an iterator past the last stored value.
  /// \return End iterator over the value vector.
  const_iterator end() const { return Map.end(); }

private:
  std::vector<T> Map;
};

/// Reference to a physical register, register unit, or register mask.
///
/// Encodes the kind of reference in the high bits of \c Id. For register
/// references, \c Mask selects which lanes are included.
struct RegisterRef {
private:
  static constexpr RegisterId MaskFlag = 1u << 30;
  static constexpr RegisterId UnitFlag = 1u << 31;

public:
  /// Encoded register, unit, or mask identifier (0 is the null register).
  RegisterId Id = 0;
  /// Lane mask for register references; unused for units and masks.
  LaneBitmask Mask = LaneBitmask::getNone(); // Only for registers.

  /// Construct a null register reference.
  constexpr RegisterRef() = default;
  /// Construct a reference to register, unit, or mask id \p R.
  ///
  /// When \p R is a register id, \p M selects the included lanes; otherwise the
  /// lane mask is cleared.
  /// \param R Encoded register, unit, or mask identifier.
  /// \param M Lane mask applied when \p R is a register id.
  constexpr explicit RegisterRef(RegisterId R,
                                 LaneBitmask M = LaneBitmask::getAll())
      : Id(R), Mask(isRegId(R) && R != 0 ? M : LaneBitmask::getNone()) {}

  /// Return true if this reference denotes a physical register.
  ///
  /// Classifies the null register (id 0) as a register.
  /// \return True when the id is zero or a plain register id.
  constexpr bool isReg() const { return Id == 0 || isRegId(Id); }
  /// Return true if this reference denotes a register unit.
  /// \return True when the unit flag is set in \c Id.
  constexpr bool isUnit() const { return isUnitId(Id); }
  /// Return true if this reference denotes a register mask.
  /// \return True when the mask flag is set in \c Id.
  constexpr bool isMask() const { return isMaskId(Id); }

  /// Return the physical register encoded by this reference.
  /// \return \c Id interpreted as an \c MCRegister.
  constexpr MCRegister asMCReg() const {
    assert(isReg());
    return Id;
  }

  /// Return the register unit encoded by this reference.
  /// \return Unit index with the unit flag cleared.
  constexpr MCRegUnit asMCRegUnit() const {
    assert(isUnit());
    return static_cast<MCRegUnit>(Id & ~UnitFlag);
  }

  /// Return the register-mask index encoded by this reference.
  /// \return Mask index with the mask flag cleared.
  constexpr unsigned asMaskIdx() const {
    assert(isMask());
    return Id & ~MaskFlag;
  }

  /// Return true if this reference is non-empty.
  ///
  /// Register references require a non-zero id and at least one lane bit;
  /// unit and mask references are always considered non-empty.
  /// \return True when the reference denotes a meaningful operand.
  explicit constexpr operator bool() const {
    return !isReg() || (Id != 0 && Mask.any());
  }

  /// Compute a hash combining the id and lane mask.
  /// \return Hash value suitable for unordered containers.
  size_t hash() const {
    return std::hash<RegisterId>{}(Id) ^
           std::hash<LaneBitmask::Type>{}(Mask.getAsInteger());
  }

  /// Return true if \p Id encodes a physical register (not unit or mask).
  /// \param Id Identifier to classify.
  /// \return True when neither unit nor mask flags are set and \p Id is non-zero.
  static constexpr bool isRegId(RegisterId Id) {
    return Id != 0 && !(Id & UnitFlag) && !(Id & MaskFlag);
  }
  /// Return true if \p Id encodes a register unit.
  /// \param Id Identifier to classify.
  /// \return True when the unit flag is set.
  static constexpr bool isUnitId(RegisterId Id) { return Id & UnitFlag; }
  /// Return true if \p Id encodes a register mask.
  /// \param Id Identifier to classify.
  /// \return True when the mask flag is set.
  static constexpr bool isMaskId(RegisterId Id) { return Id & MaskFlag; }

  /// Encode register-unit index \p Idx as a \c RegisterId.
  /// \param Idx Zero-based register-unit index.
  /// \return Identifier with the unit flag set.
  static constexpr RegisterId toUnitId(unsigned Idx) { return Idx | UnitFlag; }

  /// Encode register-mask index \p Idx as a \c RegisterId.
  /// \param Idx Zero-based register-mask index.
  /// \return Identifier with the mask flag set.
  static constexpr RegisterId toMaskId(unsigned Idx) { return Idx | MaskFlag; }

  /// Ordering is not supported; use \c PhysicalRegisterInfo::less instead.
  /// \param Other Unused; operator is deleted.
  bool operator<(RegisterRef Other) const = delete;
  /// Equality is not supported; use \c PhysicalRegisterInfo::equal_to instead.
  /// \param Other Unused; operator is deleted.
  bool operator==(RegisterRef Other) const = delete;
  /// Inequality is not supported; use \c PhysicalRegisterInfo::equal_to instead.
  /// \param Other Unused; operator is deleted.
  bool operator!=(RegisterRef Other) const = delete;
};

/// Target register information helpers for RDF register references.
struct PhysicalRegisterInfo {
  /// Build register, unit, mask, and alias tables for \p mf.
  /// \param tri Target register info for the function's subtarget.
  /// \param mf Machine function whose register masks are collected.
  LLVM_ABI PhysicalRegisterInfo(const TargetRegisterInfo &tri,
                                const MachineFunction &mf);

  /// Return the \c RegisterId for register-mask bitset \p RM.
  /// \param RM Pointer to a register-mask bit array.
  /// \return Mask id obtained by indexing the internal mask set.
  RegisterId getRegMaskId(const uint32_t *RM) const {
    return RegisterRef::toMaskId(RegMasks.find(RM));
  }

  /// Return the register-mask bit array for mask reference \p RR.
  /// \param RR Mask-typed register reference.
  /// \return Pointer to the stored mask bits.
  const uint32_t *getRegMaskBits(RegisterRef RR) const {
    return RegMasks.get(RR.asMaskIdx());
  }

  /// Return true if register references \p RA and \p RB share any units.
  /// \param RA First register reference.
  /// \param RB Second register reference.
  /// \return True when the unit sets of \p RA and \p RB are not disjoint.
  LLVM_ABI bool alias(RegisterRef RA, RegisterRef RB) const;

  /// Return the set of physical registers aliased by \p RR.
  ///
  /// Does not include \p RR itself. Units are not allowed.
  /// \param RR Register or mask reference.
  /// \return Set of aliased register ids.
  LLVM_ABI std::set<RegisterId> getAliasSet(RegisterRef RR) const;

  /// Return a register reference covering register unit \p U.
  /// \param U Machine register unit.
  /// \return Reference with the unit's root register and lane mask.
  RegisterRef getRefForUnit(MCRegUnit U) const {
    return RegisterRef(UnitInfos[U].Reg, UnitInfos[U].Mask);
  }

  /// Return the bitvector of units preserved by mask reference \p RR.
  /// \param RR Mask-typed register reference.
  /// \return Units not clobbered by the mask.
  const BitVector &getMaskUnits(RegisterRef RR) const {
    return MaskInfos[RR.asMaskIdx()].Units;
  }

  /// Return the set of register-unit ids covered by \p RR.
  /// \param RR Register or mask reference.
  /// \return Set of intersecting register-unit identifiers.
  LLVM_ABI std::set<RegisterId> getUnits(RegisterRef RR) const;

  /// Return the bitvector of registers that alias register unit \p U.
  /// \param U Machine register unit.
  /// \return Registers that include \p U among their units.
  const BitVector &getUnitAliases(MCRegUnit U) const {
    return AliasInfos[U].Regs;
  }

  /// Remap register reference \p RR onto related register \p R.
  ///
  /// Composes or reverse-composes subregister lane masks as needed.
  /// \param RR Source register reference.
  /// \param R Destination register id related to \p RR by subregister.
  /// \return Equivalent reference expressed in terms of \p R.
  LLVM_ABI RegisterRef mapTo(RegisterRef RR, RegisterId R) const;
  /// Return the underlying target register info.
  /// \return Reference to the \c TargetRegisterInfo used at construction.
  const TargetRegisterInfo &getTRI() const { return TRI; }

  /// Compare register references \p A and \p B for equality.
  /// \param A First register reference.
  /// \param B Second register reference.
  /// \return True when \p A and \p B denote the same register content.
  LLVM_ABI bool equal_to(RegisterRef A, RegisterRef B) const;
  /// Compare register references \p A and \p B with a total order.
  /// \param A First register reference.
  /// \param B Second register reference.
  /// \return True when \p A is ordered before \p B.
  LLVM_ABI bool less(RegisterRef A, RegisterRef B) const;

  /// Print register reference \p A to stream \p OS.
  /// \param OS Output stream.
  /// \param A Register reference to print.
  LLVM_ABI void print(raw_ostream &OS, RegisterRef A) const;
  /// Print register aggregate \p A to stream \p OS.
  /// \param OS Output stream.
  /// \param A Register aggregate to print.
  LLVM_ABI void print(raw_ostream &OS, const RegisterAggr &A) const;

private:
  struct RegInfo {
    const TargetRegisterClass *RegClass = nullptr;
  };
  struct UnitInfo {
    RegisterId Reg = 0;
    LaneBitmask Mask;
  };
  struct MaskInfo {
    BitVector Units;
  };
  struct AliasInfo {
    BitVector Regs;
  };

  const TargetRegisterInfo &TRI;
  IndexedSet<const uint32_t *> RegMasks;
  std::vector<RegInfo> RegInfos;
  IndexedMap<UnitInfo, MCRegUnitToIndex> UnitInfos;
  std::vector<MaskInfo> MaskInfos;
  IndexedMap<AliasInfo, MCRegUnitToIndex> AliasInfos;
};

/// Equality functor for \c RegisterRef using \c PhysicalRegisterInfo.
struct RegisterRefEqualTo {
  /// Construct a comparator bound to physical register info \p pri.
  /// \param pri Physical register info used for equality checks.
  constexpr RegisterRefEqualTo(const llvm::rdf::PhysicalRegisterInfo &pri)
      : PRI(&pri) {}

  /// Return true if \p A and \p B are equal under the bound PRI.
  /// \param A First register reference.
  /// \param B Second register reference.
  /// \return Result of \c PhysicalRegisterInfo::equal_to.
  bool operator()(llvm::rdf::RegisterRef A, llvm::rdf::RegisterRef B) const {
    return PRI->equal_to(A, B);
  }

private:
  // Make it a pointer just in case. See comment in `RegisterRefLess` below.
  const llvm::rdf::PhysicalRegisterInfo *PRI;
};

/// Less-than functor for \c RegisterRef using \c PhysicalRegisterInfo.
struct RegisterRefLess {
  /// Construct a comparator bound to physical register info \p pri.
  /// \param pri Physical register info used for ordering.
  constexpr RegisterRefLess(const llvm::rdf::PhysicalRegisterInfo &pri)
      : PRI(&pri) {}

  /// Return true if \p A is ordered before \p B under the bound PRI.
  /// \param A First register reference.
  /// \param B Second register reference.
  /// \return Result of \c PhysicalRegisterInfo::less.
  bool operator()(llvm::rdf::RegisterRef A, llvm::rdf::RegisterRef B) const {
    return PRI->less(A, B);
  }

private:
  // Make it a pointer because apparently some versions of MSVC use std::swap
  // on the comparator object.
  const llvm::rdf::PhysicalRegisterInfo *PRI;
};

/// Aggregate of register units representing a set of physical registers.
struct RegisterAggr {
  /// Construct an empty aggregate using physical register info \p pri.
  /// \param pri Physical register info providing the unit count.
  RegisterAggr(const PhysicalRegisterInfo &pri)
      : Units(pri.getTRI().getNumRegUnits()), PRI(pri) {}
  /// Copy-construct a register aggregate.
  /// \param RG Aggregate to copy.
  RegisterAggr(const RegisterAggr &RG) = default;

  /// Return the number of register units in the aggregate.
  /// \return Count of set bits in the unit bitvector.
  unsigned size() const { return Units.count(); }
  /// Return true if the aggregate contains no register units.
  /// \return True when the unit bitvector is empty.
  bool empty() const { return Units.none(); }
  /// Return true if this aggregate aliases register reference \p RR.
  /// \param RR Register reference to test for aliasing.
  /// \return True when any unit of \p RR is present.
  LLVM_ABI bool hasAliasOf(RegisterRef RR) const;
  /// Return true if this aggregate fully covers register reference \p RR.
  /// \param RR Register reference to test for coverage.
  /// \return True when every unit of \p RR is present.
  LLVM_ABI bool hasCoverOf(RegisterRef RR) const;

  /// Return the physical register info associated with this aggregate.
  /// \return Reference to the bound \c PhysicalRegisterInfo.
  const PhysicalRegisterInfo &getPRI() const { return PRI; }

  /// Return true if this aggregate has the same units as \p A.
  /// \param A Other aggregate to compare.
  /// \return True when both unit bitvectors are equal.
  bool operator==(const RegisterAggr &A) const {
    return DenseMapInfo<BitVector>::isEqual(Units, A.Units);
  }

  /// Return true if register reference \p RA covers \p RB.
  /// \param RA Candidate covering reference.
  /// \param RB Reference that must be covered.
  /// \param PRI Physical register info used to expand units.
  /// \return True when an aggregate containing \p RA covers \p RB.
  static bool isCoverOf(RegisterRef RA, RegisterRef RB,
                        const PhysicalRegisterInfo &PRI) {
    return RegisterAggr(PRI).insert(RA).hasCoverOf(RB);
  }

  /// Insert the units of register reference \p RR into this aggregate.
  /// \param RR Register reference whose units are added.
  /// \return Reference to this aggregate.
  LLVM_ABI RegisterAggr &insert(RegisterRef RR);
  /// Insert all units from aggregate \p RG into this aggregate.
  /// \param RG Aggregate whose units are added.
  /// \return Reference to this aggregate.
  LLVM_ABI RegisterAggr &insert(const RegisterAggr &RG);
  /// Intersect this aggregate with the units of register reference \p RR.
  /// \param RR Register reference providing the intersection mask.
  /// \return Reference to this aggregate.
  LLVM_ABI RegisterAggr &intersect(RegisterRef RR);
  /// Intersect this aggregate with the units of aggregate \p RG.
  /// \param RG Aggregate providing the intersection mask.
  /// \return Reference to this aggregate.
  LLVM_ABI RegisterAggr &intersect(const RegisterAggr &RG);
  /// Remove the units of register reference \p RR from this aggregate.
  /// \param RR Register reference whose units are cleared.
  /// \return Reference to this aggregate.
  LLVM_ABI RegisterAggr &clear(RegisterRef RR);
  /// Remove all units of aggregate \p RG from this aggregate.
  /// \param RG Aggregate whose units are cleared.
  /// \return Reference to this aggregate.
  LLVM_ABI RegisterAggr &clear(const RegisterAggr &RG);

  /// Return the intersection of this aggregate with register reference \p RR.
  /// \param RR Register reference to intersect with.
  /// \return Register reference representing the overlapping lanes, if any.
  LLVM_ABI RegisterRef intersectWith(RegisterRef RR) const;
  /// Return \p RR with units present in this aggregate removed.
  /// \param RR Register reference to subtract from.
  /// \return Remainder of \p RR after clearing covered units.
  LLVM_ABI RegisterRef clearIn(RegisterRef RR) const;
  /// Attempt to express this aggregate as a single register reference.
  /// \return Combined register reference when the units form one register.
  LLVM_ABI RegisterRef makeRegRef() const;

  /// Compute a hash of the unit bitvector.
  /// \return Hash value suitable for unordered containers.
  size_t hash() const { return DenseMapInfo<BitVector>::getHashValue(Units); }

  /// Iterator yielding \c RegisterRef values covering this aggregate.
  struct ref_iterator {
    /// Map from register id to the lanes of that register present in the
    /// aggregate.
    using MapType = std::map<RegisterId, LaneBitmask>;

  private:
    MapType Masks;
    MapType::iterator Pos;
    unsigned Index;
    const RegisterAggr *Owner;

  public:
    /// Construct a begin or end iterator over the references in \p RG.
    /// \param RG Aggregate whose register references are visited.
    /// \param End When true, position the iterator at the end.
    LLVM_ABI ref_iterator(const RegisterAggr &RG, bool End);

    /// Return the current register reference.
    /// \return Reference built from the current map entry.
    RegisterRef operator*() const {
      return RegisterRef(Pos->first, Pos->second);
    }

    /// Advance to the next register reference.
    /// \return Reference to this iterator.
    ref_iterator &operator++() {
      ++Pos;
      ++Index;
      return *this;
    }

    /// Return true if this iterator equals \p I.
    /// \param I Other iterator over the same aggregate.
    /// \return True when both iterators share the same index.
    bool operator==(const ref_iterator &I) const {
      assert(Owner == I.Owner);
      (void)Owner;
      return Index == I.Index;
    }

    /// Return true if this iterator differs from \p I.
    /// \param I Other iterator over the same aggregate.
    /// \return True when the iterators are not equal.
    bool operator!=(const ref_iterator &I) const { return !(*this == I); }
  };

  /// Return a begin iterator over register references in this aggregate.
  /// \return Iterator positioned at the first reference.
  ref_iterator ref_begin() const { return ref_iterator(*this, false); }
  /// Return an end iterator over register references in this aggregate.
  /// \return Iterator positioned past the last reference.
  ref_iterator ref_end() const { return ref_iterator(*this, true); }

  /// Iterator over set bits in the unit bitvector.
  using unit_iterator = BitVector::const_set_bits_iterator;
  /// Return a begin iterator over register units in this aggregate.
  /// \return Iterator to the first set unit bit.
  unit_iterator unit_begin() const { return Units.set_bits_begin(); }
  /// Return an end iterator over register units in this aggregate.
  /// \return Iterator past the last set unit bit.
  unit_iterator unit_end() const { return Units.set_bits_end(); }

  /// Return a range over the register references in this aggregate.
  /// \return Iterator range from \c ref_begin to \c ref_end.
  iterator_range<ref_iterator> refs() const {
    return make_range(ref_begin(), ref_end());
  }
  /// Return a range over the register units in this aggregate.
  /// \return Iterator range from \c unit_begin to \c unit_end.
  iterator_range<unit_iterator> units() const {
    return make_range(unit_begin(), unit_end());
  }

private:
  BitVector Units;
  const PhysicalRegisterInfo &PRI;
};

/// Map from keys to \c RegisterAggr values with PRI-aware defaults.
///
/// This is really a \c std::map, except that it provides a non-trivial default
/// constructor to the element accessed via \c [].
template <typename KeyType> struct RegisterAggrMap {
  /// Construct a map whose default aggregates use physical register info \p pri.
  /// \param pri Physical register info used to initialize empty aggregates.
  RegisterAggrMap(const PhysicalRegisterInfo &pri) : Empty(pri) {}

  /// Return the aggregate for \p Key, inserting an empty one if absent.
  /// \param Key Map key to look up or insert.
  /// \return Reference to the aggregate associated with \p Key.
  RegisterAggr &operator[](KeyType Key) {
    return Map.emplace(Key, Empty).first->second;
  }

  /// Return an iterator to the first key/aggregate pair.
  /// \return Begin iterator over the underlying map.
  auto begin() { return Map.begin(); }
  /// Return an iterator past the last key/aggregate pair.
  /// \return End iterator over the underlying map.
  auto end() { return Map.end(); }
  /// Return a const iterator to the first key/aggregate pair.
  /// \return Const begin iterator over the underlying map.
  auto begin() const { return Map.begin(); }
  /// Return a const iterator past the last key/aggregate pair.
  /// \return Const end iterator over the underlying map.
  auto end() const { return Map.end(); }
  /// Find the aggregate associated with \p Key.
  /// \param Key Map key to look up.
  /// \return Const iterator to the entry, or \c end if absent.
  auto find(const KeyType &Key) const { return Map.find(Key); }

private:
  RegisterAggr Empty;
  std::map<KeyType, RegisterAggr> Map;

public:
  /// Key type of the underlying map.
  using key_type = typename decltype(Map)::key_type;
  /// Mapped \c RegisterAggr type of the underlying map.
  using mapped_type = typename decltype(Map)::mapped_type;
  /// Value type (\c std::pair of key and aggregate) of the underlying map.
  using value_type = typename decltype(Map)::value_type;
};

/// Write register aggregate \p A to stream \p OS.
/// \param OS Output stream.
/// \param A Aggregate to print.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const RegisterAggr &A);

/// Helper that prints a lane mask in short form.
///
/// Prints the lane mask in a short form (or not at all if all bits are set).
struct PrintLaneMaskShort {
  /// Construct a printer for lane mask \p M.
  /// \param M Lane mask to print.
  PrintLaneMaskShort(LaneBitmask M) : Mask(M) {}
  /// Lane mask to print.
  LaneBitmask Mask;
};
/// Write short-form lane mask \p P to stream \p OS.
/// \param OS Output stream.
/// \param P Printer holding the lane mask.
/// \return Reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const PrintLaneMaskShort &P);

} // end namespace rdf
} // end namespace llvm

namespace std {

template <> struct hash<llvm::rdf::RegisterRef> {
  size_t operator()(llvm::rdf::RegisterRef A) const { //
    return A.hash();
  }
};

template <> struct hash<llvm::rdf::RegisterAggr> {
  size_t operator()(const llvm::rdf::RegisterAggr &A) const { //
    return A.hash();
  }
};

} // namespace std

namespace llvm::rdf {
using RegisterSet = std::set<RegisterRef, RegisterRefLess>;
} // namespace llvm::rdf

#endif // LLVM_CODEGEN_RDFREGISTERS_H
