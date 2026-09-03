//===- MCTargetOptions.h - MC Target Options --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCTARGETOPTIONS_H
#define LLVM_MC_MCTARGETOPTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include <string>
#include <vector>

namespace llvm {

/// Controls when DWARF unwind information is emitted.
enum class EmitDwarfUnwindType {
  Always,          ///< Always emit dwarf unwind.
  NoCompactUnwind, ///< Only emit if compact unwind isn't available.
  DwarfOnly,       ///< Force compact unwind to reference DWARF.
  Default,         ///< Default behavior is based on the target.
};

/// For ELF targets, whether to adjust relocations referencing eligible local
/// symbols to use section symbols.
enum class RelocSectionSymType {
  All,      ///< For all eligible local symbols (default).
  Internal, ///< For .L symbols.
  None,     ///< Never use section symbols.
};

class StringRef;

/// Options that configure machine-code emission and the integrated assembler.
class MCTargetOptions {
public:
  /// Kind of instrumentation the assembler may inject.
  enum AsmInstrumentation {
    AsmInstrumentationNone,    ///< No assembler instrumentation.
    AsmInstrumentationAddress  ///< Instrument memory addresses.
  };

  /// Relax all fixups to the maximum size.
  bool MCRelaxAll : 1;
  /// Do not emit an executable stack note.
  bool MCNoExecStack : 1;
  /// Treat assembler warnings as errors.
  bool MCFatalWarnings : 1;
  /// Suppress all assembler warnings.
  bool MCNoWarn : 1;
  /// Suppress deprecated-feature assembler warnings.
  bool MCNoDeprecatedWarn : 1;
  /// Disable assembler type checking.
  bool MCNoTypeCheck : 1;
  /// Keep temporary labels in the output.
  bool MCSaveTempLabels : 1;
  /// Prefer incremental-linker-compatible object emission.
  bool MCIncrementalLinkerCompatible : 1;
  /// Use the ARM FDPIC (Function Descriptor PIC) ABI when assembling.
  bool FDPIC : 1;
  /// Show instruction encoding in assembly output.
  bool ShowMCEncoding : 1;
  /// Show internal MCInst dumping in assembly output.
  bool ShowMCInst : 1;
  /// Emit verbose assembly comments.
  bool AsmVerbose : 1;

  /// Preserve Comments in Assembly.
  bool PreserveAsmComments : 1;

  /// Emit 64-bit DWARF format.
  bool Dwarf64 : 1;

  /// Use the CREL relocation format for ELF.
  bool Crel = false;

  /// Emit implicit mapping symbols for ARM/AArch64 ELF.
  bool ImplicitMapSyms = false;

  /// Prefer relaxed GOTPCRELX relocations on x86-64 ELF.
  ///
  /// If true, prefer R_X86_64_[REX_]GOTPCRELX to R_X86_64_GOTPCREL on x86-64
  /// ELF.
  bool X86RelaxRelocations = true;

  /// Encode SSE instructions as their AVX equivalents when printing.
  bool X86Sse2Avx = false;

  /// For ELF relocations, controls section symbol conversion.
  RelocSectionSymType RelocSectionSym = RelocSectionSymType::All;

  /// Optional assembly syntax variant index for the printer.
  std::optional<unsigned> OutputAsmVariant;

  /// Policy for emitting DWARF unwind information.
  EmitDwarfUnwindType EmitDwarfUnwind;

  /// DWARF version to emit, or 0 for the target default.
  int DwarfVersion = 0;

  /// Whether \c .file directives should include a directory operand.
  enum DwarfDirectory {
    DisableDwarfDirectory, ///< Force disable directory operands.
    EnableDwarfDirectory,  ///< Force enable, for assemblers that support
                           ///< `.file fileno directory filename' syntax.
    DefaultDwarfDirectory  ///< Default is based on the target.
  };
  /// Whether \c .file directives should include a directory operand.
  DwarfDirectory MCUseDwarfDirectory;

  /// Whether to compress DWARF debug sections.
  DebugCompressionType CompressDebugSections = DebugCompressionType::None;

  /// Target ABI name requested for code generation.
  std::string ABIName;
  /// Assembly language dialect name (for example, masm).
  std::string AssemblyLanguage;
  /// Output path for split DWARF (.dwo) content.
  std::string SplitDwarfFile;
  /// Path for the assembler's secure log file (\c .secure_log_unique).
  std::string AsSecureLogFile;

  /// Compiler path recorded in CodeView LF_BUILDINFO.
  std::string Argv0;
  /// Command-line arguments recorded in CodeView LF_BUILDINFO.
  std::string CommandlineArgs;

  /// Additional paths to search for `.include` directives when using the
  /// integrated assembler.
  std::vector<std::string> IASSearchPaths;

  /// Extra options forwarded to the instruction printer.
  std::vector<std::string> InstPrinterOptions;

  /// Emit compact-unwind for non-canonical personality functions on Darwin.
  bool EmitCompactUnwindNonCanonical : 1;

  /// Emit SFrame unwind sections.
  bool EmitSFrameUnwind : 1;

  /// Use full register names when printing PowerPC assembly.
  bool PPCUseFullRegisterNames : 1;

  /// Force 8-byte (sdata8) pointer encodings for ELF exception-handling.
  ///
  /// On x86_64 this affects the .eh_frame FDE CFI plus the personality, LSDA,
  /// and TType encodings; on AArch64/PPC64 only the FDE CFI encoding changes
  /// (personality/LSDA/TType already default to sdata8).
  bool LargeEHEncoding = false;

  /// Construct with default MC target options.
  LLVM_ABI MCTargetOptions();

  /// Return the textual ABI name requested for the backend.
  ///
  /// If non-empty, this is the ABI name the backend should use (e.g. o32 or
  /// aapcs-linux).
  /// @return The ABI name, or an empty string when none was set.
  LLVM_ABI StringRef getABIName() const;

  /// Return the assembly language dialect name for this target.
  ///
  /// If non-empty, this is the assembly language name (e.g. masm).
  /// @return The assembly language name, or an empty string when none was set.
  LLVM_ABI StringRef getAssemblyLanguage() const;
};

} // end namespace llvm

#endif // LLVM_MC_MCTARGETOPTIONS_H
