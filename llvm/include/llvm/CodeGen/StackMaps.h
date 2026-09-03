//===- StackMaps.h - StackMaps ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_STACKMAPS_H
#define LLVM_CODEGEN_STACKMAPS_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

namespace llvm {

class AsmPrinter;
class MCSymbol;
class MCExpr;
class MCStreamer;
class raw_ostream;
class TargetRegisterInfo;

/// MI-level stackmap operands.
///
/// MI stackmap operations take the form:
/// <id>, <numBytes>, live args...
class StackMapOpers {
public:
  /// Enumerate the meta operands.
  enum MetaOperandPos {
    IDPos,      ///< Stackmap ID operand index.
    NBytesPos,  ///< Patchable-byte-count operand index.
  };

private:
  const MachineInstr* MI;

public:
  /// Construct an accessor for the operands of stackmap instruction \p MI.
  ///
  /// \param MI Stackmap machine instruction to inspect.
  LLVM_ABI explicit StackMapOpers(const MachineInstr *MI);

  /// Return the ID for the given stackmap.
  ///
  /// \return Stackmap identifier from the ID operand.
  uint64_t getID() const { return MI->getOperand(IDPos).getImm(); }

  /// Return the number of patchable bytes the given stackmap should emit.
  ///
  /// \return Number of patchable bytes to emit.
  uint32_t getNumPatchBytes() const {
    return MI->getOperand(NBytesPos).getImm();
  }

  /// Get the operand index of the variable list of non-argument operands.
  /// These hold the "live state".
  ///
  /// \return Operand index of the first live-state operand.
  unsigned getVarIdx() const {
    // Skip ID, nShadowBytes.
    return 2;
  }
};

/// MI-level patchpoint operands.
///
/// MI patchpoint operations take the form:
/// [<def>], <id>, <numBytes>, <target>, <numArgs>, <cc>, ...
///
/// IR patchpoint intrinsics do not have the <cc> operand because calling
/// convention is part of the subclass data.
///
/// SD patchpoint nodes do not have a def operand because it is part of the
/// SDValue.
///
/// Patchpoints following the anyregcc convention are handled specially. For
/// these, the stack map also records the location of the return value and
/// arguments.
class PatchPointOpers {
public:
  /// Enumerate the meta operands.
  enum {
    IDPos, ///< Patchpoint ID operand index.
    /// Operand index of the patchable byte count meta operand.
    NBytesPos,
    TargetPos,
    NArgPos, ///< Number of call arguments operand position in a patchpoint.
    CCPos, ///< Calling convention operand position in a patchpoint.
    MetaEnd ///< Sentinel past the last meta operand index.
  };

private:
  const MachineInstr *MI;
  bool HasDef;

  unsigned getMetaIdx(unsigned Pos = 0) const {
    assert(Pos < MetaEnd && "Meta operand index out of range.");
    return (HasDef ? 1 : 0) + Pos;
  }

  const MachineOperand &getMetaOper(unsigned Pos) const {
    return MI->getOperand(getMetaIdx(Pos));
  }

public:
  /// Construct an accessor for the operands of patchpoint instruction \p MI.
  ///
  /// \param MI Patchpoint machine instruction to inspect.
  LLVM_ABI explicit PatchPointOpers(const MachineInstr *MI);

  /// Return true if this patchpoint uses the \c anyregcc calling convention.
  ///
  /// \return True if the calling convention is \c anyregcc.
  bool isAnyReg() const { return (getCallingConv() == CallingConv::AnyReg); }
  /// Return true if this patchpoint defines a result register.
  ///
  /// \return True if this patchpoint has a defined result register.
  bool hasDef() const { return HasDef; }

  /// Return the ID for the given patchpoint.
  ///
  /// \return Patchpoint identifier from the ID operand.
  uint64_t getID() const { return getMetaOper(IDPos).getImm(); }

  /// Return the number of patchable bytes the given patchpoint should emit.
  ///
  /// \return Number of patchable bytes to emit.
  uint32_t getNumPatchBytes() const {
    return getMetaOper(NBytesPos).getImm();
  }

  /// Returns the target of the underlying call.
  ///
  /// \return Call-target machine operand.
  const MachineOperand &getCallTarget() const {
    return getMetaOper(TargetPos);
  }

  /// Returns the calling convention.
  ///
  /// \return Calling convention of the underlying call.
  CallingConv::ID getCallingConv() const {
    return getMetaOper(CCPos).getImm();
  }

  /// Return the operand index of the first call argument.
  ///
  /// \return Operand index of the first call argument.
  unsigned getArgIdx() const { return getMetaIdx() + MetaEnd; }

  /// Return the number of call arguments.
  ///
  /// \return Number of call arguments recorded in the patchpoint.
  uint32_t getNumCallArgs() const {
    return MI->getOperand(getMetaIdx(NArgPos)).getImm();
  }

  /// Get the operand index of the variable list of non-argument operands.
  /// These hold the "live state".
  ///
  /// \return Operand index of the first live-state operand.
  unsigned getVarIdx() const {
    return getMetaIdx() + MetaEnd + getNumCallArgs();
  }

  /// Get the index at which stack map locations will be recorded.
  /// Arguments are not recorded unless the anyregcc convention is used.
  ///
  /// \return Operand index where stack map location recording begins.
  unsigned getStackMapStartIdx() const {
    if (isAnyReg())
      return getArgIdx();
    return getVarIdx();
  }

  /// Get the next scratch register operand index.
  ///
  /// \param StartIdx Operand index to begin searching from.
  /// \return Operand index of the next scratch register.
  LLVM_ABI unsigned getNextScratchIdx(unsigned StartIdx = 0) const;
};

/// MI-level Statepoint operands
///
/// Statepoint operands take the form:
///   <id>, <num patch bytes >, <num call arguments>, <call target>,
///   [call arguments...],
///   <StackMaps::ConstantOp>, <calling convention>,
///   <StackMaps::ConstantOp>, <statepoint flags>,
///   <StackMaps::ConstantOp>, <num deopt args>, [deopt args...],
///   <StackMaps::ConstantOp>, <num gc pointer args>, [gc pointer args...],
///   <StackMaps::ConstantOp>, <num gc allocas>, [gc allocas args...],
///   <StackMaps::ConstantOp>, <num  entries in gc map>, [base/derived pairs]
///   base/derived pairs in gc map are logical indices into <gc pointer args>
///   section.
///   All gc pointers assigned to VRegs produce new value (in form of MI Def
///   operand) and are tied to it.
class StatepointOpers {
  // TODO:: we should change the STATEPOINT representation so that CC and
  // Flags should be part of meta operands, with args and deopt operands, and
  // gc operands all prefixed by their length and a type code. This would be
  // much more consistent.

  // These values are absolute offsets into the operands of the statepoint
  // instruction.
  enum { IDPos, NBytesPos, NCallArgsPos, CallTargetPos, MetaEnd };

  // These values are relative offsets from the start of the statepoint meta
  // arguments (i.e. the end of the call arguments).
  enum { CCOffset = 1, FlagsOffset = 3, NumDeoptOperandsOffset = 5 };

public:
  /// Construct an accessor for the operands of statepoint instruction \p MI.
  ///
  /// \param MI Statepoint machine instruction to inspect.
  explicit StatepointOpers(const MachineInstr *MI) : MI(MI) {
    NumDefs = MI->getNumDefs();
  }

  /// Get index of statepoint ID operand.
  ///
  /// \return Operand index of the statepoint ID.
  unsigned getIDPos() const { return NumDefs + IDPos; }

  /// Get index of Num Patch Bytes operand.
  ///
  /// \return Operand index of the patchable-byte count.
  unsigned getNBytesPos() const { return NumDefs + NBytesPos; }

  /// Get index of Num Call Arguments operand.
  ///
  /// \return Operand index of the call-argument count.
  unsigned getNCallArgsPos() const { return NumDefs + NCallArgsPos; }

  /// Get starting index of non call related arguments
  /// (calling convention, statepoint flags, vm state and gc state).
  ///
  /// \return Operand index of the first non-call-related meta argument.
  unsigned getVarIdx() const {
    return MI->getOperand(NumDefs + NCallArgsPos).getImm() + MetaEnd + NumDefs;
  }

  /// Get index of Calling Convention operand.
  ///
  /// \return Operand index of the calling convention.
  unsigned getCCIdx() const { return getVarIdx() + CCOffset; }

  /// Get index of Flags operand.
  ///
  /// \return Operand index of the statepoint flags.
  unsigned getFlagsIdx() const { return getVarIdx() + FlagsOffset; }

  /// Get index of Number Deopt Arguments operand.
  ///
  /// \return Operand index of the deoptimization-argument count.
  unsigned getNumDeoptArgsIdx() const {
    return getVarIdx() + NumDeoptOperandsOffset;
  }

  /// Return the ID for the given statepoint.
  ///
  /// \return Statepoint identifier from the ID operand.
  uint64_t getID() const { return MI->getOperand(NumDefs + IDPos).getImm(); }

  /// Return the number of patchable bytes the given statepoint should emit.
  ///
  /// \return Number of patchable bytes to emit.
  uint32_t getNumPatchBytes() const {
    return MI->getOperand(NumDefs + NBytesPos).getImm();
  }

  /// Return the target of the underlying call.
  ///
  /// \return Call-target machine operand.
  const MachineOperand &getCallTarget() const {
    return MI->getOperand(NumDefs + CallTargetPos);
  }

  /// Return the calling convention.
  ///
  /// \return Calling convention of the underlying call.
  CallingConv::ID getCallingConv() const {
    return MI->getOperand(getCCIdx()).getImm();
  }

  /// Return the statepoint flags.
  ///
  /// \return Statepoint flags immediate value.
  uint64_t getFlags() const { return MI->getOperand(getFlagsIdx()).getImm(); }

  /// Return the number of deoptimization arguments.
  ///
  /// \return Number of deoptimization arguments.
  uint64_t getNumDeoptArgs() const {
    return MI->getOperand(getNumDeoptArgsIdx()).getImm();
  }

  /// Get index of number of gc map entries.
  ///
  /// \return Operand index of the GC map entry count.
  LLVM_ABI unsigned getNumGcMapEntriesIdx();

  /// Get index of number of gc allocas.
  ///
  /// \return Operand index of the GC alloca count.
  LLVM_ABI unsigned getNumAllocaIdx();

  /// Get index of number of GC pointers.
  ///
  /// \return Operand index of the GC pointer count.
  LLVM_ABI unsigned getNumGCPtrIdx();

  /// Get index of first GC pointer operand of -1 if there are none.
  ///
  /// \return Operand index of the first GC pointer, or \c -1 if none.
  LLVM_ABI int getFirstGCPtrIdx();

  /// Get vector of base/derived pairs from statepoint.
  /// Elements are indices into GC Pointer operand list (logical).
  ///
  /// \param GCMap Output vector of base/derived pointer index pairs.
  /// \return Number of elements written to \p GCMap.
  LLVM_ABI unsigned
  getGCPointerMap(SmallVectorImpl<std::pair<unsigned, unsigned>> &GCMap);

  /// Return true if Reg is used only in operands which can be folded to
  /// stack usage.
  ///
  /// \param Reg Register to test for foldability.
  /// \return True if \p Reg appears only in foldable operands.
  LLVM_ABI bool isFoldableReg(Register Reg) const;

  /// Return true if Reg is used only in operands of MI which can be folded to
  /// stack usage and MI is a statepoint instruction.
  ///
  /// \param MI Machine instruction that must be a statepoint.
  /// \param Reg Register to test for foldability.
  /// \return True if \p MI is a statepoint and \p Reg is only used foldably.
  LLVM_ABI static bool isFoldableReg(const MachineInstr *MI, Register Reg);

private:
  const MachineInstr *MI;
  unsigned NumDefs;
};

/// Collects stackmap, patchpoint, and statepoint records and serializes them
/// into the object file's stack map section via AsmPrinter.
class StackMaps {
public:
  /// Encodes a single stack map location (register, memory, or constant).
  struct Location {
    /// Kind of value encoded in this stack map location.
    enum LocationType : uint16_t {
      Unprocessed,   ///< Location has not been classified yet.
      Register,      ///< Value lives in a register.
      Direct,        ///< Direct memory reference: register + offset.
      Indirect,      ///< Indirect memory reference through register + offset.
      Constant,      ///< Immediate constant value.
      ConstantIndex  ///< Index into the stack map constant pool.
    };
    /// Classification of this location.
    LocationType Type = Unprocessed;
    /// Size in bytes of the recorded value.
    uint16_t Size = 0;
    /// Register number associated with this location, if any.
    uint16_t Reg = 0;
    /// Offset or constant payload for this location.
    int32_t Offset = 0;

    /// Construct an empty, unprocessed location.
    Location() = default;
    /// Construct a stack map location with explicit type, size, register, and
    /// offset fields.
    ///
    /// \param Type Location classification.
    /// \param Size Size in bytes of the recorded value.
    /// \param Reg Register number for register or memory locations.
    /// \param Offset Offset or constant payload for this location.
    Location(LocationType Type, uint16_t Size, uint16_t Reg, int32_t Offset)
        : Type(Type), Size(Size), Reg(Reg), Offset(Offset) {}
  };

  /// Live-out register recorded in a stack map call site.
  struct LiveOutReg {
    /// Machine register number for this live-out.
    uint16_t Reg = 0;
    /// DWARF register number corresponding to \p Reg.
    uint16_t DwarfRegNum = 0;
    /// Size in bytes of the live-out register value.
    uint16_t Size = 0;

    /// Construct an empty live-out register record.
    LiveOutReg() = default;
    /// Construct a live-out register record.
    ///
    /// \param Reg Machine register number.
    /// \param DwarfRegNum DWARF register number for \p Reg.
    /// \param Size Size in bytes of the register value.
    LiveOutReg(uint16_t Reg, uint16_t DwarfRegNum, uint16_t Size)
        : Reg(Reg), DwarfRegNum(DwarfRegNum), Size(Size) {}
  };

  /// Operand-type markers used by the stack map operand parser.
  ///
  /// OpTypes encode information about the following logical operand (which may
  /// consist of several MachineOperands) for the OpParser.
  using OpType = enum {
    /// Direct memory reference encoded as register plus constant offset.
    DirectMemRefOp,
    /// Indirect memory reference encoded as size, register, and offset.
    IndirectMemRefOp,
    /// Constant integer operand in the stack map record.
    ConstantOp
  };

  /// Construct a stack maps collector bound to AsmPrinter \p AP.
  ///
  /// \param AP AsmPrinter used to emit the stack map section.
  LLVM_ABI StackMaps(AsmPrinter &AP);

  /// Get index of next meta operand.
  /// Similar to parseOperand, but does not actually parses operand meaning.
  ///
  /// \param MI Instruction whose operands are being walked.
  /// \param CurIdx Current meta-operand index.
  /// \return Index of the next meta operand after \p CurIdx.
  LLVM_ABI static unsigned getNextMetaArgIdx(const MachineInstr *MI,
                                             unsigned CurIdx);

  /// Clear all recorded call sites, constants, and function info.
  void reset() {
    CSInfos.clear();
    ConstPool.clear();
    FnInfos.clear();
  }

  /// Vector of stack map locations for a call site.
  using LocationVec = SmallVector<Location, 8>;
  /// Recorded live-out register locations for a stack map call site.
  using LiveOutVec = SmallVector<LiveOutReg, 8>;
  /// Constant pool mapping large constants to dense indices.
  using ConstantPool = MapVector<int64_t, int64_t>;

  /// Per-function stack size and stack map record count.
  struct FunctionInfo {
    /// Frame size in bytes for the function.
    uint64_t StackSize = 0;
    /// Number of stack map records associated with the function.
    uint64_t RecordCount = 1;

    /// Construct function info with a zero stack size.
    FunctionInfo() = default;
    /// Construct function info for a function with the given stack size.
    ///
    /// \param StackSize Frame size in bytes.
    explicit FunctionInfo(uint64_t StackSize) : StackSize(StackSize) {}
  };

  /// Recorded stackmap/patchpoint callsite offset, ID, locations, and live-outs.
  struct CallsiteInfo {
    /// Expression for the call site's offset from the function entry.
    const MCExpr *CSOffsetExpr = nullptr;
    /// Stack map or patchpoint identifier for this call site.
    uint64_t ID = 0;
    /// Recorded operand locations at this call site.
    LocationVec Locations;
    /// Live-out registers preserved at this call site.
    LiveOutVec LiveOuts;

    /// Construct an empty call site info record.
    CallsiteInfo() = default;
    /// Construct a recorded stackmap/patchpoint call site.
    ///
    /// \param CSOffsetExpr Call site offset expression from function entry.
    /// \param ID Stack map or patchpoint identifier.
    /// \param Locations Operand locations recorded for this call site.
    /// \param LiveOuts Live-out registers recorded for this call site.
    CallsiteInfo(const MCExpr *CSOffsetExpr, uint64_t ID,
                 LocationVec &&Locations, LiveOutVec &&LiveOuts)
        : CSOffsetExpr(CSOffsetExpr), ID(ID), Locations(std::move(Locations)),
          LiveOuts(std::move(LiveOuts)) {}
  };

  /// Map from function symbols to recorded stackmap function info.
  using FnInfoMap = MapVector<const MCSymbol *, FunctionInfo>;
  /// Ordered list of recorded stackmap/patchpoint call sites.
  using CallsiteInfoList = std::vector<CallsiteInfo>;

  /// Generate a stackmap record for a stackmap instruction.
  ///
  /// MI must be a raw STACKMAP, not a PATCHPOINT.
  ///
  /// \param L Label marking the stack map call site.
  /// \param MI STACKMAP machine instruction to record.
  LLVM_ABI void recordStackMap(const MCSymbol &L, const MachineInstr &MI);

  /// Generate a stackmap record for a patchpoint instruction.
  ///
  /// \param L Label marking the patchpoint call site.
  /// \param MI PATCHPOINT machine instruction to record.
  LLVM_ABI void recordPatchPoint(const MCSymbol &L, const MachineInstr &MI);

  /// Generate a stackmap record for a statepoint instruction.
  ///
  /// \param L Label marking the statepoint call site.
  /// \param MI STATEPOINT machine instruction to record.
  LLVM_ABI void recordStatepoint(const MCSymbol &L, const MachineInstr &MI);

  /// If there is any stack map data, create a stack map section and serialize
  /// the map info into it. This clears the stack map data structures
  /// afterwards.
  LLVM_ABI void serializeToStackMapSection();

  /// Get call site info.
  ///
  /// \return Mutable list of recorded call site infos.
  CallsiteInfoList &getCSInfos() { return CSInfos; }

  /// Get function info.
  ///
  /// \return Mutable map of function symbols to stack map function info.
  FnInfoMap &getFnInfos() { return FnInfos; }

private:
  static const char *WSMP;

  AsmPrinter &AP;
  CallsiteInfoList CSInfos;
  ConstantPool ConstPool;
  FnInfoMap FnInfos;

  MachineInstr::const_mop_iterator
  parseOperand(MachineInstr::const_mop_iterator MOI,
               MachineInstr::const_mop_iterator MOE, LocationVec &Locs,
               LiveOutVec &LiveOuts);

  /// Specialized parser of statepoint operands.
  /// They do not directly correspond to StackMap record entries.
  void parseStatepointOpers(const MachineInstr &MI,
                            MachineInstr::const_mop_iterator MOI,
                            MachineInstr::const_mop_iterator MOE,
                            LocationVec &Locations, LiveOutVec &LiveOuts);

  /// Create a live-out register record for the given register @p Reg.
  LiveOutReg createLiveOutReg(unsigned Reg,
                              const TargetRegisterInfo *TRI) const;

  /// Parse the register live-out mask and return a vector of live-out
  /// registers that need to be recorded in the stackmap.
  LiveOutVec parseRegisterLiveOutMask(const uint32_t *Mask) const;

  /// Record the locations of the operands of the provided instruction in a
  /// record keyed by the provided label.  For instructions w/AnyReg calling
  /// convention the return register is also recorded if requested.  For
  /// STACKMAP, and PATCHPOINT the label is expected to immediately *preceed*
  /// lowering of the MI to MCInsts.  For STATEPOINT, it expected to
  /// immediately *follow*.  It's not clear this difference was intentional,
  /// but it exists today.  
  void recordStackMapOpers(const MCSymbol &L,
                           const MachineInstr &MI, uint64_t ID,
                           MachineInstr::const_mop_iterator MOI,
                           MachineInstr::const_mop_iterator MOE,
                           bool recordResult = false);

  /// Emit the stackmap header.
  void emitStackmapHeader(MCStreamer &OS);

  /// Emit the function frame record for each function.
  void emitFunctionFrameRecords(MCStreamer &OS);

  /// Emit the constant pool.
  void emitConstantPoolEntries(MCStreamer &OS);

  /// Emit the callsite info for each stackmap/patchpoint intrinsic call.
  void emitCallsiteEntries(MCStreamer &OS);

  LLVM_ABI void print(raw_ostream &OS);
  void debug() { print(dbgs()); }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_STACKMAPS_H
