//===- PseudoProbe.h - Pseudo Probe IR Helpers ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pseudo probe IR intrinsic and dwarf discriminator manipulation routines.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PSEUDOPROBE_H
#define LLVM_IR_PSEUDOPROBE_H

#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>

namespace llvm {

class Instruction;

/// Metadata name for the module-level pseudo probe descriptor.
constexpr const char *PseudoProbeDescMetadataName = "llvm.pseudo_probe_desc";

/// Reserved probe identifier values.
enum class PseudoProbeReservedId {
  Invalid = 0, ///< Not a valid probe id.
  Last = Invalid ///< Last reserved id (same as Invalid).
};

/// Kind of code location a pseudo probe represents.
enum class PseudoProbeType {
  Block = 0, ///< Probe on a basic block.
  IndirectCall, ///< Probe on an indirect call site.
  DirectCall ///< Probe on a direct call site.
};

/// Bit flags describing optional properties of a pseudo probe.
enum class PseudoProbeAttributes {
  Reserved = 0x1, ///< Reserved attribute bit.
  Sentinel = 0x2, ///< A place holder for split function entry address.
  HasDiscriminator = 0x4, ///< For probes with a discriminator.
};

// The saturated distrution factor representing 100% for block probes.
constexpr static uint64_t PseudoProbeFullDistributionFactor =
    std::numeric_limits<uint64_t>::max();

/// Helpers that pack and unpack pseudo probe fields in a DWARF discriminator.
///
/// The following APIs encode/decode per-probe information to/from a
/// 32-bit integer which is organized as:
///  [2:0] - 0x7, this is reserved for regular discriminator,
///          see DWARF discriminator encoding rule
///  if the [28:28] bit is zero:
///    [18:3] for probe id.
///  else:
///    [15:3] for probe id, [18:16] for dwarf base discriminator.
///  [25:19] - probe distribution factor
///  [27:26] - probe type, see PseudoProbeType
///  [28:28] - indicates whether dwarf base discriminator is encoded.
///  [30:29] - reserved for probe attributes
struct PseudoProbeDwarfDiscriminator {
public:
  /// Pack probe fields into a 32-bit DWARF discriminator value.
  /// \param Index Probe identifier to encode.
  /// \param Type Probe type bits to encode (see PseudoProbeType).
  /// \param Flags Probe attribute flags to encode.
  /// \param Factor Distribution factor percentage in the range [0, 100].
  /// \param DwarfBaseDiscriminator Optional DWARF base discriminator to share
  ///        probe-id space with when both values are small enough.
  /// \return The packed 32-bit DWARF discriminator value.
  static uint32_t
  packProbeData(uint32_t Index, uint32_t Type, uint32_t Flags, uint32_t Factor,
                std::optional<uint32_t> DwarfBaseDiscriminator) {
    assert(Index <= 0xFFFF && "Probe index too big to encode, exceeding 2^16");
    assert(Type <= 0x3 && "Probe type too big to encode, exceeding 3");
    assert(Flags <= 0x7);
    assert(Factor <= 100 &&
           "Probe distribution factor too big to encode, exceeding 100");
    uint32_t V = (Index << 3) | (Factor << 19) | (Type << 26) | 0x7;
    // If both the probe id and dwarf base discriminator is small, the probe id
    // space is shared with the dwarf base discriminator, this is to make the
    // probe-based build compatible with the dwarf-based profile.
    // Pack the dwarf base discriminator into [18:16] and set the [28:28] bit.
    if (Index <= 0x1FFF && DwarfBaseDiscriminator &&
        *DwarfBaseDiscriminator <= 0x7)
      V |= (1 << 28) | (*DwarfBaseDiscriminator << 16);
    return V;
  }

  /// Extract the probe index from a packed discriminator value.
  /// \param Value Packed DWARF discriminator to decode.
  /// \return The decoded probe index.
  static uint32_t extractProbeIndex(uint32_t Value) {
    if (isDwarfBaseDiscriminatorEncoded(Value))
      return (Value >> 3) & 0x1FFF;
    return (Value >> 3) & 0xFFFF;
  }

  /// Extract the DWARF base discriminator, if one was encoded.
  /// \param Value Packed DWARF discriminator to decode.
  /// \return The DWARF base discriminator, or std::nullopt if none was encoded.
  static std::optional<uint32_t> extractDwarfBaseDiscriminator(uint32_t Value) {
    if (isDwarfBaseDiscriminatorEncoded(Value))
      return (Value >> 16) & 0x7;
    return std::nullopt;
  }

  /// Return true if \p Value encodes a DWARF base discriminator.
  /// \param Value Packed DWARF discriminator to test.
  /// \return True if a DWARF base discriminator is encoded in \p Value.
  static bool isDwarfBaseDiscriminatorEncoded(uint32_t Value) {
    return Value & 0x10000000;
  }

  /// Extract the probe type bits from a packed discriminator value.
  /// \param Value Packed DWARF discriminator to decode.
  /// \return The decoded probe type bits (see PseudoProbeType).
  static uint32_t extractProbeType(uint32_t Value) {
    return (Value >> 26) & 0x3;
  }

  /// Extract the probe attribute bits from a packed discriminator value.
  /// \param Value Packed DWARF discriminator to decode.
  /// \return The decoded probe attribute bits (see PseudoProbeAttributes).
  static uint32_t extractProbeAttributes(uint32_t Value) {
    return (Value >> 29) & 0x7;
  }

  /// Extract the probe distribution factor from a packed discriminator value.
  /// \param Value Packed DWARF discriminator to decode.
  /// \return The decoded distribution factor percentage in the range [0, 100].
  static uint32_t extractProbeFactor(uint32_t Value) {
    return (Value >> 19) & 0x7F;
  }

  /// Saturated distribution factor representing 100% for callsites.
  constexpr static uint8_t FullDistributionFactor = 100;
};

/// Descriptor pairing a function GUID with its structural hash for pseudo
/// probes.
class PseudoProbeDescriptor {
  uint64_t FunctionGUID;
  uint64_t FunctionHash;

public:
  /// Construct a descriptor from a function GUID and hash.
  /// \param GUID Function GUID associated with the probes.
  /// \param Hash Structural hash of the function.
  PseudoProbeDescriptor(uint64_t GUID, uint64_t Hash)
      : FunctionGUID(GUID), FunctionHash(Hash) {}
  /// Return the function GUID stored in this descriptor.
  /// \return The function GUID associated with the probes.
  uint64_t getFunctionGUID() const { return FunctionGUID; }
  /// Return the function structural hash stored in this descriptor.
  /// \return The structural hash of the function.
  uint64_t getFunctionHash() const { return FunctionHash; }
};

/// Decoded pseudo probe attached to an instruction.
struct PseudoProbe {
  /// Probe identifier.
  uint32_t Id;
  /// Probe type (see PseudoProbeType).
  uint32_t Type;
  /// Probe attribute flags (see PseudoProbeAttributes).
  uint32_t Attr;
  /// Associated DWARF discriminator value, if any.
  uint32_t Discriminator;
  /// Distribution factor estimating the portion of the real execution count.
  ///
  /// A saturated distribution factor stands for 1.0 or 100%. A pesudo probe has
  /// a factor with the value ranged from 0.0 to 1.0.
  float Factor;
};

static inline bool isSentinelProbe(uint32_t Flags) {
  return Flags & (uint32_t)PseudoProbeAttributes::Sentinel;
}

static inline bool hasDiscriminator(uint32_t Flags) {
  return Flags & (uint32_t)PseudoProbeAttributes::HasDiscriminator;
}

/// Extract the pseudo probe stored on \p Inst, if present.
/// \param Inst Instruction that may carry a pseudo probe.
/// \return The decoded pseudo probe, or std::nullopt if none is present.
LLVM_ABI std::optional<PseudoProbe> extractProbe(const Instruction &Inst);

/// Set the distribution factor of the pseudo probe on \p Inst.
/// \param Inst Instruction whose probe distribution factor is updated.
/// \param Factor New distribution factor in the range [0.0, 1.0].
LLVM_ABI void setProbeDistributionFactor(Instruction &Inst, float Factor);
} // end namespace llvm

#endif // LLVM_IR_PSEUDOPROBE_H
