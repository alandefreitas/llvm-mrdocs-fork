//===- llvm/CodeGen/MachineFunction.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Collect native machine code for a function.  This class contains a list of
// MachineBasicBlock instances that make up the current compiled function.
//
// This class also contains pointers to various classes which hold
// target-specific information about the generated code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEFUNCTION_H
#define LLVM_CODEGEN_MACHINEFUNCTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/iterator.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/ArrayRecycler.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Recycler.h"
#include "llvm/Support/UniqueBBID.h"
#include "llvm/Target/TargetOptions.h"
#include <bitset>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace llvm {

class BasicBlock;
class BlockAddress;
class DataLayout;
class DebugLoc;
struct DenormalMode;
class DIExpression;
class DILocalVariable;
class DILocation;
class Function;
class GISelChangeObserver;
class GlobalValue;
class TargetMachine;
class MachineConstantPool;
class MachineFrameInfo;
class MachineFunction;
class MachineJumpTableInfo;
class MachineRegisterInfo;
class MCContext;
class MCInstrDesc;
class MCSymbol;
class MCSection;
class Pass;
class PseudoSourceValueManager;
class raw_ostream;
class SlotIndexes;
class StringRef;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
/// Generic base class for all target-specific subtarget information.
class TargetSubtargetInfo;
struct WinEHFuncInfo;

/// Allocation traits for MachineBasicBlock nodes in an ilist.
template <> struct ilist_alloc_traits<MachineBasicBlock> {
  /// Delete machine basic block node \p MBB.
  /// @param MBB Basic block to destroy.
  LLVM_ABI void deleteNode(MachineBasicBlock *MBB);
};

/// Callback traits notifying a MachineFunction when MBBs enter or leave it.
template <> struct ilist_callback_traits<MachineBasicBlock> {
  /// Notify that basic block \p N was added to the function's block list.
  /// @param N Basic block that was inserted.
  LLVM_ABI void addNodeToList(MachineBasicBlock *N);
  /// Notify that basic block \p N was removed from the function's block list.
  /// @param N Basic block that was removed.
  LLVM_ABI void removeNodeFromList(MachineBasicBlock *N);

  /// Transfer nodes between lists; MBBs may not move across functions.
  /// @param OldList Source list traits (must be this list).
  /// @param First Begin of the transferred range (unused).
  /// @param Last End of the transferred range (unused).
  template <class Iterator>
  void transferNodesFromList(ilist_callback_traits &OldList, Iterator First,
                             Iterator Last) {
    assert(this == &OldList && "never transfer MBBs between functions");
  }
};

/// Hotness of MachineFunction-owned static data not represented as a global.
///
/// Typical examples are MachineJumpTableInfo and MachineConstantPool.
enum class MachineFunctionDataHotness {
  /// Hotness has not been determined.
  Unknown,
  /// Data is expected to be cold.
  Cold,
  /// Data is expected to be hot.
  Hot,
};

/// Target-specific per-function information attached to a MachineFunction.
///
/// Derived by targets to hold private data for each MachineFunction. Objects
/// are accessed/created with MF::getInfo and destroyed when the
/// MachineFunction is destroyed.
struct LLVM_ABI MachineFunctionInfo {
  /// Destroy target-specific machine function info.
  virtual ~MachineFunctionInfo();

  /// Factory function: default behavior is to call new using the
  /// supplied allocator.
  ///
  /// This function can be overridden in a derive class.
  /// @param Allocator Bump allocator that owns the created info object.
  /// @param F IR function associated with the machine function.
  /// @param STI Subtarget used to construct target info.
  /// @return Newly allocated target-specific MachineFunctionInfo of type FuncInfoTy.
  template <typename FuncInfoTy, typename SubtargetTy = TargetSubtargetInfo>
  static FuncInfoTy *create(BumpPtrAllocator &Allocator, const Function &F,
                            const SubtargetTy *STI) {
    return new (Allocator.Allocate<FuncInfoTy>()) FuncInfoTy(F, STI);
  }

  /// Allocate a copy of existing MachineFunctionInfo \p MFI.
  /// @param Allocator Bump allocator that owns the created info object.
  /// @param MFI Source info object to copy-construct.
  /// @return Newly allocated copy of \p MFI owned by \p Allocator.
  template <typename Ty>
  static Ty *create(BumpPtrAllocator &Allocator, const Ty &MFI) {
    return new (Allocator.Allocate<Ty>()) Ty(MFI);
  }

  /// Clone this info into \p DestMF with remapped basic blocks.
  ///
  /// Remaps MachineBasicBlock references from the original parent to values in
  /// the new function. Targets may assume that virtual register and frame index
  /// values are preserved in the new function.
  /// @param Allocator Allocator used for the cloned info.
  /// @param DestMF Destination machine function receiving the clone.
  /// @param Src2DstMBB Mapping from original blocks to blocks in \p DestMF.
  /// @return Cloned MachineFunctionInfo, or null in the base implementation.
  virtual MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const {
    return nullptr;
  }
};

/// Bitset of MachineFunction invariants checked by the MachineVerifier.
///
/// Each property has checking code in the MachineVerifier, and passes can
/// require that a property be set.
class MachineFunctionProperties {
  // Possible TODO: Allow targets to extend this (perhaps by allowing the
  // constructor to specify the size of the bit vector)
  // Possible TODO: Allow requiring the negative (e.g. VRegsAllocated could be
  // stated as the negative of "has vregs"

public:
  // The properties are stated in "positive" form; i.e. a pass could require
  // that the property hold, but not that it does not hold.

  /// Properties a MachineFunction may hold at a given point in the pipeline.
  enum class Property : unsigned {
    /// Function is in SSA form; each virtual register has a single def.
    IsSSA,
    /// Function contains no PHI instructions.
    NoPHIs,
    /// Register liveness in live-ins and operands is tracked accurately.
    TracksLiveness,
    /// Function uses no virtual registers.
    NoVRegs,
    /// Instruction selection failed; the function should be reset or aborted.
    FailedISel,
    /// GlobalISel MachineLegalizer has legalized pre-isel generic instructions.
    Legalized,
    /// GlobalISel RegBankSelect has assigned register banks to generic vregs.
    RegBankSelected,
    /// GlobalISel InstructionSelect has eliminated pre-isel generic instructions.
    Selected,
    /// Two-address tied-def operands have been rewritten for constraints.
    TiedOpsRewritten,
    /// Set when the function is not expected to pass machine verification.
    FailsVerification,
    /// Set when register allocation failed and an error was already reported.
    FailedRegAlloc,
    /// Debug user values must only reference defined virtual registers.
    TracksDebugUserValues,
    /// Sentinel equal to the last real property; used to size the property bitset.
    LastProperty = TracksDebugUserValues,
  };

  /// Return true if property \p P is set.
  /// @param P Property to query.
  /// @return True if \p P is set in this property bitset.
  bool hasProperty(Property P) const {
    return Properties[static_cast<unsigned>(P)];
  }

  /// Set property \p P and return this.
  /// @param P Property to set.
  /// @return This property set for chaining.
  MachineFunctionProperties &set(Property P) {
    Properties.set(static_cast<unsigned>(P));
    return *this;
  }

  /// Clear property \p P and return this.
  /// @param P Property to clear.
  /// @return This property set for chaining.
  MachineFunctionProperties &reset(Property P) {
    Properties.reset(static_cast<unsigned>(P));
    return *this;
  }

  /// Return true if the IsSSA property is set.
  /// @return True if the function is in SSA form.
  bool hasIsSSA() const { return hasProperty(Property::IsSSA); }
  /// Set the IsSSA property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setIsSSA(void) { return set(Property::IsSSA); }
  /// Clear the IsSSA property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetIsSSA(void) { return reset(Property::IsSSA); }
  /// Return true if the NoPHIs property is set (the machine function is known
  /// to contain no PHI instructions).
  /// @return True if the machine function is known to contain no PHI instructions.
  bool hasNoPHIs() const { return hasProperty(Property::NoPHIs); }
  /// Set the NoPHIs property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setNoPHIs(void) { return set(Property::NoPHIs); }
  /// Clear the NoPHIs property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetNoPHIs(void) {
    return reset(Property::NoPHIs);
  }
  /// Return true if the TracksLiveness property is set.
  /// @return True if register liveness is tracked accurately.
  bool hasTracksLiveness() const {
    return hasProperty(Property::TracksLiveness);
  }
  /// Set the TracksLiveness property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setTracksLiveness(void) {
    return set(Property::TracksLiveness);
  }
  /// Clear the TracksLiveness property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetTracksLiveness(void) {
    return reset(Property::TracksLiveness);
  }
  /// Return true if the NoVRegs property is set (the machine function uses no
  /// virtual registers).
  /// @return True if the machine function uses no virtual registers.
  bool hasNoVRegs() const { return hasProperty(Property::NoVRegs); }
  /// Set the NoVRegs property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setNoVRegs(void) { return set(Property::NoVRegs); }
  /// Clear the NoVRegs property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetNoVRegs(void) {
    return reset(Property::NoVRegs);
  }
  /// Return true if the FailedISel property is set (instruction selection
  /// failed and the machine function should be reset or aborted).
  /// @return True if instruction selection failed.
  bool hasFailedISel() const { return hasProperty(Property::FailedISel); }
  /// Set the FailedISel property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setFailedISel(void) {
    return set(Property::FailedISel);
  }
  /// Clear the FailedISel property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetFailedISel(void) {
    return reset(Property::FailedISel);
  }
  /// Return true if the Legalized property is set (GlobalISel MachineLegalizer
  /// has run and pre-isel generic instructions are legalized).
  /// @return True if pre-isel generic instructions have been legalized.
  bool hasLegalized() const { return hasProperty(Property::Legalized); }
  /// Set the Legalized property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setLegalized(void) {
    return set(Property::Legalized);
  }
  /// Clear the Legalized property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetLegalized(void) {
    return reset(Property::Legalized);
  }
  /// Return true if the RegBankSelected property is set.
  /// @return True if register banks have been assigned to generic vregs.
  bool hasRegBankSelected() const {
    return hasProperty(Property::RegBankSelected);
  }
  /// Set the RegBankSelected property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setRegBankSelected(void) {
    return set(Property::RegBankSelected);
  }
  /// Clear the RegBankSelected property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetRegBankSelected(void) {
    return reset(Property::RegBankSelected);
  }
  /// Return true if the Selected property is set.
  /// @return True if InstructionSelect has eliminated pre-isel generic instructions.
  bool hasSelected() const { return hasProperty(Property::Selected); }
  /// Set the Selected property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setSelected(void) {
    return set(Property::Selected);
  }
  /// Clear the Selected property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetSelected(void) {
    return reset(Property::Selected);
  }
  /// Return true if the TiedOpsRewritten property is set.
  /// @return True if two-address tied-def operands have been rewritten.
  bool hasTiedOpsRewritten() const {
    return hasProperty(Property::TiedOpsRewritten);
  }
  /// Set the TiedOpsRewritten property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setTiedOpsRewritten(void) {
    return set(Property::TiedOpsRewritten);
  }
  /// Clear the TiedOpsRewritten property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetTiedOpsRewritten(void) {
    return reset(Property::TiedOpsRewritten);
  }
  /// Return true if the FailsVerification property is set.
  /// @return True if the function is not expected to pass machine verification.
  bool hasFailsVerification() const {
    return hasProperty(Property::FailsVerification);
  }
  /// Set the FailsVerification property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setFailsVerification(void) {
    return set(Property::FailsVerification);
  }
  /// Clear the FailsVerification property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetFailsVerification(void) {
    return reset(Property::FailsVerification);
  }
  /// Return true if the FailedRegAlloc property is set.
  /// @return True if register allocation failed and an error was already reported.
  bool hasFailedRegAlloc() const {
    return hasProperty(Property::FailedRegAlloc);
  }
  /// Set the FailedRegAlloc property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setFailedRegAlloc(void) {
    return set(Property::FailedRegAlloc);
  }
  /// Clear the FailedRegAlloc property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetFailedRegAlloc(void) {
    return reset(Property::FailedRegAlloc);
  }
  /// Return true if the TracksDebugUserValues property is set.
  /// @return True if debug user values must only reference defined virtual registers.
  bool hasTracksDebugUserValues() const {
    return hasProperty(Property::TracksDebugUserValues);
  }
  /// Set the TracksDebugUserValues property.
  /// @return This property set for chaining.
  MachineFunctionProperties &setTracksDebugUserValues(void) {
    return set(Property::TracksDebugUserValues);
  }
  /// Clear the TracksDebugUserValues property.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetTracksDebugUserValues(void) {
    return reset(Property::TracksDebugUserValues);
  }

  /// Reset all the properties.
  /// @return This property set for chaining.
  MachineFunctionProperties &reset() {
    Properties.reset();
    return *this;
  }

  /// Reset all properties and re-establish baseline invariants.
  /// @return This property set for chaining.
  MachineFunctionProperties &resetToInitial() {
    reset();
    setIsSSA();
    setTracksLiveness();
    return *this;
  }

  /// Set all properties that are set in \p MFP.
  /// @param MFP Property set whose bits are unioned into this.
  /// @return This property set for chaining.
  MachineFunctionProperties &set(const MachineFunctionProperties &MFP) {
    Properties |= MFP.Properties;
    return *this;
  }

  /// Clear all properties that are set in \p MFP.
  /// @param MFP Property set whose bits are cleared from this.
  /// @return This property set for chaining.
  MachineFunctionProperties &reset(const MachineFunctionProperties &MFP) {
    Properties &= ~MFP.Properties;
    return *this;
  }

  /// Return true if every property required by \p V is set in this.
  /// @param V Required property set (typically from a pass).
  /// @return True if every property required by \p V is set in this.
  bool verifyRequiredProperties(const MachineFunctionProperties &V) const {
    return (Properties | ~V.Properties).all();
  }

  /// Print the MachineFunctionProperties in human-readable form.
  /// @param OS Output stream to print to.
  LLVM_ABI void print(raw_ostream &OS) const;

private:
  std::bitset<static_cast<unsigned>(Property::LastProperty) + 1> Properties;
};

/// Windows SEH handler metadata recorded for a landing pad.
struct SEHHandler {
  /// Filter or finally function. Null indicates a catch-all.
  const Function *FilterOrFinally;

  /// Address of block to recover at. Null for a finally handler.
  const BlockAddress *RecoverBA;
};

/// Landing-pad metadata retained for the current machine function.
struct LandingPadInfo {
  /// Landing pad basic block.
  MachineBasicBlock *LandingPadBlock;
  /// Labels immediately prior to associated invoke calls.
  SmallVector<MCSymbol *, 1> BeginLabels;
  /// Labels immediately after associated invoke calls.
  SmallVector<MCSymbol *, 1> EndLabels;
  /// SEH handlers active at this landing pad.
  SmallVector<SEHHandler, 1> SEHHandlers;
  /// Label emitted at the beginning of the landing pad block.
  MCSymbol *LandingPadLabel = nullptr;
  /// Type ids for this landing pad (filters are encoded as negatives).
  std::vector<int> TypeIds;

  /// Construct landing-pad info for block \p MBB.
  /// @param MBB Landing-pad machine basic block.
  explicit LandingPadInfo(MachineBasicBlock *MBB)
      : LandingPadBlock(MBB) {}
};

/// Representation of machine code for a single LLVM IR function.
class LLVM_ABI MachineFunction {
  Function &F;
  const TargetMachine &Target;
  const TargetSubtargetInfo &STI;
  MCContext &Ctx;

  // RegInfo - Information about each register in use in the function.
  MachineRegisterInfo *RegInfo;

  // Used to keep track of target-specific per-machine-function information for
  // the target implementation.
  MachineFunctionInfo *MFInfo;

  // Keep track of objects allocated on the stack.
  MachineFrameInfo *FrameInfo;

  // Keep track of constants which are spilled to memory
  MachineConstantPool *ConstantPool;

  // Keep track of jump tables for switch instructions
  MachineJumpTableInfo *JumpTableInfo;

  // Keep track of the function section.
  MCSection *Section = nullptr;

  // Keeps track of Windows exception handling related data. This will be null
  // for functions that aren't using a funclet-based EH personality.
  WinEHFuncInfo *WinEHInfo = nullptr;

  // Function-level unique numbering for MachineBasicBlocks.  When a
  // MachineBasicBlock is inserted into a MachineFunction is it automatically
  // numbered and this vector keeps track of the mapping from ID's to MBB's.
  std::vector<MachineBasicBlock*> MBBNumbering;

  // Analysis number epoch, currently never changed as we don't renumber the
  // block numbers used for analyses.
  unsigned AnalysisNumberingEpoch = 0;

  // Next MBB analysis number.
  unsigned NextAnalysisNumber = 0;

  // Pool-allocate MachineFunction-lifetime and IR objects.
  BumpPtrAllocator Allocator;

  // Allocation management for instructions in function.
  Recycler<MachineInstr> InstructionRecycler;

  // Allocation management for operand arrays on instructions.
  ArrayRecycler<MachineOperand> OperandRecycler;

  // Allocation management for basic blocks in function.
  Recycler<MachineBasicBlock> BasicBlockRecycler;

  // List of machine basic blocks in function
  using BasicBlockListType = ilist<MachineBasicBlock>;
  BasicBlockListType BasicBlocks;

  /// FunctionNumber - This provides a unique ID for each function emitted in
  /// this translation unit.
  ///
  unsigned FunctionNumber;

  /// Alignment - The alignment of the function.
  Align Alignment;

  /// ExposesReturnsTwice - True if the function calls setjmp or related
  /// functions with attribute "returns twice", but doesn't have
  /// the attribute itself.
  /// This is used to limit optimizations which cannot reason
  /// about the control flow of such functions.
  bool ExposesReturnsTwice = false;

  /// True if the function includes any inline assembly.
  bool HasInlineAsm = false;

  /// True if any WinCFI instruction have been emitted in this function.
  bool HasWinCFI = false;

  /// Current high-level properties of the IR of the function (e.g. is in SSA
  /// form or whether registers have been allocated)
  MachineFunctionProperties Properties;

  // Allocation management for pseudo source values.
  std::unique_ptr<PseudoSourceValueManager> PSVManager;

  /// List of moves done by a function's prolog.  Used to construct frame maps
  /// by debug and exception handling consumers.
  std::vector<MCCFIInstruction> FrameInstructions;

  /// List of basic blocks immediately following calls to _setjmp. Used to
  /// construct a table of valid longjmp targets for Windows Control Flow Guard.
  std::vector<MCSymbol *> LongjmpTargets;

  /// List of basic blocks that are the targets for Windows EH Continuation
  /// Guard.
  std::vector<MCSymbol *> EHContTargets;

  /// \name Exception Handling
  /// \{

  /// List of LandingPadInfo describing the landing pad information.
  std::vector<LandingPadInfo> LandingPads;

  /// Map a landing pad's EH symbol to the call site indexes.
  DenseMap<MCSymbol*, SmallVector<unsigned, 4>> LPadToCallSiteMap;

  /// Map a landing pad to its index.
  DenseMap<const MachineBasicBlock *, unsigned> WasmLPadToIndexMap;

  /// Map of invoke call site index values to associated begin EH_LABEL.
  DenseMap<MCSymbol*, unsigned> CallSiteMap;

  /// CodeView label annotations.
  std::vector<std::pair<MCSymbol *, MDNode *>> CodeViewAnnotations;

  bool CallsEHReturn = false;
  bool CallsUnwindInit = false;
  bool HasEHContTarget = false;
  bool HasEHScopes = false;
  bool HasEHFunclets = false;
  bool HasFakeUses = false;
  bool IsOutlined = false;

  /// BBID to assign to the next basic block of this function.
  unsigned NextBBID = 0;

  /// Section Type for basic blocks, only relevant with basic block sections.
  BasicBlockSection BBSectionsType = BasicBlockSection::None;

  /// Prefetch targets in this function. This includes targets that are mapped
  /// to a basic block and dangling targets.
  DenseMap<UniqueBBID, SmallVector<unsigned>> PrefetchTargets;

  /// List of C++ TypeInfo used.
  std::vector<const GlobalValue *> TypeInfos;

  /// List of typeids encoding filters used.
  std::vector<unsigned> FilterIds;

  /// List of the indices in FilterIds corresponding to filter terminators.
  std::vector<unsigned> FilterEnds;

  EHPersonality PersonalityTypeCache = EHPersonality::Unknown;

  /// \}

  /// Clear all the members of this MachineFunction, but the ones used to
  /// initialize again the MachineFunction.  More specifically, this deallocates
  /// all the dynamically allocated objects and get rids of all the XXXInfo data
  /// structure, but keeps unchanged the references to Fn, Target, and
  /// FunctionNumber.
  void clear();
  /// Allocate and initialize the different members.
  /// In particular, the XXXInfo data structure.
  /// \pre Fn, Target, and FunctionNumber are properly set.
  void init();

public:
  /// Fixed location of a variable that does not change during execution.
  ///
  /// The Address may be:
  /// * A stack index, which can be negative for fixed stack objects.
  /// * A MCRegister, whose entry value contains the address of the variable.
  class VariableDbgInfo {
    std::variant<int, MCRegister> Address;

  public:
    /// Debug variable being described.
    const DILocalVariable *Var;
    /// DIExpression applied to the variable location.
    const DIExpression *Expr;
    /// Source location associated with this debug info.
    const DILocation *Loc;

    /// Construct debug info for a variable located in stack slot \p Slot.
    /// @param Var Debug variable being described.
    /// @param Expr DIExpression applied to the variable location.
    /// @param Slot Stack slot index holding the variable.
    /// @param Loc Source location associated with this debug info.
    VariableDbgInfo(const DILocalVariable *Var, const DIExpression *Expr,
                    int Slot, const DILocation *Loc)
        : Address(Slot), Var(Var), Expr(Expr), Loc(Loc) {}

    /// Construct debug info for a variable in the entry value of \p EntryValReg.
    /// @param Var Debug variable being described.
    /// @param Expr DIExpression applied to the variable location.
    /// @param EntryValReg Register whose entry value holds the variable.
    /// @param Loc Source location associated with this debug info.
    VariableDbgInfo(const DILocalVariable *Var, const DIExpression *Expr,
                    MCRegister EntryValReg, const DILocation *Loc)
        : Address(EntryValReg), Var(Var), Expr(Expr), Loc(Loc) {}

    /// Return true if this variable is in a stack slot.
    /// @return True if the variable location is a stack slot.
    bool inStackSlot() const { return std::holds_alternative<int>(Address); }

    /// Return true if this variable is in the entry value of a register.
    /// @return True if the variable location is an entry-value register.
    bool inEntryValueRegister() const {
      return std::holds_alternative<MCRegister>(Address);
    }

    /// Returns the stack slot of this variable, assuming `inStackSlot()` is
    /// true.
    /// @return Stack slot index holding this variable.
    int getStackSlot() const { return std::get<int>(Address); }

    /// Returns the MCRegister of this variable, assuming
    /// `inEntryValueRegister()` is true.
    /// @return Register whose entry value holds this variable.
    MCRegister getEntryValueRegister() const {
      return std::get<MCRegister>(Address);
    }

    /// Updates the stack slot of this variable, assuming `inStackSlot()` is
    /// true.
    /// @param NewSlot Replacement stack slot index.
    void updateStackSlot(int NewSlot) {
      assert(inStackSlot());
      Address = NewSlot;
    }
  };

  /// Callback interface notified when instructions are inserted or removed.
  class LLVM_ABI Delegate {
    virtual void anchor();

  public:
    /// Destroy the delegate.
    virtual ~Delegate() = default;
    /// Callback after an insertion. This should not modify the MI directly.
    /// @param MI Instruction that was inserted.
    virtual void MF_HandleInsertion(MachineInstr &MI) = 0;
    /// Callback before a removal. This should not modify the MI directly.
    /// @param MI Instruction about to be removed.
    virtual void MF_HandleRemoval(MachineInstr &MI) = 0;
    /// Callback before changing MCInstrDesc. This should not modify the MI
    /// directly.
    /// @param MI Instruction whose descriptor is changing.
    /// @param TID New instruction descriptor.
    virtual void MF_HandleChangeDesc(MachineInstr &MI, const MCInstrDesc &TID) {
    }
  };

  /// Argument number paired with the register used to pass it after lowering.
  ///
  /// For now only single-register argument transfers are supported.
  struct ArgRegPair {
    /// Register used to transfer the argument.
    Register Reg;
    /// Zero-based argument number after call lowering.
    uint16_t ArgNo;
    /// Construct a pair for argument \p Arg in register \p R.
    /// @param R Register carrying the argument.
    /// @param Arg Argument number after call lowering.
    ArgRegPair(Register R, unsigned Arg) : Reg(R), ArgNo(Arg) {
      assert(Arg < (1 << 16) && "Arg out of range");
    }
  };

  /// Extra call-site metadata for argument forwarding and call-graph types.
  struct CallSiteInfo {
    /// Vector of call argument and its forwarding register.
    SmallVector<ArgRegPair, 1> ArgRegPairs;
    /// Callee type ids.
    SmallVector<ConstantInt *, 4> CalleeTypeIds;

    /// 'call_target' metadata for the DISubprogram. It is the declaration
    /// or definition of the target function and might be indirect.
    MDNode *CallTarget = nullptr;

    /// Construct empty call-site info.
    CallSiteInfo() = default;

    /// Build call-site info from IR call \p CB.
    ///
    /// Extracts the numeric type id from the CallBase's callee_type Metadata,
    /// and sets CalleeTypeIds. This is used as type id for the indirect call in
    /// the call graph section. Also extracts the MDNode from the CallBase's
    /// call_target Metadata for debug-info call-site entries.
    /// @param CB IR call providing type and call-target metadata.
    LLVM_ABI CallSiteInfo(const CallBase &CB);
  };

  /// Global callee and target flags for a call instruction.
  struct CalledGlobalInfo {
    /// Global value being called, if any.
    const GlobalValue *Callee;
    /// Target-specific flags associated with the call.
    unsigned TargetFlags;
  };

  /// Map from call instructions to their additional call-site info.
  using CallSiteInfoMap = DenseMap<const MachineInstr *, CallSiteInfo>;

private:
  Delegate *TheDelegate = nullptr;
  GISelChangeObserver *Observer = nullptr;

  /// Map a call instruction to call site arguments forwarding info.
  CallSiteInfoMap CallSitesInfo;

  /// A helper function that returns call site info for a give call
  /// instruction if debug entry value support is enabled.
  CallSiteInfoMap::iterator getCallSiteInfo(const MachineInstr *MI);

  using CalledGlobalsMap = DenseMap<const MachineInstr *, CalledGlobalInfo>;
  /// Mapping of call instruction to the global value and target flags that it
  /// calls, if applicable.
  CalledGlobalsMap CalledGlobalsInfo;

  // Callbacks for insertion and removal.
  void handleInsertion(MachineInstr &MI);
  void handleRemoval(MachineInstr &MI);
  friend struct ilist_traits<MachineInstr>;

public:
  // Need to be accessed from MachineInstr::setDesc.
  /// Notify delegates that \p MI is changing its MCInstrDesc to \p TID.
  /// @param MI Instruction whose descriptor is changing.
  /// @param TID New instruction descriptor.
  void handleChangeDesc(MachineInstr &MI, const MCInstrDesc &TID);

  /// Collection type for unchanging variable debug locations.
  using VariableDbgInfoMapTy = SmallVector<VariableDbgInfo, 4>;
  /// Debug locations for variables with a fixed address during execution.
  VariableDbgInfoMapTy VariableDbgInfos;

  /// A count of how many instructions in the function have had numbers
  /// assigned to them. Used for debug value tracking, to determine the
  /// next instruction number.
  unsigned DebugInstrNumberingCount = 0;

  /// Set value of DebugInstrNumberingCount field. Avoid using this unless
  /// you're deserializing this data.
  /// @param Num New instruction-numbering count.
  void setDebugInstrNumberingCount(unsigned Num);

  /// Pair of instruction number and operand number.
  using DebugInstrOperandPair = std::pair<unsigned, unsigned>;

  /// Remaps a debug instruction-reference from one def to another.
  ///
  /// Made up of a source instruction/operand pair, destination pair, and a
  /// qualifying subregister indicating which bits of the operand make up the
  /// substitution. For example, a debug user of %1:
  ///    %0:gr32 = someinst, debug-instr-number 1
  ///    %1:gr16 = %0.some_16_bit_subreg, debug-instr-number 2
  /// Would receive the substitution {{2, 0}, {1, 0}, $subreg}, where $subreg is
  /// the subregister number for some_16_bit_subreg.
  class DebugSubstitution {
  public:
    DebugInstrOperandPair Src;  ///< Source instruction / operand pair.
    DebugInstrOperandPair Dest; ///< Replacement instruction / operand pair.
    unsigned Subreg;            ///< Qualifier for which part of Dest is read.

    /// Construct a substitution from \p Src to \p Dest, qualified by \p Subreg.
    /// @param Src Source instruction/operand pair.
    /// @param Dest Replacement instruction/operand pair.
    /// @param Subreg Subregister qualifier on \p Dest.
    DebugSubstitution(const DebugInstrOperandPair &Src,
                      const DebugInstrOperandPair &Dest, unsigned Subreg)
        : Src(Src), Dest(Dest), Subreg(Subreg) {}

    /// Order only by source instruction / operand pair: there should never
    /// be duplicate entries for the same source in any collection.
    /// @param Other Other substitution to compare against.
    /// @return True if this source pair is ordered before \p Other.
    bool operator<(const DebugSubstitution &Other) const {
      return Src < Other.Src;
    }
  };

  /// Substitutions recording where tracked debug values were redefined.
  ///
  /// A collection of DebugSubstitution objects. For example, when one
  /// instruction is substituted for another. Keeping a record allows recovery
  /// of variable locations after compilation finishes.
  SmallVector<DebugSubstitution, 8> DebugValueSubstitutions;

  /// PHI location that is also a debug-info variable value during regalloc.
  ///
  /// Loaded by PHI-elimination and emitted as DBG_PHI during VirtRegRewriter,
  /// with maintenance by intermediate passes that edit registers (coalescing
  /// and the allocator).
  class DebugPHIRegallocPos {
  public:
    MachineBasicBlock *MBB; ///< Block where this PHI was originally located.
    Register Reg;           ///< VReg where the control-flow-merge happens.
    unsigned SubReg;        ///< Optional subreg qualifier within Reg.
    /// Construct a PHI debug position in \p MBB for \p Reg.
    /// @param MBB Block where the PHI originally lived.
    /// @param Reg Virtual register at the control-flow merge.
    /// @param SubReg Optional subregister qualifier within \p Reg.
    DebugPHIRegallocPos(MachineBasicBlock *MBB, Register Reg, unsigned SubReg)
        : MBB(MBB), Reg(Reg), SubReg(SubReg) {}
  };

  /// Map of debug instruction numbers to the position of their PHI instructions
  /// during register allocation. See DebugPHIRegallocPos.
  DenseMap<unsigned, DebugPHIRegallocPos> DebugPHIPositions;

  /// Flag for whether this function contains DBG_VALUEs (false) or
  /// DBG_INSTR_REF (true).
  bool UseDebugInstrRef = false;

  /// Create a substitution from one instruction/operand value to another.
  /// @param Src Source instruction/operand pair being replaced.
  /// @param Dest Replacement instruction/operand pair.
  /// @param SubReg Optional subregister qualifier on \p Dest.
  void makeDebugValueSubstitution(DebugInstrOperandPair Src,
                                  DebugInstrOperandPair Dest,
                                  unsigned SubReg = 0);

  /// Retarget tracked debug values from \p Old to equivalent defs in \p New.
  ///
  /// Needed when recreating an instruction during optimization that has the
  /// same signature (def operands in the same place) but a modified opcode,
  /// flags, or similar. Example: X86 moves transformed into equivalent LEAs.
  /// If the opcodes differ, limit examined operands to the first
  /// \p MaxOperand.
  /// @param Old Original instruction whose debug values are remapped.
  /// @param New Replacement instruction receiving the substitutions.
  /// @param MaxOperand Max operand index to consider when opcodes differ.
  void substituteDebugValuesForInst(const MachineInstr &Old, MachineInstr &New,
                                    unsigned MaxOperand = UINT_MAX);

  /// Find the underlying defining instruction/operand for a COPY in SSA form.
  ///
  /// Copies move values between registers rather than defining them. Labelling
  /// a COPY-like instruction with an instruction number makes value numbers
  /// non-unique later, so this follows COPY-like chains to the non-COPY
  /// definition, or creates a DBG_PHI on entry for parameters.
  /// May insert instructions into the entry block!
  /// \returns An instruction/operand pair identifying the defining value.
  /// @param MI Copy-like instruction to salvage.
  /// @param DbgPHICache Cache of already-solved COPY results.
  DebugInstrOperandPair
  salvageCopySSA(MachineInstr &MI,
                 DenseMap<Register, DebugInstrOperandPair> &DbgPHICache);

  /// Implementation helper for \c salvageCopySSA.
  /// @param MI Copy-like instruction to salvage.
  /// @return Instruction/operand pair identifying the defining value.
  DebugInstrOperandPair salvageCopySSAImpl(MachineInstr &MI);

  /// Finalize partially emitted DBG_INSTR_REF debug instructions.
  ///
  /// These are DBG_INSTR_REF instructions that knew only the vreg, not the
  /// defining instruction. After isel, each should point at an instruction or
  /// DBG_PHI.
  void finalizeDebugInstrRefs();

  /// Determine whether, in the current machine configuration, we should use
  /// instruction referencing or not.
  /// @return True if instruction referencing should be used for debug values.
  bool shouldUseDebugInstrRef() const;

  /// Returns true if the function's variable locations are tracked with
  /// instruction referencing.
  /// @return True if variable locations use instruction referencing.
  bool useDebugInstrRef() const;

  /// Set whether this function will use instruction referencing or not.
  /// @param UseInstrRef True to track variable locations with instruction refs.
  void setUseDebugInstrRef(bool UseInstrRef);

  /// A reserved operand number representing the instructions memory operand,
  /// for instructions that have a stack spill fused into them.
  const static unsigned int DebugOperandMemNumber;

  /// Construct a MachineFunction for IR function \p F.
  /// @param F IR function being lowered.
  /// @param Target Target machine for code generation.
  /// @param STI Subtarget for this function.
  /// @param Ctx MC context for symbols and sections.
  /// @param FunctionNum Unique function number within the translation unit.
  MachineFunction(Function &F, const TargetMachine &Target,
                  const TargetSubtargetInfo &STI, MCContext &Ctx,
                  unsigned FunctionNum);
  /// MachineFunctions are not copyable.
  /// @param Other Unused; copy construction is deleted.
  MachineFunction(const MachineFunction &Other) = delete;
  /// MachineFunctions are not assignable.
  /// @param Other Unused; copy assignment is deleted.
  MachineFunction &operator=(const MachineFunction &Other) = delete;
  /// Destroy the MachineFunction and its owned machine IR.
  ~MachineFunction();

  /// Reset the instance as if it was just created.
  void reset() {
    clear();
    init();
  }

  /// Reset the currently registered delegate - otherwise assert.
  /// @param delegate Delegate that must match the currently installed one.
  void resetDelegate(Delegate *delegate) {
    assert(TheDelegate == delegate &&
           "Only the current delegate can perform reset!");
    TheDelegate = nullptr;
  }

  /// Set the delegate. resetDelegate must be called before attempting
  /// to set.
  /// @param delegate Non-null delegate to install.
  void setDelegate(Delegate *delegate) {
    assert(delegate && !TheDelegate &&
           "Attempted to set delegate to null, or to change it without "
           "first resetting it!");

    TheDelegate = delegate;
  }

  /// Install GlobalISel change observer \p O.
  /// @param O Observer to notify of instruction changes, or null.
  void setObserver(GISelChangeObserver *O) { Observer = O; }

  /// Return the currently installed GlobalISel change observer.
  /// @return The installed GISelChangeObserver, or null if none.
  GISelChangeObserver *getObserver() const { return Observer; }

  /// Return the MCContext used by this machine function.
  /// @return MC context for symbols and sections in this function.
  MCContext &getContext() const { return Ctx; }

  /// Returns the Section this function belongs to.
  /// @return Section this function belongs to, or null if unset.
  MCSection *getSection() const { return Section; }

  /// Indicates the Section this function belongs to.
  /// @param S Section this function should belong to.
  void setSection(MCSection *S) { Section = S; }

  /// Return the PseudoSourceValueManager for this function.
  /// @return PseudoSourceValueManager owned by this machine function.
  PseudoSourceValueManager &getPSVManager() const { return *PSVManager; }

  /// Return the DataLayout attached to the Module associated to this MF.
  /// @return Data layout of the Module containing this function.
  const DataLayout &getDataLayout() const;

  /// Return the LLVM function that this machine code represents
  /// @return Mutable IR Function lowered by this MachineFunction.
  Function &getFunction() { return F; }

  /// Return the LLVM function that this machine code represents
  /// @return Const IR Function lowered by this MachineFunction.
  const Function &getFunction() const { return F; }

  /// getName - Return the name of the corresponding LLVM function.
  /// @return Name of the corresponding LLVM function.
  StringRef getName() const;

  /// getFunctionNumber - Return a unique ID for the current function.
  /// @return Unique function number within the translation unit.
  unsigned getFunctionNumber() const { return FunctionNumber; }

  /// Returns true if this function has basic block sections enabled.
  /// @return True if basic-block sections are enabled for this function.
  bool hasBBSections() const {
    return (BBSectionsType == BasicBlockSection::All ||
            BBSectionsType == BasicBlockSection::List ||
            BBSectionsType == BasicBlockSection::Preset);
  }

  /// Set the basic-block sections policy for this function.
  /// @param V Basic-block section kind to use.
  void setBBSectionsType(BasicBlockSection V) { BBSectionsType = V; }

  /// Set the map of basic-block IDs to prefetch callsite indexes.
  /// @param V Prefetch target map to install.
  void
  setPrefetchTargets(const DenseMap<UniqueBBID, SmallVector<unsigned>> &V) {
    PrefetchTargets = V;
  }

  /// Return the map of basic-block IDs to prefetch callsite indexes.
  /// @return Map from basic-block IDs to prefetch callsite indexes.
  const DenseMap<UniqueBBID, SmallVector<unsigned>> &
  getPrefetchTargets() const {
    return PrefetchTargets;
  }

  /// Assign IsBeginSection IsEndSection fields for basic blocks in this
  /// function.
  void assignBeginEndSections();

  /// getTarget - Return the target machine this machine code is compiled with
  /// @return Target machine used to compile this function.
  const TargetMachine &getTarget() const { return Target; }

  /// getSubtarget - Return the subtarget for which this machine code is being
  /// compiled.
  /// @return Subtarget for which this machine code is being compiled.
  const TargetSubtargetInfo &getSubtarget() const { return STI; }

  /// Return this function's subtarget cast to \c STC.
  ///
  /// In debug builds, verifies that the object being returned is of the correct
  /// type.
  /// @return Subtarget cast to \c STC.
  template<typename STC> const STC &getSubtarget() const {
    return static_cast<const STC &>(STI);
  }

  /// Return information about the registers currently in use.
  /// @return Mutable register info for this function.
  MachineRegisterInfo &getRegInfo() { return *RegInfo; }
  /// Return const information about the registers currently in use.
  /// @return Const register info for this function.
  const MachineRegisterInfo &getRegInfo() const { return *RegInfo; }

  /// Return the mutable frame info for stack objects in this function.
  ///
  /// Describes objects allocated on the stack frame in an abstract way.
  /// @return Mutable frame info describing stack objects.
  MachineFrameInfo &getFrameInfo() { return *FrameInfo; }
  /// Return the const frame info for stack objects in this function.
  /// @return Const frame info describing stack objects.
  const MachineFrameInfo &getFrameInfo() const { return *FrameInfo; }

  /// Return the jump-table info for this function, or null if none exists.
  /// @return Const jump-table info, or null if none exists.
  const MachineJumpTableInfo *getJumpTableInfo() const { return JumpTableInfo; }
  /// Return the mutable jump-table info for this function, or null if none.
  /// @return Mutable jump-table info, or null if none exists.
  MachineJumpTableInfo *getJumpTableInfo() { return JumpTableInfo; }

  /// Return existing jump-table info, or allocate one for \p JTEntryKind.
  /// @param JTEntryKind Kind of jump-table entries to create if allocating.
  /// @return Jump-table info for this function (existing or newly created).
  MachineJumpTableInfo *getOrCreateJumpTableInfo(unsigned JTEntryKind);

  /// getConstantPool - Return the constant pool object for the current
  /// function.
  /// @return Mutable constant pool for this function.
  MachineConstantPool *getConstantPool() { return ConstantPool; }
  /// Return the constant pool object for the current function.
  /// @return Const constant pool for this function.
  const MachineConstantPool *getConstantPool() const { return ConstantPool; }

  /// Return Windows EH funclet info, or null if this function has none.
  /// @return Const Windows EH funclet info, or null if none.
  const WinEHFuncInfo *getWinEHFuncInfo() const { return WinEHInfo; }
  /// Return mutable Windows EH funclet info, or null if this function has none.
  /// @return Mutable Windows EH funclet info, or null if none.
  WinEHFuncInfo *getWinEHFuncInfo() { return WinEHInfo; }

  /// getAlignment - Return the alignment of the function.
  /// @return Alignment of the function.
  Align getAlignment() const { return Alignment; }

  /// Set the alignment of the function.
  /// @param A Requested function alignment.
  void setAlignment(Align A) { Alignment = A; }

  /// Ensure the function is at least \p A bytes aligned.
  /// @param A Minimum alignment to enforce.
  void ensureAlignment(Align A) {
    if (Alignment < A)
      Alignment = A;
  }

  /// Returns the preferred alignment which comes from the function attributes
  /// (optsize, minsize, prefalign) and TargetLowering.
  /// @return Preferred function alignment from attributes and TargetLowering.
  Align getPreferredAlignment() const;

  /// Return true if this function calls a "returns twice" function.
  ///
  /// True when the function calls setjmp or similar without itself having the
  /// "returns twice" attribute.
  /// @return True if a returns-twice call is exposed without the attribute.
  bool exposesReturnsTwice() const {
    return ExposesReturnsTwice;
  }

  /// Set whether this function exposes a call to a "returns twice" function.
  /// @param B True if a returns-twice call is exposed.
  void setExposesReturnsTwice(bool B) {
    ExposesReturnsTwice = B;
  }

  /// Returns true if the function contains any inline assembly.
  /// @return True if the function contains any inline assembly.
  bool hasInlineAsm() const {
    return HasInlineAsm;
  }

  /// Set a flag that indicates that the function contains inline assembly.
  /// @param B True if the function contains inline assembly.
  void setHasInlineAsm(bool B) {
    HasInlineAsm = B;
  }

  /// Returns true if the function contains Windows CFI instructions.
  /// @return True if Windows CFI instructions have been emitted.
  bool hasWinCFI() const {
    return HasWinCFI;
  }
  /// Set whether any Windows CFI instructions have been emitted in this
  /// function.
  /// @param V True if Windows CFI instructions have been emitted.
  void setHasWinCFI(bool V) { HasWinCFI = V; }

  /// True if this function needs frame moves for debug or exceptions.
  /// @return True if frame moves are needed for debug or exceptions.
  bool needsFrameMoves() const;

  /// Return the const machine-function property set.
  /// @return Const machine-function property bitset.
  const MachineFunctionProperties &getProperties() const { return Properties; }
  /// Return the mutable machine-function property set.
  /// @return Mutable machine-function property bitset.
  MachineFunctionProperties &getProperties() { return Properties; }

  /// Return the target-specific MachineFunctionInfo cast to \c Ty.
  /// @return Mutable target-specific MachineFunctionInfo cast to \c Ty.
  template<typename Ty>
  Ty *getInfo() {
    return static_cast<Ty*>(MFInfo);
  }

  /// Return the const target-specific MachineFunctionInfo cast to \c Ty.
  /// @return Const target-specific MachineFunctionInfo cast to \c Ty.
  template<typename Ty>
  const Ty *getInfo() const {
    return static_cast<const Ty *>(MFInfo);
  }

  /// Clone target-specific info \p Old into this function and return it.
  /// @param Old Source MachineFunctionInfo to clone.
  /// @return Cloned target-specific MachineFunctionInfo cast to \c Ty.
  template <typename Ty> Ty *cloneInfo(const Ty &Old) {
    assert(!MFInfo);
    MFInfo = Ty::template create<Ty>(Allocator, Old);
    return static_cast<Ty *>(MFInfo);
  }

  /// Initialize the target-specific MachineFunctionInfo for \p STI.
  /// @param STI Subtarget used to create target function info.
  void initTargetMachineFunctionInfo(const TargetSubtargetInfo &STI);

  /// Clone MachineFunctionInfo from \p OrigMF, remapping blocks via \p Src2DstMBB.
  /// @param OrigMF Source machine function whose info is cloned.
  /// @param Src2DstMBB Mapping from original blocks to blocks in this function.
  /// @return Cloned MachineFunctionInfo for this function, or null if none.
  MachineFunctionInfo *cloneInfoFrom(
      const MachineFunction &OrigMF,
      const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB);

  /// Returns the denormal handling type for the default rounding mode of the
  /// function.
  /// @param FPType Floating-point type whose denormal mode is queried.
  /// @return Denormal handling mode for \p FPType in this function.
  DenormalMode getDenormalMode(const fltSemantics &FPType) const;

  /// Return the MachineBasicBlock with block number \p N.
  ///
  /// MachineBasicBlocks are numbered when inserted into the machine function.
  /// \c MBB::getNumber is the forward map; this is the inverse.
  /// @param N Block number to look up.
  /// @return MachineBasicBlock with number \p N.
  MachineBasicBlock *getBlockNumbered(unsigned N) const {
    assert(N < MBBNumbering.size() && "Illegal block number");
    assert(MBBNumbering[N] && "Block was removed from the machine function!");
    return MBBNumbering[N];
  }

  /// Should we be emitting segmented stack stuff for the function
  /// @return True if segmented stack prologue/epilogue should be emitted.
  bool shouldSplitStack() const;

  /// getNumBlockIDs - Return the number of MBB ID's allocated.
  /// @return Number of MachineBasicBlock IDs allocated so far.
  unsigned getNumBlockIDs() const { return (unsigned)MBBNumbering.size(); }

  /// Return the numbering "epoch" of analysis block numbers.
  /// @return Analysis-numbering epoch for this function.
  unsigned getAnalysisBlockNumberEpoch() const {
    return AnalysisNumberingEpoch;
  }

  /// Assign and return the next analysis block number.
  /// @return Newly assigned analysis block number.
  unsigned assignAnalysisNumber() { return NextAnalysisNumber++; }

  /// Return one past the highest analysis block number assigned so far.
  /// @return One past the highest analysis block number assigned so far.
  unsigned getMaxAnalysisBlockNumber() const { return NextAnalysisNumber; }

  /// Discard and recompute MachineBasicBlock numbers for this function.
  ///
  /// Guarantees that MBB numbers are sequential, dense, and match block order.
  /// If \p MBBFrom is specified, only that block and those after it are
  /// renumbered.
  /// @param MBBFrom Optional first block to renumber from; null renumbers all.
  void RenumberBlocks(MachineBasicBlock *MBBFrom = nullptr);

  /// Return an estimate of the function's code size,
  /// taking into account block and function alignment
  /// @return Estimated function size in bytes.
  int64_t estimateFunctionSizeInBytes();

  /// Print this MachineFunction to \p OS for debugging.
  /// @param OS Output stream to print to.
  /// @param Indexes Optional slot indexes to include in the dump.
  void print(raw_ostream &OS, const SlotIndexes *Indexes = nullptr) const;

  /// Display this function's CFG in a debugger graph viewer.
  ///
  /// Meant for use from the debugger: call \c F->viewCFG() and a ghostview
  /// window should pop up displaying the CFG with basic-block contents. Depends
  /// on \c dot and \c gv being on your path.
  void viewCFG() const;

  /// Display this function's CFG labels without basic-block contents.
  ///
  /// Works like \c viewCFG, but nodes show only labels. Useful when you only
  /// care about the CFG shape and want a smaller graph.
  void viewCFGOnly() const;

  /// dump - Print the current MachineFunction to cerr, useful for debugger use.
  void dump() const;

  /// Run this MachineFunction through the machine code verifier.
  ///
  /// Useful for debugger use.
  /// \returns true if no problems were found.
  /// @param P Optional legacy pass providing analysis info.
  /// @param Banner Optional banner printed with diagnostics.
  /// @param OS Optional stream for verifier output.
  /// @param AbortOnError If true, abort when verification fails.
  bool verify(Pass *P = nullptr, const char *Banner = nullptr,
              raw_ostream *OS = nullptr, bool AbortOnError = true) const;

  /// Run this MachineFunction through the verifier (new pass manager).
  ///
  /// Useful for debugger use.
  /// \returns true if no problems were found.
  /// @param MFAM Analysis manager providing verifier analyses.
  /// @param Banner Optional banner printed with diagnostics.
  /// @param OS Optional stream for verifier output.
  /// @param AbortOnError If true, abort when verification fails.
  bool verify(MachineFunctionAnalysisManager &MFAM,
              const char *Banner = nullptr, raw_ostream *OS = nullptr,
              bool AbortOnError = true) const;

  /// Run this MachineFunction through the verifier with live intervals.
  ///
  /// Useful for debugger use.
  /// TODO: Add the param for LiveStacks analysis.
  /// \returns true if no problems were found.
  /// @param LiveInts Optional live-interval analysis.
  /// @param Indexes Optional slot-index analysis.
  /// @param Banner Optional banner printed with diagnostics.
  /// @param OS Optional stream for verifier output.
  /// @param AbortOnError If true, abort when verification fails.
  bool verify(LiveIntervals *LiveInts, SlotIndexes *Indexes,
              const char *Banner = nullptr, raw_ostream *OS = nullptr,
              bool AbortOnError = true) const;

  // Provide accessors for the MachineBasicBlock list...
  /// Iterator over the machine basic blocks in this function.
  using iterator = BasicBlockListType::iterator;
  /// Const iterator over the machine basic blocks in this function.
  using const_iterator = BasicBlockListType::const_iterator;
  /// Const reverse iterator over the machine basic block list.
  using const_reverse_iterator = BasicBlockListType::const_reverse_iterator;
  /// Reverse iterator over the machine basic blocks in this function.
  using reverse_iterator = BasicBlockListType::reverse_iterator;

  /// Return the pointer-to-member used by MachineBasicBlock::getNextNode().
  /// @param MBB Unused; required by the ilist traits interface.
  /// @return Pointer-to-member for the BasicBlocks list.
  static BasicBlockListType MachineFunction::*
  getSublistAccess(MachineBasicBlock *MBB) {
    return &MachineFunction::BasicBlocks;
  }

  /// Add physical register \p PReg as a live-in and create a matching vreg.
  /// @param PReg Physical register to mark live-in.
  /// @param RC Register class for the created virtual register.
  /// @return Virtual register created for the live-in physical register.
  Register addLiveIn(MCRegister PReg, const TargetRegisterClass *RC);

  //===--------------------------------------------------------------------===//
  // BasicBlock accessor functions.
  //
  /// Return an iterator to the first MachineBasicBlock in this function.
  /// @return Iterator to the first MachineBasicBlock.
  iterator                 begin()       { return BasicBlocks.begin(); }
  /// Return a const iterator to the first MachineBasicBlock in this function.
  /// @return Const iterator to the first MachineBasicBlock.
  const_iterator           begin() const { return BasicBlocks.begin(); }
  /// Return an iterator past the last MachineBasicBlock in this function.
  /// @return Iterator past the last MachineBasicBlock.
  iterator                 end  ()       { return BasicBlocks.end();   }
  /// Return a const iterator past the last MachineBasicBlock in this function.
  /// @return Const iterator past the last MachineBasicBlock.
  const_iterator           end  () const { return BasicBlocks.end();   }

  /// Return a reverse iterator to the last MachineBasicBlock.
  /// @return Reverse iterator to the last MachineBasicBlock.
  reverse_iterator        rbegin()       { return BasicBlocks.rbegin(); }
  /// Return a const reverse iterator to the last MachineBasicBlock.
  /// @return Const reverse iterator to the last MachineBasicBlock.
  const_reverse_iterator  rbegin() const { return BasicBlocks.rbegin(); }
  /// Return a reverse iterator past the first MachineBasicBlock.
  /// @return Reverse iterator past the first MachineBasicBlock.
  reverse_iterator        rend  ()       { return BasicBlocks.rend();   }
  /// Return a const reverse iterator past the first MachineBasicBlock.
  /// @return Const reverse iterator past the first MachineBasicBlock.
  const_reverse_iterator  rend  () const { return BasicBlocks.rend();   }

  /// Return the number of machine basic blocks in this function.
  /// @return Number of machine basic blocks in this function.
  unsigned                  size() const { return (unsigned)BasicBlocks.size();}
  /// Return true if this function has no basic blocks.
  /// @return True if this function has no basic blocks.
  bool                     empty() const { return BasicBlocks.empty(); }
  /// Return a const reference to the first basic block in this function.
  /// @return Const reference to the first basic block.
  const MachineBasicBlock &front() const { return BasicBlocks.front(); }
  /// Return a mutable reference to the first basic block in this function.
  /// @return Mutable reference to the first basic block.
        MachineBasicBlock &front()       { return BasicBlocks.front(); }
  /// Return the last basic block in this function.
  /// @return Const reference to the last basic block.
  const MachineBasicBlock & back() const { return BasicBlocks.back(); }
  /// Return a mutable reference to the last basic block in this function.
  /// @return Mutable reference to the last basic block.
        MachineBasicBlock & back()       { return BasicBlocks.back(); }

  /// Append basic block \p MBB to the end of this function.
  /// @param MBB Basic block to append.
  void push_back (MachineBasicBlock *MBB) { BasicBlocks.push_back (MBB); }
  /// Insert basic block \p MBB at the beginning of this function.
  /// @param MBB Basic block to insert at the front.
  void push_front(MachineBasicBlock *MBB) { BasicBlocks.push_front(MBB); }
  /// Insert \p MBB into the block list immediately before \p MBBI.
  /// @param MBBI Insertion point in the block list.
  /// @param MBB Basic block to insert.
  void insert(iterator MBBI, MachineBasicBlock *MBB) {
    BasicBlocks.insert(MBBI, MBB);
  }
  /// Move the block at \p MBBI to immediately before \p InsertPt.
  /// @param InsertPt New position in this function's block list.
  /// @param MBBI Iterator to the block being moved.
  void splice(iterator InsertPt, iterator MBBI) {
    BasicBlocks.splice(InsertPt, BasicBlocks, MBBI);
  }
  /// Move basic block \p MBB to immediately before \p InsertPt.
  /// @param InsertPt New position in this function's block list.
  /// @param MBB Basic block being moved.
  void splice(iterator InsertPt, MachineBasicBlock *MBB) {
    BasicBlocks.splice(InsertPt, BasicBlocks, MBB);
  }
  /// Move blocks [\p MBBI, \p MBBE) to immediately before \p InsertPt.
  /// @param InsertPt New position in this function's block list.
  /// @param MBBI Begin of the block range being moved.
  /// @param MBBE End of the block range being moved.
  void splice(iterator InsertPt, iterator MBBI, iterator MBBE) {
    BasicBlocks.splice(InsertPt, BasicBlocks, MBBI, MBBE);
  }

  /// Remove the block at \p MBBI from this function without deleting it.
  /// @param MBBI Iterator to the block to remove.
  void remove(iterator MBBI) { BasicBlocks.remove(MBBI); }
  /// Remove basic block \p MBBI from this function without deleting it.
  /// @param MBBI Basic block to remove.
  void remove(MachineBasicBlock *MBBI) { BasicBlocks.remove(MBBI); }
  /// Erase and delete the block at iterator \p MBBI.
  /// @param MBBI Iterator to the block to erase.
  void erase(iterator MBBI) { BasicBlocks.erase(MBBI); }
  /// Erase and delete basic block \p MBBI.
  /// @param MBBI Basic block to erase.
  void erase(MachineBasicBlock *MBBI) { BasicBlocks.erase(MBBI); }

  /// Sort the basic blocks in this function using comparator \p comp.
  /// @param comp Comparison functor over MachineBasicBlock.
  template <typename Comp>
  void sort(Comp comp) {
    BasicBlocks.sort(comp);
  }

  /// Return the number of \p MachineInstrs in this \p MachineFunction.
  /// @return Total number of MachineInstrs across all basic blocks.
  unsigned getInstructionCount() const {
    unsigned InstrCount = 0;
    for (const MachineBasicBlock &MBB : BasicBlocks)
      InstrCount += MBB.size();
    return InstrCount;
  }

  //===--------------------------------------------------------------------===//
  // Internal functions used to automatically number MachineBasicBlocks

  /// Adds the MBB to the internal numbering. Returns the unique number
  /// assigned to the MBB.
  /// @param MBB Basic block to number.
  /// @return Unique number assigned to \p MBB.
  unsigned addToMBBNumbering(MachineBasicBlock *MBB) {
    MBBNumbering.push_back(MBB);
    return (unsigned)MBBNumbering.size()-1;
  }

  /// Remove basic-block number \p N from the internal numbering tracker.
  ///
  /// Only intended for use by the MachineBasicBlock implementation.
  /// @param N Block number to clear.
  void removeFromMBBNumbering(unsigned N) {
    assert(N < MBBNumbering.size() && "Illegal basic block #");
    MBBNumbering[N] = nullptr;
  }

  /// Allocate a new MachineInstr (prefer this over `new MachineInstr`).
  /// @param MCID Opcode / instruction descriptor.
  /// @param DL Debug location for the new instruction.
  /// @param NoImplicit If true, omit implicit operands from \p MCID.
  /// @return Newly allocated MachineInstr owned by this function.
  MachineInstr *CreateMachineInstr(const MCInstrDesc &MCID, DebugLoc DL,
                                   bool NoImplicit = false);

  /// Create a new MachineInstr which is a copy of \p Orig, identical in all
  /// ways except the instruction has no parent, prev, or next. Bundling flags
  /// are reset.
  ///
  /// Note: Clones a single instruction, not whole instruction bundles.
  /// Does not perform target specific adjustments; consider using
  /// TargetInstrInfo::duplicate() instead.
  /// @param Orig Instruction to clone.
  /// @return Newly allocated clone of \p Orig with no parent or neighbors.
  MachineInstr *CloneMachineInstr(const MachineInstr *Orig);

  /// Clones instruction or the whole instruction bundle \p Orig and insert
  /// into \p MBB before \p InsertBefore.
  ///
  /// Note: Does not perform target specific adjustments; consider using
  /// TargetInstrInfo::duplicate() instead.
  /// @param MBB Destination basic block.
  /// @param InsertBefore Insertion point within \p MBB.
  /// @param Orig Instruction or bundle to clone.
  /// @return Reference to the cloned instruction (or bundle head) in \p MBB.
  MachineInstr &
  cloneMachineInstrBundle(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator InsertBefore,
                          const MachineInstr &Orig);

  /// Delete the given MachineInstr.
  /// @param MI Instruction to destroy.
  void deleteMachineInstr(MachineInstr *MI);

  /// Allocate a new MachineBasicBlock (prefer this over `new`).
  ///
  /// Sets `MachineBasicBlock::BBID` if basic-block-sections is enabled for the
  /// function.
  /// @param BB Optional original IR basic block.
  /// @param BBID Optional unique basic-block section ID.
  /// @return Newly allocated MachineBasicBlock owned by this function.
  MachineBasicBlock *
  CreateMachineBasicBlock(const BasicBlock *BB = nullptr,
                          std::optional<UniqueBBID> BBID = std::nullopt);

  /// Delete the given MachineBasicBlock.
  /// @param MBB Basic block to destroy.
  void deleteMachineBasicBlock(MachineBasicBlock *MBB);

  /// Allocate a new MachineMemOperand owned by this function.
  ///
  /// MachineMemOperands need not be explicitly deallocated.
  /// @param PtrInfo Pointer information for the memory access.
  /// @param F Memory-operand flags.
  /// @param MemTy Access type describing the memory size.
  /// @param BaseAlignment Base alignment of the access.
  /// @param Metadata Optional MMO metadata bundle.
  /// @param SSID Atomic synchronization scope.
  /// @param Ordering Atomic success ordering.
  /// @param FailureOrdering Atomic failure ordering.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(
      MachinePointerInfo PtrInfo, MachineMemOperand::Flags F, LLT MemTy,
      Align BaseAlignment, const MMOMetadata &Metadata = MMOMetadata(),
      SyncScope::ID SSID = SyncScope::System,
      AtomicOrdering Ordering = AtomicOrdering::NotAtomic,
      AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic);
  /// Allocate a new MachineMemOperand with a LocationSize access size.
  /// @param PtrInfo Pointer information for the memory access.
  /// @param F Memory-operand flags.
  /// @param Size Access size.
  /// @param BaseAlignment Base alignment of the access.
  /// @param Metadata Optional MMO metadata bundle.
  /// @param SSID Atomic synchronization scope.
  /// @param Ordering Atomic success ordering.
  /// @param FailureOrdering Atomic failure ordering.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(
      MachinePointerInfo PtrInfo, MachineMemOperand::Flags F, LocationSize Size,
      Align BaseAlignment, const MMOMetadata &Metadata = MMOMetadata(),
      SyncScope::ID SSID = SyncScope::System,
      AtomicOrdering Ordering = AtomicOrdering::NotAtomic,
      AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic);
  /// Allocate a new MachineMemOperand with a precise byte size.
  /// @param PtrInfo Pointer information for the memory access.
  /// @param F Memory-operand flags.
  /// @param Size Access size in bytes.
  /// @param BaseAlignment Base alignment of the access.
  /// @param Metadata Optional MMO metadata bundle.
  /// @param SSID Atomic synchronization scope.
  /// @param Ordering Atomic success ordering.
  /// @param FailureOrdering Atomic failure ordering.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(
      MachinePointerInfo PtrInfo, MachineMemOperand::Flags F, uint64_t Size,
      Align BaseAlignment, const MMOMetadata &Metadata = MMOMetadata(),
      SyncScope::ID SSID = SyncScope::System,
      AtomicOrdering Ordering = AtomicOrdering::NotAtomic,
      AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic) {
    return getMachineMemOperand(PtrInfo, F, LocationSize::precise(Size),
                                BaseAlignment, Metadata, SSID, Ordering,
                                FailureOrdering);
  }
  /// Allocate a new MachineMemOperand with a TypeSize access size.
  /// @param PtrInfo Pointer information for the memory access.
  /// @param F Memory-operand flags.
  /// @param Size Access size.
  /// @param BaseAlignment Base alignment of the access.
  /// @param Metadata Optional MMO metadata bundle.
  /// @param SSID Atomic synchronization scope.
  /// @param Ordering Atomic success ordering.
  /// @param FailureOrdering Atomic failure ordering.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(
      MachinePointerInfo PtrInfo, MachineMemOperand::Flags F, TypeSize Size,
      Align BaseAlignment, const MMOMetadata &Metadata = MMOMetadata(),
      SyncScope::ID SSID = SyncScope::System,
      AtomicOrdering Ordering = AtomicOrdering::NotAtomic,
      AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic) {
    return getMachineMemOperand(PtrInfo, F, LocationSize::precise(Size),
                                BaseAlignment, Metadata, SSID, Ordering,
                                FailureOrdering);
  }

  /// Allocate a MachineMemOperand copy of \p MMO adjusted by offset and size.
  ///
  /// MachineMemOperands are owned by the MachineFunction and need not be
  /// explicitly deallocated.
  /// @param MMO Existing memory operand to copy.
  /// @param Offset Byte offset applied to the copied operand.
  /// @param Ty Replacement access type/size.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          int64_t Offset, LLT Ty);
  /// Allocate a MachineMemOperand copy adjusted by \p Offset with \p Size.
  /// @param MMO Existing memory operand to copy.
  /// @param Offset Byte offset applied to the copied operand.
  /// @param Size Replacement access size.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          int64_t Offset, LocationSize Size) {
    return getMachineMemOperand(
        MMO, Offset,
        !Size.isPrecise() ? LLT()
        : Size.isScalable()
            ? LLT::scalable_vector(1, 8 * Size.getValue().getKnownMinValue())
            : LLT::scalar(8 * Size.getValue().getKnownMinValue()));
  }
  /// Allocate a MachineMemOperand copy adjusted by \p Offset with precise size.
  /// @param MMO Existing memory operand to copy.
  /// @param Offset Byte offset applied to the copied operand.
  /// @param Size Replacement access size in bytes.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          int64_t Offset, uint64_t Size) {
    return getMachineMemOperand(MMO, Offset, LocationSize::precise(Size));
  }
  /// Allocate a MachineMemOperand copy of \p MMO adjusted by offset and size.
  /// @param MMO Existing memory operand to copy.
  /// @param Offset Byte offset applied to the copied operand.
  /// @param Size Replacement access size.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          int64_t Offset, TypeSize Size) {
    return getMachineMemOperand(MMO, Offset, LocationSize::precise(Size));
  }

  /// Allocate a MachineMemOperand copy with new pointer info and size.
  ///
  /// MachineMemOperands are owned by the MachineFunction and need not be
  /// explicitly deallocated.
  /// @param MMO Existing memory operand to copy.
  /// @param PtrInfo Replacement pointer information.
  /// @param Size Replacement access size.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          const MachinePointerInfo &PtrInfo,
                                          LocationSize Size);
  /// Allocate a MachineMemOperand copy with new pointer info and LLT size.
  /// @param MMO Existing memory operand to copy.
  /// @param PtrInfo Replacement pointer information.
  /// @param Ty Replacement access type/size.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          const MachinePointerInfo &PtrInfo,
                                          LLT Ty);
  /// Allocate a MachineMemOperand copy with new pointer info and precise size.
  /// @param MMO Existing memory operand to copy.
  /// @param PtrInfo Replacement pointer information.
  /// @param Size Replacement access size in bytes.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          const MachinePointerInfo &PtrInfo,
                                          uint64_t Size) {
    return getMachineMemOperand(MMO, PtrInfo, LocationSize::precise(Size));
  }
  /// Allocate a MachineMemOperand copy with new pointer info and TypeSize.
  /// @param MMO Existing memory operand to copy.
  /// @param PtrInfo Replacement pointer information.
  /// @param Size Replacement access size.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          const MachinePointerInfo &PtrInfo,
                                          TypeSize Size) {
    return getMachineMemOperand(MMO, PtrInfo, LocationSize::precise(Size));
  }

  /// Allocate a MachineMemOperand copy of \p MMO with replaced AA metadata.
  ///
  /// MachineMemOperands are owned by the MachineFunction and need not be
  /// explicitly deallocated.
  /// @param MMO Existing memory operand to copy.
  /// @param AAInfo Replacement AliasAnalysis metadata.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          const AAMDNodes &AAInfo);

  /// Allocate a MachineMemOperand copy of \p MMO with replaced flags.
  ///
  /// MachineMemOperands are owned by the MachineFunction and need not be
  /// explicitly deallocated.
  /// @param MMO Existing memory operand to copy.
  /// @param Flags Replacement memory-operand flags.
  /// @return Newly allocated MachineMemOperand owned by this function.
  MachineMemOperand *getMachineMemOperand(const MachineMemOperand *MMO,
                                          MachineMemOperand::Flags Flags);

  /// Capacity token used when allocating recycled MachineOperand arrays.
  using OperandCapacity = ArrayRecycler<MachineOperand>::Capacity;

  /// Allocate an array of MachineOperands. This is only intended for use by
  /// internal MachineInstr functions.
  /// @param Cap Capacity of the operand array to allocate.
  /// @return Pointer to the allocated MachineOperand array.
  MachineOperand *allocateOperandArray(OperandCapacity Cap) {
    return OperandRecycler.allocate(Cap, Allocator);
  }

  /// Deallocate an array of MachineOperands and recycle the memory.
  ///
  /// Only intended for use by internal MachineInstr functions. \p Cap must be
  /// the same capacity that was used to allocate the array.
  /// @param Cap Capacity previously used to allocate \p Array.
  /// @param Array Operand array to recycle.
  void deallocateOperandArray(OperandCapacity Cap, MachineOperand *Array) {
    OperandRecycler.deallocate(Cap, Array);
  }

  /// Allocate and initialize a register mask with @p NumRegister bits.
  /// @return Pointer to a zero-initialized register mask owned by this function.
  uint32_t *allocateRegMask();

  /// Allocate a copy of shuffle mask \p Mask owned by this function.
  /// @param Mask Shuffle indices to copy into function-owned storage.
  /// @return ArrayRef to the function-owned copy of \p Mask.
  ArrayRef<int> allocateShuffleMask(ArrayRef<int> Mask);

  /// Allocate and construct an extra info structure for a `MachineInstr`.
  ///
  /// This is allocated on the function's allocator and so lives the life of
  /// the function.
  /// @param MMOs Memory operands attached to the instruction.
  /// @param PreInstrSymbol Optional symbol immediately before the instruction.
  /// @param PostInstrSymbol Optional symbol immediately after the instruction.
  /// @param HeapAllocMarker Optional heap-alloc metadata marker.
  /// @param PCSections Optional PC-sections metadata.
  /// @param CFIType Optional CFI type identifier.
  /// @param MMRAs Optional memory multi-versioning / MMRA metadata.
  /// @param DS Optional associated IR value.
  /// @return Newly allocated ExtraInfo owned by this function's allocator.
  MachineInstr::ExtraInfo *createMIExtraInfo(
      ArrayRef<MachineMemOperand *> MMOs, MCSymbol *PreInstrSymbol = nullptr,
      MCSymbol *PostInstrSymbol = nullptr, MDNode *HeapAllocMarker = nullptr,
      MDNode *PCSections = nullptr, uint32_t CFIType = 0,
      MDNode *MMRAs = nullptr, Value *DS = nullptr);

  /// Allocate a string and populate it with the given external symbol name.
  /// @param Name External symbol name to copy into function-owned storage.
  /// @return Pointer to the function-owned copy of \p Name.
  const char *createExternalSymbolName(StringRef Name);

  //===--------------------------------------------------------------------===//
  // Label Manipulation.

  /// Return the MCSymbol for the specified non-empty jump table.
  ///
  /// If \p isLinkerPrivate is specified, an 'l' label is returned, otherwise a
  /// normal 'L' label is returned.
  /// @param JTI Jump-table index whose symbol is requested.
  /// @param Ctx MC context used to create or look up the symbol.
  /// @param isLinkerPrivate If true, produce a linker-private 'l' label.
  /// @return MCSymbol for jump-table index \p JTI.
  MCSymbol *getJTISymbol(unsigned JTI, MCContext &Ctx,
                         bool isLinkerPrivate = false) const;

  /// getPICBaseSymbol - Return a function-local symbol to represent the PIC
  /// base.
  /// @return Function-local MCSymbol representing the PIC base.
  MCSymbol *getPICBaseSymbol() const;

  /// Returns a reference to a list of cfi instructions in the function's
  /// prologue.  Used to construct frame maps for debug and exception handling
  /// comsumers.
  /// @return Const list of CFI instructions in the function prologue.
  const std::vector<MCCFIInstruction> &getFrameInstructions() const {
    return FrameInstructions;
  }

  /// Append frame CFI instruction \p Inst and return its index.
  /// @param Inst CFI instruction to add to the function prologue list.
  /// @return Index of the newly appended CFI instruction.
  [[nodiscard]] unsigned addFrameInst(const MCCFIInstruction &Inst);

  /// Replace all references to register \param From with register \param To in
  /// frame instructions. Note that .cfi_escape instructions will be left as-is.
  void replaceFrameInstRegister(MCRegister From, MCRegister To);

  /// Return symbols immediately following calls to _setjmp in this function.
  ///
  /// Used to construct the longjmp target table used by Windows Control Flow
  /// Guard.
  /// @return Const list of symbols immediately following _setjmp calls.
  const std::vector<MCSymbol *> &getLongjmpTargets() const {
    return LongjmpTargets;
  }

  /// Add the specified symbol to the list of valid longjmp targets for Windows
  /// Control Flow Guard.
  /// @param Target Symbol immediately after a _setjmp call.
  void addLongjmpTarget(MCSymbol *Target) { LongjmpTargets.push_back(Target); }

  /// Returns a reference to a list of symbols that are targets for Windows
  /// EH Continuation Guard.
  /// @return Const list of Windows EH Continuation Guard target symbols.
  const std::vector<MCSymbol *> &getEHContTargets() const {
    return EHContTargets;
  }

  /// Add the specified symbol to the list of targets for Windows EH
  /// Continuation Guard.
  /// @param Target EH continuation guard target symbol.
  void addEHContTarget(MCSymbol *Target) { EHContTargets.push_back(Target); }

  /// Tries to get the global and target flags for a call site, if the
  /// instruction is a call to a global.
  /// @param MI Call instruction to look up.
  /// @return Called-global info for \p MI, or a default-constructed value.
  CalledGlobalInfo tryGetCalledGlobal(const MachineInstr *MI) const {
    return CalledGlobalsInfo.lookup(MI);
  }

  /// Notes the global and target flags for a call site.
  /// @param MI Call instruction being annotated.
  /// @param Details Callee global and target flags for \p MI.
  void addCalledGlobal(const MachineInstr *MI, CalledGlobalInfo Details) {
    assert(MI && "MI must not be null");
    assert(MI->isCandidateForAdditionalCallInfo() &&
           "Cannot store called global info for this instruction");
    assert(Details.Callee && "Global must not be null");
    CalledGlobalsInfo.insert({MI, Details});
  }

  /// Iterates over the full set of call sites and their associated globals.
  /// @return Range over call instructions and their CalledGlobalInfo.
  auto getCalledGlobals() const {
    return llvm::make_range(CalledGlobalsInfo.begin(), CalledGlobalsInfo.end());
  }

  /// \name Exception Handling
  /// \{

  /// Return true if this function contains a call to eh_return.
  /// @return True if this function contains a call to eh_return.
  bool callsEHReturn() const { return CallsEHReturn; }
  /// Set whether this function contains a call to eh_return.
  /// @param b True if the function calls eh_return.
  void setCallsEHReturn(bool b) { CallsEHReturn = b; }

  /// Return true if this function contains a call to _Unwind_Init.
  /// @return True if this function contains a call to _Unwind_Init.
  bool callsUnwindInit() const { return CallsUnwindInit; }
  /// Set whether this function contains a call to _Unwind_Init.
  /// @param b True if the function calls _Unwind_Init.
  void setCallsUnwindInit(bool b) { CallsUnwindInit = b; }

  /// Return true if this function has a Windows EH Continuation Guard target.
  /// @return True if a Windows EH Continuation Guard target is present.
  bool hasEHContTarget() const { return HasEHContTarget; }
  /// Set whether this function has a Windows EH Continuation Guard target.
  /// @param V True if an EH continuation target is present.
  void setHasEHContTarget(bool V) { HasEHContTarget = V; }

  /// Return true if this function uses EH scopes.
  /// @return True if this function uses EH scopes.
  bool hasEHScopes() const { return HasEHScopes; }
  /// Set whether this function uses EH scopes.
  /// @param V True if the function has EH scopes.
  void setHasEHScopes(bool V) { HasEHScopes = V; }

  /// Return true if this function uses EH funclets.
  /// @return True if this function uses EH funclets.
  bool hasEHFunclets() const { return HasEHFunclets; }
  /// Set whether this function uses EH funclets.
  /// @param V True if the function has EH funclets.
  void setHasEHFunclets(bool V) { HasEHFunclets = V; }

  /// Return true if this function contains FAKE_USE instructions.
  /// @return True if this function contains FAKE_USE instructions.
  bool hasFakeUses() const { return HasFakeUses; }
  /// Set whether this function contains FAKE_USE instructions.
  /// @param V True if FAKE_USE instructions are present.
  void setHasFakeUses(bool V) { HasFakeUses = V; }

  /// Return true if this function was created by the machine outliner.
  /// @return True if this function was created by the machine outliner.
  bool isOutlined() const { return IsOutlined; }
  /// Mark whether this function was created by the machine outliner.
  /// @param V True if this is an outlined function.
  void setIsOutlined(bool V) { IsOutlined = V; }

  /// Find or create an LandingPadInfo for the specified MachineBasicBlock.
  /// @param LandingPad Landing-pad block to look up or create info for.
  /// @return LandingPadInfo for \p LandingPad (existing or newly created).
  LandingPadInfo &getOrCreateLandingPadInfo(MachineBasicBlock *LandingPad);

  /// Return a reference to the landing pad info for the current function.
  /// @return Const list of landing-pad info for this function.
  const std::vector<LandingPadInfo> &getLandingPads() const {
    return LandingPads;
  }

  /// Provide the begin and end labels of an invoke style call and associate it
  /// with a try landing pad block.
  /// @param LandingPad Landing-pad block associated with the invoke.
  /// @param BeginLabel Label immediately before the invoke call.
  /// @param EndLabel Label immediately after the invoke call.
  void addInvoke(MachineBasicBlock *LandingPad,
                 MCSymbol *BeginLabel, MCSymbol *EndLabel);

  /// Add a new landing pad and extract EH info from its landingpad instruction.
  ///
  /// Returns the label ID for the landing pad entry.
  /// @param LandingPad Landing-pad block to register.
  /// @return Label symbol for the landing-pad entry.
  MCSymbol *addLandingPad(MachineBasicBlock *LandingPad);

  /// Return the type id for the specified typeinfo.  This is function wide.
  /// @param TI Typeinfo global whose type id is requested.
  /// @return Function-wide type id for \p TI.
  unsigned getTypeIDFor(const GlobalValue *TI);

  /// Return the id of the filter encoded by TyIds.  This is function wide.
  /// @param TyIds Sequence of type ids encoding the filter.
  /// @return Function-wide filter id encoded by \p TyIds.
  int getFilterIDFor(ArrayRef<unsigned> TyIds);

  /// Map the landing pad's EH symbol to the call site indexes.
  /// @param Sym Landing-pad EH symbol.
  /// @param Sites Call-site indexes active at this landing pad.
  void setCallSiteLandingPad(MCSymbol *Sym, ArrayRef<unsigned> Sites);

  /// Return if there is any wasm exception handling.
  /// @return True if any Wasm landing-pad indexes have been recorded.
  bool hasAnyWasmLandingPadIndex() const {
    return !WasmLPadToIndexMap.empty();
  }

  /// Map the landing pad to its index. Used for Wasm exception handling.
  /// @param LPad Landing-pad block receiving a Wasm EH index.
  /// @param Index Wasm landing-pad index to associate with \p LPad.
  void setWasmLandingPadIndex(const MachineBasicBlock *LPad, unsigned Index) {
    WasmLPadToIndexMap[LPad] = Index;
  }

  /// Returns true if the landing pad has an associate index in wasm EH.
  /// @param LPad Landing-pad block to query.
  /// @return True if \p LPad has an associated Wasm landing-pad index.
  bool hasWasmLandingPadIndex(const MachineBasicBlock *LPad) const {
    return WasmLPadToIndexMap.count(LPad);
  }

  /// Get the index in wasm EH for a given landing pad.
  /// @param LPad Landing-pad block whose Wasm index is requested.
  /// @return Wasm landing-pad index associated with \p LPad.
  unsigned getWasmLandingPadIndex(const MachineBasicBlock *LPad) const {
    assert(hasWasmLandingPadIndex(LPad));
    return WasmLPadToIndexMap.lookup(LPad);
  }

  /// Return true if any landing-pad EH symbol has call-site indexes recorded.
  /// @return True if any landing-pad EH symbol has call-site indexes.
  bool hasAnyCallSiteLandingPad() const {
    return !LPadToCallSiteMap.empty();
  }

  /// Get the call site indexes for a landing pad EH symbol.
  /// @param Sym Landing-pad EH symbol whose call-site indexes are requested.
  /// @return Mutable list of call-site indexes for \p Sym.
  SmallVectorImpl<unsigned> &getCallSiteLandingPad(MCSymbol *Sym) {
    assert(hasCallSiteLandingPad(Sym) &&
           "missing call site number for landing pad!");
    return LPadToCallSiteMap[Sym];
  }

  /// Return true if the landing pad Eh symbol has an associated call site.
  /// @param Sym Landing-pad EH symbol to query.
  /// @return True if \p Sym has at least one associated call-site index.
  bool hasCallSiteLandingPad(MCSymbol *Sym) {
    return !LPadToCallSiteMap[Sym].empty();
  }

  /// Return true if any call site begin label has been recorded for EH.
  /// @return True if any call-site begin label has been recorded.
  bool hasAnyCallSiteLabel() const {
    return !CallSiteMap.empty();
  }

  /// Map the begin label for a call site.
  /// @param BeginLabel Begin EH label of the call site.
  /// @param Site Call-site index associated with \p BeginLabel.
  void setCallSiteBeginLabel(MCSymbol *BeginLabel, unsigned Site) {
    CallSiteMap[BeginLabel] = Site;
  }

  /// Get the call site number for a begin label.
  /// @param BeginLabel Begin EH label whose call-site index is requested.
  /// @return Call-site index associated with \p BeginLabel.
  unsigned getCallSiteBeginLabel(MCSymbol *BeginLabel) const {
    assert(hasCallSiteBeginLabel(BeginLabel) &&
           "Missing call site number for EH_LABEL!");
    return CallSiteMap.lookup(BeginLabel);
  }

  /// Return true if the begin label has a call site number associated with it.
  /// @param BeginLabel Begin EH label to query.
  /// @return True if \p BeginLabel has an associated call-site index.
  bool hasCallSiteBeginLabel(MCSymbol *BeginLabel) const {
    return CallSiteMap.count(BeginLabel);
  }

  /// Record annotations associated with a particular label.
  /// @param Label CodeView label receiving the annotation.
  /// @param MD Metadata node describing the annotation.
  void addCodeViewAnnotation(MCSymbol *Label, MDNode *MD) {
    CodeViewAnnotations.push_back({Label, MD});
  }

  /// Return the CodeView label annotations recorded for this function.
  /// @return Array of CodeView label/annotation pairs for this function.
  ArrayRef<std::pair<MCSymbol *, MDNode *>> getCodeViewAnnotations() const {
    return CodeViewAnnotations;
  }

  /// Return a reference to the C++ typeinfo for the current function.
  /// @return Const list of C++ typeinfo globals for this function.
  const std::vector<const GlobalValue *> &getTypeInfos() const {
    return TypeInfos;
  }

  /// Return a reference to the typeids encoding filters used in the current
  /// function.
  /// @return Const list of type ids encoding filters for this function.
  const std::vector<unsigned> &getFilterIds() const {
    return FilterIds;
  }

  /// \}

  /// Collect information used to emit debugging information of a variable in a
  /// stack slot.
  /// @param Var Debug variable being described.
  /// @param Expr DIExpression applied to the variable location.
  /// @param Slot Stack slot index holding the variable.
  /// @param Loc Source location associated with this debug info.
  void setVariableDbgInfo(const DILocalVariable *Var, const DIExpression *Expr,
                          int Slot, const DILocation *Loc) {
    VariableDbgInfos.emplace_back(Var, Expr, Slot, Loc);
  }

  /// Collect information used to emit debugging information of a variable in
  /// the entry value of a register.
  /// @param Var Debug variable being described.
  /// @param Expr DIExpression applied to the variable location.
  /// @param Reg Register whose entry value holds the variable.
  /// @param Loc Source location associated with this debug info.
  void setVariableDbgInfo(const DILocalVariable *Var, const DIExpression *Expr,
                          MCRegister Reg, const DILocation *Loc) {
    VariableDbgInfos.emplace_back(Var, Expr, Reg, Loc);
  }

  /// Return the mutable collection of debug info for variables in this function.
  /// @return Mutable collection of variable debug info.
  VariableDbgInfoMapTy &getVariableDbgInfo() { return VariableDbgInfos; }
  /// Return the const collection of debug info for variables in this function.
  /// @return Const collection of variable debug info.
  const VariableDbgInfoMapTy &getVariableDbgInfo() const {
    return VariableDbgInfos;
  }

  /// Returns the collection of variables for which we have debug info and that
  /// have been assigned a stack slot.
  /// @return Filtered range of variable debug info in stack slots.
  auto getInStackSlotVariableDbgInfo() {
    return make_filter_range(getVariableDbgInfo(), [](auto &VarInfo) {
      return VarInfo.inStackSlot();
    });
  }

  /// Returns the collection of variables for which we have debug info and that
  /// have been assigned a stack slot.
  /// @return Const filtered range of variable debug info in stack slots.
  auto getInStackSlotVariableDbgInfo() const {
    return make_filter_range(getVariableDbgInfo(), [](const auto &VarInfo) {
      return VarInfo.inStackSlot();
    });
  }

  /// Returns the collection of variables for which we have debug info and that
  /// have been assigned an entry value register.
  /// @return Const filtered range of entry-value variable debug info.
  auto getEntryValueVariableDbgInfo() const {
    return make_filter_range(getVariableDbgInfo(), [](const auto &VarInfo) {
      return VarInfo.inEntryValueRegister();
    });
  }

  /// Start tracking the arguments passed to the call \p CallI.
  /// @param CallI Call instruction whose site info is recorded.
  /// @param CallInfo Call-site argument and type metadata to store.
  void addCallSiteInfo(const MachineInstr *CallI, CallSiteInfo &&CallInfo) {
    assert(CallI->isCandidateForAdditionalCallInfo());
    bool Inserted =
        CallSitesInfo.try_emplace(CallI, std::move(CallInfo)).second;
    (void)Inserted;
    assert(Inserted && "Call site info not unique");
  }

  /// Return the map from call instructions to their call-site info.
  /// @return Const map from call instructions to call-site info.
  const CallSiteInfoMap &getCallSitesInfo() const {
    return CallSitesInfo;
  }

  /// Following functions update call site info. They should be called before
  /// removing, replacing or copying call instruction.

  /// Erase the call site info for call instruction \p MI.
  /// @param MI Call instruction whose site info is removed.
  void eraseAdditionalCallInfo(const MachineInstr *MI);
  /// Copy call-site info from \p Old to \p New for a duplicated call.
  ///
  /// Used when making a copy of the instruction that will be inserted at a
  /// different point of the instruction stream.
  /// @param Old Source call instruction whose site info is copied.
  /// @param New Destination call instruction receiving the site info.
  void copyAdditionalCallInfo(const MachineInstr *Old, const MachineInstr *New);

  /// Move call-site info from \p Old to \p New when replacing a call.
  ///
  /// Used when replacing one call instruction with another to the same callee.
  /// @param Old Call instruction whose site info is moved.
  /// @param New Call instruction receiving the site info.
  void moveAdditionalCallInfo(const MachineInstr *Old, const MachineInstr *New);

  /// Return the next instruction number for debug value tracking.
  /// @return Newly assigned debug instruction number.
  unsigned getNewDebugInstrNum() {
    return ++DebugInstrNumberingCount;
  }
};

//===--------------------------------------------------------------------===//
// GraphTraits specializations for function basic block graphs (CFGs)
//===--------------------------------------------------------------------===//

/// GraphTraits specialization treating a MachineFunction as a CFG of machine
/// basic blocks, with the entry node as the first block.
template <> struct GraphTraits<MachineFunction*> :
  public GraphTraits<MachineBasicBlock*> {
  /// Return the entry node of machine function \p F (its first basic block).
  /// @param F Machine function whose CFG entry is requested.
  /// @return Entry MachineBasicBlock of \p F.
  static NodeRef getEntryNode(MachineFunction *F) { return &F->front(); }

  /// Iterator over all machine basic blocks in the function CFG.
  using nodes_iterator = pointer_iterator<MachineFunction::iterator>;

  /// Return a begin iterator over the basic blocks of \p F.
  /// @param F Machine function whose blocks are iterated.
  /// @return Begin iterator over the basic blocks of \p F.
  static nodes_iterator nodes_begin(MachineFunction *F) {
    return nodes_iterator(F->begin());
  }

  /// Return an end iterator over the basic blocks of \p F.
  /// @param F Machine function whose blocks are iterated.
  /// @return End iterator over the basic blocks of \p F.
  static nodes_iterator nodes_end(MachineFunction *F) {
    return nodes_iterator(F->end());
  }

  /// Return the number of basic blocks in machine function \p F.
  /// @param F Machine function whose block count is requested.
  /// @return Number of basic blocks in \p F.
  static unsigned       size       (MachineFunction *F) { return F->size(); }

  /// Return one past the highest analysis block number in \p F.
  /// @param F Machine function whose max analysis number is requested.
  /// @return One past the highest analysis block number in \p F.
  static unsigned getMaxNumber(MachineFunction *F) {
    return F->getMaxAnalysisBlockNumber();
  }
  /// Return the analysis-numbering epoch of machine function \p F.
  /// @param F Machine function whose numbering epoch is requested.
  /// @return Analysis-numbering epoch of \p F.
  static unsigned getNumberEpoch(MachineFunction *F) {
    return F->getAnalysisBlockNumberEpoch();
  }
};
/// GraphTraits specialization treating a const MachineFunction as a CFG of
/// machine basic blocks, with the entry node as the first block.
template <> struct GraphTraits<const MachineFunction*> :
  public GraphTraits<const MachineBasicBlock*> {
  /// Return the entry node of const machine function \p F.
  /// @param F Machine function whose CFG entry is requested.
  /// @return Entry MachineBasicBlock of \p F.
  static NodeRef getEntryNode(const MachineFunction *F) { return &F->front(); }

  /// Const iterator over all machine basic blocks in the function CFG.
  using nodes_iterator = pointer_iterator<MachineFunction::const_iterator>;

  /// Return a begin iterator over the basic blocks of \p F.
  /// @param F Machine function whose blocks are iterated.
  /// @return Begin iterator over the basic blocks of \p F.
  static nodes_iterator nodes_begin(const MachineFunction *F) {
    return nodes_iterator(F->begin());
  }

  /// Return an end iterator over the basic blocks of \p F.
  /// @param F Machine function whose blocks are iterated.
  /// @return End iterator over the basic blocks of \p F.
  static nodes_iterator nodes_end  (const MachineFunction *F) {
    return nodes_iterator(F->end());
  }

  /// Return the number of basic blocks in const machine function \p F.
  /// @param F Machine function whose block count is requested.
  /// @return Number of basic blocks in \p F.
  static unsigned       size       (const MachineFunction *F)  {
    return F->size();
  }

  /// Return one past the highest analysis block number in \p F.
  /// @param F Machine function whose max analysis number is requested.
  /// @return One past the highest analysis block number in \p F.
  static unsigned getMaxNumber(const MachineFunction *F) {
    return F->getMaxAnalysisBlockNumber();
  }
  /// Return the analysis-numbering epoch of const machine function \p F.
  /// @param F Machine function whose numbering epoch is requested.
  /// @return Analysis-numbering epoch of \p F.
  static unsigned getNumberEpoch(const MachineFunction *F) {
    return F->getAnalysisBlockNumberEpoch();
  }
};

/// GraphTraits specialization walking a MachineFunction CFG in inverse order
/// via predecessor edges.
template <> struct GraphTraits<Inverse<MachineFunction*>> :
  public GraphTraits<Inverse<MachineBasicBlock*>> {
  /// Return the entry node of the inverse machine-function CFG.
  /// @param G Inverse wrapper around the machine function.
  /// @return Entry MachineBasicBlock of the wrapped function.
  static NodeRef getEntryNode(Inverse<MachineFunction *> G) {
    return &G.Graph->front();
  }

  /// Return one past the highest analysis block number in \p F.
  /// @param F Machine function whose max analysis number is requested.
  /// @return One past the highest analysis block number in \p F.
  static unsigned getMaxNumber(MachineFunction *F) {
    return F->getMaxAnalysisBlockNumber();
  }
  /// Return the analysis-numbering epoch of machine function \p F.
  /// @param F Machine function whose numbering epoch is requested.
  /// @return Analysis-numbering epoch of \p F.
  static unsigned getNumberEpoch(MachineFunction *F) {
    return F->getAnalysisBlockNumberEpoch();
  }
};
/// GraphTraits specialization walking a const MachineFunction CFG in inverse
/// order via predecessor edges.
template <> struct GraphTraits<Inverse<const MachineFunction*>> :
  public GraphTraits<Inverse<const MachineBasicBlock*>> {
  /// Return the entry node of the inverse const machine-function CFG.
  /// @param G Inverse wrapper around the const machine function.
  /// @return Entry MachineBasicBlock of the wrapped function.
  static NodeRef getEntryNode(Inverse<const MachineFunction *> G) {
    return &G.Graph->front();
  }

  /// Return one past the highest analysis block number in \p F.
  /// @param F Machine function whose max analysis number is requested.
  /// @return One past the highest analysis block number in \p F.
  static unsigned getMaxNumber(const MachineFunction *F) {
    return F->getMaxAnalysisBlockNumber();
  }
  /// Return the analysis-numbering epoch of const machine function \p F.
  /// @param F Machine function whose numbering epoch is requested.
  /// @return Analysis-numbering epoch of \p F.
  static unsigned getNumberEpoch(const MachineFunction *F) {
    return F->getAnalysisBlockNumberEpoch();
  }
};

/// Run the machine code verifier on \p MF, reporting problems under \p Banner.
/// @param Banner Banner string printed with verification diagnostics.
/// @param MF Machine function to verify.
LLVM_ABI void verifyMachineFunction(const std::string &Banner,
                                    const MachineFunction &MF);

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEFUNCTION_H
