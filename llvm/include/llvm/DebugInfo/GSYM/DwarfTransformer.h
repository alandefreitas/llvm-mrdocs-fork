//===- DwarfTransformer.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_DWARFTRANSFORMER_H
#define LLVM_DEBUGINFO_GSYM_DWARFTRANSFORMER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/GSYM/ExtractRanges.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {

class raw_ostream;

namespace gsym {

/// Compile-unit specific state used while transforming DWARF into GSYM.
struct CUInfo;
struct FunctionInfo;
class GsymCreator;
class OutputAggregator;

/// Transforms DWARF debug info into GSYM function information.
///
/// Populates the GsymCreator object that it is constructed with. This class
/// supports converting all DW_TAG_subprogram DIEs into gsym::FunctionInfo
/// objects that include line table information and inline function
/// information. Creating a separate class to transform this data allows this
/// class to be unit tested.
class DwarfTransformer {
public:
  /// Create a DWARF transformer.
  ///
  /// \param D The DWARF to use when converting to GSYM.
  ///
  /// \param G The GSYM creator to populate with the function information
  /// from the debug info.
  ///
  /// \param LDCS Flag to indicate whether we should load the call site
  /// information from DWARF `DW_TAG_call_site` entries
  ///
  /// \param MachO Flag to indicate if the object file is mach-o (Apple's
  /// executable format). Apple has some compile unit attributes that look like
  /// split DWARF, but they aren't and they can cause warnins to be emitted
  /// about missing DWO files.
  DwarfTransformer(DWARFContext &D, GsymCreator &G, bool LDCS = false,
                   bool MachO = false)
      : DICtx(D), Gsym(G), LoadDwarfCallSites(LDCS), IsMachO(MachO) {}

  /// Convert DWARF from the object file into GSYM format.
  ///
  /// Extracts the DWARF from the supplied object file and populates the
  /// GsymCreator object that was passed to the constructor. Returns an error
  /// if something fatal is encountered.
  ///
  /// \param NumThreads The number of threads that the conversion process can
  ///                   use.
  ///
  /// \param OS The stream to log warnings and non fatal issues to. If NULL
  ///           then don't log.
  ///
  /// \returns An error indicating any fatal issues that happen when parsing
  /// the DWARF, or Error::success() if all goes well.
  LLVM_ABI llvm::Error convert(uint32_t NumThreads, OutputAggregator &OS);

  /// Verify that a GSYM file matches the DWARF debug information.
  ///
  /// Compares line and inline frame information in the GSYM file against the
  /// DWARF context for each address covered by a function.
  ///
  /// \param GsymPath The path to the GSYM file to verify.
  ///
  /// \param OS The stream to log warnings and verification issues to.
  ///
  /// \returns An error if verification fails, or Error::success() otherwise.
  LLVM_ABI llvm::Error verify(StringRef GsymPath, OutputAggregator &OS);

private:

  /// Parse the DWARF in the object file and convert it into the GsymCreator.
  Error parse();

  /// Handle any DIE (debug info entry) from the DWARF.
  ///
  /// This function will find all DW_TAG_subprogram DIEs that convert them into
  /// GSYM FuntionInfo objects and add them to the GsymCreator supplied during
  /// construction. The DIE and all its children will be recursively parsed
  /// with calls to this function.
  ///
  /// \param Strm The thread specific log stream for any non fatal errors and
  /// warnings. Once a thread has finished parsing an entire compile unit, all
  /// information in this temporary stream will be forwarded to the member
  /// variable log. This keeps logging thread safe. If the value is NULL, then
  /// don't log.
  ///
  /// \param CUI The compile unit specific information that contains the DWARF
  /// line table, cached file list, and other compile unit specific
  /// information.
  ///
  /// \param Die The DWARF debug info entry to parse.
  void handleDie(OutputAggregator &Strm, CUInfo &CUI, DWARFDie Die);

  /// Parse call site information from DWARF
  ///
  /// \param CUI   The compile unit info for the current CU.
  /// \param Die   The DWARFDie for the function.
  /// \param FI    The FunctionInfo for the function being populated.
  void parseCallSiteInfoFromDwarf(CUInfo &CUI, DWARFDie Die, FunctionInfo &FI);

  DWARFContext &DICtx;
  GsymCreator &Gsym;
  bool LoadDwarfCallSites;
  bool IsMachO;

  /// Unit test fixture with access to DwarfTransformer internals.
  friend class DwarfTransformerTest;
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_DWARFTRANSFORMER_H
