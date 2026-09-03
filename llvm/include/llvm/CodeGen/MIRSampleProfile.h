//===----- MIRSampleProfile.h: SampleFDO Support in MIR ---*- c++ -*-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the supoorting functions for machine level Sample FDO
// loader. This is used in Flow Sensitive SampelFDO.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRSAMPLEPROFILE_H
#define LLVM_CODEGEN_MIRSAMPLEPROFILE_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Support/Discriminator.h"
#include <memory>
#include <string>

namespace llvm {
class AnalysisUsage;
class MachineBlockFrequencyInfo;
class MachineFunction;
class Module;

/// Namespace for the virtual file system interface.
namespace vfs {
class FileSystem;
} // namespace vfs

using namespace sampleprof;

/// Opaque implementation of the MIR sample profile loader.
class MIRProfileLoader;
/// Machine function pass that loads SampleFDO profile data into MIR.
class LLVM_ABI MIRProfileLoaderPass : public MachineFunctionPass {
  MachineFunction *MF;
  std::string ProfileFileName;
  FSDiscriminatorPass P;
  unsigned LowBit;
  unsigned HighBit;

public:
  /// Pass identification, replacement for typeid.
  static char ID;
  /// Construct a MIR sample profile loader pass.
  ///
  /// FS bits will only use the '1' bits in the Mask.
  ///
  /// \param FileName Path to the sample profile data file.
  /// \param RemappingFileName Optional profile remapping file, or empty if
  /// unused.
  /// \param P Which flow-sensitive discriminator pass loaded samples apply to.
  /// \param FS Virtual filesystem used to read \p FileName and
  /// \p RemappingFileName.
  MIRProfileLoaderPass(std::string FileName = "",
                       std::string RemappingFileName = "",
                       FSDiscriminatorPass P = FSDiscriminatorPass::Pass1,
                       IntrusiveRefCntPtr<vfs::FileSystem> FS = nullptr);

  /// getMachineFunction - Return the last machine function computed.
  ///
  /// \return The last machine function computed, or null if none.
  const MachineFunction *getMachineFunction() const { return MF; }

  /// Return the name of this pass.
  ///
  /// \return The name of this pass.
  StringRef getPassName() const override { return "SampleFDO loader in MIR"; }

private:
  void init(MachineFunction &MF);
  bool runOnMachineFunction(MachineFunction &) override;
  bool doInitialization(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  std::unique_ptr<MIRProfileLoader> MIRSampleLoader;
  /// Hold the information of the basic block frequency.
  MachineBlockFrequencyInfo *MBFI;
};

} // namespace llvm

#endif // LLVM_CODEGEN_MIRSAMPLEPROFILE_H
