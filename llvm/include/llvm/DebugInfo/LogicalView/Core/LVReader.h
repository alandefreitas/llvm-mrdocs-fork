//===-- LVReader.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVReader class, which is used to describe a debug
// information reader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVREADER_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVREADER_H

#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"
#include "llvm/DebugInfo/LogicalView/Core/LVRange.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/ToolOutputFile.h"
#include <map>

namespace llvm {
namespace logicalview {

/// Sentinel section index used when no section is associated.
constexpr LVSectionIndex UndefinedSectionIndex = 0;

class LVScopeCompileUnit;
class LVObject;

/// Manages split-output files and their location for '--output=split'.
class LVSplitContext final {
  std::unique_ptr<ToolOutputFile> OutputFile;
  std::string Location;

public:
  /// Construct an empty split-output context.
  LVSplitContext() = default;
  /// Copy construction is not allowed.
  /// \param Other Unused source split-output context.
  LVSplitContext(const LVSplitContext &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source split-output context.
  LVSplitContext &operator=(const LVSplitContext &Other) = delete;
  /// Destroy the split-output context.
  ~LVSplitContext() = default;

  /// Create the directory used for split output files.
  /// \param Where Path of the folder that will hold split outputs.
  /// \returns Success or an error describing why folder creation failed.
  LLVM_ABI Error createSplitFolder(StringRef Where);
  /// Open a split output file with the given name and extension.
  /// \param Name Base filename for the split output.
  /// \param Extension File extension for the split output.
  /// \param OS Stream used to report open failures.
  /// \returns A zero error code on success, or the open failure code.
  LLVM_ABI std::error_code open(std::string Name, std::string Extension,
                                raw_ostream &OS);
  /// Close the current split output file if one is open.
  void close() {
    if (OutputFile) {
      OutputFile->os().close();
      OutputFile = nullptr;
    }
  }

  /// Return the directory path used for split output files.
  /// \returns Directory path for split output files.
  std::string getLocation() const { return Location; }
  /// Return the output stream for the current split file.
  /// \returns Output stream for the current split file.
  raw_fd_ostream &os() { return OutputFile->os(); }
};

/// The logical reader owns all logical elements created during parsing.
///
/// For their creation it uses a specific bump allocator for each type of
/// logical element.
class LLVM_ABI LVReader {
  LVBinaryType BinaryType;

  // Context used by '--output=split' command line option.
  LVSplitContext SplitContext;

  // Compile Units DIE Offset => Scope.
  using LVCompileUnits = std::map<LVOffset, LVScopeCompileUnit *>;
  LVCompileUnits CompileUnits;

  // Added elements to be used during elements comparison.
  LVLines Lines;
  LVScopes Scopes;
  LVSymbols Symbols;
  LVTypes Types;

  // Create split folder.
  Error createSplitFolder();
  bool OutputSplit = false;

// Define a specific bump allocator for the given KIND.
#define LV_OBJECT_ALLOCATOR(KIND)                                              \
  llvm::SpecificBumpPtrAllocator<LV##KIND> Allocated##KIND;

  // Lines allocator.
  LV_OBJECT_ALLOCATOR(Line)
  LV_OBJECT_ALLOCATOR(LineDebug)
  LV_OBJECT_ALLOCATOR(LineAssembler)

  // Locations allocator.
  LV_OBJECT_ALLOCATOR(Location)
  LV_OBJECT_ALLOCATOR(LocationSymbol)

  // Operations allocator.
  LV_OBJECT_ALLOCATOR(Operation)

  // Scopes allocator.
  LV_OBJECT_ALLOCATOR(Scope)
  LV_OBJECT_ALLOCATOR(ScopeAggregate)
  LV_OBJECT_ALLOCATOR(ScopeAlias)
  LV_OBJECT_ALLOCATOR(ScopeArray)
  LV_OBJECT_ALLOCATOR(ScopeCompileUnit)
  LV_OBJECT_ALLOCATOR(ScopeEnumeration)
  LV_OBJECT_ALLOCATOR(ScopeFormalPack)
  LV_OBJECT_ALLOCATOR(ScopeFunction)
  LV_OBJECT_ALLOCATOR(ScopeFunctionInlined)
  LV_OBJECT_ALLOCATOR(ScopeFunctionType)
  LV_OBJECT_ALLOCATOR(ScopeModule)
  LV_OBJECT_ALLOCATOR(ScopeNamespace)
  LV_OBJECT_ALLOCATOR(ScopeRoot)
  LV_OBJECT_ALLOCATOR(ScopeTemplatePack)

  // Symbols allocator.
  LV_OBJECT_ALLOCATOR(Symbol)

  // Types allocator.
  LV_OBJECT_ALLOCATOR(Type)
  LV_OBJECT_ALLOCATOR(TypeDefinition)
  LV_OBJECT_ALLOCATOR(TypeEnumerator)
  LV_OBJECT_ALLOCATOR(TypeImport)
  LV_OBJECT_ALLOCATOR(TypeParam)
  LV_OBJECT_ALLOCATOR(TypeSubrange)

#undef LV_OBJECT_ALLOCATOR

  // Scopes with ranges for current compile unit. It is used to find a line
  // giving its exact or closest address. To support comdat functions, all
  // addresses for the same section are recorded in the same map.
  using LVSectionRanges = std::map<LVSectionIndex, std::unique_ptr<LVRange>>;
  LVSectionRanges SectionRanges;

protected:
  /// Element currently being built while processing a DIE or MDNode.
  LVElement *CurrentElement = nullptr;
  /// Scope currently being built while processing a DIE or MDNode.
  LVScope *CurrentScope = nullptr;
  /// Symbol currently being built while processing a DIE or MDNode.
  LVSymbol *CurrentSymbol = nullptr;
  /// Type currently being built while processing a DIE or MDNode.
  LVType *CurrentType = nullptr;
  /// Line currently being built while processing a DIE or MDNode.
  LVLine *CurrentLine = nullptr;
  /// DIE or record offset associated with the element being processed.
  LVOffset CurrentOffset = 0;

  /// Address ranges collected for the current DIE, MDNode, or AST node.
  std::vector<LVAddressRange> CurrentRanges;

  /// Root scope of the logical view for this reader.
  LVScopeRoot *Root = nullptr;
  /// Path of the input object or executable being read.
  std::string InputFilename;
  /// Human-readable name of the input binary file format.
  std::string FileFormatName;
  /// Scoped printer used for formatted reader output.
  ScopedPrinter &W;
  /// Raw output stream backing the scoped printer.
  raw_ostream &OS;
  /// Compile unit currently being processed, if any.
  LVScopeCompileUnit *CompileUnit = nullptr;

  /// Index of the `.text` section for ELF inputs; undefined for others.
  LVSectionIndex DotTextSectionIndex = UndefinedSectionIndex;

  /// Record that \p Scope contributes ranges in \p SectionIndex.
  /// \param SectionIndex Section that owns the scope ranges.
  /// \param Scope Scope whose ranges are being registered.
  void addSectionRange(LVSectionIndex SectionIndex, LVScope *Scope);
  /// Record that \p Scope covers [\p LowerAddress, \p UpperAddress) in a section.
  /// \param SectionIndex Section that owns the address range.
  /// \param Scope Scope associated with the address range.
  /// \param LowerAddress Inclusive lower bound of the range.
  /// \param UpperAddress Exclusive upper bound of the range.
  void addSectionRange(LVSectionIndex SectionIndex, LVScope *Scope,
                       LVAddress LowerAddress, LVAddress UpperAddress);
  /// Return the range map for \p SectionIndex, creating it if needed.
  /// \param SectionIndex Section whose range map is requested.
  /// \returns Pointer to the section's LVRange map.
  LVRange *getSectionRanges(LVSectionIndex SectionIndex);

  /// Record a compile unit scope under its DIE \p Offset.
  /// \param Offset DIE offset used as the map key.
  /// \param CompileUnit Compile unit scope to register.
  void addCompileUnitOffset(LVOffset Offset, LVScopeCompileUnit *CompileUnit) {
    CompileUnits.emplace(Offset, CompileUnit);
  }

  /// Create a logical element for the given DWARF \p Tag.
  /// \param Tag DWARF tag that selects the element kind.
  /// \returns Newly allocated element, or nullptr if unsupported.
  LVElement *createElement(dwarf::Tag Tag);

  /// Create the root scope and initialize its name and format attributes.
  /// \returns Success or an error describing why root creation failed.
  virtual Error createScopes() {
    Root = createScopeRoot();
    Root->setName(getFilename());
    if (options().getAttributeFormat())
      Root->setFileFormatName(FileFormatName);
    return Error::success();
  }

  /// Build a path under the input file's directory using \p From's filename.
  ///
  /// This is useful when a type server (PDB file associated with an object
  /// file or a precompiled header file) or a DWARF split object have been
  /// moved from their original location. That is the case when running
  /// regression tests, where object files are created in one location and
  /// executed in a different location.
  /// \param From Original path whose filename is appended to the input parent.
  /// \returns Path composed as parent_path(InputFilename)/filename(From).
  std::string createAlternativePath(StringRef From) {
    // During the reader initialization, any backslashes in 'InputFilename'
    // are converted to forward slashes.
    SmallString<128> Path;
    sys::path::append(Path, sys::path::Style::posix,
                      sys::path::parent_path(InputFilename),
                      sys::path::filename(sys::path::convert_to_slash(
                          From, sys::path::Style::windows)));
    return std::string(Path);
  }

  /// Print the logical scopes produced by this reader.
  /// \returns Success or an error describing why printing failed.
  virtual Error printScopes();
  /// Print elements that matched selection patterns.
  /// \param UseMatchedElements Whether to print the matched-elements view.
  /// \returns Success or an error describing why printing failed.
  virtual Error printMatchedElements(bool UseMatchedElements);
  /// Sort scopes after they have been created; default is a no-op.
  virtual void sortScopes() {}

  /// Print elements collected for comparison under \p Root.
  /// \param Root Scope whose collected children are printed.
  void printCollectedElements(LVScope *Root);
  /// Check that the scopes tree rooted at \p Root is internally consistent.
  /// \param Root Scope tree to validate.
  /// \returns True if the tree passes integrity checks.
  bool checkIntegrityScopesTree(LVScope *Root);

public:
  /// Default construction is deleted; input and output context are required.
  LVReader() = delete;
  /// Construct a reader for \p InputFilename with the given format and printer.
  /// \param InputFilename Path of the binary being read.
  /// \param FileFormatName Display name of the binary file format.
  /// \param W Scoped printer used for reader output.
  /// \param BinaryType Kind of binary containing the debug information.
  LVReader(StringRef InputFilename, StringRef FileFormatName, ScopedPrinter &W,
           LVBinaryType BinaryType = LVBinaryType::NONE)
      : BinaryType(BinaryType), OutputSplit(options().getOutputSplit()),
        InputFilename(InputFilename), FileFormatName(FileFormatName), W(W),
        OS(W.getOStream()) {}
  /// Copy construction is not allowed.
  /// \param Other Unused source reader.
  LVReader(const LVReader &Other) = delete;
  /// Copy assignment is not allowed.
  /// \param Other Unused source reader.
  LVReader &operator=(const LVReader &Other) = delete;
  /// Destroy the logical reader and its allocated elements.
  virtual ~LVReader() = default;

// Creates a logical object of the given KIND. The signature for the created
// functions looks like:
//   ...
//   LVScope *createScope()
//   LVScopeRoot *creatScopeRoot()
//   LVType *createType();
//   ...
#define LV_CREATE_OBJECT(KIND)                                                 \
  LV##KIND *create##KIND() {                                                   \
    return new (Allocated##KIND.Allocate()) LV##KIND();                        \
  }

  /// Allocate and return a new LVLine.
  /// \returns Newly allocated line.
  LV_CREATE_OBJECT(Line)
  /// Allocate and return a new LVLineDebug.
  /// \returns Newly allocated debug line.
  LV_CREATE_OBJECT(LineDebug)
  /// Allocate and return a new LVLineAssembler.
  /// \returns Newly allocated assembler line.
  LV_CREATE_OBJECT(LineAssembler)

  /// Allocate and return a new LVLocation.
  /// \returns Newly allocated location.
  LV_CREATE_OBJECT(Location)
  /// Allocate and return a new LVLocationSymbol.
  /// \returns Newly allocated location symbol.
  LV_CREATE_OBJECT(LocationSymbol)

  /// Allocate and return a new LVScope.
  /// \returns Newly allocated scope.
  LV_CREATE_OBJECT(Scope)
  /// Allocate and return a new LVScopeAggregate.
  /// \returns Newly allocated aggregate scope.
  LV_CREATE_OBJECT(ScopeAggregate)
  /// Allocate and return a new LVScopeAlias.
  /// \returns Newly allocated alias scope.
  LV_CREATE_OBJECT(ScopeAlias)
  /// Allocate and return a new LVScopeArray.
  /// \returns Newly allocated array scope.
  LV_CREATE_OBJECT(ScopeArray)
  /// Allocate and return a new LVScopeCompileUnit.
  /// \returns Newly allocated compile-unit scope.
  LV_CREATE_OBJECT(ScopeCompileUnit)
  /// Allocate and return a new LVScopeEnumeration.
  /// \returns Newly allocated enumeration scope.
  LV_CREATE_OBJECT(ScopeEnumeration)
  /// Allocate and return a new LVScopeFormalPack.
  /// \returns Newly allocated formal-pack scope.
  LV_CREATE_OBJECT(ScopeFormalPack)
  /// Allocate and return a new LVScopeFunction.
  /// \returns Newly allocated function scope.
  LV_CREATE_OBJECT(ScopeFunction)
  /// Allocate and return a new LVScopeFunctionInlined.
  /// \returns Newly allocated inlined-function scope.
  LV_CREATE_OBJECT(ScopeFunctionInlined)
  /// Allocate and return a new LVScopeFunctionType.
  /// \returns Newly allocated function-type scope.
  LV_CREATE_OBJECT(ScopeFunctionType)
  /// Allocate and return a new LVScopeModule.
  /// \returns Newly allocated module scope.
  LV_CREATE_OBJECT(ScopeModule)
  /// Allocate and return a new LVScopeNamespace.
  /// \returns Newly allocated namespace scope.
  LV_CREATE_OBJECT(ScopeNamespace)
  /// Allocate and return a new LVScopeRoot.
  /// \returns Newly allocated root scope.
  LV_CREATE_OBJECT(ScopeRoot)
  /// Allocate and return a new LVScopeTemplatePack.
  /// \returns Newly allocated template-pack scope.
  LV_CREATE_OBJECT(ScopeTemplatePack)

  /// Allocate and return a new LVSymbol.
  /// \returns Newly allocated symbol.
  LV_CREATE_OBJECT(Symbol)

  /// Allocate and return a new LVType.
  /// \returns Newly allocated type.
  LV_CREATE_OBJECT(Type)
  /// Allocate and return a new LVTypeDefinition.
  /// \returns Newly allocated type definition.
  LV_CREATE_OBJECT(TypeDefinition)
  /// Allocate and return a new LVTypeEnumerator.
  /// \returns Newly allocated type enumerator.
  LV_CREATE_OBJECT(TypeEnumerator)
  /// Allocate and return a new LVTypeImport.
  /// \returns Newly allocated type import.
  LV_CREATE_OBJECT(TypeImport)
  /// Allocate and return a new LVTypeParam.
  /// \returns Newly allocated type parameter.
  LV_CREATE_OBJECT(TypeParam)
  /// Allocate and return a new LVTypeSubrange.
  /// \returns Newly allocated type subrange.
  LV_CREATE_OBJECT(TypeSubrange)

#undef LV_CREATE_OBJECT

  /// Allocate and return a new LVOperation with the given opcode and operands.
  /// \param OpCode Operation opcode stored in the new LVOperation.
  /// \param Operands Operand values associated with the opcode.
  /// \returns Newly allocated operation.
  LVOperation *createOperation(LVSmall OpCode, ArrayRef<LVUnsigned> Operands) {
    return new (AllocatedOperation.Allocate()) LVOperation(OpCode, Operands);
  }

  /// Return the filename at \p Index associated with \p Object.
  /// \param Object Object whose file table is queried.
  /// \param Index Index into the object's file table.
  /// \returns Filename string for the requested index.
  StringRef getFilename(LVObject *Object, size_t Index) const;
  /// Return the path of the input binary being read.
  /// \returns Path of the input binary.
  StringRef getFilename() const { return InputFilename; }
  /// Set the path of the input binary being read.
  /// \param Name New input filename to store.
  void setFilename(std::string Name) { InputFilename = std::move(Name); }
  /// Return the display name of the input binary file format.
  /// \returns Display name of the input binary file format.
  StringRef getFileFormatName() const { return FileFormatName; }

  /// Return the raw output stream used by this reader.
  /// \returns Raw output stream used by this reader.
  raw_ostream &outputStream() { return OS; }

  /// Return whether the binary type is unset.
  /// \returns True if the binary type is unset.
  bool isBinaryTypeNone() const { return BinaryType == LVBinaryType::NONE; }
  /// Return whether the input binary is ELF.
  /// \returns True if the input binary is ELF.
  bool isBinaryTypeELF() const { return BinaryType == LVBinaryType::ELF; }
  /// Return whether the input binary is COFF.
  /// \returns True if the input binary is COFF.
  bool isBinaryTypeCOFF() const { return BinaryType == LVBinaryType::COFF; }

  /// Return the compile unit currently being processed.
  /// \returns Compile unit currently being processed, or nullptr if none.
  LVScopeCompileUnit *getCompileUnit() const { return CompileUnit; }
  /// Set the compile unit currently being processed from \p Scope.
  /// \param Scope Scope that must represent a compile unit.
  void setCompileUnit(LVScope *Scope) {
    assert(Scope && Scope->isCompileUnit() && "Scope is not a compile unit");
    CompileUnit = static_cast<LVScopeCompileUnit *>(Scope);
  }
  /// Set the CodeView CPU type on the current compile unit.
  /// \param Type CPU type recorded for the compile unit.
  void setCompileUnitCPUType(codeview::CPUType Type) {
    CompileUnit->setCPUType(Type);
  }
  /// Return the CodeView CPU type of the current compile unit.
  /// \returns CodeView CPU type of the current compile unit.
  codeview::CPUType getCompileUnitCPUType() {
    return CompileUnit->getCPUType();
  }

  /// Return the root scope of the logical view.
  /// \returns Root scope of the logical view.
  LVScopeRoot *getScopesRoot() const { return Root; }

  /// Print the logical view according to the current options.
  /// \returns Success or an error describing why printing failed.
  Error doPrint();
  /// Load debug information into the logical view.
  /// \returns Success or an error describing why loading failed.
  Error doLoad();

  /// Return the register name for \p Opcode and \p Operands.
  /// \param Opcode Location or expression opcode selecting the register.
  /// \param Operands Operand values used to resolve the register name.
  /// \returns Register name string for the opcode and operands.
  virtual std::string getRegisterName(LVSmall Opcode,
                                      ArrayRef<uint64_t> Operands) {
    llvm_unreachable("Invalid instance reader.");
    return {};
  }

  /// Return the index of the `.text` section for this reader.
  /// \returns Index of the `.text` section, or the undefined sentinel.
  LVSectionIndex getDotTextSectionIndex() const { return DotTextSectionIndex; }
  /// Return the section index associated with \p Scope.
  /// \param Scope Scope whose section index is requested.
  /// \returns Section index for the scope, defaulting to `.text`.
  virtual LVSectionIndex getSectionIndex(LVScope *Scope) {
    return getDotTextSectionIndex();
  }

  /// Return whether \p Element is a system entry, optionally named \p Name.
  /// \param Element Element being classified.
  /// \param Name Optional name used by format-specific overrides.
  /// \returns True if the element is considered a system entry.
  virtual bool isSystemEntry(LVElement *Element, StringRef Name = {}) const {
    return false;
  };

  /// Return the split-output context used by '--output=split'.
  /// \returns Split-output context used by '--output=split'.
  LVSplitContext &getSplitContext() { return SplitContext; }

  /// Register \p Line if line comparison is enabled.
  /// \param Line Line that was added to the logical view.
  void notifyAddedElement(LVLine *Line) {
    if (!options().getCompareContext() && options().getCompareLines())
      Lines.push_back(Line);
  }
  /// Register \p Scope if scope comparison is enabled.
  /// \param Scope Scope that was added to the logical view.
  void notifyAddedElement(LVScope *Scope) {
    if (!options().getCompareContext() && options().getCompareScopes())
      Scopes.push_back(Scope);
  }
  /// Register \p Symbol if symbol comparison is enabled.
  /// \param Symbol Symbol that was added to the logical view.
  void notifyAddedElement(LVSymbol *Symbol) {
    if (!options().getCompareContext() && options().getCompareSymbols())
      Symbols.push_back(Symbol);
  }
  /// Register \p Type if type comparison is enabled.
  /// \param Type Type that was added to the logical view.
  void notifyAddedElement(LVType *Type) {
    if (!options().getCompareContext() && options().getCompareTypes())
      Types.push_back(Type);
  }

  /// Return the lines collected for comparison.
  /// \returns Lines collected for comparison.
  const LVLines &getLines() const { return Lines; }
  /// Return the scopes collected for comparison.
  /// \returns Scopes collected for comparison.
  const LVScopes &getScopes() const { return Scopes; }
  /// Return the symbols collected for comparison.
  /// \returns Symbols collected for comparison.
  const LVSymbols &getSymbols() const { return Symbols; }
  /// Return the types collected for comparison.
  /// \returns Types collected for comparison.
  const LVTypes &getTypes() const { return Types; }

  /// Return whether \p Line should be printed under current patterns.
  /// \param Line Line being considered for printing.
  /// \returns True if the line matches print selection.
  bool doPrintLine(const LVLine *Line) const {
    return patterns().printElement(Line);
  }
  /// Return whether \p Location should be printed under current patterns.
  /// \param Location Location being considered for printing.
  /// \returns True if the location matches print selection.
  bool doPrintLocation(const LVLocation *Location) const {
    return patterns().printObject(Location);
  }
  /// Return whether \p Scope should be printed under current patterns.
  /// \param Scope Scope being considered for printing.
  /// \returns True if the scope matches print selection.
  bool doPrintScope(const LVScope *Scope) const {
    return patterns().printElement(Scope);
  }
  /// Return whether \p Symbol should be printed under current patterns.
  /// \param Symbol Symbol being considered for printing.
  /// \returns True if the symbol matches print selection.
  bool doPrintSymbol(const LVSymbol *Symbol) const {
    return patterns().printElement(Symbol);
  }
  /// Return whether \p Type should be printed under current patterns.
  /// \param Type Type being considered for printing.
  /// \returns True if the type matches print selection.
  bool doPrintType(const LVType *Type) const {
    return patterns().printElement(Type);
  }

  /// Return the current global LVReader instance.
  /// \returns Current global LVReader instance.
  static LVReader &getInstance();
  /// Set the current global LVReader instance to \p Reader.
  /// \param Reader Reader that becomes the global instance.
  static void setInstance(LVReader *Reader);

  /// Print a summary of this reader to \p OS.
  /// \param OS Stream that receives the printed reader summary.
  void print(raw_ostream &OS) const;
  /// Print format-specific records to \p OS; default is a no-op.
  /// \param OS Stream that receives the printed records.
  virtual void printRecords(raw_ostream &OS) const {}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this reader to the debug stream.
  void dump() const { print(dbgs()); }
#endif
};

/// Return the current global LVReader instance.
/// \returns Current global LVReader instance.
inline LVReader &getReader() { return LVReader::getInstance(); }
/// Return the split-output context of the current global reader.
/// \returns Split-output context of the current global reader.
inline LVSplitContext &getReaderSplitContext() {
  return getReader().getSplitContext();
}
/// Return the compile unit of the current global reader.
/// \returns Compile unit of the current global reader, or nullptr if none.
inline LVScopeCompileUnit *getReaderCompileUnit() {
  return getReader().getCompileUnit();
}

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVREADER_H
