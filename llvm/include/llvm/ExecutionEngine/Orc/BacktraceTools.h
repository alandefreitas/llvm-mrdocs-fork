//===-- BacktraceTools.h - Backtrace symbolication tools -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tools for dumping symbol tables and symbolicating backtraces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_BACKTRACETOOLS_H
#define LLVM_EXECUTIONENGINE_ORC_BACKTRACETOOLS_H

#include "llvm/ExecutionEngine/Orc/LinkGraphLinkingLayer.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <mutex>
#include <string>

namespace llvm::orc {

/// Dumps symbol tables from LinkGraphs to enable backtrace symbolication.
///
/// This plugin appends symbol information to a file in the following format:
///   "<link graph name>"
///   <address> <symbol name>
///   <address> <symbol name>
///   ...
///
/// Where addresses are in hexadecimal and symbol names are for defined symbols.
class LLVM_ABI SymbolTableDumpPlugin : public LinkGraphLinkingLayer::Plugin {
public:
  /// Create a SymbolTableDumpPlugin that will append symbol information
  /// to the file at the given path.
  /// @param Path Path of the file to append symbol table dumps to.
  /// @return A shared SymbolTableDumpPlugin, or an error if the file cannot
  ///         be opened.
  static Expected<std::shared_ptr<SymbolTableDumpPlugin>>
  Create(StringRef Path);

  /// Create a SymbolTableDumpPlugin. The resulting object is in an invalid
  /// state if, upon return, EC != std::error_code().
  /// Prefer SymbolTableDumpPlugin::Create.
  /// @param Path Path of the file to append symbol table dumps to.
  /// @param EC Set to a non-success code if the output file cannot be opened.
  SymbolTableDumpPlugin(StringRef Path, std::error_code &EC);

  /// Copy construction is deleted.
  /// @param Other Plugin that would be copied.
  SymbolTableDumpPlugin(const SymbolTableDumpPlugin &Other) = delete;
  /// Copy assignment is deleted.
  /// @param Other Plugin that would be copied.
  SymbolTableDumpPlugin &operator=(const SymbolTableDumpPlugin &Other) = delete;
  /// Move construction is deleted.
  /// @param Other Plugin that would be moved.
  SymbolTableDumpPlugin(SymbolTableDumpPlugin &&Other) = delete;
  /// Move assignment is deleted.
  /// @param Other Plugin that would be moved.
  SymbolTableDumpPlugin &operator=(SymbolTableDumpPlugin &&Other) = delete;

  /// Install a post-allocation pass that dumps defined symbol addresses.
  /// @param MR Materialization responsibility for the graph being linked.
  /// @param G Link graph whose pass configuration may be modified.
  /// @param Config Pass configuration to update.
  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &G,
                        jitlink::PassConfiguration &Config) override;

  /// No-op failure notification.
  /// @param MR Materialization responsibility for the failed graph.
  /// @return Success; this plugin has no failure-side effects.
  Error notifyFailed(MaterializationResponsibility &MR) override {
    return Error::success();
  }

  /// No-op resource removal notification.
  /// @param JD JITDylib whose resources are being removed.
  /// @param K Key identifying the resources to remove.
  /// @return Success; this plugin does not track resources to remove.
  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
    return Error::success();
  }

  /// No-op resource transfer notification.
  /// @param JD JITDylib whose resources are being transferred.
  /// @param DstKey Destination resource key.
  /// @param SrcKey Source resource key.
  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override {}

private:
  raw_fd_ostream OutputStream;
  std::mutex DumpMutex;
};

/// A class for symbolicating backtraces using a previously dumped symbol table.
class LLVM_ABI DumpedSymbolTable {
public:
  /// Create a DumpedSymbolTable from the given path.
  /// @param Path Path of the dumped symbol table file to load.
  /// @return A DumpedSymbolTable, or an error if the file cannot be loaded.
  static Expected<DumpedSymbolTable> Create(StringRef Path);

  /// Given a backtrace, try to symbolicate any unsymbolicated lines using the
  /// symbol addresses in the dumped symbol table.
  /// @param Backtrace Backtrace text whose unsymbolicated lines should be
  ///        resolved.
  /// @return Backtrace text with unsymbolicated lines resolved where possible.
  std::string symbolicate(StringRef Backtrace);

private:
  DumpedSymbolTable(std::unique_ptr<MemoryBuffer> SymtabBuffer);

  void parseBuffer();

  struct SymbolInfo {
    StringRef SymName;
    StringRef GraphName;
  };

  std::map<uint64_t, SymbolInfo> SymbolInfos;
  std::unique_ptr<MemoryBuffer> SymtabBuffer;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_BACKTRACETOOLS_H
