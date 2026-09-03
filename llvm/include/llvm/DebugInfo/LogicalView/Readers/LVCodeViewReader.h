//===-- LVCodeViewReader.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVCodeViewReader class, which is used to describe a
// debug information (COFF) reader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_READERS_CODEVIEWREADER_H
#define LLVM_DEBUGINFO_LOGICALVIEW_READERS_CODEVIEWREADER_H

#include "llvm/DebugInfo/CodeView/AppendingTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/LogicalView/Readers/LVBinaryReader.h"
#include "llvm/DebugInfo/LogicalView/Readers/LVCodeViewVisitor.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryItemStream.h"
#include "llvm/Support/BinaryStreamArray.h"

namespace llvm {

/// Traits for treating CodeView type records as binary stream items.
template <> struct BinaryItemTraits<codeview::CVType> {
  /// Return the length in bytes of \p Item.
  /// \param Item CodeView type record whose length is requested.
  /// \returns Length in bytes of the type record.
  static size_t length(const codeview::CVType &Item) { return Item.length(); }
  /// Return the raw bytes that make up \p Item.
  /// \param Item CodeView type record whose bytes are requested.
  /// \returns Byte array view of the type record data.
  static ArrayRef<uint8_t> bytes(const codeview::CVType &Item) {
    return Item.data();
  }
};

namespace codeview {
class LazyRandomTypeCollection;
}
namespace object {
struct coff_section;
}
/// PDB types referenced by the CodeView logical-view reader.
namespace pdb {
class SymbolGroup;
}
namespace logicalview {

class LVElement;
class LVLine;
class LVScope;
class LVScopeCompileUnit;
class LVSymbol;
class LVType;
class LVTypeVisitor;
class LVSymbolVisitor;
class LVSymbolVisitorDelegate;

/// Small vector of name string refs collected while reading CodeView.
using LVNames = SmallVector<StringRef, 16>;

/// Reader that builds a logical view from CodeView (COFF/PDB) debug info.
///
/// The DWARF reader uses the DWARF constants to create the logical elements.
/// The DW_TAG_* and DW_AT_* are used to select the logical object and to
/// set specific attributes, such as name, type, etc.
/// As the CodeView constants are different to the DWARF constants, the
/// CodeView reader will map them to the DWARF ones.
class LLVM_ABI LVCodeViewReader final : public LVBinaryReader {
  friend class LVTypeVisitor;
  friend class LVSymbolVisitor;
  friend class LVSymbolVisitorDelegate;

  using LVModules = std::vector<LVScope *>;
  LVModules Modules;

  // Encapsulates access to the input file and any dependent type server,
  // including any precompiled header object.
  llvm::pdb::InputFile Input;
  std::shared_ptr<llvm::pdb::InputFile> TypeServer;
  std::shared_ptr<LazyRandomTypeCollection> PrecompHeader;

  // Persistance data when loading a type server.
  ErrorOr<std::unique_ptr<MemoryBuffer>> BuffOrErr = nullptr;
  std::unique_ptr<MemoryBuffer> MemBuffer;
  std::unique_ptr<llvm::pdb::IPDBSession> Session;
  std::unique_ptr<llvm::pdb::NativeSession> PdbSession;

  // Persistance data when loading a precompiled header.
  BumpPtrAllocator BuilderAllocator;
  std::unique_ptr<AppendingTypeTableBuilder> Builder;
  std::unique_ptr<BinaryItemStream<CVType>> ItemStream;
  std::unique_ptr<BinaryStreamReader> ReaderPrecomp;
  std::vector<CVType> TypeArray;
  CVTypeArray TypeStream;
  CVTypeArray CVTypesPrecomp;

  // Persistance data when loading an executable file.
  std::unique_ptr<MemoryBuffer> BinaryBuffer;
  std::unique_ptr<llvm::object::Binary> BinaryExecutable;

  Error loadTargetInfo(const object::ObjectFile &Obj);
  Error loadTargetInfo(const llvm::pdb::PDBFile &Pdb);

  void mapRangeAddress(const object::ObjectFile &Obj,
                       const object::SectionRef &Section,
                       bool IsComdat) override;

  llvm::object::COFFObjectFile &getObj() { return Input.obj(); }
  llvm::pdb::PDBFile &getPdb() { return Input.pdb(); }
  bool isObj() const { return Input.isObj(); }
  bool isPdb() const { return Input.isPdb(); }
  StringRef getFileName() { return Input.getFilePath(); }

  // Pathname to executable image.
  std::string ExePath;

  LVOffset CurrentOffset = 0;
  int32_t CurrentModule = -1;

  using RelocMapTy = DenseMap<const llvm::object::coff_section *,
                              std::vector<llvm::object::RelocationRef>>;
  RelocMapTy RelocMap;

  // Object files have only one type stream that contains both types and ids.
  // Precompiled header objects don't contain an IPI stream. Use the TPI.
  LazyRandomTypeCollection &types() {
    return TypeServer ? TypeServer->types()
                      : (PrecompHeader ? *PrecompHeader : Input.types());
  }
  LazyRandomTypeCollection &ids() {
    return TypeServer ? TypeServer->ids()
                      : (PrecompHeader ? *PrecompHeader : Input.ids());
  }

  LVLogicalVisitor LogicalVisitor;

  Expected<StringRef>
  getFileNameForFileOffset(uint32_t FileOffset,
                           const llvm::pdb::SymbolGroup *SG = nullptr);
  void printRelocatedField(StringRef Label,
                           const llvm::object::coff_section *CoffSection,
                           uint32_t RelocOffset, uint32_t Offset,
                           StringRef *RelocSym);

  Error printFileNameForOffset(StringRef Label, uint32_t FileOffset,
                               const llvm::pdb::SymbolGroup *SG = nullptr);

  Error loadPrecompiledObject(PrecompRecord &Precomp, CVTypeArray &CVTypesObj);
  Error loadTypeServer(TypeServer2Record &TS);
  Error traverseTypes(llvm::pdb::PDBFile &Pdb, LazyRandomTypeCollection &Types,
                      LazyRandomTypeCollection &Ids);

  Error collectInlineeInfo(DebugInlineeLinesSubsectionRef &Lines,
                           const llvm::pdb::SymbolGroup *SG = nullptr);

  void cacheRelocations();
  Error resolveSymbol(const llvm::object::coff_section *CoffSection,
                      uint64_t Offset, llvm::object::SymbolRef &Sym);
  Error resolveSymbolName(const llvm::object::coff_section *CoffSection,
                          uint64_t Offset, StringRef &Name);
  Error traverseTypeSection(StringRef SectionName,
                            const llvm::object::SectionRef &Section);
  Error traverseSymbolSection(StringRef SectionName,
                              const llvm::object::SectionRef &Section);
  Error traverseInlineeLines(StringRef Subsection);

  DebugChecksumsSubsectionRef CVFileChecksumTable;
  DebugStringTableSubsectionRef CVStringTable;

  Error traverseSymbolsSubsection(StringRef Subsection,
                                  const llvm::object::SectionRef &Section,
                                  StringRef SectionContents);

  /// Given a .debug$S section, find the string table and file checksum table.
  /// This function taken from (COFFDumper.cpp).
  /// TODO: It can be moved to the COFF library.
  Error initializeFileAndStringTables(BinaryStreamReader &Reader);

  Error createLines(const FixedStreamArray<LineNumberEntry> &LineNumbers,
                    LVAddress Addendum, uint32_t Segment, uint32_t Begin,
                    uint32_t Size, uint32_t NameIndex,
                    const llvm::pdb::SymbolGroup *SG = nullptr);
  Error createScopes(llvm::object::COFFObjectFile &Obj);
  Error createScopes(llvm::pdb::PDBFile &Pdb);
  Error processModule();

protected:
  /// Build the logical scopes tree from the CodeView input.
  /// \returns Success or an error if scopes could not be built.
  Error createScopes() override;
  /// Sort the root scope after scopes have been created.
  void sortScopes() override;

public:
  /// Deleted; a CodeView reader requires an input object or PDB.
  LVCodeViewReader() = delete;
  /// Construct a reader for a COFF object file with CodeView debug info.
  ///
  /// \param Filename Path of the input file being read.
  /// \param FileFormatName Human-readable object format name.
  /// \param Obj COFF object that supplies CodeView sections.
  /// \param W Printer used for diagnostic and dump output.
  /// \param ExePath Path to the executable image, when available.
  LVCodeViewReader(StringRef Filename, StringRef FileFormatName,
                   llvm::object::COFFObjectFile &Obj, ScopedPrinter &W,
                   StringRef ExePath)
      : LVBinaryReader(Filename, FileFormatName, W, LVBinaryType::COFF),
        Input(&Obj), ExePath(ExePath), LogicalVisitor(this, W, Input) {}
  /// Construct a reader for a PDB file with CodeView debug info.
  ///
  /// \param Filename Path of the input file being read.
  /// \param FileFormatName Human-readable object format name.
  /// \param Pdb PDB file that supplies CodeView type and symbol streams.
  /// \param W Printer used for diagnostic and dump output.
  /// \param ExePath Path to the executable image, when available.
  LVCodeViewReader(StringRef Filename, StringRef FileFormatName,
                   llvm::pdb::PDBFile &Pdb, ScopedPrinter &W, StringRef ExePath)
      : LVBinaryReader(Filename, FileFormatName, W, LVBinaryType::COFF),
        Input(&Pdb), ExePath(ExePath), LogicalVisitor(this, W, Input) {}
  /// Deleted; CodeView readers are not copyable.
  /// \param Other Unused source reader.
  LVCodeViewReader(const LVCodeViewReader &Other) = delete;
  /// Deleted; CodeView readers are not copy-assignable.
  /// \param Other Unused source reader.
  LVCodeViewReader &operator=(const LVCodeViewReader &Other) = delete;
  /// Destroy the CodeView reader.
  ~LVCodeViewReader() override = default;

  /// Resolve a relocated linkage name for a COFF section field.
  ///
  /// \param CoffSection COFF section that owns the relocation.
  /// \param RelocOffset Offset of the relocation within the section.
  /// \param Offset Field offset used when resolving the symbol name.
  /// \param RelocSym Optional output for the resolved symbol name.
  void getLinkageName(const llvm::object::coff_section *CoffSection,
                      uint32_t RelocOffset, uint32_t Offset,
                      StringRef *RelocSym);

  /// Record a compile-unit or module scope for later module-index lookup.
  /// \param Scope Module scope to append to the module table.
  void addModule(LVScope *Scope) { Modules.push_back(Scope); }
  /// Return the scope for module index \p Modi, or nullptr if out of range.
  /// \param Modi Zero-based module index.
  /// \returns Module scope for \p Modi, or nullptr if out of range.
  LVScope *getScopeForModule(uint32_t Modi) {
    return Modi >= Modules.size() ? nullptr : Modules[Modi];
  }

  /// Return the string name of CodeView symbol kind \p Kind.
  /// \param Kind CodeView symbol record kind to name.
  /// \returns String name of the given CodeView symbol kind.
  static StringRef getSymbolKindName(SymbolKind Kind);
  /// Format CodeView register \p Register for CPU architecture \p CPU.
  /// \param Register CodeView register identifier.
  /// \param CPU Target CPU used to select the register name table.
  /// \returns Formatted register name string for \p Register on \p CPU.
  static std::string formatRegisterId(RegisterId Register, CPUType CPU);

  /// Return the register name for a CodeView location opcode and operands.
  /// \param Opcode Location operation code (unused for CodeView).
  /// \param Operands Operand list; the register id is taken from Operands[0].
  /// \returns Formatted register name for the given operands.
  std::string getRegisterName(LVSmall Opcode,
                              ArrayRef<uint64_t> Operands) override;

  /// Return true if \p Element is a compiler-generated or MSVC system entry.
  ///
  /// Marks matching elements as system so they print only with
  /// `internal=system`.
  /// \param Element Logical element to classify; may be marked as system.
  /// \param Name Name to test; falls back to \p Element's name when empty.
  /// \returns True when the entry is a compiler-generated or MSVC system
  /// entry.
  bool isSystemEntry(LVElement *Element, StringRef Name) const override;

  /// Print a debug summary of this reader to \p OS.
  /// \param OS Output stream to write to.
  void print(raw_ostream &OS) const;
  /// Print collected CodeView type and symbol records to \p OS.
  /// \param OS Output stream to write to.
  void printRecords(raw_ostream &OS) const override {
    LogicalVisitor.printRecords(OS);
  };

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump this reader to the debug stream.
  void dump() const { print(dbgs()); }
#endif
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_READERS_CODEVIEWREADER_H
