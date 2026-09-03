//===- llvm/DebugInfo/Symbolize/DIPrinter.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the DIPrinter class, which is responsible for printing
// structures defined in DebugInfo/DIContext.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_SYMBOLIZE_DIPRINTER_H
#define LLVM_DEBUGINFO_SYMBOLIZE_DIPRINTER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/JSON.h"
#include <memory>
#include <vector>

namespace llvm {
struct DILineInfo;
class DIInliningInfo;
struct DIGlobal;
struct DILocal;
class ErrorInfoBase;
class raw_ostream;

/// Namespace for LLVM symbolization APIs and result printers.
namespace symbolize {

/// Helper that loads and formats nearby source lines for a location.
class SourceCode;

/// Describes one symbolization query presented to a printer.
struct Request {
  /// Name of the module (object file) being symbolized.
  StringRef ModuleName;
  /// Optional address within the module for the query.
  std::optional<uint64_t> Address;
  /// Optional symbol name when the query is by name rather than address.
  StringRef Symbol;
};

/// Abstract interface for printing symbolization results.
class DIPrinter {
public:
  /// Construct a printer with default state.
  DIPrinter() = default;
  /// Destroy the printer.
  virtual ~DIPrinter() = default;

  /// Print a single source line location for \p Request.
  /// @param Request Symbolization query that produced \p Info.
  /// @param Info Source line information to print.
  virtual void print(const Request &Request, const DILineInfo &Info) = 0;
  /// Print an inlining stack for \p Request.
  /// @param Request Symbolization query that produced \p Info.
  /// @param Info Inlining frames to print, innermost first.
  virtual void print(const Request &Request, const DIInliningInfo &Info) = 0;
  /// Print a global data symbol for \p Request.
  /// @param Request Symbolization query that produced \p Global.
  /// @param Global Global variable or data symbol information.
  virtual void print(const Request &Request, const DIGlobal &Global) = 0;
  /// Print local variables for \p Request.
  /// @param Request Symbolization query that produced \p Locals.
  /// @param Locals Local variable descriptors to print.
  virtual void print(const Request &Request,
                     const std::vector<DILocal> &Locals) = 0;
  /// Print multiple source locations for \p Request.
  /// @param Request Symbolization query that produced \p Locations.
  /// @param Locations Source line locations to print.
  virtual void print(const Request &Request,
                     const std::vector<DILineInfo> &Locations) = 0;

  /// Report a symbolization error for \p Request.
  ///
  /// @returns true if the caller should also print an empty result.
  /// @param Request Symbolization query that failed.
  /// @param ErrorInfo Details of the failure.
  virtual bool printError(const Request &Request,
                          const ErrorInfoBase &ErrorInfo) = 0;

  /// Begin a grouped list of printed results.
  virtual void listBegin() = 0;
  /// End a grouped list of printed results.
  virtual void listEnd() = 0;
};

/// Options controlling how printers format symbolization output.
struct PrinterConfig {
  /// If true, print the requested address before each result.
  bool PrintAddress;
  /// If true, print function names with locations.
  bool PrintFunctions;
  /// If true, use more readable spacing and delimiters.
  bool Pretty;
  /// If true, print verbose field-oriented location details.
  bool Verbose;
  /// Number of surrounding source lines to show for each location.
  int SourceContextLines;
};

/// Callback invoked when a printer reports an error.
using ErrorHandler = std::function<void(const ErrorInfoBase &, StringRef)>;

/// Base class for plain-text LLVM and GNU-style symbolization printers.
class LLVM_ABI PlainPrinterBase : public DIPrinter {
protected:
  /// Stream that receives printed output.
  raw_ostream &OS;
  /// Callback used to report symbolization errors.
  ErrorHandler ErrHandler;
  /// Formatting options for this printer.
  PrinterConfig Config;

  /// Print one line-info frame, marking it as inlined when \p Inlined is set.
  /// @param Info Source line information for the frame.
  /// @param Inlined True when this frame is an inlined caller.
  void print(const DILineInfo &Info, bool Inlined);
  /// Print \p FunctionName when function printing is enabled.
  /// @param FunctionName Name of the function for this frame.
  /// @param Inlined True when this frame is an inlined caller.
  void printFunctionName(StringRef FunctionName, bool Inlined);
  /// Print the compact filename/line form of a location.
  /// @param Filename Source file name for the location.
  /// @param Info Source line information to print.
  virtual void printSimpleLocation(StringRef Filename,
                                   const DILineInfo &Info) = 0;
  /// Print surrounding source context for \p SourceCode.
  /// @param SourceCode Loaded source snippet to format.
  void printContext(SourceCode SourceCode);
  /// Print verbose field-oriented location details.
  /// @param Filename Source file name for the location.
  /// @param Info Source line information to print.
  void printVerbose(StringRef Filename, const DILineInfo &Info);
  /// Optionally print the function start address from \p Info.
  /// @param Info Source line information that may include a start address.
  virtual void printStartAddress(const DILineInfo &Info) {}
  /// Print any trailing text after a complete result.
  virtual void printFooter() {}

private:
  void printHeader(std::optional<uint64_t> Address);

public:
  /// Construct a plain printer writing to \p OS.
  /// @param OS Output stream for printed results.
  /// @param EH Callback invoked for symbolization errors.
  /// @param Config Formatting options for this printer.
  PlainPrinterBase(raw_ostream &OS, ErrorHandler EH, PrinterConfig &Config)
      : OS(OS), ErrHandler(EH), Config(Config) {}

  /// Print a single source line location for \p Request.
  /// @param Request Symbolization query that produced \p Info.
  /// @param Info Source line information to print.
  void print(const Request &Request, const DILineInfo &Info) override;
  /// Print an inlining stack for \p Request.
  /// @param Request Symbolization query that produced \p Info.
  /// @param Info Inlining frames to print, innermost first.
  void print(const Request &Request, const DIInliningInfo &Info) override;
  /// Print a global data symbol for \p Request.
  /// @param Request Symbolization query that produced \p Global.
  /// @param Global Global variable or data symbol information.
  void print(const Request &Request, const DIGlobal &Global) override;
  /// Print local variables for \p Request.
  /// @param Request Symbolization query that produced \p Locals.
  /// @param Locals Local variable descriptors to print.
  void print(const Request &Request,
             const std::vector<DILocal> &Locals) override;
  /// Print multiple source locations for \p Request.
  /// @param Request Symbolization query that produced \p Locations.
  /// @param Locations Source line locations to print.
  void print(const Request &Request,
             const std::vector<DILineInfo> &Locations) override;

  /// Report a symbolization error via the error handler.
  ///
  /// @returns true so the caller also prints an empty result.
  /// @param Request Symbolization query that failed.
  /// @param ErrorInfo Details of the failure.
  bool printError(const Request &Request,
                  const ErrorInfoBase &ErrorInfo) override;

  /// No-op list begin for plain printers.
  void listBegin() override {}
  /// No-op list end for plain printers.
  void listEnd() override {}
};

/// Plain printer that formats output in LLVM llvm-symbolizer style.
class LLVM_ABI LLVMPrinter : public PlainPrinterBase {
private:
  void printSimpleLocation(StringRef Filename, const DILineInfo &Info) override;
  void printStartAddress(const DILineInfo &Info) override;
  void printFooter() override;

public:
  /// Construct an LLVM-style printer writing to \p OS.
  /// @param OS Output stream for printed results.
  /// @param EH Callback invoked for symbolization errors.
  /// @param Config Formatting options for this printer.
  LLVMPrinter(raw_ostream &OS, ErrorHandler EH, PrinterConfig &Config)
      : PlainPrinterBase(OS, EH, Config) {}
};

/// Plain printer that formats output in GNU addr2line style.
class LLVM_ABI GNUPrinter : public PlainPrinterBase {
private:
  void printSimpleLocation(StringRef Filename, const DILineInfo &Info) override;

public:
  /// Construct a GNU-style printer writing to \p OS.
  /// @param OS Output stream for printed results.
  /// @param EH Callback invoked for symbolization errors.
  /// @param Config Formatting options for this printer.
  GNUPrinter(raw_ostream &OS, ErrorHandler EH, PrinterConfig &Config)
      : PlainPrinterBase(OS, EH, Config) {}
};

/// Printer that emits symbolization results as JSON objects.
class LLVM_ABI JSONPrinter : public DIPrinter {
private:
  raw_ostream &OS;
  PrinterConfig Config;
  std::unique_ptr<json::Array> ObjectList;

  void printJSON(const json::Value &V) {
    json::OStream JOS(OS, Config.Pretty ? 2 : 0);
    JOS.value(V);
    OS << '\n';
  }

public:
  /// Construct a JSON printer writing to \p OS.
  /// @param OS Output stream for JSON results.
  /// @param Config Formatting options such as pretty-printing.
  JSONPrinter(raw_ostream &OS, PrinterConfig &Config)
      : OS(OS), Config(Config) {}

  /// Print a single source line location as JSON for \p Request.
  /// @param Request Symbolization query that produced \p Info.
  /// @param Info Source line information to print.
  void print(const Request &Request, const DILineInfo &Info) override;
  /// Print an inlining stack as JSON for \p Request.
  /// @param Request Symbolization query that produced \p Info.
  /// @param Info Inlining frames to print, innermost first.
  void print(const Request &Request, const DIInliningInfo &Info) override;
  /// Print a global data symbol as JSON for \p Request.
  /// @param Request Symbolization query that produced \p Global.
  /// @param Global Global variable or data symbol information.
  void print(const Request &Request, const DIGlobal &Global) override;
  /// Print local variables as JSON for \p Request.
  /// @param Request Symbolization query that produced \p Locals.
  /// @param Locals Local variable descriptors to print.
  void print(const Request &Request,
             const std::vector<DILocal> &Locals) override;
  /// Print multiple source locations as JSON for \p Request.
  /// @param Request Symbolization query that produced \p Locations.
  /// @param Locations Source line locations to print.
  void print(const Request &Request,
             const std::vector<DILineInfo> &Locations) override;

  /// Emit a JSON object describing a symbolization error.
  ///
  /// @returns false because the error is already included in the JSON output.
  /// @param Request Symbolization query that failed.
  /// @param ErrorInfo Details of the failure.
  bool printError(const Request &Request,
                  const ErrorInfoBase &ErrorInfo) override;

  /// Begin collecting printed objects into a JSON array.
  void listBegin() override;
  /// Finish the JSON array opened by listBegin and print it.
  void listEnd() override;
};
} // namespace symbolize
} // namespace llvm

#endif
