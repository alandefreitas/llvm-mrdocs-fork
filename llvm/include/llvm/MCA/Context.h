//===---------------------------- Context.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a class for holding ownership of various simulated
/// hardware units.  A Context also provides a utility routine for constructing
/// a default out-of-order pipeline with fetch, dispatch, execute, and retire
/// stages.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_CONTEXT_H
#define LLVM_MCA_CONTEXT_H

#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/CustomBehaviour.h"
#include "llvm/MCA/HardwareUnits/HardwareUnit.h"
#include "llvm/MCA/Pipeline.h"
#include "llvm/MCA/SourceMgr.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {
namespace mca {

/// This is a convenience struct to hold the parameters necessary for creating
/// the pre-built "default" out-of-order pipeline.
struct PipelineOptions {
  /// Construct pipeline options from the given hardware and analysis settings.
  /// \param UOPQSize Number of entries in the micro-op queue.
  /// \param DecThr Maximum decoder throughput in instructions per cycle.
  /// \param DW Maximum number of micro-ops dispatched per cycle.
  /// \param RFS Maximum number of physical registers for register mappings.
  /// \param LQS Size of the load queue.
  /// \param SQS Size of the store queue.
  /// \param NoAlias If true, assume loads and stores do not alias.
  /// \param ShouldEnableBottleneckAnalysis If true, enable bottleneck analysis.
  PipelineOptions(unsigned UOPQSize, unsigned DecThr, unsigned DW, unsigned RFS,
                  unsigned LQS, unsigned SQS, bool NoAlias,
                  bool ShouldEnableBottleneckAnalysis = false)
      : MicroOpQueueSize(UOPQSize), DecodersThroughput(DecThr),
        DispatchWidth(DW), RegisterFileSize(RFS), LoadQueueSize(LQS),
        StoreQueueSize(SQS), AssumeNoAlias(NoAlias),
        EnableBottleneckAnalysis(ShouldEnableBottleneckAnalysis) {}
  /// Number of entries in the micro-op queue.
  unsigned MicroOpQueueSize;
  /// Maximum decoder throughput in instructions per cycle.
  unsigned DecodersThroughput;
  /// Maximum number of micro-ops dispatched per cycle.
  unsigned DispatchWidth;
  /// Maximum number of physical registers which can be used for register
  /// mappings.
  unsigned RegisterFileSize;
  /// Size of the load queue.
  unsigned LoadQueueSize;
  /// Size of the store queue.
  unsigned StoreQueueSize;
  /// If true, assume that loads and stores do not alias.
  bool AssumeNoAlias;
  /// If true, enable bottleneck analysis during simulation.
  bool EnableBottleneckAnalysis;
};

/// Owns simulated hardware units and builds default MCA pipelines.
class Context {
  SmallVector<std::unique_ptr<HardwareUnit>, 4> Hardware;
  const MCRegisterInfo &MRI;
  const MCSubtargetInfo &STI;

public:
  /// Construct a context for the given register and subtarget info.
  /// \param R Target register information used by simulated units.
  /// \param S Subtarget information providing the scheduling model.
  Context(const MCRegisterInfo &R, const MCSubtargetInfo &S) : MRI(R), STI(S) {}
  /// Contexts are not copyable.
  /// \param C Unused; copy construction is deleted.
  Context(const Context &C) = delete;
  /// Contexts are not copy-assignable.
  /// \param C Unused; copy assignment is deleted.
  Context &operator=(const Context &C) = delete;

  /// Return the MC register information associated with this context.
  /// \return The MC register information used by this context.
  const MCRegisterInfo &getMCRegisterInfo() const { return MRI; }
  /// Return the MC subtarget information associated with this context.
  /// \return The MC subtarget information used by this context.
  const MCSubtargetInfo &getMCSubtargetInfo() const { return STI; }

  /// Take ownership of a simulated hardware unit.
  /// \param H Hardware unit to own for the lifetime of this context.
  void addHardwareUnit(std::unique_ptr<HardwareUnit> H) {
    Hardware.push_back(std::move(H));
  }

  /// Construct a basic pipeline for simulating an out-of-order pipeline.
  /// This pipeline consists of Fetch, Dispatch, Execute, and Retire stages.
  /// \param Opts Pipeline sizing and analysis options.
  /// \param SrcMgr Source of instructions to simulate.
  /// \param CB Target-specific custom behaviour hooks.
  /// \return A pipeline with Fetch, Dispatch, Execute, and Retire stages.
  LLVM_ABI std::unique_ptr<Pipeline>
  createDefaultPipeline(const PipelineOptions &Opts, SourceMgr &SrcMgr,
                        CustomBehaviour &CB);

  /// Construct a basic pipeline for simulating an in-order pipeline.
  /// This pipeline consists of Fetch, InOrderIssue, and Retire stages.
  /// \param Opts Pipeline sizing and analysis options.
  /// \param SrcMgr Source of instructions to simulate.
  /// \param CB Target-specific custom behaviour hooks.
  /// \return A pipeline with Fetch, InOrderIssue, and Retire stages.
  LLVM_ABI std::unique_ptr<Pipeline>
  createInOrderPipeline(const PipelineOptions &Opts, SourceMgr &SrcMgr,
                        CustomBehaviour &CB);
};

} // namespace mca
} // namespace llvm
#endif // LLVM_MCA_CONTEXT_H
