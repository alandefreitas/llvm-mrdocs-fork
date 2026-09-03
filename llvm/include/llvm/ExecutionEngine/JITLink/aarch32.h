//===------ aarch32.h - Generic JITLink arm/thumb utilities -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing arm/thumb objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_AARCH32
#define LLVM_EXECUTIONENGINE_JITLINK_AARCH32

#include "TableManager.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace jitlink {
/// Generic JITLink utilities for AArch32 (Arm and Thumb) objects.
namespace aarch32 {

/// Check whether the given target flags are set for this Symbol.
/// \param Sym Symbol whose target flags are inspected.
/// \param Flags Target flags to test for.
/// \return True if \p Flags are set on \p Sym.
LLVM_ABI bool hasTargetFlags(Symbol &Sym, TargetFlagsType Flags);

/// JITLink-internal AArch32 fixup kinds
enum EdgeKind_aarch32 : Edge::Kind {

  ///
  /// Relocations of class Data respect target endianness (unless otherwise
  /// specified)
  ///
  FirstDataRelocation = Edge::FirstRelocation,

  /// Relative 32-bit value relocation
  Data_Delta32 = FirstDataRelocation,

  /// Absolute 32-bit value relocation
  Data_Pointer32,

  /// Relative 31-bit value relocation that preserves the most-significant bit
  Data_PRel31,

  /// Create GOT entry and store offset
  Data_RequestGOTAndTransformToDelta32,

  LastDataRelocation = Data_RequestGOTAndTransformToDelta32,

  ///
  /// Relocations of class Arm (covers fixed-width 4-byte instruction subset)
  ///
  FirstArmRelocation,

  /// Write immediate value for unconditional PC-relative branch with link.
  /// We patch the instruction opcode to account for an instruction-set state
  /// switch: we use the bl instruction to stay in ARM and the blx instruction
  /// to switch to Thumb.
  Arm_Call = FirstArmRelocation,

  /// Write immediate value for conditional PC-relative branch without link.
  /// If the branch target is not ARM, we are forced to generate an explicit
  /// interworking stub.
  Arm_Jump24,

  /// Write immediate value to the lower halfword of the destination register
  Arm_MovwAbsNC,

  /// Write immediate value to the top halfword of the destination register
  Arm_MovtAbs,

  LastArmRelocation = Arm_MovtAbs,

  ///
  /// Relocations of class Thumb16 and Thumb32 (covers Thumb instruction subset)
  ///
  FirstThumbRelocation,

  /// Write immediate value for unconditional PC-relative branch with link.
  /// We patch the instruction opcode to account for an instruction-set state
  /// switch: we use the bl instruction to stay in Thumb and the blx instruction
  /// to switch to ARM.
  Thumb_Call = FirstThumbRelocation,

  /// Write immediate value for PC-relative branch without link. The instruction
  /// can be made conditional by an IT block. If the branch target is not ARM,
  /// we are forced to generate an explicit interworking stub.
  Thumb_Jump24,

  /// Write immediate value to the lower halfword of the destination register
  Thumb_MovwAbsNC,

  /// Write immediate value to the top halfword of the destination register
  Thumb_MovtAbs,

  /// Write PC-relative immediate value to the lower halfword of the destination
  /// register
  Thumb_MovwPrelNC,

  /// Write PC-relative immediate value to the top halfword of the destination
  /// register
  Thumb_MovtPrel,

  LastThumbRelocation = Thumb_MovtPrel,

  /// No-op relocation
  None,

  LastRelocation = None,
};

/// Flags enum for AArch32-specific symbol properties
enum TargetFlags_aarch32 : TargetFlagsType {
  /// Symbol refers to Thumb code rather than Arm code.
  ThumbSymbol = 1 << 0,
};

/// Human-readable name for a given CPU architecture kind
/// \param K ARM CPU architecture kind from build attributes.
/// \return Human-readable name for \p K.
LLVM_ABI const char *getCPUArchName(ARMBuildAttrs::CPUArch K);

/// Get a human-readable name for the given AArch32 edge kind.
/// \param K AArch32 edge kind to name.
/// \return Human-readable name for \p K.
LLVM_ABI const char *getEdgeKindName(Edge::Kind K);

/// AArch32 uses stubs for a number of purposes, like branch range extension
/// or interworking between Arm and Thumb instruction subsets.
///
/// Stub implementations vary depending on CPU architecture (v4, v6, v7),
/// instruction subset and branch type (absolute/PC-relative).
///
/// For each kind of stub, the StubsFlavor defines one concrete form that is
/// used throughout the LinkGraph.
///
/// Stubs are often called "veneers" in the official docs and online.
///
enum class StubsFlavor {
  /// No stub flavor selected yet.
  Undefined = 0,
  /// Non-position-independent stubs for pre-v7 CPUs.
  pre_v7,
  /// Non-position-independent Arm and Thumb stubs for v7 and later.
  v7,
};

/// JITLink sub-arch configuration for Arm CPU models
struct ArmConfig {
  /// True when Thumb branch encoding uses the J1/J2 range bits.
  bool J1J2BranchEncoding = false;
  /// Preferred stub implementation for this CPU model.
  StubsFlavor Stubs = StubsFlavor::Undefined;
  // In the long term, we might want a linker switch like --target1-rel
  /// Treat R_ARM_TARGET1 as a relative relocation when true.
  bool Target1Rel = false;
};

/// Obtain the sub-arch configuration for a given Arm CPU model.
/// \param CPUArch ARM CPU architecture kind from build attributes.
/// \return Sub-arch configuration for \p CPUArch.
inline ArmConfig getArmConfigForCPUArch(ARMBuildAttrs::CPUArch CPUArch) {
  ArmConfig ArmCfg;
  if (CPUArch == ARMBuildAttrs::v7 || CPUArch >= ARMBuildAttrs::v7E_M) {
    ArmCfg.J1J2BranchEncoding = true;
    ArmCfg.Stubs = StubsFlavor::v7;
  } else {
    ArmCfg.J1J2BranchEncoding = false;
    ArmCfg.Stubs = StubsFlavor::pre_v7;
  }
  return ArmCfg;
}

/// Immutable pair of halfwords, Hi and Lo, with overflow check
struct HalfWords {
  /// Construct a zero Hi/Lo halfword pair.
  constexpr HalfWords() : Hi(0), Lo(0) {}
  /// Construct from 16-bit Hi and Lo halfwords.
  /// \param Hi First halfword value; must fit in 16 bits.
  /// \param Lo Second halfword value; must fit in 16 bits.
  constexpr HalfWords(uint32_t Hi, uint32_t Lo) : Hi(Hi), Lo(Lo) {
    assert(isUInt<16>(Hi) && "Overflow in first half-word");
    assert(isUInt<16>(Lo) && "Overflow in second half-word");
  }
  /// First halfword.
  const uint16_t Hi;
  /// Second halfword.
  const uint16_t Lo;
};

/// FixupInfo base class is required for dynamic lookups.
struct FixupInfoBase {
  /// Return dynamic fixup info for edge kind \p K, or null if none exists.
  /// \param K AArch32 edge kind to look up.
  /// \return Pointer to the fixup info for \p K, or null if none exists.
  LLVM_ABI static const FixupInfoBase *getDynFixupInfo(Edge::Kind K);
  /// Destroy the fixup-info object.
  virtual ~FixupInfoBase() = default;
};

/// FixupInfo checks for Arm edge kinds work on 32-bit words
struct FixupInfoArm : public FixupInfoBase {
  /// Predicate that validates the Arm instruction opcode word.
  bool (*checkOpcode)(uint32_t Wd) = nullptr;
};

/// FixupInfo check for Thumb32 edge kinds work on a pair of 16-bit halfwords
struct FixupInfoThumb : public FixupInfoBase {
  /// Predicate that validates the Thumb32 instruction halfword pair.
  bool (*checkOpcode)(uint16_t Hi, uint16_t Lo) = nullptr;
};

/// Collection of named constants per fixup kind
///
/// Mandatory entries:
///   Opcode      - Values of the op-code bits in the instruction, with
///                 unaffected bits nulled
///   OpcodeMask  - Mask with all bits set that encode the op-code
///
/// Other common entries:
///   ImmMask     - Mask with all bits set that encode the immediate value
///   RegMask     - Mask with all bits set that encode the register
///
/// Specializations can add further custom fields without restrictions.
///
template <EdgeKind_aarch32 Kind> struct FixupInfo {};

/// Shared opcode and immediate masks for Arm branch/call fixups.
struct FixupInfoArmBranch : public FixupInfoArm {
  /// Fixed opcode bits for Arm B/BL-class instructions.
  static constexpr uint32_t Opcode = 0x0a000000;
  /// Mask of immediate bits encoding the branch offset.
  static constexpr uint32_t ImmMask = 0x00ffffff;
};

/// Fixup info for the Arm_Jump24 relocation kind.
template <> struct FixupInfo<Arm_Jump24> : public FixupInfoArmBranch {
  /// Mask of bits that encode the Arm_Jump24 opcode.
  static constexpr uint32_t OpcodeMask = 0x0f000000;
};

/// Fixup info for the Arm_Call relocation kind.
template <> struct FixupInfo<Arm_Call> : public FixupInfoArmBranch {
  /// Mask of bits that encode the Arm_Call opcode.
  static constexpr uint32_t OpcodeMask = 0x0e000000;
  /// Mask of condition-code bits, excluding the BLX bit.
  static constexpr uint32_t CondMask = 0xe0000000; // excluding BLX bit
  /// Condition-code encoding for an unconditional branch.
  static constexpr uint32_t Unconditional = 0xe0000000;
  /// H bit used when encoding BLX.
  static constexpr uint32_t BitH = 0x01000000;
  /// Bit that distinguishes BLX from BL.
  static constexpr uint32_t BitBlx = 0x10000000;
};

/// Shared masks for Arm MOVT/MOVW immediate fixups.
struct FixupInfoArmMov : public FixupInfoArm {
  /// Mask of bits that encode the MOVT/MOVW opcode.
  static constexpr uint32_t OpcodeMask = 0x0ff00000;
  /// Mask of bits that encode the immediate value.
  static constexpr uint32_t ImmMask = 0x000f0fff;
  /// Mask of bits that encode the destination register.
  static constexpr uint32_t RegMask = 0x0000f000;
};

/// Fixup info for the Arm_MovtAbs relocation kind.
template <> struct FixupInfo<Arm_MovtAbs> : public FixupInfoArmMov {
  /// Fixed opcode bits for Arm MOVT (absolute).
  static constexpr uint32_t Opcode = 0x03400000;
};

/// Fixup info for the Arm_MovwAbsNC relocation kind.
template <> struct FixupInfo<Arm_MovwAbsNC> : public FixupInfoArmMov {
  /// Fixed opcode bits for Arm MOVW (absolute, no check).
  static constexpr uint32_t Opcode = 0x03000000;
};

/// Fixup info for the Thumb_Jump24 relocation kind.
template <> struct FixupInfo<Thumb_Jump24> : public FixupInfoThumb {
  /// Fixed opcode halfwords for Thumb B.W.
  static constexpr HalfWords Opcode{0xf000, 0x9000};
  /// Mask of halfword bits that encode the Thumb_Jump24 opcode.
  static constexpr HalfWords OpcodeMask{0xf800, 0x9000};
  /// Mask of halfword bits that encode the branch immediate.
  static constexpr HalfWords ImmMask{0x07ff, 0x2fff};
};

/// Fixup info for the Thumb_Call relocation kind.
template <> struct FixupInfo<Thumb_Call> : public FixupInfoThumb {
  /// Fixed opcode halfwords for Thumb BL/BLX.
  static constexpr HalfWords Opcode{0xf000, 0xc000};
  /// Mask of halfword bits that encode the Thumb_Call opcode.
  static constexpr HalfWords OpcodeMask{0xf800, 0xc000};
  /// Mask of halfword bits that encode the call immediate.
  static constexpr HalfWords ImmMask{0x07ff, 0x2fff};
  /// Low halfword H bit used when encoding BLX.
  static constexpr uint16_t LoBitH = 0x0001;
  /// Low halfword bit that selects BL rather than BLX.
  static constexpr uint16_t LoBitNoBlx = 0x1000;
};

/// Shared masks for Thumb MOVT/MOVW immediate fixups.
struct FixupInfoThumbMov : public FixupInfoThumb {
  /// Mask of halfword bits that encode the MOVT/MOVW opcode.
  static constexpr HalfWords OpcodeMask{0xfbf0, 0x8000};
  /// Mask of halfword bits that encode the immediate value.
  static constexpr HalfWords ImmMask{0x040f, 0x70ff};
  /// Mask of halfword bits that encode the destination register.
  static constexpr HalfWords RegMask{0x0000, 0x0f00};
};

/// Fixup info for the Thumb_MovtAbs relocation kind.
template <> struct FixupInfo<Thumb_MovtAbs> : public FixupInfoThumbMov {
  /// Fixed opcode halfwords for Thumb MOVT (absolute).
  static constexpr HalfWords Opcode{0xf2c0, 0x0000};
};

/// Fixup info for the Thumb_MovtPrel relocation kind.
template <> struct FixupInfo<Thumb_MovtPrel> : public FixupInfoThumbMov {
  /// Fixed opcode halfwords for Thumb MOVT (PC-relative).
  static constexpr HalfWords Opcode{0xf2c0, 0x0000};
};

/// Fixup info for the Thumb_MovwAbsNC relocation kind.
template <> struct FixupInfo<Thumb_MovwAbsNC> : public FixupInfoThumbMov {
  /// Fixed opcode halfwords for Thumb MOVW (absolute, no check).
  static constexpr HalfWords Opcode{0xf240, 0x0000};
};

/// Fixup info for the Thumb_MovwPrelNC relocation kind.
template <> struct FixupInfo<Thumb_MovwPrelNC> : public FixupInfoThumbMov {
  /// Fixed opcode halfwords for Thumb MOVW (PC-relative, no check).
  static constexpr HalfWords Opcode{0xf240, 0x0000};
};

/// Helper function to read the initial addend for Data-class relocations.
/// \param G Link graph that owns the block.
/// \param B Block containing the fixup location.
/// \param Offset Byte offset of the fixup within \p B.
/// \param Kind Data-class edge kind that selects the read encoding.
/// \return The encoded addend, or an error if the kind is unsupported.
LLVM_ABI Expected<int64_t>
readAddendData(LinkGraph &G, Block &B, Edge::OffsetT Offset, Edge::Kind Kind);

/// Helper function to read the initial addend for Arm-class relocations.
/// \param G Link graph that owns the block.
/// \param B Block containing the fixup location.
/// \param Offset Byte offset of the fixup within \p B.
/// \param Kind Arm-class edge kind that selects the read encoding.
/// \return The encoded addend, or an error if the kind is unsupported.
LLVM_ABI Expected<int64_t> readAddendArm(LinkGraph &G, Block &B,
                                         Edge::OffsetT Offset, Edge::Kind Kind);

/// Helper function to read the initial addend for Thumb-class relocations.
/// \param G Link graph that owns the block.
/// \param B Block containing the fixup location.
/// \param Offset Byte offset of the fixup within \p B.
/// \param Kind Thumb-class edge kind that selects the read encoding.
/// \param ArmCfg AArch32 sub-arch configuration affecting Thumb encoding.
/// \return The encoded addend, or an error if the kind is unsupported.
LLVM_ABI Expected<int64_t> readAddendThumb(LinkGraph &G, Block &B,
                                           Edge::OffsetT Offset,
                                           Edge::Kind Kind,
                                           const ArmConfig &ArmCfg);

/// Read the initial addend for a REL-type relocation. It's the value encoded
/// in the immediate field of the fixup location by the compiler.
/// \param G Link graph that owns the block.
/// \param B Block containing the fixup location.
/// \param Offset Byte offset of the fixup within \p B.
/// \param Kind Edge kind that selects the relocation class and encoding.
/// \param ArmCfg AArch32 sub-arch configuration for Thumb-class kinds.
/// \return The encoded addend, or an error if the kind is unsupported.
inline Expected<int64_t> readAddend(LinkGraph &G, Block &B,
                                    Edge::OffsetT Offset, Edge::Kind Kind,
                                    const ArmConfig &ArmCfg) {
  if (Kind <= LastDataRelocation)
    return readAddendData(G, B, Offset, Kind);

  if (Kind <= LastArmRelocation)
    return readAddendArm(G, B, Offset, Kind);

  if (Kind <= LastThumbRelocation)
    return readAddendThumb(G, B, Offset, Kind, ArmCfg);

  assert(Kind == None && "Not associated with a relocation class");
  return 0;
}

/// Helper function to apply the fixup for Data-class relocations.
/// \param G Link graph that owns the block.
/// \param B Block whose content is updated.
/// \param E Edge describing the Data-class fixup to apply.
/// \return Success, or an error if the fixup cannot be applied.
LLVM_ABI Error applyFixupData(LinkGraph &G, Block &B, const Edge &E);

/// Helper function to apply the fixup for Arm-class relocations.
/// \param G Link graph that owns the block.
/// \param B Block whose content is updated.
/// \param E Edge describing the Arm-class fixup to apply.
/// \return Success, or an error if the fixup cannot be applied.
LLVM_ABI Error applyFixupArm(LinkGraph &G, Block &B, const Edge &E);

/// Helper function to apply the fixup for Thumb-class relocations.
/// \param G Link graph that owns the block.
/// \param B Block whose content is updated.
/// \param E Edge describing the Thumb-class fixup to apply.
/// \param ArmCfg AArch32 sub-arch configuration affecting Thumb encoding.
/// \return Success, or an error if the fixup cannot be applied.
LLVM_ABI Error applyFixupThumb(LinkGraph &G, Block &B, const Edge &E,
                               const ArmConfig &ArmCfg);

/// Apply fixup expression for edge to block content.
/// \param G Link graph that owns the block.
/// \param B Block whose content is updated.
/// \param E Edge describing the fixup to apply.
/// \param ArmCfg AArch32 sub-arch configuration for Thumb-class kinds.
/// \return Success, or an error if the fixup cannot be applied.
inline Error applyFixup(LinkGraph &G, Block &B, const Edge &E,
                        const ArmConfig &ArmCfg) {
  Edge::Kind Kind = E.getKind();

  if (Kind <= LastDataRelocation)
    return applyFixupData(G, B, E);

  if (Kind <= LastArmRelocation)
    return applyFixupArm(G, B, E);

  if (Kind <= LastThumbRelocation)
    return applyFixupThumb(G, B, E, ArmCfg);

  assert(Kind == None && "Not associated with a relocation class");
  return Error::success();
}

/// Populate a Global Offset Table from edges that request it.
class GOTBuilder : public TableManager<GOTBuilder> {
public:
  /// Return the object-file section name used for GOT entries.
  /// \return The section name string "$__GOT".
  static StringRef getSectionName() { return "$__GOT"; }

  /// Visit \p E and transform GOT-request edges into GOT uses.
  /// \param G Link graph being traversed.
  /// \param B Block that contains \p E, if any.
  /// \param E Edge that may request a GOT entry.
  /// \return True if the edge was transformed.
  LLVM_ABI bool visitEdge(LinkGraph &G, Block *B, Edge &E);
  /// Create a GOT entry pointing at \p Target and return its symbol.
  /// \param G Link graph that will own the GOT entry.
  /// \param Target Symbol that the new GOT entry should reference.
  /// \return An anonymous symbol pointing at the new GOT entry.
  LLVM_ABI Symbol &createEntry(LinkGraph &G, Symbol &Target);

private:
  Section *GOTSection = nullptr;
};

/// Stubs builder for pre-v7 CPUs that emit non-position-independent Arm stubs.
///
/// These architectures have no MovT/MovW instructions and don't support Thumb2.
/// BL is the only Thumb instruction that can generate stubs and they can always
/// be transformed into BLX.
class StubsManager_prev7 {
public:
  /// Construct an empty pre-v7 stubs manager.
  StubsManager_prev7() = default;

  /// Name of the object file section that will contain all our stubs.
  /// \return The section name string for pre-v7 stubs.
  static StringRef getSectionName() {
    return "__llvm_jitlink_aarch32_STUBS_prev7";
  }

  /// Implements link-graph traversal via visitExistingEdges()
  /// \param G Link graph being traversed.
  /// \param B Block that contains \p E, if any.
  /// \param E Edge that may require a pre-v7 stub.
  /// \return True if the edge was redirected through a stub.
  LLVM_ABI bool visitEdge(LinkGraph &G, Block *B, Edge &E);

private:
  // Each stub uses a single block that can have 2 entryponts, one for Arm and
  // one for Thumb
  struct StubMapEntry {
    Block *B = nullptr;
    Symbol *ArmEntry = nullptr;
    Symbol *ThumbEntry = nullptr;
  };

  std::pair<StubMapEntry *, bool> getStubMapSlot(StringRef Name) {
    auto &&[Stubs, NewStub] = StubMap.try_emplace(Name);
    return std::make_pair(&Stubs->second, NewStub);
  }

  Symbol *getOrCreateSlotEntrypoint(LinkGraph &G, StubMapEntry &Slot,
                                    bool Thumb);

  DenseMap<StringRef, StubMapEntry> StubMap;
  Section *StubsSection = nullptr;
};

/// Stubs builder for v7 emits non-position-independent Arm and Thumb stubs.
class StubsManager_v7 {
public:
  /// Construct an empty v7 stubs manager.
  StubsManager_v7() = default;

  /// Name of the object file section that will contain all our stubs.
  /// \return The section name string for v7 stubs.
  static StringRef getSectionName() {
    return "__llvm_jitlink_aarch32_STUBS_v7";
  }

  /// Implements link-graph traversal via visitExistingEdges().
  /// \param G Link graph being traversed.
  /// \param B Block that contains \p E, if any.
  /// \param E Edge that may require a v7 stub.
  /// \return True if the edge was redirected through a stub.
  LLVM_ABI bool visitEdge(LinkGraph &G, Block *B, Edge &E);

private:
  // Two slots per external: Arm and Thumb
  using StubMapEntry = std::tuple<Symbol *, Symbol *>;

  Symbol *&getStubSymbolSlot(StringRef Name, bool Thumb) {
    StubMapEntry &Stubs = StubMap[Name];
    if (Thumb)
      return std::get<1>(Stubs);
    return std::get<0>(Stubs);
  }

  DenseMap<StringRef, StubMapEntry> StubMap;
  Section *StubsSection = nullptr;
};

} // namespace aarch32
} // namespace jitlink
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_AARCH32
