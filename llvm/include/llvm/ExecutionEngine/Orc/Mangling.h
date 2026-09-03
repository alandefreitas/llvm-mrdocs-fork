//===------ Mangling.h -- Name Mangling Utilities for ORC -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Name mangling utilities for ORC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MANGLING_H
#define LLVM_EXECUTIONENGINE_ORC_MANGLING_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm::orc {

/// Mangles symbol names then uniques them in the context of an
/// ExecutionSession.
class MangleAndInterner {
public:
  /// Target-specific linker name-mangling convention.
  enum class ManglingMode {
    /// No special prefix; pass names through unchanged.
    None,
    /// ELF mangling (DataLayout \c m:e).
    ELF,
    /// Mach-O mangling; prepends an underscore (DataLayout \c m:o).
    MachO,
    /// Windows COFF mangling (DataLayout \c m:w).
    WinCOFF,
    /// Windows COFF x86 mangling; prepends an underscore (DataLayout \c m:x).
    WinCOFFX86,
    /// GOFF mangling (DataLayout \c m:l).
    GOFF,
    /// Mips mangling (DataLayout \c m:m).
    Mips,
    /// XCOFF mangling (DataLayout \c m:a).
    XCOFF
  };

  /// Construct a mangler using the session target triple and optional ABI.
  /// @param ES Session whose symbol pool receives interned names.
  /// @param ABIName Optional ABI name used when computing the data layout.
  LLVM_ABI MangleAndInterner(ExecutionSession &ES, StringRef ABIName = "");
  /// Construct a mangler with an explicit mangling mode.
  /// @param ES Session whose symbol pool receives interned names.
  /// @param Mode Linker mangling convention to apply.
  LLVM_ABI MangleAndInterner(ExecutionSession &ES, ManglingMode Mode);
  /// Construct a mangler whose mode is derived from a data layout.
  /// @param ES Session whose symbol pool receives interned names.
  /// @param DL Data layout whose mangling specifier selects the mode.
  LLVM_ABI MangleAndInterner(ExecutionSession &ES, const DataLayout &DL);
  /// Mangle \p Name and intern the result in the execution session.
  /// @param Name Unmangled symbol name to process.
  /// @return Interned pointer to the mangled symbol name.
  LLVM_ABI SymbolStringPtr operator()(StringRef Name);

private:
  static ManglingMode fromDataLayoutStr(StringRef DLStr);
  static ManglingMode fromTriple(const Triple &TT, StringRef ABIName);
  static ManglingMode fromDataLayout(const DataLayout &DL);
  bool doNotMangleLeadingQuestionMark() const;

  ExecutionSession &ES;
  ManglingMode Mode;
};

/// Maps IR global values to their linker symbol names / flags.
///
/// This utility can be used when adding new IR globals in the JIT.
class IRSymbolMapper {
public:
  /// Options that control how IR globals are mapped to linker symbols.
  struct ManglingOptions {
    /// If true, emit emulated-TLS helper symbols for thread-local globals.
    bool EmulatedTLS = false;
  };

  /// Map from mangled symbol names to the IR globals that define them.
  using SymbolNameToDefinitionMap = std::map<SymbolStringPtr, GlobalValue *>;

  /// Add mangled symbols for the given GlobalValues to SymbolFlags.
  ///
  /// If a SymbolToDefinitionMap pointer is supplied then it will be populated
  /// with Name-to-GlobalValue* mappings. Note that this mapping is not
  /// necessarily one-to-one: thread-local GlobalValues, for example, may
  /// produce more than one symbol, in which case the map will contain duplicate
  /// values.
  /// @param ES Session used to mangle and intern symbol names.
  /// @param MO Options controlling emulated-TLS and related mangling.
  /// @param GVs Global values whose linker symbols should be recorded.
  /// @param SymbolFlags Map updated with mangled names and JIT symbol flags.
  /// @param SymbolToDefinition Optional map of mangled names to defining GVs.
  LLVM_ABI static void
  add(ExecutionSession &ES, const ManglingOptions &MO,
      ArrayRef<GlobalValue *> GVs, SymbolFlagsMap &SymbolFlags,
      SymbolNameToDefinitionMap *SymbolToDefinition = nullptr);
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_MANGLING_H
