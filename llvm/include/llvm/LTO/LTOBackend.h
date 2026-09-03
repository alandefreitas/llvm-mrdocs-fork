//===-LTOBackend.h - LLVM Link Time Optimizer Backend ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the "backend" phase of LTO, i.e. it performs
// optimization and code generation on a loaded module. It is generally used
// internally by the LTO class but can also be used independently, for example
// to implement a standalone ThinLTO backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LTOBACKEND_H
#define LLVM_LTO_LTOBACKEND_H

#include "llvm/ADT/MapVector.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/IPO/FunctionImport.h"

namespace llvm {

class BitcodeModule;
class Error;
class Module;
class Target;

namespace lto {

/// Runs middle-end LTO optimizations on \p Mod.
/// @param Conf LTO configuration controlling optimization.
/// @param TM Target machine used by optimization passes.
/// @param Task Task identifier passed to hooks and diagnostics.
/// @param Mod Module to optimize.
/// @param IsThinLTO Whether this is a ThinLTO optimization pipeline.
/// @param ExportSummary Combined index used for ThinLTO export decisions, or
/// null.
/// @param ImportSummary Combined index used for ThinLTO import decisions, or
/// null.
/// @param CmdArgs Command-line arguments optionally embedded in the module.
/// @param BitcodeLibFuncs Names of bitcode runtime library functions.
/// @return True if optimization completed and the post-opt hook (if any)
/// succeeded.
LLVM_ABI bool opt(const Config &Conf, TargetMachine *TM, unsigned Task,
                  Module &Mod, bool IsThinLTO,
                  ModuleSummaryIndex *ExportSummary,
                  const ModuleSummaryIndex *ImportSummary,
                  const std::vector<uint8_t> &CmdArgs,
                  ArrayRef<StringRef> BitcodeLibFuncs);

/// Runs a regular LTO backend. The regular LTO backend can also act as the
/// regular LTO phase of ThinLTO, which may need to access the combined index.
/// @param C LTO configuration for optimization and code generation.
/// @param AddStream Callback that creates an output stream for generated code.
/// @param ParallelCodeGenParallelismLevel Number of parallel code-generation
/// partitions; 1 disables splitting.
/// @param M Module to optimize and code-generate.
/// @param CombinedIndex Combined module summary index for the link.
/// @param BitcodeLibFuncs Names of bitcode runtime library functions.
/// @return Success, or an error from optimization or code generation.
LLVM_ABI Error backend(const Config &C, AddStreamFn AddStream,
                       unsigned ParallelCodeGenParallelismLevel, Module &M,
                       ModuleSummaryIndex &CombinedIndex,
                       ArrayRef<StringRef> BitcodeLibFuncs);

/// Runs a ThinLTO backend.
///
/// If \p ModuleMap is not nullptr, all the module files to be imported have
/// already been mapped to memory and the corresponding BitcodeModule objects
/// are saved in the ModuleMap. If \p ModuleMap is nullptr, module files will
/// be mapped to memory on demand and at any given time during importing, only
/// one source module will be kept open at the most. If \p CodeGenOnly is true,
/// the backend will skip optimization and only perform code generation. If
/// \p IRAddStream is not nullptr, it will be called just before code generation
/// to serialize the optimized IR.
/// @param C LTO configuration for optimization and code generation.
/// @param Task Task identifier for this ThinLTO backend partition.
/// @param AddStream Callback that creates an output stream for generated code.
/// @param M Module to optimize, import into, and code-generate.
/// @param CombinedIndex Combined module summary index for the link.
/// @param ImportList Map of source modules and GUIDs to import into \p M.
/// @param DefinedGlobals Summaries of globals defined in \p M.
/// @param ModuleMap Optional map of already-loaded BitcodeModule objects keyed
/// by module path; null loads modules on demand.
/// @param CodeGenOnly Whether to skip optimization and only run code
/// generation.
/// @param BitcodeLibFuncs Names of bitcode runtime library functions.
/// @param IRAddStream Optional callback invoked to serialize optimized IR
/// before code generation.
/// @param CmdArgs Command-line arguments optionally embedded in the module.
/// @return Success, or an error from importing, optimization, or code
/// generation.
LLVM_ABI Error thinBackend(
    const Config &C, unsigned Task, AddStreamFn AddStream, Module &M,
    const ModuleSummaryIndex &CombinedIndex,
    const FunctionImporter::ImportMapTy &ImportList,
    const GVSummaryMapTy &DefinedGlobals,
    MapVector<StringRef, BitcodeModule> *ModuleMap, bool CodeGenOnly,
    ArrayRef<StringRef> BitcodeLibFuncs, AddStreamFn IRAddStream = nullptr,
    const std::vector<uint8_t> &CmdArgs = std::vector<uint8_t>());

/// Finalizes and flushes an optimization remarks output file.
/// @param DiagOutputFile Handle of the remarks file to finalize, or null.
/// @return Success after flushing, or success immediately if \p DiagOutputFile
/// is null.
LLVM_ABI Error finalizeOptimizationRemarks(LLVMRemarkFileHandle DiagOutputFile);

/// Returns the BitcodeModule that is ThinLTO.
/// @param BMs Bitcode modules to search for the ThinLTO module.
/// @return Pointer to the ThinLTO BitcodeModule, or nullptr if none is found.
LLVM_ABI BitcodeModule *findThinLTOModule(MutableArrayRef<BitcodeModule> BMs);

/// Variant of the above.
/// @param MBRef Memory buffer containing bitcode modules to search.
/// @return The ThinLTO BitcodeModule, or an error if none is found.
LLVM_ABI Expected<BitcodeModule> findThinLTOModule(MemoryBufferRef MBRef);

/// Distributed ThinLTO: collect the referenced modules based on
/// module summary and initialize ImportList.
/// @param M Importing module whose identifier is used to skip self-summaries.
/// @param CombinedIndex Combined module summary index providing import
/// candidates.
/// @param ImportList Import map populated with GUIDs to import into \p M.
/// @return True on success, false if the operation failed.
LLVM_ABI bool initImportList(const Module &M,
                             const ModuleSummaryIndex &CombinedIndex,
                             FunctionImporter::ImportMapTy &ImportList);
}
}

#endif
