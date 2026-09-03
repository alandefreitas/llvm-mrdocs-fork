//===- MIParser.h - Machine Instructions Parser -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the function that parses the machine instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRPARSER_MIPARSER_H
#define LLVM_CODEGEN_MIRPARSER_MIPARSER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/IR/TrackingMDRef.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/UniqueBBID.h"
#include <map>
#include <utility>

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MDNode;
class RegisterBank;
struct SlotMapping;
class SMDiagnostic;
class SourceMgr;
class StringRef;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class TargetSubtargetInfo;

/// Parsing state for a virtual register referenced in MIR.
struct VRegInfo {
  /// How this virtual register is typed during MIR parsing.
  enum : uint8_t {
    UNKNOWN, ///< Kind has not been determined yet.
    NORMAL,  ///< Typed with a target register class.
    GENERIC, ///< Untyped generic virtual register.
    REGBANK  ///< Typed with a register bank.
  } Kind = UNKNOWN; ///< Active typing kind for this virtual register.
  bool Explicit = false; ///< VReg was explicitly specified in the .mir file.
  /// Register class or bank that types this virtual register.
  union {
    const TargetRegisterClass *RC; ///< Target register class when Kind is NORMAL.
    const RegisterBank *RegBank;   ///< Register bank when Kind is REGBANK.
  } D; ///< Active typing payload selected by \c Kind.
  Register VReg;         ///< Assigned virtual register number.
  Register PreferredReg; ///< Preferred physical register, if any.
  uint8_t Flags = 0;     ///< Target-specific virtual register flags.
};

/// Map from register class names to register classes.
using Name2RegClassMap = StringMap<const TargetRegisterClass *>;
/// Map from register bank names to register banks.
using Name2RegBankMap = StringMap<const RegisterBank *>;

/// Target-specific name tables used while parsing MIR machine instructions.
struct PerTargetMIParsingState {
private:
  const TargetSubtargetInfo &Subtarget;

  /// Maps from instruction names to op codes.
  StringMap<unsigned> Names2InstrOpCodes;

  /// Maps from register names to registers.
  StringMap<Register> Names2Regs;

  /// Maps from register mask names to register masks.
  StringMap<const uint32_t *> Names2RegMasks;

  /// Maps from subregister names to subregister indices.
  StringMap<unsigned> Names2SubRegIndices;

  /// Maps from target index names to target indices.
  StringMap<int> Names2TargetIndices;

  /// Maps from direct target flag names to the direct target flag values.
  StringMap<unsigned> Names2DirectTargetFlags;

  /// Maps from direct target flag names to the bitmask target flag values.
  StringMap<unsigned> Names2BitmaskTargetFlags;

  /// Maps from MMO target flag names to MMO target flag values.
  StringMap<MachineMemOperand::Flags> Names2MMOTargetFlags;

  /// Maps from register class names to register classes.
  Name2RegClassMap Names2RegClasses;

  /// Maps from register bank names to register banks.
  Name2RegBankMap Names2RegBanks;

  void initNames2InstrOpCodes();
  void initNames2Regs();
  void initNames2RegMasks();
  void initNames2SubRegIndices();
  void initNames2TargetIndices();
  void initNames2DirectTargetFlags();
  void initNames2BitmaskTargetFlags();
  void initNames2MMOTargetFlags();

  LLVM_ABI void initNames2RegClasses();
  LLVM_ABI void initNames2RegBanks();

public:
  /// Try to convert an instruction name to an opcode. Return true if the
  /// instruction name is invalid.
  ///
  /// \param InstrName - The instruction mnemonic to look up.
  /// \param OpCode - Set to the resolved opcode on success.
  /// \returns true if the instruction name is invalid.
  LLVM_ABI bool parseInstrName(StringRef InstrName, unsigned &OpCode);

  /// Try to convert a register name to a register number. Return true if the
  /// register name is invalid.
  ///
  /// \param RegName - The register name to look up.
  /// \param Reg - Set to the resolved register on success.
  /// \returns true if the register name is invalid.
  LLVM_ABI bool getRegisterByName(StringRef RegName, Register &Reg);

  /// Check if the given identifier is a name of a register mask.
  ///
  /// \param Identifier - The register mask name to look up.
  /// \returns The register mask, or null if the identifier isn't a register
  /// mask.
  LLVM_ABI const uint32_t *getRegMask(StringRef Identifier);

  /// Check if the given identifier is a name of a subregister index.
  ///
  /// \param Name - The subregister index name to look up.
  /// \returns The subregister index, or 0 if the name isn't a subregister
  /// index class.
  LLVM_ABI unsigned getSubRegIndex(StringRef Name);

  /// Try to convert a name of target index to the corresponding target index.
  ///
  /// \param Name - The target index name to look up.
  /// \param Index - Set to the resolved target index on success.
  /// \returns true if the name isn't a name of a target index.
  LLVM_ABI bool getTargetIndex(StringRef Name, int &Index);

  /// Try to convert a name of a direct target flag to the corresponding
  /// target flag.
  ///
  /// \param Name - The direct target flag name to look up.
  /// \param Flag - Set to the resolved flag value on success.
  /// \returns true if the name isn't a name of a direct flag.
  LLVM_ABI bool getDirectTargetFlag(StringRef Name, unsigned &Flag);

  /// Try to convert a name of a bitmask target flag to the corresponding
  /// target flag.
  ///
  /// \param Name - The bitmask target flag name to look up.
  /// \param Flag - Set to the resolved flag value on success.
  /// \returns true if the name isn't a name of a bitmask target flag.
  LLVM_ABI bool getBitmaskTargetFlag(StringRef Name, unsigned &Flag);

  /// Try to convert a name of a MachineMemOperand target flag to the
  /// corresponding target flag.
  ///
  /// \param Name - The MMO target flag name to look up.
  /// \param Flag - Set to the resolved MMO flag on success.
  /// \returns true if the name isn't a name of a target MMO flag.
  LLVM_ABI bool getMMOTargetFlag(StringRef Name,
                                 MachineMemOperand::Flags &Flag);

  /// Check if the given identifier is a name of a register class.
  ///
  /// \param Name - The register class name to look up.
  /// \returns The register class, or null if the name isn't a register class.
  LLVM_ABI const TargetRegisterClass *getRegClass(StringRef Name);

  /// Check if the given identifier is a name of a register bank.
  ///
  /// \param Name - The register bank name to look up.
  /// \returns The register bank, or null if the name isn't a register bank.
  LLVM_ABI const RegisterBank *getRegBank(StringRef Name);

  /// Try to convert a virtual register flag name to its value. Return true if
  /// the flag name is invalid.
  ///
  /// \param FlagName - The virtual register flag name to look up.
  /// \param FlagValue - Set to the resolved flag value on success.
  /// \returns true if the flag name is invalid.
  LLVM_ABI bool getVRegFlagValue(StringRef FlagName, uint8_t &FlagValue) const;

  /// Construct parsing state for the given subtarget.
  ///
  /// \param STI - Subtarget whose register classes and banks are indexed.
  PerTargetMIParsingState(const TargetSubtargetInfo &STI)
    : Subtarget(STI) {
    initNames2RegClasses();
    initNames2RegBanks();
  }

  /// Destroy the per-target MIR parsing state.
  ~PerTargetMIParsingState() = default;

  /// Reinitialize name tables if the active subtarget changes.
  ///
  /// \param NewSubtarget - Subtarget to parse against going forward.
  LLVM_ABI void setTarget(const TargetSubtargetInfo &NewSubtarget);
};

/// Per-function MIR parsing state, including slots and virtual registers.
struct PerFunctionMIParsingState {
  BumpPtrAllocator Allocator; ///< Allocator for VRegInfo and related objects.
  MachineFunction &MF;        ///< Machine function being parsed.
  SourceMgr *SM;              ///< Source manager for the MIR being parsed.
  const SlotMapping &IRSlots; ///< Slot mapping for the embedded LLVM IR.
  PerTargetMIParsingState &Target; ///< Target-specific name lookup tables.

  /// Machine metadata nodes keyed by slot number.
  std::map<unsigned, TrackingMDNodeRef> MachineMetadataNodes;
  /// Forward-referenced machine metadata awaiting definition.
  std::map<unsigned, std::pair<TempMDTuple, SMLoc>> MachineForwardRefMDNodes;

  DenseMap<unsigned, MachineBasicBlock *> MBBSlots; ///< MBB slot to block map.
  DenseMap<Register, VRegInfo *> VRegInfos; ///< Numbered virtual register info.
  StringMap<VRegInfo *> VRegInfosNamed;     ///< Named virtual register info.
  DenseMap<unsigned, int> FixedStackObjectSlots; ///< Fixed stack object slots.
  DenseMap<unsigned, int> StackObjectSlots;      ///< Ordinary stack object slots.
  DenseMap<unsigned, unsigned> ConstantPoolSlots; ///< Constant pool entry slots.
  DenseMap<unsigned, unsigned> JumpTableSlots;    ///< Jump table entry slots.

  /// Maps from slot numbers to function's unnamed values.
  DenseMap<unsigned, const Value *> Slots2Values;

  /// Construct parsing state for one machine function.
  ///
  /// \param MF - Machine function being parsed.
  /// \param SM - Source manager for MIR diagnostics.
  /// \param IRSlots - Slot mapping for the embedded LLVM IR module.
  /// \param Target - Target-specific name lookup tables.
  LLVM_ABI PerFunctionMIParsingState(MachineFunction &MF, SourceMgr &SM,
                                     const SlotMapping &IRSlots,
                                     PerTargetMIParsingState &Target);

  /// Get or create info for the numbered virtual register \p Num.
  ///
  /// \param Num - Numbered virtual register to look up or create.
  /// \returns The VRegInfo for \p Num.
  LLVM_ABI VRegInfo &getVRegInfo(Register Num);
  /// Get or create info for the named virtual register \p RegName.
  ///
  /// \param RegName - Named virtual register to look up or create.
  /// \returns The VRegInfo for \p RegName.
  LLVM_ABI VRegInfo &getVRegInfoNamed(StringRef RegName);
  /// Return the IR value mapped to unnamed value slot \p Slot.
  ///
  /// \param Slot - Unnamed IR value slot number to look up.
  /// \returns The IR value mapped to \p Slot, or null if none.
  LLVM_ABI const Value *getIRValue(unsigned Slot);
};

/// Parse the machine basic block definitions, and skip the machine
/// instructions.
///
/// This function runs the first parsing pass on the machine function's body.
/// It parses only the machine basic block definitions and creates the machine
/// basic blocks in the given machine function.
///
/// The machine instructions aren't parsed during the first pass because all
/// the machine basic blocks aren't defined yet - this makes it impossible to
/// resolve the machine basic block references.
///
/// \param PFS - Per-function parsing state that receives the new blocks.
/// \param Src - Machine function body source to parse.
/// \param Error - Diagnostic filled in when parsing fails.
/// \returns true if an error occurred.
LLVM_ABI bool parseMachineBasicBlockDefinitions(PerFunctionMIParsingState &PFS,
                                                StringRef Src,
                                                SMDiagnostic &Error);

/// Parse the machine instructions.
///
/// This function runs the second parsing pass on the machine function's body.
/// It skips the machine basic block definitions and parses only the machine
/// instructions and basic block attributes like liveins and successors.
///
/// The second parsing pass assumes that the first parsing pass already ran
/// on the given source string.
///
/// \param PFS - Per-function parsing state with blocks already defined.
/// \param Src - Machine function body source to parse.
/// \param Error - Diagnostic filled in when parsing fails.
/// \returns true if an error occurred.
LLVM_ABI bool parseMachineInstructions(PerFunctionMIParsingState &PFS,
                                       StringRef Src, SMDiagnostic &Error);

/// Parse a machine basic block reference from \p Src.
///
/// \param PFS - Per-function parsing state with MBB slots.
/// \param MBB - Set to the referenced machine basic block on success.
/// \param Src - Source text containing the MBB reference.
/// \param Error - Diagnostic filled in when parsing fails.
/// \returns true if an error occurred.
LLVM_ABI bool parseMBBReference(PerFunctionMIParsingState &PFS,
                                MachineBasicBlock *&MBB, StringRef Src,
                                SMDiagnostic &Error);

/// Parse a register reference from \p Src.
///
/// \param PFS - Per-function parsing state used for name and VReg lookup.
/// \param Reg - Set to the referenced register on success.
/// \param Src - Source text containing the register reference.
/// \param Error - Diagnostic filled in when parsing fails.
/// \returns true if an error occurred.
LLVM_ABI bool parseRegisterReference(PerFunctionMIParsingState &PFS,
                                     Register &Reg, StringRef Src,
                                     SMDiagnostic &Error);

/// Parse a named physical register reference from \p Src.
///
/// \param PFS - Per-function parsing state used for register name lookup.
/// \param Reg - Set to the referenced register on success.
/// \param Src - Source text containing the named register reference.
/// \param Error - Diagnostic filled in when parsing fails.
/// \returns true if an error occurred.
LLVM_ABI bool parseNamedRegisterReference(PerFunctionMIParsingState &PFS,
                                          Register &Reg, StringRef Src,
                                          SMDiagnostic &Error);

/// Parse a virtual register reference from \p Src.
///
/// \param PFS - Per-function parsing state that owns VRegInfo entries.
/// \param Info - Set to the referenced virtual register info on success.
/// \param Src - Source text containing the virtual register reference.
/// \param Error - Diagnostic filled in when parsing fails.
/// \returns true if an error occurred.
LLVM_ABI bool parseVirtualRegisterReference(PerFunctionMIParsingState &PFS,
                                            VRegInfo *&Info, StringRef Src,
                                            SMDiagnostic &Error);

/// Parse a stack object reference from \p Src.
///
/// \param PFS - Per-function parsing state with stack object slots.
/// \param FI - Set to the referenced frame index on success.
/// \param Src - Source text containing the stack object reference.
/// \param Error - Diagnostic filled in when parsing fails.
/// \returns true if an error occurred.
LLVM_ABI bool parseStackObjectReference(PerFunctionMIParsingState &PFS, int &FI,
                                        StringRef Src, SMDiagnostic &Error);

/// Parse a prefetch target (BBID and callsite index) from \p Src.
///
/// \param PFS - Per-function parsing state used while parsing the target.
/// \param Target - Set to the parsed prefetch target on success.
/// \param Src - Source text containing the prefetch target.
/// \param Error - Diagnostic filled in when parsing fails.
/// \returns true if an error occurred.
LLVM_ABI bool parsePrefetchTarget(PerFunctionMIParsingState &PFS,
                                  CallsiteID &Target, StringRef Src,
                                  SMDiagnostic &Error);

/// Parse a metadata node reference from \p Src.
///
/// \param PFS - Per-function parsing state with machine metadata slots.
/// \param Node - Set to the referenced metadata node on success.
/// \param Src - Source text containing the metadata node reference.
/// \param Error - Diagnostic filled in when parsing fails.
/// \returns true if an error occurred.
LLVM_ABI bool parseMDNode(PerFunctionMIParsingState &PFS, MDNode *&Node,
                          StringRef Src, SMDiagnostic &Error);

/// Parse a machine metadata definition from \p Src.
///
/// \param PFS - Per-function parsing state that receives the metadata node.
/// \param Src - Source text containing the machine metadata definition.
/// \param SourceRange - Source range used when rewriting diagnostics.
/// \param Error - Diagnostic filled in when parsing fails.
/// \returns true if an error occurred.
LLVM_ABI bool parseMachineMetadata(PerFunctionMIParsingState &PFS,
                                   StringRef Src, SMRange SourceRange,
                                   SMDiagnostic &Error);

} // end namespace llvm

#endif // LLVM_CODEGEN_MIRPARSER_MIPARSER_H
