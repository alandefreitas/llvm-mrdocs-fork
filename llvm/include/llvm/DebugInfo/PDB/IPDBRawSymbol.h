//===- IPDBRawSymbol.h - base interface for PDB symbol types ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBRAWSYMBOL_H
#define LLVM_DEBUGINFO_PDB_IPDBRAWSYMBOL_H

#include "PDBTypes.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {
class raw_ostream;
class StringRef;

namespace pdb {

/// Bitmask selecting which symbol-id fields to print or recurse into when
/// dumping.
enum class PdbSymbolIdField : uint32_t {
  None = 0,                    ///< No symbol-id fields.
  SymIndexId = 1 << 0,         ///< The symbol's own SymIndexId.
  LexicalParent = 1 << 1,      ///< Lexical parent symbol id.
  ClassParent = 1 << 2,        ///< Class parent symbol id.
  Type = 1 << 3,               ///< Type symbol id.
  UnmodifiedType = 1 << 4,     ///< Unmodified type symbol id.
  All = 0xFFFFFFFF,            ///< All symbol-id fields.
  /// Largest enumerator marker for LLVM_BITMASK_LARGEST_ENUMERATOR.
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ All)
};

/// Dump a named symbol-id field, optionally recursing into the referenced
/// symbol.
///
/// \param OS Output stream to write to.
/// \param Name Field name to print.
/// \param Value Symbol index id for this field.
/// \param Indent Indentation level in spaces.
/// \param Session PDB session used to resolve and dump child symbols.
/// \param FieldId Which symbol-id field this value represents.
/// \param ShowFlags Bitmask of fields that should be printed.
/// \param RecurseFlags Bitmask of fields that should be expanded recursively.
LLVM_ABI void
dumpSymbolIdField(raw_ostream &OS, StringRef Name, SymIndexId Value, int Indent,
                  const IPDBSession &Session, PdbSymbolIdField FieldId,
                  PdbSymbolIdField ShowFlags, PdbSymbolIdField RecurseFlags);

/// Interface representing an arbitrary PDB symbol.
///
/// It exposes a monolithic interface consisting of accessors for the union of
/// all properties that are valid for any symbol type.  This interface is then
/// wrapped by a concrete class which exposes only those set of methods valid
/// for this particular symbol type.  See PDBSymbol.h for more details.
class LLVM_ABI IPDBRawSymbol {
public:
  /// Destroy the raw symbol.
  virtual ~IPDBRawSymbol();

  /// Dump this symbol's properties to \p OS.
  ///
  /// \param OS Output stream to write to.
  /// \param Indent Indentation level in spaces.
  /// \param ShowIdFields Bitmask of symbol-id fields to print.
  /// \param RecurseIdFields Bitmask of symbol-id fields to expand recursively.
  virtual void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
                    PdbSymbolIdField RecurseIdFields) const = 0;

  /// Find child symbols of the given type.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \returns An enumerator over matching child symbols.
  virtual std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type) const = 0;

  /// Find child symbols of the given type whose name matches \p Name.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \param Name Name pattern to match.
  /// \param Flags Name-search options controlling the match.
  /// \returns An enumerator over matching child symbols.
  virtual std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type, StringRef Name,
               PDB_NameSearchFlags Flags) const = 0;

  /// Find child symbols by section and offset address.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \param Name Name pattern to match.
  /// \param Flags Name-search options controlling the match.
  /// \param Section Section index of the address.
  /// \param Offset Offset within the section.
  /// \returns An enumerator over matching child symbols.
  virtual std::unique_ptr<IPDBEnumSymbols>
  findChildrenByAddr(PDB_SymType Type, StringRef Name,
                     PDB_NameSearchFlags Flags,
                     uint32_t Section, uint32_t Offset) const = 0;

  /// Find child symbols by virtual address.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \param Name Name pattern to match.
  /// \param Flags Name-search options controlling the match.
  /// \param VA Virtual address to search.
  /// \returns An enumerator over matching child symbols.
  virtual std::unique_ptr<IPDBEnumSymbols>
  findChildrenByVA(PDB_SymType Type, StringRef Name, PDB_NameSearchFlags Flags,
                   uint64_t VA) const = 0;

  /// Find child symbols by relative virtual address.
  ///
  /// \param Type Symbol tag of children to enumerate.
  /// \param Name Name pattern to match.
  /// \param Flags Name-search options controlling the match.
  /// \param RVA Relative virtual address to search.
  /// \returns An enumerator over matching child symbols.
  virtual std::unique_ptr<IPDBEnumSymbols>
  findChildrenByRVA(PDB_SymType Type, StringRef Name, PDB_NameSearchFlags Flags,
                    uint32_t RVA) const = 0;

  /// Find inline frames at the given section and offset address.
  ///
  /// \param Section Section index of the address.
  /// \param Offset Offset within the section.
  /// \returns An enumerator over matching inline frame symbols.
  virtual std::unique_ptr<IPDBEnumSymbols>
  findInlineFramesByAddr(uint32_t Section, uint32_t Offset) const = 0;

  /// Find inline frames at the given relative virtual address.
  ///
  /// \param RVA Relative virtual address to search.
  /// \returns An enumerator over matching inline frame symbols.
  virtual std::unique_ptr<IPDBEnumSymbols>
  findInlineFramesByRVA(uint32_t RVA) const = 0;

  /// Find inline frames at the given virtual address.
  ///
  /// \param VA Virtual address to search.
  /// \returns An enumerator over matching inline frame symbols.
  virtual std::unique_ptr<IPDBEnumSymbols>
  findInlineFramesByVA(uint64_t VA) const = 0;

  /// Find line numbers for all inlinees of this symbol.
  ///
  /// \returns An enumerator over matching line-number entries.
  virtual std::unique_ptr<IPDBEnumLineNumbers> findInlineeLines() const = 0;

  /// Find inlinee line numbers covering the given section and offset range.
  ///
  /// \param Section Section index of the start address.
  /// \param Offset Offset within the section.
  /// \param Length Length in bytes of the address range.
  /// \returns An enumerator over matching line-number entries.
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByAddr(uint32_t Section, uint32_t Offset,
                         uint32_t Length) const = 0;

  /// Find inlinee line numbers covering the given RVA range.
  ///
  /// \param RVA Relative virtual address of the start of the range.
  /// \param Length Length in bytes of the address range.
  /// \returns An enumerator over matching line-number entries.
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByRVA(uint32_t RVA, uint32_t Length) const = 0;

  /// Find inlinee line numbers covering the given VA range.
  ///
  /// \param VA Virtual address of the start of the range.
  /// \param Length Length in bytes of the address range.
  /// \returns An enumerator over matching line-number entries.
  virtual std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByVA(uint64_t VA, uint32_t Length) const = 0;

  /// Append this symbol's raw data bytes to \p bytes.
  ///
  /// \param bytes Vector that receives the data bytes.
  virtual void getDataBytes(llvm::SmallVector<uint8_t, 32> &bytes) const = 0;

  /// Fill \p Version with the backend compiler version for this symbol.
  ///
  /// \param Version Receives the backend version components.
  virtual void getBackEndVersion(VersionInfo &Version) const = 0;

  /// Return the access level of this member symbol.
  ///
  /// \returns The access level of this member symbol.
  virtual PDB_MemberAccess getAccess() const = 0;

  /// Return the section-relative address offset of this symbol.
  ///
  /// \returns The section-relative address offset of this symbol.
  virtual uint32_t getAddressOffset() const = 0;

  /// Return the section index of this symbol's address.
  ///
  /// \returns The section index of this symbol's address.
  virtual uint32_t getAddressSection() const = 0;

  /// Return the PDB age associated with this symbol.
  ///
  /// \returns The PDB age associated with this symbol.
  virtual uint32_t getAge() const = 0;

  /// Return the symbol id of this array's index type.
  ///
  /// \returns The symbol id of this array's index type.
  virtual SymIndexId getArrayIndexTypeId() const = 0;

  /// Return the base data offset for this symbol.
  ///
  /// \returns The base data offset for this symbol.
  virtual uint32_t getBaseDataOffset() const = 0;

  /// Return the base data slot for this symbol.
  ///
  /// \returns The base data slot for this symbol.
  virtual uint32_t getBaseDataSlot() const = 0;

  /// Return the symbol id of this symbol's base symbol.
  ///
  /// \returns The symbol id of this symbol's base symbol.
  virtual SymIndexId getBaseSymbolId() const = 0;

  /// Return the builtin type kind for this type symbol.
  ///
  /// \returns The builtin type kind for this type symbol.
  virtual PDB_BuiltinType getBuiltinType() const = 0;

  /// Return the bit position of this bitfield member.
  ///
  /// \returns The bit position of this bitfield member.
  virtual uint32_t getBitPosition() const = 0;

  /// Return the calling convention of this function type.
  ///
  /// \returns The calling convention of this function type.
  virtual PDB_CallingConv getCallingConvention() const = 0;

  /// Return the symbol id of this symbol's class parent.
  ///
  /// \returns The symbol id of this symbol's class parent.
  virtual SymIndexId getClassParentId() const = 0;

  /// Return the name of the compiler that produced this symbol.
  ///
  /// \returns The name of the compiler that produced this symbol.
  virtual std::string getCompilerName() const = 0;

  /// Return the element or parameter count associated with this symbol.
  ///
  /// \returns The element or parameter count associated with this symbol.
  virtual uint32_t getCount() const = 0;

  /// Return the number of live ranges associated with this symbol.
  ///
  /// \returns The number of live ranges associated with this symbol.
  virtual uint32_t getCountLiveRanges() const = 0;

  /// Fill \p Version with the frontend compiler version for this symbol.
  ///
  /// \param Version Receives the frontend version components.
  virtual void getFrontEndVersion(VersionInfo &Version) const = 0;

  /// Return the source language of this symbol.
  ///
  /// \returns The source language of this symbol.
  virtual PDB_Lang getLanguage() const = 0;

  /// Return the symbol id of this symbol's lexical parent.
  ///
  /// \returns The symbol id of this symbol's lexical parent.
  virtual SymIndexId getLexicalParentId() const = 0;

  /// Return the library or object name associated with this symbol.
  ///
  /// \returns The library or object name associated with this symbol.
  virtual std::string getLibraryName() const = 0;

  /// Return the section-relative offset of this symbol's live-range start.
  ///
  /// \returns The section-relative offset of this symbol's live-range start.
  virtual uint32_t getLiveRangeStartAddressOffset() const = 0;

  /// Return the section index of this symbol's live-range start address.
  ///
  /// \returns The section index of this symbol's live-range start address.
  virtual uint32_t getLiveRangeStartAddressSection() const = 0;

  /// Return the RVA of this symbol's live-range start.
  ///
  /// \returns The RVA of this symbol's live-range start.
  virtual uint32_t getLiveRangeStartRelativeVirtualAddress() const = 0;

  /// Return the register id used as the local base pointer.
  ///
  /// \returns The register id used as the local base pointer.
  virtual codeview::RegisterId getLocalBasePointerRegisterId() const = 0;

  /// Return the symbol id of this dimension's lower bound.
  ///
  /// \returns The symbol id of this dimension's lower bound.
  virtual SymIndexId getLowerBoundId() const = 0;

  /// Return the memory-space kind for this HLSL symbol.
  ///
  /// \returns The memory-space kind for this HLSL symbol.
  virtual uint32_t getMemorySpaceKind() const = 0;

  /// Return the name of this symbol.
  ///
  /// \returns The name of this symbol.
  virtual std::string getName() const = 0;

  /// Return the number of accelerator pointer tags on this symbol.
  ///
  /// \returns The number of accelerator pointer tags on this symbol.
  virtual uint32_t getNumberOfAcceleratorPointerTags() const = 0;

  /// Return the number of columns in this matrix type.
  ///
  /// \returns The number of columns in this matrix type.
  virtual uint32_t getNumberOfColumns() const = 0;

  /// Return the number of type modifiers on this type.
  ///
  /// \returns The number of type modifiers on this type.
  virtual uint32_t getNumberOfModifiers() const = 0;

  /// Return the number of register indices on this symbol.
  ///
  /// \returns The number of register indices on this symbol.
  virtual uint32_t getNumberOfRegisterIndices() const = 0;

  /// Return the number of rows in this matrix type.
  ///
  /// \returns The number of rows in this matrix type.
  virtual uint32_t getNumberOfRows() const = 0;

  /// Return the object file name associated with this symbol.
  ///
  /// \returns The object file name associated with this symbol.
  virtual std::string getObjectFileName() const = 0;

  /// Return the OEM id associated with this symbol.
  ///
  /// \returns The OEM id associated with this symbol.
  virtual uint32_t getOemId() const = 0;

  /// Return the OEM symbol id associated with this symbol.
  ///
  /// \returns The OEM symbol id associated with this symbol.
  virtual SymIndexId getOemSymbolId() const = 0;

  /// Return the offset of this member within its UDT.
  ///
  /// \returns The offset of this member within its UDT.
  virtual uint32_t getOffsetInUdt() const = 0;

  /// Return the target CPU platform for this symbol.
  ///
  /// \returns The target CPU platform for this symbol.
  virtual PDB_Cpu getPlatform() const = 0;

  /// Return the rank of this multi-dimensional array type.
  ///
  /// \returns The rank of this multi-dimensional array type.
  virtual uint32_t getRank() const = 0;

  /// Return the register id where this symbol is located.
  ///
  /// \returns The register id where this symbol is located.
  virtual codeview::RegisterId getRegisterId() const = 0;

  /// Return the register type associated with this symbol.
  ///
  /// \returns The register type associated with this symbol.
  virtual uint32_t getRegisterType() const = 0;

  /// Return the relative virtual address of this symbol.
  ///
  /// \returns The relative virtual address of this symbol.
  virtual uint32_t getRelativeVirtualAddress() const = 0;

  /// Return the sampler slot for this HLSL resource.
  ///
  /// \returns The sampler slot for this HLSL resource.
  virtual uint32_t getSamplerSlot() const = 0;

  /// Return the signature value associated with this symbol.
  ///
  /// \returns The signature value associated with this symbol.
  virtual uint32_t getSignature() const = 0;

  /// Return the size in bytes of this member within its UDT.
  ///
  /// \returns The size in bytes of this member within its UDT.
  virtual uint32_t getSizeInUdt() const = 0;

  /// Return the slot index associated with this symbol.
  ///
  /// \returns The slot index associated with this symbol.
  virtual uint32_t getSlot() const = 0;

  /// Return the source file name associated with this symbol.
  ///
  /// \returns The source file name associated with this symbol.
  virtual std::string getSourceFileName() const = 0;

  /// Return the source line where this type is defined.
  ///
  /// \returns A line-number entry for the type definition, or null if none.
  virtual std::unique_ptr<IPDBLineNumber>
  getSrcLineOnTypeDefn() const = 0;

  /// Return the stride between elements of this array or matrix.
  ///
  /// \returns The stride between elements of this array or matrix.
  virtual uint32_t getStride() const = 0;

  /// Return the symbol id of this symbol's subtype.
  ///
  /// \returns The symbol id of this symbol's subtype.
  virtual SymIndexId getSubTypeId() const = 0;

  /// Return the path of the PDB or symbols file for this symbol.
  ///
  /// \returns The path of the PDB or symbols file for this symbol.
  virtual std::string getSymbolsFileName() const = 0;

  /// Return this symbol's unique SymIndexId within the session.
  ///
  /// \returns This symbol's unique SymIndexId within the session.
  virtual SymIndexId getSymIndexId() const = 0;

  /// Return the target offset for this thunk or branch symbol.
  ///
  /// \returns The target offset for this thunk or branch symbol.
  virtual uint32_t getTargetOffset() const = 0;

  /// Return the target relative virtual address for this thunk or branch.
  ///
  /// \returns The target relative virtual address for this thunk or branch.
  virtual uint32_t getTargetRelativeVirtualAddress() const = 0;

  /// Return the target virtual address for this thunk or branch.
  ///
  /// \returns The target virtual address for this thunk or branch.
  virtual uint64_t getTargetVirtualAddress() const = 0;

  /// Return the target section for this thunk or branch symbol.
  ///
  /// \returns The target section for this thunk or branch symbol.
  virtual uint32_t getTargetSection() const = 0;

  /// Return the texture slot for this HLSL resource.
  ///
  /// \returns The texture slot for this HLSL resource.
  virtual uint32_t getTextureSlot() const = 0;

  /// Return the timestamp associated with this symbol.
  ///
  /// \returns The timestamp associated with this symbol.
  virtual uint32_t getTimeStamp() const = 0;

  /// Return the metadata token associated with this managed symbol.
  ///
  /// \returns The metadata token associated with this managed symbol.
  virtual uint32_t getToken() const = 0;

  /// Return the symbol id of this symbol's type.
  ///
  /// \returns The symbol id of this symbol's type.
  virtual SymIndexId getTypeId() const = 0;

  /// Return the UAV slot for this HLSL resource.
  ///
  /// \returns The UAV slot for this HLSL resource.
  virtual uint32_t getUavSlot() const = 0;

  /// Return the undecorated (demangled) name of this symbol.
  ///
  /// \returns The undecorated (demangled) name of this symbol.
  virtual std::string getUndecoratedName() const = 0;

  /// Return the undecorated name using the given undecoration flags.
  ///
  /// \param Flags Flags controlling which undecoration options to apply.
  /// \returns The undecorated name string.
  virtual std::string getUndecoratedNameEx(PDB_UndnameFlags Flags) const = 0;

  /// Return the symbol id of this modified type's unmodified type.
  ///
  /// \returns The symbol id of this modified type's unmodified type.
  virtual SymIndexId getUnmodifiedTypeId() const = 0;

  /// Return the symbol id of this dimension's upper bound.
  ///
  /// \returns The symbol id of this dimension's upper bound.
  virtual SymIndexId getUpperBoundId() const = 0;

  /// Return the constant value associated with this symbol.
  ///
  /// \returns The constant value associated with this symbol.
  virtual Variant getValue() const = 0;

  /// Return the virtual base displacement index for this base class.
  ///
  /// \returns The virtual base displacement index for this base class.
  virtual uint32_t getVirtualBaseDispIndex() const = 0;

  /// Return the virtual base offset for this base class.
  ///
  /// \returns The virtual base offset for this base class.
  virtual uint32_t getVirtualBaseOffset() const = 0;

  /// Return the builtin type describing this virtual base table.
  ///
  /// \returns The virtual base table type, or null if none.
  virtual std::unique_ptr<PDBSymbolTypeBuiltin>
  getVirtualBaseTableType() const = 0;

  /// Return the symbol id of this UDT's virtual table shape.
  ///
  /// \returns The symbol id of this UDT's virtual table shape.
  virtual SymIndexId getVirtualTableShapeId() const = 0;

  /// Return the data kind of this data symbol.
  ///
  /// \returns The data kind of this data symbol.
  virtual PDB_DataKind getDataKind() const = 0;

  /// Return the symbol tag (kind) of this symbol.
  ///
  /// \returns The symbol tag (kind) of this symbol.
  virtual PDB_SymType getSymTag() const = 0;

  /// Return the GUID associated with this symbol.
  ///
  /// \returns The GUID associated with this symbol.
  virtual codeview::GUID getGuid() const = 0;

  /// Return the offset associated with this symbol.
  ///
  /// \returns The offset associated with this symbol.
  virtual int32_t getOffset() const = 0;

  /// Return the this-adjust value for this member function.
  ///
  /// \returns The this-adjust value for this member function.
  virtual int32_t getThisAdjust() const = 0;

  /// Return the virtual base pointer offset for this base class.
  ///
  /// \returns The virtual base pointer offset for this base class.
  virtual int32_t getVirtualBasePointerOffset() const = 0;

  /// Return the location type of this symbol.
  ///
  /// \returns The location type of this symbol.
  virtual PDB_LocType getLocationType() const = 0;

  /// Return the machine type associated with this symbol.
  ///
  /// \returns The machine type associated with this symbol.
  virtual PDB_Machine getMachineType() const = 0;

  /// Return the thunk ordinal for this thunk symbol.
  ///
  /// \returns The thunk ordinal for this thunk symbol.
  virtual codeview::ThunkOrdinal getThunkOrdinal() const = 0;

  /// Return the length in bytes of this symbol.
  ///
  /// \returns The length in bytes of this symbol.
  virtual uint64_t getLength() const = 0;

  /// Return the length in bytes of this symbol's live range.
  ///
  /// \returns The length in bytes of this symbol's live range.
  virtual uint64_t getLiveRangeLength() const = 0;

  /// Return the virtual address of this symbol.
  ///
  /// \returns The virtual address of this symbol.
  virtual uint64_t getVirtualAddress() const = 0;

  /// Return the UDT kind (class, struct, union, or interface).
  ///
  /// \returns The UDT kind (class, struct, union, or interface).
  virtual PDB_UdtType getUdtKind() const = 0;

  /// Return true if this UDT has a constructor.
  ///
  /// \returns True if this UDT has a constructor.
  virtual bool hasConstructor() const = 0;

  /// Return true if this function uses a custom calling convention.
  ///
  /// \returns True if this function uses a custom calling convention.
  virtual bool hasCustomCallingConvention() const = 0;

  /// Return true if this function uses a far return.
  ///
  /// \returns True if this function uses a far return.
  virtual bool hasFarReturn() const = 0;

  /// Return true if this symbol represents code.
  ///
  /// \returns True if this symbol represents code.
  virtual bool isCode() const = 0;

  /// Return true if this symbol was generated by the compiler.
  ///
  /// \returns True if this symbol was generated by the compiler.
  virtual bool isCompilerGenerated() const = 0;

  /// Return true if this type is const-qualified.
  ///
  /// \returns True if this type is const-qualified.
  virtual bool isConstType() const = 0;

  /// Return true if Edit and Continue is enabled for this symbol.
  ///
  /// \returns True if Edit and Continue is enabled for this symbol.
  virtual bool isEditAndContinueEnabled() const = 0;

  /// Return true if this symbol is a function.
  ///
  /// \returns True if this symbol is a function.
  virtual bool isFunction() const = 0;

  /// Return true if the address of this symbol has been taken.
  ///
  /// \returns True if the address of this symbol has been taken.
  virtual bool getAddressTaken() const = 0;

  /// Return true if this symbol has no stack ordering.
  ///
  /// \returns True if this symbol has no stack ordering.
  virtual bool getNoStackOrdering() const = 0;

  /// Return true if this function uses alloca.
  ///
  /// \returns True if this function uses alloca.
  virtual bool hasAlloca() const = 0;

  /// Return true if this UDT has an assignment operator.
  ///
  /// \returns True if this UDT has an assignment operator.
  virtual bool hasAssignmentOperator() const = 0;

  /// Return true if this symbol has C types.
  ///
  /// \returns True if this symbol has C types.
  virtual bool hasCTypes() const = 0;

  /// Return true if this UDT has a cast operator.
  ///
  /// \returns True if this UDT has a cast operator.
  virtual bool hasCastOperator() const = 0;

  /// Return true if this symbol has debug information.
  ///
  /// \returns True if this symbol has debug information.
  virtual bool hasDebugInfo() const = 0;

  /// Return true if this function uses C++ exception handling.
  ///
  /// \returns True if this function uses C++ exception handling.
  virtual bool hasEH() const = 0;

  /// Return true if this function uses asynchronous exception handling.
  ///
  /// \returns True if this function uses asynchronous exception handling.
  virtual bool hasEHa() const = 0;

  /// Return true if this function has a frame pointer.
  ///
  /// \returns True if this function has a frame pointer.
  virtual bool hasFramePointer() const = 0;

  /// Return true if this function contains inline assembly.
  ///
  /// \returns True if this function contains inline assembly.
  virtual bool hasInlAsm() const = 0;

  /// Return true if this function has the inline attribute.
  ///
  /// \returns True if this function has the inline attribute.
  virtual bool hasInlineAttribute() const = 0;

  /// Return true if this function uses an interrupt return.
  ///
  /// \returns True if this function uses an interrupt return.
  virtual bool hasInterruptReturn() const = 0;

  /// Return true if this function uses longjmp.
  ///
  /// \returns True if this function uses longjmp.
  virtual bool hasLongJump() const = 0;

  /// Return true if this symbol contains managed code.
  ///
  /// \returns True if this symbol contains managed code.
  virtual bool hasManagedCode() const = 0;

  /// Return true if this UDT has nested types.
  ///
  /// \returns True if this UDT has nested types.
  virtual bool hasNestedTypes() const = 0;

  /// Return true if this function has the noinline attribute.
  ///
  /// \returns True if this function has the noinline attribute.
  virtual bool hasNoInlineAttribute() const = 0;

  /// Return true if this function has the noreturn attribute.
  ///
  /// \returns True if this function has the noreturn attribute.
  virtual bool hasNoReturnAttribute() const = 0;

  /// Return true if this symbol has optimized-code debug info.
  ///
  /// \returns True if this symbol has optimized-code debug info.
  virtual bool hasOptimizedCodeDebugInfo() const = 0;

  /// Return true if this UDT has an overloaded operator.
  ///
  /// \returns True if this UDT has an overloaded operator.
  virtual bool hasOverloadedOperator() const = 0;

  /// Return true if this function uses structured exception handling.
  ///
  /// \returns True if this function uses structured exception handling.
  virtual bool hasSEH() const = 0;

  /// Return true if this function has security checks.
  ///
  /// \returns True if this function has security checks.
  virtual bool hasSecurityChecks() const = 0;

  /// Return true if this function uses setjmp.
  ///
  /// \returns True if this function uses setjmp.
  virtual bool hasSetJump() const = 0;

  /// Return true if this function has strict GS checks.
  ///
  /// \returns True if this function has strict GS checks.
  virtual bool hasStrictGSCheck() const = 0;

  /// Return true if this is an accelerator group shared local.
  ///
  /// \returns True if this is an accelerator group shared local.
  virtual bool isAcceleratorGroupSharedLocal() const = 0;

  /// Return true if this is an accelerator pointer-tag live range.
  ///
  /// \returns True if this is an accelerator pointer-tag live range.
  virtual bool isAcceleratorPointerTagLiveRange() const = 0;

  /// Return true if this is an accelerator stub function.
  ///
  /// \returns True if this is an accelerator stub function.
  virtual bool isAcceleratorStubFunction() const = 0;

  /// Return true if this symbol is aggregated.
  ///
  /// \returns True if this symbol is aggregated.
  virtual bool isAggregated() const = 0;

  /// Return true if this is an introducing virtual function.
  ///
  /// \returns True if this is an introducing virtual function.
  virtual bool isIntroVirtualFunction() const = 0;

  /// Return true if this symbol was converted from CIL.
  ///
  /// \returns True if this symbol was converted from CIL.
  virtual bool isCVTCIL() const = 0;

  /// Return true if this constructor initializes a virtual base.
  ///
  /// \returns True if this constructor initializes a virtual base.
  virtual bool isConstructorVirtualBase() const = 0;

  /// Return true if this function returns a C++ UDT.
  ///
  /// \returns True if this function returns a C++ UDT.
  virtual bool isCxxReturnUdt() const = 0;

  /// Return true if this data is aligned.
  ///
  /// \returns True if this data is aligned.
  virtual bool isDataAligned() const = 0;

  /// Return true if this is HLSL data.
  ///
  /// \returns True if this is HLSL data.
  virtual bool isHLSLData() const = 0;

  /// Return true if this code is hotpatchable.
  ///
  /// \returns True if this code is hotpatchable.
  virtual bool isHotpatchable() const = 0;

  /// Return true if this is an indirect virtual base class.
  ///
  /// \returns True if this is an indirect virtual base class.
  virtual bool isIndirectVirtualBaseClass() const = 0;

  /// Return true if this UDT is an interface.
  ///
  /// \returns True if this UDT is an interface.
  virtual bool isInterfaceUdt() const = 0;

  /// Return true if this is an intrinsic symbol.
  ///
  /// \returns True if this is an intrinsic symbol.
  virtual bool isIntrinsic() const = 0;

  /// Return true if this symbol was compiled with link-time code generation.
  ///
  /// \returns True if this symbol was compiled with link-time code generation.
  virtual bool isLTCG() const = 0;

  /// Return true if this symbol's location depends on control flow.
  ///
  /// \returns True if this symbol's location depends on control flow.
  virtual bool isLocationControlFlowDependent() const = 0;

  /// Return true if this is an MSIL netmodule.
  ///
  /// \returns True if this is an MSIL netmodule.
  virtual bool isMSILNetmodule() const = 0;

  /// Return true if this matrix type is stored in row-major order.
  ///
  /// \returns True if this matrix type is stored in row-major order.
  virtual bool isMatrixRowMajor() const = 0;

  /// Return true if this symbol is managed code.
  ///
  /// \returns True if this symbol is managed code.
  virtual bool isManagedCode() const = 0;

  /// Return true if this symbol is MSIL code.
  ///
  /// \returns True if this symbol is MSIL code.
  virtual bool isMSILCode() const = 0;

  /// Return true if this member pointer uses multiple inheritance.
  ///
  /// \returns True if this member pointer uses multiple inheritance.
  virtual bool isMultipleInheritance() const = 0;

  /// Return true if this function is declared naked.
  ///
  /// \returns True if this function is declared naked.
  virtual bool isNaked() const = 0;

  /// Return true if this symbol is nested within another type.
  ///
  /// \returns True if this symbol is nested within another type.
  virtual bool isNested() const = 0;

  /// Return true if this local was optimized away.
  ///
  /// \returns True if this local was optimized away.
  virtual bool isOptimizedAway() const = 0;

  /// Return true if this UDT is packed.
  ///
  /// \returns True if this UDT is packed.
  virtual bool isPacked() const = 0;

  /// Return true if this pointer is based on a symbol value.
  ///
  /// \returns True if this pointer is based on a symbol value.
  virtual bool isPointerBasedOnSymbolValue() const = 0;

  /// Return true if this is a pointer to a data member.
  ///
  /// \returns True if this is a pointer to a data member.
  virtual bool isPointerToDataMember() const = 0;

  /// Return true if this is a pointer to a member function.
  ///
  /// \returns True if this is a pointer to a member function.
  virtual bool isPointerToMemberFunction() const = 0;

  /// Return true if this function is pure virtual.
  ///
  /// \returns True if this function is pure virtual.
  virtual bool isPureVirtual() const = 0;

  /// Return true if this is an rvalue reference type.
  ///
  /// \returns True if this is an rvalue reference type.
  virtual bool isRValueReference() const = 0;

  /// Return true if this UDT is a ref UDT.
  ///
  /// \returns True if this UDT is a ref UDT.
  virtual bool isRefUdt() const = 0;

  /// Return true if this is a reference type.
  ///
  /// \returns True if this is a reference type.
  virtual bool isReference() const = 0;

  /// Return true if this is a restricted type.
  ///
  /// \returns True if this is a restricted type.
  virtual bool isRestrictedType() const = 0;

  /// Return true if this symbol is a return value.
  ///
  /// \returns True if this symbol is a return value.
  virtual bool isReturnValue() const = 0;

  /// Return true if this symbol uses safe buffers.
  ///
  /// \returns True if this symbol uses safe buffers.
  virtual bool isSafeBuffers() const = 0;

  /// Return true if this symbol is scoped.
  ///
  /// \returns True if this symbol is scoped.
  virtual bool isScoped() const = 0;

  /// Return true if this symbol was compiled with SDL checks.
  ///
  /// \returns True if this symbol was compiled with SDL checks.
  virtual bool isSdl() const = 0;

  /// Return true if this member pointer uses single inheritance.
  ///
  /// \returns True if this member pointer uses single inheritance.
  virtual bool isSingleInheritance() const = 0;

  /// Return true if this symbol is split (PGO cold/hot).
  ///
  /// \returns True if this symbol is split (PGO cold/hot).
  virtual bool isSplitted() const = 0;

  /// Return true if this symbol is static.
  ///
  /// \returns True if this symbol is static.
  virtual bool isStatic() const = 0;

  /// Return true if this module has private symbols.
  ///
  /// \returns True if this module has private symbols.
  virtual bool hasPrivateSymbols() const = 0;

  /// Return true if this type is unaligned.
  ///
  /// \returns True if this type is unaligned.
  virtual bool isUnalignedType() const = 0;

  /// Return true if this code is unreachable.
  ///
  /// \returns True if this code is unreachable.
  virtual bool isUnreached() const = 0;

  /// Return true if this UDT is a value UDT.
  ///
  /// \returns True if this UDT is a value UDT.
  virtual bool isValueUdt() const = 0;

  /// Return true if this function or base is virtual.
  ///
  /// \returns True if this function or base is virtual.
  virtual bool isVirtual() const = 0;

  /// Return true if this is a virtual base class.
  ///
  /// \returns True if this is a virtual base class.
  virtual bool isVirtualBaseClass() const = 0;

  /// Return true if this member pointer uses virtual inheritance.
  ///
  /// \returns True if this member pointer uses virtual inheritance.
  virtual bool isVirtualInheritance() const = 0;

  /// Return true if this type is volatile-qualified.
  ///
  /// \returns True if this type is volatile-qualified.
  virtual bool isVolatileType() const = 0;

  /// Return true if this function was inlined.
  ///
  /// \returns True if this function was inlined.
  virtual bool wasInlined() const = 0;

  /// Return the unused string property for this symbol.
  ///
  /// \returns The unused string property for this symbol.
  virtual std::string getUnused() const = 0;
};

// Expanded (instead of the macro) so MrDocs can attach docs to each using.
/// Bring bitmask enum bitwise NOT into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator~;
/// Bring bitmask enum bitwise OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|;
/// Bring bitmask enum bitwise AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&;
/// Bring bitmask enum bitwise XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^;
/// Bring bitmask enum left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<;
/// Bring bitmask enum right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>;
/// Bring bitmask enum in-place OR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator|=;
/// Bring bitmask enum in-place AND into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator&=;
/// Bring bitmask enum in-place XOR into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator^=;
/// Bring bitmask enum in-place left-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator<<=;
/// Bring bitmask enum in-place right-shift into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator>>=;
/// Bring bitmask enum logical-not into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::operator!;
/// Bring bitmask enum any-bits-set test into this namespace for ADL.
using ::llvm::BitmaskEnumDetail::any;

} // namespace pdb
} // namespace llvm

#endif
