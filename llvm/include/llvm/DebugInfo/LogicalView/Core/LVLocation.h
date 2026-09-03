//===-- LVLocation.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVOperation and LVLocation classes, which are used
// to describe variable locations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLOCATION_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLOCATION_H

#include "llvm/DebugInfo/LogicalView/Core/LVObject.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace logicalview {

/// Inclusive pair of logical lines bounding a location range.
using LVLineRange = std::pair<LVLine *, LVLine *>;

/// Opcode sentinel for a simple DW_AT_data_member_location member offset.
const LVSmall LVLocationMemberOffset = 0;

/// Single DWARF or CodeView location-expression operation and its operands.
class LVOperation final {
  // To describe an operation:
  // OpCode
  // Operands[0]: First operand.
  // Operands[1]: Second operand.
  //   OP_bregx, OP_bit_piece, OP_[GNU_]const_type,
  //   OP_[GNU_]deref_type, OP_[GNU_]entry_value, OP_implicit_value,
  //   OP_[GNU_]implicit_pointer, OP_[GNU_]regval_type, OP_xderef_type.
  LVSmall Opcode = 0;
  SmallVector<uint64_t> Operands;

public:
  /// Default construction is not allowed.
  LVOperation() = delete;
  /// Construct an operation from \p Opcode and \p Operands.
  /// \param Opcode Location-expression opcode.
  /// \param Operands Operand values associated with the opcode.
  LVOperation(LVSmall Opcode, ArrayRef<LVUnsigned> Operands)
      : Opcode(Opcode), Operands(Operands) {}
  /// Copy construction is not allowed.
  /// \param Other Unused source operation.
  LVOperation(const LVOperation &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source operation.
  LVOperation &operator=(const LVOperation &Other) = delete;
  /// Destroy the location operation.
  ~LVOperation() = default;

  /// Return the opcode for this operation.
  /// \returns The location-expression opcode.
  LVSmall getOpcode() const { return Opcode; }
  /// Return a human-readable DWARF description of the operands.
  /// \returns Formatted DWARF operand description string.
  LLVM_ABI std::string getOperandsDWARFInfo();
  /// Return a human-readable CodeView description of the operands.
  /// \returns Formatted CodeView operand description string.
  LLVM_ABI std::string getOperandsCodeViewInfo();

  /// Print this operation to \p OS.
  /// \param OS Stream that receives the printed operation.
  /// \param Full Whether to print full operand details.
  LLVM_ABI void print(raw_ostream &OS, bool Full = true) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this operation to the debug stream.
  void dump() const { print(dbgs()); }
#endif
};

/// Logical-view representation of a debug location or location range.
class LLVM_ABI LVLocation : public LVObject {
  enum class Property {
    IsAddressRange,
    IsBaseClassOffset,
    IsBaseClassStep,
    IsClassOffset,
    IsFixedAddress,
    IsLocationSimple,
    IsGapEntry,
    IsOperation,
    IsOperationList,
    IsRegister,
    IsStackOffset,
    IsDiscardedRange,
    IsInvalidRange,
    IsInvalidLower,
    IsInvalidUpper,
    IsCallSite,
    LastEntry
  };
  // Typed bitvector with properties for this location.
  LVProperties<Property> Properties;

  // True if the location it is associated with a debug range.
  bool hasAssociatedRange() const {
    return !getIsClassOffset() && !getIsDiscardedRange();
  }

protected:
  /// Lower source line associated with this location range.
  LVLine *LowerLine = nullptr;
  /// Upper source line associated with this location range.
  LVLine *UpperLine = nullptr;

  /// Lower address of the active range as an offset from a base address.
  LVAddress LowPC = 0;
  /// Upper address of the active range as an offset or length.
  LVAddress HighPC = 0;

  /// Set the location kind from the associated DWARF attribute.
  void setKind();

public:
  /// Construct an empty location and mark it as a location object.
  LVLocation() : LVObject() { setIsLocation(); }
  /// Copy construction is not allowed.
  /// \param Other Unused source location.
  LVLocation(const LVLocation &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source location.
  LVLocation &operator=(const LVLocation &Other) = delete;
  /// Destroy the location.
  ~LVLocation() override = default;

  /// Return whether this location describes an address range.
  /// \returns True when the address-range property is set.
  bool getIsAddressRange() const {
    return Properties.get(Property::IsAddressRange);
  }
  /// Mark this location as describing an address range.
  void setIsAddressRange() { Properties.set(Property::IsAddressRange); }
  /// Clear the address-range property on this location.
  void resetIsAddressRange() { Properties.reset(Property::IsAddressRange); }
  /// Return whether this location is a base-class offset.
  /// \returns True when the base-class-offset property is set.
  bool getIsBaseClassOffset() const {
    return Properties.get(Property::IsBaseClassOffset);
  }
  /// Mark this location as a base-class offset.
  void setIsBaseClassOffset() { Properties.set(Property::IsBaseClassOffset); }
  /// Clear the base-class-offset property on this location.
  void resetIsBaseClassOffset() {
    Properties.reset(Property::IsBaseClassOffset);
  }
  /// Return whether this location is a base-class step.
  /// \returns True when the base-class-step property is set.
  bool getIsBaseClassStep() const {
    return Properties.get(Property::IsBaseClassStep);
  }
  /// Mark this location as a base-class step.
  void setIsBaseClassStep() { Properties.set(Property::IsBaseClassStep); }
  /// Clear the base-class-step property on this location.
  void resetIsBaseClassStep() { Properties.reset(Property::IsBaseClassStep); }
  /// Return whether this location is a class member offset.
  /// \returns True when the class-offset property is set.
  bool getIsClassOffset() const {
    return Properties.get(Property::IsClassOffset);
  }
  /// Mark this location as a class member offset.
  void setIsClassOffset() {
    Properties.set(Property::IsClassOffset);
    setIsLocationSimple();
  }
  /// Clear the class-offset property on this location.
  void resetIsClassOffset() { Properties.reset(Property::IsClassOffset); }
  /// Return whether this location is a fixed address.
  /// \returns True when the fixed-address property is set.
  bool getIsFixedAddress() const {
    return Properties.get(Property::IsFixedAddress);
  }
  /// Mark this location as a fixed address.
  void setIsFixedAddress() {
    Properties.set(Property::IsFixedAddress);
    setIsLocationSimple();
  }
  /// Clear the fixed-address property on this location.
  void resetIsFixedAddress() { Properties.reset(Property::IsFixedAddress); }
  /// Return whether this location is a simple location.
  /// \returns True when the simple-location property is set.
  bool getIsLocationSimple() const {
    return Properties.get(Property::IsLocationSimple);
  }
  /// Mark this location as a simple location.
  void setIsLocationSimple() { Properties.set(Property::IsLocationSimple); }
  /// Clear the simple-location property on this location.
  void resetIsLocationSimple() {
    Properties.reset(Property::IsLocationSimple);
  }
  /// Return whether this location represents a coverage gap entry.
  /// \returns True when the gap-entry property is set.
  bool getIsGapEntry() const { return Properties.get(Property::IsGapEntry); }
  /// Mark this location as a coverage gap entry.
  void setIsGapEntry() { Properties.set(Property::IsGapEntry); }
  /// Clear the gap-entry property on this location.
  void resetIsGapEntry() { Properties.reset(Property::IsGapEntry); }
  /// Return whether this location is an operation list.
  /// \returns True when the operation-list property is set.
  bool getIsOperationList() const {
    return Properties.get(Property::IsOperationList);
  }
  /// Mark this location as an operation list.
  void setIsOperationList() { Properties.set(Property::IsOperationList); }
  /// Clear the operation-list property on this location.
  void resetIsOperationList() { Properties.reset(Property::IsOperationList); }
  /// Return whether this location is a single operation.
  /// \returns True when the operation property is set.
  bool getIsOperation() const { return Properties.get(Property::IsOperation); }
  /// Mark this location as a single operation.
  void setIsOperation() { Properties.set(Property::IsOperation); }
  /// Clear the operation property on this location.
  void resetIsOperation() { Properties.reset(Property::IsOperation); }
  /// Return whether this location is a register location.
  /// \returns True when the register property is set.
  bool getIsRegister() const { return Properties.get(Property::IsRegister); }
  /// Mark this location as a register location.
  void setIsRegister() { Properties.set(Property::IsRegister); }
  /// Clear the register property on this location.
  void resetIsRegister() { Properties.reset(Property::IsRegister); }
  /// Return whether this location is a stack-frame offset.
  /// \returns True when the stack-offset property is set.
  bool getIsStackOffset() const {
    return Properties.get(Property::IsStackOffset);
  }
  /// Mark this location as a stack-frame offset.
  void setIsStackOffset() {
    Properties.set(Property::IsStackOffset);
    setIsLocationSimple();
  }
  /// Clear the stack-offset property on this location.
  void resetIsStackOffset() { Properties.reset(Property::IsStackOffset); }
  /// Return whether this location's range was discarded.
  /// \returns True when the discarded-range property is set.
  bool getIsDiscardedRange() const {
    return Properties.get(Property::IsDiscardedRange);
  }
  /// Mark this location's range as discarded.
  void setIsDiscardedRange() { Properties.set(Property::IsDiscardedRange); }
  /// Clear the discarded-range property on this location.
  void resetIsDiscardedRange() {
    Properties.reset(Property::IsDiscardedRange);
  }
  /// Return whether this location has an invalid address range.
  /// \returns True when the invalid-range property is set.
  bool getIsInvalidRange() const {
    return Properties.get(Property::IsInvalidRange);
  }
  /// Mark this location as having an invalid address range.
  void setIsInvalidRange() { Properties.set(Property::IsInvalidRange); }
  /// Clear the invalid-range property on this location.
  void resetIsInvalidRange() { Properties.reset(Property::IsInvalidRange); }
  /// Return whether the lower bound of this location is invalid.
  /// \returns True when the invalid-lower property is set.
  bool getIsInvalidLower() const {
    return Properties.get(Property::IsInvalidLower);
  }
  /// Mark the lower bound of this location as invalid.
  void setIsInvalidLower() { Properties.set(Property::IsInvalidLower); }
  /// Clear the invalid-lower property on this location.
  void resetIsInvalidLower() { Properties.reset(Property::IsInvalidLower); }
  /// Return whether the upper bound of this location is invalid.
  /// \returns True when the invalid-upper property is set.
  bool getIsInvalidUpper() const {
    return Properties.get(Property::IsInvalidUpper);
  }
  /// Mark the upper bound of this location as invalid.
  void setIsInvalidUpper() { Properties.set(Property::IsInvalidUpper); }
  /// Clear the invalid-upper property on this location.
  void resetIsInvalidUpper() { Properties.reset(Property::IsInvalidUpper); }
  /// Return whether this location describes a call site.
  /// \returns True when the call-site property is set.
  bool getIsCallSite() const { return Properties.get(Property::IsCallSite); }
  /// Mark this location as describing a call site.
  void setIsCallSite() { Properties.set(Property::IsCallSite); }
  /// Clear the call-site property on this location.
  void resetIsCallSite() { Properties.reset(Property::IsCallSite); }

  /// Return a string naming the kind of this location.
  /// \returns C string describing the location kind.
  const char *kind() const override;
  /// Update the location kind for simple stack-offset based locations.
  ///
  /// Mark the locations that have only DW_OP_fbreg as stack offset based.
  virtual void updateKind() {}

  /// Return the lower source line for this location range.
  /// \returns Pointer to the lower line, or nullptr if unset.
  const LVLine *getLowerLine() const { return LowerLine; }
  /// Set the lower source line for this location range.
  /// \param Line Lower line to associate with the range.
  void setLowerLine(LVLine *Line) { LowerLine = Line; }
  /// Return the upper source line for this location range.
  /// \returns Pointer to the upper line, or nullptr if unset.
  const LVLine *getUpperLine() const { return UpperLine; }
  /// Set the upper source line for this location range.
  /// \param Line Upper line to associate with the range.
  void setUpperLine(LVLine *Line) { UpperLine = Line; }

  /// Return the lower address of this location range.
  /// \returns Lower address offset of the active range.
  LVAddress getLowerAddress() const override { return LowPC; }
  /// Set the lower address of this location range.
  /// \param Address Lower address offset to store.
  void setLowerAddress(LVAddress Address) override { LowPC = Address; }
  /// Return the upper address of this location range.
  /// \returns Upper address offset or length of the active range.
  LVAddress getUpperAddress() const override { return HighPC; }
  /// Set the upper address of this location range.
  /// \param Address Upper address offset or length to store.
  void setUpperAddress(LVAddress Address) override { HighPC = Address; }

  /// Return a formatted string describing the location interval.
  /// \returns Formatted interval description string.
  std::string getIntervalInfo() const;

  /// Validate the address range against the compile unit line mapping.
  /// \returns True when the range is valid or has no associated range.
  bool validateRanges();

  /// Calculate symbol coverage from \p Locations into \p Factor and
  /// \p Percentage.
  ///
  /// In order to calculate a symbol coverage (percentage), take the ranges
  /// and obtain the number of units (bytes) covered by those ranges. We can't
  /// use the line numbers, because they can be zero or invalid.
  /// We return:
  ///   false: No locations or multiple locations.
  ///   true: a single location.
  /// \param Locations Locations whose coverage should be calculated.
  /// \param Factor Cumulative coverage factor in address units.
  /// \param Percentage Coverage percentage for a simple location.
  /// \returns True when \p Locations holds a single simple location.
  static bool calculateCoverage(LVLocations *Locations, unsigned &Factor,
                                float &Percentage);

  /// Record an address range and location-descriptor offsets.
  /// \param LowPC Lower address of the range.
  /// \param HighPC Upper address of the range.
  /// \param SectionOffset Offset of the location section entry.
  /// \param LocDescOffset Offset of the location description.
  virtual void addObject(LVAddress LowPC, LVAddress HighPC,
                         LVUnsigned SectionOffset, uint64_t LocDescOffset) {}
  /// Record a location-expression operation.
  /// \param Opcode Location-expression opcode.
  /// \param Operands Operand values for the opcode.
  virtual void addObject(LVSmall Opcode, ArrayRef<LVUnsigned> Operands) {}

  /// Print every location in \p Locations to \p OS.
  /// \param Locations Locations to print.
  /// \param OS Stream that receives the printed locations.
  /// \param Full Whether to print full location details.
  static void print(LVLocations *Locations, raw_ostream &OS, bool Full = true);
  /// Print the location interval to \p OS.
  /// \param OS Stream that receives the printed interval.
  /// \param Full Whether to print full interval details.
  void printInterval(raw_ostream &OS, bool Full = true) const;
  /// Print the raw address range and extras to \p OS.
  /// \param OS Stream that receives the printed range.
  /// \param Full Whether to print full raw details.
  void printRaw(raw_ostream &OS, bool Full = true) const;
  /// Print kind-specific raw extras for this location to \p OS.
  /// \param OS Stream that receives the printed extras.
  /// \param Full Whether to print full extra details.
  virtual void printRawExtra(raw_ostream &OS, bool Full = true) const {}

  /// Print this location to \p OS.
  /// \param OS Stream that receives the printed location.
  /// \param Full Whether to print full location details.
  void print(raw_ostream &OS, bool Full = true) const override;
  /// Print location-specific extra information to \p OS.
  /// \param OS Stream that receives the printed extras.
  /// \param Full Whether to print full extra details.
  void printExtra(raw_ostream &OS, bool Full = true) const override;

  /// Print the basic and extra information for this location to \p OS.
  ///
  /// Used mainly to debug IR.
  /// \param OS Stream that receives the printed information.
  /// \param Full Whether to print full details.
  void printCommon(raw_ostream &OS, bool Full = true) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump basic and extra location information to the debug stream.
  void dumpCommon() const { printCommon(dbgs(), /*Full=*/true); }
#endif
};

/// Location attached to a symbol, including its expression operations.
class LLVM_ABI LVLocationSymbol final : public LVLocation {
  // Location descriptors for the active range.
  std::unique_ptr<LVOperations> Entries;

  void updateKind() override;

public:
  /// Construct an empty symbol location.
  LVLocationSymbol() : LVLocation() {}
  /// Copy construction is not allowed.
  /// \param Other Unused source symbol location.
  LVLocationSymbol(const LVLocationSymbol &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source symbol location.
  LVLocationSymbol &operator=(const LVLocationSymbol &Other) = delete;
  /// Destroy the symbol location.
  ~LVLocationSymbol() override = default;

  /// Record an address range and location-descriptor offsets.
  /// \param LowPC Lower address of the range.
  /// \param HighPC Upper address of the range.
  /// \param SectionOffset Offset of the location section entry.
  /// \param LocDescOffset Offset of the location description.
  void addObject(LVAddress LowPC, LVAddress HighPC, LVUnsigned SectionOffset,
                 uint64_t LocDescOffset) override;
  /// Record a location-expression operation for this symbol location.
  /// \param Opcode Location-expression opcode.
  /// \param Operands Operand values for the opcode.
  void addObject(LVSmall Opcode, ArrayRef<LVUnsigned> Operands) override;

  /// Print the raw location operations to \p OS.
  /// \param OS Stream that receives the printed operations.
  /// \param Full Whether to print full operation details.
  void printRawExtra(raw_ostream &OS, bool Full = true) const override;
  /// Print symbol-location extras and entries to \p OS.
  /// \param OS Stream that receives the printed extras.
  /// \param Full Whether to print full entry details.
  void printExtra(raw_ostream &OS, bool Full = true) const override;
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVLOCATION_H
