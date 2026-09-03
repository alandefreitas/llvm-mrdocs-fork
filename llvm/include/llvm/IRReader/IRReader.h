//===---- llvm/IRReader/IRReader.h - Reader for LLVM IR files ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines functions for reading LLVM IR. They support both
// Bitcode and Assembly, automatically detecting the input format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IRREADER_IRREADER_H
#define LLVM_IRREADER_IRREADER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/AsmParser/AsmParserContext.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {

class MemoryBuffer;
class MemoryBufferRef;
class Module;
class SMDiagnostic;
class LLVMContext;

/// Create a Module from a MemoryBuffer, with lazy function-body loading for
/// bitcode.
///
/// If the given MemoryBuffer holds a bitcode image, return a Module for it
/// which does lazy deserialization of function bodies. Otherwise, attempt to
/// parse it as LLVM Assembly and return a fully populated Module. The
/// ShouldLazyLoadMetadata flag is passed down to the bitcode reader to
/// optionally enable lazy metadata loading. This takes ownership of \p Buffer.
///
/// \param Buffer Memory buffer containing bitcode or LLVM assembly; ownership
///               is taken.
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param ShouldLazyLoadMetadata If true, defer loading metadata for bitcode.
/// \return A Module with lazy function-body loading for bitcode, or a fully
///         populated Module for assembly; nullptr on error.
LLVM_ABI std::unique_ptr<Module>
getLazyIRModule(std::unique_ptr<MemoryBuffer> Buffer, SMDiagnostic &Err,
                LLVMContext &Context, bool ShouldLazyLoadMetadata = false);

/// Create a Module from a file, with lazy function-body loading for bitcode.
///
/// If the given file holds a bitcode image, return a Module for it which does
/// lazy deserialization of function bodies. Otherwise, attempt to parse it as
/// LLVM Assembly and return a fully populated Module. The
/// ShouldLazyLoadMetadata flag is passed down to the bitcode reader to
/// optionally enable lazy metadata loading.
///
/// \param Filename Path to the file containing bitcode or LLVM assembly.
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param ShouldLazyLoadMetadata If true, defer loading metadata for bitcode.
/// \return A Module with lazy function-body loading for bitcode, or a fully
///         populated Module for assembly; nullptr on error.
LLVM_ABI std::unique_ptr<Module>
getLazyIRFileModule(StringRef Filename, SMDiagnostic &Err, LLVMContext &Context,
                    bool ShouldLazyLoadMetadata = false);

/// If the given MemoryBuffer holds a bitcode image, return a Module
/// for it.  Otherwise, attempt to parse it as LLVM Assembly and return
/// a Module for it.
///
/// \param Buffer Memory buffer containing bitcode or LLVM assembly.
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Callbacks Optional parser customization callbacks.
/// \param ParserContext Optional context that records source locations.
/// \return A Module for the buffer contents, or nullptr on error.
LLVM_ABI std::unique_ptr<Module>
parseIR(MemoryBufferRef Buffer, SMDiagnostic &Err, LLVMContext &Context,
        ParserCallbacks Callbacks = {},
        AsmParserContext *ParserContext = nullptr);

/// If the given file holds a bitcode image, return a Module for it.
/// Otherwise, attempt to parse it as LLVM Assembly and return a Module
/// for it.
///
/// \param Filename Path to the file containing bitcode or LLVM assembly.
/// \param Err Error result info.
/// \param Context Context in which to allocate globals info.
/// \param Callbacks Optional parser customization callbacks.
/// \param ParserContext Optional context that records source locations.
/// \return A Module for the file contents, or nullptr on error.
LLVM_ABI std::unique_ptr<Module>
parseIRFile(StringRef Filename, SMDiagnostic &Err, LLVMContext &Context,
            ParserCallbacks Callbacks = {},
            AsmParserContext *ParserContext = nullptr);
}

#endif
