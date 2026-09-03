//===- llvm/InitializePasses.h - Initialize All Passes ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations for the pass initialization routines
// for the entire LLVM project.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_INITIALIZEPASSES_H
#define LLVM_INITIALIZEPASSES_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class PassRegistry;

/// Initialize all passes linked into the Core library.
/// @param Registry Pass registry used to register the passes.
LLVM_ABI void initializeCore(PassRegistry &Registry);

/// Initialize all passes linked into the TransformUtils library.
/// @param Registry Pass registry used to register the passes.
LLVM_ABI void initializeTransformUtils(PassRegistry &Registry);

/// Initialize all passes linked into the ScalarOpts library.
/// @param Registry Pass registry used to register the passes.
LLVM_ABI void initializeScalarOpts(PassRegistry &Registry);

/// Initialize all passes linked into the Vectorize library.
/// @param Registry Pass registry used to register the passes.
LLVM_ABI void initializeVectorization(PassRegistry &Registry);

/// Initialize all passes linked into the InstCombine library.
/// @param Registry Pass registry used to register the passes.
LLVM_ABI void initializeInstCombine(PassRegistry &Registry);

/// Initialize all passes linked into the IPO library.
/// @param Registry Pass registry used to register the passes.
LLVM_ABI void initializeIPO(PassRegistry &Registry);

/// Initialize all passes linked into the Analysis library.
/// @param Registry Pass registry used to register the passes.
LLVM_ABI void initializeAnalysis(PassRegistry &Registry);

/// Initialize all passes linked into the CodeGen library.
/// @param Registry Pass registry used to register the passes.
LLVM_ABI void initializeCodeGen(PassRegistry &Registry);

/// Initialize all passes linked into the GlobalISel library.
/// @param Registry Pass registry used to register the passes.
LLVM_ABI void initializeGlobalISel(PassRegistry &Registry);

/// Initialize all passes linked into the CodeGen library.
/// @param Registry Pass registry used to register the passes.
LLVM_ABI void initializeTarget(PassRegistry &Registry);

/// Initialize the AAResultsWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeAAResultsWrapperPassPass(PassRegistry &Registry);
/// Initialize the AlwaysInlinerLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeAlwaysInlinerLegacyPassPass(PassRegistry &Registry);
/// Initialize the AssignmentTrackingAnalysis pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeAssignmentTrackingAnalysisPass(PassRegistry &Registry);
/// Initialize the AssumptionCacheTracker pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeAssumptionCacheTrackerPass(PassRegistry &Registry);
/// Initialize the AtomicExpandLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeAtomicExpandLegacyPass(PassRegistry &Registry);
/// Initialize the BasicBlockMatchingAndInference pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBasicBlockMatchingAndInferencePass(PassRegistry &Registry);
/// Initialize the BasicBlockPathCloning pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBasicBlockPathCloningPass(PassRegistry &Registry);
/// Initialize the InsertCodePrefetch pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeInsertCodePrefetchPass(PassRegistry &Registry);
/// Initialize the BasicBlockSectionsProfileReaderWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeBasicBlockSectionsProfileReaderWrapperPassPass(PassRegistry &Registry);
/// Initialize the BasicBlockSections pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBasicBlockSectionsPass(PassRegistry &Registry);
/// Initialize the BarrierNoop pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBarrierNoopPass(PassRegistry &Registry);
/// Initialize the BasicAAWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBasicAAWrapperPassPass(PassRegistry &Registry);
/// Initialize the BlockFrequencyInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBlockFrequencyInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineBlockHashInfo pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineBlockHashInfoPass(PassRegistry &Registry);
/// Initialize the BranchFolderLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBranchFolderLegacyPass(PassRegistry &Registry);
/// Initialize the BranchProbabilityInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBranchProbabilityInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the BranchRelaxationLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBranchRelaxationLegacyPass(PassRegistry &Registry);
/// Initialize the BreakCriticalEdges pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBreakCriticalEdgesPass(PassRegistry &Registry);
/// Initialize the BreakFalseDepsLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeBreakFalseDepsLegacyPass(PassRegistry &Registry);
/// Initialize the CanonicalizeFreezeInLoops pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCanonicalizeFreezeInLoopsPass(PassRegistry &Registry);
/// Initialize the CFGSimplifyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCFGSimplifyPassPass(PassRegistry &Registry);
/// Initialize the CFGuard pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCFGuardPass(PassRegistry &Registry);
/// Initialize the CFGuardLongjmp pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCFGuardLongjmpPass(PassRegistry &Registry);
/// Initialize the CFIFixupLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCFIFixupLegacyPass(PassRegistry &Registry);
/// Initialize the CFIInstrInserterLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCFIInstrInserterLegacyPass(PassRegistry &Registry);
/// Initialize the CallGraphDOTPrinter pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCallGraphDOTPrinterPass(PassRegistry &Registry);
/// Initialize the CallGraphViewer pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCallGraphViewerPass(PassRegistry &Registry);
/// Initialize the CallGraphWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCallGraphWrapperPassPass(PassRegistry &Registry);
/// Initialize the CheckDebugMachineModuleLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCheckDebugMachineModuleLegacyPass(PassRegistry &Registry);
/// Initialize the CodeGenPrepareLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCodeGenPrepareLegacyPassPass(PassRegistry &Registry);
/// Initialize the ComplexDeinterleavingLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeComplexDeinterleavingLegacyPassPass(PassRegistry &Registry);
/// Initialize the ConstantHoistingLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeConstantHoistingLegacyPassPass(PassRegistry &Registry);
/// Initialize the CycleInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeCycleInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the DAE pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDAEPass(PassRegistry &Registry);
/// Initialize the DCELegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDCELegacyPassPass(PassRegistry &Registry);
/// Initialize the DSELegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDSELegacyPassPass(PassRegistry &Registry);
/// Initialize the DXILMetadataAnalysisWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDXILMetadataAnalysisWrapperPassPass(PassRegistry &Registry);
/// Initialize the DXILMetadataAnalysisWrapperPrinter pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDXILMetadataAnalysisWrapperPrinterPass(PassRegistry &Registry);
/// Initialize the DXILResourceBindingWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDXILResourceBindingWrapperPassPass(PassRegistry &Registry);
/// Initialize the DXILResourceTypeWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDXILResourceTypeWrapperPassPass(PassRegistry &Registry);
/// Initialize the DXILResourceWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDXILResourceWrapperPassPass(PassRegistry &Registry);
/// Initialize the DeadMachineInstructionElim pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDeadMachineInstructionElimPass(PassRegistry &Registry);
/// Initialize the DebugifyMachineModule pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDebugifyMachineModulePass(PassRegistry &Registry);
/// Initialize the DependenceAnalysisWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDependenceAnalysisWrapperPassPass(PassRegistry &Registry);
/// Initialize the DetectDeadLanesLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDetectDeadLanesLegacyPass(PassRegistry &Registry);
/// Initialize the DomOnlyPrinterWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDomOnlyPrinterWrapperPassPass(PassRegistry &Registry);
/// Initialize the DomOnlyViewerWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDomOnlyViewerWrapperPassPass(PassRegistry &Registry);
/// Initialize the DomPrinterWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDomPrinterWrapperPassPass(PassRegistry &Registry);
/// Initialize the DomViewerWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDomViewerWrapperPassPass(PassRegistry &Registry);
/// Initialize the DominanceFrontierWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDominanceFrontierWrapperPassPass(PassRegistry &Registry);
/// Initialize the DominatorTreeWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDominatorTreeWrapperPassPass(PassRegistry &Registry);
/// Initialize the DummyCGSCCPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDummyCGSCCPassPass(PassRegistry &Registry);
/// Initialize the DwarfEHPrepareLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeDwarfEHPrepareLegacyPassPass(PassRegistry &Registry);
/// Initialize the EarlyCSELegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeEarlyCSELegacyPassPass(PassRegistry &Registry);
/// Initialize the EarlyCSEMemSSALegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeEarlyCSEMemSSALegacyPassPass(PassRegistry &Registry);
/// Initialize the EarlyIfConverterLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeEarlyIfConverterLegacyPass(PassRegistry &Registry);
/// Initialize the EarlyIfPredicator pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeEarlyIfPredicatorPass(PassRegistry &Registry);
/// Initialize the EarlyMachineLICM pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeEarlyMachineLICMPass(PassRegistry &Registry);
/// Initialize the EarlyTailDuplicateLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeEarlyTailDuplicateLegacyPass(PassRegistry &Registry);
/// Initialize the EdgeBundlesWrapperLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeEdgeBundlesWrapperLegacyPass(PassRegistry &Registry);
/// Initialize the EHContGuardTargetsLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeEHContGuardTargetsLegacyPass(PassRegistry &Registry);
/// Initialize the ExpandIRInstsLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeExpandIRInstsLegacyPassPass(PassRegistry &Registry);
/// Initialize the ExpandPostRALegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeExpandPostRALegacyPass(PassRegistry &Registry);
/// Initialize the ExpandReductions pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeExpandReductionsPass(PassRegistry &Registry);
/// Initialize the ExpandVariadics pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeExpandVariadicsPass(PassRegistry &Registry);
/// Initialize the ExternalAAWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeExternalAAWrapperPassPass(PassRegistry &Registry);
/// Initialize the FEntryInserterLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeFEntryInserterLegacyPass(PassRegistry &Registry);
/// Initialize the FinalizeISel pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeFinalizeISelPass(PassRegistry &Registry);
/// Initialize the FixIrreducible pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeFixIrreduciblePass(PassRegistry &Registry);
/// Initialize the FixupStatepointCallerSavedLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeFixupStatepointCallerSavedLegacyPass(PassRegistry &Registry);
/// Initialize the FlattenCFGLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeFlattenCFGLegacyPassPass(PassRegistry &Registry);
/// Initialize the FuncletLayoutLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeFuncletLayoutLegacyPass(PassRegistry &Registry);
/// Initialize the GCEmptyBasicBlocksLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeGCEmptyBasicBlocksLegacyPass(PassRegistry &Registry);
/// Initialize the GCMachineCodeAnalysis pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeGCMachineCodeAnalysisPass(PassRegistry &Registry);
/// Initialize the GCModuleInfo pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeGCModuleInfoPass(PassRegistry &Registry);
/// Initialize the GVNLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeGVNLegacyPassPass(PassRegistry &Registry);
/// Initialize the GlobalDCELegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeGlobalDCELegacyPassPass(PassRegistry &Registry);
/// Initialize the GlobalMergeFuncPassWrapper pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeGlobalMergeFuncPassWrapperPass(PassRegistry &Registry);
/// Initialize the GlobalMerge pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeGlobalMergePass(PassRegistry &Registry);
/// Initialize the GlobalsAAWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeGlobalsAAWrapperPassPass(PassRegistry &Registry);
/// Initialize the HardwareLoopsLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeHardwareLoopsLegacyPass(PassRegistry &Registry);
/// Initialize the LibcallLoweringInfoWrapper pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLibcallLoweringInfoWrapperPass(PassRegistry &Registry);
/// Initialize the MIRProfileLoaderPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMIRProfileLoaderPassPass(PassRegistry &Registry);
/// Initialize the IRTranslatorLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeIRTranslatorLegacyPass(PassRegistry &Registry);
/// Initialize the IVUsersWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeIVUsersWrapperPassPass(PassRegistry &Registry);
/// Initialize the IfConverter pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeIfConverterPass(PassRegistry &Registry);
/// Initialize the ImmutableModuleSummaryIndexWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeImmutableModuleSummaryIndexWrapperPassPass(PassRegistry &Registry);
/// Initialize the ImplicitNullChecksLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeImplicitNullChecksLegacyPass(PassRegistry &Registry);
/// Initialize the IndirectBrExpandLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeIndirectBrExpandLegacyPassPass(PassRegistry &Registry);
/// Initialize the InferAddressSpaces pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeInferAddressSpacesPass(PassRegistry &Registry);
/// Initialize the InlineAsmPrepare pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeInlineAsmPreparePass(PassRegistry &Registry);
/// Initialize the InstSimplifyLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeInstSimplifyLegacyPassPass(PassRegistry &Registry);
/// Initialize the InstructionCombiningPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeInstructionCombiningPassPass(PassRegistry &Registry);
/// Initialize the InstructionSelectLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeInstructionSelectLegacyPass(PassRegistry &Registry);
/// Initialize the InterleavedAccess pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeInterleavedAccessPass(PassRegistry &Registry);
/// Initialize the InterleavedLoadCombine pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeInterleavedLoadCombinePass(PassRegistry &Registry);
/// Initialize the JMCInstrumenter pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeJMCInstrumenterPass(PassRegistry &Registry);
/// Initialize the MachineKCFILegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineKCFILegacyPass(PassRegistry &Registry);
/// Initialize the LCSSAVerificationPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLCSSAVerificationPassPass(PassRegistry &Registry);
/// Initialize the LCSSAWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLCSSAWrapperPassPass(PassRegistry &Registry);
/// Initialize the LazyBFIPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLazyBFIPassPass(PassRegistry &Registry);
/// Initialize the LazyBlockFrequencyInfoPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLazyBlockFrequencyInfoPassPass(PassRegistry &Registry);
/// Initialize the LazyBranchProbabilityInfoPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLazyBranchProbabilityInfoPassPass(PassRegistry &Registry);
/// Initialize the LazyMachineBlockFrequencyInfoPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLazyMachineBlockFrequencyInfoPassPass(PassRegistry &Registry);
/// Initialize the LazyValueInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLazyValueInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the LegacyLICMPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLegacyLICMPassPass(PassRegistry &Registry);
/// Initialize the LegalizerLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLegalizerLegacyPass(PassRegistry &Registry);
/// Initialize the GISelCSEAnalysisWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeGISelCSEAnalysisWrapperPassPass(PassRegistry &Registry);
/// Initialize the GISelValueTrackingAnalysisLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeGISelValueTrackingAnalysisLegacyPass(PassRegistry &Registry);
/// Initialize the LiveDebugValuesLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLiveDebugValuesLegacyPass(PassRegistry &Registry);
/// Initialize the LiveDebugVariablesWrapperLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLiveDebugVariablesWrapperLegacyPass(PassRegistry &Registry);
/// Initialize the LiveIntervalsWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLiveIntervalsWrapperPassPass(PassRegistry &Registry);
/// Initialize the LiveRangeShrink pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLiveRangeShrinkPass(PassRegistry &Registry);
/// Initialize the LiveRegMatrixWrapperLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLiveRegMatrixWrapperLegacyPass(PassRegistry &Registry);
/// Initialize the LiveStacksWrapperLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLiveStacksWrapperLegacyPass(PassRegistry &Registry);
/// Initialize the LiveVariablesWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLiveVariablesWrapperPassPass(PassRegistry &Registry);
/// Initialize the LoadStoreOptLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLoadStoreOptLegacyPass(PassRegistry &Registry);
/// Initialize the LoadStoreVectorizerLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLoadStoreVectorizerLegacyPassPass(PassRegistry &Registry);
/// Initialize the LocalStackSlotPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLocalStackSlotPassPass(PassRegistry &Registry);
/// Initialize the LocalizerLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLocalizerLegacyPass(PassRegistry &Registry);
/// Initialize the LogicalSROALegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLogicalSROALegacyPassPass(PassRegistry &Registry);
/// Initialize the LoopDataPrefetchLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLoopDataPrefetchLegacyPassPass(PassRegistry &Registry);
/// Initialize the LoopInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLoopInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the LoopPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLoopPassPass(PassRegistry &Registry);
/// Initialize the LoopSimplify pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLoopSimplifyPass(PassRegistry &Registry);
/// Initialize the LoopStrengthReduce pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLoopStrengthReducePass(PassRegistry &Registry);
/// Initialize the LoopTermFold pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLoopTermFoldPass(PassRegistry &Registry);
/// Initialize the LoopUnroll pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLoopUnrollPass(PassRegistry &Registry);
/// Initialize the LowerAtomicLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLowerAtomicLegacyPassPass(PassRegistry &Registry);
/// Initialize the LowerEmuTLS pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLowerEmuTLSPass(PassRegistry &Registry);
/// Initialize the LowerGlobalDtorsLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLowerGlobalDtorsLegacyPassPass(PassRegistry &Registry);
/// Initialize the LowerIntrinsics pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLowerIntrinsicsPass(PassRegistry &Registry);
/// Initialize the LowerInvokeLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLowerInvokeLegacyPassPass(PassRegistry &Registry);
/// Initialize the LowerSwitchLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeLowerSwitchLegacyPassPass(PassRegistry &Registry);
/// Initialize the MIRAddFSDiscriminators pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMIRAddFSDiscriminatorsPass(PassRegistry &Registry);
/// Initialize the MIRCanonicalizer pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMIRCanonicalizerPass(PassRegistry &Registry);
/// Initialize the MIRNamer pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMIRNamerPass(PassRegistry &Registry);
/// Initialize the MIRPrintingPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMIRPrintingPassPass(PassRegistry &Registry);
/// Initialize the MachineBlockFrequencyInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeMachineBlockFrequencyInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineBlockPlacementLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineBlockPlacementLegacyPass(PassRegistry &Registry);
/// Initialize the MachineBlockPlacementStatsLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineBlockPlacementStatsLegacyPass(PassRegistry &Registry);
/// Initialize the MachineBranchProbabilityInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeMachineBranchProbabilityInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineCFGPrinterLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineCFGPrinterLegacyPass(PassRegistry &Registry);
/// Initialize the MachineCSELegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineCSELegacyPass(PassRegistry &Registry);
/// Initialize the MachineCombinerLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineCombinerLegacyPass(PassRegistry &Registry);
/// Initialize the MachineCopyPropagationLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineCopyPropagationLegacyPass(PassRegistry &Registry);
/// Initialize the MachineCycleInfoPrinterLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineCycleInfoPrinterLegacyPass(PassRegistry &Registry);
/// Initialize the MachineCycleInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineCycleInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineDominanceFrontierWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineDominanceFrontierWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineDominatorTreeWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineDominatorTreeWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineFunctionPrinterPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineFunctionPrinterPassPass(PassRegistry &Registry);
/// Initialize the MachineFunctionSplitter pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineFunctionSplitterPass(PassRegistry &Registry);
/// Initialize the MachineLateInstrsCleanupLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineLateInstrsCleanupLegacyPass(PassRegistry &Registry);
/// Initialize the MachineLICM pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineLICMPass(PassRegistry &Registry);
/// Initialize the MachineLoopInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineLoopInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineModuleInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineModuleInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineOptimizationRemarkEmitterPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeMachineOptimizationRemarkEmitterPassPass(PassRegistry &Registry);
/// Initialize the MachineOutliner pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineOutlinerPass(PassRegistry &Registry);
/// Initialize the StaticDataProfileInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStaticDataProfileInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the StaticDataAnnotatorLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStaticDataAnnotatorLegacyPass(PassRegistry &Registry);
/// Initialize the MachinePipeliner pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachinePipelinerPass(PassRegistry &Registry);
/// Initialize the MachinePostDominatorTreeWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachinePostDominatorTreeWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineRegionInfoPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineRegionInfoPassPass(PassRegistry &Registry);
/// Initialize the MachineRegisterClassInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineRegisterClassInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineSanitizerBinaryMetadataLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeMachineSanitizerBinaryMetadataLegacyPass(PassRegistry &Registry);
/// Initialize the MIR2VecVocabLegacyAnalysis pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMIR2VecVocabLegacyAnalysisPass(PassRegistry &Registry);
/// Initialize the MIR2VecVocabPrinterLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMIR2VecVocabPrinterLegacyPassPass(PassRegistry &Registry);
/// Initialize the MIR2VecPrinterLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMIR2VecPrinterLegacyPassPass(PassRegistry &Registry);
/// Initialize the MachineSchedulerLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineSchedulerLegacyPass(PassRegistry &Registry);
/// Initialize the MachineSinkingLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineSinkingLegacyPass(PassRegistry &Registry);
/// Initialize the MachineTraceMetricsWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineTraceMetricsWrapperPassPass(PassRegistry &Registry);
/// Initialize the MachineUniformityInfoPrinterPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineUniformityInfoPrinterPassPass(PassRegistry &Registry);
/// Initialize the MachineUniformityAnalysisPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineUniformityAnalysisPassPass(PassRegistry &Registry);
/// Initialize the MachineVerifierLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMachineVerifierLegacyPassPass(PassRegistry &Registry);
/// Initialize the MemoryDependenceWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMemoryDependenceWrapperPassPass(PassRegistry &Registry);
/// Initialize the MemorySSAWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeMemorySSAWrapperPassPass(PassRegistry &Registry);
/// Initialize the ModuleSummaryIndexWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeModuleSummaryIndexWrapperPassPass(PassRegistry &Registry);
/// Initialize the ModuloScheduleTest pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeModuloScheduleTestPass(PassRegistry &Registry);
/// Initialize the NaryReassociateLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeNaryReassociateLegacyPassPass(PassRegistry &Registry);
/// Initialize the ObjCARCContractLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeObjCARCContractLegacyPassPass(PassRegistry &Registry);
/// Initialize the OptimizationRemarkEmitterWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeOptimizationRemarkEmitterWrapperPassPass(PassRegistry &Registry);
/// Initialize the OptimizePHIsLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeOptimizePHIsLegacyPass(PassRegistry &Registry);
/// Initialize the PEILegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePEILegacyPass(PassRegistry &Registry);
/// Initialize the PHIElimination pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePHIEliminationPass(PassRegistry &Registry);
/// Initialize the PartiallyInlineLibCallsLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePartiallyInlineLibCallsLegacyPassPass(PassRegistry &Registry);
/// Initialize the PatchableFunctionLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePatchableFunctionLegacyPass(PassRegistry &Registry);
/// Initialize the PeepholeOptimizerLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePeepholeOptimizerLegacyPass(PassRegistry &Registry);
/// Initialize the PhiValuesWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePhiValuesWrapperPassPass(PassRegistry &Registry);
/// Initialize the PhysicalRegisterUsageInfoWrapperLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializePhysicalRegisterUsageInfoWrapperLegacyPass(PassRegistry &Registry);
/// Initialize the PlaceBackedgeSafepointsLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePlaceBackedgeSafepointsLegacyPassPass(PassRegistry &Registry);
/// Initialize the PostDomOnlyPrinterWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePostDomOnlyPrinterWrapperPassPass(PassRegistry &Registry);
/// Initialize the PostDomOnlyViewerWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePostDomOnlyViewerWrapperPassPass(PassRegistry &Registry);
/// Initialize the PostDomPrinterWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePostDomPrinterWrapperPassPass(PassRegistry &Registry);
/// Initialize the PostDomViewerWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePostDomViewerWrapperPassPass(PassRegistry &Registry);
/// Initialize the PostDominatorTreeWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePostDominatorTreeWrapperPassPass(PassRegistry &Registry);
/// Initialize the PostInlineEntryExitInstrumenter pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePostInlineEntryExitInstrumenterPass(PassRegistry &Registry);
/// Initialize the PostMachineSchedulerLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePostMachineSchedulerLegacyPass(PassRegistry &Registry);
/// Initialize the PostRAHazardRecognizerLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePostRAHazardRecognizerLegacyPass(PassRegistry &Registry);
/// Initialize the PostRAMachineSinkingLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePostRAMachineSinkingLegacyPass(PassRegistry &Registry);
/// Initialize the PostRASchedulerLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePostRASchedulerLegacyPass(PassRegistry &Registry);
/// Initialize the PreISelIntrinsicLoweringLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePreISelIntrinsicLoweringLegacyPassPass(PassRegistry &Registry);
/// Initialize the PrintFunctionPassWrapper pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePrintFunctionPassWrapperPass(PassRegistry &Registry);
/// Initialize the PrintModulePassWrapper pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePrintModulePassWrapperPass(PassRegistry &Registry);
/// Initialize the ProcessImplicitDefsLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeProcessImplicitDefsLegacyPass(PassRegistry &Registry);
/// Initialize the ProfileSummaryInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeProfileSummaryInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the PromoteLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePromoteLegacyPassPass(PassRegistry &Registry);
/// Initialize the RABasic pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRABasicPass(PassRegistry &Registry);
/// Initialize the PseudoProbeInserter pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializePseudoProbeInserterPass(PassRegistry &Registry);
/// Initialize the RAGreedyLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRAGreedyLegacyPass(PassRegistry &Registry);
/// Initialize the ReachingDefInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeReachingDefInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the ReassociateLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeReassociateLegacyPassPass(PassRegistry &Registry);
/// Initialize the RegAllocEvictionAdvisorAnalysisLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeRegAllocEvictionAdvisorAnalysisLegacyPass(PassRegistry &Registry);
/// Initialize the RegAllocFast pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegAllocFastPass(PassRegistry &Registry);
/// Initialize the RegAllocPriorityAdvisorAnalysisLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeRegAllocPriorityAdvisorAnalysisLegacyPass(PassRegistry &Registry);
/// Initialize the RegAllocScoring pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegAllocScoringPass(PassRegistry &Registry);
/// Initialize the RegBankSelectLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegBankSelectLegacyPass(PassRegistry &Registry);
/// Initialize the RegToMemWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegToMemWrapperPassPass(PassRegistry &Registry);
/// Initialize the RegUsageInfoCollectorLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegUsageInfoCollectorLegacyPass(PassRegistry &Registry);
/// Initialize the RegUsageInfoPropagationLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegUsageInfoPropagationLegacyPass(PassRegistry &Registry);
/// Initialize the RegionInfoPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegionInfoPassPass(PassRegistry &Registry);
/// Initialize the RegionOnlyPrinter pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegionOnlyPrinterPass(PassRegistry &Registry);
/// Initialize the RegionOnlyViewer pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegionOnlyViewerPass(PassRegistry &Registry);
/// Initialize the RegionPrinter pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegionPrinterPass(PassRegistry &Registry);
/// Initialize the RegionViewer pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegionViewerPass(PassRegistry &Registry);
/// Initialize the RegisterCoalescerLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRegisterCoalescerLegacyPass(PassRegistry &Registry);
/// Initialize the RemoveLoadsIntoFakeUsesLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRemoveLoadsIntoFakeUsesLegacyPass(PassRegistry &Registry);
/// Initialize the RemoveRedundantDebugValuesLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRemoveRedundantDebugValuesLegacyPass(PassRegistry &Registry);
/// Initialize the RenameIndependentSubregsLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRenameIndependentSubregsLegacyPass(PassRegistry &Registry);
/// Initialize the ReplaceWithVeclibLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeReplaceWithVeclibLegacyPass(PassRegistry &Registry);
/// Initialize the ResetMachineFunction pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeResetMachineFunctionPass(PassRegistry &Registry);
/// Initialize the RuntimeLibraryInfoWrapper pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeRuntimeLibraryInfoWrapperPass(PassRegistry &Registry);
/// Initialize the SCEVAAWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeSCEVAAWrapperPassPass(PassRegistry &Registry);
/// Initialize the SROALegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeSROALegacyPassPass(PassRegistry &Registry);
/// Initialize the SafeStackLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeSafeStackLegacyPassPass(PassRegistry &Registry);
/// Initialize the SafepointIRVerifier pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeSafepointIRVerifierPass(PassRegistry &Registry);
/// Initialize the SelectOptimize pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeSelectOptimizePass(PassRegistry &Registry);
/// Initialize the ScalarEvolutionWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeScalarEvolutionWrapperPassPass(PassRegistry &Registry);
/// Initialize the ScalarizeMaskedMemIntrinLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeScalarizeMaskedMemIntrinLegacyPassPass(PassRegistry &Registry);
/// Initialize the ScalarizerLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeScalarizerLegacyPassPass(PassRegistry &Registry);
/// Initialize the ScavengerTest pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeScavengerTestPass(PassRegistry &Registry);
/// Initialize the ScopedNoAliasAAWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeScopedNoAliasAAWrapperPassPass(PassRegistry &Registry);
/// Initialize the SeparateConstOffsetFromGEPLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeSeparateConstOffsetFromGEPLegacyPassPass(PassRegistry &Registry);
/// Initialize the ShadowStackGCLowering pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeShadowStackGCLoweringPass(PassRegistry &Registry);
/// Initialize the ShrinkWrapLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeShrinkWrapLegacyPass(PassRegistry &Registry);
/// Initialize the SinkingLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeSinkingLegacyPassPass(PassRegistry &Registry);
/// Initialize the SjLjEHPrepare pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeSjLjEHPreparePass(PassRegistry &Registry);
/// Initialize the SlotIndexesWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeSlotIndexesWrapperPassPass(PassRegistry &Registry);
/// Initialize the SpeculativeExecutionLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeSpeculativeExecutionLegacyPassPass(PassRegistry &Registry);
/// Initialize the SpillPlacementWrapperLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeSpillPlacementWrapperLegacyPass(PassRegistry &Registry);
/// Initialize the StackColoringLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStackColoringLegacyPass(PassRegistry &Registry);
/// Initialize the StackFrameLayoutAnalysisLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStackFrameLayoutAnalysisLegacyPass(PassRegistry &Registry);
/// Initialize the StaticDataSplitterLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStaticDataSplitterLegacyPass(PassRegistry &Registry);
/// Initialize the StackMapLiveness pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStackMapLivenessPass(PassRegistry &Registry);
/// Initialize the StackProtector pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStackProtectorPass(PassRegistry &Registry);
/// Initialize the StackSafetyGlobalInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStackSafetyGlobalInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the StackSafetyInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStackSafetyInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the StackSlotColoringLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStackSlotColoringLegacyPass(PassRegistry &Registry);
/// Initialize the StraightLineStrengthReduceLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeStraightLineStrengthReduceLegacyPassPass(PassRegistry &Registry);
/// Initialize the StripConvergenceIntrinsicsLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void
initializeStripConvergenceIntrinsicsLegacyPassPass(PassRegistry &Registry);
/// Initialize the StripDebugMachineModule pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStripDebugMachineModulePass(PassRegistry &Registry);
/// Initialize the StructurizeCFGLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeStructurizeCFGLegacyPassPass(PassRegistry &Registry);
/// Initialize the TailCallElim pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeTailCallElimPass(PassRegistry &Registry);
/// Initialize the TailDuplicateLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeTailDuplicateLegacyPass(PassRegistry &Registry);
/// Initialize the TargetLibraryInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeTargetLibraryInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the TargetPassConfig pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeTargetPassConfigPass(PassRegistry &Registry);
/// Initialize the TargetTransformInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeTargetTransformInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the TwoAddressInstructionLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeTwoAddressInstructionLegacyPassPass(PassRegistry &Registry);
/// Initialize the TypeBasedAAWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeTypeBasedAAWrapperPassPass(PassRegistry &Registry);
/// Initialize the TypePromotionLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeTypePromotionLegacyPass(PassRegistry &Registry);
/// Initialize the InitUndefLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeInitUndefLegacyPass(PassRegistry &Registry);
/// Initialize the UniformityInfoWrapperPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeUniformityInfoWrapperPassPass(PassRegistry &Registry);
/// Initialize the UnifyLoopExitsLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeUnifyLoopExitsLegacyPassPass(PassRegistry &Registry);
/// Initialize the UnpackMachineBundlesLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeUnpackMachineBundlesLegacyPass(PassRegistry &Registry);
/// Initialize the UnreachableBlockElimLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeUnreachableBlockElimLegacyPassPass(PassRegistry &Registry);
/// Initialize the UnreachableMachineBlockElimLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeUnreachableMachineBlockElimLegacyPass(PassRegistry &Registry);
/// Initialize the VerifierLegacyPass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeVerifierLegacyPassPass(PassRegistry &Registry);
/// Initialize the VirtRegMapWrapperLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeVirtRegMapWrapperLegacyPass(PassRegistry &Registry);
/// Initialize the VirtRegRewriterLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeVirtRegRewriterLegacyPass(PassRegistry &Registry);
/// Initialize the WasmEHPrepare pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeWasmEHPreparePass(PassRegistry &Registry);
/// Initialize the WindowsSecureHotPatching pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeWindowsSecureHotPatchingPass(PassRegistry &Registry);
/// Initialize the WinEHPrepare pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeWinEHPreparePass(PassRegistry &Registry);
/// Initialize the WriteBitcodePass pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeWriteBitcodePassPass(PassRegistry &Registry);
/// Initialize the XRayInstrumentationLegacy pass.
/// @param Registry Pass registry used to register the pass.
LLVM_ABI void initializeXRayInstrumentationLegacyPass(PassRegistry &Registry);

} // end namespace llvm

#endif // LLVM_INITIALIZEPASSES_H
