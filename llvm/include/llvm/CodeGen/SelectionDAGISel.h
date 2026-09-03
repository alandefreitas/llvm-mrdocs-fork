//===-- llvm/CodeGen/SelectionDAGISel.h - Common Base Class------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SelectionDAGISel class, which is used as the common
// base class for SelectionDAG-based instruction selectors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SELECTIONDAGISEL_H
#define LLVM_CODEGEN_SELECTIONDAGISEL_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/BasicBlock.h"
#include <memory>

namespace llvm {
class AAResults;
class AssumptionCache;
class TargetInstrInfo;
class TargetMachine;
class SSPLayoutInfo;
/// Builds a SelectionDAG from LLVM IR for instruction selection.
class SelectionDAGBuilder;
class SDValue;
class MachineRegisterInfo;
class MachineFunction;
class OptimizationRemarkEmitter;
class TargetLowering;
class TargetLibraryInfo;
class TargetTransformInfo;
class FunctionLoweringInfo;
class SwiftErrorValueTracking;
class GCFunctionInfo;
class ScheduleDAGSDNodes;

/// SelectionDAGISel - This is the common base class used for SelectionDAG-based
/// pattern-matching instruction selectors.
class LLVM_ABI SelectionDAGISel {
public:
  /// Target machine for the current compilation.
  TargetMachine &TM;
  /// Target library information for the current function.
  const TargetLibraryInfo *LibInfo;
  /// Libcall lowering information for the current target.
  const LibcallLoweringInfo *LibcallLowering;

  /// Per-function lowering state shared with the DAG builder.
  std::unique_ptr<FunctionLoweringInfo> FuncInfo;
  /// Tracker for Swift error values during ISel.
  std::unique_ptr<SwiftErrorValueTracking> SwiftError;
  /// Machine function currently being instruction-selected.
  MachineFunction *MF;
  /// Module-level machine information for the current module.
  MachineModuleInfo *MMI;
  /// Register information for the current machine function.
  MachineRegisterInfo *RegInfo;
  /// SelectionDAG being built and selected for the current function.
  SelectionDAG *CurDAG;
  /// Builder that lowers IR into the current SelectionDAG.
  std::unique_ptr<SelectionDAGBuilder> SDB;
  /// Optional batch alias-analysis results for the current function.
  mutable std::optional<BatchAAResults> BatchAA;
  /// Assumption cache for the current function, if available.
  AssumptionCache *AC = nullptr;
  /// GC function info for the current function, if any.
  GCFunctionInfo *GFI = nullptr;
  /// Stack-protector layout info for the current function, if any.
  SSPLayoutInfo *SP = nullptr;
  /// Target transform info for the current function, if available.
  const TargetTransformInfo *TTI = nullptr;
  /// Optimization level used for instruction selection.
  CodeGenOptLevel OptLevel;
  /// Target instruction information for the current subtarget.
  const TargetInstrInfo *TII;
  /// Target lowering interface for the current subtarget.
  const TargetLowering *TLI;
  /// True if FastISel was attempted and failed for the current function.
  bool FastISelFailed;
  /// Instructions whose argument copies were elided during selection.
  SmallPtrSet<const Instruction *, 4> ElidedArgCopyInstrs;

  /// Current optimization remark emitter.
  /// Used to report things like combines and FastISel failures.
  std::unique_ptr<OptimizationRemarkEmitter> ORE;

  /// True if the current function matches `-filter-print-funcs`.
  ///
  /// This is primarily used by ISEL_DUMP, which spans multiple member
  /// functions. Storing the filter result here means filtering is done once.
  bool MatchFilterFuncName = false;
  /// Name of the function currently being instruction-selected.
  StringRef FuncName;

  /// Hardware mode used by getValueTypeForHwMode for the current subtarget.
  ///
  /// Initialized based on the subtarget used by the MachineFunction.
  unsigned HwMode;

  /// Construct a SelectionDAG instruction selector for target machine \p tm.
  ///
  /// \param tm Target machine providing subtarget and lowering hooks.
  /// \param OL Optimization level for selection.
  explicit SelectionDAGISel(TargetMachine &tm,
                            CodeGenOptLevel OL = CodeGenOptLevel::Default);
  /// Destroy the SelectionDAG instruction selector.
  virtual ~SelectionDAGISel();

  /// Returns a (possibly null) pointer to the current BatchAAResults.
  ///
  /// \return Pointer to the current BatchAAResults, or null if none.
  BatchAAResults *getBatchAA() const {
    if (BatchAA.has_value())
      return &BatchAA.value();
    return nullptr;
  }

  /// Return the target lowering interface used by this selector.
  ///
  /// \return The target lowering interface for the current subtarget.
  const TargetLowering *getTargetLowering() const { return TLI; }

  /// Initialize analysis results from the new pass manager.
  ///
  /// \param MFAM Machine function analysis manager to query.
  void initializeAnalysisResults(MachineFunctionAnalysisManager &MFAM);
  /// Initialize analysis results from a legacy machine function pass.
  ///
  /// \param MFP Legacy pass providing getAnalysis<> accessors.
  void initializeAnalysisResults(MachineFunctionPass &MFP);

  /// Run SelectionDAG instruction selection on machine function \p mf.
  ///
  /// \param mf Machine function to select.
  /// \return True if the machine function was modified.
  virtual bool runOnMachineFunction(MachineFunction &mf);

  /// Emit target-specific code at function entry before selection.
  virtual void emitFunctionEntryCode() {}

  /// PreprocessISelDAG - This hook allows targets to hack on the graph before
  /// instruction selection starts.
  virtual void PreprocessISelDAG() {}

  /// PostprocessISelDAG() - This hook allows the target to hack on the graph
  /// right after selection.
  virtual void PostprocessISelDAG() {}

  /// Main hook for targets to transform nodes into machine nodes.
  ///
  /// \param N DAG node to select into machine nodes.
  virtual void Select(SDNode *N) = 0;

  /// Select \p Op as a target memory addressing mode for inline asm.
  ///
  /// Matches according to constraint \p ConstraintID. If this does not match
  /// or is not implemented, return true. The resultant operands (which will
  /// appear in the machine instruction) should be added to the OutOps vector.
  ///
  /// \param Op Address value to select.
  /// \param ConstraintID Inline-asm memory constraint to satisfy.
  /// \param OutOps Receives selected addressing-mode operands on success.
  /// \return True if selection failed or is unimplemented.
  virtual bool
  SelectInlineAsmMemoryOperand(const SDValue &Op,
                               InlineAsm::ConstraintCode ConstraintID,
                               std::vector<SDValue> &OutOps) {
    return true;
  }

  /// Return true if folding operand \p N of \p U into \p Root is profitable.
  ///
  /// \param N Operand value considered for folding.
  /// \param U User node that currently consumes \p N.
  /// \param Root Root of the selection match under consideration.
  /// \return True if folding \p N into \p Root is profitable.
  virtual bool IsProfitableToFold(SDValue N, SDNode *U, SDNode *Root) const;

  /// Return true if operand \p N of \p U may legally fold into \p Root.
  ///
  /// FIXME: This is a static member function because the MSP430/X86 targets
  /// use it during isel. This could become a proper member.
  ///
  /// \param N Operand value considered for folding.
  /// \param U User node that currently consumes \p N.
  /// \param Root Root of the selection match under consideration.
  /// \param OptLevel Current optimization level.
  /// \param IgnoreChains If true, ignore chain constraints when folding.
  /// \return True if folding \p N into \p Root is legal.
  static bool IsLegalToFold(SDValue N, SDNode *U, SDNode *Root,
                            CodeGenOptLevel OptLevel,
                            bool IgnoreChains = false);

  /// Mark node IDs invalid for topological pruning after replacing \p N.
  ///
  /// \param N Node whose positive node IDs on users must be invalidated.
  static void InvalidateNodeId(SDNode *N);
  /// Return the original node ID of \p N before InvalidateNodeId negation.
  ///
  /// \param N Node whose possibly invalidated ID should be recovered.
  /// \return The non-negative original node ID of \p N.
  static int getUninvalidatedNodeId(SDNode *N);

  /// Propagate node-ID invalidation through users of newly created node \p N.
  ///
  /// \param N Replacement node whose user chain must keep the node-ID invariant.
  static void EnforceNodeIdInvariant(SDNode *N);

  /// Opcodes used by the TableGen-generated DAG matcher state machine.
  enum BuiltinOpcodes {
    OPC_Scope, ///< Push a matcher scope / backtrack point.
    OPC_RecordNode, ///< Record the current node.
    OPC_RecordChild0, ///< Record child 0 of the current node.
    OPC_RecordChild1, ///< Record child 1 of the current node.
    OPC_RecordChild2, ///< Record child 2 of the current node.
    OPC_RecordChild3, ///< Record child 3 of the current node.
    OPC_RecordChild4, ///< Record child 4 of the current node.
    OPC_RecordChild5, ///< Record child 5 of the current node.
    OPC_RecordChild6, ///< Record child 6 of the current node.
    OPC_RecordChild7, ///< Record child 7 of the current node.
    OPC_RecordMemRef, ///< Record a memory reference from the current node.
    OPC_CaptureGlueInput, ///< Capture the glue input of the current node.
    OPC_CaptureDeactivationSymbol, ///< Capture a deactivation symbol operand.
    OPC_MoveChild, ///< Move to a child indexed by the next matcher byte.
    OPC_MoveChild0, ///< Move to child 0.
    OPC_MoveChild1, ///< Move to child 1.
    OPC_MoveChild2, ///< Move to child 2.
    OPC_MoveChild3, ///< Move to child 3.
    OPC_MoveChild4, ///< Move to child 4.
    OPC_MoveChild5, ///< Move to child 5.
    OPC_MoveChild6, ///< Move to child 6.
    OPC_MoveChild7, ///< Move to child 7.
    OPC_MoveSibling, ///< Move to a sibling indexed by the next matcher byte.
    OPC_MoveSibling0, ///< Move to sibling 0.
    OPC_MoveSibling1, ///< Move to sibling 1.
    OPC_MoveSibling2, ///< Move to sibling 2.
    OPC_MoveSibling3, ///< Move to sibling 3.
    OPC_MoveSibling4, ///< Move to sibling 4.
    OPC_MoveSibling5, ///< Move to sibling 5.
    OPC_MoveSibling6, ///< Move to sibling 6.
    OPC_MoveSibling7, ///< Move to sibling 7.
    OPC_MoveParent, ///< Move to the parent of the current node.
    OPC_CheckSame, ///< Check the current node equals a previously recorded one.
    OPC_CheckChild0Same, ///< Check child 0 equals a previously recorded node.
    OPC_CheckChild1Same, ///< Check child 1 equals a previously recorded node.
    OPC_CheckChild2Same, ///< Check child 2 equals a previously recorded node.
    OPC_CheckChild3Same, ///< Check child 3 equals a previously recorded node.
    OPC_CheckPatternPredicate, ///< Run a pattern predicate by index.
    OPC_CheckPatternPredicate0, ///< Run pattern predicate 0.
    OPC_CheckPatternPredicate1, ///< Run pattern predicate 1.
    OPC_CheckPatternPredicate2, ///< Run pattern predicate 2.
    OPC_CheckPatternPredicate3, ///< Run pattern predicate 3.
    OPC_CheckPatternPredicate4, ///< Run pattern predicate 4.
    OPC_CheckPatternPredicate5, ///< Run pattern predicate 5.
    OPC_CheckPatternPredicate6, ///< Run pattern predicate 6.
    OPC_CheckPatternPredicate7, ///< Run pattern predicate 7.
    OPC_CheckPatternPredicateTwoByte, ///< Run a pattern predicate with a 16-bit index.
    OPC_CheckPredicate, ///< Run a node predicate by index.
    OPC_CheckPredicate0, ///< Run node predicate 0.
    OPC_CheckPredicate1, ///< Run node predicate 1.
    OPC_CheckPredicate2, ///< Run node predicate 2.
    OPC_CheckPredicate3, ///< Run node predicate 3.
    OPC_CheckPredicate4, ///< Run node predicate 4.
    OPC_CheckPredicate5, ///< Run node predicate 5.
    OPC_CheckPredicate6, ///< Run node predicate 6.
    OPC_CheckPredicate7, ///< Run node predicate 7.
    OPC_CheckPredicateWithOperands, ///< Run a node predicate with recorded operands.
    OPC_CheckOpcode, ///< Check the current node's opcode.
    OPC_SwitchOpcode, ///< Dispatch on the current node's opcode.
    OPC_CheckType, ///< Check the current node's value type.
    // Space-optimized forms that implicitly encode VT.
    OPC_CheckTypeI32, ///< Check the current node has i32 type.
    OPC_CheckTypeI64, ///< Check the current node has i64 type.
    OPC_CheckTypeByHwMode, ///< Check type using a hw-mode type table entry.
    // Space-optimized form that implicitly encodes index 0.
    OPC_CheckTypeByHwMode0, ///< Check type using hw-mode table entry 0.
    OPC_CheckTypeRes, ///< Check a result value type by result index.
    OPC_CheckTypeResByHwMode, ///< Check a result type via hw-mode table.
    OPC_SwitchType, ///< Dispatch on the current node's value type.
    OPC_CheckChild0Type, ///< Check child 0's value type.
    OPC_CheckChild1Type, ///< Check child 1's value type.
    OPC_CheckChild2Type, ///< Check child 2's value type.
    OPC_CheckChild3Type, ///< Check child 3's value type.
    OPC_CheckChild4Type, ///< Check child 4's value type.
    OPC_CheckChild5Type, ///< Check child 5's value type.
    OPC_CheckChild6Type, ///< Check child 6's value type.
    OPC_CheckChild7Type, ///< Check child 7's value type.

    OPC_CheckChild0TypeI32, ///< Check child 0 has i32 type.
    OPC_CheckChild1TypeI32, ///< Check child 1 has i32 type.
    OPC_CheckChild2TypeI32, ///< Check child 2 has i32 type.
    OPC_CheckChild3TypeI32, ///< Check child 3 has i32 type.
    OPC_CheckChild4TypeI32, ///< Check child 4 has i32 type.
    OPC_CheckChild5TypeI32, ///< Check child 5 has i32 type.
    OPC_CheckChild6TypeI32, ///< Check child 6 has i32 type.
    OPC_CheckChild7TypeI32, ///< Check child 7 has i32 type.

    OPC_CheckChild0TypeI64, ///< Check child 0 has i64 type.
    OPC_CheckChild1TypeI64, ///< Check child 1 has i64 type.
    OPC_CheckChild2TypeI64, ///< Check child 2 has i64 type.
    OPC_CheckChild3TypeI64, ///< Check child 3 has i64 type.
    OPC_CheckChild4TypeI64, ///< Check child 4 has i64 type.
    OPC_CheckChild5TypeI64, ///< Check child 5 has i64 type.
    OPC_CheckChild6TypeI64, ///< Check child 6 has i64 type.
    OPC_CheckChild7TypeI64, ///< Check child 7 has i64 type.

    OPC_CheckChild0TypeByHwMode, ///< Check child 0 type via hw-mode table.
    OPC_CheckChild1TypeByHwMode, ///< Check child 1 type via hw-mode table.
    OPC_CheckChild2TypeByHwMode, ///< Check child 2 type via hw-mode table.
    OPC_CheckChild3TypeByHwMode, ///< Check child 3 type via hw-mode table.
    OPC_CheckChild4TypeByHwMode, ///< Check child 4 type via hw-mode table.
    OPC_CheckChild5TypeByHwMode, ///< Check child 5 type via hw-mode table.
    OPC_CheckChild6TypeByHwMode, ///< Check child 6 type via hw-mode table.
    OPC_CheckChild7TypeByHwMode, ///< Check child 7 type via hw-mode table.

    OPC_CheckChild0TypeByHwMode0, ///< Check child 0 type via hw-mode entry 0.
    OPC_CheckChild1TypeByHwMode0, ///< Check child 1 type via hw-mode entry 0.
    OPC_CheckChild2TypeByHwMode0, ///< Check child 2 type via hw-mode entry 0.
    OPC_CheckChild3TypeByHwMode0, ///< Check child 3 type via hw-mode entry 0.
    OPC_CheckChild4TypeByHwMode0, ///< Check child 4 type via hw-mode entry 0.
    OPC_CheckChild5TypeByHwMode0, ///< Check child 5 type via hw-mode entry 0.
    OPC_CheckChild6TypeByHwMode0, ///< Check child 6 type via hw-mode entry 0.
    OPC_CheckChild7TypeByHwMode0, ///< Check child 7 type via hw-mode entry 0.

    OPC_CheckInteger, ///< Check the current node is an integer constant.
    OPC_CheckChild0Integer, ///< Check child 0 is an integer constant.
    OPC_CheckChild1Integer, ///< Check child 1 is an integer constant.
    OPC_CheckChild2Integer, ///< Check child 2 is an integer constant.
    OPC_CheckChild3Integer, ///< Check child 3 is an integer constant.
    OPC_CheckChild4Integer, ///< Check child 4 is an integer constant.
    OPC_CheckCondCode, ///< Check the current node is a condition code.
    OPC_CheckChild2CondCode, ///< Check child 2 is a condition code.
    OPC_CheckValueType, ///< Check the current node is a value-type constant.
    OPC_CheckComplexPat, ///< Match a complex pattern by index.
    OPC_CheckComplexPat0, ///< Match complex pattern 0.
    OPC_CheckComplexPat1, ///< Match complex pattern 1.
    OPC_CheckComplexPat2, ///< Match complex pattern 2.
    OPC_CheckComplexPat3, ///< Match complex pattern 3.
    OPC_CheckComplexPat4, ///< Match complex pattern 4.
    OPC_CheckComplexPat5, ///< Match complex pattern 5.
    OPC_CheckComplexPat6, ///< Match complex pattern 6.
    OPC_CheckComplexPat7, ///< Match complex pattern 7.
    OPC_CheckAndImm, ///< Check an AND with an immediate mask.
    OPC_CheckOrImm, ///< Check an OR with an immediate mask.
    OPC_CheckImmAllOnesV, ///< Check for an all-ones vector immediate.
    OPC_CheckImmAllZerosV, ///< Check for an all-zeros vector immediate.
    OPC_CheckUndef, ///< Check the current node is UNDEF.
    OPC_CheckFoldableChainNode, ///< Check the current chain node is foldable.

    OPC_EmitInteger, ///< Emit an integer constant operand.
    // Space-optimized forms that implicitly encode integer VT.
    OPC_EmitIntegerI8, ///< Emit an i8 integer constant.
    OPC_EmitIntegerI16, ///< Emit an i16 integer constant.
    OPC_EmitIntegerI32, ///< Emit an i32 integer constant.
    OPC_EmitIntegerI64, ///< Emit an i64 integer constant.
    OPC_EmitIntegerByHwMode, ///< Emit an integer typed by hw-mode table.
    OPC_EmitIntegerByHwMode0, ///< Emit an integer typed by hw-mode entry 0.
    OPC_EmitRegister, ///< Emit a physical register operand.
    OPC_EmitRegisterI32, ///< Emit an i32 physical register operand.
    OPC_EmitRegisterI64, ///< Emit an i64 physical register operand.
    OPC_EmitRegisterByHwMode, ///< Emit a register typed by hw-mode table.
    OPC_EmitRegister2, ///< Emit a two-byte-encoded physical register.
    OPC_EmitRegisterByHwMode2, ///< Emit a two-byte register typed by hw-mode.
    OPC_EmitConvertToTarget, ///< Emit a Target* form of a recorded operand.
    OPC_EmitConvertToTarget0, ///< Emit Target* form of recorded operand 0.
    OPC_EmitConvertToTarget1, ///< Emit Target* form of recorded operand 1.
    OPC_EmitConvertToTarget2, ///< Emit Target* form of recorded operand 2.
    OPC_EmitConvertToTarget3, ///< Emit Target* form of recorded operand 3.
    OPC_EmitConvertToTarget4, ///< Emit Target* form of recorded operand 4.
    OPC_EmitConvertToTarget5, ///< Emit Target* form of recorded operand 5.
    OPC_EmitConvertToTarget6, ///< Emit Target* form of recorded operand 6.
    OPC_EmitConvertToTarget7, ///< Emit Target* form of recorded operand 7.
    OPC_EmitMergeInputChains, ///< Emit a TokenFactor merging recorded chains.
    OPC_EmitMergeInputChains1_0, ///< Merge one input chain from recorded node 0.
    OPC_EmitMergeInputChains1_1, ///< Merge one input chain from recorded node 1.
    OPC_EmitMergeInputChains1_2, ///< Merge one input chain from recorded node 2.
    OPC_EmitCopyToReg, ///< Emit a CopyToReg for a recorded value.
    OPC_EmitCopyToReg0, ///< Emit CopyToReg for recorded value 0.
    OPC_EmitCopyToReg1, ///< Emit CopyToReg for recorded value 1.
    OPC_EmitCopyToReg2, ///< Emit CopyToReg for recorded value 2.
    OPC_EmitCopyToReg3, ///< Emit CopyToReg for recorded value 3.
    OPC_EmitCopyToReg4, ///< Emit CopyToReg for recorded value 4.
    OPC_EmitCopyToReg5, ///< Emit CopyToReg for recorded value 5.
    OPC_EmitCopyToReg6, ///< Emit CopyToReg for recorded value 6.
    OPC_EmitCopyToReg7, ///< Emit CopyToReg for recorded value 7.
    OPC_EmitCopyToRegTwoByte, ///< Emit CopyToReg with a two-byte register index.
    OPC_EmitNodeXForm, ///< Emit a value produced by an SDNodeXForm.
    OPC_EmitNode, ///< Emit a new target machine node.
    OPC_EmitNodeByHwMode, ///< Emit a machine node with hw-mode result types.
    // Space-optimized forms that implicitly encode number of result VTs.
    OPC_EmitNode0, ///< Emit a machine node with 0 results.
    OPC_EmitNode1, ///< Emit a machine node with 1 result.
    OPC_EmitNode2, ///< Emit a machine node with 2 results.
    // Space-optimized forms that implicitly encode EmitNodeInfo.
    OPC_EmitNode1None, ///< Emit a 1-result node with no chain/glue flags.
    OPC_EmitNode2None, ///< Emit a 2-result node with no chain/glue flags.
    OPC_EmitNode0Chain, ///< Emit a 0-result node that has a chain.
    OPC_EmitNode1Chain, ///< Emit a 1-result node that has a chain.
    OPC_EmitNode2Chain, ///< Emit a 2-result node that has a chain.
    OPC_MorphNodeTo, ///< Morph the matched node into a target machine node.
    OPC_MorphNodeToByHwMode, ///< Morph into a machine node with hw-mode types.
    // Space-optimized forms that implicitly encode number of result VTs.
    OPC_MorphNodeTo0, ///< Morph into a machine node with 0 results.
    OPC_MorphNodeTo1, ///< Morph into a machine node with 1 result.
    OPC_MorphNodeTo2, ///< Morph into a machine node with 2 results.
    // Space-optimized forms that implicitly encode EmitNodeInfo.
    OPC_MorphNodeTo1None, ///< Morph to a 1-result node with no chain/glue flags.
    OPC_MorphNodeTo2None, ///< Morph to a 2-result node with no chain/glue flags.
    OPC_MorphNodeTo0Chain, ///< Morph to a 0-result node that has a chain.
    OPC_MorphNodeTo1Chain, ///< Morph to a 1-result node that has a chain.
    OPC_MorphNodeTo2Chain, ///< Morph to a 2-result node that has a chain.
    OPC_MorphNodeTo1GlueInput, ///< Morph to a 1-result node with glue input.
    OPC_MorphNodeTo2GlueInput, ///< Morph to a 2-result node with glue input.
    OPC_MorphNodeTo1GlueOutput, ///< Morph to a 1-result node with glue output.
    OPC_MorphNodeTo2GlueOutput, ///< Morph to a 2-result node with glue output.
    OPC_CompleteMatch, ///< Finish the match and update uses of the matched node.
    // Contains 32-bit offset in table for pattern being selected
    OPC_Coverage ///< Record pattern coverage using a 32-bit table offset.
  };

  /// Flags describing chain/glue/variadic shape for EmitNode / MorphNodeTo.
  enum {
    OPFL_None = 0,       ///< Node has no chain or glue input and isn't variadic.
    OPFL_Chain = 1,      ///< Node has a chain input.
    OPFL_GlueInput = 2,  ///< Node has a glue input.
    OPFL_GlueOutput = 4, ///< Node has a glue output.
    OPFL_MemRefs = 8,    ///< Node gets accumulated MemRefs.
    OPFL_Variadic0 = 1 << 4, ///< Node is variadic, root has 0 fixed inputs.
    OPFL_Variadic1 = 2 << 4, ///< Node is variadic, root has 1 fixed inputs.
    OPFL_Variadic2 = 3 << 4, ///< Node is variadic, root has 2 fixed inputs.
    OPFL_Variadic3 = 4 << 4, ///< Node is variadic, root has 3 fixed inputs.
    OPFL_Variadic4 = 5 << 4, ///< Node is variadic, root has 4 fixed inputs.
    OPFL_Variadic5 = 6 << 4, ///< Node is variadic, root has 5 fixed inputs.
    OPFL_Variadic6 = 7 << 4, ///< Node is variadic, root has 6 fixed inputs.
    OPFL_Variadic7 = 8 << 4, ///< Node is variadic, root has 7 fixed inputs.

    OPFL_VariadicInfo = 15 << 4 ///< Mask for extracting the OPFL_VariadicN bits.
  };

  /// Return how many fixed arity values to skip when copying from a variadic root.
  ///
  /// Transforms an EmitNode flags word into the number of fixed arity values
  /// that should be skipped when copying from the root.
  ///
  /// \param Flags EmitNode / MorphNodeTo flags word containing OPFL_VariadicN.
  /// \return Number of fixed arity values to skip, or -1 if not variadic.
  static inline int getNumFixedFromVariadicInfo(unsigned Flags) {
    return ((Flags&OPFL_VariadicInfo) >> 4)-1;
  }


protected:
  /// DAGSize - Size of DAG being instruction selected.
  ///
  unsigned DAGSize = 0;

  /// Replace all uses of the old node value \p F with the new value \p T.
  ///
  /// \param F Value whose uses should be rewritten.
  /// \param T Replacement value.
  void ReplaceUses(SDValue F, SDValue T) {
    CurDAG->ReplaceAllUsesOfValueWith(F, T);
    EnforceNodeIdInvariant(T.getNode());
  }

  /// Replace all uses of \p Num old values in \p F with the new values in \p T.
  ///
  /// \param F Array of values whose uses should be rewritten.
  /// \param T Array of replacement values.
  /// \param Num Number of value pairs to replace.
  void ReplaceUses(const SDValue *F, const SDValue *T, unsigned Num) {
    CurDAG->ReplaceAllUsesOfValuesWith(F, T, Num);
    for (unsigned i = 0; i < Num; ++i)
      EnforceNodeIdInvariant(T[i].getNode());
  }

  /// Replace all uses of the old node \p F with the new node \p T.
  ///
  /// \param F Node whose uses should be rewritten.
  /// \param T Replacement node.
  void ReplaceUses(SDNode *F, SDNode *T) {
    CurDAG->ReplaceAllUsesWith(F, T);
    EnforceNodeIdInvariant(T);
  }

  /// Replace all uses of \p F with \p T, then remove \p F from the DAG.
  ///
  /// \param F Node to replace and remove.
  /// \param T Replacement node.
  void ReplaceNode(SDNode *F, SDNode *T) {
    CurDAG->ReplaceAllUsesWith(F, T);
    EnforceNodeIdInvariant(T);
    CurDAG->RemoveDeadNode(F);
  }

  /// Select memory operands of an INLINEASM node, rewriting \p Ops in place.
  ///
  /// Calls to this are automatically generated by tblgen. Others should not
  /// call it.
  ///
  /// \param Ops INLINEASM operands to rewrite; memory operands are selected.
  /// \param DL Source location for newly created nodes.
  void SelectInlineAsmMemoryOperands(std::vector<SDValue> &Ops,
                                     const SDLoc &DL);

  /// Return the TableGen pattern text for matcher coverage index \p index.
  ///
  /// \param index Coverage / pattern table index.
  /// \return Pattern text for the matcher coverage entry at \p index.
  virtual StringRef getPatternForIndex(unsigned index) {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  /// Return the TableGen include path for pattern instantiation index \p index.
  ///
  /// \param index Coverage / pattern table index.
  /// \return Include path string for the pattern at \p index.
  virtual StringRef getIncludePathForIndex(unsigned index) {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  /// Return true if the current function should be optimized for code size.
  ///
  /// \param MF Machine function being selected (unused; uses CurDAG).
  /// \return True if the current function should be optimized for size.
  bool shouldOptForSize(const MachineFunction *MF) const {
    return CurDAG->shouldOptForSize();
  }

public:
  /// Return true if AND of \p LHS with \p RHS matches desired mask \p DesiredMaskS.
  ///
  /// Generated by tblgen. Tolerates DAG combiner simplifications of the mask.
  ///
  /// \param LHS Left-hand operand of the AND.
  /// \param RHS Constant right-hand mask actually present in the DAG.
  /// \param DesiredMaskS Mask value specified in the TableGen pattern.
  /// \return True if the AND matches \p DesiredMaskS.
  bool CheckAndMask(SDValue LHS, ConstantSDNode *RHS,
                    int64_t DesiredMaskS) const;
  /// Return true if OR of \p LHS with \p RHS matches desired mask \p DesiredMaskS.
  ///
  /// Generated by tblgen. Tolerates DAG combiner simplifications of the mask.
  ///
  /// \param LHS Left-hand operand of the OR.
  /// \param RHS Constant right-hand mask actually present in the DAG.
  /// \param DesiredMaskS Mask value specified in the TableGen pattern.
  /// \return True if the OR matches \p DesiredMaskS.
  bool CheckOrMask(SDValue LHS, ConstantSDNode *RHS,
                    int64_t DesiredMaskS) const;


  /// Run TableGen pattern predicate \p PredNo and return whether it succeeds.
  ///
  /// Generated by tblgen in the target. The predicate number is a private
  /// implementation detail of the generated matcher code.
  ///
  /// \param PredNo Pattern predicate index to evaluate.
  /// \return True if the pattern predicate succeeds.
  virtual bool CheckPatternPredicate(unsigned PredNo) const {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  /// Run TableGen node predicate \p PredNo on \p Op and return whether it succeeds.
  ///
  /// Generated by tblgen in the target. The predicate number is a private
  /// implementation detail of the generated matcher code.
  ///
  /// \param Op Node value to test.
  /// \param PredNo Node predicate index to evaluate.
  /// \return True if the node predicate succeeds.
  virtual bool CheckNodePredicate(SDValue Op, unsigned PredNo) const {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  /// Run TableGen node predicate \p PredNo on \p Op with recorded \p Operands.
  ///
  /// Generated by tblgen in the target. The predicate number is a private
  /// implementation detail of the generated matcher code.
  ///
  /// \param Op Node value to test.
  /// \param PredNo Node predicate index to evaluate.
  /// \param Operands Previously recorded operand values for the predicate.
  /// \return True if the node predicate succeeds.
  virtual bool
  CheckNodePredicateWithOperands(SDValue Op, unsigned PredNo,
                                 ArrayRef<SDValue> Operands) const {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  /// Match complex pattern \p PatternNo rooted at \p N and append results.
  ///
  /// Generated by tblgen in the target.
  ///
  /// \param Root Root of the selection match.
  /// \param Parent Parent of \p N in the DAG, if any.
  /// \param N Value being matched by the complex pattern.
  /// \param PatternNo Complex pattern index to run.
  /// \param Result Receives matched values and their producing nodes.
  /// \return True if the complex pattern matched.
  virtual bool CheckComplexPattern(SDNode *Root, SDNode *Parent, SDValue N,
                                   unsigned PatternNo,
                        SmallVectorImpl<std::pair<SDValue, SDNode*> > &Result) {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  /// Apply TableGen SDNodeXForm \p XFormNo to value \p V.
  ///
  /// \param V Input value to transform.
  /// \param XFormNo Index of the generated SDNodeXForm.
  /// \return Transformed SDValue.
  virtual SDValue RunSDNodeXForm(SDValue V, unsigned XFormNo) {
    llvm_unreachable("Tblgen should generate this!");
  }

  /// Return the MVT for TableGen type index \p Index under the current HwMode.
  ///
  /// \param Index TableGen type-by-hw-mode table index.
  /// \return The MVT selected for \p Index under the current hardware mode.
  virtual MVT getValueTypeForHwMode(unsigned Index) const {
    llvm_unreachable("Tblgen should generate the implementation of this!");
  }

  /// Interpret TableGen matcher bytecode and select \p NodeToMatch.
  ///
  /// \param NodeToMatch DAG node to select.
  /// \param MatcherTable TableGen-generated matcher opcode stream.
  /// \param TableSize Size in bytes of \p MatcherTable.
  /// \param OperandLists Operand-list table referenced by emit opcodes.
  void SelectCodeCommon(SDNode *NodeToMatch, const uint8_t *MatcherTable,
                        unsigned TableSize, const uint8_t *OperandLists);

  /// Return true if complex patterns for this target can mutate the
  /// DAG.
  ///
  /// \return True if complex-pattern matchers may mutate the DAG.
  virtual bool ComplexPatternFuncMutatesDAG() const {
    return false;
  }

  /// Return whether the node may raise an FP exception.
  ///
  /// \param Node DAG node to inspect for floating-point exception behavior.
  /// \return True if \p Node may raise a floating-point exception.
  bool mayRaiseFPException(SDNode *Node) const;

  /// Return true if OR node \p N is equivalent to an ADD of a stack offset.
  ///
  /// \param N OR node to test.
  /// \return True if \p N is an OR equivalent to adding a stack offset.
  bool isOrEquivalentToAdd(const SDNode *N) const;

private:

  // Calls to these functions are generated by tblgen.
  void Select_INLINEASM(SDNode *N);
  void Select_READ_REGISTER(SDNode *Op);
  void Select_WRITE_REGISTER(SDNode *Op);
  void Select_UNDEF(SDNode *N);
  void Select_FAKE_USE(SDNode *N);
  void Select_RELOC_NONE(SDNode *N);
  void CannotYetSelect(SDNode *N);

  void Select_FREEZE(SDNode *N);
  void Select_ARITH_FENCE(SDNode *N);
  void Select_MEMBARRIER(SDNode *N);

  void Select_CONVERGENCECTRL_ANCHOR(SDNode *N);
  void Select_CONVERGENCECTRL_ENTRY(SDNode *N);
  void Select_CONVERGENCECTRL_LOOP(SDNode *N);

  void pushStackMapLiveVariable(SmallVectorImpl<SDValue> &Ops, SDValue Operand,
                                SDLoc DL);
  void Select_STACKMAP(SDNode *N);
  void Select_PATCHPOINT(SDNode *N);

  void Select_JUMP_TABLE_DEBUG_INFO(SDNode *N);

private:
  void DoInstructionSelection();
  SDNode *MorphNode(SDNode *Node, unsigned TargetOpc, SDVTList VTList,
                    ArrayRef<SDValue> Ops, unsigned EmitNodeInfo);

  /// Prepares the landing pad to take incoming values or do other EH
  /// personality specific tasks. Returns true if the block should be
  /// instruction selected, false if no code should be emitted for it.
  bool PrepareEHLandingPad();

  // Mark and Report IPToState for each Block under AsynchEH
  void reportIPToStateForBlocks(MachineFunction *Fn);

  /// Perform instruction selection on all basic blocks in the function.
  void SelectAllBasicBlocks(const Function &Fn);

  /// Perform instruction selection on a single basic block, for
  /// instructions between \p Begin and \p End.  \p HadTailCall will be set
  /// to true if a call in the block was translated as a tail call.
  void SelectBasicBlock(BasicBlock::const_iterator Begin,
                        BasicBlock::const_iterator End,
                        bool &HadTailCall);
  void FinishBasicBlock();

  void CodeGenAndEmitDAG();

  /// Generate instructions for lowering the incoming arguments of the
  /// given function.
  void LowerArguments(const Function &F);

  void ComputeLiveOutVRegInfo();

  /// Create the scheduler. If a specific scheduler was specified
  /// via the SchedulerRegistry, use it, otherwise select the
  /// one preferred by the target.
  ///
  ScheduleDAGSDNodes *CreateScheduler();

  /// OpcodeOffset - This is a cache used to dispatch efficiently into isel
  /// state machines that start with a OPC_SwitchOpcode node.
  std::vector<unsigned> OpcodeOffset;

  void UpdateChains(SDNode *NodeToMatch, SDValue InputChain,
                    SmallVectorImpl<SDNode *> &ChainNodesMatched,
                    bool isMorphNodeTo);
};

/// Legacy pass-manager wrapper that owns a SelectionDAGISel and runs it.
class LLVM_ABI SelectionDAGISelLegacy : public MachineFunctionPass {
  std::unique_ptr<SelectionDAGISel> Selector;

public:
  /// Construct a legacy SelectionDAG ISel pass with pass ID \p ID.
  ///
  /// \param ID Pass identifier used by the legacy pass manager.
  /// \param S SelectionDAG instruction selector to own and run.
  SelectionDAGISelLegacy(char &ID, std::unique_ptr<SelectionDAGISel> S);

  /// Destroy the legacy SelectionDAG ISel pass and its owned selector.
  ~SelectionDAGISelLegacy() override = default;

  /// Declare analyses required and preserved by SelectionDAG ISel.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Run SelectionDAG instruction selection on machine function \p MF.
  ///
  /// \param MF Machine function to select.
  /// \return True if the machine function was modified.
  bool runOnMachineFunction(MachineFunction &MF) override;
};

/// New pass-manager wrapper that runs SelectionDAG instruction selection.
class SelectionDAGISelPass
    : public RequiredPassInfoMixin<SelectionDAGISelPass> {
  std::unique_ptr<SelectionDAGISel> Selector;

protected:
  /// Construct a pass that owns instruction selector \p Selector.
  ///
  /// \param Selector SelectionDAG instruction selector to run.
  SelectionDAGISelPass(std::unique_ptr<SelectionDAGISel> Selector)
      : Selector(std::move(Selector)) {}

public:
  /// Run SelectionDAG instruction selection on machine function \p MF.
  ///
  /// \param MF Machine function to select.
  /// \param MFAM Analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};
}

#endif /* LLVM_CODEGEN_SELECTIONDAGISEL_H */
