//===- MIRParser.h - MIR serialization format parser ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This MIR serialization library is currently a work in progress. It can't
// serialize machine functions at this time.
//
// This file declares the functions that parse the MIR serialization format
// files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRPARSER_MIRPARSER_H
#define LLVM_CODEGEN_MIRPARSER_MIRPARSER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include <functional>
#include <memory>
#include <optional>

namespace llvm {

class Function;
class LLVMContext;
class MemoryBuffer;
class Module;
/// Private implementation details of \c MIRParser.
class MIRParserImpl;
class MachineModuleInfo;
class SMDiagnostic;
class StringRef;

template <typename IRUnitT, typename... ExtraArgTs> class AnalysisManager;
using ModuleAnalysisManager = AnalysisManager<Module>;

/// Callback that may override the data layout of a parsed MIR module.
///
/// The first argument is the target triple. The second is the data layout
/// string from the input, or a default. That input layout is used if the
/// callback returns std::nullopt.
typedef llvm::function_ref<std::optional<std::string>(StringRef, StringRef)>
    DataLayoutCallbackTy;

/// This class initializes machine functions by applying the state loaded from
/// a MIR file.
class MIRParser {
  std::unique_ptr<MIRParserImpl> Impl;

public:
  /// Constructs a MIR parser that owns the given implementation.
  ///
  /// \param Impl - Private parser implementation that owns the MIR buffer.
  LLVM_ABI MIRParser(std::unique_ptr<MIRParserImpl> Impl);
  /// Deleted copy constructor; MIRParser is not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  MIRParser(const MIRParser &Other) = delete;
  /// Destroys the MIR parser and its implementation.
  LLVM_ABI ~MIRParser();

  /// Parses the optional LLVM IR module in the MIR file.
  ///
  /// A new, empty module is created if the LLVM IR isn't present.
  /// \param DataLayoutCallback - Optional override for the module data layout.
  /// \returns nullptr if a parsing error occurred.
  LLVM_ABI std::unique_ptr<Module>
  parseIRModule(DataLayoutCallbackTy DataLayoutCallback =
                    [](StringRef, StringRef) { return std::nullopt; });

  /// Parses MachineFunctions in the MIR file and add them to the given
  /// MachineModuleInfo \p MMI.
  ///
  /// \param M - The LLVM IR module that owns the machine functions.
  /// \param MMI - Machine module info that receives the parsed functions.
  /// \returns true if an error occurred.
  LLVM_ABI bool parseMachineFunctions(Module &M, MachineModuleInfo &MMI);

  /// Parses MachineFunctions into MachineFunctionAnalysis results in \p MAM.
  ///
  /// Machine functions from the MIR file are added as the result of
  /// MachineFunctionAnalysis in ModulePassManager \p MAM.
  /// User should register at least MachineFunctionAnalysis,
  /// MachineModuleAnalysis, FunctionAnalysisManagerModuleProxy and
  /// PassInstrumentationAnalysis in \p MAM before parsing MIR.
  ///
  /// \param M - The LLVM IR module that owns the machine functions.
  /// \param MAM - Module analysis manager that holds MachineFunctionAnalysis.
  /// \returns true if an error occurred.
  LLVM_ABI bool parseMachineFunctions(Module &M, ModuleAnalysisManager &MAM);
};

/// This function is the main interface to the MIR serialization format parser.
///
/// It reads in a MIR file and returns a MIR parser that can parse the embedded
/// LLVM IR module and initialize the machine functions by parsing the machine
/// function's state.
///
/// \param Filename - The name of the file to parse.
/// \param Error - Error result info.
/// \param Context - Context which will be used for the parsed LLVM IR module.
/// \param ProcessIRFunction - function to run on every IR function or stub
/// loaded from the MIR file.
/// \returns A MIR parser for the file, or nullptr if a parsing error occurred.
LLVM_ABI std::unique_ptr<MIRParser> createMIRParserFromFile(
    StringRef Filename, SMDiagnostic &Error, LLVMContext &Context,
    std::function<void(Function &)> ProcessIRFunction = nullptr);

/// This function is another interface to the MIR serialization format parser.
///
/// It returns a MIR parser that works with the given memory buffer and that can
/// parse the embedded LLVM IR module and initialize the machine functions by
/// parsing the machine function's state.
///
/// \param Contents - The MemoryBuffer containing the machine level IR.
/// \param Context - Context which will be used for the parsed LLVM IR module.
/// \param ProcessIRFunction - function to run on every IR function or stub
/// loaded from the MIR file.
/// \returns A MIR parser for the given buffer.
LLVM_ABI std::unique_ptr<MIRParser>
createMIRParser(std::unique_ptr<MemoryBuffer> Contents, LLVMContext &Context,
                std::function<void(Function &)> ProcessIRFunction = nullptr);

} // end namespace llvm

#endif // LLVM_CODEGEN_MIRPARSER_MIRPARSER_H
