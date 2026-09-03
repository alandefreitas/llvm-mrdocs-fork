//===-- MCTargetOptionsCommandFlags.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains machine code-specific flags that are shared between
// different command line tools.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCTARGETOPTIONSCOMMANDFLAGS_H
#define LLVM_MC_MCTARGETOPTIONSCOMMANDFLAGS_H

#include "llvm/Support/Compiler.h"
#include <optional>
#include <string>

namespace llvm {

class MCTargetOptions;
enum class RelocSectionSymType;
enum class EmitDwarfUnwindType;
class StringRef;

namespace mc {

/// Return whether -mc-relax-all is enabled.
/// @return True if -mc-relax-all is enabled.
LLVM_ABI bool getRelaxAll();
/// Return the -mc-relax-all value if it was explicitly set.
/// @return The explicit -mc-relax-all value, or std::nullopt if unset.
LLVM_ABI std::optional<bool> getExplicitRelaxAll();

/// Return whether -incremental-linker-compatible is enabled.
/// @return True if -incremental-linker-compatible is enabled.
LLVM_ABI bool getIncrementalLinkerCompatible();

/// Return whether -fdpic is enabled.
/// @return True if -fdpic is enabled.
LLVM_ABI bool getFDPIC();

/// Return the value of the -dwarf-version command-line option.
/// @return The DWARF version from the command-line option.
LLVM_ABI int getDwarfVersion();

/// Return whether -dwarf64 is enabled.
/// @return True if -dwarf64 is enabled.
LLVM_ABI bool getDwarf64();

/// Return the value of the -emit-dwarf-unwind command-line option.
/// @return The emit-dwarf-unwind mode from the command-line option.
LLVM_ABI EmitDwarfUnwindType getEmitDwarfUnwind();

/// Return whether -emit-compact-unwind-non-canonical is enabled.
/// @return True if -emit-compact-unwind-non-canonical is enabled.
LLVM_ABI bool getEmitCompactUnwindNonCanonical();

/// Return whether -gsframe is enabled.
/// @return True if -gsframe is enabled.
LLVM_ABI bool getEmitSFrameUnwind();

/// Return whether -asm-show-inst is enabled.
/// @return True if -asm-show-inst is enabled.
LLVM_ABI bool getShowMCInst();

/// Return whether -fatal-warnings is enabled.
/// @return True if -fatal-warnings is enabled.
LLVM_ABI bool getFatalWarnings();

/// Return whether -no-warn is enabled.
/// @return True if -no-warn is enabled.
LLVM_ABI bool getNoWarn();

/// Return whether -no-deprecated-warn is enabled.
/// @return True if -no-deprecated-warn is enabled.
LLVM_ABI bool getNoDeprecatedWarn();

/// Return whether -no-type-check is enabled.
/// @return True if -no-type-check is enabled.
LLVM_ABI bool getNoTypeCheck();

/// Return whether -save-temp-labels is enabled.
/// @return True if -save-temp-labels is enabled.
LLVM_ABI bool getSaveTempLabels();

/// Return whether -crel is enabled.
/// @return True if -crel is enabled.
LLVM_ABI bool getCrel();

/// Return whether -implicit-mapsyms is enabled.
/// @return True if -implicit-mapsyms is enabled.
LLVM_ABI bool getImplicitMapSyms();

/// Return whether -x86-relax-relocations is enabled.
/// @return True if -x86-relax-relocations is enabled.
LLVM_ABI bool getX86RelaxRelocations();

/// Return whether -x86-sse2avx is enabled.
/// @return True if -x86-sse2avx is enabled.
LLVM_ABI bool getX86Sse2Avx();

/// Return the value of the -reloc-section-sym command-line option.
/// @return The reloc-section-sym mode from the command-line option.
LLVM_ABI RelocSectionSymType getRelocSectionSym();

/// Return whether -large-eh-encoding is enabled.
/// @return True if -large-eh-encoding is enabled.
LLVM_ABI bool getLargeEHEncoding();

/// Return the value of the -target-abi command-line option.
/// @return The target ABI name from the command-line option.
LLVM_ABI StringRef getABIName();

/// Return the value of the -as-secure-log-file command-line option.
/// @return The secure log file path from the command-line option.
LLVM_ABI StringRef getAsSecureLogFile();

/// Create this object with static storage to register mc-related command
/// line options.
struct RegisterMCTargetOptionsFlags {
  /// Register the shared MC command-line options.
  LLVM_ABI RegisterMCTargetOptionsFlags();
};

/// Initialize an MCTargetOptions object from the registered MC flags.
/// @return An MCTargetOptions instance populated from the registered flags.
LLVM_ABI MCTargetOptions InitMCTargetOptionsFromFlags();

} // namespace mc

} // namespace llvm

#endif
