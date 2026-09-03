//===--- ModRef.h - Memory effect modeling ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Definitions of ModRefInfo and MemoryEffects, which are used to
// describe the memory effects of instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MODREF_H
#define LLVM_SUPPORT_MODREF_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Flags indicating whether a memory access modifies or references memory.
///
/// This is no access at all, a modification, a reference, or both
/// a modification and a reference.
enum class ModRefInfo : uint8_t {
  /// The access neither references nor modifies the value stored in memory.
  NoModRef = 0,
  /// The access may reference the value stored in memory.
  Ref = 1,
  /// The access may modify the value stored in memory.
  Mod = 2,
  /// The access may reference and may modify the value stored in memory.
  ModRef = Ref | Mod,
  LLVM_MARK_AS_BITMASK_ENUM(ModRef),
};

/// Return whether \p MRI indicates neither modification nor reference.
/// \param MRI Mod/ref flags to test.
/// \return True if \p MRI indicates neither modification nor reference.
[[nodiscard]] inline bool isNoModRef(const ModRefInfo MRI) {
  return MRI == ModRefInfo::NoModRef;
}
/// Return true if \p MRI indicates any memory modification or reference.
/// \param MRI Mod/ref flags to test.
/// \return True if \p MRI indicates any memory modification or reference.
[[nodiscard]] inline bool isModOrRefSet(const ModRefInfo MRI) {
  return MRI != ModRefInfo::NoModRef;
}
/// Return true if \p MRI indicates both modification and reference.
/// \param MRI Mod/ref flags to test.
/// \return True if \p MRI indicates both modification and reference.
[[nodiscard]] inline bool isModAndRefSet(const ModRefInfo MRI) {
  return MRI == ModRefInfo::ModRef;
}
/// Return whether \p MRI includes a possible memory modification.
/// \param MRI Mod/ref flags to test.
/// \return True if \p MRI includes a possible memory modification.
[[nodiscard]] inline bool isModSet(const ModRefInfo MRI) {
  return static_cast<int>(MRI) & static_cast<int>(ModRefInfo::Mod);
}
/// Return whether \p MRI includes a possible memory reference.
/// \param MRI Mod/ref flags to test.
/// \return True if \p MRI includes a possible memory reference.
[[nodiscard]] inline bool isRefSet(const ModRefInfo MRI) {
  return static_cast<int>(MRI) & static_cast<int>(ModRefInfo::Ref);
}

/// Debug print ModRefInfo.
/// \param OS Stream to write to.
/// \param MR Mod/ref flags to print.
/// \return A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, ModRefInfo MR);

/// The locations at which a function might access memory.
enum class IRMemLocation {
  /// Access to memory via argument pointers.
  ArgMem = 0,
  /// Memory that is inaccessible via LLVM IR.
  InaccessibleMem = 1,
  /// Errno memory.
  ErrnoMem = 2,
  /// Any other memory.
  Other = 3,
  /// Represents target specific state.
  TargetMem0 = 4,
  TargetMem1 = 5,

  /// Helpers to iterate all locations in the MemoryEffectsBase class.
  First = ArgMem,
  Last = TargetMem1,
};

/// Bitmask of Mod/Ref effects for each memory location kind in \c LocationEnum.
template <typename LocationEnum> class MemoryEffectsBase {
public:
  /// Memory location kind parameterized by \c LocationEnum (for example
  /// \c IRMemLocation in \c MemoryEffects).
  using Location = LocationEnum;

private:
  uint32_t Data = 0;

  static constexpr uint32_t BitsPerLoc = 2;
  static constexpr uint32_t LocMask = (1 << BitsPerLoc) - 1;

  static uint32_t getLocationPos(Location Loc) {
    return (uint32_t)Loc * BitsPerLoc;
  }

  MemoryEffectsBase(uint32_t Data) : Data(Data) {}

  void setModRef(Location Loc, ModRefInfo MR) {
    Data &= ~(LocMask << getLocationPos(Loc));
    Data |= static_cast<uint32_t>(MR) << getLocationPos(Loc);
  }

public:
  /// Returns iterator over all supported location kinds.
  /// \return Iterator range over all supported location kinds.
  static auto locations() {
    return enum_seq_inclusive(Location::First, Location::Last,
                              force_iteration_on_noniterable_enum);
  }
  /// Returns iterator over all target location kinds
  /// \return Iterator range over all target location kinds.
  static auto targetMemLocations() {
    return enum_seq_inclusive(Location::TargetMem0, Location::TargetMem1,
                              force_iteration_on_noniterable_enum);
  }

  /// Create MemoryEffectsBase that can access only the given location with the
  /// given ModRefInfo.
  /// \param Loc Memory location that may be accessed.
  /// \param MR Mod/ref behavior allowed for \p Loc.
  MemoryEffectsBase(Location Loc, ModRefInfo MR) { setModRef(Loc, MR); }

  /// Create MemoryEffectsBase that can access any location with the given
  /// ModRefInfo.
  /// \param MR Mod/ref behavior applied to every location.
  explicit MemoryEffectsBase(ModRefInfo MR) {
    for (Location Loc : locations())
      setModRef(Loc, MR);
  }

  /// Create MemoryEffectsBase that can read and write any memory.
  /// \return Effects that may read and write any memory.
  static MemoryEffectsBase unknown() {
    return MemoryEffectsBase(ModRefInfo::ModRef);
  }

  /// Create MemoryEffectsBase that cannot read or write any memory.
  /// \return Effects that access no memory.
  static MemoryEffectsBase none() {
    return MemoryEffectsBase(ModRefInfo::NoModRef);
  }

  /// Create MemoryEffectsBase that can read any memory.
  /// \return Effects that may read any memory.
  static MemoryEffectsBase readOnly() {
    return MemoryEffectsBase(ModRefInfo::Ref);
  }

  /// Create MemoryEffectsBase that can write any memory.
  /// \return Effects that may write any memory.
  static MemoryEffectsBase writeOnly() {
    return MemoryEffectsBase(ModRefInfo::Mod);
  }

  /// Create MemoryEffectsBase that can only access argument memory.
  /// \param MR Mod/ref behavior for argument memory.
  /// \return Effects limited to argument memory.
  static MemoryEffectsBase argMemOnly(ModRefInfo MR = ModRefInfo::ModRef) {
    return MemoryEffectsBase(Location::ArgMem, MR);
  }

  /// Create MemoryEffectsBase that can only access inaccessible memory.
  /// \param MR Mod/ref behavior for inaccessible memory.
  /// \return Effects limited to inaccessible memory.
  static MemoryEffectsBase
  inaccessibleMemOnly(ModRefInfo MR = ModRefInfo::ModRef) {
    return MemoryEffectsBase(Location::InaccessibleMem, MR);
  }

  /// Create MemoryEffectsBase that can only access errno memory.
  /// \param MR Mod/ref behavior for errno memory.
  /// \return Effects limited to errno memory.
  static MemoryEffectsBase errnoMemOnly(ModRefInfo MR = ModRefInfo::ModRef) {
    return MemoryEffectsBase(Location::ErrnoMem, MR);
  }

  /// Create MemoryEffectsBase that can only access other memory.
  /// \param MR Mod/ref behavior for other memory.
  /// \return Effects limited to other memory.
  static MemoryEffectsBase otherMemOnly(ModRefInfo MR = ModRefInfo::ModRef) {
    return MemoryEffectsBase(Location::Other, MR);
  }

  /// Create MemoryEffectsBase that can only access inaccessible or argument
  /// memory.
  /// \param MR Mod/ref behavior for inaccessible and argument memory.
  /// \return Effects limited to inaccessible and argument memory.
  static MemoryEffectsBase
  inaccessibleOrArgMemOnly(ModRefInfo MR = ModRefInfo::ModRef) {
    MemoryEffectsBase FRMB = none();
    FRMB.setModRef(Location::ArgMem, MR);
    FRMB.setModRef(Location::InaccessibleMem, MR);
    return FRMB;
  }

  /// Create MemoryEffectsBase that can only access inaccessible or errno
  /// memory.
  /// \param InaccessibleMR Mod/ref behavior for inaccessible memory.
  /// \param ErrnoMR Mod/ref behavior for errno memory.
  /// \return Effects limited to inaccessible and errno memory.
  static MemoryEffectsBase
  inaccessibleOrErrnoMemOnly(ModRefInfo InaccessibleMR = ModRefInfo::ModRef,
                             ModRefInfo ErrnoMR = ModRefInfo::ModRef) {
    MemoryEffectsBase FRMB = none();
    FRMB.setModRef(Location::InaccessibleMem, InaccessibleMR);
    FRMB.setModRef(Location::ErrnoMem, ErrnoMR);
    return FRMB;
  }

  /// Create MemoryEffectsBase that can only access inaccessible, argument or
  /// errno memory.
  /// \param InaccessibleOrArgMR Mod/ref behavior for inaccessible and argument
  ///        memory.
  /// \param ErrnoMR Mod/ref behavior for errno memory.
  /// \return Effects limited to inaccessible, argument, and errno memory.
  static MemoryEffectsBase inaccessibleOrArgOrErrnoMemOnly(
      ModRefInfo InaccessibleOrArgMR = ModRefInfo::ModRef,
      ModRefInfo ErrnoMR = ModRefInfo::ModRef) {
    MemoryEffectsBase FRMB = none();
    FRMB.setModRef(Location::InaccessibleMem, InaccessibleOrArgMR);
    FRMB.setModRef(Location::ArgMem, InaccessibleOrArgMR);
    FRMB.setModRef(Location::ErrnoMem, ErrnoMR);
    return FRMB;
  }

  /// Create MemoryEffectsBase that can only access argument or errno memory.
  /// \param ArgMR Mod/ref behavior for argument memory.
  /// \param ErrnoMR Mod/ref behavior for errno memory.
  /// \return Effects limited to argument and errno memory.
  static MemoryEffectsBase
  argumentOrErrnoMemOnly(ModRefInfo ArgMR = ModRefInfo::ModRef,
                         ModRefInfo ErrnoMR = ModRefInfo::ModRef) {
    MemoryEffectsBase FRMB = none();
    FRMB.setModRef(Location::ArgMem, ArgMR);
    FRMB.setModRef(Location::ErrnoMem, ErrnoMR);
    return FRMB;
  }

  /// Create MemoryEffectsBase from an encoded integer value (used by memory
  /// attribute).
  /// \param Data Encoded bitfield of per-location ModRefInfo values.
  /// \return MemoryEffectsBase decoded from \p Data.
  static MemoryEffectsBase createFromIntValue(uint32_t Data) {
    return MemoryEffectsBase(Data);
  }

  /// Convert MemoryEffectsBase into an encoded integer value (used by memory
  /// attribute).
  /// \return Encoded integer representation of these effects.
  uint32_t toIntValue() const {
    return Data;
  }

  /// Get ModRefInfo for the given Location.
  /// \param Loc Memory location whose ModRefInfo to return.
  /// \return ModRefInfo for \p Loc.
  ModRefInfo getModRef(Location Loc) const {
    return ModRefInfo((Data >> getLocationPos(Loc)) & LocMask);
  }

  /// Get new MemoryEffectsBase with modified ModRefInfo for Loc.
  /// \param Loc Memory location to update.
  /// \param MR New Mod/ref behavior for \p Loc.
  /// \return A copy with \p Loc updated to \p MR.
  MemoryEffectsBase getWithModRef(Location Loc, ModRefInfo MR) const {
    MemoryEffectsBase ME = *this;
    ME.setModRef(Loc, MR);
    return ME;
  }

  /// Get new MemoryEffectsBase with NoModRef on the given Loc.
  /// \param Loc Memory location to clear.
  /// \return A copy with \p Loc cleared to NoModRef.
  MemoryEffectsBase getWithoutLoc(Location Loc) const {
    MemoryEffectsBase ME = *this;
    ME.setModRef(Loc, ModRefInfo::NoModRef);
    return ME;
  }

  /// Get ModRefInfo for any location.
  /// \return Combined ModRefInfo across all locations.
  ModRefInfo getModRef() const {
    ModRefInfo MR = ModRefInfo::NoModRef;
    for (Location Loc : locations())
      MR |= getModRef(Loc);
    return MR;
  }

  /// Whether this function accesses no memory.
  /// \return True if no memory is accessed.
  bool doesNotAccessMemory() const { return Data == 0; }

  /// Whether this function only (at most) reads memory.
  /// \return True if memory is only read, never written.
  bool onlyReadsMemory() const { return !isModSet(getModRef()); }

  /// Whether this function only (at most) writes memory.
  /// \return True if memory is only written, never read.
  bool onlyWritesMemory() const { return !isRefSet(getModRef()); }

  /// Whether this function only (at most) accesses argument memory.
  /// \return True if only argument memory may be accessed.
  bool onlyAccessesArgPointees() const {
    return getWithoutLoc(Location::ArgMem).doesNotAccessMemory();
  }

  /// Whether this function may access argument memory.
  /// \return True if argument memory may be accessed.
  bool doesAccessArgPointees() const {
    return isModOrRefSet(getModRef(Location::ArgMem));
  }

  /// Whether this function only (at most) accesses inaccessible memory.
  /// \return True if only inaccessible memory may be accessed.
  bool onlyAccessesInaccessibleMem() const {
    return getWithoutLoc(Location::InaccessibleMem).doesNotAccessMemory();
  }

  /// Whether this function only (at most) accesses inaccessible or target
  /// memory.
  /// \return True if only inaccessible or target memory may be accessed.
  bool onlyAccessesInaccessibleOrTargetMem() const {
    MemoryEffectsBase ME = *this;
    for (auto Loc : MemoryEffectsBase::targetMemLocations())
      ME &= ME.getWithoutLoc(Loc);
    return ME.getWithoutLoc(Location::InaccessibleMem).doesNotAccessMemory();
  }

  /// Whether location is target memory location.
  /// \param Loc Location to test for membership in the target-memory range.
  /// \return True if \p Loc is a target memory location.
  bool isTargetMemLoc(IRMemLocation Loc) const {
    for (auto L : targetMemLocations())
      if (Loc == L)
        return true;
    return false;
  }

  /// Whether the target memory locations are all the same.
  /// So it behaves as the default read/write, but for Target
  /// locations only.
  /// \return True if all target memory locations have the same ModRefInfo.
  bool isTargetMemLocSameForAll() const {
    ModRefInfo Expected = getModRef(IRMemLocation::TargetMem0);
    for (auto Loc : targetMemLocations()) {
      if (Expected != getModRef(Loc))
        return false;
    }
    return true;
  }

  /// Whether this function only (at most) accesses errno memory.
  /// \return True if only errno memory may be accessed.
  bool onlyAccessesErrnoMem() const {
    return getWithoutLoc(Location::ErrnoMem).doesNotAccessMemory();
  }

  /// Whether this function only (at most) accesses argument and inaccessible
  /// memory.
  /// \return True if only argument and inaccessible memory may be accessed.
  bool onlyAccessesInaccessibleOrArgMem() const {
    return getWithoutLoc(Location::InaccessibleMem)
        .getWithoutLoc(Location::ArgMem)
        .doesNotAccessMemory();
  }

  /// Intersect with other MemoryEffectsBase.
  /// \param Other Effects to intersect with.
  /// \return The intersection of this and \p Other.
  MemoryEffectsBase operator&(MemoryEffectsBase Other) const {
    return MemoryEffectsBase(Data & Other.Data);
  }

  /// Intersect (in-place) with other MemoryEffectsBase.
  /// \param Other Effects to intersect with.
  /// \return A reference to this MemoryEffectsBase.
  MemoryEffectsBase &operator&=(MemoryEffectsBase Other) {
    Data &= Other.Data;
    return *this;
  }

  /// Union with other MemoryEffectsBase.
  /// \param Other Effects to union with.
  /// \return The union of this and \p Other.
  MemoryEffectsBase operator|(MemoryEffectsBase Other) const {
    return MemoryEffectsBase(Data | Other.Data);
  }

  /// Union (in-place) with other MemoryEffectsBase.
  /// \param Other Effects to union with.
  /// \return A reference to this MemoryEffectsBase.
  MemoryEffectsBase &operator|=(MemoryEffectsBase Other) {
    Data |= Other.Data;
    return *this;
  }

  /// Subtract other MemoryEffectsBase.
  /// \param Other Effects to clear from this set.
  /// \return Effects in this set but not in \p Other.
  MemoryEffectsBase operator-(MemoryEffectsBase Other) const {
    return MemoryEffectsBase(Data & ~Other.Data);
  }

  /// Subtract (in-place) with other MemoryEffectsBase.
  /// \param Other Effects to clear from this set.
  /// \return A reference to this MemoryEffectsBase.
  MemoryEffectsBase &operator-=(MemoryEffectsBase Other) {
    Data &= ~Other.Data;
    return *this;
  }

  /// Check whether this is the same as other MemoryEffectsBase.
  /// \param Other Effects to compare against.
  /// \return True if the memory effects are equal.
  bool operator==(MemoryEffectsBase Other) const { return Data == Other.Data; }

  /// Check whether this is different from other MemoryEffectsBase.
  /// \param Other Effects to compare against.
  /// \return True if the memory effects differ.
  bool operator!=(MemoryEffectsBase Other) const { return !operator==(Other); }
};

/// Summary of how a function affects memory in the program.
///
/// Loads from constant globals are not considered memory accesses for this
/// interface. Also, functions may freely modify stack space local to their
/// invocation without having to report it through these interfaces.
using MemoryEffects = MemoryEffectsBase<IRMemLocation>;

/// Debug print MemoryEffects.
/// \param OS Stream to write to.
/// \param RMRB Memory effects to print.
/// \return A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, MemoryEffects RMRB);

/// Legacy alias for \c MemoryEffects.
using FunctionModRefBehavior = MemoryEffects;

/// Components of the pointer that may be captured.
enum class CaptureComponents : uint8_t {
  /// No pointer components are captured.
  None = 0,
  /// Only whether the address compares equal to null may be captured.
  AddressIsNull = (1 << 0),
  /// The pointer address (including nullness) may be captured.
  Address = (1 << 1) | AddressIsNull,
  /// Capture of read-only provenance without full provenance.
  ReadProvenance = (1 << 2),
  /// Provenance metadata, including read-only and full provenance capture.
  Provenance = (1 << 3) | ReadProvenance,
  /// All address and provenance components may be captured.
  All = Address | Provenance,
  LLVM_MARK_AS_BITMASK_ENUM(Provenance),
};

/// Return whether \p CC captures no pointer components.
/// \param CC Capture-component flags to test.
/// \return True if no pointer components are captured.
inline bool capturesNothing(CaptureComponents CC) {
  return CC == CaptureComponents::None;
}

/// Return whether any pointer component may be captured in \p CC.
/// \param CC Capture-component flags to test.
/// \return True if any pointer component may be captured.
inline bool capturesAnything(CaptureComponents CC) {
  return CC != CaptureComponents::None;
}

/// Return whether \p CC captures only address-is-null, not the full address.
/// \param CC Capture-component flags to test.
/// \return True if \p CC captures only address-is-null.
inline bool capturesAddressIsNullOnly(CaptureComponents CC) {
  return (CC & CaptureComponents::Address) == CaptureComponents::AddressIsNull;
}

/// Return whether \p CC captures any address component.
/// \param CC Capture-component flags to test.
/// \return True if \p CC captures any address component.
inline bool capturesAddress(CaptureComponents CC) {
  return (CC & CaptureComponents::Address) != CaptureComponents::None;
}

/// Return whether \p CC captures read-only provenance but not full provenance.
/// \param CC Capture-component flags to test.
/// \return True if \p CC captures only read-only provenance.
inline bool capturesReadProvenanceOnly(CaptureComponents CC) {
  return (CC & CaptureComponents::Provenance) ==
         CaptureComponents::ReadProvenance;
}

/// Return whether \p CC captures full provenance (not merely read-only).
/// \param CC Capture-component flags to test.
/// \return True if \p CC captures full provenance.
inline bool capturesFullProvenance(CaptureComponents CC) {
  return (CC & CaptureComponents::Provenance) == CaptureComponents::Provenance;
}

/// Return true if \p CC captures any provenance component.
/// \param CC Capture-component flags to test.
/// \return True if \p CC captures any provenance component.
inline bool capturesAnyProvenance(CaptureComponents CC) {
  return (CC & CaptureComponents::Provenance) != CaptureComponents::None;
}

/// Return whether \p CC includes every capture component.
/// \param CC Capture-component flags to test.
/// \return True if \p CC includes every capture component.
inline bool capturesAll(CaptureComponents CC) {
  return CC == CaptureComponents::All;
}

/// Debug print CaptureComponents.
/// \param OS Stream to write to.
/// \param CC Capture components to print.
/// \return A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, CaptureComponents CC);

/// Represents which components of the pointer may be captured in which
/// location. This represents the captures(...) attribute in IR.
///
/// For more information on the precise semantics see LangRef.
class CaptureInfo {
  CaptureComponents OtherComponents;
  CaptureComponents RetComponents;

public:
  /// Construct capture info with separate components for non-return and return
  /// locations.
  /// \param OtherComponents Components that may be captured outside the return
  ///        value.
  /// \param RetComponents Components that may be captured via the return value.
  CaptureInfo(CaptureComponents OtherComponents,
              CaptureComponents RetComponents)
      : OtherComponents(OtherComponents), RetComponents(RetComponents) {}

  /// Construct capture info with the same components for all capture locations.
  /// \param Components Components that may be captured in any location.
  CaptureInfo(CaptureComponents Components)
      : OtherComponents(Components), RetComponents(Components) {}

  /// Create CaptureInfo that does not capture any components of the pointer
  /// \return CaptureInfo that captures nothing.
  static CaptureInfo none() { return CaptureInfo(CaptureComponents::None); }

  /// Create CaptureInfo that may capture all components of the pointer.
  /// \return CaptureInfo that may capture all components.
  static CaptureInfo all() { return CaptureInfo(CaptureComponents::All); }

  /// Create CaptureInfo that may only capture via the return value.
  /// \param RetComponents Components that may be captured via the return value.
  /// \return CaptureInfo that only allows capture via the return value.
  static CaptureInfo
  retOnly(CaptureComponents RetComponents = CaptureComponents::All) {
    return CaptureInfo(CaptureComponents::None, RetComponents);
  }

  /// Whether the pointer is only captured via the return value.
  /// \return True if captures occur only via the return value.
  bool isRetOnly() const { return capturesNothing(OtherComponents); }

  /// Get components potentially captured by the return value.
  /// \return Components that may be captured via the return value.
  CaptureComponents getRetComponents() const { return RetComponents; }

  /// Get components potentially captured through locations other than the
  /// return value.
  /// \return Components that may be captured outside the return value.
  CaptureComponents getOtherComponents() const { return OtherComponents; }

  /// Get the potentially captured components of the pointer (regardless of
  /// location).
  /// \return All potentially captured components regardless of location.
  operator CaptureComponents() const { return OtherComponents | RetComponents; }

  /// Return whether \p Other describes the same captured components.
  /// \param Other Capture info to compare against.
  /// \return True if the capture infos are equal.
  bool operator==(CaptureInfo Other) const {
    return OtherComponents == Other.OtherComponents &&
           RetComponents == Other.RetComponents;
  }

  /// Return whether \p Other describes different captured components.
  /// \param Other Capture info to compare against.
  /// \return True if the capture infos differ.
  bool operator!=(CaptureInfo Other) const { return !(*this == Other); }

  /// Compute union of CaptureInfos.
  /// \param Other Capture info to union with.
  /// \return The union of this and \p Other.
  CaptureInfo operator|(CaptureInfo Other) const {
    return CaptureInfo(OtherComponents | Other.OtherComponents,
                       RetComponents | Other.RetComponents);
  }

  /// Compute intersection of CaptureInfos.
  /// \param Other Capture info to intersect with.
  /// \return The intersection of this and \p Other.
  CaptureInfo operator&(CaptureInfo Other) const {
    return CaptureInfo(OtherComponents & Other.OtherComponents,
                       RetComponents & Other.RetComponents);
  }

  /// Compute union of CaptureInfos in-place.
  /// \param Other Capture info to union with.
  /// \return A reference to this CaptureInfo.
  CaptureInfo &operator|=(CaptureInfo Other) {
    OtherComponents |= Other.OtherComponents;
    RetComponents |= Other.RetComponents;
    return *this;
  }

  /// Compute intersection of CaptureInfos in-place.
  /// \param Other Capture info to intersect with.
  /// \return A reference to this CaptureInfo.
  CaptureInfo &operator&=(CaptureInfo Other) {
    OtherComponents &= Other.OtherComponents;
    RetComponents &= Other.RetComponents;
    return *this;
  }

  /// Create CaptureInfo from an encoded integer value (used by captures
  /// attribute).
  /// \param Data Encoded bitfield of other and return capture components.
  /// \return CaptureInfo decoded from \p Data.
  static CaptureInfo createFromIntValue(uint32_t Data) {
    return CaptureInfo(CaptureComponents(Data >> 4),
                       CaptureComponents(Data & 0xf));
  }

  /// Convert CaptureInfo into an encoded integer value (used by captures
  /// attribute).
  /// \return Encoded integer representation of this CaptureInfo.
  uint32_t toIntValue() const {
    return (uint32_t(OtherComponents) << 4) | uint32_t(RetComponents);
  }
};

/// Print a human-readable description of \p Info.
/// \param OS Stream to write to.
/// \param Info Capture info to print.
/// \return A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, CaptureInfo Info);

} // namespace llvm

#endif
