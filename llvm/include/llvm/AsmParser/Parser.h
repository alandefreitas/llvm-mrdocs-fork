//===-- Parser.h - Parser for LLVM IR text assembly files -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  These classes are implemented by the lib/AsmParser library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ASMPARSER_PARSER_H
#define LLVM_ASMPARSER_PARSER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/AsmParser/AsmParserContext.h"
#include "llvm/Support/Compiler.h"
#include <memory>
#include <optional>

namespace llvm {

class Constant;
class DIExpression;
class LLVMContext;
class MemoryBufferRef;
class Module;
class ModuleSummaryIndex;
struct SlotMapping;
class SMDiagnostic;
class Type;

/// Callback that may override the data layout string found in LLVM assembly.
///
/// The first argument is the target triple. The second is the data layout
/// string from the input, or a default. That input layout is used if the
/// callback returns std::nullopt.
typedef llvm::function_ref<std::optional<std::string>(StringRef, StringRef)>
    DataLayoutCallbackTy;

/// Parse LLVM Assembly from a file.
///
/// This function is a main interface to the LLVM Assembly Parser. It parses
/// an ASCII file that (presumably) contains LLVM Assembly code. It returns a
/// Module (intermediate representation) with the corresponding features. Note
/// that this does not verify that the generated Module is valid, so you should
/// run the verifier after parsing the file to check that it is okay.
///
/// \param Filename The name of the file to parse
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
/// \return The parsed Module, or null on error.
LLVM_ABI std::unique_ptr<Module>
parseAssemblyFile(StringRef Filename, SMDiagnostic &Err, LLVMContext &Context,
                  SlotMapping *Slots = nullptr);

/// Parse LLVM Assembly from a string.
///
/// The function is a secondary interface to the LLVM Assembly Parser. It parses
/// an ASCII string that (presumably) contains LLVM Assembly code. It returns a
/// Module (intermediate representation) with the corresponding features. Note
/// that this does not verify that the generated Module is valid, so you should
/// run the verifier after parsing the file to check that it is okay.
///
/// \param AsmString The string containing assembly
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
/// \param ParserContext Optional context that records source locations.
/// \return The parsed Module, or null on error.
LLVM_ABI std::unique_ptr<Module>
parseAssemblyString(StringRef AsmString, SMDiagnostic &Err,
                    LLVMContext &Context, SlotMapping *Slots = nullptr,
                    AsmParserContext *ParserContext = nullptr);

/// Holds the Module and ModuleSummaryIndex returned by the interfaces
/// that parse both.
struct ParsedModuleAndIndex {
  /// The parsed Module, or null when only an index was produced.
  std::unique_ptr<Module> Mod;
  /// The parsed ModuleSummaryIndex, or null when none was present.
  std::unique_ptr<ModuleSummaryIndex> Index;
};

/// Parse LLVM Assembly and its module summary from a file.
///
/// This function is a main interface to the LLVM Assembly Parser. It parses
/// an ASCII file that (presumably) contains LLVM Assembly code, including
/// a module summary. It returns a Module (intermediate representation) and
/// a ModuleSummaryIndex with the corresponding features. Note that this does
/// not verify that the generated Module or Index are valid, so you should
/// run the verifier after parsing the file to check that they are okay.
///
/// \param Filename The name of the file to parse
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
/// \param DataLayoutCallback Override datalayout in the llvm assembly.
/// \return The parsed Module and ModuleSummaryIndex.
LLVM_ABI ParsedModuleAndIndex parseAssemblyFileWithIndex(
    StringRef Filename, SMDiagnostic &Err, LLVMContext &Context,
    SlotMapping *Slots = nullptr,
    DataLayoutCallbackTy DataLayoutCallback = [](StringRef, StringRef) {
      return std::nullopt;
    });

/// Parse an assembly file with index without upgrading debug info.
///
/// Only for use in llvm-as for testing; this does not produce a valid module.
///
/// \param Filename The name of the file to parse
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
/// \param DataLayoutCallback Override datalayout in the llvm assembly.
/// \return The parsed Module and ModuleSummaryIndex.
LLVM_ABI ParsedModuleAndIndex parseAssemblyFileWithIndexNoUpgradeDebugInfo(
    StringRef Filename, SMDiagnostic &Err, LLVMContext &Context,
    SlotMapping *Slots, DataLayoutCallbackTy DataLayoutCallback);

/// Parse a module summary index from an LLVM Assembly file.
///
/// This function is a main interface to the LLVM Assembly Parser. It parses
/// an ASCII file that (presumably) contains LLVM Assembly code for a module
/// summary. It returns a ModuleSummaryIndex with the corresponding features.
/// Note that this does not verify that the generated Index is valid, so you
/// should run the verifier after parsing the file to check that it is okay.
///
/// \param Filename The name of the file to parse
/// \param Err Error result info.
/// \return The parsed ModuleSummaryIndex, or null on error.
LLVM_ABI std::unique_ptr<ModuleSummaryIndex>
parseSummaryIndexAssemblyFile(StringRef Filename, SMDiagnostic &Err);

/// Parse a module summary index from an LLVM Assembly string.
///
/// The function is a secondary interface to the LLVM Assembly Parser. It parses
/// an ASCII string that (presumably) contains LLVM Assembly code for a module
/// summary. It returns a ModuleSummaryIndex with the corresponding features.
/// Note that this does not verify that the generated Index is valid, so you
/// should run the verifier after parsing the file to check that it is okay.
///
/// \param AsmString The string containing assembly
/// \param Err Error result info.
/// \return The parsed ModuleSummaryIndex, or null on error.
LLVM_ABI std::unique_ptr<ModuleSummaryIndex>
parseSummaryIndexAssemblyString(StringRef AsmString, SMDiagnostic &Err);

/// Parse LLVM Assembly from a MemoryBuffer.
///
/// parseAssemblyFile and parseAssemblyString are wrappers around this function.
///
/// \param F The MemoryBuffer containing assembly
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
/// \param DataLayoutCallback Override datalayout in the llvm assembly.
/// \param ParserContext Optional context that records source locations.
/// \return The parsed Module, or null on error.
LLVM_ABI std::unique_ptr<Module> parseAssembly(
    MemoryBufferRef F, SMDiagnostic &Err, LLVMContext &Context,
    SlotMapping *Slots = nullptr,
    DataLayoutCallbackTy DataLayoutCallback =
        [](StringRef, StringRef) { return std::nullopt; },
    AsmParserContext *ParserContext = nullptr);

/// Parse LLVM Assembly including the summary index from a MemoryBuffer.
///
/// parseAssemblyFileWithIndex is a wrapper around this function.
///
/// \param F The MemoryBuffer containing assembly with summary
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
/// \return The parsed Module and ModuleSummaryIndex.
LLVM_ABI ParsedModuleAndIndex
parseAssemblyWithIndex(MemoryBufferRef F, SMDiagnostic &Err,
                       LLVMContext &Context, SlotMapping *Slots = nullptr);

/// Parse LLVM Assembly for summary index from a MemoryBuffer.
///
/// parseSummaryIndexAssemblyFile is a wrapper around this function.
///
/// \param F The MemoryBuffer containing assembly with summary
/// \param Err Error result info.
/// \return The parsed ModuleSummaryIndex, or null on error.
LLVM_ABI std::unique_ptr<ModuleSummaryIndex>
parseSummaryIndexAssembly(MemoryBufferRef F, SMDiagnostic &Err);

/// Parse LLVM Assembly into an existing Module or ModuleSummaryIndex.
///
/// This function is the low-level interface to the LLVM Assembly Parser.
/// This is kept as an independent function instead of being inlined into
/// parseAssembly for the convenience of interactive users that want to add
/// recently parsed bits to an existing module.
///
/// \param F The MemoryBuffer containing assembly
/// \param M The module to add data to.
/// \param Index The index to add data to.
/// \param Err Error result info.
/// \param Slots The optional slot mapping that will be initialized during
///              parsing.
/// \param DataLayoutCallback Override datalayout in the llvm assembly.
/// \param ParserContext Optional context that records source locations.
/// \return true on error.
LLVM_ABI bool parseAssemblyInto(
    MemoryBufferRef F, Module *M, ModuleSummaryIndex *Index, SMDiagnostic &Err,
    SlotMapping *Slots = nullptr,
    DataLayoutCallbackTy DataLayoutCallback =
        [](StringRef, StringRef) { return std::nullopt; },
    AsmParserContext *ParserContext = nullptr);

/// Parse a type and a constant value in the given string.
///
/// The constant value can be any LLVM constant, including a constant
/// expression.
///
/// \param Asm The string containing the type and constant.
/// \param Err Error result info.
/// \param M The module providing context for named types and values.
/// \param Slots The optional slot mapping that will restore the parsing state
/// of the module.
/// \return null on error.
LLVM_ABI Constant *parseConstantValue(StringRef Asm, SMDiagnostic &Err,
                                      const Module &M,
                                      const SlotMapping *Slots = nullptr);

/// Parse a type in the given string.
///
/// \param Asm The string containing the type.
/// \param Err Error result info.
/// \param M The module providing context for named types.
/// \param Slots The optional slot mapping that will restore the parsing state
/// of the module.
/// \return null on error.
LLVM_ABI Type *parseType(StringRef Asm, SMDiagnostic &Err, const Module &M,
                         const SlotMapping *Slots = nullptr);

/// Parse a type at the beginning of the given string.
///
/// \param Asm The string that starts with a type.
/// \param Read The number of characters consumed while parsing the type.
/// \param Err Error result info.
/// \param M The module providing context for named types.
/// \param Slots The optional slot mapping that will restore the parsing state
/// of the module.
/// \return null on error.
LLVM_ABI Type *parseTypeAtBeginning(StringRef Asm, unsigned &Read,
                                    SMDiagnostic &Err, const Module &M,
                                    const SlotMapping *Slots = nullptr);

/// Parse a DIExpression body at the beginning of the given string.
///
/// \param Asm The string that starts with a DIExpression body.
/// \param Read The number of characters consumed while parsing.
/// \param Err Error result info.
/// \param M The module providing context for named types and values.
/// \param Slots The optional slot mapping that will restore the parsing state
/// of the module.
/// \return null on error.
LLVM_ABI DIExpression *
parseDIExpressionBodyAtBeginning(StringRef Asm, unsigned &Read,
                                 SMDiagnostic &Err, const Module &M,
                                 const SlotMapping *Slots);

} // End llvm namespace

#endif
