//===- LinePrinter.h ------------------------------------------ *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_LINEPRINTER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_LINEPRINTER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/DebugInfo/PDB/Native/FormatUtil.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"

#include <list>

// Container for filter options to control which elements will be printed.
struct FilterOptions {
  std::list<std::string> ExcludeTypes;
  std::list<std::string> ExcludeSymbols;
  std::list<std::string> ExcludeCompilands;
  std::list<std::string> IncludeTypes;
  std::list<std::string> IncludeSymbols;
  std::list<std::string> IncludeCompilands;
  uint32_t PaddingThreshold;
  uint32_t SizeThreshold;
  std::optional<uint32_t> DumpModi;
  std::optional<uint32_t> ParentRecurseDepth;
  std::optional<uint32_t> ChildrenRecurseDepth;
  std::optional<uint32_t> SymbolOffset;
  bool JustMyCode;
};

namespace llvm {
namespace msf {
class MSFStreamLayout;
} // namespace msf
namespace pdb {

class ClassLayout;
class PDBFile;
class SymbolGroup;
class WithColor;

/// Formats indented, optionally colored text for dumping PDB contents.
class LinePrinter {
  friend class pdb::WithColor;

public:
  /// Construct a line printer writing to \p Stream.
  ///
  /// \param Indent Number of spaces to add or remove for each Indent/Unindent
  ///     call when an explicit amount is not supplied.
  /// \param UseColor Whether colored output is enabled.
  /// \param Stream The output stream to write to.
  /// \param Filters Filter options controlling which items are printed.
  LLVM_ABI LinePrinter(int Indent, bool UseColor, raw_ostream &Stream,
                       const FilterOptions &Filters);

  /// Increase the current indentation level.
  ///
  /// \param Amount Spaces to add; if zero, uses the default indent width.
  LLVM_ABI void Indent(uint32_t Amount = 0);
  /// Decrease the current indentation level.
  ///
  /// \param Amount Spaces to remove; if zero, uses the default indent width.
  LLVM_ABI void Unindent(uint32_t Amount = 0);
  /// Write a newline and indent to the current indentation level.
  LLVM_ABI void NewLine();

  /// Print \p T on a new indented line.
  ///
  /// \param T The text to print.
  LLVM_ABI void printLine(const Twine &T);
  /// Print \p T to the stream without starting a new line.
  ///
  /// \param T The text to print.
  LLVM_ABI void print(const Twine &T);
  /// Format arguments with \p Fmt and print the result on a new indented line.
  ///
  /// \param Fmt A formatv format string.
  /// \param Items Values substituted into \p Fmt.
  template <typename... Ts> void formatLine(const char *Fmt, Ts &&...Items) {
    printLine(formatv(Fmt, std::forward<Ts>(Items)...));
  }
  /// Format arguments with \p Fmt and print the result without a new line.
  ///
  /// \param Fmt A formatv format string.
  /// \param Items Values substituted into \p Fmt.
  template <typename... Ts> void format(const char *Fmt, Ts &&...Items) {
    print(formatv(Fmt, std::forward<Ts>(Items)...));
  }

  /// Print a labeled hex dump of \p Data starting at \p StartOffset.
  ///
  /// \param Label Text printed before the dump.
  /// \param Data Bytes to format.
  /// \param StartOffset Offset displayed beside the first byte.
  LLVM_ABI void formatBinary(StringRef Label, ArrayRef<uint8_t> Data,
                             uint64_t StartOffset);
  /// Print a labeled hex dump of \p Data with addresses based at \p BaseAddr.
  ///
  /// \param Label Text printed before the dump.
  /// \param Data Bytes to format.
  /// \param BaseAddr Base address added to \p StartOffset for display.
  /// \param StartOffset Offset from \p BaseAddr of the first byte.
  LLVM_ABI void formatBinary(StringRef Label, ArrayRef<uint8_t> Data,
                             uint64_t BaseAddr, uint64_t StartOffset);

  /// Dump a slice of an MSF stream identified by index.
  ///
  /// \param Label Text printed before the dump.
  /// \param File The PDB file owning the stream.
  /// \param StreamIdx Index of the MSF stream to dump.
  /// \param StreamPurpose Human-readable description of the stream's role.
  /// \param Offset Byte offset into the stream at which to start.
  /// \param Size Number of bytes to dump, or zero for the remainder.
  LLVM_ABI void formatMsfStreamData(StringRef Label, PDBFile &File,
                                    uint32_t StreamIdx, StringRef StreamPurpose,
                                    uint64_t Offset, uint64_t Size);
  /// Dump a substream of an MSF stream using its layout.
  ///
  /// \param Label Text printed before the dump.
  /// \param File The PDB file providing block size and data.
  /// \param Stream Layout of the MSF stream being dumped.
  /// \param Substream Byte range within the stream to format.
  LLVM_ABI void formatMsfStreamData(StringRef Label, PDBFile &File,
                                    const msf::MSFStreamLayout &Stream,
                                    BinarySubstreamRef Substream);
  /// Dump every MSF block occupied by \p Stream.
  ///
  /// \param File The PDB file providing block size and data.
  /// \param Stream Layout describing which blocks belong to the stream.
  LLVM_ABI void formatMsfStreamBlocks(PDBFile &File,
                                      const msf::MSFStreamLayout &Stream);

  /// Return whether colored output is enabled.
  ///
  /// \returns True if colored output is enabled.
  bool hasColor() const { return UseColor; }
  /// Return the underlying output stream.
  ///
  /// \returns The underlying output stream.
  raw_ostream &getStream() { return OS; }
  /// Return the current indentation width in spaces.
  ///
  /// \returns The current indentation width in spaces.
  int getIndentLevel() const { return CurrentIndent; }

  /// Return true if \p Class should be omitted from output by the filters.
  ///
  /// \param Class The class layout to test against type and padding filters.
  ///
  /// \returns True if the class should be omitted from output.
  LLVM_ABI bool IsClassExcluded(const ClassLayout &Class);
  /// Return true if a type with \p TypeName and \p Size should be omitted.
  ///
  /// \param TypeName The type name to match against include/exclude filters.
  /// \param Size The type size in bytes, compared to the size threshold.
  ///
  /// \returns True if the type should be omitted from output.
  LLVM_ABI bool IsTypeExcluded(llvm::StringRef TypeName, uint64_t Size);
  /// Return true if a symbol named \p SymbolName should be omitted.
  ///
  /// \param SymbolName The symbol name to match against include/exclude
  ///     filters.
  ///
  /// \returns True if the symbol should be omitted from output.
  LLVM_ABI bool IsSymbolExcluded(llvm::StringRef SymbolName);
  /// Return true if a compiland named \p CompilandName should be omitted.
  ///
  /// \param CompilandName The compiland name to match against include/exclude
  ///     filters.
  ///
  /// \returns True if the compiland should be omitted from output.
  LLVM_ABI bool IsCompilandExcluded(llvm::StringRef CompilandName);

  /// Return the filter options used by this printer.
  ///
  /// \returns The filter options used by this printer.
  const FilterOptions &getFilters() const { return Filters; }

private:
  template <typename Iter>
  void SetFilters(std::list<Regex> &List, Iter Begin, Iter End) {
    List.clear();
    for (; Begin != End; ++Begin)
      List.emplace_back(StringRef(*Begin));
  }

  raw_ostream &OS;
  int IndentSpaces;
  int CurrentIndent;
  bool UseColor;
  const FilterOptions &Filters;

  std::list<Regex> ExcludeCompilandFilters;
  std::list<Regex> ExcludeTypeFilters;
  std::list<Regex> ExcludeSymbolFilters;

  std::list<Regex> IncludeCompilandFilters;
  std::list<Regex> IncludeTypeFilters;
  std::list<Regex> IncludeSymbolFilters;
};

/// Holds a line printer together with indent and label-width settings.
struct PrintScope {
  /// Construct a print scope for \p P with indentation \p IndentLevel.
  ///
  /// \param P The line printer to use.
  /// \param IndentLevel Indentation amount associated with this scope.
  explicit PrintScope(LinePrinter &P, uint32_t IndentLevel)
      : P(P), IndentLevel(IndentLevel) {}
  /// Construct a print scope copying \p Other and setting \p LabelWidth.
  ///
  /// \param Other The print scope to copy the printer and indent from.
  /// \param LabelWidth Width used when aligning labels in this scope.
  explicit PrintScope(const PrintScope &Other, uint32_t LabelWidth)
      : P(Other.P), IndentLevel(Other.IndentLevel), LabelWidth(LabelWidth) {}

  /// The line printer used for output in this scope.
  LinePrinter &P;
  /// Indentation amount associated with this scope.
  uint32_t IndentLevel;
  /// Width used when aligning labels; zero if unused.
  uint32_t LabelWidth = 0;
};

/// Return a copy of \p Scope with its label width set to \p W.
///
/// \param Scope The print scope to copy.
/// \param W The label width to apply.
///
/// \returns A copy of \p Scope with label width set to \p W.
inline PrintScope withLabelWidth(const PrintScope &Scope, uint32_t W) {
  return PrintScope{Scope, W};
}

/// RAII helper that indents a line printer for the lifetime of the object.
struct AutoIndent {
  /// Indent \p L by \p Amount spaces until this object is destroyed.
  ///
  /// \param L The line printer to indent.
  /// \param Amount Spaces to indent; if zero, uses the printer's default.
  explicit AutoIndent(LinePrinter &L, uint32_t Amount = 0)
      : L(&L), Amount(Amount) {
    L.Indent(Amount);
  }
  /// Bind to the printer and indent amount from \p Scope.
  ///
  /// \param Scope Print scope supplying the printer and indent level.
  explicit AutoIndent(const PrintScope &Scope) {
    L = &Scope.P;
    Amount = Scope.IndentLevel;
  }
  /// Restore the previous indentation level on the bound printer.
  ~AutoIndent() {
    if (L)
      L->Unindent(Amount);
  }

  /// The line printer being indented, or null if unbound.
  LinePrinter *L = nullptr;
  /// Number of spaces added (and later removed) by this helper.
  uint32_t Amount = 0;
};

/// Stream \p Item to the output stream owned by \p Printer.
///
/// \param Printer The line printer whose stream receives \p Item.
/// \param Item The value to insert into the stream.
///
/// \returns The underlying raw_ostream after insertion.
template <class T>
inline raw_ostream &operator<<(LinePrinter &Printer, const T &Item) {
  return Printer.getStream() << Item;
}

/// Categories of text that can be colored when dumping PDB contents.
enum class PDB_ColorItem {
  None,          ///< No special color; reset to the default.
  Address,       ///< Absolute or relative addresses.
  Type,          ///< Type names and type-related text.
  Comment,       ///< Comments and secondary annotations.
  Padding,       ///< Structure padding regions.
  Keyword,       ///< Language or PDB keywords.
  Offset,        ///< Offsets within a section or record.
  Identifier,    ///< Symbol and identifier names.
  Path,          ///< File system paths.
  SectionHeader, ///< Section or subsection headers.
  LiteralValue,  ///< Numeric or string literal values.
  Register,      ///< Machine register names.
};

/// Applies a PDB dump color for the lifetime of the object.
class WithColor {
public:
  /// Apply color \p C to \p P's stream if coloring is enabled.
  ///
  /// \param P The line printer whose stream receives the color.
  /// \param C The color category to apply.
  LLVM_ABI WithColor(LinePrinter &P, PDB_ColorItem C);
  /// Reset the stream color if coloring was applied.
  LLVM_ABI ~WithColor();

  /// Return the colored output stream.
  ///
  /// \returns The colored output stream.
  raw_ostream &get() { return OS; }

private:
  void applyColor(PDB_ColorItem C);
  raw_ostream &OS;
  bool UseColor;
};
} // namespace pdb
} // namespace llvm

#endif
