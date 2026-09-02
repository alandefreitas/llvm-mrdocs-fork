//===-- llvm/Support/CodeGen.h - CodeGen Concepts ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file define some types which define code generation concepts. For
// example, relocation model.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CODEGEN_H
#define LLVM_SUPPORT_CODEGEN_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <optional>

namespace llvm {

  /// Relocation model types.
  namespace Reloc {
    // Cannot be named PIC due to collision with -DPIC
    /// Code relocation model.
    enum Model {
      Static,    ///< No dynamic relocations.
      PIC_,      ///< Position-independent code.
      DynamicNoPIC, ///< Dynamic but not PIC.
      ROPI,      ///< Read-only position independent.
      RWPI,      ///< Read-write position independent.
      ROPI_RWPI, ///< Combined ROPI and RWPI.
    };
  }

  /// Code model types.
  namespace CodeModel {
    // Sync changes with CodeGenCWrappers.h.
    /// Code model selecting address-range assumptions.
    enum Model {
      Tiny,   ///< Tiny code model.
      Small,  ///< Small code model.
      Kernel, ///< Kernel code model.
      Medium, ///< Medium code model.
      Large,  ///< Large code model.
    };
  }

  /// PIC level used to map -fpic/-fPIC.
  namespace PICLevel {
    /// Position-independent code level.
    enum Level {
      NotPIC = 0,   ///< Not position-independent.
      SmallPIC = 1, ///< Small PIC (-fpic).
      BigPIC = 2,   ///< Big PIC (-fPIC).
    };
  }

  /// Position-independent executable level.
  namespace PIELevel {
    /// PIE code model size level.
    enum Level {
      Default = 0, ///< Default PIE level.
      Small = 1,   ///< Small PIE.
      Large = 2,   ///< Large PIE.
    };
  }

  /// Thread-local storage models.
  namespace TLSModel {
    /// Thread-local storage access model.
    enum Model {
      GeneralDynamic, ///< General dynamic TLS model.
      LocalDynamic,   ///< Local dynamic TLS model.
      InitialExec,    ///< Initial-exec TLS model.
      LocalExec,      ///< Local-exec TLS model.
    };
  }

  enum class ExceptionHandling : int {
    None,     ///< No exception support
    DwarfCFI, ///< DWARF-like instruction based exceptions
    SjLj,     ///< setjmp/longjmp based exceptions
    ARM,      ///< ARM EHABI
    WinEH,    ///< Windows Exception Handling
    Wasm,     ///< WebAssembly Exception Handling
    AIX,      ///< AIX Exception Handling
    ZOS, ///< z/OS MVS Exception Handling. Very similar to DwarfCFI, but the
         ///< PPA1 is used instead of an .eh_frame section.
  };

  /// The floating-point format used for the target's "long double" type.
  enum class LongDoubleFormat {
    IEEEsingle,        ///< IEEE binary32.
    IEEEdouble,        ///< IEEE binary64.
    X87DoubleExtended, ///< x87 80-bit extended precision.
    IEEEquad,          ///< IEEE binary128.
    PPCDoubleDouble,   ///< PowerPC double-double.
  };

  /// Returns the IR floating-point type name for a LongDoubleFormat.
  inline StringRef getLongDoubleFormatName(LongDoubleFormat Format) {
    switch (Format) {
    case LongDoubleFormat::IEEEsingle:
      return "float";
    case LongDoubleFormat::IEEEdouble:
      return "double";
    case LongDoubleFormat::X87DoubleExtended:
      return "x86_fp80";
    case LongDoubleFormat::IEEEquad:
      return "fp128";
    case LongDoubleFormat::PPCDoubleDouble:
      return "ppc_fp128";
    }
    return "";
  }

  /// Parses an IR floating-point type name into a LongDoubleFormat, returning
  /// std::nullopt if it does not name a supported long double format.
  inline std::optional<LongDoubleFormat> parseLongDoubleFormat(StringRef Name) {
    if (Name == "float")
      return LongDoubleFormat::IEEEsingle;
    if (Name == "double")
      return LongDoubleFormat::IEEEdouble;
    if (Name == "x86_fp80")
      return LongDoubleFormat::X87DoubleExtended;
    if (Name == "fp128")
      return LongDoubleFormat::IEEEquad;
    if (Name == "ppc_fp128")
      return LongDoubleFormat::PPCDoubleDouble;
    return std::nullopt;
  }

  /// Floating-point ABI selection.
  namespace FloatABI {
  enum ABIType {
    Default, ///< Target-specific default (soft or hard).
    Soft,    ///< Soft-float ABI.
    Hard,    ///< Hard-float ABI.
  };

  /// Parse the string spelling used by the "float-abi" IR module flag into an
  /// ABIType.
  inline std::optional<ABIType> parseABIType(StringRef S) {
    if (S == "soft")
      return Soft;
    if (S == "hard")
      return Hard;
    return std::nullopt;
  }

  /// Returns the string spelling used by the "float-abi" IR module flag for a
  /// Soft or Hard ABIType. Default has no spelling.
  inline StringRef getABITypeName(ABIType ABI) {
    switch (ABI) {
    case Soft:
      return "soft";
    case Hard:
      return "hard";
    case Default:
      break;
    }
    return "";
  }
  } // namespace FloatABI

  /// Embedded ABI version selection.
  enum class EABI {
    Unknown, ///< Unknown EABI.
    Default, ///< EABI not specified.
    EABI4,   ///< EABI version 4.
    EABI5,   ///< EABI version 5.
    GNU,     ///< GNU EABI.
  };

  /// Code generation optimization level.
  enum class CodeGenOptLevel {
    None = 0,      ///< No optimization (-O0).
    Less = 1,      ///< Minimal optimization (-O1).
    Default = 2,   ///< Default optimization (-O2, -Os, -Oz).
    Aggressive = 3 ///< Aggressive optimization (-O3).
  };

  /// Helpers for parsing and converting code-generation optimization levels.
  namespace CodeGenOpt {
  /// Get the \c Level identified by the integer \p OL.
  ///
  /// Returns std::nullopt if \p OL is invalid.
  inline std::optional<CodeGenOptLevel> getLevel(int OL) {
    if (OL < 0 || OL > 3)
      return std::nullopt;
    return static_cast<CodeGenOptLevel>(OL);
  }
  /// Parse \p C as a single digit integer and get matching \c CodeGenLevel.
  ///
  /// Returns std::nullopt if the input is not a valid optimization level.
  inline std::optional<CodeGenOptLevel> parseLevel(char C) {
    if (C < '0')
      return std::nullopt;
    return getLevel(static_cast<int>(C - '0'));
  }
  } // namespace CodeGenOpt

  /// Output kind requested from or returned by addPassesToEmitFile.
  enum class CodeGenFileType {
    AssemblyFile, ///< Emit an assembly file.
    ObjectFile,   ///< Emit an object file.
    Null,         ///< Do not emit any output.
  };

  /// Specify which functions should keep the frame pointer.
  enum class FramePointerKind {
    None,            ///< Omit the frame pointer.
    NonLeaf,         ///< Keep the frame pointer in non-leaf functions.
    All,             ///< Keep the frame pointer in all functions.
    Reserved,        ///< Reserve the frame pointer register.
    NonLeafNoReserve, ///< Non-leaf frame pointer without reservation.
  };

  /// Specify what type of zeroing for callee-used registers.
  namespace ZeroCallUsedRegs {
  const unsigned ONLY_USED = 1U << 1; ///< Zero only registers used in the function.
  const unsigned ONLY_GPR = 1U << 2;  ///< Restrict zeroing to GPRs.
  const unsigned ONLY_ARG = 1U << 3;  ///< Restrict zeroing to argument registers.

  /// Kind of zeroing applied to call-used registers.
  enum class ZeroCallUsedRegsKind : unsigned int {
    Skip = 1U << 0, ///< Don't zero any call-used regs.
    UsedGPRArg = ONLY_USED | ONLY_GPR | ONLY_ARG, ///< Used GPRs that pass args.
    UsedGPR = ONLY_USED | ONLY_GPR, ///< Used call-used GPRs.
    UsedArg = ONLY_USED | ONLY_ARG, ///< Used regs that pass args.
    Used = ONLY_USED, ///< Used call-used regs.
    AllGPRArg = ONLY_GPR | ONLY_ARG, ///< All call-used GPRs that pass args.
    AllGPR = ONLY_GPR, ///< All call-used GPRs.
    AllArg = ONLY_ARG, ///< All call-used regs that pass args.
    All = 0, ///< All call-used regs.
  };
  } // namespace ZeroCallUsedRegs

  enum class UWTableKind {
    None = 0,  ///< No unwind table requested
    Sync = 1,  ///< "Synchronous" unwind tables
    Async = 2, ///< "Asynchronous" unwind tables (instr precise)
    Default = 2, ///< Default unwind table kind (async).
  };

  /// Function return thunk generation mode.
  enum class FunctionReturnThunksKind : unsigned int {
    Keep = 0,    ///< No function return thunk.
    Extern = 1,  ///< Replace returns with jump to thunk, don't emit thunk.
    Invalid = 2, ///< Not used.
  };

  /// Windows x64 exception-handling unwind info version.
  enum class WinX64EHUnwindMode {
    Default = 4, ///< Toolchain default/auto.

    V1 = 0,           ///< V1 unwind info.
    V2BestEffort = 1, ///< V2 where possible, fall back to V1.
    V2Required = 2,   ///< V2 required; error if unavailable.
    V3 = 3,           ///< V3 unwind info.
  };

  /// Control Flow Guard enablement mode.
  enum class ControlFlowGuardMode {
    Disabled = 0,  ///< Don't enable Control Flow Guard.
    TableOnly = 1, ///< Emit CFG tables without checks.
    Enabled = 2,   ///< Enable CFG checks and emit tables.
  };

  /// Control Flow Guard check mechanism.
  enum class ControlFlowGuardMechanism {
    Automatic = 0, ///< Choose the mechanism from the target.
    Check = 1,     ///< Use the check mechanism.
    Dispatch = 2,  ///< Use the dispatch mechanism.
  };

  } // namespace llvm

#endif
