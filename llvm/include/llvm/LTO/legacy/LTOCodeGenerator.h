//===-LTOCodeGenerator.h - LLVM Link Time Optimizer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the LTOCodeGenerator class.
//
//   LTO compilation consists of three phases: Pre-IPO, IPO and Post-IPO.
//
//   The Pre-IPO phase compiles source code into bitcode file. The resulting
// bitcode files, along with object files and libraries, will be fed to the
// linker to through the IPO and Post-IPO phases. By using obj-file extension,
// the resulting bitcode file disguises itself as an object file, and therefore
// obviates the need of writing a special set of the make-rules only for LTO
// compilation.
//
//   The IPO phase perform inter-procedural analyses and optimizations, and
// the Post-IPO consists two sub-phases: intra-procedural scalar optimizations
// (SOPT), and intra-procedural target-dependent code generator (CG).
//
//   As of this writing, we don't separate IPO and the Post-IPO SOPT. They
// are intermingled together, and are driven by a single pass manager (see
// PassManagerBuilder::populateLTOPassManager()).
//   FIXME: populateLTOPassManager no longer exists.
//
//   The "LTOCodeGenerator" is the driver for the IPO and Post-IPO stages.
// The "CodeGenerator" here is bit confusing. Don't confuse the "CodeGenerator"
// with the machine specific code generator.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LEGACY_LTOCODEGENERATOR_H
#define LLVM_LTO_LEGACY_LTOCODEGENERATOR_H

#include "llvm-c/lto.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/LTO/Config.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <string>
#include <vector>

namespace llvm {
template <typename T> class ArrayRef;
class LLVMContext;
class DiagnosticInfo;
class Linker;
class Mangler;
class MemoryBuffer;
class TargetLibraryInfo;
class TargetMachine;
class raw_ostream;
class raw_pwrite_stream;

/// Enable global value internalization in LTO.
LLVM_ABI extern cl::opt<bool> EnableLTOInternalization;

//===----------------------------------------------------------------------===//
/// C++ class which implements the opaque lto_code_gen_t type.
///
struct LTOCodeGenerator {
  /// Return a string identifying the LTO library version.
  /// \return Version string identifying the LTO library.
  LLVM_ABI static const char *getVersionString();

  /// Construct an LTO code generator that uses the given LLVM context.
  /// \param Context LLVM context shared by merged modules.
  LLVM_ABI LTOCodeGenerator(LLVMContext &Context);
  /// Destroy this LTO code generator.
  LLVM_ABI ~LTOCodeGenerator();

  /// Merge given module.  Return true on success.
  ///
  /// Resets \a HasVerifiedInput.
  /// \param Mod Module to merge into the combined LTO module.
  /// \return True on success.
  LLVM_ABI bool addModule(struct LTOModule *Mod);

  /// Set the destination module.
  ///
  /// Resets \a HasVerifiedInput.
  /// \param M Module that becomes the merged module for this generator.
  LLVM_ABI void setModule(std::unique_ptr<LTOModule> M);

  /// Record assembly undefined references from the given module.
  /// \param Mod Module whose asm undefined refs are merged in.
  LLVM_ABI void setAsmUndefinedRefs(struct LTOModule *Mod);
  /// Set the target options used for code generation.
  /// \param Options Target options to apply.
  LLVM_ABI void setTargetOptions(const TargetOptions &Options);
  /// Set the debug-info model to emit.
  /// \param Debug Debug-info model (none or DWARF).
  LLVM_ABI void setDebugInfo(lto_debug_model Debug);
  /// Set the relocation model for code generation.
  /// \param Model Relocation model, or nullopt for the target default.
  void setCodePICModel(std::optional<Reloc::Model> Model) {
    Config.RelocModel = Model;
  }

  /// Set the file type to be emitted (assembly or object code).
  /// The default is CodeGenFileType::ObjectFile.
  /// \param FT Output file type (assembly or object code).
  void setFileType(CodeGenFileType FT) { Config.CGFileType = FT; }

  /// Set the target CPU name for code generation.
  /// \param MCpu Target CPU name.
  void setCpu(StringRef MCpu) { Config.CPU = std::string(MCpu); }
  /// Set the target feature attributes for code generation.
  /// \param MAttrs Target feature attribute strings.
  void setAttrs(std::vector<std::string> MAttrs) {
    Config.MAttrs = std::move(MAttrs);
  }
  /// Set the optimization level used for LTO and code generation.
  /// \param OptLevel Optimization level (typically 0-3).
  LLVM_ABI void setOptLevel(unsigned OptLevel);

  /// Set whether LTO should internalize unreferenced globals.
  /// \param Value Whether internalization is enabled.
  void setShouldInternalize(bool Value) { ShouldInternalize = Value; }
  /// Set whether to embed use-lists when writing bitcode.
  /// \param Value Whether use-lists should be embedded in bitcode.
  void setShouldEmbedUselists(bool Value) { ShouldEmbedUselists = Value; }
  /// Set the path where IR is saved before optimization, if any.
  /// \param Value Output path for pre-optimization IR, or empty to disable.
  void setSaveIRBeforeOptPath(std::string Value) {
    SaveIRBeforeOptPath = std::move(Value);
  }

  /// Restore linkage of globals
  ///
  /// When set, the linkage of globals will be restored prior to code
  /// generation. That is, a global symbol that had external linkage prior to
  /// LTO will be emitted with external linkage again; and a local will remain
  /// local. Note that this option only affects the end result - globals may
  /// still be internalized in the process of LTO and may be modified and/or
  /// deleted where legal.
  ///
  /// The default behavior will internalize globals (unless on the preserve
  /// list) and, if parallel code generation is enabled, will externalize
  /// all locals.
  /// \param Value Whether to restore original global linkages before codegen.
  void setShouldRestoreGlobalsLinkage(bool Value) {
    ShouldRestoreGlobalsLinkage = Value;
  }

  /// Record a symbol that must be preserved during LTO.
  /// \param Sym Symbol name that must not be internalized or discarded.
  void addMustPreserveSymbol(StringRef Sym) { MustPreserveSymbols.insert(Sym); }

  /// Pass options to the driver and optimization passes.
  ///
  /// These options are not necessarily for debugging purpose (the function
  /// name is misleading).  This function should be called before
  /// LTOCodeGenerator::compilexxx(), and
  /// LTOCodeGenerator::writeMergedModules().
  /// \param Opts Driver and optimization pass options to record.
  LLVM_ABI void setCodeGenDebugOptions(ArrayRef<StringRef> Opts);

  /// Parse the options set in setCodeGenDebugOptions.
  ///
  /// Like \a setCodeGenDebugOptions(), this must be called before
  /// LTOCodeGenerator::compilexxx() and
  /// LTOCodeGenerator::writeMergedModules().
  LLVM_ABI void parseCodeGenDebugOptions();

  /// Write the merged module to the file specified by the given path.  Return
  /// true on success.
  ///
  /// Calls \a verifyMergedModuleOnce().
  /// \param Path File path to write the merged bitcode module to.
  /// \return True on success.
  LLVM_ABI bool writeMergedModules(StringRef Path);

  /// Compile the merged module into a *single* output file; the path to output
  /// file is returned to the caller via argument "name". Return true on
  /// success.
  ///
  /// \note It is up to the linker to remove the intermediate output file.  Do
  /// not try to remove the object file in LTOCodeGenerator's destructor as we
  /// don't who (LTOCodeGenerator or the output file) will last longer.
  /// \param Name Set on success to the path of the generated output file.
  /// \return True on success.
  LLVM_ABI bool compile_to_file(const char **Name);

  /// Optimize and compile the merged module into a single in-memory buffer.
  ///
  /// As with compile_to_file(), this compiles the merged module into a single
  /// output file. Instead of returning the output file path to the caller
  /// (linker), it brings the output to a buffer and returns the buffer to the
  /// caller. This function should delete the intermediate file once its
  /// content is brought to memory. Returns null if the compilation was not
  /// successful.
  /// \return In-memory buffer with the compiled output, or null on failure.
  LLVM_ABI std::unique_ptr<MemoryBuffer> compile();

  /// Optimizes the merged module.  Returns true on success.
  ///
  /// Calls \a verifyMergedModuleOnce().
  /// \return True on success.
  LLVM_ABI bool optimize();

  /// Compile the merged optimized module into a single in-memory buffer.
  ///
  /// Brings the output to a buffer and returns it to the caller. Returns null
  /// if the compilation was not successful.
  /// \return In-memory buffer with the compiled output, or null on failure.
  LLVM_ABI std::unique_ptr<MemoryBuffer> compileOptimized();

  /// Compile the merged optimized module into one or more output streams.
  ///
  /// Emits \p ParallelismLevel output files each representing a linkable
  /// partition of the module. If out contains more than one element, code
  /// generation is done in parallel with \p ParallelismLevel threads. Output
  /// files will be written to the streams created using the \p AddStream
  /// callback. Returns true on success.
  ///
  /// Calls \a verifyMergedModuleOnce().
  /// \param AddStream Callback that creates an output stream per partition.
  /// \param ParallelismLevel Number of parallel code-generation partitions.
  /// \return True on success.
  LLVM_ABI bool compileOptimized(AddStreamFn AddStream,
                                 unsigned ParallelismLevel);

  /// Enable the Freestanding mode: indicate that the optimizer should not
  /// assume builtins are present on the target.
  /// \param Enabled Whether freestanding mode is enabled.
  void setFreestanding(bool Enabled) { Config.Freestanding = Enabled; }

  /// Enable or disable IR verification during LTO.
  /// \param Value Whether verification should be disabled.
  void setDisableVerify(bool Value) { Config.DisableVerify = Value; }

  /// Enable or disable debug output from the pass manager.
  /// \param Enabled Whether pass-manager debugging is enabled.
  void setDebugPassManager(bool Enabled) { Config.DebugPassManager = Enabled; }

  /// Set the external diagnostic handler callback and its context.
  /// \param DiagHandler Callback invoked for each diagnostic, or null to clear.
  /// \param Ctxt User context pointer passed to \p DiagHandler.
  LLVM_ABI void setDiagnosticHandler(lto_diagnostic_handler_t DiagHandler,
                                     void *Ctxt);

  /// Return the LLVM context used by this code generator.
  /// \return LLVM context used by this code generator.
  LLVMContext &getContext() { return Context; }

  /// Destroy the merged module, releasing ownership of it.
  void resetMergedModule() { MergedModule.reset(); }
  /// Forward an LLVM diagnostic to the external LTO diagnostic handler.
  /// \param DI Diagnostic to report.
  LLVM_ABI void DiagnosticHandler(const DiagnosticInfo &DI);

private:
  /// Verify the merged module on first call.
  ///
  /// Sets \a HasVerifiedInput on first call and doesn't run again on the same
  /// input.
  void verifyMergedModuleOnce();

  bool compileOptimizedToFile(const char **Name);
  void restoreLinkageForExternals();
  void applyScopeRestrictions();
  void preserveDiscardableGVs(
      Module &TheModule,
      llvm::function_ref<bool(const GlobalValue &)> mustPreserveGV);

  bool determineTarget();
  std::unique_ptr<TargetMachine> createTargetMachine();

  bool useAIXSystemAssembler();
  bool runAIXSystemAssembler(SmallString<128> &AssemblyFile);

  void emitError(const std::string &ErrMsg);
  void emitWarning(const std::string &ErrMsg);

  void finishOptimizationRemarks();

  LLVMContext &Context;
  std::unique_ptr<Module> MergedModule;
  std::unique_ptr<Linker> TheLinker;
  std::unique_ptr<TargetMachine> TargetMach;
  bool EmitDwarfDebugInfo = false;
  bool ScopeRestrictionsDone = false;
  bool HasVerifiedInput = false;
  StringSet<> MustPreserveSymbols;
  StringSet<> AsmUndefinedRefs;
  StringMap<GlobalValue::LinkageTypes> ExternalSymbols;
  std::vector<std::string> CodegenOptions;
  std::string FeatureStr;
  std::string NativeObjectPath;
  const Target *MArch = nullptr;
  lto_diagnostic_handler_t DiagHandler = nullptr;
  void *DiagContext = nullptr;
  bool ShouldInternalize = EnableLTOInternalization;
  bool ShouldEmbedUselists = false;
  bool ShouldRestoreGlobalsLinkage = false;
  LLVMRemarkFileHandle DiagnosticOutputFile;
  std::unique_ptr<ToolOutputFile> StatsFile = nullptr;
  std::string SaveIRBeforeOptPath;

  lto::Config Config;
};

/// A convenience function that calls cl::ParseCommandLineOptions on the given
/// set of options.
/// \param Options Command-line option strings to parse (without argv[0]).
LLVM_ABI void parseCommandLineOptions(std::vector<std::string> &Options);
} // namespace llvm
#endif
