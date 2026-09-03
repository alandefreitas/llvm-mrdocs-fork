//===-- CodeGenTargetMachineImpl.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file describes the CodeGenTargetMachineImpl class, which
/// implements a set of functionality used by \c TargetMachine classes in
/// LLVM that make use of the target-independent code generator.
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_CODEGENTARGETMACHINEIMPL_H
#define LLVM_CODEGEN_CODEGENTARGETMACHINEIMPL_H
#include "llvm/Support/Compiler.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

/// Implements TargetMachine functionality for CodeGen-based targets.
///
/// Provides a set of functionality in the \c TargetMachine class for targets
/// that make use of the independent code generator (CodeGen) library. Must not
/// be used directly in code unless to inherit its implementation.
class LLVM_ABI CodeGenTargetMachineImpl : public TargetMachine {
protected: // Can only create subclasses.
  /// Construct a CodeGen-backed target machine for the given target.
  ///
  /// \param T Target description this machine is created for.
  /// \param DataLayoutString Data layout string used to build the DataLayout.
  /// \param TT Target triple describing the architecture and OS.
  /// \param CPU Target CPU name.
  /// \param FS Target feature string.
  /// \param Options Target-specific codegen options.
  /// \param RM Relocation model to use.
  /// \param CM Code model to use.
  /// \param OL Codegen optimization level.
  CodeGenTargetMachineImpl(const Target &T, StringRef DataLayoutString,
                           const Triple &TT, StringRef CPU, StringRef FS,
                           const TargetOptions &Options, Reloc::Model RM,
                           CodeModel::Model CM, CodeGenOptLevel OL);

  /// Initialize MCAsmInfo and related MC components for this target machine.
  void initAsmInfo();

  /// Reset internal state.
  virtual void reset() {};

public:
  /// Get a TargetTransformInfo implementation for the target.
  ///
  /// The TTI returned uses the common code generator to answer queries about
  /// the IR.
  ///
  /// \param F Function for which target transform info is requested.
  /// \return Target transform info for \p F based on the common code generator.
  TargetTransformInfo getTargetTransformInfo(const Function &F) const override;

  /// Create a pass configuration object to be used by addPassToEmitX methods
  /// for generating a pipeline of CodeGen passes.
  ///
  /// \param PM Pass manager that will own the created pass configuration.
  /// \return The newly created pass configuration for this target.
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  /// Add passes that emit the specified file from the given pass manager.
  ///
  /// Typically this will involve several steps of code generation. \p MMIWP is
  /// an optional parameter that, if set to non-nullptr, will be used to set the
  /// MachineModuleInfo for this PM.
  ///
  /// \param PM Pass manager that receives the codegen and emit passes.
  /// \param Out Stream that receives the primary output file.
  /// \param DwoOut Optional stream for DWARF split (DWO) output.
  /// \param FileType Kind of file to emit (assembly, object, etc.).
  /// \param DisableVerify Whether to skip IR/MIR verification.
  /// \param MMIWP Optional MachineModuleInfo wrapper to attach to \p PM.
  /// \return True if file emission is not supported for this target.
  bool
  addPassesToEmitFile(PassManagerBase &PM, raw_pwrite_stream &Out,
                      raw_pwrite_stream *DwoOut, CodeGenFileType FileType,
                      bool DisableVerify = true,
                      MachineModuleInfoWrapperPass *MMIWP = nullptr) override;

  /// Add passes that emit machine code for use with MCJIT.
  ///
  /// This method returns true if machine code is not supported. It fills the
  /// MCContext Ctx pointer which can be used to build custom MCStreamer.
  ///
  /// \param PM Pass manager that receives the codegen and emit passes.
  /// \param Ctx Set to the MCContext used for emission.
  /// \param Out Stream that receives the emitted machine code.
  /// \param DisableVerify Whether to skip IR/MIR verification.
  /// \return True if machine code emission is not supported.
  bool addPassesToEmitMC(PassManagerBase &PM, MCContext *&Ctx,
                         raw_pwrite_stream &Out,
                         bool DisableVerify = true) override;

  /// Adds an AsmPrinter pass to the pipeline that prints assembly or
  /// machine code from the MI representation.
  ///
  /// \param PM Pass manager that receives the AsmPrinter pass.
  /// \param Out Stream that receives the primary assembly or object output.
  /// \param DwoOut Optional stream for DWARF split (DWO) output.
  /// \param FileType Kind of file to emit (assembly, object, etc.).
  /// \param Context MC context used while printing.
  /// \return True if the AsmPrinter could not be added.
  bool addAsmPrinter(PassManagerBase &PM, raw_pwrite_stream &Out,
                     raw_pwrite_stream *DwoOut, CodeGenFileType FileType,
                     MCContext &Context) override;

  /// Create an MCStreamer for the requested output file type.
  ///
  /// \param Out Stream that receives the primary assembly or object output.
  /// \param DwoOut Optional stream for DWARF split (DWO) output.
  /// \param FileType Kind of file to emit (assembly, object, etc.).
  /// \param Ctx MC context used to build the streamer.
  /// \return The created streamer, or an error if creation fails.
  Expected<std::unique_ptr<MCStreamer>>
  createMCStreamer(raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
                   CodeGenFileType FileType, MCContext &Ctx) override;
};

/// Return the effective code model, or \p Default when \p CM has no value.
///
/// The tiny and kernel models will produce an error, so targets that support
/// them or require more complex codemodel selection logic should implement and
/// call their own getEffectiveCodeModel.
///
/// \param CM Optional requested code model.
/// \param Default Code model used when \p CM has no value.
/// \return The selected code model.
inline CodeModel::Model
getEffectiveCodeModel(std::optional<CodeModel::Model> CM,
                      CodeModel::Model Default) {
  if (CM) {
    // By default, targets do not support the tiny and kernel models.
    if (*CM == CodeModel::Tiny)
      reportFatalUsageError("Target does not support the tiny CodeModel");
    if (*CM == CodeModel::Kernel)
      reportFatalUsageError("Target does not support the kernel CodeModel");
    return *CM;
  }
  return Default;
}

} // namespace llvm

#endif
