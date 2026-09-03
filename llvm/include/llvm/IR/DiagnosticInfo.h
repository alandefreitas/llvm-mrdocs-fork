//===- llvm/IR/DiagnosticInfo.h - Diagnostic Declaration --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the different classes involved in low level diagnostics.
//
// Diagnostics reporting is still done as part of the LLVMContext.
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_DIAGNOSTICINFO_H
#define LLVM_IR_DIAGNOSTICINFO_H

#include "llvm-c/Types.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TypeSize.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace llvm {

// Forward declarations.
class DiagnosticPrinter;
class DIFile;
class DISubprogram;
class CallInst;
class Function;
class Instruction;
class InstructionCost;
class Module;
class Type;
class Value;

/// Defines the different supported severity of a diagnostic.
enum DiagnosticSeverity : char {
  /// An error that should typically stop compilation.
  DS_Error,
  /// A warning that does not prevent continuing.
  DS_Warning,
  /// An optimization or analysis remark.
  DS_Remark,
  /// A note that attaches additional information to a prior diagnostic.
  DS_Note
};

/// Defines the different supported kind of a diagnostic.
/// This enum should be extended with a new ID for each added concrete subclass.
enum DiagnosticKind {
  /// Generic diagnostic without a source location.
  DK_Generic,
  /// Generic diagnostic with an associated location.
  DK_GenericWithLoc,
  /// Inline assembly diagnostic.
  DK_InlineAsm,
  /// Register allocation failure diagnostic.
  DK_RegAllocFailure,
  /// Resource limit diagnostic (e.g. stack size).
  DK_ResourceLimit,
  /// Stack frame size diagnostic.
  DK_StackSize,
  /// Linker diagnostic.
  DK_Linker,
  /// Lowering diagnostic.
  DK_Lowering,
  /// Legalization failure diagnostic.
  DK_LegalizationFailure,
  /// Debug metadata version mismatch diagnostic.
  DK_DebugMetadataVersion,
  /// Invalid debug metadata being ignored.
  DK_DebugMetadataInvalid,
  /// IR instrumentation diagnostic.
  DK_Instrumentation,
  /// Instruction selection fallback diagnostic.
  DK_ISelFallback,
  /// Sample profile diagnostic.
  DK_SampleProfile,
  /// Applied optimization remark.
  DK_OptimizationRemark,
  /// Missed optimization remark.
  DK_OptimizationRemarkMissed,
  /// Optimization analysis remark.
  DK_OptimizationRemarkAnalysis,
  /// FP non-commutativity analysis remark.
  DK_OptimizationRemarkAnalysisFPCommute,
  /// Pointer aliasing analysis remark.
  DK_OptimizationRemarkAnalysisAliasing,
  /// Optimization failure diagnostic.
  DK_OptimizationFailure,
  /// First IR optimization remark kind (inclusive).
  DK_FirstRemark = DK_OptimizationRemark,
  /// Last IR optimization remark kind (inclusive).
  DK_LastRemark = DK_OptimizationFailure,
  /// Applied machine-IR optimization remark.
  DK_MachineOptimizationRemark,
  /// Missed machine-IR optimization remark.
  DK_MachineOptimizationRemarkMissed,
  /// Machine-IR optimization analysis remark.
  DK_MachineOptimizationRemarkAnalysis,
  /// First machine-IR remark kind (inclusive).
  DK_FirstMachineRemark = DK_MachineOptimizationRemark,
  /// Last machine-IR remark kind (inclusive).
  DK_LastMachineRemark = DK_MachineOptimizationRemarkAnalysis,
  /// Machine IR parser diagnostic.
  DK_MIRParser,
  /// PGO profile diagnostic.
  DK_PGOProfile,
  /// Unsupported feature diagnostic.
  DK_Unsupported,
  /// Unsupported target intrinsic diagnostic.
  DK_UnsupportedTargetIntrinsic,
  /// SourceMgr-based diagnostic.
  DK_SrcMgr,
  /// Diagnostic for a dontcall attribute violation.
  DK_DontCall,
  /// MisExpect analysis diagnostic.
  DK_MisExpect,
  /// First kind ID reserved for plugins (must remain last).
  DK_FirstPluginKind // Must be last value to work with
                     // getNextAvailablePluginDiagnosticKind
};

/// Return the next available kind ID for a plugin diagnostic.
///
/// Each time this function is called, it returns a different number.
/// Therefore, a plugin that wants to "identify" its own classes
/// with a dynamic identifier, just have to use this method to get a new ID
/// and assign it to each of its classes.
/// The returned ID will be greater than or equal to DK_FirstPluginKind.
/// Thus, the plugin identifiers will not conflict with the
/// DiagnosticKind values.
///
/// @return A fresh plugin diagnostic kind ID (>= DK_FirstPluginKind).
LLVM_ABI int getNextAvailablePluginDiagnosticKind();

/// Abstract base class for backend diagnostic reporting.
///
/// The print method must be overloaded by the subclasses to print a
/// user-friendly message in the client of the backend (let us call it a
/// frontend).
class LLVM_ABI DiagnosticInfo {
private:
  /// Kind defines the kind of report this is about.
  const /* DiagnosticKind */ int Kind;
  /// Severity gives the severity of the diagnostic.
  const DiagnosticSeverity Severity;

  virtual void anchor();
public:
  /// Construct a diagnostic with the given kind and severity.
  ///
  /// \param Kind Diagnostic kind identifier
  /// \param Severity Severity of this diagnostic
  DiagnosticInfo(/* DiagnosticKind */ int Kind, DiagnosticSeverity Severity)
      : Kind(Kind), Severity(Severity) {}

  /// Destroy the diagnostic.
  virtual ~DiagnosticInfo() = default;

  /// Return the kind of this diagnostic.
  ///
  /// @return The diagnostic kind identifier.
  /* DiagnosticKind */ int getKind() const { return Kind; }
  /// Return the severity of this diagnostic.
  ///
  /// @return The severity of this diagnostic.
  DiagnosticSeverity getSeverity() const { return Severity; }

  /// Print a user-friendly message using \p DP.
  ///
  /// This is the default message that will be printed to the user.
  /// It is used when the frontend does not directly take advantage
  /// of the information contained in fields of the subclasses.
  /// The printed message must not end with '.' nor start with a severity
  /// keyword.
  ///
  /// \param DP Printer that receives the message
  virtual void print(DiagnosticPrinter &DP) const = 0;
};

/// Callback type that receives a diagnostic for handling.
using DiagnosticHandlerFunction = std::function<void(const DiagnosticInfo &)>;

/// Generic diagnostic carrying a message and optional instruction.
class LLVM_ABI DiagnosticInfoGeneric : public DiagnosticInfo {
  const Twine &MsgStr;
  const Instruction *Inst = nullptr;

public:
  /// Construct a generic diagnostic with message \p MsgStr.
  ///
  /// This class does not copy \p MsgStr, therefore the reference must be valid
  /// for the whole life time of the Diagnostic.
  ///
  /// \param MsgStr Message to be reported to the frontend
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoGeneric(const Twine &MsgStr LLVM_LIFETIME_BOUND,
                        DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_Generic, Severity), MsgStr(MsgStr) {}

  /// Construct a generic diagnostic for instruction \p I.
  ///
  /// \param I Instruction that triggered the diagnostic
  /// \param ErrMsg Message to be reported to the frontend
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoGeneric(const Instruction *I,
                        const Twine &ErrMsg LLVM_LIFETIME_BOUND,
                        DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_Generic, Severity), MsgStr(ErrMsg), Inst(I) {}

  /// Return the diagnostic message.
  ///
  /// @return The diagnostic message.
  const Twine &getMsgStr() const { return MsgStr; }
  /// Return the optional instruction associated with this diagnostic.
  ///
  /// @return The optional instruction, or null if none.
  const Instruction *getInstruction() const { return Inst; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoGeneric.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoGeneric.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_Generic;
  }
};

/// Diagnostic information for inline asm reporting.
/// This is basically a message and an optional location.
class LLVM_ABI DiagnosticInfoInlineAsm : public DiagnosticInfo {
private:
  /// Optional line information. 0 if not set.
  uint64_t LocCookie = 0;
  /// Message to be reported.
  const Twine &MsgStr;
  /// Optional origin of the problem.
  const Instruction *Instr = nullptr;

public:
  /// Construct an inline asm diagnostic with an optional location cookie.
  ///
  /// This class does not copy \p MsgStr, therefore the reference must be valid
  /// for the whole life time of the Diagnostic.
  ///
  /// \param LocCookie If non-zero, line number for this report
  /// \param MsgStr Message to report
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoInlineAsm(uint64_t LocCookie,
                          const Twine &MsgStr LLVM_LIFETIME_BOUND,
                          DiagnosticSeverity Severity = DS_Error);

  /// Construct an inline asm diagnostic from instruction \p I.
  ///
  /// This class does not copy \p MsgStr, therefore the reference must be valid
  /// for the whole life time of the Diagnostic. Same for \p I.
  ///
  /// \param I Original instruction that triggered the diagnostic
  /// \param MsgStr Message to report
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoInlineAsm(const Instruction &I,
                          const Twine &MsgStr LLVM_LIFETIME_BOUND,
                          DiagnosticSeverity Severity = DS_Error);

  /// Return the optional location cookie (0 if unset).
  ///
  /// @return The location cookie, or 0 if unset.
  uint64_t getLocCookie() const { return LocCookie; }
  /// Return the diagnostic message.
  ///
  /// @return The diagnostic message.
  const Twine &getMsgStr() const { return MsgStr; }
  /// Return the optional instruction that triggered the diagnostic.
  ///
  /// @return The optional instruction, or null if none.
  const Instruction *getInstruction() const { return Instr; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoInlineAsm.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoInlineAsm.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_InlineAsm;
  }
};

/// Diagnostic information for debug metadata version reporting.
/// This is basically a module and a version.
class LLVM_ABI DiagnosticInfoDebugMetadataVersion : public DiagnosticInfo {
private:
  /// The module that is concerned by this debug metadata version diagnostic.
  const Module &M;
  /// The actual metadata version.
  unsigned MetadataVersion;

public:
  /// Construct a debug metadata version diagnostic for module \p M.
  ///
  /// \param M Module concerned by this diagnostic
  /// \param MetadataVersion Actual metadata version
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoDebugMetadataVersion(const Module &M, unsigned MetadataVersion,
                                     DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfo(DK_DebugMetadataVersion, Severity), M(M),
        MetadataVersion(MetadataVersion) {}

  /// Return the module concerned by this diagnostic.
  ///
  /// @return The module concerned by this diagnostic.
  const Module &getModule() const { return M; }
  /// Return the debug metadata version.
  ///
  /// @return The debug metadata version.
  unsigned getMetadataVersion() const { return MetadataVersion; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoDebugMetadataVersion.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoDebugMetadataVersion.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_DebugMetadataVersion;
  }
};

/// Diagnostic information for stripping invalid debug metadata.
class LLVM_ABI DiagnosticInfoIgnoringInvalidDebugMetadata
    : public DiagnosticInfo {
private:
  /// The module that is concerned by this invalid debug metadata diagnostic.
  const Module &M;

public:
  /// Construct a diagnostic for ignoring invalid debug metadata in \p M.
  ///
  /// \param M Module concerned by this diagnostic
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoIgnoringInvalidDebugMetadata(
      const Module &M, DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfo(DK_DebugMetadataInvalid, Severity), M(M) {}

  /// Return the module concerned by this diagnostic.
  ///
  /// @return The module concerned by this diagnostic.
  const Module &getModule() const { return M; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoIgnoringInvalidDebugMetadata.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoIgnoringInvalidDebugMetadata.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_DebugMetadataInvalid;
  }
};

/// Diagnostic information for the sample profiler.
class LLVM_ABI DiagnosticInfoSampleProfile : public DiagnosticInfo {
public:
  /// Construct a sample-profile diagnostic with file and line.
  ///
  /// \param FileName Input file associated with this diagnostic
  /// \param LineNum Line number where the diagnostic occurred
  /// \param Msg Message to report
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoSampleProfile(StringRef FileName, unsigned LineNum,
                              const Twine &Msg LLVM_LIFETIME_BOUND,
                              DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_SampleProfile, Severity), FileName(FileName),
        LineNum(LineNum), Msg(Msg) {}
  /// Construct a sample-profile diagnostic with a file name.
  ///
  /// \param FileName Input file associated with this diagnostic
  /// \param Msg Message to report
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoSampleProfile(StringRef FileName,
                              const Twine &Msg LLVM_LIFETIME_BOUND,
                              DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_SampleProfile, Severity), FileName(FileName),
        Msg(Msg) {}
  /// Construct a sample-profile diagnostic with only a message.
  ///
  /// \param Msg Message to report
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoSampleProfile(const Twine &Msg LLVM_LIFETIME_BOUND,
                              DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_SampleProfile, Severity), Msg(Msg) {}

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoSampleProfile.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoSampleProfile.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_SampleProfile;
  }

  /// Return the input file name associated with this diagnostic.
  ///
  /// @return The input file name associated with this diagnostic.
  StringRef getFileName() const { return FileName; }
  /// Return the line number (0 if none).
  ///
  /// @return The line number, or 0 if none.
  unsigned getLineNum() const { return LineNum; }
  /// Return the diagnostic message.
  ///
  /// @return The diagnostic message.
  const Twine &getMsg() const { return Msg; }

private:
  /// Name of the input file associated with this diagnostic.
  StringRef FileName;

  /// Line number where the diagnostic occurred. If 0, no line number will
  /// be emitted in the message.
  unsigned LineNum = 0;

  /// Message to report.
  const Twine &Msg;
};

/// Diagnostic information for the PGO profiler.
class LLVM_ABI DiagnosticInfoPGOProfile : public DiagnosticInfo {
public:
  /// Construct a PGO profile diagnostic.
  ///
  /// \param FileName Input file associated with this diagnostic
  /// \param Msg Message to report
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoPGOProfile(const char *FileName,
                           const Twine &Msg LLVM_LIFETIME_BOUND,
                           DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_PGOProfile, Severity), FileName(FileName), Msg(Msg) {}

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoPGOProfile.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoPGOProfile.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_PGOProfile;
  }

  /// Return the input file name associated with this diagnostic.
  ///
  /// @return The input file name associated with this diagnostic.
  const char *getFileName() const { return FileName; }
  /// Return the diagnostic message.
  ///
  /// @return The diagnostic message.
  const Twine &getMsg() const { return Msg; }

private:
  /// Name of the input file associated with this diagnostic.
  const char *FileName;

  /// Message to report.
  const Twine &Msg;
};

/// Source location used when emitting diagnostics.
class DiagnosticLocation {
  DIFile *File = nullptr;
  unsigned Line = 0;
  unsigned Column = 0;

public:
  /// Construct an invalid (empty) diagnostic location.
  DiagnosticLocation() = default;
  /// Construct a location from debug location \p DL.
  ///
  /// \param DL Debug location to convert
  LLVM_ABI DiagnosticLocation(const DebugLoc &DL);
  /// Construct a location from subprogram \p SP.
  ///
  /// \param SP DI subprogram providing file and line
  LLVM_ABI DiagnosticLocation(const DISubprogram *SP);

  /// Return true if this location has a valid file.
  ///
  /// @return True if this location has a valid file.
  bool isValid() const { return File; }
  /// Return the full path to the file.
  ///
  /// @return The absolute path to the source file.
  LLVM_ABI std::string getAbsolutePath() const;
  /// Return the file name relative to the compilation directory.
  ///
  /// @return The file path relative to the compilation directory.
  LLVM_ABI StringRef getRelativePath() const;
  /// Return the line number.
  ///
  /// @return The source line number.
  unsigned getLine() const { return Line; }
  /// Return the column number.
  ///
  /// @return The source column number.
  unsigned getColumn() const { return Column; }
};

/// Common features for diagnostics with an associated location.
class LLVM_ABI DiagnosticInfoWithLocationBase : public DiagnosticInfo {
  void anchor() override;
public:
  /// Construct a location-aware diagnostic for function \p Fn.
  ///
  /// \param Kind Diagnostic kind
  /// \param Severity Severity of this diagnostic
  /// \param Fn Function where the diagnostic is being emitted
  /// \param Loc Location information to use in the diagnostic
  DiagnosticInfoWithLocationBase(enum DiagnosticKind Kind,
                                 enum DiagnosticSeverity Severity,
                                 const Function &Fn,
                                 const DiagnosticLocation &Loc)
      : DiagnosticInfo(Kind, Severity), Fn(Fn), Loc(Loc) {}

  /// Return true if location information is available for this diagnostic.
  ///
  /// @return True if location information is available.
  bool isLocationAvailable() const { return Loc.isValid(); }

  /// Return the location as a "file:line:col" string.
  ///
  /// If location information is not available, it returns "<unknown>:0:0".
  ///
  /// @return The location string, or "<unknown>:0:0" if unavailable.
  std::string getLocationStr() const;

  /// Fill in relative path, line, and column for this diagnostic.
  ///
  /// \param RelativePath Relative source file path (output)
  /// \param Line Line number (output)
  /// \param Column Column number (output)
  void getLocation(StringRef &RelativePath, unsigned &Line,
                   unsigned &Column) const;

  /// Return the absolute path to the file.
  ///
  /// @return The absolute path to the source file.
  std::string getAbsolutePath() const;

  /// Return the function where this diagnostic is triggered.
  ///
  /// @return The function where this diagnostic is triggered.
  const Function &getFunction() const { return Fn; }
  /// Return the diagnostic location.
  ///
  /// @return The diagnostic location.
  DiagnosticLocation getLocation() const { return Loc; }

private:
  /// Function where this diagnostic is triggered.
  const Function &Fn;

  /// Debug location where this diagnostic is triggered.
  DiagnosticLocation Loc;
};

/// Diagnostic for a legalization failure at a given location.
class LLVM_ABI DiagnosticInfoLegalizationFailure
    : public DiagnosticInfoWithLocationBase {
private:
  /// Message to be reported.
  const Twine &MsgStr;

public:
  /// Construct a legalization-failure diagnostic.
  ///
  /// \param MsgStr Message to report
  /// \param Fn Function where the diagnostic is emitted
  /// \param Loc Location of the failure
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoLegalizationFailure(const Twine &MsgStr LLVM_LIFETIME_BOUND,
                                    const Function &Fn,
                                    const DiagnosticLocation &Loc,
                                    DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfoWithLocationBase(DK_LegalizationFailure, Severity, Fn,
                                       Loc),
        MsgStr(MsgStr) {}

  /// Return the diagnostic message.
  ///
  /// @return The diagnostic message.
  const Twine &getMsgStr() const { return MsgStr; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoLegalizationFailure.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoLegalizationFailure.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_LegalizationFailure;
  }
};

/// Generic location-aware diagnostic with a message.
class LLVM_ABI DiagnosticInfoGenericWithLoc
    : public DiagnosticInfoWithLocationBase {
private:
  /// Message to be reported.
  const Twine &MsgStr;

public:
  /// Construct a generic location-aware diagnostic.
  ///
  /// This class does not copy \p MsgStr, therefore the reference must be valid
  /// for the whole life time of the Diagnostic.
  ///
  /// \param MsgStr Message to be reported to the frontend
  /// \param Fn Function where the diagnostic is emitted
  /// \param Loc Location information for the diagnostic
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoGenericWithLoc(const Twine &MsgStr, const Function &Fn,
                               const DiagnosticLocation &Loc,
                               DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfoWithLocationBase(DK_GenericWithLoc, Severity, Fn, Loc),
        MsgStr(MsgStr) {}

  /// Return the diagnostic message.
  ///
  /// @return The diagnostic message.
  const Twine &getMsgStr() const { return MsgStr; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoGenericWithLoc.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoGenericWithLoc.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_GenericWithLoc;
  }
};

/// Diagnostic for a register allocation failure.
class LLVM_ABI DiagnosticInfoRegAllocFailure
    : public DiagnosticInfoWithLocationBase {
private:
  /// Message to be reported.
  const Twine &MsgStr;

public:
  /// Construct a regalloc-failure diagnostic at location \p DL.
  ///
  /// This class does not copy \p MsgStr, therefore the reference must be valid
  /// for the whole life time of the Diagnostic.
  ///
  /// \param MsgStr Message to be reported to the frontend
  /// \param Fn Function where the diagnostic is emitted
  /// \param DL Location information for the diagnostic
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoRegAllocFailure(const Twine &MsgStr, const Function &Fn,
                                const DiagnosticLocation &DL,
                                DiagnosticSeverity Severity = DS_Error);

  /// Construct a regalloc-failure diagnostic for function \p Fn.
  ///
  /// \param MsgStr Message to be reported to the frontend
  /// \param Fn Function where the diagnostic is emitted
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoRegAllocFailure(const Twine &MsgStr, const Function &Fn,
                                DiagnosticSeverity Severity = DS_Error);

  /// Return the diagnostic message.
  ///
  /// @return The diagnostic message.
  const Twine &getMsgStr() const { return MsgStr; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoRegAllocFailure.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoRegAllocFailure.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_RegAllocFailure;
  }
};

/// Diagnostic information for stack size etc. reporting.
/// This is basically a function and a size.
class LLVM_ABI DiagnosticInfoResourceLimit
    : public DiagnosticInfoWithLocationBase {
private:
  /// The function that is concerned by this resource limit diagnostic.
  const Function &Fn;

  /// Description of the resource type (e.g. stack size)
  const Twine &ResourceName;

  /// The computed size usage
  uint64_t ResourceSize;

  // Threshould passed
  uint64_t ResourceLimit;

public:
  /// Construct a resource-limit diagnostic for function \p Fn.
  ///
  /// \param Fn Function concerned by this diagnostic
  /// \param ResourceName Description of the resource type (e.g. stack size)
  /// \param ResourceSize Computed size usage
  /// \param ResourceLimit Threshold that was exceeded
  /// \param Severity Severity of this diagnostic
  /// \param Kind Diagnostic kind (defaults to DK_ResourceLimit)
  DiagnosticInfoResourceLimit(const Function &Fn,
                              const Twine &ResourceName LLVM_LIFETIME_BOUND,
                              uint64_t ResourceSize, uint64_t ResourceLimit,
                              DiagnosticSeverity Severity = DS_Warning,
                              DiagnosticKind Kind = DK_ResourceLimit);

  /// Return the function concerned by this diagnostic.
  ///
  /// @return The function concerned by this diagnostic.
  const Function &getFunction() const { return Fn; }
  /// Return the resource type name.
  ///
  /// @return The resource type name.
  const Twine &getResourceName() const { return ResourceName; }
  /// Return the computed resource size usage.
  ///
  /// @return The computed resource size usage.
  uint64_t getResourceSize() const { return ResourceSize; }
  /// Return the resource limit threshold.
  ///
  /// @return The resource limit threshold.
  uint64_t getResourceLimit() const { return ResourceLimit; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a resource-limit or stack-size diagnostic.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a resource-limit or stack-size diagnostic.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_ResourceLimit || DI->getKind() == DK_StackSize;
  }
};

/// Diagnostic for a stack frame size limit being exceeded.
class LLVM_ABI DiagnosticInfoStackSize : public DiagnosticInfoResourceLimit {
  void anchor() override;
  const Twine ResourceNameStr{"stack frame size"};

public:
  /// Construct a stack-size diagnostic for function \p Fn.
  ///
  /// \param Fn Function concerned by this diagnostic
  /// \param StackSize Computed stack frame size
  /// \param StackLimit Stack size limit
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoStackSize(const Function &Fn, uint64_t StackSize,
                          uint64_t StackLimit,
                          DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfoResourceLimit(Fn, ResourceNameStr, StackSize, StackLimit,
                                    Severity, DK_StackSize) {}

  /// Return the computed stack frame size.
  ///
  /// @return The computed stack frame size.
  uint64_t getStackSize() const { return getResourceSize(); }
  /// Return the stack size limit.
  ///
  /// @return The stack size limit.
  uint64_t getStackLimit() const { return getResourceLimit(); }

  /// Return true if \p DI is a DiagnosticInfoStackSize.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoStackSize.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_StackSize;
  }
};

/// Common features for diagnostics dealing with optimization remarks
/// that are used by both IR and MIR passes.
class LLVM_ABI DiagnosticInfoOptimizationBase
    : public DiagnosticInfoWithLocationBase {
public:
  /// Marker used to set IsVerbose via the stream interface.
  struct setIsVerbose {};

  /// Marker that marks following stream arguments as record-only.
  ///
  /// When an instance of this is inserted into the stream, the arguments
  /// following will not appear in the remark printed in the compiler output
  /// (-Rpass) but only in the optimization record file
  /// (-fsave-optimization-record).
  struct setExtraArgs {};

  /// Key-value argument used by the optimization remark stream interface.
  ///
  /// It internally converts everything into a key-value pair.
  struct Argument {
    /// Argument key name.
    std::string Key;
    /// Argument value as text.
    std::string Val;
    /// If set, the debug location corresponding to the value.
    DiagnosticLocation Loc;

    /// Construct a string argument with key "String".
    ///
    /// \param Str String value for the argument
    explicit Argument(StringRef Str = "") : Key("String"), Val(Str) {}
    /// Construct an argument from IR value \p V.
    ///
    /// \param Key Argument key name
    /// \param V IR value to stringify
    LLVM_ABI Argument(StringRef Key, const Value *V);
    /// Construct an argument from type \p T.
    ///
    /// \param Key Argument key name
    /// \param T Type to stringify
    LLVM_ABI Argument(StringRef Key, const Type *T);
    /// Construct an argument from string \p S.
    ///
    /// \param Key Argument key name
    /// \param S String value
    LLVM_ABI Argument(StringRef Key, StringRef S);
    /// Construct an argument from C string \p S.
    ///
    /// \param Key Argument key name
    /// \param S C string value
    Argument(StringRef Key, const char *S) : Argument(Key, StringRef(S)) {};
    /// Construct an argument from integer \p N.
    ///
    /// \param Key Argument key name
    /// \param N Integer value
    LLVM_ABI Argument(StringRef Key, int N);
    /// Construct an argument from float \p N.
    ///
    /// \param Key Argument key name
    /// \param N Float value
    LLVM_ABI Argument(StringRef Key, float N);
    /// Construct an argument from long \p N.
    ///
    /// \param Key Argument key name
    /// \param N Long value
    LLVM_ABI Argument(StringRef Key, long N);
    /// Construct an argument from long long \p N.
    ///
    /// \param Key Argument key name
    /// \param N Long long value
    LLVM_ABI Argument(StringRef Key, long long N);
    /// Construct an argument from unsigned \p N.
    ///
    /// \param Key Argument key name
    /// \param N Unsigned value
    LLVM_ABI Argument(StringRef Key, unsigned N);
    /// Construct an argument from unsigned long \p N.
    ///
    /// \param Key Argument key name
    /// \param N Unsigned long value
    LLVM_ABI Argument(StringRef Key, unsigned long N);
    /// Construct an argument from unsigned long long \p N.
    ///
    /// \param Key Argument key name
    /// \param N Unsigned long long value
    LLVM_ABI Argument(StringRef Key, unsigned long long N);
    /// Construct an argument from element count \p EC.
    ///
    /// \param Key Argument key name
    /// \param EC Element count value
    LLVM_ABI Argument(StringRef Key, ElementCount EC);
    /// Construct an argument from boolean \p B.
    ///
    /// \param Key Argument key name
    /// \param B Boolean value
    Argument(StringRef Key, bool B) : Key(Key), Val(B ? "true" : "false") {}
    /// Construct an argument from debug location \p dl.
    ///
    /// \param Key Argument key name
    /// \param dl Debug location
    LLVM_ABI Argument(StringRef Key, DebugLoc dl);
    /// Construct an argument from instruction cost \p C.
    ///
    /// \param Key Argument key name
    /// \param C Instruction cost
    LLVM_ABI Argument(StringRef Key, InstructionCost C);
    /// Construct an argument from branch probability \p P.
    ///
    /// \param Key Argument key name
    /// \param P Branch probability
    LLVM_ABI Argument(StringRef Key, BranchProbability P);
  };

  /// Construct an optimization remark diagnostic.
  ///
  /// \param Kind Diagnostic kind
  /// \param Severity Severity of this diagnostic
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark (single-word,
  /// CamelCase)
  /// \param Fn Function where the diagnostic is being emitted
  /// \param Loc Location information to use in the diagnostic. If line table
  /// information is available, the diagnostic will include the source code
  /// location.
  DiagnosticInfoOptimizationBase(enum DiagnosticKind Kind,
                                 enum DiagnosticSeverity Severity,
                                 const char *PassName, StringRef RemarkName,
                                 const Function &Fn,
                                 const DiagnosticLocation &Loc)
      : DiagnosticInfoWithLocationBase(Kind, Severity, Fn, Loc),
        PassName(PassName), RemarkName(RemarkName) {}

  /// Append string \p S to the remark message.
  ///
  /// \param S String to insert
  void insert(StringRef S);
  /// Append argument \p A to the remark.
  ///
  /// \param A Key-value argument to insert
  void insert(Argument A);
  /// Mark this remark as verbose.
  ///
  /// \param V Verbose marker (unused value)
  void insert(setIsVerbose V);
  /// Mark following arguments as optimization-record-only.
  ///
  /// \param EA Extra-args marker (unused value)
  void insert(setExtraArgs EA);

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if this optimization remark is enabled.
  ///
  /// Enabled by one of the LLVM command line flags (-pass-remarks,
  /// -pass-remarks-missed, or -pass-remarks-analysis). Note that this only
  /// handles the LLVM flags. We cannot access Clang flags from here (they
  /// are handled in BackendConsumer::OptimizationRemarkHandler).
  ///
  /// @return True if this optimization remark is enabled.
  virtual bool isEnabled() const = 0;

  /// Return the name of the pass emitting this remark.
  ///
  /// @return The name of the pass emitting this remark.
  StringRef getPassName() const { return PassName; }
  /// Return the textual remark identifier.
  ///
  /// @return The textual remark identifier.
  StringRef getRemarkName() const { return RemarkName; }
  /// Return the formatted remark message.
  ///
  /// @return The formatted remark message.
  std::string getMsg() const;
  /// Return the profile hotness, if available.
  ///
  /// @return The profile hotness, or std::nullopt if unavailable.
  std::optional<uint64_t> getHotness() const { return Hotness; }
  /// Set the profile hotness to \p H.
  ///
  /// \param H Optional hotness count
  void setHotness(std::optional<uint64_t> H) { Hotness = H; }

  /// Return true if this remark is expected to be noisy.
  ///
  /// @return True if this remark is expected to be noisy.
  bool isVerbose() const { return IsVerbose; }

  /// Return the collected remark arguments.
  ///
  /// @return The collected remark arguments.
  ArrayRef<Argument> getArgs() const { return Args; }

  /// Return true if \p DI is an optimization remark diagnostic.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is an optimization remark diagnostic.
  static bool classof(const DiagnosticInfo *DI) {
    return (DI->getKind() >= DK_FirstRemark &&
            DI->getKind() <= DK_LastRemark) ||
           (DI->getKind() >= DK_FirstMachineRemark &&
            DI->getKind() <= DK_LastMachineRemark);
  }

  /// Return true if this is a passed-optimization remark.
  ///
  /// @return True if this is a passed-optimization remark.
  bool isPassed() const {
    return (getKind() == DK_OptimizationRemark ||
            getKind() == DK_MachineOptimizationRemark);
  }

  /// Return true if this is a missed-optimization remark.
  ///
  /// @return True if this is a missed-optimization remark.
  bool isMissed() const {
    return (getKind() == DK_OptimizationRemarkMissed ||
            getKind() == DK_MachineOptimizationRemarkMissed);
  }

  /// Return true if this is an optimization analysis remark.
  ///
  /// @return True if this is an optimization analysis remark.
  bool isAnalysis() const {
    return (getKind() == DK_OptimizationRemarkAnalysis ||
            getKind() == DK_MachineOptimizationRemarkAnalysis);
  }

protected:
  /// Name of the pass that triggers this report. If this matches the
  /// regular expression given in -Rpass=regexp, then the remark will
  /// be emitted.
  const char *PassName;

  /// Textual identifier for the remark (single-word, CamelCase).
  ///
  /// Can be used by external tools reading the output file for optimization
  /// remarks to identify the remark.
  StringRef RemarkName;

  /// If profile information is available, this is the number of times the
  /// corresponding code was executed in a profile instrumentation run.
  std::optional<uint64_t> Hotness;

  /// Arguments collected via the streaming interface.
  SmallVector<Argument, 4> Args;

  /// The remark is expected to be noisy.
  bool IsVerbose = false;

  /// If positive, the index of the first argument that only appear in
  /// the optimization records and not in the remark printed in the compiler
  /// output.
  int FirstExtraArgIndex = -1;
};

/// Append string \p S to optimization remark \p R and return \p R.
///
/// Allow the insertion operator to return the actual remark type rather than a
/// common base class.  This allows returning the result of the insertion
/// directly by value, e.g. return OptimizationRemarkAnalysis(...) << "blah".
///
/// \param R Remark to append to
/// @return The forwarded remark \p R after inserting \p S.
template <class RemarkT>
decltype(auto)
operator<<(RemarkT &&R,
           std::enable_if_t<std::is_base_of_v<DiagnosticInfoOptimizationBase,
                                              std::remove_reference_t<RemarkT>>,
                            StringRef> S) {
  R.insert(S);
  return std::forward<RemarkT>(R);
}

/// Append argument \p A to optimization remark \p R and return \p R.
///
/// \param R Remark to append to
/// @return The forwarded remark \p R after inserting \p A.
template <class RemarkT>
decltype(auto)
operator<<(RemarkT &&R,
           std::enable_if_t<std::is_base_of_v<DiagnosticInfoOptimizationBase,
                                              std::remove_reference_t<RemarkT>>,
                            DiagnosticInfoOptimizationBase::Argument> A) {
  R.insert(A);
  return std::forward<RemarkT>(R);
}

/// Mark optimization remark \p R as verbose and return \p R.
///
/// \param R Remark to update
/// @return The forwarded remark \p R marked verbose.
template <class RemarkT>
decltype(auto)
operator<<(RemarkT &&R,
           std::enable_if_t<std::is_base_of_v<DiagnosticInfoOptimizationBase,
                                              std::remove_reference_t<RemarkT>>,
                            DiagnosticInfoOptimizationBase::setIsVerbose> V) {
  R.insert(V);
  return std::forward<RemarkT>(R);
}

/// Mark following arguments of \p R as record-only and return \p R.
///
/// \param R Remark to update
/// @return The forwarded remark \p R with following args marked record-only.
template <class RemarkT>
decltype(auto)
operator<<(RemarkT &&R,
           std::enable_if_t<std::is_base_of_v<DiagnosticInfoOptimizationBase,
                                              std::remove_reference_t<RemarkT>>,
                            DiagnosticInfoOptimizationBase::setExtraArgs> EA) {
  R.insert(EA);
  return std::forward<RemarkT>(R);
}

/// Common features for diagnostics dealing with optimization remarks
/// that are used by IR passes.
class LLVM_ABI DiagnosticInfoIROptimization
    : public DiagnosticInfoOptimizationBase {
  void anchor() override;
public:
  /// Construct an IR optimization remark for the given pass and code region.
  ///
  /// \param Kind Diagnostic kind
  /// \param Severity Severity of this diagnostic
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark (single-word,
  /// CamelCase)
  /// \param Fn Function where the diagnostic is being emitted
  /// \param Loc Location information to use in the diagnostic. If line table
  /// information is available, the diagnostic will include the source code
  /// location.
  /// \param CodeRegion IR value that the optimization operates on. This is
  /// currently used to provide run-time hotness information with PGO.
  DiagnosticInfoIROptimization(enum DiagnosticKind Kind,
                               enum DiagnosticSeverity Severity,
                               const char *PassName, StringRef RemarkName,
                               const Function &Fn,
                               const DiagnosticLocation &Loc,
                               const BasicBlock *CodeRegion = nullptr)
      : DiagnosticInfoOptimizationBase(Kind, Severity, PassName, RemarkName, Fn,
                                       Loc),
        CodeRegion(CodeRegion) {}

  /// Construct an IR optimization remark from an existing remark.
  ///
  /// This is useful when a transformation pass (e.g LV) wants to emit a remark
  /// (\p Orig) generated by one of its analyses (e.g. LAA) as its own analysis
  /// remark.  The string \p Prepend will be emitted before the original
  /// message.
  ///
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param Prepend Text emitted before the original message
  /// \param Orig Existing remark to copy from
  DiagnosticInfoIROptimization(const char *PassName, StringRef Prepend,
                               const DiagnosticInfoIROptimization &Orig)
      : DiagnosticInfoOptimizationBase(
            (DiagnosticKind)Orig.getKind(), Orig.getSeverity(), PassName,
            Orig.RemarkName, Orig.getFunction(), Orig.getLocation()),
        CodeRegion(Orig.getCodeRegion()) {
    *this << Prepend;
    llvm::append_range(Args, Orig.Args);
  }

  /// Construct a legacy IR optimization remark with message \p Msg.
  ///
  /// Note that this class does not copy this message, so this reference must be
  /// valid for the whole life time of the diagnostic.
  ///
  /// \param Kind Diagnostic kind
  /// \param Severity Severity of this diagnostic
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param Fn Function where the diagnostic is being emitted
  /// \param Loc Location information to use in the diagnostic. If line table
  /// information is available, the diagnostic will include the source code
  /// location.
  /// \param Msg Message to show
  DiagnosticInfoIROptimization(enum DiagnosticKind Kind,
                               enum DiagnosticSeverity Severity,
                               const char *PassName, const Function &Fn,
                               const DiagnosticLocation &Loc, const Twine &Msg)
      : DiagnosticInfoOptimizationBase(Kind, Severity, PassName, "", Fn, Loc) {
    *this << Msg.str();
  }

  /// Return the IR region the optimization operates on.
  ///
  /// @return The IR region the optimization operates on, or null if none.
  const BasicBlock *getCodeRegion() const { return CodeRegion; }

  /// Return true if \p DI is an IR optimization remark.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is an IR optimization remark.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() >= DK_FirstRemark && DI->getKind() <= DK_LastRemark;
  }

private:
  /// The IR region (currently basic block) that the optimization operates on.
  /// This is currently used to provide run-time hotness information with PGO.
  const BasicBlock *CodeRegion = nullptr;
};

/// Diagnostic information for applied optimization remarks.
class LLVM_ABI OptimizationRemark : public DiagnosticInfoIROptimization {
public:
  /// Construct an optimization remark for the given pass and code region.
  ///
  /// \param PassName Name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass=, then the diagnostic will
  /// be emitted.
  /// \param RemarkName Textual identifier for the remark (single-word,
  /// CamelCase)
  /// \param Loc Debug location for the remark
  /// \param CodeRegion Region that the optimization operates on
  OptimizationRemark(const char *PassName, StringRef RemarkName,
                     const DiagnosticLocation &Loc,
                     const BasicBlock *CodeRegion);

  /// Construct a remark deriving location from instruction \p Inst.
  ///
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark
  /// \param Inst Instruction used to derive debug location and code region
  OptimizationRemark(const char *PassName, StringRef RemarkName,
                     const Instruction *Inst);

  /// Construct a remark deriving location from function \p Func.
  ///
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark
  /// \param Func Function used to derive debug location and code region
  OptimizationRemark(const char *PassName, StringRef RemarkName,
                     const Function *Func);

  /// Return true if \p DI is an OptimizationRemark.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is an OptimizationRemark.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationRemark;
  }

  /// Return true if this remark is enabled by -Rpass=.
  ///
  /// \see DiagnosticInfoOptimizationBase::isEnabled
  ///
  /// @return True if this remark is enabled by -Rpass=.
  bool isEnabled() const override;

private:
  /// This is deprecated now and only used by the function API below.
  /// \p PassName is the name of the pass emitting this diagnostic. If
  /// this name matches the regular expression given in -Rpass=, then the
  /// diagnostic will be emitted. \p Fn is the function where the diagnostic
  /// is being emitted. \p Loc is the location information to use in the
  /// diagnostic. If line table information is available, the diagnostic
  /// will include the source code location. \p Msg is the message to show.
  /// Note that this class does not copy this message, so this reference
  /// must be valid for the whole life time of the diagnostic.
  OptimizationRemark(const char *PassName, const Function &Fn,
                     const DiagnosticLocation &Loc, const Twine &Msg)
      : DiagnosticInfoIROptimization(DK_OptimizationRemark, DS_Remark, PassName,
                                     Fn, Loc, Msg) {}
};

/// Diagnostic information for missed-optimization remarks.
class LLVM_ABI OptimizationRemarkMissed : public DiagnosticInfoIROptimization {
public:
  /// Construct a missed-optimization remark for the given pass and region.
  ///
  /// \param PassName Name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-missed=, then the
  /// diagnostic will be emitted.
  /// \param RemarkName Textual identifier for the remark (single-word,
  /// CamelCase)
  /// \param Loc Debug location for the remark
  /// \param CodeRegion Region that the optimization operates on
  OptimizationRemarkMissed(const char *PassName, StringRef RemarkName,
                           const DiagnosticLocation &Loc,
                           const BasicBlock *CodeRegion);

  /// Construct a missed remark deriving location from instruction \p Inst.
  ///
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark
  /// \param Inst Instruction used to derive code region and debug location
  OptimizationRemarkMissed(const char *PassName, StringRef RemarkName,
                           const Instruction *Inst);

  /// Construct a missed remark deriving location from function \p F.
  ///
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark
  /// \param F Function used to derive code region and debug location
  OptimizationRemarkMissed(const char *PassName, StringRef RemarkName,
                           const Function *F);

  /// Return true if \p DI is an OptimizationRemarkMissed.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is an OptimizationRemarkMissed.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationRemarkMissed;
  }

  /// Return true if this remark is enabled by -Rpass-missed=.
  ///
  /// \see DiagnosticInfoOptimizationBase::isEnabled
  ///
  /// @return True if this remark is enabled by -Rpass-missed=.
  bool isEnabled() const override;

private:
  /// This is deprecated now and only used by the function API below.
  /// \p PassName is the name of the pass emitting this diagnostic. If
  /// this name matches the regular expression given in -Rpass-missed=, then the
  /// diagnostic will be emitted. \p Fn is the function where the diagnostic
  /// is being emitted. \p Loc is the location information to use in the
  /// diagnostic. If line table information is available, the diagnostic
  /// will include the source code location. \p Msg is the message to show.
  /// Note that this class does not copy this message, so this reference
  /// must be valid for the whole life time of the diagnostic.
  OptimizationRemarkMissed(const char *PassName, const Function &Fn,
                           const DiagnosticLocation &Loc, const Twine &Msg)
      : DiagnosticInfoIROptimization(DK_OptimizationRemarkMissed, DS_Remark,
                                     PassName, Fn, Loc, Msg) {}
};

/// Diagnostic information for optimization analysis remarks.
class LLVM_ABI OptimizationRemarkAnalysis
    : public DiagnosticInfoIROptimization {
public:
  /// Construct an optimization analysis remark for the given pass and region.
  ///
  /// \param PassName Name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-analysis=, then the
  /// diagnostic will be emitted.
  /// \param RemarkName Textual identifier for the remark (single-word,
  /// CamelCase)
  /// \param Loc Debug location for the remark
  /// \param CodeRegion Region that the optimization operates on
  OptimizationRemarkAnalysis(const char *PassName, StringRef RemarkName,
                             const DiagnosticLocation &Loc,
                             const BasicBlock *CodeRegion);

  /// Construct an analysis remark from an existing remark.
  ///
  /// This is useful when a transformation pass (e.g LV) wants to emit a remark
  /// (\p Orig) generated by one of its analyses (e.g. LAA) as its own analysis
  /// remark.  The string \p Prepend will be emitted before the original
  /// message.
  ///
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param Prepend Text emitted before the original message
  /// \param Orig Existing remark to copy from
  OptimizationRemarkAnalysis(const char *PassName, StringRef Prepend,
                             const OptimizationRemarkAnalysis &Orig)
      : DiagnosticInfoIROptimization(PassName, Prepend, Orig) {}

  /// Construct an analysis remark deriving location from instruction \p Inst.
  ///
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark
  /// \param Inst Instruction used to derive code region and debug location
  OptimizationRemarkAnalysis(const char *PassName, StringRef RemarkName,
                             const Instruction *Inst);

  /// Construct an analysis remark deriving location from function \p F.
  ///
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark
  /// \param F Function used to derive code region and debug location
  OptimizationRemarkAnalysis(const char *PassName, StringRef RemarkName,
                             const Function *F);

  /// Return true if \p DI is an OptimizationRemarkAnalysis.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is an OptimizationRemarkAnalysis.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationRemarkAnalysis;
  }

  /// Return true if this remark is enabled by -Rpass-analysis=.
  ///
  /// \see DiagnosticInfoOptimizationBase::isEnabled
  ///
  /// @return True if this remark is enabled by -Rpass-analysis=.
  bool isEnabled() const override;

  /// Pass name that forces the remark to always be printed.
  static const char *AlwaysPrint;

  /// Return true if this remark should always be printed.
  ///
  /// @return True if this remark should always be printed.
  bool shouldAlwaysPrint() const { return getPassName() == AlwaysPrint; }

protected:
  /// Construct a legacy analysis remark with message \p Msg.
  ///
  /// \param Kind Diagnostic kind
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param Fn Function where the diagnostic is emitted
  /// \param Loc Location information for the diagnostic
  /// \param Msg Message to show
  OptimizationRemarkAnalysis(enum DiagnosticKind Kind, const char *PassName,
                             const Function &Fn, const DiagnosticLocation &Loc,
                             const Twine &Msg)
      : DiagnosticInfoIROptimization(Kind, DS_Remark, PassName, Fn, Loc, Msg) {}

  /// Construct an analysis remark of subclass kind \p Kind.
  ///
  /// \param Kind Diagnostic kind for the subclass
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark
  /// \param Loc Debug location for the remark
  /// \param CodeRegion Region that the optimization operates on
  OptimizationRemarkAnalysis(enum DiagnosticKind Kind, const char *PassName,
                             StringRef RemarkName,
                             const DiagnosticLocation &Loc,
                             const BasicBlock *CodeRegion);

private:
  /// This is deprecated now and only used by the function API below.
  /// \p PassName is the name of the pass emitting this diagnostic. If
  /// this name matches the regular expression given in -Rpass-analysis=, then
  /// the diagnostic will be emitted. \p Fn is the function where the diagnostic
  /// is being emitted. \p Loc is the location information to use in the
  /// diagnostic. If line table information is available, the diagnostic will
  /// include the source code location. \p Msg is the message to show. Note that
  /// this class does not copy this message, so this reference must be valid for
  /// the whole life time of the diagnostic.
  OptimizationRemarkAnalysis(const char *PassName, const Function &Fn,
                             const DiagnosticLocation &Loc, const Twine &Msg)
      : DiagnosticInfoIROptimization(DK_OptimizationRemarkAnalysis, DS_Remark,
                                     PassName, Fn, Loc, Msg) {}
};

/// Diagnostic information for optimization analysis remarks related to
/// floating-point non-commutativity.
class LLVM_ABI OptimizationRemarkAnalysisFPCommute
    : public OptimizationRemarkAnalysis {
  void anchor() override;
public:
  /// Construct an FP-commutativity analysis remark.
  ///
  /// The front-end will append its own message related to options that address
  /// floating-point non-commutativity.
  ///
  /// \param PassName Name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-analysis=, then the
  /// diagnostic will be emitted.
  /// \param RemarkName Textual identifier for the remark (single-word,
  /// CamelCase)
  /// \param Loc Debug location for the remark
  /// \param CodeRegion Region that the optimization operates on
  OptimizationRemarkAnalysisFPCommute(const char *PassName,
                                      StringRef RemarkName,
                                      const DiagnosticLocation &Loc,
                                      const BasicBlock *CodeRegion)
      : OptimizationRemarkAnalysis(DK_OptimizationRemarkAnalysisFPCommute,
                                   PassName, RemarkName, Loc, CodeRegion) {}

  /// Return true if \p DI is an OptimizationRemarkAnalysisFPCommute.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is an OptimizationRemarkAnalysisFPCommute.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationRemarkAnalysisFPCommute;
  }

private:
  /// This is deprecated now and only used by the function API below.
  /// \p PassName is the name of the pass emitting this diagnostic. If
  /// this name matches the regular expression given in -Rpass-analysis=, then
  /// the diagnostic will be emitted. \p Fn is the function where the diagnostic
  /// is being emitted. \p Loc is the location information to use in the
  /// diagnostic. If line table information is available, the diagnostic will
  /// include the source code location. \p Msg is the message to show. The
  /// front-end will append its own message related to options that address
  /// floating-point non-commutativity. Note that this class does not copy this
  /// message, so this reference must be valid for the whole life time of the
  /// diagnostic.
  OptimizationRemarkAnalysisFPCommute(const char *PassName, const Function &Fn,
                                      const DiagnosticLocation &Loc,
                                      const Twine &Msg)
      : OptimizationRemarkAnalysis(DK_OptimizationRemarkAnalysisFPCommute,
                                   PassName, Fn, Loc, Msg) {}
};

/// Diagnostic information for optimization analysis remarks related to
/// pointer aliasing.
class LLVM_ABI OptimizationRemarkAnalysisAliasing
    : public OptimizationRemarkAnalysis {
  void anchor() override;
public:
  /// Construct a pointer-aliasing analysis remark.
  ///
  /// The front-end will append its own message related to options that address
  /// pointer aliasing legality.
  ///
  /// \param PassName Name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-analysis=, then the
  /// diagnostic will be emitted.
  /// \param RemarkName Textual identifier for the remark (single-word,
  /// CamelCase)
  /// \param Loc Debug location for the remark
  /// \param CodeRegion Region that the optimization operates on
  OptimizationRemarkAnalysisAliasing(const char *PassName, StringRef RemarkName,
                                     const DiagnosticLocation &Loc,
                                     const BasicBlock *CodeRegion)
      : OptimizationRemarkAnalysis(DK_OptimizationRemarkAnalysisAliasing,
                                   PassName, RemarkName, Loc, CodeRegion) {}

  /// Return true if \p DI is an OptimizationRemarkAnalysisAliasing.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is an OptimizationRemarkAnalysisAliasing.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationRemarkAnalysisAliasing;
  }

private:
  /// This is deprecated now and only used by the function API below.
  /// \p PassName is the name of the pass emitting this diagnostic. If
  /// this name matches the regular expression given in -Rpass-analysis=, then
  /// the diagnostic will be emitted. \p Fn is the function where the diagnostic
  /// is being emitted. \p Loc is the location information to use in the
  /// diagnostic. If line table information is available, the diagnostic will
  /// include the source code location. \p Msg is the message to show. The
  /// front-end will append its own message related to options that address
  /// pointer aliasing legality. Note that this class does not copy this
  /// message, so this reference must be valid for the whole life time of the
  /// diagnostic.
  OptimizationRemarkAnalysisAliasing(const char *PassName, const Function &Fn,
                                     const DiagnosticLocation &Loc,
                                     const Twine &Msg)
      : OptimizationRemarkAnalysis(DK_OptimizationRemarkAnalysisAliasing,
                                   PassName, Fn, Loc, Msg) {}
};

/// Diagnostic information for machine IR parser.
// FIXME: Remove this, use DiagnosticInfoSrcMgr instead.
class LLVM_ABI DiagnosticInfoMIRParser : public DiagnosticInfo {
  const SMDiagnostic &Diagnostic;

public:
  /// Construct a MIR parser diagnostic.
  ///
  /// \param Severity Severity of this diagnostic
  /// \param Diagnostic Underlying SMDiagnostic
  DiagnosticInfoMIRParser(DiagnosticSeverity Severity,
                          const SMDiagnostic &Diagnostic)
      : DiagnosticInfo(DK_MIRParser, Severity), Diagnostic(Diagnostic) {}

  /// Return the underlying SMDiagnostic.
  ///
  /// @return The underlying SMDiagnostic.
  const SMDiagnostic &getDiagnostic() const { return Diagnostic; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoMIRParser.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoMIRParser.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_MIRParser;
  }
};

/// Diagnostic information for IR instrumentation reporting.
class LLVM_ABI DiagnosticInfoInstrumentation : public DiagnosticInfo {
  const Twine &Msg;

public:
  /// Construct an instrumentation diagnostic with message \p DiagMsg.
  ///
  /// \param DiagMsg Message to report
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoInstrumentation(const Twine &DiagMsg,
                                DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfo(DK_Instrumentation, Severity), Msg(DiagMsg) {}

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoInstrumentation.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoInstrumentation.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_Instrumentation;
  }
};

/// Diagnostic information for ISel fallback path.
class LLVM_ABI DiagnosticInfoISelFallback : public DiagnosticInfo {
  /// The function that is concerned by this diagnostic.
  const Function &Fn;

public:
  /// Construct an ISel-fallback diagnostic for function \p Fn.
  ///
  /// \param Fn Function concerned by this diagnostic
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoISelFallback(const Function &Fn,
                             DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfo(DK_ISelFallback, Severity), Fn(Fn) {}

  /// Return the function concerned by this diagnostic.
  ///
  /// @return The function concerned by this diagnostic.
  const Function &getFunction() const { return Fn; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoISelFallback.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoISelFallback.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_ISelFallback;
  }
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
/// Convert an opaque \c LLVMDiagnosticInfoRef to a \c DiagnosticInfo pointer.
///
/// \param P Opaque C API diagnostic reference to unwrap.
/// @return The DiagnosticInfo pointer corresponding to \p P.
inline DiagnosticInfo *unwrap(LLVMDiagnosticInfoRef P) {
  return reinterpret_cast<DiagnosticInfo *>(P);
}

/// Convert a \c DiagnosticInfo pointer to an opaque \c LLVMDiagnosticInfoRef.
///
/// \param P DiagnosticInfo to wrap for the C API.
/// @return An opaque C API diagnostic reference for \p P.
inline LLVMDiagnosticInfoRef wrap(const DiagnosticInfo *P) {
  return reinterpret_cast<LLVMDiagnosticInfoRef>(
      const_cast<DiagnosticInfo *>(P));
}

/// Diagnostic information for optimization failures.
class LLVM_ABI DiagnosticInfoOptimizationFailure
    : public DiagnosticInfoIROptimization {
public:
  /// Construct an optimization-failure diagnostic with message \p Msg.
  ///
  /// Note that this class does not copy this message, so this reference must be
  /// valid for the whole life time of the diagnostic.
  ///
  /// \param Fn Function where the diagnostic is being emitted
  /// \param Loc Location information to use in the diagnostic. If line table
  /// information is available, the diagnostic will include the source code
  /// location.
  /// \param Msg Message to show
  DiagnosticInfoOptimizationFailure(const Function &Fn,
                                    const DiagnosticLocation &Loc,
                                    const Twine &Msg)
      : DiagnosticInfoIROptimization(DK_OptimizationFailure, DS_Warning,
                                     nullptr, Fn, Loc, Msg) {}

  /// Construct an optimization-failure remark for the given pass and region.
  ///
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark (single-word,
  /// CamelCase)
  /// \param Loc Debug location for the remark
  /// \param CodeRegion Region that the optimization operates on
  DiagnosticInfoOptimizationFailure(const char *PassName, StringRef RemarkName,
                                    const DiagnosticLocation &Loc,
                                    const BasicBlock *CodeRegion);

  /// Return true if \p DI is a DiagnosticInfoOptimizationFailure.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoOptimizationFailure.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_OptimizationFailure;
  }

  /// Return true if this optimization-failure diagnostic is enabled.
  ///
  /// \see DiagnosticInfoOptimizationBase::isEnabled
  ///
  /// @return True if this optimization-failure diagnostic is enabled.
  bool isEnabled() const override;
};

/// Diagnostic information for unsupported feature in backend.
class LLVM_ABI DiagnosticInfoUnsupported
    : public DiagnosticInfoWithLocationBase {
private:
  const Twine &Msg;

public:
  /// Construct an unsupported-feature diagnostic for function \p Fn.
  ///
  /// Note that this class does not copy this message, so this reference must be
  /// valid for the whole life time of the diagnostic.
  ///
  /// \param Fn Function where the diagnostic is being emitted
  /// \param Msg Message to show
  /// \param Loc Location information to use in the diagnostic. If line table
  /// information is available, the diagnostic will include the source code
  /// location.
  /// \param Severity Severity of this diagnostic
  DiagnosticInfoUnsupported(
      const Function &Fn, const Twine &Msg LLVM_LIFETIME_BOUND,
      const DiagnosticLocation &Loc = DiagnosticLocation(),
      DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfoWithLocationBase(DK_Unsupported, Severity, Fn, Loc),
        Msg(Msg) {}

  /// Return true if \p DI is a DiagnosticInfoUnsupported.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoUnsupported.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_Unsupported;
  }

  /// Return the diagnostic message.
  ///
  /// @return The diagnostic message.
  const Twine &getMessage() const { return Msg; }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  void print(DiagnosticPrinter &DP) const override;
};

/// Diagnostic information for unsupported target intrinsics in backend.
class LLVM_ABI DiagnosticInfoUnsupportedTargetIntrinsic
    : public DiagnosticInfoWithLocationBase {
private:
  unsigned IntrinsicID;
  StringRef RequiredFeatures;

public:
  /// Construct an unsupported-target-intrinsic diagnostic.
  ///
  /// \param Fn Function where the diagnostic is being emitted
  /// \param IntrinsicID Unsupported intrinsic identifier
  /// \param Loc Location information for the diagnostic
  DiagnosticInfoUnsupportedTargetIntrinsic(
      const Function &Fn, unsigned IntrinsicID,
      const DiagnosticLocation &Loc = DiagnosticLocation());

  /// Return true if \p DI is a DiagnosticInfoUnsupportedTargetIntrinsic.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoUnsupportedTargetIntrinsic.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_UnsupportedTargetIntrinsic;
  }

  /// Return the unsupported intrinsic identifier.
  ///
  /// @return The unsupported intrinsic identifier.
  unsigned getIntrinsicID() const { return IntrinsicID; }
  /// Return the required target features for the intrinsic.
  ///
  /// @return The required target features for the intrinsic.
  StringRef getRequiredFeatures() const { return RequiredFeatures; }
  /// Return the formatted diagnostic message.
  ///
  /// @return The formatted diagnostic message.
  std::string getMessage() const;

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  void print(DiagnosticPrinter &DP) const override;
};

/// Diagnostic information for MisExpect analysis.
class LLVM_ABI DiagnosticInfoMisExpect : public DiagnosticInfoWithLocationBase {
public:
  /// Construct a MisExpect diagnostic for instruction \p Inst.
  ///
  /// \param Inst Instruction with the mismatched expectation
  /// \param Msg Message to report
  DiagnosticInfoMisExpect(const Instruction *Inst,
                          const Twine &Msg LLVM_LIFETIME_BOUND);

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  /// \see DiagnosticInfo::print
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoMisExpect.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoMisExpect.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_MisExpect;
  }

  /// Return the diagnostic message.
  ///
  /// @return The diagnostic message.
  const Twine &getMsg() const { return Msg; }

private:
  /// Message to report.
  const Twine &Msg;
};

static DiagnosticSeverity getDiagnosticSeverity(SourceMgr::DiagKind DK) {
  switch (DK) {
  case llvm::SourceMgr::DK_Error:
    return DS_Error;
    break;
  case llvm::SourceMgr::DK_Warning:
    return DS_Warning;
    break;
  case llvm::SourceMgr::DK_Note:
    return DS_Note;
    break;
  case llvm::SourceMgr::DK_Remark:
    return DS_Remark;
    break;
  }
  llvm_unreachable("unknown SourceMgr::DiagKind");
}

/// Diagnostic information for SMDiagnostic reporting.
class LLVM_ABI DiagnosticInfoSrcMgr : public DiagnosticInfo {
  const SMDiagnostic &Diagnostic;
  StringRef ModName;

  // For inlineasm !srcloc translation.
  bool InlineAsmDiag;
  uint64_t LocCookie;

public:
  /// Construct a SourceMgr diagnostic for module \p ModName.
  ///
  /// \param Diagnostic Underlying SMDiagnostic
  /// \param ModName Module name associated with this diagnostic
  /// \param InlineAsmDiag Whether this is an inline asm diagnostic
  /// \param LocCookie Optional location cookie for !srcloc translation
  DiagnosticInfoSrcMgr(const SMDiagnostic &Diagnostic, StringRef ModName,
                       bool InlineAsmDiag = true, uint64_t LocCookie = 0)
      : DiagnosticInfo(DK_SrcMgr, getDiagnosticSeverity(Diagnostic.getKind())),
        Diagnostic(Diagnostic), ModName(ModName), InlineAsmDiag(InlineAsmDiag),
        LocCookie(LocCookie) {}

  /// Return the module name associated with this diagnostic.
  ///
  /// @return The module name associated with this diagnostic.
  StringRef getModuleName() const { return ModName; }
  /// Return true if this is an inline asm diagnostic.
  ///
  /// @return True if this is an inline asm diagnostic.
  bool isInlineAsmDiag() const { return InlineAsmDiag; }
  /// Return the underlying SMDiagnostic.
  ///
  /// @return The underlying SMDiagnostic.
  const SMDiagnostic &getSMDiag() const { return Diagnostic; }
  /// Return the optional location cookie.
  ///
  /// @return The optional location cookie.
  uint64_t getLocCookie() const { return LocCookie; }
  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  void print(DiagnosticPrinter &DP) const override;

  /// Return true if \p DI is a DiagnosticInfoSrcMgr.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoSrcMgr.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_SrcMgr;
  }
};

/// Emit a dontcall diagnostic for call instruction \p CI.
///
/// \param CI Call that violates a dontcall attribute
LLVM_ABI void diagnoseDontCall(const CallInst &CI);

/// Inlining location extracted from debug info.
struct DebugInlineInfo {
  /// Name of the inlined function.
  StringRef FuncName;
  /// Source file name for the inlining location.
  StringRef Filename;
  /// Line number of the inlining location.
  unsigned Line;
  /// Column number of the inlining location.
  unsigned Column;
};

/// Diagnostic for a call that violates a dontcall attribute.
class LLVM_ABI DiagnosticInfoDontCall : public DiagnosticInfo {
  StringRef CalleeName;
  StringRef Note;
  uint64_t LocCookie;
  MDNode *InlinedFromMD = nullptr;
  SmallVector<DebugInlineInfo, 4> DebugInlineChain;

public:
  /// Construct a dontcall diagnostic for callee \p CalleeName.
  ///
  /// \param CalleeName Name of the called function
  /// \param Note Additional note text
  /// \param DS Severity of this diagnostic
  /// \param LocCookie Optional location cookie
  /// \param InlinedFromMD Optional inline-from metadata node
  DiagnosticInfoDontCall(StringRef CalleeName, StringRef Note,
                         DiagnosticSeverity DS, uint64_t LocCookie,
                         MDNode *InlinedFromMD = nullptr)
      : DiagnosticInfo(DK_DontCall, DS), CalleeName(CalleeName), Note(Note),
        LocCookie(LocCookie), InlinedFromMD(InlinedFromMD) {}

  /// Return the callee function name.
  ///
  /// @return The callee function name.
  StringRef getFunctionName() const { return CalleeName; }
  /// Return the additional note text.
  ///
  /// @return The additional note text.
  StringRef getNote() const { return Note; }
  /// Return the optional location cookie.
  ///
  /// @return The optional location cookie.
  uint64_t getLocCookie() const { return LocCookie; }
  /// Return the optional inline-from metadata node.
  ///
  /// @return The optional inline-from metadata node, or null if none.
  MDNode *getInlinedFromMD() const { return InlinedFromMD; }
  /// Return inlining decisions associated with this diagnostic.
  ///
  /// @return Inlining decisions associated with this diagnostic.
  SmallVector<std::pair<StringRef, uint64_t>> getInliningDecisions() const;

  /// Set the debug inline chain to \p Chain.
  ///
  /// \param Chain Inlining locations extracted from debug info
  void setDebugInlineChain(SmallVector<DebugInlineInfo, 4> &&Chain) {
    DebugInlineChain = std::move(Chain);
  }
  /// Return the debug inline chain.
  ///
  /// @return The debug inline chain.
  ArrayRef<DebugInlineInfo> getDebugInlineChain() const {
    return DebugInlineChain;
  }

  /// Print this diagnostic using \p DP.
  ///
  /// \param DP Printer that receives the message
  void print(DiagnosticPrinter &DP) const override;
  /// Return true if \p DI is a DiagnosticInfoDontCall.
  ///
  /// \param DI Diagnostic to test
  /// @return True if \p DI is a DiagnosticInfoDontCall.
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_DontCall;
  }
};

} // end namespace llvm

#endif // LLVM_IR_DIAGNOSTICINFO_H
