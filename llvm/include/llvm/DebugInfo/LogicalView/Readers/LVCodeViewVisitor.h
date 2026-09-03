//===-- LVCodeViewVisitor.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVCodeViewVisitor class, which is used to describe a
// debug information (CodeView) visitor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_READERS_CODEVIEWVISITOR_H
#define LLVM_DEBUGINFO_LOGICALVIEW_READERS_CODEVIEWVISITOR_H

#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/CodeView/SymbolDumpDelegate.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbacks.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/DebugInfo/LogicalView/Readers/LVBinaryReader.h"
#include "llvm/DebugInfo/PDB/Native/InputFile.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include <stack>
#include <utility>

namespace llvm {
namespace logicalview {

using namespace llvm::codeview;

class LVCodeViewReader;
class LVLogicalVisitor;
/// Shared forward-reference, type, string, and line state for CodeView visitors.
struct LVShared;

/// Visitor that walks CodeView type streams and records kinds for later use.
class LLVM_ABI LVTypeVisitor final : public TypeVisitorCallbacks {
  ScopedPrinter &W;
  LVLogicalVisitor *LogicalVisitor;
  LazyRandomTypeCollection &Types;
  LazyRandomTypeCollection &Ids;
  uint32_t StreamIdx;
  LVShared *Shared = nullptr;

  // In a PDB, a type index may refer to a type (TPI) or an item ID (IPI).
  // In a COFF or PDB (/Z7), the type index always refer to a type (TPI).
  // When creating logical elements, we must access the correct element
  // table, while searching for a type index.
  bool HasIds = false;

  // Current type index during the types traversal.
  TypeIndex CurrentTypeIndex = TypeIndex::None();

  void printTypeIndex(StringRef FieldName, TypeIndex TI,
                      uint32_t StreamIdx) const;

public:
  /// Construct a type visitor for the given type and ID collections.
  ///
  /// \param W Printer used for diagnostic and dump output.
  /// \param LogicalVisitor Logical visitor that builds elements from records.
  /// \param Types Type collection (TPI) for the current input.
  /// \param Ids Item ID collection (IPI), or the same as \p Types when absent.
  /// \param StreamIdx Stream index identifying whether records come from TPI or
  /// IPI.
  /// \param Shared Shared visitor state for forward refs and recorded kinds.
  LVTypeVisitor(ScopedPrinter &W, LVLogicalVisitor *LogicalVisitor,
                LazyRandomTypeCollection &Types, LazyRandomTypeCollection &Ids,
                uint32_t StreamIdx, LVShared *Shared)
      : TypeVisitorCallbacks(), W(W), LogicalVisitor(LogicalVisitor),
        Types(Types), Ids(Ids), StreamIdx(StreamIdx), Shared(Shared) {
    HasIds = &Types != &Ids;
  }

  /// Called when visitation of a type record begins without a type index.
  /// \param Record The type record being visited.
  /// \returns Success or an error if beginning visitation fails.
  Error visitTypeBegin(CVType &Record) override;
  /// Called when visitation of a type record begins at a known type index.
  /// \param Record The type record being visited.
  /// \param TI Type index of \p Record in the type stream.
  /// \returns Success or an error if beginning visitation fails.
  Error visitTypeBegin(CVType &Record, TypeIndex TI) override;
  /// Called when visitation of a field-list member begins.
  /// \param Record The member record being visited.
  /// \returns Success or an error if beginning visitation fails.
  Error visitMemberBegin(CVMemberRecord &Record) override;
  /// Called when visitation of a field-list member ends.
  /// \param Record The member record whose visitation is complete.
  /// \returns Success or an error if ending visitation fails.
  Error visitMemberEnd(CVMemberRecord &Record) override;
  /// Action to take on unknown field-list members.
  /// \param Record The unknown member record being visited.
  /// \returns Success or an error if handling the unknown record fails.
  Error visitUnknownMember(CVMemberRecord &Record) override;

  /// Visit a known BuildInfo type record.
  /// \param Record Raw CodeView type record being visited.
  /// \param Args Deserialized BuildInfo record payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVType &Record, BuildInfoRecord &Args) override;
  /// Visit a known Class type record.
  /// \param Record Raw CodeView type record being visited.
  /// \param Class Deserialized Class record payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVType &Record, ClassRecord &Class) override;
  /// Visit a known Enum type record.
  /// \param Record Raw CodeView type record being visited.
  /// \param Enum Deserialized Enum record payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVType &Record, EnumRecord &Enum) override;
  /// Visit a known FuncId type record.
  /// \param Record Raw CodeView type record being visited.
  /// \param Func Deserialized FuncId record payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVType &Record, FuncIdRecord &Func) override;
  /// Visit a known Procedure type record.
  /// \param Record Raw CodeView type record being visited.
  /// \param Proc Deserialized Procedure record payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVType &Record, ProcedureRecord &Proc) override;
  /// Visit a known StringId type record.
  /// \param Record Raw CodeView type record being visited.
  /// \param String Deserialized StringId record payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVType &Record, StringIdRecord &String) override;
  /// Visit a known UdtSourceLine type record.
  /// \param Record Raw CodeView type record being visited.
  /// \param Line Deserialized UdtSourceLine record payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVType &Record, UdtSourceLineRecord &Line) override;
  /// Visit a known Union type record.
  /// \param Record Raw CodeView type record being visited.
  /// \param Union Deserialized Union record payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVType &Record, UnionRecord &Union) override;
  /// Action to take on unknown type records.
  /// \param Record The unknown type record being visited.
  /// \returns Success or an error if handling the unknown record fails.
  Error visitUnknownType(CVType &Record) override;
};

/// Symbol visitor delegate that resolves COFF section relocations and names.
class LLVM_ABI LVSymbolVisitorDelegate final : public SymbolVisitorDelegate {
  LVCodeViewReader *Reader;
  const llvm::object::coff_section *CoffSection;
  StringRef SectionContents;

public:
  /// Construct a symbol visitor delegate for one COFF section.
  ///
  /// \param Reader CodeView reader that owns relocation and name resolution.
  /// \param Section Object section whose symbols are being visited.
  /// \param Obj COFF object file that contains \p Section.
  /// \param SectionContents Raw bytes of \p Section.
  LVSymbolVisitorDelegate(LVCodeViewReader *Reader,
                          const llvm::object::SectionRef &Section,
                          const llvm::object::COFFObjectFile *Obj,
                          StringRef SectionContents)
      : Reader(Reader), SectionContents(SectionContents) {
    CoffSection = Obj->getCOFFSection(Section);
  }

  /// Return the byte offset of the current record within the section contents.
  /// \param Reader Stream reader positioned at the current symbol record.
  /// \returns Byte offset of the current record within the section contents.
  uint32_t getRecordOffset(BinaryStreamReader Reader) override {
    ArrayRef<uint8_t> Data;
    if (Error Err = Reader.readLongestContiguousChunk(Data)) {
      llvm::consumeError(std::move(Err));
      return 0;
    }
    return Data.data() - SectionContents.bytes_begin();
  }

  /// Print a relocated COFF field with an optional resolved symbol name.
  ///
  /// \param Label Field label printed before the relocated value.
  /// \param RelocOffset Offset of the relocation within the section.
  /// \param Offset Field offset used when resolving the symbol name.
  /// \param RelocSym Optional output for the resolved symbol name.
  void printRelocatedField(StringRef Label, uint32_t RelocOffset,
                           uint32_t Offset, StringRef *RelocSym = nullptr);

  /// Resolve a relocated linkage name for a COFF section field.
  ///
  /// \param RelocOffset Offset of the relocation within the section.
  /// \param Offset Field offset used when resolving the symbol name.
  /// \param RelocSym Optional output for the resolved symbol name.
  void getLinkageName(uint32_t RelocOffset, uint32_t Offset,
                      StringRef *RelocSym = nullptr);

  /// Return the source file name for a CodeView file checksum offset.
  /// \param FileOffset Offset into the file checksum / string table.
  /// \returns Source file name for the given CodeView file checksum offset.
  StringRef getFileNameForFileOffset(uint32_t FileOffset) override;
  /// Return the debug string table subsection for the current module.
  /// \returns Debug string table subsection for the current module.
  DebugStringTableSubsectionRef getStringTable() override;
};

class LVElement;
class LVScope;
class LVSymbol;
class LVType;

/// Visitor for CodeView symbol streams found in COFF object files and PDB
/// files.
class LLVM_ABI LVSymbolVisitor final : public SymbolVisitorCallbacks {
  LVCodeViewReader *Reader;
  ScopedPrinter &W;
  LVLogicalVisitor *LogicalVisitor;
  LazyRandomTypeCollection &Types;
  LazyRandomTypeCollection &Ids;
  LVSymbolVisitorDelegate *ObjDelegate;
  LVShared *Shared;

  // Symbol offset when processing PDB streams.
  uint32_t CurrentOffset = 0;
  // Current object name collected from S_OBJNAME.
  StringRef CurrentObjectName;
  // Last symbol processed by S_LOCAL.
  LVSymbol *LocalSymbol = nullptr;

  bool HasIds;
  bool InFunctionScope = false;
  bool IsCompileUnit = false;

  // Register for the locals and parameters symbols in the current frame.
  RegisterId LocalFrameRegister = RegisterId::NONE;
  RegisterId ParamFrameRegister = RegisterId::NONE;

  void printLocalVariableAddrRange(const LocalVariableAddrRange &Range,
                                   uint32_t RelocationOffset);
  void printLocalVariableAddrGap(ArrayRef<LocalVariableAddrGap> Gaps);
  void printTypeIndex(StringRef FieldName, TypeIndex TI) const;

  // Return true if this symbol is a Compile Unit.
  bool symbolIsCompileUnit(SymbolKind Kind) {
    switch (Kind) {
    case SymbolKind::S_COMPILE2:
    case SymbolKind::S_COMPILE3:
      return true;
    default:
      return false;
    }
  }

  // Determine symbol kind (local or parameter).
  void determineSymbolKind(LVSymbol *Symbol, RegisterId Register) {
    if (Register == LocalFrameRegister) {
      Symbol->setIsVariable();
      return;
    }
    if (Register == ParamFrameRegister) {
      Symbol->setIsParameter();
      return;
    }
    // Assume is a variable.
    Symbol->setIsVariable();
  }

  void setLocalVariableType(LVSymbol *Symbol, TypeIndex TI);

public:
  /// Construct a symbol visitor for CodeView symbol streams.
  ///
  /// \param Reader CodeView reader that owns the logical view being built.
  /// \param W Printer used for diagnostic and dump output.
  /// \param LogicalVisitor Logical visitor that builds elements from symbols.
  /// \param Types Type collection (TPI) for the current input.
  /// \param Ids Item ID collection (IPI), or the same as \p Types when absent.
  /// \param ObjDelegate Optional COFF section delegate for relocations.
  /// \param Shared Shared visitor state for forward refs and recorded kinds.
  LVSymbolVisitor(LVCodeViewReader *Reader, ScopedPrinter &W,
                  LVLogicalVisitor *LogicalVisitor,
                  LazyRandomTypeCollection &Types,
                  LazyRandomTypeCollection &Ids,
                  LVSymbolVisitorDelegate *ObjDelegate, LVShared *Shared)
      : Reader(Reader), W(W), LogicalVisitor(LogicalVisitor), Types(Types),
        Ids(Ids), ObjDelegate(ObjDelegate), Shared(Shared) {
    HasIds = &Types != &Ids;
  }

  /// Called when visitation of a symbol record begins without a stream offset.
  /// \param Record The symbol record being visited.
  /// \returns Success or an error if beginning visitation fails.
  Error visitSymbolBegin(CVSymbol &Record) override;
  /// Called when visitation of a symbol record begins at a known offset.
  /// \param Record The symbol record being visited.
  /// \param Offset Byte offset of \p Record within its symbol stream.
  /// \returns Success or an error if beginning visitation fails.
  Error visitSymbolBegin(CVSymbol &Record, uint32_t Offset) override;
  /// Called when visitation of a symbol record ends.
  /// \param Record The symbol record whose visitation is complete.
  /// \returns Success or an error if ending visitation fails.
  Error visitSymbolEnd(CVSymbol &Record) override;
  /// Action to take on unknown symbol records.
  /// \param Record The unknown symbol record being visited.
  /// \returns Success or an error if handling the unknown record fails.
  Error visitUnknownSymbol(CVSymbol &Record) override;

  /// Visit a known Block symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Block Deserialized Block symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, BlockSym &Block) override;
  /// Visit a known BPRelative symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Local Deserialized BPRelative symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, BPRelativeSym &Local) override;
  /// Visit a known BuildInfo symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param BuildInfo Deserialized BuildInfo symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, BuildInfoSym &BuildInfo) override;
  /// Visit a known Compile2 symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Compile2 Deserialized Compile2 symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, Compile2Sym &Compile2) override;
  /// Visit a known Compile3 symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Compile3 Deserialized Compile3 symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, Compile3Sym &Compile3) override;
  /// Visit a known Constant symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Constant Deserialized Constant symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, ConstantSym &Constant) override;
  /// Visit a known Data symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Data Deserialized Data symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, DataSym &Data) override;
  /// Visit a known DefRangeFramePointerRelFullScope symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param DefRangeFramePointerRelFullScope Deserialized full-scope frame
  /// pointer relative def-range payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record,
                         DefRangeFramePointerRelFullScopeSym
                             &DefRangeFramePointerRelFullScope) override;
  /// Visit a known DefRangeFramePointerRel symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param DefRangeFramePointerRel Deserialized frame-pointer-relative
  /// def-range payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(
      CVSymbol &Record,
      DefRangeFramePointerRelSym &DefRangeFramePointerRel) override;
  /// Visit a known DefRangeRegisterRel symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param DefRangeRegisterRel Deserialized register-relative def-range
  /// payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record,
                         DefRangeRegisterRelSym &DefRangeRegisterRel) override;
  /// Visit a known DefRangeRegisterRelIndir symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param DefRangeRegisterRelIndir Deserialized indirect register-relative
  /// def-range payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(
      CVSymbol &Record,
      DefRangeRegisterRelIndirSym &DefRangeRegisterRelIndir) override;
  /// Visit a known DefRangeRegister symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param DefRangeRegister Deserialized register def-range payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record,
                         DefRangeRegisterSym &DefRangeRegister) override;
  /// Visit a known DefRangeSubfieldRegister symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param DefRangeSubfieldRegister Deserialized subfield register def-range
  /// payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(
      CVSymbol &Record,
      DefRangeSubfieldRegisterSym &DefRangeSubfieldRegister) override;
  /// Visit a known DefRangeSubfield symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param DefRangeSubfield Deserialized subfield def-range payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record,
                         DefRangeSubfieldSym &DefRangeSubfield) override;
  /// Visit a known DefRange symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param DefRange Deserialized DefRange symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, DefRangeSym &DefRange) override;
  /// Visit a known FrameProc symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param FrameProc Deserialized FrameProc symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, FrameProcSym &FrameProc) override;
  /// Visit a known InlineSite symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param InlineSite Deserialized InlineSite symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, InlineSiteSym &InlineSite) override;
  /// Visit a known Local symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Local Deserialized Local symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, LocalSym &Local) override;
  /// Visit a known ObjName symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param ObjName Deserialized ObjName symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, ObjNameSym &ObjName) override;
  /// Visit a known Proc symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Proc Deserialized Proc symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, ProcSym &Proc) override;
  /// Visit a known RegRelative symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Local Deserialized RegRelative symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, RegRelativeSym &Local) override;
  /// Visit a known RegRelativeIndir symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Local Deserialized RegRelativeIndir symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, RegRelativeIndirSym &Local) override;
  /// Visit a known ScopeEnd symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param ScopeEnd Deserialized ScopeEnd symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, ScopeEndSym &ScopeEnd) override;
  /// Visit a known Thunk32 symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Thunk Deserialized Thunk32 symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, Thunk32Sym &Thunk) override;
  /// Visit a known UDT symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param UDT Deserialized UDT symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, UDTSym &UDT) override;
  /// Visit a known UsingNamespace symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param UN Deserialized UsingNamespace symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, UsingNamespaceSym &UN) override;
  /// Visit a known JumpTable symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param JumpTable Deserialized JumpTable symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, JumpTableSym &JumpTable) override;
  /// Visit a known Caller symbol record.
  /// \param Record Raw CodeView symbol record being visited.
  /// \param Caller Deserialized Caller symbol payload.
  /// \returns Success or an error if visitation fails.
  Error visitKnownRecord(CVSymbol &Record, CallerSym &Caller) override;
};

/// Visitor for CodeView types and symbols that populates logical elements.
class LVLogicalVisitor final {
  LVCodeViewReader *Reader;
  ScopedPrinter &W;

  // Encapsulates access to the input file and any dependent type server,
  // including any precompiled header object.
  llvm::pdb::InputFile &Input;
  std::shared_ptr<llvm::pdb::InputFile> TypeServer = nullptr;
  std::shared_ptr<LazyRandomTypeCollection> PrecompHeader = nullptr;

  std::shared_ptr<LVShared> Shared;

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

  using LVScopeStack = std::stack<LVScope *>;
  LVScopeStack ScopeStack;
  LVScope *ReaderParent = nullptr;
  LVScope *ReaderScope = nullptr;
  bool InCompileUnitScope = false;

  // Allow processing of argument list.
  bool ProcessArgumentList = false;
  StringRef OverloadedMethodName;
  std::string CompileUnitName;

  // Inlined functions source information.
  using LVInlineeEntry = std::pair<uint32_t, StringRef>;
  using LVInlineeInfo = std::map<TypeIndex, LVInlineeEntry>;
  LVInlineeInfo InlineeInfo;

  Error visitFieldListMemberStream(TypeIndex TI, LVElement *Element,
                                   ArrayRef<uint8_t> FieldList);

  LVType *createBaseType(TypeIndex TI, StringRef TypeName);
  LVType *createPointerType(TypeIndex TI, StringRef TypeName);
  LVSymbol *createParameter(TypeIndex TI, StringRef Name, LVScope *Parent);
  LVSymbol *createParameter(LVElement *Element, StringRef Name,
                            LVScope *Parent);
  void createDataMember(CVMemberRecord &Record, LVScope *Parent, StringRef Name,
                        TypeIndex Type, MemberAccess Access);
  void createParents(StringRef ScopedName, LVElement *Element);

public:
  /// Construct a logical visitor for the given CodeView input.
  ///
  /// \param Reader CodeView reader that owns the logical view being built.
  /// \param W Printer used for diagnostic and dump output.
  /// \param Input Input file encapsulating types, IDs, and dependent servers.
  LLVM_ABI LVLogicalVisitor(LVCodeViewReader *Reader, ScopedPrinter &W,
                            llvm::pdb::InputFile &Input);

  /// Current logical element while processing a type or symbol record.
  ///
  /// Shared with the symbol visitor for the active record.
  LVElement *CurrentElement = nullptr;
  /// Current logical scope while processing a type or symbol record.
  ///
  /// Shared with the symbol visitor for the active record.
  LVScope *CurrentScope = nullptr;
  /// Current logical symbol while processing a type or symbol record.
  ///
  /// Shared with the symbol visitor for the active record.
  LVSymbol *CurrentSymbol = nullptr;
  /// Current logical type while processing a type or symbol record.
  ///
  /// Shared with the symbol visitor for the active record.
  LVType *CurrentType = nullptr;

  /// Set the type-server input used for dependent PDB type streams.
  /// \param TypeServer Shared input file for the loaded type server PDB.
  void setInput(std::shared_ptr<llvm::pdb::InputFile> TypeServer) {
    this->TypeServer = TypeServer;
  }
  /// Set the precompiled-header type collection used as a TPI substitute.
  /// \param PrecompHeader Shared lazy type collection for the precompiled
  /// header.
  void setInput(std::shared_ptr<LazyRandomTypeCollection> PrecompHeader) {
    this->PrecompHeader = PrecompHeader;
  }

  /// Record inlinee source location information for type index \p TI.
  ///
  /// \param TI Type index of the inlined function.
  /// \param LineNumber Source line number for the inline site.
  /// \param Filename Source file name for the inline site.
  void addInlineeInfo(TypeIndex TI, uint32_t LineNumber, StringRef Filename) {
    InlineeInfo.emplace(std::piecewise_construct, std::forward_as_tuple(TI),
                        std::forward_as_tuple(LineNumber, Filename));
  }

  /// Print a CodeView type index field to the scoped printer.
  ///
  /// \param FieldName Printed field name for the type index.
  /// \param TI Type index value to format and print.
  /// \param StreamIdx Stream index selecting TPI or IPI name resolution.
  LLVM_ABI void printTypeIndex(StringRef FieldName, TypeIndex TI,
                               uint32_t StreamIdx);
  /// Print member access, method kind, and options from \p Attrs.
  /// \param Attrs Packed member attributes to print.
  LLVM_ABI void printMemberAttributes(MemberAttributes Attrs);
  /// Print discrete member access, method kind, and options fields.
  ///
  /// \param Access Member access specifier to print.
  /// \param Kind Method kind to print.
  /// \param Options Method option flags to print.
  LLVM_ABI void printMemberAttributes(MemberAccess Access, MethodKind Kind,
                                      MethodOptions Options);

  /// Create a logical element for the given CodeView type leaf kind.
  /// \param Kind Type leaf kind selecting the element class to create.
  /// \returns Newly created logical element for the given kind.
  LLVM_ABI LVElement *createElement(TypeLeafKind Kind);
  /// Create a logical element for the given CodeView symbol kind.
  /// \param Kind Symbol kind selecting the element class to create.
  /// \returns Newly created logical element for the given kind.
  LLVM_ABI LVElement *createElement(SymbolKind Kind);
  /// Create or look up a logical element for type index \p TI of kind \p Kind.
  ///
  /// \param TI Type index associated with the element.
  /// \param Kind Type leaf kind selecting the element class to create.
  /// \returns Existing or newly created logical element for \p TI.
  LLVM_ABI LVElement *createElement(TypeIndex TI, TypeLeafKind Kind);

  /// Decode InlineSite annotation bytecode into code and line offsets.
  ///
  /// \param AbstractFunction Abstract (outline) function scope for the site.
  /// \param InlinedFunction Inlined function scope receiving the annotations.
  /// \param InlineSite Deserialized InlineSite symbol with annotation data.
  /// \returns Success or an error if annotation decoding fails.
  LLVM_ABI Error inlineSiteAnnotation(LVScope *AbstractFunction,
                                      LVScope *InlinedFunction,
                                      InlineSiteSym &InlineSite);

  /// Push \p Scope as the current reader scope.
  /// \param Scope Scope that becomes the active reader scope.
  void pushScope(LVScope *Scope) {
    ScopeStack.push(ReaderParent);
    ReaderParent = ReaderScope;
    ReaderScope = Scope;
  }
  /// Pop the current reader scope and restore its parent.
  void popScope() {
    ReaderScope = ReaderParent;
    ReaderParent = ScopeStack.top();
    ScopeStack.pop();
  }
  /// Close the current compile-unit scope if one is active.
  void closeScope() {
    if (InCompileUnitScope) {
      InCompileUnitScope = false;
      popScope();
    }
  }
  /// Set the root scope used as the initial reader scope.
  /// \param Root Root logical scope for the reader.
  void setRoot(LVScope *Root) { ReaderScope = Root; }

  /// Add \p Scope to the logical tree, optionally as a compile unit.
  ///
  /// \param Scope Scope to insert under the current reader scope.
  /// \param IsCompileUnit Whether \p Scope is a compile-unit scope.
  LLVM_ABI void addElement(LVScope *Scope, bool IsCompileUnit);
  /// Add \p Symbol to the current reader scope.
  /// \param Symbol Symbol to insert under the current reader scope.
  LLVM_ABI void addElement(LVSymbol *Symbol);
  /// Add \p Type to the current reader scope.
  /// \param Type Type to insert under the current reader scope.
  LLVM_ABI void addElement(LVType *Type);

  /// Return the compile-unit name collected from object or compile symbols.
  /// \returns Compile-unit name collected from object or compile symbols.
  std::string getCompileUnitName() { return CompileUnitName; }
  /// Set the compile-unit name collected from object or compile symbols.
  /// \param Name Compile-unit name to store.
  void setCompileUnitName(std::string Name) {
    CompileUnitName = std::move(Name);
  }

  /// Find or create the logical element for type index \p TI.
  ///
  /// \param StreamIdx Stream index selecting TPI or IPI lookup.
  /// \param TI Type index of the requested element.
  /// \param Parent Optional parent scope when creating a missing element.
  /// \returns Logical element for type index \p TI.
  LLVM_ABI LVElement *getElement(uint32_t StreamIdx, TypeIndex TI,
                                 LVScope *Parent = nullptr);
  /// Return the shared visitor state for types, strings, and line records.
  /// \returns Shared visitor state for types, strings, and line records.
  LVShared *getShared() { return Shared.get(); }

  /// Return the current reader scope.
  /// \returns Current reader scope.
  LVScope *getReaderScope() const { return ReaderScope; }

  /// Print the start of a type record dump for \p Element.
  ///
  /// \param Record Raw CodeView type record being printed.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \param StreamIdx Stream index selecting TPI or IPI labeling.
  LLVM_ABI void printTypeBegin(CVType &Record, TypeIndex TI, LVElement *Element,
                               uint32_t StreamIdx);
  /// Print the end of a type record dump.
  /// \param Record Raw CodeView type record whose dump is complete.
  LLVM_ABI void printTypeEnd(CVType &Record);
  /// Print the start of a field-list member dump for \p Element.
  ///
  /// \param Record Raw field-list member record being printed.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \param StreamIdx Stream index selecting TPI or IPI labeling.
  LLVM_ABI void printMemberBegin(CVMemberRecord &Record, TypeIndex TI,
                                 LVElement *Element, uint32_t StreamIdx);
  /// Print the end of a field-list member dump.
  /// \param Record Raw field-list member record whose dump is complete.
  LLVM_ABI void printMemberEnd(CVMemberRecord &Record);

  /// Enable processing of argument-list type records.
  void startProcessArgumentList() { ProcessArgumentList = true; }
  /// Disable processing of argument-list type records.
  void stopProcessArgumentList() { ProcessArgumentList = false; }

  /// Attach collected filenames to the current compile unit.
  LLVM_ABI void processFiles();
  /// Attach collected line records to the current compile unit.
  LLVM_ABI void processLines();
  /// Deduce and create namespace scopes from collected names.
  LLVM_ABI void processNamespaces();

  /// Print collected CodeView type and symbol records to \p OS.
  /// \param OS Output stream to write to.
  LLVM_ABI void printRecords(raw_ostream &OS) const;

  /// Visit an unknown type record at type index \p TI.
  ///
  /// \param Record Raw unknown CodeView type record.
  /// \param TI Type index of \p Record.
  /// \returns Success or an error if handling the unknown record fails.
  LLVM_ABI Error visitUnknownType(CVType &Record, TypeIndex TI);
  /// Visit a known ArgList type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Args Deserialized ArgList record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, ArgListRecord &Args,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known Array type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param AT Deserialized Array record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, ArrayRecord &AT, TypeIndex TI,
                                  LVElement *Element);
  /// Visit a known BitField type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param BF Deserialized BitField record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, BitFieldRecord &BF,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known BuildInfo type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param BI Deserialized BuildInfo record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, BuildInfoRecord &BI,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known Class type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Class Deserialized Class record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, ClassRecord &Class,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known Enum type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Enum Deserialized Enum record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, EnumRecord &Enum,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known FieldList type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param FieldList Deserialized FieldList record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, FieldListRecord &FieldList,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known FuncId type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Func Deserialized FuncId record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, FuncIdRecord &Func,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known Label type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param LR Deserialized Label record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, LabelRecord &LR, TypeIndex TI,
                                  LVElement *Element);
  /// Visit a known Modifier type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Mod Deserialized Modifier record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, ModifierRecord &Mod,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known MemberFuncId type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Id Deserialized MemberFuncId record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, MemberFuncIdRecord &Id,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known MemberFunction type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param MF Deserialized MemberFunction record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, MemberFunctionRecord &MF,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known MethodOverloadList type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Overloads Deserialized MethodOverloadList record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record,
                                  MethodOverloadListRecord &Overloads,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known Pointer type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Ptr Deserialized Pointer record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, PointerRecord &Ptr,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known Procedure type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Proc Deserialized Procedure record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, ProcedureRecord &Proc,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known Union type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Union Deserialized Union record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, UnionRecord &Union,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known TypeServer2 type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param TS Deserialized TypeServer2 record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, TypeServer2Record &TS,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known VFTable type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param VFT Deserialized VFTable record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, VFTableRecord &VFT,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known VFTableShape type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Shape Deserialized VFTableShape record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, VFTableShapeRecord &Shape,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known StringList type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Strings Deserialized StringList record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, StringListRecord &Strings,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known StringId type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param String Deserialized StringId record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, StringIdRecord &String,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known UdtSourceLine type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param SourceLine Deserialized UdtSourceLine record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record,
                                  UdtSourceLineRecord &SourceLine, TypeIndex TI,
                                  LVElement *Element);
  /// Visit a known UdtModSourceLine type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param ModSourceLine Deserialized UdtModSourceLine record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record,
                                  UdtModSourceLineRecord &ModSourceLine,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known Precomp type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param Precomp Deserialized Precomp record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, PrecompRecord &Precomp,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known EndPrecomp type record and update \p Element.
  ///
  /// \param Record Raw CodeView type record being visited.
  /// \param EndPrecomp Deserialized EndPrecomp record payload.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownRecord(CVType &Record, EndPrecompRecord &EndPrecomp,
                                  TypeIndex TI, LVElement *Element);

  /// Visit an unknown field-list member at type index \p TI.
  ///
  /// \param Record Raw unknown field-list member record.
  /// \param TI Type index of the enclosing field list.
  /// \returns Success or an error if handling the unknown record fails.
  LLVM_ABI Error visitUnknownMember(CVMemberRecord &Record, TypeIndex TI);
  /// Visit a known BaseClass member and update \p Element.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param Base Deserialized BaseClass member payload.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownMember(CVMemberRecord &Record, BaseClassRecord &Base,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known DataMember member and update \p Element.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param Field Deserialized DataMember member payload.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownMember(CVMemberRecord &Record,
                                  DataMemberRecord &Field, TypeIndex TI,
                                  LVElement *Element);
  /// Visit a known Enumerator member and update \p Element.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param Enum Deserialized Enumerator member payload.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownMember(CVMemberRecord &Record,
                                  EnumeratorRecord &Enum, TypeIndex TI,
                                  LVElement *Element);
  /// Visit a known ListContinuation member and update \p Element.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param Cont Deserialized ListContinuation member payload.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownMember(CVMemberRecord &Record,
                                  ListContinuationRecord &Cont, TypeIndex TI,
                                  LVElement *Element);
  /// Visit a known NestedType member and update \p Element.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param Nested Deserialized NestedType member payload.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownMember(CVMemberRecord &Record,
                                  NestedTypeRecord &Nested, TypeIndex TI,
                                  LVElement *Element);
  /// Visit a known OneMethod member and update \p Element.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param Method Deserialized OneMethod member payload.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownMember(CVMemberRecord &Record,
                                  OneMethodRecord &Method, TypeIndex TI,
                                  LVElement *Element);
  /// Visit a known OverloadedMethod member and update \p Element.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param Method Deserialized OverloadedMethod member payload.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownMember(CVMemberRecord &Record,
                                  OverloadedMethodRecord &Method, TypeIndex TI,
                                  LVElement *Element);
  /// Visit a known StaticDataMember member and update \p Element.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param Field Deserialized StaticDataMember member payload.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownMember(CVMemberRecord &Record,
                                  StaticDataMemberRecord &Field, TypeIndex TI,
                                  LVElement *Element);
  /// Visit a known VFPtr member and update \p Element.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param VFTable Deserialized VFPtr member payload.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownMember(CVMemberRecord &Record, VFPtrRecord &VFTable,
                                  TypeIndex TI, LVElement *Element);
  /// Visit a known VirtualBaseClass member and update \p Element.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param Base Deserialized VirtualBaseClass member payload.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  LLVM_ABI Error visitKnownMember(CVMemberRecord &Record,
                                  VirtualBaseClassRecord &Base, TypeIndex TI,
                                  LVElement *Element);

  /// Deserialize member \p T via \p Callbacks and visit it for \p Element.
  ///
  /// \tparam T Concrete CodeView member record type to deserialize.
  /// \param Record Raw field-list member record being visited.
  /// \param Callbacks Visitor callbacks used to deserialize \p T.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if visitation fails.
  template <typename T>
  Error visitKnownMember(CVMemberRecord &Record,
                         TypeVisitorCallbacks &Callbacks, TypeIndex TI,
                         LVElement *Element) {
    TypeRecordKind RK = static_cast<TypeRecordKind>(Record.Kind);
    T KnownRecord(RK);
    if (Error Err = Callbacks.visitKnownMember(Record, KnownRecord))
      return Err;
    if (Error Err = visitKnownMember(Record, KnownRecord, TI, Element))
      return Err;
    return Error::success();
  }

  /// Deserialize type record \p T and visit it for \p Element.
  ///
  /// \tparam T Concrete CodeView type record type to deserialize.
  /// \param Record Raw CodeView type record being visited.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if visitation fails.
  template <typename T>
  Error visitKnownRecord(CVType &Record, TypeIndex TI, LVElement *Element) {
    TypeRecordKind RK = static_cast<TypeRecordKind>(Record.kind());
    T KnownRecord(RK);
    if (Error Err = TypeDeserializer::deserializeAs(
            const_cast<CVType &>(Record), KnownRecord))
      return Err;
    if (Error Err = visitKnownRecord(Record, KnownRecord, TI, Element))
      return Err;
    return Error::success();
  }

  /// Dispatch visitation of a field-list member through \p Callbacks.
  ///
  /// \param Record Raw field-list member record being visited.
  /// \param Callbacks Visitor callbacks that select the concrete member type.
  /// \param TI Type index of the enclosing field list.
  /// \param Element Logical element associated with the enclosing type.
  /// \returns Success or an error if member visitation fails.
  LLVM_ABI Error visitMemberRecord(CVMemberRecord &Record,
                                   TypeVisitorCallbacks &Callbacks,
                                   TypeIndex TI, LVElement *Element);
  /// Finish visitation of type record \p Record for \p Element.
  ///
  /// \param Record Raw CodeView type record whose concrete kind is dispatched.
  /// \param TI Type index of \p Record.
  /// \param Element Logical element associated with \p Record.
  /// \returns Success or an error if finishing visitation fails.
  LLVM_ABI Error finishVisitation(CVType &Record, TypeIndex TI,
                                  LVElement *Element);
};

} // namespace logicalview
} // namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_READERS_CODEVIEWVISITOR_H
