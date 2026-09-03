//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFUNWINDTABLE_H
#define LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFUNWINDTABLE_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/DWARF/LowLevel/DWARFCFIProgram.h"
#include "llvm/DebugInfo/DWARF/LowLevel/DWARFExpression.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <map>
#include <vector>

namespace llvm {

namespace dwarf {
/// Sentinel value used when a register number is not valid or not set.
constexpr uint32_t InvalidRegisterNumber = UINT32_MAX;

/// A location for the CFA or a register in an unwind row.
///
/// This is decoded from the DWARF Call Frame Information instructions and put
/// into an UnwindRow.
class UnwindLocation {
public:
  /// How a CFA or register value is recovered during unwinding.
  enum Location {
    /// Not specified.
    Unspecified,
    /// Register is not available and can't be recovered.
    Undefined,
    /// Register value is in the register, nothing needs to be done to unwind
    /// it:
    ///   reg = reg
    Same,
    /// Register is in or at the CFA plus an offset:
    ///   reg = CFA + offset
    ///   reg = defef(CFA + offset)
    CFAPlusOffset,
    /// Register or CFA is in or at a register plus offset, optionally in
    /// an address space:
    ///   reg = reg + offset [in addrspace]
    ///   reg = deref(reg + offset [in addrspace])
    RegPlusOffset,
    /// Register or CFA value is in or at a value found by evaluating a DWARF
    /// expression:
    ///   reg = eval(dwarf_expr)
    ///   reg = deref(eval(dwarf_expr))
    DWARFExpr,
    /// Value is a constant value contained in "Offset":
    ///   reg = Offset
    Constant,
  };

private:
  Location Kind;   /// The type of the location that describes how to unwind it.
  uint32_t RegNum; /// The register number for Kind == RegPlusOffset.
  int32_t Offset;  /// The offset for Kind == CFAPlusOffset or RegPlusOffset.
  std::optional<uint32_t> AddrSpace;   /// The address space for Kind ==
                                       /// RegPlusOffset for CFA.
  std::optional<DWARFExpression> Expr; /// The DWARF expression for Kind ==
                                       /// DWARFExpression.
  bool Dereference; /// If true, the resulting location must be dereferenced
                    /// after the location value is computed.

  // Constructors are private to force people to use the create static
  // functions.
  UnwindLocation(Location K)
      : Kind(K), RegNum(InvalidRegisterNumber), Offset(0),
        AddrSpace(std::nullopt), Dereference(false) {}

  UnwindLocation(Location K, uint32_t Reg, int32_t Off,
                 std::optional<uint32_t> AS, bool Deref)
      : Kind(K), RegNum(Reg), Offset(Off), AddrSpace(AS), Dereference(Deref) {}

  UnwindLocation(DWARFExpression E, bool Deref)
      : Kind(DWARFExpr), RegNum(InvalidRegisterNumber), Offset(0), Expr(E),
        Dereference(Deref) {}

public:
  /// Create a location whose rule is set to Unspecified. This means the
  /// register value might be in the same register but it wasn't specified in
  /// the unwind opcodes.
  ///
  /// \returns An UnwindLocation with Unspecified kind.
  LLVM_ABI static UnwindLocation createUnspecified();
  /// Create a location where the value is undefined and not available. This can
  /// happen when a register is volatile and can't be recovered.
  ///
  /// \returns An UnwindLocation with Undefined kind.
  LLVM_ABI static UnwindLocation createUndefined();
  /// Create a location where the value is known to be in the register itself.
  ///
  /// \returns An UnwindLocation with Same kind.
  LLVM_ABI static UnwindLocation createSame();
  /// Create a location whose value is the CFA plus an offset.
  ///
  /// Most registers that are spilled onto the stack use this rule. The rule for
  /// the register will use this rule and specify a unique offset from the CFA
  /// with \a Deref set to true. This value will be relative to a CFA value which
  /// is typically defined using the register plus offset location.
  /// \see createRegisterPlusOffset(...) for more information.
  ///
  /// \param Off Offset from the CFA.
  ///
  /// \returns An UnwindLocation for CFA plus \p Off without dereference.
  LLVM_ABI static UnwindLocation createIsCFAPlusOffset(int32_t Off);
  /// Create a location that must be dereferenced at the CFA plus an offset.
  ///
  /// \param Off Offset from the CFA.
  ///
  /// \returns An UnwindLocation that must be dereferenced at CFA plus \p Off.
  LLVM_ABI static UnwindLocation createAtCFAPlusOffset(int32_t Off);
  /// Create a location at a register plus an offset, without dereference.
  ///
  /// Optionally in the specified address space (used mostly for the CFA). The
  /// CFA is usually defined using this rule by using the stack pointer or frame
  /// pointer as the register, with an offset that accounts for all spilled
  /// registers and all local variables in a function, and Deref == false.
  ///
  /// \param Reg Register number that holds the base address.
  /// \param Off Offset from the register value.
  /// \param AddrSpace Optional address space for the location.
  ///
  /// \returns An UnwindLocation for register plus offset without dereference.
  LLVM_ABI static UnwindLocation
  createIsRegisterPlusOffset(uint32_t Reg, int32_t Off,
                             std::optional<uint32_t> AddrSpace = std::nullopt);
  /// Create a location that must be dereferenced at a register plus an offset.
  ///
  /// \param Reg Register number that holds the base address.
  /// \param Off Offset from the register value.
  /// \param AddrSpace Optional address space for the location.
  ///
  /// \returns An UnwindLocation that must be dereferenced at register plus
  /// offset.
  LLVM_ABI static UnwindLocation
  createAtRegisterPlusOffset(uint32_t Reg, int32_t Off,
                             std::optional<uint32_t> AddrSpace = std::nullopt);
  /// Create a location whose value is the result of a DWARF expression.
  ///
  /// This allows complex expressions to be evaluated in order to unwind a
  /// register or CFA value.
  ///
  /// \param Expr DWARF expression that computes the location value.
  ///
  /// \returns An UnwindLocation whose value is the result of evaluating \p Expr.
  LLVM_ABI static UnwindLocation createIsDWARFExpression(DWARFExpression Expr);
  /// Create a location that must be dereferenced after evaluating a DWARF
  /// expression.
  ///
  /// \param Expr DWARF expression that computes the address to dereference.
  ///
  /// \returns An UnwindLocation that must be dereferenced after evaluating
  /// \p Expr.
  LLVM_ABI static UnwindLocation createAtDWARFExpression(DWARFExpression Expr);
  /// Create a location whose value is the constant \p Value.
  ///
  /// \param Value Constant value stored in the location.
  ///
  /// \returns An UnwindLocation whose value is the constant \p Value.
  LLVM_ABI static UnwindLocation createIsConstant(int32_t Value);

  /// Return the kind of this unwind location.
  ///
  /// \returns The Location kind of this unwind location.
  Location getLocation() const { return Kind; }
  /// Return the register number for RegPlusOffset locations.
  ///
  /// \returns The register number.
  uint32_t getRegister() const { return RegNum; }
  /// Return the offset for CFAPlusOffset or RegPlusOffset locations.
  ///
  /// \returns The offset value.
  int32_t getOffset() const { return Offset; }
  /// Return true if this location has an address space.
  ///
  /// \returns True if this location has an address space.
  bool hasAddressSpace() const {
    if (AddrSpace)
      return true;
    return false;
  }
  /// Return the address space for RegPlusOffset CFA locations.
  ///
  /// \returns The address space value.
  uint32_t getAddressSpace() const {
    assert(Kind == RegPlusOffset && AddrSpace);
    return *AddrSpace;
  }
  /// Return the constant value for Constant locations.
  ///
  /// \returns The constant value.
  int32_t getConstant() const { return Offset; }
  /// Return true if the computed location must be dereferenced.
  ///
  /// \returns True if the computed location must be dereferenced.
  bool getDereference() const { return Dereference; }

  /// Set the register number used by this location.
  ///
  /// Some opcodes will modify the CFA location's register only, so we need to
  /// be able to modify the CFA register when evaluating DWARF Call Frame
  /// Information opcodes.
  ///
  /// \param NewRegNum New register number for this location.
  void setRegister(uint32_t NewRegNum) { RegNum = NewRegNum; }
  /// Some opcodes will modify the CFA location's offset only, so we need
  /// to be able to modify the CFA offset when evaluating DWARF Call Frame
  /// Information opcodes.
  ///
  /// \param NewOffset New offset for this location.
  void setOffset(int32_t NewOffset) { Offset = NewOffset; }
  /// Set the constant value used by this location.
  ///
  /// Some opcodes modify a constant value and we need to be able to update
  /// the constant value (DW_CFA_GNU_window_save which is also known as
  /// DW_CFA_AARCH64_negate_ra_state).
  ///
  /// \param Value New constant value for this location.
  void setConstant(int32_t Value) { Offset = Value; }

  /// Return the optional DWARF expression for DWARFExpr locations.
  ///
  /// \returns The DWARF expression, or std::nullopt if none.
  std::optional<DWARFExpression> getDWARFExpressionBytes() const {
    return Expr;
  }

  /// Return true if this location equals \p RHS.
  ///
  /// \param RHS Location to compare against.
  ///
  /// \returns True if this location equals \p RHS.
  LLVM_ABI bool operator==(const UnwindLocation &RHS) const;
};

/// A class that can track all registers with locations in a UnwindRow object.
///
/// Register locations use a map where the key is the register number and the
/// the value is a UnwindLocation.
///
/// The register maps are put into a class so that all register locations can
/// be copied when parsing the unwind opcodes DW_CFA_remember_state and
/// DW_CFA_restore_state.
class RegisterLocations {
  std::map<uint32_t, UnwindLocation> Locations;

public:
  /// Return the location for the register in \a RegNum if there is a location.
  ///
  /// \param RegNum the register number to find a location for.
  ///
  /// \returns A location if one is available for \a RegNum, or std::nullopt
  /// otherwise.
  std::optional<UnwindLocation> getRegisterLocation(uint32_t RegNum) const {
    auto Pos = Locations.find(RegNum);
    if (Pos == Locations.end())
      return std::nullopt;
    return Pos->second;
  }

  /// Return the register numbers that have locations in this object.
  ///
  /// \returns A SmallVector of register numbers that have locations.
  SmallVector<uint32_t, 4> getRegisters() const {
    SmallVector<uint32_t, 4> Registers;
    for (auto &&[Register, _] : Locations)
      Registers.push_back(Register);
    return Registers;
  }

  /// Set the location for the register in \a RegNum to \a Location.
  ///
  /// \param RegNum the register number to set the location for.
  ///
  /// \param Location the UnwindLocation that describes how to unwind the value.
  void setRegisterLocation(uint32_t RegNum, const UnwindLocation &Location) {
    Locations.erase(RegNum);
    Locations.insert(std::make_pair(RegNum, Location));
  }

  /// Removes any rule for the register in \a RegNum.
  ///
  /// \param RegNum the register number to remove the location for.
  void removeRegisterLocation(uint32_t RegNum) { Locations.erase(RegNum); }

  /// Returns true if we have any register locations in this object.
  ///
  /// \returns True if this object has any register locations.
  bool hasLocations() const { return !Locations.empty(); }

  /// Return the number of register locations in this object.
  ///
  /// \returns The number of register locations.
  size_t size() const { return Locations.size(); }

  /// Return true if this object equals \p RHS.
  ///
  /// \param RHS Register locations to compare against.
  ///
  /// \returns True if this object equals \p RHS.
  bool operator==(const RegisterLocations &RHS) const {
    return Locations == RHS.Locations;
  }
};

/// A class that represents a single row in the unwind table that is decoded by
/// parsing the DWARF Call Frame Information opcodes.
///
/// The row consists of an optional address, the rule to unwind the CFA and all
/// rules to unwind any registers. If the address doesn't have a value, this
/// row represents the initial instructions for a CIE. If the address has a
/// value the UnwindRow represents a row in the UnwindTable for a FDE. The
/// address is the first address for which the CFA location and register rules
/// are valid within a function.
///
/// UnwindRow objects are created by parsing opcodes in the DWARF Call Frame
/// Information and UnwindRow objects are lazily populated and pushed onto a
/// stack in the UnwindTable when evaluating this state machine. Accessors are
/// needed for the address, CFA value, and register locations as the opcodes
/// encode a state machine that produces a sorted array of UnwindRow objects
/// \see UnwindTable.
class UnwindRow {
  /// The address will be valid when parsing the instructions in a FDE. If
  /// invalid, this object represents the initial instructions of a CIE.
  std::optional<uint64_t> Address; ///< Address for row in FDE, invalid for CIE.
  UnwindLocation CFAValue;   ///< How to unwind the Call Frame Address (CFA).
  RegisterLocations RegLocs; ///< How to unwind all registers in this list.

public:
  /// Construct an empty unwind row with an unspecified CFA location.
  UnwindRow() : CFAValue(UnwindLocation::createUnspecified()) {}

  /// Returns true if the address is valid in this object.
  ///
  /// \returns True if the address is valid in this object.
  bool hasAddress() const { return Address.has_value(); }

  /// Get the address for this row.
  ///
  /// Clients should only call this function after verifying it has a valid
  /// address with a call to \see hasAddress().
  ///
  /// \returns The address for this row.
  uint64_t getAddress() const { return *Address; }

  /// Set the address for this UnwindRow.
  ///
  /// The address represents the first address for which the CFAValue and
  /// RegLocs are valid within a function.
  ///
  /// \param Addr First address at which this row's rules apply.
  void setAddress(uint64_t Addr) { Address = Addr; }

  /// Offset the address for this UnwindRow.
  ///
  /// The address represents the first address for which the CFAValue and
  /// RegLocs are valid within a function. Clients must ensure that this object
  /// already has an address (\see hasAddress()) prior to calling this
  /// function.
  ///
  /// \param Offset Amount to add to this row's address.
  void slideAddress(uint64_t Offset) { *Address += Offset; }
  /// Return a mutable reference to the CFA unwind location.
  ///
  /// \returns A mutable reference to the CFA unwind location.
  UnwindLocation &getCFAValue() { return CFAValue; }
  /// Return the CFA unwind location.
  ///
  /// \returns The CFA unwind location.
  const UnwindLocation &getCFAValue() const { return CFAValue; }
  /// Return a mutable reference to the register locations for this row.
  ///
  /// \returns A mutable reference to the register locations for this row.
  RegisterLocations &getRegisterLocations() { return RegLocs; }
  /// Return the register locations for this row.
  ///
  /// \returns The register locations for this row.
  const RegisterLocations &getRegisterLocations() const { return RegLocs; }
};

/// A table of UnwindRow objects for a CIE or FDE.
///
/// Contains all UnwindRow objects for an FDE or a single unwind row for a CIE.
/// To unwind an address the rows, which are sorted by start address, can be
/// searched to find the UnwindRow with the lowest starting address that is
/// greater than or equal to the address that is being looked up.
class UnwindTable {
public:
  /// Container type that stores the unwind rows.
  using RowContainer = std::vector<UnwindRow>;
  /// Iterator over mutable unwind rows.
  using iterator = RowContainer::iterator;
  /// Iterator over const unwind rows.
  using const_iterator = RowContainer::const_iterator;

  /// Construct an unwind table from the rows in \p Rows.
  ///
  /// \param Rows Row container to take ownership of.
  UnwindTable(RowContainer &&Rows) : Rows(std::move(Rows)) {}

  /// Return the number of unwind rows in this table.
  ///
  /// \returns The number of unwind rows.
  size_t size() const { return Rows.size(); }
  /// Return an iterator to the first unwind row.
  ///
  /// \returns An iterator to the first unwind row.
  iterator begin() { return Rows.begin(); }
  /// Return an iterator to the first unwind row.
  ///
  /// \returns A const iterator to the first unwind row.
  const_iterator begin() const { return Rows.begin(); }
  /// Return an iterator past the last unwind row.
  ///
  /// \returns An iterator past the last unwind row.
  iterator end() { return Rows.end(); }
  /// Return an iterator past the last unwind row.
  ///
  /// \returns A const iterator past the last unwind row.
  const_iterator end() const { return Rows.end(); }
  /// Return the unwind row at \p Index.
  ///
  /// \param Index Zero-based index into the row container.
  ///
  /// \returns A const reference to the unwind row at \p Index.
  const UnwindRow &operator[](size_t Index) const {
    assert(Index < size());
    return Rows[Index];
  }

private:
  RowContainer Rows;
};

/// Parse the information in the CFIProgram and update the CurrRow object
/// that the state machine describes.
///
/// This function emulates the state machine described in the DWARF Call Frame
/// Information opcodes and will push CurrRow onto a RowContainer when needed.
///
/// \param CFIP the CFI program that contains the opcodes from a CIE or FDE.
///
/// \param CurrRow the current row to modify while parsing the state machine.
///
/// \param InitialLocs If non-NULL, we are parsing a FDE and this contains
/// the initial register locations from the CIE. If NULL, then a CIE's
/// opcodes are being parsed and this is not needed. This is used for the
/// DW_CFA_restore and DW_CFA_restore_extended opcodes.
///
/// \returns An error if the DWARF Call Frame Information opcodes have state
/// machine errors, or the accumulated rows otherwise.
LLVM_ABI Expected<UnwindTable::RowContainer>
parseRows(const CFIProgram &CFIP, UnwindRow &CurrRow,
          const RegisterLocations *InitialLocs);

} // end namespace dwarf

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_LOWLEVEL_DWARFUNWINDTABLE_H
